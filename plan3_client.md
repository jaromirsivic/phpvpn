# Implementační plán 3: Configuration Wizard a Communication Contracts

Stav: plán realizace, bez provedené implementace. Zpracováno 5. září 2026 nad revizí `9f25f1333834596e46394f67f0a056a2595506da` v `C:\git\phpvpn`. Projekt zatím není nikde nasazený. Tento dokument zahrnuje rozhodnutí zadavatele z navazujícího rozhovoru a nahrazuje rozporné formulace původního zadání v rozsahu této etapy.

Výchozí podklady jsou `protocol.md`, `client.md`, skutečný zdrojový kód, existující testy a konvence z `plan_client.md` a `plan2_client.md`. Cesty souborů níže jsou relativní ke kořeni `C:\git\phpvpn`. Při realizaci se nejprve společně aktualizují normativní dokumenty podle kapitoly 11. Historické poznámky se zpětně nepřepisují.

## 1. Výsledek a hranice etapy

Tray dostane popisný čtyřkrokový průvodce s podmíněným odpojovacím krokem. Služba vlastní načtení a uložení konfigurace i všechny síťové kontroly. Průvodce smí vytvořit nebo změnit `config.json` až při úspěšném Finish. Po úspěchu se zavře a otevře zavíratelné prázdné okno Communication Contracts. VPN se tím nepřipojí.

Součástí jsou změny serverového defaultu, rozšíření výchozí mapy přístupových klíčů, IPC, správy konfiguračního souboru, bezpečného rušení a celkových HTTP timeoutů, Edit Config, testů a dokumentace. Edit Config může po výslovném potvrzení vytvořit chybějící soubor ze společné platné šablony již před otevřením editoru. Jde o samostatnou cestu vedle Finish Wizardu. Není součástí editor komunikačních kontraktů, automatická distribuce klíčů, rezervace IP na serveru, nové drátové endpointy, automatické připojení, implementace macOS/Linux ani změna přijatého modelu veřejného lokálního IPC.

Implementace zůstává C++20, MSVC, CMake/Ninja, Win32, WinHTTP a CNG bez nové knihovny třetí strany. Přenositelné modely nesmějí obsahovat `HWND`, `HANDLE`, Win32 zprávy ani závislost na tray procesu. Pythonové závislosti a běhy se spravují výhradně přes `uv`.

## 2. Závazná rozhodnutí zadavatele

| ID | Rozhodnutí |
|---|---|
| D01 | Přesné menu: Connect/Disconnect, oddělovač, Configuration Wizard, Communication Contracts, Edit Config, oddělovač, Show Info, Statistics, oddělovač, Exit. |
| D02 | Bez souboru zůstávají aktivní Wizard, Communication Contracts, Edit Config a Exit. Contracts zobrazí varování, Edit Config nabídne Ano/Ne pro vytvoření výchozího souboru a po Ano jej vytvoří služba a otevře editor. |
| D03 | Varování uvede skutečně vyhodnocenou absolutní očekávanou cestu a možnost spustit Wizard nebo ručně zkopírovat konfiguraci získanou například od administrátora. Doslovný nerozbalený text `%ProgramData%` nestačí. |
| D04 | Instalace, start služby ani Connect nevytvářejí výchozí konfigurační soubor. |
| D05 | Opravitelný soubor pro Wizard je jinak platná konfigurace, které chybí pouze `virtualIp`, `accessKey` nebo obě pole. Ostatní neplatnosti vyžadují ve Wizardu Reset. Každý čitelný soubor, včetně syntakticky chybného JSON, však lze otevřít a opravit přes Edit Config. Nečitelnost je samostatná chyba přístupu. |
| D06 | Reset Wizardu zahodí celý koncept a přejde na nové výchozí nastavení, včetně TLS, CIDR, limitů a původních kontraktů. Disk se nemění. Zastaralá uložená revize neblokuje pozdější Finish. Reset editoru má vlastní pravidla D24. |
| D07 | Běžná editace platné nebo opravitelné konfigurace mění pouze URL, `serverSecret`, `virtualIp` a `accessKey`. Ostatní hodnoty a kontrakty se zachovají. |
| D08 | Na nultý krok se nelze vracet. Server URL nikdy nemá Back, ani po dokončeném odpojení. |
| D09 | Ověřovací polling předem upozorní na možné nenávratné odebrání čekajících zpráv. Ověří strukturu a autentizační MAC celé odpovědi, zprávy zahodí bez dešifrování, uchovávání a doručení do tunelu. |
| D10 | Cizí Connect průvodce pozastaví. Uživatel dostane informaci, že někdo provedl Connect. Dosavadní síťové kontroly se zneplatní a nabídne se nové výslovné globální odpojení. Connect není blokován po celou dobu otevřeného průvodce. |
| D11 | Výchozí kontrakty lze vytvářet pouze pro kanonické `vpnCidr` tvaru `10.A.B.0/24`. Peery jsou adresy `10.A.B.2` až `10.A.B.15`, kromě vlastní IP. Vznikne 13 kontraktů při vlastní IP v tomto rozsahu, jinak 14. |
| D12 | Pro jiné CIDR se výchozí kontrakty nevytvoří. Wizard přímo vysvětlí, že důvodem je `vpnCidr`. Toto pravidlo nemaže již existující kontrakty. |
| D13 | Změna vlastní IP na adresu existujícího kontraktu se odmítne. Hláška vysvětlí konflikt a navrhne jinou vlastní IP nebo opravu kontraktu přes Edit Config před novým průchodem. Nic se automaticky neodstraní. |
| D14 | Chyby průvodce mohou zobrazit URL, pevnou cestu ke konfiguraci, HTTP status a konkrétní příčinu. Nevypisují klíče, celé requesty, ciphertexty ani autentizační tagy. |
| D15 | Serverová výchozí mapa `ACCESS_KEYS` se rozšíří z `.2–.4` na `10.99.99.2–.15`. Každý klíč je hexadecimální poslední oktet opakovaný 32krát. |
| D16 | Nový default je přesně `CHANGE_THIS_SERVER_SECRET`. Pouze nulová délka se normalizuje na tento default. Každý neprázdný secret, včetně 32 nebo 64 ASCII nul, zůstane doslova zachován. |
| D17 | Nezavádí se automatická migrace ani zkoušení historických secretů. Nové příkazy rozšíří IPC v1, původních sedm příkazů se zachová. Wizard potřebuje novou službu. |
| D18 | Pokud soubor zmizí nebo se poškodí za běhu VPN, aktivní spojení pokračuje s již načteným snímkem. Disconnect zůstane dostupné. Nový Connect, Show Info a Statistics jsou zakázané. |
| D19 | Bezpečné rušení HTTP je společné pro probe přerušený Connectem, globální Disconnect, stop služby i Cancel/Reset/Escape/X Wizardu. Má krátkou konečnou dobu a potvrzený terminální stav před nahrazením requestu nebo uvolněním jeho prostředků. |
| D20 | Každý HTTP request má jediný absolutní monotónní deadline zahrnující DNS, spojení, TLS, odeslání, hlavičky i celé tělo. Dílčí přenos jej neobnovuje. Timeout využije bezpečné rušení D19. |
| D21 | `get-info` zůstane mimo hlavní VPN síťové vlákno, s vlastním dočasným transportem a kontextem. Nejvýše jedna diagnostika běží současně. Wizard může sdílet tuto oddělenou řídicí cestu. Tři VPN pracovní role se nemění. |
| D22 | Poslední úspěšné uložení vyhrává mezi všemi editory a Wizard relacemi v obou směrech. Save/Finish ukládá celý validovaný dokument, bez automatického slučování, revizního odmítnutí nebo potvrzení přepsání mezitím uloženého obsahu. |
| D23 | Jeden Next spustí jeden logický síťový pokus. Tray ani služba automaticky neopakují start ani kontrolu, ani po 401 nebo ztrátě IPC odpovědi. Úspěch je platný pro UI až po doručení a ověření aktuálního výsledku. Nový pokus vyžaduje další Next. Pravidla idempotence Finish se nemění. |
| D24 | Edit Config načte skutečný čitelný obsah bez ohledu na validitu. Vytvoření chybějícího souboru po Ano zapisuje ihned. Reset editoru po Ano mění pouze koncept a ukládá se až přes Save. Obě cesty používají jedinou platnou šablonu podle 5.6. |
| D25 | IPC přijímá i odesílá asynchronně, odděluje dlouhou diagnostiku od krátkého řízení a má omezená spojení, paměť i fronty a absolutní I/O deadline. Stop služby má vlastní cestu nezávislou na pipe. Konkrétní limity a cíle odezvy určí implementační návrh. |
| D26 | Wizard relace používá obnovovaný monotónní pronájem, výchozí potvrzení každých 15 s a expirace po 60 s bez kontaktu. Expirace bezpečně uklidí pouze danou relaci, neodpojí VPN a nezruší již provedený commit ani jeho potvrzení. |

Poslední rozhodnutí mají přednost i před historickými požadavky na revizně podmíněný Finish, sdílení VPN síťového vlákna s diagnostikou, automatické obnovení síťového pokusu a zákaz otevření nevalidního souboru v editoru. Ostatní dosavadní rozhodnutí zůstávají v platnosti.

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
| `client/apps/tray/main.cpp` | Dnešní menu nemá požadované pořadí, konfigurační dostupnost ani nová okna. | Zavést čistý menu model, Wizard a placeholder. |
| `status_reader.cpp` | Asynchronní čtečky mají lokální timeouty. CommandReader má 30 s, ale health + info mohou dohromady trvat déle. | Wizard musí používat krátké start/get-result příkazy, ne čekat na dvě HTTP operace v jediném 30s IPC volání. |
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
| `client/src/core/config/config_store.*` | Autoritativní snímek disku, revize pro čerstvost a receipts, serializované úplné zápisy podle D22 a create-if-missing editoru. |
| `client/src/platform/iface/config_files.h` | Testovatelné rozhraní čtení, atomické publikace a vytvoření bez přepsání existujícího cíle, bez Win32 typů. |
| `client/src/platform/windows/config_files_win.*` | Windows souborové operace, ACL, identita souboru, flush, publish a obnova po chybě zápisu. |
| `client/src/core/wizard/wizard_types.h` | Koncept, pole, typované výsledky, identifikátory a stavy. |
| `client/src/core/wizard/wizard_model.*` | Přenositelné UI přechody, validace polí, invalidace kroků a výsledné prezentační instrukce. |
| `client/src/core/wizard/wizard_defaults.*` | Výběr navržené IP, odvození Access Key, číselné řazení IP pro SHA-256 kontraktů. |
| `client/src/core/wizard/wizard_service.*` | Omezená správa relací a jejich pronájmů, vazby důkazů na vstupy, generaci konceptu a epochu spojení, samostatná životnost commit receipts. |
| `client/src/core/wizard/probe_client.*` | Jednorázové health/info/data operace nad dočasnými parametry a vlastním TimeSync. |
| `client/src/core/ipc/wizard_ipc.*` | Striktní stavitelé a parsery nových schémat, obálky odpovědí a oddělená diagnostika Wizardu. |
| `client/apps/tray/menu_model.*` | Pořadí položek, dostupnost a akce při neplatné konfiguraci bez Win32 závislosti. |
| `client/apps/tray/wizard_window.*` | Win32 prezentace, vazba ovládacích prvků na čistý model, fokus, zprávy a životnost okna. |
| `client/apps/tray/wizard_reader.*` | Asynchronní IPC adaptéry pro Wizard, výsledky korelované s generací okna a operace. |
| `client/apps/tray/wizard_layout.*` | Čistá geometrie průvodce, parametrizovaná DPI, plochou a metrikami textu. |
| `client/apps/tray/contracts_window.*` | Jediné zavíratelné placeholder okno dané tray instance. |

Existující `layout.*`, `window_utils.h`, `status_reader.*`, `resource.h`, `main.cpp`, build seznamy a základní chybové kódy se rozšíří. Přesun společných pomocných funkcí z tray je vhodný jen tam, kde je skutečně použijí nové komponenty. Tato etapa nemá plošně přestavovat celý klient.

### 4.2 Hranice dat

- ConfigStore vlastní uložený dokument a jeho revizi. Aktivní VPN vlastní svůj neměnný `ClientConfigPtr`, přijatý při novém logickém Connect.
- WizardService vlastní originální dokument, neměnný kontext konceptu a síťové důkazy. Uložená revize slouží pro čerstvost, není podmínkou Finish. Dočasné koncepty ani ověřené info se nepersistují.
- WizardModel vlastní právě zobrazený krok, lokálně editované texty a stav prezentace. Jeho lokální validace pomáhá uživateli, nikdy nenahrazuje validaci služby.
- ProbeClient nesmí mít referenci na `TrafficCounters`, fronty, `TunnelSession`, aktivní NetworkWorker ani publikované bootstrap kontrolní body.
- Služba vrací pouze potřebné typované výsledky. Celé HTTP requesty, ciphertext ani odvozené klíče neposílá do UI.
- CSPRNG vytváří UUID requestů, operací a relací. Tyto identifikátory slouží ke korelaci, nikoli jako nová autorizační hranice veřejného IPC.

### 4.3 Vlákna a plánování

Hlavní VPN síťové vlákno je vyhrazené pro bootstrap, recovery, časovou synchronizaci a datový provoz VPN. `get-info` zůstane mimo něj, s vlastním dočasným transportem a časovým kontextem jako dnes. Diagnostika nesmí sdílet proměnný TimeSync VPN, fronty ani provozní čítače. Původní přímý formát odpovědi get-info se zachová.

Wizard probe použije stejnou oddělenou omezenou řídicí síťovou obsluhu s vlastním ProbeClient. Zvolený návrh připouští nejvýše jednu dlouhou řídicí síťovou operaci celkem, tedy get-info nebo Wizard kontrolu. Obsazená kapacita vrátí `busy`, nevytváří neomezenou frontu. Na jednu Wizard relaci připadá nejvýše jedna rozpracovaná kontrola. IPC start kontroly rychle vrátí `operationId`, tray dále pouze čte její stav. Tunelová vlákna se kvůli diagnostice ani probe nespouštějí. Omezená obsluha řízení a platformní I/O callbacky nejsou čtvrtou VPN pracovní rolí.

Příchod nového Connect přeruší Wizard probe, bezpečně zruší jeho request a před bootstrapem počká na potvrzené ukončení práce. Nikdy nesmějí běžet dva data requesty současně, ani Wizard polling proti VPN bootstrapu/provozu. Samostatné diagnostické health/info smějí souběžně běžet s VPN datovým provozem. Čekání na ukončení podle D19 se vztahuje na rušený request před jeho nahrazením nebo uvolněním, nezavádí globální zákaz souběhu těchto izolovaných cest. `stateMutex_` ani zámek ConfigStore nesmějí být drženy při HTTP nebo kryptografii.

IPC příjem, čtení neúplné zprávy a odesílání odpovědi implementovat asynchronně. Teprve po úplném načtení a validaci se příkaz předá odpovídající obsluze. Čekání na další bajty nebo na odběr odpovědi nesmí držet pracovníka vykonávajícího krátké řízení. Pro get-status a Disconnect zajistit kapacitu nezávislou na dlouhé diagnostice. Přijetí Disconnectu jen předá požadavek VPN části služby. Dokončení lze potvrdit až po teardown, Wizard nadále čeká na skutečné `disconnected` a `idle`. Datové pakety se přes řídicí IPC nepřenášejí.

Omezit počet přijatých spojení, velikosti zpráv, celkovou paměť a fronty práce. Čtení i zápis mají vlastní absolutní monotónní I/O deadline, který dílčí přenos neobnoví. Přebytečná spojení nebo práci řízeně odmítat. Samotné navýšení počtu synchronních workerů ani priorita dosud nerozpoznaného příkazu nestačí. Odstranit neomezený `FlushFileBuffers(pipe)` a garantovat bezpečnou životnost overlapped I/O až do potvrzeného dokončení či zrušení. Stop přes Windows Service Manager má vlastní cestu nezávislou na pipe a ukončí čekající IPC i HTTP operace.

Před implementací v etapě A stanovit konkrétní limity, podporovaný profil zatížení a měřitelné meze pro odezvu get-status, přijetí Disconnectu, dokončení teardown a stop služby. Při podporovaném zatížení nesmějí pomalí IPC klienti vyčerpat obsluhu řízení. Při přetížení zůstane spotřeba omezená a stop proveditelný. Nejde o absolutní dostupnost při libovolném zahlcení veřejné pipe. Počet obslužných vláken se odvodí od návrhu, není pevně určen číslem čtyři.

### 4.4 Společný HTTP transport: rušení a celkový deadline

Opravit rozhraní Transport i Windows implementaci jako společnou součást VPN, get-info a Wizard probe. Použít asynchronní WinHTTP s explicitním vlastníkem requestu, bufferů a callback kontextu. Rušení synchronního volání zavřením handle z jiného vlákna není přípustné. Návrat `WinHttpCloseHandle` sám neznamená, že skončily callbacky. Kontext držet do závěrečného `WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING` a dokončení všech jeho uživatelů. Callbacky nesmějí čekáním na sebe navzájem vytvořit deadlock. Podklad: [Microsoft: WinHttpCloseHandle](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpclosehandle).

Tentýž mechanismus musí pokrýt Connect během probe, globální Disconnect s aktivním provozním requestem, stop služby při restartu/aktualizaci/odinstalaci i Cancel, Reset, Escape a X Wizardu. Souběh dokončení, timeoutu a více rušicích událostí vybere právě jeden konečný výsledek a právě jedno zavření. Náhrada requestu, rekonfigurace transportu a uvolnění jeho objektů počkají na potvrzený terminální stav. Rušení má krátkou konečnou dobu podle měřitelné brány 12.1, není slibována nulová prodleva. Zabránit použití zavřeného handle, dvojímu dokončení a dvojímu účtování přečtených bajtů.

Každý request dostane při zahájení jediný absolutní deadline z monotónních hodin a `responseTimeoutSeconds`, výchozí hodnota je 30 s. Jeden rozpočet zahrnuje DNS, spojení, TLS handshake, odeslání requestu, příjem hlaviček i celé tělo. Dílčí bloky ani přechod do další transportní fáze jej neposouvají. Dílčí WinHTTP timeouty jsou doplňkové, samy celkový deadline nezajišťují. Podklad: [Microsoft: WINHTTP_OPTION_RECEIVE_TIMEOUT](https://learn.microsoft.com/en-us/windows/win32/winhttp/option-flags#winhttp_option_receive_timeout).

Po vypršení deadline ihned zahájit bezpečné rušení, počkat na potvrzené ukončení a vrátit timeout, nikoli pozdní úspěch. Úklid nesmí dál spotřebovávat celý původní HTTP timeout. Zkontrolovat převody sekund, násobení i součet s monotónním časem a všechny návratové hodnoty nastavení timeoutů. Velké podporované hodnoty obsloužit bezpečným dělením platformních čekání, nebo nereprezentovatelnou hodnotu explicitně odmítnout před HTTP. Nikdy ji nepřetočit na záporný či nekonečný timeout ani tiše změnit význam konfigurace. Limity převodů zaznamenat v normativní dokumentaci.

Po timeoutu a cancel musí být transport bezpečně znovu použitelný. VPN nadále účtuje přesně jednou i částečné tělo a blok přes limit, před dokončeným Disconnectem účetnictví uzavře. Diagnostika a probe do VPN čítačů nevstupují. Oprava asynchronního transportu nemění tři sériové VPN role ani jejich ownership.

## 5. Konfigurace, dostupnost a revize

### 5.1 Typy a rozhraní

Zavést `ConfigAvailability = missing | unreadable | invalid | repairable | valid`. Nedostupná služba je samostatný UI stav, nesmí se vydávat za missing.

`StoredConfigSnapshot` obsahuje dostupnost, bezpečný kód důvodu, neprůhledný `revisionToken`, interní identitu souboru a SHA-256 přesných bajtů. Pro každý čitelný soubor uchová skutečný text pro editor i při nevalidním JSON, pro valid/repairable navíc DOM pro Wizard. Token slouží k detekci čerstvosti a identifikaci potvrzené publikace, nikoli k odmítání zastaralého Save/Finish. Nesmí se odvozovat pouze z mtime, velikosti nebo `ClientConfig::generation`.

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

Platná existující IP nebo platný existující klíč se zachová samostatně i při absenci druhého pole. Chybějící klíč se navrhne z ponechané nebo zvolené IP. Žádný již existující platný klíč se potichu nevymění jen proto, že byla navržena IP.

### 5.3 Zachování hodnot a Reset

V režimu edit/repair provést patch pouze čtyř pojmenovaných členů `masterSetup` v původním DOM. Nezrekonstruovat dokument z `ClientConfig`, protože by se ztratilo například rozlišení vynechaných defaultů a explicitních hodnot. Povolená je změna whitespace a formátování JSON. Hodnoty, typy a přítomnost nezobrazených členů se zachovají.

Reset Wizardu vytvoří nový koncept s URL `http://MODIFY-THIS-URL/phpvpn.php`, secretem `CHANGE_THIS_SERVER_SECRET`, `trustAnyTlsCertificate=true`, `vpnCidr=10.99.99.0/24`, současnými runtime defaulty a nově rozhodnutou sadou kontraktů. Runtime lze v JSON vynechat a využít sdílené defaulty. Původní obsah se po Resetu nepoužije pro slučování ani jako podmínka zápisu. Odlišný úplný default editoru je definován v 5.6.

Při unreadable se může po Resetu vyplňovat nový koncept, ale Finish nesmí obejít souborovou chybu přístupu. Pokud služba při publikaci stále nemůže bezpečně pracovat s aktuálním cílem a obnovou po chybě, zápis odmítne a navede na opravu přístupu. Nevyžaduje shodu s revizí při otevření. Reset neopravuje ACL ani nepřesměrovává cestu.

### 5.4 Čerstvost a reakce menu

Při startu načíst dostupnost. Nový `get-config-state` provede omezenou diskovou kontrolu, zatímco původní `get-status` zůstane pouze paměťový. Tray požádá o config-state po úvodním get-status, na svém nezávislém pětisekundovém cyklu, při otevření menu a po uložení. Stav nesmí určovat tray přímým `exists` na ProgramData.

Před otevřením editoru/placeholderu a před novým Connect služba znovu načte aktuální dostupnost. Mezi otevřením menu a kliknutím se mohla změnit. Editor smí načíst nevalidní text, Connect a get-info vyžadují úplně platný diskový snímek. `get-config` a get-info nesmějí po zjištění neplatného disku vracet nebo používat starou uloženou cache. Aktivní spojení si svůj již přijatý snímek ponechá.

| Stav konfigurace/služby | Connect při neaktivní VPN | Disconnect při aktivní VPN | Wizard | Edit Config | Contracts | Show Info / Statistics |
|---|---|---|---|---|---|---|
| valid | dostupné | dostupné | dostupné | skutečný obsah | běžné okno | dostupné |
| repairable / čitelný invalid | zakázané | dostupné podle D18 | dostupné | skutečný obsah včetně chybného JSON | vysvětlení bez okna | zakázané |
| unreadable | zakázané | dostupné podle D18 | dostupné | chyba přístupu, bez náhradního dokumentu | vysvětlení bez okna | zakázané |
| missing | zakázané | dostupné podle D18 | dostupné | Ano/Ne k vytvoření, po Ano zápis a editor | varování bez okna | zakázané |
| služba nedostupná nebo stav dosud neznámý | zakázané | nelze bezpečně odeslat | vysvětlení, bez kontrol a Finish | vysvětlení nedostupnosti | vysvětlení nedostupnosti | zakázané |

Exit zůstává dostupné vždy. Otevřené Statistics po změně souboru přestanou prezentovat nový provoz jako dostupnou funkci a zobrazí stav nedostupné konfigurace bez vynulování cache. Čtení get-status samo se nezakazuje. Save úplně validuje odeslaný dokument a bezpečnost publikace, nikoli obsahovou platnost nebo revizní shodu dosavadního souboru. Smí tak opravit nevalidní obsah i uložit starší koncept podle D22. Rozšíření Edit Config automaticky nepovoluje ostatní položky menu.

### 5.5 Atomický zápis: poslední úspěšné uložení vyhrává

Všechny zápisy služby, včetně set-config, Finish a potvrzeného vytvoření výchozího souboru z editoru, procházejí jediným ConfigStore a stejnou serializační branou. Mezi Save a Finish rozhoduje pořadí úspěšné publikace celých dokumentů. Pozdější Save starého editoru smí přepsat Finish a pozdější Finish otevřeného Wizardu smí přepsat Save, stejně tak dvě relace stejného typu. Stará revize sama nezpůsobí konflikt, nové otevření, potvrzení přepsání ani automatické slučování. Cizí uložení nezneplatňuje koncept a jeho důkazy jen kvůli změně uložené revize. Wizard sestaví celý dokument ze svého originálu a konceptu podle edit/repair/reset, nikoli z mezitím uloženého cizího dokumentu.

Postup Finish:

1. Ověřit živou relaci, shodu instance služby, connection epoch, síťové důkazy a přesné čtyři hodnoty. Sestavit výsledný dokument a spustit úplný validátor ještě před vytvořením temp souboru.
2. Vstoupit do serializační brány zápisu. Zjistit aktuální existenci a bezpečnou identitu cíle a adresáře pro publikaci a obnovu po chybě. Nevyžadovat obsah, existenci ani revizi shodnou s otevřením okna. Save/Finish mohou uložit i po ruční editaci či běžném delete/recreate cíle, pokud je aktuální publikační operace bezpečná.
3. Nový soubor i dočasné soubory smějí vzniknout jen v autoritativním chráněném adresáři při schválené ukládací operaci: Finish, validovaný set-config/Save nebo potvrzené create-if-missing editoru. Dočasný název obsahuje náhodné ID, vzniká přes CREATE_NEW, nikdy sdíleným `config.json.tmp` s truncation. Zakázat reparse přesměrování cíle a neočekávanou identitu adresáře. Nastavit ochranu SYSTEM/Administrators dříve, než se zapíší tajné bajty.
4. Zapsat celé tělo, ověřit počet bajtů a provést `FlushFileBuffers` nad skutečným file handle. Samotné `ostream::flush` nestačí pro tento účel. Viz [Microsoft: FlushFileBuffers](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers).
5. Bez síťové práce ověřit aktuální publikační podmínky. Save/Finish nemají create-only podmínku odvozenou od otevření okna. Zvolit atomickou publikaci pro aktuální stav cíle a bezpečně vyřešit změnu jeho existence během publikace. Jen výslovné `createDefaultIfMissing` nesmí přepsat mezitím vzniklý soubor, v tom případě načte skutečný aktuální obsah nebo vrátí konkrétní chybu. Pro existující cíl použít platformní replace se zálohou v témže adresáři, s kontrolou ACL a skutečného výsledku. `REPLACEFILE_WRITE_THROUGH` nepoužívat jako záruku, protože není podporován. Viz [Microsoft: ReplaceFileW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew).
6. Úspěch publikace je commit point. Teprve potom publikovat novou uloženou generaci v paměti a receipt. Aktivní konfigurace se tím nemění. Po commitu se již nesmí vrátit běžné cancelled, jako by disk zůstal nezměněný.
7. Při chybě odstranit pouze vlastní temp soubor, nikdy cizí `config.json`. U částečných chyb replacement API ověřit polohu původního souboru a zálohy, případně bezpečně obnovit původní cíl. Neprovádět slepé mazání zálohy ani cizího nového souboru. Restart musí umět rozpoznat vlastní nedokončený commit z minimálního záznamu bez klíčů a nesmí automaticky dokončit dosud neprovedený Finish.

Atomická publikace a ochrana před přepsáním staršího obsahu jsou odlišné vlastnosti. Druhá není požadována, poslední uložení smí mezitím provedené obsahové změny nahradit. Zůstává povinná serializace podporovaných IPC zapisovatelů a bezpečnost souborových operací i při přímém zásahu administrátora během publikační sekce. Testy musí prokázat, že nedojde k truncation způsobené službou, chybnému ACL, smazání nesprávného cíle při rollbacku nebo falešnému tvrzení, že se nic nezapsalo. Nevyžadovat compare-and-swap proti původnímu obsahu ani oplock pouze pro zabránění schválenému přepsání. Podrobnosti technické brány jsou v 12.1.

Zálohy mají stejná omezení přístupu jako konfigurace. Ponechat nejvýše poslední vlastní známou verzi, nevytvářet neomezenou historii secretů. Záloha slouží k bezpečné obnově po chybě, ne k reviznímu blokování Save/Finish. Výpadek napájení a selhání média mají vlastní recovery výsledek, nelze slibovat obnovu na fyzicky nefunkčním disku.

### 5.6 Edit Config: skutečný obsah, vytvoření a Reset

Nový `get-config-document` vrátí skutečný text čitelného souboru i při chybějících povinných polích, jiné konfigurační chybě nebo syntakticky chybném JSON. Nevalidní obsah nenahradí defaultem ani starou cache. Služba stále výhradně čte pevnou cestu. Původní přímý `get-config` zůstane pro platný konfigurační JSON, nevalidní dokument zpřístupní jen nová textová obálka. Nečitelnost existujícího souboru je chyba přístupu a nikdy nespustí vytvoření náhrady.

Při skutečné neexistenci zobrazí Edit Config Ano/Ne se skutečně vyhodnocenou absolutní cestou. Ano pošle `create-default-config`, služba společnou šablonu úplně zvaliduje a atomicky vytvoří chybějící soubor podle 5.5. Editor otevře skutečný výsledek operace. Pokud cíl mezitím vznikl, nesmí být přepsán šablonou. Ne soubor nevytvoří a neotevře nový dokument. Cancel editoru po potvrzeném vytvoření již vzniklý soubor nemaže.

Tlačítko Reset v editoru vyžádá Ano/Ne k obnovení výchozí konfigurace. Ano nahradí pouze rozpracovaný text a označí jej jako změněný, Ne zachová dosavadní text. Disk se změní až po úspěšném Save se stejnou úplnou validací jako Connect. Cancel zahodí neuložené úpravy. Tato pravidla editoru nenahrazují Reset Wizardu.

Vytvoření i Reset editoru používají jedinou továrnu `makeDefaultEditorDocument` ve společném klientském kódu. Služba poskytne tutéž šablonu přes `get-default-config`, tray nedrží vlastní kopii JSON. Wizard sdílí konstanty a algoritmus odvození klíčů, ale zachová svůj placeholder URL a volitelné generování 13/14 kontraktů. Úplná šablona editoru je:

```json
{
  "masterSetup": {
    "serverUrl": "http://127.0.0.1/phpvpn.php",
    "trustAnyTlsCertificate": true,
    "serverSecret": "CHANGE_THIS_SERVER_SECRET",
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
| otevření | načíst službu, konfiguraci, epochu a nový sessionId. Aktivní VPN vede na disconnectGate, jinak na serverUrl nebo chybu vyžadující Reset. |
| Next na disconnectGate | text předem vysvětlí globální dopad. Odeslat disconnect, čekat na skutečný `disconnected` a `idle`, ne pouze na přijetí IPC. Pak krok označit zeleně a přejít na serverUrl. |
| Next serverUrl | lokální validace a check-url. Jen úspěch přejde na serverSecret. |
| Next serverSecret | check-secret s novým health a info. Jen úplný úspěch přejde na identity a případně jednou doplní chybějící návrhy. |
| Next identity | lokální validace, kontrola kolize kontraktu, upozornění na spotřebování zpráv a check-access. Jen úspěch přejde na contracts. |
| Back | contracts → identity → serverSecret → serverUrl. Zachovat koncept. Samotný Back nedělá síť ani zápis. Další Next opět ověří svůj krok. |
| změna URL | zneplatnit všechny síťové výsledky, ověřený čas, info a návrhy závislé na starém serveru. Ručně zadané hodnoty nezahodit. |
| změna secretu/default checkboxu | zneplatnit secret a access výsledky, čas a info. |
| změna IP/Access Key | zneplatnit access výsledek, znovu spočítat kolize a náhled počtu kontraktů. |
| Reset | bezpečně zrušit vlastní kontrolu, po ukončení práce vytvořit nový koncept a vyčistit výsledky. Žádné diskové operace ani revizní podmínka pozdějšího Finish. |
| Cancel / Escape / X | mimo commit zrušit vlastní operaci a relaci, zavřít okno, vymazat koncept. Neodesílat Connect ani dodatečný Disconnect. |
| Finish | úplná službová validace a atomický zápis podle D22, bez dalšího data pollingu. Během krátkého commitu nelze předstírat úspěšný Cancel. |
| úspěšný Finish | receipt confirmed, zavřít Wizard, obnovit config-state a otevřít placeholder jednou. |
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

Pole zobrazuje přesnou uloženou hodnotu. Default checkbox je zaškrtnutý právě při doslovné shodě s `CHANGE_THIS_SERVER_SECRET`, u nového/resetovaného konceptu od začátku. Při zaškrtnutí je pole zakázané a zobrazuje nový default. Prázdná uložená hodnota zůstává prázdná s nezaškrtnutým checkboxem, i když se kryptograficky normalizuje na default.

Zachovat přesné UTF-8 bajty bez trimu, změny velikosti písmen nebo Unicode normalizace. Win32 převod nesmí potichu ukončit secret na NUL. Pro existující řídicí znaky použít výslovně označené zobrazení jako JSON string literal a striktní dekódování v tomto režimu, pro běžný text obyčejné pole. Zobrazení escape sekvencí nesmí zaměnit doslovné znaky `\n` se skutečným koncem řádku. Nezměněná hodnota se bere z původních bajtů modelu.

Next provede nový health, ověří GCM, sestaví samostatný TimeSnapshot podle normativního RTT a bezpečnostní rezervy, potom odešle info, ověří obálku a dešifruje. Doplnit `InfoResponse::clients` jako seznam `{Ipv4Address address, int64 lastSeenMs}`. Vyžadovat clients array, validní položky, kanonické adresy, unikátní adresy a platné milisekundové UTC lastSeen. Neznámá pole plaintextu info nadále ignorovat v souladu s protokolem. Statistický čas v budoucnosti je platná hodnota a může být výsledkem markeru, nesmí se automaticky zaměnit za chybějícího klienta.

### 7.3 IP a Access Key

IP musí být kanonická a `vpnCidr.isValidClientAddress`, tedy ne síť, první rezervovaná host adresa ani broadcast. Klíč musí být přesně 64 malých hex znaků, žádné automatické lowercasing nebo odstraňování whitespace.

Původní automatický návrh IP zůstává v rozsahu `10.99.99.2–.15`. Při opravitelném souboru jej omezit na platné klientské adresy zachovaného CIDR a vyloučit kolize s existujícími kontrakty. Pokud je množina prázdná, vyžádat ruční IP s vysvětlením zachovaného CIDR. Rozšíření pravidla generování kontraktů na jiné `10.A.B.0/24` samo nepřidává nový protokol pro automatickou alokaci IP.

Z ověřených clients vybrat nejnižší adresu kandidátní množiny, která chybí. Jsou-li všechny přítomné, zvolit nejstarší lastSeen, při shodě nižší IP. Poslední oktet se porovnává jako číslo. clients není autoritativní evidence volných adres ani rezervace. UI musí říct, že jde o návrh a může dojít ke kolizi s jiným klientem. Služba sama nepřidává ACCESS_KEYS na vzdálený server.

Access Key se navrhne jako `hex2(lastOctet)` opakované 32krát. U automaticky odvozeného dosud ručně nezměněného klíče lze při změně IP návrh přepočítat. Ručně zadaný nebo existující klíč se nikdy bez výslovné volby nepřepíše. UI uvede, zda klíč právě tvoří návrh, nebo vlastní hodnotu.

Před provedením kontroly je trvale viditelné upozornění: `This check may remove waiting messages for this IP address from the server. The messages will be discarded and will not be delivered to the VPN. If the result does not reach this window, the check may still have removed messages. Each new click on Next starts a new check and may remove more messages. Checks are not retried automatically.` Tlačítko Next je výslovný souhlas s tímto jedním pokusem, bez dalšího povinného modálního potvrzení.

Každý pokus check-access získá čerstvý ověřený health a info ve vlastním kontextu, aby dlouhé vyplňování a změna systémového času nepoužily starý odhad či serverové limity. Potom provede nejvýše jeden prázdný data request. Všechny health používají auth-none. Žádný automatický retry žádného kroku sekvence, ani po 401. Další pokus je nové stisknutí Next a znovu začíná čerstvým health. Normativní pravidla obnovy času po 401 se podle kapitoly 11 zpřesní tak, aby se ve Wizardu obnova odložila do tohoto nového výslovného pokusu. Recovery běžné VPN se tím nemění.

Prázdný request má pouze auth-full a LF. Vyžadovat HTTP úspěch, úplnou strukturu NDJSON, limity těla/řádku/počtu, správný requestId v data-result i data-mac, kanoničnost všech záznamů a správný HMAC. Pro prázdný odeslaný seznam navíc vyžadovat prázdné accepted/deferred ID seznamy a nulové acceptedRecords/acceptedBytes. Ověřovat shodu cílové IP a přípustnost směrovacích polí vrácených dat, nevydávat MAC za důkaz platného payload plaintextu.

Po tomto ověření záznamy zahodit. Nekontrolovat payload GCM, nevolat `ingestRecord`, nealokovat per-peer fronty a nezvyšovat ani diagnostické drop/ciphertext čítače VPN. Tento výslovný Wizard režim nemění plné ověřování a doručování při normálním Connect a provozu. MAC protokolu nehashuje celý ciphertext, UI proto nesmí tvrdit, že ověřilo end-to-end obsah zpráv.

Při 401 uvést, že autentizace byla odmítnuta a má se ověřit ACCESS_KEYS pro konkrétní virtuální IP v souboru na zadané serverové URL. Neoznačit timeout, 503 nebo chybný MAC automaticky za chybný Access Key. Webová URL neznamená, že je přes prohlížeč dostupný zdroj PHP. Nápověda má doporučit otevřít soubor ve správě hostingu nebo se obrátit na správce, nikoli vydávat URL za serverovou filesystem cestu.

### 7.4 Kontrakty

V edit/repair zobrazit pouze počet zachovaných kontraktů a informaci, že průvodce je nemění. D13 se ověří již při změně IP a službou před check-access, ne až po spotřebování serverové fronty.

V create/reset zobrazit zaškrtnuté `Create default communication contracts`, pouze pokud CIDR odpovídá `10.A.B.0/24`. Pro ostatní CIDR bude volba viditelná jako zakázaná/nezaškrtnutá a pod ní srozumitelné vysvětlení s aktuálním CIDR. Výsledkem je prázdný objekt `communicationContracts`. Nepřebírat staré kontrakty z konceptu před Resetem.

Peery generovat z platných adres stejného prefixu `.2–.15`, vlastní vynechat. Každý payloadKey je SHA-256 přes přesné ASCII `minNumericIp_maxNumericIp`, oddělené jediným podtržítkem, bez konce řádku. Výstup je lowercase hex. Souměrný pár dává stejný klíč na obou klientech, směry nadále odlišuje protokolové AAD.

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
| `wizard-open` | žádná | `sessionId`, `serviceInstanceId`, `revisionToken`, `availability`, `mode`, `fields` nebo null, `context` nebo null, `contractCount`, `connectionEpoch`, `vpnState`, `vpnPhase` |
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

`fields` obsahuje právě čtyři stringy `serverUrl`, `serverSecret`, `virtualIp`, `accessKey`. `context` obsahuje efektivní `vpnCidr`, `trustAnyTlsCertificate` a všech osm runtime limitů. Služba je odvodí z vlastního originálu nebo resetovaného konceptu, nesmí věřit klientem podstrčenému TLS nebo CIDR. `contractCount` je nezáporné celé číslo. `revisionToken` je neprůhledný náhodný identifikátor revize v rámci instance služby, nikoli zveřejněný hash secretu ani očekávaná revize commitu.

`get-config-document` pro missing vrátí document/revisionToken null, pro každý čitelný soubor skutečný text a dostupnost, pro nečitelnost konkrétní chybu přístupu. Textový string v obálce dovoluje i syntakticky chybný JSON, neznamená oslabení validace IPC. `create-default-config` nepřijímá cestu ani obsah. Při souběžném vzniku souboru vrátí `created:false` a jeho skutečný čitelný obsah, případně konkrétní chybu. Na validitu tohoto cizího obsahu se neváže jeho otevření. Po úspěchu vytvoření vrací `created:true`. Save dále používá původní set-config s úplným dokumentem a jeho službovou validací. Při nepodporovaných nových příkazech tray oznámí nutnost nové služby a nepoužije starou cache ani vytvoření přes fallback.

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

Čekání tray na výsledek jednoho Next má samostatný konečný monotónní rozpočet od stisku, zahrnující IPC start, maximálně 1/2/3 HTTP requesty pro url/secret/access, konečný rozpočet lokálního zpracování a doručení výsledku. Konkrétní IPC a lokální rozpočty stanovit v etapě A, pro výchozí HTTP hodnoty vycházet z nejvýše 30/60/90 s síťové části. Celkový součet a převody kontrolovat na přetečení. Polling ani keepalive konečný termín neobnovují. Timeout nebo jiná chyba kteréhokoli IPC volání kontroly ukončí aktuální Next v UI, zruší další čtení jeho výsledku a vyžádá bezpečné ukončení práce. Pozdní succeeded už krok nezmění. Nový Next je vždy ruční, může získat nová data a spotřebovat další zprávy.

Commit a jeho receipt mají bezpečnou životnost oddělenou od pronájmu editace. Expirace ani zavření nesmějí vrátit již publikovaný zápis nebo jej označit za neprovedený. Před publikační sekcí rezervovat místo pro záznam výsledku. Registry receipts musí mít konečný limit a retenční dobu stanovené v etapě A, při zaplnění nový commit odmítnout před zápisem, nikoli vyřadit potřebné potvrzení. Receipt zachovat alespoň po zbytek živé relace a po stanovenou dobu řešení nejasného Finish i po její expiraci. wizard-get-commit může pouze přečíst existující záznam pod původní trojicí serviceInstanceId/sessionId/commitId, neobnoví pronájem ani nepovolí další zápis. Terminální potvrzení je neměnné, dokládá konkrétní publikaci, i když ji pozdější Save již přepsal. Vyřazené či expirované relace se přes opakovaný Finish nikdy nezaloží znovu.

Pád služby ruší relace a síťové důkazy. Při restartu během nejasného Finish klient načte nový config-state a zobrazí výslovné ověření výsledku, nezačne automaticky nový zápis. Trvalý commit recovery záznam identifikuje pouze publikaci a revizi, neobsahuje další kopii secretů. Nezjištěný výsledek zůstane unknown, nikdy se z něj neodvodí automatický retry. Tato pravidla zachovávají idempotenci Finish a řešení nejasného zápisu, změna D23 se týká pouze síťových kontrol.

Limit zprávy zahrnuje JSON escaping. Dokument, který se s obálkou nevejde do v1, odmítnout před kontrolou a zápisem s informací o velikosti, nerozdělovat jej na nezdokumentované kusy. Doplnit testy horních hranic a UTF-8. Limity responseTimeoutSeconds se při převodu do WinHTTP nesmějí násobením přetočit na záporný nebo nekonečný timeout.

### 8.4 Chyby a bezpečnost

Přidat stabilní kódy například `config_unreadable`, `wizard_session_expired`, `wizard_prerequisite_failed`, `wizard_attempt_closed`, `wizard_connection_interrupted`, `wizard_commit_unknown`, `wizard_contract_self_conflict` a `wizard_unsupported`, dále rozlišit busy, timeout a chybu souborové publikace. Nezavádět config_conflict pro zastaralou uloženou revizi. Původní chyby neměnit na raw systémové výpisy.

Wizard chyba má strukturované `code`, hotové anglické `description`, `recoverable`, `field` nebo null, `httpStatus` nebo null, `suggestedAction` a případně povolené `serverUrl`/`configPath`. URL a cesta pocházejí z konceptu a pevného úložiště, ne z neověřeného serverového těla. Síťovou příčinu mapovat na DNS, connect, TLS, timeout, odmítnutou autentizaci, neplatný formát a neplatné kryptografické ověření. Neočekávanou HTML odpověď nespouštět ani nezobrazovat jako HTML.

Zákaz citlivého logování platí i pro detail chyby. Dnešní `Error::message()` přidává detail, proto se do běžného Error.detail nesmějí vložit requesty, klíče ani raw těla. Wizard diagnostika má vlastní povolená pole a nesmí prosakovat do get-status, progress, Statistics, tooltipu nebo loggeru. Konfigurační pole Wizardu a editoru samozřejmě smějí podle svého účelu zobrazit hodnoty, které uživatel zadává.

Zachovat vědomě veřejný přístup pro lokální aplikace, bez impersonace a bez kontroly PID. Pipe musí výslovně odmítat vzdálené klienty. Příkazy nepřijímají filesystem cestu, HTTP metodu, vlastní action, libovolné hlavičky ani spustitelný příkaz. UI nemá nové oprávnění zapisovat ProgramData. Nepřidávat síťový allowlist, který by zrušil existující podporu lokálních HTTP serverů.

## 9. Win32 UI a texty pro začátečníka

UI zůstane anglické jako současné menu. Texty budou soustředěné a testovatelné, bez čísel řádků v PHP souboru. Jedno Wizard okno na tray relaci. Opakované otevření existující okno aktivuje, nezahodí koncept ani nevytvoří druhý sessionId. Jiná interaktivní relace může mít vlastní průvodce, každý ukládá svůj úplný koncept podle D22.

Výchozí klientská plocha přibližně `1000 × 680` při 96 DPI, levý seznam kroků přibližně 230 DIP, pravá obsahová část a samostatný dolní pás tlačítek. Okno se vystředí na pracovní ploše aktivního monitoru a zmenší na dostupnou plochu. Při malé ploše se posouvá obsah, nikoli dostupnost Cancel/Next/Finish. Finální geometrie vychází z naměřených textů, ne z předpokladu, že se nápověda vždy vejde na dva řádky.

Aktivní krok je tučný a označený, úspěšný má zelenou značku a textový význam, chyba červenou značku a popis. Barva není jediný nositel informace. Ovládání podporuje Tab, Shift+Tab, klávesové zkratky, Enter na povolené výchozí akci a Escape jako Cancel. Next při nevalidním poli nesmí spustit síť, fokus přejde na první chybu. Během kontroly zobrazit konkrétní činnost, zakázat změnu ověřovaných polí a opakované Next, ale umožnit Cancel/Reset s korektním zrušením operace.

| Místo | Obsah nápovědy |
|---|---|
| začátek | průvodce nastaví tento počítač, změny se uloží až pomocí Finish, ke spojení se následně použije Connect |
| disconnectGate | VPN používají všechny relace na tomto počítači, Next ji odpojí všem a průvodce ji sám neobnoví |
| Server URL Configuration | zadat úplnou adresu serverového skriptu, příklad URL, kde ji získat, při placeholderu výslovně vyzvat k nahrazení |
| SERVER_SECRET Configuration | hodnota odpovídá konstantě SERVER_SECRET v horní části serverového phpvpn.php, lze ji získat od správce, default je veřejně známý |
| IP Address Configuration | virtuální adresa tohoto počítače, povolený zachovaný CIDR, navržená adresa nemusí být skutečně volná |
| ACCESS_KEY | přesný klíč ze serverové mapy ACCESS_KEYS pro zvolenou IP, vysvětlení 64 malých hex znaků a zřetelné upozornění na spotřebování čekajících zpráv |
| Communication Contracts | které protějšky budou vytvořeny a jejich počet, nebo počet zachovaných kontraktů, nebo důvod nevytvoření kvůli CIDR |
| Finish | uloží konfiguraci a otevře okno Communication Contracts, nepřipojuje VPN |

Příklad chybějícího souboru: `The configuration file does not exist at: {absolutePath}. Run Configuration Wizard to create it after you successfully finish all steps. If you already have a config.json from your administrator, you can copy it to this location.` Nečitelnost a neplatnost mají vlastní pravdivý úvod, nikdy text „does not exist“, pokud soubor existuje.

Edit Config má místo tohoto varování vlastní potvrzení s tlačítky Yes/No: `This application uses the configuration file at: {absolutePath}. The file does not exist. Create it now from the default template?` Nečitelnost otevře konkrétní chybu přístupu, čitelný nevalidní dokument se otevře k opravě bez náhrady obsahu. Potvrzené vytvoření proběhne na disku již před otevřením editoru, Cancel soubor neodstraní.

Editor doplnit o Reset v dolním pásu vedle Save a Cancel. Potvrzení Yes/No: `Restore the default configuration in this editor? The file will change only after you choose Save.` Ano načte stejnou šablonu služby jako create-default-config a označí obsah za změněný. Ne ani chyba získání šablony nesmějí ztratit rozpracovaný text. Zachovat výchozí plochu `1080 × 560` při 100% DPI, oba posuvníky a škálování. Při resize a změně DPI musejí zůstat viditelná a funkční všechna tři tlačítka, bez původního artefaktu vedle Cancel. Uložení nevyžaduje souhlas s přepsáním mezitím uložených změn.

Příklad timeoutu Next: `The check did not complete in time. Your entries have been kept. Choose Next to start a new check.` Příklad zániku relace: `This wizard session expired because contact with the service was lost. Open Configuration Wizard again to continue.` Probíhající nejasný Finish se místo tvrzení o neuložení řídí vlastním potvrzením commitu.

Příklad kolize: `This IP address is already used by an existing communication contract. A contract cannot point to this computer itself. Choose another IP address, or cancel the wizard and use Edit Config to correct the conflicting contract before trying again.` Zobrazit konkrétní kolizní virtuální IP, žádný payloadKey. Hláška je vysvětlení, nikoli dodatečné schvalování automatického odstranění.

Placeholder má titul `Communication Contracts`, běžné záhlaví, možnost zavřít a prázdnou obsahovou plochu. Neobsahuje zatím falešné editační prvky. Jeho otevření z menu provede aktuální kontrolu dostupnosti. Automatické otevření po Finish není podmínkou úspěšnosti uložené konfigurace, selhání vytvoření HWND nesmí změnit confirmed commit na neúspěšný zápis.

Wizard může mít vlastní 500ms polling stavu dosud běžící operace a epochy pouze po dobu otevření. Když žádnou kontrolu neprovádí, jeho pronájem udržuje 15s keepalive, případně jiný platný požadavek dané relace. Původní pětisekundový tray timer i 500ms timer Connecting progress window zůstávají nezávislé. Zavření Wizardu ukončí jeho polling i keepalive. Odpověď smí změnit UI jen při shodě windowGeneration, serviceInstanceId, sessionId, operationId, clientAttemptId a attemptSequence, a pokud pokus ještě neskončil a nevypršel jeho konečný termín. Staré potvrzení ani úspěch nesmějí ovlivnit nový pokus.

## 10. Server, defaulty a kompatibilita

V `phpvpn.php` upravit konfigurační hlavičku a komentář, `Config::fromConstants`, `Config::withSecret`, `Config::isInsecureSecret` a `Application::isConfiguredSecretInsecure`. Zavést jedinou interní konstantu/default helper tak, aby se nemohly lišit normalizace a výchozí hodnota. Veřejná stránka musí varovat při novém defaultu, prázdném nakonfigurovaném secretu, řetězci samých ASCII nul a samých nulových bajtů. Autentizované endpointy tyto hodnoty nadále neodmítají jen kvůli jejich předvídatelnosti.

Rozšířit ACCESS_KEYS na `.2–.15`, zachovat dosavadní `.2–.4`, doplnit `.5–.9` a `.10–.15` s dvojicemi `05` až `09` a `0a` až `0f`. Není to dynamické přenastavení serveru při změně klientského CIDR. Pro jinou síť musí správce nadále sladit serverové adresní parametry a mapu.

Stejný default použít v C++ key_derivation, config_defaults, předvídatelnostním warningu, Python normalizaci a dokumentaci. Nenahrazovat plošně všechny řetězce nul, některé jsou známé AES testovací vektory nebo explicitně testovaná neprázdná tajemství.

Původní drátové formáty, kryptografické domény a serverVersion negotiation se nemění. Kombinace nové a staré implementace s explicitně stejným neprázdným secretem dál interoperuje. Prázdná hodnota má novou efektivní hodnotu, takže kombinace starého a nového defaultu interoperabilní není. Nezavádět fallback, automatický rewrite existujících configů ani dedukci secretu podle serverVersion.

Nová tray při `ipc_unknown_command` na get-config-state nebo wizard-open zobrazí, že Wizard vyžaduje aktualizovanou službu, a nic nevytváří. Původní příkazy zůstanou dostupné podle původní smlouvy, pokud je kompatibilní stav služby známý. Žádné hledání alternativní pipe ani automatický downgrade nové operace na set-config. Starý klient nové příkazy nepotřebuje, původních sedm příkazů na nové službě musí stále fungovat.

Projekt nemá nasazené uživatele, proto není potřeba migrační utilita, dlouhodobá dvojí IPC verze ani složitý rollout. Novou službu a tray distribuovat společně. Instalační skripty nesmějí zapisovat ani nepřímo vyvolat vznik configu a musí po instalaci správně popsat obě cesty: úspěšný Finish Wizardu nebo výslovně potvrzené vytvoření přes Edit Config. Existující config se při reinstalaci zachová.

## 11. Aktualizace dokumentace a testovací infrastruktury

| Soubor | Konkrétní změna |
|---|---|
| `protocol.md` | Konzistentně změnit default a normalizaci v 3.2, odkazech 7.2, příkladu PHP v 15, testech 16.1/16.3 a checklistu 17, veřejné varování v 5.1 a výchozí mapu klíčů v souvisejících příkladech. V 5.2, 11.2 a 13 vymezit explicitní Wizard kontrolu bez automatického retry podle následující tabulky. Zachovat wire formáty, best-effort clients a at-most-once polling. |
| `client.md` | Promítnout D01–D26 do konfigurace, tří VPN rolí a oddělené diagnostiky, transportu, obnovy, IPC, menu, editoru, Wizardu, čítačů a lifecycle testů. Konkrétní normativní zásahy jsou v následující tabulce, nestačí připojit obecný odkaz na plán. |
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

| Nález | Místo normativní změny | Závazný obsah a vazba na plán |
|---|---|---|
| 1 | `client.md`: Architektura/Transport, Běhový model, Disconnect, upgrade a rollback, Windows lifecycle testy | Společná bezpečná životnost a rušení všech HTTP cest podle 4.4, krátká konečná doba, terminální stav před nahrazením/uvolněním, žádné dvojí zavření či callback po uvolnění kontextu. Zahrnout Connect proti probe, globální Disconnect, všechny důvody stopu služby a zavření/Reset Wizardu. Zachovat jednorázové účtování částečných VPN odpovědí. |
| 2 | `client.md`: Konfigurační soubor/responseTimeoutSeconds, Příjem dat, Transport a povinné testy. `protocol.md` 11.2 | Upřesnit celkový absolutní monotónní deadline DNS až konec těla bez obnovy dílčím blokem, kontrolované časové převody a kontrolu nastavení timeoutů. V protokolu ponechat obecné doporučení 15 s jako doporučení, uvést, že nativní klient používá vlastní normativní konfigurovatelný default 30 s. Timeout vyvolá bezpečné zrušení a nesmí skončit pozdním úspěchem. Recovery se určuje podle provozní či Wizard cesty. |
| 3 | `client.md`: Účel, Architektura/Runtime, Běhový model, Synchronizace času, veřejné IPC/get-info | Formulace „právě tři vlákna“ vztáhnout na tři VPN pracovní role, ne na všechny platformní a řídicí činnosti procesu. Jediné vlastnictví health/info/data a TimeSync vztáhnout na VPN kontext. Explicitně povolit oddělenou omezenou diagnostiku get-info s vlastním transportem/časem souběžně s VPN daty. Probe používá oddělenou cestu, ale nesmí překrýt VPN data request. Žádné proměnné VPN kontexty, fronty ani čítače v diagnostice. |
| 4 | `client.md`: Konfigurační soubor, set-config, Wizard Finish a souběhové testy | Úplná validace a atomická serializovaná publikace celého dokumentu podle posledního úspěšného uložení. Zastaralý editor i Wizard smí přepsat mezitím uložený obsah bez nové výzvy nebo sloučení. Cizí revize nezneplatňuje důkazy. Epochy, validace vstupů, ACL, bezpečné chyby a idempotence Finish zůstávají povinné. |
| 5 | `client.md`: Stavový automat připojení/Obnova, responseTimeoutSeconds, Wizard kontroly, IPC a UI. `protocol.md` 5.2, 11.2 a tabulka chyb 13 | Výslovně oddělit provozní recovery od jednorázového Wizard Next. Ve Wizardu chyba v libovolném úseku včetně IPC a HTTP 401 ukončí pokus, žádný automatický start/retry health/info/data. Povinnost obnovy ověřeného času po možné odchylce se vykoná až na začátku dalšího ručně vyžádaného autentizovaného pokusu. Obecný backoff/recovery VPN zůstane. Definovat konečné doručení do tray, korelaci a ignorování pozdního úspěchu, nový Next může znovu odebrat zprávy. Průběžné čtení operace neprovádí síť. Finish má oddělenou idempotenci. Lokální attemptSequence není wire replay cache podle 6.3 protokolu. |
| 6 | `client.md`: Konfigurační soubor, get-config/set-config a whitelist nových IPC příkazů, Edit Config/menu, distribuce a testy editoru | Skutečný čitelný text i při nevalidním JSON přes novou textovou obálku, bez změny původního přímého formátu get-config pro platný JSON. Potvrzené create-if-missing s absolutní cestou, okamžitý validovaný atomický zápis a bez přepsání mezitím vzniklého cíle. Nečitelnost není missing. Reset editoru potvrzením mění jen text, Cancel neodstraní již vytvořený soubor. Jediná šablona přesně podle 5.6, všechna tři tlačítka v layoutu a regresích. Omezit tvrzení „soubor vzniká až při Finish“ na Wizard, instalace/start/Connect dál nic nevytvářejí. Ostatní menu položky nezískávají výjimku editoru. |
| 7 | `client.md`: Veřejné lokální IPC, get-info, disconnect, Windows Service stop a lifecycle testy | Asynchronní příjem/čtení/zápis, dispatch až po úplné validaci, dostupná kapacita krátkého řízení nezávislá na diagnostice, limity spojení/zpráv/paměti/front a absolutní I/O deadline. Rozlišit přijetí Disconnectu a jeho skutečné dokončení bez změny požadavku na potvrzený teardown. Stop služby nezávisí na pipe. Uvést konkrétní implementačně ověřené meze a profil zatížení, nikoli slib neomezené odolnosti vůči zahlcení. |
| 8 | `client.md`: Windows Service, konzole a tray, Wizard IPC/životnost a lifecycle testy | Odlišit autonomní životnost globální VPN od dočasných Wizard relací. Pronájem 15/60 s z monotónního času, platný kontakt jen dané relace, bezpečné zrušení a úklid po expiraci, bez nového vlákna/PID. Pozdní kontakt neobnovuje relaci, heartbeat neprodlužuje pokus. Samostatná životnost rozpracovaného commitu a receipts. Upřesnit, že odpojení jednorázové pipe není důkaz pádu tray. |

Ve společných scénářích `client.md` zachovat plné ověření a doručení bootstrap payloadu VPN. Samostatně normativně popsat Wizard polling, který po úplném ověření struktury a data-mac vše zahodí a neúčtuje do VPN statistik. Doplnit celý konkrétní whitelist a uzavřená schémata kapitoly 8, omezenou výjimku configPath/URL jen v určených konfiguračních odpovědích a UI, nikoli v get-status, statistikách a logu. Nálezy 9–12 nevyvolávají další změny požadavků.

Serverový deployment pro testy dnes používá vlastní secret a klíče. Jeho existující fixture nesmí být potichu nahrazena defaulty. Pro ověření defaultního Wizard scénáře zavést samostatný fixture profil s `.2–.15`. Generovaný `tests-integration/server.json` není normativní dokument a nemá se ručně přepisovat jako součást změny defaultu.

## 12. Pořadí implementace a kontrolní brány

### 12.1 Etapa A: smlouvy a technické ověřovací brány

1. Aktualizovat normativní dokumenty podle D01–D26 a matice v kapitole 11, včetně konkrétních schémat IPC. Zkontrolovat všechny původní věty o vlastnictví HTTP, recovery, vzniku konfigurace a počtu vláken.
2. Definovat ConfigFiles/ConfigStore a testovací backend s bariérami před čtením, flush a publish. Na cílových Windows ověřit atomické Save/Finish i create-if-missing, chyby replacement API, ACL a externí write/rename/delete v publikačním okně. Při běžné publikaci služby je vidět celý starý nebo nový dokument. Při chybě nesmí vzniknout truncation, smazání nesprávného cíle, únik temp tajemství ani falešný úspěch/nezapsání. Přepsání cizího dříve uloženého obsahu je podle D22 dovolené a nepovažuje se za selhání brány.
3. Navrhnout a na Windows ověřit asynchronní WinHTTP request lifecycle, krátkou konečnou mez rušení, potvrzení posledního callbacku, jediné dokončení a absolutní deadline. Ověřit opětovné použití transportu, timeout ve všech fázích i hraniční převody. Bez tohoto důkazu nepoužít starý synchronní cancel pro nové probe ani pro provozní VPN.
4. Pro IPC zapsat číselné limity spojení, paměti a front, deadline čtení/zápisu, rozpočet lokální části Next, toleranci lease časovače a limity/retenci commit receipts. Popsat podporované zatížení a měřitelné cíle odezvy get-status, přijetí a dokončení Disconnectu a stopu. Zátěžový test s neúplnými požadavky, neodebíranými odpověďmi a pomalým get-info musí prokázat oddělení řízení od čekajícího I/O a časově omezené zastavení při přetížení.
5. Výsledky bran zaznamenat do implementačního návrhu a příslušných normativních částí. Pokud se požadovanou bezpečnost či meze nepodaří splnit, vyřešit konkrétní mechanismus před vydáním a popsat nevyřešený technický nález. Nevracet jako povinné opravy odmítnutou ochranu před zastaralým uložením nebo automatickou obnovu Next.

### 12.2 Etapa B: konfigurace a defaulty

Implementovat ConfigStore se serializací posledního úspěšného uložení, analýzu opravitelnosti, patch dokumentu a továrnu Wizard konceptu. Přidat čtení nevalidního textu, jedinou validní šablonu editoru a create-if-missing bez přepsání. Revize ponechat pro čerstvost/receipts a odstranit auto-create při instalaci/startu/Connect. Provést související server/C++/Python změnu defaultu a serverové mapy ve stejné sadě změn. Upravit deployment fixture. Gate: konfigurační, souborové a kryptografické testy včetně shodné šablony vytvoření/Reset.

### 12.3 Etapa C: protokolové pomocné funkce a ProbeClient

Dokončit společnou opravu bezpečného cancel a absolutního HTTP deadline z etapy A. Rozdělit health parse/verify, doplnit clients, implementovat čerstvý time context a přesně jeden data probe bez front a účetnictví. Žádný automatický retry celé kontroly ani jejího health/info/data. Opravit auth-none při opakovaném health ve sdílené cestě. Gate: transportní závody, pomalé i neukončené odpovědi, řízené parsery a nezávislé serverové kontraktní testy, bez spuštění tunelu.

### 12.4 Etapa D: služba a IPC

Implementovat oddělenou omezenou diagnostickou/probe cestu, asynchronní pipe I/O a nezávislou kapacitu krátkého řízení. Přidat Wizard pronájmy, aktuální pokusy bez historie a automatického retry, důkazy, epochy, rušení a oddělené receipts. Původní set-config přesměrovat do ConfigStore. Gate: neblokovaný VPN provoz během pomalého get-info, odezva řízení při obsazených IPC spojeních podle etapy A, ztracené odpovědi, lease cleanup, restart a stale IDs.

### 12.5 Etapa E: model a UI

Nejprve čistý WizardModel, menu a layout s testy. Pak Win32 Wizard, placeholder, oprava Edit Config pro nevalidní obsah a vytvoření/Reset s potvrzením a integrace do tray. Zachovat generace oken, konečné čekání Next, ignorování pozdního výsledku, keepalive a nezávislé pollingové intervaly. Gate: žádný přímý HTTP nebo diskový zápis v tray, přesné menu a testy všech přechodů včetně editoru.

### 12.6 Etapa F: integrační a ruční ověření

Debug/Release/ASan, testy PHP a Python, test Windows souborů a pipe, čistá instalace v testovací VM, dva tray uživatelé, DPI a resize. Sestavit distribuční balíček až po průchodu relevantních bran. Zapsat výsledky a omezení do nového `client/tests/implementation-plan3-report.md`, nevydávat existující plan2 report za test této etapy.

## 13. Testovací strategie

### 13.1 Nové automatické sady

| Soubor | Smysluplné scénáře |
|---|---|
| `client/tests/unit/test_wizard_document.cpp` | valid/repairable/invalid/unreadable klasifikace, více současných chyb, zachování nezobrazených hodnot a absence polí, UTF-8/BOM, duplicitní JSON, žádný zápis Wizardu před Finish. |
| `client/tests/unit/test_config_store.cpp` | dva starší koncepty a poslední úspěšná publikace, editor → Finish i Finish → starý editor, dva editory/Wizardy, zachování důkazů po cizím uložení, ruční změna stejné délky/mtime a delete/recreate bez revizního odmítnutí, selhání zápisu/flush/publish, cache až po commitu. |
| `client/tests/unit/test_config_editor.cpp` | otevření přesného čitelného nevalidního či syntakticky chybného JSON, žádná stará cache, unreadable není missing, vytvoření Yes/No a okamžitý zápis, souběžný vznik cíle bez přepsání, Cancel nemaže vytvořený soubor, Reset Yes/No/Cancel mění pouze editor, totožná validní šablona a úplná validace Save. |
| `client/tests/unit/test_config_files_win.cpp` | skutečné dočasné soubory, ACL, unikátní temp názvy, locked/unreadable cíle, reparse odmítnutí, externí writer v publikačním okně, recovery a invariant starý/nový úplný soubor. |
| `client/tests/unit/test_wizard_defaults.cpp` | výběr chybějící/nejstarší/shodné IP, prázdné clients, všech 14 přítomných, budoucí lastSeen, nevhodný CIDR, zachování ručního klíče a numerické pořadí `.2` proti `.10`. |
| `client/tests/unit/test_wizard_model.cpp` | všechny přechody, bez Back na prvním kroku, invalidace závislostí, Reset, Cancel, chyby bez postupu, cizí Connect, rychlý Connect/Disconnect, Finish unknown, stale výsledky. |
| `client/tests/unit/test_wizard_probe.cpp` | health syntax bez přijetí času, chybný tag, requestId, clients, GCM info, timeout každé části, 401/503, poškozené NDJSON/MAC, neočekávaný accepted výsledek, maximálně jeden data request. |
| `client/tests/unit/test_wizard_service.cpp` | chybějící předpoklady, podstrčené vstupy při Finish, session/epoch mismatch, explicitní resume, cancel/preemption, limit relací, monotónní pronájem a bezpečný úklid, odmítnutí opakovaného pořadí pokusu bez historie, Reset/Connect pořadí nemažou, oddělená idempotence commitId a dostupný receipt po expiraci. |
| `client/tests/unit/test_wizard_ipc.cpp` | úplná schémata obou směrů, neznámá/duplicitní pole, typy, UUID, integer overflow, escape overhead 4MiB, korelace výsledků a filtr citlivých údajů. |
| `client/tests/unit/test_tray_menu.cpp` | přesné pořadí a separátory, tabulka dostupnosti, výjimka Disconnect, akce warning místo okna, neznámý stav není missing. |
| `client/tests/unit/test_wizard_layout.cpp` | velikosti monitorů, DPI 100/125/150/200 %, dlouhé texty, stabilní dolní tlačítka, žádné překryvy, scroll obsahové části. |
| `client/tests/unit/test_winhttp_lifecycle.cpp` | řízený souběh cancel/deadline/success/callback, opakované rušení, Connect proti probe, globální Disconnect, stop služby a všechny způsoby zrušení Wizardu, žádné použití zavřeného handle ani dvojí dokončení, transport znovu použitelný. |
| `client/tests/unit/test_winhttp_deadline.cpp` | pomalé DNS/spojení/TLS, zdržení hlaviček, pomalé bloky těla, tělo bez konce, absolutní deadline bez obnovy, chyby nastavení timeoutů, hraniční sekundové hodnoty a součty, korektní timeout i částečné účetnictví. |
| `client/tests/unit/test_ipc_availability.cpp` | pomalý get-info souběžně s VPN daty, nejvýše jedna diagnostika, neúplné IPC requesty a neodebírané odpovědi bez obsazení krátkého řízení, limity a přetížení, měřené get-status, přijetí i dokončení Disconnectu a stop nezávislý na pipe. |
| `client/tests/unit/test_wizard_reader.cpp` | ztracené start acknowledgement i finální výsledky, timeout ve všech úsecích řetězce, žádný automatický resend, červený stejný krok a zachované hodnoty, pozdní úspěch po timeoutu a během dalšího Next, polling a keepalive neprodlužují termín, samostatné řešení Finish unknown. |
| `client/tests/contract/test_server_contract.cpp` | skutečný PHP relay: URL, secret, info.clients, `.2–.15`, polling s čekajícími zprávami, potvrzení jejich spotřeby bez tunelu a default kompatibilita. |

Testy modelů nemají jen zrcadlit implementaci. Použít tabulkové scénáře odvozené z D01–D26 a explicitní invarianty. Kryptografické vektory porovnat s nezávislým PHP/Python výpočtem, ne s druhým voláním stejné C++ funkce. Pro souběh používat bariéry a kontrolované dokončení, nikoli náhodné čekání a naději, že se race projeví. Transportní a IPC garance kromě řízených jednotkových testů ověřit přes skutečný Windows transport a pipe. Pro DNS/TLS a přetížení popsat reprodukovatelný fixture a měřené limity, samotný mock není důkazem platformní životnosti.

### 13.2 Povinné regrese

- Přepsat `test_default_config.cpp`: po startu služby a neúspěšném Connect soubor nevznikne. Soubor vytvoří Finish nebo výslovně potvrzené create-if-missing editoru, nikoli automatické `ensureDefaultConfigFile`. Editor create/Reset mají jedinou shodnou validní šablonu, Wizard zachová vlastní 13/14 kontraktů.
- Rozšířit `test_crypto.cpp`, `tests.php` a `test_crypto_vectors.py`: empty → nový default, `0`, 32/64 ASCII nul, NUL bajty a neprázdný custom secret beze změny. Ověřit všechny relevantní odvozené klíče a veřejné varování.
- Zachovat `test_runtime_status.cpp`, `test_status_contract.cpp`, `test_traffic_counters.cpp` a `test_winhttp_accounting.cpp`. Před a po každém probe porovnat všechny globální kontrolní body, čítače a aktivní stav. Výslovné disconnect a cizí Connect mají své původní legitimní resetovací účinky, nesmějí se zaměnit za vedlejší efekt probe.
- Ověřit, že normal Connect stále dešifruje a doručuje payload z bootstrap pollingu. Wizard režim zahození se nesmí stát defaultem společného parseru nebo NetworkWorker.
- Ověřit opakovaný health s existujícím TimeSnapshot proti skutečnému serveru, který přijímá pouze auth-none.
- Ověřit původních sedm IPC příkazů, přesný formát get-info/get-config, nezměněný status parser a intervaly tray/progress.
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

Čistá instalace a start služby bez souboru. Všechny čtyři aktivní položky bez konfigurace. Contracts zobrazí varování, Edit Config potvrzení vytvoření se skutečnou cestou i při nestandardním ProgramData. Yes vytvoří soubor před editací, No neotevře nový dokument, Cancel následně soubor nemaže. Otevřít čitelný poškozený JSON i repairable, ověřit Reset Yes/No a Cancel. Nečitelnost nesmí být zaměněna za missing. Ruční kopie configu se projeví bez restartu služby.

Průchod novým, existujícím i resetovaným konceptem. Zrušení na každém kroku a během čekající kontroly. Zavření a znovuotevření při opožděné odpovědi. Selhání Finish bez zavření okna. Úspěšný Finish otevře placeholder právě jednou a nevytvoří tunel. Opakované otevření Wizardu nezahodí koncept.

Dva interaktivní uživatelé, jedna služba. Cizí Connect v každém kroku i těsně před Finish, rychlý Connect/Disconnect, současný Save editoru a Finish druhé relace. Viditelná informace o cizím Connect a žádné automatické opětovné připojení. Zmizelý/poškozený config při connected zachová provoz a dostupné Disconnect.

Ověřit obě pořadí posledního úspěšného uložení včetně editoru otevřeného před Wizardem a Wizardu otevřeného před Save. Žádné revizní potvrzení přepsání ani vynucené nové otevření. Pomalá diagnostika během provozu nesmí zastavit polling VPN. Ztratit IPC odpověď po úspěšném data probe, ověřit červený krok a až dalším Next nový pokus. Ukončit tray bez close, ověřit úklid po vypršení pronájmu, nezávislost druhé relace, sleep/resume a Finish s nejasným potvrzením.

Změny velikosti ze všech hran a rohů, malé pracovní plochy, taskbar na různých stranách, DPI 100/125/150/200 %, přesun mezi monitory, tabulátor, čitelnost vysvětlení a návrat fokusu po chybě. Stejná kontrola resize regresí editoru jako v plan2. Evidence výsledků obsahuje OS, build, DPI, scénář, výsledek a testera, žádné klíče ani screenshoty v repozitáři.

## 14. Přesná akceptační kritéria

| ID | Požadovaný pozorovatelný výsledek |
|---|---|
| A01 | Čistá instalace, start služby a Connect bez configu nevytvoří `config.json`. Služba poskytuje IPC a zůstane provozuschopná pro konfiguraci. |
| A02 | Menu má přesně pořadí D01. Bez konfigurace jsou aktivní Wizard, Contracts, Edit Config, Exit. Contracts zobrazí varování bez okna, Edit Config nabídne vytvoření Yes/No, až po Yes a úspěšném výsledku otevře editor. |
| A03 | Varování Contracts obsahuje absolutní cestu získanou službou, instrukci Wizard → Finish a možnost zkopírovat existující config. Potvrzení editoru obsahuje stejnou skutečnou cestu, její účel a otázku na vytvoření ze šablony. Chybějící služba ani chyba přístupu nejsou missing. |
| A04 | Invalid/unreadable/repairable blokují Connect, Show Info, Statistics a běžné okno Contracts podle tabulky 5.4. Edit Config otevře každý čitelný obsah včetně chybného JSON, unreadable zobrazí chybu bez náhrady. Běžící VPN pokračuje na aktivním snímku a Disconnect zůstává dostupné. |
| A05 | Wizard nepíše konfiguraci, zálohu ani temp JSON před Finish. Cancel a Reset před Finish nezmění původní soubor bajtově. |
| A06 | Opravitelnost Wizardu je pouze absence IP/klíče při jinak platném dokumentu. Nesprávné typy nebo další chyby vyžadují Reset Wizardu nebo opravu čitelného obsahu přes Edit Config. Úplný validátor nepřijme neúplnou konfiguraci pro Connect ani Save. |
| A07 | Edit/repair zachová všechny nezobrazené hodnoty a kontrakty, včetně explicitních TLS/runtime hodnot a vynechaných volitelných polí. Reset je vědomě nahradí novými defaulty až při Finish. |
| A08 | Odpojovací Next čeká na skutečné dokončení. Server URL nemá Back. Zrušení a dokončení Wizardu neobnoví VPN. |
| A09 | Krok URL uspěje na syntakticky platném running health i s nesouhlasným GCM tagem, ale nepublikuje čas ani kryptografické ověření. Chybný formát a requestId neprojdou. |
| A10 | Krok secret vyžaduje nový ověřený health, validní TimeSnapshot, ověřený a dešifrovaný info včetně použitelného clients. Jediná chyba ponechá uživatele v kroku. |
| A11 | Checkbox secretu odpovídá přesné nové výchozí hodnotě. Prázdná a jiné neprázdné hodnoty se v souboru nepozorovaně nemění, UI zachová přesné bajty secretu. |
| A12 | Navržená IP je nejnižší chybějící kandidát, jinak nejstarší lastSeen s číselným tie-break. clients není prezentováno jako rezervace IP. Při prázdné kandidátní množině je vyžádána ruční IP. |
| A13 | Access Key má přesně požadovaný formát. Návrh z posledního oktetu dává například 32×02 pro `.2` a 32×0a pro `.10`. Ruční a existující klíč se automaticky nepřepíše. |
| A14 | Kolizní vlastní IP se odmítne před data kontrolou. Hláška vysvětlí příčinu a možnosti nápravy. Existující kontrakt zůstane beze změny. |
| A15 | Upozornění vysvětluje spotřebování zpráv i možnost serverového úspěchu bez doručeného výsledku. Jeden Next spustí jednu sekvenci bez automatického retry jakéhokoli HTTP nebo IPC startu, ani po 401. Obsahuje nejvýše jeden prázdný data request. Ztracená odpověď nebo jiná chyba ukončí pokus červeně na stejném kroku se zachováním hodnot. Nový ruční Next smí znovu odebrat zprávy, žádná historie pokusů nevynucuje znovuotevření Wizardu. |
| A16 | Access kontrola přijme pouze úplnou syntakticky správnou a MAC ověřenou odpověď se správným requestId a nulovým výsledkem pro prázdný request. Obsah datových zpráv nedešifruje, nedoručí a neuchová. |
| A17 | Žádný probe nezmění aktivní VPN, bootstrap body, runtime čas, fronty ani devět provozních čítačů. Neotevře Wintun a nevytvoří route. |
| A18 | Create/reset s `10.A.B.0/24` vytvoří přesně 13 nebo 14 kontraktů podle vlastní IP, správné klíče mapy a uvedené SHA-256 vektory. Jiný CIDR nebo odškrtnutá volba vytvoří prázdný objekt a UI vysvětlí výsledek. |
| A19 | Finish přijme jen poslední prokázané čtyři hodnoty a kompletně validní výsledný JSON. Změna vstupu po úspěšném Next bez nového ověření přes IPC neprojde. |
| A20 | Poslední úspěšná publikace vyhrává pro editor → Finish, Finish → starý editor, dva editory i dva Wizardy. Každý uloží celý svůj validovaný dokument. Zastaralá revize, ruční obsahová změna ani běžný vznik/smazání cíle samy nevyvolají odmítnutí, potvrzení přepsání, sloučení, invalidaci důkazů ani nové otevření. Souborová bezpečnost zůstává povinná. Create-if-missing je zvláštní operace, která existující cíl nepřepíše. |
| A21 | Čtenář při běžné atomické publikaci uvidí celý původní nebo celý nový JSON. Simulované chyby zápisu/flush/replacement nezanechají truncation, veřejně čitelné temp tajemství ani falešný úspěch. Souborová brána 12.1 je doložená. |
| A22 | Opakování commitId nepíše znovu a změněný vstup se odmítne. Ztracená odpověď se řeší receipt i po expiraci editace v definované retenční době. Expirace nevrátí publikovaný zápis, pozdější Save nezmění historický výsledek commitu. Limit registru se řeší před publikací. Restart služby zneplatní staré relace a nevede k automatickému opakování Finish. |
| A23 | Cizí Connect vyvolá informaci, pozastavení a invalidaci kontrol, i když se spojení mezi pollingy opět odpojilo. Uživatel může výslovně globálně odpojit a zopakovat kontroly. |
| A24 | Connect není blokovaný životností okna. Předchozí probe se skutečně ukončí před začátkem bootstrapu a nikdy nevzniknou paralelní data requesty. |
| A25 | Chyby rozlišují timeout, transport, autentizaci, špatný formát, tag/MAC, konflikt kontraktu, expiraci a zápis. Stará revize není chyba. Obsahují pouze povolené údaje, žádné klíče, requesty, ciphertext a tagy v UI chybách nebo logu. |
| A26 | get-status zůstává paměťový, konzistentní a bez citlivých dat. Během pomalého probe/get-info a podporovaného zatížení IPC funguje krátké řízení v mezích etapy A. Asynchronní čekání na neúplné requesty a neodebírané odpovědi nedrží kapacitu pro příkazy. Změřit get-status, přijetí i dokončení Disconnectu a nezávislý stop služby, při přetížení prostředky zůstanou omezené. |
| A27 | Nové IPC schéma je uzavřené a otestované. Neplatná operace, nadbytečné pole, cizí session/operation nebo nesprávná epocha nemají vedlejší efekt. Původních sedm příkazů projde regresí. |
| A28 | Nová tray se starou službou pravdivě oznámí chybějící podporu Wizardu. Nevytvoří config fallbackem ani nezkouší jinou pipe nebo starý secret. |
| A29 | Default je konzistentní v PHP, C++, Python testovacím klientu a normativních dokumentech. Veřejná stránka varuje i při novém defaultu a prázdném secretu, endpointy zůstávají funkční. |
| A30 | Výchozí server autentizuje všechny `.2–.15` s odpovídajícími klíči. Testovací deploy skript stále vytvoří přesně svůj deklarovaný profil a odmítne neočekávanou hlavičku. |
| A31 | Úspěšný Finish zavře Wizard a otevře placeholder právě jednou. Okno je prázdné, zavíratelné a neprovádí úpravy kontraktů. Samo nezpůsobí Connect. |
| A32 | UI je čitelné a ovladatelné na malé ploše a všech testovaných DPI, bez ořezání tlačítek a resize artefaktů. Čisté unit testy nenahrazují doloženou ruční kontrolu. |
| A33 | Normální close vyžádá okamžitý úklid. Pád/zamrznutí tray, odhlášení nebo výpadek IPC ukončí relaci po 60 s bez platného kontaktu plus definovaná tolerance časovače a konečná doba bezpečného rušení. Vyplňování udržuje relaci 15s keepalive bez HTTP, wall clock nemění lhůtu, sleep/resume a pozdní kontakt expirovanou relaci neoživí. Jiná relace i globální VPN pokračují, commit má samostatné potvrzení. |
| A34 | Všechny rušicí události D19 i souběh cancel/timeout/success/callback skončí v krátké konečné době, bez dvojího zavření, use-after-free, deadlocku a paralelních data requestů. Před nahrazením nebo uvolněním je potvrzen terminální stav a transport lze znovu použít. |
| A35 | Jediný monotónní HTTP deadline zahrne DNS, spojení, TLS, odeslání, hlavičky a celé tělo. Pomalé dílčí bloky ani neukončené tělo jej neposunou. Testy hraničních hodnot a chyby nastavení timeoutů nevytvoří záporný/nekonečný timeout. Po bezpečném úklidu se vrátí timeout a transport i síťová obsluha zůstanou použitelné. |
| A36 | Pomalý diagnostický health/info běží s vlastním transportem a časem mimo hlavní VPN síťové vlákno a nezastaví datový polling. Druhý get-info dostane busy, nevznikne neomezená fronta nebo počet vláken. Tři VPN role, fronty a provozní čítače zůstávají oddělené. |
| A37 | Edit Config po Yes vytvoří validní soubor ihned, pozdější Cancel jej nemaže. No nic nevytvoří ani neotevře nový dokument. Cíl vzniklý během potvrzení se nepřepíše, editor načte skutečný obsah nebo konkrétní chybu. Reset Yes mění jen editor, No zachová koncept a Cancel zahodí neuložené změny. Vytvoření a Reset mají stejnou šablonu 5.6 s jedním kontraktem a všemi povinnými hodnotami, Save i vytvoření projdou úplným validátorem. |
| A38 | Tray zobrazí zelený krok a pokračuje jen po doručení ověřeného úspěchu aktuálního Next před konečným termínem. Polling ani keepalive termín neobnoví. Pozdní úspěch po chybě/timeoutu a během nového pokusu UI nezmění. Nový pokus počká na bezpečné ukončení předchozí práce nebo dostane busy. Opakované či nižší attemptSequence nevykoná síť ani po Resetu/Connectu. |
| A39 | Normativní dokumenty před implementací výslovně obsahují změny matice kapitoly 11. Nezůstane obecné pravidlo, které zakazuje oddělenou diagnostiku, nutí Wizard automaticky opakovat kontrolu po 401, brání editaci nevalidního textu nebo dovoluje vytvoření souboru jen přes Finish. Výsledné schéma IPC, meze a testy odpovídají implementaci. |

## 15. Dokončení a praktická omezení

Produktová rozhodnutí D01–D26 jsou vyřešená. Technické ověření Windows publikace, transportní životnosti, absolutních timeoutů, dostupnosti IPC, pronájmů a crash scénářů je povinná součást implementace, nikoli deklarovaný hotový výsledek tohoto plánu. Bez splnění bran 12.1 nelze tyto vlastnosti označit za dokončené. Ochrana před přepsáním zastaralého obsahu není požadavkem, rozhoduje poslední úspěšná publikace.

Výstup implementace musí obsahovat aktualizované normativní dokumenty, zdroje, testy a report s přesnými provedenými/neprovedenými kontrolami. Commit, nasazení, restart instalované služby ani změna skutečného ProgramData nejsou součástí přípravy tohoto plánu.

Při přípravě a aktualizaci plánu byly prostudovány normativní dokumenty, rozhodnutí z revize, relevantní zdrojový kód a testovací infrastruktura. SHA-256 příklady jsou podkladem testů. Nebyla provedena implementace, překlad, test nové funkce ani vizuální validace neexistujícího Wizardu. Tato aktualizace mění pouze `plan3_client.md`.
