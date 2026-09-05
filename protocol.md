# Drátový protokol phpvpn v1

Tento dokument je normativní specifikací protokolu phpvpn v1. Klíčová slova **MUSÍ**, **NESMÍ**, **MĚL BY**, **NEMĚL BY** a **SMÍ** se vykládají ve významu RFC 2119.

Protokol je veřejně známý. Softwarová implementace phpvpn je open source a je zveřejněna pod licencí MIT.

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

- autenticitu času a provozních polí odpovědi `health` vůči klientovi znalému `SERVER_SECRET`;
- autentizaci požadavků `info` znalostí serverového tajemství;
- autentizaci požadavků `data` současnou znalostí jediného serverového tajemství a přístupového klíče konkrétního klienta;
- integritu autentizovaného požadavku včetně přesných bajtů jeho těla;
- end-to-end důvěrnost a integritu payloadu pomocí AES-256-GCM;
- důvěrnost a integritu obsahu odpovědi `info` pomocí AES-256-GCM;
- autenticitu výsledku akce `data` a integritu pořadí, počtu a úplnosti jejích datových řádků pomocí HMAC-SHA-256;
- oddělení klíčů podle účelu pomocí HKDF-SHA-256.

PHP implementace serveru i PHP implementace klienta musí všechny operace AES-256-GCM vždy provádět přibalenou softwarovou implementací v čistém PHP. Pro AES-256-GCM NESMÍ použít OpenSSL, Sodium ani jiné kryptografické rozšíření, a to ani tehdy, když je dostupné. Dostupnost volitelných rozšíření proto nemění funkce ani chování implementace.

### 1.2 Vědomě neposkytované vlastnosti

Protokol pomocí autentizačního timestampu odmítá požadavky starší než povolené 60sekundové časové okno. Nezabrání však opakování platné zprávy uvnitř tohoto okna a neposkytuje exactly-once ani garantované doručení, ochranu proti zahození nebo zdržení zprávy ani skrytí provozních metadat. Úspěšná odpověď `data` autentizuje pomocí HMAC svůj `data-result` a uspořádaný seznam dvojic `messageId` a GCM tag, takže klient ověří počet, pořadí a úplnost datových řádků bez hashování jejich ciphertextu. Obsah každého datového řádku nadále chrání jeho vlastní AES-GCM tag. Tato ochrana nezabrání zahození nebo replay celé platné odpovědi. Server používá vědomě zvolený optimistický at-most-once model: data odstraní z bufferu před odesláním odpovědi a při ztrátě nebo nezpracování odpovědi je znovu nenabídne.

V režimu čistého HTTP může útočník odposlechnout platný pollingový požadavek `data` a uvnitř jeho 60sekundového okna jej beze změny přehrát. Server takový replay znovu autentizuje, atomicky vyprázdní příchozí frontu oběti a vrátí její záznamy útočníkovi. Útočník bez `PAYLOAD_KEY` jejich plaintext nepřečte, ale záznamy z relay nenávratně odstraní dříve, než je získá legitimní klient. Opakovaným odposlechem nových pollingových požadavků může tímto způsobem trvale krást ciphertexty a ničit příchozí frontu oběti. Tento důsledek je známý, protokol v1 s ním výslovně počítá a riziko vědomě přijímá.

Přijetí rizika vychází z předpokladu, že klientská, transportní nebo aplikační vrstva si chybějící komunikaci typicky vyžádá znovu nebo vyvolá nové odeslání, takže vzniknou nové IP pakety a pokus o doručení se opakuje. Nejde však o záruku protokolu: například UDP nebo jednorázový provoz bez vlastní obnovy může být ztracen definitivně a soustavný replay může bránit doručení i opakovaných pokusů. V1 kvůli tomuto riziku nezavádí nonce, replay cache, potvrzované vydání ani obnovu vyprázdněné fronty. Protokol nezajišťuje ani dostupnost při tomto útoku, zaplnění úložiště nebo vyčerpání PHP workerů.

## 2. Normativní rozhodnutí

| Oblast | Rozhodnutí v1 |
|---|---|
| Architektura | Bezstavové, krátké HTTP požadavky; žádná serverová session. |
| Wire format | JSON pro řídicí akce, NDJSON pro datovou akci. |
| Doručení | Optimistické at-most-once na úrovni serverového bufferu, bez potvrzení. Po atomickém načtení jsou data ze serveru odstraněna ještě před přenosem odpovědi. |
| Čas serveru | Klient posílá neautentizovaný `auth-none` s čerstvým `requestId`; server vrací veřejný `health` autentizovaný GCM tagem odvozeným ze `SERVER_SECRET`. |
| Replay | Timestamp omezuje platnost autentizovaného požadavku na ±60 s vůči času serveru. Uvnitř okna není replay detekován; bez deduplikace a bez perzistence timestampu nebo nonce. Replay odposlechnutého HTTP pollingu může vyprázdnit a nenávratně zničit příchozí frontu oběti; v1 toto riziko vědomě přijímá. |
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

1. `SERVER_SECRET`: právě jedna konstanta v konfigurační hlavičce PHP souboru, kterou znají server a oprávnění klienti. Výchozí hodnota je řetězec `"00000000000000000000000000000000"`, tedy přesně 32 ASCII znaků `0`, a správce ji má nahradit bezpečnou náhodnou hodnotou. Slouží pro `auth-semi`, šifrování `info` a odvození autentizačního klíče `auth-full`.
2. `ACCESS_KEY`: právě jeden 32bajtový náhodný klíč unikátní pro jednoho klienta. Zná jej pouze server a daný klient a používá se jako vstupní tajemství při odvození autentizačního klíče `auth-full`. Server jej vyhledá v mapě `ACCESS_KEYS` podle VPN adresy `src`; samotný klíč ani jeho identifikátor se nikdy nepřenáší.
3. `PAYLOAD_KEY`: právě jeden pevný 32bajtový end-to-end klíč pro každou uspořádanou dvojici VPN adres (`src`, `dst`). Znají jej pouze klienti daného směru komunikace; relay server jej NESMÍ mít. Distribuce je mimo protokol.

Každá implementace serveru i klienta při startu normalizuje `SERVER_SECRET` ještě před validací konfigurace a před první kryptografickou operací. Je-li nakonfigurovaná hodnota přesně prázdný řetězec o délce nula bajtů, implementace ji za běhu nahradí řetězcem `"00000000000000000000000000000000"`, tedy 32 bajty `0x30`. Všude dále v tomto dokumentu označuje `SERVER_SECRET` tuto efektivní normalizovanou hodnotu. Jakákoli neprázdná hodnota se ponechá beze změny; zejména řetězec `"0"` ani hodnota tvořená nulovými bajty `0x00` se nenormalizují.

Kryptografické operace používají přesné bajty efektivního `SERVER_SECRET` bez implicitního hex nebo base64 dekódování. Prázdný nakonfigurovaný řetězec, hodnota tvořená pouze nulovými bajty `0x00` i řetězec tvořený pouze ASCII znaky `0` jsou platné vstupy a autentizované endpointy je nesmí odmítnout jen kvůli jejich obsahu; taková konfigurace však neposkytuje praktické utajení a veřejná stránka na ni upozorňuje. Každý `ACCESS_KEY` se zapisuje jako přesně 64 malých hexadecimálních znaků a pro kryptografii se dekóduje na 32 bajtů. Klienti vedou lokální mapu `PAYLOAD_KEYS[src][dst]`; každá její hodnota se pro AES-256-GCM dekóduje na přesně 32 bajtů.

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

Aktivní útočník na HTTP trase může požadavky a odpovědi kopírovat, opakovat, zdržet, zahodit, přeskupit, měnit nebo spojení ukončit. Nemůže bez znalosti příslušného klíče vytvořit nový platný HMAC požadavku, HMAC odpovědi `data` ani GCM tag jednotlivého payloadu. Závěrečný `data-mac` proto odhalí odstranění, přidání nebo přeuspořádání datových řádků i jakoukoli změnu `data-result`; změnu obsahu jednotlivého datového záznamu se zachovaným GCM tagem odhalí jeho následné AES-GCM ověření. Útočník stále může zahodit celou odpověď, přerušit spojení nebo zopakovat celou dříve platnou odpověď se shodným aktuálním `requestId`. Protože server záznamy před odesláním odstraní, nedoručení odpovědi znamená jejich nevratnou ztrátu na úrovni relay. Autentizovaný požadavek lze úspěšně opakovat pouze uvnitř jeho 60sekundového časového okna; odpověď nemá samostatnou kontrolu stáří.

Požadavek `auth-none` sám není autentizovaný a útočník jej může číst, měnit nebo opakovat. Odpověď `health` však váže serverový UTC čas a všechna ostatní provozní pole k čerstvému klientskému `requestId` pomocí GCM tagu odvozeného ze `SERVER_SECRET`. Útočník bez tohoto tajemství nemůže podstrčit budoucí timestamp ani změnit legitimní odpověď tak, aby ji klient přijal. Starou odpověď pro jiný `requestId` klient odmítne. Útočník může požadavek či odpověď zdržet nebo zahodit, což může způsobit zastaralý čas nebo nedostupnost, nikoli však nepozorovaný posun klientových podpisů do budoucnosti.

Pro útok na pollingovou frontu útočník nemusí změnit provoz na trase ani znát klíč. Stačí mu pasivně zachytit platný HTTP požadavek `data`, získanou kopii během otevřeného okna sám odeslat serveru a tento postup opakovat s novými požadavky oběti. Každá úspěšná kopie provede běžné at-most-once vyprázdnění fronty ve prospěch spojení útočníka. Tím lze dlouhodobě odebírat všechny nově čekající záznamy a legitimnímu klientovi ponechávat prázdnou frontu. Jde o výslovně přijaté riziko čistého HTTP podle kapitoly 1.2, nikoli o vlastnost, kterou by v1 detekovala nebo napravovala.

HTTPS navíc poskytuje ochranu transportních metadat v těle a hlavičkách, autentizaci TLS protistrany a ochranu spojení proti změně. Zda jej administrátor použije, je provozní rozhodnutí mimo povinné požadavky protokolu.

## 5. Endpointy

| URL | Metoda | Požadavek | Úspěšná odpověď |
|---|---|---|---|
| `/phpvpn.php` | `GET`, `HEAD` | bez těla | HTML provozní stránka |
| `/phpvpn.php?action=health` | `POST` | jeden JSON objekt `auth-none` | jeden autentizovaný JSON objekt `health` |
| `/phpvpn.php?action=info` | `POST` | jeden JSON objekt `auth-semi` | jeden JSON objekt `info` |
| `/phpvpn.php?action=data` | `POST` | NDJSON začínající `auth-full` | NDJSON s datovými záznamy a výsledkem |

Jiné metody vrací `405 method_not_allowed` s hlavičkou `Allow`. Neznámá akce vrací `404 not_found`.

### 5.1 Provozní stránka

`GET /phpvpn.php` vrací jednoduché HTML s `<h1>Server is running</h1>`. Byla-li nakonfigurovaná hodnota `SERVER_SECRET` prázdná nebo je-li jeho efektivní hodnota tvořena pouze nulovými bajty `0x00` či pouze ASCII znaky `0`, stránka navíc zobrazí červený `<h2>Server is not secure. Please modify SERVER_SECRET in the phpvpn.php</h2>`.

Tato veřejná stránka provádí pouze uvedenou kontrolu a neprozrazuje konfiguraci. Varování je informativní: prázdný nebo nulový `SERVER_SECRET` neblokuje `health`, `info` ani `data` a sám o sobě nesmí vést k `503 server_misconfigured`. Protože je však taková hodnota veřejně předvídatelná, neposkytuje tag odpovědi `health` praktický důkaz identity serveru.

### 5.2 `health`

`health` používá metodu `POST` a Content-Type `application/json; charset=utf-8`. Požadavek obsahuje právě jeden JSON objekt `auth-none` a žádné další bajty:

```json
{"type":"auth-none","requestId":"c22cd6e8-14a9-4d83-97b7-54c26a72d1f0"}
```

- `auth-none` je záměrně neautentizovaný typ požadavku;
- neobsahuje `timestamp`, `src`, `tag`, secret ani jiné pole a nedokládá znalost žádného tajemství;
- klient pro každý pokus generuje nový UUIDv4 `requestId` a nesmí přijmout odpověď náležející jinému požadavku;
- server po syntaktické validaci odpoví každému žadateli, nevede pro `auth-none` session ani persistentní stav a replay požadavku zpracuje jako nový samostatný požadavek.

Úspěšná odpověď má Content-Type `application/json; charset=utf-8` a právě tato pole:

```json
{"serverVersion":"1.0.0","status":"running","description":"OK","timestamp":"2026-08-09T20:15:30.123Z","requestId":"c22cd6e8-14a9-4d83-97b7-54c26a72d1f0","iv":"BASE64URL","tag":"BASE64URL"}
```

- `serverVersion` je verze serverové aplikace;
- `status` je při úspěchu `running`;
- `description` je při úspěchu `OK`;
- `timestamp` je aktuální UTC čas serveru s přesností na milisekundy, přesně ve formátu `YYYY-MM-DDTHH:MM:SS.sssZ`;
- `requestId` je přesná hodnota z odpovídajícího požadavku `auth-none`;
- `iv` je 12 nových náhodných bajtů z CSPRNG, zapsaných jako base64url bez paddingu;
- `tag` je 16bajtový AES-256-GCM autentizační tag zapsaný jako base64url bez paddingu a vypočtený podle kapitoly 7.3.

Pořadí členů JSON objektu není sémantické. Klient znalý `SERVER_SECRET` MUSÍ před použitím `timestamp` ověřit, že jde o platný 24znakový UTC čas ve formátu `YYYY-MM-DDTHH:MM:SS.sssZ`, dále ověřit `requestId`, délku IV, odvodit klíč a ověřit GCM tag. Do úspěšného ověření nesmí změnit svůj časový offset ani vytvořit autentizovaný požadavek založený na přijatém čase. Při chybějícím nebo neplatném tagu, neshodném `requestId` či jiném neplatném poli celou odpověď zahodí.

Klient musí před prvním autentizovaným požadavkem získat a ověřit čas serveru přes `health`; stejný postup zopakuje po `401`, pokud může být příčinou odchylka hodin. Server neověřuje znalost `SERVER_SECRET` u požadavku, ale platná odpověď dokazuje tuto znalost klientovi, který umí tag nezávisle ověřit.

## 6. Společný wire formát

### 6.1 JSON

- Kódování je UTF-8 bez BOM.
- Protokolová pole používají pouze ASCII.
- JSON objekt NESMÍ obsahovat duplicitní názvy členů.
- Neznámé pole v autentizačním, datovém nebo výsledkovém záznamu je chyba `400 bad_request`.
- Číselná pole jsou nezáporná celá čísla v bezpečném rozsahu PHP integeru.
- Base64 hodnoty používají výhradně kanonické base64url podle pravidel níže.
- Binární porovnání HMAC a tagů se provádí konstantním časem.

PHP implementace musí duplicitní top-level klíče odhalit před nebo během dekódování; samotné `json_decode()` je tiše přepíše a není pro tuto kontrolu dostačující.

`BASE64URL(x)` označuje jediný kanonický textový zápis přesných bajtů `x` pomocí abecedy `ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_` v tomto pořadí, bez znaku `=` a bez whitespace. Délka kanonického zápisu modulo 4 smí být pouze 0, 2 nebo 3; zbytek 1 je vždy neplatný. Prázdný řetězec je kanonickým zápisem prázdné posloupnosti bajtů, ale konkrétní pole jej může zakázat nebo vyžadovat jinou dekódovanou délku.

Nevyužité spodní bity posledního znaku MUSÍ být nulové:

- při délce modulo 4 rovné 2 musí mít poslední znak nulové spodní 4 bity své šestibitové hodnoty;
- při délce modulo 4 rovné 3 musí mít poslední znak nulové spodní 2 bity své šestibitové hodnoty;
- při délce modulo 4 rovné 0 žádné nevyužité bity nevznikají.

Příjemce MUSÍ před použitím hodnoty, kontrolou její dekódované délky nebo vložením jejího textu do AAD či jiného kanonického řetězce ověřit kanoničnost. Ekvivalentní povinná kontrola je: ověřit abecedu a délku, přidat pouze padding nutný pro dekodér, striktně dekódovat, znovu zakódovat podle `BASE64URL` a vyžadovat bajtovou shodu s původním textem. Samotné úspěšné dekódování nestačí, protože tolerantní dekodér může ignorovat nenulové nevyužité bity. Například `_w` je kanonický zápis bajtu `ff`, zatímco `_x` musí být odmítnut, i když jej tolerantní dekodér může převést na stejný bajt. Obdobně `__8` je kanonický zápis bajtů `ffff`, zatímco `__9` je nekanonický.

Nekanonická base64url hodnota v požadavku je `400 bad_request`. Klient při nekanonické base64url hodnotě v odpovědi odmítne celou odpověď a nesmí z ní použít plaintext, čas, výsledek odesílání ani payload. Po úspěšné kontrole je původní přijatý text totožný s `BASE64URL` dekódovaných bajtů; implementace proto smí pro AAD nebo jiný kanonický řetězec použít původní text nebo znovu zakódovanou hodnotu se shodným výsledkem.

### 6.2 NDJSON

NDJSON je posloupnost kompaktních JSON objektů. Každý záznam včetně posledního MUSÍ končit jedním bajtem LF (`0x0A`). CRLF, prázdný řádek a fragment bez závěrečného LF jsou neplatné. Velikosti se počítají z přijatých bajtů, nikoli z dekódovaného JSON.

### 6.3 Identifikátory

`requestId` a `messageId` jsou UUID verze 4 v malých hexadecimálních znacích ve tvaru `8-4-4-4-12`. Generují se CSPRNG.

Identifikátory slouží ke korelaci. Server ani klient je NESMÍ používat jako replay cache nebo deduplikační klíč napříč požadavky. `messageId` musí být unikátní pouze uvnitř jednoho těla `data`, aby byla odpověď jednoznačná; opakování celého požadavku se stejnými identifikátory je znovu zpracováno.

## 7. Kryptografické primitivy a kanonizace

Povinné algoritmy v1 jsou SHA-256, HMAC-SHA-256, HKDF-SHA-256 podle RFC 5869 a AES-256-GCM s 16bajtovým tagem.

Všechny textové kanonické řetězce jsou ASCII/UTF-8, hodnoty jsou oddělené jedním LF a řetězec nekončí LF, není-li uvedeno jinak. `SHA256HEX(x)` je SHA-256 nad přesnými bajty `x`, zapsaný 64 malými hexadecimálními znaky. Výstup `HMAC-SHA-256(key, x)` má 32 bajtů a do HMAC pole `tag` se zapisuje base64url bez paddingu. HMAC `tag` ve zprávách `auth-semi`, `auth-full` a `data-mac` se podle typu zprávy jednoznačně odlišuje od 16bajtového AES-GCM pole `tag` ve zprávách `health`, `info` a `data`.

### 7.1 Klíč `auth-semi`

Klient i server odvodí jediný 32bajtový klíč pro HMAC tag požadavku `auth-semi`:

```text
authSemiSaltDomain = "phpvpn/auth-semi/salt"
authSemiKeyDomain  = "phpvpn/auth-semi/key"
authSemiSalt       = SHA-256(authSemiSaltDomain)   // 32 raw bytes
semiKey            = HKDF-SHA-256(SERVER_SECRET, authSemiSalt, authSemiKeyDomain, 32)
```

`authSemiSaltDomain` s příponou `/salt` a `authSemiKeyDomain` s příponou `/key` jsou veřejné konstantní doménové řetězce pro oddělení kontextu: první určuje hashovaný HKDF salt a druhý přímo tvoří hodnotu HKDF `info`. Nejsou tajemstvím ani položkou požadavku; `auth-semi` nepřenáší salt ani IV. Jediným vstupním tajemstvím odvození je `SERVER_SECRET`. `semiKey` se nepřenáší, neukládá do protokolového stavu a nemá identifikátor.

### 7.2 Klíč `auth-full`

Po syntaktické validaci `src` klient i server odvodí jediný 32bajtový klíč pro HMAC tag požadavku `auth-full`:

```text
authFullSaltDomain   = "phpvpn/auth-full/salt"
authFullKeyDomain    = "phpvpn/auth-full/key"
authFullSaltMaterial = authFullSaltDomain || NUL || src
authFullSalt         = HMAC-SHA-256(SERVER_SECRET, authFullSaltMaterial)   // 32 raw bytes
authFullInfo         = authFullKeyDomain || NUL || src
fullAuthKey          = HKDF-SHA-256(ACCESS_KEY, authFullSalt, authFullInfo, 32)
```

`authFullSaltDomain` a `authFullKeyDomain` jsou veřejné konstantní doménové řetězce pro oddělení kontextu: první označuje vstup odvození saltu a druhý hodnotu HKDF `info`. `NUL` je jediný bajt `0x00` a `src` jsou přesné ASCII bajty validované VPN adresy bez vodicích nul. Doménové řetězce, oddělovač, pořadí argumentů i výstupní délka jsou normativní. Na rozdíl od veřejně konstantního `authSemiSalt` je `authFullSalt` klientsky specifický a odvozuje se ze `SERVER_SECRET` a `src`; klientův jediný `ACCESS_KEY` je poté vstupním tajemstvím HKDF. Bez obou tajemství nelze vypočítat správný `fullAuthKey` ani HMAC tag. `fullAuthKey` se nepřenáší, neukládá do protokolového stavu a nemá identifikátor.

Prázdný nakonfigurovaný `SERVER_SECRET` se podle kapitoly 3.2 před tímto odvozením normalizuje na 32 ASCII znaků `0`. Prázdný nebo nulový vstup tak zůstává platnou konfigurací, ale jako veřejně předvídatelná hodnota neposkytuje druhé praktické tajemství; proto na něj provozní stránka povinně upozorňuje.

### 7.3 Autentizace odpovědi `health`

Klient i server odvodí pro každý `requestId` samostatný 32bajtový klíč odpovědi `health` z efektivního `SERVER_SECRET`:

```text
healthSaltDomain   = "phpvpn/health-response/salt"
healthKeyDomain    = "phpvpn/health-response/key"
healthSaltMaterial = healthSaltDomain || NUL || requestId
healthSalt         = SHA-256(healthSaltMaterial)   // 32 raw bytes
healthInfo         = healthKeyDomain || NUL || requestId
healthKey          = HKDF-SHA-256(SERVER_SECRET, healthSalt, healthInfo, 32)
```

Doménové řetězce, jediný oddělovač `NUL`, přesné ASCII bajty UUID `requestId` a výstupní délka jsou normativní. `requestId` odděluje klíč každého pokusu a současně váže odpověď k čerstvé klientské výzvě. Jediným tajným vstupem je efektivní `SERVER_SECRET`; samotný `auth-none` žádné tajemství neposílá.

Server sestaví AAD z přesných validovaných hodnot odpovědi:

```text
aad = "PHPVPN-HEALTH-RESPONSE" LF
      "health" LF
      requestId LF
      serverVersion LF
      status LF
      description LF
      timestamp LF
      BASE64URL(iv)
```

Server provede AES-256-GCM s `healthKey`, 12bajtovým `iv`, uvedeným `aad` a prázdným plaintextem o délce nula bajtů. Ciphertext je proto také prázdný a do odpovědi se nepřenáší. Výsledný 16bajtový GCM tag autentizuje IV i všechna veřejná pole odpovědi. Tato autentizační aplikace AES-GCM s prázdným plaintextem odpovídá konstrukci GMAC; `SERVER_SECRET` se nepřipojuje jako veřejná data, ale prostřednictvím `healthKey` tvoří tajný klíčový materiál tagu.

Klient sestaví stejné AAD z přesných přijatých hodnot a tag ověří softwarovou implementací AES-256-GCM. Teprve potom smí použít `timestamp`. Změna `serverVersion`, `status`, `description`, `timestamp`, `requestId`, IV nebo tagu ověření zneplatní. Odpověď s platným tagem, ale jiným než aktuálně očekávaným `requestId` je rovněž neplatná. Server ani klient `requestId` nebo IV nepersistují a nevytvářejí replay databázi.

### 7.4 Softwarová implementace AES-256-GCM

AES-256-GCM je povinné kryptografické primitivum. Každá PHP implementace musí pro všechny jeho operace bez výjimky používat jedinou přibalenou softwarovou implementaci v čistém PHP. Operace NESMÍ delegovat na `openssl_encrypt()`, `openssl_decrypt()`, Sodium ani jiné kryptografické rozšíření. Toto pravidlo platí pro autentizaci odpovědi `health`, šifrování odpovědi `info` i pro šifrování, autentizované dešifrování a ověřování end-to-end payloadu `data`.

Softwarová implementace musí poskytovat úplné operace AES-256-GCM potřebné pro šifrování, autentizované dešifrování a ověření tagu. Zahrnuje přinejmenším dopřednou blokovou transformaci AES-256, čítačovou část GCTR a autentizační funkci GHASH. Protože GCM používá v obou směrech pouze dopřednou transformaci AES, samostatná inverzní bloková transformace AES není vyžadována. Implementace musí podporovat normativní 32bajtový klíč, 12bajtové IV, libovolné AAD předepsané tímto protokolem, prázdný plaintext pro GMAC odpovědi `health` a 16bajtový tag. Dešifrovaný plaintext NESMÍ zpřístupnit volajícímu před úspěšným ověřením celého GCM tagu.

Pro stejné bajty klíče, IV, AAD a plaintextu musí softwarová implementace vytvořit ciphertext a tag odpovídající normativnímu AES-256-GCM. Přítomnost nebo absence OpenSSL, Sodium či jiného rozšíření NESMÍ změnit wire formát, výstup kryptografické operace, odvození klíčů, chybové kódy ani žádné jiné protokolové chování.

## 8. Akce `info`

### 8.1 Požadavek `auth-semi`

Content-Type je `application/json; charset=utf-8`. Tělo obsahuje právě jeden JSON objekt a žádné další bajty:

```json
{"type":"auth-semi","timestamp":"2026-08-09T20:15:30.123Z","requestId":"4e94de3a-b9ad-4bc3-9297-82b98a19a143","tag":"BASE64URL"}
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

`timestamp` je JSON string o přesně 24 ASCII znacích ve tvaru `YYYY-MM-DDTHH:MM:SS.sssZ`, shodném s `health.timestamp`, `info.generatedAt` a `lastSeen`. Musí představovat platné gregoriánské datum a UTC čas s přesností na milisekundy; rok má čtyři číslice, ostatní číselné části kromě tříciferných milisekund přesně dvě, `T` a `Z` jsou velká, sekundy jsou `00` až `59` a milisekundy `000` až `999`. Nejsou dovoleny chybějící nebo jinak dlouhý zlomek sekund, časová zóna s offsetem ani leap second `60`. Neplatný formát nebo kalendářní hodnota vrací `400 bad_request`. Po úspěšném parsování server porovná oba UTC okamžiky s přesností na milisekundy a požadavek přijme pouze při absolutní odchylce nejvýše 60 sekund; větší odchylka vrací obecné `401 unauthorized`.

Do `canonical` vstupují přesné ASCII bajty validovaného `timestamp` beze změny a bez normalizace. Pole `tag` musí být base64url bez paddingu a po dekódování mít přesně 32 bajtů; neplatné kódování nebo délka vrací `400 bad_request`. Server jej porovnává s vypočteným HMAC konstantním časem a nesouhlas vrací obecné `401 unauthorized`.

`requestId` je korelační hodnota. Server nepersistuje `timestamp` ani `requestId`. Timestamp omezuje replay starého požadavku, ale identický platný požadavek lze uvnitř otevřeného 60sekundového okna zopakovat a server jej přijme znovu.

### 8.2 Plaintext odpovědi

Server sestaví jeden kompaktní JSON objekt. Minimální obsah je:

```json
{"serverVersion":"1.0.0","generatedAt":"2026-08-09T20:15:30.123Z","clients":[{"address":"10.99.99.2","lastSeen":"2026-08-09T20:14:52.000Z"}],"limits":{"maxDataRecordsPerRequest":16,"maxRequestBodyBytes":983040,"maxQueueBytesPerClient":16777216,"queueTtlSeconds":60,"minDataPollIntervalMs":50,"maxDataPollIntervalMs":3200}}
```

`generatedAt` je aktuální čas serveru přesně ve formátu `YYYY-MM-DDTHH:MM:SS.sssZ`. `clients` obsahuje pouze VPN adresu a přibližný čas poslední úspěšně zpracované komunikace. Nesmí obsahovat token, secret, fyzickou IP, cestu na disku ani obsah fronty. Klient, který ještě nemá platný statistický záznam, může být vynechán. `lastSeen` je UTC RFC 3339 přesně ve formátu `YYYY-MM-DDTHH:MM:SS.sssZ`. Kvůli 30sekundovému omezení aktualizací a best-effort povaze statistik jde pouze o přibližný provozní údaj, nikoli o auditní stopu ani autoritativní důkaz dostupnosti klienta.

`limits` popisuje skutečné aktivní hodnoty serveru. Klient nesmí spoléhat pouze na výchozí hodnoty této specifikace. `minDataPollIntervalMs` a `maxDataPollIntervalMs` jsou doporučené dolní a horní hranice intervalu mezi voláními `data`; klient se jimi smí řídit, ale nejsou pro něj závazné. Odpověď `info` nesmí obsahovat název, konfiguraci ani jinou informaci o interním úložišti serveru.

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
{"type":"auth-full","src":"10.99.99.2","timestamp":"2026-08-09T20:15:30.123Z","requestId":"69e82c2a-2732-466f-b9b1-f891084df639","tag":"BASE64URL"}
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
{"type":"data","messageId":"d5b543a9-9fc3-4c98-916c-ea0198f67a79","src":"10.99.99.2","dst":"10.99.99.4","iv":"BASE64URL","tag":"BASE64URL","ciphertext":"BASE64URL"}
```

Záznam `data` MUSÍ být zapsán jako kompaktní UTF-8 JSON objekt bez whitespace mimo hodnoty řetězců a přesně v uvedeném pořadí členů: `type`, `messageId`, `src`, `dst`, `iv`, `tag`, `ciphertext`. Po členu `ciphertext` NESMÍ následovat žádný další člen. Hodnota `ciphertext` je poslední řetězcová hodnota objektu; bezprostředně po jejím uzavíracím znaku `"` následuje `}` a LF. Všechny hodnoty záznamu `data` jsou ASCII a `ciphertext` je kanonický base64url bez paddingu, takže v něm NESMÍ být JSON escape sekvence.

Implementace smí nejprve načíst a validovat krátkou hlavičku končící doslovným prefixem `,"ciphertext":"` a potom hodnotu `ciphertext` streamovat. Musí však ověřit její kanonický base64url tvar, uzavírací `"}` a LF; jiný zápis, whitespace, změněné pořadí členů nebo další člen po `ciphertext` MUSÍ odmítnout.

Pravidla hodnot:

- `src` se musí přesně shodovat se `src` v `auth-full`; jinak `401 unauthorized`;
- `dst` musí být platná VPN adresa;
- `messageId` musí být unikátní v rámci právě jednoho requestu, ale server jej neporovnává s minulostí;
- `iv` po dekódování musí mít 12 bajtů;
- `tag` po dekódování musí mít 16 bajtů;
- `ciphertext` nesmí být prázdný;
- velikost záznamu `data` je omezena pouze celkovým limitem těla požadavku `MAX_REQUEST_BODY_BYTES`.

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

Plaintext je jedna binární dávka IPv4 paketů pro přesnou uspořádanou dvojici `src`, `dst`. Všechny vícebajtové hodnoty používají network byte order (big-endian):

```text
packetCount                     : unsigned 32-bit integer
packetLength[0..packetCount-1]  : unsigned 16-bit integer
packetBytes[0..packetCount-1]   : přesně packetLength[i] bajtů IPv4 paketu
```

`packetCount` MUSÍ být nejméně 1. Každý `packetLength` MUSÍ být v rozsahu 20 až 65535 a každá položka `packetBytes` MUSÍ být úplný syntakticky platný IPv4 paket, jehož délka IPv4 hlavičky a pole Total Length přesně odpovídají `packetLength`. Součet délky čtyřbajtové hlavičky, všech dvoubajtových délek a všech `packetBytes` MUSÍ přesně odpovídat délce plaintextu; trailing data ani částečná položka nejsou povoleny.

Odesílatel sestaví dávku pouze z paketů, jejichž vnitřní zdrojová a cílová IPv4 adresa se přesně shodují s vnějšími poli `src` a `dst` datového záznamu. Poté celý plaintext dávky zašifruje AES-256-GCM a přenese ciphertext a 16bajtový tag. Příjemce musí před předáním paketů virtuální kartě:

1. zahodit řádek, pokud se `dst` nerovná jeho jediné nakonfigurované VPN adrese;
2. vyhledat jediný klíč podle přesných směrovacích adres `src` a `dst`;
3. sestavit přesné AAD;
4. ověřit GCM tag a teprve poté získat plaintext;
5. ověřit `packetCount`, tabulku délek, přesnou délku plaintextu a každý IPv4 paket;
6. ověřit shodu vnitřních zdrojových a cílových adres každého paketu s `src` a `dst`;
7. předat všechny platné pakety dávky virtuální kartě sekvenčně v pořadí, v jakém jsou uloženy v plaintextu. Neplatná dávka se nesmí předat částečně.

Relay tag neověřuje, protože nemá `PAYLOAD_KEY`. Změna `src`, `dst`, `messageId`, IV nebo ciphertextu způsobí u příjemce neplatný tag.

#### Jedinečnost IV

Pro každý konkrétní `PAYLOAD_KEY` MUSÍ odesílatel zabránit opakovanému použití stejného 12bajtového IV pro šifrování dvou různých plaintextů. Odesílatel používá 12 náhodných bajtů z CSPRNG.

Klient nevede databázi přijatých IV a IV nepersistuje kvůli replay kontrole. Jedinečnost IV je povinnost odesílatele pro korektnost AES-GCM, nikoli mechanismus detekce replay. Opakované doručení stejné trojice IV/ciphertext/tag je přijato jako další platná zpráva.

### 9.5 Odpověď `data`

Úspěšná odpověď má Content-Type `application/x-ndjson; charset=utf-8` a pořadí:

1. nula nebo více přímo uložených záznamů `data`;
2. právě jeden záznam `data-result`;
3. právě jeden závěrečný záznam `data-mac`.

Po úspěšném ověření požadavku a zpracování jeho odesílaných záznamů server atomickou operací načte všechny neexpirované datové záznamy aktuálně uložené v bufferu klienta a současně je z persistentního bufferu odstraní. Vyprázdnění příchozího bufferu volajícího NESMÍ záviset na tom, zda se podařilo publikovat některý nebo všechny jeho odesílané záznamy. Server je proto provede i tehdy, když kvůli nedostatku kapacity nepřijal žádný odesílaný záznam. Nově publikovaný záznam se `dst` shodným s autentizovaným `src` proto patří již do této odpovědi. Teprve z takto získaného lokálního seznamu server sestaví odpověď. Počet vrácených záznamů se neomezuje samostatným protokolovým ani konfiguračním limitem a odpověď se nestránkuje. Jedinou horní mezí objemu je kapacita bufferu. Každý záznam se vrací přímo ve stejném schématu `data`, bez další obálky:

```json
{"type":"data","messageId":"e8edc241-96ec-4b51-9336-1fb0836f4db5","src":"10.99.99.4","dst":"10.99.99.2","iv":"BASE64URL","tag":"BASE64URL","ciphertext":"BASE64URL"}
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

```ndjson
{"type":"data-result","requestId":"69e82c2a-2732-466f-b9b1-f891084df639","acceptedMessageIds":[],"deferredMessageIds":[],"acceptedRecords":0,"acceptedBytes":0}
{"type":"data-mac","requestId":"69e82c2a-2732-466f-b9b1-f891084df639","tag":"BASE64URL"}
```

Server autentizuje počet, pořadí a úplnost datových řádků a přesný záznam `data-result` pomocí klíče `fullAuthKey` odvozeného pro aktuální požadavek. Nechť `recordCount` je počet řádků `data` v odpovědi, `dataResultLine` jsou přesné UTF-8 bajty JSON objektu `data-result` bez ukončovacího LF a dvojice `messageId_i`, `gcmTag_i` pocházejí z i-tého datového řádku v pořadí odpovědi. `gcmTag_i` je přesný base64url řetězec pole `tag` tak, jak je uveden v tomto řádku. Server sestaví:

```text
canonical = "PHPVPN-DATA-RESPONSE" LF
            "data" LF
            requestId LF
            DECIMAL(recordCount) LF
            messageId_1 LF
            gcmTag_1 LF
            ...
            messageId_recordCount LF
            gcmTag_recordCount LF
            SHA256HEX(dataResultLine)

responseTag = HMAC-SHA-256(fullAuthKey, canonical)
```

`DECIMAL(recordCount)` je nezáporný desítkový zápis bez znaménka a bez vodicích nul; nula se zapisuje jako `0`. Při `recordCount = 0` se blok dvojic zcela vynechá. `canonical` končí posledním hexadecimálním znakem `SHA256HEX(dataResultLine)` a nekončí LF. Hashuje se pouze malý řádek `data-result`, nikoli ciphertext datových řádků. Záznam `data-mac` má přesně tři pole:

```json
{"type":"data-mac","requestId":"69e82c2a-2732-466f-b9b1-f891084df639","tag":"BASE64URL"}
```

Jeho `requestId` se musí shodovat s aktuálním požadavkem i s `data-result`. Pole `tag` je base64url bez paddingu a dekóduje se na přesně 32 bajtů `responseTag`.

Klient MUSÍ přijmout úplné NDJSON tělo s uvedeným pořadím a přesně jedním závěrečným `data-mac`. Z přijatých datových řádků ve stejném pořadí sestaví `canonical`, použije přesné přijaté bajty `dataResultLine`, vypočte HMAC a porovná jej konstantním časem. Chybějící nebo neplatný `data-mac`, neshodný `requestId`, jiný počet či pořadí dvojic nebo změněný `data-result` zneplatní celou odpověď. V takovém případě klient NESMÍ odstranit žádnou položku ze své odchozí fronty ani předat žádný přijatý payload a odpověď zahodí.

Teprve po úspěšném ověření `data-mac` klient použije `acceptedMessageIds` a každý datový řádek samostatně ověří podle AES-GCM pravidel v kapitole 9.4. Řádek s neplatným GCM tagem zahodí a ostatní platné payloady může zpracovat. GCM ověření chrání pole a ciphertext jednotlivého záznamu; HMAC nad uspořádanými dvojicemi chrání jeho přítomnost a pozici v odpovědi. Protokol neposílá žádné následné potvrzení.

Jakmile worker atomicky převezme konkrétní soubor dávky k odeslání, nesmí jej žádný jiný worker vydat. Pokud worker selže před odesláním odpovědi, odpověď nedorazí, je neúplná, má neplatný `data-mac` nebo ji klient nezpracuje, již převzaté dávky se neobnovují. Jde o vědomě přijaté optimistické at-most-once chování na úrovni jednotlivé dávky. `data-mac` zajišťuje autenticitu výsledku a detekci změny počtu či pořadí datových řádků, ale nezajišťuje doručení.

## 10. Fronty a doručovací sémantika

### 10.1 Model

Každý cílový klient má logický perzistentní buffer. Do jeho velikosti se započítávají všechny dosud nevydané záznamy. Výchozí limit je 16 MiB logických serializovaných bajtů na klienta bez interní režie backendu.

Server může z jednoho requestu vytvořit samostatné dávky podle cíle. Publikace jedné dávky musí být atomická: po pádu je viditelná celá, nebo vůbec. Úspěch jednoho cíle nezávisí na selhání jiného cíle.

Při sestavení odpovědi server vyhledá všechny neexpirované publikované dávky v bufferu autentizovaného klienta. Každou dávku před odesláním atomicky převezme; dávku, kterou již převzal jiný worker, přeskočí. Vrácený seznam může obsahovat více dříve publikovaných dávek. Server jej nesmí stránkovat ani omezit počtem záznamů.

### 10.2 Částečné přijetí

Server rozdělí odesílané záznamy do cílových skupin podle `dst` a skupiny zpracovává deterministicky. Publikace jedné cílové skupiny je atomická: server publikuje všechny její záznamy, nebo žádný. Přijaté položky uvede v `acceptedMessageIds`, nepřijaté v `deferredMessageIds`.

Pro každou cílovou frontu, do které aktuální požadavek zapisuje, server postupuje takto:

1. pokusí se atomicky publikovat celou cílovou skupinu;
2. pokud se skupina nevejde kvůli nedostatku kapacity, provede právě jeden cílený sweep této fronty, který odstraní všechny její expirované záznamy;
3. po sweepu zopakuje pokus o atomické publikování celé cílové skupiny právě jednou;
4. pokud se skupina nevejde ani při druhém pokusu, server z této skupiny do dané fronty nezapíše nic a všechna její `messageId` uvede v `deferredMessageIds`.

V rámci jednoho požadavku `data` smí server pro každou konkrétní cílovou frontu provést nejvýše jeden kapacitně vyvolaný sweep a nejvýše dva pokusy o publikaci. Nedostatek kapacity není chybou celého požadavku. Autentizovaný a syntakticky platný požadavek kvůli němu vždy pokračuje vyprázdněním příchozího bufferu volajícího a vrací `200`, `data-result` a závěrečný `data-mac`, i když jsou všechny odesílané položky uvedeny v `deferredMessageIds`. Server kvůli nedostatku kapacity cílové fronty NESMÍ vrátit HTTP `429`. Při nedostupnosti backendu vrací `503 queue_unavailable`.

Chybová odpověď nikdy neopravňuje klienta odstranit položku z odchozí fronty. Při úspěchu klient vyhodnotí jediný syntakticky platný `data-result` se shodným `requestId` pouze po úspěšném ověření bezprostředně následujícího závěrečného `data-mac` podle kapitoly 9.5.

### 10.3 Vydání, ztráta a opakování

Vyprázdnění celé fronty není atomická operace. Server soubory dávek postupně přebírá, vydává a maže. Převzetí každé jednotlivé dávky však musí být atomické: po souběžném zpracování ji smí vydat nejvýše jeden worker. Souběžně publikovaná dávka, kterou worker při počátečním průchodu frontou ještě nenalezl, zůstane pro další poll. Server nečeká na reakci klienta a již převzaté nebo odstraněné dávky neobnovuje. Pád workeru, přerušení spojení, zahození odpovědi nebo chyba klienta po převzetí dávky proto může znamenat její ztrátu. Vyšší vrstvy přenášeného IP provozu mohou podle svých vlastních pravidel provést nové odeslání.

Klient ani server neporovnávají `messageId`, IV, request body nebo autentizační HMAC tag s historií. Replay autentizovaného odesílacího requestu uvnitř platného 60sekundového okna založí další kopii dat; po uzavření okna server request odmítne. Replay datové odpovědi může payload předat znovu, přestože server tentýž záznam z bufferu vydal jen jednou. Duplicita na trase i ztráta po vyprázdnění bufferu jsou výslovně přijatá omezení.

Stejná pravidla platí pro replay pollingového požadavku bez odesílaných dat. Jeho úspěšné ověření opravňuje každou přehrávající kopii k operaci `drainAll`; první kopie, která zastihne neprázdnou frontu, záznamy odstraní a obdrží je ve své odpovědi. Server je nesmí rezervovat pro původní spojení, vrátit do fronty ani rozlišovat legitimní a přehrávající kopii. Obnovu komunikace lze očekávat pouze tehdy, když klient nebo vyšší vrstva vytvoří nové pakety; tato provozní kompenzace nemění přijetí možnosti trvalého útoku.

### 10.4 Expirace

Výchozí `QUEUE_TTL_SECONDS` je 60 s. Čas běží od prvního úspěšného publikování. Po expiraci smí backend dávku smazat bez doručení. Údržba může být oportunistická, takže fyzické odstranění může nastat později; expirovaná dávka však již nesmí být zahrnuta do odpovědi ani započítána do využité kapacity fronty.

Vedle oportunistické údržby musí backend podporovat cílený sweep jedné konkrétní fronty podle kapitoly 10.2. Kapacitně vyvolaný sweep odstraní všechny soubory dávek této fronty expirované k času zpracování požadavku, bez ohledu na jejich stav `.receiving`, `.data` nebo `.sending`. Jeho účelem je uvolnit skutečně dostupnou kapacitu před jediným opakovaným pokusem o publikaci; nesmí procházet jiné klientské fronty.

### 10.5 Storage driver

Wire protokol nesmí záviset na názvech souborů, adresářích ani databázové technologii. Implementace používá rozhraní s operacemi odpovídajícími alespoň:

- `enqueue(destination, records, expiresAt)`;
- `drainAll(client, now)`;
- `usage(client)`;
- `readClientStats()` a `touchLastSeenBestEffort(client, now)`;
- `sweepQueue(destination, now)` pro cílené odstranění expirací z jedné fronty;
- `sweep(now)` pro omezenou oportunistickou údržbu.

Povinný driver `disk` používá per-client adresáře a atomické `rename()` ve stejném filesystému. Konfigurační proměnná `BUFFER_DIRECTORY` určuje kořen tohoto úložiště. Její výchozí hodnota je `./phpvpn`, relativní cesta se vyhodnocuje vůči adresáři souboru `phpvpn.php`; administrátor ji smí změnit na jinou absolutní nebo relativní cestu. Dávka se zapisuje pod příponou `.receiving`, po úspěšném dokončení zápisu se přejmenuje na `.data` a worker ji před odesláním atomicky přejmenuje na `.sending`. Pouze worker, který uspěl v přejmenování `.data` na `.sending`, smí dávku vydat a poté smazat. Název dávky před stavovou příponou má tvar `YYYY-MM-DDTHH-MM-SS.sssZ_<bytes>_<src>_<uuid>`, kde první pole je jediný časový údaj: čas expirace v UTC s přesně třemi číslicemi milisekund. Pomlčky mezi hodinou, minutou a sekundou jsou filename-safe náhradou dvojteček; `Z` označuje UTC. Příklad: `2026-08-12T14-37-05.123Z_2048_10.99.99.2_d5b543a9-9fc3-4c98-916c-ea0198f67a79.data`. `<bytes>` je počet serializovaných bajtů, `<src>` je validovaná zdrojová VPN adresa a `<uuid>` je náhodné UUID verze 4. Každý per-client adresář navíc smí obsahovat statistický marker `info.json` podle kapitoly 10.6; tento soubor není dávkou a operace fronty jej musí ignorovat. Konfigurační volba `STORAGE_DRIVER` má výchozí hodnotu `disk`; budoucí `memcached`, `mariadb`, `mysql` nebo `postgresql` musí zachovat stejné atomické převzetí jednotlivých dávek a kapacitní chování.

Adresář klienta s VPN adresou `a.b.c.d` má cestu `BUFFER_DIRECTORY/a_b_c_d`. Jeho jméno vzniká výhradně nahrazením tří teček adresy podtržítky; například buffer cílové adresy `10.99.99.2` je `BUFFER_DIRECTORY/10_99_99_2`. Adresář patří vždy cílové adrese `dst`, nikoli odesílateli uvedenému v názvu dávky. Při výchozím rozsahu může kořen obsahovat nejvýše 253 takových podadresářů pro adresy `10.99.99.2` až `10.99.99.254`.

Kořenový i per-client adresář se vytváří líně a pouze z již validované VPN adresy. Server musí adresář `dst` vytvořit před zápisem nebo vyprázdněním jeho fronty, je-li dosud nepřítomný. Po úspěšném ověření a normálním zpracování požadavku `data` musí obdobně v režimu best-effort provést pokus o vytvoření adresáře autentizovaného `src`, aby v něm mohl vytvořit nebo aktualizovat marker `info.json`; selhání této statistické operace se řídí kapitolou 10.6. Požadavek `info` ani pouhé čtení konfigurace nevytváří adresáře pro ostatní klienty.

### 10.6 Statistiky klientů ovladače `disk`

Ovladač `disk` ukládá statistiku každého klienta samostatně do souboru `info.json` v jeho per-client adresáři. Neexistuje žádný centrální soubor statistik. Soubor `info.json` je marker: jeho čas poslední změny (`mtime`) představuje `lastSeen` daného klienta s přesností poskytovanou filesystémem.

Výchozí a implementací vytvářený obsah `info.json` je prázdný soubor o délce nula bajtů. Navzdory příponě `.json` jeho obsah netvoří JSON dokument, nenese žádná protokolová data a implementace jej NESMÍ číst, parsovat ani do něj zapisovat. Název je pouze konvenční. Aktualizace statistiky mění výhradně `mtime`; soubor nesmí obsahovat přístupový klíč, serverové tajemství, fyzickou adresu, payload ani interní cestu.

Po úspěšném ověření a normálním zpracování každého požadavku `data` server:

1. jediným načtením metadat zjistí `mtime` souboru `info.json` v per-client adresáři autentizovaného `src`;
2. pouze pokud soubor chybí, jeho `mtime` nelze získat nebo je platný čas vůči aktuálnímu času serveru starší než 30 sekund, provede jediný pokus o `touch()` markeru s aktuálním časem;
3. chybějící marker při tomto pokusu vytvoří jako prázdný soubor; existujícímu markeru změní pouze `mtime`;
4. při stáří přesně 30 sekund ani při `mtime` v budoucnosti aktualizaci neprovádí.

Endpoint `info` projde nakonfigurované klienty a pro každý z nich se pokusí načíst pouze `mtime` jeho markeru. Úspěšně načtený a reprezentovatelný čas převede na UTC RFC 3339 přesně ve formátu `YYYY-MM-DDTHH:MM:SS.sssZ` a použije jako `lastSeen`; chybějící přesnost filesystému vyjádří nulami v příslušných milisekundách. Klienta s chybějícím nebo nečitelným markerem, neplatným `mtime` nebo chybějícím per-client adresářem z pole `clients` vynechá. Obsah markeru se ani při akci `info` nečte.

Statistiky jsou striktně best effort. Selhání načtení metadat, vytvoření markeru nebo změny jeho `mtime` NESMÍ změnit HTTP status, `data-result`, odpovídající `data-mac`, publikování odesílaných dat, vyprázdnění příchozího bufferu ani předání payloadů. Implementace kvůli statistikám NESMÍ provádět retry, čekat, získávat souborový zámek ani vracet chybu. Souběžné aktualizace stejného markeru jsou povoleny a poslední dokončená metadata operace vyhrává; různí klienti používají různé markery a navzájem si statistiku nepřepisují.

Soubor `info.json` NESMÍ být přímo dostupný přes HTTP ani HTTPS. `BUFFER_DIRECTORY` má být mimo document root; používá-li administrátor výchozí cestu `./phpvpn` uvnitř document rootu, webserver MUSÍ přímý přístup k celému tomuto adresáři explicitně zakázat. Endpoint `info` neprozrazuje jméno, umístění ani chyby markeru. Marker se NESMÍ započítávat do využité kapacity fronty, vydávat v odpovědi `data`, přejmenovávat mezi stavy dávky ani odstraňovat při TTL nebo oportunistickém sweepu.

## 11. Limity a timeouty

### 11.1 Výchozí limity

| Konstanta | Výchozí hodnota | Význam |
|---|---:|---|
| `MAX_DATA_RECORDS_PER_REQUEST` | 16 | Počet záznamů `data` v jednom requestu. |
| `MAX_AUTH_RECORD_BYTES` | 4096 B | První řádek včetně LF. |
| `MAX_REQUEST_BODY_BYTES` | 983040 B | Celé NDJSON tělo požadavku včetně všech LF. `983040 B` znamená 960 kB. Hodnota je záměrně nižší než výchozí limit 1 MiB serverů Nginx a Tengine. |
| `MAX_INFO_PLAINTEXT_BYTES` | 262144 B | Plaintext šifrované odpovědi `info`. |
| `MAX_QUEUE_BYTES_PER_CLIENT` | 16777216 B | 16 MiB logické persistentní fronty. |
| `AUTH_TIMESTAMP_SKEW_SECONDS` | 60 s | Nejvyšší absolutní odchylka autentizačního timestampu od času serveru. |
| `QUEUE_TTL_SECONDS` | 60 s | Nejvyšší věk čekající dávky. |
| `LOCK_TIMEOUT_MS` | 200 ms | Nejvyšší čekání PHP workeru na storage zámek. |
| `MIN_DATA_POLL_INTERVAL_MS` | 50 ms | Doporučený nejkratší interval mezi voláními `data`. |
| `MAX_DATA_POLL_INTERVAL_MS` | 3200 ms | Doporučený nejdelší interval mezi voláními `data`. |

Konfigurační invarianty musí zajistit, že `MAX_REQUEST_BODY_BYTES` pojme autentizační řádek a maximální počet maximálních záznamů a že `MIN_DATA_POLL_INTERVAL_MS` je kladné a není větší než `MAX_DATA_POLL_INTERVAL_MS`. PHP `post_max_size`, limity proxy a webserveru pro požadavek musí být přísně vyšší než `MAX_REQUEST_BODY_BYTES`. Webserver a proxy musí umět bez stránkování přenést odpověď obsahující celou frontu o velikosti `MAX_QUEUE_BYTES_PER_CLIENT` plus `data-result` a závěrečný `data-mac`; jinak server musí při diagnostice hlásit chybnou konfiguraci.

Překročení syntaktického limitu vrací `413 payload_too_large`. Nedostatek kapacity persistentní cílové fronty není syntaktická ani protokolová chyba. Po jednom cíleném sweepu a jednom opakovaném pokusu server vrátí nepřijaté položky v `deferredMessageIds` úspěšné odpovědi `200`.

### 11.2 Časové chování

Server odpovídá okamžitým vyprázdněním aktuálního bufferu. Nikdy nečeká na příchod nových dat, nepoužívá `sleep`, long-polling ani streaming neurčité délky.

Doporučené výchozí hodnoty klienta:

- connect timeout 5 s;
- celkový timeout requestu 15 s;
- exponenciální backoff po síťové chybě nebo `503`, počínaje 250 ms, nejvýše 30 s, vždy s jitterem;
- interval mezi voláními `data` může klient omezit podle aktivních hodnot `minDataPollIntervalMs` a `maxDataPollIntervalMs` z odpovědi `info`; výchozí doporučené rozmezí je 80 až 1 000 ms.

Server může u `503` poslat `Retry-After`. Klient jej respektuje jako dolní hranici čekání. Položky uvedené v `deferredMessageIds` klient ponechá ve své odchozí frontě pro pozdější pokus, ale nadále pokračuje v pollingu, aby nedostupný cíl neblokoval jeho příchozí provoz.

## 12. Stavy a přechody

### 12.1 Klientská komunikace

Protokol nemá kryptografickou session ani přihlašovací handshake. Neautentizovaný požadavek `auth-none` nezakládá session a autentizovaný `health` pouze jednorázově váže odpověď k `requestId`. Logický bootstrap je:

1. povinný `POST health` s čerstvým `auth-none` před prvním autentizovaným požadavkem; klient ověří GCM tag odpovědi a teprve poté použije verzi a čas pro korekci offsetu;
2. volitelný autentizovaný `info` pro aktuální limity a přehled;
3. libovolný počet samostatně autentizovaných `data` cyklů;
4. ukončení prostým zastavením požadavků.

Neexistuje login, session ID, keepalive ani logout. Každý request lze zpracovat jiným PHP workerem nebo uzlem se sdíleným storage a stejnou konfigurací klíčů.

### 12.2 Stav uložené dávky

```text
neexistuje --zápis--> receiving --publish--> data --převzetí--> sending --odeslání--> smazána
                    \___________________________ TTL _______________________________/
```

Soubor ve stavu `.receiving` není publikovanou dávkou. Atomické přejmenování na `.data` jej publikuje a atomické přejmenování na `.sending` jej přidělí právě jednomu odesílajícímu workeru. Pád během `enqueue` nesmí publikovat částečnou dávku. Pád po převzetí do `.sending` může způsobit ztrátu této dávky, nikdy její automatické opětovné vydání serverem. Sweep smí smazat expirovaný soubor v kterémkoli z těchto stavů.

## 13. Chyby

Chybové tělo je malý obecný JSON objekt:

```json
{"type":"error","code":"unauthorized"}
```

| HTTP | `code` | Význam | Reakce klienta |
|---:|---|---|---|
| 400 | `bad_request` | Neplatný JSON/NDJSON, pořadí, pole, hodnota, formát UTC timestampu, kódování či délka HMAC tagu nebo duplicitní klíč. | Opravit request; neopakovat beze změny. |
| 401 | `unauthorized` | Neplatný autentizační HMAC tag, `src`, konfigurace `SERVER_SECRET` nebo `ACCESS_KEY`, podvržené `src` v datovém záznamu nebo timestamp mimo povolené okno. | Obnovit čas pouze přes platně ověřený `health`, ověřit obě tajemství; nemaž data. |
| 404 | `not_found` | Neznámá akce. | Opravit URL. |
| 405 | `method_not_allowed` | Nepovolená metoda. | Řídit se `Allow`. |
| 413 | `payload_too_large` | Překročen limit těla nebo počtu záznamů. | Zmenšit request. |
| 415 | `unsupported_media_type` | Chybný Content-Type. | Opravit Content-Type. |
| 503 | `queue_busy` | Vyčerpán limit čekání na storage zámek. | Krátký backoff. |
| 503 | `queue_unavailable` | Backend nelze bezpečně použít. | Backoff; administrátorská kontrola. |
| 503 | `server_misconfigured` | Nekonzistentní limity nebo jiná neplatná serverová konfigurace. | Neopakovat rychle; opravit server. |
| 500 | `internal_error` | Neočekávaná interní chyba. | Ponechat data, backoff. |

Chybová odpověď není autentizovaná. Klient ji smí použít jako diagnostiku nebo podnět k backoffu, nikdy jako důkaz přijetí dat. Server nesmí v chybě prozradit cestu, token, secret, existenci jiného klienta, obsah fronty ani výpis výjimky.

Chyba per-client statistického markeru `info.json` nikdy není protokolovou chybou a NESMÍ vyvolat `500`, `503` ani změnit jinak zvolený status odpovědi. Nesmí být uvedena v chybovém těle ani odlišitelná časováním záměrného čekání nebo retry.

## 14. Kompatibilita a rozšiřování

Hodnota `type` určuje schéma zprávy. Serverová hodnota `serverVersion` v `health` a `info` je informativní SemVer verze aplikace a není wire negotiation.

Normativní typy zpráv mají uzavřenou sadu polí. Nové volitelné informace se přidávají především do šifrovaného plaintextu `info`, kde klient musí ignorovat neznámá pole. Nekompatibilní změna povinných polí, kanonizace nebo kryptografických algoritmů vyžaduje nový název typu zprávy a nové kryptografické doménové řetězce.

Neexistuje automatický downgrade. Klient nesmí po chybě podporovaného typu zkusit historickou akci nebo slabší autentizaci. Akce `data` a `info` nenabízejí aliasy starých názvů.

## 15. Dopad na implementaci v PHP

Doporučené oddělení odpovědností v jednom nasaditelném PHP souboru:

- `Config` a `ConfigValidator`: klíče, limity, storage driver a invarianty;
- `Router` a `Http`: metoda, akce, hlavičky a obecné chyby;
- `JsonWire` a `NdjsonReader`: limitované čtení, duplicitní klíče, striktní pole a kanonické base64url ověřené zpětným zakódováním;
- `KeyDerivation` a `Authenticator`: HKDF, HMAC, kanonizace a konstantní porovnání;
- `AesGcm`: jediná softwarová implementace AES-256-GCM v čistém PHP pro GMAC odpovědi `health`, šifrování `info` a end-to-end payload;
- `Storage` interface a `DiskStorage`;
- `ClientStats`: best-effort čtení `mtime` a nejvýše jeden pokus o aktualizaci per-client markeru `info.json`;
- `HealthHandler`, `InfoHandler` a `DataHandler`;
- `Maintenance`: cílený sweep expirací jedné fronty při nedostatku kapacity a omezený oportunistický sweep.

Serverová implementace potřebuje `hash_hmac`, SHA-256, `random_bytes`, striktní JSON a pro `health` i `info` vlastní softwarovou implementaci AES-256-GCM v čistém PHP. Pro HKDF může použít `hash_hkdf()`. PHP server i PHP klient musí všechny operace AES-256-GCM provádět výhradně touto softwarovou implementací podle kapitoly 7.4. Nesmí obsahovat výběr kryptografického backendu ani větev používající pro AES-256-GCM OpenSSL, Sodium či jiné rozšíření.

Softwarová implementace má implementovat pouze rozsah potřebný pro tento protokol: AES-256 v dopředném směru, GCTR, GHASH, 96bitové IV, 128bitový tag a prázdný plaintext s neprázdným AAD pro GMAC. Při inicializaci musí projít known-answer testem AES-256-GCM s pevnými vstupy a očekávanými výstupy. Výsledek testu se v rámci běhu procesu zapamatuje, aby se test neopakoval pro každý požadavek. Software nesmí obsahovat větev, která chybu softwarové implementace řeší vypnutím šifrování, změnou algoritmu nebo odesláním plaintextu.

Normalizace z kapitoly 3.2 musí proběhnout při startu ještě před předáním konfigurace do `KeyDerivation`. V PHP se testuje přesně nulová délka řetězce, nikoli obecná nepravdivost hodnoty:

```php
$serverSecret = SERVER_SECRET;
if ($serverSecret === '') {
    $serverSecret = str_repeat('0', 32);
}
```

Všechny následné kryptografické operace používají výhradně `$serverSecret`. Funkce `empty()` se pro tuto normalizaci NESMÍ použít, protože by chybně považovala platný neprázdný řetězec `"0"` za prázdný. Po normalizaci není vstupní klíč žádného zde předepsaného volání `hash_hkdf()` prázdný: původně prázdný `SERVER_SECRET` má 32 ASCII znaků `0`, každá jiná hodnota `SERVER_SECRET` je z definice neprázdná a odvození `auth-full` dostane validovaný 32bajtový `ACCESS_KEY`. Prázdný nakonfigurovaný `SERVER_SECRET` proto nesmí vyvolat `ValueError`, `500 internal_error` ani `503 server_misconfigured`.

Tělo požadavku `data` může mít přibližně 4,00 MiB. Implementace jej smí streamovat a průběžně hashovat, ale všechny změny storage musí odložit až po ověření `auth-full`. Odpověď může obsahovat celý buffer klienta, tedy až přibližně `MAX_QUEUE_BYTES_PER_CLIENT` plus `data-result` a `data-mac`. Server pro HMAC odpovědi zpracovává pouze uspořádaný seznam `messageId` a GCM tagů a malý hash `data-result`, nikoli celý ciphertext; výpočet lze provádět průběžně s konstantní dodatečnou pamětí. `memory_limit` musí mít dostatečnou rezervu pro request, celý seznam odpovědi a JSON struktury.

Diskový driver musí používat cesty odvozené pouze z již validované VPN adresy, zamykání s timeoutem, atomickou publikaci a obranu proti zpřístupnění bufferů i statistických markerů přes webserver. Nemá používat neomezený globální zámek. Zákaz zámků pro `info.json` nemění zámková pravidla datového bufferu. Údržba po odpovědi musí mít časový a početní rozpočet.

Statistika `lastSeen` se zvažuje pouze po platně autentizovaném a normálně zpracovaném požadavku `data`. Její 30sekundové omezení, jediný pokus o aktualizaci metadat a veškerá selhání jsou oddělené od výsledku datového zpracování podle kapitoly 10.6.

## 16. Povinné testovací scénáře

### 16.1 Transport a směrování

1. Stejný request přes HTTP, HTTPS, přímé spojení a proxy vede ke stejnému aplikačnímu výsledku.
2. Podvržené `X-Forwarded-*` hlavičky neovlivní autentizaci, `src`, frontu ani odpověď.
3. URL s neznámou akcí a nepovolená metoda vrátí správné obecné chyby.
4. Root stránka zobrazí H1; při výchozích 32 ASCII znacích `0` i při prázdném nebo jinak pouze nulovém `SERVER_SECRET` navíc zobrazí červený H2, přičemž `health`, `info` a `data` zůstávají funkční.
5. `GET` a `HEAD` na akci `health` vrátí `405` s `Allow: POST`. Validní `POST` obsahuje právě `auth-none` s čerstvým UUIDv4 `requestId`, bez timestampu, tagu nebo jiného důkazu znalosti tajemství.
6. Úspěšný `health` má přesně sedm povinných polí `serverVersion`, `status`, `description`, `timestamp`, původní `requestId`, 12bajtové `iv` a 16bajtový `tag`; status je `running`, description `OK` a timestamp má přesný UTC tvar `YYYY-MM-DDTHH:MM:SS.sssZ` s přesností na milisekundy.
7. Klient před ověřením GCM tagu nezmění časový offset. Po úspěšném ověření pomocí efektivního `SERVER_SECRET` odvodí offset vůči serveru; změněná nebo podvržená odpověď offset neovlivní.

### 16.2 JSON, NDJSON a limity

1. Validní prázdný poll obsahuje pouze `auth-full` s LF.
2. Chybějící poslední LF, CRLF, prázdný řádek, duplicitní klíč a neznámé pole jsou odmítnuty. Záznam `data` s whitespace, jiným pořadím členů, `ciphertext` mimo poslední pozici nebo dalším členem za `ciphertext` je odmítnut.
3. Jeden datový řádek `data` může využít zbývající kapacitu celého těla požadavku; odmítnut je až request, jehož celá velikost překročí `MAX_REQUEST_BODY_BYTES`.
4. Přesně 16 datových záznamů projde, sedmnáctý vrátí `413` bez změny storage.
5. Hrany pro celé tělo a buffer jsou testovány na `limit-1`, `limit` a `limit+1`.
6. Neplatná base64url, délka IV, délka tagu, UUID, VPN adresa a neznámé nebo nadbytečné pole jsou odmítnuty.
7. Kanonické zápisy `_w` pro bajt `ff` a `__8` pro bajty `ffff` projdou obecnou base64url validací. Nekanonické zápisy `_x` a `__9`, délka se zbytkem 1 modulo 4, znak `=`, whitespace a znaky `+` nebo `/` jsou odmítnuty, i když je použitý dekodér umí převést na bajty. Test decode–reencode musí pro každou přijatou hodnotu vrátit přesně původní text.
8. Úspěšná odpověď obsahuje nula nebo více přímých řádků `data`, právě jeden `data-result` a právě jeden poslední `data-mac`; další řádek za `data-mac` je neplatný.
9. `auth-none` má přesně pole `type` a `requestId`; chybějící, neznámé nebo nadbytečné pole včetně `timestamp`, `src` či `tag` vrátí `400 bad_request`.
10. Dávka payloadu se dvěma IPv4 pakety projde jen při přesném big-endian `packetCount`, tabulce délek a odpovídajících IPv4 Total Length. Nulový `packetCount`, délka kratší než 20, trailing bajt, neúplná tabulka délek, neúplný paket nebo neshoda vnitřního `src` či `dst` s datovým řádkem způsobí zahození celé dávky bez předání jediného paketu.

### 16.3 Kryptografie

1. Known-answer testy pro HKDF-SHA-256, HMAC-SHA-256 a AES-256-GCM.
2. `auth-semi` i `auth-full` nesou `timestamp` jako JSON string přesně ve formátu `YYYY-MM-DDTHH:MM:SS.sssZ`; číselná hodnota, neplatné kalendářní datum, malá písmena, chybějící nebo jinak dlouhý zlomek sekund, offset, chybějící `Z` a sekunda `60` jsou odmítnuty.
3. Known-answer test `auth-semi` sestaví `canonical` v pořadí doménový řetězec, `POST`, `info`, přesný timestamp a `requestId`; změna jediného znaku timestampu změní 32bajtový HMAC `tag`. Změna `ACCESS_KEY` nesmí změnit `semiKey` ani tag, protože `auth-semi` nadále závisí pouze na `SERVER_SECRET`.
4. Known-answer test odvození `auth-full` pro pevné `SERVER_SECRET`, `ACCESS_KEY` a `src` ověří přesné `authFullSaltMaterial`, 32bajtový `authFullSalt`, `authFullInfo` a 32bajtový `fullAuthKey` podle kapitoly 7.2.
5. Změna jediného bajtu `SERVER_SECRET` nebo `ACCESS_KEY` musí změnit `fullAuthKey` i výsledný tag při jinak stejném requestu. Tag vypočtený se znalostí pouze jednoho z obou tajemství nesmí projít ověřením.
6. Known-answer test `auth-full` sestaví `canonical` v pořadí doménový řetězec, `POST`, `data`, `src`, přesný timestamp, `requestId` a `SHA256HEX(body)` a tag vypočte výhradně jako `HMAC-SHA-256(fullAuthKey, canonical)`; změna timestampu nebo jediného bajtu body tag zneplatní.
7. Server přijme UTC časy s odchylkou přesně `-60.000`, `0.000` a `+60.000` s a odmítne `-60.001` a `+60.001` s; porovnání provádí s přesností na milisekundy a po zpracování nevytvoří timestamp ani nonce záznam.
8. Autentizační pole `tag` v typech `auth-semi` a `auth-full` musí být base64url bez paddingu a dekódovat se na přesně 32 bajtů; porovnání probíhá konstantním časem. Typ `auth-none` pole `tag` NESMÍ obsahovat.
9. `auth-full.src` vybere právě jeden `ACCESS_KEY` a je vstupem odvození i kanonizace; jeho změna změní `fullAuthKey` i tag. Neznámé `src` skončí obecným `401` po odvození s dummy klíčem a každý datový záznam musí mít stejné `src`.
10. Příjemce zahodí řádek, jehož `dst` není jeho vlastní nakonfigurovaná VPN adresa, ještě před výběrem `PAYLOAD_KEY`; pro každou zbývající uspořádanou dvojici `src` a `dst` jednoznačně vybere právě jeden pevný `PAYLOAD_KEY`. Relay tuto mapu nemá.
11. Změna `messageId`, směrovacího pole `src` nebo `dst`, IV, ciphertextu či tagu zneplatní GCM ověření, protože routingová pole zůstávají součástí AAD.
12. `health` IV má 12 bajtů a GCM tag 16 bajtů. `info` salt má 32 bajtů, IV 12 bajtů a GCM tag 16 bajtů; dvě odpovědi mají nové náhodné hodnoty.
13. Klient neparsuje plaintext `info` ani nepředá payload před úspěšným ověřením GCM tagu.
14. Každý přímý datový řádek odpovědi se samostatně ověří pomocí AES-GCM; řádek s neplatným GCM tagem se zahodí bez ovlivnění ostatních platných řádků.
15. Nakonfigurovaný `SERVER_SECRET` o délce nula bajtů se při startu serveru i klienta normalizuje na přesně 32 ASCII znaků `0`. Known-answer test pro prázdnou a explicitně zadanou hodnotu `"00000000000000000000000000000000"` musí dát shodný `semiKey`, klíč odpovědi `info`, `authFullSalt`, `fullAuthKey` i výsledné tagy a nesmí vyvolat `ValueError`. Neprázdný řetězec `"0"` a 32 nulových bajtů `0x00` se nenormalizují a musí dát výsledky odpovídající jejich vlastním přesným bajtům.
16. PHP server i PHP klient používá softwarovou implementaci AES-256-GCM v čistém PHP bez ohledu na to, zda jsou OpenSSL, Sodium nebo jiná kryptografická rozšíření dostupná. Test s dostupnými i nedostupnými rozšířeními musí projít stejnou softwarovou cestou a vytvořit přesně stejné výstupy pro `health`, `info` i end-to-end payload.
17. Pro pevný klíč, IV, AAD a plaintext vytvoří softwarová implementace přesně očekávaný ciphertext a 16bajtový tag podle known-answer vektoru AES-256-GCM. Správně dešifruje platný vektor, odmítne změnu jediného bitu klíče, IV, AAD, ciphertextu či tagu a před úspěšným ověřením tagu nezpřístupní žádný plaintext.
18. Known-answer test `data-mac` sestaví `canonical` z doménového řetězce, akce, `requestId`, desítkového počtu záznamů, uspořádaných dvojic `messageId` a přesného base64url GCM tagu a `SHA256HEX` přesných bajtů `data-result` bez LF. Výsledkem je přesně 32bajtový `HMAC-SHA-256(fullAuthKey, canonical)`, jehož pole `tag` je base64url bez paddingu a dekóduje se na přesně 32 bajtů.
19. Přidání, odstranění nebo přeuspořádání datového řádku, změna jeho `messageId` či GCM tagu, změna počtu záznamů nebo jediného bajtu `data-result` zneplatní `data-mac`. Změnu jiného pole datového řádku při zachovaném GCM tagu odhalí následné AES-GCM ověření. Chybějící, změněný nebo neplatný `data-mac` nesmí způsobit odstranění žádné položky z odchozí fronty ani předání payloadu.
20. Known-answer test `health` pro pevný efektivní `SERVER_SECRET`, `requestId`, IV a veřejná pole ověří přesné `healthSaltMaterial`, `healthSalt`, `healthInfo`, `healthKey`, AAD a 16bajtový GMAC tag nad prázdným plaintextem podle kapitoly 7.3.
21. Změna jediného znaku `serverVersion`, `status`, `description`, `timestamp`, `requestId` nebo base64url IV zneplatní tag `health`. Prázdný, zkrácený nebo prodloužený tag a tag vypočtený s jiným `SERVER_SECRET` jsou odmítnuty a nesmí změnit klientský časový offset.
22. Odpověď s platným tagem pro starý nebo jiný `requestId` je odmítnuta. Replay stejného požadavku `auth-none` může vytvořit více platných serverových odpovědí se stejným `requestId`, ale klient přijme nejvýše odpověď k právě probíhajícímu pokusu a server kvůli tomu nevytvoří persistentní stav.

### 16.4 Fronty, souběh a pády

1. Atomická publikace odolá pádu před a po přejmenování `.receiving` na `.data` bez částečné dávky.
2. Dva workery posílající stejnému cíli neztratí ani nespojí dávku.
3. Odpověď postupně převezme nalezené soubory `.data`, vrátí je jako přímé řádky `data` bez stránkování a bez obálky a po vydání je smaže; jejich počet může překročit 16.
4. Za datovými řádky následuje právě jeden `data-result` s přesnou uzavřenou sadou výsledkových polí a právě jeden závěrečný `data-mac`; prázdný buffer vrátí tyto dva řádky.
5. Pád workeru nebo přerušení spojení po převzetí souboru `.data` do `.sending` může vést ke ztrátě této dávky a následující poll ji znovu nevydá.
6. Dávky zapsané souběžně po počátečním průchodu souborů `.data` zůstanou ve frontě pro další poll a nesmí se ztratit.
7. Jedna plná cílová fronta nezruší již přijaté položky pro jiný cíl a výsledek je přesný.
8. Při prvním nedostatku kapacity daného cíle server právě jednou spustí `sweepQueue(dst, now)` a publikaci celé cílové skupiny právě jednou zopakuje.
9. Pokud se cílová skupina nevejde ani po sweepu, není z ní publikována žádná položka, všechna její `messageId` jsou v `deferredMessageIds` a odpověď má status `200`.
10. Požadavek, jehož všechny odesílané skupiny byly odloženy, přesto postupně vydá nalezené dávky z příchozího bufferu volajícího a vrátí je spolu s `data-result` a `data-mac`; nevrací `429`.
11. V rámci jednoho požadavku proběhne pro každé unikátní `dst` nejvýše jeden kapacitně vyvolaný sweep a nejvýše dva pokusy o publikaci.
12. 16MiB limit započítává pouze neexpirované dosud nevydané buffered záznamy; fyzicky přítomné expirované záznamy kapacitu nesnižují.
13. Expirovaná dávka není vydána; sweep ji maže ve stavu `.receiving`, `.data` i `.sending`.
14. Storage lock skončí po 200 ms řízenou chybou, nikoli blokací workeru.
15. Stejná sada kontraktních testů běží nad každým storage driverem.
16. Název diskové dávky obsahuje právě jeden UTC čas: expiraci ve filename-safe tvaru `YYYY-MM-DDTHH-MM-SS.sssZ` s přesně třemi číslicemi milisekund.
17. Výchozí `BUFFER_DIRECTORY` se vyhodnotí jako `./phpvpn` vůči souboru `phpvpn.php`; validovaná adresa `10.99.99.2` se mapuje na podadresář `10_99_99_2`. Adresář cíle vznikne při prvním čtení nebo zápisu jeho fronty a adresář úspěšně autentizovaného zdroje se best-effort vytvoří pro jeho marker `info.json`.

### 16.5 Statistiky klientů

1. Ovladač `disk` nepoužívá centrální soubor statistik; každý per-client adresář smí obsahovat právě jeden marker `info.json`.
2. Marker vytvořený implementací je prázdný soubor o délce nula bajtů. Jeho obsah se nečte ani neparsuje jako JSON a `lastSeen` se odvozuje výhradně z jeho `mtime`.
3. Chybějící marker se při způsobilé aktualizaci jediným pokusem vytvoří jako prázdný soubor. Existujícímu markeru se jediným pokusem změní pouze `mtime`.
4. `mtime` starý přesně 30 sekund ani čas v budoucnosti aktualizaci nevyvolá; čas starý 31 sekund se aktualizuje na aktuální čas.
5. Selhání načtení `mtime`, vytvoření markeru nebo jeho aktualizace nezmění HTTP status, `data-result`, odpovídající `data-mac`, publikování ani `drainAll`; zejména nevyvolá `500` ani `503`.
6. Dva souběžné workery nepoužijí zámek ani retry; souběžné aktualizace téhož markeru jsou povolené a markery různých klientů se navzájem neovlivní.
7. `info` načte pouze `mtime` markerů nakonfigurovaných klientů, platné hodnoty převede na UTC RFC 3339 a klienty s chybějícím nebo nečitelným markerem vynechá bez interní cesty nebo diagnostiky.
8. Marker se nezapočítá do kapacity fronty, není vydán jako `data` a přežije cílený i oportunistický sweep. Přímý HTTP i HTTPS požadavek na `info.json` a jiné soubory storage rootu je znemožněn.

### 16.6 Omezená ochrana proti replay

1. Dvojí odeslání identického platného requestu uvnitř 60sekundového okna vytvoří dvě zpracování.
2. Tentýž request se stářím 61 s server odmítne obecným `401`; timestamp se nepersistuje a nevznikne nonce ani replay databáze.
3. Dvojí doručení stejné platné odpovědi vede k opětovnému ověření `data-mac` i jednotlivých GCM tagů a payload je předán dvakrát.
4. Opakované `messageId`, IV a autentizační HMAC tag nejsou porovnávány s historií.
5. Replay uvnitř okna může zaplnit frontu, ale nemůže bez klíče změnit chráněný obsah nebo vytvořit nový platný tag.
6. Zdrojový strom a runtime storage po testech neobsahují adresář, tabulku ani soubor určený pro nonce/replay cache.
7. Kopie platného prázdného HTTP pollingu odeslaná útočníkem uvnitř 60sekundového okna úspěšně provede `drainAll` fronty oběti a získá její šifrované záznamy; následný legitimní poll je již nedostane. Opakování scénáře s nově zachycenými pollingy může frontu soustavně vyprazdňovat. Server útok nedetekuje, záznamy neobnoví a nevytvoří replay stav.
8. Vyšší vrstva může po ztrátě vytvořit nové pakety a pokus opakovat, ale test nesmí předpokládat retransmisi pro každý typ provozu ani garantovat, že opakovaný pokus při pokračujícím útoku dorazí.
9. Replay dřívější odpovědi `health` proti novému `auth-none` selže na neshodě `requestId`. Zdržení aktuální platné odpovědi může způsobit pouze starší ověřený čas nebo nedostupnost; bez znalosti `SERVER_SECRET` nelze vytvořit platný tag s budoucím timestampem.

## 17. Kontrolní seznam shody

Implementace je kompatibilní s phpvpn v1 pouze tehdy, když:

- podporuje všechny čtyři endpointy a přesné typy zpráv;
- přijímá `health` pouze metodou `POST` s neautentizovaným `auth-none`, vrací původní `requestId`, IV a GCM tag nad všemi provozními poli a klient použije serverový čas teprve po ověření tagu pomocí efektivního `SERVER_SECRET`;
- při startu serveru i klienta normalizuje pouze přesně prázdný nakonfigurovaný `SERVER_SECRET` na 32 ASCII znaků `0` a pro všechny kryptografické operace používá tuto efektivní hodnotu;
- je-li napsána v PHP, provádí všechny operace AES-256-GCM vždy jedinou přibalenou softwarovou implementací v čistém PHP a nepoužívá pro ně OpenSSL, Sodium ani jiné kryptografické rozšíření;
- přijímá pouze kanonické base64url bez paddingu, ověřuje nulové nevyužité bity zpětným zakódováním nebo ekvivalentní kontrolou a nekanonický text nikdy nepoužije v AAD ani jiném kanonickém řetězci;
- používá normativní kanonizaci, oddělené klíče a délky AES-GCM;
- ověřuje autentizaci před jakoukoli změnou storage;
- vyžaduje v `auth-semi` i `auth-full` přesný UTC string `YYYY-MM-DDTHH:MM:SS.sssZ`, zahrnuje jej v normativním pořadí do HMAC `tag` a odmítá absolutní odchylku větší než 60 s bez perzistence timestampu;
- odvozuje klíč HMAC tagu `auth-full` normativním spojením jediného `SERVER_SECRET`, jediného `ACCESS_KEY` vybraného podle `src` a samotného `src`, takže správný tag vyžaduje znalost obou tajemství;
- vrací nalezené publikované dávky přímo jako `data` bez obálky a bez stránkování, následované jediným `data-result` a jediným závěrečným `data-mac`, který autentizuje uspořádaný seznam `messageId` a GCM tagů i přesný `data-result`;
- používá atomické at-most-once převzetí jednotlivých dávek bez potvrzení, zachovává TTL a přesné částečné výsledky odesílání;
- při nedostatku kapacity provede nejvýše jeden cílený sweep pro každé `dst`, publikaci celé cílové skupiny jednou zopakuje a trvající nedostatek kapacity vyjádří pouze pomocí `deferredMessageIds` v odpovědi `200`, nikdy pomocí `429`;
- vede best-effort `lastSeen` jako `mtime` prázdného per-client markeru `info.json`, omezuje jeho aktualizaci na hodnoty starší než 30 sekund a nikdy kvůli statistice nemění datový výsledek;
- znemožňuje přímý webový přístup k bufferům, statistikám a ostatním souborům storage rootu;
- uplatňuje stejné chování přes HTTP, HTTPS a proxy;
- mimo kontrolu stáří timestampu neimplementuje serverovou ani klientskou replay deduplikaci a nepersistuje timestamp ani nonce;
- výslovně zachovává přijaté riziko, že replay odposlechnutého HTTP pollingu uvnitř platného okna může opakovaně vyprázdnit a nenávratně zničit příchozí frontu oběti;
- udržuje jediný pevný payload klíč pro každou uspořádanou dvojici `src` a `dst` mimo relay server;
- respektuje zveřejněné aktivní limity bez ohledu na použitý storage driver;
- neprozrazuje tajemství ani interní cesty v odpovědích a logu.
