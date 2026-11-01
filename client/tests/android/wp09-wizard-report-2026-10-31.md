# WP09: Configuration Wizard a HTTP diagnostika, report 2026-10-31

Výsledek pracovního balíku WP09 podle [plan-android-v001.md](../../../Plans/2026-10-31-09-40-08/plan-android-v001.md)
(krok 9, „Kotlin část“, T-A06) a [android-screen-map.md](../../../Plans/2026-10-31-09-40-08/android-screen-map.md)
(řádky 11 až 18, 30 až 33, 40, 46, 47 a toky). Závazné podklady: [client.md](../../../client.md) (Configuration
Wizard, Síťové kroky Wizardu, Rozšíření IPC v1, HTTP hlavičky konfigurace), [tray_UX.md](../../../tray_UX.md) (UX08,
UX09) a návrh [assets/android-en](../../../assets/android-en/RULES.md). Co se nespustilo, je NOT RUN s důvodem.
Instrumentované výsledky jsou z emulátoru API 35 (x86_64, 1080 × 2400 px, 420 dpi), žádný není z reálného zařízení.

## Co vzniklo

| Část | Soubory (`client/android/app/src/…`) | Obsah |
|---|---|---|
| Logika polí | `main/…/ui/screens/wizard/WizardDraft.kt`, `WizardRules.kt`, `HeaderRules.kt` | Port draftové části `WizardModel` (`wizard_model.cpp`): výchozí secret, literál JSON pro secret s řídicími znaky, automatická IP z ověřeného návrhu, výchozí klíč (hex posledního oktetu 32×) sledující každou změnu IP, volba kontraktů, lokální validace kroku (`validateCurrent`) s kódy desktopu. Port `defaultAccessKey`, `supportsDefaultContracts`, `defaultContractCandidates`, `missingDefaultContractPeers`, `parseServerUrl`, `isValidClientAddress` a `normalizeHttpHeaders` s důvodem, řádkem a názvem chyby. |
| Stav relace | `WizardViewModel.kt`, `WizardDependencies.kt`, `WizardNavigation.kt` | Jeden `WizardViewModel` na navigační graf Wizardu vlastní `WizardController` (WP06) a `WizardDraft`, skutečný příznak dirty pro 32, navigaci podle stavu relace (jedna žádost najednou, spotřebovaná jednou), Wizard stranu `SaveOutcomeSource` a best-effort `wizard-close`. Závislosti z `AppContainer`, v testech přes `LocalWizardDependencies`. |
| Obrazovky | `GateScreen.kt` (11), `UrlScreen.kt` (12, 40), `HeadersScreen.kt` (13), `SecretScreen.kt` (14), `IdentityScreen.kt` (15), `FinishScreen.kt` (16), `SavedScreen.kt` (17), `ErrorScreen.kt` (18), `ExpiredScreen.kt` (47), `WizardCommon.kt` | Bezstavové obrazovky (stav + callbacky) a trasy, které je spojují s ViewModelem. Jen komponenty a téma WP07. |
| Diagnostika | `main/…/ui/screens/diagnostic/HttpDiagnosticText.kt`, `DiagnosticScreen.kt` | 30 a 31: port `diagnosticDisplayText` a `diagnosticSizeKiB`, zdroj dat, poznámky, záložky. |
| Sdílený kontrakt | `main/…/ui/screens/save/SaveOutcomeSource.kt` | Rozhraní mezi ukládajícími toky a sdílenými obrazovkami 33 a 46 (WP10), viz níže. |
| FLAG_SECURE | `main/…/ui/screens/secure/SecureWindow.kt`, `res/values/ids_wp09.xml` | Počítaný příznak okna, viz níže. |
| Graf | `main/…/ui/navigation/WizardGraph.kt` | Zástupné obrazovky nahrazené trasami WP09, `PLACEHOLDER_HAS_DRAFT` už Wizard nepoužívá. |
| Texty | `res/values/strings_wp09.xml`, `res/values-cs/strings_wp09.xml` | 100 klíčů `wp09_*` (EN a CS se shodnými klíči), jen texty, které návrh nemá a smlouva vyžaduje. `strings.xml` se neměnil. |
| Testovací služba | `debug/…/debug/FakeWizardService.kt` | Skriptovaná náhrada Wizard části služby nad `FakeIpcTransport` (debug zdrojová sada, sdílená JVM a instrumentovanými testy). |
| Bridge | `bridge/…/controller/WizardController.kt` | Čtyři malé změny pro paritu s desktopem, viz Odchylky 1. |

## Obrazovky a implementovaná pravidla

| ID | Pravidla |
|---|---|
| 11 Before setup | Jen při aktivním nebo čekajícím spojení: `wizard-open` hlásí stav VPN, odpojený klient bránu přeskočí (trasa 11 se nahradí 12, žádný `disconnect`). „Disconnect and continue“ pošle právě jeden `disconnect`, čeká na potvrzený `disconnected` a `idle` (čtení `get-status` po 500 ms) a pak pošle právě jeden `wizard-resume`; brána nezůstává na zásobníku. Už odpojený klient po cizím Connectu dostane „Continue“ (jen resume). „Keep connected“ a Back opouštějí Wizard (32 při neuloženém draftu). Text po cizím Connectu: „A VPN connection was started while setup was open…“. Chyba odpojení zůstává na bráně. |
| 12 Server address | `wizard-open` při vytvoření relace, Verify = lokální validace a jeden `wizard-check-url`; obrazovka se mění až podle skutečného výsledku (navigace sleduje stav kontroleru). `MODIFY-THIS-URL` je odmítnut před sítí vlastní radou, neplatná URL (userinfo, query, fragment, port, schéma) také. Pole URL je zvýrazněné pro `config_invalid_url` a `server_version_incompatible`, nikdy pro chybu transportu nebo timeout. Nečitelná konfigurace ukáže kartu „This configuration needs attention“ s „Start with defaults“ a „Inspect configuration“, selhané otevření „Try again“. Odkaz na 13. Back je opuštění Wizardu (desktopový Back je v prvním kroku zakázaný). |
| 40 Reset wizard | Dialog nad 12: „Start over“ pošle `wizard-reset`, draft převezme výchozí hodnoty služby (placeholder URL, výchozí secret, automatická IP, výchozí klíč), označí se dirty, kontroly se zneplatní; soubor se mění jen přes Finish. Neplatný detail HTTP se před resetem uklidí jedním `cancel-attempt`. |
| 13 HTTP headers | Kopie hlaviček draftu (LF pro zobrazení), „Use in wizard“ = sdílený validátor, do draftu přijde kanonické CRLF a změna zneplatní všechny kontroly (krok se vrátí na URL). Chyba uvádí řádek a případně název hlavičky, nikdy hodnotu. Reset načte výchozí šablonu jen do textu editoru. Cancel a Back zahodí jen úpravu hlaviček (32, když nějaká je). Text úpravy je ve ViewModelu, ne v saved state (může obsahovat cookies). FLAG_SECURE. |
| 14 Server secret | Vlastní nebo výchozí secret; přepínač výchozího vypne pole a dá do něj veřejně známý default, vypnutí ponechá zobrazený text. `SecretField` je skryté s dočasným Show. Secret s řídicími znaky se edituje jako JSON literál se zvláštním popiskem a nápovědou. Notice „The default is not private“ je vždy vidět jako v návrhu. „Server URL verified“ s hostem po úspěšném prvním kroku. Back = krok 12 s draftem. FLAG_SECURE. |
| 15 IP and access key | Automatická IP bere jen návrh poslední úspěšné kontroly secretu a vypne pole; bez návrhu vysvětlení a Verify odmítne lokálně (`wizard_prerequisite_failed`). Výchozí klíč se počítá sdílenou čistou funkcí při každé změně IP (neplatná IP ho vyprázdní), pole je vypnuté a ukazuje náhled klíče; služba ho ověří v `wizard-check-access`. Vlastní klíč je skrytý `SecretField`. Vždy viditelné varování o zahazovaných zprávách před akcí, věta, že návrh IP není rezervace. FLAG_SECURE. |
| 16 Complete setup | Existující, chybějící a přidávané výchozí kontrakty z původního DOM (`.2` až `.15` bez vlastní IP a existujících peerů, 13 nebo 14), volba se pošle až s `wizard-finish`; mimo `10.A.B.0/24` je volba vypnutá s vysvětlením a request má `createDefaultContracts=false`. Finish je jeden atomický zápis; chyba zůstane na 16 s draftem, nejasný výsledek vede na 33 a 16 v tom stavu nabízí „Verify save“ (jeden `wizard-get-commit`, nikdy druhý Finish). Během commitu nejde Back ani Cancel. |
| 17 Configuration saved | Vlastní stav (souhrn bez tajemství předaný z potvrzeného Finish), Wizard graf je pryč. Connect je výslovná volba: `VpnService.prepare` (03), po souhlasu Connect a Connection (04), odmítnutí vede na 37; „Connect later“ jen na Connection; „Review contracts“ otevře 19. |
| 18 Verification error | Nad krokem, jehož kontrola selhala (zobrazí se jednou na neúspěšný pokus). Varianty: `crypto_tag_mismatch` („Secret does not match“), `server_version_incompatible` (jmenuje verzi aplikace i serveru, serverová jen v sanitizované podobě `[0-9A-Za-z.+-]{1,32}`), timeout (vstupy zůstávají platné, žádné pole se neoznačí), `server_unauthorized` u klíče, `wizard_contract_self_conflict`, obecná chyba podle kódu a akce. Text vždy z resources podle kódu, nikdy z popisu služby, secret se neopakuje. „Correct …“ a Back vrátí na správný krok s označeným polem. „HTTP error details“ jen když probe zachytil výměnu. |
| 47 Session expired | Vypršení pronájmu (60 s bez kontaktu) nebo `wizard_session_expired` nahradí celý Wizard; staré důkazy se nemohou použít k uložení (draft je zahozen s relací). „Reopen wizard“ otevře novou relaci, „Inspect configuration“ editor. |
| 32 Unsaved changes | Skutečný dirty příznak: čerstvě načtený draft je čistý, každá editace, přepnutí, Reset a změna IP ověřeným návrhem ho nastaví. Back na 11 a 12 a „Cancel setup“ na všech krocích se ptají jen při změnách; čistý Wizard odchází hned. |
| 30, 31 HTTP response, request | Zdroj Wizard: detail živé relace (zmizí s invalidací pokusu jedním `cancel-attempt`); zdroj Connect: `httpDiagnostic` ze statusu (nebo z průběhu vlastního Connectu) připnutý k pokusu (`connectionEpoch`, `connectAttemptId`), jiný pokus se nikdy nevydává za tento. Záložky Request a Response, štítky původu (`winhttp` = zachyceno transportem, `merged`, `application`, `available`, `unavailable`), poznámky o neúplnosti, HTTP/?, zkrácení (63 KiB + 1 KiB, počet vynechaných bajtů) a `\xHH`. Obsah je jen text (`CodeBlock`), bez interpretace a bez automatického kopírování. Zavření pohledu detail neruší (smlouva: X dovolí znovuotevření). |

Společné: lease s `wizard-keepalive` po 15 s, jeden ruční pokus na Verify bez automatického opakování,
změna epochy připojení (cizí Connect) vrátí Wizard na 11 se zachovaným draftem a všemi kontrolami pending,
zavření Wizardu (Cancel, Discard, Keep connected, opuštění grafu) pošle jeden best-effort `wizard-close`
s rozpočtem 1 s na procesovém scope; po potvrzeném Finish a po vypršení se znovu neposílá.

### Navigace

Stav relace určuje krok, obrazovky jen následují: krok, který už je na zásobníku, se obnoví (vše nad ním
zmizí, např. 18 nebo brána po přerušení), nový krok se položí nad předchozí, 18 se kreslí nad krokem, 17 a 47
nahrazují celý graf. Žádost o navigaci spotřebuje jen jedna obrazovka; žádosti vzniklé, když je nahoře cizí
obrazovka (30, 33), počkají na další obrazovku Wizardu. Back na 14 až 16 je desktopový Back (předchozí krok,
draft zůstává, bez sítě), viz Odchylky 2.

## `SaveOutcomeSource`: kontrakt pro WP10

Soubor `ui/screens/save/SaveOutcomeSource.kt` (popis je i v hlavičce souboru):

```kotlin
enum class SaveOutcomePhase { IDLE, UNKNOWN, CHECKING, CONFIRMED, NOT_SAVED, CONFLICT }
data class SaveOutcomeState(phase, canRepeat, draftChanges: Int?, errorCode: String?,
                            serviceRestarted, configAvailability: ConfigAvailability?, configPath: String?)
interface SaveOutcomeSource {
    val origin: SaveOrigin
    val state: StateFlow<SaveOutcomeState>
    fun verify(): Boolean       // "Verify save" na 33: jedna kontrola, nikdy druhý zápis
    fun repeatSave(): Boolean   // "Save draft again" na 46, jen když canRepeat
}
object SaveOutcomeRegistry { fun register(source): () -> Unit; fun source(origin): SaveOutcomeSource? }
@Composable fun rememberSaveOutcomeSource(origin: SaveOrigin): SaveOutcomeSource?
fun NavController.followSaveOutcome(origin: SaveOrigin, phase: SaveOutcomePhase)
```

- Každý ukládající tok implementuje zdroj ve stavovém držáku, který vlastní draft, a registruje ho po celou
  dobu života draftu (Wizard: `WizardViewModel` v `init`, odregistrace v `onCleared`), ne podle složené
  obrazovky, protože 33 se kreslí nad původem.
- Sdílená obrazovka (WP10) si zdroj vezme přes `rememberSaveOutcomeSource(route.origin)`, vykreslí `state`
  (UNKNOWN: 33 s povoleným „Verify save“, CHECKING: akce vypnuté) a při každé změně fáze zavolá
  `followSaveOutcome`: CONFIRMED → Wizard 17 (`finishWizard`), ostatní 25 (`showSaved`); NOT_SAVED → Wizard zpět
  na 16 s draftem a chybou, ostatní 46; CONFLICT → 46; IDLE, UNKNOWN, CHECKING zůstávají.
- `verify()` vrací false, když teď nelze nic kontrolovat (kontrola běží, žádný nejasný zápis). Wizard: jeden
  `wizard-get-commit` pro původní `commitId`; po restartu služby jednorázový `get-config-state`, stav pak nese
  `serviceRestarted`, `configAvailability`, `configPath` (text pro 33 je `wp09_finish_restarted`).
- Wizard nikdy neopakuje zápis z 33 ani 46: `canRepeat` je vždy false a `repeatSave()` vrací false. Selhaný
  receipt je autoritativní „neuloženo“ a vede zpět na 16, kde je Finish nová výslovná publikace s novým
  `commitId`. 46 tedy Wizard nepoužívá.
- „Inspect current configuration“ je jen navigace do editoru (`openEditor()`).
- Bez registrovaného zdroje (obnovený proces, původ už zavřený) sdílená obrazovka ukáže nejasný stav bez
  kontroly a nabídne jen prohlídku; nikdy netvrdí úspěch ani neúspěch.

## FLAG_SECURE

`SecureWindow()` (`ui/screens/secure/SecureWindow.kt`) zavolá obrazovka s tajemstvím jednou. `DisposableEffect`
najde aktivitu z `LocalView` a přes `SecureWindowFlags.acquire/release` počítá reference v tagu decor view
(`R.id.secure_window_count`): první uživatel přidá `WindowManager.LayoutParams.FLAG_SECURE`, poslední ho odebere,
pokud ho okno nemělo už předtím. Přechod 14 → 15 tak příznak nikdy nesundá. Použito na 13, 14 a 15; WP10 ho může
použít beze změny na 20, 21 a 23. `LocalSecureWindowEnabled` vypínají jen screenshot testy (jinak by 13 až 15 byly
černé); test příznaku (`WizardFlowTest`) ho nechává zapnutý a kontroluje skutečné `window.attributes.flags`.

## Testy

Příkazy (JBR 25, Gradle 9.8.2, offline, Windows 11): `gradlew :app:assembleDebug :app:testDebugUnitTest
:bridge:testDebugUnitTest` a s `ANDROID_SERIAL=emulator-5562` `gradlew :app:connectedDebugAndroidTest`.
Všechny **BUILD SUCCESSFUL**, bez varování Kotlinu.

| Třída | Testů | Výsledek | Obsah |
|---|---|---|---|
| `wizard.WizardDraftTest` | 9 | PASS | 1:1 polní případy `test_wizard_model.cpp` (`DefaultKeyTransitionsAndExistingValue`, `HttpHeadersCanonical…`, `ExactSecretAndLiteralRoundTrip`, `ContractsAndResumePreserveDraft`) a lokální validace, návrh IP, chybějící kontext, kopie a mazání tajemství |
| `wizard.WizardRulesTest` | 6 | PASS | výchozí klíč, klientské adresy, kandidáti kontraktů, `parseServerUrl`, literál secretu, shoda validátoru hlaviček s `HttpHeaders.normalize` |
| `wizard.WizardViewModelTest` | 23 | PASS | nad `FakeWizardService` na virtuálním čase: hlavní cesta s navigací, lokální validace, každá chybová větev 18 a označené pole, timeout bez retry, invalidace HTTP detailu (`DiagnosticCorrelates…` strana ViewModelu), vypršení (47) lokálně i službou, 33 s receiptem confirmed, failed, unknown a po restartu, brána se stavovými přechody, cizí Connect, Reset, hlavičky, close, kontrakty a Back, nepodporovaná síť, chyba otevření |
| `diagnostic.HttpDiagnosticTextTest` | 7 | PASS | UTF-8 a konce řádků, `\xHH` pro neplatné a řídicí bajty, zkrácení s odděleným dekódováním, neplatné délky, nejhorší případ 262166 jednotek, KiB |
| `Wp09StringResourcesTest` | 4 | PASS | shodné klíče EN a CS, prefix `wp09_`, argumenty, každý klíč použitý |
| ostatní testy `app` (WP06, WP07) | 12 | PASS | beze změny |
| `bridge` (WP06) | 102 | PASS | včetně `WizardControllerTest` po změnách kontroleru |
| `WizardFlowTest` (Compose UI, emulátor) | 15 | PASS | hlavní cesta 12 → 14 → 15 → 16 → 17 s FLAG_SECURE na 14 a 15 a bez něj na 12 a 16, 18 bez secretu a návrat, 18 → 30 → 31 → 18 → 12, verze serveru, Back na 12 a 32 (Keep editing, Discard s `wizard-close`), Back na 14 a Cancel s 32, čistý Cancel, brána (jeden `disconnect`, jeden resume), přeskočení brány, cizí Connect, 47 a nová relace, 33 a Verify save → 17, Reset na placeholder, 13 s chybou a FLAG_SECURE, Connect diagnostika na 30 |
| `WizardScreenshots` (emulátor) | 1 | PASS | 27 stavů × 3 varianty = 81 PNG |

Instrumentované testy běžely na `emulator-5562`. AVD `phpvpn-api35` už běželo na `emulator-5558` u jiného agenta
(a nebylo `-read-only`), proto WP09 použil vlastní kopii `phpvpn-api35-wp09` (stejný systémový obraz
`android-35/google_apis/x86_64` a stejný `config.ini`). Emulátor je po skončení zastavený.

| Položka | Stav | Důvod |
|---|---|---|
| T-A06 (hlavní cesta Wizardu a chyby 18, 30, 31 proti lokálnímu serveru) | NOT RUN | Patří WP11; WP09 testuje proti `FakeWizardService`, skutečný JNI transport (WP04) v aplikaci ještě není. |
| 17 → 03 → 04 → 01 (souhlas a Connect) | NOT RUN | Systémový dialog a služba VPN jsou WP05, obrazovky 04 a 01 WP08; UI test ověřuje jen 17 a „Connect later“. |
| Sdílené obrazovky 33 a 46 | NOT RUN (WP10) | Wizard naviguje na `SaveUnknown(Wizard)` (ověřeno), obrazovku a její napojení na `SaveOutcomeSource` dodá WP10. |
| Vstup z 00 Welcome a 06 do 12 | NOT RUN | 00 a 06 jsou WP08 (zástupné); vstupní funkce `openWizardUrl` a `openWizard` jsou beze změny. |
| API 34 a 36, reálné zařízení | NOT RUN | Zadání určilo jeden emulátor API 35. |
| TalkBack, klávesnice, české screenshoty | NOT RUN | Mimo rozsah zadání; role a popisy dávají komponenty WP07. |
| Port `test_wizard_layout.cpp` | NOT RUN (nepřenositelné) | Jde o Win32 geometrii patičky; na Androidu ji nahrazuje `ScreenScaffold` a screenshoty 320 dp / 200 %. |

## Screenshoty

`work/wp09` (gitignored): pro 11 (a přerušení), 12 (a placeholder, chyba načtení), 13 (a chyba), 14 (a výchozí
secret), 15 (a ruční zadání, kontrola), 16 (a nepodporovaná síť, nejasný zápis), 17, 18 (secret, verze, HTTP,
timeout, přístup), 30, 31, 30 bez dat, 32, 40 a 47 vždy `*-412.png`, `*-412-full.png` (celá výška stránky) a
`*-320-fs200.png`. Porovnáno okem s `assets/android-en/previews/full`:

- Shoda: lišta s Back a titulem, stepper „PHONE SETUP / Step n of 4“, nadpisy a úvody, karta HTTP headers
  s chevronem, notice v barvách návrhu, přepínače, Show u secretu, tlačítka patičky mimo posuvnou oblast, hero
  17 se štítkem SAVED, karta serveru, 18 s chybovou notice, 30/31 se záložkami a kódem, dialogy 32 a 40.
- Opraveno po porovnání: 14 zobrazuje notice „The default is not private“ vždy (dříve jen při výchozím
  secretu); 15 při výchozím klíči ukazuje náhled klíče místo nefunkčního skrytého pole s vypnutým Show; řádek
  „HTTP error details“ na 18 je jako v návrhu samostatný, ne v kartě; hodnota „Reconstructed by the app“ se na 31
  opakovala v kartě, nyní „Application headers only“.
- 320 dp a 200 %: bez horizontálního posuvu a bez ořezu, dlouhé texty se zalamují, obsah se posouvá svisle.

## Odchylky

1. **Změny v `WizardController` (bridge, WP06).** `WizardUiState.commitChecking` (běžící kontrola receiptu pro
   fázi CHECKING), `back()` maže chybu opuštěného kroku jako `WizardModel::back`, `reset()` nejprve pošle jeden
   `cancel-attempt` pro neplatný HTTP detail (dříve se při resetu neposlal, protože kanál držel reset),
   `interrupt()` zapomene známé odpojení, takže okamžité „Disconnect and continue“ po cizím Connectu nejdřív
   odpojí místo holého resume. Testy WP06 beze změny prochází.
2. **Back na 14 až 16 je předchozí krok, ne 32.** Mapa uvádí Back na 12–16 → 32. Desktopový Back zachovává draft
   a nic nezahazuje, proto Back na 14–16 vrací krok (bez dialogu), 32 se ptá všude, kde by se draft ztratil:
   Back na 11 a 12, „Cancel setup“ na všech krocích, Back/Cancel na 13 (jen úprava hlaviček).
3. **18 neukazuje pole secretu** (návrh ho kreslí): secret se na 18 neopakuje ani maskovaně, „Correct the secret“
   vrací na 14, kde je pole označené. 18 proto nepotřebuje FLAG_SECURE.
4. **15 a 16 mají „Cancel setup“** (návrh jen primární akci), aby šel Wizard zrušit z každého kroku.
5. **Výchozí přístupový klíč je na 15 vidět** jako náhled (vypnuté pole, monospace); je veřejně odvoditelný
   z IP. Vlastní klíč zůstává skrytý se Show.
6. **FLAG_SECURE i na 13**: hlavičky mohou obsahovat cookies (návrh: „Treat possible sensitive header content
   accordingly“).
7. **Connect ze 17** volá `VpnService.prepare` a po souhlasu `ConnectionRepository.connect()` na procesovém scope;
   start přes foreground službu (`ACTION_CONNECT`, WP05) zatím neexistuje a jeho napojení je jediné místo
   (`WizardSavedRoute`).
8. **16 v nejasném stavu nabízí „Verify save“ přímo** (stejná jediná kontrola jako 33), aby šlo receipt ověřit i
   po Back z 33 a než WP10 dodá sdílenou obrazovku.
9. **Verze aplikace v 18** pochází z `versionName` (stejný zdroj jako `PHPVPN_CLIENT_VERSION`,
   `client/CMakeLists.txt`); z popisu služby se bere jen sanitizovaná verze serveru.
10. **Detail Connectu se při zavření 30/31 neruší.** `dismiss-connect-diagnostic` patří zavření chybového panelu
    06 (WP08, `ConnectionRepository.dismissDiagnostic`); samotné zavření detailu podle smlouvy dovolí znovuotevření.
11. **Texty mimo návrh** (100 klíčů `wp09_*`, EN i CS) dodal WP09; česká verze nemá předlohu v `assets/android-cz`.
12. **`FakeWizardService` je v debug zdrojové sadě** (sdílí ji JVM i instrumentované testy, release ho nemá).
13. **Mazání tajemství je best effort** jako ve WP06: draft drží secret a klíč v `SecretText`, ale Compose
    `TextField` pracuje s neměnným `String`, takže kopie hodnoty pole zůstávají GC. Nic se neloguje, testy
    porovnávají tajemství bez výpisu hodnot.
14. **Hlavní dispatcher vykonává IPC okamžitě** (`Dispatchers.Main.immediate` a synchronní falešný transport):
    UI test odhalil, že odpověď Resetu mohla dorazit dřív, než ViewModel Reset zaznamenal; opraveno (Reset se
    označí před voláním), JVM testy s `StandardTestDispatcher` to neukázaly.

## Doplněk: `WizardFlowTest` při 320 dp a písmu 200 % (po sloučení s WP10)

WP10 hlásil, že 4 z 15 testů `WizardFlowTest` při 320 dp / 200 % neprojdou (hlavní cesta, diagnostika Connect,
editor hlaviček, HTTP detail). Reprodukováno po `git merge android` (tip `f6d5134`) na `phpvpn-api35-wp09`
(`emulator-5562`, `wm density 540`, `font_scale 2.0`): stejné čtyři testy selhaly.

**Příčina byla v testech, ne v obrazovkách.** Při 320 dp a 200 % leží většina obsahu pod připnutou patičkou a je
dosažitelná jen svislým posunem (pravidlo návrhu: posouvá se jen tělo, patička zůstává). Testy kontrolovaly
`assertIsDisplayed()` u uzlů mimo viditelnou část (URL na 17, „HTTP status: 200“ na 18, kód odpovědi na 30) a
klikaly na řádek „HTTP headers“ na 12, aniž by ho posunuly na obrazovku; klik pak dopadl na patičku a 13 se
neotevřelo. Při 411 dp se vše vešlo do první obrazovky, proto tam testy procházely. Obrazovky se neměnily:
nic nepřetéká vodorovně a vše je posunem dosažitelné (ověřeno i screenshoty `*-320-fs200.png`).

Oprava (`WizardUiTestBase`, `WizardFlowTest`): `scrollIntoView()` posune tělo k uzlu (uzly mimo posuvný kontejner,
patička a lišta, zůstanou) a `shown()` = posun + `assertIsDisplayed()`; všechny kontroly viditelnosti obsahu a
všechna kliknutí na obsah je používají.

| Sada | 411 dp, písmo 100 % | 320 dp (`wm density 540`), písmo 200 % |
|---|---|---|
| `WizardFlowTest` (WP09) | 15/15 PASS | 15/15 PASS (předtím 11/15) |
| `WizardSaveOutcomeUiTest` (WP10) | 2/2 PASS | 2/2 PASS |
| `:app:testDebugUnitTest` | PASS | – |

Emulátor je po běhu vrácený na výchozí hustotu a písmo a zastavený.
