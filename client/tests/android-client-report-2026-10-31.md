# Android client report, 2026-10-31

Výsledky pracovních balíků podle [plan-android-v001.md](../../Plans/2026-10-31-09-40-08/plan-android-v001.md).
Co se nespustilo, je NOT RUN s důvodem. Žádný výsledek z emulátoru se nevydává za výsledek z reálného zařízení.

## WP00: prostředí, kostra a CI

### Prostředí (Windows host)

| Součást | Stav |
|---|---|
| Android Studio | Rabbit 2, 2026.2.2 (`AI-262.10968.63.2622.16730706`), JBR 25.0.4 v `D:\androidstudio\jbr` |
| SDK | `%LOCALAPPDATA%\Android\Sdk`; platform-tools 36.0.0, build-tools 35.0.0 a 36.0.0, platform android-36, emulátor 36.1.9 |
| cmdline-tools | nainstalováno WP00: `commandlinetools-win-15859902_latest.zip` (SHA-256 ověřen), `cmdline-tools/latest` = 22.0 |
| NDK | `ndk;28.2.13676358` (r28c, výchozí NDK AGP 9.5) |
| CMake ze SDK | `cmake;3.31.6` (projekt vyžaduje ≥ 3.28) |
| Platformy | android-34, android-35, android-36 |
| Systémové obrazy | `google_apis;x86_64` pro API 34 a 35 (WP00), `google_apis_playstore;x86_64` pro API 36 (předinstalováno) |
| Akcelerace emulátoru | WHPX 10.0.22631 „installed and usable“ (`emulator -accel-check`) |
| Gradle | 9.8.2 přes wrapper, AGP 9.5.0 (vestavěný Kotlin), Compose compiler a serialization plugin 2.4.21 |

### Verze knihoven a jejich zdůvodnění

- AGP 9.5.0 je poslední stabilní (9.6 je preview), vyžaduje Gradle ≥ 9.6 a JDK 17; vestavěný Kotlin,
  `org.jetbrains.kotlin.android` se neaplikuje.
- Navigation Compose 2.10.2, Lifecycle 2.11.0, Activity Compose 1.13.0, core-ktx 1.19.1, Compose BOM 2026.09.00
  (Compose 1.12.1, Material 3 1.4.0), kotlinx.serialization 1.12.0, kotlinx.coroutines 1.11.0 (poslední stabilní
  k 2026-10-31).
- `compileSdk 37.2` místo plánovaných 36: první `assembleDebug` selhal na `checkDebugAarMetadata`, protože
  navigation 2.10.2, core 1.19.1 a lifecycle 2.11.0 vyžadují překlad proti API 37. `minSdk 34` a `targetSdk 36`
  zůstávají, platforma `platforms;android-37.2` doinstalována. Plán upraven.

### Ověření politik Androidu a Google Play (dokumentace k 2026-10-31)

| Otázka | Zjištění | Zdroj |
|---|---|---|
| `foregroundServiceType="systemExempted"` pro VPN | Typ je dovolen mimo jiné pro „VPN apps (configured using Settings > Network & Internet > VPN)“, tedy aplikace s deklarovanou `VpnService`. Jinak `startForeground` vyhodí `ForegroundServiceTypeNotAllowedException`. Oprávnění `FOREGROUND_SERVICE_SYSTEM_EXEMPTED`. Náhradní `specialUse` vyžaduje `<property android:name="android.app.PROPERTY_SPECIAL_USE_FGS_SUBTYPE">` a zdůvodnění při revizi v Play Console. | [fg-service-types](https://developer.android.com/develop/background-work/services/fg-service-types), aktualizováno 2026-10-28 |
| Foreground služba z `BOOT_COMPLETED` na API 35+ | Zakázané typy jsou jen `dataSync`, `camera`, `mediaPlayback`, `phoneCall`, `mediaProjection`, `microphone`. `systemExempted` zakázaný není. Test bez restartu: `adb shell am broadcast -a android.intent.action.BOOT_COMPLETED <package>`. | [behavior-changes-15](https://developer.android.com/about/versions/15/behavior-changes-15), aktualizováno 2026-10-28 |
| Požadavek Play na `targetSdk` | Od 2026-08-31 musí nové aplikace i aktualizace cílit na API 36 (Android 16); prodloužení do 2026-11-01 na žádost. Plán s `targetSdk 36` vyhovuje. | [Play target API](https://support.google.com/googleplay/android-developer/answer/11926878) |
| Deklarace `VpnService` v manifestu | `android:permission="android.permission.BIND_VPN_SERVICE"` a intent-filter `android.net.VpnService`. `onRevoke` nemusí přijít na hlavním vlákně a jiná síť už směruje provoz. | [develop/connectivity/vpn](https://developer.android.com/develop/connectivity/vpn) |
| Doporučená spojení Compose | Compose Compiler Gradle plugin má verzi shodnou s Kotlinem (2.4.21), `buildFeatures.compose = true` zůstává. | [compose/compiler](https://developer.android.com/develop/ui/compose/compiler) |

### Kostra

- `client/android`: Gradle wrapper 9.8.2, `settings.gradle.kts`, `gradle/libs.versions.toml`, moduly `bridge`
  (nativní build přes `client/CMakeLists.txt`, cíl `phpvpn-android`, ABI arm64-v8a a x86_64) a `app`
  (Compose, `applicationId io.github.jaromirsivic.phpvpn`, `namespace phpvpn.android`, verze z CMake).
- CMake: `PHPVPN_PLATFORM=android` při `CMAKE_SYSTEM_NAME STREQUAL "Android"`, `-fPIC` místo `-fPIE`, bez `-pie`,
  `_FORTIFY_SOURCE=2`, `PHPVPN_ANDROID_DEPS`, `apps/android-jni` s `jni_entry.cpp` (`JNI_OnLoad` +
  `RegisterNatives`, `nativeVersion()` jako UTF-8 `byte[]`).
- CI: `.github/workflows/client-android.yml` (joby `deps` s cache podle hashe `build-deps.sh` a `app`).
- Jediná změna v `core/base`: bionic nemá `explicit_bzero` (hlavičky ani `libc.so` API 34 v NDK r28c), proto
  `secure_buffer.cpp` používá na Androidu C23 `memset_explicit` (bionic od API 34). Plán bod 13 opraven.

### Výsledky

| Test | Výsledek | Poznámka |
|---|---|---|
| Nativní build `phpvpn-android` pro x86_64 přímo CMake 4.4.2 + NDK r28c (`-Werror`, `BUILD_TESTING=OFF`) | PASS | celé `phpvpn-core` a `phpvpn-platform` (POSIX soubory) přeloženy bez varování; `llvm-readelf -l`: LOAD zarovnání 0x4000, `GNU_RELRO`, `GNU_STACK RW` |
| `gradlew assembleDebug` lokálně | PASS | `app-debug.apk` (`versionCode 10003`, `versionName 1.0.3`, `minSdkVersion 34`, `targetSdkVersion 36`, `compileSdkVersion 37`), `native-code: arm64-v8a x86_64`, `zipalign -c -P 16 -v 4` „Verification successful“ |
| APK na `phpvpn-api34` zobrazí text z nativní knihovny | PASS | emulátor x86_64 API 34 headless (WHPX, boot 24 s), `uiautomator dump` obsahuje `text="phpvpn native client 1.0.3"`, snímek `work/build/wp00-api34.png`, žádný záznam `AndroidRuntime:E` |
| `gradlew testDebugUnitTest` lokálně | PASS | zatím bez testů, ověřuje jen konfiguraci úlohy |
| CI `client-android.yml` | NOT RUN | spustí se po pushnutí větve `android` |
| Nalezeno a opraveno | | `app` bez `abiFilters` balilo i `armeabi-v7a` a `x86` knihovny androidx; doplněno `abiFilters` do obou modulů (R4). |
