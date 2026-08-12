# Návrh multiplatformního klienta phpvpn

## Účel

Tento dokument popisuje návrh nativního klienta pro protokol phpvpn v1. Klient funguje jako krátce připojovaný relayový peer: zachytává IPv4 pakety z virtuálního rozhraní, end-to-end je šifruje pro konkrétního příjemce, odesílá je přes endpoint `data` a přijaté, ověřené pakety opět zapisuje do virtuálního rozhraní.

Primárním cílem je Windows. Návrh současně odděluje platformně nezávislé jádro od síťového rozhraní a integrace operačního systému, aby jej bylo možné přenést na macOS a Linux. Doporučeným jazykem je C++20 s malou, explicitní C++ platformní vrstvou; čisté C je možné, ale bude znamenat více ruční správy zdrojů a chybových stavů.

Server nezná payload klíče ani IP obsah. Klient zajišťuje výběr klíče, AES-256-GCM ochranu payloadu, validaci a předání paketů TUN/TAP rozhraní.

## Architektura

Jádro klienta nesmí záviset na API konkrétní platformy. Je rozděleno do těchto modulů:

| Modul | Odpovědnost |
|---|---|
| `Config` | Načtení, přísná validace a držení konfigurace v paměti. |
| `Protocol` | Kanonizace, JSON/NDJSON, `health`, `info`, `data`, HMAC/HKDF a base64url. |
| `Crypto` | AES-256-GCM pro payload, bezpečné mazání tajných hodnot a generování IV z CSPRNG. |
| `Transport` | HTTP/HTTPS POST, časové limity, kontrola velikosti odpovědi a politika certifikátů. |
| `Tunnel` | Abstrakce čtení a zápisu surových IPv4 paketů do virtuálního rozhraní. |
| `Queues` | Fronty podle cílové VPN adresy, plánování flushů, limity paměti a souhrnné statistiky zahozených paketů. |
| `Runtime` | Stavový automat připojení, vlákna nebo event loop, ukončení a srozumitelný log. |

Platformní implementace poskytne pouze rozhraní `Tunnel` a případné API pro služby, privilegia a síťovou konfiguraci. Pro HTTP lze použít jednotnou knihovnu dostupnou na všech třech platformách, například libcurl. Pro nativní kryptografii lze použít ověřenou knihovnu s interoperabilní implementací AES-GCM; požadavek čistého PHP AES-GCM z protokolu se týká PHP implementací, nikoli nativního klienta.

## Konfigurační soubor

Konfigurace je JSON, načtený jednou při startu. Po úspěšné validaci zůstává v paměti; běžný datový tok znovu nečte disk. Chybná nebo neúplná konfigurace brání spuštění.

Navržená struktura:

```json
{
  "masterSetup": {
    "serverUrl": "https://vpn.example.test/phpvpn.php",
    "trustAnyTlsCertificate": true,
    "serverSecret": "server-shared-secret",
    "virtualIp": "10.99.99.2",
    "accessKey": "64-znakový-malý-hex"
  },
  "communicationContracts": {
    "10.99.99.3": {
      "payloadKey": "64-znakový-malý-hex"
    }
  }
}
```

`serverUrl` obsahuje celou URL serverového souboru `phpvpn.php` a může používat HTTP nebo HTTPS. `accessKey` odpovídá protokolovému `ACCESS_KEY`; je přesně 32 bajtů zapsaných jako 64 malých hexadecimálních znaků. Každý `payloadKey` je protokolový `PAYLOAD_KEY`: pevný 32bajtový klíč pro přesně danou uspořádanou dvojici `src`, `dst`. Nejde o uživatelské heslo ani o klíč odvozovaný za běhu.

`trustAnyTlsCertificate` má podle současného rozhodnutí výchozí hodnotu `true`. Při HTTPS pak klient přijme libovolný certifikát pro nakonfigurovanou URL včetně self-signed nebo nedůvěryhodného certifikátu. Toto nastavení má být viditelně vypsáno při startu a uživatel jej má volit vědomě; bezpečnější budoucí profil nastaví hodnotu na `false` a bude vyžadovat standardní validaci řetězce a názvu hostitele.

Konfigurace nesmí být běžně vypisována do logu. Log smí zobrazit adresu serveru, virtuální IP a seznam vzdálených IP bez klíčů a secretů.

## Stavový automat připojení

1. **Načtení konfigurace.** Klient validuje URL, VPN IP, hex formát a délky klíčů, jedinečnost kontraktů a dostupnost virtuálního rozhraní.
2. **Health.** Vygeneruje nový UUIDv4 `requestId` a odešle `auth-none` na `?action=health`.
3. **Ověření serveru a času.** Pokud odpověď odpovídá syntaxi `health` a nese stav `running`, klient ověří, že `requestId` souhlasí, a ověří GCM tag podle protokolu. Teprve potom přijme čas serveru a vypočítá `timeDelta`; lokální systémový čas nikdy nemění.
4. **Časová rezerva.** Serverový čas se plánuje s milisekundovou přesností. Klient používá efektivní serverový čas `localNow + timeDelta - safetyMargin`. Výchozí konzervativní mez je jedna sekunda za serverem; přesná hodnota má být konfigurovatelná či později definitivně stanovena. Specifikace serveru musí být upravena z sekund na milisekundy.
5. **Info.** Klient autentizovaně odešle `auth-semi` na `?action=info`, ověří a dešifruje odpověď a uloží obdržené limity a doporučené provozní hodnoty. Tvrdé limity, zejména maximální velikost těla požadavku, musí dodržet. Doporučené intervaly pollingů slouží jako plánovač, pokud nejsou v budoucí specifikaci označené za závazné.
6. **Provoz.** Po úspěšném `health` a `info` se zahájí čtení tunelu a pravidelný polling endpointu `data`.
7. **Obnova.** Při dočasné síťové nebo autentizační chybě klient nevypíná tunel bezprostředně, loguje stav, používá omezený backoff a opakuje `health` a `info` před návratem do provozu. Chyba konfigurace nebo neplatný server secret vyžaduje zásah uživatele.

Příkazový výstup má stručně ukazovat fáze: připojování k serveru, dostupný server, úspěšně ověřený `SERVER_SECRET`, zjištěné provozní limity a stav tunelu. Tajemství ani autentizační tagy se nevypisují.

## Odchozí tok paketů

Virtuální rozhraní dodává kompletní IPv4 pakety. Klient z hlavičky IPv4 přečte cílovou adresu a pracuje jen s adresami VPN prostoru.

1. Paket určený vlastní virtuální IP se neodesílá jako odchozí zpráva přes relay.
2. Pro cíl, který nemá platný `communicationContract`, se paket tiše zahodí. Log nehlásí každý paket, ale například jednou za jednu až pět sekund souhrn po cílové IP.
3. Pro známý cíl se paket přidá do FIFO bucketu dané cílové IP. Fronty zachovávají pořadí paketů pro jednotlivého příjemce.
4. Flush nastane po doporučeném minimálním pollingovém intervalu, nebo když se další paket bezpečně nevejde do plánovaného requestu. Je-li buffer plný před povoleným časem pollingu, další pakety se zahazují a zahrnou do souhrnného logu.
5. Po vypršení intervalu se odešle request `data`; i při prázdných frontách se odešle prázdný autentizovaný polling, aby klient vyzvedl příchozí data.

### Balení jedné cílové dávky

Jedna dávka pro danou vzdálenou IP má binární plaintext:

```text
packetCount : unsigned 32-bit integer
packetLength[0..packetCount-1] : unsigned 16-bit integer pro každý paket
packetBytes[0..packetCount-1] : surové IPv4 pakety ve stejném pořadí
```

Musí být pevně stanoveno pořadí bajtů vícebajtových čísel; doporučený je network byte order (big-endian). Každý paket musí mít délku v rozsahu reprezentovatelném 16 bity a parser na příjmu musí ověřit, že součet délek přesně odpovídá zbývajícím bajtům. Přídavný náhodný „salt“ v plaintextu se nepřidává: AES-GCM již používá pro každý záznam nový náhodný 12bajtový IV a další náhodná data bezpečnost nezvyšují.

Plaintext se zašifruje AES-256-GCM klíčem z kontraktu přesně pro uspořádanou dvojici lokální `src` a vzdálená `dst`. Použije se nový 12bajtový IV, 16bajtový tag a normativní AAD z `protocol.md`, včetně směrovacích polí a `messageId`. IV, ciphertext i tag se kódují kanonickým base64url bez paddingu a tvoří normativní řádek `data`. Jednotlivé řádky se spojí LF do NDJSON, předcházené `auth-full`, jehož HMAC pokrývá přesné bajty těla.

### Velikostní plánování

Pro dávku s `n` pakety a součtem jejich délek `S` je délka binárního plaintextu `4 + 2n + S`. Base64url délka hodnoty o `x` bajtech je `4 × floor(x / 3) + (0, 2, 3)` pro zbytek dělení `x` hodnoty `0, 1, 2`. Samostatně se započítají IV, GCM tag, JSON syntaxe, `messageId`, `src`, `dst`, LF řádky i řádek `auth-full`.

Plánovač může předem používat přesný výpočet, včetně rezervy 256 bajtů pro budoucí rozšíření. Závazná poslední kontrola však vždy změří skutečně sestavené tělo requestu v bajtech. Pokud přesahuje `MAX_REQUEST_BODY_BYTES`, klient odebere poslední přidanou dávku či paket a zařadí jej do dalšího requestu. Nesmí se spoléhat pouze na hrubý přepočet „tři čtvrtiny“, protože JSON a odděleně kódované hodnoty mají vlastní režii.

## Příjem dat

Odpověď na `data` je NDJSON: nula nebo více řádků `data`, právě jeden `data-result` a poslední `data-mac`.

1. Klient ověří strukturu odpovědi, `data-result` a závěrečný `data-mac`. Ten chrání počet, pořadí a úplnost datových řádků.
2. U každého řádku nejdříve ověří, že `dst` je jeho lokální virtuální IP. Cizí řádek zahodí ještě před výběrem payload klíče.
3. Podle přesné dvojice `src`, `dst` vyhledá kontrakt. Neexistuje-li, řádek zahodí a souhrnně zaloguje.
4. Ověří kanonický base64url, délky IV a tagu a provede AES-GCM dešifrování s příslušným `PAYLOAD_KEY` a normativním AAD. Neplatný GCM tag znamená zahození pouze daného řádku.
5. Ověří formát binární dávky: hlavičku počtu, tabulku délek, přesný počet a hranice surových bajtů. Nesmí předat částečně rozbalenou nebo neověřenou dávku.
6. Platné IPv4 pakety zapíše do tunelu sekvenčně v pořadí, v jakém byly v dávce uvedeny.

Protokol je at-most-once na serverovém bufferu. Ztracená HTTP odpověď nebo pád po převzetí může znamenat ztrátu dávky; klient nemá předstírat garantované doručení. Aplikační vrstvy nad VPN mohou používat vlastní retransmise.

## Bezpečnostní model a přijatá rizika

- Relay server nemá payload klíče a nemůže dešifrovat IP obsah; důvěrnost a integritu jednoho payloadu zajišťuje AES-256-GCM mezi klienty.
- `health` autentizuje serverový čas a veřejná pole pomocí `SERVER_SECRET`; `info` a `data` vyžadují přesnou protokolovou autentizaci.
- HTTP je podporované, ale aktivní útočník může zachycený platný polling v časovém okně zopakovat. Tím může vyzvednout a nenávratně odstranit ciphertext z příchozí fronty oběti, i když plaintext bez payload klíče nepřečte. Pro běžný provoz se proto doporučuje HTTPS.
- Pokud `trustAnyTlsCertificate` zůstane `true`, HTTPS nechrání klienta spolehlivě proti podvržení serveru na úrovni TLS. Nastavení je vědomě akceptované kompatibilitní riziko a musí být zřetelně viditelné v konfiguraci i startovním logu.
- Protokol nebrání replayi uvnitř platného časového okna, neposkytuje exactly-once, potvrzení ani ochranu dostupnosti. Klient nemá vytvářet vlastní replay cache, pokud se současně nezmění normativní specifikace.
- Klíče, tagy, ciphertexty, celé requesty a interní cesty se nesmí zapisovat do logů. Konfigurační soubor má mít omezená přístupová oprávnění.

## Specifika platforem

### Windows

Windows je primární platforma. Použije se ovladač TUN/TAP kompatibilní s požadovaným způsobem provozu; při výběru je vhodné preferovat aktivně udržované řešení s dokumentovaným uživatelským API a možností tichého nasazení. Aplikace obvykle potřebuje administrátorské oprávnění pro instalaci ovladače, vytvoření či konfiguraci rozhraní a směrování.

Službový režim je vhodný pro dlouhodobý běh a automatické obnovení. Konzolový režim zůstane pro diagnostiku, s čitelnými fázemi připojení a agregovanými statistikami. Síťovou konfiguraci, DNS a routes je nutné měnit pouze explicitně a při ukončení obnovit předchozí stav.

### macOS

V macOS je doporučené použít nativní síťový mechanismus podporovaný systémem, typicky Network Extension s packet tunnel providerem. Ten vyžaduje správné podepisování, entitlementy a uživatelské schválení. Společné C++ jádro zůstává stejné; malý host/provider obstará čtení a zápis paketů a předá je přes stabilní C nebo C++ rozhraní.

Alternativy založené na `/dev/tun` nejsou na moderním macOS obecně přenositelné ani doporučené pro distribuovaný produkt. Je nutné počítat s pravidly sandboxingu, životním cyklem rozšíření a omezeními distribuce.

### Linux

Na Linuxu je přirozeným rozhraním TUN zařízení `/dev/net/tun`. Klient vytvoří TUN rozhraní, přiřadí virtuální IP a nastaví potřebné routy přes netlink nebo spolehlivý systémový integrační modul. Operace vyžadují oprávnění `CAP_NET_ADMIN` nebo vhodně omezený privilegovaný pomocný proces.

Produkční služba může běžet pod systemd s minimálními capability, restartovací politikou a ochranou konfiguračního souboru. Implementace nemá předpokládat jeden konkrétní firewall ani síťový manažer; jejich integrace má být volitelná.

## Doporučené pořadí implementace

1. Implementovat a testovat čisté multiplatformní jádro: konfiguraci, kanonickou base64url, HMAC/HKDF, AES-GCM, čas a parsery.
2. Implementovat HTTP transport a kontraktní testy `health`, `info` a prázdného `data` pollingu proti serveru.
3. Implementovat fronty, přesné plánování velikosti a odchozí/příchozí datové řádky bez tunelu, s testovacími binárními pakety.
4. Přidat Windows tunel a end-to-end test dvou klientů.
5. Přidat linuxovou TUN implementaci, poté macOS Network Extension wrapper.
6. Doplnit zátěžové testy, pádové scénáře, neplatné NDJSON, limity těla, ztrátu odpovědi a bezpečnostní regresní testy.

Před implementací je nutné promítnout milisekundový formát času do `protocol.md` a definitivně normativně definovat binární payload formát, byte order, velikostní politiku a chování při přeplněných frontách.
