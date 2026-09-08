# WP07: závěrečný audit dokumentace plan3_fix3

## Balíček a výsledek

WP07, dokumentační a normativní audit. **S7 PASS v dokumentačním rozsahu.** Tento výsledek neopravňuje k označení S8 za splněný ani k vydání. Matice níže zachovává neprovedené části jako NOT RUN. WP07 nespouštěl build, unit/UI testy ani benchmarky. PASS opřený o WP00 až WP06 je převzatá, konkrétně označená evidence, nikoli nové spuštění.

Práce přímo v C:/git/phpvpn. HEAD zůstal `f4b585ff2232d19626c870821a40218f525da213`. Bez větve, worktree, commitu nebo push.

## HEAD, vstupní stav a převzaté změny

Před zápisem bylo porovnáno všech **266/266** položek WP06 final-files.json podle cesty, délky a SHA-256. Celý status měl **83 položek** a přesně odpovídal WP06 final-status.txt. Scope WP06 uvádí pět změněných a dva nové soubory, nulový zásah do produkčních zdrojů a prázdné OutsideScope. Rekonstrukce jeho dvou normativních náhrad z baseline přesně odpovídala vstupním dokumentům.

Read-only kontrola S6 zahrnula skutečný dev-tray.ps1, README, dev_preview.inc, produkční a UI CMake seznamy, finální log dev scén a dev testů, runner-cases.json a evidované PE. Všech sedm existujících PE má shodný hash i velikost oproti production-pe.json, Machine 0x8664 a žádný ASCII ani UTF-16 `--dev-preview`. Produkční zdroje neobsahují preview parser. WP06 doložil 21 scén, 9/9 finálních Release testů a 30 variant runneru. Tyto scénáře nebyly ve WP07 znovu spuštěny.

Vstupní manifest WP07 navíc zahrnuje **333** existujících Git cest, včetně untracked souborů. Samostatný manifest chrání **16** historických reportů a normativních příloh WP02 až WP06 včetně oprav. Inventář dist má 17 souborů. Zdrojové a buildové změny předchozích balíčků zůstávají převzatým necommitnutým stavem.

Lokální AGENTS.md nebyl nalezen. Platí instrukce dodané v zadání. Předání výslovně povoluje hranaté rohy. WP06 odstranil jeden nesouvisející timeline obrázek na výslovný pokyn uživatele, tato historická odchylka je vysvětlená v jeho reportu a není změnou WP07.

## Změněné soubory a vlastní normativní opravy

Vlastní změna obsahuje pouze:

- client.md, devět přesných náhrad.
- tray_UX.md, sedm přesných náhrad.
- client/tests/implementation-plan3-fix3-wp07.md, tento nový report.
- Ignorovanou textovou evidenci pod work/logs/wp07.

client/tests/ui/README.md odpovídá implementovanému WP06 a nebyl změněn. Žádný C++, PowerShell/CMake produkční soubor ani test nebyl změněn. audit.ps1 je pouze reprodukční auditní pomůcka v ignorovaných logách.

Přesné doslovné Before/After, dokument, sekce a vazba jsou ve [vlastním ledgeru](../../work/logs/wp07/normative-replacements.json). Rekonstrukce dokumentů z WP07 baseline přes všech 16 položek je přesná po normalizaci CRLF/LF. Nejde o dodatečnou implementaci chybějícího pravidla, existující pravidla byla sjednocena mezi jejich několika výskyty.

| Místo | Odstraněná nejednoznačnost a vlastní oprava | Vazba |
|---|---|---|
| UX00, úvod | Popis výhradně budoucí implementace nahrazen rozlišením normativní specifikace, dodaných WP00 až WP06, auditu WP07 a následné integrační brány WP08. | N01/N09, F08/F09, A23 |
| UX00, autority | Budoucí „se mají zapracovat“ nahrazeno doloženými průběžnými náhradami a odkazem na audit. | N01 až N09, F08, A23 |
| UX01, rohy | Výslovně zaznamenána již schválená výjimka hranatých nativních rohů. Ostatní geometrie a tolerance zůstávají. | Uživatelské rozhodnutí, N09b, A20 |
| UX02, infrastruktura | Budoucí připojování specializovaných paintů nahrazeno dokončeným napojením WP03 až WP05 a samostatným rozsahem evidence. | N09a, F01/F08, A27 |
| UX03, Back/sidebar | „Není interaktivní navigací“ dostalo výslovný podmět Sidebar. Zákaz navigace se nesmí omylem vztahovat na Back. | N07, F04, A21 |
| UX11, Password OK | „Vybraný řádek“ nahrazen řádkem zachyceným při otevření. Focus rodiče cíl nemění. | N03/N04, F05, A09 |
| client.md, sdílený základ | Historická budoucí formulace WP02/WP03 až WP05 nahrazena současným stavem a odkazem na tento report. | N09, F01/F08/F09, A23 |
| client.md, get-config-document | Popis Edit Config výslovně odkazuje i na již implementovaného druhého konzumenta Contracts. Zachováno jediné čtení a rozdíl validního DOM oproti opravitelnému textu. | N02, F05, A01/A02 |
| client.md, vstupní limity | Oddělen limit hesla 255 Unicode skalárních hodnot od 512 KiB rozpočtu všech Key. | N03/N04, F05, A08/A11 |
| client.md, šest řádků terminologie | Všechny zbývající výskyty Communication Contracts sjednoceny na skutečný anglický label Communication contracts, včetně menu. | N01/N02, F08, A23/A29 |

Nezavádí se nový význam N01 až N09, nový titul, limit, IPC příkaz, lhůta ani publikační pravidlo.

## Audit průběžných náhrad podle vlastníků

WP00 a WP01 mají report v dokončených původních úlohách, nikoli ve vlastním místním report.md. Pro tento audit byly načteny jejich původní výstupy včetně netruncovaných dokumentačních patchů. Jejich kopie v adresáři WP07 jsou označené jako kopie, původní historie se nemění.

| Vlastník a synchronizace | Dokumentační rozsah a konkrétní evidence | Ověření návaznosti |
|---|---|---|
| WP00, S0 | Úloha 01a07e19-6bac-7ee0-b813-e0e162bbfdaf. client: konfigurační rozsah, full/repair, rezervace a publikace. UX10/13/14: 252 řádků, odstranění 4092 a virtualizace. | Přesné původní patche aplikované v paměti na HEAD. Devět původních změněných souborů odpovídá vlastnictví. S0 report 56 cílených testů PASS. |
| WP01, S1 | Úloha 01a07e29-2ab6-7de2-ae85-faa2d3066203. client modely a testy, UX02/10 až UX14 model/DOM/Password bez tvrzení o hotovém nativním Save. | Přesné patche a zaznamenaný přesun sekce modelů navazují na WP00. S1 report 56 testů, z toho 19 nových modelových. |
| WP02, S2 | work/logs/wp02/report.md a normative-replacements.md. client sdílená infrastruktura/monitor, UX00/02/03/14, metodika N09b a budoucí F09 bez dist. | Rekonstrukce WP00 → WP01 → WP02 je přesně rovna oběma dokumentům ve WP03 baseline po normalizaci konců řádků. S2 report 46 unit a dva native testy. |
| WP03, S3 | work/logs/wp03/report.md a 14 Before/After položek, client 8 a UX 6. Odpočet, safe chyby, details, Editor/Statistics a testy. | Výsledek přesně odpovídá WP04 baseline. S3 45 unit, 6 Release native a jeden Reset smoke, navíc tři opakování odpočtu. |
| WP04, S4 | work/logs/wp04/report.md a 19 položek, client 14 a UX 5. Back, copy, varování, metadata a Finish bez handoffu. | Přesná rekonstrukce do WP03 Save baseline. Jedna prázdná Before je doložené vložení za nadpis UX14, nikoli chybějící náhrada. S4 51 různých unit a 12 native testů. |
| Vrácený WP03 Save/lifecycle | work/logs/wp03-save-lifecycle/report.md a 5 položek, client 3 a UX 2. Editor snapshot, Save klasifikace, původní deadline, unknown a cleanup. | Přesně odpovídá WP04 lifecycle baseline. Výslovně omezený účastník, tehdy nikoli plný Exit. 75 různých cílených případů, navíc 8 pod ASAN. |
| Vrácený WP04 Wizard lifecycle | work/logs/wp04-wizard-lifecycle/report.md a 4 položky, client 3 a UX 1. Snapshot/approvedDiscard/endSession, lease a 15s Finish deadline. | Přesně odpovídá WP05 baseline. S4 znovu ověřen, společný host tehdy stále navazující práce. 133 různých unit/UI a 22 ASAN případů. |
| WP05, S5 | work/logs/wp05/report.md a 15 položek, client 5 a UX 10. Nativní Contracts/Password, vlastní Save controller a společný Exit/broadcast. | Přesně odpovídá WP06 baseline. S5 finálně 13/13 Release native, 137 unit, 33 native ASAN a 12 unit ASAN. Report zachovává starší chyby a jejich skutečná opakování. |
| WP06, S6 | work/logs/wp06/report.md a dvě položky, po jedné v client a UX14. Implementovaný runner, přesné View/Scene, oddělený Release a omezení fixture. | Přesně odpovídá vstupu WP07. 266 hashů a celý status shodný. Dev fixture se nelinkuje do produkčních targetů. |
| WP07, S7 | Tento report a vlastní ledger 16 náhrad. | Pouze terminologie, aktuální stav a konzistence, nová dokumentační/integrační evidence bez nového runtime PASS. |

Strojová návaznost je v [inherited-chain.json](../../work/logs/wp07/inherited-chain.json), raná rekonstrukce v [wp00-wp02-reconstruction.json](../../work/logs/wp07/wp00-wp02-reconstruction.json) a původní dostupné reporty/patches v [inherited-history.json](../../work/logs/wp07/inherited-history.json).

Hranice vlastnictví byly kontrolovány proti kartám a jednotlivým předáním. Pozdější změny společného TrayApplication a CMake jsou v předáních vyjmenované. Opravy WP03/WP04 před WP05 jsou samostatně dohledatelné. Výjimky WP05 pro StatusReader/Progress endSession a jediné zastaralé menu očekávání jsou popsané v jeho scope a reportu.

WP05 výslovně přiznal, že nedoložil kompletní četbu všech podkladů bez truncation před první editací. Tato historická procedurální odchylka zůstává odchylkou. Následné testy ani tento audit ji nepřepisují na splněnou vstupní proceduru. Předání S5/S6 ji uvádí odděleně od funkčních bran a neobsahuje nevyřešený funkční blokátor.

## Neměnné významy všech dvanácti rozhodnutí

| Problém | Zachovaný kontrakt v dokumentaci | Ověřená implementační vazba |
|---|---|---|
| 1 | Jednorázové get-config pouze pro details, samostatný nezměněný get-status, přijatý rozdíl uloženého a aktivního snímku. | progress_details.cpp, ProgressWindow, TrayPolling, UX04/05 a client Windows tray. |
| 2 | Finish uloží a zavře bez Contracts/handoffu. Pozdější ruční akce čte aktuální dokument. F09 bez dist. | TrayApplication wizard_.open bez callbacku, WizardWindow confirmed/receipt, startConfigAction. |
| 3 | Lokální omezený SHA a DOM kandidát mohou být synchronní. IPC asynchronní. | PasswordKeyModel, ContractsDocument, ContractsSaveController a CommandReader. |
| 4 | Warmup a nejhorší z nejméně tří běhů, přesné původní cíle a hranice. | UX14 zachováno. Proxy WP05 oddělené od nedoložené úplné produkční N09b. |
| 5 | Nezávislé koncepty, více nemodálních dětí, neměnný cíl/generace, poslední publikace, společný Exit/logoff. | ContractTarget, PasswordShaWindow, lifecycle snapshoty, advanceExit/endSession. |
| 6 | Globální /24 až /29, nejvýše 252 řádků bez virtualizace, žádná migrace. | Ipv4Cidr minimum 24, maximum 29, validateConfigValues, checkedContractPeerCount, nativní row controls. |
| 7 | DOM v core/config, čisté modely core/presentation, Win32 v apps/tray, stávající statická knihovna. | Explicitní CMake objekty a rozhraní bez HWND v modelech. |
| 8 | Přesná chyba Key a konkrétní kanonický pár v Random. | kContractKeyError, ContractTarget, ContractsWindow, UX11. |
| 9 | Rodič → bod menu → foreground → kurzor → primární, zachování nebo minimální oprava geometrie. | layout/window_utils, captured menuPlacement, reactivateWindow. |
| 10 | Cache, změnová invalidace a NIM_MODIFY, 500 paintů a 20 cyklů. | UiResources, positionControls, ShellIcon a cílené resource testy. Plná systémová brána zůstává neprovedená. |
| 11 | Pouze angličtina, čísla nezávislá na locale, bez lokalizačního frameworku. | ui_style, wizard_text, englishInteger, exact labely a Password title. Jiné skutečné locale nebylo ověřeno. |
| 12 | Přesné klientské plochy, 6/8 DIP, 11 mockupů × 4 DPI a funkční přístupnost. | UX03/14 a specializované layouty. Schválené hranaté rohy a nemodalita jsou výslovné výjimky, ostatní brána není nahrazena screenshotem. |

## Matice A01 až A32

PASS znamená, že pro uvedené funkční/modelové kritérium existuje označený důkaz v předchozím balíčku nebo nový statický audit WP07. Zahrnuje-li kritérium neprovedenou podmínku, celý řádek je konzervativně NOT RUN a provedená část je uvedena zvlášť. Není zjištěný důkaz funkčního FAIL v rozsahu tohoto dokumentačního auditu. Syntetický backend není reálná služba ani důkaz souborové publikace OS.

| ID | Stav | Konkrétní důkaz a hranice |
|---|---|---|
| A01 | PASS | WP05 ManualLoadSingleReadReopenAndCommonExitNoDiscard, availability test a WP06 21 scén. TrayApplication::showMenu nečte config, startConfigAction používá pro Contracts jeden get-config-document, reaktivace nečte. |
| A02 | PASS | WP05 ManualAvailabilityMatrixNeverCreatesOrFallsBack, FailedCheckBlocksRepeatAndMissingRequiresFreshConfirmation. Parser a finishConfigAction mají valid/repairable/invalid/missing, přístup, encoding, limit, bezpečnost a IPC větve bez fallbacku. |
| A03 | PASS | WP01 ContractsModel 9/9, checkedContractPeerCount a numerické pořadí, WP05 /24 1008 řádkových HWND a /29 16 HWND, vždy čtyři controls na peer. |
| A04 | PASS | WP00 56 cílených testů včetně full/repair, ConfigStore/set-config, runtime.cidr_, wizard_service.cidr_, wizard_document a hran front. Aktuální minimum 24 a maximum 29 ve sdíleném IPv4. Žádná wire změna ani migrace. |
| A05 | PASS | WP01 ContractsModel a wizard_defaults, referenční .2_.3 vektor a oba směry. Model zachovává existující klíč, chybějící volá defaultPayloadKey, Enabled false. |
| A06 | PASS | WP01 aktivní/neaktivní invalid Key včetně posledního řádku, WP05 NativeRowsKeysChildrenAndImmutableTargets a SmallCidrResizeTabOrderAndVisualCapture. 63/65/uppercase/nonhex neprocházejí jako aktivní validní Key. |
| A07 | PASS | WP01 fake CryptoProvider, WP05 RandomFailureAndPartialCreateFailureRetainSafeState a native řádkový test. No/Escape/X bez změny, výchozí No, 32 crypto bajtů, failure zachová klíč i Enabled. |
| A08 | PASS | WP01 PasswordKey 4/4 s nezávislými vektory, WP05 PasswordUnicodeLimitPreservesSelectionAndResult. Empty, 254/255/256 ASCII/non-BMP, combining, UTF-16 a řídicí znaky, celé odmítnutí bez truncation. |
| A09 | NOT RUN | WP05 doložil immutable exact title, MSAA jméno, WS_EX_APPWINDOW, dva stejné/různé cíle, poslední OK, generace a hlavní Save. WP06 password_children_parent_ok PASS. Skutečná interaktivní čtečka obrazovky a Alt-Tab zůstávají neověřené. |
| A10 | PASS | WP05 native dirty/clean/unknown, modelové přípravy a počítadla, requestClose/approvedDiscard. Před OK 0 publikací, při saving původní deadline. |
| A11 | PASS | WP01 ContractsDocument 6/6, absence/přítomnost, typy, unknown členy, 512 KiB dokument/Key, 4 MiB skutečná obálka, overflow. WP00 validace před ConfigStore publikací, WP05 whole-document save. OS filesystem E2E není tímto prokázán. |
| A12 | PASS | WP04 produkční Finish a opakované WP04 lifecycle/WP05 ASAN/WP06 regrese: 1 Finish, 1 close, 0 config read, 0 Connect, 0 automatických Contracts. |
| A13 | PASS | WP04 lost Finish a explicitní get-commit, nikdy druhý Finish. WP05 a WP06 pro ruční Contracts ověřují jeden aktuální get-config-document. Platný receipt a samostatné ruční čtení nejsou handoff. |
| A14 | PASS | WP03 model/reader s řízenou změnou .3 → .4, jedno get-config na místní otevření, status beze změny, žádný log celého dokumentu. |
| A15 | PASS | WP03 fake monotonic clock a tři Release UI běhy actual-paint, 3000 až 3500 ms bez Disconnectu. WP06 connected_countdown znovu používá produkční timer. |
| A16 | PASS | WP03 tabulkové TrayProgress/WP03Models pro šest kategorií a unknown fallback, native failure smoke. WP06 Connect/Failed skutečný endpoint failure. Kompletní vizuální shoda chybových obrazovek patří A20. |
| A17 | PASS | WP03 TrayPolling, backend s 350ms latencí a maximem jednoho status requestu na reader. Zůstává nezávislých 500 ms a 5 s, Statistics nemá vlastní timer. WP05 změnil jen abandon ownership. |
| A18 | PASS | WP05 WholeKeyBudgetAndIndependentEditorsDoNotRebase, UnknownCheckMatchAndConfirmedRepeatAreExplicit a missing/failure. WP00 ConfigStore a WP01 immutable DOM. Nové řídicí cesty nezavádějí CAS, automatický rebase, retry ani změnu aktivní VPN. |
| A19 | NOT RUN | WP05 prokazuje všech 252 × 4 controls, native Tab/Shift+Tab a MSAA jména, bez recyklace. Skutečná čtečka enumerující celý editor nebyla spuštěna. Raw počty objektů jsou uvedené níže bez tvrzení o úplné accessibility bráně. |
| A20 | NOT RUN | WP03/WP04/WP05 native smoke při 144 DPI, čisté layout testy 96/120/144/192. Chybí číselný report 11 × 4, tolerance všech prvků 6/8 DIP, fyzické DPI přechody a HC. Hranaté rohy jsou schválené. |
| A21 | PASS | WP03 EditorDocument/Reset/Statistics 9 uint64/stale a native NUL/scrolly, WP04 gate/Back/metadata/warning/focus/undo. Pozdější lifecycle opravy a WP05 ASAN zachovávají tyto regrese. |
| A22 | PASS | Nový WP07 audit: stejné tabulky 7 základních a 17 Wizard/config IPC příkazů před/po HEAD, žádný nový command. Wizard IPC diff mění pouze dva safe action texty. Nové tray/model zdroje nemají log call site. Původní parser testy WP00/WP04 zachovávají uzavřené schema. |
| A23 | PASS | Nový WP07 audit: přesná řetězová rekonstrukce dokumentů WP00 až WP06, 16 vlastních oprav, nula aktivních starých konfliktních pravidel, scope a nedotčená historie. Historická procedurální odchylka WP05 zůstává přiznaná. |
| A24 | NOT RUN | Dílčí srovnatelné PE reporty WP02/WP03 RelWithDebInfo a WP05/WP06 skutečný Release existují, sekce i SHA doloženy. WP07 ověřil sedm hashů. Chybí jednotný skutečný Release baseline před celým WP00 až WP06, dílčí delty se nesčítají jako jedna finální metrika. |
| A25 | PASS | WP06 izolovaný runner/fixture, scope a 17/17 dist. Nový WP07 hashový audit potvrzuje dist a nulovou vlastní změnu kódu/skriptů/instalace. Žádná skutečná služba nebyla spuštěna. |
| A26 | NOT RUN | WP05 lokální Release proxy má warmup a 20 vzorků, konkrétní hodnoty níže. Chybí celá produkční start/menu/idle CPU tabulka, oddělení fake režie a všechny systémové kombinace N09b. |
| A27 | NOT RUN | WP02 cache/shell a WP03/WP04/WP05 500 paintů a 20 cyklů mají dílčí PASS. Chybí úplný finální souhrnný důkaz všech oken s reálnými DPI změnami a požadovaným working set baseline. |
| A28 | NOT RUN | WP02 geometry/placement a pozdější layout/resource regrese PASS. Fyzický unplug/replug, změna rozlišení a přesun přes skutečné monitory nejsou prokázané. |
| A29 | NOT RUN | Staticky anglické texty, přesné uint64 formátování a Key/Random/Password titul doložené. Skutečné jiné Windows locale, zalamování a interaktivní dostupnost titulku nebyly ověřené. |
| A30 | PASS | WP05 OriginalDeadlineExitWaitAndIndependentEditor, CommonExitRevalidatesAllDraftsAfterModalReentry a RealSessionBroadcastOnIsolatedDesktopNeverWaitsForReaders. Původní 30s Save a 15s Wizard termíny, společné No/Yes, query/false/true, cleanup bez retry/Disconnect. Jde o izolovaný broadcast, nikoli skutečné odhlášení OS. |
| A31 | PASS | WP01 čisté model headers, samostatné objekty core a linkerové mapy service/CLI bez nepoužitých modelů. WP07 ověřil zdrojové rozdělení a explicitní CMake, novou mapu negeneroval. |
| A32 | PASS | WP06 finální 9/9 Release dev testů, 21 scén, 30 runner variant v runner-cases.json a příslušných logách. Skutečný produkční pohled, izolované endpointy, no-op/inkrementální build, chyby bez starého spuštění. WP07 ověřil kód a existující artefakty, nespouštěl runner. |

## Runtime, vizuální a PE evidence bez rozšíření její platnosti

WP05 finální log final-wp05.txt uvádí 13/13 a 20 měřených cyklů po plném warmupu. Windows 11 Pro 22631, i9-13900HX, 32 logických procesorů, přibližně 34,16 GB fyzické RAM, 144 DPI, MSVC AMD64, skutečný Release, syntetický /24:

| Lokální proxy WP05 | Nejhorší finální hodnota |
|---|---:|
| Otevření Contracts | 416,366 ms |
| Scroll | 20,8403 ms |
| Key | 2,9294 ms |
| Password včetně paint | 6,8683 ms |
| Příprava kandidáta | 1,1411 ms |
| Peak working set proti baseline | +4 915 200 B |
| Peak USER / GDI | +1026 / +298 |
| Po 20 cyklech USER / GDI | +2 / +0 |
| Working set po cyklech | +311 296 B |
| Izolovaný session cleanup | 47 ms |

GDI a USER jsou uvedeny samostatně, ne jako jeden zaměnitelný počet. Dřívější scroll 38,6344 ms překročil cíl 33 ms, zůstal pod hranicí 100 ms. Dřívější neúplný warmup měl skutečný FAIL otevření 1066,52 ms a růstu paměti. Report WP05 zachovává selhání, vysvětlení i finální opakování, WP07 je nemaže. Ani finální proxy neprokazuje celou A26.

Srovnatelný skutečný Release WP05: produkční EXE 741376 → 841728 B, +100352 B (+13,54 %), .text 413184 → 492544 B (+19,21 %), .rdata 246272 → 260608 B (+5,82 %), .rsrc 55808 → 55808 B. Oba AMD64, NotSigned, bez PDB. WP06 stejný produkční Release nemění ve velikosti ani sekcích, hash relinkovaného výsledku se liší. To je dílčí delta WP05/WP06, nikoli jednotný baseline celé implementace. Historická distribuční binárka se nepoužívá jako rovnocenný Release baseline.

## IPC, wire, bezpečnost a distribuční hranice

Nový audit tabulek zachoval přesně následující seznamy:

- Základních 7: get-status, connect, disconnect, get-info, get-config, set-config, get-diagnostics.
- Wizard/config 17: get-config-state, get-config-document, get-default-config, create-default-config, wizard-open, wizard-reset, wizard-keepalive, wizard-check-url, wizard-check-secret, wizard-check-access, wizard-get-operation, wizard-cancel-operation, wizard-cancel-attempt, wizard-resume, wizard-finish, wizard-get-commit, wizard-close.

Výsledek před/po je v [ipc-commands.json](../../work/logs/wp07/ipc-commands.json). set-config nemá receipt. Check porovnává aktuální validní celý DOM a jeho shoda není dokladem historické publikace. Finální klasifikace ack a odmítnutí zůstává v CommandReader. Wizard Finish používá vlastní původní receipt a explicitní Verify save.

Read-only hranice protocol.md: změna CIDR je klientská konfigurační kompatibilita. Nemění směrovací src/dst, délku PAYLOAD_KEY, AES-GCM, AAD, HKDF, IV, NDJSON ani end-to-end kontrolu. Symetrické předvyplnění klíče neodstraňuje směrové src/dst z AAD. Heslo se hashuje lokálně, není nový wire parametr. Distribuční pravidla nadále určuje distribution.md a F09 není instalovatelný balíček.

Žádné skutečné konfigurace, hesla ani klíče nebyly pro audit načteny. Citlivé buffery mají pouze deklarované best-effort čištění, nikoli novou záruku přepsání kopií OS, alokátoru nebo clipboardu. Nový scan produkčních tray/model cest neobsahuje logovací call site. Případné zobrazení URL/IP, klíče nebo hesla zůstává omezené na příslušná určená pole, nikoli diagnostiku.

## Provedené příkazy a výsledky WP07

Reprodukovatelný audit je [audit.ps1](../../work/logs/wp07/audit.ps1). Načte se jako UTF-8, aby Windows PowerShell 5 správně četl české vzory:

~~~powershell
& ([scriptblock]::Create([IO.File]::ReadAllText('C:\git\phpvpn\work\logs\wp07\audit.ps1')))
~~~

Použité read-only příkazy zahrnují git rev-parse HEAD, git status --short, git diff --name-only, git diff --stat, git diff pro relevantní soubory, git show HEAD:<path>, git ls-files --cached --others --exclude-standard a Get-FileHash -Algorithm SHA256. Předchozí kód nebyl spuštěn jako součást rekonstrukce historických shell náhrad, aplikovaly se pouze jejich textové operace.

Cílené textové kontroly:

~~~powershell
rg -n -- '4092|16 to 29|This address range is too large|neuk[aá]že číselný odpočet|nezobrazuje číselný odpočet|nativní prvky jen pro viditelné řádky|uprostřed pracovní plochy aktivního monitoru|Není interaktivní navigací|Specializované obrazovky a jejich plná akceptace jsou navazující|Communication Contracts' client.md tray_UX.md
rg -n -- 'placeholder|virtualiz|recykl|/16|/23|524288|odpoč|handoff|modální|zakázan|dist' client.md tray_UX.md
rg -n -- '252|/24|/29|255 Unicode|Password SHA - Contract|Key must contain exactly 64 lowercase hexadecimal characters|Replace the key for|Check current configuration|Save draft again|WM_ENDSESSION|3000|NIM_MODIFY|6 DIP|8 DIP|nejméně třikrát|dev-tray.ps1' client.md tray_UX.md
git -c core.safecrlf=false diff --check
~~~

První scan má 0 výskytů, exit 1 zde znamená očekávanou absenci shody. Širší kontextový scan záměrně najde negace virtualizace/handoffu, historicky odmítané prefixy, URL placeholder a cue Password. Tyto výskyty nejsou aktivním starým pravidlem. Limit 524288 patří JSON a Key, nikoli heslu. Modální Random potvrzení zůstává povolené. Věty o dist popisují zákaz kopie a oddělené vydání.

Konkrétní počty a úplné výstupy jsou v rg-results.json a rg-*.txt pod work/logs/wp07. **git diff --check PASS**, bez whitespace chyb. Dočasný parametr core.safecrlf nemění Git konfiguraci.

## NOT RUN a zbytková rizika

- Ve WP07 žádný nový build, unit/UI test, preview nebo benchmark. Dokumentační změna vyžaduje textový a scope audit. WP08 má předepsané úzké nenulové filtry a explicitní targety.
- Úplná A09/A19/A20/A24/A26/A27/A28/A29 v rozsahu uvedeném v matici. Systémový screen reader, Alt-Tab, všechny skutečné DPI/monitor/HC/locale kombinace a jednotný produkční Release baseline zůstávají samostatnou evidencí.
- Plné agregované CTest, integrační, síťové, lifecycle, install/uninstall, multiuser a distribuční/signature testy byly záměrně vynechány podle zadání. Reálná služba, VPN, HTTP, SCM, Wintun a ProgramData nebyly ovládány.
- Starý OwnershipJournal s /16 až /23 se podle reportu WP00 nově odmítá jako journal_corrupt. Skutečný upgrade a migrace nebyly provedeny. Není to potají rozšířený rozsah WP07.
- Dlouhodobý provoz, fault injection libovolného kernel driveru a memory pressure nejsou zaručené cílenými in-memory testy.

## Předání WP08

WP08 nejprve ověří HEAD, [final-files.json](../../work/logs/wp07/final-files.json), celý [final-status.txt](../../work/logs/wp07/final-status.txt), [scope-audit.json](../../work/logs/wp07/scope-audit.json), vlastní ledger a inherited-chain. Manifest zahrnuje všechny Git cesty včetně untracked, nikoli jen tracked diff. Výsledný scope dovoluje pouze oba normativní dokumenty a tento nový report, původní historie a všech 17 dist souborů zůstávají shodné.

Žádný implementační blokátor nebyl tímto dokumentačním auditem prokázán. Zbývající neprovedené důkazy mají vlastníky: WP02 pro sdílenou geometrii/resources, WP03 pro Connect/Edit/Statistics, WP04 pro Wizard, WP05 pro Contracts/Password a společný lifecycle, WP06 pro dev runner a fixture. WP08 musí hodnotit reálné výsledky podle své karty, nevydávat tuto matici za nové spuštění a případný FAIL vrátit příslušnému vlastníkovi.

**S7 je předatelný. S8 a READY FOR USER REVIEW nejsou výsledkem WP07.**

