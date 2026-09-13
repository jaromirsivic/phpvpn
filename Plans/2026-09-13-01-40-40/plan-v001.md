---
plan_version: 1
created_at: 2026-09-13T02:19:19+02:00
source: ..\..\Intents\2026-09-13-01-40-40\intent-v001.md
---

# Realizační plán: Diagnostika HTTP requestu a response v konfiguračním wizardu

## 1. Rozsah a výchozí stav

Autoritou je celý intent v uvedeném `source`. Tento dokument je první úplná neměnná verze plánu. Neobsahuje provedenou implementaci. Realizovat přímo v `C:\git\phpvpn` na `tray_UX`, bez worktree. Při přípravě ověřena větev a HEAD `80b71b800a42909b859380f364fb3fc21e8c5710`. Existující změny `.intent-builder-active` a adresář intentu patří koordinátorovi a musí zůstat zachovány.

Deadline předané plánovací práce je 2026-09-13 04:10:40 Europe/Berlin. Čas lze kontrolovat mezi nástroji, tvrdé automatické přerušení libovolného běžícího nástroje není garantováno. Následná realizace vyžaduje samostatnou volbu uživatele, tento plán ji nespouští.

Výsledná funkce se týká výhradně neúspěšného zpracování přijaté HTTP odpovědi v `Server URL` a `Server secret`. Běžná bezpečná chyba zůstává zobrazena. Volitelná diagnostika zpřístupní červené `Error detail` a samostatné okno `Request` / `Response`. Zachycení nemění request, síťové pořadí, kryptografii, retry ani timeouty. Žádná produktová změna `phpvpn.php`.

## 2. Ověřená mapa implementace

Poznávání začalo `graft map`, následovalo `graft ask --source`, `graft skeleton` a úplné hledání přes `graft grep`. Relevantní nalezené úseky byly dočteny podle potřeby. Cesty a řádky níže se vztahují k výchozímu HEAD.

| Vrstva | Soubory a stávající symboly | Důsledek pro změnu |
|---|---|---|
| Rozhraní transportu | `client/src/platform/iface/transport.h:25–73`, `TransportSettings`, `HttpResponse`, `HttpExchangeResult`, `Transport::post` | Response dnes nese status, content type a body. Exchange samostatně počítá přijaté body bajty i při chybě. |
| WinHTTP | `client/src/platform/windows/transport_winhttp.cpp:44–58,127–172,194–268`, `RequestState`, `WinHttpTransport::post`, `exchange` | Hlavičky jsou dostupné po `HEADERS_AVAILABLE`, handle se zavírá před návratem. Dnešní body čtení i limity musí zůstat zachované. |
| Probe | `client/src/core/wizard/probe_client.cpp:11–89`, `prepare`, `post`, `health`, `info`, `checkUrl`, `checkSecret` | `post` mapuje status a content type ještě před parserem. `health(false)` kontroluje syntaxi, `health(true)` navíc kryptografii a čas. |
| Služba | `client/src/core/wizard/wizard_service.cpp:31–82,175–351,352–416`, `execute`, `startProbe`, `probeTask`, `cancelLocked`, `closeLocked`, `expireLocked` | Terminální výsledek vzniká až po kontrole epochy, generace, cancel a živé relace. `cancelLocked` dnes brzy vrací i pro terminální operaci. |
| Typy a IPC | `client/src/core/wizard/wizard_types.h:6–42`, `client/src/core/ipc/wizard_ipc.cpp:53–87,138–207,395–545` | Rozšířit přesné whitelisty a serializaci. Nepřidávat raw do `WizardError`, `Error.detail` ani `payload` úspěchu. |
| Model | `client/src/core/wizard/wizard_model.cpp:25–28,96–103,123–188`, zejména `applyOperation` | Identita a timeout se ověřují před přijetím výsledku. `fail` zavírá attempt. Diagnostiku přiřadit až po těchto kontrolách. |
| Tray | `client/apps/tray/wizard_window.cpp:122–148,242–340,660–714`, `request`, `receive`, `render`, `layout` | Zachovat window generation a oddělené IPC readery. Nový detail nesmí prodlužovat rozpočet kontroly Next. |
| Layout | `client/apps/tray/wizard_layout.cpp:5–24`, `client/apps/tray/layout.cpp:101–165` | Současná patička má Reset vlevo, Back a Next vpravo. Společný helper má standardní mezeru 8 DIP a zalamování skupin. |
| Vzor detailu | `client/apps/tray/editor_window.cpp:20–56,294–441`, `drawInfoTab`, `selectInfoTab`, `open`, `layout` | Vytáhnout společné kreslení tabů, nepoužívat pretty-print JSON z editoru. |
| Limity | `client/src/core/ipc/ipc_protocol.h:31`, `wizard_ipc.cpp:460–469`, `client/src/platform/windows/ipc_pipe.cpp:452–512` | IPC limit je 4 MiB pro celou JSON obálku, nikoli pouze body. |

Existující testy ProbeClientu a WizardService jsou v `client/tests/unit/test_runtime_status.cpp`, nikoli v samostatných souborech pojmenovaných podle těchto tříd. Transportní reálné testy jsou v `client/tests/unit/test_winhttp_accounting.cpp` a používají `BodyServer`.

## 3. Rozhodnutí o datech a zachycení

### 3.1 Vnitřní reprezentace

Přidat `HttpDiagnosticExchange`, který vlastní neměnná data konkrétního volání:

- `action`: pouze health nebo info pro tuto funkci.
- Request: metoda `POST`, plná skutečná cílová URL s `?action=…`, blok přesně aplikací předaných hlaviček, volitelný blok systémem dostupných request headers a původní body jako bajty.
- Response: dostupná původní status line, numerický status 100–599 nebo nepřítomný, blok dostupných raw response headers a původní body jako bajty.
- Metadata: `responseReceived`, `bodyComplete`, `statusLineSource` s hodnotou `transport` nebo `reconstructed`, dostupnost systémových request headers a response headers.
- Rozlišit chybu zpracování odpovědi od chyby provedení transportu. Samotné `responseBodyBytesRead > 0` není dostatečná podmínka nabídky detailu.

Body ukládat délkově, nikoli jako nulou zakončený text. Hlavičky uchovat v podobě vrácené WinHTTP, včetně duplicit a pořadí. Nepřevádět je do mapy, která by sloučila nebo ztratila položky. Hlavičky jsou aplikační Unicode reprezentace WinHTTP, body jsou nezměněné bajty. Nejde o wire dump.

`TransportSettings` dostane výchozí vypnuté `captureWizardHttpDiagnostics`. Nastavit pouze pro `checkUrl` a `checkSecret`, navíc podle IPC opt-in uvedeného níže. `checkAccess`, runtime worker, Server Info a ostatní uživatelé transportu zůstanou bez zachycení. Zachovat stávající signaturu `Transport::post`, rozšířit návratový `HttpExchangeResult` o volitelný vlastněný snapshot a klasifikaci dokončení. Zachovat dosavadní konstruktory, agregátové inicializace a účtování bajtů.

Pro velké body použít sdílený neměnný vlastník, případně přesun vlastnictví při dokončení, aby parser, služba a serializace nedržely opakované plné kopie. Žádná statická nebo globální proměnná „last response“. Datový vlastník se explicitně uvolňuje při invalidaci a při destrukci best-effort vymaže vlastní paměť. Nelze slibovat forenzní odstranění všech kopií vytvořených OS.

### 3.2 Místo a okamžik zachycení

V `WinHttpTransport::exchange` uložit přesnou metodu, URL, hlavičky předané `WinHttpSendRequest` a body vstupu. Po dokončeném `WinHttpReceiveResponse` a `HEADERS_AVAILABLE`, před stávající interpretací content type/content length a před zavřením handle, získat response headers a dostupné request headers.

Použít dvouprůchodové `WinHttpQueryHeaders`: nejprve velikost, potom dynamický buffer, s kontrolou přetečení a výsledné délky. Response dotaz `WINHTTP_QUERY_RAW_HEADERS_CRLF`, request dotaz navíc s `WINHTTP_QUERY_FLAG_REQUEST_HEADERS`. Žádný pevný 256znakový limit pro diagnostické hlavičky. Dotaz provést až ve správném stavu handle, synchronní povaha dotazu nemění asynchronní síťový tok. Viz [Microsoft: WinHttpQueryHeaders](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpqueryheaders).

Request zobrazuje odděleně aplikací předané hlavičky a systémový snapshot, pokud existuje. Jejich původ bude označen mimo vlastní bloky, aby duplicitní obsah nebyl vydáván za dvojí odeslání. Pokud systémový snapshot není dostupný, zachovat všechny aplikační hlavičky a označit nedostupnost systémového bloku. Response status line převzít z raw hlaviček, jinak sestavit `HTTP status <code> (reconstructed)`. Nevymýšlet HTTP verzi nebo reason phrase. Selhání dodatečného diagnostického dotazu nesmí nahradit původní bezpečnou chybu ani proměnit úspěšný probe v chybu.

Přijaté body zachytit před `ResponseParser::mapServerError`, kontrolou content type, `parseHealthSyntax`, `verifyHealth`, `TimeSync::makeSnapshot` a `parseInfo`. Nevkládat sem dešifrované info, normalizovaný JSON ani text z `Error::message()`.

Na každém novém `health` nebo `info` vymazat kandidáta ještě před generováním UUID, timestampu a kryptografickou přípravou. Nestačí reset až uvnitř `post`, protože lokální příprava info může selhat ještě před jeho voláním. Po úspěšném zpracování jednotlivého HTTP volání kandidáta ihned uvolnit. Při odpovídající chybě zpracování přesunout kandidáta do `failedHttpDiagnostic_`. `takeFailedHttpDiagnostic()` je jednorázový přesun do služby.

### 3.3 Klasifikace hranic

| Situace | Detail |
|---|---|
| Přijatá odpověď, chybný status, content type, syntaxe, korelace, MAC, decrypt nebo časový údaj z odpovědi | Ano, konkrétní chybující výměna a dosavadní bezpečná chyba. |
| URL probe a syntakticky validní odpověď s kryptograficky chybným obsahem | Zachovat dnešní `health(false)`. Kryptografii nepřidávat. Kryptografické odmítnutí ověřovat v secret kroku. |
| Úspěšný health a selhání přípravy/transportu info bez odpovědi | Ne. Žádná předchozí úspěšná výměna. |
| DNS, TCP, TLS, timeout, cancel, IPC chyba nebo přerušená relace | Ne, nevznikla výsledná chyba zpracování odpovědi. Platí i při transportním přerušení po částečném příjmu. |
| Odpověď odmítnutá při kontrole transportního body limitu nebo metadat | Zachovat skutečně již přijaté bajty, pokud jsou dostupné, s `bodyComplete=false`. Výslovně uvést, že transport nepřijal celou odpověď. Nečíst další data pouze kvůli diagnostice. |

Při limitu zahrnout i poslední již dokončený read blok, který limit překročil, nepředstírat jeho nepřijetí. Dnešní buffer má 64 KiB, zachycený prefix proto může být nejvýše aktuální body limit plus jeden 64KiB blok. Toto není tichý ořez diagnostiky, je to úplný záznam aplikačně přijatých bajtů s výslovným údajem o nedokončené HTTP odpovědi. Transportní timeout a rušení mají stále přednost při volbě konečného výsledku.

## 4. Velikostní strategie a přesné Wizard IPC

### 4.1 Rozhodnutí: samostatné blokové načítání

Neměnit globální `kIpcMaxMessageBytes = 4 * 1024 * 1024`. Výchozí `maxResponseBodyBytes = 67108864` je 64 MiB a konfigurace nemá horní hranici 64 MiB. Přímá JSON string reprezentace může expandovat až šestkrát, samotné 64MiB body tedy až na 384 MiB bez headers, requestu a obálky. Ani přímé Base64 by se nevešlo. Fixní navýšení na „64 MiB plus rezerva“ není správné.

Terminální neúspěšný operation result dostane pouze volitelný objekt `httpDiagnostic` s deskriptorem. Vlastní request a response se načtou ze služby po blocích výhradně po kliknutí. Tím se současně omezí opakované přenosy při pollingu. Blokování dokumentů/configu se nemění, jde o zvlášť dokumentované příkazy pro HTTP detail.

### 4.2 Opt-in a operace

`wizard-check-url` a `wizard-check-secret` přijmou navíc jediný volitelný člen `diagnosticsVersion`, přesně celé číslo `1`. Vynechání znamená staré chování bez zachycení. Na ostatních stávajících příkazech je tento člen nepovolen. Uložit opt-in do `Operation`.

Původních osm členů operation objektu zůstává povinných. Povolit pouze devátý volitelný `httpDiagnostic`. Vynechává se úplně při úspěchu, běžném selhání bez odpovědi a při starém klientovi. `null` ani jiné náhradní hodnoty nepřijímat. Diagnostika je platná pouze pro `operationState=failed`, `kind=url|secret`, nenulovou existující chybu, platné `clientAttemptId` a kladné `attemptSequence`. `payload` zůstává null.

Deskriptor má právě tyto členy:

```text
version: integer 1
diagnosticId: UUID v4
action: "health" | "info"
httpStatus: integer 100..599 | null
bodyComplete: boolean
statusLineSource: "transport" | "reconstructed"
systemRequestHeadersAvailable: boolean
responseHeadersAvailable: boolean
parts: object s přesně osmi nezápornými uint64 délkami
```

Klíče `parts` jsou přesně `requestMethod`, `requestUrl`, `requestApplicationHeaders`, `requestSystemHeaders`, `requestBody`, `responseStatusLine`, `responseHeaders`, `responseBody`. Textové části jsou UTF-8 encoding aplikačního textu, body zůstávají bajty. Délky označují bajty před Base64. `url` operace dovoluje pouze health. `secret` dovoluje health nebo info. Pokud safe error nese HTTP status, musí se shodovat s deskriptorem. Chybějící systémové hlavičky mají délku nula a availability=false. Prázdné body je legitimní, nikoli důkaz nepřijaté odpovědi.

Identita deskriptoru je zděděna z obklopujícího výsledku a aktuální session: `serviceInstanceId`, `sessionId`, `operationId`, `clientAttemptId`, `attemptSequence`, `connectionEpoch`. Nedělat z `diagnosticId` samostatnou globální přístupovou schopnost.

### 4.3 Nové příkazy

Přidat do `WizardCommand` a mapy jmen:

1. `wizard-get-http-diagnostic-chunk`.
2. `wizard-release-http-diagnostic`.

Oba mají přesně společnou obálku `ipcVersion=1`, `requestId`, `command`, `serviceInstanceId`, `sessionId` a identitu `operationId`, `clientAttemptId`, `attemptSequence`, `connectionEpoch`, `diagnosticId`. Čtecí příkaz navíc obsahuje `part` jako jeden z osmi názvů a `offset` jako nezáporné uint64. Klient si nevolí libovolnou velikost bloku.

Čtecí odpověď v běžné `buildResult` obálce má právě `operationId`, `clientAttemptId`, `attemptSequence`, `connectionEpoch`, `diagnosticId`, `part`, `offset`, `totalBytes`, `dataBase64Url`, `eof`. `dataBase64Url` je kanonické Base64URL bez paddingu, kódování použije existující `core/protocol/base64url.*`. Délka dekódovaného bloku musí být přesně `min(196608, totalBytes-offset)`. `eof` odpovídá rovnosti `offset+decodedSize=totalBytes`. Pro nulovou část a offset 0 vrátit prázdná data a eof=true. Offset za koncem odmítnout. Počítat rozdílem, nevytvářet přetékající součet.

Odpověď release má přesně `operationId`, `clientAttemptId`, `attemptSequence`, `connectionEpoch`, `diagnosticId`, `released=true`. Opakovaný release stejné známé identity je idempotentní. Odlišná identita se odmítne a nesmí odstranit nový detail. Po release držet pouze identifikátor/tombstone do nahrazení operace, žádná raw data. Další get-chunk vrátí bezpečné `wizard_attempt_closed`.

Upravit `requestFields`, `parseRequest`, `buildRequest`, `operationValue`, `parseOperation`, `parseReply`. Přidat typované helpery pro deskriptor a blok. Neoslabovat `shape` obecně, každá větev má přesně vyjmenované povinné a volitelné členy. Odmítnout duplicity, cizí členy, floaty, záporná čísla, přetečení, špatné UUID, nekanonické Base64, nesoulad délek, části a EOF. Serializátor nesmí vytvořit stav zakázaný parserem.

`IpcDispatcher::handle` už směruje přes `WizardIpc::command`, ověřit nové směrování testem. Pořadí enumu a predikát `command >= reset` nepoužívat jako důkaz nového schématu, pro nové příkazy explicitně vyjmenovat členy.

### 4.4 Důkaz limitu a spotřeba paměti

Blok 196608 bajtů má nejvýše 262144 Base64URL ASCII znaků, které nepotřebují JSON escaping. Deskriptor ani bloková metadata neobsahují libovolně dlouhý serverový text. Pro plnou obálku rezervovat a testem dokázat horní mez 4096 bajtů. Největší bloková zpráva je tedy nejvýše 266240 bajtů, bezpečně pod 4194304. Serializer nadále ověřuje skutečnou velikost zprávy. Request URL i hlavičky jsou samostatné blokované části, takže ani extrémně dlouhá URL nezvětší deskriptor.

Žádná nová celková diagnostická kvóta nesmí zkrátit úspěšně přijatou odpověď. Velikost zůstává odvozena od existujícího transportního limitu a dostupné paměti. Blokovat při serializaci přímo z vlastníka dat, nesestavovat nejprve obrovský JSON nebo Base64 celého body. V tray sestavovat po částech s kontrolou limitů `size_t`, uint64 a Win32 textových délek, nealokovat slepě podle nedůvěryhodné deklarované délky. Pro každý stream append kontrolovat reálně přijatou délku, hlavičky i body mohou být větší než jeden blok.

Při nedostatku paměti zobrazit bezpečné selhání načtení detailu, nikdy neoznačit prefix za úplný. Původní probe chyba se nemění. Úplný detail musí fungovat minimálně na výchozích 64 MiB a ověřených hraničních velikostech. Pro nepodporovatelnou nativní textovou délku vrátit explicitní chybu zobrazení, nikoli tiché zkrácení. Nezvyšovat pipe kapacity ani limity ostatních příkazů.

## 5. Služba, korelace a životnost

`probeTask` převezme diagnostiku teprve po dokončení ProbeClientu. Pod `mutex_` nejprve vyhodnotí stávající priority: změna epochy/interrupted, cancel, zaniklá relace, shutdown, generation mismatch. V těchto větvích data zahodí. Pouze failed URL/secret s odpovídající chybou zpracování přijaté odpovědi dostane vlastníka raw dat v `Operation` a deskriptor ve výsledku. `WizardError` se vytvoří stávajícím `safeError`.

Každý get-chunk ověří živou session, service instance, aktuální offline epochu, současnou operation, všechna ID, sequence, generaci, terminální failed stav a přítomný stejný diagnosticId. Nepoužívat fallback do registru commit receipts. Ty nesmějí obsahovat HTTP diagnostiku. Snapshot je po publikaci neměnný, jeden blok lze přečíst opakovaně beze změny. Nové čtení po invalidaci je zakázané. Jediná již rozpracovaná odpověď může bezpečně dokončit svůj malý blok, tray ji odmítne podle generace.

Přidat centrální `clearHttpDiagnosticLocked(Operation&)`, oddělený od mazání prerequisite evidence. Volat jej i pro již terminální operace. Doplnit před dnešní early return v `cancelLocked`, při novém přijatém attempt sequence ještě před možným lokálním odmítnutím probe, reset/resume, close, expiraci, interrupt/connect a shutdown. Přesun předchozího výsledku do lokální kopie nesmí prodlužovat držení celého body při běžném get-operation.

V modelu držet deskriptor a identitu pouze aktuálního failed pokusu. `applyOperation` použije stávající korelaci, timeout a epoch check, zavolá bezpečné `fail` a potom přiřadí platný deskriptor. Běžné `fail` detail maže. Zastaralá odpověď nesmí přepsat ani odstranit detail novějšího platného pokusu.

Detail zneplatnit před lokální validací nového Next, nikoli až po úspěšném `beginCheck`. Stejně při změně URL/secret, Back, změně kroku, load/reset/resume, timeoutu, IPC chybě, cancel, interrupt, sessionExpired, destroy a endSession. UI současně zavře okno detailu, zastaví reader a zruší rozpracovanou sestavu.

Pokud změna kroku nebo vstupu sama neposílá nový probe, frontovat release přes samostatný krátký control reader s původní identitou. Změna UI na síťovou odpověď nečeká. Při dostupné službě release data odstraní hned, při výpadku se nesmí držení prodlužovat heartbeat obnovováním staré session. Ukončit dotčenou session podle současného chybového lifecycle, vzdálená data uklidí lease. Místní approvedDiscard/endSession zachová existující pravidlo bez IPC, vzdálené odstranění zajišťuje stávající 60s lease. Nepřidávat kvůli tomu sledování PID nebo nové servisní vlákno.

Detail window zavřený křížkem uvolní své dekódované/textové kopie. Ve službě lze data uchovat do invalidace relevantního failed pokusu, aby opětovný klik četl tutéž výměnu. Přepínání tabů data nepřepisuje. Automatický zápis do logu, telemetry, souboru, konfigurace či clipboardu je zakázaný. Clipboard naplní pouze standardní explicitní kopírování uživatelem.

## 6. Win32 UI a zobrazení

### 6.1 Tlačítko a layout

Rozšířit `WizardLayout` o čtvrté tlačítko a vstup `errorDetailVisible` + krok. Použít stávající `computeButtonFooterLayout`, měřenou šířku textu a standardní 8 DIP gap. Pořadí v prvním kroku: Reset vlevo, vpravo stávající Back podle dosavadní politiky, `Error detail`, `Verify and continue`. Ve druhém kroku: Reset vlevo, vpravo `Back`, `Error detail`, `Verify and continue`. Skryté tlačítko nerezervuje místo a není v tab order. Zachovat stávající skupiny, prioritu primárního tlačítka a responsivní zalomení.

Použít existující červenou roli `ButtonRole::reset` a její hover/pressed/focus barvy pro vzhled detailového tlačítka. Role určuje kreslení, command ID určuje akci. Nepřidávat nový barevný systém. Ověřit také high contrast, kde sdílené systémové barvy mohou záměrně nahradit červenou kvůli čitelnosti.

`WizardWindow::render` odvodí viditelnost z modelového `canShowHttpDiagnostic()`, ne pouze z HTTP statusu nebo libovolné chyby. `layout` a nativní Z/tab order řadí tlačítka podle vizuálního pořadí. Zachovat focus při běžném renderu, při odstranění aktivního detailového tlačítka přesunout focus na smysluplný dostupný prvek.

### 6.2 Samostatné okno

Přidat `HttpErrorDetailWindow` v nových `client/apps/tray/http_error_detail_window.h/.cpp`. Top-level owned window s owner HWND wizardu, resize, standardní titulek `Error detail` a zavírací křížek. Na opakovaný klik aktivovat existující okno stejného detailu. Žádná patička, OK ani Cancel.

Dva taby `Request`, `Response`, dva víceřádkové read-only textové editory s horizontálním a vertikálním posuvem, monospaced fontem a výběrem/kopírováním. Použít stávající moderní textbox attachment a `UiResources`. Vyčlenit `drawInfoTab` do sdíleného helperu `ui_tabs.h/.cpp`, který využijí EditorWindow i nový detail. Změna Server Info je pouze sdílení kreslení bez změny chování.

Obsah se načítá asynchronně samostatným readerem až po kliknutí, nesmí blokovat UI thread nebo heartbeat. Během načítání jsou dostupné pouze dva taby s krátkým stavovým textem. Raw obsah publikovat až po ověřeném dokončení všech osmi částí. Chyba čtení zruší neúplnou sestavu a zobrazí bezpečný stav načtení, nikdy prefix vydávaný za úplnou odpověď. Původní safe error wizardu zůstává zachována. HTTP se znovu nevolá.

Reader musí kontrolovat requestId, celou identitu, part, offset, totalBytes, Base64 a eof i vlastní detail generation. Po zavření okna, změně pokusu nebo relace ignorovat opožděné bloky. Jeden blok má dosavadní 5000ms IPC timeout. Načítání detailu má vlastní konečný rozpočet, odvozený od počtu očekávaných bloků krát 5000 ms s kontrolou přetečení, nikdy nemění deadline již ukončeného Next. Bloky se čtou sekvenčně bez automatického HTTP retry.

Request zobrazí metodu, cílovou URL, zachované bloky headers s označeným původem, prázdný řádek a původní body. Response zobrazí transportní nebo výslovně rekonstruovaný status, dostupné hlavičky, prázdný řádek a body. Metadata o nedokončeném příjmu a dostupnosti hlaviček jsou jasně oddělená od obsahu zprávy.

Textová body validní UTF-8 zobrazovat bez pretty-print, trimování a změn whitespace. Nepoužívat HTML control, WebView nebo shell. Na NUL, neplatné UTF-8 a jiné nereprezentovatelné bajty nesmí `SetWindowTextW` tiše ukončit nebo nahradit obsah. Pro tento okrajový případ použít výslovně označenou reverzibilní bajtovou textovou reprezentaci s `\xHH`, v níž se escapuje také samotný backslash, zatímco původní bajty zůstávají nedotčené ve vlastníkovi dat. Tato fallback reprezentace je technická prezentace binárních bajtů, ne parsování protokolu nebo redakce. Testy ověří bezeztrátovou zpětnou rekonstrukci. U běžného HTML/JS/JSON textu musí být vidět původní text přímo.

Nativní edit nesmí zavést vlastní 4MiB limit odvozený od IPC. Nastavit kapacitu podle skutečné ověřené textové délky a přečtením délky potvrdit úplné vložení. Kontrolovat převody do Win32 integerů, zachování řádků a správný obsah clipboardu. Při chybě vložení nezobrazit neoznačený prefix.

## 7. Přesný seznam změn a pořadí realizace

1. **Kontrakty a testové scénáře.** `client/src/platform/iface/transport.h`, `client/src/core/wizard/wizard_types.h`, `client/src/core/ipc/wizard_ipc.h/.cpp`. Zavést typy snapshotu, deskriptoru, bloků a opt-in. Dokončit striktní round-trip testy před zapojením UI.
2. **Transport a ProbeClient.** `client/src/platform/windows/transport_winhttp.cpp`, `client/src/core/wizard/probe_client.h/.cpp`. Upravit `prepare`, `post`, `health`, `info`, vstupy checkUrl/checkSecret a jednorázový getter diagnostiky. `checkAccess` explicitně capture vypíná. Parsery neměnit, jen jejich výsledky obalit řízením životnosti kandidáta.
3. **Operace a služba.** `client/src/core/wizard/wizard_service.h/.cpp`, `Operation`, `execute`, `startProbe`, `probeTask`, cleanup helpery. Přidat uložení, blokové čtení a release. Udržet mutex a epoch/generation invarianty. Ověřit dispatcher bez nutnosti produktové změny jeho obecného směrování.
4. **Tray model a čtení.** `client/src/core/wizard/wizard_model.h/.cpp`, `client/apps/tray/wizard_reader.h/.cpp`, `wizard_window.h/.cpp`. Přidat modelový deskriptor, clear/canShow API, dedikovaný reader a generace sestavy, napojit release na invalidaci a stávající lifecycle.
5. **Dialog a layout.** Nové `client/apps/tray/http_error_detail_window.h/.cpp`, nové sdílené `ui_tabs.h/.cpp`, úprava `editor_window.cpp`, `wizard_layout.h/.cpp`, `wizard_text.h`, `wizard_window.h/.cpp`. Znovu použít `ui_controls`, `ui_style`, `ui_resources`, nikoli kopírovat paralelní design. Nové zdroje zapsat do `client/apps/CMakeLists.txt` a všech relevantních targetů `client/tests/CMakeLists.txt`. Nový core implementační soubor, bude-li potřeba pro slice/typové helpery, registrovat v `client/src/CMakeLists.txt`.
6. **Testy, dokumentace, integrace.** Provést matici níže. Aktualizovat `client.md` a `protocol.md` podle konečného schématu. Po významné implementaci `graft build`. V této plánovací úloze se graf ani produktová dokumentace nemění.

Nepřidávat raw data do obecných status struktur, loggeru, `Error`, kryptografických parserů, runtime data requestů, receiptů nebo config serializace. Není důvod měnit `status_reader` ani pipe limity, kromě případného přesně doloženého odstranění dočasných textových kopií. Regresní testy musí potvrdit dosavadní pipe dostupnost a paměťové limity.

## 8. Testovací matice a pořadí ověření

### 8.1 Automatické testy

| Vrstva / soubor | Povinné scénáře a ověřovaný výsledek |
|---|---|
| `client/tests/unit/test_winhttp_accounting.cpp`, `BodyServer` | Skutečně přijaté status/headers/body, duplicitní headers, dlouhé headers nad 256 znaků, systémové request headers, přesné request body. Capture zapnuto/vypnuto. Prázdné response body, HTTP 4xx/5xx. Limit a poslední read blok, EOF vs neúplná odpověď. Dosavadní DNS/TLS/deadline/cancel/accounting testy beze změny výsledků. |
| `client/tests/unit/test_runtime_status.cpp`, ProbeClient scénáře | URL syntax error/HTML/JS, špatná korelace, secret health MAC failure, health success + info parse/decrypt/MAC failure. Přesně vybraná action a obsah. Health success + chyba přípravy info, DNS/TLS/timeout info nesmí vrátit health. Špatný content type/status a časová chyba. Úspěch nemá failed snapshot. Access nikdy capture nezapne. |
| Tentýž soubor, `WizardServiceFixture` | Diagnostika jen failed URL/secret s opt-in. Žádná na succeeded/cancelled/interrupted/finish/access. Změna epochy nebo generace před publikací potlačí snapshot. Nový pokus i lokálně odmítnutý nový start smaže starý detail. Release, cancel terminální operace, close/reset/resume, lease expiry, shutdown a restart odstraní data. Cizí session/ID/sequence/epoch nečte ani nemaže. |
| `client/tests/unit/test_wizard_ipc.cpp` | Staré schéma bez diagnostiky, nové opt-in, přesné round-trip všech částí, embedded NUL a libovolné bajty. Unknown/duplicate/missing member, null místo objektu, nesprávný stav/kind/action/status, neplatné UUID/uint64/Base64, chybná délka/offset/eof. Bezpečná chyba zůstává bez raw. |
| `client/tests/unit/test_wizard_model.cpp` | Viditelnost pouze aktuálního failed prvního/druhého kroku. Mismatch operationId/clientAttemptId/sequence/epoch a opožděný výsledek. Clear před lokální validací Next, změna polí, Back/reset/load/cancel/timeout/IPC/interruption/session expiry. Stará zpráva nesmaže nový detail. |
| `client/tests/unit/test_wizard_layout.cpp` | Pořadí detailu v obou krocích, přesná standardní mezera, hidden geometry, běžné/kompaktní šířky a 96/144/192 DPI. Žádný překryv, primární tlačítko čitelné v podporované minimální velikosti. |
| `client/tests/ui/tray_ui_tests.cpp` a nový účelový unit soubor pro formatter/reader podle potřeby | Přesný text a červená role, pořadí Tab/Shift+Tab, focus při skrytí, opakovaný klik aktivuje stejné okno. Owner, dva taby, readonly editory, selection/Ctrl+C, žádná patička, resize/DPI, close X, zavření s wizardem. HTML a JS inertní, Unicode/CRLF/NUL fallback a velký text beze ztráty. Opožděný chunk po close/new attempt nic neotevře. |
| `client/tests/unit/test_ipc_availability_win.cpp` a resource profil | Skutečný pipe round-trip více bloků, 4MiB hranice ostatních příkazů beze změny, maximální blok a obálka pod důkazem limitu. Release během čtení, restart služby, žádné blokování ostatního IPC a heartbeat. |

Velikostní sada obsahuje 0, 1, 196607, 196608, 196609 bajtů, více bloků, 4 MiB−1/4 MiB/4 MiB+1 a 64 MiB body. V resource testu ověřit i konfiguraci nad 64 MiB, aby nebyl default zaměněn za maximum. Obsah s mnoha `\u0000`, uvozovkami a backslashi, všechny byte hodnoty a nesprávné deklarované uint64 délky. Syntetické overflow testy nealokují extrémní velikosti. Celá sestava má byte-for-byte porovnání s fixture, nikoli pouze shodu zkráceného začátku.

Použít existující CMake presety z `client/CMakePresets.json`, ve vhodném MSVC vývojovém prostředí: `cmake --preset debug`, `cmake --build --preset debug`, `ctest --preset debug`, pracovní adresář `client`. Ověřit i release build a cílené ASAN testy změněného vlastnictví, pokud prostředí podporuje preset. UI, resource, contract a service-e2e targety mají vlastní CMake volby a část není registrována v běžném CTest. Při realizaci je explicitně zapnout a spustit, nespoléhat na samotné zelené CTest. `test_winhttp_accounting.cpp` musí být zahrnut do odpovídajícího targetu, ověřit registraci.

Před spuštěním reálných testů prověřit jejich argumenty a profily podle existujícího test runneru. Nikdy netisknout reálné raw secret/request fixture do selhávajícího assertion nebo logu. Testové porovnání používat délku, bezpečné jméno případu a případně hash syntetických dat.

### 8.2 Lokální Nginx/PHP až po automatických testech

Při plánování byla read-only potvrzena existence `D:\nginx`, `nginx.exe`, adresářů `conf`, `html`, `php8.5.9` a řídicích skriptů. Nebyl ověřen běžící server, port, konkrétní document root ani testovací phpvpn.php. Žádný server se nyní nespouštěl a nic se neupravovalo.

Při realizaci nejprve zjistit aktivní konfiguraci, přesnou cestu testovacího `phpvpn.php` a vlastnictví procesu. Použít izolovanou lokální testovací lokaci/port přes HTTP a syntetické secrets. Před změnou mimo repo ověřit absolutní cílovou cestu, zachovat obnovitelný originál a hash. Konfiguraci nebo fixture dočasně změnit tak, aby přidala marker textu/HTML/JS před nebo za odpověď, cíleně pro health a odděleně pro info. Přidaný obsah má být obyčejný testový text bez vedlejších účinků.

Ověřit ručně nebo UI runnerem oba kroky: standardní safe error a detail, konkrétní POST URL/action, všechny dostupné headers, nezměněný přidaný marker, správné body, inertnost textu a odstranění starého detailu po novém ověření. V secret testu musí nejprve uspět health a být pokažené jen info. Přidat chybný status, úspěšný kontrolní průchod a nedostupnou lokální adresu bez HTTP odpovědi.

V `finally` obnovit přesné původní soubory/konfiguraci, ověřit hash a pouze potřebný reload. Neukončovat cizí nesouvisející procesy a neprovádět produktový commit testovacích úprav. Pokud prostředí není dostupné, konkrétně vykázat NOT RUN a lokální akceptaci označit jako neověřenou. Nenahrazovat povinný lokální důkaz externím hostingem.

### 8.3 Externí best-effort jako poslední krok

Teprve po úspěšných automatických a lokálních testech ověřit `https://phpvpn.freehosting.dev/phpvpn.php`. Použít pouze testovací hodnoty, žádné lokální produkční secrets. Výsledek zaznamenat jako PASS, FAIL nebo NOT RUN a uvést konkrétní důvod, bez raw diagnostického obsahu.

Pokud server ukončí TLS před HTTP odpovědí, očekávané klientské chování je bezpečná TLS chyba bez `Error detail`. To není důkaz funkce pro hostingem vložený obsah. Externí nedostupnost sama dokončení neblokuje. V tomto plánu je externí kontrola NOT RUN, protože implementace a lokální akceptace ještě neproběhly.

## 9. Dokumentace, kompatibilita a rizika

V `client.md` aktualizovat zejména sekce Společný HTTP transport (od ř. 284), Configuration Wizard (434), Síťové kroky Wizardu (489), Rozšíření IPC v1 a příkazy (553–635), Životnost a limity (635), Chyby a bezpečnost (651) a Povinné testy (746). Zapsat přesný opt-in, deskriptor, dva nové příkazy, validaci, blokovou mez, životnost a výjimku lease pro místní endSession. Dosavadní pravidlo neblokování config dokumentů zachovat. Popsat pozice tlačítka, okno, případ nekompletní odpovědi a bezeztrátovou prezentaci netextových bajtů.

V `protocol.md` doplnit k HTTP transportu (sekce 4, od ř. 87) stručné vymezení vrstvy před parsováním. Serverový HTTP/wire protokol, kryptografické obálky, phpvpn.php a pořadí health/info zůstávají stejné. Rozšiřuje se pouze lokální Wizard IPC. Podrobné lokální schéma odkázat do client.md, nevytvářet druhou nesynchronní specifikaci.

Kompatibilita: `ipcVersion` zůstává 1, existující klient bez opt-in dostává přesně původní výsledky. Nový klient umí výsledek bez deskriptoru. Nový klient posílá opt-in pouze pro první dva kroky. Starší služba jej může striktně odmítnout před startem HTTP. V takovém případě zobrazit bezpečnou informaci o nekompatibilní službě/požadované aktualizaci, neprovádět automatické opakování probe bez opt-in. Nasazovat odpovídající tray a službu společně. Přidat testy obou smíšených kombinací, neměnit globální verzi IPC a neignorovat neznámé členy kvůli kompatibilitě.

Hlavní rizika a jejich řešení:

- **Únik dat:** diagnostika má vlastní vlastník, žádné obecné Error/detail/loggery, žádná perzistence. Lokální pipe ACL zůstávají stejné, čtení má stávající hranici důvěry IPC a úplnou session identitu.
- **Zastaralá data:** generace UI i služby, identity všech chunků, clear před validací a explicitní release včetně terminálních operací. Ověřit závody release/new probe/connect.
- **Paměť:** jedna neproměnná serverová kopie, Base64 po blocích, lokální dekódované kopie pouze pro otevřený detail, kontrolované chyby bez tichého ořezu. Resource test s výchozím maximálním body.
- **Nepravdivý „raw“ výpis:** hlavičky mají původ WinHTTP, status fallback označen, částečné přijetí označeno, body se neparsují. Unicode/binary prezentace nemění zachycené bajty.
- **Regrese transportu:** žádné nové reads za limitem, stejné callback/handle ownership, deadline a cancellation precedence, stávající accounting testy musí projít.
- **Regrese UI:** společné taby a červená role, kontroly DPI, kompaktní layout, focus, owner a rozpracované readery při zavření.

## 10. Podmínky dokončení realizace

Realizace je dokončena po splnění všech akceptačních kritérií intentu, úspěšných automatických a dostupných povinných lokálních testech, ověřeném úklidu dočasných Nginx/PHP změn a aktualizaci obou dokumentů. Chybějící povinné lokální ověření se musí vykázat jako omezení, nikoli jako splněná akceptace. Externí výsledek je samostatný best-effort záznam.

Před předáním ověřit diff vůči výchozímu stavu, absenci raw diagnostiky v logu/configu, větev tray_UX, registraci všech nových zdrojů a testů a aktuálnost Graft. Nepřepisovat tento plán. Materiální změna návrhu vytvoří novou úplnou `plan-v002.md` s odkazem na předchozí verzi.
