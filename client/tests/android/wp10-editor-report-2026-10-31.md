# WP10: kontrakty, heslo, editor a výsledky uložení, report 2026-10-31

Výsledek pracovního balíku WP10 podle [plan-android-v001.md](../../../Plans/2026-10-31-09-40-08/plan-android-v001.md)
(krok 10, „Kotlin část“, testy T-U03, T-U04) a [android-screen-map.md](../../../Plans/2026-10-31-09-40-08/android-screen-map.md)
(řádky 19 až 25, 32, 33, 36a, 39, 41, 42, 46). Co se nespustilo, je NOT RUN s důvodem. Všechny výsledky jsou z JVM
testů a z emulátoru `phpvpn-api36-wp10` (x86_64, API 36, 1080 × 2400 px, 420 dpi = 411 dp), žádný není z reálného
zařízení.

## Co vzniklo

Vše je v `client/android/app`, jediný nový soubor v `bridge` je rozhraní `PasswordKeyModelHandle`. Soubory WP07, WP08
a WP09 (`MainGraph.kt`, `WizardGraph.kt`, `NavShared.kt`, `Routes.kt`, `NavActions.kt`, `PhpvpnNavHost.kt`, komponenty,
téma, `strings.xml`) se neměnily.

| Část | Soubory | Obsah |
|---|---|---|
| Rozhraní pro WP04 | `bridge/.../controller/PasswordKeyModel.kt` | `PasswordKeyModelHandle` a `PasswordKeyError` podle `password_key_model.h`. |
| Kotlin náhrady nativních modelů | `app/.../standin/StandInContractsDocument.kt`, `StandInContractsModel.kt`, `StandInContractsSaveMachine.kt`, `StandInSaveResponseClassifier.kt`, `StandInPasswordKeyModel.kt`, `RandomPassword.kt` | Port `contracts_document.cpp`, `contracts_model.cpp`, `contracts_save_machine.cpp`, `save_response.cpp`, `password_key_model.cpp` a Random z `password_sha_window.cpp` za rozhraními WP06. Obrazovky je znají jen přes rozhraní. |
| Závislosti toků | `ui/screens/ConfigFlowDependencies.kt` | `IpcClient`, `ConnectionRepository`, klasifikátor a továrny nativních modelů. Výchozí hodnoty z `AppContainer`, testy dodají vlastní přes `LocalConfigFlowDependencies`. WP04 vymění jen tři továrny. |
| FLAG_SECURE | `ui/screens/SecureWindow.kt` | WP09 v `android` zatím nebyl, proto vlastní mechanismus: počítadlo držitelů na okno, příznak zmizí až s poslední zabezpečenou obrazovkou (překryv při přechodech). |
| Výsledek uložení | `ui/screens/save/SaveOutcomeSource.kt` | Rozhraní přesně podle zadání, viz níže. |
| Kontrakty a heslo | `ui/screens/contracts/*`, `ui/screens/password/*` | `ContractsViewModel`, obrazovky 19/41, 20/22, `PasswordKeyViewModel`, obrazovka 21. |
| Editor | `ui/screens/editor/*` | `EditorViewModel` nad `EditorController`, obrazovky 23/39, 24/42, obsah dialogu 36a. |
| Výsledky uložení | `ui/screens/save/*` | `SavedViewModel` (25) a obrazovky 25, 33, 46. |
| Navigace | `ui/navigation/ContractsGraph.kt`, `EditorGraph.kt` | Zástupné obrazovky WP07 nahrazené skutečnými, včetně `saveOutcomes`. |
| Texty | `res/values/strings_wp10.xml`, `res/values-cs/strings_wp10.xml` | 63 klíčů EN a CS se shodnou sadou, stavy, které statický návrh nemá (načítání, odmítnutí, neznámý výsledek, neaktivní VPN na 25, potvrzená varianta 46). |
| Testovací odpovědi | `src/debug/.../debug/ConfigReplies.kt` | Syntetické odpovědi IPC v1 (dokumenty, create-default, set-config, status) pro JVM i Compose testy. Jen debug. |
| Testy | `src/test/.../standin/*`, `ui/screens/**`, `Wp10StringResourcesTest.kt`, `src/androidTest/.../wp10/*` | Viz „Testy“. |

## Obrazovky a implementovaná pravidla

| ID | Cesta | Pravidla |
|---|---|---|
| 19 | `contracts` | Jedno `get-config-document`, model se otevře jen pro `valid`. Řádky jsou všechny klientské adresy `vpnCidr` kromě vlastní IP, číselně seřazené, 20 na stránku (`kTablePageSize` z `table_pagination.h`). Draft patří cílové IP v jednom modelu pro celý graf kontraktů, ne viditelným řádkům. Tag má vždy text (Enabled, Disabled, Invalid key). Počet změn v upozornění i v tlačítku Save. Save s blokující chybou neposílá nic a otevře 22 pro první neplatný kontrakt. Discard a Back se změnami nebo s neznámým výsledkem vedou na 32, během ukládání Back nic nedělá. `missing`, `repairable`, `invalid` a chyby čtení mají vlastní vysvětlení s cestou ze služby a nabídku Wizardu nebo editoru, bez okna kontraktů. |
| 41 | `contracts?page=n` | Přepnutí stránky nahrazuje záznam seznamu (`openContractsPage`), draft zůstává. Back na další stránce vede na stránku 1, teprve tam na 32 nebo ven. Upozornění „Changes from the previous page are preserved.“ |
| 20 | `contracts/{ip}` | Detail mění přímo sdílený draft. Snímek řádku při otevření slouží pro Discard (32): obnoví jen tento řádek, ostatní změny zůstávají. Klíč skrytý v `SecretField`, Show dočasně. Klíč vypnutého kontraktu je jen pro čtení a akce Password nedostupná. Neplatný klíč má chybu přímo u pole s počtem znaků. Odmítnutá změna nad limitem 512 KiB pro všechny klíče ponechá původní text. FLAG_SECURE. |
| 22 | `contracts/{ip}` | Varianta detailu, když je kontrakt při otevření zapnutý s neplatným klíčem (cesta ze Save). Chyba patří tomuto kontraktu a poli, upozornění jmenuje první neplatný kontrakt, ostatní neuložené změny zůstávají. FLAG_SECURE. |
| 21 | `contracts/{ip}/password` | `PasswordKeyModelHandle`: UTF-16 vstup jako `CharArray` bez převodu, po každém volání vymazaný. Prázdné heslo má standardní hash, nápověda se nehashuje. Over-long a řídicí znak odmítnou celou editaci. Default = kanonický pár `minIP_maxIP`, Random = 64 znaků z abecedy desktopu bez zkreslení. Use key jen pro zachycený cíl (`ContractTarget`), jen když je zapnutý, po uložení je cíl zastaralý. FLAG_SECURE. |
| 23 | `editor` | Otevření z 00, 10, 36 nejprve `get-config-document`. Skutečný text, monospace, bez autokorekce, cesta ze služby (soukromé úložiště aplikace) místo cesty z Windows. Upozornění pro dokument `invalid`/`repairable`, pro nově vytvořený soubor a pro `created:false`. Neodeslaný požadavek a selhaný Reset mají vlastní text, draft zůstává. Neznámý výsledek zmrazí text a nabídne jen ověření. FLAG_SECURE. |
| 39 | `editor` | Text validátoru ze `set-config`, když ho služba dodá (kurátorovaná chyba `masterSetup.httpHeaders line N: …`), jinak text kódu z resources. Číslo řádku nikdy nevzniká v klientu. `config_invalid_json` má titul Invalid JSON, ostatní hodnoty Invalid configuration value. Draft zůstává editovatelný. |
| 24 | `editor/reset` | Potvrzení Reset in editor / Keep my draft. `get-default-config` mění jen draft, nic nezapisuje. Selhání vrátí do editoru s textem a nezměněným draftem. |
| 42 | `editor/reset` | Úplný výchozí dokument služby (ne výřez) jen pro čtení, upozornění, že se nic neuložilo, Continue in editor a Complete with the wizard. FLAG_SECURE. |
| 36a | dialog `config/create-default` | Nad 00, 10, 36. Skutečná absolutní cesta ze služby. Ano pošle `create-default-config` bez cesty a obsahu a otevře skutečný výsledek; `created:false` otevře soubor, který mezitím vznikl, bez přepsání. Ne nic nevytvoří a vrátí zpět. Přímý vstup (zástupné 00 a 36 z WP07 navigují na dialog s prázdnou cestou) nejprve přečte `get-config-document`; existující soubor se rovnou otevře v editoru. Zavřený dialog bez odpovědi nic nevytváří a další otevření se zeptá znovu. Cancel nebo Discard editoru po vytvoření soubor nemaže (žádný příkaz neodchází). Obsah dialogu se při velkém písmu posouvá. |
| 25 | `saved` | Po potvrzeném uložení jedno `get-status`. Connected: Reconnect podle `ReconnectController` (souhlas, nové čtení stejného spojení, Disconnect, čekání na `disconnected`/`idle` téže identity do 30 s, teprve pak jeden Connect) nebo Use on next connection. Neaktivní: Connect nebo Not now. Přechodový stav a nedostupný status: nic automatického, jen návrat a vysvětlení. |
| 33 | `save/unknown?origin=` | Draft zachovaný a zmrazený. Verify save volá `SaveOutcomeSource.verify()` jednou, nikdy nepíše. Nerozhodnutá kontrola zůstane na 33 s důvodem. Pro kontrakty také Inspect current configuration (editor). |
| 46 | `save/checked?origin=` | Potvrzený zápis je úspěch (Changes are saved, Continue na 25), ne Retry. Jiný nebo chybějící soubor: Draft not saved, Save draft again jen jako výslovná volba a jen když to stroj dovolí (`canRepeat`). Konflikt se nikdy nepřepíše tiše. |
| 32 | dialog | Sdílený `rememberUnsavedChangesGuard` z WP07 se skutečným příznakem: seznam (dirty nebo neznámý výsledek), detail (změna proti snímku), 21 (rozepsané heslo), editor (dirty nebo neznámý výsledek). |

Wizard strana 33 a 46 (ověření receiptu `wizard-get-commit`) patří WP09; pro `origin=wizard` zůstává zástupná obrazovka
WP09.

## SaveOutcomeSource

WP09 rozhraní v `android` při merge ještě nemělo, proto je v `ui/screens/save/SaveOutcomeSource.kt` přesně podle zadání:

```kotlin
interface SaveOutcomeSource { suspend fun verify(): SaveVerification }
sealed class SaveVerification {
    data class Confirmed(val summary: String) : SaveVerification()   // zápis prokázán → 46 úspěch → 25
    data class Failed(val error: String) : SaveVerification()        // draft není aktuální stav → 46, opakování jen výslovně
    data object Unknown : SaveVerification()                         // kontrola nerozhodla → zůstat na 33
}
```

`ContractsViewModel` i `EditorViewModel` ho implementují: jedno `get-config-document`, porovnání s odeslaným kandidátem
(`ContractsController.checkSaveResult`, `EditorController.checkSaveResult`). `summary` je cesta ke konfiguraci, `error`
je `different` (jiný nebo chybějící soubor). Kontrola běží ve `viewModelScope`, takže opuštění obrazovky během čtení
nenechá stav viset. Hlavní session sloučí s verzí WP09, pokud přijde dřív.

## Rozhraní, která implementuje WP04

Obrazovky používají jen rozhraní; WP04 vymění tři továrny v `ConfigFlowDependencies`:

```kotlin
val classifier: SaveResponseClassifier                  // classifySaveResponse (save_response.cpp)
val contractsMachine: () -> ContractsSaveMachineHandle   // ContractsSaveMachine + ContractsModel (model přes .model)
val passwordKeyModel: () -> PasswordKeyModelHandle       // PasswordKeyModel (nové rozhraní, bridge/controller/PasswordKeyModel.kt)
```

```kotlin
interface PasswordKeyModelHandle : AutoCloseable {
    fun update(password: CharArray): PasswordKeyError   // UTF-16 bez převodu, volající maže pole
    val valid: Boolean
    fun key(): SecretText                               // kopie, volající maže
    val scalarCount: Int
    val error: PasswordKeyError
    fun clear()
    override fun close()
}
```

Co Kotlin náhrada pokrývá stejně jako nativní model: řádky, vyloučení vlastní IP, rozsah `/24` až `/29` a adresy,
`[0-9a-f]{64}`, dirty podle publikovatelného výsledku, blokující chyby a první fokus, limit 512 KiB pro klíče, cíle
s identitou a generací, Random 32 bajtů, zmrazený kandidát, patch jen `communicationContracts` (absent zůstane absent,
present po odebrání `{}`), velikost obálky skutečným builderem požadavku, `matchesDocument` bez ohledu na pořadí členů,
stavy a generace stroje, 30s deadline, kontrola neznámého výsledku z nezpracované odpovědi, klasifikace odpovědi
`set-config` včetně kanonických popisů a hlaviček s číslem řádku, Password → SHA-256 s limity a řídicími znaky.

Co umí jen nativní model: úplná validace konfigurace `validateConfigValues` (Kotlin kontroluje strukturní podmnožinu:
URL, secret, gramatiku hlaviček, CIDR, vlastní IP, access key, uzavřené kontrakty; runtime limity, TLS a metadata
Wizardu ne; služba při `set-config` validuje vždy celé), `SecureBuffer` a `secureWipe` (Kotlin maže `CharArray`, ale
`String` kopie Compose polí, parseru a kodéru zůstávají na GC), anglické `notice` stroje (Kotlin dává u chyb jen jméno
kódu; UI `notice` nezobrazuje, texty vybírá podle událostí a kódů z resources), crypto provider jádra místo
`MessageDigest` a `SecureRandom`.

## Testy

Příkaz: `JAVA_HOME=D:/androidstudio/jbr ANDROID_SERIAL=emulator-5564 ./gradlew --offline :app:assembleDebug
:app:testDebugUnitTest :app:connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.package=phpvpn.android.wp10`
na Windows 11. **BUILD SUCCESSFUL.** `:bridge:testDebugUnitTest` také PASS (nové rozhraní nic nerozbilo).

### JVM (T-J01, Gradle `testDebugUnitTest`)

`:app:testDebugUnitTest` celkem 64 testů, z toho 52 nových WP10, všechny PASS.

| Třída | Testů | Výsledek | Obsah |
|---|---|---|---|
| `standin.StandInContractsModelTest` | 11 | PASS | 1:1 `test_contracts_model.cpp` (stejná jména) a patch/`matchesDocument` z `contracts_document.cpp` |
| `standin.StandInPasswordKeyModelTest` | 5 | PASS | 1:1 `test_password_key.cpp` (vektory, 254/255/256 skalárů, surrogáty, řídicí znaky, selhání crypto) a Random bez zkreslení |
| `standin.StandInContractsSaveMachineTest` | 8 | PASS | port `test_contracts_save_machine.cpp` (potvrzení, odmítnutí, unknown → check, výsledky kontroly, invalid/load/close, velikost obálky, klasifikace) a shoda popisů s `error.cpp` |
| `ui.screens.contracts.ContractsViewModelTest` | 10 | PASS | jen `valid` otevře model, 252 řádků bez vlastní IP, draft na stránkách 1 a 13, Discard detailu, neplatný klíč → 22 bez zápisu, heslo (vektory client.md, Default, Random, zastaralý cíl), publikace jen kontraktů, odmítnutí a `config_write_failed` → unknown, shoda = úspěch, konflikt jen po výslovném Save again, selhaná kontrola |
| `ui.screens.editor.EditorViewModelTest` | 11 | PASS | 36a Ne nic nevytvoří, Ano vytvoří (požadavek jen trojice) a otevře skutečný dokument, `created:false` nepřepíše, Cancel nic nemaže, přímý 36a nad existujícím souborem nic nevytvoří, Reset jen draft a selhání, potvrzené uložení, 39 s textem validátoru a řádkem, unknown → shoda, konflikt → Save again, selhaná kontrola, zavřený 36a se zeptá znovu |
| `ui.screens.save.SavedViewModelTest` | 4 | PASS | Reconnect: Disconnect před Connect a Connect až po potvrzeném idle téže identity; Disconnect bez dokončení do 30 s nikdy nepřipojí; jiné spojení po souhlasu nic nepošle; neaktivní VPN → Connect; nedostupný a přechodový status nic automatického |
| `Wp10StringResourcesTest` | 3 | PASS | shodné klíče EN a CS, stejné argumenty, žádný prázdný text, žádné předefinování klíče `strings.xml` |

`UiSourceRulesTest` (barvy jen v tématu, žádný text v Kotlinu) a `StringResourcesTest` dál PASS.

### Compose UI na emulátoru

| Test | Obsah | 411 dp, písmo 100 % | 320 dp (`wm density 540`), písmo 200 % |
|---|---|---|---|
| T-U03 `ContractsUiTest.paginationKeepsTheDraftPerTargetIp` | změna na stránce 1, změna na stránce 2, zpět, obě v počtu a v řádku, vlastní IP není řádek | PASS | PASS |
| T-U03 `invalidKeyBlocksSaveOn22AndOtherChangesStay` | chyba u pole, Save → 22, oprava, uložení obou kontraktů, 25 s Reconnect | PASS | PASS |
| T-U03 `keyFromPasswordGoesToTheEnabledContract` | vypnutý kontrakt bez akce Password, 21, SHA-256 „abc“, Use key do detailu, FLAG_SECURE | PASS | PASS |
| T-U03 `unknownSaveIsVerifiedAndAConflictIsOnlyResavedExplicitly` | 33, ověření cizí konfigurace → 46, druhý zápis jen po Save draft again | PASS | PASS |
| T-U03 `aMatchingCheckIsASuccessNotARetry` | 33 → shoda → 46 úspěch bez Save again → 25, jeden zápis | PASS | PASS |
| T-U04 `EditorUiTest.noCreatesNothingAndReturns` | 36a se skutečnou cestou, Ne → zpět, odešlo jen `get-config-document` | PASS | PASS |
| T-U04 `yesCreatesAndOpensTheActualDocumentAndCancelDeletesNothing` | Ano → skutečný dokument a cesta, úprava, Back → 32 → Discard, odešlo jen čtení a create, FLAG_SECURE zapnutý a po odchodu vypnutý | PASS | PASS |
| T-U04 `createdFalseOpensTheFileThatAppearedWithoutOverwriting` | `created:false` otevře cizí soubor, žádný `set-config` | PASS | PASS |
| `resetChangesOnlyTheDraftAndSaveLeadsTo25` | 24 → 42 bez zápisu, Continue, Save → 25 ve variantě Connect | PASS | PASS |
| `rejectedSaveShows39WithTheValidatorTextAndKeepsTheDraft` | 39 s textem validátoru a číslem řádku, Back to editor, draft zachovaný | PASS | PASS |

Celkem 10 Compose testů, 10/10 PASS v obou konfiguracích (Gradle `connectedDebugAndroidTest` při 411 dp a dvakrát
`am instrument` kvůli snímkům, protože Gradle po běhu aplikaci odinstaluje i se snímky).

| Položka | Stav | Důvod |
|---|---|---|
| T-A07 (uložení při aktivním spojení a Reconnect proti skutečnému serveru) | NOT RUN | Patří WP11; vyžaduje JNI transport WP04 a skutečnou službu. Sekvence 25 je ověřená jen JVM testy s `FakeIpcTransport`. |
| Nativní modely přes JNI | NOT RUN | Dodá WP04; testy běží s Kotlin náhradami. |
| Wizard strana 33/46 | NOT RUN | WP09. |
| Šířka 360 dp | NOT RUN | Zadání požadovalo 412 a 320 dp / 200 %. |
| Reálné zařízení | NOT RUN | Jen emulátor. |

## Snímky

32 snímků v `work/wp10` (gitignored), `<id>-w411-f1.0.png` a `<id>-w320-f2.0.png` pro 19, 20, 21, 22, 23 (začátek
a cesta), 24, 25 (Reconnect, při 411 dp i Connect), 33, 36a, 39, 41, 42, 46 (konflikt i shoda). Zabezpečená okna se
fotí tak, že test těsně před snímkem FLAG_SECURE dočasně sundá a hned vrátí (příznak předtím ověří); klávesnice se před
snímkem schová.

Porovnání okem s `assets/android-en/previews/full` při 411 dp: rozložení, typografie, karty, tagy, upozornění, patička
s primárním tlačítkem a ghost akcí odpovídají 19, 20, 21, 22, 23, 24, 25, 33, 39, 41, 42, 46. Rozdíly jsou datové
a záměrné: 20 řádků na stránku místo ukázkových 4; skutečné adresy `10.99.99.0/24` místo `10.80.0.0/24`; 22 bez
přepínače (jako návrh) a se skrytým klíčem (Show k dispozici); 42 ukazuje celý výchozí dokument, ne výřez; 25 pro
neaktivní VPN nabízí Connect; 46 má potvrzenou variantu, kterou návrh nekreslí; 36a je nová obrazovka bez předlohy.
Při 320 dp a 200 % nic nepřetéká vodorovně a vše je dosažitelné posunem; titul horní lišty se láme uprostřed slova
(„Communicatio/n contracts“), to je komponenta `TopBar` z WP07.

## Odchylky

1. **Kotlin náhrady místo JNI.** `ContractsModel`, `ContractsSaveMachine`, `classifySaveResponse` a `PasswordKeyModel`
   běží do WP04 v Kotlinu (balíček `phpvpn.android.standin`) za rozhraními. Validace konfigurace je strukturní podmnožina
   (viz výše); `notice` stroje má u chyb jen jméno kódu.
2. **`SecureWindow` a `SaveOutcomeSource`** vznikly ve WP10, protože WP09 je v `android` zatím neměl. Sloučí hlavní
   session.
3. **Detail kontraktu edituje draft přímo**, Apply to draft jen zavře detail; Discard (32) vrátí řádek do stavu při
   otevření. Tím zůstává model jediným zdrojem pravdy i pro klíč z 21 (`applyPasswordKey` nad zachyceným cílem).
4. **Neplatný klíč se do draftu přijme** (jako tabulka desktopu) a blokuje až Save, které otevře 22. Chyba se ukazuje
   u pole hned při psaní.
5. **Řízení odmítnutí kontraktů.** `ContractsController` vrací jen událost; ViewModel si kód odmítnutí bere z obalu
   klasifikátoru, aby 19 mohl ukázat konkrétní text.
6. **Kontrola nejasného zápisu v Kotlinu vyžaduje přítomné `communicationContracts`** u dokumentu `valid`
   (`StructuralConfigDocumentCheck` z WP06). Platný dokument bez tohoto objektu by se v kontrole hlásil jako selhání
   kontroly (zůstane 33), ne jako shoda. Port testu save machine proto používá přítomný objekt. Nativní kontrola WP04 to
   odstraní.
7. **Connect na 25** volá `ConnectionRepository.connect()`; systémový souhlas VPN (03) a jeho odmítnutí (37) řeší cesta
   WP05/WP08.
8. **36a a editor sdílejí ViewModel** vázaný na kořen navigačního grafu, protože dialog leží mimo graf editoru;
   `close()` maže relaci při každém opuštění editoru.
9. **Dialog 36a se posouvá** při velkém písmu (jinak by Ano a Ne při 320 dp / 200 % nebyly dosažitelné). Sdílený dialog
   32 z WP07 a `TopBar` stejné úpravy nemají; při testech 320 dp / 200 % se tlačítka 32 vešla.
10. **Emulátor.** AVD `phpvpn-api36` běžel na portu 5556 u jiného agenta bez `-read-only`, druhá instance se
    nespustila. WP10 proto použil vlastní klon `phpvpn-api36-wp10` (stejný systémový obraz API 36, stejný hardware) na
    portu 5564, na konci zastavený.
11. **Tajemství v polích Compose.** Heslo, klíče a dokument drží `BasicTextField` jako `String`; model dostává
    `CharArray`/`SecretText`, které se po použití maže. Nic se neloguje, `toString()` modelů obsah nevypisuje.
