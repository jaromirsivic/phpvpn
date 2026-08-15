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

### Běhový model

Společné jádro používá tři sériově pracující role. Implementace je smí realizovat třemi vlákny nebo ekvivalentním event-loopem, ale vlastnictví dat a pravidla souběhu musí zůstat stejná.

1. **Vstupní pracovník tunelu** je jediným čtenářem TUN/TAP. Provede úplnou validaci IPv4 paketu podle pravidel níže, vyžaduje lokální virtuální IP jako vnitřní zdroj a zařadí jej výhradně do FIFO fronty kontraktu odpovídajícího jeho vnitřnímu cíli. Nešifruje ani nečeká na síť.
2. **Síťový pracovník** je jediným vlastníkem `health`, `info` a `data` requestů, plánování dávek, šifrování, ověřování odpovědí a dešifrování. V jednom okamžiku smí existovat nanejvýš jeden probíhající `data` request.
3. **Výstupní pracovník tunelu** je jediným zapisovatelem do TUN/TAP. Sekvenčně odebírá ověřené IPv4 pakety z příchozí FIFO fronty a zapisuje je do virtuálního rozhraní.

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
    "maxOutboundQueueBytes": 16777216,
    "maxInboundQueueBytes": 16777216
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

`maxOutboundQueueBytes` je celková maximální velikost paketů načtených z TUN/TAP, které čekají na odeslání na server. `maxInboundQueueBytes` je celková maximální velikost ověřených a dešifrovaných paketů čekajících na zápis do TUN/TAP. Obě hodnoty jsou kladná celá čísla v bajtech a při vynechání mají výchozí hodnotu 16 MiB (`16777216`).

## Stavový automat připojení

1. **Načtení konfigurace.** Klient validuje URL, VPN IP, hex formát a délky klíčů, jedinečnost kontraktů a dostupnost virtuálního rozhraní.
2. **Health.** Vygeneruje nový UUIDv4 `requestId` a odešle `auth-none` na `?action=health`.
3. **Ověření serveru a času.** Pokud odpověď odpovídá syntaxi `health` a nese stav `running`, klient ověří, že `requestId` souhlasí, a ověří GCM tag podle protokolu. Teprve potom přijme čas serveru a vypočítá `timeDelta`; lokální systémový čas nikdy nemění.
4. **Časová rezerva.** Serverový čas se plánuje s milisekundovou přesností. Klient používá efektivní serverový čas `localNow + timeDelta - safetyMargin`. Výchozí konzervativní mez je jedna sekunda za serverem; přesná hodnota má být konfigurovatelná či později definitivně stanovena.
5. **Info.** Klient autentizovaně odešle `auth-semi` na `?action=info`, ověří a dešifruje odpověď a uloží obdržené limity a doporučené provozní hodnoty. Tvrdé limity, zejména maximální velikost těla požadavku, musí dodržet. Doporučené intervaly pollingů slouží jako plánovač, pokud nejsou v budoucí specifikaci označené za závazné.
6. **Ověření přístupového klíče.** Ještě před spuštěním tří pracovních rolí klient provede prázdný autentizovaný polling `data`. Tím ověří `ACCESS_KEY` i úplnou odpověď `data`. Přijaté platné dávky zpracuje stejně jako při běžném pollingu a připraví je pro výstupní pracovník.
7. **Provoz.** Teprve po úspěšném `health`, `info` a prvním prázdném `data` pollingu se zahájí čtení tunelu a pravidelný polling endpointu `data`.
8. **Obnova.** Při dočasné síťové nebo autentizační chybě klient nevypíná tunel bezprostředně, loguje stav, používá omezený backoff a opakuje `health`, `info` a prázdný `data` polling před návratem do provozu. Chyba konfigurace nebo neplatný server secret vyžaduje zásah uživatele.

Příkazový výstup má stručně ukazovat fáze: připojování k serveru, dostupný server, úspěšně ověřený `SERVER_SECRET`, zjištěné provozní limity a stav tunelu. Tajemství ani autentizační tagy se nevypisují.

## Odchozí tok paketů a plánování requestů

Virtuální rozhraní dodává kompletní IPv4 pakety. Před zařazením musí vstupní pracovník provést úplnou strukturální validaci podle následující podkapitoly. Vnitřní zdroj musí přesně odpovídat lokální virtuální IP a vnitřní cíl musí být platná VPN IP s existujícím `communicationContract`; výběr fronty i payload klíče proto vždy vychází ze stejné uspořádané dvojice `(lokální IP, vnitřní cíl)`. Paket určený vlastní virtuální IP, paket s neplatnou hlavičkou nebo paket bez platného kontraktu se přes relay neodesílá; zahození se loguje pouze souhrnně po důvodu a cíli. Při dosažení `maxOutboundQueueBytes` nové pakety zahazuje.

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

Pracovník používá rozpočet `maxRequestBodyBytes - 1024`; 1 KiB je pevná rezerva nad očekávanou serializační režií. Konečná kontrola vždy změří skutečně sestavené tělo v bajtech. Pokud by další datový element nebo paket překročil `maxRequestBodyBytes` či limit počtu záznamů, zůstane na čele své FIFO fronty pro další request.

Výběr je spravedlivý: pracovník vede kruhové pořadí cílů, v první fázi se pokusí zařadit alespoň jeden paket z každé neprázdné fronty a v dalších průchodech doplňuje pakety opět kruhově, dokud zbývá rozpočet. Po každém requestu posune počáteční cíl. Jedna dlouhá fronta tak nevytlačuje krátké fronty; pakety v každé konkrétní frontě zůstávají FIFO.

## Příjem dat

Odpověď na `data` je NDJSON: nula nebo více řádků `data`, právě jeden `data-result` a poslední `data-mac`.

1. Klient ověří strukturu odpovědi, `data-result` a závěrečný `data-mac`. Ten chrání počet, pořadí a úplnost datových řádků.
2. U každého řádku nejdříve ověří, že `dst` je jeho lokální virtuální IP. Cizí řádek zahodí ještě před výběrem payload klíče.
3. Podle přesné dvojice `src`, `dst` vyhledá kontrakt. Neexistuje-li, řádek zahodí a souhrnně zaloguje.
4. Ověří kanonický base64url, délky IV a tagu a provede AES-GCM dešifrování s příslušným `PAYLOAD_KEY` a normativním AAD. Neplatný GCM tag znamená zahození pouze daného řádku.
5. Ověří formát binární dávky: hlavičku počtu, tabulku délek, přesný počet a hranice surových bajtů. Nesmí předat částečně rozbalenou nebo neověřenou dávku.
6. U každého rozbaleného paketu provede úplnou validaci IPv4. Navíc vyžaduje, aby jeho vnitřní zdroj přesně odpovídal `src` datového řádku a vnitřní cíl lokální virtuální IP, tedy `dst` datového řádku. Nevyhovující paket zahodí ještě před vložením do příchozí fronty; platnost ostatních paketů dávky tím není dotčena.
7. Před pollingem kontroluje rezervu `maxInboundQueueBytes`, aby data nevyzvedl jen proto, aby je následně zahodil. Jestliže se ověřený paket do příchozí fronty přesto nevejde, zahodí jej a souhrnně zaloguje.
8. Výstupní pracovník zapíše pouze takto platné IPv4 pakety do tunelu sekvenčně v pořadí, v jakém byly v dávce uvedeny.

Protokol je at-most-once na serverovém bufferu. Ztracená HTTP odpověď nebo pád po převzetí může znamenat ztrátu dávky; klient nemá předstírat garantované doručení. Aplikační vrstvy nad VPN mohou používat vlastní retransmise.

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

Windows je primární platforma. Použije se ovladač TUN/TAP kompatibilní s požadovaným způsobem provozu; při výběru je vhodné preferovat aktivně udržované řešení s dokumentovaným uživatelským API a možností tichého nasazení. Aplikace obvykle potřebuje administrátorské oprávnění pro instalaci ovladače, vytvoření či konfiguraci rozhraní a směrování.

Službový režim je určený pro dlouhodobý běh a automatické obnovení. Konzolová aplikace zůstane lokálním klientem služby pro diagnostiku a podporované řídicí operace; sama nespouští VPN běh. Síťovou konfiguraci, DNS a routes je nutné měnit pouze explicitně a při ukončení obnovit předchozí stav.

Platformní vrstva poskytne jeden sekvenční zdroj paketů pro vstupního pracovníka a jeden sekvenční cíl pro výstupního pracovníka. Případné asynchronní I/O dokončení se serializuje na těchto hranicích.

#### Windows Service, konzole a tray

Pro běžný provoz je klient implementovaný jako Windows Service. Služba výhradně vlastní konfiguraci, TUN/TAP, všechny síťové operace a všechny tři pracovní role. Samostatná tray aplikace běží v uživatelské relaci a spolu s konzolovou aplikací je pouze lokálním klientem služby. Obě aplikace smějí službu ovládat výhradně přes níže definované lokální IPC; nesmějí samy otevírat tunel ani provádět VPN provoz. Přes `get-config` a `set-config` však mohou dočasně přijmout nebo odeslat konfiguraci včetně `SERVER_SECRET`, `ACCESS_KEY` a `PAYLOAD_KEY`; stejný přístup má každá lokální aplikace připojená k pipe.

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

`disconnect` v rámci služby bezpečně ukončí pravidelný polling, zastaví tři pracovní role, uzavře tunel a obnoví síťový stav, který klient změnil. Příkaz je idempotentní; pokud je již odpojeno, vrátí aktuální stav. Dokončení příkazu nesmí být oznámeno dříve, než je další VPN polling zastaven a tunel bezpečně uzavřen.

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

Distribuce pro Windows je složka `dist`. Obsahuje binární soubory služby, tray aplikace, konzolového správce, všechny nezbytné knihovny a skripty `install.bat` a `uninstall.bat`. Složka `dist` je kompletní předatelná distribuční jednotka.

`install.bat` je podporovaný vstupní bod instalace. Smí zavolat pomocný PowerShellový skript a vyžádat si zvýšená administrátorská oprávnění. Nainstaluje a zaregistruje Windows Service, provede nutné kroky pro TUN/TAP, nastaví veřejný lokální přístup k named pipe podle této smlouvy a nakonfiguruje automatické spuštění tray aplikace při přihlášení uživatele; tray aplikace se po dokončení instalace spustí a zobrazí vedle systémových hodin. `uninstall.bat` provede opačný, bezpečný postup: zastaví a odregistruje službu přes Windows Service Manager, odebere automatické spuštění tray aplikace a odstraní pouze součásti vytvořené instalací. Konfigurační soubor a uživatelská data se při odinstalaci nemažou bez výslovné volby uživatele.

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
