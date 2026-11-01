# PHPVPN source code

PHPVPN is a self-hosted VPN that relays end-to-end encrypted IPv4 packets through a short-connection
HTTP/HTTPS server. This repository contains the complete source code: the native client for Windows,
Linux and macOS (service, command-line interface and tray application), the Android client
(development build, not distributed yet), the relay servers in PHP, Python and Node.js, the WordPress
plugin, the build, packaging, installation and verification scripts, the tests and the documentation.

This is a source distribution. It contains no compiled program, package, Wintun DLL, Gradle wrapper
JAR, certificate, private key or deployment configuration. The root [readme.txt](readme.txt) is not a description of
this source tree: it is the installation guide that the packager copies into a built binary
distribution.

## Public defaults

The servers and the client ship with public default values that are needed for a first start: the
default server secret `CHANGE_THIS_SERVER_SECRET`, the default access keys of the VPN addresses
`10.99.99.2` to `10.99.99.15` and the default communication contracts. They are public examples,
not credentials. Before any real deployment, set your own server secret and your own access keys in
the server configuration and in every client.

## Layout

| Path | Content |
|---|---|
| `client/` | C++20 client: `src/` core and platform code (Windows, POSIX, Linux, macOS, Android), `apps/` service, CLI, tray (Windows, Linux GTK, macOS AppKit) and the Android JNI library, `packaging/` Linux and macOS packaging and install templates, `tools/` build and packaging scripts (`tools/android/` builds the static OpenSSL and curl of the Android library), `tests/` unit, end-to-end, UI and Android tests, `third_party/wintun/` Wintun headers and license |
| `client/android/` | the Android application: Gradle project with the `app` module (Kotlin, Jetpack Compose) and the `bridge` module (JNI binding of the native core) |
| `server/php/` | `phpvpn.php`, the standalone PHP relay, and its tests |
| `server/python/` | `phpvpn.py`, the standalone Python relay, its locked dependencies, launch scripts and tests |
| `server/node.js/` | `phpvpn.js`, the standalone Node.js relay, and its tests |
| `server/wordpress/` | the VPN Server WordPress plugin (`version7/vpn-server`) and its guide |
| `tests-integration/` | protocol tests shared by the server implementations |
| `tools/` | distribution build and verification, version bump, WordPress plugin build, configuration helpers |
| `assets/` | icons, images, the status page mockup and the Android screen design (`android-en/`) |
| `documentation/` | the English protocol copy that the binary distribution ships, with the hash of its source |
| `.github/workflows/` | the Linux check, the macOS build (the only place where the macOS client is compiled) and the Android build with its unit tests |

## Documentation

- [protocol.md](protocol.md): the wire protocol and the server behaviour.
- [client.md](client.md): the client, its installation, configuration, IPC and platform integration.
- [tray_UX.md](tray_UX.md): the tray application and its windows, and the Android user interface.
- [documentation/android-client.md](documentation/android-client.md): installation and use of the Android development build; [client/tools/android/README.md](client/tools/android/README.md): the native Android dependencies and the development APKs; [assets/android-en/README.md](assets/android-en/README.md): the Android screen design.
- [distribution.md](distribution.md): how the binary distribution is built and verified.
- [create_source_code_distribution.md](create_source_code_distribution.md): how a source distribution like this one is made.
- Server guides: [PHP](server/php/README.md), [Python](server/python/README.md), [Node.js](server/node.js/README.md), [WordPress](server/wordpress/README.md) and the [plugin developer notes](server/wordpress/version7/README.md).
- Tests: [integration tests](tests-integration/README.md), [end-to-end tests](client/tests/e2e/README.md), [UI tests](client/tests/ui/README.md), [Android tests](client/tests/android/README.md).

## Machine specific configuration

Paths of tools, test servers, the Linux build host and the signing certificate are kept in one file,
[phpvpn.config.json](phpvpn.config.json), read by `tools/phpvpn_config.py` and
`tools/phpvpn-config.ps1`. In this distribution the file is a template: every value that belongs to
one computer (tool paths, the signing certificate thumbprint, the Linux build host, the Android test
phone and relay, the Hyper-V VM) is empty, the keys and the relative test server paths are kept.
Fill in what you use before you build or test, or point the environment variable `PHPVPN_CONFIG`
to your own copy. An empty tool path means a lookup in `PATH`; relative paths resolve against the
repository root.

## Prerequisites

| Part | Tools |
|---|---|
| Windows client | Windows 10 or 11 x64, Visual Studio 2022 or later with the C++ x64 build tools and a Windows SDK, CMake 3.28 or later, Ninja, Windows PowerShell 5.1, Internet access for Wintun |
| Linux packages | a Linux host with podman (or docker) and Internet access; the packages are built in clean `fedora:44` and `ubuntu:24.04` containers |
| Linux client from source | GCC or Clang with C++20, CMake 3.28+, Ninja, pkg-config, OpenSSL 3 and libcurl 8.4+ development files, libsystemd; the tray additionally needs GTK 3 and Ayatana AppIndicator |
| macOS client | a GitHub repository with Actions: `.github/workflows/client-macos.yml` builds and tests the arm64 package on a `macos-15` runner |
| Android client | JDK 17, Android SDK with platform `android-37.2`, build-tools 36.0.0, NDK 28.2.13676358 and CMake 3.31.6 (as `.github/workflows/client-android.yml` installs them), Gradle 9.8.2 for the wrapper JAR, and the prebuilt static OpenSSL 3.5.9 and curl 8.23.0 of `client/tools/android/build-deps.sh` (built on Linux) |
| PHP server | 64-bit PHP 8.2 or later, no extensions beyond the default build |
| Python server | Python 3.13 managed by uv; `server/python/init.bat` or `init.sh` prepares both |
| Node.js server | Node.js 24 LTS, no npm packages |
| WordPress plugin | WordPress 6.6+ on 64-bit PHP 8.2+ |

## Wintun

The Windows client uses the signed Wintun 0.14.1 driver library, which is not stored here. Download
and verify it once:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File client/tools/get-wintun.ps1
```

The script downloads `https://www.wintun.net/builds/wintun-0.14.1.zip`, checks the archive SHA-256
`07C256185D6EE3652E09FA55C0B673E2624B565E02C4B9091C79CA7D2F24EF51`, extracts the x64, arm64 and x86
`wintun.dll` into `client/third_party/wintun/<arch>/` and checks the WireGuard LLC Authenticode
signature of each. Keep the original file name. The license is in
[client/third_party/wintun/LICENSE.txt](client/third_party/wintun/LICENSE.txt).

Run it before you create the commit described under [Binary distribution](#binary-distribution), so
that the three DLLs are part of that commit, as they are in the original project repository. The
distribution build refuses any uncommitted or untracked file under `client/`.

## Gradle wrapper

`client/android/gradle/wrapper/gradle-wrapper.jar` is a compiled JAR and is not stored here, only
`gradle-wrapper.properties` (Gradle 9.8.2). Recreate the JAR once with an installed Gradle 9.8.2,
from `client/android`, after `local.properties` exists there (the project is configured during the
command, so the Android SDK location must be known):

```sh
gradle wrapper --gradle-version 9.8.2
```

The command rewrites `gradlew`, `gradlew.bat` and the properties file with the same content and adds
the JAR. The original project tracks the JAR with SHA-256
`3b8a25775a69158b5ad2b1d17a80a88dc7b40a352a73eb27d06c612a7ce68e98`; the official checksum of the 9.8.2
wrapper JAR is published at `https://services.gradle.org/distributions/gradle-9.8.2-wrapper.jar.sha256`.

## Build and test

Windows client (from the repository root; the script finds MSVC through vswhere and builds into
`work/build/<preset>`). Keep the repository in a folder whose full path contains only ASCII
characters: the Windows SDK resource compiler (`rc.exe`) truncates non-ASCII paths and the version
resources then fail to compile. Spaces in the path are fine.

```powershell
./client/tools/build-x64.ps1 -Preset release -Configure -Test
```

Linux client from source (from `client/`):

```sh
cmake --preset linux-release
cmake --build --preset linux-release
ctest --preset linux-release
```

Linux packages (on the Linux build host, from `client/`, in a Git checkout without uncommitted changes):

```sh
sh tools/linux/build-packages-in-container.sh fedora:44 ../work/linux-packages/<id>
sh tools/linux/build-packages-in-container.sh ubuntu:24.04 ../work/linux-packages/<id>
```

Android client (from `client/android`; `local.properties` with `sdk.dir` and `phpvpn.androidDeps`,
the folder written by `client/tools/android/build-deps.sh`, see
[client/tools/android/README.md](client/tools/android/README.md); `JAVA_HOME` must point to a JDK 17):

```sh
./gradlew :bridge:testDebugUnitTest :app:testDebugUnitTest :app:assembleDebug
```

`client/tools/android/make-android-dev.ps1` builds and checks the development APKs. The Android
client is not part of the binary distribution; [documentation/android-client.md](documentation/android-client.md)
describes the development build and its provisional application identifier.

Servers:

```sh
php server/php/tests/tests.php
node --test "server/node.js/tests/*.test.js"
uv run --no-project --python 3.13 --with pytest --with requests --with psutil --with-requirements server/python/requirements-server.txt python -B -m pytest server/python/tests -q
```

WordPress plugin: `python tools/build-wordpress-plugin.py --check` verifies that the generated
server core matches `server/php/phpvpn.php`, `--zip <folder>` writes the installable plugin ZIP.

Run a server locally: `php -S 127.0.0.1:8081 -t server/php` and open `http://127.0.0.1:8081/phpvpn.php`;
`node server/node.js/phpvpn.js` (port 8080); on Windows `server/python/init.bat` and `start.bat`
(port 8000). Each server guide describes the configuration.

## Signing identity

The packagers sign the release manifests of the Windows, Linux and macOS folders and pin the signing
key in the installers. No certificate or key is supplied. Create your own identity, for a local test
for example:

```powershell
New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=phpvpn local test' -KeyAlgorithm RSA -KeyLength 3072 -CertStoreLocation Cert:\CurrentUser\My
```

and put its `Thumbprint` into `signing.certificateThumbprint` of `phpvpn.config.json`. Such a
certificate is not a trusted publisher signature; use your own publisher certificate for a release.

## Binary distribution

[distribution.md](distribution.md) is the binding procedure, `tools/make-distribution.ps1` the entry
point. Before you build a complete binary distribution, put these sources into your own Git
repository, run `client/tools/get-wintun.ps1`, adapt `phpvpn.config.json` and create a commit. It can be a new repository with its own history; the original
history is not needed and not included. The Linux packages, the macOS package and the binary
distribution must come from the same commit, without uncommitted changes in the distributed
sources: `tools/make-distribution.ps1` compares the `HEAD` commit with the provenance of the Linux
and macOS packages and refuses packages built from another commit. Do not bypass this check with
`-AllowLinuxProvenanceMismatch` or `-AllowMacosProvenanceMismatch` for a release.

In short: `./tools/bump-version.ps1 -Commit`, build the Linux packages on the build host, let the
`client-macos` workflow build the macOS package from the same pushed commit and download its
artifact `phpvpn-macos-arm64`, then run

```powershell
./client/tools/build-x64.ps1 -Preset release -Configure -ResourceTests
./tools/make-distribution.ps1 -BuildDirectory ./work/build/release -LinuxPackagesDirectory ./work/linux-packages/<id> -MacosPackagesDirectory ./work/macos-packages/<id> -SignBinaries
```

## License

The project is released under the [MIT License](LICENSE). Third-party components keep their own
licenses, in particular Wintun ([client/third_party/wintun/LICENSE.txt](client/third_party/wintun/LICENSE.txt)).
