# Drátový protokol phpvpn v1

Tento dokument je normativní specifikací protokolu phpvpn v1. Klíčová slova **MUSÍ**, **NESMÍ**, **MĚL BY**, **NEMĚL BY** a **SMÍ** se vykládají ve významu RFC 2119.

## 1. Účel a rozsah

phpvpn je krátce připojovaný store-and-forward relay pro přenos end-to-end šifrovaných IPv4 paketů mezi klienty. Server:

- autentizuje požadavky a oprávnění odesílatele;
- kontroluje syntaxi, směrovací metadata, limity a kapacitu front;
- ukládá neprůhledné šifrované záznamy do front cílových klientů;
- vydává čekající záznamy a zpracovává potvrzení jejich převzetí;
- eviduje čas poslední úspěšné komunikace klientů.

Server nezná klíče užitečného zatížení, obsah IPv4 paketů nedešifruje a neinterpretuje. Distribuce payload klíčů, konfigurace virtuální síťové karty, přidělování VPN adres a zpracování dešifrovaného IP provozu jsou odpovědností klientské vrstvy mimo tento protokol.

Protokol je navržen pro PHP 8.2+, více souběžných PHP workerů a úložiště s omezenými prostředky. Nevyžaduje dlouhé spojení, long-polling, WebSocket, cron ani serverovou session.

### 1.1 Bezpečnostní cíle

Protokol poskytuje:

- autentizaci požadavků `info` znalostí serverového tajemství;
- autentizaci požadavků `data` znalostí přístupového klíče konkrétního klienta;
- integritu autentizovaného požadavku včetně přesných bajtů jeho těla;
- end-to-end důvěrnost a integritu payloadu pomocí AES-256-GCM;
- důvěrnost a integritu obsahu odpovědi `info` pomocí AES-256-GCM;
- oddělení klíčů podle účelu pomocí HKDF-SHA-256.

php implementace serveru by v základu neměla vyžadovat žádné rozšíření základního instalačního balíčku PHP. Tedy ani OpenSSL. Algoritmus AES-256-GCM je implementovaný softwarově.

### 1.2 Vědomě neposkytované vlastnosti

Protokol pomocí autentizačního timestampu odmítá požadavky starší než povolené 60sekundové časové okno. Nezabrání však opakování platné zprávy uvnitř tohoto okna a neposkytuje exactly-once ani garantované doručení, ochranu proti zahození nebo zdržení zprávy ani skrytí provozních metadat. Úspěšná odpověď `data` není jako celek autentizovaná: AES-GCM chrání jednotlivé payloady a jejich směrovací AAD, nikoli úplnost odpovědi, pořadí řádků ani `data-result` a `receiptId`. Protokol nezajišťuje ani dostupnost při zaplnění úložiště nebo vyčerpání PHP workerů.

## 2. Normativní rozhodnutí

| Oblast | Rozhodnutí v1 |
|---|---|
| Architektura | Bezstavové, krátké HTTP požadavky; žádná serverová session. |
| Wire format | JSON pro řídicí akce, NDJSON pro datovou akci. |
| Doručení | At-least-once s explicitním potvrzením celého vráceného snapshotu. Duplicita je možný a akceptovaný výsledek. |
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

1. `SERVER_SECRET`: právě jedna konstanta v konfigurační hlavičce PHP souboru, kterou znají server a oprávnění klienti. Výchozí hodnota je 32 nulových bajtů a správce ji má nahradit bezpečnou náhodnou hodnotou. Slouží pro `auth-semi` a šifrování `info`.
2. `ACCESS_KEY`: právě jeden 32bajtový náhodný klíč unikátní pro jednoho klienta. Zná jej pouze server a daný klient a používá se přímo jako klíč HMAC požadavku `auth-full`. Server jej vyhledá v mapě `ACCESS_KEYS` podle VPN adresy `src`; samotný klíč ani jeho identifikátor se nikdy nepřenáší.
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
- NESMÍ zapisovat tajemství, podpisy, ciphertext, tagy nebo celé requesty do logu.

V URL je pouze `?action=<akce>`. Identita klienta, tokeny, podpisy a šifrovaný obsah patří výhradně do těla. Proxy musí předat tělo bajtově beze změny; transformace konců řádků, dekomprese na jiný obsah nebo přepis JSON zneplatní HMAC.

### 4.2 Hranice ochrany při HTTP

Čisté HTTP je platný a podporovaný režim. Aplikační kryptografie v něm chrání tajné hodnoty a autenticitu v rozsahu popsaném tímto dokumentem, nenahrazuje však zabezpečený transport.

Pasivní pozorovatel HTTP spojení vidí URL, metodu, velikosti, časování, VPN adresy `src` a `dst`, identifikátory zpráv a objem provozu. Nevidí plaintext payloadu ani plaintext odpovědi `info`.

Aktivní útočník na HTTP trase může požadavky a odpovědi kopírovat, opakovat, zdržet, zahodit, přeskupit, měnit nebo spojení ukončit. Nemůže bez znalosti příslušného klíče vytvořit nový platný HMAC požadavku ani GCM tag jednotlivého payloadu. Odpověď `data` však nemá vlastní MAC: útočník může odstranit nebo přeskupit platné datové řádky a změnit, odstranit či podvrhnout `data-result` včetně `receiptId`. Klient tak v režimu HTTP nemůže kryptograficky ověřit úplnost vráceného bufferu ani správnost výsledku přijetí; podvržený výsledek nebo potvrzení neúplné odpovědi může způsobit ztrátu dat. Autentizovaný požadavek lze úspěšně opakovat pouze uvnitř jeho 60sekundového časového okna; odpověď nemá samostatnou kontrolu stáří.

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
- `timestamp` je aktuální UTC čas ve formátu RFC 3339 s přesností na sekundy. Klient jej před autentizací převede na Unix čas a použije pro korekci vlastního časového offsetu.

Pořadí členů JSON objektu není sémantické. Klient musí před prvním autentizovaným požadavkem získat čas serveru přes `health`; stejné srovnání zopakuje po `401`, pokud může být příčinou odchylka hodin.

## 6. Společný wire formát

### 6.1 JSON

- Kódování je UTF-8 bez BOM.
- Protokolová pole používají pouze ASCII.
- JSON objekt NESMÍ obsahovat duplicitní názvy členů.
- Neznámé pole v autentizačním, datovém, potvrzovacím nebo výsledkovém záznamu je chyba `400 bad_request`.
- Číselná pole jsou nezáporná celá čísla v bezpečném rozsahu PHP integeru.
- Base64 hodnoty používají base64url abecedu `A-Z a-z 0-9 _ -` bez `=` paddingu.
- Binární porovnání HMAC a tagů se provádí konstantním časem.

PHP implementace musí duplicitní top-level klíče odhalit před nebo během dekódování; samotné `json_decode()` je tiše přepíše a není pro tuto kontrolu dostačující.

### 6.2 NDJSON

NDJSON je posloupnost kompaktních JSON objektů. Každý záznam včetně posledního MUSÍ končit jedním bajtem LF (`0x0A`). CRLF, prázdný řádek a fragment bez závěrečného LF jsou neplatné. Velikosti se počítají z přijatých bajtů, nikoli z dekódovaného JSON.

### 6.3 Identifikátory

`requestId`, `messageId` a `receiptId` jsou UUID verze 4 v malých hexadecimálních znacích ve tvaru `8-4-4-4-12`. Generují se CSPRNG.

Identifikátory slouží ke korelaci a potvrzení. Server ani klient je NESMÍ používat jako replay cache nebo deduplikační klíč napříč požadavky. `messageId` musí být unikátní pouze uvnitř jednoho těla `data`, aby byla odpověď jednoznačná; opakování celého požadavku se stejnými identifikátory je znovu zpracováno.

## 7. Kryptografické primitivy a kanonizace

Povinné algoritmy v1 jsou SHA-256, HMAC-SHA-256, HKDF-SHA-256 podle RFC 5869 a AES-256-GCM s 16bajtovým tagem.

Všechny textové kanonické řetězce jsou ASCII/UTF-8, hodnoty jsou oddělené jedním LF a řetězec nekončí LF, není-li uvedeno jinak. `SHA256HEX(x)` je SHA-256 nad přesnými bajty `x`, zapsaný 64 malými hexadecimálními znaky. `HMAC(key, x)` se na drát zapisuje base64url bez paddingu.

### 7.1 Klíč `auth-semi`

```text
salt    = SHA-256("phpvpn/auth-semi")               // 32 raw bytes
info    = "phpvpn/auth-semi"
semiKey = HKDF-SHA-256(SERVER_SECRET, salt, info, 32)
```

## 8. Akce `info`

### 8.1 Požadavek `auth-semi`

Content-Type je `application/json; charset=utf-8`. Tělo obsahuje právě jeden JSON objekt a žádné další bajty:

```json
{"type":"auth-semi","timestamp":1786306530,"requestId":"4e94de3a-b9ad-4bc3-9297-82b98a19a143","signature":"BASE64URL"}
```

Podpis se vypočte:

```text
canonical = "PHPVPN-AUTH-SEMI" LF
            "POST" LF
            "info" LF
            DECIMAL(timestamp) LF
            requestId

signature = HMAC-SHA-256(semiKey, canonical)
```

`timestamp` je Unix čas v celých sekundách jako nezáporné JSON integer číslo bez zlomku a exponentu. V kanonickém vstupu se zapíše jeho minimální desítková reprezentace bez znaménka a vodicích nul. Server přijme požadavek pouze tehdy, když `abs(serverUnixTime - timestamp) <= 60`; jinak vrátí obecné `401 unauthorized`.

`requestId` je korelační hodnota. Server nepersistuje `timestamp` ani `requestId`. Timestamp omezuje replay starého požadavku, ale identický platný požadavek lze uvnitř otevřeného 60sekundového okna zopakovat a server jej přijme znovu.

### 8.2 Plaintext odpovědi

Server sestaví jeden kompaktní JSON objekt. Minimální obsah je:

```json
{"serverVersion":"1.0.0","generatedAt":"2026-08-09T20:15:30Z","clients":[{"address":"10.99.99.2","lastSeen":"2026-08-09T20:14:52Z"}],"limits":{"maxSerializedDataRecordBytes":262144,"maxDataRecordsPerRequest":16,"maxAckRecordsPerRequest":64,"maxRequestBodyBytes":4214784,"maxQueueBytesPerClient":16777216,"queueTtlSeconds":60}}
```

`clients` obsahuje pouze VPN adresu a poslední úspěšnou komunikaci. Nesmí obsahovat token, secret, fyzickou IP, cestu na disku ani obsah fronty. Klient, který ještě nekomunikoval, může být vynechán. `lastSeen` je UTC RFC 3339.

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
2. nula až `MAX_ACK_RECORDS_PER_REQUEST` záznamů `ack`;
3. nula až `MAX_DATA_RECORDS_PER_REQUEST` záznamů `data`.

Po prvním `data` již nesmí následovat `ack`. Samotný `auth-full` je platný polling bez potvrzení a bez odesílaných dat.

### 9.2 Záznam `auth-full`

```json
{"type":"auth-full","src":"10.99.99.2","timestamp":1786306530,"requestId":"69e82c2a-2732-466f-b9b1-f891084df639","signature":"BASE64URL"}
```

`signature` pokrývá všechny bajty za ukončovacím LF prvního řádku:

```text
body = exact bytes after the LF terminating auth-full

canonical = "PHPVPN-AUTH-FULL" LF
            "POST" LF
            "data" LF
            src LF
            DECIMAL(timestamp) LF
            requestId LF
            SHA256HEX(body)

signature = HMAC-SHA-256(ACCESS_KEY, canonical)
```

Server nejprve syntakticky ověří `src`, podle něj v konstantě `ACCESS_KEYS` vyhledá právě jeden `ACCESS_KEY` a tím přímo ověří HMAC. Musí dočíst a hashovat celé tělo. Před úspěšným ověřením HMAC NESMÍ zpracovat `ack`, změnit frontu ani publikovat data. Při neznámém klientovi nebo klíči musí provést srovnatelnou práci s náhodně vypadajícím 32bajtovým dummy klíčem a vrátit obecné `401 unauthorized`.

Pro `timestamp` platí stejný formát a kontrola `abs(serverUnixTime - timestamp) <= 60` jako u `auth-semi`. Timestamp je součástí HMAC, takže jej nelze změnit bez zneplatnění podpisu. Zajišťuje omezenou čerstvost vůči starým replayům; stejný autentizovaný požadavek lze uvnitř platného 60sekundového okna přehrát a server jej zpracuje znovu. Server timestamp nepersistuje.

### 9.3 Záznam `ack`

```json
{"type":"ack","receiptId":"4036dcf4-d800-44a9-8676-b4d730a5ca08"}
```

Klient posílá `ack` až po přijetí celé odpovědi, ověření všech vrácených payloadů a jejich předání vyšší vrstvě. Jediný `receiptId` potvrzuje všechny datové řádky vrácené v odpovědi, ve které jej server vydal. Neznámý, expirovaný nebo již potvrzený `receiptId` je úspěšný no-op. Tato idempotence mazání není replay deduplikací payloadu a nevyžaduje historii potvrzení.

### 9.4 Záznam `data`

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

### 9.5 End-to-end šifrování payloadu

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

### 9.6 Odpověď `data`

Úspěšná odpověď má Content-Type `application/x-ndjson; charset=utf-8` a pořadí:

1. nula nebo více přímo uložených záznamů `data`;
2. právě jeden závěrečný záznam `data-result`.

Server do odpovědi zahrne všechny neexpirované datové záznamy, které jsou v okamžiku atomického načtení připravené ve frontě klienta. Počet vrácených záznamů se neomezuje samostatným protokolovým ani konfiguračním limitem a odpověď se nestránkuje. Jedinou horní mezí objemu snapshotu je kapacita fronty. Každý záznam se vrací přímo ve stejném schématu `data`, bez další obálky:

```json
{"type":"data","messageId":"e8edc241-96ec-4b51-9336-1fb0836f4db5","src":"10.99.99.4","dst":"10.99.99.2","iv":"BASE64URL","ciphertext":"BASE64URL","tag":"BASE64URL"}
```

Záznam `data-result`:

```json
{"type":"data-result","requestId":"69e82c2a-2732-466f-b9b1-f891084df639","acceptedMessageIds":["d5b543a9-9fc3-4c98-916c-ea0198f67a79"],"deferredMessageIds":[],"acceptedRecords":1,"acceptedBytes":2048,"receiptId":"4036dcf4-d800-44a9-8676-b4d730a5ca08"}
```

- `acceptedMessageIds` jsou jediné položky, které smí odesílatel odstranit ze své odchozí fronty;
- `deferredMessageIds` zůstávají klientovi pro pozdější pokus;
- obě pole bez průniku přesně pokrývají všechny `messageId` v požadavku;
- `acceptedBytes` je součet přijatých serializovaných datových řádků včetně LF;
- `receiptId` je poslední člen objektu. Je-li v odpovědi alespoň jeden datový řádek, obsahuje čerstvé UUID a potvrzuje celý vrácený snapshot; nebyl-li vrácen žádný datový řádek, má hodnotu `null` a klient žádný `ack` neposílá.

Příklad výsledku pollingu bez odeslaných i bez vrácených dat:

```json
{"type":"data-result","requestId":"69e82c2a-2732-466f-b9b1-f891084df639","acceptedMessageIds":[],"deferredMessageIds":[],"acceptedRecords":0,"acceptedBytes":0,"receiptId":null}
```

Klient MUSÍ přijmout úplné NDJSON tělo s právě jedním závěrečným `data-result`, jehož `requestId` odpovídá aktuálnímu požadavku. Každý datový řádek musí samostatně ověřit podle AES-GCM pravidel v kapitole 9.5. Nenulové `receiptId` smí potvrdit až po úspěšném ověření a předání všech vrácených payloadů; pokud je odpověď neúplná, syntakticky neplatná nebo některý tag selže, `ack` neposílá a server může celý snapshot nabídnout znovu.

Odpověď jako celek nemá HMAC ani jinou ochranu úplnosti. `requestId`, výsledková pole a `receiptId` nejsou v režimu HTTP kryptograficky chráněné; bezpečnostní důsledky stanoví kapitola 4.2. Klient nevede replay cache a opakovaně doručené platné payloady znovu zpracuje.

## 10. Fronty a doručovací sémantika

### 10.1 Model

Každý cílový klient má logickou perzistentní frontu. Do její velikosti se započítávají připravené i právě vydané, ale dosud nepotvrzené záznamy. Výchozí limit je 16 MiB logických serializovaných bajtů na klienta bez interní režie backendu.

Server může z jednoho requestu vytvořit samostatné dávky podle cíle. Publikace jedné dávky musí být atomická: po pádu je viditelná celá, nebo vůbec. Úspěch jednoho cíle nezávisí na selhání jiného cíle.

Při sestavení odpovědi server atomicky načte všechny neexpirované záznamy, které jsou právě připravené pro autentizovaného klienta, a spojí je do jediného potvrzovaného snapshotu. Snapshot může obsahovat více dříve publikovaných dávek. Server jej nesmí stránkovat ani omezit počtem záznamů.

### 10.2 Částečné přijetí

Server zpracovává cílové skupiny deterministicky. Přijaté položky uvede v `acceptedMessageIds`, nepřijaté v `deferredMessageIds`. Pokud přijme alespoň jednu položku nebo požadavek žádnou neodesílá, vrací `200` a `data-result`. Pokud nelze přijmout žádnou odesílanou položku kvůli kapacitě, vrací `429 queue_full`; při nedostupnosti backendu `503 queue_unavailable`.

Chybová odpověď nikdy neopravňuje klienta odstranit položku z odchozí fronty. Při úspěchu klient vyhodnotí pouze jediný syntakticky platný závěrečný `data-result` se shodným `requestId`; v režimu HTTP však jeho správnost není kryptograficky zaručena.

### 10.3 Vydání, potvrzení a opakování

Server všechny záznamy právě vráceného snapshotu atomicky přesune do stavu `inflight` a přidělí jim jediné `receiptId`. Po platném `ack` celý snapshot odstraní. Pokud odpověď neobsahuje žádná data, nevytváří stav `inflight`, do `data-result.receiptId` zapíše `null` a klient nic nepotvrzuje. Při přerušení spojení, neplatné odpovědi nebo chybějícím potvrzení může server nepotvrzený snapshot znovu nabídnout. Opakované doručení je normální.

Klient ani server neporovnávají `messageId`, IV, request body nebo podpis s historií. Replay autentizovaného odesílacího requestu uvnitř platného 60sekundového okna založí další kopii dat; po uzavření okna server request odmítne. Replay doručovací odpovědi předá payload znovu. To může zaplnit frontu, zvýšit náklady nebo opakovat účinek UDP/aplikačních dat; jde o výslovně přijaté omezení.

### 10.4 Expirace

Výchozí `QUEUE_TTL_SECONDS` je 60 s. Čas běží od prvního úspěšného publikování a vydání do `inflight` jej neobnovuje. Po expiraci smí backend dávku smazat bez doručení. Údržba může být oportunistická, takže fyzické odstranění může nastat později; expirovaná dávka však již nesmí být nově vydána.

### 10.5 Storage driver

Wire protokol nesmí záviset na názvech souborů, adresářích ani databázové technologii. Implementace používá rozhraní s operacemi odpovídajícími alespoň:

- `enqueue(destination, records, expiresAt)`;
- `claimAll(client)`;
- `ack(client, receiptId)`;
- `usage(client)`;
- `lastSeen()` a `recordLastSeen(client)`;
- `sweep(now)`.

Povinný driver `disk` používá per-client adresáře, zámek s omezeným čekáním, zápis do dočasného souboru a atomické `rename()` ve stejném filesystému. Konfigurační volba `STORAGE_DRIVER` má výchozí hodnotu `disk`; budoucí `memcached`, `mariadb`, `mysql` nebo `postgresql` musí zachovat stejné atomické a kapacitní chování.

## 11. Limity a timeouty

### 11.1 Výchozí limity

| Konstanta | Výchozí hodnota | Význam |
|---|---:|---|
| `MAX_SERIALIZED_DATA_RECORD_BYTES` | 262144 B | Celý datový NDJSON řádek včetně LF. |
| `MAX_DATA_RECORDS_PER_REQUEST` | 16 | Počet záznamů `data` v jednom requestu. |
| `MAX_ACK_RECORDS_PER_REQUEST` | 64 | Počet záznamů `ack`. |
| `MAX_AUTH_RECORD_BYTES` | 4096 B | První řádek včetně LF. |
| `MAX_ACK_RECORD_BYTES` | 256 B | Jeden `ack` včetně LF. |
| `MAX_REQUEST_BODY_BYTES` | 4214784 B | `4096 + 64×256 + 16×262144`. |
| `MAX_INFO_PLAINTEXT_BYTES` | 262144 B | Plaintext šifrované odpovědi `info`. |
| `MAX_QUEUE_BYTES_PER_CLIENT` | 16777216 B | 16 MiB logické persistentní fronty. |
| `AUTH_TIMESTAMP_SKEW_SECONDS` | 60 s | Nejvyšší absolutní odchylka autentizačního timestampu od času serveru. |
| `QUEUE_TTL_SECONDS` | 60 s | Nejvyšší věk čekající dávky. |
| `LOCK_TIMEOUT_MS` | 200 ms | Nejvyšší čekání PHP workeru na storage zámek. |

Konfigurační invarianty musí zajistit, že `MAX_REQUEST_BODY_BYTES` pojme autentizační řádek a maximální počet maximálních záznamů. PHP `post_max_size`, limity proxy a webserveru pro požadavek musí být přísně vyšší než `MAX_REQUEST_BODY_BYTES`. Webserver a proxy musí umět bez stránkování přenést odpověď obsahující celou frontu o velikosti `MAX_QUEUE_BYTES_PER_CLIENT` plus závěrečný `data-result`; jinak server musí při diagnostice hlásit chybnou konfiguraci.

Překročení syntaktického limitu vrací `413 payload_too_large`. Zaplnění persistentní fronty vrací `429 queue_full` a nesmí být zaměněno za syntaktickou chybu.

### 11.2 Časové chování

Server odpovídá okamžitým snapshotem fronty. Nikdy nečeká na příchod nových dat, nepoužívá `sleep`, long-polling ani streaming neurčité délky.

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

### 12.2 Stav dávky

```text
neexistuje --enqueue--> ready --claimAll--> inflight --ack--> smazána
                              |                 |
                              | TTL             | TTL / reclaim
                              v                 v
                           smazána          ready nebo smazána
```

`claimAll` přesune do jednoho snapshotu všechny právě připravené záznamy a nesmí změnit jejich původní časy expirace. Pád během `enqueue` nesmí publikovat částečnou dávku. Pád po `claimAll` a před `ack` může vést k opakovanému doručení celého snapshotu.

## 13. Chyby

Chybové tělo je malý obecný JSON objekt:

```json
{"type":"error","code":"unauthorized"}
```

| HTTP | `code` | Význam | Reakce klienta |
|---:|---|---|---|
| 400 | `bad_request` | Neplatný JSON/NDJSON, pořadí, pole, hodnota nebo duplicitní klíč. | Opravit request; neopakovat beze změny. |
| 401 | `unauthorized` | Neplatný podpis, `src`, `ACCESS_KEY`, podvržené `src` v datovém záznamu nebo timestamp mimo povolené okno. | Obnovit čas přes `health`, ověřit konfiguraci; nemaž data. |
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
- `InfoHandler` a `DataHandler`;
- `Maintenance`: omezený oportunistický sweep expirací a statistik.

Serverová implementace potřebuje `hash_hmac`, SHA-256, `random_bytes`, striktní JSON a pro `info` AES-256-GCM. Doporučená realizace používá `hash_hkdf()` a `openssl_encrypt(..., 'aes-256-gcm', ...)`. Chybí-li bezpečná AES-GCM implementace, `info` vrací `503 server_misconfigured`; datový relay může zůstat funkční, protože payload nedešifruje. Vlastní neauditovaná implementace AES se nedoporučuje.

Tělo požadavku `data` může mít přibližně 4,02 MiB. Implementace jej smí streamovat a průběžně hashovat, ale všechny změny storage musí odložit až po ověření `auth-full`. Odpověď může obsahovat celou frontu klienta, tedy až přibližně `MAX_QUEUE_BYTES_PER_CLIENT` plus `data-result`. Bezpečnou volbou je validované záznamy dočasně držet v omezené paměti; `memory_limit` musí mít dostatečnou rezervu pro request, celý snapshot odpovědi a JSON struktury.

Diskový driver musí používat cesty odvozené pouze z již validované VPN adresy, zamykání s timeoutem, atomickou publikaci a obranu proti zpřístupnění front přes webserver. Nemá používat neomezený globální zámek. Údržba po odpovědi musí mít časový a početní rozpočet.

Statistika `lastSeen` se aktualizuje pouze po platně autentizovaném `data` requestu s úspěšnou odpovědí. Selhání statistiky nesmí změnit úspěšné doručení.

## 16. Povinné testovací scénáře

### 16.1 Transport a směrování

1. Stejný request přes HTTP, HTTPS, přímé spojení a proxy vede ke stejnému aplikačnímu výsledku.
2. Podvržené `X-Forwarded-*` hlavičky neovlivní autentizaci, `src`, frontu ani odpověď.
3. URL s neznámou akcí a nepovolená metoda vrátí správné obecné chyby.
4. Root stránka zobrazí H1; při výchozích 32 nulových bajtech i při prázdném nebo jinak pouze nulovém `SERVER_SECRET` navíc zobrazí červený H2, přičemž `info` a `data` zůstávají funkční.
5. `health` má čtyři povinná pole včetně `serverVersion`, status `running`, description `OK` a UTC timestamp; klient z něj před autentizací správně odvodí offset vůči Unix času serveru.

### 16.2 JSON, NDJSON a limity

1. Validní prázdný poll obsahuje pouze `auth-full` s LF.
2. Chybějící poslední LF, CRLF, prázdný řádek, duplicitní klíč a neznámé pole jsou odmítnuty.
3. Přesně 262144bajtový datový řádek projde, o bajt delší vrátí `413`.
4. Přesně 16 datových záznamů projde, sedmnáctý vrátí `413` bez změny storage.
5. Hrany pro ack, celé tělo a frontu jsou testovány na `limit-1`, `limit` a `limit+1`.
6. Neplatná base64url, délka IV, délka tagu, UUID, VPN adresa a neznámé nebo nadbytečné pole jsou odmítnuty.
7. Úspěšná odpověď obsahuje nula nebo více přímých řádků `data` a právě jeden poslední `data-result`; další řádek za výsledkem je neplatný.

### 16.3 Kryptografie

1. Known-answer testy pro HKDF-SHA-256, HMAC-SHA-256 a AES-256-GCM.
2. `auth-semi` i `auth-full` nesou celočíselný Unix `timestamp`; změna timestampu nebo kteréhokoli dalšího členu kanonického requestu či jediného bajtu body zneplatní HMAC.
3. Server přijme odchylky přesně `-60`, `0` a `+60` s a odmítne `-61` a `+61` s; po zpracování nevytvoří timestamp ani nonce záznam.
4. `auth-full.src` vybere právě jeden `ACCESS_KEY`; neznámé `src` skončí obecným `401` s výpočtem nad dummy klíčem a každý datový záznam musí mít stejné `src`.
5. Příjemce pro každou uspořádanou dvojici `src` a `dst` jednoznačně vybere právě jeden pevný `PAYLOAD_KEY`; relay tuto mapu nemá.
6. Změna `messageId`, směrovacího pole `src` nebo `dst`, IV, ciphertextu či tagu zneplatní GCM ověření, protože routingová pole zůstávají součástí AAD.
7. `info` salt má 32 bajtů, IV 12 bajtů a tag 16 bajtů; dvě odpovědi mají nové náhodné hodnoty.
8. Klient neparsuje plaintext `info` ani nepředá payload před úspěšným ověřením tagu.
9. Každý přímý datový řádek odpovědi se samostatně ověří pomocí AES-GCM; neplatný tag zabrání potvrzení celého snapshotu.

### 16.4 Fronty, souběh a pády

1. Atomická publikace odolá pádu před a po `rename()` bez částečné dávky.
2. Dva workery posílající stejnému cíli neztratí ani nespojí dávku.
3. Odpověď vrátí všechny právě připravené záznamy jako přímé řádky `data`, bez stránkování a bez obálky; jejich počet může překročit 16.
4. Neprázdná odpověď má v posledním poli `data-result` jediné nenulové `receiptId`, které potvrzuje celý snapshot; prázdná odpověď má `receiptId:null` a klient neposílá `ack`.
5. Potvrzený snapshot zmizí; neznámý a opakovaný `ack` je no-op. Přerušení po doručení a před `ack` vede k možnému opakovanému doručení celého snapshotu.
6. Jedna plná cílová fronta nezruší již přijaté položky pro jiný cíl a výsledek je přesný.
7. 16MiB limit započítává `ready` i `inflight`; po zaplnění se vrací `429`.
8. Dávka starší než 60 s není vydána; `claimAll` její expiraci neprodlouží.
9. Storage lock skončí po 200 ms řízenou chybou, nikoli blokací workeru.
10. Stejná sada kontraktních testů běží nad každým storage driverem.

### 16.5 Omezená ochrana proti replay

1. Dvojí odeslání identického platného requestu uvnitř 60sekundového okna vytvoří dvě zpracování.
2. Tentýž request se stářím 61 s server odmítne obecným `401`; timestamp se nepersistuje a nevznikne nonce ani replay databáze.
3. Dvojí doručení stejné platné odpovědi vede k opětovnému ověření jednotlivých GCM tagů a payload je předán dvakrát.
4. Opakované `messageId`, IV a podpis nejsou porovnávány s historií.
5. Replay uvnitř okna může zaplnit frontu, ale nemůže bez klíče změnit chráněný obsah nebo vytvořit nový platný tag.
6. Zdrojový strom a runtime storage po testech neobsahují adresář, tabulku ani soubor určený pro nonce/replay cache.

## 17. Kontrolní seznam shody

Implementace je kompatibilní s phpvpn v1 pouze tehdy, když:

- podporuje všechny čtyři endpointy a přesné typy zpráv;
- používá normativní kanonizaci, oddělené klíče a délky AES-GCM;
- ověřuje autentizaci před jakoukoli změnou storage;
- vyžaduje podepsaný Unix timestamp v `auth-semi` i `auth-full` a odmítá absolutní odchylku větší než 60 s bez jeho perzistence;
- vrací všechny právě připravené záznamy přímo jako `data` bez obálky a bez stránkování, zakončené jediným `data-result`;
- zachovává at-least-once frontu, jedno potvrzení celého neprázdného snapshotu, TTL a přesné částečné výsledky;
- uplatňuje stejné chování přes HTTP, HTTPS a proxy;
- mimo kontrolu stáří timestampu neimplementuje serverovou ani klientskou replay deduplikaci a nepersistuje timestamp ani nonce;
- udržuje jediný pevný payload klíč pro každou uspořádanou dvojici `src` a `dst` mimo relay server;
- respektuje zveřejněné aktivní limity bez ohledu na použitý storage driver;
- neprozrazuje tajemství ani interní cesty v odpovědích a logu.
