# WP12: balení, kontroly a vývojové výstupy, report 2026-11-01

Výsledek vývojové části kroku 12 podle [plan-android-v001.md](../../../Plans/2026-10-31-09-40-08/plan-android-v001.md)
(krok 12, rozhodnutí R2, „Zjištěná omezení a volby“ body 14 a 15). Podle R2 se zatím nedistribuuje, WP12 proto dodává
jen debug podepsané APK pro obě ABI ve `work/android/` a jejich kontroly. Co se nespustilo, je NOT RUN s důvodem.
Výsledky jsou z Windows hostitele (Android SDK, build tools 36.0.0, NDK 28.2.13676358, JDK `D:/androidstudio/jbr`
25.0.4), z emulátoru `emulator-5554` (`phpvpn-api34`, google_apis x86_64, API 34) a z emulátoru `emulator-5560`
(`phpvpn-api36-wp10`, x86_64, API 36), který jsem pro tento balík spustil a po testech zastavil. Žádný výsledek není
z reálného zařízení ani z CI.

Větev `worktree-agent-a941a73ab8a2de971` nad `android` 867de95 (WP00 až WP10 kromě WP11, sloučený WP05). Commity:
`316e89e` (lint chyby), `114654b` (skript, splity ABI, vyloučení testovací knihovny), `083a1aa` (CI), `ee6aeaf`
(dokumentace) a commit s tímto reportem. Závěrečný běh skriptu je nad `ee6aeaf` s čistým stromem
(`sourceDirty: false`).

## Co vzniklo

| Část | Soubory | Obsah |
|---|---|---|
| Skript | `client/tools/android/make-android-dev.ps1` | Windows PowerShell 5.1 i PowerShell 7 (bez `&&`, ternárního operátoru a `$IsWindows`). Najde JDK, SDK, build tools, NDK a předsestavené závislosti, spustí `gradlew assembleDebug assembleRelease` s `-Pphpvpn.abiSplits=true` a zvlášť `:app:lintDebug`, zkopíruje výstupy, provede kontroly, zapíše `manifest.json` a `SHA256SUMS`. Exit 0 = všechny kontroly prošly, 1 = některá selhala, 2 = sestavení nebo prostředí. |
| Gradle | `client/android/app/build.gradle.kts` | Volitelné splity ABI (vlastnost `phpvpn.abiSplits`, výchozí vypnuto, takže běžný build dál dává jedno `app-<typ>.apk`), `packaging.jniLibs.excludes` pro `**/libphpvpn-android-tests.so`, komentář release větve (R2). |
| Lint | `client/android/app/lint.xml`, `AndroidManifest.xml`, `ScreenSupport.kt`, `EditorGraph.kt` | Viz „Lint“. |
| CI | `.github/workflows/client-android.yml` | Krok „Package and check“ v jobu `app`, viz „CI“. Job `deps` beze změny. |
| Dokumentace | `distribution.md` (odstavec Android), `documentation/android-client.md`, `client/tools/android/README.md` | Nové výstupy, kontroly a obsah artefaktu `phpvpn-android-debug`. |

### Rozhodnutí v prostředí

- **JAVA_HOME:** parametr `-JavaHome`, jinak `D:/androidstudio/jbr`, pokud existuje, jinak proměnná `JAVA_HOME`
  (na CI ji nastaví `actions/setup-java`). Skript ji nastaví i pro `apksigner` a přidá `bin` na začátek `PATH`.
- **SDK:** `-AndroidSdk`, `ANDROID_HOME`, `ANDROID_SDK_ROOT`, `sdk.dir` z `local.properties`, `%LOCALAPPDATA%\Android\Sdk`.
  Build tools nejnovější nainstalované, nejméně 35.0.0 (`zipalign -P`). NDK verze se čte z `bridge/build.gradle.kts`,
  verze klienta z `project(phpvpn-client VERSION …)` v `client/CMakeLists.txt` stejně jako v Gradle.
- **Závislosti:** ve stejném pořadí jako `bridge`: `phpvpn.androidDeps` z `local.properties`, potom `PHPVPN_ANDROID_DEPS`.
  Bez `manifest.json` závislostí skript skončí kódem 2, protože knihovna by neměla crypto provider ani transport.

## Výstupy

`work/android/1.0.3/` (gitignored), závěrečný běh 2026-11-01T00:51:29Z, commit `ee6aeaf`:

| Soubor | Velikost | Podpis | Obsah |
|---|---|---|---|
| `phpvpn-1.0.3-debug.apk` | 55 695 022 B | debug keystore (`C=US, O=Android, CN=Android Debug`), schéma v2 | univerzální, `arm64-v8a` + `x86_64` |
| `phpvpn-1.0.3-arm64-v8a-debug.apk` | 45 223 940 B | debug keystore, v2 | jen `arm64-v8a` (telefony) |
| `phpvpn-1.0.3-x86_64-debug.apk` | 45 536 774 B | debug keystore, v2 | jen `x86_64` (emulátor) |
| `phpvpn-1.0.3-release-unsigned.apk` | 42 807 152 B | **nepodepsané** (Gradle bez release `signingConfig`), nelze instalovat | univerzální release |
| `symbols/arm64-v8a/libphpvpn-android.so` | 39 766 584 B | – | nestripnutá release knihovna |
| `symbols/x86_64/libphpvpn-android.so` | 38 118 656 B | – | nestripnutá release knihovna |
| `SHA256SUMS` | – | – | SHA-256 všech souborů výše a `manifest.json` (formát `sha256sum -c`, ověřeno) |
| `manifest.json` | – | – | `version` 1.0.3, `versionCode` 10003, `applicationId`, `commit`, `sourceDirty`, `buildDate`, `abis`, `sdk` (compileSdk 37.2, minSdk 34, targetSdk 36, build tools 36.0.0, NDK 28.2.13676358, CMake 3.31.6, AGP 9.5.0, Kotlin 2.4.21, Gradle 9.8.2, JDK), `dependencies` (z `work/android-deps/openssl-3.5.9-curl-8.23.0/manifest.json`: OpenSSL 3.5.9 a curl 8.23.0 s hashi tarballů, hash receptu, NDK, API 34, datum sestavení), `apks` (velikost, podpis, nativní knihovny, build ID), `lint`, `checks` (všechny kontroly s výsledkem) a `files` (cesta, velikost, SHA-256). |

Splity ABI: `splits.abi` s `include("arm64-v8a", "x86_64")` a `isUniversalApk = true` funguje vedle `ndk.abiFilters`
bez konfliktu, stačila jedna podmíněná konfigurace. Univerzální APK se splity jsem s `app-debug.apk` běžného buildu
bajtově neporovnával; prochází stejnými kontrolami.

Kopie pro symbolizaci pochází z `client/android/app/build/intermediates/merged_native_libs/release/mergeReleaseNativeLibs/out/lib/<abi>/`,
tedy z knihovny před úlohou `stripReleaseDebugSymbols` (CMake `RelWithDebInfo`, obsahuje `.debug_info`). Skript
ověřuje, že její GNU build ID je shodné s knihovnou v release APK. Debug knihovny se nekopírují.

### Velikosti knihoven

| Knihovna (nekomprimovaná v APK, `extractNativeLibs=false`) | arm64-v8a | x86_64 |
|---|---|---|
| `libphpvpn-android.so` release (stripnutá) | 7 750 088 B | 8 200 264 B |
| `libphpvpn-android.so` debug (stripnutá, debug build CMake) | 10 138 120 B | 10 450 960 B |
| `libphpvpn-android.so` nestripnutá (`symbols/`) | 39 766 584 B | 38 118 656 B |
| `libandroidx.graphics.path.so` (Compose, androidx) | 10 096 B | 10 760 B |
| `libphpvpn-android-tests.so` | v žádném APK aplikace | |

Release knihovny mají stejnou velikost jako v reportu WP04. Zbytek APK tvoří hlavně dex: release 25,2 MB
(`classes.dex` + `classes2.dex`) bez R8, debug víc souborů dex. R8 je odložený (viz níže), takže release APK je
dnes jen o 13 MB menší než debug.

## Kontroly

Závěrečný běh: **37 PASS, 0 FAIL**. Skript je provádí pro každé APK, výsledky jsou i v `manifest.json`.

| Kontrola | Cíl | Výsledek | Podrobnosti |
|---|---|---|---|
| `apksigner verify --print-certs -v` | 3 debug APK | PASS | Signer #1 `C=US, O=Android, CN=Android Debug`, ověřeno schématem v2 |
| `apksigner verify` (stav podpisu) | release | PASS | „DOES NOT VERIFY“, žádný podepisovatel: nepodepsané, jak R2 očekává. Kontrola selže, kdyby release APK podpis mělo. |
| `zipalign -c -P 16 -v 4` | všechna 4 APK | PASS | „Verification successful“, 0 položek BAD |
| `aapt2 dump badging` | všechna 4 APK | PASS | `package` `io.github.jaromirsivic.phpvpn`, `versionCode='10003'` (1·10000 + 0·100 + 3), `versionName='1.0.3'`, `minSdkVersion:'34'`, `targetSdkVersion:'36'`, `native-code: 'arm64-v8a' 'x86_64'` (split APK jen svoje ABI), oprávnění přesně `INTERNET`, `ACCESS_NETWORK_STATE`, `FOREGROUND_SERVICE`, `FOREGROUND_SERVICE_SYSTEM_EXEMPTED`, `POST_NOTIFICATIONS`, `RECEIVE_BOOT_COMPLETED`; `application-debuggable` u debug ano, u release ne |
| `aapt2 dump xmltree --file AndroidManifest.xml` | release | PASS | Exportované přesně `MainActivity` (MAIN/LAUNCHER), `PhpvpnVpnService` (`permission=BIND_VPN_SERVICE`, filtr přesně `android.net.VpnService`, `foregroundServiceType=0x400` = `systemExempted`) a `BootReceiver` (filtr přesně `BOOT_COMPLETED`); `InitializationProvider` neexportovaný; `allowBackup=false`, `dataExtractionRules` nastavené |
| `aapt2 dump xmltree --file AndroidManifest.xml` | 3 debug APK | PASS | Stejné tři komponenty a navíc jen pevný seznam debug komponent, viz odchylka 1 |
| Nativní knihovny v APK | všechna 4 APK | PASS | jen `lib/arm64-v8a` a `lib/x86_64` (split jen svoje), žádná `libphpvpn-android-tests.so`, všechny `.so` uložené bez komprese |
| `llvm-nm -D --defined-only` `libphpvpn-android.so` | 4 APK × ABI (6 knihoven) | PASS | jediný definovaný symbol `JNI_OnLoad` |
| `llvm-readelf -lW` LOAD | 6 knihoven | PASS | 4× LOAD, zarovnání vždy `0x4000` (16 KB) |
| Release knihovna stripnutá | release, obě ABI | PASS | bez `.symtab` a `.debug_*`; build ID arm64 `ebf6e615…0588`, x86_64 `e47e1baf…3d41` |
| Kopie v `symbols/` | obě ABI | PASS | obsahuje `.debug_info`, build ID shodné s knihovnou v APK |
| Lint `:app:lintDebug` | `app` | PASS | 0 chyb, 51 varování, viz „Lint“ |
| `sha256sum -c SHA256SUMS` (Git Bash) | výstupní složka | PASS | 7 souborů OK |

Další ověření mimo skript:

| Test | Výsledek | Poznámka |
|---|---|---|
| `:bridge:connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=phpvpn.android.bridge.PosixNativeTest`, `ANDROID_SERIAL=emulator-5554` | PASS | 1 test (`posixLayerPassesInsideTheApplicationSandbox`), 0 selhání. Testovací APK modulu `bridge` dál nese `libphpvpn-android-tests.so` pro obě ABI, vyloučení v `app` se ho netýká. |
| `testDebugUnitTest` (JVM, `app` a `bridge`) | PASS | `app` 183, `bridge` 109 testů, 0 selhání |
| `:app:connectedDebugAndroidTest` třídy `wp10.EditorUiTest` a `ui.MainFlowsTest`, `emulator-5560` (API 36) | PASS | 5 + 16 testů, 0 selhání; pokrývají změněné `editorViewModel()` a `currentLocale()`. Spuštěno na vlastním emulátoru, aby se neměnila aplikace nainstalovaná na sdílených emulátorech. |
| `assembleRelease` s `isMinifyEnabled = false` | PASS | release APK vzniká, knihovny AGP stripuje sám (`stripReleaseDebugSymbols`) |

## Lint

Před změnou `:app:lintDebug` hlásil **4 chyby** a 55 varování, build lintu selhal. Chyby:

| Chyba | Místo | Posouzení a oprava |
|---|---|---|
| `NonObservableLocale` | `ScreenSupport.kt`, `currentLocale()` | Skutečná: `Locale.getDefault()` v composable funkci. Opraveno, locale se bere jen z pozorovatelné konfigurace, `Locale.ROOT` jen pro teoreticky prázdný seznam. |
| `UnrememberedGetBackStackEntry` | `EditorGraph.kt`, `editorViewModel()` | Skutečná podle pravidla: `remember` bez klíče typu `NavBackStackEntry`. Opraveno klíčem `LocalViewModelStoreOwner.current as? NavBackStackEntry`. Chování se nemění, kořenový graf má po celou dobu stejnou položku (ověřeno `EditorUiTest`). |
| `ForegroundServicePermission` | `AndroidManifest.xml`, služba | Falešná: lint zná jako podmínku `systemExempted` jen `SCHEDULE_EXACT_ALARM`/`USE_EXACT_ALARM`, druhou podmínkou je VPN aplikace se souhlasem (ověřeno WP05 na API 34 a 35). Přidat oprávnění k alarmům by porušilo seznam oprávnění. `tools:ignore` na elementu s komentářem. |
| `MissingClass` | `AndroidManifest.xml`, `ProfileInstallReceiver` | Falešná: element je značka `tools:node="remove"`, ne odkaz na třídu. `tools:ignore` na elementu s komentářem. |

`app/lint.xml` ignoruje jen tato vysvětlená varování (chyby nikdy):

| Varování | Rozsah | Důvod |
|---|---|---|
| `OldTargetApi` (1) | celý modul | `targetSdk 36` je rozhodnutí plánu, API 37 je jen compile SDK kvůli androidx. |
| `SdCardPath` (2) | `src/debug` | Debug náhrady (`ConfigReplies.kt`, `FakeWizardService.kt`) uvádějí skutečnou cestu aplikace na testovacím emulátoru, aplikace sama ji bere z `Context`. |
| `ExportedReceiver` (1) | `src/debug/AndroidManifest.xml` | `DebugVpnCommandReceiver` je adb vstup host testů WP05, jen v debug. |

Zbývá **0 chyb a 51 varování**, ponechaných bez potlačení: `UnusedResources` 42 (40 v `strings.xml`, 1 v
`strings_wp10.xml`, 1 v `symbols.xml`; texty a symboly obrazovkové mapy, které zatím žádná obrazovka nepoužívá, patří
k revizi UI, ne k balení), `MissingKeepAnnotation` 5 (`Routes.kt`, enumy v typovaných routách; týká se až
minifikace, tedy odloženého R8), `UseKtx` 2 (`Platform.kt`, `SharedPreferences.edit`), `LocalContextResourcesRead` 1
(`AboutScreen.kt`), `ModifierFactoryExtensionFunction` 1 (`Foundation.kt`). Rozbor je i v `manifest.json`.
Lint modulu `bridge` (`:bridge:lintDebug`) zadání nevyžadovalo a nespouštěl jsem ho samostatně (NOT RUN).

## CI

Job `app` workflow `client-android` po kroku „Build and test“ (beze změny, `assembleDebug testDebugUnitTest`) spustí
krok **„Package and check“** s `shell: pwsh` (PowerShell 7 je na runnerech `ubuntu-24.04` předinstalovaný), tedy tentýž
skript, ne bash variantu: `client/tools/android/make-android-dev.ps1 -GradleArguments '--no-daemon'` se stejnou
`PHPVPN_ANDROID_DEPS` jako build. JDK je Temurin 17 z `setup-java` (cesta `D:/androidstudio/jbr` na Linuxu neexistuje),
build tools 36.0.0 a NDK z `setup-android`. Skript na CI navíc sestaví release variantu a spustí lint. Krok „APKs“
(`if: success() || failure()`) nahraje celou `work/android` jako artefakt **`phpvpn-android-debug`** (uvnitř složka
`1.0.3/`) místo samotného `app-debug.apk`; jméno artefaktu zůstalo, aby platily odkazy v `client.md`. Job `deps`
beze změny.

Skript je napsaný tak, aby běžel i na Linuxu (přípony `.exe`/`.bat` jen na Windows, `gradlew` místo `gradlew.bat`,
host adresář NDK se hledá, ZIP přes .NET), ale **pwsh na Linuxu jsem nespustil** (lokálně pwsh není, push dělá
uživatel): NOT RUN, ověří první běh workflow.

Doplněno po sloučení (běh `client-android` #18 nad `95f8478`, 2026-11-01): krok na `ubuntu-24.04` doběhl, 34 kontrol
PASS, 3 FAIL, všechny `apksigner verify --print-certs` nad debug APK s hlášením `DN '', schemes v2`. Linuxový
`apksigner` tedy ověřil schéma v2, ale skript z jeho výstupu nezískal řádek `Signer #1 certificate DN`. Log CI
samotný výstup neobsahuje. Oprava ve skriptu: DN se porovnává jako množina složek (`CN=Android Debug`,
`O=Android`, `C=US` v libovolném pořadí, protože keystore vygenerovaný AGP může složky vypsat jinak než keystore
z Android Studia), přijímá se i tvar `Signer (minSdkVersion=…)` a při selhání kontrola vypíše verdiktové řádky
`apksigner` (bez otisků) a návratový kód. Výsledek dalšího běhu CI je v hlavním reportu.

## Odloženo podle R2

- Release keystore mimo repozitář (cesta a hesla z prostředí nebo `local.properties`) a release `signingConfig`.
  Do té doby je release APK nepodepsané a skript to kontroluje.
- Volba Play App Signing nebo vlastní klíč (revize R2 před prvním vydáním).
- R8: `isMinifyEnabled = true`, pravidla pro JNI třídy a metody (`ModelNatives`, nativní callbacky, viz WP04) a
  `@Keep` pro serializované enumy rout (lint `MissingKeepAnnotation`), `debuggable=false` je už výchozí.
- AAB (`bundleRelease`) a výstupy `dist/client/Android/phpvpn-<verze>-<abi|universal>.apk` a `.aab`, volitelný krok
  Android v `tools/make-distribution.ps1`. Distribuční skripty (`make-dist.ps1`, `make-linux-dist.ps1`,
  `make-macos-dist.ps1`) se neměnily.
- Podklady pro Google Play: deklarace `VpnService`, deklarace foreground služby `systemExempted`, Data safety, zásady
  ochrany soukromí.

## Odchylky

1. **Exportované komponenty debug APK.** Zadání chce přesně tři exportované komponenty. To platí pro release APK.
   Debug APK záměrně exportuje navíc katalog a přepínač route (WP07), `DebugVpnCommandReceiver` (WP05, host testy) a
   aktivity knihoven `debugImplementation` (`androidx.compose.ui.tooling.PreviewActivity`,
   `androidx.activity.ComponentActivity` z `ui-test-manifest`). Kontrola debug APK proto vyžaduje tři komponenty
   release a povoluje navíc jen tento pevný seznam; cokoli dalšího je FAIL.
2. **Splity ABI jen na požádání.** Per-ABI APK vznikají jen s `-Pphpvpn.abiSplits=true` (skript ho předává), aby
   běžný `gradlew assembleDebug`, `service-lifecycle-tests.sh` a dokumentace dál pracovaly s `app-debug.apk`.
   Per-ABI release APK se nekopírují, jen univerzální.
3. **Lint změnil kód mimo WP12.** Opravy dvou skutečných chyb v `ScreenSupport.kt` a `EditorGraph.kt` (WP08 a WP10) a
   dvě lokální potlačení v manifestu. Ověřeno JVM testy a 21 Compose testy na emulátoru.
4. **Kontrola stripování jen u release.** Debug knihovny AGP také stripuje (v `manifest.json` `stripped: true`), ale
   skript to u nich nevyžaduje.
5. **`client.md` je po WP12 v jednom bodě zastaralý** (neupravoval jsem ho podle zadání): oddíl Android uvádí, že debug
   APK nese `libphpvpn-android-tests.so`, a oddíl CI, že artefakt `phpvpn-android-debug` obsahuje debug APK. Nově
   testovací knihovnu nenese a artefakt obsahuje `work/android/<verze>`. Totéž platí pro souhrnný report
   `client/tests/android-client-report-2026-10-31.md`. `distribution.md` a `documentation/android-client.md` jsem
   upravil, protože výstupy skriptu se od textu WP13 liší.

## NOT RUN

| Položka | Důvod |
|---|---|
| Krok „Package and check“ v GitHub Actions (pwsh na `ubuntu-24.04`) | Push a sledování CI dělá uživatel. Lokálně pwsh ani Linux pwsh nebyl k dispozici. Po sloučení viz „CI“: běh #18 selhal na kontrole DN debug certifikátu, skript opraven. |
| Instalace výstupních APK na reálné zařízení `arm64-v8a` | Žádné zařízení; patří k WP11. |
| Instalace `phpvpn-1.0.3-x86_64-debug.apk` na sdílené emulátory | Nepřepisoval jsem aplikaci, kterou mohou používat jiné balíky; aplikace ze stejných zdrojů prošla Compose testy na `emulator-5560`. |
| `:bridge:lintDebug` | Mimo zadání (`:app:lintDebug`). |
| Release podpis, R8, AAB, `dist`, podklady pro Google Play | Odloženo podle R2. |
