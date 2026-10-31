# WP06: Kotlin IPC klient a stavové modely, report 2026-10-31

Výsledek pracovního balíku WP06 podle [plan-android-v001.md](../../../Plans/2026-10-31-09-40-08/plan-android-v001.md)
(krok 6, bod 8 „Zjištěná omezení a volby“, „Kotlin část“, test T-J01) a
[android-screen-map.md](../../../Plans/2026-10-31-09-40-08/android-screen-map.md). Vše běží jako JVM testy
(Gradle `testDebugUnitTest`), bez emulátoru a bez JNI. Co se nespustilo, je NOT RUN s důvodem.

## Co vzniklo

Kód je v modulu `bridge` (čisté Kotlin třídy bez závislosti na Android frameworku) a v `app` jen
v balíčku `phpvpn.android.errors` a v souborech `res/values/errors.xml` a `res/values-cs/errors.xml`.
`bridge/build.gradle.kts` nově aplikuje plugin `kotlin.serialization`.

| Část | Soubory (`bridge/src/main/kotlin/phpvpn/android/bridge/…`) | Obsah |
|---|---|---|
| Transport | `ipc/IpcTransport.kt`, `ipc/FakeIpcTransport.kt` | Rozhraní `IpcTransport` pro WP04, `IpcTimeouts` (hodnoty z `ipc_timeouts.h` a `wizard_types.h`), `IdSource`, `MonotonicClock`. Skriptovatelný `FakeIpcTransport` (odpovědi podle příkazu, busy, timeout, zpoždění na virtuálních hodinách, vadné bajty). |
| Striktní JSON | `ipc/StrictJson.kt` | Parser se stejnými pravidly jako `core/protocol/json.cpp`: striktní UTF-8, odmítnutí duplicitních členů, hloubka 32, párované surrogáty, žádné bajty navíc, čísla jako přesný literál (celý rozsah uint64). kotlinx.serialization by duplicitu tiše přijal. |
| Modely | `ipc/WireModels.kt`, `ipc/SecretText.kt` | kotlinx.serialization modely všech 26 příkazů IPC v1 (požadavky i odpovědi), obálky, chyby (`SafeError`, `WizardError` s volitelnými `serverUrl`/`configPath`), `http-octets-v1`, `OperationResult` s uzavřeným payloadem podle `kind`. Tajemství (`serverSecret`, `accessKey`, dokumenty, `set-config`) jsou `SecretText` (CharArray, `wipe()`, `toString()` bez obsahu). |
| Validace | `ipc/WireValidation.kt`, `ipc/HttpHeaders.kt`, `ipc/WireFormats.kt` | Port `tray::parseStatusResponse`, `WizardIpc::parseReply` a pomocných parserů (view, fields, context, error, operation, receipt, diagnostika), `parseServerInfoResult` (bez nativního modelu plaintextu), `normalizeHttpHeaders`, UUIDv4, IPv4/CIDR, ISO 8601, absolutní POSIX cesta. Kde desktop odmítá neznámé členy, odmítá i Android; plaintext `get-info` se jako na desktopu nekontroluje po členech. |
| Klient | `ipc/IpcClient.kt`, `ipc/IpcCommand.kt` | Jedna suspend funkce na příkaz, výsledek `IpcOutcome` (`Success`, `ServiceError` s příznakem `busy`, `TimedOut`, `Rejected`). Kontrola korelace `requestId`, limit 4 MiB před odesláním i po přijetí, nulování bajtů požadavku i odpovědi. `setConfigRaw` a `rawPlain` vracejí nezpracovanou odpověď pro nativní klasifikaci. |
| Chyby | `errors/ErrorCode.kt`, `errors/ErrorTextKey.kt`, `errors/ErrorTexts.kt` | `ErrorCode` 1:1 s `error.h`/`error.cpp` (jméno na drátě, `recoverable`) plus `tunnel_permission_missing`. `ErrorTextKey` (137 klíčů): zpráva pro každý kód, titulek/zpráva/akce pro veřejné chyby připojení (`status_snapshot.cpp`) a lokální `status_unavailable`/`disconnect_unconfirmed`, akce Wizardu podle `WizardIpc::safeError`, `error_unknown`. Text se vybírá kódem, nikdy z přijatého `description`. |
| Modely stavu | `model/StatusModel.kt`, `ProgressModel.kt`, `PollSchedule.kt`, `StatisticsModel.kt`, `ReconnectController.kt` | Přepis `status_model`, `progress_model`, `poll_schedule.h`, `statistics_model.h` a `reconnect_controller.h`. `ProgressView` nese klíče textů místo anglických literálů. |
| Repozitáře | `repository/ConnectionRepository.kt`, `StatisticsRepository.kt`, `ServerInfoRepository.kt` | Viz níže. |
| Kontrolery | `controller/WizardController.kt`, `EditorController.kt`, `ContractsController.kt`, `NativeModels.kt` | Viz níže; `NativeModels.kt` jsou rozhraní pro WP04. |
| Texty | `app/src/main/res/values/errors.xml`, `values-cs/errors.xml`, `app/.../errors/ErrorStrings.kt` | EN a CS ve hlasu návrhu (06, 08, 18, 36, 47; čeština tykáním jako `assets/android-cz`), shodné sady klíčů. `ErrorStrings.resource(key)` je vyčerpávající `when`, nový klíč bez textu se nepřeloží. |

### Stavové držáky

- **`ConnectionRepository`**: `status: StateFlow<ConnectionStatus>` (`Loading`, `Available(TrayStatus)`,
  `Unavailable(lastKnown)` = obrazovka 08). `get-status` s rozpočtem 400 ms běží jen při odběru
  (`SharingStarted.WhileSubscribed`): 500 ms při `connecting`, `recovering`, `reconnecting`, `stopping` a po dobu
  otevřeného vlastního průběhu Connect/Disconnect, jinak 5 s. `refresh()` je mimořádné čtení, pevný termín
  neposouvá smyčku podruhé. `connect()`, `disconnect()` (30 s) vracejí `CommandResult` (`Accepted`, `Failed`,
  `Unconfirmed`); timeout Disconnectu je `disconnect_unconfirmed` (38), nikdy úspěch. `progress` je
  `ProgressModel` vlastního příkazu (04, 06, 07, 38), `dismissDiagnostic` invaliduje detail a pošle
  `dismiss-connect-diagnostic`.
- **`StatisticsRepository`**: deset hodnot z jednoho snímku, `WAITING` (43, žádné nuly), `CURRENT` (26),
  `LAST_KNOWN` (27) při výpadku; součet „Total size“ 65bitově.
- **`ServerInfoRepository`**: jeden požadavek najednou, poslední úspěch přežije chybu (45), auto refresh vypnutý
  ve výchozím stavu, další termín 5 s po dokončení (i chybném), jen při odběru. Surová odpověď
  (`ServerInfoReply.rawReply`) a `ServerInfoHook` pro nativní `ServerInfoModel` (stránkování zůstává v WP04).
- **`WizardController`**: `wizard-open`, pronájem s `wizard-keepalive` každých 15 s a lokální expirací po 60 s
  bez kontaktu (47), jeden ruční pokus na Next s rostoucím `attemptSequence`, rozpočet 1/2/3 × timeout + 15 s,
  dotazování operace po 500 ms, `wizard-cancel-attempt` při invalidaci neúspěšného HTTP detailu nebo vypršení,
  přerušení epochou (→ 11), brána 11 s jedním `disconnect` a jedním `wizard-resume`, Finish s `commitId`,
  nejasný zápis (33) ověřovaný jen `wizard-get-commit` (po restartu služby jednorázově `get-config-state`),
  Reset a Close řídicím kanálem (`wizard-close` 1 s). Logika polí (výchozí klíč, návrh IP, literál tajemství) je
  mimo rozsah: patří do UI modelu WP09.
- **`EditorController`**: `get-config-document`; `missing` → 36a se skutečnou cestou, Ne nic nevytvoří, Ano pošle
  `create-default-config` bez cesty a obsahu a otevře skutečný výsledek (`created:false` otevře mezitím vzniklý
  dokument bez přepsání), Cancel po vytvoření nic nemaže. Reset (`get-default-config`, 5 s) mění jen draft. Save
  přes `set-config` (30 s) a nativní klasifikaci; `unknown` → 33 → kontrola přes `get-config-document` → 46:
  shoda = úspěch, rozdíl = draft zůstává a zapisuje se jen po výslovném `repeatSave()`. Chyby
  `config_unreadable`, `config_encoding_error`, `config_too_large` → 36.
- **`ContractsController`**: jen IPC strana `ContractsSaveController` nad nativním strojem; rozhodnutí jsou ve stroji.

## Rozhraní, která implementuje WP04

```kotlin
// bridge/src/main/kotlin/phpvpn/android/bridge/ipc/IpcTransport.kt
fun interface IpcTransport {
    suspend fun dispatch(request: ByteArray, timeoutMs: Int): ByteArray?   // null = timeout nebo vypnutí
}
```

Skutečný transport volá `NativeBridge.nativeDispatch(request, timeoutMs)` uvnitř `withContext` na vlastním
omezeném `Dispatchers.IO` a nikdy nesmí běžet na hlavním vlákně. `request` je kompletní UTF-8 JSON, volající ho po
návratu nuluje; odpověď patří volajícímu, který ji nuluje po parsování.

```kotlin
// bridge/src/main/kotlin/phpvpn/android/bridge/controller/NativeModels.kt
data class SaveOperation(val windowGeneration: Long, val draftGeneration: Long, val operationGeneration: Long, val deadlineMs: Long)
enum class SaveOutcome { CONFIRMED, NOT_SENT, REJECTED, UNKNOWN }
data class SaveResult(val operation: SaveOperation, val outcome: SaveOutcome, val error: ErrorCode, val validationDescription: String = "")
fun interface SaveResponseClassifier {        // classifySaveResponse (save_response.cpp)
    fun classify(operation: SaveOperation, requestId: String, response: ByteArray?, writeAttempted: Boolean, completedAtMs: Long): SaveResult
}

enum class ContractColumn { ENABLED, KEY, RANDOM, PASSWORD }
data class ContractFocus(val remoteIp: String, val column: ContractColumn)
class ContractRowView(val remoteIp: String, val enabled: Boolean, val keyText: SecretText, val keyValid: Boolean, val dirty: Boolean, val keyErrorVisible: Boolean)
interface ContractTargetHandle { val remoteIp: String; val contractId: String }
interface ContractsModelHandle {              // ContractsModel (contracts_model.h)
    fun rows(): List<ContractRowView>
    fun row(remoteIp: String): ContractRowView?
    val localIp: String; val configPath: String; val revisionToken: String
    val dirty: Boolean; val blockingErrors: Int; val firstBlockingFocus: ContractFocus?
    val keyTextBytes: Long; val canPrepareSave: Boolean; val frozen: Boolean; val closed: Boolean
    val focus: ContractFocus?; val saveGeneration: Long
    fun setEnabled(remoteIp: String, enabled: Boolean): ErrorCode?
    fun setKey(remoteIp: String, key: SecretText): ErrorCode?
    fun setFocus(focus: ContractFocus?): ErrorCode?
    fun target(remoteIp: String): ContractTargetHandle?
    fun confirmRandom(target: ContractTargetHandle, confirmed: Boolean): ErrorCode?
    fun applyPasswordKey(target: ContractTargetHandle, key: SecretText): ErrorCode?
    fun resumeEditing(saveGeneration: Long): Boolean
}
enum class ContractsStage { LOADING, EDITING, INVALID, SAVING, SAVED, SAVE_UNKNOWN, CLOSED }
enum class ContractsEvent { NONE, SAVED, MATCHED, REJECTED, UNKNOWN, DIFFERENT, CHECK_FAILED }
data class ContractsLifecycleSnapshot(val open: Boolean, val dirty: Boolean, val saving: Boolean, val unknown: Boolean, val checking: Boolean,
    val windowGeneration: Long, val draftGeneration: Long, val operationGeneration: Long, val checkGeneration: Long, val deadlineMs: Long)
class PreparedSave(val operation: SaveOperation, val document: SecretText)
interface ContractsSaveMachineHandle : AutoCloseable {   // ContractsSaveMachine (contracts_save_machine.h)
    fun load(document: SecretText, configPath: String, revisionToken: String): ErrorCode?
    val model: ContractsModelHandle?; val stage: ContractsStage; val saving: Boolean
    fun snapshot(checking: Boolean): ContractsLifecycleSnapshot
    fun changed()
    fun prepare(confirmedRepeat: Boolean, checking: Boolean): ErrorCode?
    fun dispatch(nowMs: Long): PreparedSave?           // = dispatch(nowMs, sender) se senderem, který přijme
    fun saveFinished(result: SaveResult): ContractsEvent
    fun check(checking: Boolean): Long?                 // = check(checking, sender), vrací checkGeneration
    fun checkFinished(checkGeneration: Long, requestId: String, response: ByteArray?): ContractsEvent  // nativní WizardIpc::parseReply
    fun canRepeat(checking: Boolean): Boolean
    val notice: String
    override fun close()
}

// bridge/src/main/kotlin/phpvpn/android/bridge/ipc/WireValidation.kt (volitelné, nativní documentMatches)
interface ConfigDocumentCheck {
    fun matches(document: String, availability: ConfigAvailability): Boolean
    fun validTemplate(document: String): Boolean
}
// bridge/src/main/kotlin/phpvpn/android/bridge/repository/ServerInfoRepository.kt
fun interface ServerInfoHook { fun accept(reply: ServerInfoReply): Boolean }
```

`null` jako `ErrorCode?` odpovídá úspěšnému C++ `Status`. Callbacky senderů C++ stroje jsou rozdělené na návratové
hodnoty, takže přes JNI nejde žádná Kotlin lambda.

## Testy (T-J01)

Příkaz: `JAVA_HOME=D:/androidstudio/jbr ./gradlew :bridge:testDebugUnitTest :app:testDebugUnitTest :app:assembleDebug`
na Windows 11, Gradle 9.8.2, AGP 9.5.0, offline. **BUILD SUCCESSFUL.** WP06 přidal 106 testů, všechny PASS.

| Třída | Testů | Výsledek | Obsah |
|---|---|---|---|
| `model.TrayStatusTest` | 6 | PASS | 1:1 `test_tray_status.cpp` (jména `TrayStatus_*`) nad `tray_fixture.h` |
| `model.TrayProgressTest` | 7 | PASS | 1:1 `test_tray_progress.cpp` (`TrayProgress_*`, `TrayPolling_*`); místo anglického popisu se porovnává kód a klíč textu |
| `model.StatusContractTest` | 5 | PASS | 1:1 `test_status_contract.cpp` (celý rozsah uint64, pořadí členů, `httpDiagnostic` 65535 až 200000 B, retire diagnostiky) a klientská strana `dismiss-connect-diagnostic` |
| `model.StatusMatrixTest` | 3 | PASS | všech 5 stavů × 9 fází × 81 kombinací čtyř checkpointů × s/bez `connectionStartedAt` (7290 případů): parser, indikátor, průběh Connect i Disconnect; všechny tristate; shoda epochy a `connectAttemptId` |
| `ipc.WizardIpcTest` | 16 | PASS | port `test_wizard_ipc.cpp` (`wizard_ipc_*`) pro klientskou stranu: 17 schémat požadavků, diagnostika, osmice, limity, legacy, dokumenty, cesty, kontext, reset |
| `ipc.IpcClientTest` | 15 | PASS | všech 26 příkazů proti fixturám včetně zpětné serializace modelu na stejný objekt, busy obou obálek, timeout, vadné/nekorelované/příliš velké odpovědi, limit požadavku, nulování bufferů, klientská část `test_ipc.cpp` |
| `ipc.StrictJsonTest` | 8 | PASS | duplicity, UTF-8, surrogáty, hloubka, čísla, UUID/IPv4/ISO, gramatika hlaviček |
| `errors.ErrorCompletenessTest` | 6 | PASS | parsuje `error.h` a `error.cpp`: každý kód s jménem a `recoverable`, pořadí; kódy ze `status_snapshot.cpp`; allowlist z `progress_model.cpp`; každý kód jako `lastError`; akce podle `safeError` |
| `repository.RepositoriesTest` | 11 | PASS | polling jen při odběru, 5 s / 500 ms, extra čtení, 08 s posledním stavem, Connect/Disconnect a průběh, 26/27/43, formát bajtů, Server Info a auto refresh |
| `controller.WizardControllerTest` | 12 | PASS | čtyři kroky a receipt, keepalive 15 s a lease 60 s (47), `wizard_session_expired` z keepalive, brána 11 (jeden disconnect, jeden resume), přerušení, detail a jediný cancel-attempt, rozpočet pokusu, ztracený Finish → 33 → get-commit, restart služby → get-config-state, failed commit, reset a close, wizard_unsupported |
| `controller.EditorControllerTest` | 9 | PASS | 36a Ne/Ano, `created:false`, Cancel po vytvoření, 36, Reset jen draft, potvrzený a odmítnutý zápis, unknown → shoda, konflikt bez tichého přepsání, selhání kontroly |
| `controller.ContractsControllerTest` | 4 | PASS | otevření jen platného dokumentu, payload stroje, raw kontrola nejasného zápisu, chyba prepare |
| `app: errors.ErrorResourcesTest` | 4 | PASS | stejné klíče `values/errors.xml` a `values-cs/errors.xml` = klíče `ErrorTextKey`, text pro každý kód z `error.h` v obou jazycích, `ErrorStrings` bez kolizí, žádné desktopové formulace |

Fixtury jsou v `bridge/src/test/resources/ipc` (55 souborů): `status-fixture.json` je doslovně `kStatusFixture`
z `tray_fixture.h`, `default-config-document.json` je `kDefaultConfigDocument` z `config.h`, ostatní jsou
přepsané inline JSON z `test_ipc.cpp`, `test_status_contract.cpp`, `test_wizard_ipc.cpp`,
`service_runtime.cpp` (busy) a `server_info_fixture.h`. Testy čtou zdrojové soubory C++ přímo z `client/src`
a `client/apps`, takže nový `ErrorCode` v `error.h` bez Kotlin protějšku a textů test shodí.

`:app:testDebugUnitTest` celkem 12 testů (4 WP06 + 8 WP07), `:app:assembleDebug` PASS (včetně nativního buildu
pro arm64-v8a a x86_64).

| Položka | Stav | Důvod |
|---|---|---|
| Skutečný JNI transport a nativní modely | NOT RUN | `nativeDispatch` a `jni_models.cpp` dodá WP04; WP06 testuje proti `FakeIpcTransport` a referenčnímu portu klasifikátoru v testech. |
| Instrumentované testy a T-U04 na emulátoru | NOT RUN | Mimo WP06 (WP10, WP11); WP06 nemá UI. |

## Odchylky

1. **Validace obsahu dokumentu.** C++ `documentMatches` a kontrola šablony `get-default-config` spouštějí celý
   validátor konfigurace. Kotlin ho nemá; výchozí `StructuralConfigDocumentCheck` kontroluje velikost 512 KiB,
   syntaxi a povinné členy u `valid`, jen syntaxi u `repairable` a u `invalid` nic, aby rozbitý text zůstal
   editovatelný. V `wizard_ipc_configuration_documents_*` se proto dva případy liší od C++ (platný dokument
   ohlášený jako `repairable` nebo `invalid` Kotlin přijme); test to uvádí. WP04 může vložit nativní
   `ConfigDocumentCheck`.
2. **Mazání tajemství je best effort.** Bajty požadavku i odpovědi se nulují, tajemství jsou v modelech
   `SecretText` s `wipe()` a bez obsahu v `toString()`. kotlinx.serialization ale kóduje a dekóduje přes
   neměnný `String`, tyto kopie zůstávají na GC (stejná výhrada jako „best effort, not an allocator/OS guarantee“
   v `progress_details.cpp`). Nic se neloguje.
3. **`get-config-document` nikdy nevrací `unreadable`, `encoding_error`, `too_large`** jako dostupnost (C++ parser
   vyžaduje text dokumentu). Jsou to chybové obálky s cestou; `EditorController` je mapuje na 36.
4. **`EditorDocument` (literál pro U+0000)** se nepřepisuje: řeší omezení Win32 EDIT, Compose text s U+0000 unese.
5. **`WizardModel`** (pole, výchozí klíč, návrh IP, literál tajemství) se nepřepisuje, `WizardController` dostává
   hotový draft `WizardFields`. Patří do WP09 (Kotlin port nebo JNI).
6. **Akce `server_version_incompatible`** nemá na konci „This client is version X“; verzi doplní UI z `BuildConfig`.
7. **`config_directory_security_invalid`** má na Androidu akci „restartuj aplikaci“ místo restartu služby
   administrátorem (plán, krok 10).
8. **Případ C++ testu `"X: \xff"`** nelze v Kotlin `String` vyjádřit; neplatné UTF-8 odmítá transportní dekódování.
9. **`status-connected.json`** má na rozdíl od C++ testu builderu `connectionStartedAt`, protože tray parser
   `connected` bez něj odmítá.
10. **`get-diagnostics`** se kontroluje jako uzavřený objekt, přestože desktop žádný parser nemá (smlouva:
    „pouze pole povolená pro daný příkaz“).
11. **`FakeIpcTransport`** je v `bridge/src/main`, aby ho mohly použít i testy `app` (test fixtures Android
    knihovny by vyžadovaly další nastavení Gradle). Neobsahuje nic nativního, R8 ho z release odstraní.
12. **`tunnel_permission_missing`** je v Kotlin `ErrorCode` už teď (`ANDROID_ADDITIONS`), v `error.h` ho doplní
    WP03/WP04 a WP13 ho zapíše do client.md.
13. **Polling** je 500 ms i po dobu otevřeného vlastního průběhu Connect/Disconnect (desktopové okno průběhu),
    nejen při přechodovém stavu ze statusu.
