# Implementační plán 3: Configuration Wizard a Communication Contracts

Stav: plán realizace, bez provedené implementace. Zpracováno 5. září 2026 nad revizí `9f25f1333834596e46394f67f0a056a2595506da` v `C:\git\phpvpn`. Projekt zatím není nikde nasazený. Tento dokument zahrnuje rozhodnutí zadavatele z navazujícího rozhovoru a nahrazuje rozporné formulace původního zadání v rozsahu této etapy.

Výchozí podklady jsou `protocol.md`, `client.md`, `notes/2026-09-05-09-53-11_notes.processed.md`, skutečný zdrojový kód, existující testy a konvence z `plan_client.md` a `plan2_client.md`. Cesty souborů níže jsou relativní ke kořeni `C:\git\phpvpn`. Při realizaci se nejprve společně aktualizují normativní dokumenty. Historické poznámky se zpětně nepřepisují.

## 1. Výsledek a hranice etapy

Tray dostane popisný čtyřkrokový průvodce s podmíněným odpojovacím krokem. Služba vlastní načtení a uložení konfigurace i všechny síťové kontroly. Průvodce smí vytvořit nebo změnit `config.json` až při úspěšném Finish. Po úspěchu se zavře a otevře zavíratelné prázdné okno Communication Contracts. VPN se tím nepřipojí.

Součástí jsou změny serverového defaultu, rozšíření výchozí mapy přístupových klíčů, IPC, správy konfiguračního souboru, testů a dokumentace. Není součástí editor komunikačních kontraktů, automatická distribuce klíčů, rezervace IP na serveru, nové drátové endpointy, automatické připojení, implementace macOS/Linux ani změna přijatého modelu veřejného lokálního IPC.

Implementace zůstává C++20, MSVC, CMake/Ninja, Win32, WinHTTP a CNG bez nové knihovny třetí strany. Přenositelné modely nesmějí obsahovat `HWND`, `HANDLE`, Win32 zprávy ani závislost na tray procesu. Pythonové závislosti a běhy se spravují výhradně přes `uv`.

## 2. Závazná rozhodnutí zadavatele

| ID | Rozhodnutí |
|---|---|
| D01 | Přesné menu: Connect/Disconnect, oddělovač, Configuration Wizard, Communication Contracts, Edit Config, oddělovač, Show Info, Statistics, oddělovač, Exit. |
| D02 | Bez souboru zůstávají aktivní Wizard, Communication Contracts, Edit Config a Exit. Edit Config a Communication Contracts zobrazí varování místo svého běžného okna. |
| D03 | Varování uvede skutečně vyhodnocenou absolutní očekávanou cestu a možnost spustit Wizard nebo ručně zkopírovat konfiguraci získanou například od administrátora. Doslovný nerozbalený text `%ProgramData%` nestačí. |
| D04 | Instalace, start služby ani Connect nevytvářejí výchozí konfigurační soubor. |
| D05 | Opravitelný soubor je jinak platná konfigurace, které chybí pouze `virtualIp`, `accessKey` nebo obě pole. Ostatní neplatnosti, nesprávné typy, prázdná/neplatná IP, chybný klíč, neznámá pole, poškozený JSON a nečitelnost vyžadují Reset. |
| D06 | Reset zahodí celý koncept a přejde na nové výchozí nastavení, včetně TLS, CIDR, limitů a původních kontraktů. Disk se nemění. Původní revize pro kontrolu konfliktu se Resetem nezruší. |
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

Poslední rozhodnutí mají přednost před původním požadavkem na vždy přesně 13 kontraktů, rozporným umístěním položky Communication Contracts pod Edit Config, podmíněným Back na prvním kroku a obecně povolenými citlivými výpisy chyb.

## 3. Zjištěný výchozí stav a dopady

| Komponenta | Zjištění ve zdrojovém kódu | Potřebný zásah |
|---|---|---|
| `client/apps/service/service_host.cpp` | `ServiceRuntime::start` volá `ensureDefaultConfigFile`. | Odstranit automatické vytvoření, zachovat vytvoření chráněného adresáře a běh služby bez konfigurace. |
| `client/src/platform/windows/paths_win.*` | Obsahuje atomický create defaultu a adresář `PHPVPN`. | Odstranit odpovědnost za default, zachovat autoritativní vyhodnocení cesty. Velikost písmen existujícího adresáře nemigrovat. |
| `client/src/core/config/config.h` | Celý default JSON obsahuje starou placeholder URL a dokonce 64 ASCII nul v secretu. | Nahradit továrnou na koncept a jedinou konstantou nového secretu. 64 nul není totéž jako starý protokolový default 32 nul. |
| `config_validator.cpp` | Úplný validátor správně odmítá chybějící IP/klíč, neznámá pole a kontrakt na vlastní IP. | Zachovat striktní běhovou validaci, přidat oddělenou analýzu opravitelnosti. |
| `vpn_engine.cpp` | `storedConfigJson` vrací cache. `loadStoredConfig` při chybě nemaže starou cache. `setConfigJson` zapisuje sdílené `.tmp` před uzamčením cache. | Centralizovat disk, dostupnost, revize, serializaci všech zápisů a publikaci cache do ConfigStore. |
| `vpn_engine.cpp` | Connect někdy čte soubor pouze při prázdné cache. Bootstrap pak přebírá uložený snímek. | Každý nový logický Connect musí přijmout aktuální plně validní diskový snímek. Aktivní snímek a recovery stejného spojení se nepřepínají externí editací. |
| `ipc_protocol.*`, `ipc_dispatcher.*` | Uzavřených sedm příkazů, IPC v1, 4MiB limit zprávy. | Přidat konkrétní schémata Wizard příkazů a jejich parsery. Nezavádět obecný HTTP ani souborový příkaz. |
| `ipc_pipe.cpp` | Server obsluhuje jedno spojení po druhém. `get-info` provádí synchronní síť uvnitř handleru. Zápis odpovědi a flush mohou čekat neomezeně. | Oddělit dlouhé operace, zajistit omezenou souběžnou obsluhu a přerušitelné I/O. |
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
| `client/src/core/config/config_store.*` | Autoritativní snímek diskové konfigurace, revize, porovnání a serializace uložené generace. |
| `client/src/platform/iface/config_files.h` | Testovatelné rozhraní čtení a podmíněného atomického zápisu bez Win32 typů. |
| `client/src/platform/windows/config_files_win.*` | Windows souborové operace, ACL, identita souboru, flush, publish a obnova po chybě zápisu. |
| `client/src/core/wizard/wizard_types.h` | Koncept, pole, typované výsledky, identifikátory a stavy. |
| `client/src/core/wizard/wizard_model.*` | Přenositelné UI přechody, validace polí, invalidace kroků a výsledné prezentační instrukce. |
| `client/src/core/wizard/wizard_defaults.*` | Výběr navržené IP, odvození Access Key, číselné řazení IP pro SHA-256 kontraktů. |
| `client/src/core/wizard/wizard_service.*` | Omezená správa rozpracovaných relací, vazby důkazů na vstupy, revizi konfigurace a epochu spojení. |
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
- WizardService vlastní originální dokument, očekávanou revizi a síťové důkazy. Dočasné koncepty ani ověřené info se nepersistují.
- WizardModel vlastní právě zobrazený krok, lokálně editované texty a stav prezentace. Jeho lokální validace pomáhá uživateli, nikdy nenahrazuje validaci služby.
- ProbeClient nesmí mít referenci na `TrafficCounters`, fronty, `TunnelSession`, aktivní NetworkWorker ani publikované bootstrap kontrolní body.
- Služba vrací pouze potřebné typované výsledky. Celé HTTP requesty, ciphertext ani odvozené klíče neposílá do UI.
- CSPRNG vytváří UUID requestů, operací a relací. Tyto identifikátory slouží ke korelaci, nikoli jako nová autorizační hranice veřejného IPC.

### 4.3 Vlákna a plánování

Síťové operace Wizardu se vykonávají na existujícím jediném síťovém vlákně služby jako nové typované řídicí úlohy. Mají samostatný ProbeClient a transport. Tunelová vlákna se kvůli nim nespouštějí. Nebude přidána čtvrtá VPN pracovní role.

Požadavek na kontrolu se zařadí a IPC ihned vrátí `operationId`. Klient výsledek periodicky čte. V jednu chvíli poběží nejvýše jedna Wizard síťová operace globálně, další dostane `busy`, nikoli neomezenou frontu. Na jednu Wizard relaci připadá nejvýše jedna rozpracovaná operace.

`connect`, `disconnect` a stop mají přednost. Příchod nového Connect zruší probíhající Wizard transport, počká na jeho skutečné ukončení a až potom zahájí bootstrap. Nesmějí běžet dva data requesty současně. Totéž platí pro stop služby. Síťový zámek nesmí držet `stateMutex_` ani zámek souborového úložiště při HTTP nebo kryptografii.

Také jednorázový `get-info` se přesměruje na síťovou řídicí úlohu s vlastním kontextem. Původní formát jeho IPC odpovědi se zachová. Jeho čekající IPC obsluha nesmí obsadit jedinou cestu pro get-status a disconnect. V síťovém plánovači má provozní spojení přednost a dotazy get-info se nesmí hromadit bez limitu.

Pipe server dostane omezenou souběžnou obsluhu, například čtyři řídicí workery, s odděleným limitem dlouhého get-info na jednu operaci. Veškeré čtení a zápis pipe mají konečný deadline a reagují na stop. Odstranit neomezený `FlushFileBuffers(pipe)` nebo jej nahradit framingem, který už má dokončené odeslání. Limit workerů nenahrazuje časové limity klienta.

## 5. Konfigurace, dostupnost a revize

### 5.1 Typy a rozhraní

Zavést `ConfigAvailability = missing | unreadable | invalid | repairable | valid`. Nedostupná služba je samostatný UI stav, nesmí se vydávat za missing.

`StoredConfigSnapshot` obsahuje dostupnost, bezpečný kód důvodu, neprůhledný `revisionToken`, interní identitu souboru a SHA-256 přesných bajtů. Pro valid/repairable obsahuje původní DOM a originální dokument. Token se nesmí odvozovat pouze z mtime, velikosti nebo současného `ClientConfig::generation`.

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
Result<CommitReceipt> ConfigStore::commitIfUnchanged(
    const RevisionToken& expected,
    std::string_view completeValidatedJson,
    const CommitId& commitId);
```

`CommitReceipt` obsahuje `commitId`, novou revizi a stav confirmed. Souborový backend nesmí při neurčitém výsledku operačního systému tvrdit ani úspěch, ani bezpečné nezapsání bez ověření skutečného stavu.

### 5.2 Opravitelný dokument

Neoslabovat `parseAndValidateConfig`. Samostatná analýza musí prokázat, že všechny nalezené chyby jsou výhradně absence obou povolených polí. Nestačí zachytit první chybu validátoru a prohlásit zbytek za platný.

Prakticky vyčlenit sdílenou validaci struktury, známých členů, URL, secretu, TLS, CIDR, runtime limitů a kontraktů. V režimu analýzy lze vynechat kontrolu chybějící IP/klíče a vazbu kontraktů na dosud neznámou vlastní IP. Tato vazba se povinně ověří při doplnění IP a znovu při Finish. Typované neúplné údaje nesmějí vznikat jako falešně validní `ClientConfig` s dummy klíčem.

Platná existující IP nebo platný existující klíč se zachová samostatně i při absenci druhého pole. Chybějící klíč se navrhne z ponechané nebo zvolené IP. Žádný již existující platný klíč se potichu nevymění jen proto, že byla navržena IP.

### 5.3 Zachování hodnot a Reset

V režimu edit/repair provést patch pouze čtyř pojmenovaných členů `masterSetup` v původním DOM. Nezrekonstruovat dokument z `ClientConfig`, protože by se ztratilo například rozlišení vynechaných defaultů a explicitních hodnot. Povolená je změna whitespace a formátování JSON. Hodnoty, typy a přítomnost nezobrazených členů se zachovají.

Reset vytvoří nový dokument s URL `http://MODIFY-THIS-URL/phpvpn.php`, secretem `CHANGE_THIS_SERVER_SECRET`, `trustAnyTlsCertificate=true`, `vpnCidr=10.99.99.0/24`, současnými runtime defaulty a nově rozhodnutou sadou kontraktů. Runtime lze v JSON vynechat a využít sdílené defaulty. Původní dokument zůstává pouze jako baseline pro detekci konfliktu až do zrušení nebo commitu.

Při unreadable se může po Resetu vyplňovat nový koncept, ale Finish nesmí obejít nemožnost bezpečně určit původní revizi. Pokud služba stále nemůže soubor přečíst a získat stabilní snímek, zápis odmítne a navede na opravu přístupu. Reset neopravuje ACL ani nepřesměrovává cestu.

### 5.4 Čerstvost a reakce menu

Při startu načíst dostupnost. Nový `get-config-state` provede omezenou diskovou kontrolu, zatímco původní `get-status` zůstane pouze paměťový. Tray požádá o config-state po úvodním get-status, na svém nezávislém pětisekundovém cyklu, při otevření menu a po uložení. Stav nesmí určovat tray přímým `exists` na ProgramData.

Před otevřením editoru/placeholderu a před novým Connect služba znovu ověří aktuální konfiguraci. Mezi otevřením menu a kliknutím se mohla změnit. `get-config` a get-info nesmějí po zjištění neplatného disku vracet nebo používat starou uloženou cache. Aktivní spojení si svůj již přijatý snímek ponechá.

| Stav konfigurace/služby | Connect při neaktivní VPN | Disconnect při aktivní VPN | Wizard | Edit Config / Contracts | Show Info / Statistics |
|---|---|---|---|---|---|
| valid | dostupné | dostupné | dostupné | běžné okno | dostupné |
| repairable / invalid / unreadable / missing | zakázané | dostupné podle D18 | dostupné | aktivní položka, vysvětlující hláška, bez běžného okna | zakázané |
| služba nedostupná nebo stav dosud neznámý | zakázané | nelze bezpečně odeslat | otevře vysvětlení nedostupnosti, bez kontrol a Finish | vysvětlení nedostupnosti | zakázané |

Exit zůstává dostupné vždy. Otevřené Statistics po změně souboru přestanou prezentovat nový provoz jako dostupnou funkci a zobrazí stav nedostupné konfigurace bez vynulování cache. Čtení get-status samo se nezakazuje. Otevřený editor nesmí svým starým obsahem obcházet kontrolu aktuální dostupnosti při Save.

### 5.5 Podmíněný atomický zápis

Všechny zápisy služby, včetně původního set-config, musejí procházet jediným ConfigStore a stejnou serializační branou. Úspěšný set-config zvýší revizi a zneplatní baseline všech dříve otevřených Wizard relací. Wizard po konfliktu nikdy automaticky nesloučí cizí změny ani nepřejde na novější baseline. Nabídne znovu otevřít průvodce nad aktuálním souborem.

Postup Finish:

1. Ověřit živou relaci, shodu instance služby, connection epoch, síťové důkazy a přesné čtyři hodnoty. Sestavit výsledný dokument a spustit úplný validátor ještě před vytvořením temp souboru.
2. Vstoupit do serializační brány zápisu. Znovu získat skutečný diskový snímek a porovnat revizi, existenci, identitu a hash bajtů s baseline. Změna obsahu stejné délky nebo s vráceným mtime je stále konflikt.
3. Nový soubor i dočasné soubory smějí vzniknout jen v autoritativním chráněném adresáři a až po Finish. Dočasný název obsahuje náhodné ID, vzniká přes CREATE_NEW, nikdy sdíleným `config.json.tmp` s truncation. Zakázat reparse přesměrování cíle a neočekávanou identitu adresáře. Nastavit ochranu SYSTEM/Administrators dříve, než se zapíší tajné bajty.
4. Zapsat celé tělo, ověřit počet bajtů a provést `FlushFileBuffers` nad skutečným file handle. Samotné `ostream::flush` nestačí pro tento účel. Viz [Microsoft: FlushFileBuffers](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers).
5. Bez síťové práce znovu ověřit baseline a publikační podmínky. Pro původně chybějící soubor provést rename bez přepsání existujícího cíle. Pokud cíl mezitím vznikl, vrátit konflikt. Pro existující cíl použít platformní replace se zálohou v témže adresáři, s kontrolou ACL a skutečného výsledku. `REPLACEFILE_WRITE_THROUGH` nepoužívat jako záruku, protože není podporován. Viz [Microsoft: ReplaceFileW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew).
6. Úspěch publikace je commit point. Teprve potom publikovat novou uloženou generaci v paměti a receipt. Aktivní konfigurace se tím nemění. Po commitu se již nesmí vrátit běžné cancelled, jako by disk zůstal nezměněný.
7. Při chybě odstranit pouze vlastní temp soubor, nikdy cizí `config.json`. U částečných chyb replacement API ověřit polohu původního souboru a zálohy, případně bezpečně obnovit původní cíl. Neprovádět slepé mazání zálohy ani cizího nového souboru. Restart musí umět rozpoznat vlastní nedokončený commit z minimálního záznamu bez klíčů a nesmí automaticky dokončit dosud neprovedený Finish.

Atomické nahrazení a porovnání revize jsou dvě odlišné vlastnosti. Samotná dvojice přečtení hashe a `ReplaceFileW` není atomické compare-and-swap vůči libovolnému procesu zapisujícímu mimo službu. Souběh všech podporovaných IPC zapisovatelů musí být plně serializovaný. Ruční změny během editace a před publikační sekcí se povinně rozpoznají. Přímý souběžný zásah administrátora do souboru během samotné publikační sekce musí Windows backend pokrýt samostatným testem a ochranou přístupu, nesmí se za vyřešený považovat jen přidáním druhého hashe. Je to první technická ověřovací brána etapy, podrobně v části 12.1.

Zálohy mají stejná omezení přístupu jako konfigurace. Ponechat nejvýše poslední vlastní známou verzi, nevytvářet neomezenou historii secretů. Detekce konfliktu se neřeší zálohou až po tichém přepsání. Výpadek napájení a selhání média mají vlastní recovery výsledek, nelze slibovat obnovu na fyzicky nefunkčním disku.

## 6. Přenosný model průvodce

### 6.1 Stavy

`WizardMode = create | edit | repair | reset`. Kroky jsou `disconnectGate`, `serverUrl`, `serverSecret`, `identity`, `contracts`. Vedle kroku model vede `loading`, `editing`, `checking`, `waitingDisconnect`, `interruptedByConnect`, `conflict`, `committing`, `commitUnknown`, `completed` nebo `closed`.

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
| Reset | zrušit vlastní kontrolu, vytvořit nový koncept, vyčistit výsledky, zachovat původní baseline pro konflikt. Žádné diskové operace. |
| Cancel / Escape / X | mimo commit zrušit vlastní operaci a relaci, zavřít okno, vymazat koncept. Neodesílat Connect ani dodatečný Disconnect. |
| Finish | úplná službová validace a podmíněný zápis, bez dalšího data pollingu. Během krátkého commitu nelze předstírat úspěšný Cancel. |
| úspěšný Finish | receipt confirmed, zavřít Wizard, obnovit config-state a otevřít placeholder jednou. |
| chyba Finish | zůstat na contracts, zachovat koncept a zobrazit konkrétní nápravu. Při konfliktu žádné tlačítko pro tiché vynucené přepsání. |
| ztracená odpověď Finish | commitUnknown, ověřit receipt, slepě neopakovat zápis ani netvrdit, že soubor nebyl změněn. |

Pokud selže Next, uživatel zůstává na právě aktivním kroku. Chyba sítě není důvod k automatickému postupu ani k tichému používání starého zeleného výsledku. Reset a Cancel jsou dostupné i při chybě načtení, kromě krátké nerozhodnuté commit sekce.

### 6.2 Cizí Connect a epochy

Služba zavede interní nezáporný 64bitový `connectionEpoch`, zvýšený při přijetí každého nového logického Connect. Idempotentní Connect v aktivním stavu epochu nezvyšuje. Epochu resetuje pouze nová instance služby, která má nové `serviceInstanceId`. Při vyčerpání číselného rozsahu fail-closed, nikoli přetečení.

Wizard zaznamená epochu při otevření a po potvrzeném odpojení. Každá kontrola a commit kontrolují shodu. Tím se zachytí i Connect a Disconnect dokončené mezi dvěma UI pollingy. Událost nesmí být odvozena pouze z aktuálního názvu stavu.

Text informace: `Another application or user started a VPN connection while this wizard was open. Previous checks are no longer valid. To continue, choose Next to disconnect the VPN for everyone.` Neuvádět vymyšleného uživatele ani proces, veřejné IPC identitu nesleduje.

Při přerušení zachovat vyplněné texty, ukončit vlastní probe, označit síťové výsledky pending a nabídnout odpojovací bránu. Pokud je v době obsluhy již opět disconnected, stále zobrazit informaci o proběhlém Connect a vyžadovat opakování kontrol od serverUrl, ale další zbytečný Disconnect není nutný. Po výslovném odpojení a převzetí nové epochy opakovat všechny kontroly. Předchozí disková baseline se nezmění.

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

Před provedením kontroly je trvale viditelné upozornění: `This check may remove waiting messages for this IP address from the server. The messages will be discarded and will not be delivered to the VPN.` Tlačítko Next je výslovný souhlas s tímto jedním pokusem, bez dalšího povinného modálního potvrzení.

Každý pokus check-access získá čerstvý ověřený health a info ve vlastním kontextu, aby dlouhé vyplňování a změna systémového času nepoužily starý odhad či serverové limity. Potom provede nejvýše jeden prázdný data request. Všechny health používají auth-none. Žádný automatický retry data, ani při 401. Další pokus je nové stisknutí Next a znovu začíná čerstvým health. Tím se splní požadavek synchronizace po možné časové chybě bez tichého opakovaného odebírání fronty.

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
| `wizard-open` | žádná | `sessionId`, `serviceInstanceId`, `revisionToken`, `availability`, `mode`, `fields` nebo null, `context` nebo null, `contractCount`, `connectionEpoch`, `vpnState`, `vpnPhase` |
| `wizard-reset` | relace | stejný stav relace jako open, mode reset, nové defaulty, zachovaná baseline |
| `wizard-check-url` | relace, `clientAttemptId`, `serverUrl` | `operationId`, `operationState:"pending"` |
| `wizard-check-secret` | relace, `clientAttemptId`, `serverUrl`, `serverSecret` | `operationId`, `operationState:"pending"` |
| `wizard-check-access` | relace, `clientAttemptId`, `fields` | `operationId`, `operationState:"pending"` |
| `wizard-get-operation` | relace, `operationId` | aktuální typovaný OperationResult |
| `wizard-cancel-operation` | relace, `operationId` | `operationId`, stav cancelling/cancelled nebo již terminální stav |
| `wizard-resume` | relace, `connectionEpoch` | aktuální relace po potvrzeném odpojení, všechny důkazy pending |
| `wizard-finish` | relace, `commitId`, `fields`, `createDefaultContracts` | `operationId`, `operationState:"pending"`, dokončení se čte stejným get-operation |
| `wizard-close` | relace | `closed:true`, bez dopadu na VPN |

`fields` obsahuje právě čtyři stringy `serverUrl`, `serverSecret`, `virtualIp`, `accessKey`. `context` obsahuje efektivní `vpnCidr`, `trustAnyTlsCertificate` a všech osm runtime limitů. Služba je odvodí z vlastní baseline, nesmí věřit klientem podstrčenému TLS nebo CIDR. `contractCount` je nezáporné celé číslo. `revisionToken` je neprůhledný náhodný identifikátor revize v rámci instance služby, nikoli zveřejněný hash secretu.

`clientAttemptId` je UUIDv4 logického stisku Next. Opakování stejného ID se shodnými parametry vrátí stejnou operaci, s jinými parametry chybu. Brání to dvojímu data pollingu při ztrátě IPC odpovědi. Nové vědomé opakování Next má nové ID. Stejné pravidlo platí pro commitId a opakované Finish.

`OperationResult` obsahuje `operationId`, `kind` z pevného výčtu url/secret/access/finish, `operationState` z pending/running/cancelling/succeeded/failed/cancelled/interrupted, `connectionEpoch`, `error` nebo null a `payload` nebo null. Payload je uzavřený podle kind: URL jen výsledek kontroly, secret navíc případný návrh IP, access jen výsledek a počet zahozených záznamů, finish receipt s commitId a novou revizí. Text klíče odvozeného z IP může UI spočítat společnou čistou funkcí, služba jej vždy nezávisle ověří jako vstup check-access.

Každý kontrolní důkaz uložený službou váže přesné relevantní hodnoty, revizi baseline, TLS/runtime kontext, serviceInstanceId a connectionEpoch. Finish vyžaduje shodu se všemi potřebnými důkazy. Po návratu Back a změně textu tedy nelze přes IPC uložit jiné hodnoty než ty, které byly ověřené. `createDefaultContracts` nemění platnost přístupového ověření, ale služba při Finish ověří jeho přípustnost pro daný režim/CIDR.

`wizard-resume` nesmí odpojit sám od sebe ani přijmout klientem tvrzený disconnected. Služba ověří aktuální stav, fázi a nepřítomnost čekajícího Connect. Samotné globální odpojení zůstává původním příkazem disconnect.

### 8.3 Životnost a limity

Nejvýše osm rozpracovaných Wizard relací na službu, každá jeden koncept a jedna aktuální operace. Neaktivní relaci ukončit po 30 minutách bez Wizard IPC, nikoli při pravidelných čteních její aktivní operace. Na expiraci se koncept a odvozené hodnoty zahodí. Doba jedné síťové operace je omezená transportními timeouty a lze ji kdykoli zrušit.

Dokončený commit receipt se ponechá minimálně po zbývající životnost relace, aby šlo řešit ztracenou odpověď. Pád služby ruší relace a síťové důkazy. Při restartu během nejasného Finish klient načte nový config-state a zobrazí výslovné ověření výsledku, nezačne automaticky nový zápis. Trvalý commit recovery záznam identifikuje pouze publikaci a revizi, neobsahuje další kopii secretů.

Limit zprávy zahrnuje JSON escaping. Dokument, který se s obálkou nevejde do v1, odmítnout před kontrolou a zápisem s informací o velikosti, nerozdělovat jej na nezdokumentované kusy. Doplnit testy horních hranic a UTF-8. Limity responseTimeoutSeconds se při převodu do WinHTTP nesmějí násobením přetočit na záporný nebo nekonečný timeout.

### 8.4 Chyby a bezpečnost

Přidat stabilní kódy například `config_conflict`, `config_unreadable`, `wizard_session_expired`, `wizard_prerequisite_failed`, `wizard_connection_interrupted`, `wizard_commit_unknown`, `wizard_contract_self_conflict` a `wizard_unsupported`. Původní chyby neměnit na raw systémové výpisy.

Wizard chyba má strukturované `code`, hotové anglické `description`, `recoverable`, `field` nebo null, `httpStatus` nebo null, `suggestedAction` a případně povolené `serverUrl`/`configPath`. URL a cesta pocházejí z konceptu a pevného úložiště, ne z neověřeného serverového těla. Síťovou příčinu mapovat na DNS, connect, TLS, timeout, odmítnutou autentizaci, neplatný formát a neplatné kryptografické ověření. Neočekávanou HTML odpověď nespouštět ani nezobrazovat jako HTML.

Zákaz citlivého logování platí i pro detail chyby. Dnešní `Error::message()` přidává detail, proto se do běžného Error.detail nesmějí vložit requesty, klíče ani raw těla. Wizard diagnostika má vlastní povolená pole a nesmí prosakovat do get-status, progress, Statistics, tooltipu nebo loggeru. Konfigurační pole Wizardu a editoru samozřejmě smějí podle svého účelu zobrazit hodnoty, které uživatel zadává.

Zachovat vědomě veřejný přístup pro lokální aplikace, bez impersonace a bez kontroly PID. Pipe musí výslovně odmítat vzdálené klienty. Příkazy nepřijímají filesystem cestu, HTTP metodu, vlastní action, libovolné hlavičky ani spustitelný příkaz. UI nemá nové oprávnění zapisovat ProgramData. Nepřidávat síťový allowlist, který by zrušil existující podporu lokálních HTTP serverů.

## 9. Win32 UI a texty pro začátečníka

UI zůstane anglické jako současné menu. Texty budou soustředěné a testovatelné, bez čísel řádků v PHP souboru. Jedno Wizard okno na tray relaci. Opakované otevření existující okno aktivuje, nezahodí koncept ani nevytvoří druhý sessionId. Jiná interaktivní relace může mít vlastní průvodce nad stejnou baseline.

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

Příklad kolize: `This IP address is already used by an existing communication contract. A contract cannot point to this computer itself. Choose another IP address, or cancel the wizard and use Edit Config to correct the conflicting contract before trying again.` Zobrazit konkrétní kolizní virtuální IP, žádný payloadKey. Hláška je vysvětlení, nikoli dodatečné schvalování automatického odstranění.

Placeholder má titul `Communication Contracts`, běžné záhlaví, možnost zavřít a prázdnou obsahovou plochu. Neobsahuje zatím falešné editační prvky. Jeho otevření z menu provede aktuální kontrolu dostupnosti. Automatické otevření po Finish není podmínkou úspěšnosti uložené konfigurace, selhání vytvoření HWND nesmí změnit confirmed commit na neúspěšný zápis.

Wizard může mít vlastní 500ms polling stavu operace a epochy pouze po dobu otevření. Původní pětisekundový tray timer i 500ms timer Connecting progress window zůstávají nezávislé. Zavření Wizardu ukončí jeho polling. Opožděná odpověď musí projít kontrolou windowGeneration, serviceInstanceId, sessionId, operationId a clientAttemptId, než smí změnit UI.

## 10. Server, defaulty a kompatibilita

V `phpvpn.php` upravit konfigurační hlavičku a komentář, `Config::fromConstants`, `Config::withSecret`, `Config::isInsecureSecret` a `Application::isConfiguredSecretInsecure`. Zavést jedinou interní konstantu/default helper tak, aby se nemohly lišit normalizace a výchozí hodnota. Veřejná stránka musí varovat při novém defaultu, prázdném nakonfigurovaném secretu, řetězci samých ASCII nul a samých nulových bajtů. Autentizované endpointy tyto hodnoty nadále neodmítají jen kvůli jejich předvídatelnosti.

Rozšířit ACCESS_KEYS na `.2–.15`, zachovat dosavadní `.2–.4`, doplnit `.5–.9` a `.10–.15` s dvojicemi `05` až `09` a `0a` až `0f`. Není to dynamické přenastavení serveru při změně klientského CIDR. Pro jinou síť musí správce nadále sladit serverové adresní parametry a mapu.

Stejný default použít v C++ key_derivation, config_defaults, předvídatelnostním warningu, Python normalizaci a dokumentaci. Nenahrazovat plošně všechny řetězce nul, některé jsou známé AES testovací vektory nebo explicitně testovaná neprázdná tajemství.

Původní drátové formáty, kryptografické domény a serverVersion negotiation se nemění. Kombinace nové a staré implementace s explicitně stejným neprázdným secretem dál interoperuje. Prázdná hodnota má novou efektivní hodnotu, takže kombinace starého a nového defaultu interoperabilní není. Nezavádět fallback, automatický rewrite existujících configů ani dedukci secretu podle serverVersion.

Nová tray při `ipc_unknown_command` na get-config-state nebo wizard-open zobrazí, že Wizard vyžaduje aktualizovanou službu, a nic nevytváří. Původní příkazy zůstanou dostupné podle původní smlouvy, pokud je kompatibilní stav služby známý. Žádné hledání alternativní pipe ani automatický downgrade nové operace na set-config. Starý klient nové příkazy nepotřebuje, původních sedm příkazů na nové službě musí stále fungovat.

Projekt nemá nasazené uživatele, proto není potřeba migrační utilita, dlouhodobá dvojí IPC verze ani složitý rollout. Novou službu a tray distribuovat společně. Instalační skripty nesmějí zapisovat ani nepřímo vyvolat vznik configu a musí po instalaci správně popsat, že soubor vytvoří Wizard. Existující config se při reinstalaci zachová.

## 11. Aktualizace dokumentace a testovací infrastruktury

| Soubor | Konkrétní změna |
|---|---|
| `protocol.md` | 3.2 default a normalizace, 5.1 veřejné varování, související scénáře 16.3. Výslovně zachovat povahu best-effort clients a at-most-once pollingu. |
| `client.md` | konfigurace bez auto-create, opravitelnost a Reset, menu tabulka s výjimkou Disconnect, Wizard kroky a UI, výjimka diagnostiky, nové IPC schéma, plánování dočasných kontrol, pravidla čítačů, kontrakty 13/14/0, akceptační scénáře. |
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

Serverový deployment pro testy dnes používá vlastní secret a klíče. Jeho existující fixture nesmí být potichu nahrazena defaulty. Pro ověření defaultního Wizard scénáře zavést samostatný fixture profil s `.2–.15`. Generovaný `tests-integration/server.json` není normativní dokument a nemá se ručně přepisovat jako součást změny defaultu.

## 12. Pořadí implementace a kontrolní brány

### 12.1 Etapa A: smlouvy a souborová technická ověřovací brána

1. Aktualizovat normativní dokumenty podle D01–D18 a schema IPC.
2. Definovat rozhraní ConfigFiles, ConfigStore a testovací backend s deterministickými bariérami před čtením, kontrolou revize, flush a publish.
3. Na cílových Windows ověřit náhradu existujícího souboru, všechny relevantní chyby replacement API, ochranu ACL a souběžný externí writer. Praktický výsledek musí splnit: úplné staré nebo nové JSON, žádná truncation, žádná ztracená detekovaná cizí změna, zachování předchozího obsahu při hlášeném bezpečném selhání.
4. Pro externí write/rename mezi poslední kontrolou a publish ověřit vhodný krátkodobý Windows guard, případně oplock s korektním zpracováním break/ack. Oplock není prostý mutex a vlastní replacement nesmí způsobit deadlock. Chování je potřeba ověřit vůči skutečnému filesystemu. Podklad: [Microsoft: Opportunistic Locks](https://learn.microsoft.com/en-us/windows/win32/fileio/opportunistic-locks).
5. Pokud zvolený backend pro tento úzký souběh neumí požadovanou garanci, neoznačit běžné hash+replace za hotové řešení a nepokračovat k vydání s tichým oslabením požadavku. Vyřešit publikační mechanismus v této etapě, nebo před změnou rozsahu garance vrátit konkrétní technický nález zadavateli. Nejde o otevřenou volbu chování UI, ale o ověření splnitelnosti přesné bezpečnostní vlastnosti.

### 12.2 Etapa B: konfigurace a defaulty

Implementovat ConfigStore, analýzu opravitelnosti, patch dokumentu, továrnu nového konceptu, revize a zrušení auto-create. Provést související server/C++/Python změnu defaultu a serverové mapy ve stejné sadě změn. Upravit deployment fixture. Gate: kompletní lokální konfigurační a kryptografické testy.

### 12.3 Etapa C: protokolové pomocné funkce a ProbeClient

Rozdělit health parse/verify, doplnit clients, implementovat čerstvý time context a přesně jeden data probe bez front a účetnictví. Opravit auth-none při opakovaném health ve sdílené cestě. Gate: řízené odpovědi i nezávislé serverové kontraktní testy, bez spuštění tunelu.

### 12.4 Etapa D: služba a IPC

Implementovat omezené Wizard relace, operace a důkazy, epochy, priority zrušení, krátké IPC odpovědi, striktní schémata a souběžnou pipe obsluhu. Původní set-config přesměrovat do ConfigStore. Gate: souběžné get-status/disconnect během zablokovaného probe, ztracené odpovědi, restart a stale IDs.

### 12.5 Etapa E: model a UI

Nejprve čistý WizardModel, menu a layout s testy. Pak Win32 Wizard, placeholder a integrace do tray. Zachovat generace oken a nezávislé pollingové intervaly. Gate: žádný síťový nebo diskový zápis v tray, přesné menu a testy všech přechodů.

### 12.6 Etapa F: integrační a ruční ověření

Debug/Release/ASan, testy PHP a Python, test Windows souborů a pipe, čistá instalace v testovací VM, dva tray uživatelé, DPI a resize. Sestavit distribuční balíček až po průchodu relevantních bran. Zapsat výsledky a omezení do nového `client/tests/implementation-plan3-report.md`, nevydávat existující plan2 report za test této etapy.

## 13. Testovací strategie

### 13.1 Nové automatické sady

| Soubor | Smysluplné scénáře |
|---|---|
| `client/tests/unit/test_wizard_document.cpp` | valid/repairable/invalid/unreadable klasifikace, více současných chyb, zachování nezobrazených hodnot a absence polí, UTF-8/BOM, duplicitní JSON, žádný disk před Finish. |
| `client/tests/unit/test_config_store.cpp` | stejné baseline ze dvou relací, vítěz a konflikt, set-config proti Finish, ruční změna stejné délky/mtime, delete/recreate, chybějící soubor nově vznikne, selhání zápisu/flush/publish, cache až po commitu. |
| `client/tests/unit/test_config_files_win.cpp` | skutečné dočasné soubory, ACL, unikátní temp názvy, locked/unreadable cíle, reparse odmítnutí, externí writer v publikačním okně, recovery a invariant starý/nový úplný soubor. |
| `client/tests/unit/test_wizard_defaults.cpp` | výběr chybějící/nejstarší/shodné IP, prázdné clients, všech 14 přítomných, budoucí lastSeen, nevhodný CIDR, zachování ručního klíče a numerické pořadí `.2` proti `.10`. |
| `client/tests/unit/test_wizard_model.cpp` | všechny přechody, bez Back na prvním kroku, invalidace závislostí, Reset, Cancel, chyby bez postupu, cizí Connect, rychlý Connect/Disconnect, Finish unknown, stale výsledky. |
| `client/tests/unit/test_wizard_probe.cpp` | health syntax bez přijetí času, chybný tag, requestId, clients, GCM info, timeout každé části, 401/503, poškozené NDJSON/MAC, neočekávaný accepted výsledek, maximálně jeden data request. |
| `client/tests/unit/test_wizard_service.cpp` | chybějící předpoklady, podstrčené změněné vstupy při Finish, session/epoch mismatch, explicitní resume, cancel/preemption, limity relací a idempotence attempt/commit ID. |
| `client/tests/unit/test_wizard_ipc.cpp` | úplná schémata obou směrů, neznámá/duplicitní pole, typy, UUID, integer overflow, escape overhead 4MiB, korelace výsledků a filtr citlivých údajů. |
| `client/tests/unit/test_tray_menu.cpp` | přesné pořadí a separátory, tabulka dostupnosti, výjimka Disconnect, akce warning místo okna, neznámý stav není missing. |
| `client/tests/unit/test_wizard_layout.cpp` | velikosti monitorů, DPI 100/125/150/200 %, dlouhé texty, stabilní dolní tlačítka, žádné překryvy, scroll obsahové části. |
| `client/tests/contract/test_server_contract.cpp` | skutečný PHP relay: URL, secret, info.clients, `.2–.15`, polling s čekajícími zprávami, potvrzení jejich spotřeby bez tunelu a default kompatibilita. |

Testy modelů nemají jen zrcadlit implementaci. Použít tabulkové scénáře odvozené z D01–D18 a explicitní invarianty. Kryptografické vektory porovnat s nezávislým PHP/Python výpočtem, ne s druhým voláním stejné C++ funkce. Pro souběh používat bariéry a kontrolované dokončení, nikoli náhodné čekání a naději, že se race projeví.

### 13.2 Povinné regrese

- Přepsat `test_default_config.cpp`: po startu služby a neúspěšném Connect soubor nevznikne. Platný dokument vzniká továrnou pro Finish, nikoli `ensureDefaultConfigFile`.
- Rozšířit `test_crypto.cpp`, `tests.php` a `test_crypto_vectors.py`: empty → nový default, `0`, 32/64 ASCII nul, NUL bajty a neprázdný custom secret beze změny. Ověřit všechny relevantní odvozené klíče a veřejné varování.
- Zachovat `test_runtime_status.cpp`, `test_status_contract.cpp`, `test_traffic_counters.cpp` a `test_winhttp_accounting.cpp`. Před a po každém probe porovnat všechny globální kontrolní body, čítače a aktivní stav. Výslovné disconnect a cizí Connect mají své původní legitimní resetovací účinky, nesmějí se zaměnit za vedlejší efekt probe.
- Ověřit, že normal Connect stále dešifruje a doručuje payload z bootstrap pollingu. Wizard režim zahození se nesmí stát defaultem společného parseru nebo NetworkWorker.
- Ověřit opakovaný health s existujícím TimeSnapshot proti skutečnému serveru, který přijímá pouze auth-none.
- Ověřit původních sedm IPC příkazů, přesný formát get-info/get-config, nezměněný status parser a intervaly tray/progress.
- Pro data probe s platným MAC a payloadem, který nelze dešifrovat neexistujícím kontraktem, očekávat úspěšné ověření přístupu a zahození. Pro chybné MAC očekávat neúspěch bez jakéhokoli doručení.
- Simulovat změnu systémových hodin, dlouhé vyplňování, návrat Back a jiný server. Čas ani ověřené clients se nesmějí přenášet mezi odlišnými důkazy.

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

Čistá instalace a start služby bez souboru. Všechny čtyři aktivní položky bez konfigurace. Varování ukazuje skutečnou cestu i při nestandardním ProgramData. Ruční kopie configu se projeví bez restartu služby. Neplatná a nečitelná konfigurace mají odlišné hlášky.

Průchod novým, existujícím i resetovaným konceptem. Zrušení na každém kroku a během čekající kontroly. Zavření a znovuotevření při opožděné odpovědi. Selhání Finish bez zavření okna. Úspěšný Finish otevře placeholder právě jednou a nevytvoří tunel. Opakované otevření Wizardu nezahodí koncept.

Dva interaktivní uživatelé, jedna služba. Cizí Connect v každém kroku i těsně před Finish, rychlý Connect/Disconnect, současný Save editoru a Finish druhé relace. Viditelná informace o cizím Connect a žádné automatické opětovné připojení. Zmizelý/poškozený config při connected zachová provoz a dostupné Disconnect.

Změny velikosti ze všech hran a rohů, malé pracovní plochy, taskbar na různých stranách, DPI 100/125/150/200 %, přesun mezi monitory, tabulátor, čitelnost vysvětlení a návrat fokusu po chybě. Stejná kontrola resize regresí editoru jako v plan2. Evidence výsledků obsahuje OS, build, DPI, scénář, výsledek a testera, žádné klíče ani screenshoty v repozitáři.

## 14. Přesná akceptační kritéria

| ID | Požadovaný pozorovatelný výsledek |
|---|---|
| A01 | Čistá instalace, start služby a Connect bez configu nevytvoří `config.json`. Služba poskytuje IPC a zůstane provozuschopná pro konfiguraci. |
| A02 | Menu má přesně pořadí D01. Bez konfigurace jsou aktivní Wizard, Contracts, Edit Config, Exit. Obě konfigurační položky zobrazí správné varování a neotevřou běžné okno. |
| A03 | Warning obsahuje absolutní cestu získanou službou, instrukci Wizard → úspěšný Finish a možnost zkopírovat existující config. Chybějící služba není hlášena jako chybějící soubor. |
| A04 | Invalid/unreadable/repairable blokují běžné funkce. Běžící VPN pokračuje na aktivním snímku a Disconnect zůstává dostupné. Samotná detekce disku VPN neodpojí. |
| A05 | Wizard nepíše konfiguraci, zálohu ani temp JSON před Finish. Cancel a Reset před Finish nezmění původní soubor bajtově. |
| A06 | Opravitelnost je pouze absence IP/klíče při jinak platném dokumentu. Nesprávné typy nebo další chyby vyžadují Reset. Úplný validátor nepřijme neúplnou konfiguraci pro Connect. |
| A07 | Edit/repair zachová všechny nezobrazené hodnoty a kontrakty, včetně explicitních TLS/runtime hodnot a vynechaných volitelných polí. Reset je vědomě nahradí novými defaulty až při Finish. |
| A08 | Odpojovací Next čeká na skutečné dokončení. Server URL nemá Back. Zrušení a dokončení Wizardu neobnoví VPN. |
| A09 | Krok URL uspěje na syntakticky platném running health i s nesouhlasným GCM tagem, ale nepublikuje čas ani kryptografické ověření. Chybný formát a requestId neprojdou. |
| A10 | Krok secret vyžaduje nový ověřený health, validní TimeSnapshot, ověřený a dešifrovaný info včetně použitelného clients. Jediná chyba ponechá uživatele v kroku. |
| A11 | Checkbox secretu odpovídá přesné nové výchozí hodnotě. Prázdná a jiné neprázdné hodnoty se v souboru nepozorovaně nemění, UI zachová přesné bajty secretu. |
| A12 | Navržená IP je nejnižší chybějící kandidát, jinak nejstarší lastSeen s číselným tie-break. clients není prezentováno jako rezervace IP. Při prázdné kandidátní množině je vyžádána ruční IP. |
| A13 | Access Key má přesně požadovaný formát. Návrh z posledního oktetu dává například 32×02 pro `.2` a 32×0a pro `.10`. Ruční a existující klíč se automaticky nepřepíše. |
| A14 | Kolizní vlastní IP se odmítne před data kontrolou. Hláška vysvětlí příčinu a možnosti nápravy. Existující kontrakt zůstane beze změny. |
| A15 | Před data kontrolou je viditelné upozornění na spotřebování zpráv. Jeden logický pokus provede nejvýše jeden prázdný data request, včetně ztracené IPC odpovědi. |
| A16 | Access kontrola přijme pouze úplnou syntakticky správnou a MAC ověřenou odpověď se správným requestId a nulovým výsledkem pro prázdný request. Obsah datových zpráv nedešifruje, nedoručí a neuchová. |
| A17 | Žádný probe nezmění aktivní VPN, bootstrap body, runtime čas, fronty ani devět provozních čítačů. Neotevře Wintun a nevytvoří route. |
| A18 | Create/reset s `10.A.B.0/24` vytvoří přesně 13 nebo 14 kontraktů podle vlastní IP, správné klíče mapy a uvedené SHA-256 vektory. Jiný CIDR nebo odškrtnutá volba vytvoří prázdný objekt a UI vysvětlí výsledek. |
| A19 | Finish přijme jen poslední prokázané čtyři hodnoty a kompletně validní výsledný JSON. Změna vstupu po úspěšném Next bez nového ověření přes IPC neprojde. |
| A20 | Souběžný set-config, Finish druhé relace, ruční změna nebo nově vzniklý/smazaný soubor vedou u zastaralé baseline na konflikt. Není provedeno tiché přepsání ani automatické přebázování po Resetu. |
| A21 | Čtenář při běžné atomické publikaci uvidí celý původní nebo celý nový JSON. Simulované chyby zápisu/flush/replacement nezanechají truncation, veřejně čitelné temp tajemství ani falešný úspěch. Souborová brána 12.1 je doložená. |
| A22 | Opakování commitId nepíše znovu. Ztracená odpověď se řeší receipt, restart služby zneplatní staré relace a nevede k automatickému opakování Finish. |
| A23 | Cizí Connect vyvolá informaci, pozastavení a invalidaci kontrol, i když se spojení mezi pollingy opět odpojilo. Uživatel může výslovně globálně odpojit a zopakovat kontroly. |
| A24 | Connect není blokovaný životností okna. Předchozí probe se skutečně ukončí před začátkem bootstrapu a nikdy nevzniknou paralelní data requesty. |
| A25 | Chyby rozlišují timeout, transport, autentizaci, špatný formát, tag/MAC, konflikt a zápis. Obsahují pouze povolené údaje, žádné klíče, requesty, ciphertext a tagy v UI chybách nebo logu. |
| A26 | get-status zůstává paměťový, konzistentní a citlivá data neobsahuje. Během pomalého probe funguje get-status, disconnect a zrušení UI bez čekání na HTTP timeout. |
| A27 | Nové IPC schéma je uzavřené a otestované. Neplatná operace, nadbytečné pole, cizí session/operation nebo nesprávná epocha nemají vedlejší efekt. Původních sedm příkazů projde regresí. |
| A28 | Nová tray se starou službou pravdivě oznámí chybějící podporu Wizardu. Nevytvoří config fallbackem ani nezkouší jinou pipe nebo starý secret. |
| A29 | Default je konzistentní v PHP, C++, Python testovacím klientu a normativních dokumentech. Veřejná stránka varuje i při novém defaultu a prázdném secretu, endpointy zůstávají funkční. |
| A30 | Výchozí server autentizuje všechny `.2–.15` s odpovídajícími klíči. Testovací deploy skript stále vytvoří přesně svůj deklarovaný profil a odmítne neočekávanou hlavičku. |
| A31 | Úspěšný Finish zavře Wizard a otevře placeholder právě jednou. Okno je prázdné, zavíratelné a neprovádí úpravy kontraktů. Samo nezpůsobí Connect. |
| A32 | UI je čitelné a ovladatelné na malé ploše a všech testovaných DPI, bez ořezání tlačítek a resize artefaktů. Čisté unit testy nenahrazují doloženou ruční kontrolu. |
| A33 | Ukončení tray, odhlášení a pád okna ruší pouze jeho dočasný koncept/operace. Služba a případné spojení jiné relace pokračují. Opožděné výsledky neovlivní nově otevřené okno. |

## 15. Dokončení a praktická omezení

Produktová rozhodnutí D01–D18 jsou vyřešená. Technické ověření konkrétní Windows publikace, ručního souběhu a crash scénářů je povinná součást implementace, nikoli deklarovaný hotový výsledek tohoto plánu. Bez splnění souborové brány 12.1 nelze označit požadavek na atomický zápis a ochranu proti konfliktům za dokončený.

Výstup implementace musí obsahovat aktualizované normativní dokumenty, zdroje, testy a report s přesnými provedenými/neprovedenými kontrolami. Commit, nasazení, restart instalované služby ani změna skutečného ProgramData nejsou součástí přípravy tohoto plánu.

Při přípravě plánu byl prostudován relevantní zdrojový kód a testy a nezávisle vypočteny SHA-256 příklady. Nebyla provedena implementace, překlad, test nové funkce ani vizuální validace neexistujícího Wizardu. Jediný nově vytvořený projektový soubor této plánovací etapy je `plan3_client.md`.
