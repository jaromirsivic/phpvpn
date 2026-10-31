# WP02: POSIX refaktor a nativní testy na Androidu, 2026-10-31

Výsledek pracovního balíku WP02 podle [plan-android-v001.md](../../../Plans/2026-10-31-09-40-08/plan-android-v001.md)
(krok 2, body 1, 3, 13 a 17, testy T-N01 a T-N03). Co se nespustilo, je NOT RUN s důvodem. Žádný výsledek
z emulátoru se nevydává za výsledek z reálného zařízení.

Větev `worktree-agent-aa7e87d997d0bf42a` nad `android` (2c0689c). Worktree byl založen nad `main` (12e3ac1),
proto byl nejdřív rychle převinut na `android`.

## Co se změnilo a proč

### A. `FdTunnel` (`client/src/platform/posix/fd_tunnel.{h,cpp}`)

- Třída `TunTunnel` z `linux/tunnel_tun.cpp` je beze změny logiky přesunuta jako `FdTunnel : Tunnel`:
  `readv` s přetokovým bajtem a počítadlem `oversized`, `send` jedním `write`, `poll` spolu s `eventfd`
  zastavením, omezené čekání 200 × 5 ms při plné frontě (`EAGAIN`, `EWOULDBLOCK`, `ENOBUFS`), `signalStop`
  a `detach`. Texty chyb, kódy chyb a pořadí kontrol jsou shodné bajt po bajtu.
- `tunnel_tun.cpp` drží jen vytvoření, ověření a odstranění TUN rozhraní a volá `std::make_shared<FdTunnel>`.
  Odstraněny jsou jen include a konstanty, které používala přesunutá třída.
- `fd_tunnel.cpp` je v seznamu zdrojů `phpvpn-platform` pro Linux a Android, ne pro macOS (utun má vlastní
  4bajtovou hlavičku rodiny protokolů). macOS soubory nejsou změněné.
- Nový test `tests/linux/unit/test_fd_tunnel.cpp` (5 případů, Linux sada i Android) běží bez práv nad párem
  `AF_UNIX SOCK_SEQPACKET`, který zachovává hranice paketů jako TUN nebo deskriptor `VpnService`: obousměrný
  přenos, timeout, přerostlý paket, odmítnutí délky, probuzení `signalStop`, `detach` a omezené čekání při plné
  frontě (to netns testy nepokrývají, README to uvádí).

### B. Android politika důvěry v `paths_posix` (bod 13)

- `openTrustedDirectory`, `secureServiceDirectory`, `secureServiceDirectories` mají nový poslední parametr
  `const std::filesystem::path& trustRoot = {}` a `PosixConfigFilePolicy` nový poslední člen
  `std::filesystem::path trustRoot{}`, který `createPosixConfigFiles` předává do obou volání
  `openTrustedDirectory`.
- Prázdný `trustRoot` (výchozí hodnota, jediná, kterou předávají služby Linuxu a macOS): procházení od `/`
  přesně jako dosud. Linux a macOS chování se nemění, existující volání se nemění.
- Neprázdný `trustRoot`: musí být absolutní, lexikálně normální, ne `/`, bez koncového lomítka, a `directory`
  musí být stejný nebo pod ním (porovnání po komponentách, `/data/app2` není pod `/data/app`), jinak
  `invalid_argument`. Kotva se otevře tak, jak je zadaná: odkazy mezi jejími předky se následují (jsou mimo
  model důvěry, na Androidu `/data/user/0 -> /data/data`), její poslední komponenta odkaz být nesmí. Kotva musí
  být adresář s vlastníkem `expectedOwner`, do kterého ostatní nesmí psát; skupinová práva se nekontrolují,
  protože Android zakládá adresáře aplikace `0771` se soukromou skupinou aplikace. Komponenty pod kotvou se
  kontrolují stejnými pravidly jako dosud (vlastník root nebo `expectedOwner`, bez zápisu skupiny a ostatních,
  pokud není sticky bit, žádný odkaz).
- `#if defined(__ANDROID__)`: prázdný `trustRoot` vrátí `invalid_argument` („the trusted walk needs the
  private application directory“). Procházení od `/` by na Androidu stejně selhalo na `/data` (vlastník
  `system`) a chybějící kotva je chyba hosta, ne nebezpečný adresář.
- `secureServiceDirectory` s kotvou vytváří a opravuje jen adresáře ostře pod kotvou; kotvu samotnou nikdy.
- Nový test `tests/linux/unit/test_paths_trust_root.cpp` (4 případy na Linuxu, 5 na Androidu) simuluje
  androidí strom dočasným adresářem: předek kotvy zapisovatelný pro všechny bez sticky bitu zastupuje
  systémové `/data`. Ověřuje, že walk od `/` takový strom odmítne a kotvený přijme, vytvoření adresářů služby
  s přesnými módy, publikaci `ConfigFiles` nad kotvou, zachování všech pravidel pod kotvou, vlastnictví
  a mód kotvy, odkaz jako kotvu i odkaz nad kotvou, neplatné kotvy a adresáře mimo kotvu. Linux sémantika bez
  kotvy zůstává pokrytá stávajícím `test_config_files_linux.cpp`.
- `test_config_files_linux.cpp` na Androidu kotví walk na kanonickém `TMPDIR` (`trustRoot()`), jinde předává
  prázdnou kotvu, takže na Linuxu a macOS testuje totéž co dřív.

### API pro WP04 (`apps/android-jni/android_host.cpp`)

```cpp
// Kotva = Context.getNoBackupFilesDir() (UTF-8 bajty z Kotlinu), vlastník = getuid().
const std::filesystem::path appRoot = noBackupFilesDir;          // např. /data/user/0/<pkg>/no_backup
const uid_t self = ::getuid();

PosixDirectoryOverrides overrides{appRoot / "phpvpn" / "config", appRoot / "phpvpn" / "state",
                                  appRoot / "phpvpn" / "run", appRoot / "phpvpn" / "logs"};
auto directories = resolvePosixDirectories(overrides);           // všechny čtyři adresáře přepsané
PHPVPN_TRY(secureServiceDirectory(appRoot / "phpvpn", 0700, self, appRoot));
PHPVPN_TRY(secureServiceDirectories(directories.value(), self, appRoot));
PHPVPN_TRY(secureServiceDirectory(directories->logs, kPosixLogDirectoryMode, self, appRoot));
auto files = createPosixConfigFiles(configFilePath(directories.value()),
                                    PosixConfigFilePolicy{self, {}, appRoot});
```

- Adresář konfigurace musí ležet pod kotvou (ne přímo `noBackupFilesDir`, ten má `0771`, a
  `ConfigFiles` vyžaduje u svého adresáře vlastníka a žádný zápis skupiny). `secureServiceDirectory` vytváří
  vždy jen poslední komponentu, proto nejdřív `phpvpn`, potom podadresáře.
- Při agregátní inicializaci `PosixConfigFilePolicy` uvádět všechny tři členy (`{uid, hook, trustRoot}`),
  aby `-Wmissing-field-initializers` nehlásil vynechaný člen.

### C. Nativní testy na emulátoru (T-N01)

- `client/tests/CMakeLists.txt`: na Androidu se `PHPVPN_PORTABLE_CRYPTO_TEST_SOURCES` přidají jen při
  nastaveném `PHPVPN_ANDROID_DEPS` (OpenSSL provider z WP01); bez něj se kryptografická podmnožina vynechá
  a `phpvpn-core-tests` linkuje jen `phpvpn-core`. Nová větev `PHPVPN_PLATFORM STREQUAL "android"` přidá
  `phpvpn-android-posix-tests` (POSIX případy pro diagnostiku jako shell uživatel) a vrátí se před Linux větví.
- `client/tools/android/run-native-tests.ps1` (Windows PowerShell 5.1): konfigurace hostitelským CMake 4.4.2,
  Ninja 1.13.2 a NDK r28c (`android.toolchain.cmake`, `RelWithDebInfo`, `ANDROID_STL=c++_static`,
  `PHPVPN_BUILD_TRAY=OFF`, `BUILD_TESTING=ON`, volitelně `PHPVPN_ANDROID_DEPS`), build, kontrola zařízení
  a ABI, `adb push` do `/data/local/tmp/phpvpn-tests`, spuštění s `TMPDIR` v soukromém podadresáři, návratový
  kód první selhavší sady. Parametry `-Abi` (`x86_64`, `arm64-v8a`), `-Serial` (výchozí `emulator-5554`),
  `-Filter` (podřetězec `suite.name`, jen bezpečné znaky), `-Tests`, `-ApiLevel`, `-BuildDir`,
  `-AndroidDeps`, `-SkipBuild`. Výchozí sady: `phpvpn-core-tests` a `phpvpn-logger-tests`.
- Přenosné testy prošly na bionic bez úprav; žádná oprava pro bionic nebyla potřeba.

### D. T-N03 v sandboxu aplikace

- `client/apps/android-jni/native_tests_jni.cpp` a cíl `phpvpn-android-tests` (SHARED), jen při
  `-DPHPVPN_ANDROID_NATIVE_TESTS=ON`. Jedna JNI metoda `NativeTestLibrary.nativeRun(directory: ByteArray,
  filter: ByteArray): ByteArray` (registrovaná v `JNI_OnLoad`) nastaví `TMPDIR`, přesměruje `std::cout`,
  spustí harness a vrátí jeho výstup jako UTF-8 s posledním řádkem `exit <status>`.
- Zdroje sdílí `client/tests/android/posix_tests.cmake` se spustitelným souborem z bodu C:
  `test_config_files_linux.cpp`, `test_paths_trust_root.cpp`, `test_fd_tunnel.cpp` a nová sonda
  `client/tests/android/test_posix_syscalls.cpp`, která volá každé systémové volání `config_files_posix`
  a `durable_file_posix` zvlášť a tiskne přesné `errno`.
- `tests/unit/harness.cpp`: `main` se nepřekládá při `PHPVPN_TEST_HARNESS_NO_MAIN` (knihovna nemá `main`).
- Bez WP01 chybí `createPlatformCryptoProvider`. `client/tests/android/test_crypto_provider.cpp` je testovací
  provider (SHA-256 podle FIPS 180-4, HMAC-SHA-256, `getrandom`, AES-GCM vrací `not_supported`), přidává se
  jen do testovacích cílů a jen bez `PHPVPN_ANDROID_DEPS`. Konfigurační soubory potřebují jen SHA-256
  a náhodné UUID; test „abc“ ověřuje známý otisk.
- `client/android/bridge/build.gradle.kts`: build type `debug` přidává `-DPHPVPN_ANDROID_NATIVE_TESTS=ON`
  a cíl `phpvpn-android-tests`. Release cíl vůbec nekonfiguruje.
- `client/android/bridge/src/androidTest/kotlin/phpvpn/android/bridge/PosixNativeTest.kt`: ověří uid aplikace
  (≥ 10000) a SELinux doménu `untrusted_app` z `/proc/self/attr/current`, spustí nativní případy v
  `noBackupFilesDir/native-tests`, každý řádek výstupu zapíše do logcat (`phpvpn-native-tests`) a vyžaduje
  souhrn `N test(s) run, 0 failed.` a `exit 0`.
- `libphpvpn-android.so` žádný testovací kód neobsahuje (release i debug 4,7 až 5,5 kB, jen `jni_entry.cpp`).
  Debug AAR modulu `bridge` ale nese i `libphpvpn-android-tests.so` (2,4 MB na ABI), takže ho obsahuje i
  `app-debug.apk`; `app-release-unsigned.apk` ho neobsahuje (ověřeno `unzip -l`).

### Další opravy

- `client/tests/unit/test_server_version.cpp:57`: GCC 13 s `-Werror=range-loop-construct` odmítl kopii
  `const std::string` ve smyčce přes inicializační seznam; Linux build z commitu 12e3ac1 (a tedy i `main`
  a `android` před WP02) se nepřeložil. Proměnná je teď `const std::string&`, test se nemění.

## Příkazy

Android (Windows host, PowerShell, kořen worktree):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File client\tools\android\run-native-tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File client\tools\android\run-native-tests.ps1 -Serial emulator-5556 -SkipBuild
powershell -NoProfile -ExecutionPolicy Bypass -File client\tools\android\run-native-tests.ps1 -Tests phpvpn-android-posix-tests
$env:JAVA_HOME = 'D:\androidstudio\jbr'; cd client\android
.\gradlew.bat :bridge:connectedDebugAndroidTest
.\gradlew.bat assembleDebug assembleRelease
```

Skript interně: `cmake -S client -B work/build/android-tests-x86_64 -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
-DCMAKE_TOOLCHAIN_FILE=<NDK>/build/cmake/android.toolchain.cmake -DANDROID_ABI=x86_64
-DANDROID_PLATFORM=android-34 -DANDROID_STL=c++_static -DPHPVPN_BUILD_TRAY=OFF -DBUILD_TESTING=ON
-DPHPVPN_ANDROID_DEPS=`, `cmake --build ... --target phpvpn-core-tests phpvpn-logger-tests`,
`adb -s emulator-5554 push ...`, `adb shell "cd /data/local/tmp/phpvpn-tests && TMPDIR=/data/local/tmp/phpvpn-tests/tmp ./<test>"`.

Linux (WSL Ubuntu 24.04, kernel 6.18.54.1-microsoft-standard-WSL2, GCC 13.3.0, CMake 3.28.3, Ninja; strom
zkopírovaný `rsync` do `~/phpvpn-wp02`, GTK a ayatana-appindicator dev balíčky nainstalované, tray se
překládá), přesně podle `.github/workflows/client-linux.yml`:

```sh
cd client
cmake --preset linux-release -DPHPVPN_BUILD_PRIVILEGED_TESTS=OFF
cmake --build --preset linux-release
ctest --test-dir ../work/build/linux-release --output-on-failure -L "portable|linux"
ctest --test-dir ../work/build/linux-release --output-on-failure -R "phpvpn-logger-tests"
# WSL běží jako root, CI runner ne: stejné binárky ještě jednou jako nobody
setpriv --reuid=65534 --regid=65534 --clear-groups env TMPDIR=/tmp ../work/build/linux-release/tests/<test>
# netns testy (root, vlastní build bez tray)
cmake --preset linux-release -B ../work/build/linux-netns -DPHPVPN_BUILD_PRIVILEGED_TESTS=ON -DPHPVPN_BUILD_TRAY=OFF
cmake --build ../work/build/linux-netns --target phpvpn-netns-tests
sh tests/linux/netns/run-netns-tests.sh ../work/build/linux-netns
```

## Výsledky

| Test | Výsledek | Poznámka |
|---|---|---|
| Linux build před WP02 (`android` 2c0689c) | FAIL | `test_server_version.cpp:57` `-Werror=range-loop-construct` (GCC 13.3); opraveno v c841664 |
| Linux build s WP02 (`linux-release`, `-Werror`, tray zapnutý) | PASS | GCC 13.3.0, bez varování |
| `ctest -L "portable\|linux"` | PASS | 2/2: `phpvpn-core-tests` 303 případů, `phpvpn-linux-tests` 138 případů (včetně 5 `FdTunnel` a 4 `paths_trust_root`), v WSL jako root |
| Totéž jako `nobody` (uid 65534, jako CI runner bez rootu) | PASS | 303/0, 138/0, logger 2/0 |
| `ctest -R phpvpn-logger-tests` | PASS | 2 případy |
| netns testy (`phpvpn-netns-tests`, root v `unshare --net --mount`) | PASS | 12/12 v WSL2; TUN, dummy, adresy, routy, ping, oversized, stop, recovery po `kill -9`, celý `TunnelSession` nad `FdTunnel` |
| macOS build (Apple clang) | NOT RUN | není macOS host ani push do CI; jen inspekce: macOS soubory beze změny, `paths_posix` a `config_files_posix` bez nových varování na GCC i clang 19 |
| T-N01 `phpvpn-core-tests` na `phpvpn-api34` (x86_64, Android 14) | PASS | 214 případů, 0 selhání (bez kryptografické podmnožiny) |
| T-N01 `phpvpn-logger-tests` na `phpvpn-api34` | PASS | 2/0 |
| T-N01 totéž na `phpvpn-api36` (`emulator-5556`, Android 16) | PASS | 214/0 a 2/0, binárky pro `android-34` |
| T-N01 kryptografická podmnožina (`PHPVPN_PORTABLE_CRYPTO_TEST_SOURCES`) | NOT RUN | chybí OpenSSL z WP01 (`PHPVPN_ANDROID_DEPS`), háček v `tests/CMakeLists.txt` je připravený |
| POSIX případy jako shell uživatel (`-Tests phpvpn-android-posix-tests`, diagnostika) | FAIL (očekávané) | 36/9: SELinux doména `shell` nesmí `link` ani `create fifo_file` na `shell_data_file`; nejde o T-N03 |
| NDK build `libphpvpn-android-tests.so` (arm64-v8a, x86_64, clang 19.0.1, `-Werror`) | PASS | Gradle `buildCMakeDebug[...][phpvpn-android,phpvpn-android-tests]` |
| `gradlew assembleDebug assembleRelease` | PASS | release APK jen `libphpvpn-android.so` |
| **T-N03** `:bridge:connectedDebugAndroidTest` na `phpvpn-api34` | **FAIL** | doména `u:r:untrusted_app:s0:c193,...`, uid 10193; 36 případů, 27 prošlo, 9 selhalo, vše kvůli `linkat` (EACCES), viz níže |
| **T-N03** totéž na `phpvpn-api36` | **FAIL** | uid 10217, `untrusted_app`; stejných 9 selhání |
| `FdTunnel` v sandboxu aplikace (API 34, 36) | PASS | 5/5 |
| Politika důvěry: Linux (`paths_trust_root`, `paths_linux`) | PASS | 4 + 3 případy |
| Politika důvěry: Android v sandboxu (API 34, 36) | PASS | 5/5 `paths_trust_root` včetně `android_never_walks_from_the_file_system_root`, 3/3 `paths_linux` s kotvou |
| `config_files_linux` v sandboxu bez původního souboru (factory, limity, růst, `createIfMissing`, souběh, recovery bez zálohy, journal) | PASS | 12 z 20 případů `config_files_linux` (8 selhání viz níže), `durable_file_linux` 2/2 |
| CI `client-android.yml`, `client-linux.yml` | NOT RUN | bez pushe |

## T-N03: SELinux v sandboxu aplikace

Stav před follow-upem; vyřešeno strategií `exchange`, viz [Follow-up](#follow-up-záloha-bez-hardlinku-strategie-exchange).

Sonda `android_posix.config_file_system_calls_are_permitted` v `noBackupFilesDir` jako `untrusted_app`
(API 34 i 36, ext4 `0xef53`):

| Volání (`config_files_posix`) | Výsledek |
|---|---|
| `open(O_DIRECTORY\|O_NOFOLLOW)`, `fstatfs`, `fstat`, `fstatat(AT_SYMLINK_NOFOLLOW)` | ok |
| `openat(O_CREAT\|O_EXCL\|O_NOFOLLOW)`, `fchmod 0600`, `write`, `lseek`, `fcntl` | ok |
| `fsync` souboru, **`fsync` adresáře** | ok |
| **`renameat2(RENAME_NOREPLACE)`** | ok, na existující jméno `EEXIST` (17) |
| `renameat` (nahrazení), `unlinkat`, `mkdirat`, `symlinkat`, `mkfifoat` | ok |
| `renameat2(RENAME_EXCHANGE)` (kandidát náhrady) | ok, inody se prohodí |
| **`linkat(dir, "config.json", dir, "config.previous", 0)`** | **`EACCES` (13)** |

Logcat (API 34):

```
avc: denied { link } for name="stage" dev="dm-40" ino=132354
  scontext=u:r:untrusted_app:s0:c193,c256,c512,c768
  tcontext=u:object_r:app_data_file:s0:c193,c256,c512,c768 tclass=file permissive=0
```

Politika Androidu zakazuje aplikacím hardlinky v jejich datech. Publikace přes existující `config.json`
(režim `replace` s původním souborem, tedy každé uložení konfigurace po prvním) proto skončí
`config_write_failed` ve `failBeforePublish`, původní soubor zůstane nedotčený. Selhávající případy:
`bounded_read_exact_hash_and_replace_create_only`, `repeated_commit_is_idempotent_and_never_replayed`,
`wrong_directory_mode_or_moved_directory_blocks_publication`, `injected_prepublication_failures_preserve_original`
(stav `backupLinked` se nedosáhne), `crash_between_every_step_recovers_consistently`,
`confirmed_receipt_survives_external_change_and_allows_later_save`, `config_store_over_the_linux_backend`,
`unsafe_targets_are_unreadable_and_never_written_through` (fixture `::link` pro cizí hardlink, `EACCES`) a sonda.
Kód `config_files_posix` jsem podle zadání neoslaboval.

## Návrh náhradní cesty pro Android (`config_files_posix`)

Původní návrh; implementace se od něj liší výběrem přes politiku místo `#ifdef`, samostatným stavem
`backupMoved` a `RENAME_NOREPLACE` při přesunu do zálohy (viz Follow-up).

Doporučení: **záloha výměnou jmen místo hardlinku**, jen v `#if defined(__ANDROID__)`, bez změny formátu
journalu `PHPVPN-CONFIG-1` a bez změny Linux a macOS cesty.

1. Do `stageVerified` beze změny (journal, `fsync` journalu i adresáře, stage, ověření identity a hashe).
2. Místo `linkat(target → config.previous)` a `renameat(stage → target)`: po revalidaci, že `config.json`
   má identitu `record.original`, `renameat2(dir, stage, dir, "config.json", RENAME_EXCHANGE)`. Jedna atomická
   operace: `config.json` je publikovaná stage (bod publikace), jméno stage drží původní inode.
3. `renameat(dir, stage, dir, "config.previous")` přesune původní inode do zálohy (nahradí starou zálohu) a
   `fsync` adresáře. Záloha má přesně identitu `record.original` jako dnes po `linkat`.
4. Recovery dostane jeden nový mezistav: `config.json` = `record.staged`, jméno stage = `record.original`.
   Dokončí se přesunem na `config.previous` (s kontrolou identity) a potvrzením, stejně jako `afterPublish`.
5. Pořadí háčků se na Androidu změní (`backupLinked` až po publikaci), takže testy budou mít pro Android
   vlastní očekávání pořadí. Fixture s cizím hardlinkem v `unsafe_targets...` je na Androidu nemožná i pro
   útočníka (stejná politika), test ji tam vynechá jako NOT RUN.

`RENAME_EXCHANGE` sonda ověřila v sandboxu na API 34 i 36. Zamítnutá alternativa: záloha kopií (nový inode,
recovery by porovnávala hash místo identity, větší zásah do protokolu). Rozhodnutí a implementace patří do
navazujícího kroku (WP02 follow-up nebo WP03), protože jde o sdílený soubor Linuxu a macOS a musí projít
`client-linux.yml` i `client-macos.yml`.

## Odchylky od plánu

- Hotovo-kritérium WP02 nebylo v první iteraci splněné (T-N03 na `linkat`); follow-up ho splnil.
- Testovací SHA-256 provider místo OpenSSL v T-N03 a v `phpvpn-android-posix-tests`, dokud WP01 nedodá
  `PHPVPN_ANDROID_DEPS`; s ním se automaticky použije skutečný provider.
- `run-native-tests.ps1` spouští výchozí jen T-N01 sady; POSIX případy jako shell uživatel jen na vyžádání.
- `app-debug.apk` obsahuje `libphpvpn-android-tests.so` z debug varianty `bridge`. Pokud to vadí, stačí v `app`
  `packaging { jniLibs { excludes += "**/libphpvpn-android-tests.so" } }` pro debug (modul `app` jsem neměnil).
- `test_paths_trust_root.cpp` není v macOS sadě (`phpvpn-macos-tests`), protože ji nelze ověřit bez macOS.
- Gradle spustil instrumentovaný test na obou připojených emulátorech (API 34 a 36, druhý spustil jiný agent).
- Mimo plán opravený `test_server_version.cpp` (rozbitý Linux build na GCC 13).

## Follow-up: záloha bez hardlinku (strategie `exchange`)

Na pokyn koordinátora po sloučení WP01 a WP02 do `android` (f21112d, 64d6d26 sloučeno do větve WP02).
Předsestavené OpenSSL 3.5.9 a curl 8.23.0 z WP01: `D:/git/phpvpn/work/android-deps/openssl-3.5.9-curl-8.23.0`
(`phpvpn.androidDeps` v `client/android/local.properties`, `-AndroidDeps` u `run-native-tests.ps1`).

### Změna

- `PosixConfigFilePolicy::backupStrategy` typu `PosixConfigBackupStrategy { hardLink, exchange }`, výchozí
  `kDefaultPosixConfigBackupStrategy`: `hardLink` na Linuxu a macOS, `exchange` na Androidu. Výběr je
  politikou, ne jen `#ifdef`, takže Linux testuje obě strategie. WP04 předává `exchange` explicitně:
  `PosixConfigFilePolicy{getuid(), {}, noBackupFilesDir, PosixConfigBackupStrategy::exchange}`.
- `hardLink` beze změny chování: stejné kroky, háčky, stavy journalu a recovery.
- `exchange` (jen když existuje původní `config.json`, jinak je publikace shodná s `hardLink`):
  1. Až po `stageVerified` beze změny (journal, `fsync` journalu i adresáře, stage, ověření hashe).
  2. `beforePublish`; revalidace: `config.json` musí být bezpečný a mít identitu `record.original`
     a `config.previous` nesmí existovat, jinak `failBeforePublish` (nic se nepublikovalo, stage a journal
     se uklidí, výsledek `config_write_failed`). Cíl vyměněný během ukládání se tak nikdy nepřepíše.
  3. `renameat2(stage, config.json, RENAME_EXCHANGE)`: bod publikace, `afterPublish`, `fsync` adresáře,
     `afterDirectorySync`.
  4. Kontrola, že jméno stage drží `record.original`, `renameat2(stage, config.previous, RENAME_NOREPLACE)`,
     `fsync` adresáře, nový háček `backupMoved`.
  5. Ověření identity `config.json`, `beforeConfirm`, `CONFIRMED` do journalu, stejně jako `hardLink`.
- macOS nemá `RENAME_EXCHANGE`: `createPosixConfigFiles` vrátí pro `exchange` `not_supported` („the exchange
  backup strategy needs RENAME_EXCHANGE, macOS has none“), volání swapu je tam pahýl s `ENOTSUP`. Výchozí
  macOS cesta a build se nemění.
- Hlavička `config_files_posix.h` popisuje obě strategie a nový mezistav, recovery kód ho komentuje.

### Journal a recovery

Záznam journalu se nemění (`PHPVPN-CONFIG-1`, identity `staged` a `original`, případně `CONFIRMED`); žádný
příznak strategie se nezapisuje, protože identity inodů rozliší každý mezistav. Recovery proto nezávisí na
nastavené strategii (test obnoví stav po pádu strategie `exchange` instancí s `hardLink`).

| Stav na disku po pádu (`exchange`) | Recovery |
|---|---|
| `config.json` = original, stage = staged (před swapem) | `notPublished`, úklid stage a journalu, jako `hardLink` |
| **nový mezistav**: `config.json` = staged, jméno stage = original, `config.previous` chybí (pád v `afterPublish` nebo `afterDirectorySync`) | `confirmed` (identita a hash cíle); před úklidem dokončí přesun `renameat2(stage, config.previous, RENAME_NOREPLACE)` + `fsync` adresáře, pak běžný úklid (`config.previous` s identitou original, journal). Opakovatelné: po přesunu jméno stage original nedrží. Jméno v cestě nechá journal další recovery, jako každý nedokončený úklid. |
| `config.json` = staged, `config.previous` = original (pád v `backupMoved`, `beforeConfirm`) | `confirmed`, stejný stav jako po publikaci `hardLink` |

Záruky `client.md` zůstávají: poslední úspěšné uložení vyhrává (další uložení po odmítnutém přepíše externí
změnu a ta je pak v `config.previous`), žádné tiché přepsání (swap běží jen nad journalovaným originálem),
recovery je idempotentní (opakovaný pád v `beforeRecoveryCleanup` i dvojí recovery ověřeny testem).

### Testy

- `test_config_files_linux.cpp`: každý scénář backendu (19) se registruje makrem `PHPVPN_CONFIG_FILES_TEST`
  pro každou strategii platformy: `config_files_linux.*` s `hardLink` (Linux, macOS) a `config_files_exchange.*`
  s `exchange` (Linux, Android). Rozdíly v očekávání: `backupLinked` je jen `hardLink`, `backupMoved` jen
  `exchange` a patří k publikovaným stavům; na Androidu fixture cizího hardlinku je NOT RUN.
- Nové testy `exchange`: původní inode jako záloha bez linku (`nlink` 1), recovery mezistavu po pádu v
  `afterPublish` i `afterDirectorySync` včetně pádu v `beforeRecoveryCleanup` a recovery instancí `hardLink`,
  selhání po swapu (`afterPublish`, `afterDirectorySync`, `backupMoved`, `beforeConfirm`) a cíl vyměněný během
  ukládání (původní soubor se přesune, ne smaže, aby se číslo inodu nerecyklovalo). Plus výchozí strategie
  podle platformy a na macOS odmítnutí `exchange` (jen inspekcí).
- Sonda `android_posix` už `linkat` a `link` jen hlásí (obě `EACCES` v sandboxu), vyžaduje `RENAME_EXCHANGE`.

### Výsledky follow-upu

| Test | Výsledek | Poznámka |
|---|---|---|
| Linux build (WSL, GCC 13.3, `linux-release`, `-Werror`, tray zapnutý) | PASS | bez varování |
| `ctest -L "portable\|linux"` | PASS | `phpvpn-core-tests` 303, `phpvpn-linux-tests` 162 (dříve 138: +19 variant `config_files_exchange`, +4 testy `exchange`, +1 výchozí strategie) |
| Totéž jako `nobody` | PASS | 303/0, 162/0, logger 2/0 |
| `ctest -R phpvpn-logger-tests` | PASS | 2 |
| T-N01 s OpenSSL z WP01 na `phpvpn-api34` (`emulator-5554`) | PASS | `phpvpn-core-tests` 303/0 včetně kryptografické podmnožiny, logger 2/0 |
| T-N01 s OpenSSL z WP01 na `phpvpn-api36` (`emulator-5556`) | PASS | 303/0, logger 2/0 |
| **T-N03** `:bridge:connectedDebugAndroidTest`, `ANDROID_SERIAL=emulator-5554` | **PASS** | uid 10195, `u:r:untrusted_app`; 41 nativních případů, 0 selhání (23 `config_files_exchange`, 5 `paths_trust_root`, 5 `FdTunnel`, 3 `paths_linux`, 2 `durable_file_linux`, sonda, výchozí strategie, statfs politika); skutečný OpenSSL provider |
| **T-N03** totéž, `ANDROID_SERIAL=emulator-5556` | **PASS** | uid 10220, `untrusted_app`; 41/0 |
| T-N03 dílčí případ: podvržený hardlink v `unsafe_targets...` | NOT RUN | aplikace hardlink vytvořit nemůže (SELinux), útočník v sandboxu také ne |
| T-N03 dílčí případ: cizí vlastník souboru | NOT RUN | `chown` vyžaduje root, pokryto politikou očekávaného vlastníka |
| `gradlew assembleDebug assembleRelease` s `phpvpn.androidDeps` | PASS | |
| netns testy | NOT RUN | follow-up nemění tunel ani síť; poslední běh 12/12 výše |
| macOS (Apple clang, `phpvpn-macos-tests`) | NOT RUN | není macOS host; inspekcí: výchozí `hardLink`, `exchange` odmítnut ve factory, pahýl swapu bez `renameat2` |

Příkazy follow-upu:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File client\tools\android\run-native-tests.ps1 `
    -AndroidDeps D:/git/phpvpn/work/android-deps/openssl-3.5.9-curl-8.23.0 -BuildDir work\build\android-tests-deps-x86_64
powershell -NoProfile -ExecutionPolicy Bypass -File client\tools\android\run-native-tests.ps1 `
    -Serial emulator-5556 -SkipBuild -BuildDir work\build\android-tests-deps-x86_64
$env:JAVA_HOME = 'D:\androidstudio\jbr'; cd client\android
$env:ANDROID_SERIAL = 'emulator-5554'; .\gradlew.bat :bridge:connectedDebugAndroidTest
$env:ANDROID_SERIAL = 'emulator-5556'; .\gradlew.bat :bridge:connectedDebugAndroidTest
```

Linux beze změny příkazů (sekce Příkazy). `ANDROID_SERIAL` omezil instrumentovaný test na jeden emulátor,
protože třetí emulátor (`emulator-5558`) patřil jinému agentovi.
