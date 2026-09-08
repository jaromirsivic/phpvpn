# WP08: finální integrační audit

**Výsledek S8: NOT READY.** Funkční cílené testy prošly, ale dostupné nativní snímky prokazují překročení geometrické tolerance N09b/A20 v Connectu a Editoru. Úplná vizuální, DPI a produkční výkonová akceptace navíc není doložena. Nálezy se nesmějí změnit na PASS pouhou úpravou dokumentace. WP08 vrací vizuální opravu WP03 ve spolupráci s WP02. Po opravě je nutné nové převzetí diffu a opakování dotčených bran.

Audit proběhl 8. září 2026 přímo v `C:\git\phpvpn`, HEAD `f4b585ff2232d19626c870821a40218f525da213`. Bez commitu, push, worktree, instalace nebo spuštění produkční služby, CLI či tray.

## Konkrétní blokátory

| ID | Nález | Důkaz a vlastník |
|---|---|---|
| V01 | Connect: levá hrana hlavní karty je 32 DIP, reference po převodu klientské plochy vyžaduje přibližně 50,18 DIP. Rozdíl 18,18 DIP překračuje 6 DIP. | `progress_layout.h`, výpočet margin 32, nové success/failure captures 144 DPI. WP03, případná společná korekce WP02. |
| V02 | Editor: horní hrana textového pole je 116 DIP, reference po převodu vyžaduje přibližně 86,74 DIP. Rozdíl 29,26 DIP překračuje 6 DIP. | `layout.cpp`, margin 32 + headerSpace 84, nový capture 1620×840 px při 144 DPI. WP03/WP02. |
| E01 | Úplná geometrie a textová metrologie 11 referencí při 96/120/144/192 DPI není dokončena. | Matice obsahuje všech 44 řádků, tři aktuální FAIL a 41 NOT RUN. U osmi řádků při 144 DPI existuje nově zkontrolovaný smoke, který není úplnou akceptací. WP03/WP04/WP05 a závěrečný audit WP08. |
| E02 | Chybí jednotný srovnatelný benchmark před celou změnou a po ní, production start/menu/idle CPU a oddělená režie fixture. | Lokální 20cyklová Release UI proxy prošla mezemi selhání, nenahrazuje celou A26. WP08 s měřicím adaptérem vlastníka WP06. |
| E03 | Chybí skutečné DPI přechody a odpojování monitorů, high contrast, interaktivní screen reader a přepínač aplikací. | Čistá geometrie, MSAA, WS_EX_APPWINDOW a syntetické zprávy tyto skutečné scénáře nedokládají. WP02/WP05 a prostředí pro finální akceptaci. |

Schválené hranaté rohy nejsou vadou. Rozdíly V01/V02 jsou v poloze prvků uvnitř klientské plochy, nesouvisejí s rámečkem, systémovým titulkem nebo rasterizací fontu. Nejde o triviální izolovanou opravu bez dopadu na referenční layout. Produkční kód ani normativní pravidla proto tento audit nepřepisuje.

## Převzetí a rozsah změn

Před prvním zápisem prošlo 334/334 délek a SHA-256 z WP07 a přesná shoda inventáře i statusu. Audit WP07 byl znovu proveden v paměti s odstraněnými zápisy. Rekonstruoval zdokumentované Before/After řetězce WP03, WP04, oba opravné lifecycle balíčky, WP05, WP06 a vlastní normativní změny WP07. Rekonstrukce obou aktuálních dokumentů je přesná po normalizaci konců řádků. Dále prošla nezměněnost 16 historických dokumentů a 17 distribučních souborů. Opakovaný výsledek je v `work/logs/wp08/s7-reconstruction-recheck.json`.

Převzaté oblasti: WP00 globální CIDR a validace, WP01 DOM/model/password, WP02 Win32 infrastruktura a injection, WP03 Connect/Editor/Statistics včetně opravy Save, WP04 Wizard včetně lifecycle opravy, WP05 Contracts/common Exit/session host, WP06 runner/preview, WP07 normativní audit. Historická procedurální výhrada WP05 o vstupní četbě zůstává zachovaná.

**Vlastní změny WP08:** pouze tento nový report a ignorované reprodukční skripty, logy, linker mapy a snímky pod `work/logs/wp08`. Runner navíc zapisuje své jedinečné logy pod `work/logs/tray-dev`. Všech 334 převzatých souborů zůstává bajtově shodných. `client.md`, `tray_UX.md`, `protocol.md`, `distribution.md`, C++, testy, build skripty, CMake a assets nebyly změněny. Nevznikla žádná vlastní normativní náhrada.

Kumulativní diff a status jsou uložené v `final-diff.txt` a `final-status.txt`, hashový inventář zahrnuje i untracked soubory. Audit vlastnictví vychází z manifestů a přesných ledgerů všech předchozích balíčků, samotný diff proti HEAD by nestačil. Kontrola produkčních cest pokryla centralizovanou tray koordinaci, čtečky, Save, Contracts model/DOM/password, placement/cache/shell, progress, editor/statistics, Wizard prezentaci a skutečné testové/produkční linkování. S7 textová rekonstrukce není důkazem splnění vizuálního kontraktu.

## Build a cílené testy

Windows 11 Pro 10.0.22631, MSVC AMD64 19.44, CMake 4.4.2, Ninja 1.13.2. CPU i9-13900HX, 24 jader / 32 logických procesorů, RAM 34 158 268 416 B. Skutečné nativní UI běželo při 144 DPI. SystemLocale a Culture jsou cs-CZ, UICulture en-US. Podrobnosti jsou v `environment.json`.

| Kontrola | Výsledek |
|---|---|
| `cmake --preset release` a explicitní `phpvpn-tray phpvpn-service phpvpn-cli phpvpn-client-tests` | PASS, existující RelWithDebInfo cache, `build-production.log`. Produkční EXE se pouze sestavily. |
| Skutečný Release `work/build/wp03-ui-release`, explicitní tray/service/CLI a oba opt-in UI targety | PASS, `build-ui.log`. Žádný all/install/package/CTest. |
| Unit: 29 nenulových filtrů | 182 spuštění, 168 různých případů, 0 selhání. `unit-results.json`, `unit-*.log`. |
| WP03Windows | 6/6 PASS, detaily/read/poll/countdown/failure/editor/statistics/resources. |
| WP03SaveWindows | 4/4 PASS, dirty/undo/generace/striktní ack/deadline/abandon. |
| WP05Windows | 13/13 PASS, native controls, Password, unknown/repeat, více editorů, Exit, skutečný broadcast na izolovaném neviditelném desktopu, resource proxy. |
| wp04_ | 11/11 PASS, gate, warning, stable controls, Finish, lost reply, snapshots a lifecycle. |
| wp06_ | 9/9 PASS včetně matice 21 scén, skutečného View Tray shellu, 46s preview a původního 30s Save. |
| Další úzké nativní regrese | 9/9 PASS: happy_path, stored_identity, fix2_, late_, heartbeat_epoch, editor_reset_failure_generation, command_reader_private_disconnect. |
| Součet nativních regresí | 52 různých testů, 0 selhání, `native-results.json`. |
| F09 runner Release Build | PASS, no-op build 0,086 s. `runner-release-build.log`. |
| F09 nulový filtr | Očekávaný exit 1, skutečně 0 testů, starý program není vydáván za testový PASS. `runner-zero-filter.log`. |
| F09 pozitivní Debug a Release filtr | Oba 1/1 PASS. Debug build 4,801 s / test 0,431 s. Samostatné logy `runner-debug-positive.log` a `runner-release-positive.log`, původní izolovaný reader a skutečná matice 21 scén. |

Unit filtry: config., config_store., payload.cidr_, runtime.cidr_, wizard, Wizard, fix2, probe, ContractsModel., ContractsDocument., PasswordKey., WP03Save., WP04Lifecycle., TrayApplicationSeam., TrayReaders., TrayPolling., TrayProgress., TrayLayout., TrayStatus., WP03Models., EditorDocument., StatusContract., TrayShell., TrayResources., TrayControls., TrayGeometry., TrayPlacement., TrayMenu., ipc. Jsou to case-sensitive substringy. `ipc.` zahrnuje také 11 případů wizard_ipc., proto má 22 testů. První jednorázový wrapper chybně očekával 11 a skončil na své kontrole počtu. Nativní log od počátku uvádí 22/22 PASS, jeho obsah nebyl přepsán. Evidence byla opravena na skutečný počet a překryv, nikoli na změnu testového výsledku.

Nativní testy běžely z `work/logs/wp08/runroot`, protože WP05 má relativní capture cestu. Nové obrázky tedy nepřepsaly historické WP05 captures. WP03/WP04 capture proměnné ukazovaly explicitně do WP08. BMP byly pouze převedeny beze změny obsahu do PNG pomocí System.Drawing.

## Vizuální měření

`visual-matrix.csv` má 44 řádků s referencí, OS, DPI, předepsanou a skutečnou klientskou plochou, převodem reference, tolerancemi, povolenými adaptacemi, výsledkem a důvodem. `visual-findings.json` uchovává jednotlivé číselné vstupy V01/V02. Reprodukce je ve `visual-matrix.ps1`.

Referenční klientská plocha Connectu byla identifikována po odstranění titulku a rámu jako x=1, y=79, šířka=1461, výška=995 px. Převod celé této plochy na 940×680 DIP dává pro levou hranu karty x=79: `(79−1)×940/1461 = 50,18 DIP`. Skutečná hrana je x=48 px při 144 DPI, tedy 32 DIP. Stejná geometrie platí pro connect1 a connect2.

Editor: referenční klientská plocha x=1, y=55, šířka=1563, výška=949 px, převod na 1080×560 DIP. Horní hrana textového pole y=202 dává `(202−55)×560/949 = 86,74 DIP`. Skutečná hrana je y=174 px při 144 DPI, tedy 116 DIP. Při ruční identifikaci hran je přiznaná nejistota ±2 referenční pixely. Ani konzervativní změna o několik pixelů nemůže vysvětlit uvedené překročení 6 DIP. Nejde o měření baseline textu s tolerancí 8 DIP.

Byly vizuálně prohlédnuty všechny reference a odpovídající nové snímky 144 DPI. Přítomnost a smysl sekcí, podporované akce a bezpečnostní copy byly porovnány se schválenými adaptacemi. Například Password zůstává nemodální se schváleným systémovým titulkem, prázdný vstup má cue a hash prázdného řetězce. Wizard identity zachovává celé dodatečné varování o nevratném odebrání zpráv. Tyto adaptace nelze používat jako obecnou výjimku z geometrie. U zbylých osmi referencí není úplná per-element metrologie provedena a výsledek zůstává NOT RUN. Statický snímek také nenahrazuje klávesnici nebo čtečku obrazovky.

## Runtime, resources a PE

Skutečný Release WP05 benchmark použil syntetický /24 dokument, všechny 252 řádky a 1008 řádkových HWND, stejný warmup pro paint/input/MSAA/Password/kandidáta a 20 jednotlivě zalogovaných cyklů. Celý proces obsahuje in-process backend a již otevřený skutečný TrayApplication se syntetickým shell adaptérem. Jde o **lokální UI/resource proxy**, nikoli plný produkční benchmark View Tray. Režie backendu není samostatně vyčíslena.

| Metrika, nejhorší z 20 cyklů | Aktuálně | Cíl / mez selhání | Výsledek |
|---|---:|---|---|
| Otevření 252 řádků včetně paint | 735,162 ms | 500 / 1000 ms | Nad cílem, pod mezí selhání |
| Scroll/focus a paint | 19,3113 ms | 33 / 100 ms | PASS proxy |
| Key vstup a paint | 8,2798 ms | 16 / 100 ms | PASS proxy |
| Password SHA a paint | 14,1346 ms | 16 / 100 ms | PASS proxy |
| Příprava lokálního kandidáta | 1,4214 ms | 1000 / 2000 ms | PASS proxy |
| Peak working set navíc | 4 939 776 B | 5 / 10 MiB | PASS proxy |
| Peak USER / GDI navíc | 1025 / 298 | nejvýše +1200 každého | PASS proxy |
| Po 20 cyklech USER / GDI | 0 / 0 navíc | nejvýše +5 každého | PASS proxy |
| Po 20 cyklech working set / private bytes | +126 976 / +196 608 B | nejvýše +1 MiB | PASS proxy |

WP03 dále opakovala 500 paintů a 20 cyklů pro Progress/Editor/Statistics bez přírůstku GDI/USER. WP04 resource případ prošel. Cache unit testy pokryly 96/120/144/192 DPI, ale to není 20 skutečných open/DPI/close cyklů všech produkčních oken. Countdown od skutečně viditelného success paintu do zavření trval 3328 ms. Izolovaný session broadcast doručil query 1× a end 2×, cleanup 78 ms, bez retry, Disconnect nebo pozdního dialogu. Není to skutečné odhlášení Windows.

PE kontrola znovu prověřila šest aktuálních produkčních EXE ve dvou konfiguracích a uchovaný WP06 tray baseline. Všechny jsou AMD64 0x8664, NotSigned, bez hledaných ASCII/UTF-16 fixture/preview markerů. `production-pe.json` obsahuje hashe i raw sekce. Skutečný Release tray má 841 728 B, .text 492 544 B, .rdata 260 608 B a .rsrc 55 808 B. Proti srovnatelnému WP06 je delta všech těchto hodnot 0 B / 0 %. RelWithDebInfo tray má 831 488 B. PDB se do EXE metriky nepočítá. Tato delta nezastupuje chybějící jednotnou baseline před celým WP00 až WP06.

Staré mapy v produkčním build adresáři předcházely aktuálním EXE, proto nebyly vydávány za čerstvý důkaz. `linkage.ps1` zopakoval aktuální explicitní linker příkazy se stejnými objekty a knihovnami do odděleného adresáře WP08, pouze s novými cestami EXE/PDB/map. Aktuální service i CLI mají 0 objektů contracts_document/contracts_model/password_key_model, testový target všechny tři. Výsledky a přesné argumenty jsou v `linkage-results.json` a `linkage/`. Produkční CMake graph obsahuje explicitní zdroje, UI harness je opt-in mimo CTest a nepatří do produkčních targetů.

## A01 až A32

PASS znamená uvedený cílený důkaz, nikoli skutečnou službu, VPN nebo distribuci. Částečně neprovedená akceptační položka má jako celek NOT RUN. A20 je FAIL, protože existuje konkrétní prokázané překročení.

| A | Stav | Konkrétní evidence / omezení |
|---|---|---|
| A01 | PASS | TrayApplication startConfigAction, menu bez čtení, WP05 manual single read + reactivation, 1 document / 0 state / 0 save. |
| A02 | PASS | WP05 availability matrix pro osm chybných odpovědí, žádný fallback ani automatické vytvoření. |
| A03 | PASS | ContractsModel 9 případů, native /24 1008 HWND a /29 16 HWND, numerické pořadí a self exclusion. |
| A04 | PASS | config/config_store/payload/runtime/wizard/fix2 filtry, společné Ipv4Cidr 24..29 před publikací a Connect. |
| A05 | PASS | ContractsModel/Document, společný defaultPayloadKey, přesný .2_.3 vektor a oba směry. |
| A06 | PASS | Model + NativeRowsKeysChildrenAndImmutableTargets, active/unchecked, key budget a focus. |
| A07 | PASS | Model CryptoProvider, native Random No/Yes a failure, zachování enabled/value. |
| A08 | PASS | PasswordKey 4 + PasswordUnicodeLimitPreservesSelectionAndResult, empty/emoji/invalid UTF16/NUL/crypto. |
| A09 | NOT RUN | Model/native multiple immutable targets a poslední OK PASS, MSAA title a WS_EX_APPWINDOW PASS. Interaktivní reader a skutečný Alt-Tab nejsou doloženy. |
| A10 | PASS | WP05 manual/unknown/Exit, WP03 Save dirty/undo, žádný zápis před OK. |
| A11 | PASS | ContractsDocument 6, config_store a budget cases, preserving DOM a 512 KiB / 4 MiB. |
| A12 | PASS | wp04_production_finish a wp06 publication, 1 Finish / 1 close / 0 config read / 0 Connect / 0 Contracts. |
| A13 | PASS | wp04_production_lost_finish, explicitní 1 get-commit, žádný druhý Finish, ruční Contracts jeden nový read. |
| A14 | PASS | WP03Models + OneConfigPerOpenAndIndependentStatusWithNoDisconnect, uložené details oddělené od status. |
| A15 | PASS | TrayProgress + actual paint native test, 3328 ms, 3/2/1, žádný Disconnect. |
| A16 | PASS | Sdílená safe tabulka a modelové vektory všech šesti kategorií, native failure freeze. |
| A17 | PASS | TrayPolling a native reader counts, 500 ms vs 5 s, nejvýše jeden status request na reader. |
| A18 | PASS | WholeKeyBudgetAndIndependentEditorsDoNotRebase a unknown/check/repeat, celý kandidát bez CAS/rebase. |
| A19 | NOT RUN | 252 skutečných native řádků, tab/focus/scroll a MSAA smoke PASS. Celá enumerace interaktivní čtečkou nebyla provedena. |
| A20 | FAIL | V01/V02, 44řádková matice. Chybějící ostatní metrologie/DPI/HC není PASS. |
| A21 | PASS | wp04_, EditorDocument/WP03Windows/Reset a Statistics přesných 9 uint64, NUL/Undo/oba scrolly. |
| A22 | PASS | Nezměněné seznamy 7 + 17 příkazů, 22 ipc. případů, striktní Save parser a žádný nový tray/model log call site. |
| A23 | PASS | Přesná normativní rekonstrukce S7, beze změny historických reportů. Vizuální implementační nesoulad je samostatně A20 FAIL. |
| A24 | NOT RUN | Aktuální AMD64/sekce/hash/podpis a delta proti WP06 PASS. Jednotná srovnatelná baseline celé změny chybí. |
| A25 | PASS | 17/17 dist hashů, explicitní fixture endpointy, žádný zásah do instalace/služby/ProgramData. |
| A26 | NOT RUN | 20cyklová Release UI proxy pod mezemi selhání. Chybí celý production start/menu/idle/baseline a oddělení fixture. |
| A27 | NOT RUN | 500 paintů, static DPI cache a 20 open/close proxy PASS. Skutečné opakované DPI cykly všech oken chybí. |
| A28 | NOT RUN | Geometry/placement tests PASS, skutečný unplug/změna rozlišení/DPI celé UI sestavy chybí. |
| A29 | PASS | Aktuální cs-CZ culture/system locale, anglické native UI, přesné uint64 včetně >2^53 a MAX, locale-independent englishInteger. UICulture je en-US, jiná Windows display language není tímto tvrzena. |
| A30 | PASS | Původní 30s Save / 15s commit, společný Exit No/Yes/reentry, reálný broadcast pouze izolovaného desktopu, bez nového IPC na endSession. |
| A31 | PASS | Core include scan, samostatné phpvpn-core objekty a nově vytvořené service/CLI/test linker mapy. |
| A32 | PASS | Nová 21scénová matice, dlouhý preview a izolace, runner Build/Debug/Release/zero. Rozsáhlé negativní mutační scénáře jsou převzatá ověřená evidence WP06, zde znovu nemutovány. |

## Textové a architektonické invarianty

Globální CIDR tok: `config_validator.cpp` používá Ipv4Cidr::parse s mezemi 24/29. Repair `analyzeWizardDocument` volá stejnou validateConfigValues s allowMissingIdentity. ConfigStore::save validuje před files_->publish. set-config jde přes dispatcher → VpnEngine::setConfigJson → ConfigStore::save. Wizard Finish jde přes buildWizardDocument → store_.save. Connect čte validovaný StoredConfigSnapshot a odmítá chybějící config před beforeConnect_, publikací pendingConfig_ a spuštěním síťové operace. Tyto cesty mají nové cílené testy, nebyla spuštěna reálná publikace.

Runtime injection je hodnotová: TrayRuntime předává soukromý pipeName všem readers a oknům. Produkční kIpcPipeNameW se v tray objevuje pouze ve složení runtime v main.cpp. Preview generuje vlastní UUID pipe i mutex a používá skutečný TrayApplication. Status/Command workery vlastní shared state a kopii endpointu, ne pointer UI. endSession nejprve odvolá doručování a zastaví vlastní polling, poté zruší účastníky. Statistics nemají vlastní čtečku ani timer. ShellIcon porovnává skutečně publikovanou ikonu a tooltip před NIM_MODIFY.

| Schválené rozhodnutí | Trasování při auditu |
|---|---|
| 1: uložené details versus aktivní status | N01/N05, progress_details + reader/model tests, A14/A17. |
| 2: Finish bez handoffu | N06, TrayApplication Wizard bez Contracts callbacku, A12/A13. |
| 3: omezený synchronní SHA/kandidát, async IPC | N03/N04, password/model/save controller, A08/A11. |
| 4: warmup a nejhorší měření | N09b, 20 individuálních proxy vzorků. Celá A26 zůstává NOT RUN. |
| 5: souběžní editoři/Password, snapshot/generace/Exit | N04/N04a, WP05 a Save/Wizard native lifecycle, A09/A18/A30. |
| 6: globální CIDR a všechny native řádky | N03/F00a, 252/4 peery, A03/A04/A19. |
| 7: modely oddělené od Win32 | N09/F02, headers/CMake/fresh maps, A31. |
| 8: přesné Key/Random texty | N03, contract target a model/native assertion, A06/A07. |
| 9: deterministické monitory | N09a, helper order a pure geometry. Fyzická A28 NOT RUN. |
| 10: resources/změnový shell | N09a, cache a ShellIcon, 500 paints. Plná A27 NOT RUN. |
| 11: angličtina | N09a, sdílené konstanty bez lokalizačního frameworku, cs-CZ numeric environment, A29. |
| 12: přesné klientské plochy a reference | N09b, skutečné velikosti sedí, vnitřní geometrie V01/V02 FAIL. |

Audit konflikt markerů, produkčních preview markerů, Win32 typů v modelech a nových tray/model log call sites má nula shod. `git diff --check` má exit 0. IPC seznamy, protocol.md a distribution.md jsou nezměněné. Žádné heslo nebylo odesláno přes IPC. Best-effort cleanup vlastněných bufferů není zárukou přepsání všech kopií parseru, alokátoru, OS nebo clipboardu.

## NOT RUN a následný postup

Nespouštěny plné agregované CTest, rozsáhlé service/network/lifecycle/multiuser/instalační/distribuční sady, skutečná služba, produkční pipe, HTTP, Wintun, SCM, VPN ani skutečné odhlášení OS. Zakazuje je rozsah tohoto auditu. ASAN nebyl nově opakován nad nezměněnými produkčními zdroji, historická WP03/WP04/WP05 ASAN evidence je zachovaná. Není nově prokázán libovolně blokovaný kernel driver, dlouhodobý memory pressure ani všechny pořadí násilného ukončení.

Úplná vizuální metrologie a výkonová instrumentace se nepovažují za nedostupný nástroj jen proto, že zatím nebyly napsané. Tento audit je vrací jako konkrétní zbývající práci E01/E02. Po prokázaném V01/V02 pokračoval v nezávislých cílených kontrolách a nyní končí předáním vlastníkům, nepokračuje širokou vizuální implementací. Není požadováno žádné nové uživatelské rozhodnutí o schválených kontraktech.

Před opravou vlastník ověří aktuální `work/logs/wp08/final-files.json`, `scope-audit.json`, tento report a předchozí S7. Zachová všechny necommitnuté balíčky a historické důkazy. Opraví layout proti zaznamenané klientské referenci, ověří dopad na malá okna, skutečná DPI, focus/scroll/keyboard a resources. WP08 poté musí znovu začít kontrolou diffu a převzetí a uzavřít všechny zbývající brány. Do té doby platí **NOT READY**, nikoli READY FOR USER REVIEW.

**Stav po předání: POZASTAVENO.** Koordinátorka předala výslovný pokyn uživatele dokončit jen aktuální WP08, odeslat report do původní úlohy a ukončit běh. Výše uvedené vlastnictví a následný postup jsou seznamem zbývající práce, nikoli jejím spuštěním. Žádná opravná nebo navazující úloha nebyla zahájena. Vlastní nativní testy a preview dokončily běh, žádný jejich proces nezůstal spuštěný.
