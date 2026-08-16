# Návrh multiplatformního klienta phpvpn

## Účel

Tento dokument popisuje návrh nativního klienta pro protokol phpvpn v1. Klient funguje jako krátce připojovaný relayový peer: zachytává IPv4 pakety z virtuálního rozhraní, end-to-end je šifruje pro konkrétního příjemce, odesílá je přes endpoint `data` a přijaté, ověřené pakety opět zapisuje do virtuálního rozhraní.

Primárním cílem je Windows. Návrh současně odděluje platformně nezávislé jádro od síťového rozhraní a integrace operačního systému, aby jej bylo možné přenést na macOS a Linux. Doporučeným jazykem je C++20 s malou, explicitní C++ platformní vrstvou; čisté C je možné, ale bude znamenat více ruční správy zdrojů a chybových stavů.

Server nezná payload klíče ani IP obsah. Klient zajišťuje výběr klíče, AES-256-GCM ochranu payloadu, validaci a předání paketů virtuálnímu L3 rozhraní.

## Architektura

Jádro klienta nesmí záviset na API konkrétní platformy. Je rozděleno do těchto modulů:

| Modul | Odpovědnost |
|---|---|
| `Config` | Načtení, přísná validace a držení konfigurace v paměti. |
| `Protocol` | Kanonizace, JSON/NDJSON, `health`, `info`, `data`, HMAC/HKDF a base64url. |
| `Crypto` | AES-256-GCM pro payload, bezpečné mazání tajných hodnot a generování IV z CSPRNG. |
| `Transport` | HTTP/HTTPS POST, časové limity, kontrola velikosti odpovědi a politika certifikátů. |
| `Tunnel` | Abstrakce čtení a zápisu surových IPv4 paketů do virtuálního rozhraní. |
| `Queues` | Odchozí fronty podle cílové VPN adresy, příchozí fronty podle zdrojové VPN adresy, plánování flushů, logické per-peer rezervace, limity paměti a souhrnné statistiky zahozených paketů. |
| `Runtime` | Stavový automat připojení, vlákna nebo event loop, ukončení a srozumitelný log. |

Platformní implementace poskytne pouze rozhraní `Tunnel` a případné API pro služby, privilegia a síťovou konfiguraci. Pro HTTP lze použít jednotnou knihovnu dostupnou na všech třech platformách, například libcurl. Pro nativní kryptografii lze použít ověřenou knihovnu s interoperabilní implementací AES-GCM; požadavek čistého PHP AES-GCM z protokolu se týká PHP implementací, nikoli nativního klienta.

### Běhový model

Společné jádro používá tři sériově pracující role. Implementace je smí realizovat třemi vlákny nebo ekvivalentním event-loopem, ale vlastnictví dat a pravidla souběhu musí zůstat stejná.

1. **Vstupní pracovník tunelu** je jediným čtenářem virtuálního L3 rozhraní. Provede úplnou validaci IPv4 paketu podle pravidel níže, vyžaduje lokální virtuální IP jako vnitřní zdroj a zařadí jej výhradně do FIFO fronty kontraktu odpovídajícího jeho vnitřnímu cíli. Nešifruje ani nečeká na síť.
2. **Síťový pracovník** je jediným vlastníkem `health`, `info` a `data` requestů, plánování dávek, šifrování, ověřování odpovědí a dešifrování. V jednom okamžiku smí existovat nanejvýš jeden probíhající `data` request.
3. **Výstupní pracovník tunelu** je jediným zapisovatelem do virtuálního L3 rozhraní. Sekvenčně odebírá ověřené IPv4 pakety z příchozích per-peer FIFO front účtovaných podle zdrojové VPN adresy a zapisuje je do virtuálního rozhraní. V každé peer frontě zachovává FIFO; vzájemné pořadí obsluhy různých zdrojových peerů tato specifikace neurčuje.

Fronty se zamykají pouze krátce při vložení, odebrání nebo atomickém převzetí položek a při změně jejich velikostních počítadel. Zámek nesmí být držen během šifrování, dešifrování, HTTP komunikace, čekání na odpověď ani zápisu do tunelu. První pracovník proto pokračuje ve čtení i v době, kdy síťový pracovník zpracovává předchozí request.

## Konfigurační soubor

Konfigurace je JSON ve vlastnictví služby. Služba ji načte a přísně validuje při každém novém pokusu o připojení; po úspěšné validaci zůstává po dobu připojení v paměti a běžný datový tok znovu nečte disk. Chybná nebo neúplná konfigurace brání spuštění VPN provozu. Tray ani konzolová aplikace konfiguraci přímo nečtou ani nezapisují do souboru a nevalidují ji za službu. Přes veřejné IPC však mohou získat nebo odeslat celý konfigurační JSON, a proto mohou při obsluze `get-config` a `set-config` dočasně držet i jeho tajné hodnoty v paměti.

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
  "runtime": {
    "maxOutboundQueueBytes": 67108864,
    "maxInboundQueueBytes": 67108864,
    "minPerPeerQueueBytes": 131072,
    "maxResponseBodyBytes": 67108864,
    "maxNdjsonLineBytes": 67108864,
    "maxDataRecordsPerResponse": 65535,
    "responseTimeoutSeconds": 30
  },
  "communicationContracts": {
    "10.99.99.3": {
      "payloadKey": "64-znakový-malý-hex"
    }
  }
}
```

`serverUrl` obsahuje celou URL serverového souboru `phpvpn.php` a klient záměrně podporuje HTTP i HTTPS. `accessKey` odpovídá protokolovému `ACCESS_KEY`; je přesně 32 bajtů zapsaných jako 64 malých hexadecimálních znaků. Každý `payloadKey` je protokolový `PAYLOAD_KEY`: pevný 32bajtový klíč pro přesně danou uspořádanou dvojici `src`, `dst`. Nejde o uživatelské heslo ani o klíč odvozovaný za běhu.

`trustAnyTlsCertificate` má podle současného rozhodnutí výchozí hodnotu `true`. Při HTTPS pak klient záměrně přijme libovolný certifikát pro nakonfigurovanou URL, včetně self-signed certifikátu a certifikátu vydaného neznámou nebo nedůvěryhodnou certifikační autoritou. Jde o vědomě přijaté kritické bezpečnostní riziko: klient nebude ve výchozím stavu ověřovat řetězec důvěry ani důvěryhodnost vydávající autority. Implementace klienta s tímto rizikem záměrně počítá. Bezpečnější budoucí profil může nastavit hodnotu na `false` a vyžadovat standardní validaci řetězce a názvu hostitele.

Konfigurace nesmí být běžně vypisována do logu. Log smí zobrazit adresu serveru, virtuální IP a seznam vzdálených IP bez klíčů a secretů. To neomezuje záměrně veřejné IPC příkazy `get-config` a `set-config`.

`maxOutboundQueueBytes` je celková maximální velikost paketů načtených z virtuálního L3 rozhraní, které čekají na odeslání na server. `maxInboundQueueBytes` je celková maximální velikost ověřených a dešifrovaných paketů čekajících na zápis do tohoto rozhraní. `minPerPeerQueueBytes` je minimální logická rezervace pro každou možnou per-peer frontu v každém směru. Všechny tři hodnoty jsou uživatelsky konfigurovatelná kladná celá čísla v bajtech; při vynechání mají první dvě výchozí hodnotu 64 MiB (`67108864`) a třetí 128 KiB (`131072`).

Ve výchozím adresním prostoru existuje 253 platných klientských adres `10.99.99.2` až `10.99.99.254`, takže jeden lokální klient má nejvýše 252 vzdálených peerů. Při jiném společně nakonfigurovaném adresním prostoru se `maxRemotePeers` odvodí z počtu všech možných platných klientských adres po vyloučení jediné lokální adresy. Nejde pouze o počet právě uvedených `communicationContracts`, ale o počet všech peerů, pro které v daném adresním prostoru může existovat fronta.

Před každým pokusem o připojení MUSÍ konfigurace pro odchozí a příchozí směr samostatně splnit:

```text
maxOutboundQueueBytes >= maxRemotePeers × minPerPeerQueueBytes
maxInboundQueueBytes  >= maxRemotePeers × minPerPeerQueueBytes
```

Nesplnění kteréhokoli invariantu konfiguraci odmítne ještě před `health` a před vytvořením tunelu. `minPerPeerQueueBytes` je pouze logická rezervace pro admission účetnictví; implementace NESMÍ fyzicky předalokovat 252 bufferů ani jiný maximální počet per-peer front. Skutečná fronta a její paměť vznikají líně až při prvním přijatém paketu daného peeru.

Při každém pokusu o zařazení nového paketu se v příslušném směru použije obsazenost po hypotetickém vložení a MUSÍ platit obě podmínky: skutečný součet obsazeností všech peer front nepřekročí odpovídající `maxQueueBytes` a současně

```text
sum(max(obsazenostPeerFronty, minPerPeerQueueBytes)) přes všechny možné peer fronty <= maxQueueBytes
```

Odchozí směr používá jako `maxQueueBytes` hodnotu `maxOutboundQueueBytes` a účtuje fronty podle cílové VPN adresy; příchozí směr používá `maxInboundQueueBytes` a účtuje je podle zdrojové VPN adresy. Z uvedeného invariantu plyne, že jediná peer fronta může dosáhnout nanejvýš `maxQueueBytes - (maxRemotePeers - 1) × minPerPeerQueueBytes`. Pro výchozí hodnoty je tato mez 32.625 MiB, tedy 32 MiB + 640 KiB (`34209792` bajtů). Logická rezervace se počítá v každém směru samostatně a neznamená vlastnictví předem přidělené fyzické paměti.

`maxResponseBodyBytes`, `maxNdjsonLineBytes`, `maxDataRecordsPerResponse` a `responseTimeoutSeconds` jsou uživatelsky konfigurovatelná kladná celá čísla. Při vynechání mají po řadě výchozí hodnoty `67108864` bajtů (64 MiB), `67108864` bajtů, `65535` záznamů a `30` sekund. Prozatím pro ně neexistuje globální horní strop; úplná validace konfigurace však vždy odmítne nulu, zápornou hodnotu, necelé číslo nebo hodnotu nereprezentovatelnou podporovaným celočíselným typem implementace.

`maxResponseBodyBytes` omezuje počet přesných bajtů těla jedné HTTP odpovědi. Transport jej průběžně počítá při přijímání odpovědí `health`, `info` i `data`; při překročení limitu request ukončí, neúplnou odpověď nepoužije a nepředá ji protokolovému parseru. `maxNdjsonLineBytes` omezuje počet bajtů jednoho řádku NDJSON odpovědi endpointu `data`, včetně jeho ukončovacího LF. NDJSON parser délku kontroluje průběžně a příliš dlouhý řádek odmítne ještě před base64 dekódováním jeho polí. `maxDataRecordsPerResponse` je maximální počet záznamů, tedy NDJSON řádků typu `data`, které klient přijme v jedné odpovědi endpointu `data`; parser po překročení limitu odmítne celou odpověď ještě před následným kryptografickým zpracováním překračujícího řádku.

`responseTimeoutSeconds` je celková doba od zahájení HTTP requestu do přijetí celé odpovědi a platí pro `health`, `info` i `data`. Při jejím překročení klient request ukončí, nepoužije žádná neúplná data a přejde do běžné recovery logiky odpovídající dané fázi. Limity celkové velikosti odpovědi, délky NDJSON řádku, počtu datových řádků a celkového času se kontrolují samostatně a žádný z nich nenahrazuje ostatní.

## Stavový automat připojení

1. **Načtení konfigurace.** Klient validuje URL, VPN IP, hex formát a délky klíčů, jedinečnost kontraktů a dostupnost podporované platformní vrstvy. Na Windows v této fázi smí ověřit distribuční manifest, DLL, architekturu a povinné Wintun API, ale NESMÍ ještě vytvořit ani otevřít adaptér, zahájit Wintun session nebo měnit síť.
2. **Health.** Vygeneruje nový UUIDv4 `requestId` a odešle `auth-none` na `?action=health`.
3. **Ověření serveru a času.** Pokud odpověď odpovídá syntaxi `health` a nese stav `running`, klient ověří, že `requestId` souhlasí, a ověří GCM tag podle protokolu. Teprve potom přijme čas serveru a vypočítá `timeDelta`; lokální systémový čas nikdy nemění.
4. **Časová rezerva.** Serverový čas se plánuje s milisekundovou přesností. Klient používá efektivní serverový čas `localNow + timeDelta - safetyMargin`. Výchozí konzervativní mez je jedna sekunda za serverem; přesná hodnota má být konfigurovatelná či později definitivně stanovena.
5. **Info.** Klient autentizovaně odešle `auth-semi` na `?action=info`, ověří a dešifruje odpověď a uloží obdržené limity a doporučené provozní hodnoty. Tvrdé limity, zejména maximální velikost těla požadavku, musí dodržet. Doporučené intervaly pollingů slouží jako plánovač, pokud nejsou v budoucí specifikaci označené za závazné.
6. **Ověření přístupového klíče.** Ještě před spuštěním tří pracovních rolí klient provede prázdný autentizovaný polling `data`. Tím ověří `ACCESS_KEY` i úplnou odpověď `data`. Přijaté platné dávky zpracuje stejně jako při běžném pollingu a připraví je pro výstupní pracovník.
7. **Spuštění tunelu a provoz.** Teprve po úspěšném `health`, `info` a prvním prázdném `data` pollingu služba na Windows vytvoří nebo otevře jediný vlastní Wintun adaptér, zahájí jedinou session, nakonfiguruje vlastní adresu a routy a spustí tři pracovní role. Čtení tunelu a pravidelný polling endpointu `data` nesmějí začít dříve.
8. **Obnova.** Při dočasné síťové nebo autentizační chybě klient nevypíná tunel bezprostředně, loguje stav, používá omezený backoff a opakuje `health`, `info` a prázdný `data` polling před návratem do provozu. Chyba konfigurace nebo neplatný server secret vyžaduje zásah uživatele.

Příkazový výstup má stručně ukazovat fáze: připojování k serveru, dostupný server, úspěšně ověřený `SERVER_SECRET`, zjištěné provozní limity a stav tunelu. Tajemství ani autentizační tagy se nevypisují.

## Odchozí tok paketů a plánování requestů

Virtuální rozhraní dodává kompletní IPv4 pakety. Před zařazením musí vstupní pracovník provést úplnou strukturální validaci podle následující podkapitoly. Vnitřní zdroj musí přesně odpovídat lokální virtuální IP a vnitřní cíl musí být platná VPN IP s existujícím `communicationContract`; výběr fronty i payload klíče proto vždy vychází ze stejné uspořádané dvojice `(lokální IP, vnitřní cíl)`. Paket určený vlastní virtuální IP, paket s neplatnou hlavičkou nebo paket bez platného kontraktu se přes relay neodesílá; zahození se loguje pouze souhrnně po důvodu a cíli.

Odchozí per-peer fronta se účtuje podle cílové VPN adresy paketu. Pokud by vložení nového paketu porušilo globální `maxOutboundQueueBytes` nebo logickou per-peer rezervaci, vstupní pracovník zahodí pouze tento nový paket. NESMÍ kvůli němu odstranit ani přeuspořádat již zařazené pakety. Zahození eviduje agregovaně podle stabilního bezpečného důvodu a cílového peeru, bez obsahu paketu, klíčů, tagů, ciphertextu nebo jiných citlivých hodnot.

Síťový pracovník pro každý request vybírá pouze requestově velký prefix front. Jeden datový element patří přesně jedné uspořádané dvojici `src`, `dst` a nese dávku paketů jen pro tento cíl. Binární plaintext, byte order a validační pravidla jsou normativně definované v `protocol.md`. Síťový pracovník jej šifruje AES-256-GCM příslušným `PAYLOAD_KEY` a vytváří normativní řádek `data`.

### Úplná validace vnitřních IPv4 paketů

Klient považuje IPv4 paket za platný pouze tehdy, pokud má alespoň 20 bajtů, první nibble udává verzi 4, `IHL` je alespoň 5 a délka hlavičky nepřesahuje délku bufferu. Pole `total length` musí být alespoň tak velké jako délka hlavičky a musí se přesně rovnat délce celého bufferu; klient nesmí akceptovat přívěsné ani chybějící bajty. Kontrolní součet IPv4 hlavičky musí být platný. Implementace před každým čtením pole ověří jeho hranice a nesmí z neověřených hodnot provádět alokaci ani výpočet ukazatelů.

Klient fragmenty znovu neskládá ani je neinterpretuje jako neúplný paket. Platný IPv4 fragment smí projít jako samostatný celý paket, pokud splňuje výše uvedenou strukturální kontrolu a kontrolu vnitřních adres. Fragmenty nikdy nesmějí obcházet vazbu adres ani vytvářet vlastní stav skládání.

Při odesílání musí být vnitřní zdroj shodný s lokální virtuální IP a vnitřní cíl shodný s cílem kontraktu, do jehož fronty byl paket zařazen. Po přijetí musí být vnitřní zdroj shodný s `src` datového řádku a vnitřní cíl shodný s lokální virtuální IP, která je současně `dst` datového řádku. Kontrola se provede po úspěšném AES-GCM dešifrování a rozbalení dávky, ale před vložením paketu do příchozí fronty nebo zápisem do tunelu. Každý nevyhovující paket se samostatně zahodí a eviduje pouze souhrnně; neplatný paket nesmí zablokovat zpracování ostatních paketů stejné dávky.

### Pollingový interval

Klient používá absolutní lokální hranice 20 ms a 2 s. Po ověřené odpovědi `info` vypočte:

```text
effectiveMinPollIntervalMs = clamp(minDataPollIntervalMs, 20, 2000)
effectiveMaxPollIntervalMs = clamp(maxDataPollIntervalMs, 20, 2000)
```

Síťový pracovník provede polling i při prázdných odchozích frontách a zahájí jej nejpozději po efektivním maximu, je-li volný. Timeout může skutečný interval prodloužit; klient kvůli tomu neotevře paralelní request.

### Velikostní a spravedlivé plánování

Pracovník používá rozpočet `maxRequestBodyBytes - 1024`; 1 KiB je pevná rezerva nad očekávanou serializační režií. Konečná kontrola vždy změří skutečně sestavené tělo v bajtech. Pokud paket nebo z něj vytvářený datový element z čela per-peer FIFO nelze vložit do aktuální dávky, ale vešel by se do samostatného jinak prázdného platného requestu, zůstane na čele této FIFO fronty pro další request.

Pokud se paket nebo datový element z čela per-peer FIFO nevejde ani do samostatného jinak prázdného platného requestu po započtení všech přesných režijních bajtů šifrování, kódování, NDJSON a autentizačního řádku, síťový pracovník jej atomicky odstraní z čela fronty a trvale zahodí. Toto odstranění NESMÍ změnit vzájemné FIFO pořadí zbývajících paketů. Klient zahození agregovaně eviduje podle důvodu a cíle; do logu NESMÍ zapsat obsah paketu, klíče, tagy, ciphertext ani jiné citlivé hodnoty. MTU tunelu a nakonfigurovaná nebo odvozená maximální velikost datového elementu jsou preventivní limity, ale toto povinné runtime rozhodnutí a přesné změření skutečně sestaveného requestu nenahrazují.

Výběr je spravedlivý: pracovník vede kruhové pořadí cílů, v první fázi se pokusí zařadit alespoň jeden paket z každé neprázdné fronty a v dalších průchodech doplňuje pakety opět kruhově, dokud zbývá rozpočet. Po každém requestu posune počáteční cíl. Jedna dlouhá fronta tak nevytlačuje krátké fronty; pakety v každé konkrétní frontě zůstávají FIFO. FIFO určuje neměnné pořadí paketů uvnitř jedné peer fronty, zatímco kruhové plánování určuje pouze spravedlivé střídání mezi různými peer frontami při sestavení requestu. Kruhové plánování NESMÍ měnit admission účetnictví, obcházet logické rezervace ani přeuspořádat pakety uvnitř fronty.

## Příjem dat

Odpověď na `data` je NDJSON: nula nebo více řádků `data`, právě jeden `data-result` a poslední `data-mac`.

1. Transport nejprve průběžně uplatní `maxResponseBodyBytes` a `responseTimeoutSeconds`. NDJSON parser potom průběžně uplatní `maxNdjsonLineBytes` ještě před base64 dekódováním příliš dlouhého řádku, počítá řádky typu `data` a odmítne celou odpověď, jakmile jejich počet překročí `maxDataRecordsPerResponse`; překračující řádek nesmí před odmítnutím kryptograficky zpracovat. Poté klient ověří strukturu odpovědi, `data-result` a závěrečný `data-mac`. Ten chrání počet, pořadí a úplnost datových řádků.
2. U každého řádku nejdříve ověří, že `dst` je jeho lokální virtuální IP. Cizí řádek zahodí ještě před výběrem payload klíče.
3. Podle přesné dvojice `src`, `dst` vyhledá kontrakt. Neexistuje-li, řádek zahodí a souhrnně zaloguje.
4. Ověří kanonický base64url, délky IV a tagu a provede AES-GCM dešifrování s příslušným `PAYLOAD_KEY` a normativním AAD. Neplatný GCM tag znamená zahození pouze daného řádku.
5. Ověří formát binární dávky: hlavičku počtu, tabulku délek, přesný počet a hranice surových bajtů. Nesmí předat částečně rozbalenou nebo neověřenou dávku.
6. U každého rozbaleného paketu provede úplnou validaci IPv4. Navíc vyžaduje, aby jeho vnitřní zdroj přesně odpovídal `src` datového řádku a vnitřní cíl lokální virtuální IP, tedy `dst` datového řádku. Nevyhovující paket zahodí ještě před vložením do příchozí fronty; platnost ostatních paketů dávky tím není dotčena.
7. Před pollingem ochrana příchozí fronty vyhodnotí nejen zbývající globální prostor `maxInboundQueueBytes`, ale také logické rezervace všech možných zdrojových peerů podle stejného admission invariantu. Polling odloží, pokud podle aktuálního účetnictví nelze bezpečně přijmout další platný paket; běžný maximální pollingový interval tuto kapacitní ochranu nepřebíjí. Protože klient před přijetím odpovědi nezná její skutečné rozdělení podle zdrojů, tato předběžná kontrola nenahrazuje runtime admission každého paketu.
8. Příchozí per-peer fronta se účtuje podle `src` datového řádku. Pokud by vložení již ověřeného paketu porušilo globální `maxInboundQueueBytes` nebo logickou per-peer rezervaci, klient zahodí pouze tento nový paket. NESMÍ odstranit ani přeuspořádat již zařazené pakety a zahození agregovaně eviduje podle bezpečného důvodu a zdrojového peeru, bez obsahu paketu nebo citlivých hodnot.
9. Výstupní pracovník zapíše pouze takto platné IPv4 pakety do tunelu sekvenčně v pořadí, v jakém byly v dávce uvedeny.

Protokol je at-most-once na serverovém bufferu. Ztracená HTTP odpověď nebo pád po převzetí může znamenat ztrátu dávky; klient nemá předstírat garantované doručení. Výchozí hodnota `65535` pro `maxDataRecordsPerResponse` je vysoký klientský kompromis bez změny protokolu. Uživatel smí limit zmenšit, ale tím může způsobit odmítnutí odpovědi obsahující dávky, které server již atomicky vyzvedl a z bufferu odstranil. Aplikační vrstvy nad VPN mohou používat vlastní retransmise.

## Povinné testovací scénáře front a per-peer kvót

1. Výchozí adresní prostor odvodí přesně 252 vzdálených peerů a výchozí hodnoty obou 64MiB globálních limitů a 128KiB logické rezervace projdou validací bez fyzického vytvoření 252 front nebo bufferů.
2. Pro odchozí i příchozí směr samostatně projde konfigurace s `maxQueueBytes = maxRemotePeers × minPerPeerQueueBytes` a hodnota o jeden bajt menší je před `health` odmítnuta. Stejný test se provede pro jiný společný adresní prostor s počtem peerů odvozeným z jeho konfigurace.
3. Při výchozích hodnotách lze jednu peer frontu naplnit přesně na 32.625 MiB (`34209792` bajtů), pokud ostatní možné fronty zůstávají prázdné; další bajt je odmítnut kvůli logické rezervaci, aniž by se změnily již zařazené pakety.
4. Odchozí fronty se odděleně účtují podle `dst` a příchozí podle `src`. Paket na hranici globálního limitu i logické rezervace projde, paket o jeden bajt nad příslušnou hranicí se zahodí jako nový a zachová obsah i FIFO pořadí všech existujících front.
5. Kruhové plánování obsluhuje neprázdné odchozí peer fronty spravedlivě, ale v každé zachová FIFO a nikdy nepřesune kapacitu vyhrazenou admission invariantem. Agregovaná diagnostika rozliší globální limit a logickou rezervaci a uvede peer, aniž by obsahovala paket nebo citlivé hodnoty.
6. Předběžná ochrana příchozí fronty před pollingem zahrne globální obsazenost i logické rezervace; souběžná změna obsazenosti nebo neočekávané rozdělení přijaté odpovědi je následně znovu zachyceno runtime admission každého paketu bez odstranění či přeuspořádání dříve zařazených paketů.

## Bezpečnostní model a přijatá rizika

- Relay server nemá payload klíče a nemůže dešifrovat IP obsah; důvěrnost a integritu jednoho payloadu zajišťuje AES-256-GCM mezi klienty.
- `health` autentizuje serverový čas a veřejná pole pomocí `SERVER_SECRET`; `info` a `data` vyžadují přesnou protokolovou autentizaci.
- HTTP zůstává záměrně podporovaným provozním režimem. Jsme si vědomi, že aktivní útočník může zachycený platný polling v časovém okně zopakovat, vyzvednout ciphertext z příchozí fronty oběti a nenávratně jej z relay odstranit, i když bez payload klíče nepřečte plaintext. Toto riziko replay útoku a související ztráty dostupnosti vědomě přijímáme; klient bude implementován s podporou HTTP a bez mechanismu, který by tento protokolový replay sám potlačoval.
- Pokud `trustAnyTlsCertificate` zůstane `true`, HTTPS nechrání klienta spolehlivě proti podvržení serveru na úrovni TLS. Nastavení je vědomě akceptované kompatibilitní riziko a musí být zřetelně viditelné v konfiguraci i startovním logu.
- Řídicí named pipe je záměrně dostupná každé lokální aplikaci bez ověřování uživatele, skupiny, procesu nebo Windows tokenu. Každý lokální proces proto může číst stav a diagnostiku služby, vyžádat si `info` a úplnou konfiguraci včetně jejích tajných hodnot, uložit novou konfiguraci a odeslat povolené příkazy včetně `connect` a `disconnect`. Toto riziko neoprávněného lokálního ovládání, změny konfigurace a zpřístupnění jejích údajů vědomě přijímáme. Validace formátu zpráv, verze smlouvy a whitelist příkazů tím nejsou zrušeny.
- Protokol nebrání replayi uvnitř platného časového okna, neposkytuje exactly-once, potvrzení ani ochranu dostupnosti. Klient nemá vytvářet vlastní replay cache, pokud se současně nezmění normativní specifikace.
- Klíče, tagy, ciphertexty, celé requesty a interní cesty se nesmí zapisovat do logů. Konfigurační soubor má mít omezená přístupová oprávnění jako ochranu proti přímému čtení ze souborového systému; tato oprávnění však neomezují záměrně veřejné `get-config` a `set-config` přes named pipe.

## Specifika platforem

### Windows

Windows je primární platforma. Klient MUSÍ používat výhradně Wintun jako L3 TUN pro IPv4. NESMÍ používat TAP, vlastní kernelový ovladač ani automaticky přejít na jiný tunelový backend, chybí-li nebo selže-li Wintun. Instalace, vytvoření či konfigurace adaptéru a změny rout vyžadují administrátorská oprávnění; samotný běh těchto operací patří výhradně Windows Service.

Službový režim je určený pro dlouhodobý běh a automatické obnovení. Konzolová aplikace zůstane lokálním klientem služby pro diagnostiku a podporované řídicí operace; sama nespouští VPN běh. Síťovou konfiguraci, DNS a routy je nutné měnit pouze explicitně, průběžně evidovat jejich vlastnictví a při ukončení odstranit pouze změny vytvořené klientem.

Platformní vrstva poskytne jeden sekvenční zdroj paketů pro vstupního pracovníka a jeden sekvenční cíl pro výstupního pracovníka. Případné asynchronní I/O dokončení se serializuje na těchto hranicích.

#### Wintun baseline a integrita distribuce

Baseline první verze je oficiální předkompilovaný a digitálně podepsaný Wintun (nejnovější stabilní verze) stažený z `wintun.net`, a to pouze pro architektury podporované konkrétním vydáním phpvpn. Projekt NESMÍ Wintun sám překládat, upravovat ani přejmenovat `wintun.dll`. Distribuce NESMÍ vyjímat ani samostatně dodávat soubory `.sys`, `.cat` nebo `.inf`; používá upstream architekturní `wintun.dll` v podobě a distribučním modelu určeném Wintunem a přikládá odpovídající upstream licenční text.

Podepsaný release manifest MUSÍ pro každou podporovanou architekturu uvést verzi Wintunu, očekávanou architekturu a SHA-256 přesných bajtů dodané `wintun.dll`. `install.bat` nebo jeho podepsaný pomocný instalátor MUSÍ před registrací služby a před jakoukoli změnou sítě ověřit podpis manifestu, shodu SHA-256, platný digitální podpis DLL a shodu architektury DLL s instalovanou službou a operačním systémem. Jakákoli neshoda je neobnovitelná chyba daného balíčku; instalace selže uzavřeně a NESMÍ vytvořit adaptér, nainstalovat službu ani změnit adresy, DNS nebo routy.

Oficiální ZIP Wintun (nejnovější stabilní verze) má publikovaný SHA-256 `07c256185d6ee3652e09fa55c0b673e2624b565e02c4b9091c79ca7d2f24ef51`. Tato hodnota ověřuje výhradně původní ZIP archiv při přípravě vydání; NENÍ hashem žádné jednotlivé `wintun.dll` a NESMÍ nahradit per-DLL hashe v podepsaném release manifestu distribuční složky `dist`.

`wintun.dll` MUSÍ být uložena v instalačním adresáři chráněném proti zápisu neprivilegovaným uživatelem. Služba ji načítá pouze absolutní kanonickou cestou pomocí `LoadLibraryExW` s příznaky `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32`. NESMÍ ji načíst přes `PATH`, aktuální pracovní adresář, relativní cestu, uživatelský profil, dočasný adresář ani jiné zapisovatelné umístění.

Platformní vrstva má pevný allowlist API potřebných první verzí: `WintunCreateAdapter`, `WintunOpenAdapter`, `WintunCloseAdapter`, `WintunGetRunningDriverVersion`, `WintunStartSession`, `WintunEndSession`, `WintunGetReadWaitEvent`, `WintunReceivePacket`, `WintunReleaseReceivePacket`, `WintunAllocateSendPacket` a `WintunSendPacket`. Služba před první síťovou změnou bezpečně načte DLL a vyřeší všechny tyto symboly. Chybějící symbol, nesprávná architektura nebo jiná staticky zjistitelná nekompatibilita způsobí chybu bez vytvoření adaptéru, session, adresy nebo routy. Kompatibilitu skutečně běžící verze ověří bezprostředně po vytvoření nebo otevření adaptéru podle následující části a před zahájením session nebo změnou adres a rout; žádný fallback není dovolen.

#### Vlastnictví adaptéru a session

Wintun DLL, adaptér i session smí používat pouze Windows Service. Tray ani konzolová aplikace NESMĚJÍ načíst `wintun.dll`, volat její API, otevírat adaptér ani přímo měnit tunel; nadále komunikují pouze přes IPC. V jednom procesu a pro jednu instalaci phpvpn smí existovat nejvýše jeden otevřený adaptér a jedna aktivní session. Adaptér používá pevný název `phpvpn` a pevný typ tunelu `phpvpn`; implementace používá stabilní instalační identitu adaptéru a NESMÍ při opakovaném připojení vytvářet další adaptéry s odvozenými názvy.

Služba smí adaptér vytvořit nebo otevřít a session zahájit až po úspěšném bootstrapu `health`, `info`, prázdný `data` polling. Bezprostředně po vytvoření nebo otevření adaptéru ověří pomocí `WintunGetRunningDriverVersion`, že skutečně běžící Wintun patří do podporovaného kompatibilního rozsahu vydání. Nevyhovující výsledek zavře adaptér a skončí bez zahájení session a bez konfigurace adres a rout. Pro první verzi má `WintunStartSession` kapacitu ring bufferu přesně 4 MiB (`4194304` bajtů), tedy mocninu dvou uvnitř rozsahu povoleného API; jiná hodnota vyžaduje předchozí zátěžové, overflow a paměťové testy a aktualizaci této specifikace.

Po úspěšném vytvoření session služba přidá pouze svou virtuální IPv4 adresu a své explicitně evidované routy. Záznam vlastnictví rozlišuje prostředky vytvořené aktuální instalací od existujících adaptérů, adres a rout jiných aplikací. Recovery po pádu, restartu služby nebo systému je idempotentní: služba nejprve porovná skutečný stav se svým záznamem a opraví nebo odstraní jen vlastní nedokončené změny; před novým úspěšným bootstrapem přitom NESMÍ vytvořit ani otevřít adaptér nebo zahájit session a nesmí slepě mazat stejně vypadající cizí síťovou konfiguraci.

#### Sekvenční I/O a vlastnictví paketů

Vstupní pracovník je jediným volajícím `WintunReceivePacket`. Každý úspěšně vrácený buffer vlastní Wintun session a pracovník jej MUSÍ po zkopírování nebo dokončení validace právě jednou vrátit pomocí `WintunReleaseReceivePacket`, a to i při neplatném, ne-IPv4, příliš velkém nebo jinak zahozeném paketu. Ukazatel do receive ringu NESMÍ uložit do FIFO, předat jinému vláknu ani použít po `WintunReleaseReceivePacket`. Neplatné a non-IPv4 pakety se bezpečně zahodí a evidují pouze agregovaným čítačem; validace IPv4 a zařazení do per-peer FIFO se dále řídí společnými pravidly tohoto dokumentu.

Není-li paket okamžitě dostupný, pracovník čeká přerušitelně na událost z `WintunGetReadWaitEvent` společně s vlastní událostí ukončení. NESMÍ pollovat v busy-loopu. Disconnect, stop služby, chyba session, sleep/hibernate a recovery musí čekání probudit nebo jinak řízeně ukončit, aby bylo možné pracovníka připojit bez časově neomezeného blokování.

Výstupní pracovník je jediným volajícím `WintunAllocateSendPacket` a `WintunSendPacket`. Pro každý již ověřený IPv4 paket nejprve alokuje přesně potřebný buffer v send ringu, zkopíruje do něj paket a jediným `WintunSendPacket` předá vlastnictví session. Buffer NESMÍ odeslat dvakrát, použít po předání ani sdílet s jiným vláknem. Nedostatek místa v ringu nesmí vést k přepsání paměti ani busy-loopu; pracovník použije omezené přerušitelné čekání/backpressure a při zastavení bezpečně zahodí dosud nealokovaný paket podle existujících limitů a FIFO pravidel.

#### Disconnect, upgrade a rollback

Disconnect MUSÍ proběhnout v tomto pořadí: zastavit zakládání nových `data` pollingů a zrušit nebo nechat bezpečně doběhnout jediný existující request; signalizovat ukončení síťového i obou tunelových pracovníků, probudit jejich čekání a všechna vlákna připojit; odstranit pouze vlastní routy a vlastní adresu; zavolat nejvýše jednou `WintunEndSession`; a nakonec zavolat nejvýše jednou `WintunCloseAdapter`. Jednotlivé kroky i celý postup jsou idempotentní, snesou částečnou inicializaci a po pádu se dokončí přes vlastnický reconciliation. Stav `disconnected` se nesmí oznámit před dokončením tohoto pořadí.

Upgrade je atomický a fail-closed: podporovaný instalační postup zastaví službu, ověří podpis balíčku a manifestu, per-DLL hash, digitální podpis, architekturu a API, atomicky nahradí aplikační soubory, provede omezený test vytvoření/otevření adaptéru a session bez trvalých rout a teprve potom službu spustí. Selže-li kontrola nebo test, síť zůstane odpojená a instalátor obnoví předchozí aplikační soubory pouze tehdy, pokud ověřil jejich kompatibilitu se skutečně běžící verzí Wintunu.

Rollback aplikace NESMÍ slepě downgradovat kernelový ovladač. Před návratem starší aplikace musí instalátor ověřit, že její allowlist API a podporovaný rozsah zahrnují skutečně běžící Wintun; jinak rollback odmítne a ponechá službu zastavenou s bezpečnou diagnostikou. Instalace, upgrade, rollback ani běžná odinstalace NESMĚJÍ automaticky volat `WintunDeleteDriver`, protože systémový Wintun může současně používat WireGuard nebo jiná aplikace.

#### Windows Service, konzole a tray

Pro běžný provoz je klient implementovaný jako Windows Service. Služba výhradně vlastní konfiguraci, Wintun adaptér a session, všechny síťové operace a všechny tři pracovní role. Samostatná tray aplikace běží v uživatelské relaci a spolu s konzolovou aplikací je pouze lokálním klientem služby. Obě aplikace smějí službu ovládat výhradně přes níže definované lokální IPC; nesmějí načítat Wintun DLL, otevírat tunel ani provádět VPN provoz. Přes `get-config` a `set-config` však mohou dočasně přijmout nebo odeslat konfiguraci včetně `SERVER_SECRET`, `ACCESS_KEY` a `PAYLOAD_KEY`; stejný přístup má každá lokální aplikace připojená k pipe.

Instalace, odinstalace, registrace, spuštění a zastavení samotné Windows Service nejsou operacemi IPC. Instalaci a odinstalaci zajišťují výhradně `install.bat`, `uninstall.bat` a Windows Service Manager. Konzolová aplikace nesmí tuto hranici obcházet řídicím příkazem poslaným přes pipe.

##### Veřejné lokální IPC

IPC je Windows named pipe s pevnou verzovanou smlouvou v1. Název pipe je pevná implementační konstanta obsahující verzi; klient nesmí vyhledávat pipe podle uživatelského vstupu ani přijmout neoznačenou nebo jinou verzi smlouvy. Zprávy jsou UTF-8 JSON a každý požadavek obsahuje `ipcVersion`, `requestId` a `command`; `set-config` navíc obsahuje právě jedno pole `config`. `ipcVersion` je pro tuto smlouvu celé číslo `1`, `requestId` je čerstvé UUIDv4 a `command` je právě jedna z povolených hodnot. Neznámá verze, neznámý příkaz, duplicitní pole nebo nadbytečné pole se odmítne stručnou bezpečnou IPC chybou bez provedení operace.

Instalátor nastaví na řídicí pipe explicitní Windows ACL, která umožní připojení každé lokální aplikaci bez ohledu na jejího uživatele, skupinu nebo efektivní Windows token. Přístup nesmí vyžadovat zvláštní účet, členství ve skupině ani oprávnění vytvořené instalátorem. Služba při připojení klienta neprovádí impersonaci ani autorizaci identity klienta a nesmí rozhodovat podle názvu procesu, PID, cesty spustitelného souboru nebo údajů ve zprávě. Jedinou ochrannou hranicí IPC zůstává pevná verzovaná smlouva, úplná validace zprávy a výhradní seznam povolených příkazů.

Smlouva v1 podporuje výhradně tyto příkazy:

- `get-status`;
- `connect`;
- `disconnect`;
- `get-info`;
- `get-config`;
- `set-config`;
- `get-diagnostics`.

Pipe NESMÍ podporovat průběžný odběr stavu, restart služby, spuštění libovolného procesu nebo příkazu ani samostatný příkaz pro práci s klíči mimo celý konfigurační JSON předávaný příkazy `get-config` a `set-config`. Nesmí poskytovat obecný rozšiřující příkaz, kterým by bylo možné tato omezení obejít.

Úspěšné odpovědi kromě `get-info` a `get-config` obsahují `ipcVersion`, původní `requestId`, výsledek příkazu a pouze pole povolená pro daný příkaz. Chyba obsahuje původní `requestId`, stabilní stručný kód, bezpečný uživatelský popis a údaj, zda je chyba obnovitelná. Nesmí obsahovat detail výjimky, systémový chybový text ani interní cestu. Odpovědi `get-info` a `get-config` mají zvláštní přímý formát popsaný níže.

`get-status` vrátí bezpečný lokální snímek obsahující:

- stav právě z množiny `disconnected`, `connecting`, `connected`, `recovering`, `error`;
- fázi právě z množiny `idle`, `health`, `info`, `access-key-check`, `starting-tunnel`, `running`, `stopping`;
- UTC čas poslední změny stavu;
- samostatný stav dosažitelnosti serveru a stav jeho kryptografického ověření, každý jako `unknown`, `yes` nebo `no`;
- stav tunelu;
- bezpečnou poslední chybu, nebo `null`, není-li žádná;
- agregované počty přijatých a odeslaných paketů a počet obnov;
- verzi klienta a verzi IPC smlouvy.

Snímek `get-status` NESMÍ obsahovat URL, konfiguraci, VPN ani jiné IP adresy, klíče, autentizační nebo GCM tagy, ciphertexty ani detailní síťové chyby. Jde pouze o čtení již existujícího lokálního stavu; příkaz nesmí sám spustit `health`, `info`, `data`, tunel ani obnovu.

`connect` zahájí v rámci služby bootstrap v přesném pořadí `health`, `info` a prázdný autentizovaný polling `data`. Tunel, tři pracovní role a pravidelný VPN provoz smí služba spustit až po úspěchu všech tří kroků. Pokud připojování nebo provoz již běží, příkaz je idempotentní a vrátí aktuální stav bez založení druhého bootstrapu, tunelu nebo sady pracovních rolí.

`disconnect` v rámci služby provede přesné pořadí z části „Disconnect, upgrade a rollback“: zastaví nové pollingy, ukončí pracovníky, odstraní pouze vlastní routy a adresu, ukončí Wintun session a zavře adaptér. Příkaz je idempotentní a recovery-safe; pokud je již odpojeno, vrátí aktuální stav. Dokončení příkazu nesmí být oznámeno dříve, než je další VPN polling zastaven a všechny tyto kroky bezpečně dokončeny.

`get-info` provede uvnitř služby jednorázový `health` a po jeho úspěšném ověření jednorázový `info`. Služba ověří a dešifruje obě odpovědi. Příkaz nesmí spustit tunel, pracovní role ani `data` polling a nesmí měnit stav odpojeného klienta na připojený. Při úspěchu služba pošle přes pipe bez vlastní IPC obálky přesně validovaný dešifrovaný plaintext objekt `info` ze serveru, tedy objekt s poli `serverVersion`, `generatedAt`, `clients` a `limits`. Nevrací šifrovaný obal odpovědi, klíče, tagy ani ciphertext. Při selhání vrátí pouze stručnou bezpečnou IPC chybu. Služba, tray ani konzolová aplikace NESMĚJÍ kvůli tomuto příkazu zapisovat nebo vytvářet lokální soubor s výsledkem; tray a konzolová aplikace plaintext přímo zobrazí.

`get-config` vrátí přes pipe bez vlastní IPC obálky přesný aktuální konfigurační JSON služby. Smí jej vyžádat každá lokální aplikace bez ohledu na uživatele, skupinu nebo Windows token. Konfigurační JSON se neposílá v logu ani v chybové odpovědi, ale jeho zpřístupnění přes veřejnou pipe je vědomě přijaté riziko tohoto klienta.

`set-config` přijme v poli `config` celý konfigurační JSON. Služba před zápisem ověří syntaxi JSON a úplnou platnost konfigurace stejnými pravidly jako při připojení; při chybě nic nezapíše a vrátí bezpečnou IPC chybu. Při úspěchu služba konfiguraci atomicky uloží do pevného konfiguračního souboru, nahradí jí aktuální konfiguraci a vrátí běžnou úspěšnou IPC odpověď. Příkaz sám neodpojí ani nepřipojí VPN; uložená konfigurace se použije při následujícím pokusu o připojení.

`get-diagnostics` vrátí rozšířený jednorázový bezpečný snímek místního stavu. Obsahuje stav, verzi a dobu běhu služby; výsledek validace konfigurace a bezpečná varování; výsledky posledního ověření `health`, `info` a `data` a poslední obnovy; připravenost tunelu; pro vstupní i výstupní fronty počet položek, obsazené bajty a platný limit; agregované provozní čítače; a bezpečnou poslední chybu včetně údaje o její obnovitelnosti. Příkaz je pouze čtecí, nespouští nové síťové požadavky, tunel ani obnovu a NESMÍ vrátit URL, VPN ani jiné IP adresy, klíče, tagy, ciphertexty, obsah paketů, interní cesty ani detailní systémové nebo síťové chyby.

Tray získává stav pevně příkazem `get-status` jednou za 5 sekund a navíc ihned po každém příkazu `connect` nebo `disconnect`. Interval se za žádných okolností nezkracuje, neprodlužuje ani jinak adaptivně nemění. Smlouva nepoužívá streamování ani odběr změn stavu.

Tray používá ikonu Material Symbols `shield_lock` pod licencí Apache 2.0. Šedá ikona znamená odpojeno, oranžová ikona znamená připojování a zelená ikona znamená úspěšně ověřené připojení. Po selhání se zobrazí modální chyba a ikona se vrátí do šedého stavu.

Menu tray aplikace má přesně toto pořadí:

1. **Edit Config** pošle službě přes IPC příkaz `get-config` a otevře jednoduché editační okno. Jeho obsah tvoří víceřádkové editovatelné textové pole s neproporcionálním písmem, které obsahuje vrácený konfigurační JSON, a tlačítka **Save** a **Cancel**. **Save** odešle aktuální obsah pole příkazem `set-config`; **Cancel** zavře okno bez zápisu. Tray konfiguraci nečte ani nezapisuje přímo do souboru. Úprava se použije až při následujícím pokusu o připojení; je-li služba připojená, uživatel ji nejprve odpojí.
2. **Show Info** pošle službě přes IPC příkaz `get-info` a otevře obdobné okno s víceřádkovým textovým polem s neproporcionálním písmem. Vrácený JSON je v poli pouze pro čtení: uživatel jej smí označit a zkopírovat, nesmí jej upravit ani uložit. Tato volba nespouští `data` polling ani VPN provoz.
3. **Connect** pošle službě přes IPC příkaz `connect`; při připojeném nebo připojovaném stavu se tato položka změní na **Disconnect**, která pošle příkaz `disconnect` a bezpečně zastaví VPN provoz služby.

Bootstrap po volbě **Connect** probíhá v pořadí `health`, `info`, prázdný `data` polling. Selhání `health` zobrazí, že se nepodařilo spojit a ověřit server na nakonfigurované URL. Selhání `info` po úspěšném `health` oznámí problém se `SERVER_SECRET` nebo s odpovědí serveru. Selhání prvního `data` requestu se jako problém `ACCESS_KEY` označí pouze při autorizačním výsledku; ostatní chyby zůstanou obecnou síťovou nebo serverovou chybou. Diagnostika nikdy nevypisuje klíče, tagy ani ciphertext.

#### Distribuce a instalace ve Windows

Distribuce pro Windows je složka `dist`. Obsahuje binární soubory služby, tray aplikace, konzolového správce, všechny nezbytné knihovny, nepřejmenované architekturní `wintun.dll`, upstream licenční text, podepsaný release manifest s per-DLL SHA-256 a skripty `install.bat` a `uninstall.bat`. Neobsahuje samostatné Wintun `.sys`, `.cat` ani `.inf`. Složka `dist` je kompletní předatelná distribuční jednotka; instalační postup NESMÍ stahovat nebo přijímat neověřenou náhradu některé její součásti.

`install.bat` je podporovaný vstupní bod instalace. Smí zavolat podepsaný pomocný PowerShellový skript a vyžádat si zvýšená administrátorská oprávnění. Nejdříve fail-closed ověří celý balíček podle podepsaného manifestu a teprve potom nainstaluje a zaregistruje Windows Service, připraví chráněný instalační adresář a Wintun, nastaví veřejný lokální přístup k named pipe podle této smlouvy a nakonfiguruje automatické spuštění tray aplikace při přihlášení uživatele; tray aplikace se po dokončení instalace spustí a zobrazí vedle systémových hodin. `uninstall.bat` provede opačný, bezpečný postup: zastaví a odregistruje službu přes Windows Service Manager, odebere automatické spuštění tray aplikace a odstraní pouze součásti vytvořené instalací. Nevolá automaticky `WintunDeleteDriver` a neodstraní systémový Wintun používaný jinou aplikací. Konfigurační soubor a uživatelská data se při odinstalaci nemažou bez výslovné volby uživatele.

#### Povinné Windows integrační a lifecycle testy

Každé podporované vydání a architektura MUSÍ před publikací projít alespoň těmito scénáři:

1. čistá instalace z úplné složky `dist`, první bootstrap a vytvoření právě jednoho adaptéru `phpvpn` a jedné session s 4MiB ringem;
2. instalace a běh na systému, kde Wintun současně používá WireGuard nebo jiná aplikace, bez změny či odstranění jejího adaptéru a ovladače;
3. fail-closed odmítnutí po změně hashe DLL, neplatném podpisu manifestu nebo DLL, nesprávné architektuře, chybějícím povinném API a nepodporované běžící verzi, vždy bez nové služby a síťové změny;
4. opakované connect/disconnect cykly, částečné selhání každé inicializační a teardown fáze a kontrola, že nezůstala vlastní adresa, routa, druhý adaptér ani session;
5. pád a restart služby v každé fázi, včetně reconciliation vlastních zbytků bez zásahu do cizí síťové konfigurace;
6. sleep, hibernate a resume během čekání na receive event, během pollingového requestu i při zaplněném send ringu, bez busy-loopu a deadlocku;
7. podporovaný Windows upgrade a následný restart systému;
8. upgrade a rollback phpvpn s ověřením kompatibility skutečně běžícího Wintunu a s odmítnutím slepého downgrade ovladače;
9. zaplnění a overflow 4MiB receive i send ringu, backpressure, probuzení při stopu a zachování paměťové bezpečnosti, ownershipu a FIFO pravidel;
10. souběh s jinou VPN, kolize rout a odpojení phpvpn bez odstranění cizích adres, DNS nebo rout;
11. odinstalace při přítomnosti jiného uživatele Wintunu, která odstraní pouze phpvpn součásti, automaticky nevolá `WintunDeleteDriver` a neporuší funkčnost cizího Wintunu.

### macOS

V macOS je doporučené použít nativní síťový mechanismus podporovaný systémem, typicky Network Extension s packet tunnel providerem. Ten vyžaduje správné podepisování, entitlementy a uživatelské schválení. Společné C++ jádro zůstává stejné; malý host/provider obstará čtení a zápis paketů a předá je přes stabilní C nebo C++ rozhraní.

`NEPacketTunnelFlow` může předávat dávky paketů v callbacku. Provider je předá vstupnímu pracovníkovi v pořadí dávky; výstupní pracovník může zapisovat dávky, aniž by tím změnil FIFO pořadí jednotlivých paketů.

Alternativy založené na `/dev/tun` nejsou na moderním macOS obecně přenositelné ani doporučené pro distribuovaný produkt. Je nutné počítat s pravidly sandboxingu, životním cyklem rozšíření a omezeními distribuce.

### Linux

Na Linuxu je přirozeným rozhraním TUN zařízení `/dev/net/tun`. Klient vytvoří TUN rozhraní, přiřadí virtuální IP a nastaví potřebné routy přes netlink nebo spolehlivý systémový integrační modul. Operace vyžadují oprávnění `CAP_NET_ADMIN` nebo vhodně omezený privilegovaný pomocný proces.

Produkční služba může běžet pod systemd s minimálními capability, restartovací politikou a ochranou konfiguračního souboru před přímým přístupem přes souborový systém. Tato ochrana nemění záměrně veřejný přístup ke konfiguraci přes IPC. Implementace nemá předpokládat jeden konkrétní firewall ani síťový manažer; jejich integrace má být volitelná.

Základní implementace používá jeden TUN file descriptor a tři společné pracovní role. Linuxový multiqueue TUN je volitelná optimalizace; nesmí změnit společná pravidla FIFO, limitů front ani vlastnictví paketů.

## Doporučené pořadí implementace

1. Implementovat a testovat čisté multiplatformní jádro: konfiguraci, kanonickou base64url, HMAC/HKDF, AES-GCM, čas a parsery.
2. Implementovat HTTP transport a kontraktní testy `health`, `info` a prázdného `data` pollingu proti serveru.
3. Implementovat tři pracovní role, omezené fronty, spravedlivé plánování a přesné měření velikosti requestu bez tunelu.
4. Implementovat agregovaný binární payload a kontraktní testy jeho parseru.
5. Přidat Windows tunel a end-to-end test dvou klientů.
6. Přidat linuxovou TUN implementaci, poté macOS Network Extension wrapper.
7. Doplnit zátěžové testy, pádové scénáře, neplatné NDJSON, limity těla, přeplněné fronty, ztrátu odpovědi a bezpečnostní regresní testy.

Před implementací musí být změny `client.md` a `protocol.md` nasazeny společně; agregovaný payload není kompatibilní se starším pravidlem jednoho IPv4 paketu v ciphertextu.
