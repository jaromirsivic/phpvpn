# Implementační plán 3: Configuration Wizard a Communication Contracts

Aktualizováno implementací [plan3_fix2.md](plan3_fix2.md). Nový [report](client/tests/implementation-plan3-fix2-report.md) a [matice AK01 až AK17](client/tests/implementation-plan3-fix2-matrix.md) oddělují automatické důkazy od neprovedených SCM, NTFS a ručních UI kontrol.

Aktualizace realizace 6. září 2026: zdroje, dokumentace a testy jsou implementované v pracovním stromu. Provedené testy, distribuční artefakty a dosud nedoložené akceptační brány uvádí [implementační report](client/tests/implementation-plan3-report.md) a [úplná matice B/A/D](client/tests/implementation-plan3-matrix.md). Následující původní stav i závěrečná poznámka o přípravě plánu jsou historické a nepopisují aktuální stav implementace.

Stav: plán realizace, bez provedené implementace. Původně zpracováno 5. září 2026 nad revizí `9f25f1333834596e46394f67f0a056a2595506da` v `C:\git\phpvpn`, aktualizováno 6. září 2026 v pracovním stromu nad revizí `15d8c9abbc14f607ffcc47dbf845b7a374a9a364`. Projekt zatím není nikde nasazený. Tento dokument zahrnuje rozhodnutí zadavatele z navazujícího rozhovoru a nahrazuje rozporné formulace původního zadání v rozsahu této etapy.

Výchozí podklady jsou `protocol.md`, `client.md`, skutečný zdrojový kód, existující testy a konvence z `plan_client.md` a `plan2_client.md`. Cesty souborů níže jsou relativní ke kořeni `C:\git\phpvpn`. Při realizaci se nejprve společně aktualizují normativní dokumenty podle kapitoly 11. Historické poznámky se zpětně nepřepisují.

## 1. Výsledek a hranice etapy

Tray dostane popisný čtyřkrokový průvodce s podmíněným odpojovacím krokem. Služba vlastní načtení a uložení konfigurace i všechny síťové kontroly. Průvodce smí vytvořit nebo změnit `config.json` až při úspěšném Finish. Po úspěchu se zavře a otevře zavíratelné prázdné okno Communication Contracts. VPN se tím nepřipojí.

Součástí jsou změny serverového defaultu, rozšíření výchozí mapy přístupových klíčů, IPC, správy konfiguračního souboru, bezpečného rušení a celkových HTTP timeoutů, Edit Config, testů a dokumentace. Edit Config může po výslovném potvrzení vytvořit chybějící soubor ze společné platné šablony již před otevřením editoru. Jde o samostatnou cestu vedle Finish Wizardu. Není součástí editor komunikačních kontraktů, automatická distribuce klíčů, rezervace IP na serveru, nové drátové endpointy, automatické připojení, implementace macOS/Linux ani změna přijatého modelu veřejného lokálního IPC.

Implementace zůstává C++20, MSVC, CMake/Ninja, Win32, WinHTTP a CNG bez nové knihovny třetí strany. Přenositelné modely nesmějí obsahovat `HWND`, `HANDLE`, Win32 zprávy ani závislost na tray procesu. Pythonové závislosti a běhy se spravují výhradně přes `uv`.

## 2. Závazná rozhodnutí zadavatele

| ID | Rozhodnutí |
|---|---|
| D01 | Přesné menu: Connect/Disconnect, oddělovač, Configuration Wizard, Communication Contracts, Edit Config, oddělovač, Show Info, Statistics, oddělovač, Exit. |
| D02 | Konfiguračně závislé položky menu zůstávají kliknutelné. Služba načte a autoritativně vyhodnotí aktuální soubor až po zvolení konkrétní akce, nikoli při startu tray, periodicky nebo při otevření menu. Chyba či nabídka nápravy se zobrazí až po kliknutí. Bez souboru Contracts zobrazí varování, Edit Config nabídne Ano/Ne pro vytvoření výchozího souboru a po Ano jej vytvoří služba a otevře editor. Disconnect se řídí paměťovým stavem VPN, Exit zůstává dostupné. |
| D03 | Varování uvede skutečně vyhodnocenou absolutní očekávanou cestu a možnost spustit Wizard nebo ručně zkopírovat konfiguraci získanou například od administrátora. Doslovný nerozbalený text `%ProgramData%` nestačí. |
| D04 | Každý start služby po bezpečné přípravě adresáře a jednoznačné recovery jednou klasifikuje autoritativní `config.json`. Pouze skutečně chybějící soubor vytvoří přes `ConfigStore::ensureDefaultAtStartup()` a sdílené `createDefault()` v atomickém režimu `createIfMissing`. Existující bajty včetně starého, vadného, nečitelného, nadlimitního či chybně kódovaného souboru nezmění. Souběžně vzniklý cíl nepřepíše. Chyba nezpůsobí automatický retry, IPC a diagnostika zůstanou dostupné, nevyřešená bezpečná inicializace blokuje publikaci i Connect. Connect nic nevytváří. Startup nevolá tray IPC, HTTP ani Connect. Wizard zapisuje až při Finish, Edit Config může výslovně potvrzeným Yes obnovit soubor smazaný za běhu. Aktivní VPN pokračuje na svém snímku, Disconnect disk nečte. |
| D05 | Opravitelný soubor pro Wizard je jinak platná konfigurace, které chybí pouze `virtualIp`, `accessKey` nebo obě pole. Ostatní neplatnosti vyžadují ve Wizardu Reset. Edit Config otevře i syntakticky chybný JSON, pouze pokud jde o platný jednoznačně dekódovatelný UTF-8 text v mezích 5.1. Nečitelnost, nepodporované či poškozené kódování a překročení velikosti jsou odlišné chyby. Jediná vstupní normalizace je odstranění jednoho volitelného UTF-8 BOM na úplném začátku. |
| D06 | Reset Wizardu zahodí celý koncept a přejde na nové výchozí nastavení, včetně TLS, CIDR, limitů a původních kontraktů. Disk se nemění. Zastaralá uložená revize neblokuje pozdější Finish. Reset editoru má vlastní pravidla D24. |
| D07 | Edit/repair patchuje čtyři konkrétní hodnoty a tři volby InWizard. Ostatní hodnoty a každý původní kontrakt zachová, při potvrzené volbě doplní pouze chybějící výchozí peery. |
| D08 | Na nultý krok se nelze vracet. Server URL nikdy nemá Back, ani po dokončeném odpojení. |
| D09 | `wizard-check-access` využije obecnou jednorázovou diagnostickou kontrolu přístupu podle 7.3 a normativního návrhu v kapitole 11. Provede nejvýše jeden prázdný data request, úplně ověří HTTP/NDJSON/korelaci/limity/nulový výsledek/data-mac a všechny datové záznamy zahodí bez payload GCM, dešifrování nebo jakéhokoli použití. Předem upozorní na možné nenávratné odebrání čekajících zpráv. Výjimka neplatí pro normální bootstrap, recovery ani provozní data polling. |
| D10 | Cizí Connect průvodce pozastaví. Uživatel dostane informaci, že někdo provedl Connect. Dosavadní síťové kontroly se zneplatní a nabídne se nové výslovné globální odpojení. Connect není blokován po celou dobu otevřeného průvodce. |
| D11 | Sdílené `defaultContractCandidates()` a `missingDefaultContractPeers()` vybírají z `.2` až `.15` v podporovaném CIDR, vynechají vlastní IP a všechny původní peery. Kandidátů bez self je 13 nebo 14, počet nových položek je pouze chybějící rozdíl. Každý původní kontrakt a jeho přesný payload klíč zůstává i při změně vlastní IP a mimo výchozí rozsah. Doplnění úplné sady je obsahová no-op. Nový create/reset koncept původní kontrakty nepřebírá, při false má prázdný objekt. Nové payload klíče jsou SHA-256 přes ASCII `minNumericIp_maxNumericIp` s číselným pořadím, jedním podtržítkem a bez LF, výstup lowercase hex. Náhled i publikace vycházejí ze stejného původního session DOM, neslučují pozdější cizí Save. Poslední úspěšná úplná publikace vyhrává. |
| D12 | Mimo podporované CIDR zůstane checkbox viditelný, zaškrtnutý a zakázaný s vysvětlením. Efektivní požadavek je false a původní kontrakty se zachovají. |
| D13 | Změna vlastní IP na adresu existujícího kontraktu se odmítne. Hláška vysvětlí konflikt a navrhne jinou vlastní IP nebo opravu kontraktu přes Edit Config před novým průchodem. Nic se automaticky neodstraní. |
| D14 | Chyby průvodce mohou zobrazit URL, pevnou cestu ke konfiguraci, HTTP status a konkrétní příčinu. Nevypisují klíče, celé requesty, ciphertexty ani autentizační tagy. |
| D15 | Serverová výchozí mapa `ACCESS_KEYS` se rozšíří z `.2–.4` na `10.99.99.2–.15`. Každý klíč je hexadecimální poslední oktet opakovaný 32krát. |
| D16 | Nový default je přesně `CHANGE_THIS_SERVER_SECRET`. Pouze nulová délka se normalizuje na tento default. Každý neprázdný secret, včetně 32 nebo 64 ASCII nul, zůstane doslova zachován. |
| D17 | Nezavádí se automatická migrace ani zkoušení historických secretů. Nové příkazy rozšíří IPC v1, původních sedm příkazů se zachová. Wizard potřebuje novou službu. |
| D18 | Pokud soubor zmizí nebo se poškodí za běhu VPN, aktivní spojení pokračuje s již načteným snímkem. Disconnect zůstane dostupné podle paměťového stavu VPN a nečte soubor. Connect při neaktivní VPN, Show Info, Statistics a Contracts zůstávají kliknutelné, jejich provedení při neplatném aktuálním snímku služba odmítne až po zvolení akce s konkrétním vysvětlením podle 5.4. |
| D19 | Bezpečné rušení HTTP je společné pro probe přerušený Connectem, globální Disconnect, stop služby i Cancel/Reset/Escape/X Wizardu. Má krátkou konečnou dobu a potvrzený terminální stav před nahrazením requestu nebo uvolněním jeho prostředků. |
| D20 | Každý HTTP request má jediný absolutní monotónní deadline zahrnující DNS, spojení, TLS, odeslání, hlavičky i celé tělo. Dílčí přenos jej neobnovuje. Timeout využije bezpečné rušení D19. |
| D21 | `get-info` zůstane mimo hlavní VPN síťové vlákno, s vlastním dočasným transportem a kontextem. Nejvýše jedna diagnostika běží současně. Wizard může sdílet tuto oddělenou řídicí cestu. Tři VPN pracovní role se nemění. Tray musí mít pro Disconnect cestu nezávislou na čekajícím Show Info či jiné dlouhé diagnostice. Obsazený společný CommandReader nesmí kliknutí tiše zahodit. Akce předá požadavek službě a sleduje dokončení, nebo zobrazí konkrétní chybu. |
| D22 | Poslední úspěšné uložení vyhrává mezi všemi editory a Wizard relacemi v obou směrech. Save/Finish ukládá celý validovaný dokument, bez automatického slučování, revizního odmítnutí nebo potvrzení přepsání mezitím uloženého obsahu. |
| D23 | Jeden Next spustí jeden logický síťový pokus. Tray ani služba automaticky neopakují start ani kontrolu, ani po 401 nebo ztrátě IPC odpovědi. Úspěch je platný pro UI až po doručení a ověření aktuálního výsledku. Nový pokus vyžaduje další Next. Pravidla idempotence Finish se nemění. |
| D24 | Edit Config načte skutečný UTF-8 text v mezích 5.1 bez ohledu na platnost JSON. Jiné kódování a neplatné UTF-8 odmítne konkrétní chybou bez náhrady znaků, odhadu kódování nebo base64 fallbacku. Vytvoření chybějícího souboru po Ano zapisuje ihned. Reset editoru po Ano mění pouze koncept a ukládá se až přes Save. Obě cesty používají jedinou platnou šablonu podle 5.6. |
| D25 | IPC přijímá i odesílá asynchronně, odděluje dlouhou diagnostiku od krátkého řízení a má omezená spojení, paměť i fronty a absolutní I/O deadline. Stop služby má vlastní cestu nezávislou na pipe. Konkrétní limity a cíle odezvy určí implementační návrh. |
| D26 | Wizard relace používá obnovovaný monotónní pronájem, výchozí potvrzení každých 15 s a expirace po 60 s bez kontaktu. Expirace bezpečně uklidí pouze danou relaci, neodpojí VPN a nezruší již provedený commit ani jeho potvrzení. |
| D27 | Před Access Key je **Use default access key** inicializované z `useDefaultAccessKeyInWizard` s výše uvedeným fallbackem pouze při absenci. Při true je pole zakázané a obsahuje `hex2(lastOctet)` opakované 32krát, například 32×`11` pro `.17`. Při false zůstává uložená hodnota editovatelná i při shodě s defaultem. Každá skutečná změna IP při true klíč přepočítá, neplatná IP včetně adresy mimo CIDR jej vyprázdní. Před IP polem je **Automatically detect IP address** podle `autoDetectIpAddressInWizard`. Při true je IP zakázaná a čeká na ověřený návrh z poslední úspěšné kontroly secretu. Návrh nahradí i neprázdnou placeholder IP, při false zůstane ruční IP zachovaná. Bez návrhu Next identity nepokračuje, UI vysvětlí možnost vypnout automatiku a zadat IP nebo se vrátit ke kontrole secretu. Zapnutí použije pouze stále platný návrh, samo nespouští HTTP. Změna URL/secretu, Reset, Connect, expirace či resume návrh a příslušné důkazy zneplatní. Back/Next zachová volby, resume zachová rozpracované hodnoty a všechny volby, ale vyžádá nové kontroly. |
| D28 | Různé klientské počítače smějí používat stejnou virtuální IP. phpvpn jedinečnost klientských IP nevynucuje ani nerezervuje. Kolize je pro systém přípustná, ale jde o odpovědnost a chybu konfigurace provozovatele. To nemění zákaz, aby vlastní IP současně byla cílem vlastního `communicationContract`. |

Poslední rozhodnutí mají přednost i před historickými požadavky na revizně podmíněný Finish, sdílení VPN síťového vlákna s diagnostikou, automatické obnovení síťového pokusu, předběžné zakazování menu podle konfigurace, zákaz otevření nevalidního JSON v editoru a automatické zachování existujícího či ručně zadaného Access Key při aktivním výchozím režimu. Editor nadále vyžaduje platné UTF-8 a stanovený limit.

## 3. Zjištěný výchozí stav a dopady

| Komponenta | Zjištění ve zdrojovém kódu | Potřebný zásah |
|---|---|---|
| `client/apps/service/service_host.cpp` | `ServiceRuntime::start` volá `ensureDefaultConfigFile`. | Odstranit automatické vytvoření, zachovat vytvoření chráněného adresáře a běh služby bez konfigurace. |
| `client/src/platform/windows/paths_win.*` | Obsahuje atomický create defaultu a adresář `PHPVPN`. | Odstranit odpovědnost za default, zachovat autoritativní vyhodnocení cesty. Velikost písmen existujícího adresáře nemigrovat. |
| `client/src/core/config/config.h` | Celý default JSON obsahuje starou placeholder URL a dokonce 64 ASCII nul v secretu. | Nahradit společnými konstantami, továrnou na Wizard koncept a jedinou platnou šablonou pro vytvoření/Reset editoru. 64 nul není totéž jako starý protokolový default 32 nul. |
| `config_validator.cpp` | Úplný validátor správně odmítá chybějící IP/klíč, neznámá pole a kontrakt na vlastní IP. | Zachovat striktní běhovou validaci, přidat oddělenou analýzu opravitelnosti. |
| `vpn_engine.cpp` | `storedConfigJson` vrací cache. `loadStoredConfig` při chybě nemaže starou cache. `setConfigJson` zapisuje sdílené `.tmp` před uzamčením cache. | Centralizovat disk, dostupnost, revize, serializaci všech zápisů a publikaci cache do ConfigStore. |
| `vpn_engine.cpp` | Connect někdy čte soubor pouze při prázdné cache. Bootstrap pak přebírá uložený snímek. | Každý nový logický Connect musí přijmout aktuální plně validní diskový snímek. Aktivní snímek a recovery stejného spojení se nepřepínají externí editací. |
| `ipc_protocol.*`, `ipc_dispatcher.*` | Uzavřených sedm příkazů, IPC v1, 4MiB limit zprávy. | Přidat konkrétní schémata Wizard příkazů a jejich parsery. Nezavádět obecný HTTP ani souborový příkaz. |
| `ipc_pipe.cpp` | Server obsluhuje jedno spojení po druhém. `get-info` provádí synchronní síť uvnitř handleru. Zápis odpovědi a flush mohou čekat neomezeně. | Oddělit dlouhé operace, zajistit omezenou souběžnou obsluhu a přerušitelné I/O. |
| `transport_winhttp.cpp` | Synchronní WinHTTP ruší zavřením handle z jiného vlákna. Přes komentář o deadline používá jen dílčí timeouty, nekontroluje převod sekund ani výsledek `WinHttpSetTimeouts`. | Společná oprava životnosti a rušení pomocí asynchronního WinHTTP a absolutního monotónního deadline podle 4.4. |
| `VpnEngine::getInfo` | Vytváří vlastní dočasný transport a NetworkWorker mimo hlavní VPN síťové vlákno. | Zachovat izolaci, omezit diagnostiku na jednu operaci a přesunout její čekání mimo kapacitu krátkých IPC příkazů. |
| `network_worker.cpp` | `configure` vyžaduje plnou konfiguraci a odvozuje také klíč pro data. | Pro neúplný koncept zavést specializovaný ProbeClient bez umělých platných IP/klíčů. |
| `NetworkWorker::performHealth` | Po vytvoření časového snímku staví `auth-semi` pro health. | Při společném refaktoringu opravit na vždy čerstvý `auth-none`, který vyžaduje protokol. Zabránit přenosu této chyby do opakovaných kontrol průvodce. |
| `response_parser.cpp` | Health spojuje syntaktickou a kryptografickou validaci. Info dosud neparsuje `clients` do typovaných hodnot. | Rozdělit health parse/verify, doplnit důslednou validaci `clients`. |
| `NetworkWorker::verifyAccessKey` | Běžný bootstrap zpracovává příchozí payload do front. | V průvodci ho nepoužít. Nová cesta skončí po ověření odpovědi a zahození zpráv. |
| `client/apps/tray/main.cpp` | Dnešní menu nemá požadované pořadí ani nová okna. Show Info a Disconnect používají společný CommandReader, obsazený startCommand další požadavek neodešle. | Zavést čistý menu model s kontrolou konfigurace až po akci, Wizard, placeholder a nezávislé odesílání Disconnectu s viditelnou chybou při selhání. |
| `status_reader.cpp` | Asynchronní čtečky mají lokální timeouty a společný CommandReader obsluhuje dlouhou diagnostiku i krátké řízení. | Oddělit diagnostickou čtečku od řídicí cesty Disconnectu, zachovat jeho odeslání a potvrzení dokončení během čekající diagnostiky. Wizard používá krátké start/get-result příkazy. Dosavadní 30s timeout Show Info se nemění. |
| `phpvpn.php` | Default, dvě normalizační cesty a dvě kontroly veřejného varování. ACCESS_KEYS jen `.2–.4`. | Aktualizovat všechny cesty konzistentně. |
| `tools/deploy-test.ps1` | Nahrazuje přesný starý řetězec secretu a přesný třípoložkový blok ACCESS_KEYS. | Upravit současně se serverem, jinak integrační deployment selže nebo použije jiný secret. |
| `tests-integration/phpvpn_client.py` | Vlastní normalizace na 32 nul. Testovací payload klíče používají jiný záměrný algoritmus. | Změnit normalizaci. Existující testovací payload helper plošně nenahrazovat algoritmem Wizardu. |
| CMake a testy | Vlastní micro harness, řízené transporty, reálný loopback WinHTTP, Debug/Release/ASan. Volitelný contract target odkazuje na neexistující zdroj. | Navázat na stávající testy, doplnit skutečné zdroje kontraktního targetu. Neoznačovat neprovedené ruční testy za ověřené. |

Historické plány nejsou důkazem současné implementace. Například v nich zmíněný crash-safe zápis a neblokující IPC dnes nejsou plně realizované. Tyto nedostatky jsou přímo relevantní k nové funkci a jejich oprava patří do této etapy.

## 4. Architektura a vlastnictví

### 4.1 Nové komponenty

| Nový soubor nebo dvojice `.h/.cpp` | Odpovědnost |
|---|---|
| `client/src/core/config/config_document.*` | Analýza platného/opravitelného dokumentu, zachování DOM a změna pouze povolených hodnot. |
| `client/src/core/config/config_defaults.*` | Nový koncept, sdílené defaulty a politika generování kontraktů. |
| `client/src/core/config/config_store.*` | Jedno autoritativní omezené čtení pro zvolenou akci, striktní UTF-8 podle 5.1, revize pro snímky a receipts, serializované úplné zápisy podle D22 a create-if-missing editoru. |
| `client/src/platform/iface/config_files.h` | Testovatelné rozhraní čtení, atomické publikace a vytvoření bez přepsání existujícího cíle, bez Win32 typů. |
| `client/src/platform/windows/config_files_win.*` | Windows souborové operace, ACL, identita souboru, flush, publish a obnova po chybě zápisu. |
| `client/src/core/wizard/wizard_types.h` | Koncept, pole, typované výsledky, identifikátory a stavy. |
| `client/src/core/wizard/wizard_model.*` | Přenositelné UI přechody, validace polí, invalidace kroků a výsledné prezentační instrukce. |
| `client/src/core/wizard/wizard_defaults.*` | Výběr navržené IP, přesné odvození výchozího Access Key a číselné řazení IP pro SHA-256 kontraktů. |
| `client/src/core/wizard/wizard_service.*` | Omezená správa relací a jejich pronájmů, vazby důkazů na vstupy, generaci konceptu a epochu spojení, samostatná životnost commit receipts. |
| `client/src/core/wizard/probe_client.*` | Jednorázové health/info/data operace nad dočasnými parametry a vlastním TimeSync. |
| `client/src/core/ipc/wizard_ipc.*` | Striktní stavitelé a parsery nových schémat, obálky odpovědí a oddělená diagnostika Wizardu. |
| `client/apps/tray/menu_model.*` | Pořadí a kliknutelnost položek bez předběžné kontroly souboru, Connect/Disconnect podle paměťového stavu VPN a reakce na výsledek konkrétní akce bez Win32 závislosti. |
| `client/apps/tray/wizard_window.*` | Win32 prezentace, vazba ovládacích prvků na čistý model, fokus, zprávy a životnost okna. |
| `client/apps/tray/wizard_reader.*` | Asynchronní IPC adaptéry pro Wizard, výsledky korelované s generací okna a operace. |
| `client/apps/tray/wizard_layout.*` | Čistá geometrie průvodce, parametrizovaná DPI, plochou a metrikami textu. |
| `client/apps/tray/contracts_window.*` | Jediné zavíratelné placeholder okno dané tray instance. |

Existující `layout.*`, `window_utils.h`, `status_reader.*`, `resource.h`, `main.cpp`, build seznamy a základní chybové kódy se rozšíří. Přesun společných pomocných funkcí z tray je vhodný jen tam, kde je skutečně použijí nové komponenty. Tato etapa nemá plošně přestavovat celý klient.

V `status_reader.*` oddělit vlastníky a asynchronní IPC spojení diagnostické a krátké řídicí cesty. Čekající Show Info ani jiná dlouhá diagnostika nesmějí obsadit čtečku, frontu nebo příznak busy potřebný pro odeslání Disconnectu. Řídicí cesta musí jeho výsledek zpracovat nezávisle na diagnostickém callbacku.

### 4.2 Hranice dat

- ConfigStore vlastní uložený dokument a jeho revizi. Aktivní VPN vlastní svůj neměnný `ClientConfigPtr`, přijatý při novém logickém Connect.
- WizardService vlastní originální dokument, neměnný kontext konceptu a síťové důkazy. Uložená revize slouží pro čerstvost, není podmínkou Finish. Dočasné koncepty ani ověřené info se nepersistují.
- WizardModel vlastní právě zobrazený krok, lokálně editované texty, `useDefaultAccessKey` a stav prezentace. Přepočet výchozího klíče je čistý a synchronní přechod modelu spuštěný každou změnou IP. Jeho lokální validace pomáhá uživateli, nikdy nenahrazuje validaci služby.
- ProbeClient nesmí mít referenci na `TrafficCounters`, fronty, `TunnelSession`, aktivní NetworkWorker ani publikované bootstrap kontrolní body.
- Služba vrací pouze potřebné typované výsledky. Celé HTTP requesty, ciphertext ani odvozené klíče neposílá do UI.
- CSPRNG vytváří UUID requestů, operací a relací. Tyto identifikátory slouží ke korelaci, nikoli jako nová autorizační hranice veřejného IPC.

### 4.3 Vlákna a plánování

Hlavní VPN síťové vlákno je vyhrazené pro bootstrap, recovery, časovou synchronizaci a datový provoz VPN. `get-info` zůstane mimo něj, s vlastním dočasným transportem a časovým kontextem jako dnes. Diagnostika nesmí sdílet proměnný TimeSync VPN, fronty ani provozní čítače. Původní přímý formát odpovědi get-info se zachová.

Wizard probe použije stejnou oddělenou omezenou řídicí síťovou obsluhu s vlastním ProbeClient. Zvolený návrh připouští nejvýše jednu dlouhou řídicí síťovou operaci celkem, tedy get-info nebo Wizard kontrolu. Obsazená kapacita vrátí `busy`, nevytváří neomezenou frontu. Na jednu Wizard relaci připadá nejvýše jedna rozpracovaná kontrola. IPC start kontroly rychle vrátí `operationId`, tray dále pouze čte její stav. Tunelová vlákna se kvůli diagnostice ani probe nespouštějí. Omezená obsluha řízení a platformní I/O callbacky nejsou čtvrtou VPN pracovní rolí.

Příchod nového Connect přeruší Wizard probe, bezpečně zruší jeho request a před bootstrapem počká na potvrzené ukončení práce. Nikdy nesmějí běžet dva data requesty současně, ani Wizard polling proti VPN bootstrapu/provozu. Samostatné diagnostické health/info smějí souběžně běžet s VPN datovým provozem. Čekání na ukončení podle D19 se vztahuje na rušený request před jeho nahrazením nebo uvolněním, nezavádí globální zákaz souběhu těchto izolovaných cest. `stateMutex_` ani zámek ConfigStore nesmějí být drženy při HTTP nebo kryptografii.

IPC příjem, čtení neúplné zprávy a odesílání odpovědi implementovat asynchronně. Teprve po úplném načtení a validaci se příkaz předá odpovídající obsluze. Čekání na další bajty nebo na odběr odpovědi nesmí držet pracovníka vykonávajícího krátké řízení. Pro get-status a Disconnect zajistit kapacitu nezávislou na dlouhé diagnostice. Přijetí Disconnectu jen předá požadavek VPN části služby. Dokončení lze potvrdit až po teardown, Wizard nadále čeká na skutečné `disconnected` a `idle`. Datové pakety se přes řídicí IPC nepřenášejí.

Toto službové oddělení již návrh řeší. Povinné doplnění v tray podle D21 zajistí skutečné doručení Disconnectu i během čekajícího Show Info. Tray nesmí čekat na konec diagnostiky ani požadavek tiše vynechat kvůli obsazenému CommandReaderu. Omezená souborová obsluha akcí nesmí držet tuto řídicí kapacitu ani zámek paměťového stavu VPN během čtení, dekódování, hashování nebo parsování konfigurace.

Omezit počet přijatých spojení, velikosti zpráv, celkovou paměť a fronty práce. Čtení i zápis mají vlastní absolutní monotónní I/O deadline, který dílčí přenos neobnoví. Přebytečná spojení nebo práci řízeně odmítat. Samotné navýšení počtu synchronních workerů ani priorita dosud nerozpoznaného příkazu nestačí. Odstranit neomezený `FlushFileBuffers(pipe)` a garantovat bezpečnou životnost overlapped I/O až do potvrzeného dokončení či zrušení. Stop přes Windows Service Manager má vlastní cestu nezávislou na pipe a ukončí čekající IPC i HTTP operace.

Před implementací v bloku B01 stanovit konkrétní limity, podporovaný profil zatížení a měřitelné meze pro odezvu get-status, přijetí Disconnectu, dokončení teardown a stop služby. Při podporovaném zatížení nesmějí pomalí IPC klienti vyčerpat obsluhu řízení. Při přetížení zůstane spotřeba omezená a stop proveditelný. Nejde o absolutní dostupnost při libovolném zahlcení veřejné pipe. Počet obslužných vláken se odvodí od návrhu, není pevně určen číslem čtyři.

### 4.4 Společný HTTP transport: rušení a celkový deadline

Opravit rozhraní Transport i Windows implementaci jako společnou součást VPN, get-info a Wizard probe. Použít asynchronní WinHTTP s explicitním vlastníkem requestu, bufferů a callback kontextu. Rušení synchronního volání zavřením handle z jiného vlákna není přípustné. Návrat `WinHttpCloseHandle` sám neznamená, že skončily callbacky. Kontext držet do závěrečného `WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING` a dokončení všech jeho uživatelů. Callbacky nesmějí čekáním na sebe navzájem vytvořit deadlock. Podklad: [Microsoft: WinHttpCloseHandle](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpclosehandle).

Tentýž mechanismus musí pokrýt Connect během probe, globální Disconnect s aktivním provozním requestem, stop služby při restartu/aktualizaci/odinstalaci i Cancel, Reset, Escape a X Wizardu. Souběh dokončení, timeoutu a více rušicích událostí vybere právě jeden konečný výsledek a právě jedno zavření. Náhrada requestu, rekonfigurace transportu a uvolnění jeho objektů počkají na potvrzený terminální stav. Rušení má krátkou konečnou dobu podle měřitelné brány B01 a implementačního bloku B07, není slibována nulová prodleva. Zabránit použití zavřeného handle, dvojímu dokončení a dvojímu účtování přečtených bajtů.

Každý request dostane při zahájení jediný absolutní deadline z monotónních hodin a `responseTimeoutSeconds`, výchozí hodnota je 30 s. Jeden rozpočet zahrnuje DNS, spojení, TLS handshake, odeslání requestu, příjem hlaviček i celé tělo. Dílčí bloky ani přechod do další transportní fáze jej neposouvají. Dílčí WinHTTP timeouty jsou doplňkové, samy celkový deadline nezajišťují. Podklad: [Microsoft: WINHTTP_OPTION_RECEIVE_TIMEOUT](https://learn.microsoft.com/en-us/windows/win32/winhttp/option-flags#winhttp_option_receive_timeout).

Po vypršení deadline ihned zahájit bezpečné rušení, počkat na potvrzené ukončení a vrátit timeout, nikoli pozdní úspěch. Úklid nesmí dál spotřebovávat celý původní HTTP timeout. Zkontrolovat převody sekund, násobení i součet s monotónním časem a všechny návratové hodnoty nastavení timeoutů. Velké podporované hodnoty obsloužit bezpečným dělením platformních čekání, nebo nereprezentovatelnou hodnotu explicitně odmítnout před HTTP. Nikdy ji nepřetočit na záporný či nekonečný timeout ani tiše změnit význam konfigurace. Limity převodů zaznamenat v normativní dokumentaci.

Po timeoutu a cancel musí být transport bezpečně znovu použitelný. VPN nadále účtuje přesně jednou i částečné tělo a blok přes limit, před dokončeným Disconnectem účetnictví uzavře. Diagnostika a probe do VPN čítačů nevstupují. Oprava asynchronního transportu nemění tři sériové VPN role ani jejich ownership.

## 5. Konfigurace, dostupnost a revize

### 5.1 Typy a rozhraní

Zavést `ConfigAvailability = missing | unreadable | encoding_error | too_large | invalid | repairable | valid`. Nedostupná služba je samostatný UI stav, nesmí se vydávat za missing. `unreadable` znamená chybu přístupu nebo čtení, `encoding_error` načtené bajty s nepodporovaným či poškozeným kódováním a `too_large` překročený limit čtení. `invalid` a `repairable` se určují až po úspěšném omezeném načtení a ověření UTF-8.

`StoredConfigSnapshot` obsahuje dostupnost, bezpečný kód důvodu, neprůhledný `revisionToken`, interní identitu souboru a pro úplně načtený obsah SHA-256 přesných vstupních bajtů. Pro platný UTF-8 text v limitu uchová skutečný text pro editor i při nevalidním JSON, pro valid/repairable navíc DOM pro Wizard. Při odmítnutí čtení, velikosti či kódování neposkytne editorový text ani DOM, neúplný hash nesmí vydávat za revizi obsahu. Token slouží k identifikaci snímku a potvrzené publikace, nikoli k odmítání zastaralého Save/Finish. Nesmí se odvozovat pouze z mtime, velikosti nebo `ClientConfig::generation`.

Jednorázové čtení konfigurace má limit `maxConfigFileBytes = 512 KiB` (524288 bajtů), včetně případného BOM. Limit vynutit v souborovém backendu před alokací podle deklarované velikosti a průběžně při čtení. I při růstu souboru načíst nejvýše limit plus jeden detekční bajt, větší obsah odmítnout jako `config_too_large` bez jeho úplného načtení, hashování nebo parsování. Stejný limit použít pro výsledný dokument před Save/Finish/create-if-missing. Volba ponechává rezervu i pro nejhorší šestinásobný JSON escaping textu pod 4MiB limitem IPC, skutečnou zakódovanou obálku přesto kontrolovat samostatně. Pomalé čtení běží mimo kapacitu get-status, Disconnectu a stopu služby. Nevzniká periodický refresh, watcher ani cache vyžadující opakované procházení disku.

`config.json` musí být platný jednoznačně dekódovatelný UTF-8 text. Striktní dekodér odmítne neplatné pokračovací bajty, neúplné a overlong sekvence, surrogate kódové body a hodnoty mimo Unicode. UTF-16/UTF-32 včetně jejich BOM a jiné nepodporované kódování vrátí `config_encoding_error`. Žádné nahrazování znaků, odhadování kódování, Unicode normalizace ani base64 přenos vadných bajtů. Platné UTF-8 je jediná interpretace vstupu, původní kódování bajtově totožného textu se neodhaduje.

Jedinou vstupní normalizací je odstranění jednoho volitelného UTF-8 BOM `EF BB BF` na offsetu 0 před parsováním a předáním textu editoru. BOM jinde ani opakovaný BOM se jako značka kódování nepřijímá a neodstraňuje. U+FEFF uvnitř JSON stringu je znak hodnoty, nikoli BOM, a zachová se. Nezměněné ostatní znaky, whitespace a konce řádků se přenesou přesně. Save/Finish a šablona zapisují UTF-8 bez BOM. Samotné otevření nebo Cancel původní bajty na disku nemění. Striktní převody UTF-8/UTF-16 v tray nesmějí ztrácet znaky ani neplatné sekvence opravovat.

Navržená interní rozhraní:

```cpp
Result<StoredConfigSnapshot> ConfigStore::refresh();
StoredConfigSummary ConfigStore::summary() const;
Result<WizardDocument> analyzeWizardDocument(std::string_view json);
Result<std::string> buildWizardDocument(
    const WizardDocument& original,
    WizardMode mode,
    const WizardFields& fields,
    bool createDefaultContracts);
Result<CommitReceipt> ConfigStore::commit(
    std::string_view completeValidatedJson,
    const CommitId& commitId);
Result<EditorDocument> ConfigStore::readForEditing();
Result<EditorDocument> ConfigStore::createDefaultIfMissing();
std::string makeDefaultEditorDocument();
```

`CommitReceipt` obsahuje `commitId`, novou revizi a stav confirmed. Souborový backend nesmí při neurčitém výsledku operačního systému tvrdit ani úspěch, ani bezpečné nezapsání bez ověření skutečného stavu.

### 5.2 Opravitelný dokument

Neoslabovat `parseAndValidateConfig`. Samostatná analýza musí prokázat, že všechny nalezené chyby jsou výhradně absence obou povolených polí. Nestačí zachytit první chybu validátoru a prohlásit zbytek za platný.

Prakticky vyčlenit sdílenou validaci struktury, známých členů, URL, secretu, TLS, CIDR, runtime limitů a kontraktů. V režimu analýzy lze vynechat kontrolu chybějící IP/klíče a vazbu kontraktů na dosud neznámou vlastní IP. Tato vazba se povinně ověří při doplnění IP a znovu při Finish. Typované neúplné údaje nesmějí vznikat jako falešně validní `ClientConfig` s dummy klíčem.

`masterSetup` přijímá tři volitelné striktní JSON booleany: `useDefaultServerSecretInWizard`, `autoDetectIpAddressInWizard` a `useDefaultAccessKeyInWizard`. Úplná šablona editoru i nový/resetovaný koncept Wizardu používají true. Při absenci se secret odvodí přesnou shodou s `CHANGE_THIS_SERVER_SECRET`, automatická IP jako false při platné uložené IP a true při chybějící IP, výchozí klíč jako false při platném uloženém klíči a true při jeho absenci. Každý příznak se odvozuje samostatně. Explicitní false má přednost i v repair bez konkrétní hodnoty, pole pak zůstane prázdné a editovatelné. Jiný typ, duplicita nebo neznámý člen je chyba. Metadata mění pouze Wizard, běžný runtime používá výhradně uložené konkrétní hodnoty secretu, IP a klíče.

### 5.3 Zachování hodnot a Reset

V režimu edit/repair patchovat čtyři konkrétní hodnoty a tři příznaky `InWizard` v původním DOM a při efektivní true aditivně doplnit chybějící kontrakty. Dokument nerekonstruovat z `ClientConfig`. Hodnoty, typy a přítomnost ostatních členů zůstávají zachované, změna whitespace a formátování JSON je povolená.

Reset Wizardu vytvoří nový koncept s URL `http://MODIFY-THIS-URL/phpvpn.php`, secretem `CHANGE_THIS_SERVER_SECRET`, `trustAnyTlsCertificate=true`, `vpnCidr=10.99.99.0/24`, současnými runtime defaulty, třemi true příznaky InWizard a nově rozhodnutou sadou kontraktů. Runtime lze v JSON vynechat a využít sdílené defaulty. Původní obsah se po Resetu nepoužije pro slučování ani jako podmínka zápisu. Odlišný úplný default editoru je definován v 5.6.

Při unreadable se může po Resetu vyplňovat nový koncept, ale Finish nesmí obejít souborovou chybu přístupu. Pokud služba při publikaci stále nemůže bezpečně pracovat s aktuálním cílem a obnovou po chybě, zápis odmítne a navede na opravu přístupu. Nevyžaduje shodu s revizí při otevření. Reset neopravuje ACL ani nepřesměrovává cestu.

### 5.4 Jednorázová kontrola po akci a reakce menu

Start tray, jeho pravidelný timer ani pouhé otevření menu nenačítají konfiguraci. Pětisekundový tray timer dál čte pouze paměťový `get-status`. Žádný pravidelný pětisekundový `get-config-state`, předběžná menu kontrola, přímé `exists` na ProgramData ani automatický diskový refresh po Save/Finish. Menu nezakazuje konfiguračně závislé položky podle starého nebo neznámého stavu souboru. Connect/Disconnect se přepíná podle paměťového stavu VPN.

Po zvolení konkrétní akce služba jednou načte aktuální soubor v mezích 5.1 a tento snímek použije pro kontrolu i provedení akce. Connect, Show Info, Statistics a Contracts vyžadují validní snímek, jinak zobrazí konkrétní chybu nebo návod až po kliknutí. Connect a get-info mají kontrolu přímo v obsluze příkazu, Edit Config v `get-config-document` a Wizard v `wizard-open`. Pro zvolené Statistics nebo Contracts lze použít jednorázový `get-config-state`, jehož výsledek přímo rozhodne o otevření okna, bez druhého čtení konfigurace. Samotné otevření menu neposílá žádný z těchto příkazů. Další samostatná akce později načte nový snímek, žádná uložená revize neblokuje Save/Finish.

Editor přijme syntakticky chybný JSON jen jako platný UTF-8 text. `get-config` a get-info nesmějí po zjištění neplatného disku vracet nebo používat starou uloženou cache. Aktivní spojení si svůj již přijatý snímek ponechá. Disconnect nečte konfiguraci vůbec a použije nezávislou řídicí cestu tray podle D21.

Všechny konfiguračně závislé položky zůstávají kliknutelné. Tabulka popisuje výsledek obsluhy po kliknutí, nikoli předběžné zakazování položek podle souboru:

| Stav zjištěný při akci | Connect při neaktivní VPN | Disconnect při aktivní VPN | Wizard | Edit Config | Contracts | Show Info / Statistics |
|---|---|---|---|---|---|---|
| valid | provést Connect | odeslat bez čtení souboru | edit | skutečný UTF-8 text | běžné okno | provést akci |
| repairable / UTF-8 invalid | chyba a návod k opravě | odeslat bez čtení souboru | repair nebo nabídka Reset | skutečný UTF-8 text včetně chybného JSON | vysvětlení bez okna | chyba a návod k opravě |
| unreadable | chyba přístupu | odeslat bez čtení souboru | chyba a nabídka Reset podle 5.3 | chyba přístupu, bez náhrady | vysvětlení bez okna | chyba přístupu |
| encoding_error | chyba kódování | odeslat bez čtení souboru | chyba a nabídka Reset | chyba kódování, bez dokumentu | vysvětlení bez okna | chyba kódování |
| too_large | chyba velikosti | odeslat bez čtení souboru | chyba a nabídka Reset | chyba velikosti, bez dokumentu | vysvětlení bez okna | chyba velikosti |
| missing | návod k vytvoření, bez Connect | odeslat bez čtení souboru | create | Ano/Ne k vytvoření, po Ano zápis a editor | varování bez okna | návod k vytvoření |
| služba nedostupná | konkrétní chyba služby/IPC | pokus o odeslání, při selhání viditelná chyba | vysvětlení, bez kontrol a Finish | vysvětlení nedostupnosti | vysvětlení nedostupnosti | vysvětlení nedostupnosti |

Dosud neznámá konfigurace není missing, vyhodnotí se až v akci. Exit zůstává dostupné vždy. Otevřené Statistics dále používají paměťový get-status, změnu souboru na pozadí nesledují ani kvůli ní nevynulují cache. Nové zvolení Statistics opět provede jednorázovou kontrolu. Save úplně validuje odeslaný dokument včetně UTF-8 a velikosti a bezpečnost publikace, nikoli obsahovou platnost nebo revizní shodu dosavadního souboru. Smí tak opravit nevalidní obsah i uložit starší koncept podle D22.

### 5.5 Atomický zápis: poslední úspěšné uložení vyhrává

Všechny zápisy služby, včetně startup vytvoření, set-config, Finish a potvrzeného vytvoření výchozího souboru z editoru, procházejí jediným ConfigStore a stejnou serializační branou. Mezi Save a Finish rozhoduje pořadí úspěšné publikace celých dokumentů. Pozdější Save starého editoru smí přepsat Finish a pozdější Finish otevřeného Wizardu smí přepsat Save, stejně tak dvě relace stejného typu. Stará revize sama nezpůsobí konflikt, nové otevření, potvrzení přepsání ani automatické slučování. Cizí uložení nezneplatňuje koncept a jeho důkazy jen kvůli změně uložené revize. Wizard sestaví celý dokument ze svého originálu a konceptu podle edit/repair/reset, nikoli z mezitím uloženého cizího dokumentu.

Postup Finish:

1. Ověřit živou relaci, shodu instance služby, connection epoch, síťové důkazy a přesných sedm fields. Sestavit výsledný dokument a spustit úplný validátor ještě před vytvořením temp souboru.
2. Vstoupit do serializační brány zápisu. Zjistit aktuální existenci a bezpečnou identitu cíle a adresáře pro publikaci a obnovu po chybě. Nevyžadovat obsah, existenci ani revizi shodnou s otevřením okna. Save/Finish mohou uložit i po ruční editaci či běžném delete/recreate cíle, pokud je aktuální publikační operace bezpečná.
3. Nový soubor i dočasné soubory smějí vzniknout jen v autoritativním chráněném adresáři při schválené ukládací operaci: Finish, validovaný set-config/Save nebo potvrzené create-if-missing editoru. Dočasný název obsahuje náhodné ID, vzniká přes CREATE_NEW, nikdy sdíleným `config.json.tmp` s truncation. Zakázat reparse přesměrování cíle a neočekávanou identitu adresáře. Nastavit ochranu SYSTEM/Administrators dříve, než se zapíší tajné bajty.
4. Zapsat celé tělo, ověřit počet bajtů a provést `FlushFileBuffers` nad skutečným file handle. Samotné `ostream::flush` nestačí pro tento účel. Viz [Microsoft: FlushFileBuffers](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers).
5. Bez síťové práce ověřit aktuální publikační podmínky. Save/Finish nemají create-only podmínku odvozenou od otevření okna. Zvolit atomickou publikaci pro aktuální stav cíle a bezpečně vyřešit změnu jeho existence během publikace. Jen výslovné `createDefaultIfMissing` nesmí přepsat mezitím vzniklý soubor, v tom případě načte skutečný aktuální obsah nebo vrátí konkrétní chybu. Pro existující cíl použít platformní replace se zálohou v témže adresáři, s kontrolou ACL a skutečného výsledku. `REPLACEFILE_WRITE_THROUGH` nepoužívat jako záruku, protože není podporován. Viz [Microsoft: ReplaceFileW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew).
6. Úspěch publikace je commit point. Teprve potom publikovat novou uloženou generaci v paměti a receipt. Aktivní konfigurace se tím nemění. Po commitu se již nesmí vrátit běžné cancelled, jako by disk zůstal nezměněný.
7. Při chybě odstranit pouze vlastní temp soubor, nikdy cizí `config.json`. U částečných chyb replacement API ověřit polohu původního souboru a zálohy, případně bezpečně obnovit původní cíl. Neprovádět slepé mazání zálohy ani cizího nového souboru. Restart musí umět rozpoznat vlastní nedokončený commit z minimálního záznamu bez klíčů a nesmí automaticky dokončit dosud neprovedený Finish.

Atomická publikace a ochrana před přepsáním staršího obsahu jsou odlišné vlastnosti. Druhá není požadována, poslední uložení smí mezitím provedené obsahové změny nahradit. Zůstává povinná serializace podporovaných IPC zapisovatelů a bezpečnost souborových operací i při přímém zásahu administrátora během publikační sekce. Testy musí prokázat, že nedojde k truncation způsobené službou, chybnému ACL, smazání nesprávného cíle při rollbacku nebo falešnému tvrzení, že se nic nezapsalo. Nevyžadovat compare-and-swap proti původnímu obsahu ani oplock pouze pro zabránění schválenému přepsání. Podrobnosti technických bran jsou v B01, B03 a B04.

Zálohy mají stejná omezení přístupu jako konfigurace. Ponechat nejvýše poslední vlastní známou verzi, nevytvářet neomezenou historii secretů. Záloha slouží k bezpečné obnově po chybě, ne k reviznímu blokování Save/Finish. Výpadek napájení a selhání média mají vlastní recovery výsledek, nelze slibovat obnovu na fyzicky nefunkčním disku.

### 5.6 Edit Config: skutečný obsah, vytvoření a Reset

Nový `get-config-document` po zvolení Edit Config jednou načte pevnou cestu a vrátí skutečný UTF-8 text v mezích 5.1 i při chybějících povinných polích, jiné konfigurační chybě nebo syntakticky chybném JSON. Jediná normalizace je odstranění počátečního volitelného BOM. Nevalidní JSON nenahradí defaultem ani starou cache. Původní přímý `get-config` zůstane pro platný konfigurační JSON, nevalidní dokument zpřístupní jen nová textová obálka. Nečitelnost existujícího souboru je chyba přístupu. Neplatné UTF-8 či jiné kódování vyvolá konkrétní chybu kódování bez dokumentu a bez náhrady znaků, odhadu nebo base64 fallbacku. Nadlimitní soubor vyvolá chybu velikosti. Žádná z těchto chyb nespustí vytvoření náhrady.

Při skutečné neexistenci zobrazí Edit Config Ano/Ne se skutečně vyhodnocenou absolutní cestou. Ano pošle `create-default-config`, služba společnou šablonu úplně zvaliduje a atomicky vytvoří chybějící soubor podle 5.5. Editor otevře skutečný výsledek operace. Pokud cíl mezitím vznikl, nesmí být přepsán šablonou. Ne soubor nevytvoří a neotevře nový dokument. Cancel editoru po potvrzeném vytvoření již vzniklý soubor nemaže.

Tlačítko Reset v editoru vyžádá Ano/Ne k obnovení výchozí konfigurace. Ano nahradí pouze rozpracovaný text a označí jej jako změněný, Ne zachová dosavadní text. Disk se změní až po úspěšném Save se stejnou úplnou validací jako Connect. Cancel zahodí neuložené úpravy. Tato pravidla editoru nenahrazují Reset Wizardu.

Vytvoření i Reset editoru používají jedinou továrnu `makeDefaultEditorDocument` ve společném klientském kódu. Služba poskytne tutéž šablonu přes `get-default-config`, tray nedrží vlastní kopii JSON. Wizard sdílí konstanty a algoritmus odvození klíčů, ale zachová svůj placeholder URL a volitelné aditivní doplnění kandidátní sady 13/14 kontraktů. Úplná šablona editoru je:

```json
{
  "masterSetup": {
    "serverUrl": "http://127.0.0.1/phpvpn.php",
    "trustAnyTlsCertificate": true,
    "serverSecret": "CHANGE_THIS_SERVER_SECRET",
    "useDefaultServerSecretInWizard": true,
    "autoDetectIpAddressInWizard": true,
    "useDefaultAccessKeyInWizard": true,
    "vpnCidr": "10.99.99.0/24",
    "virtualIp": "10.99.99.2",
    "accessKey": "0202020202020202020202020202020202020202020202020202020202020202"
  },
  "communicationContracts": {
    "10.99.99.3": {
      "payloadKey": "918c488528c2bc41a90f000b0a63bab0ba418790058ea7b448a3760aec16349d"
    }
  }
}
```

Sekce runtime je vynechána a používá sdílené defaulty včetně 30s timeoutu. Šablona musí projít syntaktickou i úplnou konfigurační validací bez výjimek. Loopback URL označuje relay na stejném počítači, nikoli ověřenou dostupnost serveru. Úspěšné vytvoření ani Save nenahrazují síťové ověření při Connect. Předvídatelné hodnoty a jeden kontrakt odpovídají schválené šabloně z nálezu 6.

## 6. Přenosný model průvodce

### 6.1 Stavy

`WizardMode = create | edit | repair | reset`. Kroky jsou `disconnectGate`, `serverUrl`, `serverSecret`, `identity`, `contracts`. Vedle kroku model vede `loading`, `editing`, `checking`, `waitingDisconnect`, `interruptedByConnect`, `sessionExpired`, `committing`, `commitUnknown`, `completed` nebo `closed`. Zastaralá uložená revize nemá vlastní chybový stav.

Každý síťový krok má výsledek `pending | success | failed`. Výsledky jsou svázané s přesnými vstupy, ne s tím, že uživatel již jednou stiskl Next. První krok nemá Back. Sidebar je informativní, neslouží k přeskočení neověřeného kroku.

| Událost | Předpoklad a výsledek |
|---|---|
| otevření | načíst službu, konfiguraci, epochu a nový sessionId. Aktivní VPN vede na disconnectGate, jinak na serverUrl nebo chybu vyžadující Reset. Při prvním vstupu do identity kroku se existující `accessKey` načte jako vlastní hodnota s odškrtnutým checkboxem. Nový/resetovaný koncept nebo repair bez klíče začne v zaškrtnutém výchozím režimu. |
| Next na disconnectGate | text předem vysvětlí globální dopad. Odeslat disconnect, čekat na skutečný `disconnected` a `idle`, ne pouze na přijetí IPC. Pak krok označit zeleně a přejít na serverUrl. |
| Next serverUrl | lokální validace a check-url. Jen úspěch přejde na serverSecret. |
| Next serverSecret | check-secret s novým health a info. Jen úplný úspěch přejde na identity a případně jednou doplní chybějící návrhy. |
| Next identity | lokální validace, kontrola kolize kontraktu, upozornění na spotřebování zpráv a check-access. Jen úspěch přejde na contracts. |
| Back | contracts → identity → serverSecret → serverUrl. Zachovat koncept. Samotný Back nedělá síť ani zápis. Další Next opět ověří svůj krok. |
| změna URL | zneplatnit všechny síťové výsledky, ověřený čas, info a návrhy závislé na starém serveru. Ručně zadané hodnoty nezahodit. |
| změna secretu/default checkboxu | zneplatnit secret a access výsledky, čas a info. |
| změna IP/Access Key/default checkboxu | zneplatnit access výsledek, znovu spočítat kolizi s vlastním kontraktem a náhled počtu kontraktů. Je-li **Use default access key** zaškrtnutý, každá uživatelská i systémová změna IP současně okamžitě přegeneruje Access Key. Zaškrtnutí z vlastního režimu okamžitě přepíše textbox defaultem. Odškrtnutí zachová právě zobrazenou hodnotu a zpřístupní pole, nikdy neobnovuje dřívější vlastní hodnotu z `config.json`. |
| Reset | bezpečně zrušit vlastní kontrolu, po ukončení práce vytvořit nový koncept a vyčistit výsledky. Žádné diskové operace ani revizní podmínka pozdějšího Finish. |
| Cancel / Escape / X | mimo commit zrušit vlastní operaci a relaci, zavřít okno, vymazat koncept. Neodesílat Connect ani dodatečný Disconnect. |
| Finish | úplná službová validace a atomický zápis podle D22, bez dalšího data pollingu. Během krátkého commitu nelze předstírat úspěšný Cancel. |
| úspěšný Finish | receipt confirmed, zavřít Wizard a otevřít placeholder jednou podle potvrzené publikace, bez dodatečného get-config-state nebo diskové kontroly. |
| chyba Finish | zůstat na contracts, zachovat koncept a zobrazit konkrétní nápravu. Samotná zastaralá revize není chybou a nevyvolá potvrzení přepsání. |
| ztracená odpověď Finish | commitUnknown, ověřit receipt, slepě neopakovat zápis ani netvrdit, že soubor nebyl změněn. |
| expirace pronájmu | sessionExpired, zastavit kontroly i Finish, oznámit zánik relace a nabídnout nové otevření. Neobnovovat starou relaci opožděným potvrzením, výsledek rozpracovaného commitu ověřit samostatně. |

Každý Next založí jeden aktuální pokus. Zelený výsledek a postup jsou dovoleny až po doručení a ověření úspěchu tohoto pokusu v tray. Chyba kdekoli v řetězci tray → služba → server → služba → tray pokus ukončí, zobrazí červenou hlášku na stejném kroku a zachová texty. Nedodání v konečném časovém limitu je timeout, ostatní chyby mají skutečnou příčinu. Nepoužívat dřívější úspěch, automaticky neopakovat start ani žádnou část kontroly, včetně health/info a retry po 401. Další pokus vznikne výhradně novým Next a může znovu spotřebovat zprávy, i když předchozí na serveru uspěl.

Po chybě nebo timeoutu tray označí pokus lokálně za uzavřený a pozdní úspěch ignoruje. Pokud práce ve službě ještě běží, vyžádá její zrušení, je-li operace známa. Při ztraceném start acknowledgement použije korelaci aktuálního pokusu a rušení podle 8.2, start znovu neodesílá. Další Next může začít až po potvrzeném ukončení předchozí práce, nebo obdrží busy. Průběžné čtení dosud běžící operace není nový pokus. Reset a Cancel jsou dostupné i při chybě načtení, kromě krátké nerozhodnuté commit sekce.

### 6.2 Cizí Connect a epochy

Služba zavede interní nezáporný 64bitový `connectionEpoch`, zvýšený při přijetí každého nového logického Connect. Idempotentní Connect v aktivním stavu epochu nezvyšuje. Epochu resetuje pouze nová instance služby, která má nové `serviceInstanceId`. Při vyčerpání číselného rozsahu fail-closed, nikoli přetečení.

Wizard zaznamená epochu při otevření a po potvrzeném odpojení. Každá kontrola a commit kontrolují shodu. Tím se zachytí i Connect a Disconnect dokončené mezi dvěma UI pollingy. Událost nesmí být odvozena pouze z aktuálního názvu stavu.

Text informace: `Another application or user started a VPN connection while this wizard was open. Previous checks are no longer valid. To continue, choose Next to disconnect the VPN for everyone.` Neuvádět vymyšleného uživatele ani proces, veřejné IPC identitu nesleduje.

Při přerušení zachovat vyplněné texty, ukončit vlastní probe, označit síťové výsledky pending a nabídnout odpojovací bránu. Pokud je v době obsluhy již opět disconnected, stále zobrazit informaci o proběhlém Connect a vyžadovat opakování kontrol od serverUrl, ale další zbytečný Disconnect není nutný. Po výslovném odpojení a převzetí nové epochy provést všechny kontroly znovu až jednotlivými ručními Next. Originální dokument konceptu se cizím uložením nepřepíná a jeho revize Finish neblokuje.

Guard commitu a přijetí Connect mají jednoznačné pořadí. Connect přijatý před commitem zneplatní uložení. Connect přijatý až po commitu načte nově uloženou konfiguraci. Žádné z těchto pořadí nesmí vyžadovat blokování Connect po celou dobu průvodce.

## 7. Síťové kroky a pravidla dat

### 7.1 Server URL

Použít existující `parseServerUrl`: HTTP/HTTPS, bez userinfo, query a fragmentu. Zachovat podporu jiného názvu skriptu a cesty, protože stávající validátor i testy přijímají například `vpn.php` a `relay.php`. Nápověda doporučí úplnou URL phpvpn.php, nevytvoří nové povinné pravidlo přípony. Placeholder `MODIFY-THIS-URL` odmítnout v průvodci s konkrétní nápovědou před DNS.

TLS politika a limity pocházejí z zachovaného kontextu nebo nových defaultů. Nikdy nezměnit `trustAnyTlsCertificate=false` na true jen proto, aby kontrola prošla. Nepovolit redirect, proxy ani automatickou přenosovou autentizaci oproti dnešnímu transportu.

Probe odešle jeden nový `auth-none`. Rozdělit ResponseParser na `parseHealthSyntax` a `verifyHealth`, používané společně i běžným NetworkWorker. První krok ověří HTTP status, tvar odpovědi, status running, korelaci UUID, syntaxi času a kanoničnost/délky IV a tagu. Neověřuje GCM tag, nepřijímá serverový čas do TimeSync a neoznačuje server za kryptograficky ověřený.

### 7.2 SERVER_SECRET

Checkbox secretu se řídí `useDefaultServerSecretInWizard`, pouze při absenci přesnou shodou s `CHANGE_THIS_SERVER_SECRET`. True zobrazí tento default v zakázaném poli, false přesnou uloženou hodnotu v editovatelném poli. Zapnutí přepíše koncept defaultem, vypnutí ponechá zobrazený text. Každé přepnutí zneplatní secret/access důkazy a Finish uloží hodnotu i příznak.

Zachovat přesné UTF-8 bajty bez trimu, změny velikosti písmen nebo Unicode normalizace. Win32 převod nesmí potichu ukončit secret na NUL. Pro existující řídicí znaky použít výslovně označené zobrazení jako JSON string literal a striktní dekódování v tomto režimu, pro běžný text obyčejné pole. Zobrazení escape sekvencí nesmí zaměnit doslovné znaky `\n` se skutečným koncem řádku. Nezměněná hodnota se bere z původních bajtů modelu.

Next provede nový health, ověří GCM, sestaví samostatný TimeSnapshot podle normativního RTT a bezpečnostní rezervy, potom odešle info, ověří obálku a dešifruje. Doplnit `InfoResponse::clients` jako seznam `{Ipv4Address address, int64 lastSeenMs}`. Vyžadovat clients array, validní položky, kanonické adresy, unikátní adresy a platné milisekundové UTC lastSeen. Neznámá pole plaintextu info nadále ignorovat v souladu s protokolem. Statistický čas v budoucnosti je platná hodnota a může být výsledkem markeru, nesmí se automaticky zaměnit za chybějícího klienta.

### 7.3 IP a Access Key

IP musí být kanonická a `vpnCidr.isValidClientAddress`, tedy ne síť, první rezervovaná host adresa ani broadcast. Klíč musí být přesně 64 malých hex znaků, žádné automatické lowercasing nebo odstraňování whitespace.

Původní automatický návrh IP zůstává v rozsahu `10.99.99.2–.15`. Při opravitelném souboru jej omezit na platné klientské adresy zachovaného CIDR a vyloučit kolize s existujícími kontrakty. Pokud je množina prázdná, vyžádat ruční IP s vysvětlením zachovaného CIDR. Rozšíření pravidla generování kontraktů na jiné `10.A.B.0/24` samo nepřidává nový protokol pro automatickou alokaci IP.

Z ověřených clients vybrat nejnižší adresu kandidátní množiny, která chybí. Jsou-li všechny přítomné, zvolit nejstarší lastSeen, při shodě nižší IP. Poslední oktet se porovnává jako číslo. `clients` není autoritativní evidence volných adres ani rezervace. Různé klientské počítače smějí mít stejnou virtuální IP, phpvpn její jedinečnost nevynucuje a systém takovou kolizi neodmítá. UI musí pravdivě říct, že návrh není rezervací, duplicita je technicky přípustná a případná kolize je odpovědností, respektive chybou konfigurace provozovatele. Služba sama nepřidává `ACCESS_KEYS` na vzdálený server. Toto pravidlo nemění D13: lokální `virtualIp` nesmí současně zůstat klíčem vlastního `communicationContract`, protože by kontrakt ukazoval na tento počítač samotný.

Před IP polem je **Automatically detect IP address** podle `autoDetectIpAddressInWizard`. Při true je IP zakázaná a čeká na ověřený návrh z poslední úspěšné kontroly secretu. Návrh nahradí i neprázdnou placeholder IP, při false zůstane ruční IP zachovaná. Bez návrhu Next identity nepokračuje, UI vysvětlí možnost vypnout automatiku a zadat IP nebo se vrátit ke kontrole secretu. Zapnutí použije pouze stále platný návrh, samo nespouští HTTP. Změna URL/secretu, Reset, Connect, expirace či resume návrh a příslušné důkazy zneplatní. Back/Next zachová volby, resume zachová rozpracované hodnoty a všechny volby, ale vyžádá nové kontroly.

Před Access Key je **Use default access key** inicializované z `useDefaultAccessKeyInWizard` s výše uvedeným fallbackem pouze při absenci. Při true je pole zakázané a obsahuje `hex2(lastOctet)` opakované 32krát, například 32×`11` pro `.17`. Při false zůstává uložená hodnota editovatelná i při shodě s defaultem. Každá skutečná změna IP při true klíč přepočítá, neplatná IP včetně adresy mimo CIDR jej vyprázdní.

Uložené příznaky mají vždy přednost před odvozením. Shoda uloženého klíče s algoritmickým defaultem sama neaktivuje výchozí režim. Back/Next zachová aktuální volbu i text.

Dokud je checkbox zaškrtnutý, každá změna IP bez ohledu na zdroj změny okamžitě přegeneruje pole z nového posledního oktetu. Platí to pro psaní uživatele, návrh IP ze serverového `clients`, návrat Back/Next, Reset i jinou systémovou aktualizaci modelu. Neplatná nebo neúplná IP nesmí vytvořit zdánlivě platný klíč ani zachovat klíč od předchozí IP jako aktuální default. UI v takovém mezistavu zobrazí pole zakázané a validaci dokončí až po platné IP.

Po odškrtnutí checkboxu se pole zpřístupní a uživatel smí upravit právě zobrazenou hodnotu, která stále musí projít přesnou validací 64 malých hexadecimálních znaků. Zaškrtnutí bez dalšího potvrzení okamžitě zahodí jakýkoli ručně zadaný nebo původně načtený obsah a nahradí jej výchozím klíčem z právě platné IP. Následné odškrtnutí ponechá poslední vygenerovanou hodnotu jako editovatelný počáteční text a původní vlastní klíč z `config.json` neobnoví. Finish uloží konkrétní `accessKey` i `useDefaultAccessKeyInWizard`, služba ověří jejich soulad a přesné fields při check-access i Finish.

Před provedením kontroly je trvale viditelné upozornění: `This check may remove waiting messages for this IP address from the server. The messages will be discarded and will not be delivered to the VPN. If the result does not reach this window, the check may still have removed messages. Each new click on Next starts a new check and may remove more messages. Checks are not retried automatically.` Tlačítko Next je výslovný souhlas s tímto jedním pokusem, bez dalšího povinného modálního potvrzení.

Každý pokus check-access získá čerstvý ověřený health a info ve vlastním kontextu, aby dlouhé vyplňování a změna systémového času nepoužily starý odhad či serverové limity. Potom provede nejvýše jeden prázdný data request. Všechny health používají auth-none. Žádný automatický retry žádného kroku sekvence, ani po 401. Další pokus je nové stisknutí Next a znovu začíná čerstvým health. Normativní pravidla obnovy času po 401 se podle kapitoly 11 zpřesní tak, aby se ve Wizardu obnova odložila do tohoto nového výslovného pokusu. Recovery běžné VPN se tím nemění.

Prázdný request má pouze auth-full a LF. Vyžadovat HTTP úspěch, úplnou strukturu NDJSON, limity těla/řádku/počtu, správný requestId v data-result i data-mac, kanoničnost všech záznamů a správný HMAC. Pro prázdný odeslaný seznam navíc vyžadovat prázdné accepted/deferred ID seznamy a nulové acceptedRecords/acceptedBytes. Ověřovat shodu cílové IP a přípustnost směrovacích polí vrácených dat, nevydávat MAC za důkaz platného payload plaintextu.

Po tomto ověření všechny datové záznamy zahodit. `wizard-check-access` v ProbeClient používá výhradně obecnou jednorázovou diagnostickou možnost navrženou v kapitole 11. Nekontrolovat payload GCM ani nedešifrovat. Žádný přijatý payload neuložit, nedoručit síťové vrstvě, nezařadit do provozní fronty, nepředat jiné části klienta ani jinak nepoužít. Nevolat `ingestRecord`, nealokovat per-peer fronty, neodstranit položku z provozní odchozí fronty a nezměnit tunel, aktivní stav, čas, kontrolní body ani provozní čítače, včetně drop/ciphertext. Kontrola ověřuje Access Key a autentizovanou strukturu serverové odpovědi, neprokazuje autenticitu, integritu ani čitelnost plaintextu jednotlivých payloadů. Normální bootstrap, recovery i provozní data polling nadále po data-mac plně ověřují payload GCM a platná data zpracují podle běžných pravidel. MAC protokolu nehashuje celý ciphertext, UI proto nesmí tvrdit, že ověřilo end-to-end obsah zpráv.

Při 401 uvést, že autentizace byla odmítnuta a má se ověřit ACCESS_KEYS pro konkrétní virtuální IP v souboru na zadané serverové URL. Neoznačit timeout, 503 nebo chybný MAC automaticky za chybný Access Key. Webová URL neznamená, že je přes prohlížeč dostupný zdroj PHP. Nápověda má doporučit otevřít soubor ve správě hostingu nebo se obrátit na správce, nikoli vydávat URL za serverovou filesystem cestu.

### 7.4 Kontrakty

Ve všech režimech create/reset/edit/repair se vždy zobrazí `Add missing default communication contracts`, při každém novém otevření zaškrtnuté i pro úplnou sadu. UI ukáže počet všech původních kontraktů, chybějících výchozích kontraktů a plánované doplnění. Při odškrtnutí je doplnění nula a původní obsah i přítomnost objektu zůstávají zachované. Mimo `10.A.B.0/24` je checkbox stále zaškrtnutý, ale zakázaný s vysvětlením a efektivní request je false. Služba podstrčené true pro jiný CIDR odmítne. Volba kontraktů se do JSON neukládá.

Sdílené `defaultContractCandidates()` a `missingDefaultContractPeers()` vybírají z `.2` až `.15` v podporovaném CIDR, vynechají vlastní IP a všechny původní peery. Kandidátů bez self je 13 nebo 14, počet nových položek je pouze chybějící rozdíl. Každý původní kontrakt a jeho přesný payload klíč zůstává i při změně vlastní IP a mimo výchozí rozsah. Doplnění úplné sady je obsahová no-op. Nový create/reset koncept původní kontrakty nepřebírá, při false má prázdný objekt. Nové payload klíče jsou SHA-256 přes ASCII `minNumericIp_maxNumericIp` s číselným pořadím, jedním podtržítkem a bez LF, výstup lowercase hex. Náhled i publikace vycházejí ze stejného původního session DOM, neslučují pozdější cizí Save. Poslední úspěšná úplná publikace vyhrává.

Vlastní IP nesmí kolidovat s existujícím kontraktem. Konflikt se ověří před check-access a znovu před publikací, automatické doplnění žádný původní kontrakt neodstraní.

Nezávisle vypočtené očekávané testovací vektory:

| Přesný vstup | SHA-256 |
|---|---|
| `10.99.99.2_10.99.99.3` | `918c488528c2bc41a90f000b0a63bab0ba418790058ea7b448a3760aec16349d` |
| `10.99.99.2_10.99.99.10` | `f41f0ed508411aef592228e9d73ae137a4d31933f038160c04ae1e8d70597ebe` |
| `10.20.30.2_10.20.30.20` | `3e0d09769e22926032c4dbf961e2d6bc7caa9c6f326af51362c827849358b265` |

Tyto klíče i výchozí Access Key jsou veřejně odvoditelné a určené pro výchozí konfiguraci pet projektu. Wizard to vysvětlí jednoduchou větou. Nesmí je označit za náhodné nebo bezpečné tajemství. Nezavádí se povinné bezpečnostní schvalování navíc.

## 8. Přesný návrh rozšíření IPC v1

### 8.1 Společná pravidla

Ponechat `ipcVersion=1`, pevnou pipe `\\.\pipe\phpvpn-control-v1`, UTF-8 JSON, jeden request a response na spojení, UUIDv4 requestId a limit 4 MiB na kompletní zakódovanou zprávu. Všechny nové requesty mají uzavřenou sadu polí a přísné typy. Odmítnout duplicity, nadbytečné členy, neznámý příkaz, neplatné ID a operaci pro jinou relaci před jakýmkoli vedlejším efektem.

Nové příkazy nevyužívají přímé formáty get-config/get-info. Mají obálku `{ipcVersion, requestId, result}` nebo standardní `{ipcVersion, requestId, error}`. `result` je pro každý příkaz níže přesně definovaný objekt. Rozšířit buildery odděleně od původní acknowledgement obálky, která má `result:"ok"`.

Nesmí se změnit schema get-status kvůli cestě nebo secretům. `serviceInstanceId`, epochy a konfigurace patří do nových odpovědí. Existující parsery statusu musejí dále projít beze změny významu čítačů. Pořadí JSON členů není sémantické.

### 8.2 Příkazy

Každý řádek uvádí další request pole nad povinnou trojicí. Relace je vždy identifikována dvojicí `serviceInstanceId` a `sessionId`, obě UUIDv4.

| Příkaz | Další request pole | Úspěšný result |
|---|---|---|
| `get-config-state` | žádná | `availability`, `reasonCode` nebo null, `configPath`, `revisionToken` nebo null, `serviceInstanceId`, `connectionEpoch`, `vpnState`, `vpnPhase` |
| `get-config-document` | žádná | `configPath`, `availability`, `reasonCode` nebo null, `document` jako string nebo null, `revisionToken` nebo null |
| `get-default-config` | žádná | `document` jako string se společnou validní šablonou editoru |
| `create-default-config` | žádná, odeslat jen po Ano | `created` jako bool, `configPath`, `availability`, `document` jako string, `revisionToken` |
| `wizard-open` | žádná | `sessionId`, `serviceInstanceId`, `revisionToken`, `availability`, `mode`, `fields` nebo null, `context` nebo null, `contractCount`, `existingDefaultContractPeers`, `connectionEpoch`, `vpnState`, `vpnPhase` |
| `wizard-reset` | relace | stejný stav relace jako open, mode reset, nové defaulty, bez revizní podmínky Finish |
| `wizard-keepalive` | relace | `alive:true`, `leaseDurationMs:60000` |
| `wizard-check-url` | relace, `clientAttemptId`, `attemptSequence`, `serverUrl` | `operationId`, `operationState:"pending"` |
| `wizard-check-secret` | relace, `clientAttemptId`, `attemptSequence`, `serverUrl`, `serverSecret` | `operationId`, `operationState:"pending"` |
| `wizard-check-access` | relace, `clientAttemptId`, `attemptSequence`, `fields` | `operationId`, `operationState:"pending"` |
| `wizard-get-operation` | relace, `operationId` | aktuální typovaný OperationResult |
| `wizard-cancel-operation` | relace, `operationId` | `operationId`, stav cancelling/cancelled nebo již terminální stav |
| `wizard-cancel-attempt` | relace, `clientAttemptId`, `attemptSequence` | `attemptSequence`, `attemptState` z cancelling/cancelled/closed |
| `wizard-resume` | relace, `connectionEpoch` | aktuální relace po potvrzeném odpojení, všechny důkazy pending |
| `wizard-finish` | relace, `commitId`, `fields`, `createDefaultContracts` | `operationId`, `operationState:"pending"`, dokončení se čte stejným get-operation |
| `wizard-get-commit` | relace, `commitId` | `commitState` z pending/confirmed/failed/unknown, `receipt` nebo null, `error` nebo null |
| `wizard-close` | relace | `closed:true`, bez dopadu na VPN |

`fields` obsahuje právě čtyři stringy `serverUrl`, `serverSecret`, `virtualIp`, `accessKey` a tři povinné booleany `useDefaultServerSecretInWizard`, `autoDetectIpAddressInWizard`, `useDefaultAccessKeyInWizard`. `context` obsahuje efektivní `vpnCidr`, `trustAnyTlsCertificate` a všech osm runtime limitů. Služba je odvodí z vlastního originálu nebo resetovaného konceptu, nesmí věřit klientem podstrčenému TLS nebo CIDR. `contractCount` je nezáporné celé číslo. `revisionToken` je neprůhledný náhodný identifikátor revize v rámci instance služby, nikoli zveřejněný hash secretu ani očekávaná revize commitu.

`existingDefaultContractPeers` je povinné pole nejvýše 14 kanonických, unikátních a číselně seřazených kandidátních IPv4 stringů z původního session DOM. Počet nesmí překročit `contractCount`, který zahrnuje všechny původní kontrakty. Mimo podporovaný CIDR je pole prázdné. Při fields/context null musí být pole prázdné a počet nula, stejně jako u create/reset konceptu bez kontraktů. Finish nepřijímá klientem tvrzené počty ani tento seznam. Sedmipoložkové fields tvoří access evidence i commit fingerprint. True default secret/key musí souhlasit s konkrétní hodnotou, true auto IP s návrhem uloženým službou při poslední platné secret kontrole. Změna kteréhokoli příznaku vyžaduje nový access důkaz. Stejný commitId s jinými fields je odmítnut, totožný receipt neopakuje zápis.

Služba a tray používají nové uzavřené schéma společně. Přesný historický čtyřstringový tvar je `wizard_unsupported`, ostatní vadná schémata `ipc_malformed`. Žádný downgrade, vynechání metadat ani přímý zápis tray. Staré konfigurace jsou nadále přijímány bez automatické migrace, původních sedm IPC příkazů se nemění.

`get-config-document` pro missing vrátí document/revisionToken null, pro UTF-8 text v mezích 5.1 skutečný text po odstranění případného počátečního BOM a dostupnost. Pro nečitelnost vrátí konkrétní chybu přístupu, pro neplatné či nepodporované kódování `config_encoding_error`, pro překročení velikosti `config_too_large`, vždy bez dokumentu. Textový string v obálce dovoluje syntakticky chybný JSON, pouze pokud je platným UTF-8 textem. IPC ani tray nesmějí bajty opravovat nebo převádět přes base64. `create-default-config` nepřijímá cestu ani obsah. Při souběžném vzniku souboru vrátí `created:false` a jeho skutečný UTF-8 text v limitu, případně konkrétní chybu přístupu, kódování nebo velikosti. Na validitu JSON tohoto cizího textu se neváže jeho otevření. Po úspěchu vytvoření vrací `created:true`. Save dále používá původní set-config s úplným dokumentem a jeho službovou validací včetně kódování a velikosti. Při nepodporovaných nových příkazech tray oznámí nutnost nové služby a nepoužije starou cache ani vytvoření přes fallback.

`get-config-state` má pouze jednorázové použití: obsluha zvolených Statistics/Contracts a výslovné ověření nejasného Finish po restartu služby. Nevolá se při startu tray, na timeru, při otevření menu ani automaticky po Save/Finish. `wizard-open`, `get-config-document`, Connect a get-info provedou svou vlastní jedinou autoritativní kontrolu bez předřazeného get-config-state. `availability` a bezpečný reasonCode v konfiguračních odpovědích rozlišují všechny stavy 5.1, včetně chyby kódování a velikosti. `wizard-open` při odmítnutí obsahu neposkytne jeho fields/context, dovolí pouze nabídku Reset nebo nápravy podle 5.4.

`clientAttemptId` je UUIDv4 jednoho stisku Next a slouží ke korelaci, nikoli k automatickému obnovování startu. Každý ze tří wizard-check requestů navíc obsahuje `attemptSequence`, kladné 64bitové pořadové číslo pokusu v relaci. Tray ho zvýší při každém novém ručním Next, nikdy při pollingu výsledku. Služba drží pouze nejvyšší přijaté pořadí a aktuální pokus. Start se stejným nebo nižším pořadím se odmítne bez sítě, nikdy znovu neprovede vyřazený pokus. Vyšší pořadí během dosud běžící operace dostane busy a samo se později nespustí. I takto uzavřený validní start posune nejvyšší pořadí, aniž by změnil dosud běžící operaci. Reset a přerušení Connectem nemažou nejvyšší pořadí, nové sessionId začíná novou řadu. Přetečení se odmítne. Nevzniká historie UUID ani důvod nutit uživatele znovu otevřít Wizard po běžném počtu pokusů.

Tray start posílá právě jednou. Při ztracené odpovědi jej neopakuje. `wizard-cancel-attempt` umožní uzavřít pokus i bez doručeného operationId: služba zapamatuje jeho pořadí, zruší odpovídající práci a odmítne opožděný start tohoto či nižšího pořadí. Staré rušení nesmí zasáhnout novější pokus a neshodné ID stejného aktuálního pořadí se odmítne. Pokud zrušení nelze doručit, HTTP deadline a pronájem dál omezují životnost práce. Nový Next respektuje busy a čekání na potvrzené ukončení. Úspěšně doručené průběžné get-operation pouze čte stav již známé operace a nespouští novou kontrolu.

Finish má samostatná nezměněná pravidla idempotence: stejné commitId se shodným úplným vstupem vrací stejný výsledek, změněný vstup se odmítne a zápis se neopakuje. Ztracené potvrzení se čte přes get-operation nebo wizard-get-commit, nikoli novým zápisem. Toto pravidlo se nesmí zaměnit za zrušenou obnovu síťových pokusů.

`OperationResult` obsahuje `operationId`, `kind` z pevného výčtu url/secret/access/finish, `operationState` z pending/running/cancelling/succeeded/failed/cancelled/interrupted, `connectionEpoch`, `clientAttemptId` a `attemptSequence` pro kontroly (pro finish null), `error` nebo null a `payload` nebo null. Payload je uzavřený podle kind: URL jen výsledek kontroly, secret navíc případný návrh IP, access jen výsledek a počet zahozených záznamů, finish receipt s commitId a novou revizí. Text klíče odvozeného z IP může UI spočítat společnou čistou funkcí, služba jej vždy nezávisle ověří jako vstup check-access.

Každý kontrolní důkaz uložený službou váže přesné relevantní hodnoty, generaci konceptu, TLS/runtime kontext, serviceInstanceId, connectionEpoch a aktuální pokus. Finish vyžaduje shodu se všemi potřebnými důkazy. Po návratu Back a změně textu tedy nelze přes IPC uložit jiné hodnoty než ty, které byly ověřené. Změna uložené revize cizím Save/Finish sama důkaz nezneplatňuje. `createDefaultContracts` nemění platnost přístupového ověření, ale služba při Finish ověří jeho přípustnost pro daný režim/CIDR.

`wizard-resume` nesmí odpojit sám od sebe ani přijmout klientem tvrzený disconnected. Služba ověří aktuální stav, fázi a nepřítomnost čekajícího Connect. Samotné globální odpojení zůstává původním příkazem disconnect.

### 8.3 Životnost a limity

Nejvýše osm rozpracovaných Wizard relací na službu, každá jeden koncept a jedna aktuální kontrola. Každá otevřená relace má obnovovaný pronájem, výchozí potvrzení každých 15 s a expiraci po 60 s bez platného kontaktu. Služba porovnává uplynulý monotónní čas, ne počet zmeškaných pingů. Platný požadavek k dané živé relaci smí kontakt obnovit. Při pouhém vyplňování tray odešle krátký wizard-keepalive. Obecné get-status/get-config-state ani provoz jiné relace její pronájem neobnovují.

Heartbeat je pouze lokální IPC, nespouští HTTP, nic nepersistuje a není retry Next. Neprodlužuje HTTP deadline ani limit doručení výsledku do tray. Použít existující správu relací a službový časovač, nezavádět kvůli pronájmu nové vlákno nebo sledování PID. Intervaly lze při implementaci odůvodněně doladit a současně aktualizovat dokumentaci a testy, princip zůstává závazný.

Po expiraci nejprve relaci označit za neplatnou, poté bezpečně zrušit rozpracovanou kontrolu a po jejím terminálním stavu uvolnit koncept i prostředky. Objekt používaný callbackem nebo vláknem se neuvolní předčasně. Místo v limitu zůstane započítané do dokončení úklidu. Opožděný request relaci neoživí. Normální zavření vyžádá úklid ihned, při pádu/zamrznutí tray, odhlášení nebo výpadku IPC nastane nejpozději po době pronájmu plus stanovené toleranci časovače a konečné době rušení. Po resume služba vyhodnotí uplynulou dobu před dalším obnovením pronájmu, použitý časový zdroj musí zohlednit dobu uspání. Expirace není důkazem smrti procesu a neodpojí globální VPN ani jiný Wizard.

Čekání tray na výsledek jednoho Next má samostatný konečný monotónní rozpočet od stisku, zahrnující IPC start, maximálně 1/2/3 HTTP requesty pro url/secret/access, konečný rozpočet lokálního zpracování a doručení výsledku. Konkrétní IPC a lokální rozpočty stanovit v B01, pro výchozí HTTP hodnoty vycházet z nejvýše 30/60/90 s síťové části. Celkový součet a převody kontrolovat na přetečení. Polling ani keepalive konečný termín neobnovují. Timeout nebo jiná chyba kteréhokoli IPC volání kontroly ukončí aktuální Next v UI, zruší další čtení jeho výsledku a vyžádá bezpečné ukončení práce. Pozdní succeeded už krok nezmění. Nový Next je vždy ruční, může získat nová data a spotřebovat další zprávy.

Commit a jeho receipt mají bezpečnou životnost oddělenou od pronájmu editace. Expirace ani zavření nesmějí vrátit již publikovaný zápis nebo jej označit za neprovedený. Před publikační sekcí rezervovat místo pro záznam výsledku. Registry receipts musí mít konečný limit a retenční dobu stanovené v B01, při zaplnění nový commit odmítnout před zápisem, nikoli vyřadit potřebné potvrzení. Receipt zachovat alespoň po zbytek živé relace a po stanovenou dobu řešení nejasného Finish i po její expiraci. wizard-get-commit může pouze přečíst existující záznam pod původní trojicí serviceInstanceId/sessionId/commitId, neobnoví pronájem ani nepovolí další zápis. Terminální potvrzení je neměnné, dokládá konkrétní publikaci, i když ji pozdější Save již přepsal. Vyřazené či expirované relace se přes opakovaný Finish nikdy nezaloží znovu.

Pád služby ruší relace a síťové důkazy. Při restartu během nejasného Finish klient v rámci řešení této rozpracované akce jednorázově načte nový config-state a zobrazí výslovné ověření výsledku, nezačne automaticky nový zápis. Nejde o kontrolu při běžném startu tray ani o periodický refresh. Trvalý commit recovery záznam identifikuje pouze publikaci a revizi, neobsahuje další kopii secretů. Nezjištěný výsledek zůstane unknown, nikdy se z něj neodvodí automatický retry. Tato pravidla zachovávají idempotenci Finish a řešení nejasného zápisu, změna D23 se týká pouze síťových kontrol.

Limit zprávy zahrnuje JSON escaping. Samostatný limit souboru a výsledného dokumentu je 512 KiB podle 5.1 a vynucuje se již při čtení před úplným hashováním a parsováním. Dokument, který se s obálkou nevejde do v1, odmítnout před další kontrolou a zápisem s informací o velikosti, nerozdělovat jej na nezdokumentované kusy. Doplnit testy obou horních hranic, escapingu a striktního UTF-8. Limity responseTimeoutSeconds se při převodu do WinHTTP nesmějí násobením přetočit na záporný nebo nekonečný timeout.

### 8.4 Chyby a bezpečnost

Přidat stabilní kódy například `config_unreadable`, `config_encoding_error`, `config_too_large`, `wizard_session_expired`, `wizard_prerequisite_failed`, `wizard_attempt_closed`, `wizard_connection_interrupted`, `wizard_commit_unknown`, `wizard_contract_self_conflict` a `wizard_unsupported`, dále rozlišit busy, timeout a chybu souborové publikace. Nezavádět config_conflict pro zastaralou uloženou revizi. Původní chyby neměnit na raw systémové výpisy.

Wizard chyba má strukturované `code`, hotové anglické `description`, `recoverable`, `field` nebo null, `httpStatus` nebo null, `suggestedAction` a případně povolené `serverUrl`/`configPath`. URL a cesta pocházejí z konceptu a pevného úložiště, ne z neověřeného serverového těla. Síťovou příčinu mapovat na DNS, connect, TLS, timeout, odmítnutou autentizaci, neplatný formát a neplatné kryptografické ověření. Neočekávanou HTML odpověď nespouštět ani nezobrazovat jako HTML.

Zákaz citlivého logování platí i pro detail chyby. Dnešní `Error::message()` přidává detail, proto se do běžného Error.detail nesmějí vložit requesty, klíče ani raw těla. Wizard diagnostika má vlastní povolená pole a nesmí prosakovat do get-status, progress, Statistics, tooltipu nebo loggeru. Konfigurační pole Wizardu a editoru samozřejmě smějí podle svého účelu zobrazit hodnoty, které uživatel zadává.

Zachovat vědomě veřejný přístup pro lokální aplikace, bez impersonace a bez kontroly PID. Pipe musí výslovně odmítat vzdálené klienty. Příkazy nepřijímají filesystem cestu, HTTP metodu, vlastní action, libovolné hlavičky ani spustitelný příkaz. UI nemá nové oprávnění zapisovat ProgramData. Nepřidávat síťový allowlist, který by zrušil existující podporu lokálních HTTP serverů.

## 9. Win32 UI a texty pro začátečníka

UI zůstane anglické jako současné menu. Texty budou soustředěné a testovatelné, bez čísel řádků v PHP souboru. Jedno Wizard okno na tray relaci. Opakované otevření existující okno aktivuje, nezahodí koncept ani nevytvoří druhý sessionId. Jiná interaktivní relace může mít vlastní průvodce, každý ukládá svůj úplný koncept podle D22.

Otevření menu nic nečte z disku a neposílá get-config-state. Konfiguračně závislé položky lze zvolit i při dosud neznámé nebo dříve chybné konfiguraci. Teprve obsluha konkrétní akce zobrazí čekání na její jedinou kontrolu a případně odpovídající chybu či návod z 5.4. Disconnect zůstává při aktivní VPN funkční i během čekajícího Show Info či jiné dlouhé diagnostiky. Tray ihned použije nezávislou řídicí cestu, viditelně sleduje odpojování až do skutečného dokončení a při chybě odeslání nebo potvrzení ukáže konkrétní příčinu. Obsazení diagnostické čtečky není důvod kliknutí ignorovat. Pozdní diagnostický výsledek nesmí přepsat stav odpojování nebo VPN znovu připojit.

Výchozí klientská plocha přibližně `1000 × 680` při 96 DPI, levý seznam kroků přibližně 230 DIP, pravá obsahová část a samostatný dolní pás tlačítek. Okno se vystředí na pracovní ploše aktivního monitoru a zmenší na dostupnou plochu. Při malé ploše se posouvá obsah, nikoli dostupnost Cancel/Next/Finish. Finální geometrie vychází z naměřených textů, ne z předpokladu, že se nápověda vždy vejde na dva řádky.

Aktivní krok je tučný a označený, úspěšný má zelenou značku a textový význam, chyba červenou značku a popis. Barva není jediný nositel informace. Ovládání podporuje Tab, Shift+Tab, klávesové zkratky, Enter na povolené výchozí akci a Escape jako Cancel. Next při nevalidním poli nesmí spustit síť, fokus přejde na první chybu. Během kontroly zobrazit konkrétní činnost, zakázat změnu ověřovaných polí a opakované Next, ale umožnit Cancel/Reset s korektním zrušením operace.

| Místo | Obsah nápovědy |
|---|---|
| začátek | průvodce nastaví tento počítač, změny se uloží až pomocí Finish, ke spojení se následně použije Connect |
| disconnectGate | VPN používají všechny relace na tomto počítači, Next ji odpojí všem a průvodce ji sám neobnoví |
| Server URL Configuration | zadat úplnou adresu serverového skriptu, příklad URL, kde ji získat, při placeholderu výslovně vyzvat k nahrazení |
| SERVER_SECRET Configuration | hodnota odpovídá konstantě SERVER_SECRET v horní části serverového phpvpn.php, lze ji získat od správce, default je veřejně známý |
| IP Address Configuration | virtuální adresa tohoto počítače, povolený zachovaný CIDR, návrh není rezervace, různé počítače smějí mít stejnou IP a případná kolize je chybou konfigurace provozovatele |
| ACCESS_KEY | před polem checkbox **Use default access key**. Nový/resetovaný koncept nebo repair bez klíče začíná zaškrtnutý, existující klíč se zobrazí jako vlastní s odškrtnutým checkboxem. Zaškrtnutí jej přepíše defaultem, následné odškrtnutí vygenerovaný text ponechá a původní klíč neobnoví. Aktivní default se okamžitě přegeneruje při každé změně IP. Dále vysvětlit přesný klíč ze serverové mapy ACCESS_KEYS, 64 malých hex znaků a spotřebování čekajících zpráv. |
| Communication Contracts | které protějšky budou vytvořeny a jejich počet, nebo počet zachovaných kontraktů, nebo důvod nevytvoření kvůli CIDR |
| Finish | uloží konfiguraci a otevře okno Communication Contracts, nepřipojuje VPN |

Příklad chybějícího souboru: `The configuration file does not exist at: {absolutePath}. Run Configuration Wizard to create it after you successfully finish all steps. If you already have a config.json from your administrator, you can copy it to this location.` Nečitelnost a neplatnost mají vlastní pravdivý úvod, nikdy text „does not exist“, pokud soubor existuje.

Edit Config má místo tohoto varování vlastní potvrzení s tlačítky Yes/No: `This application uses the configuration file at: {absolutePath}. The file does not exist. Create it now from the default template?` Nečitelnost otevře konkrétní chybu přístupu. Syntakticky chybný JSON se otevře k opravě pouze jako platný UTF-8 text v limitu. Chyba kódování zobrazí například `The configuration file at {absolutePath} is not valid UTF-8 text. Save it as UTF-8 in an external editor and try again.` Při překročení velikosti uvést limit 512 KiB. Neotevřít náhradní nebo částečně opravený dokument. Volitelný počáteční BOM se normalizuje podle 5.1. Potvrzené vytvoření proběhne na disku již před otevřením editoru, Cancel soubor neodstraní.

Editor doplnit o Reset v dolním pásu vedle Save a Cancel. Potvrzení Yes/No: `Restore the default configuration in this editor? The file will change only after you choose Save.` Ano načte stejnou šablonu služby jako create-default-config a označí obsah za změněný. Ne ani chyba získání šablony nesmějí ztratit rozpracovaný text. Zachovat výchozí plochu `1080 × 560` při 100% DPI, oba posuvníky a škálování. Při resize a změně DPI musejí zůstat viditelná a funkční všechna tři tlačítka, bez původního artefaktu vedle Cancel. Uložení nevyžaduje souhlas s přepsáním mezitím uložených změn.

Příklad timeoutu Next: `The check did not complete in time. Your entries have been kept. Choose Next to start a new check.` Příklad zániku relace: `This wizard session expired because contact with the service was lost. Open Configuration Wizard again to continue.` Probíhající nejasný Finish se místo tvrzení o neuložení řídí vlastním potvrzením commitu.

Příklad kolize: `This IP address is already used by an existing communication contract. A contract cannot point to this computer itself. Choose another IP address, or cancel the wizard and use Edit Config to correct the conflicting contract before trying again.` Zobrazit konkrétní kolizní virtuální IP, žádný payloadKey. Hláška je vysvětlení, nikoli dodatečné schvalování automatického odstranění.

Placeholder má titul `Communication Contracts`, běžné záhlaví, možnost zavřít a prázdnou obsahovou plochu. Neobsahuje zatím falešné editační prvky. Po zvolení Contracts z menu se provede jediná aktuální kontrola dostupnosti. Automatické otevření po Finish použije potvrzený výsledek publikace bez nového čtení souboru. Není podmínkou úspěšnosti uložené konfigurace, selhání vytvoření HWND nesmí změnit confirmed commit na neúspěšný zápis.

Wizard může mít vlastní 500ms polling stavu dosud běžící operace a epochy pouze po dobu otevření. Když žádnou kontrolu neprovádí, jeho pronájem udržuje 15s keepalive, případně jiný platný požadavek dané relace. Původní pětisekundový tray timer pro paměťový get-status i 500ms timer Connecting progress window zůstávají nezávislé a nespouštějí čtení konfigurace. Epochu otevřené relace číst z jejího paměťového stavu, nikoli přes diskový get-config-state. Zavření Wizardu ukončí jeho polling i keepalive. Odpověď smí změnit UI jen při shodě windowGeneration, serviceInstanceId, sessionId, operationId, clientAttemptId a attemptSequence, a pokud pokus ještě neskončil a nevypršel jeho konečný termín. Staré potvrzení ani úspěch nesmějí ovlivnit nový pokus.

## 10. Server, defaulty a kompatibilita

V `phpvpn.php` upravit konfigurační hlavičku a komentář, `Config::fromConstants`, `Config::withSecret`, `Config::isInsecureSecret` a `Application::isConfiguredSecretInsecure`. Zavést jedinou interní konstantu/default helper tak, aby se nemohly lišit normalizace a výchozí hodnota. Veřejná stránka musí varovat při novém defaultu, prázdném nakonfigurovaném secretu, řetězci samých ASCII nul a samých nulových bajtů. Autentizované endpointy tyto hodnoty nadále neodmítají jen kvůli jejich předvídatelnosti.

Rozšířit ACCESS_KEYS na `.2–.15`, zachovat dosavadní `.2–.4`, doplnit `.5–.9` a `.10–.15` s dvojicemi `05` až `09` a `0a` až `0f`. Není to dynamické přenastavení serveru při změně klientského CIDR. Pro jinou síť musí správce nadále sladit serverové adresní parametry a mapu.

Stejný default použít v C++ key_derivation, config_defaults, předvídatelnostním warningu, Python normalizaci a dokumentaci. Nenahrazovat plošně všechny řetězce nul, některé jsou známé AES testovací vektory nebo explicitně testovaná neprázdná tajemství.

Původní drátové formáty, kryptografické domény a serverVersion negotiation se nemění. Kombinace nové a staré implementace s explicitně stejným neprázdným secretem dál interoperuje. Prázdná hodnota má novou efektivní hodnotu, takže kombinace starého a nového defaultu interoperabilní není. Nezavádět fallback, automatický rewrite existujících configů ani dedukci secretu podle serverVersion.

Nová tray při `ipc_unknown_command` na příkazu zvolené nové funkce, například jednorázovém get-config-state, get-config-document nebo wizard-open, zobrazí nutnost aktualizované služby pro tuto funkci a nic nevytváří. Kvůli detekci podpory neprovádí konfigurační polling ani kontrolu při otevření menu. Původní příkazy zůstanou dostupné podle původní smlouvy, pokud je kompatibilní stav služby známý. Žádné hledání alternativní pipe ani automatický downgrade nové operace na set-config. Starý klient nové příkazy nepotřebuje, původních sedm příkazů na nové službě musí stále fungovat.

Projekt nemá nasazené uživatele, proto není potřeba migrační utilita, dlouhodobá dvojí IPC verze ani složitý rollout. Novou službu a tray distribuovat společně. Instalační skripty nesmějí zapisovat ani nepřímo vyvolat vznik configu a musí po instalaci správně popsat obě cesty: úspěšný Finish Wizardu nebo výslovně potvrzené vytvoření přes Edit Config. Existující config se při reinstalaci zachová.

## 11. Aktualizace dokumentace a testovací infrastruktury

| Soubor | Konkrétní změna |
|---|---|
| `protocol.md` | Konzistentně změnit default a normalizaci v 3.2, odkazech 7.2, příkladu PHP v 15, testech 16.1/16.3 a checklistu 17, veřejné varování v 5.1 a výchozí mapu klíčů v souvisejících příkladech. V 9.5 výslovně vymezit obecnou jednorázovou diagnostickou kontrolu přístupu a úzkou výjimku z payload GCM podle normativního textu níže. Související 5.2, 11.2, 13, testy 16 a checklist 17 sladit pouze v protokolových pojmech. Protokol nepopisuje tray, Wizard, IPC ani UI. Zachovat wire formáty, best-effort clients a at-most-once polling. |
| `client.md` | Promítnout D01–D28 do konfigurace, tří VPN rolí a oddělené diagnostiky, transportu, obnovy, IPC, menu, editoru, Wizardu, čítačů a lifecycle testů. V části o třetím kroku výslovně popsat celý stavový kontrakt checkboxu **Use default access key** a jeho vazbu na každou změnu IP. Existující uložený klíč se inicializuje jako vlastní s odškrtnutým checkboxem, zaškrtnutí jej přepíše defaultem a následné odškrtnutí původní hodnotu neobnoví. V části o adresování výslovně uvést, že různé klientské počítače smějí používat stejnou virtuální IP, phpvpn jedinečnost nevynucuje a kolize je odpovědností či chybou konfigurace provozovatele. Výslovně popsat také nezávislou cestu Disconnectu v tray, kontrolu konfigurace až po zvolení akce, 512KiB limit a striktní UTF-8/BOM. Obecnou protokolovou diagnostiku svázat s wizard-check-access a ProbeClient právě zde. Konkrétní normativní zásahy jsou v následující tabulce, nestačí připojit obecný odkaz na plán. |
| `plan_client.md`, `plan2_client.md` | krátký odkaz na novější plán pro překonané části, případně označit historické defaulty jako historické. Nemíchat staré instalační požadavky s aktuálním stavem. |
| `client/src/CMakeLists.txt` | nové core/config/wizard/IPC a Windows file backend zdroje. |
| `client/apps/CMakeLists.txt` | nové tray modely, čtečky a okna. |
| `client/tests/CMakeLists.txt` | nové testy a skutečné zdroje volitelného contract targetu. |
| `client/tests/manual/README.md` | Wizard, chybějící/invalid config, dvojí relace, cizí Connect, DPI a cancel/commit scénáře s evidencí NOT RUN/RUN. |
| `tests.php` | nový default, zachování neprázdných nul, veřejné varování a rozšířená výchozí mapa. |
| `tests-integration/phpvpn_client.py` | jediná správná normalizace nového defaultu. |
| `tests-integration/test_crypto_vectors.py` | očekávání pro prázdný secret a nezávislé vektory. |
| `tests-integration/test_status_and_routing.py`, `test_health.py`, `test_info.py`, `test_security.py` | relevantní regresní a konfigurační varianty, bez odvozování očekávání jen z nové implementace. |
| `tools/deploy-test.ps1` | bezpečně ověřená náhrada nové hlavičky a rozšířené mapy, selhání při nenalezení přesně jednoho očekávaného bloku. |
| `tests-integration/README.md` | postup testování defaultu a variant secretů na oddělených testovacích instancích. |
| `client/tools/package/install.ps1`, `client/tools/dev-install.ps1` | oprava hlášek a regresní ověření čisté instalace bez souboru. |

Následující normativní změny jsou povinnou součástí budoucí implementace. V této plánovací úpravě se samotné `client.md` ani `protocol.md` ještě nepřepisují. Před změnou kódu se musí aktualizovat i původní rozporné věty a jejich testy, nikoli jen doplnit nová podkapitola vedle nich.

Navržený normativní text pro `protocol.md` 9.5 a odpovídající odkazy, bez pojmů uživatelského rozhraní nebo lokálního řízení:

> Klient smí provést jednorázovou diagnostickou kontrolu přístupu s nejvýše jedním prázdným requestem `data`, jehož tělo obsahuje pouze `auth-full` a LF. Před úspěšným ukončením musí úplně ověřit HTTP úspěch, strukturu NDJSON, korelaci `requestId` v `data-result` i `data-mac`, všechny velikostní a početní limity, kanoničnost a směrovací pravidla záznamů, nulový výsledek prázdného requestu (prázdné seznamy accepted/deferred, nulové acceptedRecords/acceptedBytes) a platný závěrečný `data-mac`. Poté smí všechny přijaté datové záznamy zahodit bez ověření payload GCM a bez dešifrování. Žádný přijatý payload nesmí uložit, doručit síťové vrstvě, zařadit do provozní fronty, předat jiné části klienta ani jinak použít. Nesmí odstranit položku z provozní odchozí fronty nebo změnit provozní stav klienta.
>
> Kontrola ověřuje Access Key a autentizovanou strukturu serverové odpovědi. Neprokazuje autenticitu, integritu ani čitelnost plaintextu jednotlivých payloadů. Tato výjimka se nevztahuje na normální bootstrap připojení, recovery ani pravidelný provozní polling `data`. Tyto cesty musí po ověření `data-mac` nadále provést úplné GCM ověření každého zpracovávaného datového záznamu a platné payloady zpracovat podle běžných pravidel.

Vazbu na `wizard-check-access`, ProbeClient, Next a krok IP Address a Access Key Configuration uvést výhradně v `client.md` a v tomto plánu podle 7.3. Tam také stanovit zákaz dopadu této operace na tunel, fronty, čítače, kontrolní body a aktivní stav VPN.

| Nález | Místo normativní změny | Závazný obsah a vazba na plán |
|---|---|---|
| 1 | `client.md`: Architektura/Transport, Běhový model, Disconnect, upgrade a rollback, Windows lifecycle testy | Společná bezpečná životnost a rušení všech HTTP cest podle 4.4, krátká konečná doba, terminální stav před nahrazením/uvolněním, žádné dvojí zavření či callback po uvolnění kontextu. Zahrnout Connect proti probe, globální Disconnect, všechny důvody stopu služby a zavření/Reset Wizardu. Zachovat jednorázové účtování částečných VPN odpovědí. |
| 2 | `client.md`: Konfigurační soubor/responseTimeoutSeconds, Příjem dat, Transport a povinné testy. `protocol.md` 11.2 | Upřesnit celkový absolutní monotónní deadline DNS až konec těla bez obnovy dílčím blokem, kontrolované časové převody a kontrolu nastavení timeoutů. V protokolu ponechat obecné doporučení 15 s jako doporučení, uvést, že nativní klient používá vlastní normativní konfigurovatelný default 30 s. Timeout vyvolá bezpečné zrušení a nesmí skončit pozdním úspěchem. Protokol rozliší provozní obnovu a jednorázovou diagnostiku, konkrétní vazbu na Wizard popíše pouze client.md. |
| 3 | `client.md`: Účel, Architektura/Runtime, Běhový model, Synchronizace času, veřejné IPC/get-info | Formulace „právě tři vlákna“ vztáhnout na tři VPN pracovní role, ne na všechny platformní a řídicí činnosti procesu. Jediné vlastnictví health/info/data a TimeSync vztáhnout na VPN kontext. Explicitně povolit oddělenou omezenou diagnostiku get-info s vlastním transportem/časem souběžně s VPN daty. Probe používá oddělenou cestu, ale nesmí překrýt VPN data request. Žádné proměnné VPN kontexty, fronty ani čítače v diagnostice. |
| 4 | `client.md`: Konfigurační soubor, set-config, Wizard Finish a souběhové testy | Úplná validace a atomická serializovaná publikace celého dokumentu podle posledního úspěšného uložení. Zastaralý editor i Wizard smí přepsat mezitím uložený obsah bez nové výzvy nebo sloučení. Cizí revize nezneplatňuje důkazy. Epochy, validace vstupů, ACL, bezpečné chyby a idempotence Finish zůstávají povinné. |
| 5 | `client.md`: Stavový automat připojení/Obnova, responseTimeoutSeconds, Wizard kontroly, IPC a UI. `protocol.md` 5.2, 11.2 a tabulka chyb 13 | Protokol obecně odliší provozní recovery od jednorázové diagnostické kontroly bez automatického opakování po chybě včetně 401. Nová samostatně vyžádaná autentizovaná kontrola začne obnovením ověřeného času, provozní backoff/recovery se nemění. Pouze client.md popíše Wizard Next, chyby IPC, konečné doručení do tray, korelaci a ignorování pozdního úspěchu, nový ruční Next a možnost opětovného odebrání zpráv. Průběžné čtení operace neprovádí síť. Finish má oddělenou idempotenci. Lokální attemptSequence není wire replay cache podle 6.3 protokolu. |
| 6 | `client.md`: Konfigurační soubor, get-config/set-config a whitelist nových IPC příkazů, Edit Config/menu, distribuce a testy editoru | Skutečný platný UTF-8 text v limitu 512 KiB i při nevalidním JSON přes novou textovou obálku, bez změny původního přímého formátu get-config pro platný JSON. Striktní dekódování, pouze počáteční volitelný BOM normalizovaný podle 5.1, žádné nahrazování znaků, odhad kódování ani base64 fallback. Nečitelnost, chyba kódování, velikosti a missing jsou různé výsledky. Potvrzené create-if-missing s absolutní cestou, okamžitý validovaný atomický zápis bez přepsání mezitím vzniklého cíle. Reset editoru potvrzením mění jen text, Cancel neodstraní již vytvořený soubor. Jediná šablona podle 5.6, všechna tři tlačítka v layoutu a regresích. Omezit tvrzení „soubor vzniká až při Finish“ na Wizard, startup vytváří pouze skutečně chybějící default, Connect dál nic nevytváří. Konfiguračně závislé menu položky jsou kliknutelné, jediná kontrola aktuálního souboru následuje až po akci. Odstranit periodický/menu refresh, Disconnect používá paměťový stav. |
| 7 | `client.md`: Veřejné lokální IPC, get-info, disconnect, tray, Windows Service stop a lifecycle testy | Zachovat již navržený asynchronní příjem/čtení/zápis, dispatch až po úplné validaci, dostupnou kapacitu krátkého řízení nezávislou na diagnostice, limity spojení/zpráv/paměti/front a absolutní I/O deadline. Doplnit povinné oddělení v tray, aby obsazený CommandReader neblokoval ani tiše nezahodil Disconnect během Show Info či jiné dlouhé diagnostiky. Ověřit skutečné přijetí službou a dokončení teardown, při selhání viditelnou konkrétní chybu. Stop služby nezávisí na pipe. Uvést konkrétní implementačně ověřené meze a profil zatížení, nikoli slib neomezené odolnosti vůči zahlcení. |
| 8 | `client.md`: Windows Service, konzole a tray, Wizard IPC/životnost a lifecycle testy | Odlišit autonomní životnost globální VPN od dočasných Wizard relací. Pronájem 15/60 s z monotónního času, platný kontakt jen dané relace, bezpečné zrušení a úklid po expiraci, bez nového vlákna/PID. Pozdní kontakt neobnovuje relaci, heartbeat neprodlužuje pokus. Samostatná životnost rozpracovaného commitu a receipts. Upřesnit, že odpojení jednorázové pipe není důkaz pádu tray. |

Ve společných scénářích `client.md` zachovat plné ověření GCM a zpracování payloadu při normálním bootstrapu, recovery i provozním pollingu VPN. Samostatně svázat `wizard-check-access` s obecnou protokolovou výjimkou 9.5, která až po úplném ověření HTTP/NDJSON/korelace/limitů/nulového výsledku/data-mac dovolí zahození bez payload GCM a bez jakéhokoli použití dat. Doplnit odpovídající pozitivní i negativní testy protokolu a zákaz dopadů na provozní stav. Doplnit celý konkrétní whitelist a uzavřená schémata kapitoly 8, omezenou výjimku configPath/URL jen v určených konfiguračních odpovědích a UI, nikoli v get-status, statistikách a logu. Nálezy 9–12 dřívější matice nevyvolávají další změny požadavků.

Serverový deployment pro testy dnes používá vlastní secret a klíče. Jeho existující fixture nesmí být potichu nahrazena defaulty. Pro ověření defaultního Wizard scénáře zavést samostatný fixture profil s `.2–.15`. Generovaný `tests-integration/server.json` není normativní dokument a nemá se ručně přepisovat jako součást změny defaultu.

## 12. Implementační bloky, závislosti a kontrolní brány

Každý blok je záměrně omezený na jednu tematickou oblast a má vlastní dokončitelný výsledek. Agent pracující na bloku musí načíst tuto kapitolu, odkazované části kapitol 1–11 a relevantní normativní dokumenty. Nesmí předpokládat, že souběžný blok již změnil sdílený soubor, pokud není uveden jako závislost. Změny společných build souborů se buď předem rozdělí po řádcích, nebo se sloučí až v B17, aby paralelní agenti neupravovali stejné seznamy zdrojů.

Povinné pořadí je B00 → B01. Po nich mohou paralelně začít B02, B03, B05, B06, B07 a B09. B04 čeká na B02+B03. B08 čeká na B06+B07. B10 čeká na B01+B09 a může běžet souběžně s B07. B11 čeká na B04+B08+B09+B10. B12 čeká na B04+B10. B13 čeká na B10+B12. B14 čeká na B02 a může běžet souběžně se službou. B15 čeká na B11+B13+B14. B16 čeká na B12+B13. B17 sloučí build a test targety po dokončení zdrojových bloků. B18 čeká na B05+B08+B11+B17. B19 je závěrečná integrační a regresní brána a čeká na všechny předchozí bloky.

### B00: Normativní dokumentace a odstranění rozporů

- **Rozsah a očekávaný výsledek:** aktualizovat `protocol.md`, `client.md`, `plan_client.md` a `plan2_client.md` podle D01–D28 a celé matice kapitoly 11. `protocol.md` dostane obecnou diagnostickou výjimku bez pojmů Wizardu. `client.md` ji sváže s ProbeClientem, popíše kontrolu až po zvolené akci, striktní UTF-8/BOM, startup vytvoření a obě explicitní cesty vytvoření souboru, nezávislý Disconnect, Access Key checkbox podle D27 a přípustné duplicitní IP podle D28. Historické plány dostanou pouze jasný odkaz na tento novější plán.
- **Závislosti a paralelnost:** bez závislostí. Musí skončit před jakýmkoli implementačním blokem. Jediný agent má vlastnit všechny čtyři dokumenty, aby nevznikly protichůdné normativní formulace.
- **Relevantní soubory:** `protocol.md`, `client.md`, `plan_client.md`, `plan2_client.md`.
- **Akceptační kritéria:** splněno A39 a dokumentační části A12, A13. Nezůstane obecná povinnost payload GCM, která by popírala úzkou diagnostickou výjimku, ani oslabení normálního bootstrapu, recovery a provozu. Nikde se netvrdí, že klientské IP musí být mezi různými počítači jedinečné.
- **Lokální ověření:** `rg` na starý secret, staré výroky o třech vláknech, automatické vytvoření configu, automatický retry Next, staré zachování Access Key a tvrzení o jedinečnosti IP. Ruční křížová kontrola všech míst vyjmenovaných v kapitole 11.

### B01: Číselné limity, rozpočty a platformní prototypy

- **Rozsah a očekávaný výsledek:** před produkčním kódem stanovit konkrétní limity IPC spojení, front, paměti a receipts, I/O deadline, lokální rozpočet Next, toleranci lease timeru a měřitelné cíle pro get-status, přijetí a dokončení Disconnectu, HTTP cancel a stop služby. Malými Windows prototypy ověřit použitelnost asynchronního WinHTTP lifecycle, overlapped pipe I/O a souborové publish strategie.
- **Závislosti a paralelnost:** závisí na B00. Je sekvenční branou pro B03, B07, B10 a B11. Jednotlivé tři prototypy mohou vznikat paralelně, ale jeden vlastník musí sloučit číselné výsledky a jejich společný profil zatížení.
- **Relevantní soubory:** implementační návrh v `client/tests/implementation-plan3-report.md`, dočasné či cílené testovací harnessy pod `client/tests`, rozhraní z 4.4, 5.5 a 8.3.
- **Akceptační kritéria:** limity jsou číselné, měřitelné a bezpečné proti přetečení. Výsledek neslibuje neomezenou odolnost proti zahlcení. Je doloženo, že zvolený mechanismus umí potvrdit terminální HTTP i pipe I/O stav a rozlišit jistý a nejasný souborový commit.
- **Lokální ověření:** spustit prototypové testy na cílových Windows s pomalou DNS/TLS/body cestou, neúplnou pipe zprávou, neodebíranou odpovědí a chybami write/flush/replace. Výsledky a skutečné časy zapsat jako RUN nebo NOT RUN.

### B02: Konfigurační datový model, defaulty a dokumentový patch

- **Rozsah a očekávaný výsledek:** implementovat sdílené konstanty, `config_defaults`, `config_document`, rozlišení valid/repairable, patch sedmi povolených členů a aditivní doplnění kontraktů a jedinou validní šablonu editoru. Továrna Wizard konceptu zachová edit/repair data i tři preference s přesným fallbackem pouze při absenci podle plan3_fix2. Reset vytvoří nový koncept a výchozí kontrakty pouze podle 7.4. Funkce výchozího Access Key vrací přesně `hex2(lastOctet)` opakované 32krát.
- **Závislosti a paralelnost:** závisí na B00+B01. Může běžet paralelně s B03, B05, B06, B07 a B09. Nesmí upravovat platformní file backend ani UI.
- **Relevantní soubory:** `client/src/core/config/config.h`, `config_validator.cpp`, nové `config_document.*`, `config_defaults.*`, `client/src/core/wizard/wizard_defaults.*`.
- **Akceptační kritéria:** úplný validátor zůstane striktní. Repairable znamená jen chybějící `virtualIp`, `accessKey` nebo obě pole. Edit/repair zachová nepovolené hodnoty a kontrakty. Šablona projde úplnou validací. Vektory `.2`, `.10`, `.15` a `.17` mají přesně 64 malých hex znaků.
- **Lokální ověření:** `test_wizard_document`, `test_default_config`, `test_wizard_defaults` včetně nezávislých SHA-256 vektorů kontraktů a hranic opravitelnosti.

### B03: Windows ConfigFiles a bezpečná atomická publikace

- **Rozsah a očekávaný výsledek:** implementovat testovatelné `ConfigFiles` a Windows backend pro omezené čtení, striktní identitu adresáře/cíle, unikátní CREATE_NEW temp, ACL před tajnými bajty, úplný write, `FlushFileBuffers`, replace/create-if-missing a bezpečnou obnovu. Backend poskytne bariéry pro deterministické souběhové testy.
- **Závislosti a paralelnost:** závisí na B00+B01. Může běžet paralelně s B02. B04 začne až po obou. Agent tohoto bloku nevytváří ConfigStore politiku posledního úspěšného uložení.
- **Relevantní soubory:** nové `client/src/platform/iface/config_files.h`, `client/src/platform/windows/config_files_win.*`, související `paths_win.*` jen pro autoritativní cestu.
- **Akceptační kritéria:** splněno A21 a file-backend část A40. Čtenář vidí celý starý nebo nový dokument. Chyba nezanechá truncation, veřejně čitelné tajemství, smazaný cizí cíl ani falešný výsledek. Create-if-missing nepřepíše mezitím vzniklý soubor.
- **Lokální ověření:** `test_config_files_win` na hranici 512 KiB, limit+1, růst souboru, reparse, locked target, externí write/rename/delete a všechny injektované chyby write/flush/publish/recovery.

### B04: ConfigStore, revize a serializace všech zápisů

- **Rozsah a očekávaný výsledek:** nad B02+B03 implementovat jediný `ConfigStore`, dostupnost, striktní UTF-8, jedinou počáteční BOM normalizaci, snapshots, revision tokens, commit receipts a serializační bránu pro Save, Finish a create-if-missing. Odstranit starou cache a sdílený `config.json.tmp`. Aktivní VPN si ponechá svůj neměnný snímek.
- **Závislosti a paralelnost:** závisí na B02+B03. Po stabilizaci veřejného rozhraní může běžet souběžně s B08 a B10, ale před B11 a B12 musí být dokončen.
- **Relevantní soubory:** nové `client/src/core/config/config_store.*`, `vpn_engine.cpp`, `service_host.cpp`, `paths_win.*`.
- **Akceptační kritéria:** splněno A01, A05–A07, A20–A22, A37, A40 a A41 na core úrovni. Poslední úspěšná úplná publikace vyhrává bez revizního konfliktu. Nadlimitní vstup se odmítne před úplným hashováním a parsováním. Službový startup vytváří pouze chybějící default po bezpečné recovery, Connect config nevytváří.
- **Lokální ověření:** `test_config_store`, `test_config_editor` a rozšířený `test_default_config`, včetně editor → Finish, Finish → starý editor, dvou editorů/Wizardů, vadného UTF-8 a nejasného commitu.

### B05: Serverový default, ACCESS_KEYS a testovací deployment profil

- **Rozsah a očekávaný výsledek:** změnit default na `CHANGE_THIS_SERVER_SECRET`, sjednotit normalizaci a warningy, rozšířit serverovou mapu `.2–.15`, upravit C++ a Python normalizaci a bezpečnou náhradu testovacího deploymentu. Zachovat explicitní testovací fixture a nepřepisovat payload helper algoritmem Wizardu.
- **Závislosti a paralelnost:** závisí na B00+B01 a může běžet paralelně s B02–B04, B06, B07 a B09. Sdílené C++ konstanty musí být před sloučením zkontrolovány proti B02.
- **Relevantní soubory:** `phpvpn.php`, `tests.php`, C++ key derivation, `tests-integration/phpvpn_client.py`, `test_crypto_vectors.py`, relevantní integrační testy, `tools/deploy-test.ps1`, `tests-integration/README.md`.
- **Akceptační kritéria:** splněno A29 a A30. Jen prázdná hodnota se normalizuje. `0`, 32/64 ASCII nul, NUL bajty a custom secret zůstávají doslova. Defaultní mapa má přesné klíče `.2–.15`. Deployment skript selže, nenajde-li právě jeden očekávaný blok.
- **Lokální ověření:** `php tests.php` a přes `uv` cílené `test_crypto_vectors.py`, `test_health.py`, `test_info.py`, `test_status_and_routing.py` a `test_security.py` proti oddělenému defaultnímu fixture.

### B06: Protokolové parsery, health rozdělení a info clients

- **Rozsah a očekávaný výsledek:** rozdělit health syntaxi a kryptografické ověření, opravit opakovaný health na čerstvý `auth-none`, doplnit plně typované `InfoResponse::clients` a sdílenou validaci data odpovědi potřebnou diagnostikou. Normální NetworkWorker musí zachovat plné payload GCM.
- **Závislosti a paralelnost:** závisí na B00+B01. Může běžet paralelně s B02–B05, B07 a B09. B08 čeká na jeho veřejná rozhraní.
- **Relevantní soubory:** `response_parser.*`, `network_worker.cpp`, protokolové typy a existující parser testy.
- **Akceptační kritéria:** URL kontrola umí skončit po syntax check bez publikace času. Secret kontrola používá GCM a validní `clients`. Kanonické a unikátní adresy a přesný UTC čas jsou vynuceny. Normální data cesty stále odmítají vadný payload GCM.
- **Lokální ověření:** řízené unit testy health/info/data parseru, regresní bootstrap/recovery/provozní data testy a skutečný opakovaný health proti PHP relay.

### B07: Asynchronní WinHTTP, bezpečné rušení a absolutní deadline

- **Rozsah a očekávaný výsledek:** nahradit nebezpečné synchronní rušení explicitně vlastněným asynchronním WinHTTP requestem. Jeden monotónní deadline pokryje DNS až celé tělo. Cancel, timeout, success a callback závod vyberou jedno dokončení, kontext žije do potvrzeného HANDLE_CLOSING a transport je znovu použitelný.
- **Závislosti a paralelnost:** závisí na B00+B01. Může běžet paralelně s B02–B06, B09 a B10. B08 a závěrečné VPN napojení čekají na dokončení.
- **Relevantní soubory:** transportní interface, `client/src/platform/windows/transport_winhttp.*`, `test_winhttp_lifecycle.cpp`, `test_winhttp_deadline.cpp`, `test_winhttp_accounting.cpp`.
- **Akceptační kritéria:** splněno A34 a A35. Žádné dvojí zavření, dvojí dokončení, use-after-free ani pozdní úspěch po deadline. Částečné VPN tělo se zaúčtuje právě jednou. Hraniční převody nevytvoří záporný či nekonečný timeout.
- **Lokální ověření:** oba nové WinHTTP testy, existující accounting regrese a loopback scénáře pro každou fázi timeoutu, opakované cancel a následné znovupoužití transportu.

### B08: Izolovaný ProbeClient a jednorázová kontrola přístupu

- **Rozsah a očekávaný výsledek:** implementovat ProbeClient s vlastním transportem a TimeSnapshot pro URL, secret a access kontrolu. Access pokus provede čerstvý health, info a nejvýše jeden prázdný `data`, úplně ověří obálku a nulový výsledek, pak všechny datové řádky zahodí bez payload GCM a bez vedlejšího efektu. Žádný krok se automaticky neopakuje.
- **Závislosti a paralelnost:** závisí na B06+B07 a na normativním vymezení B00. Může běžet paralelně s B04 a B10. B11 a B18 čekají na dokončení.
- **Relevantní soubory:** nové `client/src/core/wizard/probe_client.*`, protokolové pomocné funkce, řízený transport a `test_wizard_probe.cpp`.
- **Akceptační kritéria:** splněno A09, A10, A15–A17. Probe neotevře tunel, nepoužije aktivní TimeSync, fronty, kontrolní body ani čítače. Vadný `data-mac`, korelace, routing, limit nebo nenulový výsledek selže. Platný MAC s nedešifrovatelným payloadem smí v diagnostice uspět a payload pouze zahodit.
- **Lokální ověření:** `test_wizard_probe` pro 401, 503, timeout každé části, neplatné health/info/NDJSON/MAC, více datových řádků a důkaz nulových změn runtime stavu před a po probe.

### B09: Uzavřená schémata Wizard IPC

- **Rozsah a očekávaný výsledek:** implementovat typy, buildery a parsery všech příkazů kapitoly 8.2, včetně operation results, attemptSequence, commit receipts, config dokumentu a stabilních bezpečných chyb. Původních sedm příkazů a přímé get-info/get-config formáty zůstávají.
- **Závislosti a paralelnost:** závisí na B00+B01. Může běžet paralelně s B02–B08. B10 a B11 čekají na schémata.
- **Relevantní soubory:** `ipc_protocol.*`, `ipc_dispatcher.*`, nové `client/src/core/ipc/wizard_ipc.*`, `test_wizard_ipc.cpp`, `test_status_contract.cpp`.
- **Akceptační kritéria:** splněno schema z 8.2 a A27–A28. Duplicitní, nadbytečná nebo typově chybná pole, cizí session/operation a overflow jsou bez vedlejšího efektu. 4MiB limit zahrnuje escaping. Citlivé hodnoty se neobjeví v nepovolených odpovědích.
- **Lokální ověření:** `test_wizard_ipc` v obou směrech, fuzz/tabulkové negativní vstupy, regresní test původních sedmi příkazů a status parseru.

### B10: Asynchronní named pipe a oddělení krátkého řízení

- **Rozsah a očekávaný výsledek:** implementovat omezené overlapped přijímání, čtení a zápis IPC, dispatch až po celé validní zprávě, absolutní I/O deadline a nezávislou kapacitu pro get-status, Disconnect a stop. Dlouhá diagnostika a pomalá konfigurace nesmějí držet krátké řízení.
- **Závislosti a paralelnost:** závisí na B01+B09. Může běžet paralelně s B07. B11–B13 čekají na stabilní rozhraní. Stop služby nesmí být implementován jako další pipe příkaz.
- **Relevantní soubory:** `ipc_pipe.cpp`, service host/dispatcher wiring, `test_ipc_availability.cpp` a Windows pipe harness.
- **Akceptační kritéria:** splněny infrastrukturní části A26 a A36. Pomalé či neúplné klienty omezují stanovené limity. Get-status a přijetí Disconnectu zůstávají v cílech B01. Stop je časově omezený i při přetížení.
- **Lokální ověření:** skutečná pipe s neúplnými requesty, klienty nečtoucími response, pomalým get-info a pomalým file read. Změřit cíle B01 a ověřit omezený počet spojení, paměť a fronty.

### B11: WizardService, relace, pokusy, epochy a receipts

- **Rozsah a očekávaný výsledek:** implementovat omezené Wizard relace, 15/60s pronájem, čistý lifecycle kontrol, nejvyšší attemptSequence, důkazy vázané na vstupy, preemption Connectem, connectionEpoch, Finish idempotenci a samostatně žijící receipts. Zajistit nejvýše jednu řídicí síťovou operaci a nikdy souběžné `data` requesty s VPN.
- **Závislosti a paralelnost:** závisí na B04+B08+B09+B10. Kvůli sdílenému service dispatcheru má jednoho vlastníka. B12 může po stabilizaci ConfigStore běžet paralelně, pokud neupravuje stejné dispatcher větve.
- **Relevantní soubory:** nové `wizard_types.h`, `wizard_service.*`, service/VpnEngine integrační body, `test_wizard_service.cpp`.
- **Akceptační kritéria:** splněno A19, A22–A24, A33, A38. Opožděný kontakt neoživí relaci. Keepalive nespouští HTTP ani neprodlužuje Next. Connect přeruší probe a čeká na terminální stav. Confirmed commit zůstane zjistitelný po expiraci v retenci B01.
- **Lokální ověření:** deterministické bariérové testy dvou relací, limitu osmi, lease/sleep/resume, stale IDs, ztraceného acknowledgement, cancel bez operationId, cizího Connectu před a po commitu a restartu služby.

### B12: Konfigurační a editorové příkazy služby

- **Rozsah a očekávaný výsledek:** přesměrovat `set-config` do ConfigStore a implementovat jednorázové `get-config-state`, `get-config-document`, `get-default-config` a `create-default-config`. Connect a get-info vždy použijí čerstvý plně validní diskový snímek. Disconnect config nečte. Každá zvolená akce čte právě jednou a dál používá tento snímek.
- **Závislosti a paralelnost:** závisí na B04+B10 a schématech B09. Může běžet paralelně s B11 a B14 při rozděleném dispatcheru. B13 a B16 čekají na dokončení.
- **Relevantní soubory:** `vpn_engine.cpp`, service dispatcher, config příkazy, `service_host.cpp`, instalační a dev instalační skripty.
- **Akceptační kritéria:** splněno A01–A04, A37, A40 a A41 na službové hranici. Nevalidní JSON v platném UTF-8 je dostupný jen editorové obálce. Missing, unreadable, encoding_error a too_large se nezamění. Žádný periodický refresh ani create při startu tray, na timeru či Connectu. Startup služby používá jediný interní create-if-missing po bezpečné recovery.
- **Lokální ověření:** service-level testy všech availability stavů, přesného počtu čtení na akci, žádného čtení na Disconnect a souběhu create-if-missing s externím vznikem cíle.

### B13: Tray menu a nezávislá cesta Disconnectu

- **Rozsah a očekávaný výsledek:** implementovat čistý menu model v pořadí D01, ponechat položky kliknutelné a spouštět kontrolu až po akci. Oddělit diagnostickou čtečku od krátkého řízení tak, aby každý Disconnect byl skutečně odeslán, sledován do `disconnected/idle` nebo skončil konkrétní viditelnou chybou.
- **Závislosti a paralelnost:** závisí na B09+B10+B12. Může běžet paralelně s B11 a B14. Agent neupravuje Wizard Win32 okno, které patří do B15.
- **Relevantní soubory:** nové `client/apps/tray/menu_model.*`, `status_reader.*`, nový či upravený command reader, `main.cpp`, `test_tray_menu.cpp`, `test_tray_command_reader.cpp`.
- **Akceptační kritéria:** splněno A02–A04 a tray část A26. Start, timer a otevření menu nečtou disk. Show Info ani jiná dlouhá diagnostika neblokuje Disconnect. Pozdní diagnostika nepřepíše odpojování ani stav VPN.
- **Lokální ověření:** oba tray unit testy a skutečný pipe scénář Show Info zadržený při Disconnectu, dále pomalý config read, chyba odeslání a chyba potvrzení.

### B14: Přenosný WizardModel a výchozí Access Key režim

- **Rozsah a očekávaný výsledek:** implementovat čistý stavový model kroků, invalidaci důkazů, Back/Reset/Cancel/Finish prezentaci a lokální stav `useDefaultAccessKey`. Inicializace třetího kroku v novém/resetovaném konceptu nebo repair bez klíče aktivuje checkbox, zakáže pole a odvodí klíč z aktuální platné IP. Existující uložený klíč se načte jako vlastní hodnota s odškrtnutým checkboxem. Pouhý návrat Back/Next volbu nemění. Zaškrtnutí přepíše text defaultem a následné odškrtnutí jej ponechá, původní uloženou hodnotu neobnoví. Každá změna IP z libovolné modelové události při aktivním checkboxu přepočítá klíč.
- **Závislosti a paralelnost:** závisí na B02 a B00. Může běžet paralelně s B03–B13. Nemá Win32, IPC ani síť. B15 čeká na stabilní prezentační instrukce.
- **Relevantní soubory:** nové `wizard_model.*`, `wizard_types.h`, `wizard_defaults.*`, `test_wizard_model.cpp`, `test_wizard_defaults.cpp`.
- **Akceptační kritéria:** splněno A08, A12–A14, A23 a modelová část A38. Příklad oktetu 17 vytvoří `11` 32krát. Neplatná IP nezanechá starý default jako platný. Existující uložený klíč začne v editovatelném vlastním režimu. Zaškrtnutí jej okamžitě přepíše a další odškrtnutí ponechá vygenerovaný text bez obnovy původní hodnoty.
- **Lokální ověření:** tabulkové testy všech vstupů do identity kroku, edit/repair s existujícím klíčem včetně hodnoty shodné s defaultem, repair bez klíče, změn IP uživatelem i systémovým návrhem, Back/Next, Resetu, ruční editace, dvojího přepnutí a kolize pouze s vlastním lokálním contractem. Zvláštní test ověří, že stejná IP na jiném klientském počítači není validační chyba.

### B15: Win32 Configuration Wizard a asynchronní reader

- **Rozsah a očekávaný výsledek:** vytvořit Wizard okno, layout, ovládání, polling operace, keepalive, konečný termín Next, korelaci všech ID, odpojovací bránu a začátečnické texty. Třetí krok vykreslí checkbox před Access Key a propojí enable/disable i okamžité přepočty výhradně přes WizardModel.
- **Závislosti a paralelnost:** závisí na B11+B13+B14. Může běžet paralelně s B16, pokud jsou předem rozděleny `main.cpp`, `resource.h` a build soubory.
- **Relevantní soubory:** nové `wizard_window.*`, `wizard_reader.*`, `wizard_layout.*`, `window_utils.h`, `resource.h`, tray `main.cpp`, `test_wizard_reader.cpp`, `test_wizard_layout.cpp`.
- **Akceptační kritéria:** splněno A08–A19, A23–A25, A31–A33 a A38 v UI. Pole a tlačítka jsou přístupná klávesnicí. Chyba drží texty a fokusuje první pole. Pozdní výsledek ani stará generace UI nezmění. Cancel/Reset bezpečně ruší a neukládá.
- **Lokální ověření:** reader a layout testy, ruční DPI 100/125/150/200 %, malé plochy, dlouhé texty, Enter/Escape/Tab, změna IP oběma zdroji a vizuální ověření checkboxu před polem. Zvlášť otevřít existující config s vlastním klíčem, zaškrtnout default a znovu odškrtnout, přičemž se původní text nesmí obnovit.

### B16: Edit Config a Communication Contracts placeholder

- **Rozsah a očekávaný výsledek:** rozšířit editor o skutečný UTF-8 text, create Yes/No, Reset Yes/No a tři stabilní tlačítka. Přidat jediné zavíratelné prázdné Contracts okno. Menu Contracts kontroluje config jednou po kliknutí, automatické otevření po confirmed Finish další diskovou kontrolu nedělá.
- **Závislosti a paralelnost:** závisí na B12+B13. Může běžet paralelně s B15 při rozdělení sdílených tray souborů.
- **Relevantní soubory:** existující Edit Config okno a layout, nové `contracts_window.*`, tray resources/main, `test_config_editor.cpp` a UI layout regrese.
- **Akceptační kritéria:** splněno A03, A04, A31, A32 a A37. Vadné UTF-8, jiná kódování a nadlimitní text neotevřou náhradu. Cancel po vytvoření soubor nemaže. Placeholder nic needituje a nevyvolá Connect.
- **Lokální ověření:** unit test editorového toku a ruční resize ze všech hran, oba scrollbary, DPI, původní artefakt vedle Cancel, souběžný vznik configu a přesně jedno otevření placeholderu po Finish.

### B17: Build wiring a úplná automatická unit sada

- **Rozsah a očekávaný výsledek:** po sloučení zdrojových bloků doplnit všechny CMake seznamy, opravit chybějící contract target, odstranit dočasné harnessy nebo je převést na cílené testy a zajistit, že každý blok má skutečně spustitelný target. Neprovádět nové produktové refaktoringy.
- **Závislosti a paralelnost:** závisí na B02–B16 v rozsahu existujících zdrojů. Je sekvenčním integračním blokem, protože vlastní sdílené CMake soubory. B18 a B19 čekají na jeho dokončení.
- **Relevantní soubory:** `client/src/CMakeLists.txt`, `client/apps/CMakeLists.txt`, `client/tests/CMakeLists.txt` a všechny testy z 13.1.
- **Akceptační kritéria:** každý nový zdroj je v právě jednom správném targetu. Volitelný contract target má existující zdroj. Debug, Release a ASan se nakonfigurují a sestaví bez ručního doplňování souborů.
- **Lokální ověření:** `cmake --preset`, `cmake --build` a `ctest` pro Debug, Release a ASan podle 13.3. Výpadek ARM64 toolchainu se pravdivě zaznamená, nikoli vydá za úspěch.

### B18: Skutečný serverový kontrakt a oddělené integrační fixture

- **Rozsah a očekávaný výsledek:** vytvořit reprodukovatelný defaultní relay profil `.2–.15`, dokončit `test_server_contract.cpp` a Python integrační scénáře pro URL, secret, clients a spotřebování čekajících zpráv diagnostickým pollem. Existující deployment fixture s vlastním secretem zůstane oddělený.
- **Závislosti a paralelnost:** závisí na B05+B08+B11+B17. Může paralelně připravit PHP/Python fixture před dokončením B11, ale C++ end-to-end test se sloučí až po stabilním IPC a službě.
- **Relevantní soubory:** `client/tests/contract/test_server_contract.cpp`, `tests-integration`, `tools/deploy-test.ps1`, `tests-integration/README.md`.
- **Akceptační kritéria:** defaultní klienti `.2–.15` se autentizují. Diagnostický poll prokáže spotřebu a zahození bez tunelu. Běžný bootstrap se stejnými daty stále provede payload GCM. Fixture nepoužívá produkční ProgramData ani reálný server.
- **Lokální ověření:** contract target proti skutečnému PHP relay, `php tests.php` a `uv run ... pytest tests-integration -v -m "not slow"` podle 13.3.

### B19: Komplexní integrační a regresní testy celé implementace

- **Rozsah a očekávaný výsledek:** provést úplnou společnou bránu všech požadavků, nikoli doplňovat nové funkce. Zahrnout Debug/Release/ASan, PHP/Python, skutečný WinHTTP a pipe, čistou VM instalaci, dva interaktivní uživatele, Wizard create/edit/repair/reset, oba způsoby vytvoření configu, cizí Connect, souběžné Save/Finish, deadline/cancel, lease, receipts, DPI/resize, Wintun lifecycle a bezpečnostní regrese. Výsledek zaznamenat do nového `client/tests/implementation-plan3-report.md` s přesným RUN/NOT RUN.
- **Závislosti a paralelnost:** závisí na B00–B18. Jednotlivé testovací platformy a build konfigurace mohou běžet paralelně, ale opravy nalezených vad se vracejí vlastníkovi odpovídajícího bloku a celkový verdikt vydává jeden integrační vlastník.
- **Relevantní soubory:** všechny soubory a testy kapitoly 13, `client/tests/manual/README.md`, instalační balíček `dist`, nový implementační report.
- **Akceptační kritéria:** všechny A01–A41 jsou buď doloženě PASS, nebo je vydání blokované konkrétním nálezem. Zvlášť se prokáže D27 pro nový/resetovaný koncept, repair bez klíče, existující vlastní klíč, přepnutí vlastní → default → vlastní bez obnovy původní hodnoty a všechny zdroje změny IP. D28 se prokáže v dokumentaci, UI i validačním chování. Normální VPN GCM, čítače, status, původních sedm IPC příkazů, Wintun ownership a omezení automatického config create výhradně na bezpečný startup služby zůstanou bez regrese.
- **Lokální ověření:** příkazy 13.3, všechny sady 13.1 a 13.2, ruční scénáře 13.4 a Windows lifecycle testy z `client.md`. Balíček se sestaví až po průchodu relevantních bran. Existující plan2 report se nesmí vydávat za výsledek této etapy.

## 13. Testovací strategie

### 13.1 Nové automatické sady

| Soubor | Smysluplné scénáře |
|---|---|
| `client/tests/unit/test_wizard_document.cpp` | všechny stavy 5.1 včetně samostatné chyby kódování/velikosti, více současných chyb, zachování nezobrazených hodnot a absence polí, rozlišení existujícího platného `accessKey` od chybějícího pole pro inicializaci modelu, validní vícebajtové UTF-8, chybný pokračovací bajt, zkrácené/overlong sekvence, surrogate a hodnota mimo Unicode, UTF-16/32 BOM a Windows-1250 s neplatnými UTF-8 bajty, počáteční/opakovaný/BOM mimo začátek a zachování U+FEFF uvnitř stringu podle 5.1, duplicitní JSON, žádný zápis Wizardu před Finish. |
| `client/tests/unit/test_config_store.cpp` | dva starší koncepty a poslední úspěšná publikace, editor → Finish i Finish → starý editor, dva editory/Wizardy, zachování důkazů po cizím uložení, ruční změna stejné délky/mtime a delete/recreate bez revizního odmítnutí, selhání zápisu/flush/publish, cache až po commitu. |
| `client/tests/unit/test_config_editor.cpp` | přesný UTF-8 text včetně syntakticky chybného JSON a vícebajtových znaků, pouze normalizace počátečního BOM, Save v UTF-8 bez BOM a Cancel beze změny disku, odmítnutí chybného kódování bez náhradních znaků/odhadu/base64, odlišné chyby přístupu a velikosti, žádná stará cache, unreadable není missing, vytvoření Yes/No a okamžitý zápis, souběžný vznik cíle bez přepsání, Cancel nemaže vytvořený soubor, Reset Yes/No/Cancel mění pouze editor, totožná validní šablona a úplná validace Save. |
| `client/tests/unit/test_config_files_win.cpp` | skutečné dočasné soubory, hranice 512 KiB včetně BOM a růst během čtení, nejvýše limit + 1 načtených bajtů a žádný úplný hash/parse nadlimitního souboru, ACL, unikátní temp názvy, locked/unreadable cíle, reparse odmítnutí, externí writer v publikačním okně, recovery a invariant starý/nový úplný soubor. |
| `client/tests/unit/test_wizard_defaults.cpp` | výběr chybějící/nejstarší/shodné IP, prázdné clients, všech 14 přítomných, budoucí lastSeen, nevhodný CIDR, přesné vektory `02`, `0a`, `0f` a `11` opakované 32krát, vždy dva malé hex znaky a numerické pořadí `.2` proti `.10`. Návrh IP se nesmí vydávat za rezervaci ani odmítat jen proto, že jiný klientský počítač může používat stejnou IP. |
| `client/tests/unit/test_wizard_model.cpp` | všechny přechody, bez Back na prvním kroku, invalidace závislostí, Reset, Cancel, chyby bez postupu, cizí Connect, rychlý Connect/Disconnect, Finish unknown a stale výsledky. Nový/resetovaný koncept a repair bez klíče začínají s **Use default access key** zaškrtnutým a zakázaným polem. Edit/repair s existujícím klíčem, včetně textu shodného s defaultem, jej zobrazí s odškrtnutým checkboxem. Zaškrtnutí vlastní text přepíše, další odškrtnutí ponechá vygenerovaný text a původní neobnoví. Back/Next bez Resetu volbu zachová, každá uživatelská i systémová změna platné IP při aktivním checkboxu přegeneruje klíč a neúplná IP nesmí ponechat starý default jako platný. |
| `client/tests/unit/test_wizard_probe.cpp` | health syntax bez přijetí času, chybný tag, requestId, clients, GCM info, timeout každé části, 401/503, poškozené NDJSON/MAC, nesprávná korelace či směrování, překročené velikostní/početní limity, nenulové acceptedRecords/acceptedBytes či neprázdné accepted/deferred, maximálně jeden prázdný data request. Platné data-mac s neověřitelným payload GCM dovolí pouze zahození v diagnostice, žádné uložení/předání/použití nebo změnu odchozí fronty a provozního stavu. |
| `client/tests/unit/test_wizard_service.cpp` | chybějící předpoklady, podstrčené vstupy při Finish, session/epoch mismatch, explicitní resume, cancel/preemption, limit relací, monotónní pronájem a bezpečný úklid, odmítnutí opakovaného pořadí pokusu bez historie, Reset/Connect pořadí nemažou, oddělená idempotence commitId a dostupný receipt po expiraci. |
| `client/tests/unit/test_wizard_ipc.cpp` | úplná schémata obou směrů, neznámá/duplicitní pole, typy, UUID, integer overflow, escape overhead 4MiB, korelace výsledků a filtr citlivých údajů. |
| `client/tests/unit/test_tray_menu.cpp` | přesné pořadí a separátory, kliknutelnost konfiguračních položek ve všech stavech 5.4, nulové konfigurační IPC/čtení při startu tray, timeru a otevření menu včetně více relací, jediná autoritativní kontrola zvolené akce bez předběžné kontroly, externí změna zachycená při příští akci, Disconnect podle paměti bez čtení disku, chyba/návod až po kliknutí, neznámý stav není missing, po Save/Finish žádný automatický refresh. |
| `client/tests/unit/test_tray_command_reader.cpp` | čekající Show Info i jiná dlouhá diagnostika neobsadí odesílání Disconnectu, služba request skutečně přijme a potvrdí dokončení před dokončením diagnostiky. Obsazený CommandReader nesmí kliknutí zahodit, chyba odeslání nebo potvrzení je viditelná, pozdní diagnostika nepřepíše stav VPN. |
| `client/tests/unit/test_wizard_layout.cpp` | velikosti monitorů, DPI 100/125/150/200 %, dlouhé texty, stabilní dolní tlačítka, žádné překryvy, scroll obsahové části. |
| `client/tests/unit/test_winhttp_lifecycle.cpp` | řízený souběh cancel/deadline/success/callback, opakované rušení, Connect proti probe, globální Disconnect, stop služby a všechny způsoby zrušení Wizardu, žádné použití zavřeného handle ani dvojí dokončení, transport znovu použitelný. |
| `client/tests/unit/test_winhttp_deadline.cpp` | pomalé DNS/spojení/TLS, zdržení hlaviček, pomalé bloky těla, tělo bez konce, absolutní deadline bez obnovy, chyby nastavení timeoutů, hraniční sekundové hodnoty a součty, korektní timeout i částečné účetnictví. |
| `client/tests/unit/test_ipc_availability.cpp` | pomalý get-info souběžně s VPN daty, nejvýše jedna diagnostika, neúplné IPC requesty a neodebírané odpovědi bez obsazení krátkého řízení, limity a přetížení, měřené get-status, přijetí i dokončení Disconnectu a stop nezávislý na pipe. |
| `client/tests/unit/test_wizard_reader.cpp` | ztracené start acknowledgement i finální výsledky, timeout ve všech úsecích řetězce, žádný automatický resend, červený stejný krok a zachované hodnoty, pozdní úspěch po timeoutu a během dalšího Next, polling a keepalive neprodlužují termín, samostatné řešení Finish unknown. |
| `client/tests/contract/test_server_contract.cpp` | skutečný PHP relay: URL, secret, info.clients, `.2–.15`, polling s čekajícími zprávami, potvrzení jejich spotřeby bez tunelu a default kompatibilita. |

Testy modelů nemají jen zrcadlit implementaci. Použít tabulkové scénáře odvozené z D01–D28 a explicitní invarianty. Kryptografické vektory porovnat s nezávislým PHP/Python výpočtem, ne s druhým voláním stejné C++ funkce. Pro souběh používat bariéry a kontrolované dokončení, nikoli náhodné čekání a naději, že se race projeví. Transportní a IPC garance kromě řízených jednotkových testů ověřit přes skutečný Windows transport a pipe. Pro DNS/TLS a přetížení popsat reprodukovatelný fixture a měřené limity, samotný mock není důkazem platformní životnosti.

### 13.2 Povinné regrese

- Startup služby vytváří pouze skutečně chybějící default přes `ensureDefaultAtStartup`, nikoli přes starou samostatnou zapisovací cestu. Engine start a Connect nic nevytvářejí. Ověřit společnou šablonu startup/create/Reset editoru a oddělený koncept Wizardu, jeho preference a aditivní kontrakty.
- Rozšířit `test_crypto.cpp`, `tests.php` a `test_crypto_vectors.py`: empty → nový default, `0`, 32/64 ASCII nul, NUL bajty a neprázdný custom secret beze změny. Ověřit všechny relevantní odvozené klíče a veřejné varování.
- Zachovat `test_runtime_status.cpp`, `test_status_contract.cpp`, `test_traffic_counters.cpp` a `test_winhttp_accounting.cpp`. Před a po každém probe porovnat všechny globální kontrolní body, čítače a aktivní stav. Výslovné disconnect a cizí Connect mají své původní legitimní resetovací účinky, nesmějí se zaměnit za vedlejší efekt probe.
- Ověřit, že normální bootstrap, recovery a provozní data polling stále po data-mac ověřují payload GCM, odmítnou neplatný GCM a platné payloady zpracují podle běžných pravidel. Jednorázový diagnostický režim zahození se nesmí stát defaultem společného parseru nebo NetworkWorker.
- Ověřit opakovaný health s existujícím TimeSnapshot proti skutečnému serveru, který přijímá pouze auth-none.
- Ověřit původních sedm IPC příkazů, přesný formát get-info/get-config, nezměněný status parser a intervaly paměťového get-status v tray/progress. Původní 30s timeout Show Info zůstává. Start tray, opakované otevření menu, běh timerů a Save/Finish nevyvolají get-config-state ani čtení souboru. Každá zvolená konfigurační akce provede právě jednu autoritativní kontrolu a použije její snímek.
- Přes skutečnou tray řídicí cestu a Windows pipe spustit Show Info se zadrženou odpovědí, následně zvolit Disconnect a ověřit jeho přijetí službou i dokončené disconnected/idle, zatímco diagnostika stále čeká. Zopakovat pro jinou dlouhou diagnostiku, nečitelný a nadlimitní config. Samotné zpracování kliknutí nebo mock startCommand není dostatečný důkaz. Vynutit selhání odeslání a potvrzení, obojí musí skončit konkrétní viditelnou chybou.
- Ověřit jednorázové omezené čtení na hranici 512 KiB, nad limitem a při růstu souboru, odmítnutí před úplným hashováním/parsováním a dostupnost get-status/Disconnect/stop během pomalé souborové obsluhy. Zopakovat poslední úspěšný Save/Finish po externí změně, chybném kódování nebo nadlimitním cíli, obsahová chyba cíle ani stará revize nesmějí blokovat bezpečnou publikaci validního dokumentu v limitu.
- Pro data probe s platným MAC a payloadem, který nelze dešifrovat neexistujícím kontraktem, očekávat úspěšné ověření přístupu a zahození. Pro chybné MAC očekávat neúspěch bez jakéhokoli doručení.
- Simulovat změnu systémových hodin, dlouhé vyplňování, návrat Back a jiný server. Čas ani ověřené clients se nesmějí přenášet mezi odlišnými důkazy.
- Pro všechny Wizard síťové kroky vynutit 401, timeout HTTP i IPC a chybu zpracování/doručení. Bez nového Next neproběhne žádné opakování. Další ruční Next smí znovu odebrat serverová data, pozdní odpověď předchozího pokusu nesmí změnit jeho stav.
- Pronájem ověřit při pádu tray, vyplňování bez síťových kontrol, opožděném keepalive, dvou nezávislých relacích, expiraci během probe i Finish, změně wall clock a sleep/resume. Úklid je omezený, potvrzení commitu se neztratí a globální VPN pokračuje.

### 13.3 Spuštění při budoucí implementaci

Z MSVC developer shellu v `C:\git\phpvpn\client`:

```powershell
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
cmake --preset release
cmake --build --preset release
ctest --preset release
cmake --preset asan
cmake --build --preset asan
ctest --preset asan
```

Z kořene projektu, s dostupným PHP CLI a odděleným testovacím relay:

```powershell
php tests.php
uv run --python 3.13 --with pytest --with cryptography --with requests -- pytest tests-integration -v -m "not slow"
```

Integrační deployment připravit explicitně pro testovací instanci podle aktualizovaného README. Neukládat testovací Wizard konfiguraci do reálného ProgramData během unit testů. Testy file backendu pracují v unikátním dočasném adresáři. Testy potřebující elevaci, VM nebo více relací mají tuto podmínku výslovně označenou.

ARM64 nepovažovat za ověřené jen proto, že existuje preset. Dřívější report uvádí chybějící ARM64 toolchain. Pokud je při implementaci stále nedostupný, uvést omezení v reportu. Pro Python nic neinstalovat přes pip.

### 13.4 Ruční scénáře

Čistá instalace a start služby bez souboru. Všechny konfiguračně závislé položky jsou kliknutelné, menu ani timer nečtou konfiguraci. Až po kliknutí Connect, Show Info a Statistics zobrazí návod, Contracts varování a Edit Config potvrzení vytvoření se skutečnou cestou i při nestandardním ProgramData. Yes vytvoří soubor před editací, No neotevře nový dokument, Cancel následně soubor nemaže. Otevřít syntakticky chybný JSON v platném UTF-8 i repairable, ověřit vícebajtové znaky, normalizaci počátečního BOM, Reset Yes/No a Cancel. Vadné UTF-8 a UTF-16/32 odmítnout chybou kódování bez editorového dokumentu, nadlimitní soubor chybou velikosti. Nečitelnost nesmí být zaměněna za missing ani chybu kódování. Ruční kopie configu se projeví při příští zvolené akci bez restartu služby.

Průchod novým, existujícím i resetovaným konceptem. Zrušení na každém kroku a během čekající kontroly. Zavření a znovuotevření při opožděné odpovědi. Selhání Finish bez zavření okna. Úspěšný Finish otevře placeholder právě jednou a nevytvoří tunel. Opakované otevření Wizardu nezahodí koncept.

Ve třetím kroku ověřit všechny inicializační větve Access Key. Nový/resetovaný koncept a repair bez klíče začínají v aktivním default režimu. Existující config s vlastním `accessKey`, včetně hodnoty textově shodné s vypočteným defaultem, otevře editovatelný textbox a odškrtnutý checkbox. Zaškrtnutí okamžitě přepíše text defaultem pro aktuální IP. Následné odškrtnutí ponechá vygenerovaný text a neobnoví původní klíč. Při zaškrtnutí samostatně změnit IP psaním i systémovým návrhem a ověřit okamžitou regeneraci.

Dva interaktivní uživatelé, jedna služba. Cizí Connect v každém kroku i těsně před Finish, rychlý Connect/Disconnect, současný Save editoru a Finish druhé relace. Viditelná informace o cizím Connect a žádné automatické opětovné připojení. Zmizelý/poškozený config při connected zachová provoz a dostupné Disconnect.

Ověřit obě pořadí posledního úspěšného uložení včetně editoru otevřeného před Wizardem a Wizardu otevřeného před Save. Žádné revizní potvrzení přepsání ani vynucené nové otevření. Pomalá diagnostika během provozu nesmí zastavit polling VPN. Při čekajícím Show Info zvolit Disconnect a doložit jeho skutečné přijetí službou a dokončení ještě během čekající diagnostiky, totéž s jinou dlouhou diagnostikou a s chybou IPC pro viditelnou zpětnou vazbu. Ztratit IPC odpověď po úspěšném data probe, ověřit červený krok a až dalším Next nový pokus. Ukončit tray bez close, ověřit úklid po vypršení pronájmu, nezávislost druhé relace, sleep/resume a Finish s nejasným potvrzením.

Změny velikosti ze všech hran a rohů, malé pracovní plochy, taskbar na různých stranách, DPI 100/125/150/200 %, přesun mezi monitory, tabulátor, čitelnost vysvětlení a návrat fokusu po chybě. Stejná kontrola resize regresí editoru jako v plan2. Evidence výsledků obsahuje OS, build, DPI, scénář, výsledek a testera, žádné klíče ani screenshoty v repozitáři.

## 14. Přesná akceptační kritéria

| ID | Požadovaný pozorovatelný výsledek |
|---|---|
| A01 | Čistý start služby po bezpečné recovery vytvoří chybějící sdílený default. Existující soubor nezmění ani v případě vady. Služba poskytuje IPC při chybě úložiště, Connect nic nevytváří. |
| A02 | Menu má přesně pořadí D01. Konfiguračně závislé položky jsou kliknutelné i bez souboru, při jeho chybě nebo neznámém stavu. Start tray, timer a otevření menu nečtou konfiguraci ani neposílají get-config-state. Jediná autoritativní kontrola nastane až po zvolení akce. Bez souboru Connect/Show Info/Statistics zobrazí návod, Contracts varování bez okna, Edit Config nabídne vytvoření Yes/No a až po Yes a úspěšném výsledku otevře editor. |
| A03 | Varování Contracts obsahuje absolutní cestu získanou službou, instrukci Wizard → Finish a možnost zkopírovat existující config. Potvrzení editoru obsahuje stejnou skutečnou cestu, její účel a otázku na vytvoření ze šablony. Chybějící služba ani chyba přístupu nejsou missing. |
| A04 | Invalid/unreadable/encoding_error/too_large/repairable nezakazují položky menu. Až obsluha Connect, Show Info, Statistics nebo Contracts odmítne provedení s konkrétní chybou či návodem podle 5.4. Edit Config otevře syntakticky chybný JSON pouze jako platný UTF-8 text v limitu. Chyby přístupu, kódování a velikosti jsou odlišné a neotevřou náhradu. Běžící VPN pokračuje na aktivním snímku, otevřené Statistics používají paměťový stav a Disconnect nečte soubor. |
| A05 | Wizard nepíše konfiguraci, zálohu ani temp JSON před Finish. Cancel a Reset před Finish nezmění původní soubor bajtově. |
| A06 | Opravitelnost Wizardu je pouze absence IP/klíče při jinak platném dokumentu. Nesprávné typy nebo další chyby vyžadují Reset Wizardu nebo opravu platného UTF-8 textu v limitu přes Edit Config. Chybné kódování vyžaduje externí opravu nebo nový koncept Reset, nikoli náhradní dekódování. Úplný validátor nepřijme neúplnou konfiguraci pro Connect ani Save. |
| A07 | Edit/repair zachová nezobrazené hodnoty včetně omission a všechny původní kontrakty s přesnými klíči. Patchuje sedm fields a případně aditivně doplní pouze chybějící peery. Reset původní DOM vědomě nahradí až při Finish. |
| A08 | Odpojovací Next čeká na skutečné dokončení. Server URL nemá Back. Zrušení a dokončení Wizardu neobnoví VPN. |
| A09 | Krok URL uspěje na syntakticky platném running health i s nesouhlasným GCM tagem, ale nepublikuje čas ani kryptografické ověření. Chybný formát a requestId neprojdou. |
| A10 | Krok secret vyžaduje nový ověřený health, validní TimeSnapshot, ověřený a dešifrovaný info včetně použitelného clients. Jediná chyba ponechá uživatele v kroku. |
| A11 | Checkbox secretu načte, mění a ukládá příznak InWizard i konkrétní secret. Pouze při absenci používá přesnou shodu s defaultem. Explicitní false zachová přesné bajty včetně prázdné hodnoty. |
| A12 | Navržená IP je nejnižší chybějící kandidát, jinak nejstarší lastSeen s číselným tie-break. `clients` není prezentováno jako rezervace IP. Při prázdné kandidátní množině je vyžádána ruční IP. Dokumentace klienta i UI výslovně uvádějí, že různé klientské počítače smějí mít stejnou virtuální IP, phpvpn jedinečnost nevynucuje, systém kolizi připouští a její náprava je odpovědností provozovatele. Samostatně zůstává odmítnuta vlastní IP shodná s klíčem lokálního communication contractu. |
| A13 | Před Access Key je **Use default access key** inicializované z `useDefaultAccessKeyInWizard` s výše uvedeným fallbackem pouze při absenci. Při true je pole zakázané a obsahuje `hex2(lastOctet)` opakované 32krát, například 32×`11` pro `.17`. Při false zůstává uložená hodnota editovatelná i při shodě s defaultem. Každá skutečná změna IP při true klíč přepočítá, neplatná IP včetně adresy mimo CIDR jej vyprázdní. Před IP polem je **Automatically detect IP address** podle `autoDetectIpAddressInWizard`. Při true je IP zakázaná a čeká na ověřený návrh z poslední úspěšné kontroly secretu. Návrh nahradí i neprázdnou placeholder IP, při false zůstane ruční IP zachovaná. Bez návrhu Next identity nepokračuje, UI vysvětlí možnost vypnout automatiku a zadat IP nebo se vrátit ke kontrole secretu. Zapnutí použije pouze stále platný návrh, samo nespouští HTTP. Změna URL/secretu, Reset, Connect, expirace či resume návrh a příslušné důkazy zneplatní. Back/Next zachová volby, resume zachová rozpracované hodnoty a všechny volby, ale vyžádá nové kontroly. |
| A14 | Kolizní vlastní IP se odmítne před data kontrolou. Hláška vysvětlí příčinu a možnosti nápravy. Existující kontrakt zůstane beze změny. |
| A15 | Upozornění vysvětluje spotřebování zpráv i možnost serverového úspěchu bez doručeného výsledku. Jeden Next spustí jednu sekvenci bez automatického retry jakéhokoli HTTP nebo IPC startu, ani po 401. Obsahuje nejvýše jeden prázdný data request. Ztracená odpověď nebo jiná chyba ukončí pokus červeně na stejném kroku se zachováním hodnot. Nový ruční Next smí znovu odebrat zprávy, žádná historie pokusů nevynucuje znovuotevření Wizardu. |
| A16 | wizard-check-access používá obecnou jednorázovou diagnostickou výjimku, nejvýše jeden prázdný auth-full + LF data request a úplné ověření HTTP/NDJSON/korelace requestId/velikostních a početních limitů/směrování/nulového výsledku/závěrečného data-mac. Teprve potom zahodí všechny datové záznamy bez payload GCM a dešifrování. Žádný payload neuloží, nepředá, nezařadí do fronty ani jinak nepoužije. Výsledek neprokazuje autenticitu, integritu ani čitelnost plaintextu. Normální bootstrap, recovery a provozní data polling nadále vyžadují úplné payload GCM ověření a běžné zpracování platných dat. |
| A17 | Žádný probe nezmění aktivní VPN, bootstrap body, runtime čas, fronty ani devět provozních čítačů. Neotevře Wintun a nevytvoří route. |
| A18 | Ve všech čtyřech režimech je checkbox výchozí true. Sdílená kandidátní sada má bez self 13 nebo 14 adres, vkládá se jen její chybějící rozdíl. Původní payload klíče, nezávislé SHA-256 vektory a omission při false zůstávají zachované. Mimo podporovaný CIDR UI vysvětlí efektivní false. |
| A19 | Finish přijme jen poslední prokázané sedmipoložkové fields a kompletně validní výsledný JSON. Změna vstupu po úspěšném Next bez nového ověření přes IPC neprojde. |
| A20 | Poslední úspěšná publikace vyhrává pro editor → Finish, Finish → starý editor, dva editory i dva Wizardy. Každý uloží celý svůj validovaný dokument. Zastaralá revize, ruční obsahová změna ani běžný vznik/smazání cíle samy nevyvolají odmítnutí, potvrzení přepsání, sloučení, invalidaci důkazů ani nové otevření. Souborová bezpečnost zůstává povinná. Create-if-missing je zvláštní operace, která existující cíl nepřepíše. |
| A21 | Čtenář při běžné atomické publikaci uvidí celý původní nebo celý nový JSON. Simulované chyby zápisu/flush/replacement nezanechají truncation, veřejně čitelné temp tajemství ani falešný úspěch. Souborové brány B01, B03 a B04 jsou doložené. |
| A22 | Opakování commitId nepíše znovu a změněný vstup se odmítne. Ztracená odpověď se řeší receipt i po expiraci editace v definované retenční době. Expirace nevrátí publikovaný zápis, pozdější Save nezmění historický výsledek commitu. Limit registru se řeší před publikací. Restart služby zneplatní staré relace a nevede k automatickému opakování Finish. |
| A23 | Cizí Connect vyvolá informaci, pozastavení a invalidaci kontrol, i když se spojení mezi pollingy opět odpojilo. Uživatel může výslovně globálně odpojit a zopakovat kontroly. |
| A24 | Connect není blokovaný životností okna. Předchozí probe se skutečně ukončí před začátkem bootstrapu a nikdy nevzniknou paralelní data requesty. |
| A25 | Chyby rozlišují přístup k souboru, kódování, velikost, timeout, transport, autentizaci, špatný formát, tag/MAC, konflikt kontraktu, expiraci a zápis. Stará revize není chyba. Obsahují pouze povolené údaje, žádné klíče, requesty, ciphertext a tagy v UI chybách nebo logu. |
| A26 | get-status zůstává paměťový, konzistentní a bez citlivých dat. Během pomalého probe/get-info, omezeného čtení konfigurace a podporovaného zatížení IPC funguje krátké řízení v mezích stanovených B01. Tray během čekajícího Show Info či jiné dlouhé diagnostiky skutečně odešle Disconnect nezávislou cestou, služba jej přijme a dokončí. Obsazený CommandReader jej nesmí tiše zahodit, selhání odeslání nebo potvrzení je viditelné s konkrétní chybou. Asynchronní čekání na neúplné requesty a neodebírané odpovědi nedrží kapacitu pro příkazy. Změřit get-status, přijetí i dokončení Disconnectu vyvolaného v tray a nezávislý stop služby, při přetížení prostředky zůstanou omezené. |
| A27 | Uzavřené schéma používá sedm povinných fields a omezené existingDefaultContractPeers. Typy, duplicity, limity, pořadí, historická schémata a vazba všech fields na důkazy/receipt jsou testované. Původních sedm příkazů se nemění. |
| A28 | Nová tray se starou službou pravdivě oznámí chybějící podporu Wizardu. Nevytvoří config fallbackem ani nezkouší jinou pipe nebo starý secret. |
| A29 | Default je konzistentní v PHP, C++, Python testovacím klientu a normativních dokumentech. Veřejná stránka varuje i při novém defaultu a prázdném secretu, endpointy zůstávají funkční. |
| A30 | Výchozí server autentizuje všechny `.2–.15` s odpovídajícími klíči. Testovací deploy skript stále vytvoří přesně svůj deklarovaný profil a odmítne neočekávanou hlavičku. |
| A31 | Úspěšný Finish zavře Wizard a otevře placeholder právě jednou. Okno je prázdné, zavíratelné a neprovádí úpravy kontraktů. Samo nezpůsobí Connect. |
| A32 | UI je čitelné a ovladatelné na malé ploše a všech testovaných DPI, bez ořezání tlačítek a resize artefaktů. Čisté unit testy nenahrazují doloženou ruční kontrolu. |
| A33 | Normální close vyžádá okamžitý úklid. Pád/zamrznutí tray, odhlášení nebo výpadek IPC ukončí relaci po 60 s bez platného kontaktu plus definovaná tolerance časovače a konečná doba bezpečného rušení. Vyplňování udržuje relaci 15s keepalive bez HTTP, wall clock nemění lhůtu, sleep/resume a pozdní kontakt expirovanou relaci neoživí. Jiná relace i globální VPN pokračují, commit má samostatné potvrzení. |
| A34 | Všechny rušicí události D19 i souběh cancel/timeout/success/callback skončí v krátké konečné době, bez dvojího zavření, use-after-free, deadlocku a paralelních data requestů. Před nahrazením nebo uvolněním je potvrzen terminální stav a transport lze znovu použít. |
| A35 | Jediný monotónní HTTP deadline zahrne DNS, spojení, TLS, odeslání, hlavičky a celé tělo. Pomalé dílčí bloky ani neukončené tělo jej neposunou. Testy hraničních hodnot a chyby nastavení timeoutů nevytvoří záporný/nekonečný timeout. Po bezpečném úklidu se vrátí timeout a transport i síťová obsluha zůstanou použitelné. |
| A36 | Pomalý diagnostický health/info běží s vlastním transportem a časem mimo hlavní VPN síťové vlákno a nezastaví datový polling. Druhý get-info dostane busy, nevznikne neomezená fronta nebo počet vláken. Tři VPN role, fronty a provozní čítače zůstávají oddělené. |
| A37 | Edit Config po Yes vytvoří validní soubor ihned, pozdější Cancel jej nemaže. No nic nevytvoří ani neotevře nový dokument. Cíl vzniklý během potvrzení se nepřepíše, editor načte skutečný obsah nebo konkrétní chybu. Reset Yes mění jen editor, No zachová koncept a Cancel zahodí neuložené změny. Startup, explicitní vytvoření a Reset editoru mají stejnou šablonu 5.6 s jedním kontraktem a třemi true InWizard, Save i vytvoření projdou úplným validátorem. |
| A38 | Tray zobrazí zelený krok a pokračuje jen po doručení ověřeného úspěchu aktuálního Next před konečným termínem. Polling ani keepalive termín neobnoví. Pozdní úspěch po chybě/timeoutu a během nového pokusu UI nezmění. Nový pokus počká na bezpečné ukončení předchozí práce nebo dostane busy. Opakované či nižší attemptSequence nevykoná síť ani po Resetu/Connectu. |
| A39 | Normativní dokumenty před implementací výslovně obsahují změny matice kapitoly 11. protocol.md včetně 9.5 popisuje úzkou obecnou diagnostickou výjimku bez tray, Wizardu, IPC a UI. Její konkrétní vazba na wizard-check-access/ProbeClient/Next patří pouze do client.md a tohoto plánu. Nezůstane rozporná obecná povinnost payload GCM pro tuto diagnostiku ani oslabení GCM pro normální bootstrap/recovery/provoz. Dokumenty zachovají oddělenou diagnostiku, ruční nové pokusy a startup vytvoření a obě explicitní cesty vytvoření souboru, vyžadují kontrolu konfigurace až po akci a editor pouze pro platný UTF-8 text v limitu. Výsledné schéma IPC, meze a testy odpovídají implementaci. |
| A40 | Konfigurační backend nikdy nenačte více než 512 KiB + jeden detekční bajt ani při růstu souboru. Nadlimitní soubor odmítne před úplným hashováním/parsováním jako config_too_large, Save/Finish/create-if-missing vyžadují výsledný dokument v limitu a zakódované IPC v 4 MiB. Jednorázové kontroly po akcích nepřidávají periodický/menu ani automatický refresh po uložení. Revize ani obsah mezitím změněného cíle neblokují bezpečnou publikaci podle D22. |
| A41 | Platné vícebajtové UTF-8 se přenese beze změny znaků. Vadné pokračovací bajty, zkrácené/overlong sekvence, surrogate, hodnoty mimo Unicode a nepodporované kódování jsou config_encoding_error, odlišné od config_unreadable. Žádná náhrada znaků, odhad kódování nebo base64 fallback. Jediná normalizace odstraní jeden UTF-8 BOM na offsetu 0, jinde se BOM nepřijímá jako značka kódování ani neodstraňuje podle 5.1. Save zapisuje UTF-8 bez BOM, otevření/Cancel původní disk nemění. Edit Config odmítne neplatné UTF-8 i při filesystemově čitelném souboru. |

## 15. Dokončení a praktická omezení

Produktová rozhodnutí D01–D28 jsou vyřešená. Technické ověření Windows publikace, transportní životnosti, absolutních timeoutů, dostupnosti IPC, pronájmů a crash scénářů je povinná součást implementace, nikoli deklarovaný hotový výsledek tohoto plánu. Bez splnění příslušných bloků a jejich lokálních bran v kapitole 12 nelze tyto vlastnosti označit za dokončené. Ochrana před přepsáním zastaralého obsahu není požadavkem, rozhoduje poslední úspěšná publikace.

Výstup implementace musí obsahovat aktualizované normativní dokumenty, zdroje, testy a report s přesnými provedenými/neprovedenými kontrolami. Commit, nasazení, restart instalované služby ani změna skutečného ProgramData nejsou součástí přípravy tohoto plánu.

Při přípravě a aktualizaci plánu byly prostudovány normativní dokumenty, rozhodnutí z revize, relevantní zdrojový kód a testovací infrastruktura. SHA-256 příklady jsou podkladem testů. Nebyla provedena implementace, překlad, test nové funkce ani vizuální validace neexistujícího Wizardu. Tato aktualizace mění pouze `plan3_client.md`.
