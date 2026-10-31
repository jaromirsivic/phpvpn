# WP08: obrazovky Connection, Traffic, Server a Settings, report 2026-10-31

Výsledek pracovního balíku WP08 podle [plan-android-v001.md](../../../Plans/2026-10-31-09-40-08/plan-android-v001.md)
(krok 8, „Kotlin část“, „Životní cyklus připojení na Androidu“ kroky 1, 4, 5, 8, 9, testy T-U01 a T-U02) a
[android-screen-map.md](../../../Plans/2026-10-31-09-40-08/android-screen-map.md). Všechny výsledky jsou z JVM testů
a z vlastního emulátoru `phpvpn-api36-wp08` (kopie konfigurace `phpvpn-api36`, x86_64, API 36, 1080 × 2400 px,
420 dpi, port 5560), žádný není z reálného zařízení. Nativní runtime (WP04) ani služba (WP05) ještě neexistují,
všechny obrazovky proto běží nad `FakeIpcTransport`. Co se nespustilo, je NOT RUN s důvodem.

## Co vzniklo

Vše je v `client/android/app`. Nezměněné zůstaly komponenty, téma, `strings.xml`, `Routes.kt`, `NavActions.kt`,
`NavShared.kt`, `PhpvpnNavHost.kt` a grafy WP09/WP10. Žádná nová knihovna.

| Část | Soubory | Obsah |
|---|---|---|
| Stavové držáky | `ui/state/*.kt` | `ConnectionViewModel` (+ čisté mapování `ConnectionStateMapper`), `TrafficViewModel`, `ServerViewModel`, `SettingsViewModel`, `ExitViewModel`, `ConfigStateViewModel` (00 a 36), `StartDestinationViewModel`, `ConfigSummaryRepository`, `UiFormat` |
| Rozhraní pro WP04/WP05 | `ui/state/Platform.kt`, `ui/state/ServerInfoModel.kt` | `VpnConsent` + `SystemVpnConsent`, `ConnectActions` + `RepositoryConnectActions`, `AlwaysOnStatus`, `ConnectionUiEvents` + `DisconnectReason`, `ServerInfoModelSource` + `KotlinServerInfoModel`, `TablePagination` |
| Obrazovky | `ui/screens/connection`, `traffic`, `server`, `settings`, `common` | viz tabulka níže |
| Navigace | `ui/navigation/MainGraph.kt` | zástupné obrazovky WP07 nahrazené skutečnými, rozhodnutí o startovní obrazovce |
| Kontejner | `AppContainer.kt`, `PhpvpnApplication.kt` | nové členy `vpnConsent`, `alwaysOn`, `serverInfoModel`, `connectActions`, `configSummary`, `connectionEvents`; `PhpvpnApplication` je `open` s `createContainer()`/`createTransport()` a `resetContainer()` pro testy |
| Texty | `res/values/strings_wp08.xml`, `res/values-cs/strings_wp08.xml` | 55 klíčů, které návrh nemá (viz Odchylky) |
| Build | `app/build.gradle.kts` | `buildConfig = true` (About), runner `phpvpn.android.PhpvpnTestRunner`, sdílené `src/testShared/kotlin` pro JVM i UI testy |

Získání kontejneru: `context.appContainer`, ViewModely přes `appViewModel { container -> … }`
(`ui/screens/common/ScreenSupport.kt`), vždy s vlastníkem záznamem back stacku. Obrazovky sbírají stav jen přes
`collectAsStateWithLifecycle` a ViewModely sdílejí stav s `SharingStarted.WhileSubscribed()`, takže `get-status`
se volá jen při stavu STARTED (rozvrh 500 ms / 5 s je repozitáře z WP06). Texty jsou výhradně z resources,
chyby výhradně přes `ErrorTexts`/`ErrorStrings` podle kódu; `UiSourceRulesTest` prochází.

## Obrazovky

| ID | Kde | Implementace | Pravidlo návrhu / plánu |
|---|---|---|---|
| 00 | `WelcomeScreen` | Start: jeden `get-config-state` na aktivitu (`StartDestinationViewModel`); `missing` → `showWelcome()`, Welcome je kořen, Connection zůstává domovem po návratu z toků. „Open configuration editor“ čte `get-config-state` znovu: `missing` → `CreateDefaultConfig(skutečná cesta)` (36a, WP10), čitelný → editor, jinak 36. | Žádný účet ani adresář serverů; pozdní odpověď nikdy neodvede uživatele z jiné obrazovky. |
| 01 | `ConnectionScreen` | Jen `healthyConnected` (connected + running + tunnel yes + bez chyby). Doba z `connectionStartedAt`, tik 1 s jen ve STARTED; čítače IPv4 ze statusu; karta serveru z konfigurace; vstup do 48. | „Connected jen pro potvrzený stav“, žádný Connect při otevření detailu. |
| 02 | `ConnectionScreen` | Connect: nejdřív `VpnConsent.isGranted()`, jinak systémový dialog (03) přes `rememberVpnConsentLauncher`; povoleno → `ConnectActions.connect()`, zamítnuto → 37. Text „Another VPN app took over“ (onRevoke) a „The connection ended“ (R9) z `ConnectionUiEvents`, smaže se dalším Connect. Odmítnutý Connect: `config_*`/`contract_missing` → 36, `tunnel_permission_missing` → 37, jinak chybová notice podle kódu. „Configuration ready“ jen pro `availability == valid`, jinak vstup do 36. | Při uděleném souhlasu se 03 přeskočí; uložená konfigurace není důkaz dostupnosti serveru. |
| 03 | systém | Jen `SystemVpnConsent` (`VpnService.prepare`). | Dialog vlastní Android. |
| 04 | `ConnectionScreen` | Čtyři kontroly z vlastního průběhu (`ProgressModel`) nebo ze statusu; první otevřená kontrola „Verifying…“, další „Waiting“, nikdy pending jako úspěch. Notice „You can leave this screen“. Selhané čtení během pokusu = varování „Progress not confirmed“ (text `status_unavailable`), ne výsledek. Cancel = `disconnect`. | Desktop `progress_window.cpp`: neúspěch ani nedostupnost se nezobrazí jako úspěch. |
| 05 | `ConnectionScreen` | `connected` + `reconnecting`, `recovering` a také connected bez připraveného tunelu. „View last error“ ukáže text podle `lastError.code`. Disconnect dostupný. | Odlišné od 01, nic se neslibuje. |
| 06 | `ConnectionScreen` | Titulek/akce/zpráva jen z allowlistu `progressErrorText` (neznámý kód → communication_failed). Vstupní chyby (secret, access key, kontrakty, verze) mají jiný eyebrow a notice než transportní; vstupní → „Check configuration“, transportní → „Check server URL“ (obě 06 → 12). „HTTP error details“ jen když existuje `httpDiagnostic` (podmínka shodná s `ProgressModel`). U `server_version_incompatible` verze aplikace z BuildConfig. | Timeout není důkaz špatného tajemství. |
| 07 | `ConnectionScreen` | Vlastní Disconnect nebo `phase == stopping`; polling 500 ms; potvrzení `disconnected/idle/tunnel no` → 02 a průběh se uzavře. | Nikdy falešné potvrzení. |
| 08 | `ConnectionScreen` | `ConnectionStatus.Unavailable`; poslední známý stav jako nápověda (connected s časem začátku spojení, jinak čas, kdy byl stav viděn), vstup do posledních statistik; „Check status again“ = mimořádné čtení, nic nespouští. | Další čtení vede na libovolný potvrzený stav. |
| 10 | `SettingsScreen` | Wizard: potvrzeně odpojený → 12 přímo, jinak 11 (gate WP09). Kontrakty, editor (`openEditor`, 36a řeší WP10), About, Exit, „Battery optimisation“ (`ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS`), „Always-on VPN“ (stav z `AlwaysOnStatus`, klepnutí `ACTION_VPN_SETTINGS`), upozornění při lockdownu. | Žádné profily, země, účty; Always-on se nezaměňuje s `autoConnectOnStartup`. |
| 26/27/43 | `TrafficScreen` | Všech deset polí (IPv4 bajty a pakety po směrech, HTTP těla požadavky/odpovědi/celkem 65bitově, dotazy prázdné/s daty/celkem), čísla a velikosti podle locale (`formatStatisticsBytes` s místní desetinnou čárkou). 27 = varování a poslední hodnoty, 43 = jen pomlčky. | Bez rychlosti, pingu, historie a grafu; chybějící data nejsou nula. |
| 28 | `ServerScreen` | Souhrn (plurály), čas posledního obnovení, řádky: adresa, štítek Online (platný kontrakt zelený, neplatný jantarový) / Offline, „Contract with <lokální IP>“, platnost slovy, „Last seen“; lokální klient vynechán; stránkování 20 řádků (`table_pagination.h`); auto refresh vypnutý ve výchozím stavu, 5 s po dokončení, jen při zobrazení; Refresh now. | Přítomnost a platnost kontraktu jsou oddělené. |
| 29 | `ServerScreen` | Stejný snímek jako 28. Víceřádkový plaintext (PHP server odsazuje) se zobrazí doslova; jednořádkový se odsadí z přísného stromu (jména, pořadí a číselné literály beze změny). | Bez přejmenování polí. |
| 44 | `ServerScreen` | Platná odpověď jen s lokálním klientem: „No records yet“ + notice, Refresh zůstává. | Prázdný seznam ≠ chyba. |
| 45 | `ServerScreen` | S daty: varování s časem posledního úspěchu, štítky „Last status: online“, Retry. Bez dat: chyba podle kódu (včetně `server_version_incompatible` s verzí aplikace), vysvětlení, Retry, žádný seznam. | Poslední úspěch se ponechá. |
| 34 | `AboutScreen` | Verze z `BuildConfig.VERSION_NAME`, verze nativního jádra (`NativeBridge.version`, při chybě „Unavailable“), typ buildu, Always-on, licence Inter z `res/raw/inter_license.txt` (načtená mimo hlavní vlákno). | Nic nevymyšleného, poznámka „visual concept“ návrhu se nezobrazuje. |
| 35 | `ExitDialog` | „Close window, stay connected“ = `finish()`. „Disconnect and close“: při potvrzeném odpojení rovnou `finishAndRemoveTask()`, jinak jeden `disconnect` přes `ConnectActions` a čekání (max. 30 s) na potvrzený stav; nepotvrzeno → zavřít dialog a ukázat Connection (07/38). | Proces se nikdy násilně neukončuje (krok 8). |
| 36 | `ConfigAttentionScreen` | Podle `get-config-state`: missing (Wizard primárně, „Create default configuration and edit“ → 36a s cestou), unreadable/encoding_error/too_large (bez opravy, jen Wizard), invalid/repairable (oprava v editoru, výchozí v průvodci, důvod podle `reasonCode`), valid, nedostupná služba (Check again). Skutečná cesta k souboru. | Oprava jen pro čitelný dokument. |
| 37 | `PermissionDeniedScreen` + varianta v Connection | Route `connection/permission` sdílí ViewModel s Connection; Retry znovu volá `VpnConsent.request`, povolení → Connect a návrat; další zamítnutí zůstane na 37; „Stay disconnected“ → zpět. Stav s `tunnel_permission_missing` vykreslí stejný obsah přímo v Connection s Retry. | Zamítnutí není chyba serveru, konfigurace zůstává. |
| 38 | `ConnectionScreen` | Timeout nebo chyba vlastního Disconnect, nedostupné čtení během něj; Back i „Check status“ zavřou průběh a ukážou skutečný stav, „Try disconnecting again“ pošle nový Disconnect. | Žádné falešné potvrzení. |
| 48 | `ConnectionChecksScreen` | Čtyři kontroly posledního statusu (živého nebo posledního známého s varováním), nadpis podle výsledku, karta serveru. Nikdy nepošle Connect. | Samostatný pohled jen pro čtení. |

## Rozhraní pro WP04 a WP05

```kotlin
// ui/state/Platform.kt
interface VpnConsent { fun isGranted(): Boolean; fun request(launcher: ActivityResultLauncher<Intent>): Boolean }
class SystemVpnConsent(context: Context) : VpnConsent      // VpnService.prepare
@Composable fun rememberVpnConsentLauncher(onResult: (Boolean) -> Unit): ActivityResultLauncher<Intent>
interface ConnectActions { suspend fun connect(): CommandResult; suspend fun disconnect(): CommandResult }
class RepositoryConnectActions(connection: ConnectionRepository) : ConnectActions
data class AlwaysOnState(val alwaysOn: Boolean, val lockdown: Boolean)
interface AlwaysOnStatus { val state: StateFlow<AlwaysOnState?> }    // null = „není hlášeno“, AlwaysOnStatus.UNKNOWN
enum class DisconnectReason { REVOKED, PROCESS_RESTARTED }
class ConnectionUiEvents { val reason: StateFlow<DisconnectReason?>; fun report(reason); fun clear() }

// ui/state/ServerInfoModel.kt
fun interface ServerInfoModelSource { fun build(result: ServerInfoResult): ServerInfoModelResult }
object KotlinServerInfoModel : ServerInfoModelSource       // zrcadlo buildServerInfoModel a parseInfoPlaintext
```

- **WP05** v `PhpvpnApplication.createContainer()` předá `AppContainer(transport, SystemVpnConsent(this),
  alwaysOn = <stav ze služby>, connectActions = { repo -> <obal, který před connect spustí foreground službu
  ACTION_CONNECT a po potvrzeném Disconnect ji zastaví> })`. `onRevoke()` volá
  `container.connectionEvents.report(REVOKED)`, R9 restart bez Connect `report(PROCESS_RESTARTED)`. UI příznak
  ukazuje jen ve stavu potvrzeného odpojení a nikdy ho nečte místo `get-status`.
- **WP04** předá JNI transport (`createTransport()`) a nativní `serverInfoModel`. Kontejner jím zároveň filtruje
  `ServerInfoRepository` (`ServerInfoHook`): snímek, který model neověří, je selhané obnovení (45), jako na desktopu.
  Stránkování `TablePagination` je port `table_pagination.h`; nativní model ho může dodat také.

## Testy

Příkazy (JBR 25, Gradle 9.8.2, AGP 9.5.0, `ANDROID_SERIAL=emulator-5560`):
`gradlew :app:assembleDebug :app:testDebugUnitTest` a `gradlew :app:connectedDebugAndroidTest`.
`ui-test-junit4` bylo nutné jednou stáhnout online (v offline cache chybělo).

| Test | Testů | Výsledek | Obsah |
|---|---|---|---|
| `ConnectionStateMapperTest` | 13 | PASS | 01 jen pro zdravé spojení, 05 pro reconnecting/recovering/nepřipravený tunel, 07, 02 s důvodem (REVOKED, PROCESS_RESTARTED), 04 s pořadím kontrol, 06 transport vs. vstup, s/bez diagnostiky, neznámý kód → communication_failed, `tunnel_permission_missing` → 37, 08 s posledním stavem, vlastní průběh Connect/Disconnect (38 při timeoutu i nedostupném čtení), 48, všech 81 × 2 kombinací kontrol |
| `ConnectionViewModelTest` | 9 | PASS | polling jen při odběru, karta serveru bez tajemství, 02 → 04 → 01 s 500 ms a návratem na 5 s, souhlas zamítnut → 37 bez `connect`, 01 → 07 → 02, timeout Disconnect → 38 → Check status, 08 → libovolný potvrzený stav, odmítnutý Connect (busy/config/permission), 06 s a bez diagnostiky |
| `DestinationViewModelsTest` | 12 | PASS | 43 → 26 → 27 (hodnoty zachované), 43 bez nul, 28 + čas + stejný snímek pro 29, 45 se zachovaným snímkem, 45 bez dat, 44, stránkování a auto refresh jen při odběru, Settings 11 vs. 12, 36 pro všech 7 dostupností a timeout, editor nikdy prázdný pro missing, start rozhodnut jednou, Exit obě volby, Exit při odpojeném stavu bez příkazu, Exit timeout nikdy nezavře |
| `ServerInfoModelTest` | 8 | PASS | 1:1 `ServerInfo_states_boundaries_count_sort_and_pages`, `ipc_exact_schema_and_private_snapshot`, `future_time_missing_contract_and_exact_status_text` (bez anglického textu), `independent_count_pluralization` (počty 168/84), ověření plaintextu, `table_pagination.h`, stránkovač ze `test_server_info_layout.cpp` pro 0–252 řádků, čitelné 29 |
| `UiFormatTest` | 4 | PASS | velikosti s místní desetinnou čárkou a 65bitovým součtem, přesná celá čísla, doba, čas s datem jen pro jiný den |
| `Wp08StringResourcesTest` | 4 | PASS | shodné klíče EN/CS, shodné argumenty, žádná kolize s `strings.xml`/`errors.xml`/`symbols.xml`, neprázdné a přeložitelné texty |
| Dříve existující (`StringResourcesTest`, `UiSourceRulesTest`, `ErrorResourcesTest`) | 12 | PASS | beze změny |
| **`:app:testDebugUnitTest` celkem** | **62** | **PASS** | WP08 přidal 50 |
| `MainFlowsTest` (Compose UI, T-U01 část) | 16 | PASS | 01 → 07 → 02; 02 → 04 → 01 s uděleným souhlasem (03 přeskočen); 06 bez diagnostiky; 06 s diagnostikou, změna na vstupní chybu a 06 → 12; 08 → 01; 37 Retry (druhé zamítnutí zůstane, povolení spustí Connect); 37 Stay disconnected; 38 → Check status; onRevoke na 02; 48 bez Connect; Back z Traffic, Server a Settings vede na Connection; Exit „Close window“ jen `finish`; Exit „Disconnect and close“ nezavře před potvrzením a po něm ano; Traffic 43 → 26 → 27 a Server 45 → 44 → 28 → 45 → 29; 00 při chybějící konfiguraci → 36a bez zápisu; 36 pro neplatný dokument a About |
| `Wp08ScreenshotTest` (T-U02 část) | 24 | PASS | 24 stavů × 6 variant (412, 360, 320 dp, písmo 100 % a 200 %) + spodek stránky při 412 dp, 167 snímků |
| **`:app:connectedDebugAndroidTest` celkem** | **40** | **PASS** | emulátor-5560 |

| Položka | Stav | Důvod |
|---|---|---|
| Skutečný souhlas s VPN (systémový dialog 03), `VpnService.prepare` na zařízení | NOT RUN | UI testy odpovídají na launcher testovacím `ActivityResultRegistry`; skutečný dialog a `tunnel_permission_missing` ověří T-A11 (WP05/WP11). |
| Skutečné připojení, onRevoke, R9, Always-on, notifikace | NOT RUN | služba WP05 a JNI WP04 neexistují; příznaky a stavy jsou testované přes rozhraní a falešný transport. |
| Odkazy do systémových nastavení (baterie, VPN) | NOT RUN | jen vizuální kontrola řádků; otevření stránky Androidu nebylo v testech klepnuto. |
| Česká lokalizace na emulátoru | NOT RUN | klíče a argumenty hlídá JVM test; vizuálně kontrolována jen angličtina. |
| TalkBack a ovládání klávesnicí | NOT RUN | sémantika z komponent WP07 (role, sloučené řádky, tagy s textem), ruční průchod nebyl proveden. |
| Snímky 360 dp a 200 % pro všechny obrazovky okem | částečně | uloženy všechny, prohlédnuty 412 dp všech hlavních stavů a vybrané 320 dp/200 % (01, 28, 29, 00, 38). |

## Porovnání snímků s návrhem

Snímky jsou v `work/wp08` (gitignored) jako `<id>-<název>-w<šířka>[-fs200].png` a `-w412-bottom.png`. Šířka je
nastavená přes hustotu (celá šířka displeje = cílová šířka v dp), takže jde o skutečné vykreslení aplikace,
ne o zmenšený snímek. Snímek je výřez obrazovky, ne celá rozvinutá stránka jako `previews/full`; spodek dlouhých
stránek je v `-bottom`.

- Shoda s `previews/full`: 01 (hero compact, karta serveru, dlaždice, vstup do kontrol, Disconnect v patičce),
  02, 06 (eyebrow, nadpis, notice, čtyři kontroly, vstupy, Try again), 08, 10, 26 (dlaždice, dvě karty, hint),
  28 (souhrn, řádky se štítky a platností), 29, 34, 35, 36, 38, 45.
- Rozdíly z obsahu, ne z vykreslení: 06 má nadpis a texty podle skutečného kódu (`endpoint_unavailable_or_invalid`
  místo ukázkového „Server not responding“); 28/45 ukazují „Last seen“ s datem, protože ukázková data jsou ze
  včerejška; 34 má navíc řádky Native core version, Build a Always-on; 36 má navíc kartu s cestou k souboru;
  35 je okno dialogu se skutečnými volbami místo „Preview notification“.
- Bez opravy ponecháno: při 320 dp a 200 % se „24 min 18 s“ zalomí mezi číslem a jednotkou (text je z
  `strings.xml`, který WP08 nemění); karta Auto refresh je o něco vyšší než v návrhu (komponenta `ToggleRow` má
  48 dp dotykový cíl). Horizontální posuv ani ořez se v žádné variantě neobjevil.

## Odchylky

1. **Server card (URL, virtuální IP, síť)** se čte z `get-config-document` (jednou při otevření Connection a po
   každém Connect), stejně jako desktop čte `get_config` při každém Connect. Dokument se hned po vyjmutí tří
   veřejných členů vymaže (best effort, viz WP06 odchylka 2). Bez konfigurace se karta nekreslí.
2. **Klasifikace Server Info v Kotlinu** do WP04: zrcadlo C++ kódu. Desktopové anglické texty
   (`serverInfoCountText`, `serverClientStateText`) nahrazují plurály a štítky z resources; test porovnává počty.
   Geometrické kontroly `test_server_info_layout.cpp` (Win32) na Compose nepřenosné, přeneseno je jen stránkování.
3. **29 a jednořádková odpověď**: desktop zobrazuje plaintext vždy doslova. Android doslova jen víceřádkový (běžný
   výstup PHP serveru), jednořádkový odsadí, aby byl čitelný (zadání). Jména, pořadí a literály čísel se nemění;
   escape sekvence řetězců se mohou zobrazit dekódovaně.
4. **37 jako varianta Connection**: kromě route `connection/permission` (výsledek dialogu) vykreslí Connection
   stejný obsah pro stav `tunnel_permission_missing`, aby při návratu nevznikla navigační smyčka.
5. **38 má lištu Back a nemá spodní navigaci** jako návrh, přestože je to route `connection`.
6. **Polling po úspěchu**: průběh se uzavírá hned po potvrzeném stavu (úspěchem je samotná 01/02), jedno čtení
   naplánované na 500 ms ještě proběhne, pak platí 5 s.
7. **Exit** je dialog (WP07) a čeká na potvrzení nejvýš 30 s (`IpcTimeouts.COMMAND_MS`); pak ukáže Connection.
8. **Start**: kromě `missing` → 00 otevře nepoužitelný uložený dokument 36 nad Connection; timeout nebo `valid`
   nic nemění.
9. **Nové texty** (`strings_wp08.xml`, 55 klíčů EN i CS): načítání před prvním stavem, onRevoke a R9 na 02,
   odmítnutý Connect, rozlišení vstupní/transportní chyby v 06, poslední známý stav v 08 pro odpojeno/připojování,
   baterie a Always-on v 10, doba s hodinami, varianty 36, selhání 45 bez dat, metadata About, skutečné volby 35,
   nadpisy 48 a stránkovač. Česká verze nemá schválenou předlohu.
10. **Testovací infrastruktura**: `PhpvpnApplication` je `open` a má `resetContainer()` (`@VisibleForTesting`);
    UI testy běží s `PhpvpnTestRunner`/`TestApplication` a testovacím `ActivityResultRegistry` místo dialogu 03.
    Testy vlastního emulátoru se spouštěly na kopii AVD `phpvpn-api36-wp08`, protože `phpvpn-api36` byl zamčený
    jinými instancemi bez `-read-only`.
