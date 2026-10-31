# WP04: nativní runtime, IPC v procesu a JNI most, 2026-10-31

Výsledek kroku 4 podle [plan-android-v001.md](../../../Plans/2026-10-31-09-40-08/plan-android-v001.md)
(body 8, 12, 13, 17 a 18, „Nativní část“, „Kotlin část“, krok 7 životního cyklu). Co se nespustilo, je NOT RUN
s důvodem. Žádný výsledek z emulátoru se nevydává za výsledek z reálného zařízení.

Větev `worktree-agent-a647a6e03f4fd9684`, založená resetem na `android` (06186fd, WP00 až WP03, WP06, WP07)
a před dokončením sloučená s `android` a3e23e5 (navíc WP08 a WP10). Commity: `b761882` (nativní část),
`cb4abe0` (Kotlin a aplikace), `59be900` (testy), `b4890c1` (merge `android`, napojení na rozhraní WP08)
a commit s tímto reportem.
Sdílené soubory (`client/src`, `client/tests`, CMake mimo `apps/android-jni`) se nemění, proto Linux a Windows
kontrola nebyla potřeba (viz NOT RUN).

## Co vzniklo

### A. `android_host.{h,cpp}`: `ServicePlatformHost` pro Android

```
<noBackupFilesDir>/phpvpn/            0700, vlastník getuid()
                  config/config.json  ConfigFiles (createPosixConfigFiles, strategie exchange)
                  config/config.previous, config.publish-journal (CONFIRMED záznam zůstává)
                  state/ownership.json journal adaptéru
                  run/                 nepoužité (na Androidu není socket)
                  logs/phpvpn-service.log  8 MiB × 5 historických souborů
```

- Kotva důvěry je `noBackupFilesDir` (`trustRoot` u `secureServiceDirectory` i `PosixConfigFilePolicy`),
  vlastník `getuid()`, politika `PosixConfigFilePolicy{getuid(), {}, noBackupFilesDir, PosixConfigBackupStrategy::exchange}`.
- Všechny čtyři podadresáře mají 0700. `secureServiceDirectories` se nepoužívá, protože by `run` nastavil
  na 0755; každý adresář jde přes `secureServiceDirectory` zvlášť (nejdřív `phpvpn`, pak podadresáře).
- Rotace: výchozí hodnoty `Logger::configureFile` (8 MiB, 5 souborů), tedy shodně s desktopem; v loggeru se
  nic neliší. Soubor se konfiguruje jen jednou za proces (`Logger::configureFile` neumí znovu otevřít otevřený
  stream), takže `nativeStop` a `nativeStart` ve stejném procesu dál píšou do stejného souboru.
- logcat sink (`installLogcatSink`) se nastaví hned na začátku `nativeStart`, takže i chyba úložiště je
  v logcatu. Do souboru se zapisuje, jen když je úložiště v pořádku.
- Chyba úložiště (odkaz místo adresáře, cizí vlastník, nečitelná cesta) runtime nezastaví:
  `ServiceStorage.storageError`, IPC běží a hlásí `config_directory_security_invalid` (ověřeno testem).
- `installDirectory` prázdné, adaptér `phpvpn`, GUID `{c2e5a8d1-7b4f-4e2a-8d3c-6f1b9e0a4d27}`.

### B. `inprocess_ipc_server.{h,cpp}`: `IpcServer` v procesu (bod 8)

- `InProcessIpcEndpoint` drží `IpcExecutors` (shortControl 16 čekajících, file 4, diagnostic 1 celkem),
  klasifikátor a busy odpověď runtime (`classifyServiceIpcRequest`, `serviceIpcBusyResponse`), tedy tytéž
  jako desktop. `InProcessIpcServer` je fasáda vlastněná `ServiceRuntime`; JNI drží vlastní `shared_ptr`
  na endpoint, takže dispatch souběžný se `nativeStop` nikdy nesáhne na zrušený objekt.
- `submit(request) -> shared_ptr<IpcJob>`: null = odmítnuto bez odpovědi (neběží, prázdné, nad 4 MiB, nelze
  klasifikovat), což odpovídá zavřenému spojení na desktopu. Busy odpověď je hotový job s `done`.
- `wait(job, timeout)`: čeká na `done` přes jednu podmínkovou proměnnou, kterou budí `onJobDone` executorů.
  Timeout vrátí „bez odpovědi“, job doběhne na svém executoru a nikdo na něj nečeká; další `submit` neblokuje.
  Job při uvolnění posledním vlastníkem (čekající volající nebo executor) přepíše požadavek i odpověď
  `secureWipe` (set-config a get-config nesou tajemství).
- `onTick` na vlastním vlákně: první hned, pak každých 250 ms, výjimky se polykají (jako `ipc_unix.cpp`).
- Vypnutí ve stejném pořadí jako `IpcSocketServer::stopLocked`: zavřít příjem, ukončit tick vlákno,
  `shutdown(cancelHandlers)` jednou, `join` ticku, `join` executorů (dokončí přijaté joby i úlohy), probudit
  čekající. Restart endpointu není povolen, runtime staví při každém startu nový.
- Statistiky: `connectionSlots` = počet právě čekajících volajících, `ioTimeouts` = vypršené čekání,
  zbytek z `IpcExecutors::fillStats` (fronty, `rejected` včetně odmítnutých nad 4 MiB).
- Žádný socket, pipe ani jméno, které by mohl otevřít jiný proces.

### C. `jni_entry.cpp` (registrace jen přes `RegisterNatives`, export jen `JNI_OnLoad`)

`phpvpn.android.bridge.NativeBridge`:

| Kotlin | JNI podpis | Chování |
|---|---|---|
| `nativeVersion(): ByteArray` | `()[B` | beze změny |
| `nativeStart(noBackupFilesDir: ByteArray, packageName: ByteArray): Int` | `([B[B)I` | UTF-8 argumenty, absolutní lexikálně normální cesta bez koncového `/`, balíček `[A-Za-z0-9._]{1,255}`. Vytvoří `AndroidVpnContext` (`Options.packageName` = vyloučená aplikace), `attachRegisteredVpnHosts`, `install`, `ServiceRuntime::start`. **Nikdy** nevolá `requestStartupConnect`. Výsledky `START_OK 0`, `START_ALREADY_STARTED 1`, `START_INVALID_ARGUMENT 2`, `START_FAILED 3` (po neúspěchu se vše uklidí, kontext odinstaluje). |
| `nativeStop()` | `()V` | zruší endpoint pro nové dispatch, `ServiceRuntime::stop`, `AndroidVpnContext::uninstall`; idempotentní |
| `nativeDispatch(request: ByteArray, timeoutMs: Int): ByteArray?` | `([BI)[B` | délka se kontroluje před kopií (nad `kIpcMaxMessageBytes` → `countRejected`, null), jen bajty, nikdy `jstring`; null při timeoutu, když runtime neběží a pro požadavek, na který desktop zavírá spojení |
| `nativeStatus(): ByteArray?` | `()[B` | `IpcProtocol::buildStatusResponse` snímku `ServiceRuntime::status()` s pevným `requestId` `NativeBridge.STATUS_REQUEST_ID` = `00000000-0000-4000-8000-0000000005a7`; null, když runtime neběží |

Vlákna: `nativeStart`, `nativeStop` a `nativeStatus` serializuje jeden zámek životního cyklu (`nativeStatus`
během dlouhého `nativeStop` počká). `nativeDispatch` si endpoint vezme pod krátkým vlastním zámkem a blokuje
volající vlákno do odpovědi nebo timeoutu volajícího; smí běžet na libovolném Kotlin vlákně kromě hlavního.
To je jen dokumentované, nativní kód vlákno nekontroluje. Callbacky do Kotlinu (hosté) jsou dál v
`jni_vpn_bridge.cpp` (WP03).

Bez `PHPVPN_ANDROID_DEPS` se konfigurace podaří (testovací spustitelné soubory WP02 je nepotřebují), ale
překlad `phpvpn-android` skončí čitelným `#error` místo nedefinovaného `createPlatformTransport` při linkování.

### D. `jni_models.{h,cpp}` a Kotlin `phpvpn.android.bridge.jni`

Raw rozhraní `internal object ModelNatives` (registrováno v `JNI_OnLoad`, konvence: handle `Long`, IPv4 jako
`Int` v pořadí hostitele, chyba = UTF-8 jméno kódu, null = úspěch, text `ByteArray`, tajemství `CharArray`):

```kotlin
// ContractsModel (samostatný handle nebo zdroj modelu stroje)
contractsCreate(document: CharArray, configPath: ByteArray, revisionToken: ByteArray, error: Array<ByteArray?>): Long  // ([C[B[B[[B)J
contractsDestroy(model: Long)                                          // (J)V
contractsRowAddresses(model: Long): IntArray                           // (J)[I
contractsRowFlags(model: Long): IntArray                               // (J)[I  bit0 enabled, 1 keyValid, 2 dirty, 3 keyErrorVisible
contractsRowKey(model: Long, remoteIp: Int): CharArray?                // (JI)[C
contractsState(model: Long): LongArray?                                // (J)[J  14 hodnot
contractsText(model: Long, which: Int): ByteArray?                     // (JI)[B  0 configPath, 1 revisionToken
contractsSetEnabled(model: Long, remoteIp: Int, enabled: Boolean): ByteArray?   // (JIZ)[B
contractsSetKey(model: Long, remoteIp: Int, key: CharArray): ByteArray?         // (JI[C)[B
contractsSetFocus(model: Long, remoteIp: Int, column: Int): ByteArray?          // (JII)[B  column -1 = žádný
contractsTarget(model: Long, remoteIp: Int): Long                      // (JI)J
contractsTargetContractId(model: Long, target: Long): ByteArray?       // (JJ)[B
contractsTargetRelease(model: Long, target: Long)                      // (JJ)V
contractsConfirmRandom(model: Long, target: Long, confirmed: Boolean): ByteArray?  // (JJZ)[B
contractsApplyPasswordKey(model: Long, target: Long, key: CharArray): ByteArray?   // (JJ[C)[B
contractsResumeEditing(model: Long, saveGeneration: Long): Boolean     // (JJ)Z
// tray::ContractsSaveMachine
machineCreate(): Long; machineDestroy(machine: Long)                   // ()J, (J)V
machineLoad(machine: Long, document: CharArray, configPath: ByteArray, revisionToken: ByteArray): ByteArray?  // (J[C[B[B)[B
machineStage(machine: Long): Int; machineSaving(machine: Long): Boolean; machineHasModel(machine: Long): Boolean
machineModel(machine: Long): Long                                      // (J)J  zdroj modelu, žije se strojem
machineSnapshot(machine: Long, checking: Boolean): LongArray?          // (JZ)[J  10 hodnot
machineChanged(machine: Long); machinePrepare(machine: Long, confirmedRepeat: Boolean, checking: Boolean): ByteArray?
machineDispatch(machine: Long, nowMs: Long, operation: LongArray): CharArray?   // (JJ[J)[C  payload set-config
machineSaveFinished(machine: Long, operation: LongArray, outcome: Int, error: ByteArray, validationDescription: ByteArray): Int  // (J[JI[B[B)I
machineCheck(machine: Long, checking: Boolean): Long                   // (JZ)J  -1 = nic neodesláno
machineCheckFinished(machine: Long, checkGeneration: Long, requestId: ByteArray, response: ByteArray?): Int  // (JJ[B[B)I
machineCanRepeat(machine: Long, checking: Boolean): Boolean; machineNotice(machine: Long): ByteArray; machineClose(machine: Long)
// tray::classifySaveResponse
classifySave(operation: LongArray, requestId: ByteArray, response: ByteArray?, writeAttempted: Boolean, completedAtMs: Long): Array<ByteArray>?  // ([J[B[BZJ)[[B
// PasswordKeyModel
passwordCreate(): Long; passwordDestroy(model: Long); passwordUpdate(model: Long, password: CharArray): Int  // (J[C)I
passwordState(model: Long): IntArray?   // [valid, scalarCount, error]
passwordKey(model: Long): CharArray?; passwordClear(model: Long); passwordRandom(): CharArray?
// ServerInfoModel a stránkování
serverInfoParse(response: ByteArray, error: Array<ByteArray?>): Long   // ([B[[B)J
serverInfoDestroy, serverInfoCounts (J)[J, serverInfoRowAddresses (J)[I, serverInfoRowStates (J)[I,
serverInfoRowLastSeen (J)[J, serverInfoRowContract (JI)[B, serverInfoText (JI)[B, serverInfoStateText (I)[B,
serverInfoPageCount (I)I, serverInfoPageNumbers (II)[I
// documentMatches a kontrola šablony z wizard_ipc.cpp
configDocumentMatches(document: ByteArray, availability: ByteArray): Boolean  // ([B[B)Z
configValidTemplate(document: ByteArray): Boolean                              // ([B)Z
```

Typované obaly (vlastní handle, serializují volání zámkem handle, `close()` explicitně, `Cleaner` jen jako
pojistka pro zapomenutý handle; nativní destruktory tajemství mažou):

| Kotlin | Implementuje | Poznámka |
|---|---|---|
| `NativeContractsModel` | `ContractsModelHandle` (WP06) 1:1 | `create(document, configPath, revisionToken)` vyhodí `NativeModelException(code)`; model stroje vlastní stroj. Klíč řádku je nová `SecretText` kopie. Cizí nebo zastaralý target → `INVALID_ARGUMENT` jako `ContractsModel::accepts`. |
| `NativeContractTarget` | `ContractTargetHandle` | C++ `ContractTarget` (slabá identita, generace) zůstává v registru zdroje modelu, uvolní se s modelem (`load`, `close`). |
| `NativeContractsSaveMachine` | `ContractsSaveMachineHandle` 1:1 | `close()` = C++ `close()` (model a tajemství smazané, stroj jde znovu `load`), `release()` uvolní nativní objekt. Sender stroje je převedený na návratové hodnoty (`PreparedSave`, `checkGeneration`), odpověď kontroly parsuje nativní `WizardIpc::parseReply`. |
| `NativeSaveResponseClassifier` | `SaveResponseClassifier` | `tray::classifySaveResponse`, null odpověď = timeout (unknown). |
| `NativePasswordKeyModel` | – (dialog 21) | `update(CharArray)` předá UTF-16 jednotky beze změny; vstup delší než 510 jednotek je `TOO_LONG` bez kopie. `key()` vrací kopii hex klíče, `randomPassword()` („Random“, port `password_sha_window.cpp`: 64 znaků `ghjkmnpqrstuvwxyz` bez modulo biasu), `defaultPassword(target)` („Default“ = id kontraktu). |
| `NativeServerInfo`, `NativeServerInfoHook` | `ServerInfoHook` (WP06) | `parse(reply)` → `ServerInfoView` (řádky, počty, text počtu, plaintext, stránkování `PAGE_SIZE` 20). Hook nativně odmítnutou odpověď vrátí `false` (repozitář ji bere jako nezdařený refresh, 45) a přijatou publikuje ve `latest: StateFlow<ServerInfoView?>`, chybu v `lastError`. |
| `NativeConfigDocumentCheck` | `ConfigDocumentCheck` | plný validátor; nahrazuje `StructuralConfigDocumentCheck` WP06 a odstraňuje jeho dvě zdokumentované odchylky. |

WP08 swap point (`ui/state/ServerInfoModel.kt`, `fun interface ServerInfoModelSource { fun build(result:
ServerInfoResult): ServerInfoModelResult }`): `PhpvpnApplication` předává `AppContainer(…, serverInfoModel =
NativeServerInfoModel)`, privátní adaptér nad `NativeServerInfo.parse(result: ServerInfoResult)`, který typovaný
výsledek vrátí do obálky `get-server-info` s pevnou korelací a nechá ho klasifikovat nativním
`parseServerInfoResult` + `buildServerInfoModel`; výsledek převede na typy WP08 (`ServerInfoModel`,
`ServerInfoRow`, `ServerClientState`). Kontejner jím dál filtruje `ServerInfoRepository` (neověřený snímek = 45).
`KotlinServerInfoModel` WP08 zůstává pro testy. `NativeServerInfoHook` (bridge) je k dispozici, aplikace ho
po sloučení s WP08 nepotřebuje.

`bridge/consumer-rules.pro`: kromě `NativeBridge`, `VpnHosts`, `NetworkHost`, `TunnelHost` (dříve) nově
`-keep class phpvpn.android.bridge.jni.ModelNatives { *; }` (odstraněná třída by shodila `JNI_OnLoad`).
Modely do Kotlinu nevolají, další pravidla nejsou potřeba.

### E. Kotlin runtime a aplikace

- `NativeRuntime.ensureStarted(context): Int`: procesní, `synchronized`, při prvním volání zaregistruje
  `AndroidNetworkHost(applicationContext)` přes `VpnHosts.registerNetworkHost` a zavolá `nativeStart`
  s `noBackupFilesDir.absolutePath` a `packageName`; po neúspěchu zkusí příští volání znovu. `stop()` pro testy.
- `JniIpcTransport : IpcTransport` (balíček `jni`): `withContext(DISPATCHER) { nativeDispatch(request, timeoutMs) }`,
  `DISPATCHER = Dispatchers.IO.limitedParallelism(8, "phpvpn-ipc")`. Timeout volajícího jde beze změny dál.
  Osm vláken stačí na dlouhý příkaz, dotazování operace Wizardu, diagnostiku a status souběžně, takže 30 s
  Connect neblokuje 400 ms status.
- `PhpvpnApplication` (po sloučení s WP08): `createTransport()` zavolá `NativeRuntime.ensureStarted(this)`
  a vrátí `JniIpcTransport()`; `createContainer()` předá `SystemVpnConsent`, nativní `serverInfoModel` a
  `documentCheck = NativeConfigDocumentCheck`. `AppContainer` má jen nový poslední parametr `documentCheck`
  (výchozí `StructuralConfigDocumentCheck`). Testovací aplikace UI testů (`TestApplication`) přepisuje
  `createTransport()`, takže nativní runtime nikdy nespustí.
  Start runtime na hlavním vlákně trval na API 34 31 ms včetně vytvoření výchozí konfigurace.
- `bridge/src/main/AndroidManifest.xml`: `android.permission.INTERNET` (nativní curl potřebuje sokety uid
  aplikace; manifest `app` patří WP05, sloučí se z knihovny).

### F. CMake a knihovna

- `apps/android-jni/CMakeLists.txt`: nové zdroje, sdílené `apps/service/service_runtime.cpp`,
  `startup_connect.cpp`, `apps/tray/contracts_save_machine.cpp`, `save_response.cpp`.
- `--exclude-libs,ALL` zůstává. Runtime instancuje šablony libc++ s `std::function`, jejichž typeinfo má
  výchozí viditelnost a skončilo v `.dynsym` (16 slabých symbolů `_ZTI…`/`_ZTS…`). Nový version script
  `libphpvpn-android.map` (`global: JNI_OnLoad; local: *;`) to uzavírá.

| Kontrola (release, Gradle `RelWithDebInfo`) | arm64-v8a | x86_64 |
|---|---|---|
| definovaný dynamický symbol (`llvm-nm -D --defined-only`) | jen `JNI_OnLoad` | jen `JNI_OnLoad` |
| curl/OpenSSL v nestripnuté knihovně (`curl_multi_perform`, `curl_easy_init`, `EVP_EncryptInit_ex`, `EVP_DecryptFinal_ex`, `EVP_MAC_init`, `EVP_Digest`, `SSL_CTX_new`, `SSL_CTX_set1_cert_store`, `OSSL_PROVIDER_load`) | všech 9, celkem 2 230 symbolů `curl_`/`Curl_`/`EVP_`/`SSL_` | stejně |
| curl/OpenSSL v `.dynsym` | 0 | 0 |
| LOAD zarovnání (`llvm-readelf -l`) | 4× 0x4000 | 4× 0x4000 |
| NEEDED | `liblog.so libm.so libdl.so libc.so` | stejně |
| velikost v `app-release-unsigned.apk` | 7 750 088 B (dříve 596 576 B) | 8 200 264 B (dříve 593 424 B) |
| `zipalign -c -P 16 4` | PASS (release i debug APK) | |

## Co musí WP05 volat

- `NativeRuntime.ensureStarted(applicationContext)` v `PhpvpnVpnService.onCreate` i v `BootReceiver`
  (Application ho už volá, další volání se hned vrátí).
- `VpnHosts.registerTunnelHost(this)` v `onCreate` služby **před** prvním `connect` a
  `unregisterTunnelHost()` v `onDestroy` (registrace se ihned propíše do běžícího kontextu).
- Notifikace: `NativeBridge.nativeStatus()` mimo hlavní vlákno; odpověď je běžný `get-status`
  s `requestId == NativeBridge.STATUS_REQUEST_ID`, parsuje ji tray port WP06 (`StatusModel`). Rozvrh 5 s / 500 ms
  podle plánu; null = runtime neběží.
- Connect/Disconnect služby přes `AppContainer.ipcClient` nebo `JniIpcTransport`, nikdy `nativeDispatch` na
  hlavním vlákně.
- StartupConnect runtime **nikdy** nezapíná (`requestStartupConnect` se na Androidu nevolá, ověřeno testem
  s `autoConnectOnStartup: true`). `autoConnectOnStartup` vyhodnocuje BootReceiver (krok 7 životního cyklu)
  sám přes `get-config-document`/`get-config-state` a pak posílá `connect`.

## Testy

### Testovací relay

Connect a Wizard potřebují skutečný relay. Použit `server/php/phpvpn.php` v profilu `wizard-default` (stejný
jako kontraktní testy desktopu) na hostiteli, emulátor ho vidí jako `10.0.2.2`:

```powershell
./tools/deploy-test.ps1 -Profile wizard-default -WebRoot "$PWD/work/wp04-php" -BufferDirectory "$PWD/work/wp04-buffer" `
  -BaseUrl http://10.0.2.2:18094/phpvpn.php -LargeBodyBaseUrl http://10.0.2.2:18094/phpvpn.php -ConfigurationPath "$PWD/work/wp04-php-config.json"
D:\nginx\php8.5.9\php.exe -S 127.0.0.1:18094 -t work/wp04-php
python -I proxy.py 18096 18094     # viz Zjištění 2; testy výchozí URL http://10.0.2.2:18096/phpvpn.php
```

`proxy.py` (pomocný skript mimo repozitář, celý obsah podstatného): pro každé spojení přeposílá obě
strany a po konci odpovědi serveru počká 200 ms, než pošle klientovi `shutdown(SHUT_WR)`. Jiná URL přes
argument instrumentace `phpvpnServerUrl`. Když relay není dosažitelný, Connect a Wizard test se přeskočí
(`assumeTrue`), nikdy neprojdou naprázdno.

Příkazy (Git Bash, `JAVA_HOME=D:/androidstudio/jbr`, `client/android/local.properties` se `sdk.dir`
a `phpvpn.androidDeps`):

```sh
./gradlew :bridge:testDebugUnitTest :app:testDebugUnitTest :app:assembleDebug :app:assembleRelease
ANDROID_SERIAL=emulator-5554 ./gradlew :bridge:connectedDebugAndroidTest
ANDROID_SERIAL=emulator-5558 ./gradlew :bridge:connectedDebugAndroidTest
cmake -S client -B work/build/wp04-x86_64 -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake -DANDROID_ABI=x86_64 -DANDROID_PLATFORM=android-34 \
  -DANDROID_STL=c++_static -DPHPVPN_BUILD_TRAY=OFF -DBUILD_TESTING=OFF -DPHPVPN_ANDROID_DEPS=D:/git/phpvpn/work/android-deps/openssl-3.5.9-curl-8.23.0
cmake --build work/build/wp04-x86_64 --target phpvpn-android      # clang r28c, -Werror, bez varování
llvm-nm -D --defined-only libphpvpn-android.so; llvm-readelf -l libphpvpn-android.so; zipalign -c -P 16 4 app-release-unsigned.apk
```

### Nové testy

| Třída (`bridge/src/androidTest`, nové soubory) | Testů | Obsah |
|---|---|---|
| `runtime.IpcDispatchTest` | 9 | get-status proti `status-disconnected.json` (členy i pořadí) a přes klienta WP06; get-config, get-config-document, get-config-state, get-default-config, create-default-config proti fixturám (množina členů) a obsahu souboru; create-default-config vytvoří jednou a nikdy nepřepíše (`created:false` vrátí existující dokument); set-config publikuje se strategií exchange (`config.previous` = předchozí obsah, `st_nlink` 1, 0600, žádná stage), odmítnutý dokument nic nemění; get-diagnostics, get-info, get-server-info (s relay úspěch, nativní `ServerInfoModel` ji přijme), dismiss-connect-diagnostic korelovaně; Connect bez TunnelHost; všech 13 `wizard-*` příkazů proti relay (open, keepalive, check-url, check-secret, check-access s navrženou adresou, finish → get-operation → receipt, get-commit `confirmed`, cancel-operation, cancel-attempt, reset, resume, close); syrové požadavky (neznámý příkaz, verze 2, nevalidní JSON, navíc člen, ne-UUID, duplicitní člen, přesně 4 MiB zpracováno, 4 MiB + 1 odmítnuto bez kopie do 1 s, prázdný požadavek, 32 souběžných korelací); timeout volajícího (300 ms dodrženo), job běží dál, shortControl není blokovaný a diagnostic třída vrací bajt po bajtu `busy-classic.json` |
| `runtime.NativeRuntimeTest` | 6 | rozložení úložiště (0700, vlastník, `config.json` 0600, kotva nezměněná), oprava módu 0755 → 0700, odkaz místo `config` → runtime běží a hlásí `config_directory_security_invalid`, soubor logu a řádky v logcatu bez tajemství, `START_ALREADY_STARTED`, stop s čekajícím požadavkem (probuzení, < 2 ms), dispatch a `nativeStatus` po stopu null, nový start ve stejném procesu, `nativeStatus` korelovaný a se členy `get-status`, StartupConnect se nespustí ani s `autoConnectOnStartup: true`, neplatné argumenty `nativeStart` |
| `models.ContractsModelJniTest` | 9 | port `test_contracts_model.cpp`: 252 řádků a pořadí, hranice /29, neplatné CIDR a adresy, 252 povolených řádků a počítání chyb, existující klíč, validace klíče, dirty a fokus, rozpočet 512 KiB přesně a +1, náhodný klíč, klíč z hesla a cizí target, uzavřený handle |
| `models.ContractsSaveMachineJniTest` | 7 | port `test_contracts_save_machine.cpp`: potvrzené uložení s identitou a deadlinem, odmítnutí a not-sent, unknown → kontrola → repeat, výsledky kontroly (shoda, rozdíl, vadná odpověď, `invalid`, chybová obálka, žádná odpověď), neplatný draft a close; klasifikace na skutečných odpovědích runtime (odmítnutí `config_invalid_cidr` bajt po bajtu); `ContractsController` WP06 nad nativním strojem a runtime až po uložený soubor |
| `models.PasswordKeyJniTest` | 5 | port `test_password_key.cpp`: SHA-256 vektory, 254/255/256 ASCII i emoji, kombinující znaky, 100 000 jednotek `TOO_LONG` s ponechaným klíčem, osamocené surrogáty, řídicí znaky, BOM/NBSP, Random a Default |
| `models.ServerInfoJniTest` | 4 | port `test_server_info_model.cpp` se stejným syntetickým snímkem (60000/59999/60001 ms): stavy, hranice, řazení, stránky (`tablePageNumbers`), schéma IPC, duplicitní kontrakt, navíc člen, `server_version_incompatible`, budoucí čas, chybějící kontrakt, texty, plurály, `NativeServerInfoHook` |
| `models.ConfigDocumentCheckJniTest` | 2 | deklarovaná dostupnost musí odpovídat odvozené (fixtury valid/repairable/invalid × tři dostupnosti), obě odchylky WP06 uzavřené, kontrola šablony plným validátorem |
| JVM `jni.JniIpcTransportTest` (`bridge/src/test`) | 4 | bajty a timeout beze změny, null zůstává null, nativní volání nikdy na volajícím vlákně (vlákna `DefaultDispatcher-worker`), nejvýš `PARALLELISM` = 8 současně |

### Výsledky

| Test | Výsledek | Poznámka |
|---|---|---|
| NDK build `phpvpn-android` x86_64 (CMake přímo, clang r28c, `-Werror`) | PASS | bez varování |
| Gradle `:app:assembleDebug`, `:app:assembleRelease` (arm64-v8a, x86_64) | PASS | |
| Kontrola `.so` (export, curl/OpenSSL, `.dynsym`, LOAD 16 KB, zipalign 16 KB) | PASS | tabulka v části F |
| Build bez `PHPVPN_ANDROID_DEPS` | PASS (očekávané `#error`) | konfigurace projde, `phpvpn-android` skončí `#error` s návodem |
| `:bridge:testDebugUnitTest` | PASS | 109/109 včetně nových 4 `JniIpcTransportTest` a nezměněných WP06 `ContractsControllerTest` 4/4 a `EditorControllerTest` 9/9. Před sloučením s a3e23e5 selhávaly 2 `ErrorCompletenessTest` (`tunnel_permission_missing` v `ErrorCode.kt`); opravil je `android`, WP04 balíček `errors` neměnil. |
| `:app:testDebugUnitTest` | PASS | 114/114 (po sloučení s WP08 a WP10) |
| `:app:assembleDebugAndroidTest`, `:app:assembleRelease` po sloučení | PASS | |
| `:app:connectedDebugAndroidTest` na `emulator-5554` (regrese, mimo bránu WP04) | 49/50, **FAIL existující** | `MainFlowsTest.welcome_00_whenNoConfigurationExists` čeká 10 s na titulek 36a; selhává stejně s původními `AppContainer.kt` a `PhpvpnApplication.kt` z `android` (ověřeno), tedy souhra WP08 a WP10, ne WP04. Ostatní: `MainFlowsTest` 15/16, `Wp08ScreenshotTest` 24/24, `ContractsUiTest` 5/5, `EditorUiTest` 5/5. |
| `:bridge:connectedDebugAndroidTest`, `emulator-5554` (API 34, x86_64) | PASS | WP04 42/42; celý modul 51 testů, 0 selhání, 1 přeskočený (WP03 `VpnPlatformTest` T-A13 bez rootu adresy, beze změny); před i po sloučení s a3e23e5 |
| totéž `emulator-5558` (API 35, x86_64) | PASS | WP04 42/42; celý modul 51, 0 selhání, 1 přeskočený; před i po sloučení |
| Connect bez souhlasu (API 34, první běhy před WP03 testy) | PASS | `state=error`, `lastError=tunnel_permission_missing`, kontroly URL/secret/access SUCCESS |
| Connect se souhlasem bez TunnelHost (API 34 a 35, celé běhy) | PASS vůči skutečnému chování, **odchylka od zadání** | `lastError=tunnel_start_failed`, `state=recovering`, viz Zjištění 1 |
| Aplikace na API 34 (`am start`) | PASS | runtime v `Application.onCreate` za 31 ms, bez pádu; obrazovky jsou zatím zástupné (WP08) |
| JNI modely bez `.so` (JVM) | NOT RUN | potřebují knihovnu, proto instrumentované |
| arm64-v8a na reálném zařízení | NOT RUN | žádné zařízení; arm64 jen překlad a kontrola `.so` |
| Linux `cmake --preset linux-release` + `ctest -L "portable\|linux"`, Windows | NOT RUN | WP04 nemění sdílený soubor (`git diff` nad `client/src`, `client/tests`, kořenovým a `apps` CMake je prázdný); `apps/android-jni` se mimo Android nepřekládá |
| `test_contracts_model` počítání volání hashe/náhody a injektované selhání providera, `test_password_key` selhání crypto | NOT RUN | skutečný provider nejde přes JNI podvrhnout; ponechán pozorovatelný výsledek |
| odmítnutí senderem (`RefusedDispatch…`, odmítnutý `check`) | NOT RUN | JNI `dispatch`/`check` nemá vstříknutý sender; odmítnutí nastane jen při selhání alokace JNI |
| Busy obálka Wizard rodiny (`busy-wizard.json`) a plná fronta file třídy | NOT RUN | souborovou třídu nejde v testu deterministicky zablokovat; klasifikátor a busy odpověď jsou sdílené s desktopem, ověřena busy obálka diagnostic třídy |
| CI `client-android.yml` | NOT RUN | bez pushe |

## Zjištění

1. **Connect se souhlasem a bez TunnelHost nekončí ve stavu `error`.** `loadAndVerify` vrátí
   `tunnel_adapter_failed` („the VPN service is not running“, WP03). Kód je v `error.cpp` obnovitelný (na
   všech platformách), veřejně se hlásí jako `tunnel_start_failed` a engine zůstává v `recovering` a opakuje
   bootstrap. Bez souhlasu je výsledek podle plánu (`tunnel_permission_missing`, `error`). V UI k tomu nedojde
   (Connect jde vždy přes službu, která TunnelHost registruje před `connect`), ale zadání WP04 očekávalo
   `error`. Oprava patří do WP03/WP05: buď neobnovitelný kód pro chybějící službu, nebo `disconnect` ze služby
   při jejím zániku. Test ověřuje skutečné chování a nespadne (`tunnelReady != yes`, `nativeStatus` shodný).
2. **`php -S` přes síť emulátoru občas ztratí poslední bajt odpovědi.** Relay odpovídá bez
   `Content-Length` a hned zavírá. Přímo přes `nc` z emulátoru (bez curl i bez kódu klienta) skončily 2 z 30
   odpovědí `health` bez závěrečné `}`; přes proxy, která půl-zavře o 200 ms později, 60 ze 60 úplných. Klient
   takovou odpověď správně odmítne (`wire_invalid_json`, `server_verification_failed`, Connect se pak
   zacyklí v recovery, kontrola Wizardu selže). Jde o testovací přípravek (vestavěný server PHP + slirp
   emulátoru), ne o vadu transportu: odpovědi produkčního nginx/PHP-FPM mají délku nebo chunked kódování.
   Testy proto používají proxy; Connect a Wizard test mají navíc nejvýš tři pokusy s logem každého pokusu
   (v posledních bězích nebyl opakovaný pokus potřeba). Pro WP11 doporučuji relay za nginx.
3. **Pořadí členů ve fixturách Wizard rodiny WP06 neodpovídá službě.** Např. `config-state.json` má
   `availability` před `configPath`, služba posílá `configPath` první. Parser WP06 pořadí nekontroluje,
   proto testy porovnávají Wizard fixtury jako množiny a jen `get-status` (fixtura z C++ builderu) i s pořadím.
4. **Odpověď na neznámý klasický příkaz má `requestId: ""`** (desktop: požadavek, který neprojde validací,
   nemá identifikátor k vrácení); Wizard rodina vrací platné UUID zpět. Test drží desktopovou sémantiku.
5. `config.publish-journal` zůstává po publikaci se záznamem `CONFIRMED` (chování `config_files_posix`).
6. Adresář logů je na Androidu 0700, ne `kPosixLogDirectoryMode` 0755 (zadání WP04).

## Odchylky od plánu a zadání

- Connect bez TunnelHost se souhlasem: `recovering` + `tunnel_start_failed` místo `error` (Zjištění 1).
- `nativeDispatch` vrací null i pro požadavek nad 4 MiB a pro neklasifikovatelný požadavek (desktop zavře
  spojení bez odpovědi); Kotlin klient to vidí jako „bez odpovědi“, tedy 08, nikdy úspěch.
- `nativeStatus` má pevný `requestId`, aby ho parser WP06 přijal beze změny.
- `ContractsSaveMachineHandle.close()` je C++ `close()` (stroj lze znovu `load`, jak ho používá
  `ContractsController`); nativní objekt uvolní `release()` nebo `Cleaner`.
- `INTERNET` v manifestu `bridge` (manifest `app` patří WP05).
- Nativní Server Info model jde do aplikace přes rozhraní WP08 (`ServerInfoModelSource`), ne přes
  `ServerInfoHook`; `NativeServerInfoHook` zůstává v `bridge` jako alternativa.
- Hostitelský pomocný `proxy.py` pro relay není v repozitáři (zadání omezuje změny na `android-jni` a `bridge`).
