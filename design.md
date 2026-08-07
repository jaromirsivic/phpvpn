# Detailní návrh serverové části PHP VPN

> Stav: návrh implementace serveru.  
> Rozsah: jediný distribuovatelný soubor `phpvpn.php`, PHP 8.2+ a lokální souborový systém.  
> Cíl: bezpečný relay šifrovaných IPv4 paketů mezi autorizovanými klienty. Server obsah paketů nikdy nedešifruje ani nemění.

## 1. Výsledek a hranice systému

Server poskytuje tři logické endpointy na jediné URL `phpvpn.php`:

| Endpoint | Povolené metody | Účel |
|---|---:|---|
| výchozí stránka | `GET` bez `action` | Provozní HTML stránka s `Server is running` |
| `health` | `GET`, `POST` | Čas serveru a základní zdravotní stav |
| `push` | `POST` | Dlouhé vstupní spojení; přijímá řádkově oddělené JSON pakety |
| `pull` | `POST` | Dlouhé výstupní spojení; vrací řádkově oddělené JSON pakety pro ověřeného příjemce |

Přenosová rovina je pouze relay. End-to-end šifrování AES-256, komunikující páry a význam vnitřního IPv4 paketu jsou odpovědností klientů. Server ověřuje odesílatele, formát obálky, limity a cílovou VPN adresu; nevaliduje ani nečte vnitřní zašifrovaný paket.

Implementace musí zůstat v jediném souboru, ale uvnitř musí být rozdělena do malých funkcí bez skrytých globálních stavů. Každý HTTP požadavek je samostatný PHP proces/worker; proto se mezi požadavky nesmí spoléhat na sdílenou paměť. Sdílený stav je pouze adresář front na disku.

## 2. Přijatá rozhodnutí

### 2.1 URL a routování

Akce se určí výhradně z query parametru `action`, například `phpvpn.php?action=push`. Hodnoty nesmí být odvozovány z cesty, hlavičky ani těla.

1. Není-li `action` přítomen a jde o `GET`, vrátit HTML stavovou stránku.
2. `action=health` přijme `GET` nebo `POST`.
3. `action=push` a `action=pull` přijmou pouze `POST`.
4. Jiný endpoint vrátí `404` s malým JSON tělem.
5. Nepovolená metoda vrátí `405`, hlavičku `Allow` a JSON chybu.

Všechny API odpovědi mají `Content-Type: application/json; charset=utf-8`, `Cache-Control: no-store` a `X-Content-Type-Options: nosniff`. Dlouhé odpovědi `pull` použijí `application/x-ndjson; charset=utf-8` a `X-Accel-Buffering: no` pro běžné reverzní proxy.

### 2.2 Konfigurační blok

Na úplném začátku souboru bude jediný editovatelný blok:

```php
const SERVER_SECRET = '';
const BUFFER_TIMEOUT_MS = 250;
const CONNECTION_TIMEOUT_MS = 15000;
const BUFFER_SIZE_KB = 256;
const MAX_BUFFERS = 128;
const ACCESS_TOKENS = [
    '10.99.99.2' => '…64 hex znaků…',
];
```

Produkční výchozí `ACCESS_TOKENS` by neměly být deterministické tokeny odvozené z posledního oktetu, ale to je odpovědností administrátora.

Při startu každého požadavku `validateConfiguration()` zkontroluje: délku a entropii `SERVER_SECRET`, typy a rozsahy timeoutů, `BUFFER_SIZE_KB`, `MAX_BUFFERS`, přesný adresní rozsah a 64hexový formát tokenů. Konfigurace s chybou nesmí obsloužit `push` ani `pull` (HTTP 503). Výchozí HTML stránka současně zobrazí požadované červené varování, pokud je secret kratší než 16 znaků; doporučené minimum je ale 32 náhodných bajtů v hexu/base64.

## 3. Model adres a adresářů

Podporovaný klientský rozsah je přesně `10.99.99.2` až `10.99.99.254`. Síťová adresa `.0`, routerová/rezervovaná `.1` a broadcast `.255` se pro klientské tokeny ani cíle nepřijímají. Tento přísnější výklad nahrazuje nebezpečně široký rozsah z poznámek.

Funkce `parseVpnIp(string $ip): ?int` musí:

1. parsovat IPv4 přes `filter_var(..., FILTER_VALIDATE_IP, FILTER_FLAG_IPV4)`;
2. rozdělit oktety a ověřit prefix `10.99.99`;
3. vrátit poslední oktet jen v intervalu 2–254.

Kořen front je absolutní cesta `__DIR__ . DIRECTORY_SEPARATOR . 'phpvpn' . DIRECTORY_SEPARATOR . '10_99_99_0'`. Cesta pro poslední oktet `n` vzniká výhradně funkcí `queueDirectoryForOctet($n)` jako `…/NNN`, kde `NNN` je `str_pad((string)$n, 3, '0', STR_PAD_LEFT)`. Nikdy se nesmí skládat z neověřeného `src`, `dst`, názvu souboru ani vstupu HTTP.

Adresář se vytváří pomocí `mkdir($dir, 0700, true)`. Po neúspěchu je nutné znovu ověřit `is_dir($dir)`, protože jiný worker ho mohl založit souběžně. Teprve když adresář stále neexistuje nebo není zapisovatelný, požadavek končí řízenou chybou `503 queue_unavailable` (nikoli 404, protože zdroj nebyl nenalezen).

## 4. Autentizace a čas

### 4.1 Problém původního vzorce

Hodnota `sha256(SERVER_SECRET . ACCESS_TOKEN . minuta)` je použitelná jen jako velmi slabé časové heslo: je opakovaně použitelná po dobu minuty, neobsahuje metodu, endpoint ani tělo a prosté spojování řetězců není jednoznačné. Server zároveň nemusí vytvářet mapu všech tokenů při každém požadavku.

### 4.2 Doporučený produkční protokol

Každý klient má v konfiguraci svou VPN IP. Požadavek posílá hlavičky:

```text
Authorization: Bearer <base64url HMAC>
X-PHPVPN-Client: 10.99.99.4
X-PHPVPN-Timestamp: 2026-08-07T14:20:00Z
X-PHPVPN-Nonce: <16–32 náhodných bajtů, base64url>
```

Čas v `X-PHPVPN-Timestamp` je vždy přesná UTC hodnota vrácená endpointem `health`, beze změny časové zóny a bez lokální reinterpretace. Klient ji převezme z pole `serverTime` a použije ji jako `TIMESTAMP` v canonical requestu pro `push` i `pull`.

Podpis je `base64url(hash_hmac('sha256', canonicalRequest, clientAccessToken, true))`, kde `canonicalRequest` je přesně:

```text
v1\nMETHOD\nACTION\nCLIENT_IP\nTIMESTAMP\nNONCE\nSERVER_SECRET
```

Server parsuje timestamp výhradně jako ISO 8601 v UTC s povinným suffixem `Z` a přijme jej v toleranci ±60 sekund vůči vlastnímu UTC času. Žádná část autentizace nesmí používat lokální časovou zónu serveru ani klienta. Nonce se během platnosti ukládá do malého, atomicky vytvářeného souboru v adresáři klienta `nonces/`; kolize znamená `401 replay_detected`. Při dokončení požadavku ani při pádu se nonce nemaže, protože má zabránit opakování. Garbage collection maže jen nonce starší než 2 minuty. To řeší replay ve stejném časovém okně.

### 4.3 Kompatibilní přechodný režim

Pokud musí klienti nejprve implementovat zadaný minutový bearer token, oddělí se do `AUTH_MODE = 'legacy-minute'`. V něm server přijme hlavičku `Authorization`, pro zadaný klientský identifikátor vypočítá token pro aktuální a minulou **UTC minutu** a porovná oba přes `hash_equals`. Klient odvodí stejnou UTC minutu výhradně z `health.serverTime`. Identifikátor klienta musí být v `X-PHPVPN-Client`; nevytváří se tedy mapa 253 tokenů. Tento režim bude označen jako dočasný a při běžící konfiguraci se zapíše varování do `error_log`. V produkci musí být `AUTH_MODE = 'hmac-v1'`.

`health` nevrací tajemství, konfiguraci ani přesný diagnostický detail. Vrací právě jedinou autoritativní časovou hodnotu `serverTime` v UTC ve formátu ISO 8601 se suffixem `Z`, například `{"status":"OK","serverTime":"2026-08-08T14:20:00Z"}`. Pole `serverTimeUtc` ani čas v lokální časové zóně se neposílají. Tato stejná hodnota je zdrojem času pro bearer/HMAC autorizaci `push` i `pull`.

## 5. Formát streamu a validace

Oba datové směry používají NDJSON: jeden JSON objekt na jednom řádku, zakončený jediným `\n`. Server nikdy nečeká na neomezeně dlouhý řádek.

Platná zpráva má přesně požadovaná pole:

```json
{"type":"data","uuid":"550e8400-e29b-41d4-a716-446655440000","src":"10.99.99.2","dst":"10.99.99.4","data":"…base64…"}
```

Funkce `validatePacketLine($line, $authenticatedSource)` provede v tomto pořadí:

1. Odmítne řádek delší než `MAX_LINE_BYTES` (doporučeno 1 MiB) ještě před `json_decode`.
2. Odmítne prázdný řádek, neplatné UTF-8 a JSON jiný než objekt.
3. Vyžaduje `type === 'data'` a nepovolí neznámá povinná pole; rozšiřující pole lze zavést až s verzí protokolu.
4. Ověří UUID jako RFC 4122 text (jen super rychlý quick test), `src` a `dst` přes `parseVpnIp`.
5. Ověří `src` konstantním řetězcovým porovnáním proti autentizované IP.
6. `data` se nijak neověřují.
7. Pro frontu ponechá původní kompaktní JSON řádek; není nutné jej serializovat znovu.

Pro chybný formát, spoofovaný zdroj, překročení limitu nebo chybu autentizace se spojení okamžitě ukončí s `401` pro autentizační chybu, `400` pro syntaktickou chybu a `413` pro velikostní limit. Do odpovědi se neposílají surová data ani detail, který by útočníkovi odhaloval existenci klienta.

## 6. Endpoint `push`

### 6.1 Životní cyklus

1. Nastavit krátké časové limity pro inicializaci, vypnout výstupní buffering a načíst/ověřit konfiguraci.
2. Ověřit metodu, `Content-Type` (`application/x-ndjson` nebo `application/jsonl`) a autentizovat volajícího.
3. Otevřít `php://input` jako stream. Nepoužívat `file_get_contents('php://input')`, protože by celý dlouhý požadavek držel v paměti.
4. Inicializovat lokální mapu `destinationOctet => bufferString`, lokální součty velikostí a monotónní čas posledního flush.
5. Číst nejvýše `MAX_LINE_BYTES + 1` bajtů po částech; z vlastního akumulačního bufferu vyjímat pouze celé řádky.
6. Každý řádek validovat a připojit do bufferu cílového klienta včetně `\n`.
7. Pokud některý buffer přesáhne `BUFFER_SIZE_KB * 1024` nebo uplyne `BUFFER_TIMEOUT_MS`, synchronně flushnout všechny neprázdné buffery.
8. Pokud monotónní čas dosáhne `CONNECTION_TIMEOUT_MS`, flushnout zbytek, odpovědět 200 s metadaty a korektně skončit. Klient se ihned znovu připojí.
9. Pokud klient ukončí stream dříve, flushnout již kompletní a validované pakety; nekompletní poslední řádek zahodit a požadavek označit jako `400 incomplete_frame`.

Pro měření intervalů je nutné používat `hrtime(true)`, nikoli `time()` či systémový čas, který se může změnit NTP synchronizací.

### 6.2 Zápis fronty

`flushBuffers()` nesmí držet globální lock. Pro každý cíl volá `enqueueBatch($destinationOctet, $ndjson)`:

1. Zajistí adresář fronty a klientský `queue.lock` soubor.
2. Otevře lock přes `fopen('c+')`, získá výhradní `flock(LOCK_EX)` a drží jej pouze po dobu kontroly kapacity, vytváření a publikace jednoho batch souboru.
3. Spustí lokální garbage collection jen pro tento adresář.
4. Spočítá `.data` soubory. Je-li počet `>= MAX_BUFFERS`, batch zahodí jako řízený overflow a zvýší lokální čítač dropů. Nesmí vymazat čerstvá data jen proto, aby přijal další.
5. Vytvoří název `YYYYMMDDTHHMMSS.uuuuuuZ_<random-uuid>.writing`. UUID je z `random_bytes(16)` ve variantě RFC 4122; název nikdy neobsahuje klientský vstup.
6. Otevře soubor výhradně režimem `'x+b'`, zapíše celý batch ve smyčce kontrolující počet zapsaných bajtů, provede `fflush` a pokud platforma umožní, `fsync`.
7. Atomicky přejmenuje soubor na stejný název s `.data`. Přejmenování v rámci stejného adresáře je jediná operace publikující data čtenářům.
8. Uvolní lock v `finally`. Při chybě smaže pouze svůj známý `.writing` soubor.

Každý batch je samostatná frontová položka. Nelze garantovat globální přesné pořadí mezi souběžnými odesílateli; garantuje se pouze, že jeden publikovaný batch není čtený napůl a že každý individuální `push` zachová pořadí svých řádků v rámci batchů.

### 6.3 Odpověď

Po normálním ukončení vrátit 200 a minimální JSON například `{"status":"OK","accepted":42,"dropped":0}`. Podrobné důvody dropů patří pouze do serverového logu. Pokud zápis jedné fronty selže neobnovitelně, vrátit 503 a nezpracovaná data nepředstírat jako uložená.

## 7. Endpoint `pull`

### 7.1 Základní algoritmus

1. Ověřit metodu, konfiguraci a autentizaci příjemce.
2. Zajistit adresář jeho fronty; pokud dosud neexistuje, vytvořit jej stejně jako `push`.
3. Poslat hlavičky NDJSON, `Cache-Control: no-store`, `Connection: keep-alive`, případně deaktivaci proxy bufferování.
4. Do uplynutí `CONNECTION_TIMEOUT_MS` opakovat: získat nejstarší dostupný `.data` soubor, atomicky jej převzít, odeslat jeho obsah, flushnout HTTP výstup a potvrdit odstraněním souboru.
5. Když žádný soubor není, čekat nejvýše `BUFFER_TIMEOUT_MS` pomocí `usleep`, nejdříve však zkontrolovat `connection_aborted()` a časový limit.
6. Před ukončením provést lokální garbage collection; vrátit se bez přidávání neprotokolového JSON souhrnu do NDJSON streamu.

### 7.2 Bezpečné převzetí položky

Pouhé „otevřít, poslat, smazat“ dovolí dvěma souběžným `pull` požadavkům stejného klienta odeslat tutéž položku. Proto `claimNextBatch()` používá adresářový lock:

1. Pod `flock(LOCK_EX)` najde `.data` soubory, seřadí je lexikograficky a vybere první.
2. Atomicky jej přejmenuje na `.sending.<request-random-id>` a lock uvolní.
3. Pouze worker, kterému přejmenování uspělo, je vlastníkem dávky.
4. Obsah načte mimo lock, zkontroluje maximální velikost a zapíše do HTTP odpovědi.
5. Po úspěšném `flush()` odstraní `.sending…` soubor.

Je třeba výslovně přijmout omezení HTTP: server neumí spolehlivě zjistit, zda klient skutečně přijal už zapsaná data, pokud se síť přeruší. Doručení je proto **at-least-once**, ne exactly-once. Klient musí deduplikovat podle `uuid`; server nesmí deduplikovat globálně, protože by tím zavedl drahý a nekompletní stav.

Při odpojení během odesílání se `.sending…` soubor nepřesune do koše. Garbage collection jej po krátké rezervě (např. 20 s, nebo po uplynutí celého connection timeoutu) vrátí atomickým přejmenováním zpět na `.data`, aby jej další pull mohl doručit. Starší `.sending` soubory se tak nikdy nesmažou bez pokusu o znovuzařazení.

## 8. Garbage collection a zotavení po pádu

`garbageCollectQueue($dir)` se volá jen nad frontou autentizovaného klienta nebo cílem aktuálně zpracovávaného zápisu, nikdy nad všemi adresáři. Běží pod stejným adresářovým lockem a je idempotentní.

| Typ | Stav po vytvoření | Údržba |
|---|---|---|
| `.writing` | rozpracovaný zápis | starší než 60 s smazat; při chybě přejmenovat na `.error` |
| `.data` | publikovaný, nevyzvednutý batch | starší než 60 s smazat nebo přesunout na `.expired` podle režimu diagnostiky |
| `.sending.*` | výhradně převzatý pull workerem | starší než bezpečná rezerva vrátit na `.data` |
| `.error` / `.expired` | diagnostický artefakt | držet omezeně, např. 24 hodin; nezahrnovat do kapacity fronty |
| nonce soubory | ochrana proti replay | smazat po dvou minutách |

Čas stáří se počítá z `filemtime`, nikoli z názvu. Chybu mazání je nutné logovat bez citlivých dat; pokus o přejmenování na `.error` je pouze nejlepší úsilí. GC nesmí ukončit jinak zdravý stream jen proto, že se nepodařilo odstranit starý soubor; může ale zablokovat nový zápis při skutečně zaplněné frontě.

Pád procesu mezi zápisem a přejmenováním zanechá `.writing`, který GC odstraní. Pád po atomickém přejmenování zachová kompletní `.data`. Pád po claimu `pull` zanechá `.sending`, který se později znovu zařadí. To jsou základní scénáře obnovy bez centrální databáze.

## 9. Souběh, výkon a limity

### 9.1 Souběh

Souběžně mohou běžet desítky `push` i `pull` workerů. Izolace je podle adresáře cílové IP, takže provoz pro různé příjemce se neblokuje. Jediný lock na konkrétní frontu chrání kapacitu, pořadí claimu a atomické publikování; síťové čekání ani čtení celé dávky se nikdy nedělá pod lockem.

Lock soubor se nesmí mazat. Jeho životnost je bezpečná a `flock` se při ukončení procesu automaticky uvolní. Pro NFS nebo více serverů `flock` nemusí být dostatečný; tento návrh platí pro jeden server a jeden sdílený lokální disk.

### 9.2 Ochrana zdrojů

Konstanty, které nejsou uživatelsky editovatelné, budou součástí implementace:

| Limit | Doporučení | Důvod |
|---|---:|---|
| `MAX_LINE_BYTES` | 1 MiB | ochrana parseru a paměti |
| `MAX_TOTAL_BUFFER_BYTES` | 4 MiB na push | ochrana procesu při cílech rozptýlených do více front |
| `MAX_REQUEST_PACKETS` | 10 000 | limit práce na jedno spojení |
| `MAX_PULL_BATCH_BYTES` | 8 MiB | omezení načtení poškozeného souboru |
| `MAX_BUFFERS` | 128 na příjemce | horní hranice diskové fronty |

Při překročení se záznam odmítne dříve, než se uloží. Pro zahlcení použít 429 nebo 503 podle povahy: 429 pro limit konkrétního autentizovaného klienta, 503 pro nedostupný disk/frontu. Doporučené nasazení doplní limit počtu požadavků a maximálního počtu souběžných PHP workerů ve webserveru; vlastní PHP soubor to bezpečně nenahradí.

### 9.3 PHP a webserver

PHP runtime musí mít `max_execution_time` vyšší než connection timeout s rezervou, například 30 s; `memory_limit` nejméně 64 MiB; `output_buffering=Off`; `zlib.output_compression=Off`. PHP-FPM/Apache musí dovolit dostatek workerů pro dvojici spojení na klienta, ale s pevnou horní hranicí, aby se hostitel nevyčerpal. Vynutit HTTPS v reverzní proxy, vypnout přístup k adresáři `phpvpn/` z webu a nastavit vlastnictví adresářů pouze pro účet PHP procesu.

## 10. Chybový model a pozorovatelnost

Chyby API mají jednotný tvar:

```json
{"status":"error","code":"invalid_packet","message":"Request cannot be processed."}
```

Veřejná `message` je záměrně obecná. Interní log zapisuje čas, akci, autentizovanou IP (pokud známá), kategorii chyby, velikosti a náhodný request ID. Nikdy neloguje `SERVER_SECRET`, access token, Authorization hlavičku, nonce ani obsah `data`.

| Kategorie | HTTP | Příklad |
|---|---:|---|
| neplatná konfigurace | 503 | secret chybí, adresář nelze zapisovat |
| neautorizovaný požadavek | 401 | podpis, nonce nebo bearer token neplatný |
| chybný formát | 400 | neplatný NDJSON, UUID, pole |
| příliš velké tělo/řádek | 413 | paket překračuje limit |
| kapacita vyčerpána | 429 / 503 | plná konkrétní fronta / problém disku |
| neznámá akce | 404 | nepodporovaný endpoint |
| nepovolená metoda | 405 | GET na push |

`health` může navíc ověřit zapisovatelnost kořenového adresáře, ale nesmí vytvářet stovky klientských adresářů ani spouštět celou GC. Výsledek má být rychlý a bezpečný pro load balancer.

## 11. Struktura souboru `phpvpn.php`

Pořadí sekcí musí usnadnit bezpečnostní audit:

1. editovatelná konfigurace;
2. needitovatelné interní limity a názvy protokolu;
3. bootstrap: strict types, bezpečné hlavičky, chybový handler;
4. validace konfigurace a adres;
5. HTTP odpovědi, routování a logování;
6. autentizace včetně canonical requestu a nonce;
7. práce s cestami, locky a soubory;
8. garbage collection;
9. NDJSON parser a validace paketů;
10. implementace `handleHealth`, `handlePush`, `handlePull`;
11. jediný krátký dispatcher.

Žádná funkce nesmí přímo číst `$_GET`, `$_SERVER` nebo měnit hlavičky kromě routovací/response vrstvy. Datové funkce dostávají explicitní argumenty a vracejí strukturovaný výsledek/řízenou výjimku. Výjimka se zachytí jen na hranici endpointu, kde se bezpečně zaloguje a převede na odpověď.

## 12. Testovací strategie

### 12.1 Jednotkové testy

I když produkce má jeden soubor, testy musí existovat odděleně. Testovat bez skutečného webserveru zejména:

- hranice `parseVpnIp`: `.1`, `.2`, `.254`, `.255`, cizí síť, maskované a nekanonické zápisy;
- konfiguraci, nedostatečný secret a chybný token;
- canonicalizaci a HMAC pro platný čas, minulý/budoucí čas, pozměněnou akci a nonce replay;
- rozdělení streamu na libovolných hranicích chunků;
- max. délku řádku, neplatný JSON, neplatné base64, UUID a spoofované `src`;
- názvy a cesty fronty včetně prevence traversal;
- přechody `.writing → .data`, `.data → .sending → delete`;
- GC pro každý typ souboru a návrat opuštěného `.sending`.

### 12.2 Integrační testy

Spustit PHP vestavěný server nebo izolovaný PHP-FPM s dočasným `__DIR__/phpvpn` a ověřit:

1. HTML stránku, health přes GET i POST a zamítnuté metody;
2. push → pull pro jeden paket a zachování NDJSON;
3. více paketů pro různé cíle v jednom push;
4. souběžné push do stejného cíle bez poškozených souborů;
5. dva souběžné pull stejného cíle bez duplicity claimu;
6. ukončený pull v průběhu dávky a následné znovuzařazení;
7. plnou frontu, selhání zápisu a neexistující/adresář bez oprávnění;
8. klientský reconnect po 15 s;
9. zátěž s desítkami dvojic spojení, sledování paměti, lock contention a latence.

### 12.3 Bezpečnostní testy

Fuzzovat NDJSON parser a hlavičky, testovat replay stejné nonce, časovou odchylku, změnu endpointu pod stejným podpisem, path traversal v každém poli, soubory/symlinky v adresáři fronty a odpojení klienta během přenosu. Nasazovací kontrola ověří HTTPS, nepřístupnost fronty přes HTTP, oprávnění 0700/0600 a vypnuté PHP zobrazení chyb.

## 13. Akceptační kritéria

Server je připraven k implementaci, pokud lze prokázat následující:

1. Neautorizovaný klient nezíská ani nevloží paket; signature compare je konstantní a replay je v produkčním režimu odmítnut.
2. Odesílatel nemůže uvést jinou `src`, než svou autentizovanou VPN IP.
3. Žádný čtenář neuvidí soubor `.writing`; dvě paralelní čtení neodešlou stejný batch jako úspěšně převzatý.
4. Pád v libovolném bodě zápisu nebo čtení nezpůsobí částečně publikovaný batch; možná duplicita je pro klienta bezpečně řešitelná přes UUID.
5. Paměť jednoho `push` je omezená definovanými limity a fronta jednoho příjemce nepřeroste `MAX_BUFFERS`.
6. GC se dotýká jen relevantní fronty a po pádu obnoví opuštěný stav.
7. `push` i `pull` ukončí dlouhé spojení nejpozději po nastaveném timeoutu a klient může bezpečně reconnectovat.
8. Server se při chybě chová řízeně, nevyzrazuje tajemství a logy neobsahují citlivé přenosy.

## 14. Doporučené pořadí implementace

1. Konfigurace, bootstrap, router a `health`.
2. Validace IP/tokenů a nový HMAC autentizační modul, včetně nonce fronty.
3. Cesty, adresářové locky, atomické frontové operace a GC; pokrýt testy před přidáním endpointů.
4. Streaming parser a `push` se zátěžovými testy.
5. Claim/recovery logika a `pull` se souběžnými integračními testy.
6. Nasazovací konfigurace, metriky/logy, bezpečnostní a zátěžová validace.

Toto pořadí drží nejrizikovější vlastnosti — autentizaci, atomický souborový stav a souběh — před aplikačním streamováním. Nezavádí databázi ani broker, dokud je cílem jediný PHP server na lokálním disku; při požadavku na více instancí je změna fronty samostatný architektonický projekt.
