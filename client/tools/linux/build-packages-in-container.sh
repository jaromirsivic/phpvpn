#!/bin/sh
# Builds the Linux packages in a clean distribution container (plan-v001
# section 4.13): the RPM on Fedora, the DEB on Ubuntu 24.04.
#
#   sh tools/linux/build-packages-in-container.sh fedora:<N>|ubuntu:24.04 <output-directory>
#       [--source-commit <sha>] [--source-dirty true|false]
#
# The client/ tree is mounted read-only into the container. The container
# installs the build dependencies, builds as an unprivileged user with the
# package tests (package-rpm.sh or package-deb.sh), runs rpmlint or lintian and
# writes the packages plus a provenance fragment. <output-directory> (by
# convention work/linux-packages/<id>) collects the builds of one source
# snapshot, typically one Fedora and one Ubuntu build:
#
#   <package files>           phpvpn-<v>-<r>.x86_64.rpm, phpvpn_<v>-<r>_amd64.deb
#   SHA256SUMS                sha256sum format over all packages
#   build-provenance.json     source commit and dirty flag, and per build the
#                             distribution, image, compiler and library versions
#   provenance/<distro>.json  the per build fragments the file above joins
#   logs/lint-<distro>.log    rpmlint or lintian output
#
# The source commit and the dirty flag come from git when the tree is a
# checkout. A copy without .git (for example the synchronized VM directory)
# must pass them with --source-commit/--source-dirty or PHPVPN_SOURCE_COMMIT and
# PHPVPN_SOURCE_DIRTY. PHPVPN_PACKAGE_RELEASE sets the package release (default
# 1). PHPVPN_CONTAINER_ENGINE selects podman (default when present) or docker.
# PHPVPN_PACKAGE_LINT=0 skips the lint step.
set -eu

json_string() {
    # Values are tool and distribution identifiers; quotes, backslashes and
    # control characters are dropped rather than escaped.
    printf '"%s"' "$(printf '%s' "$1" | tr -d '\000-\037"\\')"
}

# ------------------------------------------------------------ container side
if [ "${1:-}" = "--inside" ]; then
    . /etc/os-release
    distro=$ID-$VERSION_ID
    release=${PHPVPN_PACKAGE_RELEASE:-1}
    case $ID in
        fedora)
            dnf -y --setopt=install_weak_deps=False install rpm-build rpmlint util-linux glibc-langpack-en 'dnf5-command(builddep)'
            dnf -y --setopt=install_weak_deps=False builddep /src/packaging/linux/rpm/phpvpn.spec
            ;;
        ubuntu)
            export DEBIAN_FRONTEND=noninteractive
            apt-get update
            apt-get install -y --no-install-recommends dpkg-dev debhelper fakeroot lintian util-linux
            mkdir -p /tmp/phpvpn-build-deps
            cp -R /src/packaging/linux/debian /tmp/phpvpn-build-deps/debian
            (cd /tmp/phpvpn-build-deps && apt-get build-dep -y --no-install-recommends ./)
            ;;
        *) echo "unsupported build distribution $ID" >&2; exit 2 ;;
    esac

    useradd -m phpvpn-builder
    builder_home=/home/phpvpn-builder
    cp -R /src "$builder_home/src"
    mkdir -p "$builder_home/out"
    chown -R phpvpn-builder: "$builder_home/src" "$builder_home/out"
    as_builder() { runuser -u phpvpn-builder -- env HOME="$builder_home" LC_ALL=C.UTF-8 PHPVPN_PACKAGE_RELEASE="$release" "$@"; }
    if [ "$ID" = fedora ]; then
        as_builder sh "$builder_home/src/tools/linux/package-rpm.sh" "$builder_home/out"
    else
        as_builder sh "$builder_home/src/tools/linux/package-deb.sh" "$builder_home/out"
    fi

    lint=skipped
    mkdir -p /out/packages /out/logs
    if [ "${PHPVPN_PACKAGE_LINT:-1}" != 0 ]; then
        lint=passed
        if [ "$ID" = fedora ]; then
            as_builder rpmlint "$builder_home/src/packaging/linux/rpm/phpvpn.spec" "$builder_home"/out/*.rpm > /out/logs/lint-$distro.log 2>&1 || lint=failed
        else
            as_builder lintian --info --display-info --pedantic "$builder_home"/out/*.deb \
                > /out/logs/lint-$distro.log 2>&1 || lint=failed
        fi
    fi

    if [ "$ID" = fedora ]; then
        q() { rpm -q --qf '%{VERSION}-%{RELEASE}' "$1"; }
        compiler=$(g++ --version | head -n 1)
        libraries="\"openssl\":$(json_string "$(q openssl-libs)"),\"libcurl\":$(json_string "$(q libcurl)"),\"libsystemd\":$(json_string "$(q systemd-libs)"),\"glibc\":$(json_string "$(q glibc)"),\"libstdc++\":$(json_string "$(q libstdc++)")"
        packager="rpm-build $(q rpm-build)"
    else
        q() { dpkg-query -W -f='${Version}' "$1"; }
        compiler=$(g++ --version | head -n 1)
        libraries="\"openssl\":$(json_string "$(q libssl-dev)"),\"libcurl\":$(json_string "$(q libcurl4-openssl-dev)"),\"libsystemd\":$(json_string "$(q libsystemd-dev)"),\"glibc\":$(json_string "$(q libc6-dev)"),\"libstdc++\":$(json_string "$(q libstdc++-13-dev 2> /dev/null || q libstdc++6)")"
        packager="debhelper $(q debhelper)"
    fi

    packages=
    for package in "$builder_home"/out/*; do
        name=${package##*/}
        cp "$package" "/out/packages/$name"
        entry="{\"file\":$(json_string "$name"),\"bytes\":$(wc -c < "$package" | tr -d ' '),\"sha256\":\"$(sha256sum "$package" | cut -d ' ' -f 1)\"}"
        packages=${packages:+$packages,}$entry
    done
    {
        printf '{"distribution":{"id":%s,"versionId":%s,"prettyName":%s},' \
            "$(json_string "$ID")" "$(json_string "$VERSION_ID")" "$(json_string "${PRETTY_NAME:-}")"
        printf '"image":%s,"imageId":%s,"architecture":%s,' \
            "$(json_string "${PHPVPN_IMAGE:-}")" "$(json_string "${PHPVPN_IMAGE_ID:-}")" "$(json_string "$(uname -m)")"
        printf '"compiler":%s,"cmake":%s,"packager":%s,"libraries":{%s},' \
            "$(json_string "$compiler")" "$(json_string "$(cmake --version | head -n 1)")" \
            "$(json_string "$packager")" "$libraries"
        printf '"packageRelease":%s,"lint":%s,"builtAt":%s,' \
            "$(json_string "$release")" "$(json_string "$lint")" "$(json_string "$(date -u +%Y-%m-%dT%H:%M:%SZ)")"
        printf '"sourceCommit":%s,"sourceDirty":%s,"packages":[%s]}\n' \
            "$(json_string "$PHPVPN_SOURCE_COMMIT")" "$PHPVPN_SOURCE_DIRTY" "$packages"
    } > "/out/$distro.json"
    [ "$lint" != failed ] || { echo "lint reported errors, see logs/lint-$distro.log" >&2; exit 3; }
    exit 0
fi

# ----------------------------------------------------------------- host side
image=
output=
commit=${PHPVPN_SOURCE_COMMIT:-}
dirty=${PHPVPN_SOURCE_DIRTY:-}
while [ $# -gt 0 ]; do
    case $1 in
        --source-commit) commit=${2:?}; shift 2 ;;
        --source-dirty) dirty=${2:?}; shift 2 ;;
        -*) echo "build-packages-in-container.sh: unknown option $1" >&2; exit 2 ;;
        *)
            if [ -z "$image" ]; then image=$1
            elif [ -z "$output" ]; then output=$1
            else echo "build-packages-in-container.sh: unexpected argument $1" >&2; exit 2
            fi
            shift ;;
    esac
done
if [ -z "$image" ] || [ -z "$output" ]; then
    echo "usage: build-packages-in-container.sh fedora:<N>|ubuntu:24.04 <output-directory> [--source-commit <sha>] [--source-dirty true|false]" >&2
    exit 2
fi

client=$(cd "$(dirname "$0")/../.." && pwd)
release=${PHPVPN_PACKAGE_RELEASE:-1}
case $release in
    ''|*[!0-9]*) echo "build-packages-in-container.sh: PHPVPN_PACKAGE_RELEASE must be a positive integer" >&2; exit 2 ;;
esac
case $image in
    fedora:[0-9]*) reference=registry.fedoraproject.org/$image; kind=rpm ;;
    ubuntu:24.04) reference=docker.io/library/$image; kind=deb ;;
    *) echo "build-packages-in-container.sh: image must be fedora:<N> or ubuntu:24.04" >&2; exit 2 ;;
esac

if git -C "$client" rev-parse --git-dir > /dev/null 2>&1; then
    git_commit=$(git -C "$client" rev-parse HEAD)
    if [ -n "$(git -C "$client" status --porcelain)" ]; then git_dirty=true; else git_dirty=false; fi
    if [ -n "$commit" ] && [ "$commit" != "$git_commit" ]; then
        echo "build-packages-in-container.sh: --source-commit $commit differs from the checkout $git_commit" >&2
        exit 2
    fi
    commit=$git_commit
    dirty=${dirty:-$git_dirty}
fi
if ! printf '%s' "$commit" | grep -Eq '^[0-9a-f]{40}$'; then
    echo "build-packages-in-container.sh: the tree has no .git; pass the full source commit with --source-commit or PHPVPN_SOURCE_COMMIT" >&2
    exit 2
fi
case $dirty in
    true|false) ;;
    *) echo "build-packages-in-container.sh: pass --source-dirty true|false or PHPVPN_SOURCE_DIRTY" >&2; exit 2 ;;
esac

engine=${PHPVPN_CONTAINER_ENGINE:-}
if [ -z "$engine" ]; then
    if command -v podman > /dev/null 2>&1; then engine=podman; else engine=docker; fi
fi

mkdir -p "$output/provenance" "$output/logs"
output=$(cd "$output" && pwd)
for fragment in "$output"/provenance/*.json; do
    [ -f "$fragment" ] || continue
    if ! grep -q "\"sourceCommit\":\"$commit\",\"sourceDirty\":$dirty," "$fragment" ||
       ! grep -q "\"packageRelease\":\"$release\"," "$fragment"; then
        echo "build-packages-in-container.sh: $output already holds a build of another source snapshot or release" >&2
        exit 1
    fi
done
for existing in "$output"/*."$kind"; do
    if [ -f "$existing" ]; then
        echo "build-packages-in-container.sh: $output already holds $(basename "$existing")" >&2
        exit 1
    fi
done

"$engine" pull -q "$reference" > /dev/null
image_id=$("$engine" image inspect --format '{{.Id}}' "$reference")
scratch=$(mktemp -d "${TMPDIR:-/tmp}/phpvpn-packages.XXXXXX")
trap 'rm -rf "$scratch"' EXIT INT TERM

# The source gets the shared SELinux label (z), so parallel builds of the same
# tree keep their access; the private output directory gets a private one (Z).
status=0
"$engine" run --rm \
    -v "$client:/src:ro,z" \
    -v "$scratch:/out:Z" \
    -e PHPVPN_PACKAGE_RELEASE="$release" \
    -e PHPVPN_PACKAGE_LINT="${PHPVPN_PACKAGE_LINT:-1}" \
    -e PHPVPN_SOURCE_COMMIT="$commit" \
    -e PHPVPN_SOURCE_DIRTY="$dirty" \
    -e PHPVPN_IMAGE="$reference" \
    -e PHPVPN_IMAGE_ID="$image_id" \
    "$reference" sh /src/tools/linux/build-packages-in-container.sh --inside || status=$?

if [ -d "$scratch/logs" ]; then cp "$scratch"/logs/* "$output/logs/" 2> /dev/null || true; fi
if [ "$status" != 0 ]; then
    echo "build-packages-in-container.sh: the $image build failed ($status)" >&2
    exit "$status"
fi
cp "$scratch"/packages/* "$output/"
for fragment in "$scratch"/*.json; do cp "$fragment" "$output/provenance/"; done

# Join every build of this output directory.
version=$(sed -n '/^project(phpvpn-client/,/)/s/^ *VERSION \([0-9][0-9.]*\).*/\1/p' "$client/CMakeLists.txt")
(
    cd "$output"
    set --
    for package in *.rpm *.deb; do [ -f "$package" ] && set -- "$@" "$package"; done
    sha256sum "$@" > SHA256SUMS
)
{
    printf '{"formatVersion":1,"product":"phpvpn","version":%s,"packageRelease":%s,' \
        "$(json_string "$version")" "$(json_string "$release")"
    printf '"sourceCommit":%s,"sourceDirty":%s,"builds":[' "$(json_string "$commit")" "$dirty"
    separator=
    for fragment in "$output"/provenance/*.json; do
        printf '%s' "$separator"
        tr -d '\n' < "$fragment"
        separator=,
    done
    printf ']}\n'
} > "$output/build-provenance.json"

echo "packages of $image in $output:"
cat "$output/SHA256SUMS"
