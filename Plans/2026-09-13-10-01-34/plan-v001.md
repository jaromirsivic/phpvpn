---
plan_version: 1
created_at: 2026-09-13-11-01-09
timezone: Europe/Berlin
source: ../../Intents/2026-09-13-10-01-34/intent-v002.md
source_sha256: 265c02d4e78e030582413911dbc7b6d702b854c2ab77f56049aba940a848542d
branch: tray_UX
baseline_commit: 07d1a48c44cda1d226ceea9e9afad598705d6efa
planning_model: Astra
planning_effort: Low
deadline: 2026-09-13T12:31:34+02:00
---

# Implementační plán povinných vlastních HTTP hlaviček

Tato úplná neměnná verze plánuje realizaci intent-v002. Implementace ještě neproběhla. Pracovat přímo v aktivní větvi, bez worktree. Před realizací ověřit případné novější změny proti uvedenému commitu a znovu použít Graft pro dotčené symboly. Souřadnice níže platí pro prozkoumaný stav.

## 1. Výsledek a hranice změny

Klient bude vyžadovat `masterSetup.httpHeaders` jako JSON string s alespoň jedním platným řádkem hlavičky. Stejný blok se dostane do všech requestů `health`, `info` a `data` z runtime i wizardu. Implicitní `PHPVPN/1.0` zmizí z identifikace WinHTTP relace. Aplikační Content-Type a dosavadní WinHTTP správa Connection a Content-Length zůstanou.

Výchozí hodnotou po JSON dekódování bude přesně:

```http
Cache-Control: no-cache
Pragma: no-cache
User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:155.0) Gecko/20100101 Firefox/155.0
```

Ve výchozím fyzickém JSON budou mezi řádky doslovné escape sekvence `\r\n`, bez koncového oddělovače. První členy `masterSetup` budou `serverSecret`, `serverUrl`, `httpHeaders`, `trustAnyTlsCertificate`. Následují `useDefaultServerSecretInWizard`, `vpnCidr`, `virtualIp`, `autoDetectIpAddressInWizard`, `accessKey`, `useDefaultAccessKeyInWizard` v dosavadním relativním pořadí. Samostatný nový wizard dokument má tutéž čtveřici na začátku a potom své dosavadní členy v dosavadním relativním pořadí.

Nepřidávat formulářové pole ani serverovou změnu. Neprovádět automatickou migraci starého dokumentu. Chybějící položka je chyba i v režimu opravy chybějící identity. Existující neplatný soubor se při startu nesmí přepsat výchozím. Odebrání User-Agentu znamená ponechat alespoň jinou platnou hlavičku, například `Cache-Control: no-cache`.

## 2. Vyřešená implementační rozhodnutí

### 2.1 Validace a reprezentace

Přidat společný platformně nezávislý modul `client/src/core/protocol/http_headers.h` a `.cpp`. Jeho navržené veřejné symboly jsou `normalizeHttpHeaders(std::string_view)` vracející `Result<std::string>` a `buildApplicationRequestHeaders(std::string_view contentType, std::string_view normalizedHeaders)` vracející úplný aplikační blok. Vstup druhého helperu už musí být validovaný. Modul nemá závislost na WinHTTP ani wizardu.

`normalizeHttpHeaders` bude provádět jediný lineární průchod a uplatní následující pravidla:

1. Neprázdný blok, alespoň jeden řádek `field-name: field-value`. Samotné mezery, prázdný string nebo samostatný oddělovač jsou neplatné.
2. Přijmout CRLF a samostatné LF jako oddělovače řádků. Samotné CR odmítnout. Jediný koncový oddělovač přijmout a odstranit v transportní podobě. Vnitřní prázdný řádek nebo více koncových oddělovačů odmítnout. Tím nelze zadat oddělení hlaviček od těla.
3. Název před první dvojtečkou musí být neprázdný ASCII token: písmena, číslice a `!#$%&'*+-.^_` plus zpětný apostrof, svislá čára a vlnovka. Mezery před dvojtečkou, prázdný název, dvojtečka v názvu a řádky začínající SP nebo HTAB jsou neplatné. Nepřijímat obs-fold ani request/status line.
4. Hodnota může být prázdná. Přijmout SP, HTAB, viditelné ASCII a oktety 0x80 až 0xFF. Odmítnout NUL, ostatní řídicí oktety 0x00 až 0x1F a DEL. CR a LF smějí sloužit pouze jako výše definované oddělovače. Dvojtečky, uvozovky a zpětná lomítka v hodnotě nejsou strukturální a zůstanou.
5. JSON i nadále podléhá existující přísné UTF-8 validaci. Dekódovaná hodnota se chápe jako její UTF-8 oktety, nikoli jako lokální ANSI text. Nezavádět závislost na systémové code page. NeASCII hodnoty ověřit testem skutečně odeslaných oktetů.
6. Zachovat velikost písmen názvů, hodnoty, mezery a pořadí všech řádků. Normalizovat jen oddělovače do CRLF a odstranit jeden volitelný koncový oddělovač. Žádná mapa hlaviček, deduplikace, řazení, přepisování ani slučování hodnot.
7. Nezakazovat názvy Content-Type, Connection, Content-Length, Host ani Transfer-Encoding. Kontroluje se syntaxe, nikoli význam kolizí. WinHTTP může nekonzistentní request odmítnout. Aplikace nesmí kvůli takové hodnotě změnit své parametry metody, URL nebo těla.
8. Zachovat existující limit velikosti konfiguračního dokumentu. Helper má kontrolovat velikost vstupu a přetečení při případném rozšíření LF na CRLF vůči reprezentaci velikostí použitých v transportu. Nezavádět nový svévolný limit počtu hlaviček.

`requireString` nadále rozliší nepřítomnost a nesprávný typ. Syntaktická chyba použije `ErrorCode::config_invalid_value` a popis s cestou `masterSetup.httpHeaders`, pořadovým číslem řádku a důvodem. Do chyby nekopírovat obsah hodnot, který může obsahovat přihlašovací údaje. Nepřidávat nový IPC error code.

Do `ConfigValues` a `ClientConfig` přidat `std::string httpHeaders` obsahující původní dekódovanou hodnotu. Validátor helper zavolá pro kontrolu, ale nenahradí jí `rawJson`. `TransportSettings.httpHeaders` a `WizardContext.httpHeaders` přenášejí tutéž dekódovanou hodnotu. WinHTTP při `configure` helper zavolá znovu a uloží normalizovanou kopii pro další requesty. Tím se chrání i přímé použití platformního transportu mimo konfigurační parser.

Nové členy agregačních struktur přidat na konec, aby stávající poziční inicializace nezměnily význam. Produkční `ProbeClient::prepare` převést na pojmenovaná přiřazení do lokálního `TransportSettings`. Výchozí prázdné `TransportSettings.httpHeaders` není skrytý fallback a `configure` jej odmítne. Přímé testovací konstruktory doplní platnou hodnotu explicitně.

### 2.2 Výchozí dokumenty a wizard

V `config.h` zavést sdílenou dekódovanou konstantu `kDefaultHttpHeaders`, kterou použije `makeNewWizardDocument`. Statický JSON v `kDefaultConfigDocument` zachovat jako čitelný raw string s předepsanými JSON escape sekvencemi. Shodu obou reprezentací zajistí automatický test.

`analyzeWizardDocument` zkopíruje hlavičky z `ConfigValues` do `WizardContext`. `makeNewWizardDocument` je vloží do DOM i kontextu. `buildWizardDocument` už kopíruje původní DOM a mění jen vyjmenované členy. Nezařazovat httpHeaders mezi upravované wizard fields. Ověřit zachování jeho hodnoty i pozice při editaci, opravě a doplnění kontraktů.

`WizardContext` má existující obousměrnou IPC serializaci. Rozšířit společně `WizardIpc::contextValue` a `parseContext` o string `httpHeaders`, včetně kontroly společným helperem. Stejná verze služby a tray bude používat stejné schéma. Do `WizardFields` ani příkazů `check_url`, `check_secret`, `check_access` nepřidávat editovatelné hlavičky. Service probe nadále čerpá z `session->original->context`, takže hodnotu nemůže podstrčit aktualizace jiného wizard pole. Nedoplňovat starému IPC kontextu tichý default.

### 2.3 WinHTTP a diagnostika

`WinHttpOpen` otevřít bez identifikace aplikace, s `nullptr` v prvním argumentu. Zachovat ostatní argumenty a nastavení relace. Absenci User-Agentu musí potvrdit lokální síťový test. Zdroj názvu user agent je popsán v [Microsoft WinHttpOpen](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpopen).

`buildApplicationRequestHeaders` vytvoří `Content-Type: <dosavadní typ>\r\n` a za něj připojí normalizovaný uživatelský blok. Koncové CRLF může doplnit transportní helper, nikdy však prázdný řádek ukončující hlavičky. Jeden výsledný buffer předat do dosavadního `WinHttpSendRequest` s explicitní délkou. Tento API parametr připojuje další hlavičky a délka těla zůstává v samostatném parametru. [Microsoft WinHttpSendRequest](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpsendrequest)

Pro hlavičky použít samostatný převod oktetů na WCHAR, aby UTF-8 oktety hodnot nebyly interpretovány jako názvy URL ani převáděny přes systémovou code page. Zachovat existující `widen` pro URL. Při čtení raw request hlaviček zajistit odpovídající převod zpět na oktety nebo použít aplikační rekonstrukci označenou správným zdrojem. Neaplikovat bez rozmyslu tuto změnu na dekódování jiných textů WinHTTP. Správnost mapování je implementační validační brána, nikoli předpokládaný výsledek.

Pokud ověřovací test zjistí, že společný buffer přes SendRequest nezachová opakované řádky, použít explicitní přidávání v pořadí přes `WinHttpAddRequestHeaders` s aditivní sémantikou a odeslat bez opětovného přidání týchž hlaviček. Nepoužít REPLACE, ADD_IF_NEW ani COALESCE. Opakovat stejnou matici síťových testů. Jestli ani podporovaná aditivní cesta nesplní kontrakt duplicit, realizaci v tomto bodě zastavit a reportovat omezení WinHTTP koordinátorce. Neoslabit intent ani nezačít měnit transportní knihovnu. Význam modifikátorů ověřen v [Microsoft WinHttpAddRequestHeaders](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpaddrequestheaders).

Tentýž aplikační blok předat diagnostice přímo jako oktetový string. Nevytvářet druhou, odlišnou definici hlaviček a nepřidávat User-Agent zvlášť. Zachovat současnou podmínku diagnostického capture pro health/info. Data stále posílá vlastní hlavičky, ale nezačne nově zveřejňovat tělo v diagnostice.

U `reconstructDiagnosticRequestHeaders` opravit dosavadní kontrolu pouhé existence názvu:

- Pokud raw hlavičky obsahují odpovídající úplnou posloupnost aplikačních řádků pro každý název, vrátit raw podobu a zdroj `winhttp`.
- Pokud raw chybí, vrátit celý aplikační blok se všemi duplicitami a zdrojem `application`.
- Pokud raw existuje, ale aplikační skupina chybí nebo je neúplná, rekonstruovat tuto skupinu jako celek z aplikačního bloku v původním pořadí. Zachovat raw skupiny ostatních názvů. Nevkládat jen první chybějící řádek podle názvu. Výsledek označit `merged` a neprezentovat jej jako přesný raw záznam.
- Porovnávat názvy bez ohledu na ASCII case. Srovnání normalizovaného OWS nesmí sloučit dva výskyty, včetně dvou totožných hodnot. U nejednoznačné transformace hodnot WinHTTP použít úplnou aplikační rekonstrukci dotčené skupiny. Nedovymýšlet systémovou hodnotu kolidujícího názvu.
- Zachovat `HTTP/?`, pokud nelze verzi získat, existující bounded capture, výběr prefixu/suffixu a jediný oddělovač mezi hlavičkami a tělem.

Globální pořadí různých názvů se netestuje. Testuje se pořadí hodnot uvnitř téhož názvu. Sémantická pravidla názvů a hodnot a význam pořadí vycházejí z [RFC 9110, oddíly 5.1 až 5.6](https://www.rfc-editor.org/rfc/rfc9110.html#section-5.3). Konkrétní volba přijetí LF a UTF-8 oktetů je implementační pravidlo tohoto plánu.

## 3. Přesně dotčené oblasti

| Soubor a ověřené souřadnice | Symboly a práce |
|---|---|
| `client/src/core/config/config.h:35-54,86-110` | `kDefaultConfigDocument`, nový `kDefaultHttpHeaders`, `ClientConfig.httpHeaders` |
| `client/src/core/config/config_document.h:25-47` | `WizardContext`, `ConfigValues`, nový string na konci obou struktur |
| `client/src/core/config/config_validator.cpp:34-47,119-318,320-334` | `requireString`, `validateConfigValues`, `kMasterMembers`, `parseAndValidateConfig`, validace a explicitní přesun nové hodnoty |
| nové `client/src/core/protocol/http_headers.h` a `.cpp` | Dva helpery z oddílu 2.1, bez závislosti na platformě |
| `client/src/core/config/config_defaults.cpp:4-20` | `makeDefaultEditorDocument`, `makeNewWizardDocument`, sdílené defaulty a pořadí DOM |
| `client/src/core/config/config_document.cpp:48-66,75-112` | `analyzeWizardDocument`, audit zachování v `buildWizardDocument` |
| `client/src/core/ipc/wizard_ipc.cpp:272-306` | `contextValue`, `parseContext`, přesný shape a validace nové hodnoty |
| `client/src/platform/iface/transport.h:26-32` | `TransportSettings.httpHeaders` |
| `client/src/core/runtime/network_worker.cpp:33-63` | `NetworkWorker::configure`, předání z přijatého ClientConfig |
| `client/src/core/wizard/probe_client.cpp:11-27` | `ProbeClient::prepare`, kompletní pojmenované nastavení transportu |
| `client/src/platform/windows/transport_winhttp.cpp:18-48,120-148,243-378` | `configure`, `exchange`, samostatný převod hlavičkových oktetů, diagnostický vstup. Zachovat lifecycle v `postImpl:164-221` |
| `client/src/core/wizard/http_diagnostic.cpp:23-48` | `reconstructDiagnosticRequestHeaders`, práce s opakovanými výskyty |
| `client/src/CMakeLists.txt` | Registrace nového core `.cpp` |
| `client/tests/unit/test_config.cpp`, `test_default_config.cpp` | Syntaxe, povinnost, typ, default a přesné raw JSON |
| `client/tests/unit/test_wizard_document.cpp`, `test_wizard_ipc.cpp`, `test_wizard_model.cpp` | Zachování, create/reset/repair, roundtrip kontextu |
| `client/tests/unit/test_http_diagnostic.cpp:6-34` | Rekonstrukce kompletních, chybějících a částečných opakovaných skupin |
| `client/tests/unit/test_winhttp_accounting.cpp:25-159,228-300,302-367` | `BodyServer`, `transportFor`, nové wire testy, regrese diagnostiky a lifecycle |
| `client/tests/unit/test_runtime_status.cpp:154-279,401-468,707-914,1526-1551,1677-1758` | `ScriptedTransport::configure`, probe, wizard service, refresh/recovery a konzistentní snapshot konfigurace |

Oblasti pro regresní kontrolu, bez předem nutné produkční úpravy:

- `ConfigStore::ensureDefaultAtStartup` v `client/src/core/config/config_store.cpp:33-42` a `createDefault:143-159` již používají společný default a chrání existující soubor. Rozšířit testy v `test_config_store.cpp` a `test_config_files_win.cpp`, nikoli měnit startup politiku.
- `NetworkWorker::performHealth` v `network_worker.cpp:82-127` a `TimeSync::requestTimestampMs` v `time_sync.cpp:56-76` ponechat funkčně beze změny. Test musí doložit autentizovaný timestamp a následnou projekci času.
- `VpnEngine::runNetworkThread` v `vpn_engine.cpp:490-685` již opakuje health přes NetworkWorker. Zachovat jeho synchronizaci a práci s přijatou konfigurací.
- `client/apps/tray/editor_window.cpp:20-58` a `tray_application.cpp:321-326` ověřit regresním testem formátování a uložení. Produkční úprava jen pokud skutečně chybně zacházejí s escapovaným stringem. Existující editor nesmí převést escape sekvence na fyzické neplatné zalomení uvnitř JSON stringu.
- Fixture dokumenty nezískávající default ze sdíleného helperu dohledat přes `graft grep "serverUrl"` a `graft grep "masterSetup"`. Především `test_config.cpp::documentWith`, modulový runtime fixture, `test_tray_progress.cpp` a `test_server_contract.cpp::runtimeConfig`. Doplnit platnou položku tam, kde test předpokládá platnou konfiguraci. JSON parser testy, status snapshoty a úmyslně neplatné dokumenty neměnit plošně.
- Přímé `TransportSettings` v `test_winhttp_accounting.cpp`, `test_runtime_status.cpp::runNativeHttpDiagnosticIntegration` a `client/tests/e2e/test_process_dns.cpp` doplnit explicitně. Wrapper v `client/tests/contract/test_server_contract.cpp` předává settings dál, ověřit jeho fixture.
- Uživatelské ukázky konfigurace v neindexované dokumentaci vyhledat až po Graftu pomocí `rg`. Doplnit httpHeaders v příkladech platných klientských dokumentů. Historické Intents a Plans zůstávají neměnné.

## 4. Pořadí realizace a výstupy kroků

1. **Zaznamenat výchozí stav.** `git status --short`, aktivní větev, HEAD a čas. Ověřit zdrojový intent a neměnnost markeru. Použít `graft map` a cílené dotazy. Nepřepisovat existující změny. Ověřit dostupnost x64 MSVC, Windows SDK, Ninja a CMake podle presetů.
2. **Zavést syntaktický helper a jeho testy.** Přidat soubory do CMake. Otestovat úplnou matici níže nezávisle na síti. Výstupem je validátor bez změny konfigurace nebo transportu.
3. **Rozšířit schéma, modely a defaulty.** Upravit povinnost, oba konfigurační modely a jejich převod, oba generátory dokumentu a WizardContext. Ve stejném kroku aktualizovat platné fixture dokumenty, aby jiné negativní testy neselhávaly na nové povinné položce. Dokončit default, reorder a source-preservation testy.
4. **Dokončit wizard a runtime tok.** Přidat IPC roundtrip kontextu a explicitní přiřazení v obou configure cestách. Fake transport ukládá přijaté hlavičky a u každého requestu zaznamená aktuální hodnotu. Ověřit probe i přechod na aktivní ClientConfig. V tomto kroku nesmí vzniknout alternativní default v transportu.
5. **Integrovat WinHTTP a provést wire bránu.** Odstranit session User-Agent, validovat transportní vstup, připojit aplikační blok. Na lokálním BodyServeru ověřit tři akce, User-Agent absent/custom/default, duplicity a neASCII oktety. Dočasné selhání zde opravit v adapteru, nikoli oslabením validátoru nebo testu. Zachovat cleanup i na chybě sestavení hlaviček.
6. **Dokončit diagnostiku.** Předávat stejný aplikační buffer a upravit rekonstrukci podle oddílu 2.3. Testy částečných skupin musí zachovat dva shodné řádky i pořadí odlišných hodnot. Ověřit příznaky zdroje a bounded capture.
7. **Provést vertikální regrese.** Runtime počáteční health/info/data, pravidelný health a recovery, wizard URL/secret/access, editor save/reload a explicitní reset. Ověřit zachování přijaté konfigurace během recovery i po uložení jiného souboru, v souladu se stávajícím chováním.
8. **Dokončit validaci a dokumentaci.** Debug a Release testy, cílené nativní UI kontroly a volitelné izolované contract/E2E podle dostupných fixture. Po podstatné změně kódu `graft build`, poté `git diff --check` a kontrola rozsahu. Reportovat přesné provedené příkazy a výsledky, neprovedené kontroly označit. Akceptaci potvrdit až po povinné wire bráně a matici níže.

## 5. Testovací strategie

### T1: Schéma a syntaxe

V `test_config.cpp` doplnit tabulkové pozitivní a negativní případy. Pro každé odmítnutí otestovat jak kompletní validaci, tak `analyzeWizardDocument` s chybějící identitou, aby repair neobešel povinnost.

Pozitivní případy: přesný default, jeden řádek, více řádků CRLF i LF, volitelný jediný koncový oddělovač, prázdná hodnota `X-Empty:`, HTAB v hodnotě, různé casingy stejného názvu, dvě shodné i dvě různé duplicitní hodnoty, JSON uvozovky a zpětná lomítka, UTF-8 hodnota. Přeházet členy masterSetup a porovnat všechny parsované hodnoty. `rawJson` musí zůstat původní s výjimkou již existující normalizace BOM.

Negativní případy: nepřítomná položka, null/bool/číslo/pole/objekt, prázdný string, jen whitespace, chybějící dvojtečka, prázdný nebo neASCII název, mezera před dvojtečkou, obs-fold, samotné CR, prázdný řádek uprostřed i na začátku, dvojitý koncový oddělovač, `\u0000`, jiné zakázané C0 a DEL, vložený `GET / HTTP/1.1`, `HTTP/1.1 200 OK`, blok `X: y\r\n\r\nbody`. Ověřit, že chyba neobsahuje citlivou testovací hodnotu.

Kolize názvů jsou pozitivní syntaktické případy. Odmítnutí WinHTTP kvůli nekonzistentní délce těla nesmí být zaměněno za aplikační zákaz názvu.

### T2: Default, uložení a wizard

- `test_default_config.cpp`: přesná dekódovaná hodnota, fyzické `\r\n`, přesné pořadí výchozích členů a shoda s `kDefaultHttpHeaders`.
- `test_config_store.cpp` a `test_config_files_win.cpp`: missing startup vytvoří nový default, další startup nezmění existující platnou hodnotu, starý dokument bez položky zůstane neplatný a zachovaný. Explicitní create/reset použije nový default.
- `test_wizard_document.cpp::PatchPreservesUnshownValuesAndOmission`: vlastní hlavičky na neobvyklé pozici přežijí editaci identity, repair i doplnění kontraktů. Create/reset obsahuje nový default. Roundtrip serializace porovná dekódovanou hodnotu a pořadí členů, ne libovolnou volbu JSON escapování obecného serializeru.
- `test_wizard_ipc.cpp`: context roundtrip s vlastními řádky, duplicitami a escapováním. Odmítnout chybějící, nesprávný typ a neplatný blok podle nového shape. `WizardFields` zůstane beze změny.
- Existující testy editoru/save rozšířit tak, aby simulovaly úpravu hlavičky, formátování, uložení přes set-config a nové načtení. Ověřit hodnotu i uživatelské pořadí členů. Pro první vytvoření a reset použít skutečné dosavadní příkazy, nepřidávat druhý generátor defaultu do testu.

### T3: Datový tok a čas

Rozšířit `ScriptedTransport` v `test_runtime_status.cpp` o zaznamenání celého potřebného nastavení a jeho vazby na requesty. Použít výraznou vlastní hodnotu odlišnou od defaultu.

Ověřit runtime počáteční diagnostický health, info, data polling, pravidelný health a health po recovery. Stejný přijatý config musí dodávat stejný blok i po změně souboru během aktivního spojení. Nové explicitní připojení má použít nově načtenou hodnotu.

U health použít existující kryptograficky správnou odpověď a deterministický clock. Ověřit publikovaný `TimeSnapshot` s časem z odpovědi a následný request timestamp projektovaný podle monotónního času. Při chybném ověření čas nepublikovat. Nepředepisovat nový synchronizační algoritmus.

Stejnou vlastní hodnotu otestovat u `ProbeClient::checkUrl`, `checkSecret`, `checkAccess` a přes `WizardServiceFixture`, nejen přímým voláním prepare. URL používá health, secret health/info a access zahrnuje data. Capture true/false nesmí ovlivnit připojení hlaviček.

### T4: Skutečné WinHTTP requesty

Rozšířit existující BodyServer a `transportFor`, případně přidat nový testovací soubor `test_http_headers.cpp` registrovaný v `client/tests/CMakeLists.txt`. Server váže pouze loopback a používá dynamický port. Pro samostatné requesty lze vytvořit novou serverovou instanci. Pro reuse test musí fixture umět přijmout další spojení/request a bezpečně ukončit vlákno i po selhání klienta.

Matici spustit pro health, info a data s binárně rozlišitelným tělem:

| Konfigurace | Síťová kontrola |
|---|---|
| Default | Všechny tři hlavičky a přesný Firefox User-Agent, žádný PHPVPN/1.0 |
| `X-Test: own` | Žádný User-Agent, hlavička X-Test přítomna |
| Vlastní User-Agent | Přesná uživatelova hodnota bez druhé implicitní hodnoty |
| `X-Order: first`, `x-order: second`, `X-Order: first` | Tři výskyty, pořadí first/second/first, case-insensitive identita názvu |
| Dodatečný Content-Type se stejnou bezpečnou hodnotou | Aplikační blok obsahuje původní i uživatelský řádek, žádná vlastní deduplikace |
| CRLF/LF vstup | Na drátu validní řádky bez vloženého oddělení těla |
| UTF-8 hodnota | Původní dekódované UTF-8 oktety ve value, žádný převod do lokální code page |

Každý test porovná metodu POST, původní cestu s právě jedním `?action=...`, přesné tělo za prvním `\r\n\r\n` a funkční Content-Length odpovídající tělu u nekolidující konfigurace. Content-Type porovnat s `httpActionContentType`. Connection posuzovat podle stávajícího WinHTTP chování. Nekontrolovat, že vlastní hlavičky fyzicky následují po Content-Length.

Nekonzistentní Content-Length nebo Transfer-Encoding oddělit od běžných úspěšných requestů. Ověřit průchod syntaktickou validací a nezměněné parametry těla, metody a URL. Chyba transportu je přípustná, úspěšné obejití rámování není. Negativní injekční vstup musí odmítnout již `configure` a server nesmí obdržet request.

Ponechat a spustit `WinHttpAccounting`, `WinHttpDeadline` a `WinHttpLifecycle`: započítávání pouze těla, částečné čtení, timeout, zrušení a opětovné použití. Hlavičky se nesmějí přičíst k metrice těla. Ověřit také reconfigure z defaultu na konfiguraci bez User-Agentu.

### T5: Diagnostika

V unit testu pokrýt raw kompletní, raw absent, chybějící celý název, raw s prvním výskytem z více, dvě stejné hodnoty, různé casingy a nejednoznačnou normalizaci raw hodnoty. Výsledkem je úplný aplikační blok bez domyšleného User-Agentu a správně označený zdroj. Raw úplnou podobu zachovat.

V lokálním WinHTTP testu spojit `receivedRequest()` s diagnostikou health/info. Ověřit shodu aplikačních hlaviček včetně duplicit, těla a bounded capture. U data zkontrolovat přenos hlaviček a zachování dosavadní absence diagnostického těla. Nevyžadovat raw pořadí různých názvů od rekonstruovaného výpisu.

## 6. Konkrétní validační běhy

Příkazy níže spouštět z `C:\git\phpvpn\client` v x64 MSVC developer prostředí. Presety vyžadují CMake nejméně 3.28 a Ninja, kompilátor je `cl.exe`. Harness přijímá jeden substring filtru jako první argument. Pro základní úplnou sadu není potřeba externí PHP server.

```powershell
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure
cmake --preset release
cmake --build --preset release
ctest --preset release --output-on-failure
```

Při vývoji lze použít konkrétní executable `C:\git\phpvpn\work\build\debug\tests\phpvpn-client-tests.exe` s filtrem `config`, `wizard`, `probe`, `runtime`, `http_diagnostic` nebo `WinHttp`. Filtry nenahrazují konečný běh celé sady. Novým testům dát názvy, které tyto filtry pokryjí.

Pokud se změní buffer lifetime, konverze nebo asynchronní cleanup, provést také preset `asan`. Pokud se upraví skutečné tray UI chování, sestavit s `PHPVPN_BUILD_UI_TESTS=ON` a spustit cílené editor/reset/save scénáře podle `client/tests/ui/README.md` a odpovídající WP03 kontroly. Tyto cíle nejsou automaticky v CTest.

Volitelné real-PHP contract testy vyžadují izolovaný relay a `PHPVPN_BUILD_CONTRACT_TESTS=ON`. Poskytují dodatečnou regresi protokolu, nikoli náhradu raw wire testu. Neprovádět je proti uživatelovu produkčnímu serveru. SCM/Wintun E2E podle `client/tests/e2e/README.md` využít v izolovaném prostředí pro potvrzení skutečného prvního startu, pokud je dostupné. Bez jeho běhu uvést, že startup cesta byla pokryta ConfigStore testem, nikoli SCM instalací.

Konečná kontrola v kořeni projektu:

```powershell
graft build
git diff --check
git status --short
```

Zkontrolovat, že nejsou změněny `phpvpn.php`, intent soubory ani `.intent-builder-active`. Grafové změny mohou vzniknout až při realizaci. V tomto plánovacím úkolu se graf nepřegenerovává.

## 7. Rizika a postup při problému

| Riziko | Řešení a validační brána |
|---|---|
| Povinná položka zneplatní staré dokumenty | Záměrná kompatibilitní změna intentu. Jasná chyba, zachování souboru, ruční doplnění nebo explicitní reset. Žádná automatická migrace |
| Samostatný ClientConfig nebo wizard default zapomene hodnotu | Explicitní převody, nezávislé testy obou generátorů a runtime/probe spy |
| Pozice členů struktur nebo JSON způsobí jiný význam | Nové členy na konci struktur, pojmenované přiřazení, test permutovaného JSON |
| WinHTTP mění duplicity nebo znaky | Povinný lokální raw wire test. Oprava adapteru v rámci aditivního API, jinak konkrétní report bez oslabení intentu |
| Neslučitelné framing hlavičky | Bez blacklistu názvů a bez změny těla, zachovat chybu WinHTTP. Úspěšný request nesmí přinést druhou request line nebo tělo |
| Rekonstrukce maskuje duplicity nebo tvrdí nepřesný raw obsah | Práce s celými uspořádanými skupinami, zdroje winhttp/merged/application a test částečné raw skupiny |
| Únik hodnot do validační chyby | Uvádět pouze cestu, číslo řádku a důvod. Rozsah existující uživatelem vyvolané diagnostiky se nemění |
| Změna capture ovlivní časování či účtování | Stejná capture podmínka, původní deadline a cleanup, regrese accounting/cancel/reuse |
| Plošná změna fixture oslabí negativní testy | Doplnit pouze dokumenty očekávané jako platné a zachovat cílové chyby jednotlivých testů |

Nebyla nalezena nejasnost vyžadující změnu intentu. Konkrétní přijetí oddělovačů, způsob reprezentace oktetů a algoritmus diagnostické rekonstrukce jsou rozhodnutí této plánovací verze. Pokud realizace prokáže, že WinHTTP požadovaný wire kontrakt neumožňuje, vrátit koordinátorce reprodukovatelný případ, podporovanou verzi Windows, výstup testu a přesný konflikt. Změnu požadavků smí potvrdit uživatel, nikoli implementátor.

## 8. Akceptační kontrolní seznam

| Kritérium intentu | Požadovaný důkaz |
|---|---|
| 1. Default při vytvoření/resetu | T2, oba generátory a ConfigStore startup/create |
| 2. Výchozí pořadí | T1/T2, přesná čtveřice a relativní pořadí zbývajících členů |
| 3. Jiná pozice členů | T1, permutovaný dokument se shodným parsovaným významem |
| 4. Povinnost | T1, chybějící položka v plném i repair režimu |
| 5. Typ a syntaxe | T1, negativní matice a odmítnutí před requestem |
| 6. Health/info/data | T3 a T4, předání settings i skutečné requesty |
| 7. Firefox bez PHPVPN/1.0 | T4, přesný default a absence implicitní hodnoty |
| 8. Změna/odebrání User-Agentu | T4, custom/absent i reconfigure |
| 9. Dosavadní transportní hlavičky | T4, metoda, cesta, Content-Type, framing a přesné tělo |
| 10. Kolize bez deduplikace | T1/T4, aditivní blok a bezpečný duplicitní Content-Type |
| 11. Pořadí shodného názvu | T4/T5, first/second/first a case-insensitive identita |
| 12. Ověřený serverový čas | T3, autentizovaný snapshot a následná monotónní projekce |
| 13. Wizard a diagnostika | T2/T3/T5, service probe i skutečný diagnostický request |
| 14. Editor zachovává konfiguraci | T2, edit/save/reload s vlastním pořadím |
| 15. Automatické pokrytí | Úspěšné T1 až T5, Debug a Release CTest, zaznamenaná wire brána |

Realizace je hotová, až jsou povinné důkazy úspěšné a neexistuje otevřená regrese v syntaxi, duplicitách, framingu, wizardu nebo čase. Nedostupný povinný Windows test je neověřená akceptace, nikoli úspěch.

## 9. Stav při předání plánu

Vznikl pouze tento plán. Intent a marker vlastní koordinátorka. Zdrojový kód nebyl změněn, implementační testy nebyly spuštěny a plán netvrdí ověřenou funkčnost budoucí implementace. Plánovací čtení použilo Graft a cílené zdrojové rozsahy, CMake presety a primární dokumentaci Microsoft/RFC.

Před realizací je nutná explicitní volba uživatele realizovat hotový plán. Případná další věcná úprava plánování vytvoří celý `plan-v002.md`, tuto verzi nepřepíše. Deadline platí pro toto delegované plánování. Budoucí realizace musí mít vlastní potvrzené zadání a časový rámec, nesmí automaticky pokračovat pod již dokončeným plánovacím úkolem.
