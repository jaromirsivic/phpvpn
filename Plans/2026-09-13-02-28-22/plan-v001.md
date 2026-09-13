---
plan_version: 1
created_at: 2026-09-13T02:35:10+02:00
source: ..\..\Intents\2026-09-13-02-28-22\intent-v002.md
---

# Realizační plán: Diagnostika HTTP requestu a response v konfiguračním wizardu

## 1. Autorita, rozsah a výsledek

Jediným autoritativním zadáním je celý `Intents/2026-09-13-02-28-22/intent-v002.md`. Tento plán je první úplná neměnná verze. Nahrazení velikostního pravidla bylo potvrzeno před jeho zápisem. Platí celý výpis do 64 KiB včetně, jinak prvních 63 KiB, přesně čtyři řádky `...` a poslední 1 KiB. Prefix se suffixem nikdy nepřekrývá. Pravidlo se vyhodnocuje samostatně pro request a response.

Realizace proběhne přímo v `C:\git\phpvpn` na větvi `tray_UX`, bez worktree. Při plánování byla tato větev ověřena. Produktový kód, testy ani dokumentace nebyly při přípravě plánu měněny. Existující změna `.intent-builder-active` a ostatní necommitované artefakty patří původní práci a musí zůstat zachované. Opuštěná složka intentu označená `WRONG` ani starý plán nebyly zdrojem návrhu.

Výsledkem realizace bude červené tlačítko `Error detail` pouze pro neúspěšné zpracování přijaté odpovědi v prvních dvou krocích wizardu. Otevře lokální okno se dvěma textovými taby. Nezmění se serverový protokol, kryptografie, pořadí health/info, timeouty, retry, běžné datové požadavky ani chování Show Server Info. Raw diagnostika se nikdy nestane součástí obecného `Error` nebo logu.

Deadline této plánovací práce je 2026-09-13 04:10:40 Europe/Berlin. Kontrola času probíhá mezi kroky. Nebylo tvrzeno nezávislé automatické přerušení nástroje přesně na deadline. Další realizace vyžaduje samostatnou volbu uživatele, tento dokument ji neprovádí.

## 2. Ověřené body v aktuálním kódu

Repozitář byl nejprve prozkoumán přes `graft map`, dále `graft ask --source`, `graft skeleton`, `graft callers` a `graft grep`. Následující rozsahy jsou orientační kotvy aktuální větve, před pozdější implementací je ověřit proti aktuálnímu Graft grafu.

| Vrstva | Soubor, symbol a rozsah | Dnešní chování a důsledek |
|---|---|---|
| Transportní kontrakt | `client/src/platform/iface/transport.h:25-73`, `TransportSettings`, `HttpResponse`, `HttpExchangeResult`, `Transport::post` | Response má status, body a contentType. Exchange nese také počet přečtených body bajtů. Chybí hlavičky a diagnostika. |
| WinHTTP | `client/src/platform/windows/transport_winhttp.cpp:127-172`, `WinHttpTransport::post`, a `:194-268`, `exchange` | Vlastní přesný body buffer, cílovou cestu s akcí, request handle a stav čtení. Po HEADERS_AVAILABLE zpracuje status/contentType/contentLength a čte body. Nadlimitní blok odmítá před appendem do body. Handle zavírá až vlastník post a čeká na poslední callback. |
| Probe | `client/src/core/wizard/probe_client.cpp:11-89`, `prepare`, `post`, `health`, `info`, `checkUrl`, `checkSecret` | `post` již může ztratit response při špatném statusu či contentType. `health` parsuje syntaxi a volitelně ověřuje podpis. `info` vytváří nový request a parsuje/dešifruje odpověď. |
| Operace služby | `client/src/core/wizard/wizard_service.cpp:310-416`, `startProbe`, `probeTask` | Operace drží identitu a generaci. Publikace výsledku po práci probíhá pod mutexem až po kontrole epochy, cancellation a session generation. |
| Retence služby | `client/src/core/wizard/wizard_service.cpp:31-112`, `terminal`, `cancelLocked`, `closeLocked`, `expireLocked`, `interruptForConnect`, a `:175-309`, `execute` | Cancel dnes opouští terminální operaci bez zásahu. Explicitní mazání detailu proto musí předcházet tomuto early return. Reset ruší session operation. |
| IPC | `client/src/core/wizard/wizard_types.h:31-42`, `WizardOperationResult`, `client/src/core/ipc/wizard_ipc.cpp:395-459`, `operationValue`, `parseOperation` | Operace má osm přesných polí. Nesucceeded payload musí být null. Rozšíření musí být samostatným volitelným členem. |
| Velikost IPC | `client/src/core/ipc/ipc_protocol.h:31`, `kIpcMaxMessageBytes`, `wizard_ipc.cpp:460-480` | Limit je 4194304 bajtů kompletního UTF-8 JSON. `buildResult` kontroluje limit, `parseReply` také validuje UTF-8. |
| Model | `client/src/core/wizard/wizard_model.cpp:123-181`, `beginCheck`, `acceptStart`, `applyOperation`, `fail`, `interrupt`, `sessionExpired` | Již chrání operationId, kind, attempt ID, sequence, deadline a epoch. Diagnostiku přijmout až za těmito kontrolami. |
| Tray transport | `client/apps/tray/wizard_reader.cpp:5-32`, `clearPending`, `request`, `take`, a `wizard_window.cpp:242-340`, `receive` | Výsledek se převádí přes WizardReply, generaci okna a následné parseOperation. Žádné nové IPC spojení pro detail není potřeba. |
| Patička | `client/apps/tray/wizard_window.cpp:653-714`, závěr `render`, `layout`, `wizard_layout.cpp:5-24`, `computeWizardLayout`, `layout.cpp:101-165`, `computeButtonFooterLayout` | Layout používá specifikace tlačítek, leading/trailing skupiny a základní mezeru 8 DIP. Dnešní pořadí reset/back/next rozšířit podle kroku. |
| Reference tabů | `client/apps/tray/editor_window.cpp:20-56`, `drawInfoTab`, `:294-441`, `open`, `selectInfoTab`, `updateFonts`, `layout` | Show Server Info již má native tab control s owner draw a read-only multiline EDIT. Jeho vlastní okno obsahuje OK, proto jej nelze prostě znovu otevřít pro nový detail. |

`graft callers checkSecret` potvrdil použití také v contract/e2e testech. Zachycení proto nebude implicitně aktivní pro každé použití ProbeClientu. Služba jej výslovně zapne jen pro kind url/secret.

## 3. Datové typy a přesný datový tok

Přidat společné malé typy a čisté helpery do nových `client/src/core/wizard/http_diagnostic.h` a `.cpp`:

- `BoundedHttpMessage`: `uint64_t originalSizeBytes`, bajtové řetězce `prefix`, `suffix`. Oddělovač je konstanta, nikoli odhadnutý úsek odpovědi.
- `HttpDiagnostic`: `HttpAction action`, `bool responseComplete`, informace o původu request/response headers a oba `BoundedHttpMessage`.
- `BoundedHttpCapture`: inkrementální `append`, `finish`, přetečení počtu bajtů kontrolované před součtem. Do velikosti patří start line, headers, prázdný řádek i body.
- `encodeDiagnosticOctets` a `decodeDiagnosticOctets`: bezeztrátový převod omezených bajtů do validního JSON stringu a zpět podle části 5.
- `diagnosticDisplayText`: deterministický převod omezených bajtů do bezpečného Unicode textu podle části 5, bez parsování HTTP/JSON.

Do `TransportSettings` přidat `captureWizardHttpDiagnostics = false`. Existující agregátní inicializace zůstávají funkční díky výchozí hodnotě. Do `HttpExchangeResult` přidat volitelné `diagnostic` a interní klasifikaci `responseProcessingFailed`, výchozí false. Konstruktorové overloady zachovat. Signaturu virtuálního `post(action, body)` neměnit. Obsah diagnostiky je od začátku omezený, nevzniká druhá plná kopie response body. `HttpResponse` zůstává kompatibilní pro běžné konzumenty.

Tok jedné operace:

1. `WizardService::probeTask` vytvoří samostatný ProbeClient a zapne capture pouze pro url/secret. `checkAccess` a ostatní uživatelé zůstanou s false.
2. `ProbeClient::prepare` předá capture do TransportSettings. Vyčistí poslední status i diagnostiku.
3. WinHTTP připraví omezenou rekonstrukci requestu a při přijetí response hlaviček začne omezeně sbírat response. Stávající body pro parser se nemění.
4. `ProbeClient::post` převezme kandidáta před kontrolou statusu, contentType a velikosti. Chyby těchto kontrol označí jako chybu zpracování přijaté odpovědi. Návratový bezpečný Error se nemění.
5. `health` a `info` drží kandidáta pouze po dobu zpracování konkrétní odpovědi. Při chybě parseru, korelace, kryptografie nebo validace získaných údajů jej přesunou do `failedDiagnostic_`. Při úspěchu jej ihned zruší.
6. `takeFailedHttpDiagnostic()` vrátí a vyprázdní pouze detail skutečně selhavší výměny. Volá jej probeTask před zánikem ProbeClientu. Žádné zpětné dotazování na transportní „last response“.
7. Pod mutexem služby se diagnostika přesune do `WizardOperationResult::httpDiagnostic` jen ve větvi `failed`, pro url/secret a stále platnou identitu/generaci. Větve cancelled/interrupted/succeeded/pending/running ji nikdy nenesou.
8. `wizard-get-operation` ji přenese v existující odpovědi. WizardReader ji nepersistuje. `parseOperation` a `WizardModel::applyOperation` prověří schéma i korelaci a uloží ji do `diagnostic_` svázané s původní identitou pokusu.
9. `WizardWindow::render` odvodí viditelnost tlačítka z modelu. Kliknutí zobrazí již přijatá data, neprovádí HTTP ani IPC.

Kandidáta resetovat na začátku `health` a `info`, tedy ještě před generováním request ID, timestampu a klíčů. Samotný reset v `post` nestačí: po úspěšném health může selhat lokální příprava info ještě před voláním post. Při tomto selhání nesmí zůstat health detail ani jeho status vydávaný za status info.

První krok nadále volá `health(false)`. Nové kryptografické ověřování v URL kroku se nezavádí. Jeho syntakticky neplatné kryptografické pole lze diagnostikovat, kryptograficky chybný, ale syntakticky validní tag zůstává předmětem druhého kroku. Testy musí toto rozlišení zachovat.

## 4. Zachycení aplikačního HTTP výpisu

### Request

Start line definovat jako `POST <absolutní-cílová-URL-včetně-?action=health-nebo-info> <dostupná-HTTP-verze>\r\n`. Pokud verze není dostupná, použít přesně `HTTP/?` a označit rekonstrukci v metadatech/poznámce karty. Tento zápis je aplikační rekonstrukce, nikoli tvrzení, že WinHTTP vyslal absolute-form request target. URL sestavit ze skutečné authority a target použitého při `WinHttpOpenRequest`, včetně implicitní `/` u URL bez cesty. Nezískávat ji z nějakého pozdějšího modelového pole.

Po `HEADERS_AVAILABLE`, dokud je request handle platný, získat request hlavičky přes `WinHttpQueryHeaders(WINHTTP_QUERY_RAW_HEADERS_CRLF | WINHTTP_QUERY_FLAG_REQUEST_HEADERS)`. Použít dvoufázové zjištění velikosti a bounded retry při změně velikosti. Aplikací předané hlavičky uchovat jako původní hodnoty. Při dostupném systémovém bloku zachovat pořadí a duplicitní hlavičky, včetně systémově doplněných. Pokud v něm aplikační hlavička chybí, doplnit její původní řádek a označit zdroj jako `merged`. Názvy porovnávat case-insensitive jen pro zjištění dostupnosti, hodnoty a zobrazované řádky nepřepisovat. Nevymýšlet Host, Content-Length nebo User-Agent při selhání query. Fallback `application` obsahuje všechny skutečně předané hlavičky a karta oznámí nedostupnost systémového doplnění.

Po hlavičkách vložit přesně jeden oddělovací prázdný řádek a připojit totožné body bytes předané `WinHttpSendRequest`. Rekonstrukce nesmí měnit body ani doplňovat dešifrovaný obsah. Terminální CRLF bloku hlaviček zpracovat jako framing, aby se nezdvojil prázdný řádek. Původní řádky headers uvnitř bloku se netřídí ani neupravují.

### Response

Zachytit `WINHTTP_QUERY_RAW_HEADERS_CRLF` bez request flagu, ihned po úspěšném HEADERS_AVAILABLE, před kontrolami contentType/contentLength a před předáním parseru. Převod WinHTTP UTF-16 headers na UTF-8 je aplikační reprezentace poskytovaná systémem, nepředstírá původní header wire bytes. Status line a verzi převzít z dostupného bloku. Pokud chybí, použít skutečný numerický status jako `HTTP/? <status>\r\n`, bez vymyšleného reason phrase. Při nemožnosti získat číselný status, ale prokázaném HEADERS_AVAILABLE, použít `HTTP/? [status unavailable]\r\n`, error.httpStatus zůstane null.

Při selhání raw query zkusit dostupné jednotlivé hlavičky bez změny významu síťové chyby. Metadata rozliší `winhttp`, `available` a `unavailable`. Poznámka o neúplnosti headers je mimo HTTP výpis. Neúspěch diagnostické query sám nesmí změnit úspěšný probe na chybu.

Za start line/headers/prázdný řádek přidávat přesně bloky body, které vrátil WinHTTP. Capture provést před stávající kontrolou maxResponseBodyBytes, aby i blok způsobující odmítnutí byl zahrnut mezi skutečně přijaté bytes. Neměnit počítání `responseBodyBytesRead`, původní limit ani nepokračovat v síťovém čtení jen kvůli diagnostice.

Přijatá response je doložena dokončeným HEADERS_AVAILABLE, nikoli samotným nenulovým body nebo odhadovaným statusem. Kompletní prázdná response je platný kandidát. Úspěšně dočtená response má `responseComplete=true`. Selhání zpracování již přijatých headers, překročení body limitu či kontrola nesouladu Content-Length má `responseComplete=false`, pokud se tělo nedočetlo. V tom případě originalSizeBytes znamená velikost skutečně dostupného aplikačního výpisu, nikoli tvrzený Content-Length. Karta musí zobrazit `Incomplete response: size refers to received content.`

Čistá DNS/TCP/TLS chyba, timeout čtení či cancellation není chybou parseru. Kandidát se při těchto návratech nezveřejní, ani pokud před timeoutem došly některé hlavičky nebo část body. Pokud dříve proběhlo úspěšné health a info selže bez response, detail není. Při závodu s cancellation/epochou má existující terminální rozhodnutí v `post` a probeTask přednost před kandidátem.

Dostupné response trailers jsou HTTP vrstvou poskytované headers. Po přirozeném EOF je na podporovaném OS dotázat, zachovat jejich řádky jako samostatně označenou skupinu v header části aplikační rekonstrukce před body a započíst do original size. Nepoužívat dodatečná síťová čtení. Při nepodporované query nic nedoplňovat. Pro závěrečné vložení do header části držet odděleně bounded body prefix/tail a jeho velikost, nikoli plné druhé body. Helper složí konečné omezené úseky z hlavičkové části a bounded body tak, aby součet a pořadí odpovídaly výslednému výpisu.

Použité platformní předpoklady ověřují [WinHttpQueryHeaders](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpqueryheaders) a [Query Info Flags](https://learn.microsoft.com/en-us/windows/win32/winhttp/query-info-flags). Query headers se provádí po dokončeném receive. RAW_HEADERS_CRLF poskytuje CRLF blok, REQUEST_HEADERS vybírá request. TRAILERS vyžaduje EOF a novější Windows, proto má explicitní fallback. Nepřidávat požadavek na novější OS nebo wire-encoding flag.

## 5. Bajtové pravidlo, Unicode a original size

### Jednotná definice řezu

Nechť `B` je celá aplikační reprezentace: UTF-8 start line a systémem dostupné headers, jejich framing a nezměněné body bytes. `N = |B|` je uint64 počet bajtů před řezem a před JSON escapingem. Body se před měřením nedekóduje, nedešifruje a nenormalizuje.

| Podmínka | prefix | separator | suffix | Vynecháno |
|---|---|---|---|---|
| `N <= 65536` | `B[0:N]` | prázdný | prázdný | 0 |
| `N > 65536` | `B[0:64512]` | `\r\n...\r\n...\r\n...\r\n...\r\n` | `B[N-1024:N]` | `N-65536` |

Oddělovač má 22 ASCII bajtů. Má vždy stejné CRLF na začátku a konci, aby čtyři vložené řádky obsahovaly jen tři tečky i při prefixu bez koncového newline. Přirozený koncový newline prefixu se neodstraňuje. Může proto vzniknout prázdný řádek navíc, ale vložené řádky `...` jsou vždy přesně čtyři. Obdobně neměnit začátek suffixu. Data mohou sama obsahovat tečkové řádky, proto renderer zná samostatný separator, nehledá jej textovým vyhledáváním.

Pro `N=65537` je suffix od indexu 64513, jediný vynechaný bajt je index 64512. Pro `N=66560` se vynechá 1024 bajtů. Žádné odečítání překryvu, deduplikace ani změna hranice na 65 KiB se neprovádí. Při `N=65536` zůstane celý obsah bez separatoru.

Inkrementální collector si drží prvních nejvýše 65536 bajtů, posledních nejvýše 1024 bajtů a celkový čítač. Po finish vybere podle N buď celý prefix, nebo 64512 prefix + 1024 suffix. Pomocná paměť je konstantní vůči délce body. Při skládání header/body segmentů nepřipojovat celý body znovu do stringu jen kvůli řezu.

### Bezeztrátový přenos omezených bajtů

JSON musí být validní UTF-8 i při neplatném body nebo řezu uprostřed UTF-8 znaku. Proto je diagnostický formát výslovně `http-octets-v1`. V JSON stringu prefix/suffix se každý původní bajt `b` reprezentuje Unicode skalárem `U+00bb`. Například bajty UTF-8 `C4 8D` se v přenosovém stringu reprezentují dvěma skaláry U+00C4 a U+008D. Standardní Json serializer zajistí escaping. Tray tento byte-string dekóduje zpět na původní dva bajty a teprve poté zobrazuje `č`. Toto není převod těla do jiného charsetu, je to reverzibilní IPC kódování přesně vybraných bajtů.

Parser připustí pouze skaláry U+0000 až U+00FF v prefix/suffix, nikoli libovolný Unicode. Jejich počet je přesný počet zdrojových bajtů. NUL zůstává v C++ length-aware stringu, nikdy se před dekódováním neposílá do SetWindowTextW. Formát nepoužívá base64 ani chunkované požadavky a nemění společný JSON parser/serializer.

### Bezpečný Win32 text

Zobrazení každého úseku se dekóduje samostatně striktním UTF-8 scannerem. Validní úplné sekvence se převedou beze změny na UTF-16, včetně surrogate pair pro znaky nad BMP. Nesmí se přibrat bajt ze skryté mezery pro dokončení znaku a prefix se nikdy nespojí se suffixem před dekódováním. Každý bajt, který není začátkem kompletní validní sekvence v daném úseku, se zobrazí jako ASCII `\xHH` s velkými hex číslicemi. Scanner v tomto případě spotřebuje právě jeden bajt. Tím je deterministický i řez uprostřed 2/3/4bajtového znaku, osamocený continuation byte, overlong sekvence, surrogate v UTF-8 a neúplný konec.

Stejným viditelným `\xHH` zobrazit NUL, C0 control bytes kromě TAB/CR/LF a DEL, aby nedošlo k ukončení Win32 stringu nebo skrytému ovládání textboxu. Platný běžný text, TAB a CR/LF zachovat. Neprovádět trim, pretty JSON, změnu konců řádků nebo HTML interpretaci. Karta při použití těchto náhrad ukáže `Invalid or non-displayable bytes are shown as \\xHH.` Uživatel tím pozná, že jde o zobrazení bajtů, nikoli doslovný původní text. Doslovné řetězce \xHH v běžném body se nepřepisují, raw podoba je nadále přesně zachována v omezených bajtech modelu.

Limit 63/1 KiB se vztahuje na vybrané zdrojové bajty B, nikoli na počet UTF-16 jednotek, zobrazovacích escape znaků nebo JSON bajtů. Vybírá se vždy nejvýše 65536 zdrojových bajtů. Deterministická textová projekce může mít až čtyři znaky na zdrojový bajt. Její samostatná pevná mez je 262166 UTF-16 jednotek včetně separatoru, což se ověří před SetWindowTextW. Nevzniká tím přenos dalšího body nebo rozšíření řezu. Test ověří také nedestruktivní zobrazení CR/LF a kopírování přes native EDIT.

Taby budou mít přesně názvy `Request` a `Response`. V obsahové kartě nad textem bude například `Request (Original size: 280.34 KiB)`. Formátovat deterministicky s tečkou a dvěma desetinnými místy. Nepoužívat floating point pro obrovské N: `q=N/1024`, `r=N%1024`, setiny `floor((100*r+512)/1024)`, případných 100 přenést do q. Bez overflow násobením celého N. `Original size` neobsahuje separator ani escape expanzi.

Při zkrácení karta zobrazí `Truncated: first 63 KiB and last 1 KiB, <N-65536> bytes omitted. The four dotted lines are a display separator.` Poznámka je mimo textbox, aby se nezaměnila s přijatou HTTP hlavičkou nebo body. Při nezkráceném obsahu se tato poznámka nezobrazí.

## 6. Přesné rozšíření Wizard IPC a důkaz limitu

Přidat volitelný člen `httpDiagnostic` na úroveň `WizardOperationResult`, vedle error a payload. Když diagnostika není, člen zcela vynechat. Explicitní null odmítnout. Původních osm povinných členů zůstává v původních typech. `payload` při failed zůstává null.

Schéma objektu `httpDiagnostic`, bez dalších členů:

```text
{
  "format": "http-octets-v1",
  "action": "health" | "info",
  "responseComplete": boolean,
  "requestHeadersSource": "winhttp" | "merged" | "application",
  "responseHeadersSource": "winhttp" | "available" | "unavailable",
  "request": { "originalSizeBytes": uint64, "prefix": byteString,
               "separator": string, "suffix": byteString },
  "response": { "originalSizeBytes": uint64, "prefix": byteString,
                "separator": string, "suffix": byteString }
}
```

`originalSizeBytes` vyžaduje celočíselný JSON token v nezáporném uint64 rozsahu. U skutečné zprávy musí být kladný, protože obsahuje start line a framing. Nepřipustit real, záporné číslo, overflow ani číselný string. Pro N do 65536 je počet decoded octets v prefix přesně N, separator a suffix jsou prázdné. Pro N nad 65536 je prefix přesně 64512 octets, suffix přesně 1024 a separator přesně uvedených 22 ASCII bajtů. Žádná libovolná délka „do limitu“, žádné další úseky. Při přenosové délce větší než dvojnásobek očekávaných octets po JSON parse lze byte-string odmítnout před jeho dalším dekódováním. Každá decoded octet kontrola zůstane bounded.

`parseOperation` odmítne diagnostiku, pokud kind není url/secret, state není failed, error chybí, payload není null nebo pro url není action health. Secret dovolí health/info. Identita se neduplikuje uvnitř detailu: jeho identitou je přesná rodičovská n-tice `(operationId, kind, connectionEpoch, clientAttemptId, attemptSequence)`. Zachovat UUIDv4, kladnou attemptSequence, requestId korelaci v parseReply i serviceInstanceId/sessionId kontrolu requestu ve službě. JSON duplicity odmítá stávající parser, unknown members a nové vnořené objekty musí projít uzavřeným shape testem. Neplatný detail znamená odmítnutí celé operation odpovědi jako ipc_malformed, nikoli částečné zobrazení.

### Neomezená duplikace URL

Dnešní `WizardIpc::safeError` přijímá volitelný serverUrl a `parseServerUrl` nemá délkový strop (`client/src/core/config/config_parser.cpp:47-125`). Pouhý důkaz velikosti raw polí proto nestačí. Pro failed operaci obsahující httpDiagnostic služba zavolá safeError s původním kódem, statusem a field, ale bez volitelných serverUrl a configPath. URL je již v diagnostickém Request a v aktuálním URL editoru wizardu. Zachová se běžný popis chyby, suggestedAction, recoverable i HTTP status, obecný safeError a chyby bez diagnostiky se nemění. Nevznikne tichý ořez další kopie URL ani nový limit síťového requestu.

Při přítomném httpDiagnostic bude IPC validátor navíc vyžadovat nepřítomné serverUrl/configPath, code do 128 UTF-8 bajtů, description a suggestedAction každé do 4096 UTF-8 bajtů a field serverUrl/serverSecret podle kind. Všechna skutečná safeError pole pocházejí z katalogu a tuto mez musí potvrdit test. Neopravovat nadlimitní text tichým řezem. Serializerové invarianty otestovat na všech relevantních safeError variantách a při porušení nedodat nevalidní diagnostickou operaci.

### Horní mez

Pro byte-string připadá na jeden původní octet nejvýše 6 bajtů JSON. C0 lze zapsat jako `\u00HH`, quotes/backslash mají 2, ASCII 1 a U+0080 až U+00FF v dnešním serializeru 2 UTF-8 bajty. I při serializeru escapujícím všechny U+00XX zůstává mez 6. Každý směr přenáší nejvýše 65536 octets a 22 separator bytes, konzervativně tedy `6 * 65558 = 393348` bajtů textových hodnot. U nezkráceného směru je separator prázdný a mez menší.

Oba směry nejvýše 786696 bajtů. Pro veškerý zbývající obsah rezervovat a testem vynutit 65536 bajtů: dvě chybové textové položky do 4096 bajtů mají po escaping nejvýše 49152 bajtů, code do 128 má 768, tři UUID mají dohromady 108 ASCII znaků, čtyři uint64 hodnoty nejvýše 80 číslic. Klíče, enumy, field, boolean, status, uvozovky, interpunkce a obálka ipcVersion/requestId/result mají méně než zbývajících 15428 bajtů. Test spočítá přesnou délku konstantní struktury a tuto podmínku hlídá.

Celkově tedy nejvýše **852232 bajtů**, což je méně než **4194304 bajtů** s rezervou **3342072 bajtů**. Nezavádět chunkované IPC, nový fetch příkaz ani změnu kIpcMaxMessageBytes. Test sestaví obě zprávy s maximálním šestinásobným escapingem, plné chybové meze a uint64 maximum, zavolá skutečný operationValue/buildResult/parseReply a ověří, že nepřišla fallback obálka `ipc_message_too_large`. Test navíc vyzkouší obrovský serverUrl ve vstupu služby a ověří jeho nepřítomnost v bezpečné chybě s diagnostikou.

### Kompatibilita

Nový tray přijme přesně původní osmipolovou operaci bez detailu. Při absenci pole nebude žádné tlačítko. Nový detail má explicitní format a jeho neznámá hodnota je chyba. Zachovat ipcVersion=1 a existující pipe. Starý tray s přísným parserem nové failed operation s polem odmítne. Nasazovat proto službu a tray společně a v client.md tento případ uvést. Nemapovat libovolné malformed odpovědi na wizard_unsupported ani neoslabovat stávající kontrolu legacy unknown-command. Úspěšné operace a operace bez diagnostiky si zachovají dosavadní tvar.

## 7. Životnost a ochrana před zastaralými daty

`WizardModel` bude mít accessor `httpDiagnostic()` a jediný helper `clearHttpDiagnostic()`. Detail musí být vymazán při beginCheck ještě před lokální validací, při fail z lokální/IPC chyby, změně URL/secret/default-secret, back, invalidate relevantního checkpointu, load/reset/resume, interrupt, sessionExpired, expireAttempt, cancellationFinished a ukončení modelu. V applyOperation nejprve dokončit všechny současné korelační/deadline kontroly, potom volat fail a teprve poté přesunout platný detail. Jinak by obecné fail nově přijatý detail ihned vymazalo.

Model nesmí přijmout detail do jiné služby nebo session jen na základě stejného čísla sequence. WizardWindow zachová generation guard a vazbu odpovědi na aktuální view. Při closed attempt ignorovat každý další terminal result. Při cizí epoše vyvolat stávající interrupt a odstranit detail.

Ve službě vymazat detail při novém přijatém startu, rušení daného pokusu, reset/resume, close, lease expiry, foreign connect/epoch a shutdown. Mazání terminální diagnostiky vložit před early return `cancelLocked`, nebo do explicitního helperu volaného před ním. Nesmí se přitom znovu spustit/cancelovat již dokončený HTTP request. Operace může zachovat bezpečnou chybu a identitu pro protokolové uzavření, raw obsah už ne.

Back nebo změna vstupu po selhání musí odstranit i kopii služby. WizardWindow před ztrátou původní identity odešle existující `cancel_attempt` pro tento konkrétní failed attempt. Použít stávající control reader, nikoli nový příkaz. Udělat to jednou při přechodu z „má detail“ na „nemá detail“, ne při každém znaku editoru. Ztracená odpověď nevrátí lokální detail. Pokud se požadavek nedoručí, poslední pojistkou služby je lease expiry nebo nový start. Pozdní cancel pro starou sequence nesmí vymazat nový pokus. Kontrolovat attempt ID současně se sequence.

Detailové okno se při invalidaci zavře, jeho editory se vyprázdní a zničí. Nesmí dál zobrazovat kopii starého requestu vedle nového wizardu. Zavření detailu samotným X může ponechat modelový detail pro opětovné otevření v témže failed attempt. Zavření wizardu zničí vlastníka i detail. Stop/take/clearPending v WizardReader nesmí zachovat raw reply po skončení relace.

Raw buffery nepředávat loggeru, exception textům, konfiguračnímu store, clipboardu automaticky ani telemetrii. Uživatelův explicitní Copy je povolený. Vlastní objekty mazat a přesouvat tak, aby se minimalizovaly kopie. Nelze slibovat vymazání všech kopií v paměti OS/allocatoru nebo uživatelově clipboardu, požadovaná životnost znamená odstranění aplikačně dostupných dat a žádnou perzistenci.

## 8. Win32 UI a soubory ke změně

Přidat nový `HttpErrorDetailWindow` do `client/apps/tray/http_error_detail_window.h/.cpp`. WizardWindow jej vlastní přímo. Metody: `open`, `close`, `selectTab`, `layout`, `updateFonts`, `translate` a window procedure. Před otevřením uložit identitu detailu, další kliknutí stejného pokusu jen aktivuje existující okno. Native okno má owner HWND wizardu, caption `Error detail`, resize, X a žádný footer.

Nahoře WC_TABCONTROLW s taby `Request`, `Response`, pod ním karta s original size a jen potřebnými vysvětlujícími poznámkami. Každý tab má vlastní native EDIT `ES_MULTILINE | ES_READONLY | ES_AUTOHSCROLL | ES_AUTOVSCROLL`, oba scrollbary a WS_TABSTOP pouze pro aktivní/viditelný obsah. Použít monospace font, SS_NOPREFIX pro labels, žádné rozpoznávání HTML ani odkazů. Text nastavit jednou při otevření, přepínat viditelnost, zachovat výběr a scroll každého tabu. Explicitně nastavit limit editoru nad maximální projekci a otestovat skutečně dosažitelný konec textu.

Znovu použít společné UiResources, paintCard, positionControls, LayoutFrame a TabCtrl_AdjustRect. `drawInfoTab` přesunout beze změny vizuálu z editor_window.cpp do sdíleného helperu v ui_controls.h/.cpp, například `drawModernTab`. Show Server Info pak použije tentýž helper. Nevolat EditorWindow::setContent nebo prettyEditorJson pro diagnostiku. `attachModernTextBox` má dnes výslovný kontrakt jen pro single-line EDIT, proto jej na multiline nenapojovat naslepo. Použít existující multiline editorový povrch/kartu ze Show Server Info se stejnými barvami, fontem a nativním EDIT. Nesnažit se v této funkci obecně přepisovat textbox infrastrukturu.

Do ButtonRole přidat `error` a v paintModernButton použít existující error paletu a čitelný foreground s focus/hover/pressed/disabled stavy. Nepoužít automaticky reset roli jen kvůli jiné barvě. Sdílený renderer zachová native klávesnici a accessibility.

Footer specifikace má čtyři logické sloty reset/back/errorDetail/next. Viditelnost a umístění detailu předává model do computeWizardLayout. V kroku Server URL bude trailing skupina v pořadí dosavadní Back podle stávajícího chování, Error detail, Verify and continue. Ve druhém kroku bude Back, Error detail, Verify and continue. Tím je detail v prvním kroku bezprostředně vlevo od primary a ve druhém bezprostředně vpravo od Back. Reset zůstane leading. Použít standardní 8 DIP gap, měřené šířky a stávající přelomení skupin při malé šířce. Hidden detail nevytváří prázdný slot. Visual order se promítne do native tab order. Primary label není součástí přejmenování v tomto plánu, identifikuje se existujícím nextId.

| Přesné soubory | Změna |
|---|---|
| `client/src/core/wizard/http_diagnostic.h`, `.cpp` nové | Bounded capture, výběr úseků, octet codec, safe text projekce, konstanty a validace velikostí. |
| `client/src/platform/iface/transport.h` | Opt-in flag a volitelný bounded výsledek, zachování starých overloadů. |
| `client/src/platform/windows/transport_winhttp.cpp` | post/exchange a per-request capture, headers, klasifikace přijaté response, žádná změna pořadí či deadline. |
| `client/src/core/wizard/probe_client.h`, `.cpp` | Opt-in, lokální candidate/failedDiagnostic, prepare/post/health/info/checkUrl/checkSecret, takeFailedHttpDiagnostic. |
| `client/src/core/wizard/wizard_types.h` | Volitelné httpDiagnostic v WizardOperationResult. |
| `client/src/core/wizard/wizard_service.h`, `.cpp` | probeTask, startProbe, execute, cancelLocked/closeLocked/expiry/interrupt/shutdown a odstranění redundantní URL jen u diagnostické chyby. |
| `client/src/core/ipc/wizard_ipc.h`, `.cpp` | operationValue/parseOperation a vnořené přesné validátory, beze změny safeError obecného kontraktu. |
| `client/src/core/wizard/wizard_model.h`, `.cpp` | Diagnostika v modelu a všechny lifecycle větve uvedené výše. |
| `client/apps/tray/wizard_window.h`, `.cpp` | Vlastník detailu, button ID, open/render/layout/next/command/receive/endSession/destroy/translate a cleanup přes cancel_attempt. |
| `client/apps/tray/wizard_layout.h`, `.cpp` | Rozšíření geometry a vstupů computeWizardLayout o viditelnost a šířku detailu. |
| `client/apps/tray/http_error_detail_window.h`, `.cpp` nové | Samostatné owned okno se dvěma read-only EDIT a card labels. |
| `client/apps/tray/ui_controls.h`, `.cpp` | Sdílený tab renderer a ButtonRole::error v existujícím button rendereru. |
| `client/apps/tray/editor_window.cpp` | Pouze použití sdíleného tab rendereru, žádná změna Show Server Info obsahu nebo akcí. |
| `client/apps/tray/wizard_reader.h`, `.cpp` | Jen pokud test odhalí retenci odpovědi po stop/clearPending, zajistit destrukci bounded dat. Žádná druhá detailová síťová cesta. |
| `client/src/CMakeLists.txt`, `client/apps/CMakeLists.txt`, `client/tests/CMakeLists.txt` | Registrace nových helperů/okna/testů i ve všech test targetech kompilujících wizard_window nebo editor_window. |
| `client.md`, `protocol.md` | Aktualizace po implementaci podle části 10. |

## 9. Pořadí realizace a testovací matice

### Pořadí

1. Ověřit větev, uživatelské změny a aktuální Graft kotvy. Zapsat výchozí build/test stav bez úprav cizích artefaktů.
2. Implementovat bounded helper, octet codec a Unicode projekci, nejprve unit testy přesných byte hranic a obálky.
3. Přidat opt-in transportní capture a testy na lokálním socket serveru. Udržet původní transportní accounting, timeouty a cancellation.
4. Připojit ProbeClient, WizardService, operation types a strict IPC s unit testy konkrétních failed výměn a lifecycle závodů.
5. Připojit WizardModel a cleanup služby přes existující kontrolní příkazy. Dokázat, že stale result nemůže znovu naplnit detail.
6. Implementovat owned UI, footer a sdílené tab vykreslení. Ověřit automatické native UI scénáře i vizuální rozměry.
7. Aktualizovat dokumentaci a po větších změnách obnovit Graft graf přes `graft build`. Zkontrolovat diff, žádný produktový zásah do phpvpn.php.
8. Dokončit build a relevantní automatické testy, potom lokální Nginx/PHP integraci a obnovení jejího prostředí.
9. Až nakonec externí best-effort ověření. Předat výsledky a neprovedené scénáře pravdivě, bez raw obsahu v reportu.

### Automatické testy

| Vrstva / cílové testy | Povinné scénáře a oracle |
|---|---|
| Nový `client/tests/unit/test_http_diagnostic.cpp` | N 0 pro collector helper, 1, 64511, 64512, 65535, 65536, 65537, 66560, velká streamovaná zpráva. Přesné indexy, čtyři separator řádky, žádný překryv, samostatné řezy request/response. N=65537 musí mít omitted=1. Hlavičky samy přes limit, prázdné body, obsah začínající/končící CR/LF a obsah se svými řádky `...`. |
| Stejný helper test | Streaming po jednotlivých byte i různých blocích musí odpovídat oracle nad celým B. uint64 overflow nesmí wrapnout. Výpočet original size zaokrouhlení 0.00, 64.00 i UINT64_MAX, bez floating point. |
| Codec/Unicode | Všech 256 byte hodnot round-trip. 2/3/4byte UTF-8 znak přes konec prefixu a začátek suffixu, neplatné sekvence, NUL, DEL, controls, emoji, CRLF/LF. Přesné \xHH per-byte chování, žádné spojení znaků přes skrytou mezeru. Žádný NUL či osamocený surrogate v Win32 projekci. |
| `client/tests/unit/test_winhttp_accounting.cpp` | Rozšířit BodyServer scénáře o zachycené request body/action, header duplicates, status line, systémové request headers, HTML/JS body, non-200 s prázdným/velkým body, response limit, Content-Length mismatch a query fallback. Vypnutý flag vždy bez capture. Stávající testy SuccessfulAndUnsuccessfulHttpStatusesCountBodyOnly, OversizedReadBlockIsAccountedBeforeLimitRejection a deadline/cancellation/reuse musí dále projít. |
| `client/tests/unit/test_runtime_status.cpp`, sekce probe | URL malformed health, chybná korelace, non-200, contentType HTML. Secret: selže health a info se nevolá, health uspěje a info crypto/parser selže, health uspěje a info selže lokálně či bez odpovědi. Ověřit přesný action/body a že se nezveřejní úspěšný health. Přijímané odpovědi s přidaným HTML/JS zůstanou přesné před parserem. |
| Stejný soubor, sekce wizard_service | Diagnostika jen failed url/secret, žádná u access/finish/success. Cancellation, session close, expiry, epoch change, nový start, pozdní cancel a pozdní worker outcome. Pod mutexem se nesmí obnovit detail po invalidaci. Zachovat stávající prerequisite evidence a idempotenci. |
| `client/tests/unit/test_wizard_ipc.cpp` | Old shape bez pole, validní nové schéma, odmítnutí null/extra/missing/duplicate member, unknown format/action, wrong kind/state/payload, špatné číselné typy a overflow, jiné délky segmentů, nesprávný separator, scalar >255. Negativní testy pro error.serverUrl/configPath a překročení textových mezí pouze s diagnostikou. Celý round-trip přes parseReply. |
| Maximální IPC test tamtéž | Dva maximální escaped směry, největší error pole, uint64 maximum, skutečná serializerová délka <=852232 a <4 MiB. Bez fallback chyby, bez chunků. Obrovský vstupní serverUrl se nesmí opakovat v error. |
| `client/tests/unit/test_wizard_model.cpp` | Správný failed detail a mismatch každého operationId/clientAttemptId/sequence/kind/epoch, closed attempt a deadline. Lokální validační fail po předchozím detailu, nový pokus, edit URL/secret, default-secret switch, Back, reset/load, service disconnect, IPC error, expiry a zrušení. |
| `client/tests/unit/test_wizard_layout.cpp` | Button hidden/visible v obou krocích, bezprostřední sousedství, gap 8 DIP při běžné šířce, bez kolize při kompaktní šířce, měřený primary a chybový text. DPI 96/144/192, běžné a malé pracovní plochy. |
| `client/tests/ui/tray_ui_tests.cpp` a stávající native wizard scénáře v `client/tests/unit/test_wp05_windows.cpp` | Kliknutí, správný owner HWND, přesně dva taby, no OK/Cancel/footer, multiline/read-only, výběr a Ctrl+C, tab/shift-tab a šipky, resize/DPI, zobrazení celého konce maximálního obsahu, zavření X, opakované otevření a automatické zavření při novém pokusu. HTML/JS jen inertní text. |
| UI regrese | Show Server Info taby po vyčlenění rendereru, error button barvy včetně focus/disabled/high contrast. Existující reset/primary tlačítka beze změny. |

Build prostředí používá `client/CMakePresets.json`: debug, release a asan s Ninja/MSVC. V odpovídajícím developer shellu z adresáře client provést `cmake --preset debug`, `cmake --build --preset debug` a `ctest --preset debug --output-on-failure`. Native UI jsou explicitní opt-in `PHPVPN_BUILD_UI_TESTS=ON`, cíle `phpvpn-tray-ui-tests` a `phpvpn-wp03-ui-tests` nejsou součástí unattended CTest, spustit je samostatně v interaktivní session. Pro release provést build a relevantní regresní testy po dokončení změn. ASAN použít pro nové buffer/Unicode/lifecycle cesty, pokud je v prostředí funkční, a zaznamenat případný blocker. Nevyžadovat bezdůvodně SCM/Wintun administrátorské E2E pro tuto lokální UI diagnostiku.

### Lokální Nginx/PHP ověření

Po úspěchu automatických testů read-only ověřit skutečnou existenci `D:\nginx`, aktivní konfiguraci a přesný document root/testovací phpvpn.php. Neodvozovat místo souboru jen z názvu složky. Ověřit absolutní cíl a zachovat obnovitelný původní obsah, hash a relevantní konfiguraci před změnou. Použít oddělený testovací endpoint nebo kopii, pokud to stávající lokální instalace dovoluje.

Otestovat přes HTTP tři injekční scénáře: poškozený health pro první krok, poškozený health ve druhém kroku, validní health a poškozené info. Přidat diagnostický text/HTML/JavaScript na začátek i konec odpovědi, dále syntakticky poškozený jediný byte a odpovědi kolem velikostních hranic. Skutečné měření zahrnuje headers, proto fixture musí předem vypočítat odpovídající body délku, neoznačovat 64KiB body za 64KiB celou zprávu.

Ověřit bezpečnou chybu, červené tlačítko, konkrétní action a body, přesný bounded response výpis, original size, inertní JS a odstranění detailu po dalším Verify. Ověřit také lokální endpoint bez HTTP response. Síťový záznam nebo raw autentizovaný obsah neukládat do reportu, stačí scénář, očekávaný/výsledný stav a rozměry/počty. Testovací změny obnovit v finally i po selhání, ověřit původní hash a původní stav konfigurace. Nerestartovat nebo nezastavovat cizí procesy bez potřeby testu.

Pokud Nginx/PHP není dostupný, uvést lokální integraci NOT RUN s konkrétní chybějící závislostí. Netvrdit splnění lokální akceptace pouze na základě unit testů. Externí hosting lokální ověření nenahrazuje.

### Externí best-effort test

Poslední krok po úspěšných lokálních testech: explicitně použít wizard proti `https://phpvpn.freehosting.dev/phpvpn.php`. Neobcházet TLS jiným protokolem jen pro dosažení výsledku, zachovat platné wizard nastavení. Při přijaté chybné response ověřit detail a inertní obsah. Při chybě TLS/DNS/timeoutu bez response ověřit, že se tlačítko neobjeví.

Zapsat `PASS` při skutečném ověření očekávaného chování, `FAIL` při prokazatelném nesouladu klienta, `NOT RUN` s důvodem, pokud nelze provést scénář s přijatou response nebo test nebyl spuštěn. K NOT RUN uvést i případně provedený negativní TLS scénář, aby byl výsledek přesný. Nedostupnost cizího hostingu sama neblokuje dokončení. V této plánovací fázi se lokální ani externí integrační test nespouštěl.

## 10. Dokumentace, rizika a dokončení

V `client.md` aktualizovat části Společný HTTP transport kolem řádku 284, Configuration Wizard kolem 434, Síťové kroky Wizardu kolem 489, Konfigurační a Wizard příkazy kolem 585, Životnost a limity Wizard IPC kolem 635, Chyby a bezpečnost kolem 651 a Povinné testy kolem 746. Popsat opt-in capture, failed-only přenos, korelaci a cleanup, přesné schéma http-octets-v1, velikostní matematiku, odstranění redundantního serverUrl, Unicode projekci, okno, footer, kompatibilitu a testovací výsledky. Nepřenášet do dokumentace tvrzení, že jsou přenášeny plné nadlimitní response.

V `protocol.md`, zejména Transport HTTP, HTTPS a proxy kolem řádku 87, doplnit jasné oddělení serverového wire protokolu od lokálního Wizard IPC. Endpointy, schémata HTTP body a kryptografie se nemění. Diagnostika vzniká před protokolovým parsováním a je aplikační rekonstrukcí WinHTTP, nikoli TLS/packet záznamem. Odkázat na klientský kontrakt řezu a Unicode, neduplikovat druhý odlišný algoritmus.

| Riziko | Opatření a důkaz |
|---|---|
| Ztráta chyby při parser early return | Candidate uložen před status/contentType/parser a explicitně přesunut jen při failure. Test každé vrstvy. |
| Úspěšný health vydaný za detail info | Candidate reset ještě před přípravou info, success okamžitě zahodí data. |
| Neplatné UTF-8 rozbije IPC nebo NUL ukončí textbox | Reverzibilní byte-string a samostatná deterministická bezpečná textová projekce. |
| Řez překročí zdrojový limit kvůli znaku | Žádné přibírání bajtů z mezery, vybrané octets přesné a nezávislé na dekódování. |
| Diagnostická kopie URL překročí IPC | S diagnostikou se nepřenáší volitelný error.serverUrl/configPath, ostatní pole mají ověřené bounded meze. |
| Raw data zůstanou ve službě po Back | Použít existující cancel_attempt se starou identitou a explicitní mazání i terminálních dat. |
| Callback použije uvolněný capture | Capture je per-request a žije do finálního callbacku, zachovat dosavadní jediný closer a synchronizaci. |
| Nadlimitní odpověď spotřebuje druhou plnou kopii | Bounded prefix/tail collector, původní parserový body limit beze změny. |
| Systémové headers nejsou dostupné nebo mají jinou formu | Explicitní zdroj/fallback poznámka, bez falešných headers a bez změny síťového výsledku. |
| UI zobrazuje stará citlivá data | Owned detail zavírat při každé invalidaci, kopie uvolnit, nic automaticky nelogovat ani nekopírovat. |
| Starý tray nepřijme nové pole | Párové nasazení služba/tray a test starého shape v novém parseru, žádné oslabování schema. |

Dokončení realizace vyžaduje všechny relevantní automatické testy, lokální integrační důkaz podle dostupného prostředí a pravdivý externí výsledek. Závěrečný diff musí obsahovat pouze popsanou klientskou funkci, testy, build registrace a dokumentaci. Uvést co bylo testováno, co zůstalo NOT RUN a proč. Zkontrolovat všech 14 akceptačních kritérií intent-v002, zejména jediný chybějící byte, nulový překryv, nepřítomnost detailu bez response, správnou výměnu ve druhém kroku a žádnou změnu wire protokolu. Tento neměnný plán se při pozdějším upřesnění nepřepisuje, vznikne nová úplná verze.
