# Drátový protokol phpvpn v1

Tento dokument je normativní specifikací protokolu phpvpn v1. Klíčová slova **MUSÍ**, **NESMÍ**, **MĚL BY**, **NEMĚL BY** a **SMÍ** se vykládají ve významu RFC 2119.

## 1. Účel a rozsah

phpvpn je krátce připojovaný store-and-forward relay pro přenos end-to-end šifrovaných IPv4 paketů mezi klienty. Server:

- autentizuje požadavky a oprávnění odesílatele;
- kontroluje syntaxi, směrovací metadata, limity a kapacitu front;
- ukládá neprůhledné šifrované záznamy do front cílových klientů;
- při sestavení odpovědi atomicky načítá a odstraňuje všechny čekající záznamy cílového klienta;
- eviduje čas poslední úspěšné komunikace klientů.

Server nezná klíče užitečného zatížení, obsah IPv4 paketů nedešifruje a neinterpretuje. Distribuce payload klíčů, konfigurace virtuální síťové karty, přidělování VPN adres a zpracování dešifrovaného IP provozu jsou odpovědností klientské vrstvy mimo tento protokol.

Protokol je navržen pro PHP 8.2+, více souběžných PHP workerů a úložiště s omezenými prostředky. Nevyžaduje dlouhé spojení, long-polling, WebSocket, cron ani serverovou session.

### 1.1 Bezpečnostní cíle

Protokol poskytuje:

- autentizaci požadavků `info` znalostí serverového tajemství;
- autentizaci požadavků `data` současnou znalostí jediného serverového tajemství a přístupového klíče konkrétního klienta;
- integritu autentizovaného požadavku včetně přesných bajtů jeho těla;
- end-to-end důvěrnost a integritu payloadu pomocí AES-256-GCM;
- důvěrnost a integritu obsahu odpovědi `info` pomocí AES-256-GCM;
- oddělení klíčů podle účelu pomocí HKDF-SHA-256.

php implementace serveru by v základu neměla vyžadovat žádné rozšíření základního instalačního balíčku PHP. Tedy ani OpenSSL. Algoritmus AES-256-GCM je implementovaný softwarově.

### 1.2 Vědomě neposkytované vlastnosti

Protokol pomocí autentizačního timestampu odmítá požadavky starší než povolené 60sekundové časové okno. Nezabrání však opakování platné zprávy uvnitř tohoto okna a neposkytuje exactly-once ani garantované doručení, ochranu proti zahození nebo zdržení zprávy ani skrytí provozních metadat. Úspěšná odpověď `data` není jako celek autentizovaná: AES-GCM chrání jednotlivé payloady a jejich směrovací AAD, nikoli úplnost odpovědi, pořadí řádků ani `data-result`. Server používá vědomě zvolený optimistický at-most-once model: data odstraní z bufferu před odesláním odpovědi a při ztrátě nebo nezpracování odpovědi je znovu nenabídne. Tato vlastnost je přijatelná pro šifrované IP pakety, protože ztrátu podle potřeby řeší vyšší síťové nebo aplikační vrstvy. Protokol nezajišťuje ani dostupnost při zaplnění úložiště nebo vyčerpání PHP workerů.

## 2. Normativní rozhodnutí

| Oblast | Rozhodnutí v1 |
|---|---|
| Architektura | Bezstavové, krátké HTTP požadavky; žádná serverová session. |
| Wire format | JSON pro řídicí akce, NDJSON pro datovou akci. |
| Doručení | Optimistické at-most-once na úrovni serverového bufferu, bez potvrzení. Po atomickém načtení jsou data ze serveru odstraněna ještě před přenosem odpovědi. |
| Replay | Timestamp omezuje platnost autentizovaného požadavku na ±60 s vůči času serveru. Uvnitř okna není replay detekován; bez deduplikace a bez perzistence timestampu nebo nonce. |
| Payload klíče | Klientské per-peer nebo skupinové klíče distribuované mimo relay; server je nezná. |
| Více cílů | Částečné přijetí je dovoleno a výsledek se vrací pro každý záznam. |
| Transport | HTTP i HTTPS jsou rovnocenně podporované; server funguje přímo i za proxy. HTTPS není podmínkou protokolu. |
| Úložiště | Abstraktní backend; v1 musí mít ovladač `disk`, další ovladače mohou přibýt bez změny wire formátu. |
| Kompatibilita | v1 je nový protokol; staré akce a staré wire formáty nejsou aliasy ani fallback. |

## 3. Role, identity a tajemství

### 3.1 Role

- **Relay server** přijímá HTTP požadavky a spravuje fronty.
- **Klient** má právě jednu nakonfigurovanou VPN IPv4 adresu a může být odesílatelem i příjemcem.
- **Administrátor** konfiguruje jediné serverové tajemství, klientské tokeny, limity a storage driver.

Výchozí adresní prostor je `10.99.99.0/24`. Platné klientské adresy jsou `10.99.99.2` až `10.99.99.254`, zapsané bez vodicích nul. Jiný adresní prostor vyžaduje společnou konfiguraci všech účastníků, nikoli změnu wire formátu.

### 3.2 Klíče

Server a klienti používají tři oddělené třídy klíčů:

1. `SERVER_SECRET`: právě jedna konstanta v konfigurační hlavičce PHP souboru, kterou znají server a oprávnění klienti. Výchozí hodnota je 32 nulových bajtů a správce ji má nahradit bezpečnou náhodnou hodnotou. Slouží pro `auth-semi`, šifrování `info` a odvození autentizačního klíče `auth-full`.
2. `ACCESS_KEY`: právě jeden 32bajtový náhodný klíč unikátní pro jednoho klienta. Zná jej pouze server a daný klient a používá se jako vstupní tajemství při odvození autentizačního klíče `auth-full`. Server jej vyhledá v mapě `ACCESS_KEYS` podle VPN adresy `src`; samotný klíč ani jeho identifikátor se nikdy nepřenáší.
3. `PAYLOAD_KEY`: právě jeden pevný 32bajtový end-to-end klíč pro každou uspořádanou dvojici VPN adres (`src`, `dst`). Znají jej pouze klienti daného směru komunikace; relay server jej NESMÍ mít. Distribuce je mimo protokol.

Kryptografické operace používají přesné bajty konstanty `SERVER_SECRET` bez implicitního hex nebo base64 dekódování. Prázdný řetězec, hodnota tvořená pouze nulovými bajty `0x00` i řetězec tvořený pouze ASCII znaky `0` jsou platné vstupy a autentizované endpointy je nesmí odmítnout jen kvůli jejich obsahu; taková konfigurace však neposkytuje praktické utajení a veřejná stránka na ni upozorňuje. Každý `ACCESS_KEY` se zapisuje jako přesně 64 malých hexadecimálních znaků a pro kryptografii se dekóduje na 32 bajtů. Klienti vedou lokální mapu `PAYLOAD_KEYS[src][dst]`; každá její hodnota se pro AES-256-GCM dekóduje na přesně 32 bajtů.

`SERVER_SECRET`, `ACCESS_KEY` ani `PAYLOAD_KEY` nemají identifikátor přenášený v protokolu. Klient zvolí jediný `PAYLOAD_KEY` výhradně podle přesné uspořádané dvojice `src` a `dst`.

## 4. Transport HTTP, HTTPS a proxy

### 4.1 Společná pravidla

Server MUSÍ mít stejné aplikační chování přes HTTP i HTTPS. Minimem je HTTP/1.1; HTTP/2 nebo HTTP/3 může ukončit webserver nebo proxy, pokud zachová metodu, query parametr, tělo a význam odpovědi.

Server:

- NESMÍ odvozovat autentizaci z IP adresy TCP protistrany;
- NESMÍ zahrnovat schéma, host, port ani proxy hlavičky do kryptografických výpočtů;
- NESMÍ důvěřovat `X-Forwarded-For`, `X-Forwarded-Proto`, `Forwarded` ani podobným hlavičkám pro autorizaci nebo směrování;
- MUSÍ akceptovat přímé spojení i spojení za reverzní proxy se stejným výsledkem;
- MUSÍ posílat `Cache-Control: no-store, no-cache, must-revalidate`, `Pragma: no-cache` a `X-Content-Type-Options: nosniff`;
- NESMÍ zapisovat tajemství, autentizační HMAC tagy, ciphertext, GCM tagy nebo celé requesty do logu.

V URL je pouze `?action=<akce>`. Identita klienta, tokeny, autentizační HMAC tagy a šifrovaný obsah patří výhradně do těla. Proxy musí předat tělo bajtově beze změny; transformace konců řádků, dekomprese na jiný obsah nebo přepis JSON zneplatní HMAC.

### 4.2 Hranice ochrany při HTTP

Čisté HTTP je platný a podporovaný režim. Aplikační kryptografie v něm chrání tajné hodnoty a autenticitu v rozsahu popsaném tímto dokumentem, nenahrazuje však zabezpečený transport.

Pasivní pozorovatel HTTP spojení vidí URL, metodu, velikosti, časování, VPN adresy `src` a `dst`, identifikátory zpráv a objem provozu. Nevidí plaintext payloadu ani plaintext odpovědi `info`.

Aktivní útočník na HTTP trase může požadavky a odpovědi kopírovat, opakovat, zdržet, zahodit, přeskupit, měnit nebo spojení ukončit. Nemůže bez znalosti příslušného klíče vytvořit nový platný HMAC požadavku ani GCM tag jednotlivého payloadu. Odpověď `data` však nemá vlastní MAC: útočník může odstranit nebo přeskupit platné datové řádky a změnit, odstranit či podvrhnout `data-result`. Klient tak v režimu HTTP nemůže kryptograficky ověřit úplnost vráceného bufferu ani správnost výsledku přijetí. Protože server záznamy před odesláním odstraní, zahození či poškození odpovědi znamená jejich nevratnou ztrátu na úrovni relay. Autentizovaný požadavek lze úspěšně opakovat pouze uvnitř jeho 60sekundového časového okna; odpověď nemá samostatnou kontrolu stáří.

HTTPS navíc poskytuje ochranu transportních metadat v těle a hlavičkách, autentizaci TLS protistrany a ochranu spojení proti změně. Zda jej administrátor použije, je provozní rozhodnutí mimo povinné požadavky protokolu.

## 5. Endpointy

| URL | Metoda | Požadavek | Úspěšná odpověď |
|---|---|---|---|
| `/phpvpn.php` | `GET`, `HEAD` | bez těla | HTML provozní stránka |
| `/phpvpn.php?action=health` | `GET`, `HEAD` | bez těla | `application/json` |
| `/phpvpn.php?action=info` | `POST` | jeden JSON objekt `auth-semi` | jeden JSON objekt `info` |
| `/phpvpn.php?action=data` | `POST` | NDJSON začínající `auth-full` | NDJSON s datovými záznamy a výsledkem |

Jiné metody vrací `405 method_not_allowed` s hlavičkou `Allow`. Neznámá akce vrací `404 not_found`.

### 5.1 Provozní stránka

`GET /phpvpn.php` vrací jednoduché HTML s `<h1>Server is running</h1>`. Je-li `SERVER_SECRET` prázdný, tvořený pouze nulovými bajty `0x00` nebo pouze ASCII znaky `0`, stránka navíc zobrazí červený `<h2>Server is not secure. Please modify SERVER_SECRET in the phpvpn.php</h2>`.

Tato veřejná stránka provádí pouze uvedenou kontrolu a neprozrazuje konfiguraci. Varování je informativní: prázdný nebo nulový `SERVER_SECRET` neblokuje `info` ani `data` a sám o sobě nesmí vést k `503 server_misconfigured`.

### 5.2 `health`

`health` nevyžaduje autentizaci a vrací právě tato pole:

```json
{"serverVersion":"1.0.0","status":"running","description":"OK","timestamp":"2026-08-09T20:15:30Z"}
```

- `serverVersion` je verze serverové aplikace;
- `status` je při úspěchu `running`;
- `description` je při úspěchu `OK`;
- `timestamp` je aktuální UTC čas přesně ve formátu `YYYY-MM-DDTHH:MM:SSZ`. Klient jej před autentizací parsuje jako UTC a použije pro korekci vlastního časového offsetu.

Pořadí členů JSON objektu není sémantické. Klient musí před prvním autentizovaným požadavkem získat čas serveru přes `health`; stejné srovnání zopakuje po `401`, pokud může být příčinou odchylka hodin.

## 6. Společný wire formát

### 6.1 JSON

- Kódování je UTF-8 bez BOM.
- Protokolová pole používají pouze ASCII.
- JSON objekt NESMÍ obsahovat duplicitní názvy členů.
- Neznámé pole v autentizačním, datovém nebo výsledkovém záznamu je chyba `400 bad_request`.
- Číselná pole jsou nezáporná celá čísla v bezpečném rozsahu PHP integeru.
- Base64 hodnoty používají base64url abecedu `A-Z a-z 0-9 _ -` bez `=` paddingu.
- Binární porovnání HMAC a tagů se provádí konstantním časem.

PHP implementace musí duplicitní top-level klíče odhalit před nebo během dekódování; samotné `json_decode()` je tiše přepíše a není pro tuto kontrolu dostačující.

### 6.2 NDJSON

NDJSON je posloupnost kompaktních JSON objektů. Každý záznam včetně posledního MUSÍ končit jedním bajtem LF (`0x0A`). CRLF, prázdný řádek a fragment bez závěrečného LF jsou neplatné. Velikosti se počítají z přijatých bajtů, nikoli z dekódovaného JSON.

### 6.3 Identifikátory

`requestId` a `messageId` jsou UUID verze 4 v malých hexadecimálních znacích ve tvaru `8-4-4-4-12`. Generují se CSPRNG.

Identifikátory slouží ke korelaci. Server ani klient je NESMÍ používat jako replay cache nebo deduplikační klíč napříč požadavky. `messageId` musí být unikátní pouze uvnitř jednoho těla `data`, aby byla odpověď jednoznačná; opakování celého požadavku se stejnými identifikátory je znovu zpracováno.

## 7. Kryptografické primitivy a kanonizace

Povinné algoritmy v1 jsou SHA-256, HMAC-SHA-256, HKDF-SHA-256 podle RFC 5869 a AES-256-GCM s 16bajtovým tagem.

Všechny textové kanonické řetězce jsou ASCII/UTF-8, hodnoty jsou oddělené jedním LF a řetězec nekončí LF, není-li uvedeno jinak. `SHA256HEX(x)` je SHA-256 nad přesnými bajty `x`, zapsaný 64 malými hexadecimálními znaky. Výstup `HMAC-SHA-256(key, x)` má 32 bajtů a do autentizačního pole `tag` se zapisuje base64url bez paddingu. Autentizační HMAC `tag` se podle typu zprávy jednoznačně odlišuje od 16bajtového AES-GCM pole `tag` ve zprávách `info` a `data`.

### 7.1 Klíč `auth-semi`

```text
salt    = SHA-256("phpvpn/auth-semi")               // 32 raw bytes
info    = "phpvpn/auth-semi"
semiKey = HKDF-SHA-256(SERVER_SECRET, salt, info, 32)
```

### 7.2 Klíč `auth-full`

Po syntaktické validaci `src` klient i server odvodí jediný 32bajtový klíč pro HMAC tag požadavku `auth-full`:

```text
saltMaterial = "phpvpn/auth-full/salt" || NUL || src
authFullSalt = HMAC-SHA-256(SERVER_SECRET, saltMaterial)   // 32 raw bytes
info         = "phpvpn/auth-full/key" || NUL || src
fullAuthKey  = HKDF-SHA-256(ACCESS_KEY, authFullSalt, info, 32)
```

`NUL` je jediný bajt `0x00` a `src` jsou přesné ASCII bajty validované VPN adresy bez vodicích nul. Doménové řetězce, oddělovač, pořadí argumentů i výstupní délka jsou normativní. `SERVER_SECRET` určuje `authFullSalt` a klientův jediný `ACCESS_KEY` je vstupním tajemstvím HKDF; bez obou hodnot nelze vypočítat správný `fullAuthKey` ani HMAC tag. `fullAuthKey` se nepřenáší, neukládá do protokolového stavu a nemá identifikátor.

Prázdný nebo nulový `SERVER_SECRET` zůstává podle kapitoly 3.2 platnou konfigurací, ale jako veřejně předvídatelný vstup neposkytuje druhé praktické tajemství; proto na něj provozní stránka povinně upozorňuje.

## 8. Akce `info`

### 8.1 Požadavek `auth-semi`

Content-Type je `application/json; charset=utf-8`. Tělo obsahuje právě jeden JSON objekt a žádné další bajty:

```json
{"type":"auth-semi","timestamp":"2026-08-09T20:15:30Z","requestId":"4e94de3a-b9ad-4bc3-9297-82b98a19a143","tag":"BASE64URL"}
```

HMAC tag se vypočte:

```text
canonical = "PHPVPN-AUTH-SEMI" LF
            "POST" LF
            "info" LF
            timestamp LF
            requestId

tag = HMAC-SHA-256(semiKey, canonical)
```

`timestamp` je JSON string o přesně 20 ASCII znacích ve tvaru `YYYY-MM-DDTHH:MM:SSZ`, shodném s `health.timestamp` a `info.generatedAt`. Musí představovat platné gregoriánské datum a UTC čas; rok má čtyři číslice, ostatní číselné části přesně dvě, `T` a `Z` jsou velká, sekundy jsou `00` až `59` a nejsou dovoleny zlomky, časová zóna s offsetem ani leap second `60`. Neplatný formát nebo kalendářní hodnota vrací `400 bad_request`. Po úspěšném parsování server porovná oba UTC okamžiky s přesností na celé sekundy a požadavek přijme pouze při absolutní odchylce nejvýše 60 sekund; větší odchylka vrací obecné `401 unauthorized`.

Do `canonical` vstupují přesné ASCII bajty validovaného `timestamp` beze změny a bez normalizace. Pole `tag` musí být base64url bez paddingu a po dekódování mít přesně 32 bajtů; neplatné kódování nebo délka vrací `400 bad_request`. Server jej porovnává s vypočteným HMAC konstantním časem a nesouhlas vrací obecné `401 unauthorized`.

`requestId` je korelační hodnota. Server nepersistuje `timestamp` ani `requestId`. Timestamp omezuje replay starého požadavku, ale identický platný požadavek lze uvnitř otevřeného 60sekundového okna zopakovat a server jej přijme znovu.

### 8.2 Plaintext odpovědi

Server sestaví jeden kompaktní JSON objekt. Minimální obsah je:

```json
{"serverVersion":"1.0.0","generatedAt":"2026-08-09T20:15:30Z","clients":[{"address":"10.99.99.2","lastSeen":"2026-08-09T20:14:52Z"}],"limits":{"maxSerializedDataRecordBytes":262144,"maxDataRecordsPerRequest":16,"maxRequestBodyBytes":4198400,"maxQueueBytesPerClient":16777216,"queueTtlSeconds":60}}
```

`generatedAt` je aktuální čas serveru přesně ve formátu `YYYY-MM-DDTHH:MM:SSZ`. `clients` obsahuje pouze VPN adresu a přibližný čas poslední úspěšně zpracované komunikace. Nesmí obsahovat token, secret, fyzickou IP, cestu na disku ani obsah fronty. Klient, který ještě nemá platný statistický záznam, může být vynechán. `lastSeen` je UTC RFC 3339 s přesností na sekundy. Kvůli 30sekundovému omezení zápisů a záměrně neřešeným souběhům jde pouze o best-effort provozní údaj, nikoli o auditní stopu ani autoritativní důkaz dostupnosti klienta.

`limits` popisuje skutečné aktivní hodnoty serveru. Klient nesmí spoléhat pouze na výchozí hodnoty této specifikace. Odpověď `info` nesmí obsahovat název, konfiguraci ani jinou informaci o interním úložišti serveru.

### 8.3 Šifrovaná odpověď

AES-GCM žádný salt automaticky nevytváří. Server proto pro každou odpověď explicitně generuje:

- `salt`: 32 náhodných bajtů;
- `iv`: 12 náhodných bajtů;
- `tag`: 16 bajtů.

```text
info = "phpvpn/info-response/" || requestId
key  = HKDF-SHA-256(SERVER_SECRET, salt, info, 32)

aad = "PHPVPN-INFO" LF
      "info" LF
      requestId LF
      BASE64URL(salt) LF
      BASE64URL(iv)
```

Server šifruje přesné UTF-8 bajty plaintext JSON pomocí AES-256-GCM s uvedeným `key`, `iv` a `aad`. Vrací jeden JSON objekt:

```json
{"type":"info","requestId":"4e94de3a-b9ad-4bc3-9297-82b98a19a143","salt":"BASE64URL","iv":"BASE64URL","ciphertext":"BASE64URL","tag":"BASE64URL"}
```

Klient MUSÍ nejprve ověřit délky, odvodit klíč a ověřit GCM tag nad celým ciphertextem a AAD. Plaintext smí parsovat až po úspěšném ověření tagu.

Salt ani IV nejsou anti-replay mechanismus. Opakovanou dříve platnou odpověď protokol cíleně nedetekuje. `requestId` váže odpověď k požadavku a brání záměně integrity mezi různými transakcemi, ale server ani klient nevede historii již použitých hodnot.

## 9. Akce `data`

### 9.1 Pořadí požadavku

Content-Type je `application/x-ndjson; charset=utf-8`. Požadavek má přesně toto pořadí:

1. právě jeden záznam `auth-full`;
2. nula až `MAX_DATA_RECORDS_PER_REQUEST` záznamů `data`.

Samotný `auth-full` je platný polling bez odesílaných dat.

### 9.2 Záznam `auth-full`

```json
{"type":"auth-full","src":"10.99.99.2","timestamp":"2026-08-09T20:15:30Z","requestId":"69e82c2a-2732-466f-b9b1-f891084df639","tag":"BASE64URL"}
```

HMAC `tag` pokrývá všechny bajty za ukončovacím LF prvního řádku:

```text
body = exact bytes after the LF terminating auth-full

canonical = "PHPVPN-AUTH-FULL" LF
            "POST" LF
            "data" LF
            src LF
            timestamp LF
            requestId LF
            SHA256HEX(body)

tag = HMAC-SHA-256(fullAuthKey, canonical)
```

Server nejprve syntakticky ověří `src`, podle něj v konstantě `ACCESS_KEYS` vyhledá právě jeden `ACCESS_KEY`, podle kapitoly 7.2 zkombinuje tento klíč s jediným `SERVER_SECRET` do `fullAuthKey` a teprve tím ověří HMAC tag. Musí dočíst a hashovat celé tělo. Před úspěšným ověřením HMAC NESMÍ změnit buffer ani publikovat data. Při neznámém klientovi použije místo `ACCESS_KEY` náhodně vypadající pevný 32bajtový dummy klíč, provede stejné odvození s `SERVER_SECRET` a `src` a vrátí obecné `401 unauthorized`, aby čas odpovědi neprozrazoval existenci klienta.

Pro `timestamp` platí stejný přesný UTC formát, kalendářní validace a kontrola absolutní odchylky nejvýše 60 sekund jako u `auth-semi`. Jeho přesné ASCII bajty jsou v uvedeném pořadí součástí `canonical`, takže změna kteréhokoli znaku zneplatní HMAC `tag`. Také tento `tag` se dekóduje na přesně 32 bajtů a porovnává konstantním časem. Timestamp zajišťuje omezenou čerstvost vůči starým replayům; stejný autentizovaný požadavek lze uvnitř platného 60sekundového okna přehrát a server jej zpracuje znovu. Server timestamp nepersistuje.

### 9.3 Záznam `data`

```json
{"type":"data","messageId":"d5b543a9-9fc3-4c98-916c-ea0198f67a79","src":"10.99.99.2","dst":"10.99.99.4","iv":"BASE64URL","ciphertext":"BASE64URL","tag":"BASE64URL"}
```

Pravidla:

- `src` se musí přesně shodovat se `src` v `auth-full`; jinak `401 unauthorized`;
- `dst` musí být platná VPN adresa;
- `messageId` musí být unikátní v rámci právě jednoho requestu, ale server jej neporovnává s minulostí;
- `iv` po dekódování musí mít 12 bajtů;
- `tag` po dekódování musí mít 16 bajtů;
- `ciphertext` nesmí být prázdný;
- celý NDJSON řádek včetně LF nesmí překročit `MAX_SERIALIZED_DATA_RECORD_BYTES`.

### 9.4 End-to-end šifrování payloadu

Odesílatel vyhledá jediný 32bajtový `PAYLOAD_KEY` v lokální mapě `PAYLOAD_KEYS[src][dst]`. Protokol neprovádí další odvození klíče a nepřenáší jeho identifikátor. AAD je:

```text
aad = "PHPVPN-DATA" LF
      "data" LF
      messageId LF
      src LF
      dst LF
      BASE64URL(iv)
```

Plaintext je přesný binární IPv4 paket. Odesílatel jej zašifruje AES-256-GCM a přenese ciphertext a 16bajtový tag. Příjemce musí před předáním paketu virtuální kartě:

1. vyhledat jediný klíč podle přesných směrovacích adres `src` a `dst`;
2. sestavit přesné AAD;
3. ověřit GCM tag a teprve poté získat plaintext;
4. zkontrolovat syntaxi IPv4, celkovou délku a shodu vnitřních zdrojových a cílových adres s `src` a `dst`.

Relay tag neověřuje, protože nemá `PAYLOAD_KEY`. Změna `src`, `dst`, `messageId`, IV nebo ciphertextu způsobí u příjemce neplatný tag.

#### Jedinečnost IV

Pro každý konkrétní `PAYLOAD_KEY` MUSÍ odesílatel zabránit opakovanému použití stejného 12bajtového IV pro šifrování dvou různých plaintextů. Odesílatel používá 12 náhodných bajtů z CSPRNG.

Klient nevede databázi přijatých IV a IV nepersistuje kvůli replay kontrole. Jedinečnost IV je povinnost odesílatele pro korektnost AES-GCM, nikoli mechanismus detekce replay. Opakované doručení stejné trojice IV/ciphertext/tag je přijato jako další platná zpráva.

### 9.5 Odpověď `data`

Úspěšná odpověď má Content-Type `application/x-ndjson; charset=utf-8` a pořadí:

1. nula nebo více přímo uložených záznamů `data`;
2. právě jeden závěrečný záznam `data-result`.

Po úspěšném ověření požadavku a zpracování jeho odesílaných záznamů server atomickou operací načte všechny neexpirované datové záznamy aktuálně uložené v bufferu klienta a současně je z persistentního bufferu odstraní. Nově publikovaný záznam se `dst` shodným s autentizovaným `src` proto patří již do této odpovědi. Teprve z takto získaného lokálního seznamu server sestaví odpověď. Počet vrácených záznamů se neomezuje samostatným protokolovým ani konfiguračním limitem a odpověď se nestránkuje. Jedinou horní mezí objemu je kapacita bufferu. Každý záznam se vrací přímo ve stejném schématu `data`, bez další obálky:

```json
{"type":"data","messageId":"e8edc241-96ec-4b51-9336-1fb0836f4db5","src":"10.99.99.4","dst":"10.99.99.2","iv":"BASE64URL","ciphertext":"BASE64URL","tag":"BASE64URL"}
```

Záznam `data-result`:

```json
{"type":"data-result","requestId":"69e82c2a-2732-466f-b9b1-f891084df639","acceptedMessageIds":["d5b543a9-9fc3-4c98-916c-ea0198f67a79"],"deferredMessageIds":[],"acceptedRecords":1,"acceptedBytes":2048}
```

- `acceptedMessageIds` jsou jediné položky, které smí odesílatel odstranit ze své odchozí fronty;
- `deferredMessageIds` zůstávají klientovi pro pozdější pokus;
- obě pole bez průniku přesně pokrývají všechny `messageId` v požadavku;
- `acceptedBytes` je součet přijatých serializovaných datových řádků včetně LF.

Příklad výsledku pollingu bez odeslaných i bez vrácených dat:

```json
{"type":"data-result","requestId":"69e82c2a-2732-466f-b9b1-f891084df639","acceptedMessageIds":[],"deferredMessageIds":[],"acceptedRecords":0,"acceptedBytes":0}
```

Klient MUSÍ přijmout úplné NDJSON tělo s právě jedním závěrečným `data-result`, jehož `requestId` odpovídá aktuálnímu požadavku. Každý datový řádek ověří samostatně podle AES-GCM pravidel v kapitole 9.4; řádek s neplatným tagem zahodí a ostatní platné payloady může zpracovat. Protokol neposílá žádné následné potvrzení.

Jakmile atomické načtení a odstranění skončí, server data znovu nenabídne, a to ani když PHP worker selže před odesláním, HTTP/HTTPS odpověď nedorazí, je neúplná nebo ji klient nezpracuje. Jde o vědomě přijaté optimistické at-most-once chování serverového bufferu. Odpověď jako celek nemá HMAC ani jinou ochranu úplnosti a `requestId` ani výsledková pole nejsou v režimu HTTP kryptograficky chráněné; bezpečnostní důsledky stanoví kapitola 4.2. Síťová vrstva může dříve platnou odpověď zopakovat, takže at-most-once vlastnost serverového výdeje sama o sobě nezajišťuje deduplikaci u klienta. Klient nevede replay cache a opakovaně doručené platné payloady znovu zpracuje.

## 10. Fronty a doručovací sémantika

### 10.1 Model

Každý cílový klient má logický perzistentní buffer. Do jeho velikosti se započítávají všechny dosud nevydané záznamy. Výchozí limit je 16 MiB logických serializovaných bajtů na klienta bez interní režie backendu.

Server může z jednoho requestu vytvořit samostatné dávky podle cíle. Publikace jedné dávky musí být atomická: po pádu je viditelná celá, nebo vůbec. Úspěch jednoho cíle nezávisí na selhání jiného cíle.

Při sestavení odpovědi server atomicky vyprázdní všechny neexpirované záznamy z bufferu autentizovaného klienta. Vrácený seznam může obsahovat více dříve publikovaných dávek. Server jej nesmí stránkovat ani omezit počtem záznamů.

### 10.2 Částečné přijetí

Server zpracovává cílové skupiny deterministicky. Přijaté položky uvede v `acceptedMessageIds`, nepřijaté v `deferredMessageIds`. Pokud přijme alespoň jednu položku nebo požadavek žádnou neodesílá, vrací `200` a `data-result`. Pokud nelze přijmout žádnou odesílanou položku kvůli kapacitě, vrací `429 queue_full`; při nedostupnosti backendu `503 queue_unavailable`.

Chybová odpověď nikdy neopravňuje klienta odstranit položku z odchozí fronty. Při úspěchu klient vyhodnotí pouze jediný syntakticky platný závěrečný `data-result` se shodným `requestId`; v režimu HTTP však jeho správnost není kryptograficky zaručena.

### 10.3 Vydání, ztráta a opakování

Operace vyprázdnění bufferu musí být atomická: po souběžném zápisu nebo pádu jsou konkrétní záznamy buď stále v bufferu, nebo přesně jednou vráceny handleru a ze storage odstraněny. Server nečeká na reakci klienta a odstraněné záznamy neobnovuje. Pád workeru, přerušení spojení, zahození odpovědi nebo chyba klienta po vyprázdnění proto znamenají jejich ztrátu. Vyšší vrstvy přenášeného IP provozu mohou podle svých vlastních pravidel provést nové odeslání.

Klient ani server neporovnávají `messageId`, IV, request body nebo autentizační HMAC tag s historií. Replay autentizovaného odesílacího requestu uvnitř platného 60sekundového okna založí další kopii dat; po uzavření okna server request odmítne. Replay datové odpovědi může payload předat znovu, přestože server tentýž záznam z bufferu vydal jen jednou. Duplicita na trase i ztráta po vyprázdnění bufferu jsou výslovně přijatá omezení.

### 10.4 Expirace

Výchozí `QUEUE_TTL_SECONDS` je 60 s. Čas běží od prvního úspěšného publikování. Po expiraci smí backend dávku smazat bez doručení. Údržba může být oportunistická, takže fyzické odstranění může nastat později; expirovaná dávka však již nesmí být zahrnuta do odpovědi.

### 10.5 Storage driver

Wire protokol nesmí záviset na názvech souborů, adresářích ani databázové technologii. Implementace používá rozhraní s operacemi odpovídajícími alespoň:

- `enqueue(destination, records, expiresAt)`;
- `drainAll(client, now)`;
- `usage(client)`;
- `readClientStats()` a `touchLastSeenBestEffort(client, now)`;
- `sweep(now)`.

Povinný driver `disk` používá per-client adresáře, zámek s omezeným čekáním, zápis do dočasného souboru a atomické `rename()` ve stejném filesystému. Konfigurační volba `STORAGE_DRIVER` má výchozí hodnotu `disk`; budoucí `memcached`, `mariadb`, `mysql` nebo `postgresql` musí zachovat stejné atomické a kapacitní chování.

### 10.6 Statistiky klientů ovladače `disk`

Ovladač `disk` ukládá statistiky všech klientů do jediného centrálního souboru `STORAGE_ROOT/clients.json`. Soubor leží přímo v kořeni diskového úložiště, nikdy v per-client adresáři. Jeho normativní obsah je kompaktní UTF-8 JSON bez BOM:

```json
{"clients":{"10.99.99.2":{"lastSeen":"2026-08-09T20:14:52Z"},"10.99.99.4":{"lastSeen":"2026-08-09T20:15:11Z"}}}
```

Top-level objekt obsahuje právě člen `clients`. Ten je objektem, jehož názvy členů jsou platné nakonfigurované VPN adresy a hodnoty jsou objekty s jediným členem `lastSeen` ve formátu UTC RFC 3339 s přesností na sekundy. Statistiky NESMÍ obsahovat přístupové klíče, serverové tajemství, fyzické adresy, payloady ani interní cesty.

Po úspěšném ověření a normálním zpracování každého požadavku `data` server:

1. načte celý centrální soubor statistik;
2. vyhledá záznam autentizovaného `src`;
3. pouze pokud záznam chybí, jeho `lastSeen` je neplatný nebo platný čas je vůči aktuálnímu času serveru starší než 30 sekund, nastaví `lastSeen` na aktuální UTC čas a jedním pokusem zapíše celý JSON soubor;
4. při stáří přesně 30 sekund ani při čase v budoucnosti zápis neprovádí.

Neexistující soubor se interpretuje jako `{"clients":{}}` a způsobilá aktualizace se jej pokusí vytvořit. Syntakticky poškozený JSON nebo neplatné top-level schéma se interpretuje jako prázdná mapa; způsobilá aktualizace se jej jedním pokusem nahradí minimálním validním dokumentem obsahujícím právě aktualizovaného klienta. Neznámé nebo neplatné položky klientů v jinak platné mapě se při čtení ignorují; při zápisu se zachovají pouze platné položky. Jiná chyba otevření nebo čtení než neexistence souboru ukončí statistickou aktualizaci bez pokusu o zápis.

Statistiky jsou striktně best effort. Selhání otevření, čtení, dekódování, kódování nebo zápisu NESMÍ změnit HTTP status, `data-result`, publikování odesílaných dat, vyprázdnění příchozího bufferu ani předání payloadů. Implementace kvůli statistikám NESMÍ provádět retry, čekat, získávat souborový zámek ani vracet chybu. Souběžní PHP workeři mohou navzájem přepsat své změny a poslední dokončený zápis vyhrává; tato race condition a případný dočasný regres hodnoty `lastSeen` jsou vědomě přijaty.

Soubor `clients.json` NESMÍ být přímo dostupný přes HTTP ani HTTPS. `STORAGE_ROOT` má být mimo document root; pokud není, webserver MUSÍ přímý přístup k celému storage rootu explicitně zakázat. Endpoint `info` načítá platné položky z tohoto souboru, ale neprozrazuje jeho jméno, umístění ani chyby čtení. Při chybějícím, nečitelném nebo poškozeném souboru smí `info.clients` vrátit prázdné pole.

## 11. Limity a timeouty

### 11.1 Výchozí limity

| Konstanta | Výchozí hodnota | Význam |
|---|---:|---|
| `MAX_SERIALIZED_DATA_RECORD_BYTES` | 262144 B | Celý datový NDJSON řádek včetně LF. |
| `MAX_DATA_RECORDS_PER_REQUEST` | 16 | Počet záznamů `data` v jednom requestu. |
| `MAX_AUTH_RECORD_BYTES` | 4096 B | První řádek včetně LF. |
| `MAX_REQUEST_BODY_BYTES` | 4198400 B | `4096 + 16×262144`. |
| `MAX_INFO_PLAINTEXT_BYTES` | 262144 B | Plaintext šifrované odpovědi `info`. |
| `MAX_QUEUE_BYTES_PER_CLIENT` | 16777216 B | 16 MiB logické persistentní fronty. |
| `AUTH_TIMESTAMP_SKEW_SECONDS` | 60 s | Nejvyšší absolutní odchylka autentizačního timestampu od času serveru. |
| `QUEUE_TTL_SECONDS` | 60 s | Nejvyšší věk čekající dávky. |
| `LOCK_TIMEOUT_MS` | 200 ms | Nejvyšší čekání PHP workeru na storage zámek. |

Konfigurační invarianty musí zajistit, že `MAX_REQUEST_BODY_BYTES` pojme autentizační řádek a maximální počet maximálních záznamů. PHP `post_max_size`, limity proxy a webserveru pro požadavek musí být přísně vyšší než `MAX_REQUEST_BODY_BYTES`. Webserver a proxy musí umět bez stránkování přenést odpověď obsahující celou frontu o velikosti `MAX_QUEUE_BYTES_PER_CLIENT` plus závěrečný `data-result`; jinak server musí při diagnostice hlásit chybnou konfiguraci.

Překročení syntaktického limitu vrací `413 payload_too_large`. Zaplnění persistentní fronty vrací `429 queue_full` a nesmí být zaměněno za syntaktickou chybu.

### 11.2 Časové chování

Server odpovídá okamžitým vyprázdněním aktuálního bufferu. Nikdy nečeká na příchod nových dat, nepoužívá `sleep`, long-polling ani streaming neurčité délky.

Doporučené výchozí hodnoty klienta:

- connect timeout 5 s;
- celkový timeout requestu 15 s;
- exponenciální backoff po síťové chybě, `429` nebo `503`, počínaje 250 ms, nejvýše 30 s, vždy s jitterem;
- klient musí mít lokální limit rychlosti pollingu.

Server může u `429` a `503` poslat `Retry-After`. Klient jej respektuje jako dolní hranici čekání.

## 12. Stavy a přechody

### 12.1 Klientská komunikace

Protokol nemá kryptografickou session ani přihlašovací handshake. Logický bootstrap je:

1. povinný `health` před prvním autentizovaným požadavkem pro dostupnost, verzi a korekci časového offsetu;
2. volitelný autentizovaný `info` pro aktuální limity a přehled;
3. libovolný počet samostatně autentizovaných `data` cyklů;
4. ukončení prostým zastavením požadavků.

Neexistuje login, session ID, keepalive ani logout. Každý request lze zpracovat jiným PHP workerem nebo uzlem se sdíleným storage a stejnou konfigurací klíčů.

### 12.2 Stav uložené dávky

```text
neexistuje --enqueue--> buffered --drainAll--> smazána
                              |
                              | TTL
                              v
                           smazána
```

Server nevede žádný mezistav čekající na klienta. `drainAll` atomicky vrátí handleru všechny neexpirované záznamy a odstraní je ze storage. Pád během `enqueue` nesmí publikovat částečnou dávku. Pád po `drainAll` může způsobit ztrátu již odstraněných dat, nikdy jejich automatické opětovné vydání serverem.

## 13. Chyby

Chybové tělo je malý obecný JSON objekt:

```json
{"type":"error","code":"unauthorized"}
```

| HTTP | `code` | Význam | Reakce klienta |
|---:|---|---|---|
| 400 | `bad_request` | Neplatný JSON/NDJSON, pořadí, pole, hodnota, formát UTC timestampu, kódování či délka HMAC tagu nebo duplicitní klíč. | Opravit request; neopakovat beze změny. |
| 401 | `unauthorized` | Neplatný autentizační HMAC tag, `src`, konfigurace `SERVER_SECRET` nebo `ACCESS_KEY`, podvržené `src` v datovém záznamu nebo timestamp mimo povolené okno. | Obnovit čas přes `health`, ověřit obě tajemství; nemaž data. |
| 404 | `not_found` | Neznámá akce. | Opravit URL. |
| 405 | `method_not_allowed` | Nepovolená metoda. | Řídit se `Allow`. |
| 413 | `payload_too_large` | Překročen limit těla, řádku nebo počtu záznamů. | Zmenšit request. |
| 415 | `unsupported_media_type` | Chybný Content-Type. | Opravit Content-Type. |
| 429 | `queue_full` | Cílová fronta nemá kapacitu. | Ponechat data, backoff s jitterem. |
| 503 | `queue_busy` | Vyčerpán limit čekání na storage zámek. | Krátký backoff. |
| 503 | `queue_unavailable` | Backend nelze bezpečně použít. | Backoff; administrátorská kontrola. |
| 503 | `server_misconfigured` | Nekonzistentní limity nebo chybějící povinná kryptografická funkce. | Neopakovat rychle; opravit server. |
| 500 | `internal_error` | Neočekávaná interní chyba. | Ponechat data, backoff. |

Chybová odpověď není autentizovaná. Klient ji smí použít jako diagnostiku nebo podnět k backoffu, nikdy jako důkaz přijetí dat. Server nesmí v chybě prozradit cestu, token, secret, existenci jiného klienta, obsah fronty ani výpis výjimky.

Chyba centrálního souboru klientských statistik nikdy není protokolovou chybou a NESMÍ vyvolat `500`, `503` ani změnit jinak zvolený status odpovědi. Nesmí být uvedena v chybovém těle ani odlišitelná časováním záměrného čekání nebo retry.

## 14. Kompatibilita a rozšiřování

Hodnota `type` určuje schéma zprávy. Serverová hodnota `serverVersion` v `health` a `info` je informativní SemVer verze aplikace a není wire negotiation.

Normativní typy zpráv mají uzavřenou sadu polí. Nové volitelné informace se přidávají především do šifrovaného plaintextu `info`, kde klient musí ignorovat neznámá pole. Nekompatibilní změna povinných polí, kanonizace nebo kryptografických algoritmů vyžaduje nový název typu zprávy a nové kryptografické doménové řetězce.

Neexistuje automatický downgrade. Klient nesmí po chybě podporovaného typu zkusit historickou akci nebo slabší autentizaci. Akce `data` a `info` nenabízejí aliasy starých názvů.

## 15. Dopad na implementaci v PHP

Doporučené oddělení odpovědností v jednom nasaditelném PHP souboru:

- `Config` a `ConfigValidator`: klíče, limity, storage driver a invarianty;
- `Router` a `Http`: metoda, akce, hlavičky a obecné chyby;
- `JsonWire` a `NdjsonReader`: limitované čtení, duplicitní klíče, striktní pole a base64url;
- `KeyDerivation` a `Authenticator`: HKDF, HMAC, kanonizace a konstantní porovnání;
- `InfoCrypto`: pouze šifrování odpovědi `info` přes AES-256-GCM;
- `Storage` interface a `DiskStorage`;
- `ClientStats`: best-effort čtení a nejvýše jeden pokus o zápis centrálního `clients.json`;
- `InfoHandler` a `DataHandler`;
- `Maintenance`: omezený oportunistický sweep expirací.

Serverová implementace potřebuje `hash_hmac`, SHA-256, `random_bytes`, striktní JSON a pro `info` AES-256-GCM. Doporučená realizace používá `hash_hkdf()` a `openssl_encrypt(..., 'aes-256-gcm', ...)`. Chybí-li bezpečná AES-GCM implementace, `info` vrací `503 server_misconfigured`; datový relay může zůstat funkční, protože payload nedešifruje. Vlastní neauditovaná implementace AES se nedoporučuje.

Tělo požadavku `data` může mít přibližně 4,00 MiB. Implementace jej smí streamovat a průběžně hashovat, ale všechny změny storage musí odložit až po ověření `auth-full`. Odpověď může obsahovat celý buffer klienta, tedy až přibližně `MAX_QUEUE_BYTES_PER_CLIENT` plus `data-result`. Bezpečnou volbou je validované záznamy dočasně držet v omezené paměti; `memory_limit` musí mít dostatečnou rezervu pro request, celý seznam odpovědi a JSON struktury.

Diskový driver musí používat cesty odvozené pouze z již validované VPN adresy, zamykání s timeoutem, atomickou publikaci a obranu proti zpřístupnění bufferů i centrálních statistik přes webserver. Nemá používat neomezený globální zámek. Zákaz zámků pro `clients.json` nemění zámková pravidla datového bufferu. Údržba po odpovědi musí mít časový a početní rozpočet.

Statistika `lastSeen` se zvažuje pouze po platně autentizovaném a normálně zpracovaném požadavku `data`. Její 30sekundové omezení, jediný pokus o zápis a veškerá selhání jsou oddělené od výsledku datového zpracování podle kapitoly 10.6.

## 16. Povinné testovací scénáře

### 16.1 Transport a směrování

1. Stejný request přes HTTP, HTTPS, přímé spojení a proxy vede ke stejnému aplikačnímu výsledku.
2. Podvržené `X-Forwarded-*` hlavičky neovlivní autentizaci, `src`, frontu ani odpověď.
3. URL s neznámou akcí a nepovolená metoda vrátí správné obecné chyby.
4. Root stránka zobrazí H1; při výchozích 32 nulových bajtech i při prázdném nebo jinak pouze nulovém `SERVER_SECRET` navíc zobrazí červený H2, přičemž `info` a `data` zůstávají funkční.
5. `health` má čtyři povinná pole včetně `serverVersion`, status `running`, description `OK` a UTC timestamp v přesném tvaru `YYYY-MM-DDTHH:MM:SSZ`; klient z něj před autentizací správně odvodí svůj časový offset vůči serveru.

### 16.2 JSON, NDJSON a limity

1. Validní prázdný poll obsahuje pouze `auth-full` s LF.
2. Chybějící poslední LF, CRLF, prázdný řádek, duplicitní klíč a neznámé pole jsou odmítnuty.
3. Přesně 262144bajtový datový řádek projde, o bajt delší vrátí `413`.
4. Přesně 16 datových záznamů projde, sedmnáctý vrátí `413` bez změny storage.
5. Hrany pro celé tělo a buffer jsou testovány na `limit-1`, `limit` a `limit+1`.
6. Neplatná base64url, délka IV, délka tagu, UUID, VPN adresa a neznámé nebo nadbytečné pole jsou odmítnuty.
7. Úspěšná odpověď obsahuje nula nebo více přímých řádků `data` a právě jeden poslední `data-result`; další řádek za výsledkem je neplatný.

### 16.3 Kryptografie

1. Known-answer testy pro HKDF-SHA-256, HMAC-SHA-256 a AES-256-GCM.
2. `auth-semi` i `auth-full` nesou `timestamp` jako JSON string přesně ve formátu `YYYY-MM-DDTHH:MM:SSZ`; číselná hodnota, neplatné kalendářní datum, malá písmena, offset, zlomkové sekundy, chybějící `Z` a sekunda `60` jsou odmítnuty.
3. Known-answer test `auth-semi` sestaví `canonical` v pořadí doménový řetězec, `POST`, `info`, přesný timestamp a `requestId`; změna jediného znaku timestampu změní 32bajtový HMAC `tag`. Změna `ACCESS_KEY` nesmí změnit `semiKey` ani tag, protože `auth-semi` nadále závisí pouze na `SERVER_SECRET`.
4. Known-answer test odvození `auth-full` pro pevné `SERVER_SECRET`, `ACCESS_KEY` a `src` ověří přesné `saltMaterial`, 32bajtový `authFullSalt`, `info` a 32bajtový `fullAuthKey` podle kapitoly 7.2.
5. Změna jediného bajtu `SERVER_SECRET` nebo `ACCESS_KEY` musí změnit `fullAuthKey` i výsledný tag při jinak stejném requestu. Tag vypočtený se znalostí pouze jednoho z obou tajemství nesmí projít ověřením.
6. Known-answer test `auth-full` sestaví `canonical` v pořadí doménový řetězec, `POST`, `data`, `src`, přesný timestamp, `requestId` a `SHA256HEX(body)` a tag vypočte výhradně jako `HMAC-SHA-256(fullAuthKey, canonical)`; změna timestampu nebo jediného bajtu body tag zneplatní.
7. Server přijme UTC časy s odchylkou přesně `-60`, `0` a `+60` s a odmítne `-61` a `+61` s; po zpracování nevytvoří timestamp ani nonce záznam.
8. Autentizační pole `tag` v obou auth typech musí být base64url bez paddingu a dekódovat se na přesně 32 bajtů; porovnání probíhá konstantním časem.
9. `auth-full.src` vybere právě jeden `ACCESS_KEY` a je vstupem odvození i kanonizace; jeho změna změní `fullAuthKey` i tag. Neznámé `src` skončí obecným `401` po odvození s dummy klíčem a každý datový záznam musí mít stejné `src`.
10. Příjemce pro každou uspořádanou dvojici `src` a `dst` jednoznačně vybere právě jeden pevný `PAYLOAD_KEY`; relay tuto mapu nemá.
11. Změna `messageId`, směrovacího pole `src` nebo `dst`, IV, ciphertextu či tagu zneplatní GCM ověření, protože routingová pole zůstávají součástí AAD.
12. `info` salt má 32 bajtů, IV 12 bajtů a GCM tag 16 bajtů; dvě odpovědi mají nové náhodné hodnoty.
13. Klient neparsuje plaintext `info` ani nepředá payload před úspěšným ověřením GCM tagu.
14. Každý přímý datový řádek odpovědi se samostatně ověří pomocí AES-GCM; řádek s neplatným GCM tagem se zahodí bez ovlivnění ostatních platných řádků.

### 16.4 Fronty, souběh a pády

1. Atomická publikace odolá pádu před a po `rename()` bez částečné dávky.
2. Dva workery posílající stejnému cíli neztratí ani nespojí dávku.
3. Odpověď atomicky vyprázdní všechny aktuálně buffered záznamy a vrátí je jako přímé řádky `data`, bez stránkování a bez obálky; jejich počet může překročit 16.
4. Za datovými řádky následuje právě jeden `data-result` s přesnou uzavřenou sadou výsledkových polí; prázdný buffer vrátí pouze tento řádek.
5. Pád workeru nebo přerušení spojení po `drainAll` vede ke ztrátě odstraněných dat a následující poll je znovu nevrátí.
6. Záznamy zapsané souběžně až po atomickém `drainAll` zůstanou v bufferu pro další poll a nesmí se ztratit.
7. Jedna plná cílová fronta nezruší již přijaté položky pro jiný cíl a výsledek je přesný.
8. 16MiB limit započítává pouze dosud nevydané buffered záznamy; po zaplnění se vrací `429`.
9. Dávka starší než 60 s není vydána operací `drainAll`.
10. Storage lock skončí po 200 ms řízenou chybou, nikoli blokací workeru.
11. Stejná sada kontraktních testů běží nad každým storage driverem.

### 16.5 Statistiky klientů

1. Ovladač `disk` čte jediný `STORAGE_ROOT/clients.json`; v per-client adresářích žádný statistický soubor nevytváří.
2. Neexistující soubor se při způsobilé aktualizaci vytvoří ve správném JSON schématu s jediným aktuálním klientem.
3. Platný `lastSeen` starý přesně 30 sekund ani čas v budoucnosti zápis nevyvolá; hodnota stará 31 sekund se aktualizuje na aktuální UTC čas.
4. Chybějící nebo neplatný `lastSeen` vyvolá jediný pokus o aktualizaci. Poškozený JSON se nahradí minimálním validním dokumentem; nečitelný soubor se přeskočí bez zápisu.
5. Selhání čtení, JSON dekódování nebo zápisu nezmění HTTP status, `data-result`, publikování ani `drainAll`; zejména nevyvolá `500` ani `503`.
6. Dva souběžné workery nepoužijí zámek ani retry; výsledek last-writer-wins a případná ztráta jedné statistické aktualizace jsou povolené.
7. Přímý HTTP i HTTPS požadavek na `clients.json` a jiné soubory storage rootu je znemožněn. `info` při nečitelných statistikách vrátí prázdné `clients` bez interní cesty nebo diagnostiky.

### 16.6 Omezená ochrana proti replay

1. Dvojí odeslání identického platného requestu uvnitř 60sekundového okna vytvoří dvě zpracování.
2. Tentýž request se stářím 61 s server odmítne obecným `401`; timestamp se nepersistuje a nevznikne nonce ani replay databáze.
3. Dvojí doručení stejné platné odpovědi vede k opětovnému ověření jednotlivých GCM tagů a payload je předán dvakrát.
4. Opakované `messageId`, IV a autentizační HMAC tag nejsou porovnávány s historií.
5. Replay uvnitř okna může zaplnit frontu, ale nemůže bez klíče změnit chráněný obsah nebo vytvořit nový platný tag.
6. Zdrojový strom a runtime storage po testech neobsahují adresář, tabulku ani soubor určený pro nonce/replay cache.

## 17. Kontrolní seznam shody

Implementace je kompatibilní s phpvpn v1 pouze tehdy, když:

- podporuje všechny čtyři endpointy a přesné typy zpráv;
- používá normativní kanonizaci, oddělené klíče a délky AES-GCM;
- ověřuje autentizaci před jakoukoli změnou storage;
- vyžaduje v `auth-semi` i `auth-full` přesný UTC string `YYYY-MM-DDTHH:MM:SSZ`, zahrnuje jej v normativním pořadí do HMAC `tag` a odmítá absolutní odchylku větší než 60 s bez perzistence timestampu;
- odvozuje klíč HMAC tagu `auth-full` normativním spojením jediného `SERVER_SECRET`, jediného `ACCESS_KEY` vybraného podle `src` a samotného `src`, takže správný tag vyžaduje znalost obou tajemství;
- vrací všechny aktuálně buffered záznamy přímo jako `data` bez obálky a bez stránkování, zakončené jediným `data-result`;
- používá atomické at-most-once vyprázdnění celého bufferu bez potvrzení, zachovává TTL a přesné částečné výsledky odesílání;
- vede centrální best-effort `clients.json` v kořeni diskového storage, omezuje zápis `lastSeen` na hodnoty starší než 30 sekund a nikdy kvůli statistice nemění datový výsledek;
- znemožňuje přímý webový přístup k bufferům, statistikám a ostatním souborům storage rootu;
- uplatňuje stejné chování přes HTTP, HTTPS a proxy;
- mimo kontrolu stáří timestampu neimplementuje serverovou ani klientskou replay deduplikaci a nepersistuje timestamp ani nonce;
- udržuje jediný pevný payload klíč pro každou uspořádanou dvojici `src` a `dst` mimo relay server;
- respektuje zveřejněné aktivní limity bez ohledu na použitý storage driver;
- neprozrazuje tajemství ani interní cesty v odpovědích a logu.
