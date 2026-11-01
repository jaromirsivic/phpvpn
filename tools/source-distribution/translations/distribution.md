# PHP-VPN Distribution Methodology

## Purpose and binding force

This document is the authoritative and binding methodology for building the PHP-VPN distribution. It defines when the build takes place, the sources, the directory structure, the content and the verification of the result. The keywords **MUST**, **MUST NOT**, **SHOULD**, **SHOULD NOT** and **MAY** have the same normative meaning as in [protocol.md](protocol.md).

The rules for creating the distribution tree are governed by this document. [protocol.md](protocol.md) remains the authoritative source for the wire protocol and server behavior. [client.md](client.md) remains the authoritative source for client behavior, installation, configuration, platform integration and security requirements. This methodology neither changes nor weakens those rules. Older descriptions of a flat `dist` folder and the default behavior of the tools MUST NOT replace the structure defined here.

## When to build the distribution

The distribution is typically built after implementation changes intended for release have been completed, or on the user's explicit instruction. The build MUST be based on the current state of the sources intended for handover, including intended changes that have not yet been committed. Creating a commit or pushing is not a precondition of the build, and the build itself performs neither.

On every build, the content of the project directory `dist` MUST be recreated from the current sources. Merely copying new files over the old distribution is not sufficient, because it would leave removed or renamed components behind. The resulting tree MUST NOT contain unplanned files from an older build. An explicit exception to the requirement that the documentation be current is the preserved English translation of the protocol according to the Documentation section. Preserved older packages, backups and working materials belong outside `dist`.

### Version

Every new distribution MUST have a new PHP VPN version. Before the build, `./tools/bump-version.ps1` is always run; it increments the last number of the version (`X.Y.Z` to `X.Y.Z+1`). In a single run it rewrites all version locations according to `Get-VersionLocations` in `tools/distribution-common.ps1`:
- the project `VERSION` in `client/CMakeLists.txt`, from which the tray, the service, the CLI, the Windows version resource and the Linux packages take it,
- the `Version:` of the RPM spec and a new `X.Y.Z-1` entry in its `%changelog` and in `debian/changelog`,
- the `.TH` headers of the manual pages (version and date),
- the header on the second line of `server/php/phpvpn.php`, `server/python/phpvpn.py` and `server/node.js/phpvpn.js` in the form `PHP VPN X.Y.Z (MIT License, Copyright (c) www.pilandrome.com)`,
- `SERVER_VERSION` of all three servers, which the server reports in health and info responses,
- the WordPress plugin version in `server/wordpress/version7/vpn-server` (the header and `VERSION` in `vpn-server.php`, `Stable tag` in `readme.txt` and the generated `includes/server-core.php`).

If the locations do not have the same starting version, the script changes nothing. Because the Linux packages carry the version and are built from a commit, the version change MUST be committed before the Linux build (`-Commit` creates a `Version X.Y.Z` commit from the rewritten files only). The exception for uncommitted changes from the section When to build the distribution does not apply to the version change. `tools/make-distribution.ps1` refuses the build if any version location differs from the client version, and refuses publication if the version is not higher than the `releaseVersion` of the currently published `dist`. With `-StagingOnly` it only prints this second mismatch as a warning.

Before the content is renewed, it MUST be verified that the target really is the `dist` directory under the root of the selected project. The build MAY prepare the new tree in a separate working directory and then replace the content of `dist` with it. An incomplete or unverified result MUST NOT be labeled a finished distribution.

## Mandatory structure

Paths are relative to the project root. The letter case of the platform folders and the architecture designation `x64` are binding. The main folders `client`, `server` and `documentation` and the server folders `php`, `python`, `node.js` and `wordpress` MUST have lowercase names. Older variants with uppercase letters (`PHP`, `Python`, `Node.js`) MUST NOT be used.

```text
dist/
  documentation/
    readme.txt
    protocol.md                 # optional complete English translation
  server/
    php/
      phpvpn.php
      README.md
    python/
      phpvpn.py
      requirements-server.txt
      README.md
      init.bat
      start.bat
      stop.bat
      init.sh
      start.sh
      stop.sh
      bootstrap.ps1
      server-control.py
    node.js/
      phpvpn.js
      README.md
    wordpress/
      vpn-server-<version>.zip
      README.md
  client/
    Windows/
      x64/
        install.bat
        install.ps1
        uninstall.bat
        uninstall.ps1
        install-phpvpn.exe
        uninstall-phpvpn.exe
        phpvpn-service.exe
        phpvpn-tray.exe
        phpvpn.exe
        wintun.dll
        release-manifest.json
        release-manifest.sig
        release-signing.cer
        LICENSE-wintun.txt
    macOS/
      arm64/                    # with a package from CI (macOS section)
        install.sh
        uninstall.sh
        README.md
        phpvpn-<version>-arm64.pkg
        release-manifest.json
        release-manifest.sig
        release-signing.cer
      README.md                 # instead of arm64/, only without a package from CI (placeholder)
    Linux/
      x64/
        install.sh
        uninstall.sh
        README.md
        phpvpn-<version>-<release>.x86_64.rpm
        phpvpn-tray-<version>-<release>.x86_64.rpm
        phpvpn_<version>-<release>_amd64.deb
        phpvpn-tray_<version>-<release>_amd64.deb
        release-manifest.json
        release-manifest.sig
        release-signing.cer
```

`<version>` is the client version from `client/CMakeLists.txt`, `<release>` is the common release number of all four Linux packages.

The `client/macOS` folder always contains exactly one of two variants: either the `arm64` subfolder with the complete macOS client, or only the placeholder `README.md`. A mixed or incomplete state is a build error.

The listed Windows, Linux and macOS files form the basis of the current client distribution. The list MUST be supplemented with all further scripts, programs, libraries, distributable configurations, templates and supporting files that the current implementation needs for installation and operation. Test binaries, build caches, working logs and the content of runtime queues do not belong in the distribution.

### Client archives for download

On every distribution build, a separate `dist-downloads` folder MUST also be created next to `dist`. It is not a subfolder of `dist`. The content intended for publication under `https://www.pilandrome.com/downloads/` has this structure:

```text
dist-downloads/
  phpvpn/
    client/
      1.x/
        phpvpn-win.zip
        phpvpn-macos.zip
        phpvpn-linux.zip
```

`phpvpn-win.zip` MUST contain the complete content of the verified folder `dist/client/Windows`, `phpvpn-linux.zip` the complete content of the verified folder `dist/client/Linux` and `phpvpn-macos.zip` the complete content of the verified folder `dist/client/macOS`. The content of the platform folder is placed directly in the root of its ZIP, without the parent `dist/client` tree, so the top level of the archive consists of the architecture folders (`x64/`, for macOS `arm64/`). When the distribution gains another architecture (for example `arm64/` for Windows and Linux), it is added to the archive next to the existing one, and neither the name nor the structure of the archive changes. The archives preserve relative paths, signatures, manifests and, for the Linux and macOS shell scripts, the POSIX executable attribute. They MUST come from the same build as `dist`, and their names remain stable for links from the server page.

**Placeholder for macOS without a package from CI:** the macOS package is not produced on the Windows host but in GitHub Actions (macOS section). If the distribution build receives no package, `dist/client/macOS` contains only an English `README.md` that clearly states that the client is not available, and `phpvpn-macos.zip` then contains only this `README.md` directly in its root. Such an archive MUST NOT be presented as a functional installer. The build entry point reports this state with a warning.

Before publication, the content of all three ZIPs MUST be verified against the corresponding platform folders. The archives are created and verified (file list, SHA-256 and POSIX modes) by [tools/make-download-archives.ps1](tools/make-download-archives.ps1). The overall entry point calls it on the verified staging before publication, so an archive error leaves both `dist` and `dist-downloads` unchanged. After successful publication and verification of `dist`, it replaces the previous content of `dist-downloads`. With `-StagingOnly` the archives remain in `work/distribution-<id>/dist-downloads`. Running it on its own only recreates the archives of the current `dist`. Publishing the content of `dist-downloads` to the web is a separate step.

### Documentation

The distribution MUST have three main folders `client`, `server` and `documentation`.
The root [readme.txt](readme.txt), next to `protocol.md` and `client.md`, is the maintained source of the brief English installation guide. On EVERY build it MUST be copied unchanged to `dist/documentation/readme.txt`. The README is mandatory even if the protocol translation is missing. It covers the client and server parts, the actual procedures for Windows, Linux and macOS, and the deployment of the PHP, Node.js and Python servers and of the WordPress plugin. Each of the four server folders MUST contain its own English `README.md`, whose source is the `README.md` in the repository folder of the same name `server/<implementation>`. The separate guides `server-node.md` and `server-python.md` have been replaced by the README and no longer belong in the distribution.

The Czech [protocol.md](protocol.md) remains the authoritative source and MUST NOT be overwritten by the translation. The complete English translation is maintained outside the distribution as `documentation/protocol.en.md`. It MAY be updated according to the entire current Czech document only at the user's explicit request to update the documentation when creating the distribution. A summary, or a translation of the changed parts without reviewing the whole document, is not sufficient. The translation preserves the normative meaning, identifiers, formulas, examples and all chapters. Next to it, `documentation/protocol.en.source.sha256` is kept with the SHA-256 of the Czech source used for the last translation. This record does not belong in the distribution, and it is not required to match a future Czech source in the absence of a request for an update.

Without such a request, a new translation MUST NOT be created or automatically updated. The build script only copies the existing `documentation/protocol.en.md` to `dist/documentation/protocol.md`. If the preserved source is not available, it omits the translation. An already existing English protocol from a previous distribution may first be preserved at this source path unchanged, without any claim that it is current. The `-OmitProtocol` switch allows the translation to be omitted even when the source exists. The distribution check allows its absence, but a translation that is present MUST match the preserved English source byte for byte. The README is always verified against the current root source. A new translation for the current build is prepared before the build script is run. Relative links to development documentation, for example `client.md`, require files from the source repository and are not available in a standalone distribution. This does not make that documentation a mandatory part of the distribution.

### Server

The source location of each server implementation is the repository folder of the same name: [server/php](server/php), [server/python](server/python), [server/node.js](server/node.js) and [server/wordpress](server/wordpress). During the build, only the distributed selection of files according to `Get-DistributionSources` in `tools/distribution-common.ps1` is copied from it unchanged to `dist/server/<implementation>`. Implementation tests (`server/<implementation>/tests`) and development files do not belong in the distribution.

`dist/server/php` MUST contain the most current distributable version of the PHP server and its English guide `README.md`. The source is [server/php/phpvpn.php](server/php/phpvpn.php), which is copied as `phpvpn.php` with its name and content unchanged.

`dist/server/python` MUST contain the current source `phpvpn.py`, the locked runtime dependencies `requirements-server.txt`, the English operating guide `README.md`, the six entry points `init.bat`, `start.bat`, `stop.bat`, `init.sh`, `start.sh`, `stop.sh` and their helpers `bootstrap.ps1`, `server-control.py`. Everything is copied unchanged from [server/python](server/python). The Python server can be run on its own according to this guide and implements the same client messages as PHP. The default disk mode and the optional memory mode require a single application process and worker. The distribution MUST NOT contain `.venv`, `.tools`, `.runtime`, `.bootstrap.lock`, the uv cache, bytecode, logs, test configuration, runtime queues or local secrets. The source `requirements-server.in` is used to regenerate the lock in the development repository; deployment uses the bundled `.txt`.

`dist/server/node.js` MUST contain the current source [phpvpn.js](server/node.js/phpvpn.js) and the English operating guide [README.md](server/node.js/README.md), both copied unchanged. The Node.js server is a single file without npm dependencies, build step or assets, and implements the same client messages as PHP. The distribution MUST NOT contain `node_modules`, configuration JSON with secrets, logs or the `phpvpn_data` runtime queue. A server started directly from `dist` creates the default `phpvpn_data` next to the script. During publication, this local state, like `.venv` and `.runtime` of the Python part, is not moved, published or deleted.

`dist/server/wordpress` MUST contain the installable ZIP of the VPN Server WordPress plugin `vpn-server-<version>.zip` and the English guide [README.md](server/wordpress/README.md). `<version>` is the plugin version from the header of `vpn-server.php`. The ZIP contains exactly all files of the folder [server/wordpress/version7/vpn-server](server/wordpress/version7/vpn-server), byte for byte unchanged, each under the path `vpn-server/`. The plugin's server core `includes/server-core.php` is generated from `server/php/phpvpn.php` by the tool `tools/build-wordpress-plugin.py`. The distribution build MUST reject a stale core (`--check`).

Publication into an existing `dist` renames a folder whose name has changed only in letter case (for example `server/Python` from an older distribution to `server/python`), because Windows preserves the original letter case of an existing folder. The local runtime state of the Python and Node.js parts is moved together with the folder. If a running server holds the folder, publication prints a warning and the folder must be renamed manually after the server is stopped.

The name of the distribution file `phpvpn.php` corresponds to the `/phpvpn.php` examples in the protocol documentation. The client's `serverUrl` must point to the actual URL of this file. Support for a different script name and path is governed by [client.md](client.md). The distribution MUST NOT take over an older deployed copy or a local runtime configuration instead of the current source intended for release.

### Windows x64

The graphical entry points `install-phpvpn.exe` and `uninstall-phpvpn.exe` run the corresponding PowerShell script from the same folder and require administrator rights through their own UAC manifest. They display live output, amber progress and a green or red result according to the exit code. The window stays open after completion. Normal closing is blocked during the operation. A UAC refusal happens before the GUI starts, so the window cannot show the red state for it.

Run both launchers from the extracted distribution outside `%ProgramFiles%\phpvpn`. Running a copy from the installed generation can lock its own EXE during an upgrade or uninstallation, and the operation may then end with an error. The launcher performs no self-deletion and does not work around this lock. The standard graphical uninstallation does not pass `-RemoveData`, so configuration and logs are preserved. The original BAT and PowerShell entry points, including `-VerifyOnly`, remain available.

`dist/client/Windows/x64` MUST be a complete client distribution for Windows x64 that can be handed over on its own. The binary files MUST be built from the current client sources, and all architecture-dependent components MUST correspond to x64. `install.bat` is the installation entry point; `install.ps1` and all further components it calls MUST be present and reachable through correct relative paths inside the package. The same applies to the uninstallation scripts.

Installation and upgrade MUST preserve local read-only access according to `client.md`. The Users Read & Execute permission MUST be inherited by the entire data tree, including backups, internal files and logs. On startup, the updated service repairs the ACL of the existing tree without losing the configuration. Manual verification includes opening the folder and copying the configuration without UAC, the prohibition of direct writes, and subsequent saving through Edit Config and the Wizard.

Completeness includes the necessary configuration materials but does not change the rules for creating `config.json` according to [client.md](client.md). If the default configuration is part of the program and no external template is needed, a separate runtime `config.json` is not added to the package. The local user configuration and private secrets are not copied into the general distribution.

The requirements for the original, unrenamed `wintun.dll`, signatures, architecture, licenses, manifest, installation and the prohibition of shipping separate Wintun `.sys`, `.cat` and `.inf` files are set by [client.md](client.md). The manifest and its signature MUST correspond to the final content of the `x64` folder. The installation procedure MUST NOT download or accept an unverified replacement of any component of the package.

### Linux x64

`dist/client/Linux/x64` MUST be a complete client distribution for Fedora and Ubuntu on x86_64 that can be handed over on its own. It contains the packages `phpvpn` (service and CLI, without graphical dependencies) and `phpvpn-tray` (tray application) in RPM format, built on Fedora 44, and in DEB format, built on Ubuntu 24.04. It further contains the installation entry points `install.sh` and `uninstall.sh`, the guide `README.md` and a signed manifest. The tray icons are PHPVPN's own artwork under the project license; no third-party icon license is bundled. The behavior of the packages, the installation, the service and the tray is set by [client.md](client.md) and the bundled `README.md`.

The Linux packages MUST NOT be built on a Windows host or from a source state different from that of the Windows client. They are built on a Linux build host (podman or docker) with the tool `client/tools/linux/build-packages-in-container.sh` in clean `fedora:44` and `ubuntu:24.04` containers from the same commit from which the Windows client is built. Both runs write into a single output folder `work/linux-packages/<id>` with the packages, `SHA256SUMS` and `build-provenance.json` (commit, `sourceDirty` flag, distribution, image, compiler and library versions). This folder is transferred unchanged into `work/linux-packages/<id>` of the checkout on the Windows host and passed to the overall entry point with the `-LinuxPackagesDirectory` parameter.

Before building the Windows client, the overall entry point verifies the `SHA256SUMS` of all files, the presence of exactly one package of each of the four kinds, and the provenance. `sourceCommit` MUST match the `HEAD` of the checkout, `sourceDirty` MUST be `false`, and the checkout MUST NOT have uncommitted changes under `client/`, because the packages cannot contain them. The Linux part of a release therefore requires a commit; the exception for uncommitted changes from the section When to build the distribution does not apply to it. On a mismatch, the build is refused. The `-AllowLinuxProvenanceMismatch` switch is intended exclusively for development: it prints the mismatch as a warning and records it in `work/distribution-<id>/linux-provenance-mismatch.txt`, and the signed manifest carries the actual `sourceCommit` and `sourceDirty`. Such a result MUST NOT be handed over as a release.

The folder is assembled by `client/tools/linux/make-linux-dist.ps1`. The manifest `release-manifest.json` has the same canonical form, and the signature `release-manifest.sig` the same mechanism and the same certificate `release-signing.cer`, as the Windows package. In addition, it states `platform`, `packageRelease`, the minimum distribution versions, `sourceCommit` and `sourceDirty`. It covers only the content of `client/Linux/x64`. `install.sh` pins the SHA-256 of the certificate's public key, so a change of the signing certificate requires a simultaneous change of the pin in `install.sh`. `install.sh` and `uninstall.sh` are POSIX shell scripts with LF and without a BOM, and they carry the POSIX executable attribute in the ZIP. The guide also gives the invocation `sh install.sh`, which does not depend on the attribute. The installation procedure MUST NOT download or accept an unverified replacement of any component of the folder; the package dependencies come from the distribution's repositories.

### macOS

The `dist/client/macOS` directory MUST be created on every build. Its content MUST correspond to the actually available implementation and to the possibilities of verifying it.

`dist/client/macOS/arm64` is a complete client distribution for macOS 14 and later on Apple Silicon that can be handed over on its own. It contains a single installation package `phpvpn-<version>-arm64.pkg` (the `phpvpn-service` service under launchd, the `phpvpn` CLI, the service's launchd plist, the menu bar application `phpvpn Tray.app` and its LaunchAgent), the installation entry points `install.sh` and `uninstall.sh`, the English guide `README.md` and a signed manifest. The behavior of the package, the installation, the service and the tray is set by [client.md](client.md) and the bundled `README.md`. The configuration is `/Library/Application Support/phpvpn/config.json`, the state `/private/var/db/phpvpn`, the log `/private/var/log/phpvpn`. The package does not ship these directories; they are created by the package's installation script or by the service.

The programs carry only an ad hoc code signature (`codesign -s -`) with the hardened runtime; the `.pkg` is neither signed with an Apple Developer ID nor notarized. This is a development signature, just like the development certificate of the Windows package, and it MUST NOT be presented as a trusted publisher signature. The supported installation is `sudo sh install.sh` from Terminal, which Gatekeeper does not block. Gatekeeper blocks double-clicking a downloaded `.pkg`, which moreover bypasses manifest verification; `README.md` states both consequences.

The macOS package MUST NOT be built on a Windows host. It is built by the [client-macos](.github/workflows/client-macos.yml) workflow on the GitHub Actions runner `macos-15` (Apple Silicon) with the tool `client/tools/macos/package-pkg.sh` from the same commit from which the Windows client is built. The output is the artifact `phpvpn-macos-arm64` with the package, `SHA256SUMS`, `build-provenance.json`, unsigned copies of `README.md`, `install.sh` (with the pin not filled in) and `uninstall.sh`, the test logs and a `screenshots/` folder with screenshots of the status item and the tray windows from the installation test. `build-provenance.json` states `sourceCommit`, `sourceDirty` (uncommitted changes under `client/`), the macOS runner version (`builtOn`, `buildVersion`), Xcode, SDK, compiler, CMake, the versions of OpenSSL (statically linked Homebrew `openssl@3`) and of the system libcurl, the GitHub Actions run details, `trayIncluded`, the minimum macOS version and the SHA-256 of the package.

The artifact is downloaded manually from a completed workflow run (GitHub, the Actions tab, or `gh run download <run-id> -n phpvpn-macos-arm64 -D work/macos-packages/<id>`) and extracted unchanged into `work/macos-packages/<id>` of the Windows checkout. The overall entry point takes it over with the `-MacosPackagesDirectory` parameter; without the parameter it uses `work/macos-packages/latest` if it exists. Without a package, `dist/client/macOS` remains a placeholder with a single `README.md` that explicitly states that the client is not available. The presence of the folder by itself is not a claim that the platform is supported or tested.

Before building the Windows client, the overall entry point verifies `SHA256SUMS`, exactly one package `phpvpn-<version>-arm64.pkg` of the client version with a xar header, and the provenance (platform, architecture, minimum macOS 14.0, hash match). As with Linux, `sourceCommit` MUST match the `HEAD` of the checkout, `sourceDirty` MUST be `false`, and the checkout MUST NOT have uncommitted changes under `client/`. The `-AllowMacosProvenanceMismatch` switch is intended exclusively for development: it prints the mismatch as a warning and records it in `work/distribution-<id>/macos-provenance-mismatch.txt`. Such a result MUST NOT be handed over as a release.

The folder is assembled by `client/tools/macos/make-macos-dist.ps1`. It copies the package from the artifact, and `install.sh`, `uninstall.sh` and `README.md` from the sources in `client/packaging/macos/dist` of the checkout (the copies in the artifact are not used). The manifest `release-manifest.json` has the same canonical form, and the signature `release-manifest.sig` the same mechanism and the same certificate `release-signing.cer`, as the Windows and Linux packages. In addition, it states `platform` `macos`, `architecture` `arm64`, `minimumOsVersion`, `sourceCommit`, `sourceDirty`, `builtOn` and `xcode`. It covers only the content of `client/macOS/arm64`. `install.sh` pins the SHA-256 of the certificate's public key in the same way as the Linux installer, and verifies the signature with the `openssl` tool (LibreSSL is part of macOS) and the hashes with the `shasum` tool. `install.sh` and `uninstall.sh` are POSIX shell scripts with LF and without a BOM, and they carry the POSIX executable attribute in the ZIP. The installation procedure MUST NOT download or accept an unverified replacement of any component of the folder.

Verification on the Windows host (`tools/verify-distribution.ps1`) covers the exact file list, the match of `uninstall.sh` and `README.md` with the sources and of `install.sh` with the template and the pin, LF line endings of the shell scripts, exactly one package of the client version with a xar header, the size and SHA-256 of each manifest entry, the manifest fields (version and IPC version identical to the Windows package, minimum macOS 14.0) and the signature with the same certificate as the Windows package. It rejects a half-prepared folder (`arm64` next to the placeholder, or an incomplete `arm64`). It does not verify the content of the `.pkg`, the ad hoc signatures inside it, or the installation. Those are covered by the workflow (checking the payload and the scripts in `package-pkg.sh`, `codesign --verify`, service tests, installing the package with the system `installer -pkg` on the runner with verification of the launchd daemon, the CLI, the tray launched by the LaunchAgent and Connect through the installed daemon, screenshots of the tray windows and `sudo sh uninstall.sh --remove-data`, see [client.md, macOS](client.md#macos)) and by a manual test on a Mac: `sh install.sh --verify-only`, `sudo sh install.sh` with a signed manifest, `phpvpn status`, the tray in the menu bar, an upgrade preserving the configuration, `sudo sh install.sh --headless` and `sudo sh uninstall.sh` without `--remove-data`. Tests that were not performed are listed in a dated report.

### Android

According to decision R2 in [plan-android-v001.md](Plans/2026-10-31-09-40-08/plan-android-v001.md), the Android client is not distributed yet. It is a development version with a working, non-final `applicationId` `com.pilandrome.phpvpn`. The distribution therefore MUST NOT contain a `client/Android` folder or a `phpvpn-android.zip` archive, and the overall entry point does not create them. Debug-signed APKs MUST NOT be presented as a release or added to `dist`.

The development build is created outside the distribution. `gradlew assembleDebug` in `client/android` creates the debug-signed `client/android/app/build/outputs/apk/debug/app-debug.apk` for `arm64-v8a` and `x86_64`. The prebuilt static OpenSSL and curl from the `client/tools/android/build-deps.sh` script are in `work/android-deps/<tag>`, and Gradle finds them through `phpvpn.androidDeps` in `client/android/local.properties` or through the environment variable `PHPVPN_ANDROID_DEPS` (guide in [client/tools/android/README.md](client/tools/android/README.md)). Development outputs with checks are created by [client/tools/android/make-android-dev.ps1](client/tools/android/make-android-dev.ps1) (work package WP12 of the plan): it builds the debug and release variants and stores in `work/android/<version>` the debug-signed `phpvpn-<version>-debug.apk` (both ABIs), `phpvpn-<version>-arm64-v8a-debug.apk` and `phpvpn-<version>-x86_64-debug.apk`, the unsigned `phpvpn-<version>-release-unsigned.apk`, the unstripped release libraries `symbols/<abi>/libphpvpn-android.so` for crash symbolication, `SHA256SUMS` and `manifest.json` (version, `versionCode`, commit, build date, ABI, versions of the SDK, NDK and dependencies, check results). The script fails if any check fails: `apksigner verify` (debug key, release unsigned), `zipalign -c -P 16 -v 4`, `aapt2 dump badging` (`minSdk` 34, `targetSdk` 36, `versionCode`, only `arm64-v8a` and `x86_64`, the exact list of six permissions), exported components and `allowBackup=false` from `aapt2 dump xmltree`, export of only `JNI_OnLoad` and 16 KB alignment of the LOAD segments of `libphpvpn-android.so`, a stripped release library with the same build ID as the copy in `symbols`, and lint `:app:lintDebug` without errors. The debug APK does not carry the test library `libphpvpn-android-tests.so`. The [client-android](.github/workflows/client-android.yml) workflow runs the script via `pwsh` and publishes the artifacts `android-deps` (dependencies) and `phpvpn-android-debug` (the content of `work/android/<version>`). The results are in [wp12-packaging-report-2026-11-01.md](client/tests/android/wp12-packaging-report-2026-11-01.md).

Release signing with a keystore outside the repository, AAB, the outputs `dist/client/Android/phpvpn-<version>-<abi|universal>.apk`, the optional Android step in [tools/make-distribution.ps1](tools/make-distribution.ps1) and the materials for Google Play are deferred to the revision of R2 before the first release. The behavior of the application is set by [client.md, Android](client.md#android). The English guide to installing and using the development APK is [documentation/android-client.md](documentation/android-client.md). The overall entry point copies from `documentation/` only `readme.txt` and the English protocol, so the Android guide does not get into `dist` and the root `readme.txt` does not mention Android.

## Build and verification procedure

0. Run `./tools/bump-version.ps1` and commit the new version according to the Version section.
1. Determine the current source state intended for release and perform the checks corresponding to the changes. Record the sources used, the version, the platform and the verification results. Tests that were not performed MUST NOT be reported as successful.
2. Build the client programs from these sources, build the Linux packages on the Linux build host from the same commit, and prepare the current PHP, Python and Node.js server files, the current WordPress plugin core, the Python lock and the English READMEs of all four server parts. Prepare the root English `readme.txt`. Only on an explicit request to update the documentation, translate the entire current `protocol.md` and preserve both the translation and the hash of the source. Working build directories stay outside the resulting distribution.
3. Recreate the content of `dist` and the entire mandatory structure. Fill the server part, the platform client folders and `documentation` according to the rules above. Always include `documentation/readme.txt`, and optionally the preserved English `documentation/protocol.md`.
4. Prepare the complete Windows x64 package, the Linux x64 folder and, with a package from CI, the macOS arm64 folder; verify the architecture and dependencies, include the licenses and, once the content is complete, create the corresponding signed manifest for each of them. The trust level of the signature is stated truthfully; a development signature MUST NOT be presented as a publicly trusted publisher signature.
5. Verify the hash match of the mandatory README with the root source and of a present protocol with the preserved English translation. Verify the hash match of both files of the PHP part, all eleven files of the Python part, both files of the Node.js part and the README of the WordPress part with the current sources, the byte match of all entries of the WordPress plugin ZIP with the plugin folder, all required paths, the completeness of the files, the relative references of the scripts and the integrity of the client packages, including the signatures of the Linux and macOS manifests and the provenance of the Linux and macOS packages. Verify the Python part in a clean environment outside `dist`, by initializing it with the distributed scripts and starting the distributed server. Verify health, info and data in both modes using isolated configuration copies. Verify the Node.js part by starting the distributed `phpvpn.js` from an extracted copy with an isolated configuration (`--config`, its own port and root) and exchanging health, info and data. The actual installation, operational and other tests prescribed in [client.md](client.md) are performed when creating the distribution, unless the user explicitly specifies that they are not to be performed. On such an explicit instruction, these tests are not performed.
6. Hand over the exact path to the result and a brief verification status. If a ZIP is created, its content MUST correspond to the verified result and preserve the corresponding directory structure. The archive and working reports are stored outside `dist` so that they do not mix with its content on the next build.

The canonical entry point for the **entire distribution** is [tools/make-distribution.ps1](tools/make-distribution.ps1). It requires an explicit existing CMake build from this checkout and a folder of Linux packages from the same commit. A macOS package from the same commit is optional (macOS section). It always reruns the Windows client build, so it never merely packages whatever old binaries it finds.

First, on the Windows host, the version is incremented and committed:

```powershell
./tools/bump-version.ps1 -Commit
```

The Linux packages are built on the Linux build host in a checkout of the same commit without uncommitted changes. Both runs write into the same folder; by convention, `<id>` is `<version>-<release>-<short commit>`:

```sh
cd client
sh tools/linux/build-packages-in-container.sh fedora:44 ../work/linux-packages/<id>
sh tools/linux/build-packages-in-container.sh ubuntu:24.04 ../work/linux-packages/<id>
```

A copy of the sources without `.git` MUST pass the actual commit and state through `--source-commit` and `--source-dirty`. The macOS package is built by the `client-macos` workflow after the same commit is pushed; its artifact `phpvpn-macos-arm64` is extracted into `work/macos-packages/latest` (or into `work/macos-packages/<id>` passed through `-MacosPackagesDirectory`):

```powershell
gh run download <run-id> -n phpvpn-macos-arm64 -D ./work/macos-packages/latest
```

After the Linux folder has been transferred into `work/linux-packages/<id>` of the Windows checkout, run from its root:

```powershell
./client/tools/build-x64.ps1 -Preset release -Configure -ResourceTests
./tools/make-distribution.ps1 -BuildDirectory ./work/build/release -LinuxPackagesDirectory ./work/linux-packages/<id> -MacosPackagesDirectory ./work/macos-packages/latest -SignBinaries
./tools/verify-distribution.ps1 -DistributionDirectory ./dist
```

The signing identity is determined by `signing.certificateThumbprint` in `phpvpn.config.json`, or in the file selected by the `PHPVPN_CONFIG` variable. The optional `-CertificateThumbprint` takes precedence over the configuration and is passed to all client packagers and distribution checks. The selected RSA certificate with its private key must be in `Cert:\CurrentUser\My` of the account performing the build. The private key is not exported.

Before creating and signing the manifest, the packager generates the installation scripts from the source templates. It inserts the thumbprint of the selected certificate into the Windows `install.ps1`, and the SHA-256 of its DER SubjectPublicKeyInfo into the Linux and macOS `install.sh`. The GUI, BAT and shell entry points thus verify the same publisher identity. During installation, the pin is not derived from the bundled certificate. The verification scripts compare the entire generated installer with the template and the identity from the configuration or the explicit parameter; all other files are still compared byte for byte with the sources.

The recipient of the source code distribution must set up their own signing identity. For a local test, a new certificate can be created with the command `New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=phpvpn local test' -KeyAlgorithm RSA -KeyLength 3072 -CertStoreLocation Cert:\CurrentUser\My` and its `Thumbprint` stored in the configuration. Such a certificate is not a publicly trusted publisher signature. For a release, use your own publisher certificate and ensure trusted delivery of the installer to the recipient. The package signature alone does not replace this distribution of trust. The Windows manifest covers only the content of `client/Windows/x64`, the Linux manifest only the content of `client/Linux/x64` and the macOS manifest only the content of `client/macOS/arm64`.

The overall build entry point creates a new staging in `work/distribution-<id>` and checks the exact file list including hidden files, the hashes, the architecture, the signatures of the Linux and, if present, the macOS manifest, and the icons. It creates the ZIP, preserves the POSIX executable attribute of the shell scripts, including `client/Linux/x64/install.sh` and `uninstall.sh` (and `client/macOS/arm64/install.sh` and `uninstall.sh`), verifies these attributes directly in the archive, extracts the archive into a new directory and runs the verification again. Only then does it publish `dist` and `dist.zip`. It leaves the previous tree in the working directory for recovery. If verification fails, it does not change the existing distribution. Recursive moves reject reparse points and targets outside the designated directories. The path to the extracted verified copy is printed for subsequent isolated operational tests.

The `-StagingOnly` switch stops after the verified staging, the ZIP and the verification of the extracted copy in `work/distribution-<id>`, and prints their paths. It changes neither `dist` nor `dist.zip`. It is used for trial builds and for tests before publication.

The partial tools [client/tools/make-dist.ps1](client/tools/make-dist.ps1), [client/tools/linux/make-linux-dist.ps1](client/tools/linux/make-linux-dist.ps1) and [client/tools/macos/make-macos-dist.ps1](client/tools/macos/make-macos-dist.ps1) require `-OutputDirectory <path>` under `work` or `dist`. The root `dist` is not a permitted target of these partial tools. The server part and the other platform folders are created exclusively by the overall entry point.

After the build, run `python -B tools/test-server-scripts.py --distribution <extracted-tree> --report <report.json>` in a separate development venv. The runner creates its own copies, ports and buffers, and checks the Windows BAT or native POSIX SH entry points, health/info/data, identity, concurrency, errors, URLs and banners lasting at least three seconds. `server/python/tests/test_server_control.py` adds deterministic cases of a recycled PID, unavailable permissions and a timeout. The bootstrap without uv and Python is verified separately in a clean environment. A Windows run does not replace Linux or macOS; platforms not covered and manual installation tests are listed in a dated report.

The Windows package is also verified with `client/tools/verify-package.ps1 -PackageDirectory <path>`. This read-only step checks the match of the scripts, the mandatory presence of the five EXEs, the manifest, the signatures, VerifyOnly and the exact embedded ICO resources of all five production EXEs, including their UAC levels. It requires a built `phpvpn-icon-resource-tests`, by default from the `release` preset with `BUILD_TESTING=ON`. A different verification build can be specified with `-IconResourceTest`. The four source ICO files are used only during the build and verification; they are not added separately to the runtime package.

The Linux folder is verified by the read-only `tools/verify-distribution.ps1`. It checks the exact file list, the match of `install.sh`, `uninstall.sh`, `README.md` and the licenses with the sources, LF line endings of the shell scripts, exactly four packages of the client version with a common release and a valid RPM or DEB header, the size and SHA-256 of each manifest entry, and the manifest fields (version and IPC version identical to the Windows package, exactly Fedora and Ubuntu). It verifies the signature in the same way as the Windows installer: `release-signing.cer` MUST be identical to that of the Windows package, the identity in the manifest and in the signature MUST correspond to the certificate, the public key MUST match the pin in `install.sh`, and the RSA PKCS#1 v1.5 SHA-256 signature MUST be valid for the exact bytes of the manifest. A manifest with `sourceDirty` equal to `true` raises a warning that it is not a release.

On Linux systems, `sh install.sh --verify-only` is further run from the extracted verified copy on Fedora 44 and Ubuntu 24.04 (it does not require root and installs nothing), and on a Linux host with podman a container installation test of both distributions is run:

```sh
cd client
sh tests/e2e/linux/package-install-container.sh fedora:44 <extracted-copy>/client/Linux/x64 <release-2-folder>
sh tests/e2e/linux/package-install-container.sh ubuntu:24.04 <extracted-copy>/client/Linux/x64 <release-2-folder>
```

The test verifies `--verify-only`, the rejection of modified and unlisted files, installation, the running service, `phpvpn status`, an upgrade to a higher release preserving the configuration, the tray package and uninstallation with and without data. `<release-2-folder>` is a folder of the same version with a higher release (packages built with `PHPVPN_PACKAGE_RELEASE=2`, assembled by `make-linux-dist.ps1` under `work`). It serves only for the upgrade test and does not belong in the distribution. Installation into a real system, the graphical tray and AppIndicator on Fedora GNOME are verified according to [client.md](client.md); platforms and tests that were not performed are listed in a dated report.

The status page is embedded directly in `phpvpn.php`, `phpvpn.py`, `phpvpn.js` and in the WordPress plugin core. No server distribution needs `assets/index.html`. PHP, Python and Node.js do not use a mutually readable disk format; switching the implementation over queues in progress is not a supported migration.

## source code distribution to gitpilandrome

This chapter is the procedure for an AI agent on an explicit assignment to copy the source
version of the project. Editing this document by itself does not trigger the distribution.
The source `D:\git\phpvpn` belongs in the target clone `D:\gitpilandrome\phpvpn`
(remote `https://github.com/pilandrome/phpvpn.git`).

For this specific operation, this chapter is authoritative. The methodology
[create_source_code_distribution.md](create_source_code_distribution.md)
serves as a basis for finding the necessary inputs and dependencies. Do not take over from it
the target `C:\gitsigma`, the `phpvpn_old` rotation or the emptying of the target clone.
The scope of the transfer is determined by necessity according to the rules below. The English README and other
bundled descriptions and guides are mandatory here and their translation is part of the assignment;
the optional translation with additional consent does not apply. It is explicitly permitted to keep
the public PHPVPN default values needed to run: the default server secret,
default tokens, access keys and default passwords. This exception preserves the current
behavior of the client and the servers; it does not authorize transferring real local secrets.
The preceding rules of the regular binary distribution in this document remain unchanged.

The tools of this operation (inventory, audit, staging assembly with the translations, manifest, link
check, writing into the target) are in [tools/source-distribution](tools/source-distribution/README.md).

Include the necessary sources of the client and the servers, build and installation templates, configurations,
resources, dependency locks and guides. For example, `client/packaging/linux/dist`
contains source inputs and cannot be excluded just because of the name `dist`.
Do not take over certificates and prebuilt DLLs from the binary distribution. Document how to obtain
the necessary external components and how to set up your own signing identity.
If a complete binary build requires a commit by the recipient, state this prerequisite
in the README. The distribution agent must not create that commit itself in the target clone or substitute
the original history for it; it must admit any missing verification. Keep the root `readme.txt` intended
for the binary packager in its role and, for the source copy, add an English
`README.md` if the existing file describes only the installation of prebuilt packages.

1. First verify the actual source, the target Git remote and the state of both working trees.
   Do not derive the target folder name automatically from the source name. Copy the current
   intended content, including necessary uncommitted changes. Do not change the source branch,
   do not perform a reset, stash or cleanup. If work-in-progress content is unclear, clarify the scope.
2. Build an explicit inventory of the necessary inputs according to the actual dependencies,
   including hidden, untracked and ignored files. Include only the sources,
   resources, configurations, generators and scripts needed to build or run the project,
   plus the READMEs, necessary guides and licenses. If the build requires them, also include
   the source tests and their safe inputs. The list of files in Git alone is not sufficient.
   Add a missing submodule or the actual Git LFS content; a mere pointer is not sufficient.
3. Preserve dependency manifests, existing lock files and the necessary license
   texts, copyright and NOTICE files. Do not update dependency versions when copying.
   Replace downloadable dependencies with documented acquisition of a specific compatible
   version and integrity verification, not with a copy of a local installation or cache.
   Resolve unclear redistribution rights before handover.
4. The README and other bundled descriptions and guides must be in English. Translate the current non-English
   parts completely into English when preparing the distribution copy; do not change the original
   documents. Preserve commands, identifiers and meaning; fix and verify links.
   Do not arbitrarily translate mandatory license texts. The README must state the prerequisites,
   tool versions, dependency installation and exact commands for building and running.
5. Internal paths must be relative and must work after the whole project is moved.
   No part may require an absolute path of the original computer or a file outside
   the handed-over project. This includes configurations, scripts, resources and references through
   symlinks or junctions. Documented standard tools and installable
   dependencies are environment prerequisites, not a binding to the author's local installation.
   The source and target paths given here serve only for copying.
6. Do not copy any certificates, private keys or real local passwords,
   access tokens, API keys or other secret data, not even inside sources,
   documentation or archives. The only exception is the public PHPVPN default values
   needed to run, as defined above. For each value kept, verify its
   origin in the current default implementation and record it in the report. Do not extend the
   exception to other values of the same field or file, and never to certificates
   or private keys. Inspect the content; an extension filter is not sufficient. Bundle safe
   configuration templates and describe how to replace the public default values with your own
   data before actual deployment.
   If removing such data breaks functionality, find a portable configuration
   and verify it before handover; do not bypass security checks.
7. Omit built programs and packages, build and test outputs, caches,
   virtual environments, `node_modules`, logs, runtime data, backups and working
   notes. Decide by purpose, not only by directory name or extension.
   Necessary source images, models and text launch scripts are not build waste.
8. Prepare the selected content in a new temporary folder outside both the source and the target clone.
   Never transfer the source `.git`, and do not delete or overwrite the target `.git`.
   Before writing to the target, compare the specific files and verify its state again.
   Preserve existing unrelated and in-progress changes. On a collision, request
   a decision about the specific file. Do not use blanket deletion or mirroring of the target.
   Handle the removal of provably obsolete distributed files individually
   according to the inventory, never on the assumption that the target clone is empty.
9. Verify completeness and portability from a clean copy at a different path according to the English README,
   without reading the source tree and without the original caches or environments. Verify the build
   and basic launch of the application; for models, opening them and functional controls with the necessary
   textures. Store verification outputs outside the handed-over content. Conclude the handover with
   a check of the target diff, the inventory, the links and the absence of forbidden data.
   State the target path, the changed files, the checks performed and not performed, and any obstacles.
   Without a separate instruction, do not create a commit, push, release or any other publication.
