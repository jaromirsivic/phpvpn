# Implementační plán: akce `stats` bez OpenSSL

> Zdroj zadání: [stats.md](stats.md). Cílový soubor: [phpvpn.php](phpvpn.php).
> Tento dokument je závazný rozpis práce, nikoli architektonická úvaha.

---

## 1. Rozsah

Přidat do serveru tři nové věci:

1. Zápis statistik `QUEUE_ROOT/stats.json` při úspěšném `sync`.
2. Čistě-PHP kryptografickou komponentu `StatsCrypto` s HKDF-SHA-256 a AES-256-GCM.
3. Akci `POST ?action=stats`, která vrací šifrovaný obsah `stats.json`.

Bez OpenSSL, bez Composeru, bez jakéhokoli nepovinného rozšíření PHP. Používají se
jen `hash`, `json`, `pcre`, `random`, `date` a `standard`, tedy přesně ta množina,
kterou už soubor používá dnes.

---

## 2. Rozhodnutí, která zadání nechává otevřená

| Otázka | Rozhodnutí | Důvod |
|---|---|---|
| Kudy se přenáší autorizace `stats` | Jeden NDJSON řádek v těle, `Content-Type: application/x-ndjson` | Konzistentní se záznamem `auth` u `sync`. Do URL to nesmí, kapitola 1 v [PROTOCOL.md](PROTOCOL.md) to zakazuje kvůli logům hostingu a proxy. |
| Vypínač akce | Žádný, `stats` je vždy dostupná | Doslovné znění zadání. Bez platného podpisu vrací `401` a nikdy nic neprozradí. |
| Formát `stats.json` | Plochá mapa `{"10.99.99.2":"2026-08-08T14:20:00Z"}` | Zadání: „Záznam mapuje VPN IP adresu na UTC čas posledního úspěšného `sync`.“ |
| Kdy se zápis provádí | Po odeslání odpovědi, jen pokud `sync` skončil stavem `200` | Hodnota má význam „poslední úspěšný `sync`“. Zápis za `fastcgi_finish_request()` nezdržuje klienta. |
| Salt pro HKDF | `hash('sha256', canonical, true)`, tedy 32 syrových bajtů | Zadání říká „SHA-256 kanonického autorizačního requestu“. Syrová podoba je v HKDF obvyklá a jednoznačná. |
| AAD | Bajty kanonického requestu, tedy `POST\nstats\n<timestamp>\n<nonce>` | Zadání: odpověď nesmí jít připojit k jinému requestu. |
| 32bitové PHP | `stats` vrátí `503 server_misconfigured` | AES i GHASH počítají s 32bitovými slovy v 64bitových intech. `sync` zůstává na 32 bitech funkční, mění se jen nová akce. |

---

## 3. Drátový protokol akce `stats`

### 3.1 Požadavek

```http
POST /phpvpn.php?action=stats HTTP/1.1
Content-Type: application/x-ndjson

{"type":"stats-auth","timestamp":"2026-08-08T14:20:00Z","nonce":"o5TqZ0nS1w7bK3xR9dLp","signature":"9f2c…"}
```

Tělo obsahuje přesně jeden řádek zakončený `\n`. Záznam má přesně čtyři pole.

| Pole | Pravidlo |
|---|---|
| `type` | `"stats-auth"` |
| `timestamp` | `YYYY-MM-DDTHH:MM:SSZ`, odchylka nejvýše `CLOCK_SKEW_SECONDS` |
| `nonce` | 16 až 64 znaků `A-Z a-z 0-9 _ -`, shodné s `Wire::RE_NONCE` |
| `signature` | 64 malých hexadecimálních znaků |

Klientská VPN adresa ani access token se nepoužívají. Volající prokazuje znalost
`SERVER_SECRET`.

### 3.2 Kanonický request a podpis

```text
canonical = "POST" + "\n" + "stats" + "\n" + timestamp + "\n" + nonce
signature = hash_hmac('sha256', canonical, SERVER_SECRET)      // hex
```

Řetězec nekončí novým řádkem. Tělo se do podpisu nepromítá, proto smí obsahovat
jen ten jediný řádek. Cokoli navíc je `400`, ale ohlásí se až po ověření podpisu,
stejně jako u `sync`.

### 3.3 Odvození klíče

```text
salt = sha256(canonical)                       // 32 syrových bajtů
info = "phpvpn/stats-response/v1"
key  = HKDF-SHA-256(ikm = SERVER_SECRET, salt, info, L = 32)
```

HKDF se implementuje ručně nad `hash_hmac`, tedy extract a expand podle RFC 5869.
Protože `L` se rovná délce hashe, stačí jediný blok `T(1)`.

Klíč se nikdy nerovná `SERVER_SECRET` a nevzniká zřetězením hodnot.

### 3.4 Odpověď

```json
{"alg":"AES-256-GCM","iv":"base64","ciphertext":"base64","tag":"base64"}
```

`Content-Type: application/json; charset=utf-8`, HTTP `200`. IV má 12 bajtů z
`random_bytes()` a je nový pro každou odpověď. Tag má 16 bajtů. AAD je kanonický
request. Otevřený text jsou syrové bajty `stats.json`, nebo `{}`, pokud soubor
neexistuje či ho nelze přečíst.

### 3.5 Chyby

| HTTP | `error` | Kdy |
|---|---|---|
| `400` | `bad_request` | Nečitelný JSON, jiný počet polí, další řádek za autorizačním |
| `401` | `unauthorized` | Chybný podpis, chybný formát `timestamp`, `nonce` nebo `signature`, odchylka času, opakovaný nonce |
| `405` | `method_not_allowed` | Jiná metoda než `POST`, hlavička `Allow: POST` |
| `413` | `payload_too_large` | Tělo nad `MAX_REQUEST_BYTES`, řádek nad `MAX_AUTH_LINE_BYTES` |
| `415` | `unsupported_media_type` | Jiný `Content-Type` než `application/x-ndjson` nebo `application/jsonl` |
| `503` | `server_misconfigured` | Krátký `SERVER_SECRET`, nebo `PHP_INT_SIZE < 8` |

Chybová odpověď nikdy neobsahuje statistiky ani jejich část.

---

## 4. Statistiky při `sync`

### 4.1 Obsah souboru

`QUEUE_ROOT/stats.json`, kompaktní JSON objekt, klíč je VPN adresa, hodnota UTC
čas ve tvaru z `Wire::isoTime()`.

```json
{"10.99.99.2":"2026-08-08T14:20:00Z","10.99.99.4":"2026-08-08T14:19:31Z"}
```

### 4.2 Pravidla zápisu

- Zapisuje se jen po `sync`, který skončil stavem `200`.
- Pro danou adresu se zapisuje jen tehdy, když záznam chybí nebo je starší než
  `STATS_MIN_UPDATE_SECONDS`, výchozí 30.
- Staré záznamy se nikdy nemažou. Zahazují se pouze položky, jejichž klíč není
  platná klientská adresa nebo jejichž hodnota není platný timestamp, protože ty
  do souboru nepatří a jen by ho nechávaly růst.
- Soubor se nezamyká. Zápis jde do `stats.json.<8 hex>.writing` a pak `rename()`
  přes cíl, takže čtenář nikdy nevidí rozepsaný soubor a poslední zápis vyhrává.
- Čtení je omezené na 64 KiB. Větší nebo nerozparsovatelný soubor se bere jako
  prázdný a přepíše se.
- Celý blok je v `try { } catch (Throwable) { }`. Selhání se zaloguje kategorií
  `stats_update_failed` bez jakékoli hodnoty a `sync` zůstane úspěšný.
- `health` ani `stats` soubor nemění.

### 4.3 Kde se to volá

`SyncHandler::respond()` na konci nastaví `Ctx::$syncedClient`. `Router::finish()`
po `fastcgi_finish_request()` a před `Sweeper::maybeRun()` zavolá
`StatsFile::record(Ctx::$syncedClient)`. Klient tedy zápisem není zdržen a pád
zápisu nemůže ovlivnit už odeslanou odpověď.

---

## 5. Změny v `phpvpn.php`

Přírůstek zhruba 320 řádků. Nic ze stávajícího chování se nemění.

### 5.1 Hlavičkový komentář a konfigurace

- Do seznamu endpointů v úvodním komentáři přibude `POST /phpvpn.php?action=stats`.
- Do bloku `ADVANCED TUNING` přibude:

```php
/**
 * Minimum interval between two statistics updates for one client in seconds.
 * A shorter interval keeps stats.json more accurate at the cost of more writes.
 */
const STATS_MIN_UPDATE_SECONDS  = 30;
```

### 5.2 `Ctx`

Nová statická vlastnost `public static string $syncedClient = '';`, kterou nastaví
až úspěšný `sync`.

### 5.3 Nová sekce „Software AES-256-GCM“ (mezi sekcemi Statistics a Sync)

```php
final class StatsCrypto
{
    public const IV_BYTES = 12, TAG_BYTES = 16, KEY_BYTES = 32;
    public const INFO = 'phpvpn/stats-response/v1';

    public static function supported(): bool;                    // PHP_INT_SIZE >= 8
    public static function deriveKey(string $secret, string $canonical): string;
    public static function hkdfSha256(string $ikm, string $salt, string $info, int $length): string;
    /** @return array{iv:string,ciphertext:string,tag:string} raw bytes */
    public static function encrypt(string $plaintext, string $key, string $aad): array;
    public static function seal(string $plaintext, string $key, string $aad, string $iv): array;
    public static function selfCheck(): bool;                    // vektor pro preflight

    private static function tables(): array;                     // s-box a te0..te3, jednou za request
    private static function expandKey(string $key): array;       // 60 slov, Nr = 14
    private static function encryptBlock(string $block, array $schedule): string;
    private static function ghashTable(string $h): array;        // 16 násobků H
    private static function ghash(array $table, string $data): string;
    private static function multiply(array $table, string $x): string;
    private static function lengthBlock(int $aadBytes, int $textBytes): string;
    private static function pad(string $value): string;
    private static function inc32(string $block): string;
}
```

`encrypt()` je jediný vstupní bod pro produkční kód a IV si bere z `random_bytes()`.
`seal()` přijímá IV od volajícího, což je nutné pro známé testovací vektory
a pro `selfCheck()`. Její dokumentace proto výslovně varuje, že opakované použití
kombinace klíče a IV zničí důvěrnost i autenticitu obou zpráv.

Implementační poznámky:

- AES-256 přes čtyři T-tabulky `te0` až `te3` sestavené za běhu ze S-boxu uloženého
  jako 256bajtový řetězec. Slova jsou 32bitové inty maskované `& 0xFFFFFFFF`.
  Poslední kolo používá samotný S-box.
- GCM: `H = E(K, 0^16)`, `J0 = IV || 0x00000001`, CTR začíná na `inc32(J0)`,
  `S = GHASH(A || pad || C || pad || len64(A) || len64(C))`, `T = S xor E(K, J0)`.
  Délky se skládají přes `pack('NN', …)`, aby kód nezávisel na `pack('J')`.
- GHASH násobí v GF(2^128) přes předpočítanou tabulku 16 násobků `H`, tedy 32 kroků
  po půlbajtu na blok. Naivní postup po bitech by u devítikilobajtového souboru
  znamenal víc než 70 tisíc iterací PHP.
- Stav 128bitových hodnot jsou čtyři 32bitová slova, nikoli 64bitová, aby nikde
  nevznikl problém se znaménkovým bitem.
- Metoda `encrypt()` volá `random_bytes(12)` pro každý požadavek zvlášť. Nepoužívá
  se poolovaný `Ctx::hex()`, protože IV musí být nezávislý na ostatních náhodných
  hodnotách požadavku.
- Dešifrovací cesta se do serveru nepřidává. Server nikdy nedešifruje a mrtvý kód
  v jediném souboru je zbytečná plocha. Ověření tagu je věcí klienta a testů.

### 5.4 Nová sekce „Statistics“ (za sekcí Sweeper)

```php
final class StatsFile
{
    public const EMPTY_STATS = '{}';
    private const MAX_BYTES = 65536;

    public static function path(): string;                // QUEUE_ROOT/stats.json
    public static function read(): string;                // syrové bajty, nebo '{}'
    public static function record(string $client): void;   // nikdy nevyhodí výjimku

    private static function load(): array;                 // jen platné záznamy
    private static function store(array $entries): void;   // .writing plus rename
}
```

`record()` přečte soubor, ověří potřebu zápisu podle `STATS_MIN_UPDATE_SECONDS`,
sestaví novou mapu, zapíše `.writing` soubor a přejmenuje ho přes cíl. Při selhání
`rename()` dočasný soubor smaže.

### 5.5 `StatsHandler` (za `SyncHandler`)

```php
final class StatsHandler
{
    private const NONCE_OCTET = 0;   // 0 není platná klientská adresa, kolize nehrozí

    public static function handle(): void;
    private static function parseLine(string $line): array;   // timestamp, nonce, signature
}
```

Postup `handle()`:

1. Kontrola `Content-Type` a `Content-Length` stejně jako u `sync`.
2. `Cfg::assertRuntime()`, pak `StatsCrypto::supported()`, jinak `503`.
3. `BodyReader`, první řádek s limitem `MAX_AUTH_LINE_BYTES`.
4. Přečíst druhý řádek. Pokud existuje, zapamatovat `400` a `drain()`.
5. `parseLine()`, sestavit `canonical`, ověřit `hash_equals()`, jinak `401`.
6. Teprve teď vyhodit zapamatovanou `400`.
7. Při `PERSIST_NONCES` uplatnit `NonceStore::claim(0, …)`, jinak `401`.
8. `StatsCrypto::deriveKey()`, `StatsFile::read()`, `StatsCrypto::encrypt()`.
9. Odeslat JSON v pořadí `alg`, `iv`, `ciphertext`, `tag`.
10. `Log::info('stats', [])` bez jediné citlivé hodnoty.

### 5.6 `Sweeper`

Nová privátní metoda `sweepStatsTemp()` volaná z `maybeRun()`. Smaže z kořene
`stats.json.*.writing` starší než `WRITING_STALE_SECONDS`. Bez ní by po pádu
procesu zůstal osiřelý dočasný soubor napořád.

### 5.7 `Router`

- `dispatch()`: větev `stats`, jen `POST`, jinak `Http::sendMethodNotAllowed('POST')`.
- `finish()`: po flushi zavolat `StatsFile::record(Ctx::$syncedClient)`, když je
  hodnota neprázdná.

### 5.8 `SelfTest`

Do `checkRuntime()` přibudou dvě kontroly:

- `integer_width`: `pass`, když `PHP_INT_SIZE >= 8`, jinak `fail` s poznámkou, že
  `stats` na tomto hostingu poběží jen jako `503`.
- `stats_crypto`: spustí jeden známý vektor AES-256-GCM a jeden HKDF a porovná
  výsledek. Prokáže, že šifrování na cílovém hostingu opravdu počítá správně.

---

## 6. Dokumentace a deploy

| Soubor | Změna |
|---|---|
| [PROTOCOL.md](PROTOCOL.md) | Nový řádek v tabulce endpointů v kapitole 2 a nová kapitola 13 „Akce stats“ s požadavkem, kanonickým řetězcem, odvozením klíče, tvarem odpovědi, chybami a postupem klienta. Číslování kapitol 1 až 12 zůstává. |
| [README.md](README.md) | `stats.json` do stromu „Co vzniká na disku“, krátká kapitola „Statistiky“ v části Provoz, zmínka o `STATS_MIN_UPDATE_SECONDS` a odkaz na referenčního klienta v testech. |
| [deploy/apache.htaccess](deploy/apache.htaccess) | Do `FilesMatch` přidat příponu `json`, aby `stats.json` nešlo stáhnout ani při `QUEUE_ROOT` uvnitř document rootu. |
| [deploy/nginx.conf](deploy/nginx.conf) | Totéž v `location ~ \.(…)$`. |

Dočasný soubor záměrně končí na `.writing`, protože tu příponu obě konfigurace
už blokují.

---

## 7. Testy

Lokální PHP je 8.5.9, 64bitové, **bez OpenSSL**, ale s `hash_hkdf`. Testy proto
nesmí spoléhat na `openssl_decrypt` a potřebují vlastní dešifrování.

### 7.1 `tests/lib/StatsClient.php` (nový)

Referenční klient napsaný podle protokolu, nikoli převzatý ze serveru.

- `request(string $secret, array $options = []): string` sestaví podepsaný NDJSON řádek.
- `canonical(string $timestamp, string $nonce): string`.
- `deriveKey(string $secret, string $canonical): string` přes vestavěné
  `hash_hkdf('sha256', …)`, tedy nezávisle na implementaci v serveru.
- `decrypt(string $ciphertext, string $key, string $iv, string $tag, string $aad): ?string`
  čistě v PHP, ale záměrně jinou cestou než server: klasický S-box s explicitním
  `MixColumns` a GHASH po bitech. Pomalé řešení tu nevadí a shoda dvou nezávisle
  psaných implementací je právě to, co má test prokázat.

### 7.2 `tests/unit/stats_crypto_test.php` (nový, v procesu)

- HKDF proti `hash_hkdf()` na několika náhodných vstupech.
- HKDF proti testovacím vektorům RFC 5869, případy 1 a 3.
- AES-256-GCM proti testovacím vektorům z GCM specifikace, případy 13 až 16, tedy
  prázdný text bez AAD, jeden blok, dlouhý text a dlouhý text s AAD.
- `deriveKey()` vrací 32 bajtů, liší se od `SERVER_SECRET`, od
  `sha256(SERVER_SECRET)` i od prostých zřetězení, a mění se se změnou kanonického
  requestu.
- `encrypt()` vrací 12bajtový IV, 16bajtový tag a ciphertext stejné délky jako
  otevřený text. Padesát volání dá padesát různých IV.
- Prázdný otevřený text projde a jeho tag je platný.

Pokud některý statický vektor neprojde, ověřuje se nejdřív vektor proti publikované
specifikaci a teprve pak se sahá na kód.

### 7.3 `tests/unit/stats_file_test.php` (nový, v procesu)

- První `record()` založí soubor a zapíše platný ISO čas.
- Druhé `record()` do 30 sekund soubor nezmění.
- Po umělém zestárnutí záznamu se čas aktualizuje.
- Záznam jiného klienta zůstane zachovaný.
- Poškozený obsah, prázdný soubor a soubor nad 64 KiB nevyhodí výjimku a vedou na
  regulérní přepis.
- Položky s neplatným klíčem nebo hodnotou se do nového souboru nepřenesou.

### 7.4 `tests/integration/stats_test.php` (nový, přes HTTP)

- Platný požadavek vrátí `200`, JSON se čtyřmi klíči v pořadí `alg`, `iv`,
  `ciphertext`, `tag` a `alg` rovným `AES-256-GCM`.
- Po `sync` klientů A a B obsahuje dešifrovaná mapa obě adresy s platnými časy.
- Bez podpisu, s cizím podpisem, s podpisem pod jiným tajemstvím, s prošlým
  timestampem a s podpisem pro jinou akci vrací `401`.
- Žádná chybová odpověď neobsahuje `10.99.99.`, `stats.json` ani `{"`.
- Dvě po sobě jdoucí volání vrátí různý IV i různý ciphertext.
- Změna jediného bajtu v `iv`, `tag`, `ciphertext` nebo v AAD, tedy podepsaném
  timestampu, vede na selhání ověření tagu a klient nedostane žádný částečný text.
- `GET` na `?action=stats` vrací `405` s hlavičkou `Allow: POST`.
- Volání `stats` a `health` nezmění `mtime` ani obsah `stats.json`.
- Opakované `sync` v rychlém sledu zapíše jen jeden záznam, s `PERSIST_NONCES`
  zapnutým i vypnutým.
- `php-error.log` instance zůstane prázdný.

Dále test, že v logu instance s `LOG_LEVEL = 'info'` po volání `stats` není
secret, nonce, podpis, odvozený klíč, IV, ciphertext ani tag.

### 7.5 Úpravy stávajících testů

- `tests/compat/php82_test.php`: nesmí se volat nic z `openssl_`, `sodium_`
  ani `mcrypt_` a nesmí se volat ani `hash_hkdf()`, protože zadání žádá odvození
  postavené na `hash_hmac`. Zmínka o OpenSSL v komentáři je naopak žádoucí, takže
  se hlídá volání funkce, nikoli výskyt slova. Dále že `STATS_MIN_UPDATE_SECONDS`
  má výchozí 30.
- `tests/integration/deploy_test.php`: do seznamu přípon přidat `json`.
- `tests/integration/router_test.php`: `stats` má stejnou politiku metod jako
  `sync`, tedy `405` s hlavičkou `Allow: POST`.
- `tests/integration/selftest_test.php`: preflight musí hlásit `integer_width`
  a `stats_crypto` jako `pass`.

### 7.6 Spuštění

```powershell
& D:\nginx\php8.5.9\php.exe tests\run.php
```

Cílem je nula selhání a nula nových přeskočených testů.

---

## 8. Rizika

| Riziko | Ošetření |
|---|---|
| Čistě-PHP AES je pomalý | Payload má nejvýše 253 záznamů, tedy zhruba 9 KiB. T-tabulky a 4bitový GHASH drží šifrování v jednotkách až desítkách milisekund. Měří se v integračním testu jako horní mez. |
| Chyba v GF(2^128) násobení | Statické vektory GCM plus round-trip proti nezávislé implementaci v testech. |
| 32bitové PHP | `StatsCrypto::supported()` a kontrola v preflightu. `sync` zůstává nedotčený. |
| Znovupoužití IV | IV je z `random_bytes()` pro každou odpověď zvlášť a test ověřuje unikátnost na padesáti voláních. |
| Únik tajemství do logů | Do `Log` jde jen kategorie a prázdné pole. Test kontroluje, že se v logu neobjeví secret, nonce, podpis, IV ani ciphertext. |
| Zdržení `sync` | Zápis statistik běží až po flushi odpovědi a je celý v `try`/`catch`. |
| Osiřelé dočasné soubory | Uklízí je `Sweeper::sweepStatsTemp()`. |
| `stats.json` veřejně dostupný | Kryje ho `.htaccess` psaný do `QUEUE_ROOT`, doplněné pravidlo na příponu `json` a hlavně doporučení mít `QUEUE_ROOT` mimo document root. |

---

## 9. Pořadí prací

1. `StatsCrypto` včetně HKDF, AES-256-GCM a GHASH.
2. `tests/unit/stats_crypto_test.php` a `tests/lib/StatsClient.php`, ověřit vektory.
3. `StatsFile`, `Ctx::$syncedClient`, napojení v `Router::finish()` a `SyncHandler`.
4. `tests/unit/stats_file_test.php`.
5. `StatsHandler`, routa, `Sweeper::sweepStatsTemp()`, konstanta v konfiguraci.
6. `tests/integration/stats_test.php`.
7. Preflight, `deploy/`, `PROTOCOL.md`, `README.md`.
8. Úpravy stávajících testů a plný běh celé sady.

---

## 10. Výsledek implementace

| Ověření | Výsledek |
|---|---|
| AES-256 jako blokový šifr | Vektor FIPS-197 C.3 souhlasí. |
| AES-256-GCM | Vektory 13 až 16 specifikace GCM souhlasí, včetně nezarovnaného textu s AAD. |
| HKDF-SHA-256 | Vektory 1 a 3 z RFC 5869 souhlasí, plus deset náhodných vstupů proti `hash_hkdf()`. |
| Nezávislá implementace | Klient v `tests/lib/StatsClient.php` dešifruje vše, co server zašifroval, a nic z toho, co bylo změněno. |
| Rychlost | Nejhorší payload, 253 záznamů a 9 509 bajtů, se zašifruje za 5,2 ms. Sto dvacet prázdných `sync` cyklů drží 101 req/s jako dřív. |
| Celá sada | 146 testů, nula selhání, nula přeskočených. |

Odchylky od plánu jsou jen dvě, obě popsané výše: `StatsCrypto::seal()` je veřejná
kvůli známým vektorům a sekce `Statistics` stojí v souboru před sekcí
`Software AES-256-GCM`, protože `StatsFile` se čte snáz před šifrou než za ní.
