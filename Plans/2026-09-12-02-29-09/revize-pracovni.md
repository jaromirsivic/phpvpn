---
plan_version: 3
created_at: 2026-09-12-07-28-24
based_on: plan-v002.md
source: ../../Intents/2026-09-12-02-29-09/intent-v004.md
---

# Plán konsolidace implementovaných změn klienta phpvpn

Plán obsahuje celý rozsah intentů v1–v4 a nová uživatelská rozhodnutí z [rozhodnuti-v004.md](rozhodnuti-v004.md). R01–R03 a R05–R10 jsou uzavřená, **R04 zůstává výslovně odložené**. Dokumentační větev D00–D17 lze realizovat samostatně. B01–B05 je konkrétní návrh budoucí implementace pre-poll backpressure podle intent-v004, dosud není dodaná. TT01–TT20 je samostatně rozhodnutelná testovací větev, testy B04 jsou však povinnou akceptací nové implementace.

Autoritativní zadání tvoří [intent-v001.md](../../Intents/2026-09-12-02-29-09/intent-v001.md), [intent-v002.md](../../Intents/2026-09-12-02-29-09/intent-v002.md), [intent-v003.md](../../Intents/2026-09-12-02-29-09/intent-v003.md) a [intent-v004.md](../../Intents/2026-09-12-02-29-09/intent-v004.md). Novější explicitní rozhodnutí mají přednost před historickými zdroji i otevřenými závěry plánu v2. Koordinátor potvrdil uživatelem schválené **unlimited**. Žádní další agenti nevznikají.

Plánovací fáze změnila pouze tuto složku Plans. Kód, testy, client.md, protocol.md, tray_UX.md a historické plány/notes zůstaly nedotčené. Nebyly spuštěny buildy, testy, GUI, služba ani instalační operace. Existence testu a statický důkaz z kódu jsou oddělené od výsledku jeho běhu.

## Podklady, autorita a hranice

- Projekt: `C:\git\phpvpn`, přímý uložený checkout, HEAD při revizi v3 `529d1c4e53ec48ea95e96c663a62f99af479d5bc`. Oproti předchozímu průzkumu se kód, testy ani tři cílové dokumenty nezměnily, ověřeno diffem obou revizí.
- Úplný registr a chronologie: [priskok-matice.md](priskok-matice.md), jednotlivé T01.md až T29.md. Rozklad všech šestnácti zdrojů: [pokryti-zdroju.md](pokryti-zdroju.md).
- Závazné pořadí: plan_client, plan2_client, plan3_client, plan3_fix, plan3_fix2, plan3_fix3, plan3_fix3_problems, plan3_fix3_problems2, plan3_fix3_work_for_agents, potom všech sedm notes přesně podle registru. Datum notes se nepoužívá k přesunu před plány.
- Do dokumentace vstupuje dodané chování s oporou v původních zdrojích nebo nových explicitních rozhodnutích. Již správný popis se ponechá. Budoucí backpressure se označí jako dodané teprve po B05, jiné nedodané vlastnosti se nepřebírají.
- Cíle zápisu při následné dokumentační realizaci: `client.md`, související funkční a redakční změny `tray_UX.md`, průběžná matice. Pro `protocol.md` nebyla nalezena potřebná změna. Jeho kontrola je povinná, nulový diff je platný výsledek.
- Zápisy do kódu/testů, installeru, distribution.md, zdrojových plánů, notes, assets, dist a ProgramData nejsou součástí dokumentační větve. Zdrojové soubory se nemažou. Jediná plánovaná kódová změna je B01–B05, tento plánovací úkol ji neprovádí. Testovací větev zůstává samostatně rozhodnutelná.
- Při obnovení kontextu musí realizátorka nejprve obnovit celý účinný globální blok instrukcí z reálného CODEX_HOME/AGENTS.override.md nebo AGENTS.md, případně z úplného hostem dodaného bloku, a ověřit obě přesné hranice před věcnou prací. Tento plán chráněný blok nenahrazuje. Další kontext ani worktree nevzniká bez explicitního souhlasu.

## Pořadí dokumentační realizace

### D00: Ověření výchozího stavu

- [ ] Přečíst tento celý plán, matici a cílové dokumenty. Porovnat HEAD a `git status --short` s výše uvedeným stavem. Při změně příslušného kódu znovu ověřit jen dotčené tematické důkazy, nepřepsat novější uživatelskou práci.
- [ ] Uchovat obsahové hashe tří cílů a šestnácti zdrojů pro kontrolu rozsahu. Marker a nové intent v4 patří koordinátorovi, předchozí Plans/Intents již byly commitované. Nepřepisovat jeho práci.
- [ ] Odděleně evidovat realizaci dokumentační, implementační a testovací větve. Použít uzavřená rozhodnutí bez nového potvrzování, R04 ponechat odložené.

### D01: Současná Windows architektura a sestavení

**Cíl:** client.md, Architektura a nová krátká podčást Sestavení klienta před platformními návody. **Vazba:** [T01](T01.md), S01 §1–6, S03 §4, S07 bod 7, S09 WP01/WP06.

- [ ] Upravit odstavec začínající „Platformní implementace poskytne pouze rozhraní Tunnel“. Dodaná Windows vrstva poskytuje také WinHTTP transport a CNG crypto. libcurl a jiné platformní varianty uvést pouze jako budoucí možnost, ne současnou závislost.
- [ ] Popsat C++20, MSVC, CMake nejméně 3.28, Ninja a statický CRT podle `client/CMakeLists.txt`. Doplnit rozdělení core/platform/apps a skutečné výstupy service, tray a CLI `phpvpn.exe`. CMake target CLI se jmenuje `phpvpn-cli`, název targetu není název EXE.
- [ ] Uvést význam presetů `debug`, `release` = RelWithDebInfo a `asan` podle `client/CMakePresets.json`. Existenci `arm64-release` nevydávat za ověřené distribuční ARM64 vydání. Přesný postup izolovaného skutečného Release GUI už existuje, pouze na něj odkázat.

**Důkaz:** `client/CMakeLists.txt`, `client/src/CMakeLists.txt`, `client/apps/CMakeLists.txt`, `client/CMakePresets.json`. Build systém a platformní adaptéry patří do klientské specifikace, ne wire protokolu ani UX copy.

### D02: Přesné konfigurační meze

**Cíl:** client.md, Konfigurační soubor, odstavec se čtyřmi runtime limity a navazující responseTimeoutSeconds. **Vazba:** [T02](T02.md), S01 §8, S03 §4.4/B01.

- [ ] Nahradit plošné tvrzení „neexistuje globální horní strop“ přesným rozlišením. responseTimeoutSeconds je kladné celé číslo nejvýše **2147483 sekund**. Zachovat default 30 s a absolutní monotónní význam.
- [ ] Doplnit **maxNdjsonLineBytes <= maxResponseBodyBytes**. Rovnost je dovolená, překročení se odmítá při úplné validaci, včetně repair analýzy používající tutéž validaci.
- [ ] Zachovat ostatní typové/default/budget meze a sdílený /24–/29 rozsah. Nedoplňovat neexistující horní mez timeSyncIntervalSeconds, R01 tyto nedoložené garance vyjímá, viz D11.

**Důkaz:** `client/src/core/config/config_validator.cpp::validateConfigValues`, `client/src/core/config/config.h`, sdílené kMaxHttpTimeoutSeconds. Testový podklad `client/tests/unit/test_config.cpp`. Protokolové doporučení 15 s se nemění, nejde o stejnou konfigurační hodnotu.

### D03: Jeden payloadKey pro oba směry

**Cíl:** client.md, Konfigurační soubor, výklad communicationContracts/payloadKey. **Vazba:** [T03](T03.md), S01 §2.7 a §9/14.

- [ ] Doplnit, že jediný konfigurační payloadKey daného peeru používá tento klient pro odeslání i příjem. Směr rozlišuje AAD se src a dst.
- [ ] Zachovat odkazy na obecný wire kontrakt protocol.md a atomické odmítnutí celé vadné IPv4 dávky. Nezavádět do protokolu univerzální povinnost všech implementací používat stejný klíč oběma směry.

**Důkaz:** `client/src/core/runtime/network_worker.cpp`, příjem `ingestRecord` a odchozí použití kontraktu, `client/src/core/protocol/payload_batch.cpp`. Testy crypto/wire/payload zůstávají oddělené od budoucí integrační vazby TT03.

### D04: Počet odchozích datových záznamů

**Cíl:** client.md, Velikostní a spravedlivé plánování. **Vazba:** [T05](T05.md), S01 §2.12/§13.

- [ ] K rozpočtu maxRequestBodyBytes minus 1024 doplnit současné respektování **maxDataRecordsPerRequest z ověřené odpovědi info**. Počet záznamů je další mez, nenahrazuje přesné změření těla.
- [ ] Zachovat FIFO, kruhovou spravedlnost a permanentní drop neodeslatelného čela. Maximum běžného pollingu zůstává 5 s, aktuální a budoucí admission rozlišují D12 a B01–B05.

**Důkaz:** `client/src/core/queues/scheduler.cpp::planNextRequest`, `client/tests/unit/test_queues.cpp`. Jde o klientské plánování, proto bez nové tabulky v protocol.md.

### D05: Životnost tří VPN rolí

**Cíl:** client.md, Windows Service, konzole a tray, odstavec příkazu connect. **Vazba:** [T06](T06.md), S01 §2.2/§7, S03 D21.

- [ ] Nahradit větu, že „tunel, tři pracovní role“ startují teprve po třech bootstrap krocích. Síťová role vzniká se startem engine a trvá do shutdown. Tunel a jeho dvě I/O role vznikají po úspěšném health/info/prázdném data bootstrapu.
- [ ] Ponechat idempotenci Connectu, snapshot a checkpointy. Oprava životnosti nesmí současně potvrdit nedodaný dohled pádů rolí nebo zachování tunelu při recovery, podle R03, viz D13.

**Důkaz:** `client/src/core/runtime/vpn_engine.cpp::connect/runConnectSequence/runNetworkThread/disconnect`, `client/src/core/runtime/tunnel_workers.cpp`.

### D06: Konkrétní IPC kapacity

**Cíl:** client.md, Společná pravidla rozšíření IPC, nová podčást Implementační meze lokálního řízení. **Vazba:** [T10](T10.md), S03 §4.3/§8/B01/B10.

- [ ] Zapsat 32 slotů včetně listenerů, nejvýše 16 čekajících shortControl úloh, 4 čekající file úlohy a jednu diagnostic úlohu celkem včetně právě běžící. Každá třída má samostatný executor. U prvních dvou limitů nezapočítávat aktivní úlohu do počtu čekajících.
- [ ] Zapsat absolutní I/O deadline 5000 ms a maintenance tick 250 ms nebo dříve. Zachovat limit 4 MiB zakódované zprávy a lokální pipe s odmítnutím vzdálených klientů.
- [ ] Odkazy na „meze B01“ v související společné HTTP/IPC části a testech přesměrovat na tento nadpis. Nevytvářet z počtu slotů neověřenou záruku celkového RSS nebo dostupnosti při libovolném zahlcení.

**Důkaz:** `client/src/platform/windows/ipc_pipe.cpp::submit`, kSlots/kIoDeadlineMs/kTickMs, `ipc_pipe.h`, klasifikace v `client/apps/service/service_host.cpp`. Relevantní test je `client/tests/unit/test_ipc_availability_win.cpp`, existence není nový výkonový výsledek.

### D07: Rozpočet Next a retence potvrzení

**Cíl:** client.md, Životnost a limity Wizard IPC a odkazy v Povinných testech konfigurace, Wizardu a IPC. **Vazba:** [T16](T16.md), [T17](T17.md), S03 §8.3/B01/B11.

- [ ] Místo „lokální rozpočty stanovit v B01“ uvést **1/2/3 × responseTimeoutSeconds × 1000 + 15000 ms** pro URL/secret/access. Výchozí celkové rozpočty jsou **45/75/105 s**. Výpočet i součet s monotónním počátkem kontrolují přetečení. Polling a keepalive je neobnovují. IPC start má 5000 ms.
- [ ] Místo neurčené kapacity/retence uvést nejvýše **128 receiptů**, uchování po živou relaci a možnost odstranění terminálního výsledku až po **600000 ms od uzavření relace**. Pending/neterminální operace se jen kvůli uplynutí této doby neodstraní.
- [ ] Zachovat rezervaci místa před zápisem, idempotenci, neměnnou historickou publikaci a zákaz automatického retry. Zachovat Finish bez automatického Contracts podle R07.

**Důkaz:** `client/src/core/wizard/wizard_model.cpp::beginCheck`, `client/apps/tray/wizard_reader.cpp`, `client/src/core/wizard/wizard_types.h`, `wizard_service.cpp::closeLocked/expireLocked/finishTask`. Hodnoty patří do lokálního IPC, nikoli protocol.md.

### D08: Textové logování

**Cíl:** client.md, nová podčást Logování a diagnostika u klientského provozu. **Vazba:** [T29](T29.md), S01 §18, zpřísnění bezpečných polí S02/S03.

- [ ] Popsat aktivní soubor `phpvpn-service.log` v podadresáři `logs` datového adresáře služby. Produkční datový kořen se odvozuje z ProgramData, není natvrdo předpokládaná absolutní cesta.
- [ ] Uvést rotační práh **8 MiB** a nejvýše **pět historických souborů** `phpvpn-service.1.log` až `.5.log`. Rotace se zkouší před zápisem dalšího celého řádku, pokud by překročil práh. Neformulovat práh jako garanci úspěšného diskového I/O nebo limit libovolně dlouhé jediné zprávy.
- [ ] Navázat na stávající zákaz citlivého obsahu. Logger přijímá již bezpečný text, není automatický filtr tajemství. Event Log a zvláštní startup warning se nepřidávají jako dodané funkce.

**Důkaz:** `client/src/core/base/logger.cpp/.h`, `client/apps/service/service_host.cpp::start`, `client/src/platform/windows/paths_win.cpp::logDirectory`. UI a protokol nepotřebují duplikaci lokálního logu.

### D09: Samostatnost dokumentů vůči historickým plánům

**Cíle:** client.md a tray_UX.md. **Vazba:** [T28](T28.md), S03 §11, S06 N01–N09, S08 bod 6, S09 WP07, aktuální intent v1.

- [ ] V Architektuře client.md odstranit historické omezení „WP01 samo není implementací GUI ani Save“. Zachovat rozdělení konkrétních modelů, pohledů a Save controlleru.
- [ ] Ve Společném HTTP transportu nahradit odkaz na budoucí B01 popisem implementovaných mezí D02/D06/D07 a oddělenou povinností měřeného ověření. Odkaz B07 u rušení nahradit pojmem asynchronní WinHTTP transport a příslušnými testy. Univerzální horní mez čekání na HANDLE_CLOSING nedokládat, viz T07/R03.
- [ ] V Atomickém zápisu odstranit neurčitý odkaz „B01, B03 a B04“ a odkázat na vlastní storage kontrakt a konkrétní ConfigStore/ConfigFiles testy z T12. ACL sjednotit podle R06/D15 na záměrné Users read.
- [ ] V Sdílené Win32 infrastruktuře a Přenosném jádru Contracts nahradit N/F/WP autorství konkrétními komponentami. Stejně v tray_UX.md UX02 odstranit větu o dodání WP02/WP03–WP05. Zachovat hotové technické významy a skutečné názvy testových filtrů, i když obsahují wp03 apod.
- [ ] V testovacích oddílech odlišit povinnost testu od historického výsledku. Historický report může zůstat jako datovaná evidence, nesmí být jediným místem definice aktuálního kontraktu. Přesné runtime/visual meze ponechat v UX14 a odkázat na ně z client.md.
- [ ] V Izolovaném vývoji a náhledu GUI zachovat již kompletní návod, parametry, oddělený skutečný Release a odkaz na UI README. Odstranit pouze závislost na označení F09/A32. Nevkládat duplicitní kapitolu.
- [ ] Poslední Doporučené pořadí implementace upravit tak, aby nevyzývalo znovu implementovat již dodané Windows části. Ponechat stručně budoucí platformní adaptéry, nepřesouvat neověřené pádové/instalační garance mezi hotové. Historickou výzvu „před implementací nasadit dokumenty“ nahradit zachovaným technickým upozorněním na nekompatibilitu agregovaného payloadu se starým formátem.

Tento krok je redakční konsolidace doložených částí, nikoli hromadná náhrada všech budoucích časů. Budoucí porty, nedodané vlastnosti a odložené R04 zůstávají odlišeny. Nezavádět nové skutečnosti jen podle reportů mimo zdrojový seznam.

### D10: Oddělené testovací profily

**Cíl:** client.md, krátký odstavec v části sestavení/testování. **Vazba:** [T14](T14.md), S03 §10/§11/B05/A29/A30.

- [ ] Uvést existenci profilů `integration` a `wizard-default` skriptu `tools/deploy-test.ps1`. První zachovává vlastní fixture secret a čtyři klíče, druhý používá nový default a `.2–.15`. Hlavička se před publikací ověřuje jako právě jeden očekávaný blok. Odkázat na `tests-integration/README.md`, nevkládat produkční deployment příkaz jako součást běžného čtení specifikace.
- [ ] Nedotknout se generovaného server.json, PHP ani deployment skriptu.
- [ ] Po dokončení D17 provést kontrolu V01–V06 níže, aktualizovat u každého D kroku matici na doplněno/opravena přesná část. R04 zůstává odložené, backpressure plánované až do B05. Ostatní R kroky označit podle skutečné úpravy dokumentace.

### D11: Doložená synchronizace času, R01

**Cíl:** client.md, Synchronizace času, její povinné testy a výklad timeSyncIntervalSeconds. **Důkaz:** T04, `client/src/core/runtime/vpn_engine.cpp::runNetworkThread`, TimeSync a R01.

- [ ] Vyjmout recovery po samotném selhání periodického health. Popsat aktualizaci lastHealthVerified a zachování předchozího ověřeného snapshotu při neúspěchu. Nezaměnit s recovery po chybě data requestu.
- [ ] Vyjmout nedoložené OS sleep/hibernate/resume a network-change triggery i jejich testovací povinnosti. Zachovat implementovanou detekci změny wall clock, ale popsat skutečné místo v sériové connected smyčce. Neslibovat událostní prioritu před každým dalším autentizovaným requestem.
- [ ] Zachovat default 300 s a dodanou časovou matematiku. Nepřidávat domyšlenou horní mez intervalu ani tvrzení o bezpečnosti všech extrémních hodnot. Z R01 nevzniká implementace těchto corner cases.

### D12: Polling 5 s a pravdivý stav backpressure, R02

**Cíl:** client.md, Pollingový interval, Příjem dat bod 7 a testy front. **Důkaz:** T05, `client/src/core/runtime/network_worker.cpp::nextPollDelayMs`, intent-v004.

- [ ] Zachovat maximum běžného pollingového intervalu 5000 ms. Není to povinnost odeslat data alespoň jednou za 5 s i při kapacitní blokaci.
- [ ] Před dodáním B větve vyjmout nynější tvrzení o již běžící pre-poll ochraně z popisu aktuálního provozu, případně je výslovně označit plánováno. Runtime admission každého přijatého paketu je dodaná a zůstává normativní.
- [ ] Současný low=max(20,serverMin), high=min(5000,serverMax) nevydávat za bezpečný oboustranný clamp všech extrémů. B03 naplní schválené 5s maximum i na hranách. Teprve B05 označí novou ochranu jako dodanou.

### D13: Skutečný recovery lifecycle, R03

**Cíl:** client.md, Účel, Běhový model, Stavový automat připojení a související testy. **Důkaz:** T06/T07, `client/src/core/runtime/vpn_engine.cpp` a `client/src/core/runtime/tunnel_workers.cpp`.

- [ ] Nahradit obecný fault-proof slib obnovy pádu libovolné role popisem skutečně obsluhovaných provozních chyb.
- [ ] Popsat teardown tunelu a front při recovery, potom opakovaný bootstrap a nové vytvoření. Přijatý config a původní úvodní checkpointy se zachovají, čekající pakety zrušených front nejsou garantovaně uchovány.
- [ ] Uvést skutečný backoff 1/2/4/8/16/32/60 s s až 20 % jitter. Inkrementace před výpočtem po chybě znamená první čekání 2 s. Nezaměnit s 5s data pollingem.
- [ ] Neplánovat catch/monitor rolí ani zachování tunelu. Čekání INFINITE na HANDLE_CLOSING není důkaz univerzální konečné horní meze úklidu.
- [ ] Nedotknout se Wintun integrity, journalu a pořadí cleanup z odloženého R04. R03 mění popis lifecycle enginu, R04 se tím neřeší.

### D14: Současná instalace, R05

**Cíl:** client.md, Distribuce a instalace ve Windows, instalační odstavce upgrade/rollback a související testové sliby. **Důkaz:** T09, `client/tools/make-dist.ps1`, `client/tools/package/install.ps1`, `client/tools/package/uninstall.ps1`.

- [ ] Popsat kontrolu manifestu/podpisu/hashů a balíčku, VerifyOnly, kopii do releaseVersion, přepnutí current.generation a následnou správu služby a autostartu tray v pořadí skutečného skriptu.
- [ ] Odstranit slib plně transakčního upgrade/rollback, crash-safe obnovy pointeru+SCM a startovního podpisového ověření celé generace, které kód nemá. Neimplementovat jiný installer.
- [ ] Zachovat offline balíček, licence, phpvpn.exe, běžné zachování dat při uninstall a explicitní RemoveData. Dokumentační realizace nic neinstaluje.
- [ ] Testy se omezí na skutečný VerifyOnly a běžné instalační vazby. Transakční crash recovery se nenavrhuje jako dodaná funkce, R04 zůstává odložené.

### D15: Schválené ACL a vyřazené notes, R06/R07

**Cíl:** client.md, bezpečnost/config/startup a navazující UX. **Důkaz:** T13/T15/T17/T18/T23/T25 a rozhodnutí uživatele.

- [ ] Ponechat či sjednotit Users read nad datovým stromem včetně historických souborů. Nevracet SYSTEM/Administrators-only. Čtení Users není nové právo zápisu, zachovat ochranu publikace.
- [ ] V matici označit notes bez odpovídající implementace jako irelevantní pro konsolidaci. V cílech zachovat Finish bez placeholderu/auto Connect, nedestruktivní Reset, CIDR validaci, současné menu a nemodální Password.
- [ ] Žádné staré UX chování ani jeho testy se nevracejí, zdrojové notes se nemažou.

### D16: High Contrast a výsledkové dirty, R08/R09

**Cíle:** client.md, Sdílená Win32 infrastruktura/Contracts, tray_UX.md UX02/UX11/UX12. **Důkaz:** T19/T24/T25, `client/apps/tray/ui_resources.cpp::update`, `client/src/core/presentation/contracts_model.cpp::ContractRow::dirty`.

- [ ] Popsat obnovu palety při změně High Contrast/systémových barev podle kódu. Při stejném DPI/assets se mění brushes/pens/paleta, fonty a ikony se zachovají. Již správné pasáže neduplikovat.
- [ ] Dirty je rozdíl výsledného publikovatelného stavu od původního. Změna enabled je dirty, při enabled se porovnává klíč. Stejný klíč po Random/default/Password nebo návrat k původní hodnotě nevynucuje dirty. Samotná editace klíče vypnutého nezměněného řádku se nepublikuje a dirty nezpůsobí.
- [ ] Odstranit pravidlo dirty z pouhé události generování. Zachovat saveUnknown, generace a společný Exit, toto rozhodnutí nepotlačuje nejistý výsledek Save.

### D17: Deset hodnot Statistics, R10

**Cíle:** client.md, prezentace Statistics a test 34, tray_UX.md UX00/UX07. **Důkaz:** T22, `client/apps/tray/statistics_model.h`, `client/tests/unit/test_wp03_models.cpp`.

- [ ] Zachovat devět službových čítačů a IPC typy. Desátá UI hodnota Total size je odvozený součet request/response body, nikoli nové IPC pole.
- [ ] Zapsat karty 3/3/4 a pořadí statisticsFields: Empty polls, Polls with payload, Total polls, Request body, Response body, Total size, Packets sent, Bytes sent, Packets received, Bytes received.
- [ ] Počty paketů/pollů jsou přesná celá čísla s anglickým seskupením. Bajty pod 1000 jsou v B, vyšší v desetinných kB/MB/GB/TB/PB se dvěma desetinnými místy, round-half-up a přechodem vyšší jednotky při zaokrouhlení přes její hranici. Nezaměnit s KiB ani tvrdit, že zaokrouhlené zobrazení zachová přesný počet bajtů.
- [ ] Součet se provede celočíselně i nad UINT64_MAX pomocí horního přenosového bitu a teprve potom formátuje. Zachovat jediný snapshot, stale text a existující timer.
- [ ] Connected for nově nedokumentovat, R10 výslovně schvaluje hodnoty a jednotky. Tento další objev zůstává mimo rozsah konsolidace.
- [ ] Vizuální akceptace původních mockupů má výslovnou schválenou odchylku Statistics podle R10. UI README není cílový soubor této větve, jeho staré tvrzení o devíti hodnotách zaznamenat jako navazující drobnost bez automatického rozšíření zápisu.

## Budoucí implementace pre-poll backpressure

**Stav: plánováno, dosud nedodáno.** Jediná nová kódová větev vychází z intent-v004. D větev může skončit před ní s pravdivým označením nedodání. B větev nepřidává nové vlákno, IPC ani wire změnu.

### B01: Přesná kapacitní podmínka

- [ ] Použít DirectionQueues::canAdmitAnotherPacket pod jejím mutexem s PayloadBatch::kMinPacketBytes, tedy 20 bajty. Jde o prostor alespoň pro další platný IPv4 paket, ne rezervaci celého maxResponseBodyBytes, která by mohla běžný provoz zablokovat trvale.
- [ ] Posoudit globální limit a rezervace pro nejhorší existující frontu i dosud nevytvořeného peera, pokud zbývá peer slot. Dnešní helper zkouší při neúplné množině pouze nový peer, což není nejhorší případ, když existující fronta již překročila rezervaci.
- [ ] Pod stejným zámkem použít admitsLocked pro nejvíce obsazenou existující frontu a případný nový peer. Vyžadovat přijetí v obou případech. Kontrola nesmí vytvořit frontu/buffer ani změnit účetnictví.
- [ ] Je to konzervativní předběžná brzda. Neznáme rozdělení celé odpovědi a runtime admission každého paketu zůstává nutná. Větší paket nebo celá dávka mohou dostupné místo stále překročit, nejde o slib bezeztrátovosti.

**Soubory:** `client/src/core/queues/packet_queues.cpp`, `client/src/core/queues/packet_queues.h`, `client/tests/unit/test_queues.cpp`.

### B02: Napojení před běžný data polling

- [ ] V `client/src/core/runtime/vpn_engine.cpp::runConnectedIteration` ověřit kapacitu před NetworkWorker::exchangeData, tedy před schedulerem, odebráním outbound paketů, vytvořením requestu a jeho účetnictvím.
- [ ] Při blokaci nevolat exchangeData, nezměnit poll/request/response/IPv4/drop čítače ani lastDataVerified na úspěch/chybu, nespouštět recovery a nemanipulovat frontami. Odloží se také odesílání outbound, protože data request současně polluje.
- [ ] Zůstat connected a využít existující přerušitelné čekání/takeCommand síťové smyčky. Znovu ověřit po dalším běžném intervalu, nejvýše 5 s. Bez busy loop, nový thread ani timer není potřeba. Disconnect/stop čekání přeruší.
- [ ] TunnelOutputWorker dál odčerpává inbound. Po uvolnění místa pokračuje běžný request bez nového bootstrapu. Periodický health a řízení zůstanou dostupné.
- [ ] První bootstrap/recovery access check nad čerstvými frontami zachová svou skutečnou autentizaci. Skipped nesmí znamenat úspěšný access check. Wizard ProbeClient používá vlastní zahazovanou diagnostiku a do této provozní brzdy se nezapojuje.

**Soubory:** `client/src/core/runtime/vpn_engine.cpp`, případně malý interní skipped/performed výsledek v `client/src/core/runtime/vpn_engine.h`, pokud jej čekání potřebuje. ExchangeData deferred/error cestu neměnit.

### B03: Pětisekundová mez čekání

- [ ] V `client/src/core/runtime/network_worker.cpp::nextPollDelayMs` nastavit low=clamp(serverMin,20,5000), high=clamp(serverMax,20,5000). Pro validované min<=max zůstane low<=high. Zachovat stávající adaptaci/reset při provozu.
- [ ] Ověřit extrémy celé serverové dvojice pod 20 a nad 5000. Výsledné běžné čekání bude vždy 20–5000 ms, bez změny wire schématu.
- [ ] Kapacitní blokace může přeskočit více těchto intervalů. Maximum 5 s nesmí přebít admission gate.

### B04: Povinné testy, vazba TT05

- [ ] Unit helper: volná, přesně dostatečná a nedostatečná kapacita pro 20 bajtů, globální limit, rezervace, nový peer, všechny materializované fronty a existující peer nad rezervací s volnými peer sloty. Po kontrole stejné bytes, FIFO, počty front a drops.
- [ ] In-process runtime s fake transportem a zadrženým tunnel writerem: zaplněný inbound, nulové nové data requesty během blokace i s outbound backlogem, nezměněné čítače/checkpoint. Uvolnění writeru obnoví polling bez bootstrapu, FIFO zachováno.
- [ ] Fake clock/commands: čekání bez busy loop, promptní Disconnect/stop při plné frontě. Bez dlouhého skutečného sleep v unit testu.
- [ ] Hrany serverových intervalů pod 20, uvnitř a nad 5000 vracejí 20–5000 ms. Nevyžadovat odeslání při trvající blokaci.
- [ ] Zachovat deferred/error návrat na outbound čelo po skutečně zahájeném requestu, přesně jeden handoff a partial response accounting. Gate nic předčasně neodebírá.
- [ ] Bootstrap/access a Wizard diagnostika nejsou vydány za úspěch přes skipped větev. Větší odpověď stále používá per-packet admission, test neslibuje rezervaci celé dávky.

**Soubory:** `client/tests/unit/test_queues.cpp`, `client/tests/unit/test_runtime_status.cpp`. Použít současné fake transport/driver a harness, žádný nový framework ani skutečná VPN závislost.

### B05: Akceptace a dokumentace po dodání

- [ ] Spustit nenulové cílené filtry queue/runtime a relevantní deferred/error/Disconnect regrese, sestavit dotčený core/service target. Příkazy, počty a výsledky uložit do nové evidence.
- [ ] Teprve potom změnit client.md Příjem dat bod 7 a matici z plánováno na dodáno. Popsat 20bajtový práh, oba invarianty, neznámou kapacitu celé odpovědi a zachované runtime drop pravidlo.
- [ ] Samostatně potvrdit B stav. Dokončená D větev není důkaz hotového backpressure.

## Registr uzavřených a odložených rozhodnutí

Úplná autorita je v [rozhodnuti-v004.md](rozhodnuti-v004.md). Historické důkazy se zachovají, uzavřené volby se znovu nepotvrzují.

| ID | Aktuální stav | Realizační vazba |
|---|---|---|
| R01 | Uzavřeno, nedoložené corner cases vyjmout. | D11, TT04 |
| R02 | Uzavřeno, 5 s a budoucí backpressure. | D12, B01–B05, TT05 |
| R03 | Uzavřeno, teardown/recreate. | D13, TT06/TT07 |
| R04 | **Odloženo, otevřené, nyní bez realizace a bez nových testů.** | T08, vyloučeno z D/B změn |
| R05 | Uzavřeno, současný jednoduchý installer. | D14, TT08 |
| R06 | Uzavřeno, Users read. | D15, TT10 |
| R07 | Uzavřeno, neimplementované notes irelevantní. | D15, T15/T17/T18/T20/T23/T25 |
| R08 | Uzavřeno, dynamická HC paleta. | D16, TT15 |
| R09 | Uzavřeno, výsledkové dirty. | D16, TT17 |
| R10 | Uzavřeno, 10 hodnot a jednotky. | D17, TT16/TT19 |

Samostatně **neimplementováno, nepřenášet**: nový půlkruhový asset S16, plaintext placeholder payload klíče S13, Event Log a explicitní startup warning z T29, hotové macOS/Linux porty a distribuční ARM64 slib. S12 je nerelevantní historický webový průzkum. Tyto výsledky jsou součástí pokrytí, nikoli opomenuté funkce.

## Samostatná větev testovacích změn

Tato tabulka je návrh budoucí práce, žádný řádek netvrdí nově provedený PASS. „Zachovat“ znamená, že nový test není odůvodněný konsolidací. U podmíněného doplnění nejprve zkontrolovat existující konkrétní scénář a přidat jen chybějící vazbu. Použít uzavřená rozhodnutí. Novou funkci implementuje pouze B větev, R04 se netestuje jako dodané. Neopravovat jiný kód pod názvem dokumentační práce.

| ID / témata | Existující pokrytí | Budoucí změna a rozsah | Důvod/riziko |
|---|---|---|---|
| TT01 / T01 | CMake registruje samostatné core modely a testy bez oken. Binární důkaz zde nebyl spuštěn. | Bez nového unit testu. Při samostatně schváleném build ověření sestavit core/service/CLI/tray a zkontrolovat linkerovou mapu nepoužitých modelů. | Architektonická závislost se nedokazuje kopií CMake v testu. |
| TT02 / T02 | test_config.cpp, test_wizard_document.cpp, test_config_store.cpp mají CIDR/budget a publikační gate. | Doplnit pouze chybějící rovnost NDJSON/body a +1, přesné timeout maximum/+1, full i repair cestu. | Hraniční kombinace musí být odmítnuta před HTTP. |
| TT03 / T03 | test_crypto.cpp, test_base64url.cpp, test_iso8601.cpp, test_wire.cpp, test_payload_batch.cpp mají pevné vektory a atomický decoder. | Úzký NetworkWorker test: validní plus adresně vadný paket v jedné dávce, nulové přijetí obou. Oba směry stejného klíče s odlišným AAD a negativní záměnou src/dst. Neopakovat již existující scénář. | Integrační vazba decoder/fronty může být chybná i se správným decoderem. |
| TT04 / T04 | TimeSync unit testy, runtime bootstrap scénáře. | Hraniční časové scénáře jen pro dodanou matematiku. Po R01 nové testy odstraněných failure/resume/extrémních garancí neplánovat. Testy dodané matematiky zachovat. | Nesmí vzniknout falešné potvrzení chybějící recovery. |
| TT05 / T05 | test_queues.cpp pokrývá FIFO, rezervace, count/size, drop a rotaci. | Řízený NetworkWorker+DirectionQueues scénář souběžného příjmu a deferred/error návratu na čelo. Přidat B04 pro admission gate, odklad/obnovení, FIFO/účty/stop a interval20–5000. Backpressure dosud není implementované. | Pořadí a účetnictví při návratu z HTTP. |
| TT06 / T06 | test_runtime_status.cpp má idempotenci, snapshot, recovery checkpointy a reset. | Doplnit jen chybějící start/disconnect/reconnect/shutdown počty role/session přes fake driver. Podle R03 ověřovat teardown/recreate a skutečný backoff, nikoli zachování tunelu a obecný catch. | Riziko dvojího bootstrapu nebo ztráty přijatého snapshotu. |
| TT07 / T07 | test_winhttp_accounting.cpp má partial errors, trickle deadline a cancel/reuse, platform_plan3_probe closing callback. | Windows integrační doplnění pro dosud neizolovanou DNS/send fázi a cancel versus success. Jeden výsledek, žádné dvojí účetnictví, opětovné použití. | Životnost callbacků a bufferů, ne univerzální důkaz bounded OS callbacku. |
| TT08 / T08–T09 | Existují fake ownership/installer a opt-in skutečné Windows/E2E cesty, podrobnosti v tématech. | Zachovat izolované již dodané invarianty. R04 je odložené bez nových testů. R05 vyřazuje transakční crash recovery. Ověřit aktuální VerifyOnly: hash, podpis, chybějící/nadbytečný soubor, nulové instalační účinky. Reálný SCM/Wintun scénář má vlastní prostředí. | Unit fake nenahrazuje správu adaptéru a transakční filesystem/SCM. |
| TT09 / T10–T11 | test_ipc.cpp, test_wizard_ipc.cpp, test_ipc_availability_win.cpp, test_status_contract.cpp, test_traffic_counters.cpp, runtime a WinHTTP účetnictví. | Nový test kvůli textu není nutný. Při zásahu do suite znovu použít definovaný load/snapshot/short-control scénář s nenulovým počtem testů. | Kapacity, unsigned rozsah, přesné jednorázové účetnictví. |
| TT10 / T12–T13/T21 | test_config_store.cpp a test_config_files_win.cpp mají UTF-8, identity, race, flush/publish/crash. Startup a nativní editor mají další cílené testy. | Jedna akceptace skutečného startup missing → default → editor Cancel zachová soubor, druhý start nemění bajty. Správný izolovaný NTFS/token, jinak NOT RUN. Podle R06 zachovat Users read včetně historie a ochranu proti neoprávněnému zápisu. | Integrace hostu není testem samotného VpnEngine::start. |
| TT11 / T14 | C++ defaults/crypto, Python test_server_secret_normalization a test_deploy_profiles.ps1 již testují nuly, dvě fixture a hlavičku. | Nedublovat. Jen pokud chybí, ověřit autentizaci všech `.2–.15` proti izolovanému skutečnému PHP. | Test generátoru konfigurace není síťová interoperabilita. |
| TT12 / T15 | test_plan3_fix2.cpp, test_wizard_document.cpp, model/IPC/runtime pokrývají tři preference, fallback, aditivní patch a počty. | Zachovat, doplnit pouze chybějící úplnou vazbu uložené false/auto návrh/finish/nové open. Ne duplikaci všech osmi kombinací. | Metadata nesmí změnit runtime klíče nebo smazat původní kontrakty. |
| TT13 / T16–T17 | runtime testy mají single data/no retry, lease, epochy, idempotenci/capacity/retenci receipts. wp04 lifecycle ukončení. | Pokud chybí UI vazba: ztracený start reply → cancel-attempt → opožděný start, nula nových HTTP. Finish call counts převzít z existující akceptace. R07 nevyžaduje návrat k neimplementovaným notes. | Nevratné odebrání zpráv a dvojí publikace. |
| TT14 / T18 | Menu/model, čtečky a IPC availability ověřují jednorázové čtení a nezávislou cestu Disconnect. | Bez nové obecné suite. Převzít skutečný supported-load scénář z T10 pro přijmutí a dokončení Disconnect. | Pouhé kliknutí na UI nedokazuje doručení službě. |
| TT15 / T19 | test_tray_geometry.cpp, test_tray_infrastructure.cpp a check_icons.py pokrývají model/geometrii/resources/shell/bitmapy. | Ruční screen-reader/keyboard, více monitorů/unplug/DPI, 500 paintů a 20 cyklů podle UX14. Podle R08 ověřit změnu palety se zachovanými fonty/ikonami při stejném DPI, doplnit pouze pokud chybí. | Čistá geometrie nenahrazuje skutečnou přístupnost. |
| TT16 / T20–T23 | test_tray_progress.cpp, test_wp03_models.cpp, test_wp03_windows.cpp, test_wp03_save.cpp, test_wp03_save_windows.cpp, Wizard model/layout/wp04. | Bez redundantních unit testů. Převzít vizuální akceptaci Connect/Editor/Wizard z TT19. Podle R10 zachovat test deseti hodnot, doplnit jen chybějící hrany 999/1000, zaokrouhlení, přechody jednotek a součet až 2×UINT64_MAX. | Terminální success/error, NUL/selection, unknown Save a skutečné rozměry. |
| TT17 / T24–T26 | test_contracts_model.cpp, test_contracts_document.cpp, test_password_key.cpp, test_wp05_windows.cpp a lifecycle testy. | Zachovat většinu. Titul a neměnný cíl ověřit čtečkou/přepínačem aplikací, společný Exit reentry a systémový konec převzít z existujícího izolovaného testu. Podle R09 zachovat výsledkové dirty. Pokud chybí, ověřit stejný potvrzený hash/Random, návrat k původní hodnotě, disabled-only změnu bez dirty a odlišný enabled klíč se změnou dirty. | Špatný cílový klíč, ztráta draftu nebo falešný rollback. |
| TT18 / T27 | UI harness wp06 scénáře/cleanup, test_tray_application.cpp a explicitní endpointy, test-hidden-launch.ps1. | Malá shell akceptace first/no-op/incremental, foreign cwd/spaces, cache/PE/compile failure bez starého EXE. Doplnit pouze nezachycenou větev. | Izolace preview a pravdivé selhání buildu. |
| TT19 / T28 | Existují opt-in UI/resource/server/E2E targety. Jejich existence není měření. | Samostatný report 11 mockupů s výslovnou odchylkou Statistics dle R10 při 100% klientské ploše, tolerance 6 DIP geometrie/8 DIP text, 125/150/200% funkční kontrola. Skutečný Release warmup + nejméně 3 běhy, nejhorší výsledek, CPU/paměť/GDI/USER a binární delta dle UX14. Každý neprovedený scénář NOT RUN. | Vizuální a výkonové vlastnosti nelze odvodit z unit PASS nebo z přejmenování RelWithDebInfo. |
| TT20 / T29 | V inventáři nebyl nalezen samostatný logger test. | Malý test v dočasném adresáři pro rovnost/překročení rotačního prahu, pět historií, celé souběžné řádky a sekundární sink. Nový soubor například client/tests/unit/test_logger.cpp, registrace v client/tests/CMakeLists.txt. | Neomezený růst nebo ztráta diagnostiky. Nevyžaduje ProgramData ani Event Log. |

Testovací realizace vede vlastní seznam existuje/pokrývá/spuštěno a přesné příkazy/počty. Před psaním ověří, zda se mezera mezitím nezaplnila. Pro Python balíčky platí uv. Žádný test nesmí potřebovat produkční ProgramData jako fixture. Jediná nově plánovaná implementace je B01–B05 a její testy jsou nutné pro tvrzení o dodání.

## Validace a akceptace dokumentační větve

- [ ] **V01 Rozsah:** `git diff --name-only` a `git status --short` ukážou pouze povolené dokumentační změny a pracovní artefakty této větve. Hashe všech šestnácti zdrojů zůstanou stejné. protocol.md zůstane beze změny, pokud nové rozhodnutí nevytvoří konkrétní důvod.
- [ ] **V02 Dohledatelnost:** každý D krok odkazuje na T záznam, zdrojovou část, skutečný symbol/soubor a přesný cílový nadpis. Aktualizovaná matice zachová i neimplementované/nejasné výsledky a přednost pozdějších zdrojů.
- [ ] **V03 Odkazy a názvy:** ověřit všechny nové lokální odkazy, velikost písmen tray_UX.md, phpvpn.exe versus CMake target a skutečné názvy helperů. Souborové příklady nesmějí odkazovat na neexistující config_text.cpp nebo wizard_document.cpp.
- [ ] **V04 Terminologie a čísla:** cílené `rg` na B01/B03/B04/B07, WP01–WP07, F09/A32, „před implementací“, „horní strop“, maxDataRecordsPerRequest, 2147483, 600000 a 128. Výskyty vyhodnotit významově, ne mechanicky odstranit historické názvy skutečných testů nebo poznámku o nedodání.
- [ ] **V05 Konzistence:** porovnat klientské limity s obecným wire protokolem, /24–/29, 252/4 versus 13/14 peerů, 4MiB IPC versus 512KiB dokument, přesné ASCII/Unicode/klíčové příklady a rozdíl receipt versus současná shoda DOM. Opačné věty k uzavřeným rozhodnutím nahradit, ne ponechat vedle nového textu. R04 zůstane odložené a B stav pravdivě plánováno/dodáno.
- [ ] **V06 Formát:** spustit `git diff --check`, ověřit Markdown odkazy a tabulky. Nevytvářet unit test zrcadlící text dokumentace. Pro čistě dokumentační větev není potřebný build, skutečná VPN ani úplná platformní testovací brána.

Výstup realizace uvede konkrétní změněné sekce, výsledky V01–V06, neprovedené testy a samostatný stav B01–B05. Dokumentační větev je dokončená po D00–D17 a jejich validaci, backpressure pouze po B05. R04 zůstává výslovně odložené, jeho nevyřešení neblokuje ostatní schválenou práci, ale nedovoluje tvrdit, že lze odstranit celý historický obsah. Odstranění zdrojů není součástí tohoto plánu a vyžaduje samostatný pokyn.