---
plan_version: 1
created_at: 2026-09-12-03-26-43
source: ../../Intents/2026-09-12-02-29-09/intent-v003.md
---

# Plán konsolidace implementovaných změn klienta phpvpn

Plán navazuje společně na [intent-v001.md](../../Intents/2026-09-12-02-29-09/intent-v001.md), [intent-v002.md](../../Intents/2026-09-12-02-29-09/intent-v002.md) a jejich doplnění v3. Obsahuje celou dokumentační práci, samostatně rozhodnutelný návrh testů a registr rozporů. Produktová rozhodnutí R01–R10 nejsou tímto plánem schválena. Konkrétní nesporné kroky D01–D10 lze realizovat samostatně, sporné věty se do rozhodnutí nemění. Celkové prohlášení, že historické zdroje jsou obsahově nadbytečné, je těmito rozpory zatím blokované.

Plánovací fáze změnila pouze tuto složku Plans. Kód, testy, client.md, protocol.md, tray_UX.md a historické plány/notes zůstaly nedotčené. Nebyly spuštěny buildy, testy, GUI, služba ani instalační operace. Existence testu a statický důkaz z kódu jsou oddělené od výsledku jeho běhu.

## Podklady, autorita a hranice

- Projekt: `C:\git\phpvpn`, přímý uložený checkout, HEAD při dokončování průzkumu `abb05dac68c9ac18f6658e184ef8b11aa0b0bf56`.
- Úplný registr a chronologie: [priskok-matice.md](priskok-matice.md), jednotlivé T01.md až T29.md. Rozklad všech šestnácti zdrojů: [pokryti-zdroju.md](pokryti-zdroju.md).
- Závazné pořadí: plan_client, plan2_client, plan3_client, plan3_fix, plan3_fix2, plan3_fix3, plan3_fix3_problems, plan3_fix3_problems2, plan3_fix3_work_for_agents, potom všech sedm notes přesně podle registru. Datum notes se nepoužívá k přesunu před plány.
- Do dokumentační změny vstupuje pouze klasifikace „implementováno, ale chybí nebo je popsáno nesprávně“. Již správný popis se ponechá. Nedodané požadavky ani samotná nezdrojovaná implementace se nepřenášejí jako norma.
- Cíle zápisu při následné dokumentační realizaci: `client.md`, omezený redakční zásah `tray_UX.md`, průběžná matice. Pro `protocol.md` nebyla nalezena nesporná potřebná změna. Jeho kontrola je povinná, nulový diff je platný výsledek.
- Zápisy do kódu/testů, installeru, distribution.md, zdrojových plánů, notes, assets, dist a ProgramData nejsou součástí dokumentační větve. Zdrojové soubory se nemažou. Testovací větev níže vyžaduje samostatné rozhodnutí o realizaci.
- Při obnovení kontextu musí realizátorka nejprve obnovit celý účinný globální blok instrukcí z reálného CODEX_HOME/AGENTS.override.md nebo AGENTS.md, případně z úplného hostem dodaného bloku, a ověřit obě přesné hranice před věcnou prací. Tento plán chráněný blok nenahrazuje. Další kontext ani worktree nevzniká bez explicitního souhlasu.

## Pořadí dokumentační realizace

### D00: Ověření výchozího stavu

- [ ] Přečíst tento celý plán, matici a cílové dokumenty. Porovnat HEAD a `git status --short` s výše uvedeným stavem. Při změně příslušného kódu znovu ověřit jen dotčené tematické důkazy, nepřepsat novější uživatelskou práci.
- [ ] Uchovat obsahové hashe tří cílů a šestnácti zdrojů pro kontrolu rozsahu. Výchozí untracked `.intent-builder-active`, Intents a Plans nejsou oprávnění je mazat, přesouvat nebo commitovat.
- [ ] Odděleně evidovat schválení dokumentační a testovací větve. Rozpory R se nepovažují za vyřešené schválením nesporných D kroků.

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
- [ ] Zachovat ostatní typové/default/budget meze a sdílený /24–/29 rozsah. Nedoplňovat neexistující horní mez timeSyncIntervalSeconds, tento rozpor patří R01.

**Důkaz:** `client/src/core/config/config_validator.cpp::validateConfigValues`, `client/src/core/config/config.h`, sdílené kMaxHttpTimeoutSeconds. Testový podklad `client/tests/unit/test_config.cpp`. Protokolové doporučení 15 s se nemění, nejde o stejnou konfigurační hodnotu.

### D03: Jeden payloadKey pro oba směry

**Cíl:** client.md, Konfigurační soubor, výklad communicationContracts/payloadKey. **Vazba:** [T03](T03.md), S01 §2.7 a §9/14.

- [ ] Doplnit, že jediný konfigurační payloadKey daného peeru používá tento klient pro odeslání i příjem. Směr rozlišuje AAD se src a dst.
- [ ] Zachovat odkazy na obecný wire kontrakt protocol.md a atomické odmítnutí celé vadné IPv4 dávky. Nezavádět do protokolu univerzální povinnost všech implementací používat stejný klíč oběma směry.

**Důkaz:** `client/src/core/runtime/network_worker.cpp`, příjem `ingestRecord` a odchozí použití kontraktu, `client/src/core/protocol/payload_batch.cpp`. Přesná cesta posledního souboru se při realizaci ověří inventářem, pokud aktuální checkout použije jiné umístění. Testy crypto/wire/payload zůstávají oddělené od budoucí integrační vazby TT03.

### D04: Počet odchozích datových záznamů

**Cíl:** client.md, Velikostní a spravedlivé plánování. **Vazba:** [T05](T05.md), S01 §2.12/§13.

- [ ] K rozpočtu maxRequestBodyBytes minus 1024 doplnit současné respektování **maxDataRecordsPerRequest z ověřené odpovědi info**. Počet záznamů je další mez, nenahrazuje přesné změření těla.
- [ ] Zachovat FIFO, kruhovou spravedlnost a permanentní drop neodeslatelného čela. Polling interval a chybějící runtime volání inbound admission neřešit v této změně, viz R02.

**Důkaz:** `client/src/core/runtime/scheduler.cpp::planNextRequest`, `client/tests/unit/test_queues.cpp`. Jde o klientské plánování, proto bez nové tabulky v protocol.md.

### D05: Životnost tří VPN rolí

**Cíl:** client.md, Windows Service, konzole a tray, odstavec příkazu connect. **Vazba:** [T06](T06.md), S01 §2.2/§7, S03 D21.

- [ ] Nahradit větu, že „tunel, tři pracovní role“ startují teprve po třech bootstrap krocích. Síťová role vzniká se startem engine a trvá do shutdown. Tunel a jeho dvě I/O role vznikají po úspěšném health/info/prázdném data bootstrapu.
- [ ] Ponechat idempotenci Connectu, snapshot a checkpointy. Oprava životnosti nesmí současně potvrdit nedodaný dohled pádů rolí nebo zachování tunelu při recovery, viz R03.

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
- [ ] Zachovat rezervaci místa před zápisem, idempotenci, neměnnou historickou publikaci a zákaz automatického retry. Nedotknout se sporné návaznosti UI v R07.

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
- [ ] V Atomickém zápisu odstranit neurčitý odkaz „B01, B03 a B04“ a odkázat na vlastní storage kontrakt a konkrétní ConfigStore/ConfigFiles testy z T12. Nepřepsat ACL část R06.
- [ ] V Sdílené Win32 infrastruktuře a Přenosném jádru Contracts nahradit N/F/WP autorství konkrétními komponentami. Stejně v tray_UX.md UX02 odstranit větu o dodání WP02/WP03–WP05. Zachovat hotové technické významy a skutečné názvy testových filtrů, i když obsahují wp03 apod.
- [ ] V testovacích oddílech odlišit povinnost testu od historického výsledku. Historický report může zůstat jako datovaná evidence, nesmí být jediným místem definice aktuálního kontraktu. Přesné runtime/visual meze ponechat v UX14 a odkázat na ně z client.md.
- [ ] V Izolovaném vývoji a náhledu GUI zachovat již kompletní návod, parametry, oddělený skutečný Release a odkaz na UI README. Odstranit pouze závislost na označení F09/A32. Nevkládat duplicitní kapitolu.
- [ ] Poslední Doporučené pořadí implementace upravit tak, aby nevyzývalo znovu implementovat již dodané Windows části. Ponechat stručně budoucí platformní adaptéry, nepřesouvat neověřené pádové/instalační garance mezi hotové. Historickou výzvu „před implementací nasadit dokumenty“ nahradit zachovaným technickým upozorněním na nekompatibilitu agregovaného payloadu se starým formátem.

Tento krok je redakční konsolidace doložených částí, nikoli hromadná náhrada všech budoucích časů. Budoucí porty, nedodané vlastnosti a R01–R10 zůstávají odlišeny. Nezavádět nové skutečnosti jen podle reportů mimo zdrojový seznam.

### D10: Oddělené testovací profily a závěrečná kontrola

**Cíl:** client.md, krátký odstavec v části sestavení/testování. **Vazba:** [T14](T14.md), S03 §10/§11/B05/A29/A30.

- [ ] Uvést existenci profilů `integration` a `wizard-default` skriptu `tools/deploy-test.ps1`. První zachovává vlastní fixture secret a čtyři klíče, druhý používá nový default a `.2–.15`. Hlavička se před publikací ověřuje jako právě jeden očekávaný blok. Odkázat na `tests-integration/README.md`, nevkládat produkční deployment příkaz jako součást běžného čtení specifikace.
- [ ] Nedotknout se generovaného server.json, PHP ani deployment skriptu.
- [ ] Provést kontrolu V01–V06 níže, aktualizovat u každého D kroku matici na doplněno/opravena přesná část. Sporné položky zůstávají nejasné nebo neimplementované, nikoli doplněno.

## Registr rozhodnutí mimo nespornou realizační větev

Rozhodnutí musí dodat uživatel přes koordinátora. Následné schválení jiné produktové normy nebo opravy kódu je změna rozsahu, podle významu vyžaduje nový intent nebo novou úplnou verzi plánu. Do té doby zachovat text a důkaz, nic automaticky nesjednocovat.

| ID | Rozpor a důkaz | Co vyjasnit před změnou |
|---|---|---|
| R01, T04 | Periodický health failure pouze mění příznak, nespouští požadovanou recovery. Chybí události resume/network-change. Násobení timeSyncIntervalSeconds nemá doloženou bezpečnou horní mez. | Zda existující požadavek označit jako nedodaný, nebo objednat samostatnou opravu. Nepopisovat jako hotový mechanismus. |
| R02, T05 | S01 limit 2000 ms proti kódu/client.md 5000 ms, odlišný clamp. canAdmitAnotherPacket nemá runtime volajícího před poll. | Autorita číselné odchylky a osud příslibu předběžného backpressure. |
| R03, T06/T07 | Teardown tunelu/front při transient chybě proti zachování tunelu, jiný backoff, chybí dohled pádů rolí a obecný catch. WinHTTP čeká na closing callback s INFINITE, takže statický kód nedokazuje univerzální konečnou horní mez úklidu. | Oddělit skutečný recovery stav od požadovaných garancí. Rozhodnout popis nedodání nebo samostatnou implementaci, nevytvořit nový bezpečnostní slib. |
| R04, T08 | Runtime Wintun integrita/verzní rozsah, přesný ring backoff, robustnost journalu, pořadí cleanup a ověření relay routy nejsou dodány podle celé normy. | Zachování požadavků se stavem nedodání, nebo nový implementační záměr. |
| R05, T09 | Installer ověřuje podpisy, ale nepokrývá celou deklarovanou transakci upgrade/rollback a mění current.generation před zastavením služby. | Neprohlásit podepsanou kopii za plný transakční upgrade. Vyjasnit normu versus následnou opravu. |
| R06, T13 | Zdrojové plány SYSTEM/Administrators-only, současný kód a client.md umožňují Users čtení datového stromu včetně historických klíčů. | Dodat autoritu změny ACL, nebo samostatně rozhodnout zamýšlenou politiku. |
| R07, T15/T17/T18/T20/T23/T25 | Později řazené původní notes opakují placeholder po Finish, destruktivní Reset, absenci CIDR kontroly, staré menu/vzhled a modalitu Password. Implementace odpovídá dřívějším podrobným opravám. | Vyjasnit status těchto později řazených diktátů vůči opraveným plánům. Nesloučit opačné varianty a nevracet starý kód. |
| R08, T19 | S08 odložil dynamickou HC cache, kód ji má a cílový dokument ji už připouští. S09 neobnovuje změnu jednoznačně. | Dodat zdrojovou autoritu, nebo ponechat bod mimo konsolidaci. |
| R09, T24 | Požadavek dirty i při shodném potvrzeném vygenerování proti hodnotovému porovnání modelu a testu obnovy původního klíče. Disabled změny nemusí být dirty. | Událostní versus výsledková definice dirty, včetně dopadu na Exit. |
| R10, T22 | Zdroj/client/UX vyžadují 9 přesných hodnot B, kód/testy mají 10, Total size a zaokrouhlené desetinné jednotky. Navíc Connected for nemá dohledanou oporu. | Dodat schválení změny prezentace, nebo rozhodnout samostatnou opravu. Cílové dokumenty se podle samotného kódu nepřepisují. |

Samostatně **neimplementováno, nepřenášet**: nový půlkruhový asset S16, plaintext placeholder payload klíče S13, Event Log a explicitní startup warning z T29, hotové macOS/Linux porty a distribuční ARM64 slib. S12 je nerelevantní historický webový průzkum. Tyto výsledky jsou součástí pokrytí, nikoli opomenuté funkce.

## Samostatná větev testovacích změn

Tato tabulka je návrh budoucí práce, žádný řádek netvrdí nově provedený PASS. „Zachovat“ znamená, že nový test není odůvodněný konsolidací. U podmíněného doplnění nejprve zkontrolovat existující konkrétní scénář a přidat jen chybějící vazbu. Pokud test odhalí chybějící funkci nebo R rozpor, nepřepisovat očekávání na libovolnou variantu a neopravovat implementaci pod názvem dokumentační práce.

| ID / témata | Existující pokrytí | Budoucí změna a rozsah | Důvod/riziko |
|---|---|---|---|
| TT01 / T01 | CMake registruje samostatné core modely a testy bez oken. Binární důkaz zde nebyl spuštěn. | Bez nového unit testu. Při samostatně schváleném build ověření sestavit core/service/CLI/tray a zkontrolovat linkerovou mapu nepoužitých modelů. | Architektonická závislost se nedokazuje kopií CMake v testu. |
| TT02 / T02 | test_config.cpp, test_wizard_document.cpp, test_config_store.cpp mají CIDR/budget a publikační gate. | Doplnit pouze chybějící rovnost NDJSON/body a +1, přesné timeout maximum/+1, full i repair cestu. | Hraniční kombinace musí být odmítnuta před HTTP. |
| TT03 / T03 | test_crypto.cpp, test_base64url.cpp, test_iso8601.cpp, test_wire.cpp, test_payload_batch.cpp mají pevné vektory a atomický decoder. | Úzký NetworkWorker test: validní plus adresně vadný paket v jedné dávce, nulové přijetí obou. Oba směry stejného klíče s odlišným AAD a negativní záměnou src/dst. Neopakovat již existující scénář. | Integrační vazba decoder/fronty může být chybná i se správným decoderem. |
| TT04 / T04 | TimeSync unit testy, runtime bootstrap scénáře. | Hraniční časové scénáře jen pro dodanou matematiku. R01 failure/resume/overflow patří k rozhodnutí před novým normativním očekáváním. | Nesmí vzniknout falešné potvrzení chybějící recovery. |
| TT05 / T05 | test_queues.cpp pokrývá FIFO, rezervace, count/size, drop a rotaci. | Řízený NetworkWorker+DirectionQueues scénář souběžného příjmu a deferred/error návratu na čelo. Polling/backpressure očekávání až po R02. | Pořadí a účetnictví při návratu z HTTP. |
| TT06 / T06 | test_runtime_status.cpp má idempotenci, snapshot, recovery checkpointy a reset. | Doplnit jen chybějící start/disconnect/reconnect/shutdown počty role/session přes fake driver. Catch, zachování tunelu a backoff až po R03. | Riziko dvojího bootstrapu nebo ztráty přijatého snapshotu. |
| TT07 / T07 | test_winhttp_accounting.cpp má partial errors, trickle deadline a cancel/reuse, platform_plan3_probe closing callback. | Windows integrační doplnění pro dosud neizolovanou DNS/send fázi a cancel versus success. Jeden výsledek, žádné dvojí účetnictví, opětovné použití. | Životnost callbacků a bufferů, ne univerzální důkaz bounded OS callbacku. |
| TT08 / T08–T09 | Existují fake ownership/installer a opt-in skutečné Windows/E2E cesty, podrobnosti v tématech. | Zachovat izolované již dodané invarianty. Nové testy úplné integrity/recovery/transaction až po R04/R05. Reálný SCM/Wintun scénář má vlastní prostředí. | Unit fake nenahrazuje správu adaptéru a transakční filesystem/SCM. |
| TT09 / T10–T11 | test_ipc.cpp, test_wizard_ipc.cpp, test_ipc_availability_win.cpp, test_status_contract.cpp, test_traffic_counters.cpp, runtime a WinHTTP účetnictví. | Nový test kvůli textu není nutný. Při zásahu do suite znovu použít definovaný load/snapshot/short-control scénář s nenulovým počtem testů. | Kapacity, unsigned rozsah, přesné jednorázové účetnictví. |
| TT10 / T12–T13/T21 | test_config_store.cpp a test_config_files_win.cpp mají UTF-8, identity, race, flush/publish/crash. Startup a nativní editor mají další cílené testy. | Jedna akceptace skutečného startup missing → default → editor Cancel zachová soubor, druhý start nemění bajty. Správný izolovaný NTFS/token, jinak NOT RUN. ACL očekávání až po R06. | Integrace hostu není testem samotného VpnEngine::start. |
| TT11 / T14 | C++ defaults/crypto, Python test_server_secret_normalization a test_deploy_profiles.ps1 již testují nuly, dvě fixture a hlavičku. | Nedublovat. Jen pokud chybí, ověřit autentizaci všech `.2–.15` proti izolovanému skutečnému PHP. | Test generátoru konfigurace není síťová interoperabilita. |
| TT12 / T15 | test_plan3_fix2.cpp, test_wizard_document.cpp, model/IPC/runtime pokrývají tři preference, fallback, aditivní patch a počty. | Zachovat, doplnit pouze chybějící úplnou vazbu uložené false/auto návrh/finish/nové open. Ne duplikaci všech osmi kombinací. | Metadata nesmí změnit runtime klíče nebo smazat původní kontrakty. |
| TT13 / T16–T17 | runtime testy mají single data/no retry, lease, epochy, idempotenci/capacity/retenci receipts. wp04 lifecycle ukončení. | Pokud chybí UI vazba: ztracený start reply → cancel-attempt → opožděný start, nula nových HTTP. Finish call counts převzít z existující akceptace. R07 neměnit testem. | Nevratné odebrání zpráv a dvojí publikace. |
| TT14 / T18 | Menu/model, čtečky a IPC availability ověřují jednorázové čtení a nezávislou cestu Disconnect. | Bez nové obecné suite. Převzít skutečný supported-load scénář z T10 pro přijmutí a dokončení Disconnect. | Pouhé kliknutí na UI nedokazuje doručení službě. |
| TT15 / T19 | test_tray_geometry.cpp, test_tray_infrastructure.cpp a check_icons.py pokrývají model/geometrii/resources/shell/bitmapy. | Ruční screen-reader/keyboard, více monitorů/unplug/DPI, 500 paintů a 20 cyklů podle UX14. HC změnu očekávání podmínit R08. | Čistá geometrie nenahrazuje skutečnou přístupnost. |
| TT16 / T20–T23 | test_tray_progress.cpp, test_wp03_models.cpp, test_wp03_windows.cpp, test_wp03_save.cpp, test_wp03_save_windows.cpp, Wizard model/layout/wp04. | Bez redundantních unit testů. Převzít vizuální akceptaci Connect/Editor/Wizard z TT19. Statistics očekávání blokuje R10. | Terminální success/error, NUL/selection, unknown Save a skutečné rozměry. |
| TT17 / T24–T26 | test_contracts_model.cpp, test_contracts_document.cpp, test_password_key.cpp, test_wp05_windows.cpp a lifecycle testy. | Zachovat většinu. Titul a neměnný cíl ověřit čtečkou/přepínačem aplikací, společný Exit reentry a systémový konec převzít z existujícího izolovaného testu. Změna dirty očekávání pouze po R09. | Špatný cílový klíč, ztráta draftu nebo falešný rollback. |
| TT18 / T27 | UI harness wp06 scénáře/cleanup, test_tray_application.cpp a explicitní endpointy, test-hidden-launch.ps1. | Malá shell akceptace first/no-op/incremental, foreign cwd/spaces, cache/PE/compile failure bez starého EXE. Doplnit pouze nezachycenou větev. | Izolace preview a pravdivé selhání buildu. |
| TT19 / T28 | Existují opt-in UI/resource/server/E2E targety. Jejich existence není měření. | Samostatný report 11 mockupů při 100% klientské ploše, tolerance 6 DIP geometrie/8 DIP text, 125/150/200% funkční kontrola. Skutečný Release warmup + nejméně 3 běhy, nejhorší výsledek, CPU/paměť/GDI/USER a binární delta dle UX14. Každý neprovedený scénář NOT RUN. | Vizuální a výkonové vlastnosti nelze odvodit z unit PASS nebo z přejmenování RelWithDebInfo. |
| TT20 / T29 | V inventáři nebyl nalezen samostatný logger test. | Malý test v dočasném adresáři pro rovnost/překročení rotačního prahu, pět historií, celé souběžné řádky a sekundární sink. Nový soubor například client/tests/unit/test_logger.cpp, registrace v client/tests/CMakeLists.txt. | Neomezený růst nebo ztráta diagnostiky. Nevyžaduje ProgramData ani Event Log. |

Testovací realizace vede vlastní seznam existuje/pokrývá/spuštěno a přesné příkazy/počty. Před psaním ověří, zda se mezera mezitím nezaplnila. Pro Python balíčky platí uv. Žádný test nesmí potřebovat produkční ProgramData jako fixture. R testy nejsou autorizací opravovat kód.

## Validace a akceptace dokumentační větve

- [ ] **V01 Rozsah:** `git diff --name-only` a `git status --short` ukážou pouze povolené dokumentační změny a pracovní artefakty této větve. Hashe všech šestnácti zdrojů zůstanou stejné. protocol.md zůstane beze změny, pokud nové rozhodnutí nevytvoří konkrétní důvod.
- [ ] **V02 Dohledatelnost:** každý D krok odkazuje na T záznam, zdrojovou část, skutečný symbol/soubor a přesný cílový nadpis. Aktualizovaná matice zachová i neimplementované/nejasné výsledky a přednost pozdějších zdrojů.
- [ ] **V03 Odkazy a názvy:** ověřit všechny nové lokální odkazy, velikost písmen tray_UX.md, phpvpn.exe versus CMake target a skutečné názvy helperů. Souborové příklady nesmějí odkazovat na neexistující config_text.cpp nebo wizard_document.cpp.
- [ ] **V04 Terminologie a čísla:** cílené `rg` na B01/B03/B04/B07, WP01–WP07, F09/A32, „před implementací“, „horní strop“, maxDataRecordsPerRequest, 2147483, 600000 a 128. Výskyty vyhodnotit významově, ne mechanicky odstranit historické názvy skutečných testů nebo poznámku o nedodání.
- [ ] **V05 Konzistence:** porovnat klientské limity s obecným wire protokolem, /24–/29, 252/4 versus 13/14 peerů, 4MiB IPC versus 512KiB dokument, přesné ASCII/Unicode/klíčové příklady a rozdíl receipt versus současná shoda DOM. Sporné R věty se nezamaskují přidáním opačného tvrzení jinam.
- [ ] **V06 Formát:** spustit `git diff --check`, ověřit Markdown odkazy a tabulky. Nevytvářet unit test zrcadlící text dokumentace. Pro čistě dokumentační větev není potřebný build, skutečná VPN ani úplná platformní testovací brána.

Výstup realizace uvede konkrétní změněné sekce, výsledky V01–V06, neprovedené testy a zbylá R rozhodnutí. Dokončení D kroků je dokončení nesporné dokumentační větve. Úplné uzavření původního záměru a případné pozdější odstranění zdrojových plánů nelze potvrdit, dokud není rozhodnut a pravdivě zachycen obsah R01–R10. Odstranění zdrojů navíc vyžaduje samostatný uživatelský pokyn.
