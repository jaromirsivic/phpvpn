# Detailní návrh serverové části PHP VPN – krátké obousměrné dávky

> Stav: návrh implementace serveru.
>
> Rozsah: jediný distribuovatelný soubor `phpvpn.php`, PHP 8.2+ a lokální souborový systém.
>
> Cíl: bezpečně předávat end-to-end šifrované IPv4 pakety mezi autorizovanými klienty, bez dlouho otevřených HTTP spojení.

## 1. Zásadní změna architektury

Každý klient opakuje krátký požadavek `POST ?action=sync` ve vlastním adaptivním intervalu. Interval je výhradně rozhodnutím virtuální karty; server neurčuje pevnou periodu. V jednom cyklu:

1. virtuální síťová karta klienta shromáždí odchozí IP pakety do lokální dávky;
2. klient otevře krátké HTTPS spojení a odešle tuto dávku;
3. server ji po validaci atomicky zařadí do front příslušných příjemců;
4. server ve stejné HTTP odpovědi vrátí dostupné dávky určené volajícímu klientovi;
5. spojení se uzavře;
6. klient odpověď rozbalí, deduplikuje podle `uuid` a předá pakety virtuální síťové kartě.

Požadavek může být prázdný: klient tak pouze vyzvedne pakety od ostatních klientů. Může naopak obsahovat velkou dávku; implementace ale musí vynutit bezpečné limity a v odpovědi přesně oznámit, které pakety přijala. Virtuální karta například ve Windows tento výsledek heuristicky použije pro velikost následující dávky: po plném úspěchu ji mírně zvětší, při částečném přijetí, zpoždění nebo přetížení ji zmenší. Jedna odpověď také může obsahovat mnoho paketů, avšak jen do nastaveného limitu. Pokud zůstane více dat, klient je vyzvedne v následujícím cyklu. To chrání PHP worker, paměť i limity hostingu a nevytváří dlouhé spojení.

## 2. Provozní cíle a hranice

Server je pouze relay šifrovaných dat. Nezná sdílené AES-256 klíče jednotlivých dvojic klientů, IP paket nedešifruje a nemění jej. Ověřuje pouze autorizaci, obálku zprávy, zdrojovou a cílovou VPN adresu, velikost a limity fronty.

Server musí fungovat na převážné většině běžných hostingů s PHP 8.2 nebo novějším. Vyžaduje pouze:

- PHP 8.2+ s běžně dostupnými funkcemi jádra (`hash_hmac`, `hash_equals`, `random_bytes`, JSON, práci se soubory a `flock`);
- jeden zapisovatelný lokální adresář vedle `phpvpn.php`;
- HTTPS nastavené hostingem nebo reverzní proxy;
- možnost běžného krátkého POST požadavku a odpovědi.

Implementace nesmí vyžadovat databázi, Redis, MQ broker, Composer, shellový přístup, cron, rozšíření OpenSSL, cURL, mbstring, sockets, FFI, APCu ani specifický typ webserveru. `random_bytes` a hashovací funkce jsou součástí podporovaného PHP. Server sám AES nepoužívá.

Návrh platí pro jednu instanci PHP VPN se sdíleným **lokálním** diskem. Více nezávislých serverů bez společné fronty není podporováno. Síťové souborové systémy, kde `flock` nemá spolehlivou semantiku, nejsou podporovaným provozním modelem.

## 3. Konfigurační blok v `phpvpn.php`

Na začátku souboru bude jediný administrátorem upravovaný blok:

```php
const SERVER_SECRET = '';
const MAX_REQUEST_BYTES = 8 * 1024 * 1024;
const MAX_RESPONSE_BYTES = 8 * 1024 * 1024;
const MAX_PACKET_LINE_BYTES = 1024 * 1024;
const MAX_REQUEST_PACKETS = 10000;
const MAX_ACCEPTED_REQUEST_BYTES = 4 * 1024 * 1024;
const MAX_ACCEPTED_REQUEST_PACKETS = 5000;
const MAX_BUFFERS = 128;
const QUEUE_TTL_SECONDS = 60;
const ACCESS_TOKENS = [
    '10.99.99.2' => 'replace-with-64-lowercase-hex-characters',
];
```

Interval `sync` není součástí konfigurace serveru. Virtuální karta jej určuje a přizpůsobuje sama podle latence, výsledků předchozích cyklů, zaplnění vlastní fronty, údajů `sync-result` a případného backoffu. Server pouze vynucuje velikostní a kapacitní limity požadavku; nesmí v požadavku čekat ani předpokládat konkrétní interval klienta. `MAX_REQUEST_BYTES` je nejvyšší přijatelná velikost celého těla. `MAX_ACCEPTED_REQUEST_BYTES` a `MAX_ACCEPTED_REQUEST_PACKETS` jsou nižší provozní stropy, které server skutečně zpracuje v jednom cyklu. `MAX_RESPONSE_BYTES` je strop pro vyzvednutí jedním cyklem. Všechny limity musí zůstat pod limity `post_max_size`, `upload_max_filesize`, `memory_limit` a limitem reverzní proxy daného hostingu.

Při startu každého požadavku `validateConfiguration()` ověří sílu `SERVER_SECRET`, rozsahy čísel, přesný rozsah klientských IP adres a 64hexový tvar tokenů. Neplatná konfigurace nesmí obsloužit `sync` a vrací `503`. Výchozí HTML stránka vždy zobrazí `Server is running`; při chybě secretu přidá požadované červené bezpečnostní varování.

## 4. Endpointy a HTTP pravidla

| Endpoint | Povolená metoda | Účel |
|---|---|---|
| výchozí stránka bez `action` | `GET` | provozní HTML stránka |
| `?action=health` | `GET`, `POST` | rychlý zdravotní stav a autoritativní čas serveru |
| `?action=sync` | `POST` | krátký obousměrný přenos jedné dávky |

`health` vrací `{"status":"OK","serverTime":"2026-08-08T14:20:00Z"}`. Čas je UTC v ISO 8601 se suffixem `Z`; klient jej používá pro autorizaci a koriguje jím vlastní čas. Endpoint nevrací konfiguraci, tokeny, obsah front ani detailní diagnostiku.

Pro `sync` je vyžadován `Content-Type: application/x-ndjson` nebo `application/jsonl`. Odpověď má `Content-Type: application/x-ndjson; charset=utf-8`. Všechny API odpovědi posílají `Cache-Control: no-store` a `X-Content-Type-Options: nosniff`. Neznámá akce vrací `404`, nepovolená metoda `405` a hlavičku `Allow`.

Každý `sync` musí skončit rychle. Server v něm nikdy nečeká na nový paket a nepoužívá polling, `sleep` ani streaming. Pokud pro klienta nic není, odpověď obsahuje pouze řídicí záznam `sync-result` a ukončí se ihned.

## 5. Autentizace odolná replay útoku

Minutový bearer token z původního návrhu není vhodný: lze jej opakovaně použít během celé minuty a neváže požadavek na obsah ani akci. Produkční režim použije HMAC podpis bez potřeby externího rozšíření.

Klient posílá tyto hlavičky:

```text
X-PHPVPN-Client: 10.99.99.4
X-PHPVPN-Timestamp: 2026-08-08T14:20:00Z
X-PHPVPN-Nonce: náhodná-hodnota-v-base64url
Authorization: PHPVPN-HMAC <hex-podpis>
```

Odvozený podpisový klíč je `hash_hmac('sha256', clientIp . "\n" . accessToken, SERVER_SECRET)`. Podpis je `hash_hmac('sha256', canonicalRequest, derivedSigningKey)`. `canonicalRequest` přesně obsahuje metodu, akci `sync`, klientskou IP, UTC timestamp, nonce a SHA-256 těla požadavku, každý údaj na vlastním řádku. Server:

1. ověří hlavičky a přípustnou časovou odchylku, například 60 sekund;
2. najde access token pouze pro uvedenou a validní klientskou IP;
3. vypočítá očekávaný HMAC a porovná jej přes `hash_equals`;
4. pod per-klientským lockem uloží hash nonce na disk na dvě minuty a odmítne již použitý nonce;
5. teprve poté zpracuje tělo.

Tím podpis současně vyžaduje `SERVER_SECRET` i access token konkrétního klienta. Do logů se nikdy nezapisuje access token, Authorization, nonce, secret ani data paketů.

## 6. Formát krátké dávky

Tělo požadavku je NDJSON: jeden kompaktní JSON objekt na řádek, zakončený `\n`. Prázdné tělo je platné. Řádky mohou být buď potvrzením dříve doručené dávky, nebo šifrovaným IP paketem:

```json
{"type":"ack","batchId":"20260808T142000.123456Z_random-id"}
{"type":"data","uuid":"550e8400-e29b-41d4-a716-446655440000","src":"10.99.99.2","dst":"10.99.99.4","data":"BASE64_ENCRYPTED_IP_PACKET"}
```

Server načítá tělo po částech a odmítne řádek delší než `MAX_PACKET_LINE_BYTES` ještě před `json_decode`. Řádek `ack` smí obsahovat pouze bezpečně validovaný identifikátor dávky, která patří tomuto klientovi. Řádek `data` vyžaduje objekt s přesně očekávanými poli, UUID ve formátu RFC 4122, `src` shodné s autentizovanou IP, `dst` v rozsahu `10.99.99.2` až `10.99.99.254` a neprázdná textová data. Neprovádí dešifrování ani hlubokou validaci obsahu šifrovaného paketu.

Server přijímá řádky `data` v pořadí jejich výskytu, pouze dokud nepřekročí `MAX_ACCEPTED_REQUEST_PACKETS` nebo `MAX_ACCEPTED_REQUEST_BYTES`. Platné řádky za tímto provozním limitem nejsou chybou protokolu: server je nezařadí do fronty a v odpovědi uvede jejich UUID v `deferredPacketUuids`. Klient je proto bezpečně ponechá v lokálním bufferu pro další cyklus. Chybný řádek je nadále chyba celého požadavku, protože klient nesmí pokračovat po neznámém nebo podvrženém vstupu.

Při chybném podpisu nebo podvrženém `src` server vrátí `401`. Syntaktická chyba je `400`, překročení limitu `413`, plná fronta `429` a nedostupný disk/fronta `503`. Chybová odpověď má malý obecný JSON tvar a nesmí sdělit citlivé informace ani existenci jiného klienta.

## 7. Diskové fronty a souběh

Mezi krátkými HTTP požadavky neexistuje sdílená PHP paměť. Jediným sdíleným stavem je fronta na disku:

```text
phpvpn/10_99_99_0/004/
    queue.lock
    20260808T142000.123456Z_random-id.data
```

Cesta se sestaví pouze z validovaného posledního oktetu funkcí `queueDirectoryForOctet()`. Adresář se vytváří `mkdir(..., 0700, true)` a po případném souběžném vytvoření se znovu ověří. Nikdy se neskládá z neověřeného HTTP vstupu.

V rámci jednoho `sync` server validuje řádky a přijímá je v pořadí, dokud nedosáhne provozního stropu. Přijaté pakety rozdělí podle cíle. Každou cílovou dávku zapíše zvlášť:

1. pod krátkým výhradním `flock` na `queue.lock` spustí lokální údržbu a ověří kapacitu;
2. vytvoří náhodně pojmenovaný soubor s příponou `.writing` výhradním režimem `x+b`;
3. zapíše celý batch ve smyčce a provede `fflush`;
4. atomicky jej v témže adresáři přejmenuje na `.data`;
5. uvolní lock.

Čtenář nikdy nevidí `.writing`. Lock se nedrží během HTTP odpovědi ani čtení velkých dat. Jedna neúspěšná cílová fronta znamená řízenou chybu celého požadavku; server nesmí tvrdit, že přijal dávku, kterou neuložil.

## 8. Vyzvednutí odpovědi v témže cyklu

Po úspěšném uložení odchozí dávky server vyzvedne data z fronty autentizovaného klienta. Pod jeho `queue.lock` lexikograficky vybere nejstarší `.data` soubory, dokud jejich součet nepřekročí `MAX_RESPONSE_BYTES`. Každý vybraný soubor atomicky přejmenuje na `.sending.<náhodné-id>` a lock uvolní.

Teprve potom server rozbalí obsah vybraných souborů do krátké HTTP odpovědi jako NDJSON. Každý vrácený paket obalí typem `delivery` a identifikátorem zdrojové dávky, například `{"type":"delivery","batchId":"…","packet":{…}}`. Soubor `.sending` se po odpovědi nemaže. Klient jej potvrdí řádkem `ack` v nejbližším dalším `sync`; až pak server pod lockem smaže odpovídající `.sending` soubor. Pokud se klient odpojí nebo přenos odpovědi selže, dávka zůstane pro opětovné odeslání. Doručení je proto **at-least-once**; klient musí deduplikovat podle `uuid`.

Odpověď může obsahovat i pakety vložené do fronty jinými klienty těsně před tímto cyklem. Neexistuje však garance, že obsahuje úplně vše bez omezení. Pokud limit odpovědi zbývající data odloží, následující cyklus je vyzvedne. Klient může po odpovědi obsahující limitovanou dávku provést další `sync` bez čekání na běžný interval, avšak s vlastním stropem rychlosti, aby nezahltil hosting.

Posledním řádkem každé úspěšné odpovědi je řídicí objekt, například:

```json
{"type":"sync-result","acceptedPacketUuids":["…"],"deferredPacketUuids":["…"],"acceptedPackets":120,"acceptedBytes":82000,"responseTruncated":true,"recommendedRequestBytes":65536,"recommendedRequestPackets":100}
```

`acceptedPacketUuids` určuje jediná data, která smí klient odstranit z lokální odchozí fronty. `deferredPacketUuids` zůstávají pro další cyklus. `recommendedRequestBytes` a `recommendedRequestPackets` jsou horní doporučení serveru pro další požadavek; klient je bere jako vstup do své heuristiky, nikoli jako důvod k rychlému opakování bez omezení. `responseTruncated` říká, že na serveru zůstala další data pro klienta. Server může doporučení snížit při zaplnění front, vysoké době zpracování nebo selhávajícím zápisu.

## 9. Životní cyklus klienta

Klientská virtuální karta drží odchozí frontu v paměti. Sama určuje, kdy spustí další `sync` požadavek, a z ní vezme omezenou dávku. Může jej poslat ihned při nových datech, zpomalit při přetížení nebo dočasně zrychlit, když odpověď označí další čekající data. Při přetížení musí karta omezit vlastní paměť a stanovit politiku zahazování; server nemůže bezpečně absorbovat neomezený provoz.

Klient po návratu odpovědi:

1. rozparsuje NDJSON;
2. deduplikuje `uuid` v krátkodobé lokální cache;
3. předá platné pakety virtuální síťové kartě a přidá jejich `batchId` do potvrzovací fronty;
4. odstraní jen pakety uvedené v `acceptedPacketUuids`; odložené pakety ponechá v pořadí pro další cyklus;
5. velikost příští dávky adaptuje podle doporučení serveru, částečného přijetí, latence a počtu předchozích úspěšných cyklů;
6. v příštím cyklu odešle `ack` pro zpracované doručené dávky;
7. při síťové chybě, `429` nebo `503` ponechá dávku pro retry s exponenciálním backoffem a jitterem;
8. při `401` obnoví čas z `health` a po opakovaném selhání požádá uživatele o kontrolu konfigurace.

Klient nesmí předpokládat exactly-once doručení ani trvalou dostupnost hostingu. Musí umět přijmout duplicitní paket a bezpečně obnovit komunikaci po libovolném krátkém výpadku.

## 10. Garbage collection a zotavení

Údržba se provádí pouze nad relevantní frontou a vždy pod jejím lockem:

| Typ souboru | Význam | Údržba |
|---|---|---|
| `.writing` | nedokončený zápis | po 60 sekundách smazat; při selhání označit `.error` |
| `.data` | publikovaná, nevyzvednutá dávka | po `QUEUE_TTL_SECONDS` odstranit jako expirovanou |
| `.sending.*` | dávka odeslaná do odpovědi, čekající na `ack` | po bezpečné rezervě vrátit atomicky na `.data` |
| nonce soubor | ochrana proti replay | po dvou minutách odstranit |

Stáří se určuje přes `filemtime`, ne podle názvu. Pád před publikací zanechá `.writing`, pád po publikaci zachová celé `.data` a pád v průběhu odpovědi zanechá `.sending`, který se znovu zařadí. Starý paket se může zahodit po TTL; jde o explicitní a omezenou vlastnost relay služby.

## 11. Ochrana zdrojů a nasazení

Každý požadavek zůstává krátký, ale velké dávky stále mohou narazit na limity hostingu. Instalace musí mít `post_max_size` a `memory_limit` s rezervou nad `MAX_REQUEST_BYTES`; PHP `max_execution_time` stačí pro krátký synchronní požadavek, doporučeně alespoň 30 sekund. Není třeba vypínat output buffering pro dlouhý stream, ale odpovědi se nesmí vědomě držet v aplikační paměti nad `MAX_RESPONSE_BYTES`.

Vedle diskové kvóty může hosting vyčerpat také kvótu inode, tedy maximální počet souborů a adresářů. Nedostatek inode se pro tento server považuje za stejnou nedostupnost fronty jako plný disk nebo nezapisovatelný adresář: `enqueueBatch()` nesmí publikovat částečná data, bezpečně vrátí `503 queue_unavailable` a klient ponechá svou odchozí dávku pro retry s backoffem. `MAX_BUFFERS`, lokální garbage collection a dávkové zápisy omezují růst počtu souborů; nasazovací zátěžový test musí ověřit i dostatečnou inode rezervu konkrétního hostingu.

Nasazení vyžaduje HTTPS, zákaz přímého HTTP přístupu do adresáře `phpvpn/`, práva adresářů 0700 a vlastnictví pouze účtem PHP procesu. Webserver musí mít dost workerů pro očekávané krátké požadavky, ale také pevný limit souběhu. Interval 250 ms znamená přibližně čtyři požadavky za sekundu na klienta; při desítkách klientů je nutné ověřit kapacitu konkrétního hostingu zátěžovým testem.

Server loguje pouze čas, akci, anonymizovanou/autorizovanou klientskou IP podle provozní politiky, kategorii chyby, velikost dávky a náhodné request ID. Nikdy neloguje klíče, podpisy, nonce ani obsah paketů.

## 12. Testovací strategie

Testy musí běžet na PHP 8.2, 8.3, 8.4 a 8.5. Nevyžadují externí službu ani nepovinné rozšíření.

1. **Kompatibilita a router:** výchozí HTML, `health`, `sync`, zakázané metody a hlavičky.
2. **Autorizace:** platný HMAC, pozměněná akce/tělo/timestamp, neplatná IP, replay nonce a konstantní porovnání.
3. **Dávky:** prázdný request, jeden paket, mnoho paketů, náhodné hranice chunků, neplatný NDJSON, velký řádek a spoofovaný zdroj.
4. **Obousměrný cyklus:** klient A odešle pro B; následující prázdný `sync` klienta B jej dostane v odpovědi.
5. **Kapacita:** velký request se částečně přijme a přesně oznámí UUID přijatých a odložených paketů; velká odpověď se rozdělí do více krátkých cyklů; zaplněná fronta vrací řízenou chybu bez poškození starších dat.
6. **Souběh:** paralelní zápisy do stejného cíle, dva souběžné `sync` téhož příjemce a ověření, že se batch úspěšně neodešle dvakrát.
7. **Zotavení:** pád po `.writing`, po `.data` a po claimu `.sending`; GC vrátí nebo odstraní správný soubor.
8. **Hostingová zátěž:** desítky klientů s různými adaptivními intervaly, měření latence, paměti, počtu workerů, lock contention a dopadu limitů hostingu.

## 13. Akceptační kritéria

Návrh je připraven k implementaci, když lze prokázat následující:

1. Žádný klient nedrží dlouhé `push` ani `pull` spojení; jeden cyklus vždy požadavek ukončí.
2. Prázdný `sync` bezpečně vyzvedne dostupné pakety a odpověď může obsahovat více dávek od různých odesílatelů.
3. Neautorizovaný nebo replayovaný požadavek nevloží ani nevyzvedne data.
4. Odesílatel nemůže použít jinou `src`, než svou autorizovanou VPN IP.
5. Žádný čtenář nikdy neuvidí částečně zapsaný batch a dva čtenáři úspěšně neodešlou stejný batch.
6. Paměť i disková fronta mají explicitní limity; data nad limitem nezpůsobí nekontrolovaný růst.
7. Po přerušení odpovědi je možná duplicita, ne tiché zničení nevyzvednutého batchu; klient ji vyřeší přes `uuid`.
8. Server lze provozovat na PHP 8.2+ bez databáze, Composeru, shellu, cronu a nepovinných PHP rozšíření.
9. Server v každé úspěšné odpovědi přesně určí přijaté a odložené pakety a klient podle toho adaptuje velikost další dávky.
