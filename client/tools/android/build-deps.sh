#!/usr/bin/env bash
# Builds the static OpenSSL and libcurl that libphpvpn-android.so links
# (plan-android-v001, step 1, WP01).
#
#   build-deps.sh --ndk <ndk dir> --out <dir> [--jobs N] [--work <dir>]
#                 [--downloads <dir>] [--force]
#
# Versions are pinned below together with the SHA-256 of the release tarballs
# (taken from openssl.org and the curl GitHub release, both cross-checked with
# the detached GPG signatures on 2026-10-31). The CI cache key is the hash of
# this file, so changing a version, a hash or a flag here rebuilds the cache.
#
#   OpenSSL 3.5.9 (3.5 LTS)  Configure android-arm64 / android-x86_64,
#       -D__ANDROID_API__=34, -fPIC, no-shared no-module no-engine no-legacy
#       no-tests no-apps no-docs no-autoload-config. The default provider keeps
#       everything crypto_openssl.cpp and TLS need (AES-GCM, SHA-256, HMAC,
#       HKDF, TLS 1.2/1.3); nothing is loaded at run time (no modules, no
#       engines, no openssl.cnf, compiled-in directories that never exist).
#   curl 8.23.0  CMake build, static libcurl only, OpenSSL backend, HTTP and
#       HTTPS only, threaded resolver, IPv6. No zlib, brotli, zstd, libpsl,
#       libidn2, nghttp2, ngtcp2, libssh(2), GSSAPI, c-ares, LDAP or other
#       protocols, no curl executable, no tests, no docs. No CA bundle or CA
#       path is compiled in: the Android layer installs the system CA store
#       per handle through CURLOPT_SSL_CTX_FUNCTION (WP03).
#   NDK 28.2.13676358 (r28c), API level 34, ABIs arm64-v8a and x86_64 (R4).
#   The archives are PIC and stripped of debug info (symbols kept); a smoke
#   link of all three into one shared object checks PIC, undefined symbols,
#   NEEDED entries and 16 KB LOAD alignment before anything is published.
#
# Output contract (read by client/src/CMakeLists.txt, PHPVPN_ANDROID_DEPS):
#
#   <out>/<abi>/include/{openssl,curl}/...
#   <out>/<abi>/lib/{libcrypto.a,libssl.a,libcurl.a}
#   <out>/<abi>/lib/cmake/CURL/CURLConfig.cmake   (find_package(CURL) config)
#   <out>/manifest.json   versions, tarball hashes, NDK, API level, build date
#
# The output is relocatable (no absolute build paths in the CMake files), so a
# tree built on Linux can be copied to Windows and used by Gradle there.
# Re-running with an unchanged script and a complete <out> does nothing;
# --force rebuilds. Linux only: bash, CMake >= 3.28, ninja or make, perl,
# tar, xz, sha256sum and curl or wget.
set -Eeuo pipefail

readonly OPENSSL_VERSION=3.5.9
readonly OPENSSL_TARBALL=openssl-${OPENSSL_VERSION}.tar.gz
readonly OPENSSL_SHA256=603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a
readonly OPENSSL_URLS=(
    "https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VERSION}/${OPENSSL_TARBALL}"
    "https://www.openssl.org/source/${OPENSSL_TARBALL}"
    "https://www.openssl.org/source/old/3.5/${OPENSSL_TARBALL}"
)
readonly CURL_VERSION=8.23.0
readonly CURL_TARBALL=curl-${CURL_VERSION}.tar.xz
readonly CURL_SHA256=8dadd7d2536a314f982ad05a651b713f426bf7d02cc2c76cffb9796f2218e6c5
readonly CURL_URLS=(
    "https://curl.se/download/${CURL_TARBALL}"
    "https://github.com/curl/curl/releases/download/curl-${CURL_VERSION//./_}/${CURL_TARBALL}"
)
readonly NDK_REVISION=28.2.13676358
readonly API_LEVEL=34
readonly ABIS=(arm64-v8a x86_64)
# Fixed build timestamp so OpenSSL's "built on" string does not change the
# archives between otherwise identical builds (2026-10-31T00:00:00Z).
readonly BUILD_EPOCH=1793404800
# OpenSSL compiles its install directories in (OPENSSLDIR, MODULESDIR,
# ENGINESDIR). They point to a path that never exists on a device, so no
# openssl.cnf, CA file, module or engine is ever read from disk, and no build
# path ends up in libcrypto.a. The files are installed with DESTDIR.
readonly OPENSSL_RUNTIME_PREFIX=/nonexistent/phpvpn-openssl

readonly OPENSSL_OPTIONS=(
    no-shared no-module no-engine no-legacy no-tests no-apps no-docs
    no-autoload-config
)
readonly CURL_OPTIONS=(
    -DBUILD_SHARED_LIBS=OFF
    -DBUILD_STATIC_LIBS=ON
    -DBUILD_CURL_EXE=OFF
    -DBUILD_TESTING=OFF
    -DBUILD_EXAMPLES=OFF
    -DBUILD_LIBCURL_DOCS=OFF
    -DBUILD_MISC_DOCS=OFF
    -DENABLE_CURL_MANUAL=OFF
    -DCURL_ENABLE_EXPORT_TARGET=ON
    -DCURL_USE_PKGCONFIG=OFF
    -DCURL_USE_OPENSSL=ON
    -DCURL_DISABLE_OPENSSL_AUTO_LOAD_CONFIG=ON
    -DCURL_CA_BUNDLE=none
    -DCURL_CA_PATH=none
    -DCURL_CA_FALLBACK=OFF
    -DCURL_CA_NATIVE=OFF
    -DENABLE_THREADED_RESOLVER=ON
    -DENABLE_ARES=OFF
    -DENABLE_IPV6=ON
    -DCURL_ZLIB=OFF
    -DCURL_BROTLI=OFF
    -DCURL_ZSTD=OFF
    -DCURL_USE_LIBPSL=OFF
    -DUSE_LIBIDN2=OFF
    -DUSE_NGHTTP2=OFF
    -DUSE_NGTCP2=OFF
    -DUSE_QUICHE=OFF
    -DUSE_ECH=OFF
    -DUSE_HTTPSRR=OFF
    -DCURL_USE_LIBSSH2=OFF
    -DCURL_USE_LIBSSH=OFF
    -DCURL_USE_GSSAPI=OFF
    -DCURL_USE_GSASL=OFF
    -DCURL_USE_LIBUV=OFF
    -DCURL_USE_LIBBACKTRACE=OFF
    -DCURL_DISABLE_DICT=ON
    -DCURL_DISABLE_FILE=ON
    -DCURL_DISABLE_FTP=ON
    -DCURL_DISABLE_GOPHER=ON
    -DCURL_DISABLE_IMAP=ON
    -DCURL_DISABLE_IPFS=ON
    -DCURL_DISABLE_LDAP=ON
    -DCURL_DISABLE_LDAPS=ON
    -DCURL_DISABLE_MQTT=ON
    -DCURL_DISABLE_POP3=ON
    -DCURL_DISABLE_RTSP=ON
    -DCURL_DISABLE_SMTP=ON
    -DCURL_DISABLE_TELNET=ON
    -DCURL_DISABLE_TFTP=ON
    -DCURL_DISABLE_WEBSOCKETS=ON
)

die() { echo "build-deps.sh: $*" >&2; exit 1; }
log() { echo "==> $*"; }

usage() {
    sed -n '5,6p' "$0" | sed 's/^# \{0,3\}//' >&2
    exit 2
}

ndk="" out="" jobs="" work="" downloads="" force=0
while [ $# -gt 0 ]; do
    case $1 in
        --ndk) ndk=${2:?--ndk needs a directory}; shift 2 ;;
        --out) out=${2:?--out needs a directory}; shift 2 ;;
        --jobs) jobs=${2:?--jobs needs a number}; shift 2 ;;
        --work) work=${2:?--work needs a directory}; shift 2 ;;
        --downloads) downloads=${2:?--downloads needs a directory}; shift 2 ;;
        --force) force=1; shift ;;
        -h|--help) usage ;;
        *) echo "build-deps.sh: unknown argument '$1'" >&2; usage ;;
    esac
done
[ -n "$ndk" ] && [ -n "$out" ] || usage
[ -z "$jobs" ] && jobs=$(nproc 2>/dev/null || echo 4)
case $jobs in ''|*[!0-9]*|0) die "--jobs must be a positive number" ;; esac

# ---- Preconditions -------------------------------------------------------

[ "$(uname -s)" = Linux ] || die "runs on Linux only (CI runner, Fedora VM or WSL)"
for tool in cmake perl make tar xz sha256sum; do
    command -v "$tool" >/dev/null || die "missing tool: $tool"
done
if command -v curl >/dev/null; then
    fetch() { curl -fsSL --retry 3 -o "$1" "$2"; }
elif command -v wget >/dev/null; then
    fetch() { wget -q -O "$1" "$2"; }
else
    die "missing tool: curl or wget"
fi
cmake_version=$(cmake --version | sed -n '1s/^cmake version //p')
printf '%s\n3.28\n' "$cmake_version" | sort -V -C -r \
    || die "CMake >= 3.28 required, found $cmake_version"
if command -v ninja >/dev/null; then generator=Ninja; else generator="Unix Makefiles"; fi

ndk=$(cd "$ndk" && pwd) || die "--ndk: no such directory"
[ -f "$ndk/source.properties" ] || die "$ndk is not an NDK (no source.properties)"
ndk_revision=$(sed -n 's/^Pkg\.Revision *= *//p' "$ndk/source.properties" | tr -d '\r')
[ "$ndk_revision" = "$NDK_REVISION" ] \
    || die "NDK $NDK_REVISION required, $ndk has '${ndk_revision:-no source.properties}'"
ndk_release=$(sed -n 's/^Pkg\.ReleaseName *= *//p' "$ndk/source.properties" | tr -d '\r')
toolbin=$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin
[ -x "$toolbin/clang" ] || die "$ndk has no Linux host toolchain ($toolbin/clang)"

mkdir -p "$out"
out=$(cd "$out" && pwd)
script_sha256=$(sha256sum "$0" | cut -d' ' -f1)

if [ "$force" = 0 ] && [ -f "$out/manifest.json" ] \
    && grep -q "\"recipe_sha256\": \"$script_sha256\"" "$out/manifest.json"; then
    complete=1
    for abi in "${ABIS[@]}"; do
        for lib in libcrypto.a libssl.a libcurl.a cmake/CURL/CURLConfig.cmake; do
            [ -f "$out/$abi/lib/$lib" ] || complete=0
        done
    done
    if [ "$complete" = 1 ]; then
        log "$out is up to date (recipe $script_sha256); --force rebuilds"
        exit 0
    fi
fi

if [ -z "$work" ]; then
    work=$(mktemp -d "${TMPDIR:-/tmp}/phpvpn-android-deps.XXXXXX")
    trap 'rm -rf "$work"' EXIT
else
    mkdir -p "$work"
    work=$(cd "$work" && pwd)
fi
[ -n "$downloads" ] || downloads=$work/downloads
mkdir -p "$downloads"
downloads=$(cd "$downloads" && pwd)

# The manifest marks a complete output; remove it before touching anything.
rm -f "$out/manifest.json"

export SOURCE_DATE_EPOCH=$BUILD_EPOCH
# Keep the temporary build paths out of __FILE__ strings in libcurl.a.
prefix_map="-ffile-prefix-map=$work/="

# ---- Sources -------------------------------------------------------------

download() { # <tarball> <sha256> <url>...
    local name=$1 sha=$2 file=$downloads/$1
    shift 2
    if [ -f "$file" ] && echo "$sha  $file" | sha256sum -c --status -; then
        log "$name: cached, SHA-256 verified"
        return
    fi
    rm -f "$file"
    local url
    for url in "$@"; do
        log "$name: downloading $url"
        if fetch "$file.part" "$url"; then
            if echo "$sha  $file.part" | sha256sum -c --status -; then
                mv "$file.part" "$file"
                log "$name: SHA-256 verified"
                return
            fi
            echo "build-deps.sh: $name from $url does not match SHA-256 $sha" >&2
        fi
        rm -f "$file.part"
    done
    die "$name: no source with the pinned SHA-256"
}

download "$OPENSSL_TARBALL" "$OPENSSL_SHA256" "${OPENSSL_URLS[@]}"
download "$CURL_TARBALL" "$CURL_SHA256" "${CURL_URLS[@]}"

rm -rf "$work/src"
mkdir -p "$work/src"
tar -xJf "$downloads/$CURL_TARBALL" -C "$work/src"
curl_src=$work/src/curl-$CURL_VERSION
[ -f "$curl_src/CMakeLists.txt" ] || die "unexpected curl tarball layout"

# ---- Build ---------------------------------------------------------------

build_openssl() { # <abi> <prefix>
    local abi=$1 prefix=$2 target bld=$work/build/$1
    local src=$bld/openssl-$OPENSSL_VERSION destdir=$bld/openssl-destdir
    case $abi in
        arm64-v8a) target=android-arm64 ;;
        x86_64) target=android-x86_64 ;;
        *) die "unsupported ABI $abi" ;;
    esac
    # In-tree build: __FILE__ in OpenSSL error records stays relative.
    rm -rf "$src" "$destdir" && mkdir -p "$bld"
    tar -xzf "$downloads/$OPENSSL_TARBALL" -C "$bld"
    [ -f "$src/Configure" ] || die "unexpected OpenSSL tarball layout"
    log "OpenSSL $OPENSSL_VERSION for $abi ($target, API $API_LEVEL)"
    (
        cd "$src"
        export ANDROID_NDK_ROOT=$ndk PATH=$toolbin:$PATH
        ./Configure "$target" "-D__ANDROID_API__=$API_LEVEL" \
            --prefix="$OPENSSL_RUNTIME_PREFIX" --openssldir="$OPENSSL_RUNTIME_PREFIX/ssl" \
            --libdir=lib "${OPENSSL_OPTIONS[@]}" -fPIC
        make -j"$jobs" build_libs
        make install_dev DESTDIR="$destdir"
    ) > "$work/openssl-$abi.log" 2>&1 \
        || { tail -n 40 "$work/openssl-$abi.log" >&2; die "OpenSSL build for $abi failed (log $work/openssl-$abi.log)"; }
    mkdir -p "$prefix"
    cp -a "$destdir$OPENSSL_RUNTIME_PREFIX/." "$prefix/"
}

build_curl() { # <abi> <prefix>
    local abi=$1 prefix=$2 bld=$work/build/$1/curl
    rm -rf "$bld"
    log "curl $CURL_VERSION for $abi"
    (
        cmake -S "$curl_src" -B "$bld" -G "$generator" \
            -DCMAKE_TOOLCHAIN_FILE="$ndk/build/cmake/android.toolchain.cmake" \
            -DANDROID_ABI="$abi" \
            -DANDROID_PLATFORM="android-$API_LEVEL" \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_INSTALL_PREFIX="$prefix" \
            -DCMAKE_INSTALL_LIBDIR=lib \
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
            -DCMAKE_C_FLAGS="$prefix_map" \
            -DCMAKE_FIND_ROOT_PATH="$prefix" \
            -DOPENSSL_ROOT_DIR="$prefix" \
            -DOPENSSL_USE_STATIC_LIBS=ON \
            "${CURL_OPTIONS[@]}"
        cmake --build "$bld" --parallel "$jobs"
        cmake --install "$bld"
    ) > "$work/curl-$abi.log" 2>&1 \
        || { tail -n 40 "$work/curl-$abi.log" >&2; die "curl build for $abi failed (log $work/curl-$abi.log)"; }
}

# Fails unless the archives link into one shared object with no undefined
# symbol and no text relocation (proves PIC and the OpenSSL <-> curl
# dependency), loads only system libraries and uses 16 KB LOAD alignment.
check_abi() { # <abi> <prefix>
    local abi=$1 prefix=$2 triple lib so=$work/build/$1/smoke.so
    case $abi in
        arm64-v8a) triple=aarch64-linux-android$API_LEVEL ;;
        x86_64) triple=x86_64-linux-android$API_LEVEL ;;
    esac
    for lib in libcrypto.a libssl.a libcurl.a cmake/CURL/CURLConfig.cmake; do
        [ -f "$prefix/lib/$lib" ] || die "$abi: missing lib/$lib"
    done
    grep -q "OPENSSL_VERSION_STR \"$OPENSSL_VERSION\"" "$prefix/include/openssl/opensslv.h" \
        || die "$abi: installed OpenSSL headers are not $OPENSSL_VERSION"
    grep -q "LIBCURL_VERSION \"$CURL_VERSION\"" "$prefix/include/curl/curlver.h" \
        || die "$abi: installed curl headers are not $CURL_VERSION"
    if grep -rlF "$work" "$prefix/lib/cmake" >/dev/null; then
        die "$abi: CMake package files contain build paths, the output would not be relocatable"
    fi
    for lib in libcrypto.a libssl.a libcurl.a; do
        if grep -aqF -e "$work" -e "$ndk" "$prefix/lib/$lib"; then
            die "$abi: $lib contains build or NDK paths"
        fi
    done
    "$toolbin/clang" --target="$triple" -shared -o "$so" \
        -Wl,--whole-archive "$prefix/lib/libcurl.a" "$prefix/lib/libssl.a" \
        "$prefix/lib/libcrypto.a" -Wl,--no-whole-archive \
        -Wl,--no-undefined -Wl,-z,text \
        || die "$abi: the archives do not link into a PIC shared object"
    local needed
    needed=$("$toolbin/llvm-readelf" -d "$so" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p' | sort | tr '\n' ' ')
    for lib in $needed; do
        case $lib in
            libc.so|libm.so|libdl.so) ;;
            *) die "$abi: unexpected NEEDED $lib" ;;
        esac
    done
    "$toolbin/llvm-readelf" -lW "$so" | awk '$1 == "LOAD" && $NF != "0x4000" { bad = 1 } END { exit bad }' \
        || die "$abi: LOAD segments are not 16 KB aligned"
    log "$abi: smoke link ok, NEEDED: $needed"
}

stage=$work/stage
rm -rf "$stage"
for abi in "${ABIS[@]}"; do
    prefix=$stage/$abi
    build_openssl "$abi" "$prefix"
    build_curl "$abi" "$prefix"
    # Only include/ and lib/ belong to the contract; pkg-config files and
    # curl-config carry absolute build paths, docs are not needed.
    rm -rf "$prefix/bin" "$prefix/share" "$prefix/lib/pkgconfig"
    # CURL_LIBRARIES_PRIVATE (informational, unused by the imported targets)
    # lists the OpenSSL archives by absolute path; make it prefix relative.
    sed -i "s|$prefix/|\${PACKAGE_PREFIX_DIR}/|g" "$prefix/lib/cmake/CURL/CURLConfig.cmake"
    # The NDK toolchain file always adds -g. Debug info of the dependencies
    # would triple the archives and embed NDK paths; symbols stay.
    "$toolbin/llvm-strip" --strip-debug "$prefix/lib/libcrypto.a" "$prefix/lib/libssl.a" "$prefix/lib/libcurl.a"
    check_abi "$abi" "$prefix"
done

# ---- Publish -------------------------------------------------------------

for abi in "${ABIS[@]}"; do
    rm -rf "$out/$abi"
    cp -a "$stage/$abi" "$out/$abi"
done

json_list() { # quoted, comma separated JSON strings
    local first=1 item
    for item in "$@"; do
        [ "$first" = 1 ] || printf ', '
        first=0
        printf '"%s"' "${item//\"/\\\"}"
    done
}

cat > "$out/manifest.json.part" <<EOF
{
  "schema": 1,
  "generator": "client/tools/android/build-deps.sh",
  "recipe_sha256": "$script_sha256",
  "build_date": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "build_host": "$(uname -sm), CMake $cmake_version, $generator",
  "ndk": { "revision": "$ndk_revision", "release": "$ndk_release" },
  "api_level": $API_LEVEL,
  "abis": [$(json_list "${ABIS[@]}")],
  "components": [
    {
      "name": "openssl",
      "version": "$OPENSSL_VERSION",
      "tarball": "$OPENSSL_TARBALL",
      "sha256": "$OPENSSL_SHA256",
      "options": [$(json_list "${OPENSSL_OPTIONS[@]}" -fPIC)]
    },
    {
      "name": "curl",
      "version": "$CURL_VERSION",
      "tarball": "$CURL_TARBALL",
      "sha256": "$CURL_SHA256",
      "options": [$(json_list "${CURL_OPTIONS[@]}")]
    }
  ],
  "libraries": ["libcurl.a", "libssl.a", "libcrypto.a"]
}
EOF
mv "$out/manifest.json.part" "$out/manifest.json"
log "done: $out (OpenSSL $OPENSSL_VERSION, curl $CURL_VERSION, NDK $ndk_revision, API $API_LEVEL)"
