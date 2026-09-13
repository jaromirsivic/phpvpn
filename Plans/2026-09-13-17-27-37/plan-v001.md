---
plan_version: 1
created_at: 2026-09-13-19-13-04
source: C:/git/phpvpn/Intents/2026-09-13-17-27-37/intent-v003.md
---

# PHPVPN: realizačně připravený plán

Autoritativním zadáním je úplný intent v003. Přečten byl i jeho konverzační zdroj intent-chat-v003.md. Starší verze nejsou dalším zadáním. Tento plán je úplná neměnná verze, případné pozdější podstatné upřesnění patří do plan-v002.md.

Plánování proběhlo nad C:/git/phpvpn, aktivní větví **tray_UX** a HEAD **04deaea1d48ea305a38f9a483b477df5413cd258**. Všechny níže uvedené relativní cesty mají tento projektový kořen. U existujících symbolů jsou uvedeny orientační řádky této revize. Nové soubory a symboly jsou výslovně označeny jako návrh. Kontext byl získán nejprve příkazy Graft map, ask, skeleton a grep a doplněn přesnými oblastmi zdrojů, testů a dokumentace.

Jde pouze o plán. Nebyla provedena implementace, kompilace, spuštění projektových testů, deployment ani změna stávající dokumentace. Existující změna .intent-builder-active a neversionovaný adresář intentu zůstávají vlastnictvím koordinátora. Jediným výstupem tohoto úkolu je tento plán.

Pro plánovací úkol platí konečný termín **2026-09-13 19:57:37 Europe/Berlin**, tedy 17:57:37 UTC. Další agenty ani tasky tento plánovací úkol nevytváří. Implementace má probíhat přímo v aktivní feature branch, bez worktree, pouze po samostatném rozhodnutí uživatele o realizaci.

Pořadí realizace je **1 → 2 → 3 → 4**. Blok 1 je funkčně samostatný. Blok 2 zavádí společný protokol a serverové i službové primitivy. Blok 3 je integruje do Connectu. Blok 4 přidává vyžádaný přehled a uzavírá společnou akceptaci. Každý blok končí aktualizací své normativní dokumentace a ověřením. Nevytvářet pátý implementační blok pro dokumentaci.

V následujících částech jsou technické volby, které intent záměrně nespecifikuje na úroveň API, konkrétním návrhem tohoto plánu. Nemění požadované chování. Zejména nové názvy interních helperů, lokální IPC příkaz, rozpočty rozšířeného requestu a přesná serializace podepisovaných rozšíření jsou zde rozhodnuty před implementací.

## 1. Server mimo info API

### Výsledek a závislosti

Samostatný phpvpn.php vrací status page vizuálně odpovídající assets/index.html, umožňuje ji vypnout a používá výchozí datový adresář ./phpvpn_data. API zůstává dostupné. Žádný automatický přesun starých dat.

Blok nemá závislost na novém info protokolu. Připraví i testovací deployment pro následující bloky.

### Konkrétní oblasti

| Soubor a symbol | Současný stav a plánovaná změna |
|---|---|
| phpvpn.php:1–95, konfigurační hlavička | Přidat boolean STATUS_PAGE_ENABLED = true s komentářem pro obě hodnoty. Změnit BUFFER_DIRECTORY a příklad nginx ochrany. |
| phpvpn.php:1933–1947, StatusPageHandler::handle | Nahradit jednoduché HTML vloženou šablonou mockupu. |
| phpvpn.php:2389–2421, Application::handle | U kořenového GET/HEAD vyhodnotit přepínač před vykreslením. |
| phpvpn.php:2454–2471, Application::isConfiguredSecretInsecure a runFromGlobals | Zachovat stávající rozpoznání předvídatelného secretu a potlačení HEAD těla přes HttpResponseWriter. |
| phpvpn.php:1192–1208, Config::isInsecureSecret a resolveBufferDirectory | Zachovat vlastní absolutní/relativní cestu a vyhodnocení relativní cesty vůči skriptu. |
| assets/index.html:1 | Zdroj embedded HTML, CSS a JavaScriptu, žádná runtime závislost. |
| .gitignore:8 | Ignorovat nový provozní adresář /phpvpn_data/. Ponechat ochranné ignorování starého /phpvpn/ pro zbylá ruční data. |
| tools/deploy-test.ps1:40–56, Replace-ExactlyOnce | Aktualizovat očekávaný doslovný default BUFFER_DIRECTORY. Jinak deployment po změně spadne před publikací. |
| client/tests/test_deploy_profiles.ps1 | Regrese obou profilů a odmítnutí neočekávané hlavičky včetně změněného buffer bloku. |

### Pořadí změn

1. Přidat a popsat STATUS_PAGE_ENABLED v jediné administrátorské hlavičce. Root routing nemá kvůli status page načítat kompletní Config ani inicializovat storage.
2. Při false použít existující Http::error s HTTP 404 a code not_found. GET vrátí standardní JSON chybu, HEAD stejný status a relevantní hlavičky bez těla. Ostatní metody a API routing zachovat.
3. Vložit HTML jako doslovnou nowdoc šablonu nebo dvě doslovné části s volitelným warning panelem. Vyjmout celý element aside, nikoli jen text nebo CSS viditelnost. Do stránky nevkládat skutečnou hodnotu SERVER_SECRET.
4. Zachovat přesně funkci crypto.getRandomValues(new Uint8Array(32)), lowercase hex po dvou znacích a Copy key s existující clipboard/fallback cestou. Nevkládat externí skripty, styly, fonty ani čtení assets/index.html za běhu.
5. Zachovat současnou bezpečnostní klasifikaci: výchozí secret, prázdná hodnota a dosavadní nulové hodnoty jsou insecure. Jiný bezpečný secret odstraní pouze celý warning panel. Jde o zachování současné politiky, nikoli zavedení nového hodnocení síly hesel.
6. Změnit výchozí cestu a související komentáře, ignore pravidlo a očekávanou hlavičku deployment helperu. Nevytvářet fallback na starý adresář, migraci ani automatické kopírování.
7. Připravit izolované varianty testovací kopie skriptu pro enabled/disabled, secure/default/empty secret a vlastní cestu. Nikdy neměnit konfigurační konstanty sledovaného skriptu jen kvůli testu.

### Testy a rizika

- V tests.php doplnit status a routing testy. Testy konstant spouštět v oddělené PHP instanci nad dočasnou kopií, protože konstanty nelze v jednom procesu předefinovat. Test přes skutečný HTTP writer musí pokrýt prázdné HEAD tělo, samotný BufferedResponseWriter to nezaručuje.
- V tests-integration/test_status_and_routing.py aktualizovat očekávané HTML a prověřit GET, HEAD, nepovolené metody, neznámé akce, enabled/disabled.
- V tests-integration/test_security.py ověřit nedostupnost /phpvpn_data/ i info.json přes HTTP. Staré explicitní bezpečnostní cesty není třeba odstranit.
- Zkontrolovat původní testy čekající h1 Server is running nebo staré červené h2. Nový mockup používá h1 PHPVPN server is running a aside.
- Test vlastní BUFFER_DIRECTORY ověří, že se nevytvoří nový default. Test nového defaultu nechá ve staré dočasné složce sentinel a ověří jeho bezezměnnost i absenci migrace.
- Samostatně nasadit pouze phpvpn.php do fixture bez assets. V prohlížeči porovnat obě varianty s mockupem, včetně úzkého viewportu, Generate key a Copy key. Ověřit délku 64, vzor [0-9a-f]{64}, opakované generování, schování/vyjevení výstupu a shodu zkopírovaného textu.
- Clipboard může mít jiná omezení podle kontextu HTTP/HTTPS. Ověřit obě větve přítomné v mockupu. Nezaměňovat dostupnost clipboard API s kvalitou generátoru.
- Přejmenování defaultu bez migrace znamená prázdný nový provozní stav, pokud správce data ručně nepřesune. To je požadované chování, které musí být výslovně uvedeno.

### Dokumentace v tomto bloku

- **protocol.md**, zejména 5.1, 10.5–10.6, 13 a 16.1: nový status obsah, přepínač, GET/HEAD 404 a nová výchozí cesta, povinná ochrana adresáře.
- **phpvpn.php**, hlavička a komentáře BUFFER_DIRECTORY/STATUS_PAGE_ENABLED: dopad true/false a ruční migrace.
- **tests-integration/README.md**: aktuální očekávaná stránka, varianty fixture, nový default a oddělení testovacího bufferu.
- **client.md**, část Testovací profily serveru: popsat aktualizovanou kontrolu hlavičky deployment skriptu.
- **distribution.md**, serverová část: krátce upřesnit, že stránka je embedded a distribuovaný server nepotřebuje assets/index.html. Nezavádět změnu struktury distribuce ani nyní nic nebalit.

### Validační příkazy a akceptace

Příkazy jsou určeny pro následnou realizaci z C:/git/phpvpn. PHP není v aktuálním PATH. Ověřená dostupná cesta je D:/nginx/php8.5.9/php.exe.

~~~powershell
& 'D:\nginx\php8.5.9\php.exe' -l .\phpvpn.php
& 'D:\nginx\php8.5.9\php.exe' .\tests.php
powershell -ExecutionPolicy Bypass -File .\client\tests\test_deploy_profiles.ps1
~~~

Pro HTTP testy vytvořit novou izolovanou fixture, například pod work/intent-v003, a skutečně ji obsluhovat z odpovídajícího lokálního webrootu. Deployment helper sám server nespustí. Použít tools/deploy-test.ps1 s explicitními WebRoot, BufferDirectory, BaseUrl, LargeBodyBaseUrl a ConfigurationPath. Nepoužít jeho výchozí sdílené D:/nginx/html bez ověření, že patří tomuto testu. Níže uvedený fixture.json je výstup tohoto izolovaného deploymentu.

~~~powershell
uv run --python 3.13 --with pytest --with cryptography --with requests -- pytest tests-integration/test_status_and_routing.py tests-integration/test_security.py --server-config work/intent-v003/fixture.json -v
git diff --check
~~~

Pro každou variantu enabled/disabled provést HTTP GET i HEAD proti jejímu skutečnému endpointu a health/info/data. Akceptace je splněna pouze při shodě mockupu, nepřítomnosti celého warning panelu pro bezpečný secret, 404 bez HTML při vypnutí, nulovém HEAD těle a zachování všech tří API akcí. Vlastní cesta funguje, nový default je phpvpn_data a stará data zůstávají netknutá.

## 2. Maskované komunikační kontrakty a rozšířené info API

### Výsledek a závislosti

Vznikne přesný oboustranně testovaný protokol, atomické uložení a filtrovaná odpověď. Služba bude mít typované operace pro publikaci a pro jednorázové načtení. V tomto bloku se ještě nezapojí Connect checkpoint ani nové UI.

Závislost na bloku 1 je hlavně deployment fixture. Původní semi-auth info, health, data, jejich canonical bytes, šifrování a standardní response shape zůstanou beze změny.

### Konkrétní oblasti

| Oblast | Existující soubory a symboly |
|---|---|
| Server wire a validace | phpvpn.php:428–477 Validators, 494–703 JsonWire, 1097–1209 Config, 1244–1272 Authenticator, 1884–1927 Request. |
| Server úložiště | phpvpn.php:1297–1319 Storage, 1321–1684 DiskStorage, především readLastSeenMs, touchLastSeenBestEffort, clientDirectory, writeAll, sweep a writeGuards. |
| Server endpoint | phpvpn.php:1992–2102 InfoHandler::handle a buildPlaintext. DataHandler::handle:2113–2240 je referencí full-auth, jeho canonical bytes neměnit. |
| Klient konfigurace a crypto | client/src/core/config/config.h, config_validator.cpp:120–321, client/src/core/crypto/key_derivation.cpp:51–66, canonical.h/.cpp, crypto_provider.h/.cpp. |
| Klient wire | client/src/core/protocol/request_builder.h/.cpp, response_parser.h/.cpp:110–227, json.h/.cpp. |
| Služba HTTP a snapshot | client/src/core/runtime/network_worker.h/.cpp:33–64 a 130–178, vpn_engine.h/.cpp:136–175. |
| Nezávislý protokolový klient | tests-integration/phpvpn_client.py, full-auth derivace:196–202 a info:860–877. |
| Sestavení | client/src/CMakeLists.txt a client/tests/CMakeLists.txt pro nové core zdroje a testy. |

Nové navržené soubory:
- client/src/core/crypto/masked_contracts.h/.cpp pro kanonickou dvojici IP a hash seznamu.
- client/src/core/protocol/info_extensions.h/.cpp pro typ MaskedCommunicationContract, InfoRequestOptions, canonical rozšíření a jejich limity.
- Příslušné nové testy unit/test_masked_contracts.cpp a unit/test_info_extensions.cpp.

### Přesný výpočet maskovaného kontraktu

1. Pracovat s validovanými Ipv4Address, řadit jejich 32bitové numerické hodnoty. Sdílený helper **canonicalContractId** vrací decimal dotted IPv4 nižší_IP, podtržítko a vyšší_IP.
2. Z přesně 32 bajtů payloadKey vyrobit 64 lowercase hex znaků, nezávisle na původním zápisu JSON.
3. Vstup hashovat v tomto přesném tvaru, bez zakončujícího LF nebo NUL:

~~~text
canonicalContractId + "_" + payloadKeyLowerHex + "_" + configuredServerSecret
~~~

4. Použít UTF-8 bajty původního uloženého serverSecret. Nevolat normalizeServerSecret a nepoužít NetworkWorker::normalisedServerSecret_. Zachovat casing, whitespace, Unicode i prázdný řetězec. Prázdný secret a text CHANGE_THIS_SERVER_SECRET dají jiné maskované hashe, i když mají stejný efektivní autentizační klíč.
5. Výstup SHA-256 je 64 lowercase hex znaků. Seznam vytvářet pouze z validních přítomných kontraktů, nic nedopočítávat pro vypnuté/chybějící peery. Stabilně jej serializovat podle numerické IP peer.
6. Výpočet pracuje nad jedním immutable ClientConfig. Dočasný vstup obsahující payload key a secret ukládat s omezenou životností a po použití bezpečně mazat. Nelogovat jej.

### Přesný full-auth info request

Rozšíření použije jeden JSON objekt s Content-Type application/json, nikoli data NDJSON. Základní pole jsou type = auth-full, src, timestamp, requestId a tag. Navíc je přítomné alespoň jedno z polí maskedCommunicationContracts nebo includeMaskedCommunicationContracts.

**Přítomnost** kteréhokoli rozšířeného pole zapíná full-auth, i když je include false nebo seznam prázdný. Absence obou polí ponechá výhradně původní auth-semi větev a její 4096B limit. Samostatný auth-full bez rozšíření se nepřijímá.

Návrh kanonizace pro implementaci a normativní protokol:

- Nejprve úplně validovat request včetně duplicitních členů. Neznámé top-level pole a neznámý člen záznamu odmítnout.
- Sestavit objekt **E** obsahující jen přítomná rozšířená pole, v tomto pevném pořadí: maskedCommunicationContracts, includeMaskedCommunicationContracts.
- Záznamy seznamu serializovat jako {"peer":"IP","hash":"HEX"}, v tomto pořadí členů a v původním pořadí pole. Server při ověřování podpisu pole netřídí.
- E je kompaktní UTF-8 JSON bez whitespace, BOM a koncového LF. Validované peer/hash jsou ASCII bez potřebných escape sekvencí. Boolean se zapisuje true/false. Nepřítomné pole se vynechá, nikoli nahradí false, null nebo [].
- Klientský builder publikační seznam před vytvořením requestu numericky seřadí. Změna pořadí již podepsaného pole zneplatní podpis. Pouhá změna pořadí členů JSON nebo whitespace při zachování hodnot nemění kanonický význam.
- extensionHash = lowercaseHex(SHA256(UTF8(E))).
- Kanonický podpis nemá koncový LF:

~~~text
PHPVPN-AUTH-FULL
POST
info
<src>
<timestamp>
<requestId>
<extensionHash>
~~~

- tag = canonical unpadded Base64URL(HMAC-SHA256(fullAuthKey, canonical)).
- fullAuthKey se odvodí identicky jako pro data ze server secretu, access key a src. Pouze pro tuto derivaci se přesně prázdný server secret normalizuje stejně jako dnes.
- Type je validací fixováno na auth-full. Akce a metoda jsou fixovány doménou. src, timestamp a requestId jsou přímo v podpisu, všechna rozšíření v extensionHash. Tag je jediná nepodepisovaná hodnota.
- Stávající Authenticator::fullCanonical a authFullCanonical pro data ponechat beze změny. Zavést samostatné **infoFullCanonical** a **buildInfoFull**, nikoli změnu významu současného helperu.

Příklad rozšíření E pro vyžádání bez publikace:

~~~json
{"includeMaskedCommunicationContracts":true}
~~~

Příklad E pro explicitní smazání a současné vyžádání:

~~~json
{"maskedCommunicationContracts":[],"includeMaskedCommunicationContracts":true}
~~~

Oba vstupy musí mít odlišný extensionHash. Rovněž odlišit include false od jeho absence.

### Limity, validace a chyby

- Zavést serverový **MAX_INFO_REQUEST_BODY_BYTES = 65536** a klientský shodný protokolový limit. Strop je současně nejvýše MAX_REQUEST_BODY_BYTES. Původní request bez rozšíření nadále odmítnout nad MAX_AUTH_RECORD_BYTES = 4096.
- Zavést **MAX_MASKED_COMMUNICATION_CONTRACTS = 252**. Skutečný povolený počet je minimum tohoto stropu a počtu platných klientských adres serverového VPN rozsahu minus vlastní src. Nevázat jej na MAX_DATA_RECORDS_PER_REQUEST = 16.
- 252 kompaktních dvojic peer/hash včetně auth polí se musí vejít do 65536 B. Totéž explicitně ověřit pro uložený JSON. Limity requestu, počtu, hloubky a jednotlivých stringů kontrolovat před velkými alokacemi.
- Nový schema-specific parser v JsonWire musí odlišit JSON pole od objektu a zachytit duplicitní klíče v kořeni i záznamech. Samotné json_decode s přepsáním duplicit nestačí. Původní parseFlatStringObject neměnit pro data/health.
- peer musí být canonical IPv4, rozdílný od src a platný podle serverových VPN_ADDRESS_PREFIX/VPN_HOST_MIN/VPN_HOST_MAX. Pro peer není nově vyžadován ACCESS_KEYS záznam, intent jej vyžaduje pro autentizovaného src. Záznam neexistujícího/nenakonfigurovaného klienta se stejně neobjeví v clients.
- hash musí být string s přesně 64 znaky [0-9a-f]. Žádná normalizace vadného vstupu. Jeden peer nejvýše jednou, včetně duplicit se stejným hashem.
- src musí být validní a mít vlastní ACCESS_KEYS položku. Použít stejnou dummy-key větev a constant-time porovnání jako data pro neznámého klienta, s obecnou chybou 401.
- Timestamp, request ID, canonical Base64URL a 60sekundové auth skew ověřovat jako dnes.
- Neplatný formát/typ/duplicita/peer/hash → 400 bad_request. Špatná identita, HMAC či čas → 401 unauthorized. Překročený počet nebo velikost → 413 payload_too_large. Chybný Content-Type → 415.
- Selhání požadované explicitní publikace → 503 queue_unavailable. Při nekonzistentních limitech → 503 server_misconfigured. To je nové odlišení od best-effort statistického touch, jehož chyby se nadále nepropagují do data.
- Vše ověřit před diskovým zápisem. Neplatný request nezmění obsah ani mtime.

### Atomické info.json a čtení

Rozšířit Storage o **publishMaskedContracts(client, contracts, nowMs)** s potvrzeným výsledkem a o **readClientInfoSnapshot(client)**, který může vrátit lastSeen a validovaný seznam. Původní standardní info může nadále číst jen metadata přes readLastSeenMs.

Uložený tvar je přesně jednoduchý objekt:

~~~json
{"maskedCommunicationContracts":[{"peer":"10.99.99.3","hash":"<64 lowercase hex>"}]}
~~~

1. Po full-auth a validaci publikace zajistit použitelný vlastní adresář klienta.
2. Zapisovat do jedinečného dočasného souboru ve stejném adresáři, například .info-<uuid>.tmp. Otevřít výhradně pro nový soubor, zapsat všechny bajty s kontrolou krátkého zápisu, provést flush, nastavit publikační mtime a zavřít.
3. Nahradit info.json atomickým rename v témže filesystému. Nikdy nepoužít předchozí unlink(info.json) ani zápis přímo do živého souboru. Při neúspěchu zachovat původní info.json, uklidit vlastní temp soubor a vrátit publikační chybu.
4. Publikace vždy přepíše celý seznam. [] uloží prázdný seznam, odstraněné peery nelze ponechat ze staré verze. Publikace obchází třicetisekundový touch interval.
5. Běžný data request a autentizovaný include-only/false request bez publikace používají pouze best-effort touch. Existující JSON nečtou ani nepřepisují při touch. Původní semi-auth info nemá identitu, proto jím nelze touchovat neověřené src.
6. Souběžné publikace jsou celé náhrady, poslední úspěšný rename vyhrává. Není požadován merge ani nový per-client dlouhý zámek. Čtenář pro rozšířené info použije jeden otevřený file handle pro omezené čtení i fstat, aby nekombinoval obsah jedné generace s mtime jiné.
7. Nulový historický marker, chybějící pole, nečitelný, přerostlý nebo vadný JSON znamená neplatný/chybějící publikovaný seznam. Navenek vrátit [] a nikdy částečně zachráněné neověřené dvojice. Má-li klient platné čitelné mtime, může zůstat v clients. Bez platného lastSeen jej vynechat.
8. Dočasné soubory nesmějí být dávkami, vstupem drain ani součástí kapacitního účtování. V rámci existujícího bounded sweep přidat přesně vymezený úklid osiřelých .info-<uuid>.tmp starších než nový pevný INFO_TEMP_TTL_SECONDS = 300, nikoli obecné mazání .tmp. Čerstvý soubor živého writeru nesmí být odstraněn.
9. Zápis seznamu je potvrzený, touch zůstává best effort. HTTP chyba po dokončené publikaci neznamená rollback serverového seznamu. Ztráta odpovědi se nesmí řešit domněnkou, že server nic neuložil.

### Filtrovaná odpověď a kompatibilita

- Jen include === true zapne rozšířený tvar. Publikace bez include nebo s false vrátí přesně původní top-level strukturu i klientské objekty {address,lastSeen}, bez skrytých nových polí.
- Pro rozšířenou odpověď zůstávají serverVersion, generatedAt, clients a limits. Každý clients záznam dostane maskedCommunicationContracts jako pole.
- U cizí address vrátit nejvýše jednu validní dvojici, jejíž peer === ověřené src žadatele. U vlastní address vždy []. Bez shody []. Nikdy nevracet původní délku seznamu, cizí peery, počty skrytých kontraktů, skryté hashe ani storage informace.
- Procházet pouze nakonfigurované adresy v numerickém pořadí. Klienty bez lastSeen vynechat, žádné null ani Never.
- Vygenerovat generatedAt pro jednu odpověď po zpracování její publikace/touch. Pro každého klienta pracovat s konzistentním snapshotem. Není vyžadována globální transakce přes všechny klienty.
- Zachovat limit MAX_INFO_PLAINTEXT_BYTES = 262144. Deterministicky ubírat celé klientské položky od numericky nejvyšší adresy. Nikdy neořezávat položku, hash nebo JSON string. Pokud se nevejde ani základní objekt, vrátit server_misconfigured.
- Šifrovaný obal, infoKey, salt, IV, GCM a AAD zůstávají původní. Nová response top-level capability pole tento plán nepřidává.

ResponseParser rozšířit o explicitní režim standard/extended a očekávané src:
- standard nadále přijímá původní odpovědi a stávající běžné toky. Neoslabovat stávající validace.
- extended vyžaduje maskedCommunicationContracts u všech vrácených klientů, validní pole/dvojice, unikátní adresy, validní čas a pouze peer rovný očekávanému src. Self musí mít []. Cizí klient nejvýše jednu dvojici. Vadné rozšíření odmítne celou odpověď před publikací limitů nebo UI dat.
- Chybějící rozšířená pole nelze vyhodnotit jako prázdný platný seznam. To by přijalo starý server.
- Prázdné clients neposkytuje důkaz, že server rozšíření podpořil. Pro explicitně rozšířený klientský tok je konzervativně odmítnout jako neověřitelné rozšíření. Původní standardní parser prázdné clients dále přijímá. Za normálního Connectu již existuje vlastní marker po data i publikaci. Include-only jej může vytvořit best-effort touch. Pokud není čitelný žádný marker, UI ukáže chybu načtení, nikoli předstíraný ověřený přehled. Prázdná UI tabulka je běžně reprezentována odpovědí obsahující jen vlastní klientský záznam.
- Omezení filtrovaného výsledku na očekávané src ověřuje i služba, ne až Tray.

### Klientská a službová implementace

1. Zavést typované InfoRequestOptions rozlišující absent a present-empty seznam i absent a false include. Vyhnout se defaultním parametrům, které by nechtěně zapnuly publikaci.
2. Přidat NetworkWorker::performExtendedInfo vedle performInfo. Používá již ověřený serverový čas, fullAuthKey a nový builder, předává parseru režim a vlastní IP.
3. Limity a ostatní stav z odpovědi zveřejnit až po úplném ověření GCM, requestId a plaintextu. Selhání posledního záznamu nesmí zanechat částečně aktualizované limity.
4. Přidat typ **ServerInfoSnapshot** s přesným infoPlaintext, localAddress a lokálně vypočtenými maskovanými kontrakty. Tajné klíče ani celý config nejsou součástí snapshotu.
5. Přidat **VpnEngine::fetchServerInfoOnce** založené na izolované cestě fetchInfoOnce. Jednou načte configStore_.readValid(), z téhož snímku vytvoří identitu, HTTP nastavení i lokální hashe a provede vlastní health → extended info(include true, publikace absent).
6. Zachovat původní fetchInfoOnce a lokální get-info pro standardní konzolovou diagnostiku. Nový přehled dostane v bloku 4 vlastní IPC příkaz, aby se nerozbil přímý plaintext formát get-info a nebylo nutné přenášet do Tray celý config.
7. Obě one-shot operace sdílejí omezenou diagnostickou kapacitu, cancellation a životnost dočasného transportu. Nedržet ConfigStore/config/state zámek během kryptografie nebo HTTP. Žádné retry, data polling, spuštění tunelu ani zásahy do provozních čítačů.

### Testy

- PHP tests.php: oba autentizační režimy, include false stále full-auth, publish-only standardní response, explicitní [], zachování uloženého JSON po touch, okamžitá publikace i v intervalu 30 s.
- Nezávislé golden vektory v PHP, C++ a Pythonu pro numerické .2/.10, obrácené pořadí IP, různé klíče, Unicode/mezery/casing secretu, prázdný secret, absenci/false/[] a více pořadí requestu. Vektory zapsat jako pevné očekávané canonical bytes a hashe, nepočítat expected stejným testovaným helperem.
- Negativní podpisy: změna src, timestamp, requestId, include, peer, hash, přidání/odebrání pole i změna pořadí seznamu. Data podpis použít na info a opačně, oba musí selhat.
- Bezpečnostní fixture A, B, C, D: A načte B→A, nikoli B→C nebo C→D. Vlastní seznam A se hromadně nevrátí. Totéž pro include-only bez publikace. Samotná znalost server secretu nestačí pro request za A.
- Porovnat standardní odpověď před/po publikaci při stejných hodinách a mtime: žádná nová pole. Izolovaně změnit pouze skrytý kontrakt C–D a pro A ověřit nezměněná relevantní filtrovaná data bez vedlejších počtů.
- Hranice 252/253, 65536/65537 B, 4096/4097 B pro původní request, nevalidní typy, null, string místo bool, objekt místo pole, duplicitní a escapované duplicitní názvy.
- Disk testy: čtená data během souběžných publikací jsou vždy celý starý nebo celý nový JSON. Chyba zápisu/flush/rename zachová původní soubor. Vadný JSON nedá validní seznam. Marker a temp soubory nejsou frontou. Přerušení writeru a cleanup testovat jen v izolované fixture.
- tests-integration/test_info.py, test_security.py, test_limits.py, test_concurrency.py a nový test_info_extensions.py. Změnit starý test_client_marker_is_an_empty_file na test čerstvého markeru bez publikace plus test zachování JSON, ne na globální předpoklad prázdného obsahu po jakémkoli data.
- C++ test_crypto.cpp, test_wire.cpp, nové MaskedContracts/InfoExtensions suite a test_runtime_status.cpp. Zachovat původní crypto vektory health/data beze změn.
- Služba: jediné čtení configu, nový snímek po změně souboru, include-only bez publikace, izolace aktivního spojení, chybný rozšířený response po HTTP 200, cancel a žádný retry.

### Rizika a dokumentace

Největším rizikem je rozdílná kanonizace PHP/C++/Python, použití normalizovaného secretu pro nesprávný hash, tichá ztráta uloženého seznamu a únik cizího vztahu přes neodfiltrovaná data. Pro každý problém je výše samostatná negativní brána.

Protokol zachovává stávající omezenou anti-replay ochranu timestampem. Platný publikační request lze uvnitř okna zopakovat a tím znovu nahradit seznam. Nezavádět nevyžádanou historii request ID ani tvrzení o silnější ochraně. Response crypto sdíleným server secretem také zůstává podle intentu původní. Filtrování je autorizace na serveru, nikoli nová per-client šifra odpovědi.

Aktualizovat:
- **protocol.md**: 1.1/1.2, 3.2, 6.1, 7, celý oddíl 8, 10.5–10.6, 11.1, 13–14, 16 a checklist 17. Odstranit normativní zákaz čtení obsahu markeru a odlišit best-effort touch od potvrzené publikace. Přidat přesné canonical bytes, limity, příklady a filtr.
- **client.md**: kontrakty, configured versus effective secret, protocol parser, síťové a diagnostické cesty služby, jednorázový ServerInfoSnapshot. Explicitně uvést původní get-info i novou interní operaci.
- **phpvpn.php**: komentáře info, storage, limitů, chyb a markeru.
- **tests-integration/README.md**: nový helper a testovací soubory, matice profilů, bezpečnostní fixture a rozdíl HTTP testů od fault-injection disk testů.
- Staré intent/plan artefakty a historické test reporty nepřepisovat.

### Validační příkazy a akceptace

~~~powershell
& 'D:\nginx\php8.5.9\php.exe' -l .\phpvpn.php
& 'D:\nginx\php8.5.9\php.exe' .\tests.php
powershell -ExecutionPolicy Bypass -File .\client\tools\build-x64.ps1 -Preset debug -Configure -Test
.\work\build\debug\tests\phpvpn-client-tests.exe MaskedContracts
.\work\build\debug\tests\phpvpn-client-tests.exe InfoExtensions
uv run --python 3.13 --with pytest --with cryptography --with requests -- pytest tests-integration/test_info.py tests-integration/test_info_extensions.py tests-integration/test_security.py tests-integration/test_limits.py tests-integration/test_concurrency.py --server-config work/intent-v003/fixture.json -v
git diff --check
~~~

MaskedContracts a InfoExtensions jsou nové plánované názvy suite. C++ harness v současnosti dovoluje úspěšný návrat při nule vybraných testů, proto ve výstupu vždy ověřit nenulový počet. PHP fixture musí být po změně skutečně znovu nasazena.

Akceptace vyžaduje shodné golden vektory všech tří implementací, nedotčené data vektory, funkční starý klient proti novému serveru, potvrzený atomický replace, správné []/absence chování, žádný cizí kontrakt mimo žadatele a nepřijetí rozšíření bez platného full-auth.

## 3. Connect tok, komunikace služby s Tray a první publikace

### Výsledek a závislosti

Connect provede health → standardní info → prázdný full-auth data → publikační extended info → dosavadní spuštění tunelu a pracovních rolí. Nový checkpoint **Communication contracts** zezelená až po úplném ověření rozšířené odpovědi.

Závislostí je dokončený blok 2 včetně builderu, parseru a serverového úložiště. Přehledové okno z bloku 4 pro Connect není potřeba.

### Konkrétní oblasti

| Soubor a symbol | Změna |
|---|---|
| client/src/core/runtime/vpn_engine.cpp:302–426, runConnectSequence | Vložit čtvrtý síťový krok za verifyAccessKey a před starting_tunnel. |
| vpn_engine.cpp:490–685, runNetworkThread, a :687–710 status | Reset, recovery, zrušení a zveřejnění nového checkpointu. |
| vpn_engine.h | Člen communicationContractsCheck_ a evidence publikace v aktuální connectionEpoch. |
| client/src/core/runtime/status_snapshot.h/.cpp | ConnectionPhase::communication_contracts_check, nový checkpoint a bezpečná klasifikace chyby. |
| client/src/core/ipc/ipc_protocol.cpp:197–225, buildStatusResponse | Pole communicationContractsCheck v každém stavovém snapshotu. |
| client/apps/tray/status_model.h/.cpp:37–122 | Validace a přenos čtvrtého výsledku, bez odvozování úspěchu z ostatních tří. |
| client/apps/tray/progress_model.h/.cpp:27–85 | Celý čtyřbodový snapshot a bezpečný chybový katalog. |
| client/apps/tray/progress_window.h/.cpp, progress_layout.h | Čtyři řádky, nová geometrie a delší label. Dnes pole i smyčky mají pevnou délku 3. |
| client/tests/unit/tray_fixture.h, test_status_contract.cpp, test_runtime_status.cpp, test_tray_status.cpp, test_tray_progress.cpp | Fixtures a skutečné runtime/status/progress regrese. |
| client/tests/ui/tray_ui_tests.cpp a ui/dev_preview.inc | Aktualizace syntetického backendu, scén a kontrol počtu checkpointů. |

### Pořadí a stavový kontrakt

1. V core přidat nový stav fáze a field **communicationContractsCheck**, s hodnotami pending/success/failed jako ostatní checkpointy. Přidat je do všech builderů, parserů a fixture. Chybějící pole ve staré službě nesmí nový Tray vykreslit jako success.
2. Resetovat nový field při novém uživatelském Connectu, Disconnectu a stopu spolu s ostatními. Zachovat connectionEpoch/connectAttemptId a pravidla odhazování cizích nebo starých výsledků.
3. Po úspěchu verifyAccessKey nastavit communication_contracts_check a zkontrolovat cancellation. Z aktivní konfigurace tohoto pokusu vypočítat všechny aktuálně platné kontrakty. Zavolat performExtendedInfo s přítomným úplným seznamem, i když je [], a include true.
4. Ověřit celý GCM obal a extended plaintext. HTTP 200 nebo úspěšná publikace na serveru samy o sobě nestačí. Teprve po dokončení nastavit communicationContractsCheck = success a pokračovat na tunel.
5. Chyba nového kroku nastaví failed. Tunel ani worker role se nespustí a connected/running/tunnelReady se neohlásí jako nový úspěch. Zachovat dokončené výsledky předchozích tří kroků a bezpečně uvolnit připravené lokální prostředky.
6. Přidat bezpečný kód **communication_contracts_verification_failed** pro odmítnuté/chybné/nepodporované rozšíření, s popisem ověření kontraktů a doporučením zkontrolovat kompatibilitu serveru. Neoznačit chybu tohoto kroku automaticky za chybný ACCESS_KEY, třetí bod už proběhl.
7. Timeout zachová katalog communication_timeout, ale neúspěšný checkpoint bude čtvrtý. Nový krok po neúspěchu skončí jako terminální pokus bez automatického opakování publikace, včetně neznámého výsledku po ztrátě odpovědi. Další vědomý Connect je nový pokus. Cancel nesmí otevřít chybový detail patřící zrušené generaci.
8. Zařadit nový krok do existujícího omezeného mechanismu HTTP diagnostiky, je-li odmítnutá odpověď dostupná. Nepřidávat plaintext kontraktů, klíče nebo request do veřejného statusu či logu. Klasifikace chyby nesmí záviset na dostupnosti diagnostického záznamu.
9. Runtime dnes používá runConnectSequence(false) i pro recovery. Proto nestačí vložit bezpodmínečný čtvrtý request. Zavést evidenci **communicationContractsVerifiedForEpoch** a prvního pokusu o publikaci:
   - nová uživatelská connectionEpoch začne bez ověření,
   - po prvním úspěšném rozšířeném kroku se už při recovery stejné epochy nepublikuje ani nevyžaduje extended info,
   - pokud předchozí síťový výpadek nastal ještě před dosažením čtvrtého kroku, pokračování rozpracovaného uživatelského Connectu nesmí tento krok přeskočit jen proto, že initialBootstrap je false,
   - první skutečné provedení nového kroku aktualizuje jeho výsledek. Již dokončenou historii checkpointů recovery nepřepisuje,
   - neúspěšný pokus o čtvrtý krok nevede k recovery retry tohoto requestu,
   - uživatelský reconnect načte novou uloženou konfiguraci a publikuje nový úplný seznam.
10. Běžné periodické health/info/datové operace zůstanou bez rozšířených polí. Ověřená connect odpověď se může uložit jen v dosavadní omezené paměťové podobě, nesmí spustit polling přehledu.

### Tray a geometrie

- V Connection checks zobrazit přesně Server URL, Server secret, Access key, Communication contracts.
- Použít existující pending/success/failed ikony a barvy, úspěšný čtvrtý bod je zelený.
- Snapshot vždy převzít celý, neskládat čtyři historické úspěchy z různých epoch.
- Rozšířit arrays, smyčky, fonty, custom draw, resources a layout na čtyři položky. Delší Communication contracts nesmí kolidovat se stavem Success. Šířku labelu měřit nebo vhodně zalamovat, ne ponechat původní pevný sloupec pro krátké popisky.
- Přepočítat výšku karty a scroll rozsah pro 96/120/144/192 DPI a úzké okno. Zachovat 500ms get-status polling, jediné get-config pro Connection details, nezávislý Disconnect a existující třísekundové zavření po skutečném paint úspěchu.
- Disconnect okno zůstává bez checkpointů.

### Testy, dokumentace a rizika

Runtime transport double zaznamená skutečné akce a request body. Ověřit přesné pořadí, úplnost seznamu, [] při žádných kontraktech, absence publikace před úspěchem data a stav všech bodů při každém možném selhání.

Doplnit:
- vadný access key selže ve třetím bodě a žádný extended request neodejde,
- starý server odmítající auth-full i server vracející původní plaintext při HTTP 200 selžou ve čtvrtém bodě,
- malformed poslední položka odpovědi zabrání zelenému bodu i tunelu,
- timeout/cancel těsně před requestem, během HTTP a po odpovědi nedokončí cizí epochu,
- výpadek před čtvrtým bodem a následná recovery jej nepřeskočí,
- recovery po úspěšném Connectu nový seznam nepublikuje,
- změna uloženého configu během aktivního spojení nezmění publikaci, další uživatelský reconnect ano,
- nové status pole prochází služba → IPC → parser → model → kreslení, včetně chybějících a neplatných hodnot,
- zamítnuté rozšíření je terminální i bez dostupného HTTP capture.

Aktualizovat **protocol.md** oddíl 12.1 a integrační scénáře, **client.md** connect/recovery, status IPC, error katalog, one-shot versus provozní účtování a testy. V **tray_UX.md UX05, UX14** změnit normativní trojici na čtveřici, pořadí, nový label, barvy a chyby. V **client/tests/ui/README.md** aktualizovat Connect preview. Všechny zmínky o třech bodech v dotčených hlavních dokumentech vyhodnotit kontextově, neměnit historické reporty.

Hlavní riziko je nepřiznané opakování publikace v recovery nebo zelený bod nad starým response. Druhé riziko je přetečení pevné geometrie čtvrtého řádku. Samostatně ověřit, že nový diagnostický krok neblokuje Disconnect.

### Validační příkazy a akceptace

~~~powershell
powershell -ExecutionPolicy Bypass -File .\client\tools\build-x64.ps1 -Preset debug -Configure -Test
.\work\build\debug\tests\phpvpn-client-tests.exe Runtime
.\work\build\debug\tests\phpvpn-client-tests.exe StatusContract
.\work\build\debug\tests\phpvpn-client-tests.exe TrayProgress
.\work\build\debug\tests\phpvpn-client-tests.exe TrayLayout
powershell -ExecutionPolicy Bypass -File .\client\tools\dev-tray.ps1 -Mode Test -Filter communication_contracts_connect
powershell -ExecutionPolicy Bypass -File .\client\tools\dev-tray.ps1 -Mode Test -Filter command_reader_private_disconnect
git diff --check
~~~

communication_contracts_connect je nový plánovaný native test filtr. Ověřit nenulový výběr všech filtrů. Opt-in native testy jsou mimo běžný CTest.

Přidat skutečný PHP/C++ scénář do client/tests/contract/test_server_contract.cpp nad oddělenou fixture s podporovaným rozšířením. Sestavení této sady vyžaduje PHPVPN_BUILD_CONTRACT_TESTS=ON a běžící relay. Dostupnost serveru nesmí být vydávána za úspěšný skip.

Akceptace: běžný úspěšný Connect má přesně čtyři zelené body ve správném pořadí a nový seznam v info.json ještě před spuštěním tunelu. Chyba čtvrtého kroku vede k bezpečnému neúspěchu. Runtime nezavádí periodické publikace a reconnect je jediný způsob aplikace změněného uloženého seznamu do aktivního spojení.

## 4. Show server info, Refresh a Who is online

### Výsledek a závislosti

Otevření Show server info i Refresh načte full-auth rozšířenou odpověď bez publikace lokálních změn. Read-only tabulka zobrazí cizí klienty, jejich stav a správný počet valid online clients. Porovnání vychází z aktuálně uložené lokální konfigurace načtené při dané akci, nikoli ze snímku posledního Connectu.

Blok závisí na typovaném protokolu a fetchServerInfoOnce z bloku 2 a uzavírá integraci po bloku 3.

### Konkrétní oblasti a rozhodnutí architektury

| Soubor/symbol | Plán |
|---|---|
| client/src/core/ipc/ipc_protocol.h/.cpp, IpcCommand, parseRequest, buildRequest | Nový bezparametrový příkaz get-server-info s korelovanou result obálkou. |
| client/src/core/ipc/ipc_dispatcher.cpp:14–96, handle | Zavolat fetchServerInfoOnce a vrátit typovaný snapshot. Původní get-info zachovat. |
| client/apps/service/service_host.cpp:119–134, classifier | get-server-info zařadit do diagnostic stejné kapacity jako get-info a Wizard kontroly. |
| client/apps/tray/tray_application.h/.cpp, IDM_SHOW_INFO, finishInfo:420–434 | Otevření a refresh přepojit na nový příkaz, typovaně přijmout výsledek. |
| client/apps/tray/status_reader.h/.cpp, CommandReader a readResponse:82–96 | Použít korelaci requestId existující result obálky a omezenou asynchronní cestu. |
| client/apps/tray/editor_window.cpp:259–406 | Dnešní sdílená read-only větev pro info přestane být produkčním cílem Show Info. Zachovat chování Edit Config, neprovádět nesouvisející refaktor ukládání. |
| client/apps/tray/contracts_window.cpp:84–214 a ui_style.h:28–46 | Reference vzhledu tabulky, stránkování po 20 a palety. |
| client/apps/tray/layout.h/.cpp, ui_resources.h/.cpp, ui_controls.h/.cpp | Sdílená patička, DPI, fonty, primary tlačítko a přístupnost. |
| client/apps/CMakeLists.txt, client/src/CMakeLists.txt, client/tests/CMakeLists.txt | Zapojit nové okno/model do produkčního i testovacích targetů. |

Nové navržené soubory:
- client/src/core/presentation/server_info_model.h/.cpp, čistý **ServerInfoModel** bez Win32 a lokálních hodin pro online logiku.
- client/apps/tray/server_info_window.h/.cpp, samostatný **ServerInfoWindow** se dvěma native kartami.
- client/apps/tray/server_info_layout.h pro testovatelnou geometrii.
- client/src/core/presentation/table_pagination.h/.cpp pro sdílenou čistou logiku velikosti stránky a čísel pageru, převzatou z aktuálního ContractsWindow bez změny jeho save/lifecycle chování.
- client/tests/unit/test_server_info_model.cpp a test_server_info_layout.cpp.

### Jeden response snapshot pro obě karty

**Who is online a Raw server information jsou dvě prezentace jednoho a téhož ověřeného rozšířeného info response snapshotu.** Who z něj renderuje typovanou tabulku. Raw zobrazuje celý aktuální dešifrovaný JSON téhož response, včetně maskedCommunicationContracts, které již server bezpečně odfiltroval pouze pro autentizovanou vlastní IP. Lokální srovnávací údaje jsou doplňkem tohoto společného snapshotu, nikoli druhou serverovou odpovědí.

Pro Raw kartu nesmí proběhnout druhý nezávislý info request. Otevření či Refresh vyprodukuje jediný ověřený info response, jehož identita a generace jsou společné pro obě karty. Přepnutí karty pouze změní prezentaci. Úspěšný Refresh nahradí obě prezentace atomicky, neúspěšný zachová obě na předchozím snapshotu. Nesmí existovat interval, v němž tabulka popisuje jiný response než zobrazený raw JSON.

### Přesná služba → Tray smlouva

Request:

~~~json
{"ipcVersion":1,"requestId":"<UUIDv4>","command":"get-server-info"}
~~~

Úspěšná odpověď:

~~~json
{
  "ipcVersion":1,
  "requestId":"<stejné UUIDv4>",
  "result":{
    "infoPlaintext":"<přesné ověřené dešifrované JSON bajty reprezentované JSON stringem>",
    "localAddress":"10.99.99.2",
    "localMaskedCommunicationContracts":[
      {"peer":"10.99.99.3","hash":"<64 lowercase hex>"}
    ]
  }
}
~~~

- Hodnotu ipcVersion použít ze stávající kIpcVersion. Nový příkaz rozšiřuje současnou lokální smlouvu. Starší služba musí vrátit bezpečnou chybu nepodporovaného příkazu, nikoli vést k fallbacku na neověřená data.
- Pole localAddress a lokální hashe pocházejí z téhož jednorázově načteného ClientConfig jako full-auth src. Jejich zdrojem není server, cached aktivní config ani druhý nezávislý get-config.
- Do pipe se neposílá SERVER_SECRET, ACCESS_KEY, payloadKey, celý config ani auth tag. Lokální hashe jsou pouze pro porovnání, v Raw kartě se nevykreslují.
- result builder a Tray parser mají vlastní přesné schema, ověřují typy, duplicitní peery, rozsah IP, hash a celkovou velikost. Před zveřejněním modelu znovu ověřit plaintext v extended režimu pro localAddress.
- Zachovat kIpcMaxMessageBytes = 4 MiB. Změřit skutečnou velikost celé obálky včetně JSON escapování infoPlaintext. Výsledek nikdy neořezávat. Při překročení bezpečná ipc_message_too_large.
- Selhání použije stávající korelovanou error obálku, bez raw výjimek a tajemství.
- Nový příkaz nevytváří soubor s výsledkem. Původní get-info pro CLI dále vrací standardní plaintext přímo a bez obálky.
- Jeden get-server-info provede právě health a jedno extended info s include true, bez maskedCommunicationContracts. Má samostatný transport, config snapshot a časový kontext, neovlivní běžící spojení ani čítače. Sdílí jednu diagnostic kapacitu, další souběžná akce dostane busy.

### Pořadí změn a životnost okna

1. Dokončit IPC typ, builder, dispatcher a classifier nad již existující službovou operací. Testovat je bez UI.
2. Implementovat čistý model nad kompletním ServerInfoSnapshot. Nejprve vytvořit a validovat celý nový model v dočasném objektu.
3. Přidat samostatné ServerInfoWindow se dvěma kartami ve stávajícím pořadí. Tab header design a read-only raw editor převzít ze současného Show Info. Edit Config zachová původní save a dirty lifecycle.
4. Při první volbě IDM_SHOW_INFO vydat jeden get-server-info. Otevřít výchozí Who kartu po ověřeném výsledku. Při neúspěchu zobrazit bezpečnou chybu, nevytvářet falešný prázdný úspěšný přehled.
5. Do levé strany patičky přidat **Refresh** jako ButtonRole::primary, modré, s OK vpravo. Nepoužít červenou reset roli.
6. Refresh je jediná nová síťová akce v otevřeném okně. Vydá stejný nový IPC request a v něm nepřidá publikaci. Během letu vypnout jen opakovaný Refresh, zobrazit načítání a zachovat poslední kompletní data.
7. Po úspěchu jedinou UI transakcí nahradit tabulku, titulek/počet, raw plaintext i lokální porovnávací snímek. Při neúspěchu zachovat starý celek, označit neúspěšný refresh a dovolit nové ruční kliknutí. Nemíchat nový raw JSON se starými řádky.
8. Odpovědi vázat na windowGeneration a request generation. Zavření, nové otevření, restart služby nebo cancel starou odpověď zneplatní. Nedovolit, aby opožděný výsledek znovu otevřel zavřené okno.
9. Opakovaná volba Show Info při již otevřeném okně jej aktivuje, nekumuluje paralelní načtení. Nové načtení provede Refresh. Přepínání karet a pageru neprovádí IPC, HTTP ani čtení konfigurace.
10. Při refreshi zachovat aktivní kartu a pokud možno fokus podle identity peer. Číslo stránky omezit na nový platný rozsah. Při změně raw obsahu bezpečně omezit výběr/scroll, při pouhém přepnutí karet je zachovat přesně.
11. Změna uložené konfigurace v průběhu načítání nevede k míchání dvou konfigurací. Výsledek reprezentuje snímek přečtený na začátku akce, další ruční Refresh načte novější soubor. Nezavádět timer ani automatický reconnect.

### Model stavů, počet a řazení

Převést generatedAt a lastSeen na serverové UTC milisekundy. Vlastní IP localAddress odfiltrovat před počítáním a stránkováním. Nedoplňovat řádky pro lokální kontrakty, které server vůbec nevrátil.

Pro každého cizího klienta:
- **Contract** = canonicalContractId(localAddress, address) z bloku 2.
- **Last seen** = lokálně čitelný datum a čas z jeho lastSeen. UTC převod na lokální čas slouží pouze prezentaci, ne výpočtu online.
- **Offline**, když generatedAt - lastSeen > 60000. Šedé pozadí. Hashovou shodu pro tento řádek nevyhodnocovat.
- **Online - valid communication contract**, když rozdíl ≤ 60000, lokální kontrakt existuje a jediný vrácený peer == localAddress má hash roven lokálnímu hashi pro address. Světle zelené pozadí.
- **Online - invalid communication contract**, když rozdíl ≤ 60000 a předchozí shoda neplatí. Platí i při chybějícím lokálním kontraktu, prázdném seznamu nebo rozdílném hashi. Světle žluté pozadí.

Výpočet provést v dostatečně širokém signed typu bez underflow. Budoucí lastSeen dává záporný rozdíl a podle přesného pravidla intentu patří do online větve. Nepřidávat nové automatické opravování času. Neplatný timestamp je chyba parsování, nikoli nový čtvrtý stav. Hranice přesně 60000 ms je online, 60001 ms offline.

Seřadit celý model před stránkováním podle (stavová skupina valid, invalid, offline, nižší numerická IP kontraktu, vyšší numerická IP kontraktu). Nepoužívat lexikografické řazení stringů ani počítání jen aktuální stránky.

Nadpis je přesně **Server information (N valid online clients)**. N počítá validní online cizí řádky v celém přijatém seznamu, bez vlastní IP. Neodhadovat počet klientů případně deterministicky vynechaných serverovým limitem. Žádné lastSeen: null ani Never.

Aktuální lokální klíč se může shodovat s publikovanou hodnotou protistrany, a tedy dát zelený stav ihned po uložení a novém otevření/Refresh bez reconnectu. To neznamená, že se změnil aktivní tunel nebo náš dříve publikovaný seznam. Ten se aktualizuje až uživatelským reconnectem.

### Read-only tabulka, barvy, pager a dostupnost

- Tři sloupce v přesném pořadí **Contract**, **Last seen**, **Status**. Žádné Enabled, Key, Change ani ukládací ovládání.
- Použít nativní report tabulku s dostupnými textovými buňkami a custom draw barev řádku. Nezavádět vlastní UI Automation provider. Hlavička, zaoblený kontejner, border, padding a fonty mají odpovídat Communication Contracts.
- Sloupce dimenzovat tak, aby byl celý nejdelší stav dostupný, případně horizontálním posunem. Hlavička i data se posouvají konzistentně, patička zůstává dostupná.
- Použít **ui::Color::successBackground (#F0FAF2)**, **warningBackground (#FFF6E5)** a **disabledBackground (#F0F2F5)**, tmavý čitelný text a odpovídající high contrast mapování přes UiResources. Barva je doplněk přesného textu, ne jediný nositel stavu.
- Velikost stránky je **20**. Celý model může mít až 252 cizích řádků, ale widget obsahuje jen data aktuální stránky, bez skrytých widgetů všech stránek.
- Pager skrýt při 0–20 řádcích. Při více zobrazit «, ‹, nejvýše sedm čísel s neaktivními výpustkami, ›, ». První a poslední číslo dostupné, aktivní modré, krajní neproveditelné akce disabled.
- Sdílet s ContractsWindow čistou logiku pageSize, pageCount a výběru čísel. Extrakce nesmí změnit editaci, dirty koncept, Save ani mapování peer v Contracts.
- Tab/Shift+Tab prochází karty, aktivní tabulku nebo raw editor, pager, Refresh a OK. Šipky v kartách mění kartu. Šipky v tabulce vybírají řádek. Pager funguje klávesnicí jako stávající nativní tlačítka, má stejné First/Previous/Next/Last page a Page X of Y názvy.
- Zachovat současný Ctrl+End skok na poslední datový řádek napříč stránkami a PageUp/PageDown pro posun obsahu. V raw editoru zůstávají běžné textové zkratky. Nedovolit, aby Enter na pageru/Refresh zavřel okno místo aktivace tlačítka. Escape a OK zavírají, Enter mimo akční control může zůstat defaultní OK.
- Focus a výběr vázat na peer, nikoli pořadové číslo řádku. Po refreshi chybějící peer přesune fokus na tabulku nebo nejbližší platný řádek. Přístupné názvy musí obsahovat kontrakt, čas a stav.
- Rozměry nové info obrazovky odvodit od současných 1080 × 560 DIP, omezit pracovní plochou monitoru. Na malé ploše použít scroll a adaptivní footer, nikoli oříznutá tlačítka.
- Raw server information zobrazí celý aktuální dešifrovaný serverový JSON. Může se formátovat pro čitelnost, ale žádná serverová pole nevynechat a nepřimíchat lokální IPC metadata. Výsledek zůstává pouze v paměti.

### Testy

**Model a parser**
- 0 cizích klientů, 1, 20, 21, více než 140 a maximum 252. Odpověď pouze se self dá N = 0 a prázdnou tabulku.
- Čas 59999/60000/60001 ms, budoucí lastSeen, rozdílné lokální systémové hodiny a lokální časové zóny. Lokální hodiny nesmějí měnit skupinu.
- Lokální kontrakt absent/present, publikovaný seznam []/shoda/neshoda. Offline větev musí fungovat bez vyhodnocení hashe.
- Numerické .2/.10 a adresy napříč oktety, stejné skupiny, self filtr, žádné Never a součet napříč stránkami.
- Uložit lokálně nový správný klíč bez reconnectu a načíst nový snapshot. Řádek zezelená, obsah vlastního serverového info.json a aktivní ClientConfig se nezmění.
- Podvržená IPC localAddress, vadný result, duplicitní peer, necanonical hash, cizí filtrovaný peer a chybějící extension pole odmítnou celý výsledek.

**Služba/IPC/lifecycle**
- Jeden readValid a jeden health+extended info na otevření či refresh, nikoli get-config a nikoli data request.
- Jeden nový příkaz patří do diagnostic, druhý dostane busy. Disconnect/get-status zůstanou dostupné během pomalého requestu i čtení jeho výsledku.
- 401, timeout, poškozená odpověď a restart služby nevyvolají automatický retry. Pozdní generace se nezobrazí.
- Ověřit skutečnou velikost maximální korelované obálky a její přesné requestId, bez plaintext dumpu do logu/disku.
- Původní CLI get-info stále odpovídá svým původním formátem a nepublikuje kontrakty.

**Win32 UI**
- Doplnit testy v client/tests/ui/tray_ui_tests.cpp a fixtures ui/dev_preview.inc o mixed statuses, refresh success/failure/busy, změnu stránky, close/reopen a opožděnou odpověď.
- Ověřit modrý Refresh vlevo, OK vpravo, přesné texty, full-row barvy, správný počet v nadpisu, první/střední/poslední stránku a maximálně 20 vložených datových položek.
- Přepínání Who/Raw a pager nic nenačítá, raw zůstává read-only a kompletní. Test zaznamená identitu/generaci jediného ověřeného response a porovná tabulku s parsovaným JSONem Raw karty, včetně serverem filtrovaných hashů. Při refresh success i failure musí být obě karty vždy ze stejného snapshotu.
- Model/layout testy pro 96/120/144/192 DPI, skutečné nativní resize/repaint a minimalizaci. Zachovat testy Edit Config a Contracts kvůli sdíleným helperům.
- Manuálně ověřit screen reader, high contrast, klávesnici a přesun mezi monitory. Syntetické UI testy nenahrazují fyzický DPI ani screen-reader důkaz.
- Aktualizovat dosavadní serverInformationTabs, který očekává prázdnou Who kartu a právě jeden get-info. Nově má očekávat novou tabulku a jeden get-server-info na otevření, žádný další request při samotném přepínání.

### Dokumentace v tomto bloku

- **client.md**, zejména IPC a lokální řízení, get-info, Windows Service/tray, Show Info, konfigurace versus aktivní spojení a testovací kapitoly. Přidat přesné schema get-server-info, jednorázové čtení a hranici lokálních dat. Původní get-info popsat odděleně.
- **tray_UX.md UX04, UX06, UX10, UX14**: tabulka, stavy, barvy, Refresh, atomic update, počet, paginace, focus a accessibility. Opravit zastaralé tvrzení o prázdné Who kartě a sjednotit současnou stránku po 20 s místy popisujícími všechny nativní řádky naráz.
- **protocol.md**, 8.2 a klientské akceptační scénáře: stručně vyjasnit použití generatedAt/lastSeen ze stejné odpovědi a že filtrování neznamená potvrzení skutečně aktivního tunelu.
- **client/tests/ui/README.md**: Info preview již má tabulku, nový příkaz a nové scénáře/filtry.
- **tests-integration/README.md** a relevantní **client/tests/e2e/README.md / manual/README.md**: A/B/C/D soukromé filtrování, chybějící kontrakty, změna klíče bez reconnectu a přesný postup ruční akceptace.
- **distribution.md**: pokud se připravuje vydání, zapsat požadavek souběžného nasazení kompatibilní služby a Tray kvůli novému lokálnímu příkazu. Tento plán sám vydání, instalaci nebo nový distribuční balíček nenařizuje.

### Validační příkazy a závěrečná akceptace

~~~powershell
powershell -ExecutionPolicy Bypass -File .\client\tools\build-x64.ps1 -Preset debug -Configure -Test
.\work\build\debug\tests\phpvpn-client-tests.exe ServerInfo
powershell -ExecutionPolicy Bypass -File .\client\tools\dev-tray.ps1 -Mode Test -Filter server_information
powershell -ExecutionPolicy Bypass -File .\client\tools\dev-tray.ps1 -Mode Test -Filter live_resize_repaint
powershell -ExecutionPolicy Bypass -File .\client\tools\dev-tray.ps1 -Mode Test -Filter wp06_scene_matrix
powershell -ExecutionPolicy Bypass -File .\client\tools\dev-tray.ps1 -Mode Test -Filter command_reader_private_disconnect
powershell -ExecutionPolicy Bypass -File .\client\tools\build-x64.ps1 -Preset release -Configure -Test
uv run --python 3.13 --with pytest --with cryptography --with requests -- pytest tests-integration --server-config work/intent-v003/fixture.json -v
git diff --check
~~~

ServerInfo a server_information jsou plánované testové skupiny. Každý filtr musí skutečně vybrat testy. Release preset v tomto projektu znamená RelWithDebInfo, nikoli čisté CMake Release. Native UI targety nejsou automaticky součástí CTest.

Po dokončení všech větších zdrojových změn spustit **graft build** podle AGENTS.md. To patří až k realizaci, ne k tomuto plánování.

Konečná akceptace propojí všechny čtyři bloky:
1. Nasadit pouze nový phpvpn.php do izolované fixture a ověřit status page enabled/disabled, bezpečný/výchozí secret a novou datovou cestu.
2. Připojit A a B se shodným kontraktem. Oba Connecty projdou čtyřmi body. A uvidí B zeleně a vlastní IP nezobrazí.
3. Připojit C bez shodného kontraktu s A. A vidí C žlutě. Publikace B–C a C–D nesmějí A poskytnout jejich hash ani peer seznam.
4. Vyčkat podle serverového času více než 60 sekund bez aktivity B a znovu načíst. B bude šedě, bez vyhodnocování shody.
5. Upravit uložený lokální klíč A na hodnotu publikovanou protistranou. Otevření/Refresh ukáže správnou shodu bez změny aktivního tunelu a bez nové publikace A. Teprve uživatelský reconnect publikuje nový seznam A.
6. Ověřit standardní starý info request, starou response kompatibilitu v běžném novém klientském toku, selhání nového checkpointu proti starému serveru, filtrování a všechny negativní auth testy.
7. Prověřit tabulku po 20, barvy, přesné texty, count celého modelu, Raw JSON, refresh chybu bez částečného přepsání a dostupný Disconnect během diagnostiky.
8. Potvrdit úspěch relevantních serverových, core, runtime, IPC a nativních testů. Odděleně zaznamenat skutečně provedené manuální DPI/accessibility kontroly a případné zbývající limity. Neoznačovat neprovedenou kontrolu jako PASS.

Dokončení realizace znamená splněné akceptace všech čtyř bloků, odpovídající aktuální normativní dokumentaci a ověřený výsledný diff. Při předání uvést změněné soubory, skutečné validační výsledky, neprovedené kontroly a případné provozní omezení. Commit, push, distribuce ani nasazení mimo izolované testy nejsou automatickým výstupem tohoto plánovacího zadání.
