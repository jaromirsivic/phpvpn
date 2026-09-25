---
plan_version: 1
created_at: 2026-09-25-11-54-00
source: intent-v001.md
---

# Realizační plán Python serveru PHP-VPN

## Zadání a rozsah

Autoritativní zadání: [intent-v001.md](../../Intents/2026-09-25-11-38-07/intent-v001.md). Zdrojový záznam: [intent-chat-v001.md](../../Intents/2026-09-25-11-38-07/intent-chat-v001.md). Tento plán je úplná neměnná verze 1. Upřesnění po jejím dokončení patří do nové úplné verze.

Výsledkem realizace bude samostatně spustitelný `phpvpn.py` vedle zachovaného `phpvpn.php`, se stejným klientským protokolem, výchozím `disk` a volitelným `memory`. Oba režimy zahrnují fronty, statistiky, publikované maskované kontrakty a stav údržby. Na nasazení běží jediná serverová implementace. Přenos rozpracovaného diskového stavu mezi PHP a Pythonem ani migrace dat nejsou součástí zadání.

Tento dokument pouze plánuje. Implementace, změna intentu, commit, push ani nasazení nyní neprobíhají. Výchozí prohlédnutý stav je větev `main`, commit `2530d1726bcf00df68ad659e3f20342d06c1099e`, s rozpracovanými soubory intentu a markerem koordinátorky. Realizátor nejprve znovu ověří aktuální stav a pracuje přímo v určené větvi, bez worktree. Existující cizí změny zachová.

Společný termín této plánovací práce je 2026-09-25 14:03:38 Europe/Berlin, tedy 12:03:38 UTC. Termín sleduje agent, není automaticky vynucován. Pro případnou pozdější realizaci musí koordinátorka potvrdit stále platný termín nebo jeho prodloužení. Plánovací agent nevytváří další kontexty ani agenty.

## Ověřené projektové podklady

Před čtením zdrojů byly použity `graft map`, `graft ask ... --source` a `graft skeleton`. Následující rozsahy jsou orientační body pro realizátora ve výchozím commitu, nikoli nově navrhované soubory:

| Zdroj a rozsah | Význam pro realizaci |
|---|---|
| `phpvpn.php:34–104`, `1198–1242` | Konfigurační hodnoty a jejich validace. |
| `phpvpn.php:286–434`, `440–489`, `506–791` | Časy, base64url, identity, striktní wire parsery. |
| `phpvpn.php:1303–1364` | Oddělené klíče, kanonizace a časové okno. |
| `phpvpn.php:1389–1415`, `1431–1661`, `1672–1819` | Storage rozhraní, fronty, snapshot kontraktů, best-effort statistiky a sweep. |
| `phpvpn.php:1849–1867`, `1946–1984`, `2020–2063` | Odpovědi, chyby, hlavičky a limitované čtení těla. |
| `phpvpn.php:2069–2298` | Stavová stránka, health, standardní i rozšířené info. |
| `phpvpn.php:2300–2577`, `2583–2672` | Data, pořadí zpracování, částečné výsledky, MAC a dispatch. |
| `protocol.md:89–207`, `208–435`, `481–666`, `692–760` | Transport, serializace, kryptografie, fronty, limity a chyby. |
| `protocol.md:762–893` | Předepsané scénáře a kontrolní seznam shody. |
| `tests-integration/phpvpn_client.py:536–561`, `conftest.py:25–100` | Konfigurace existujícího nezávislého HTTP testovacího klienta. |
| `tests-integration/test_security.py:186–286` | Testy vázané na lokální diskový layout, potřebují rozlišení backendu. |
| `tests-integration/README.md`, `distribution.md` | Spouštění testů, známé mezery pokrytí, závazné balení. |

Při realizaci použít graf znovu, pokud se zdroje posunou. Pro úplný seznam závislostí testů na filesystemu použít `graft grep` pro `buffer_directory`, `stat`, `info.json`, `receiving`, `sending`, případně `deployedFile`. Ranked vyhledávání není úplný inventář.

### Výklad podkladů

- `info` zahrnuje full-auth rozšíření `maskedCommunicationContracts` a `includeMaskedCommunicationContracts`. Nesmí se implementovat jen staré auth-semi info.
- Starší formulace v kontrolních scénářích o vždy prázdném `info.json` mají výjimku pro potvrzenou publikaci kontraktů. Aktuální kapitoly 8.1a, 8.2 a 10.6 i implementace publikují JSON a čtou atomický snapshot. Běžný touch jej musí zachovat.
- Diskový layout popsaný v protokolu je detail stávajícího driveru. Intent dovoluje Pythonu vlastní formát. Tento plán přesto ponechává per-client adresáře, přípony dávek a marker, protože to zjednodušuje kontrolu a testy. Nezavádí tím požadavek na čtení PHP dat.
- Pokud realizace odhalí skutečný rozpor wire specifikace s referencí PHP, nejprve vytvoří minimální reprodukci a popíše dopad. Bez souhlasu nemění protokol ani PHP. Normativní wire pravidla mají přednost před náhodnou chybou implementace.

## Rozhodnutí o řešení

### Jeden soubor, HTTP vstup a závislosti

Veškerá serverová aplikační logika, vložená stavová stránka a HTTP vstup budou v `phpvpn.py`. Použít Python 3.13 jako ověřovaný runtime, shodně s integračním prostředím. Závislosti budou `uvicorn` pro HTTP/ASGI a `cryptography` pro AES-GCM. FastAPI se nepřidá, pro tři přesně definované akce by automatické parsování a frameworkové chyby přidávaly další chování, které by se muselo vypínat.

Malý ASGI adaptér přečte syrové bajty s limitem, zavolá společnou aplikační vrstvu a odešle přesně vytvořenou odpověď. Nepoužívat automatickou validaci JSON, OpenAPI, implicitní přesměrování cest ani frameworkové 422. Oddělit protokolové chyby od chyb transportního parseru před vstupem do aplikace.

Programový vstup pod `if __name__ == '__main__'` předá instanci aplikace Uvicornu, explicitně nastaví jediný worker a vypne reload. Stejný podporovaný způsob spuštění použít pro oba drivery. Nepodporovat procesový pool, více replik za load balancerem ani externí multiprocess launcher pro memory. Nevystavovat hotovou globální instanci pro běžné `uvicorn phpvpn:app --workers ...`. Testovací factory smí existovat, dokumentace ji nepředstaví jako produkční launcher. Programové spuštění a předání instance podporuje [Uvicorn deployment](https://uvicorn.dev/deployment/).

V horním konfiguračním bloku budou vedle PHP hodnot nové síťové položky `LISTEN_HOST = '127.0.0.1'`, `LISTEN_PORT = 8000` a `SERVER_PATH = '/phpvpn.php'`. Zachování výchozí HTTP cesty umožní stávajícím klientům používat stejnou URL, název skutečného Python souboru zůstává přesně `phpvpn.py`. Správce může cestu a bind změnit nahoře. Obsluhovat jen tuto cestu, ostatní vracet jako obecné 404. Žádné statické zpřístupnění adresáře zdrojů či storage.

HTTPS ukončuje reverzní proxy před lokálně naslouchajícím procesem. Dokumentovat zachování query a přesných bajtů těla, zákaz cache, limit requestu vyšší než aplikační a přenos celé fronty v odpovědi. Proxy hlavičky se nepoužijí k autorizaci ani ke směrování. Nastavení workerů předat explicitně, protože Uvicorn jinak může použít `WEB_CONCURRENCY`. Pro reprodukovatelný běh použít `asyncio` a `h11`, vypnout nepotřebné WebSockety a access log s neomezenou URL. Příslušné možnosti jsou popsány v [Uvicorn settings](https://uvicorn.dev/settings/).

Závislosti řešit výhradně přes `uv`. Přidat `requirements-server.in` se dvěma přímými závislostmi a `requirements-server.txt` vytvořený pomocí `uv pip compile`, s přesnými otestovanými verzemi a tranzitivními závislostmi. Verze zvolit a ověřit při realizaci, tento plán netvrdí, že nyní proběhl jejich install nebo test. Distribuce musí obsahovat tento lockovaný seznam a návod `uv venv`, `uv pip sync ...`, `uv run --no-project python phpvpn.py` v připraveném prostředí. Samotný server neinstaluje balíčky za běhu.

### Konfigurace a vrstvy

Před aplikační logikou zachovat jeden souvislý blok všech administrátorských nastavení. Převést názvy a výchozí hodnoty PHP bez změny významu: `SERVER_VERSION`, `SERVER_SECRET`, všech čtrnáct `ACCESS_KEYS` pro `.2` až `.15`, `STORAGE_DRIVER = 'disk'`, `BUFFER_DIRECTORY = './phpvpn_data'`, `STATUS_PAGE_ENABLED`, rozsah VPN, všechny `MAX_*`, časové a sweep konstanty včetně `INFO_TEMP_TTL_SECONDS` a `PROTECT_BUFFER_DIRECTORY`.

Python validuje typy bez zaměnění bool a int, limity a klíče. Prázdný secret normalizuje pouze při přesné shodě s `''`, nikoli podle obecné pravdivosti. Udrží zvlášť nakonfigurovaný a efektivní secret. Soubory se nevytvářejí při importu či pouhé validaci konfigurace. Relativní buffer se vyhodnocuje vůči umístění `phpvpn.py`, nikoli aktuálnímu pracovnímu adresáři. V memory se buffer, diskové guards a TTL dočasných diskových publikací nepoužijí, což bude vysvětleno u konstant.

Uvnitř souboru oddělit Config, Clock, wire validaci a serializaci, derivaci klíčů, autentizaci, Storage, DiskStorage, MemoryStorage, handlery a ASGI adaptér. Clock, zdroj náhodnosti a úzké filesystem operace umožní řízené testy hranic a selhání. Testy nesmějí kopírovat produkční kryptografické kanonizace jako vlastní referenci.

### Protokol a kryptografie

Zachovat všechny následující vlastnosti v obou drivery:

- GET/HEAD stavové stránky s vloženým HTML, přepínačem a warning panelem podle PHP. HEAD vždy bez těla, i při chybě. `health`, `info`, `data` pouze POST, neznámá akce 404, nesprávná metoda 405 a správné `Allow`. Rozlišení Content-Type, charset a pořadí chyb podle reference.
- Všechny aplikační odpovědi mají povinné cache a nosniff hlavičky. Chyby obsahují jen obecné `type` a `code`, 503 také odpovídající Retry-After. Nezveřejňovat interní cesty, tajemství ani traceback.
- Bajtové limity včetně LF, přesná UTF-8/ASCII pravidla, odmítnutí BOM, duplicitních a neznámých klíčů, nekanonického base64url, nesprávných UUID a časů. Rozšířené info detekuje i duplicitní escapované názvy, null, záměny pole/objektu a neboolean include.
- Data mají pevné pořadí členů a kompaktní zápis podle 9.3. Uchovávat původní ověřené řádky, neparsovat a znovu serializovat payload. Striktní parser data oddělit od obecnějšího parseru rozšířeného info. NDJSON vyžaduje LF i na konci, odmítá CRLF a prázdné řádky.
- HMAC data pokrývá přesné bajty po prvním LF. `auth-full` váže secret, přístupový klíč a src. Neznámý klient projde dummy-key derivací a konstantním porovnáním. Timestamp ±60000 ms je přípustný, ±60001 ms není. Žádný nonce či replay registr.
- AES-256-GCM použít z `cryptography.hazmat.primitives.ciphers.aead.AESGCM`, 32bajtový klíč, 12bajtové IV, 16bajtový tag. API vrací ciphertext s připojeným tagem, při sestavení wire polí je rozdělit. Health používá prázdný plaintext a předepsané AAD. Info má nový salt i IV. Ověřeno proti [dokumentaci AESGCM](https://cryptography.io/en/latest/hazmat/primitives/aead/#cryptography.hazmat.primitives.ciphers.aead.AESGCM). PHP-only požadavek na ruční AES se na Python nevztahuje.
- Relay nerozšifrovává payload a nezná párové komunikační klíče. `data-mac` chrání přesné předepsané pořadí dvojic ID/tag a hash skutečného řádku `data-result`.
- Info zahrnuje standardní i full-auth režim, přítomnost `[]` a `false` je odlišná od absence. Publikace kontraktů nahrazuje celý seznam, include filtruje cizí vztahy pouze na src žadatele a self má `[]`. Pouhé standardní info nový stav nepublikuje.
- Pretty info musí odpovídat PHP formátování včetně prázdných polí a UTF-8 bajtového limitu. Nestačí předpokládat, že obecné `json.dumps(indent=4)` bude ve všech detailech totožné. Překročení ubírá celé klienty od nejvyšší IP. Rovnost limitu projde. Když se nevejde základ, vrátí se server_misconfigured.

### Storage, souběh a životnost

Jedna sdílená instance storage vznikne na celý proces, nikdy nová pro každý request. Rozhraní pokrývá použitelnost, atomickou publikaci cílové skupiny, převzetí kandidátních dávek, logické využití, cílený a omezený sweep, lastSeen, potvrzenou publikaci kontraktů a konzistentní snapshot klienta. Odpověď dostane vlastnictví převzatých dávek a povinnost dokončit úklid i při odpojení klienta.

Společná logika nejprve validuje celé tělo a autentizaci, potom pracuje se storage. Cíle zpracuje numericky, každou skupinu celou nebo vůbec. Výsledné accepted/deferred seznamy zachovají pořadí původního requestu. Kapacita počítá původní serializované řádky včetně LF, bez indexu a statistik. Při nedostatku kapacity nejvýše jeden cílený sweep a jeden opakovaný pokus, poté deferred v 200, nikdy 429. Skutečně nepoužitelný backend znamená queue_unavailable a vyčerpaný zámek queue_busy. Dříve dokončená publikace jiného cíle se při pozdější chybě nevrací zpět.

TTL se počítá od úspěšné publikace. Expirované dávky se nevydávají ani nezapočítávají do kapacity. Poll převezme všechny nalezené živé dávky bez stránkování, žádné čekání na nová data. Kandidáti se zachytí při začátku drainu, souběžně později publikované dávky mohou zůstat na další poll. Každá dávka může být převzata nejvýše jednou. Po převzetí ji chyba spojení ani restart nevrací. Při selhání čtení během odesílání se nesmí emitovat zdánlivě platný závěrečný MAC.

DiskStorage bude používat per-client adresáře a `receiving → data → sending` přes atomické přejmenování ve stejném filesystemu. Jména mohou zachovat specifikovaný expiry/bytes/src/uuid tvar, uvnitř bude explicitně verzovaný Python index a původní řádky. Kompletní zápis, flush a uzavření před publikací, kontrola metadat a délky při čtení. Nedokončené dávky se nevydají. Již převzaté `.sending` se po restartu znovu nepublikují. Sweep je uklidí po expiraci. Žádný požadavek na odolnost proti výpadku napájení nad rámec reference, případný fsync zvolit a otestovat vědomě.

Pro fronty použít krátké per-client zámky s monotónním timeoutem `LOCK_TIMEOUT_MS`, ne globální zámek přes síťové I/O. Podporovaný server má jeden proces i v disk režimu, mezi jeho requesty postačí sdílené zámky. Více procesů nad jedním Python bufferem není podporovaný způsob nasazení. Atomické diskové přechody přesto zachovat. Blokující diskové operace provádět v omezeném thread executor prostřednictvím adaptéru, aby neblokovaly ASGI smyčku. Jeden worker nevylučuje pomocná vlákna ve stejném procesu. Cancel čekající coroutine nesmí předčasně uvolnit vlastnictví operace, která ve vlákně ještě běží.

Diskové lastSeen/kontrakty zachovají `info.json`: čerstvá běžná aktivita prázdný marker, potvrzená publikace do jedinečného temp souboru a atomický replace bez unlink starého cíle. Snapshot obsahu a mtime ze stejného otevřeného souboru. Běžný touch nemá retry ani čekající souborový zámek a nezničí publikovaný obsah. Při chybě explicitní publikace zůstává původní snapshot a vrací se 503. Otestovat zvlášť Windows chování otevřených souborů.

MemoryStorage bude držet immutable dávky a metadata v mapách podle validované adresy, s per-client synchronizací a atomickým odebráním převzaté dávky. Uchová i lastSeen, seznamy kontraktů, počítadla a čas/kurzor sweepu. Snapshot klienta vrátí konzistentní kopii, ne měnitelnou referenci. Běžný touch provede jeden best-effort pokus bez čekání a nemění datový výsledek. Časy lastSeen zaokrouhlit na sekundy jako PHP mtime, zachovat hranici 30/31 sekund i budoucí čas. Publikace kontraktů throttle obchází.

Memory nesmí inicializovat DiskStorage, vytvářet buffer či guards, zapisovat temp soubory, journal, checkpoint, statistiky ani spool requestů a odpovědí. Veškerý provozní stav po restartu zmizí. Běžná uv cache, instalované balíčky či načítání zdrojů nejsou provozní storage, při testu nulových zápisů připravit prostředí předem a vypnout bytecode zápisy. Neslibovat, že uživatelský proces dokáže zakázat OS swap.

Sweep má stejný časový a početní rozpočet v obou režimech. V memory mapách se průběžně odstraní expirované dávky a nepotřebné prázdné fronty, nikdy platný statistický snapshot jen kvůli TTL fronty. Timeouty a budget měřit monotónním časem, wire UTC čas oddělit. Síťové sendy nesmějí držet frontové zámky. Hlídání celkové spotřeby RAM je důležité i při 16 MiB limitu na klienta, protože limit není globální a převzaté pomalu odesílané dávky mohou být ještě v paměti.

## Pořadí realizace a výstupy

1. **Zachytit referenci a testovací profily.** Ověřit větev, stav a aktuální intent, přes graf obnovit dotčené části. Spustit dostupný PHP baseline v izolovaném testovacím nasazení, uložit výsledky včetně skipů a známých vad. Sepsat matici wire případů a očekávaných stavů včetně priority souběžných chyb. Produkční PHP a společný `server.json` nepřepisovat.
2. **Připravit spouštění a kostru jednoho souboru.** Konfigurační blok, validace, explicitní start jednoho workeru, ASGI adaptér, obecné chyby, vložená stavová stránka, pinned runtime závislosti. Ověřit GET/HEAD, směrování, raw těla, chyby a že pouhý import nemění filesystem. Neplatný driver vrací správnou konfigurační chybu, nepřepíná potichu na disk.
3. **Implementovat wire jádro a health.** Striktní parsery, limity, časy, base64url, kryptografie, derivace a deterministické known-answer testy. Ověřit health nezávislým existujícím klientem a referenčními vektory. Dále jednotkově připravit info/data kanonizace a serializaci.
4. **Implementovat storage rozhraní a oba drivery.** Nejprve společné kontraktové testy, potom MemoryStorage a DiskStorage. Otestovat at-most-once, TTL, kapacitu, statistiky a kontrakty, úklid a fault injection. Fronty ani jejich testy nesmějí vyžadovat nasazený klientský GUI program.
5. **Dokončit info a data nad společnou logikou.** Přesné pořadí validace/autentizace/publikace/touch/drain/MAC, full-auth info včetně soukromí, selhání odesílání a omezená údržba. Projít integrační protokolové testy nejprve disk, poté memory se stejnými klientskými vstupy.
6. **Doplnit procesové a srovnávací ověření.** Restart, přerušený zápis, claim před pádem, cancellation, timeout zámku, chybné nastavení, izolované secrets a běh mimo adresář skriptu. Ověřit skutečný počet procesů/workerů, i když prostředí nabízí WEB_CONCURRENCY. Provozní klient ověřit přes HTTP a HTTPS proxy bez úpravy jeho wire logiky.
7. **Dokumentovat a připravit distribuci.** Aktualizovat metodiku, přidat návod a lockovaný seznam závislostí do Python části. Sestavení celé distribuce provést podle `distribution.md`, nikoli vydávat pouhou kopii serveru za kompletní distribuci. PHP zůstává ve své složce a Node.js prázdný. Pokud plné sestavení či podepisování není v prostředí dostupné, předat konkrétní nehotový krok, ne tvrdit dokončenou distribuci.
8. **Závěrečná kontrola.** Projít akceptační tabulku, zkontrolovat diff, shodu PHP zdroje, report skipů a žádné nechtěné provozní soubory. Po větších změnách obnovit `graft build`. Předat změny a výsledky bez automatického commitu či nasazení mimo testovací prostředí.

## Dotčené soubory

| Soubor nebo oblast | Plánovaný zásah |
|---|---|
| `phpvpn.py` | Nový samostatný server, oba drivery a konfigurační blok. |
| `requirements-server.in`, `requirements-server.txt` | Nové deklarované a lockované runtime závislosti spravované uv. |
| `server-python.md` | Nový návod spuštění, konfigurace, proxy, disk/memory, restart a single-worker omezení. |
| `tests-python/` | Nové jednotkové, storage kontraktové a fault-injection testy skutečné Python implementace. |
| `tools/test-python-server.py` | Nový orchestrátor izolovaných fixture kopií, portů, obou driverů, restartů a úklidu vlastních procesů. |
| `tests-integration/phpvpn_client.py`, `conftest.py`, `pytest.ini` | Zpětně kompatibilní metadata fixture pro implementaci/driver a značky diskových testů. Žádná změna wire klienta kvůli serveru. |
| `tests-integration/test_security.py` a další soubory nalezené úplným průchodem přes graf | Oddělit filesystem assertions od společných HTTP assertions. Rozšířit chybějící scénáře. |
| `tests-integration/README.md` | Popsat PHP baseline, Python profily, přesné příkazy a zbylé mezery. |
| `distribution.md` | Python už není prázdná připravená složka. Přidat server, potřebné závislosti a návod, ověření shody zdrojů. |
| `dist/server/Python/` | Při sestavení kopie `phpvpn.py`, `requirements-server.txt` a provozního návodu. Žádná cache, testy ani fronty. |
| `graft/` | Regenerace po významných změnách kódu. |

`phpvpn.php`, intent a jeho chatový záznam se nemění. `assets/index.html` zůstává zdrojovým podkladem, Python distribuce na něm nesmí být závislá. Klientské produkční C++ zdroje změnu protokolu nepotřebují. `client/tools/make-dist.ps1` nadále slouží Windows klientské části s explicitním OutputDirectory, serverové balení upravuje celkový postup distribuce.

## Validace

### Matice a izolace

Testovat tři samostatné fixture: PHP/disk, Python/disk a Python/memory. Každá má vlastní port, testovací konfiguraci a diskovou cestu mimo webroot. Testovací launcher vytvoří kopii serveru s upraveným horním blokem, ověří právě jedno nahrazení každé určené položky a nikdy nepřepisuje produkční zdroj. Fixture konfigurace předá `--server-config` a `--large-body-url` explicitně. Python může pro large-body používat stejný přímý endpoint, pokud jeho transport neblokuje před aplikačním limitem.

Dosavadní testovací `Configuration` předpokládá `bufferDirectory`. Rozšířit jej o nepovinná metadata driver/implementace, se zachováním původních defaultů pro PHP. Pro memory nepoužívat existenci adresáře jako důkaz funkčnosti. Jen filesystem-specifické testy lze označit jako nepoužitelné, odpovídající behaviorální testy front a statistik musí projít. Skip se nikdy nevykáže jako pass.

### Povinné skupiny testů

| Skupina | Co musí být prokázáno |
|---|---|
| Wire a kryptografie | Health tag, info decrypt, auth-semi/full, doménové oddělení, původní data bajty, data-mac, defaultní klíče všech 14 adres, prázdný/`0`/nulový/Unicode secret. |
| Formát a limity | Duplicitní i escapované klíče, NaN/Infinity, hluboký vstup, chybné typy, BOM, CRLF, missing LF, pořadí data, nekanonické b64 bity, ±1 bajt kolem všech limitů a přesné časové hrany s fake clock. |
| Směrování a chyby | GET/HEAD enabled/disabled, správná precedence 404/405, Content-Type, unknown action, všechny obecné chyby a hlavičky, žádné frameworkové 422, tajemství ani filesystem úniky. |
| Fronty obou driverů | Celá cílová skupina nebo nic, více dst a částečné výsledky, acceptedBytes včetně LF, přesná kapacita, jeden sweep/retry, drain i při plném dst, bez stránkování, expiry rovno now, žádná deduplikace mezi requesty. |
| Souběh | Paralelní send/poll téhož i různých klientů, žádné dvojí převzetí, nepřekročení kapacity závodem, publikace během drainu, timeout zámku a žádný zámek držený pomalým sendem. |
| Statistiky a kontrakty | 30/31 s a budoucí čas, best-effort selhání nemění data, include-only touch, [] nahrazení, celé staré/nové snapshoty při souběhu, A/B/C/D soukromí a pretty přesný limit/trimming. |
| Diskové chyby a restart | Selhání mkdir/write/short-write/flush/rename/read, starý info snapshot zachován, nedokončená dávka neviditelná, živá publikovaná dávka přežije restart, claimed dávka se neobnoví, TTL temp souborů a Windows rename failure. |
| Memory a restart | Fronty, lastSeen, kontrakty i sweep stav sdílené všemi requesty, po restartu prázdné, ani health/info/data/sweep nevytvoří buffer či temp soubor. Instrumentovat storage filesystem vstupy, doplnit kontrolu izolovaného adresáře. |
| Odpojení a resource lifetime | Disconnect před auth nemění stav, po publikaci nevrací potvrzená data, po claim neobnoví dávku. Úklid při cancellation a nedokončeném sendu, uvolnění paměti po drain/expiry, bounded sweep neblokuje ostatní klienty. |
| Transport a reálný klient | Přímé HTTP, HTTPS přes proxy, forged forwarded hlavičky, velké requesty a celá fronta odpovědi. Existující klient provede health, info, send/poll a restart/recovery s oběma režimy. |

Existující integrační sadu spouštět přes uv, například pro každý připravený profil:

```powershell
uv run --python 3.13 --with pytest --with cryptography --with requests -- pytest tests-integration -v --server-config <fixture.json> --large-body-url <fixture-url>
```

Nové Python testy spouštět ve stejném připraveném runtime se serverovými závislostmi a pytest. Konkrétní příkaz doplní README po vytvoření fixture runneru. Plný finální běh zahrnuje slow expiry testy. Výkonnost měřit na stejném stroji a stejném zatížení PHP/disk, Python/disk a Python/memory, zaznamenat latenci i paměť. Intent neurčuje číselný speedup, měření nesmí být nahrazeno slibem, že memory je vždy rychlejší.

Diferenciální porovnání neporovnává náhodné IV, salt a aktuální timestamp doslova. Ověří kryptografii každé odpovědi nezávisle a porovná status, hlavičky, tvar, hodnoty a efekty front. Přesné serializované bajty a kryptografické vektory ověřit deterministickými lokálními testy. PHP chybu zjištěnou baseline během portu neskrývat změkčením společných testů.

## Rizika a opatření

1. **Odlišné JSON chování Pythonu.** Striktní bajtový parser data a zvláštní validace autentizace zabrání tichému přijetí obecného JSON, který protokol zakazuje. Pretty serializace má samostatné přesné testy.
2. **Ztráta stavu při více workerech.** Podporovaný launcher běží explicitně jednou, bez reloadu a import-string multiprocess konfigurace. Ověřit skutečný process tree a dokumentovat zákaz horizontálního škálování memory. Aplikace neslibuje detekci libovolného externího launcheru či repliky na jiném stroji.
3. **Blokování ASGI a závody cancellation.** Oddělit diskovou práci do omezených vláken, vlastnictví dávek sledovat až do skutečného dokončení operace a nezamykat přes síťové čekání.
4. **Vyšší RAM než součet živých front.** Neprovádět zbytečné kopie velkých řádků a celé odpovědi, posílat konečnou sadu dávkově. Měřit souběžné pomalé příjemce a průběžné doplňování front. Případné nové admission limity nesmí měnit protokolové kapacitní výsledky bez zdůvodnění a testů.
5. **Chování filesystemu na Windows.** Nepředpokládat úspěch rename při otevřeném markeru, nepoužívat unlink jako náhradu atomického replace. Testy vyžadují celý starý nebo nový snapshot a správnou chybu, nikoli vždy úspěšný souběžný zápis.
6. **Neúplné dosavadní testy.** README přiznává mezery kolem 503, pádů, přesných časů a HTTPS. Doplní je nové lokální fixture a fault injection. Samotné zelené HTTP testy nestačí.
7. **Záměna balení za hotovou distribuci.** Ověřit hash zkopírovaných serverů, čisté prostředí pro Python dependencies a respektovat celé `distribution.md`, včetně Windows kontrol při plném sestavení.

## Podmínky dokončení

| Kritérium intentu | Důkaz |
|---|---|
| 1. Zachované PHP, samostatné Python spuštění | PHP diff beze změny, PHP baseline a start z distribučního Python adresáře. |
| 2. Stejný klient pro oba režimy | Společná HTTP sada a reálný klient pro disk i memory, nezměněný wire protokol. |
| 3. Výchozí disk a přepínač nahoře | Kontrola config bloku a dva izolované starty lišící se STORAGE_DRIVER. |
| 4. Memory bez provozního disku, správný souběh | Instrumentace filesystemu, souběžné testy, kontrola jednoho procesu a restart se ztrátou veškerého memory stavu. |
| 5. Shodné názvy konfigurace | Inventář konstant porovnaný s PHP, vysvětlené disk-only položky a síťové novinky. |
| 6. Distribuce a dokumentace | Python soubor, lock závislostí a návod ve správné složce, shoda se zdroji, úspěšný čistý start. |
| 7. Prokázaná kompatibilita | Report všech skupin výše, výsledky PHP/Python disk/Python memory, jasně oddělené neprovedené testy a známé vady. |

Za dokončenou implementaci nelze označit práci s nevyřešeným selháním povinného testu. Konečný report uvede skutečně spuštěné příkazy, runtime a dependency verze, platformu a nepokryté podmínky. Test migrace PHP diskových dat do Pythonu se nevyžaduje.

Po dokončení tohoto plánu koordinátorka předloží jeho odkaz a vyžádá výslovnou volbu realizace. Vlastnictví intentu i markeru zůstává koordinátorce. Bez této volby plánovací agent implementaci nezahajuje.
