# Drátový protokol phpvpn

> Závazná specifikace pro implementaci klientské virtuální síťové karty.
> Serverovou stranu implementuje [phpvpn.php](phpvpn.php). Architektura vychází z [design2.md](design2.md), odchylky jsou vypsané v kapitole 12.

Server je pouze relay. Šifrovaný IPv4 paket nedešifruje, nemění a jeho obsah nijak neinterpretuje. Ověřuje autorizaci, obálku zprávy, zdrojovou a cílovou VPN adresu a limity.

---

## 1. Transport

| Vlastnost | Hodnota |
|---|---|
| Protokol | HTTP/1.1. Server přijímá prostý HTTP i HTTPS naprosto stejně, transport nijak nevyhodnocuje a proxy hlavičky nečte. |
| Metoda | `POST` pro `sync`, `stats` a `selftest`, `GET` nebo `POST` pro `health`, `GET` pro výchozí stránku. |
| Content-Type požadavku | `application/x-ndjson` nebo `application/jsonl`, volitelně s `; charset=utf-8`. |
| Content-Type odpovědi | `application/x-ndjson; charset=utf-8` pro `sync`, `application/json; charset=utf-8` pro `health`, `stats`, `selftest` a chyby. |
| Kódování | UTF-8. Všechny řetězcové hodnoty protokolu jsou navíc omezené na ASCII znakové třídy. |
| Spojení | Krátké. Server nikdy nečeká na nová data, nepoužívá polling, `sleep` ani streaming. |

URL obsahuje výhradně `?action=…`. **Nikdy do ní nepatří klientská adresa, timestamp, nonce, podpis ani token**, protože URL se běžně zapisuje do logů hostingu, reverzní proxy i WAF.

Transport je pro protokol nevýznamný. Schéma, port, adresa protistrany ani hlavičky typu `X-Forwarded-Proto` neovlivňují autentizaci, HMAC, ochranu proti replay, fronty, obsah odpovědi ani HTTP status. Přímé spojení a spojení přes proxy jsou nerozlišitelné. Čisté HTTP je vědomě podporovaný režim: nezajišťuje důvěrnost, ověření serveru ani ochranu odpovědi na síti. Pokud jsou tyto vlastnosti potřeba, zajišťuje je infrastruktura před serverem. Užitečná zátěž paketů je end to end šifrovaná nezávisle na tom.

---

## 2. Endpointy

| URL | Metoda | Účel |
|---|---|---|
| `/phpvpn.php` | `GET`, `HEAD` | Provozní HTML stránka. Nevyžaduje autorizaci. |
| `/phpvpn.php?action=health` | `GET`, `POST`, `HEAD` | Verze, stav a autoritativní čas serveru. Nevyžaduje autorizaci. |
| `/phpvpn.php?action=sync` | `POST` | Jeden krátký obousměrný cyklus. Vyžaduje podpis. |
| `/phpvpn.php?action=stats` | `POST` | Šifrovaný výpis časů posledního `sync` každého klienta. Vyžaduje znalost `SERVER_SECRET`, viz kapitola 13. |
| `/phpvpn.php?action=selftest` | `POST` | Preflight prostředí. Vyžaduje podpis a je ve výchozím stavu vypnutý. |

### health

```json
{"version":"1.0.0","status":"OK","serverTime":"2026-08-08T14:20:00Z"}
```

| Pole | Typ | Význam |
|---|---|---|
| `version` | řetězec | Verze serveru PHP VPN. V úspěšné odpovědi je vždy a je **prvním klíčem** serializovaného objektu. |
| `status` | řetězec | Vždy `OK`. Úspěšná odpověď znamená, že server běží a odpovídá. |
| `serverTime` | řetězec | Autoritativní čas serveru v UTC podle kapitoly 3. |

Klient tímto endpointem koriguje vlastní hodiny. Volá jej při startu a po opakovaném `401`. Zároveň si z něj může přečíst verzi serveru, aniž by se musel autorizovat. Odpověď nikdy neobsahuje konfiguraci, tokeny, obsah front ani diagnostiku.

---

## 3. Tělo požadavku

NDJSON, tedy jeden kompaktní JSON objekt na řádek, každý řádek zakončený `\n`. **Poslední řádek musí být zakončený také**, jinak jde o chybu protokolu.

První řádek je vždy přesně jeden záznam `auth`. Samotný tento řádek je platný prázdný `sync`, kterým si klient jen vyzvedne čekající pakety.

```
{"type":"auth","client":"10.99.99.2","timestamp":"2026-08-08T14:20:00Z","nonce":"o5Tq…","signature":"9f2c…"}
{"type":"ack","batchId":"20260808T142000.123456Z_9f2c1ab7d4e60518.000000217"}
{"type":"data","uuid":"550e8400-e29b-41d4-a716-446655440000","src":"10.99.99.2","dst":"10.99.99.4","data":"BASE64"}
```

### 3.1 Záznam `auth`

Musí obsahovat přesně pět polí, žádné navíc.

| Pole | Pravidlo |
|---|---|
| `type` | `"auth"` |
| `client` | `10.99.99.2` až `10.99.99.254`, bez vodicích nul |
| `timestamp` | `YYYY-MM-DDTHH:MM:SSZ`, UTC, odchylka nejvýše `CLOCK_SKEW_SECONDS` (výchozí 60 s) |
| `nonce` | 16 až 64 znaků z `A-Z a-z 0-9 _ -`, tedy base64url bez výplně |
| `signature` | 64 malých hexadecimálních znaků |

### 3.2 Záznam `ack`

Musí obsahovat přesně dvě pole.

| Pole | Pravidlo |
|---|---|
| `type` | `"ack"` |
| `batchId` | přesně ten identifikátor, který server poslal v záznamu `delivery` |

V jednom požadavku smí být nejvýše `MAX_ACKS_PER_REQUEST` potvrzení, výchozí 256. Potvrzení neznámé nebo již potvrzené dávky je bezpečné a není chybou.

### 3.3 Záznam `data`

Musí obsahovat přesně pět polí, žádné navíc.

| Pole | Pravidlo |
|---|---|
| `type` | `"data"` |
| `uuid` | RFC 4122 v malých hexadecimálních znacích, `8-4-4-4-12` |
| `src` | musí se přesně shodovat s `client` ze záznamu `auth` |
| `dst` | `10.99.99.2` až `10.99.99.254` |
| `data` | neprázdný base64 se standardní abecedou `A-Z a-z 0-9 + /`, délka dělitelná čtyřmi, výplň nejvýše `==` |

Server obsah `data` nedekóduje. Znaková třída je vynucená proto, že se hodnota vkládá do generovaného JSON bez escapování.

---

## 4. Podpis

Access token se nikdy nepřenáší. Podpis současně dokazuje znalost `SERVER_SECRET` i tokenu konkrétního klienta.

```
derivedKey     = hash_hmac('sha256', client + "\n" + accessToken, SERVER_SECRET, raw = true)

canonical      = "POST"      + "\n"
               + action      + "\n"     // "sync" nebo "selftest"
               + client      + "\n"
               + timestamp   + "\n"
               + nonce       + "\n"
               + sha256hex(bytes of every line AFTER the auth line, its own "\n" excluded)

signature      = hash_hmac('sha256', canonical, derivedKey)   // hex
```

Hashuje se **přesně ten bajtový proud, který následuje po ukončovacím `\n` řádku `auth`**, včetně ukončovacího `\n` posledního řádku. U prázdného `sync` je to prázdný řetězec, jehož SHA-256 je `e3b0c442…b855`.

Vyloučení řádku `auth` z hashované části odstraňuje kruhovou závislost, kdy by podpis podepisoval sám sebe.

Referenční implementace klientské strany je v [tests/lib/SignRequest.php](tests/lib/SignRequest.php).

---

## 5. Tělo odpovědi

Úspěšný `sync` vrací `200` a NDJSON. Nejprve nula až N záznamů `delivery`, na konci vždy přesně jeden `sync-result`.

```
{"type":"delivery","batchId":"20260808T142000.123456Z_9f2c1ab7d4e60518.000000217","packet":{"type":"data","uuid":"…","src":"10.99.99.3","dst":"10.99.99.2","data":"BASE64"}}
{"type":"sync-result","acceptedPacketUuids":["…"],"deferredPacketUuids":[],"acceptedPackets":120,"acceptedBytes":82000,"responseTruncated":true,"recommendedRequestBytes":65536,"recommendedRequestPackets":100}
```

### 5.1 `delivery`

Jedna dávka od jednoho odesílatele může být rozložena do více řádků `delivery`, které pak sdílejí stejné `batchId`. Klient potvrzuje **dávku, ne jednotlivý paket**, takže stačí poslat `ack` pro každé unikátní `batchId`, které v odpovědi uvidí.

### 5.2 `sync-result`

| Pole | Význam |
|---|---|
| `acceptedPacketUuids` | Jediné pakety, které klient smí odstranit ze své odchozí fronty. Pořadí odpovídá pořadí v požadavku. |
| `deferredPacketUuids` | Platné pakety, které server nezařadil. Klient je ponechá v pořadí pro další cyklus. Nejde o chybu. |
| `acceptedPackets` | Počet zařazených paketů. |
| `acceptedBytes` | Počet **bajtů požadavku**, které byly zařazené. Měřeno stejnou jednotkou jako `recommendedRequestBytes`. |
| `responseTruncated` | `true` znamená, že na serveru pro klienta zůstala další data. |
| `recommendedRequestBytes` | Horní doporučení serveru pro příští požadavek. |
| `recommendedRequestPackets` | Horní doporučení serveru pro příští požadavek. |

`acceptedPacketUuids` a `deferredPacketUuids` dohromady vždy přesně pokrývají všechny záznamy `data` z požadavku, bez průniku.

---

## 6. Chybové odpovědi

Tvar je vždy malý a obecný. Nikdy neprozradí cestu, konfiguraci ani existenci jiného klienta.

```json
{"status":"error","error":"unauthorized"}
```

| HTTP | `error` | Příčina | Doporučená reakce klienta |
|---|---|---|---|
| `400` | `bad_request` | Syntaktická chyba, chybějící nebo přebývající pole, chybějící `auth`, druhý `auth`, neznámý typ záznamu | Chyba klienta. Neopakovat beze změny. |
| `401` | `unauthorized` | Neplatný podpis, neznámý klient, podvržené `src`, časová odchylka, replay | Obnovit čas z `health`, pak opakovaně selhává-li, upozornit uživatele. |
| `404` | `not_found` | Neznámá akce nebo vypnutý `selftest` | Chyba konfigurace klienta. |
| `405` | `method_not_allowed` | Špatná metoda. Odpověď obsahuje `Allow`. | Chyba klienta. |
| `413` | `payload_too_large` | Tělo, řádek nebo počet paketů nad protokolovým stropem | Zmenšit dávku a zkusit znovu. |
| `415` | `unsupported_media_type` | Chybný `Content-Type` | Chyba klienta. |
| `429` | `queue_full` | Cílová fronta nebo globální kvóta je plná. Odpověď obsahuje `Retry-After`. | Ponechat dávku, backoff s jitterem. |
| `503` | `queue_unavailable` | Disk, inode nebo zámek nedostupný | Ponechat dávku, backoff s jitterem. |
| `503` | `queue_busy` | Rozpočet na zámek vyčerpán souběhem | Ponechat dávku, krátký backoff. |
| `503` | `server_misconfigured` | Chybná konfigurace serveru | Upozornit uživatele, nemá smysl opakovat rychle. |
| `500` | `internal_error` | Neočekávaná chyba | Backoff. |

Při `400`, `401`, `413` a `415` **nebylo uloženo nic**. Při `429` a `503` také ne, s výjimkou vícecílové dávky popsané v kapitole 12.

---

## 7. Povinnosti klienta

1. Rozparsovat NDJSON po řádcích.
2. **Deduplikovat podle `uuid`** v krátkodobé lokální cache. Doručení je at-least-once, duplicita je normální stav, nikoli chyba.
3. Předat platné pakety virtuální kartě a zařadit každé unikátní `batchId` do potvrzovací fronty.
4. Ze své odchozí fronty odstranit **pouze** pakety uvedené v `acceptedPacketUuids`. Odložené ponechat v původním pořadí.
5. V nejbližším dalším `sync` odeslat `ack` pro zpracované dávky.
6. Velikost příští dávky adaptovat podle `recommendedRequestBytes`, `recommendedRequestPackets`, částečného přijetí, latence a počtu předchozích úspěšných cyklů.
7. Při `responseTruncated = true` smí klient poslat další `sync` bez čekání na běžný interval, ale musí mít vlastní strop rychlosti.
8. Při síťové chybě, `429` nebo `503` ponechat dávku a opakovat s exponenciálním backoffem a jitterem.
9. Interval `sync` si určuje klient. Server jej nepředepisuje a nikdy v požadavku nečeká.

Klient nesmí předpokládat exactly-once doručení ani trvalou dostupnost hostingu.

---

## 8. Identifikátor dávky

```
20260808T142000.123456Z_9f2c1ab7d4e60518.000000217
└──── UTC čas ────┘ └── náhoda ──┘ └ velikost ┘
```

Pevná délka 50 znaků. Regulární výraz: `^\d{8}T\d{6}\.\d{6}Z_[0-9a-f]{16}\.\d{9}$`.

Klient s identifikátorem zachází jako s neprůhledným řetězcem. Vrací jej v `ack` beze změny. Nesmí jej sám sestavovat ani z něj nic odvozovat, protože jeho vnitřní struktura je implementační detail serveru.

---

## 9. Limity

Výchozí hodnoty odpovídají typickému sdílenému hostingu s `post_max_size = 8M`. Administrátor je smí změnit, klient se proto nesmí spoléhat na konkrétní číslo a musí reagovat na `413`, `429` a na doporučení v `sync-result`.

| Konstanta | Výchozí | Význam |
|---|---|---|
| `MAX_REQUEST_BYTES` | 1 MiB | Protokolový strop celého těla. Nad něj `413`. |
| `MAX_RESPONSE_BYTES` | 1 MiB | Strop pro vyzvednutí jedním cyklem. |
| `MAX_PACKET_LINE_BYTES` | 128 KiB | Strop jednoho NDJSON řádku. |
| `MAX_REQUEST_PACKETS` | 2000 | Protokolový strop počtu záznamů `data`. Nad něj `413`. |
| `MAX_ACCEPTED_REQUEST_BYTES` | 768 KiB | Provozní strop. Nad něj se pakety odloží, nikoli odmítnou. |
| `MAX_ACCEPTED_REQUEST_PACKETS` | 1000 | Provozní strop. Nad něj se pakety odloží. |
| `MAX_ACKS_PER_REQUEST` | 256 | Strop počtu potvrzení. Nad něj `400`. |
| `MAX_BUFFERS` | 128 | Nevyzvednutých dávek na jednu frontu. Nad něj `429`. |
| `QUEUE_TTL_SECONDS` | 60 | Nevyzvednutá dávka se poté zahodí. |
| `CLOCK_SKEW_SECONDS` | 60 | Přípustná odchylka `timestamp`. |

---

## 10. Ochrana proti replay

Řízená přepínačem `PERSIST_NONCES`.

- `false`, výchozí: nonce se nezapisuje ani neporovnává. Replay uvnitř časového okna je vědomý provozní kompromis, který nevytváří žádné soubory navíc.
- `true`: hash nonce se ukládá po dobu `NONCE_WINDOW_BUCKETS × NONCE_BUCKET_SECONDS`, výchozí tři minuty. Opakovaný nonce vrátí `401`.

Klient musí generovat kryptograficky náhodný nonce pro každý požadavek bez ohledu na nastavení serveru.

---

## 11. Příklad úplného cyklu

Klient `10.99.99.2` odesílá jeden paket klientovi `10.99.99.4` a zároveň potvrzuje dávku z minulého cyklu.

```http
POST /phpvpn.php?action=sync HTTP/1.1
Host: vpn.example.com
Content-Type: application/x-ndjson
Content-Length: 421

{"type":"auth","client":"10.99.99.2","timestamp":"2026-08-08T14:20:00Z","nonce":"o5TqZ0nS1w7bK3xR9dLp","signature":"9f2c1ab7…"}
{"type":"ack","batchId":"20260808T141958.004512Z_31ba07de9c445120.000000198"}
{"type":"data","uuid":"550e8400-e29b-41d4-a716-446655440000","src":"10.99.99.2","dst":"10.99.99.4","data":"3q2+7w=="}
```

```http
HTTP/1.1 200 OK
Content-Type: application/x-ndjson; charset=utf-8
Cache-Control: no-store, no-cache, must-revalidate
X-Content-Type-Options: nosniff
Content-Length: 402

{"type":"delivery","batchId":"20260808T142000.891233Z_c40f81be2a97dd05.000000203","packet":{"type":"data","uuid":"7c9e6679-7425-40de-944b-e07fc1f90ae7","src":"10.99.99.4","dst":"10.99.99.2","data":"kBiJ3Q=="}}
{"type":"sync-result","acceptedPacketUuids":["550e8400-e29b-41d4-a716-446655440000"],"deferredPacketUuids":[],"acceptedPackets":1,"acceptedBytes":118,"responseTruncated":false,"recommendedRequestBytes":786432,"recommendedRequestPackets":1000}
```

V dalším cyklu klient odešle `ack` pro `20260808T142000.891233Z_c40f81be2a97dd05.000000203`.

---

## 12. Vědomé odchylky od design2.md

| Odchylka | Návrh | Implementace | Důvod |
|---|---|---|---|
| Selhání jedné cílové fronty | Řízená chyba celého požadavku | Uložené pakety jdou do `acceptedPacketUuids`, neuložené do `deferredPacketUuids`. `429` nebo `503` až když neuspěje **žádný** cíl. | Návrh zároveň zakazuje tvrdit, že server přijal dávku, kterou neuložil. Toto pravidlo je splněné a jeden zahlcený příjemce navíc nezastaví provoz mezi ostatními dvojicemi. |
| Název claimnutého souboru | `.sending.<náhodné-id>` | `<batchId>.sending` | Zpracování `ack` je jediný `unlink` místo výpisu adresáře. |
| Obsah souboru dávky | Uložené pakety | Uložené rovnou jako řádky `delivery` | Čtecí cesta je čistý bajtový přenos bez `json_decode` a `json_encode`. |
| Tvar `batchId` | `…_random-id` | `…_random.velikost` | Velikost v názvu umožní naplánovat odpověď do `MAX_RESPONSE_BYTES` bez jediného volání `stat`. |
| Výchozí limity | 8 MiB | 1 MiB | Výchozí `post_max_size` na běžném hostingu je 8 MiB, návrhové hodnoty se pod něj nevejdou. Viz [problems.md](problems.md). |
| Uložení nonce | Soubor na nonce | Rotované minutové buckety s osmibajtovými záznamy | Soubor na nonce vyčerpá inode kvótu sdíleného hostingu. |

Odchylky nemění nic, co by klient poznal jinak než přes hodnoty `acceptedPacketUuids` a `deferredPacketUuids`, jejichž význam zůstává přesně podle návrhu.

---

## 13. Akce `stats`

Provozní přehled, kdy server naposledy viděl kterého klienta. Volajícím je správce, nikoli klientská karta: neposílá se VPN adresa ani access token, volající prokazuje znalost `SERVER_SECRET`. Odpověď je vždy šifrovaná, i na čistém HTTP.

Server ke statistikám nepotřebuje OpenSSL ani žádné jiné nepovinné rozšíření. AES-256-GCM i HKDF-SHA-256 jsou implementované přímo v PHP, proto akce vyžaduje 64bitové PHP. Na 32bitovém buildu vrací `503 server_misconfigured` a zbytek serveru funguje dál.

### 13.1 Obsah statistik

`QUEUE_ROOT/stats.json` mapuje VPN adresu na UTC čas posledního úspěšného `sync`.

```json
{"10.99.99.2":"2026-08-08T14:20:00Z","10.99.99.4":"2026-08-08T14:19:31Z"}
```

| Vlastnost | Chování |
|---|---|
| Kdy se zapisuje | Po `sync`, který skončil stavem `200`, a to až po odeslání odpovědi, takže klienta nezdržuje. |
| Jak často | Pro jednu adresu nejvýše jednou za `STATS_MIN_UPDATE_SECONDS`, výchozí 30 s. Mladší záznam se nepřepisuje. |
| Mazání | Staré záznamy se nemažou nikdy. |
| Souběh | Bez zámku. Zápis se publikuje přejmenováním a poslední zápis vyhrává. |
| Selhání | Nikdy nezneplatní už úspěšný `sync`. |
| `health` a `stats` | Soubor nemění. |

### 13.2 Požadavek

Tělo je NDJSON s jediným řádkem, zakončeným `\n`. Podpis nepokrývá tělo, takže druhý řádek je chyba protokolu.

```http
POST /phpvpn.php?action=stats HTTP/1.1
Content-Type: application/x-ndjson

{"type":"stats-auth","timestamp":"2026-08-08T14:20:00Z","nonce":"o5TqZ0nS1w7bK3xR9dLp","signature":"9f2c…"}
```

Záznam musí obsahovat přesně čtyři pole.

| Pole | Pravidlo |
|---|---|
| `type` | `"stats-auth"` |
| `timestamp` | `YYYY-MM-DDTHH:MM:SSZ`, UTC, odchylka nejvýše `CLOCK_SKEW_SECONDS` |
| `nonce` | 16 až 64 znaků z `A-Z a-z 0-9 _ -`, kryptograficky náhodný pro každý požadavek |
| `signature` | 64 malých hexadecimálních znaků |

Autorizační údaje nikdy nepatří do URL, ze stejného důvodu jako u `sync`, viz kapitola 1.

### 13.3 Podpis

```
canonical = "POST" + "\n" + "stats" + "\n" + timestamp + "\n" + nonce
signature = hash_hmac('sha256', canonical, SERVER_SECRET)      // hex
```

Kanonický řetězec nekončí novým řádkem. Server porovnává podpis přes `hash_equals`. `SERVER_SECRET` se nikdy neposílá ani neloguje.

Při `PERSIST_NONCES = true` platí ochrana proti replay stejně jako u `sync`, jen ve vlastním bucketu.

### 13.4 Odvození klíče

Klíč pro AES se nesmí rovnat `SERVER_SECRET` ani vzniknout zřetězením hodnot, proto se odvozuje přes HKDF-SHA-256.

```
salt = sha256(canonical)                        // 32 syrových bajtů
info = "phpvpn/stats-response/v1"
key  = HKDF-SHA-256(ikm = SERVER_SECRET, salt, info, L = 32)
```

Klíč je tak svázaný s konkrétním požadavkem: žádné dvě volání nešifrují stejným klíčem.

### 13.5 Odpověď

```json
{"alg":"AES-256-GCM","iv":"base64-iv","ciphertext":"base64-ciphertext","tag":"base64-tag"}
```

| Pole | Význam |
|---|---|
| `alg` | Vždy `AES-256-GCM`. Klient jinou hodnotu nesmí zpracovat. |
| `iv` | 12 bajtů z generátoru kryptografické náhody, nový pro každou odpověď. |
| `ciphertext` | Šifrovaný obsah `stats.json`. Prázdné statistiky jsou `{}`. |
| `tag` | 16bajtový autentizační tag. |

AAD tvoří kanonický autorizační request v nešifrované podobě, takže odpověď nelze připojit k jinému požadavku.

### 13.6 Postup klienta

1. Sestavit `timestamp`, náhodný `nonce` a podpis, odeslat jediný řádek.
2. Ověřit, že `alg` je `AES-256-GCM`.
3. Odvodit stejný klíč z vlastní znalosti `SERVER_SECRET` a z kanonického requestu, který klient sám sestavil.
4. Ověřit autentizační tag nad AAD.
5. **Až po úspěšném ověření tagu** dešifrovat a zpracovat JSON. Při neplatném tagu se celá odpověď zahodí, žádná částečně dešifrovaná data se nepoužijí.

Referenční implementace klienta je v [tests/lib/StatsClient.php](tests/lib/StatsClient.php).

### 13.7 Chyby

| HTTP | `error` | Příčina |
|---|---|---|
| `400` | `bad_request` | Nečitelný JSON, jiný počet polí, jiný `type`, druhý řádek v těle |
| `401` | `unauthorized` | Neplatný podpis, chybný formát `timestamp`, `nonce` nebo `signature`, časová odchylka, replay |
| `405` | `method_not_allowed` | Jiná metoda než `POST` |
| `413` | `payload_too_large` | Tělo nebo řádek nad protokolovým stropem |
| `415` | `unsupported_media_type` | Chybný `Content-Type` |
| `503` | `server_misconfigured` | Krátký `SERVER_SECRET` nebo 32bitové PHP |

Chybová odpověď je vždy jen malý obecný dokument z kapitoly 6 a nikdy neobsahuje statistiky ani jejich část.

### 13.8 Co šifrování řeší a co ne

Šifrování chrání **obsah** odpovědi a váže ji na konkrétní požadavek. Nenahrazuje transportní zabezpečení: nechrání proti zdržení, zahození ani opakovanému doručení odpovědi na síti. Čistý HTTP zůstává podporovaný, ale pokud jsou tyto vlastnosti potřeba, zajišťuje je infrastruktura před serverem.
