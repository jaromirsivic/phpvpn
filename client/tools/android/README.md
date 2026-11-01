# Android native dependencies

`build-deps.sh` builds the static OpenSSL 3.5 LTS and libcurl 8.x that
`libphpvpn-android.so` links (plan-android-v001, step 1). Versions, tarball
SHA-256 and every build flag are pinned in the script header; the CI cache key
is the hash of the script.

| | |
|---|---|
| OpenSSL | 3.5.9, `no-shared no-module no-engine no-legacy no-tests no-apps no-docs no-autoload-config`, `-fPIC` |
| curl | 8.23.0, static, OpenSSL backend, HTTP and HTTPS only, threaded resolver, IPv6, no compression, IDN, PSL, HTTP/2 or SSH, no CA bundle or path |
| NDK | 28.2.13676358 (r28c), API 34, ABIs `arm64-v8a` and `x86_64` |

## Output

```
<out>/manifest.json                 versions, tarball hashes, NDK, API, build date
<out>/<abi>/include/{openssl,curl}
<out>/<abi>/lib/{libcrypto.a,libssl.a,libcurl.a}
<out>/<abi>/lib/cmake/CURL/         find_package(CURL) config, relocatable
```

`client/src/CMakeLists.txt` reads it through `PHPVPN_ANDROID_DEPS=<out>`. The
tree contains no absolute paths, so it can be built on Linux and copied to
Windows. Build paths and the build time stay out of the archives, so two runs
of the same script with the same NDK produce identical archives.

## Local build (Linux, Fedora VM or WSL)

Needs bash, CMake ≥ 3.28, ninja or make, perl, tar, xz, sha256sum and curl or
wget, plus the **Linux** NDK r28c (the script uses its `linux-x86_64` host
toolchain).

```sh
client/tools/android/build-deps.sh --ndk ~/android-ndk/28.2.13676358 \
    --out ~/android-deps/openssl-3.5.9-curl-8.23.0 [--jobs N]
```

Optional: `--downloads <dir>` keeps the verified tarballs between runs,
`--work <dir>` keeps the build trees and logs (default: a temporary directory
removed on exit), `--force` rebuilds an output that is already up to date.

Copy the output to `work/android-deps/<tag>` on Windows and point Gradle at it
in `client/android/local.properties`:

```properties
phpvpn.androidDeps=D\:/git/phpvpn/work/android-deps/openssl-3.5.9-curl-8.23.0
```

## CI

The `deps` job of `.github/workflows/client-android.yml` runs
`build-deps.sh --ndk "$ANDROID_HOME/ndk/$NDK_VERSION" --out work/android-deps`
only on a cache miss and publishes the tree as the `android-deps` artifact; the
`app` job passes it to Gradle through `PHPVPN_ANDROID_DEPS`.

## Updating a version

Take the tarball SHA-256 from the release page (openssl.org source page or the
GitHub release, curl GitHub release) and check it against the detached
signature (`.asc`) before changing `*_VERSION` and `*_SHA256` in the script.

## Development APKs (WP12)

`make-android-dev.ps1` (Windows PowerShell 5.1 or PowerShell 7) runs
`gradlew assembleDebug assembleRelease` with `-Pphpvpn.abiSplits=true`, then
`:app:lintDebug`, and writes `work/android/<version>`:

```
phpvpn-<version>-debug.apk               universal, debug signed
phpvpn-<version>-<abi>-debug.apk         arm64-v8a, x86_64, debug signed
phpvpn-<version>-release-unsigned.apk    universal, unsigned (R2: no release key yet)
symbols/<abi>/libphpvpn-android.so       non-stripped release library
SHA256SUMS
manifest.json                            versions, commit, toolchain, deps, checks
```

It fails (exit 1) when a packaging check fails: signatures, 16 KB zip and
LOAD alignment, badging (SDK levels, versionCode, ABIs, permissions),
exported components, exported symbols, stripping and lint errors. JAVA_HOME
defaults to `D:/androidstudio/jbr` when it exists (`-JavaHome` overrides it),
the SDK comes from `ANDROID_HOME` or `local.properties`. The `app` job of the
workflow runs the same script with `pwsh` and publishes the folder as the
`phpvpn-android-debug` artifact.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File client\tools\android\make-android-dev.ps1
```
