# WP08: závěrečný audit po T01 a uživatelském rozhodnutí

**Verdikt: READY FOR USER REVIEW.** S8 je pro tuto první vývojovou iteraci předán k uživatelské kontrole s výslovně přijatými výjimkami a níže uvedenými NOT RUN. Není to tvrzení o splnění všech původních číselných bran ani o připravenosti produkčního vydání. Nebyl nalezen nový prokázaný funkční, bezpečnostní, datový, lifecycle nebo závažný vizuální blokátor.

Audit proběhl 8. září 2026 přímo v `C:\git\phpvpn`, HEAD `f4b585ff2232d19626c870821a40218f525da213`. Novou autoritou pro toto rozhodnutí je tento report a manifest `C:\git\phpvpn\work\logs\wp08-user-review\final-files.json`. Staré reporty, původní FAIL, diagnostické výsledky a jejich hashe zůstávají zachované. Zejména historický verdikt T01 nebyl přepsán na opravu.

## Rozhodovací rámec

Uživatel pro první iteraci přijal hranaté rohy a větší geometrické odchylky, pokud nezpůsobují překrývání, nechtěné ořezání významného obsahu, nečitelnost, nedostupné akce, rozbitý průchod nebo zásadně jinou strukturu. Dále přijal časová překročení do čtyřnásobku původních mezí. U původního přísného `worst < 100 ms` se zde používá také přísné `< 400 ms`, nikoli `<= 400 ms`. Původní assertion a raw výsledek se nemění.

Tři historické FAIL přechodů Wizardu, po nichž T01 doložil 150 běžných úspěšných provedení, jsou uživatelem přijaty jako známá neblokující nestabilita testovacího hostitele pro tuto iteraci. **Kořenová příčina není prokázána. Nové PASS nejsou důkaz opravy.** Výkon Contracts je odložen do samostatné budoucí práce. Audit neprováděl optimalizaci a doporučené výkonnostní cíle samy neblokují předání. Žádný budoucí úkol ani automatizace tímto reportem nevzniká.

Fyzické nebo bezpečně nedostupné zkoušky zůstávají NOT RUN. Uživatel rozhodl o akceptaci uvedených výjimek, nikoli o přepsání normativních dokumentů, testů nebo historie. Původní `plan3_fix3.md`, `plan3_fix3_work_for_agents.md`, `client.md` a `tray_UX.md` byly ponechány beze změny.

## Převzetí, rozsah a integrita

Před prvním zápisem byly read-only ověřeny délky a SHA-256 všech 343 cest z T01 `final-files.json`, přesný obsah a pořadí `final-status.txt`, 214 položek T01 `evidence-files.json`, 224 položek `protected-files.json`, 491 položek předchozího auditního `baseline-evidence.json` a sedm produkčních/archivních PE. Vše bez rozdílu. Totéž bylo ověřeno znovu po nových testech. Nový vstupní manifest navíc chrání všech 422 souborů ve dvou adresářích `wp04-t01` a `wp08-final-review` v jejich úplném vstupním stavu. Tyto množiny se překrývají, jejich počty se nesčítají jako unikátní soubory.

Převzatý Git status měl 95 řádků, 45 modified a 50 untracked. Sémantický tracked diff obsahoval 43 souborů, dalších dva statusy jsou již vysvětlené konce řádků. Vlastní změna přidává **jen tento report**, tedy finálně 344 Git cest a 96 statusových řádků. Všech původních 343 souborů je bajtově shodných. Nové skripty, raw logy, matice, snímky a manifesty jsou ignorovaná evidence pod `work/logs/wp08-user-review`, build výstupy pod `work/build`. Dva nové běhy oficiálního runneru mají také vlastní unikátní adresáře `work/logs/tray-dev`, jejich kopie a reference jsou zahrnuty v nové evidenci.

Převzaté změny mají vysvětlenou návaznost: WP00 sdílený CIDR a validace, WP01 DOM a modely, WP02 infrastruktura a explicitní runtime závislosti, WP03 Connect/Editor/Statistics, WP04 Wizard, samostatné opravy Editor/Save a Wizard lifecycle, WP05 Contracts a společný Exit, WP06 fixture a runner, WP07 normativní audit, následné geometry/Connect/Editor timing/hidden launcher opravy a T01 diagnostika. Sdílené CMake, TrayApplication a čtečky odpovídají popsaným integračním zásahům. Historické odstranění nesouvisejícího timeline obrázku ve WP06 má vlastní uživatelské povolení a není změnou tohoto auditu.

Načteny byly oba celé plánovací dokumenty, `client.md`, `tray_UX.md`, `protocol.md`, distribuční hranice, předání WP00/WP01 z uložené kopie původních výstupů WP07, reporty WP02 až WP07 včetně oprav a všechny následné reporty od prvního WP08 po T01. Kontrola aktuálního kódu zahrnula modely/DOM, Contracts/Password a jejich native EDIT preflight, Save classifier/controller, Editor, Wizard, TrayApplication, čtečky, Connect/details/countdown, Statistics, monitor/cache/polling, explicitní CMake zdroje, produkční main, runner a diagnostické přechody fixture. Celý kumulativní diff a status jsou uložené ve vstupní evidenci. Historickou nedoloženou úplnou vstupní četbu WP05 nelze dodatečně změnit na splněnou proceduru.

`phpvpn.php` má 87801 B, 2476 CRLF, bez BOM a samostatných CR/LF. `protocol.md` má 92804 B, 846 CRLF, bez BOM a samostatných CR/LF. Normalizace pouze CRLF → LF je u obou přesně rovna HEAD. Dřívější externí změna EOL zůstává vysvětlená a oba současné hashe odpovídají T01. Tento audit jejich bajty neměnil.

Kontrolní skript je `work/logs/wp08-user-review/verify.ps1`. Výsledky jsou v `integrity-checks.json`, `scope-audit.json`, `eol-check.json`, `ipc-commands.json`, `scan-results.json`, `production-pe.json` a `linkage-review.json`. `git -c core.safecrlf=false diff --check` vrací 0. Cílené skeny mají nula konflikt markerů, zakázaných fixture značek v produkčním kódu, nových tray/model log call sites, Windows typů ve třech modelových veřejných hlavičkách a kontrolovaných zastaralých normativních výroků.

## Nové buildy a testy

Windows 11 build 22631, AMD64, Intel Core i9-13900HX, 24 jader / 32 logických procesorů, 34158268416 B fyzické RAM. MSVC 19.44.35228, toolset 14.44.35207, CMake 4.4.2, Ninja 1.13.2. Numerické locale cs-CZ, UI locale en-US. Žádná instalace závislostí. Použity existující Ninja cache, produkční `release` nadále znamená RelWithDebInfo, `wp03-ui-release` skutečný Release a `wp03-lifecycle-asan` Release s `/fsanitize=address`. Každý nativní proces měl soukromou fixture a vlastní nový pracovní adresář. GUI sady neběžely souběžně.

Čtyři explicitní build kroky v `run-native.ps1` prošly:

| Cache | Výslovné targety | Výsledek |
|---|---|---|
| release | phpvpn-tray, phpvpn-service, phpvpn-cli, phpvpn-client-tests | PASS |
| wp03-ui-release | phpvpn-tray, phpvpn-service, phpvpn-cli, phpvpn-wp03-ui-tests, phpvpn-tray-ui-tests | PASS |
| wp03-lifecycle-asan | phpvpn-wp03-ui-tests, phpvpn-tray-ui-tests | PASS |
| tray-dev-debug | phpvpn-tray-ui-tests | PASS |

Bylo spuštěno 29 nenulových unit filtrů, **186 provedení, 172 různých testů, 0 selhání**. Překryvy nejsou počítány jako další unikátní pokrytí. Jde o `config.`, `config_store.`, `payload.cidr_`, `runtime.cidr_`, `wizard`, `Wizard`, `fix2`, `probe`, `ContractsModel.`, `ContractsDocument.`, `PasswordKey.`, `WP03Save.`, `WP04Lifecycle.`, `TrayApplicationSeam.`, `TrayReaders.`, `TrayPolling.`, `TrayProgress.`, `TrayLayout.`, `TrayStatus.`, `WP03Models.`, `EditorDocument.`, `StatusContract.`, `TrayShell.`, `TrayResources.`, `TrayControls.`, `TrayGeometry.`, `TrayPlacement.`, `TrayMenu.` a `ipc.`. Reprodukce a jednotlivé logy jsou v `run-unit.ps1`, `unit-results.json` a `unit-summary.json`.

Nativní matice má **71 provedení, 70 původních PASS a jeden původní časový FAIL**, který splňuje přijatou výjimku. Vedle toho prošel jeden další Editor test přes oficiální skrytý runner. Nula provedených testů se nikdy nevykazuje jako PASS.

| Konfigurace a EXE | Filtr | Počet | Raw výsledek |
|---|---|---:|---|
| Release, phpvpn-wp03-ui-tests | WP03Windows. | 8 | PASS |
| Release, phpvpn-wp03-ui-tests | WP03SaveWindows. | 4 | PASS |
| Release, phpvpn-wp03-ui-tests | WP05Windows. | 13 | PASS |
| Release, phpvpn-tray-ui-tests | wp04_ | 11 | PASS |
| Release, phpvpn-tray-ui-tests | wp06_ | 9 | PASS, včetně 20 skutečných View/Scene kombinací |
| Release, phpvpn-tray-ui-tests | late_ | 2 | PASS |
| Release, phpvpn-tray-ui-tests | editor_reset_failure_generation | 1 | PASS |
| Release, phpvpn-tray-ui-tests | command_reader_private_disconnect | 1 | PASS |
| Release, phpvpn-tray-ui-tests | fix2_ | 2 | PASS |
| Debug, phpvpn-tray-ui-tests | fix2_ | 2 | PASS |
| ASAN Release, phpvpn-tray-ui-tests | fix2_ | 2 | PASS |
| ASAN Release, phpvpn-wp03-ui-tests | WP03SaveWindows. | 4 | 3 PASS, 1 časový FAIL |
| ASAN Release, phpvpn-tray-ui-tests | wp04_ | 11 | PASS |
| ASAN Release, phpvpn-tray-ui-tests | wp06_password_children_parent_ok | 1 | PASS |

Raw autorita je `native-final-results.json` a nezměněné `runs/*/output.log`. Při sestavení vlastní spouštěcí matice jsem nejprve omylem poslala `WP05Windows.` do `phpvpn-tray-ui-tests`. Výsledek byl exit 1 a nula testů, nikoli PASS. Potom byl jednou spuštěn správný `phpvpn-wp03-ui-tests`, všech 13 testů prošlo. Oba záznamy zůstávají, původní skript nebyl zpětně upraven tak, aby chybnou invokaci skryl.

Oficiální runner byl spuštěn z jiné cwd přes skrytý PowerShell s filtrem `editor_reset_failure_generation`. Jeho vlastní `summary.json` potvrzuje exit 0, jeden test, viditelné okno a focus, `startupHidden=0`, `console=0`, `visible=1`, `focus=1`. Ninja byl no-op, build 0,040 s, test 1,645 s. Vnější pomocný Process wrapper nevypsal ExitCode, proto je rozhodující vlastní uložený výsledek runneru, nikoli prázdné pole wrapperu. Záměrný filtr `wp08_deliberately_no_such_test` měl nula testů a runner správně skončil 1, bez spuštění jiné sady. Starší úplná hidden-launch matice 19/19 z T01 je hashově zachovaná a je převzatou evidencí, nikoli novým spuštěním všech negativních sond.

## Původní FAIL a dnešní rozhodnutí

| Položka | Zachovaný důkaz | Rozhodnutí pro toto předání |
|---|---|---|
| T01 Wizard | Původní čtyři provedení 3 FAIL / 1 PASS. Následná původní WP08 série 6/6 a diagnostické kopie 8/8. T01 přidal 150/150 běžných provedení. Dva kontrolované testy neaktivního okna a čtyři záměrné timeout FAIL se do 150 nemíchají. | Známá neblokující nestabilita výslovně přijatá uživatelem. Příčina neprokázána, žádné tvrzení o opravě. |
| Nový T01 recheck | Šest dalších scénářových provedení fix2_ v Release/Debug/ASAN prošlo. Původní predicate, pořadí a 3000ms timeout beze změny. | Nová regrese PASS, nezvyšuje míru jistoty na důkaz opravy. |
| Nový Editor lifecycle pod ASAN | Cyklus 17 z 24: gate 125 ms, QPC wall 132,148 ms, editor 109,732 ms a zánik aplikace 22,4138 ms. Raw assertion `expected worst < 100` selhala, proces exit 1. | Přijato, oba naměřené celkové časy jsou přísně pod 400 ms. Bez opakování do PASS a bez změny testu. |
| Nový Editor lifecycle Release | 24 cyklů, maximum gate 31 ms a wall 21,1815 ms, původní assertion PASS. | PASS. Rozdíl hrubého a QPC času je uváděn, hodnoty se nezaměňují. |
| Starší Editor timing | Dřívější 109/125/219 ms, diagnostické 141 ms a případ wall 101,9 ms při gate 93 ms zůstávají v původních reportech. Následná série warmup + 3 běhy v obou konfiguracích měla 192 cyklů. | Historie zachována. Přijatá časová výjimka není důkaz odstranění jitteru ani změna originálních mezí. |
| Geometrie | Statistics odchylky 22,7204 / 20,5275 / 18,3857 DIP a Wizard URL 14,2360 / 26,0836 / 99,4545 DIP byly původně FAIL proti 6 DIP. | Výslovně přijaté geometrické odchylky, raw FAIL zůstává. V nové kontrole bez prokázaného závažného dopadu na používání. |

T01 původní FAIL vznikly před příslušnými následnými assertions preferencí, fallbacku nebo počtu kontraktů. Nelze z nich vyvozovat prokázanou chybu publikace ani prokázanou neškodnost příčiny. Diagnostika pouze zpřesňuje přechod a číselná pozorování. Její přechodné vlastní selhání po zániku Password HWND a oprava `ownWindow(root)` jsou také zachované v T01 historii. Nové ASAN běhy nevydaly hlášení chyby paměti, jediný jejich FAIL je výše popsaná časová assertion.

## Vizuální a výkonnostní evidence

Nová `visual-matrix.csv` má 44 řádků pro 11 referencí a 96/120/144/192 DPI. Všech 11 originálních obrázků v assets bylo porovnáno s dostupnými nativními snímky. Devět odpovídajících snímků Connect/Editor/Statistics/Wizard pochází z hashově chráněné poslední WP08 evidence a bylo znovu vizuálně posouzeno. Contracts a Password mají také nové BMP z nynějšího WP05 běhu. Jde o syntetická data, ne skutečný konfigurační soubor.

Při 144 DPI jsou klientské plochy Connect 1410×1020 px, Editor 1620×840 px, Statistics 1500×975 px, Wizard 1500×1020 px, Contracts 1770×1020 px a Password 810×540 px. Odpovídají 940×680, 1080×560, 1000×650, 1000×680, 1180×680 a 540×360 DIP. Nově posouzené malé Contracts/Password a unknown zachovávají dostupné akce. Ořezání obsahu za hranicí záměrně posunutého viewportu se nezaměňuje za nedostupný obsah, nativní testy ověřují jeho odrolování a focus.

Zachovány jsou předepsané významové adaptace: nemodální Password a neměnný systémový titul místo zakázaného rodiče z obrázku, podtržítko v identitě kontraktu, syntetické hodnoty, nutné varování o nevratné spotřebě zpráv a Finish bez automatického Connect/Contracts. Posouzení nepotvrdilo nové překrývání, zásadně odlišnou strukturu nebo nedostupné hlavní akce. Hranaté rohy nejsou blocker.

V01/V02 mají samostatný původní i opravený důkaz. Aktuální native Connect card má levý okraj přibližně 47,333 DIP a horní 156,667 DIP, oba referenční stavy prošly původní úzkou tolerancí. Editor má horní hranu textového pole 88 DIP proti referenčním 86,7439 DIP. Nové WP03Windows znovu provedly příslušná geometrická assertions. Úplná metrologie každého prvku 6 DIP a textové baseline 8 DIP nebyla provedena. Nativní 96/120/192 DPI zůstává 33/44 NOT RUN, výpočetní testy čtyř DPI nejsou jejich náhradou. Stejně tak chybí skutečné přepnutí high contrast.

Součástí nového WP05 běhu byl existující Release resource proxy s plným warmupem a 20 jednotlivými vzorky, jeho assertions nebyly měněny:

| Metrika, nejhorší ze 20 | Nové měření | Původní cíl / hranice selhání |
|---|---:|---|
| Otevření Contracts, 252 řádků | 430,426 ms | 500 / 1000 ms |
| Scroll | 19,6181 ms | 33 / 100 ms |
| Key | 8,6286 ms | 16 / 100 ms |
| Password včetně paint | 6,0699 ms | 16 / 100 ms |
| Kandidát, 252 enabled | 0,9989 ms | 1000 / 2000 ms |
| Peak USER / GDI | +1025 / +298 | Každý počet pod +1200 |
| Peak private / working set | +4943872 / +4780032 B | Paměťový cíl 5 MiB, hranice 10 MiB |
| Po cyklech USER / GDI | 0 / 0 | Nejvýše +5 |
| Po cyklech private / working set | −24576 / +77824 B | Nejvýše +1 MiB |

Jde o lokální UI/resource proxy se syntetickým backendem, nikoli úplnou produkční N09b. Staré naměřené překročení scroll cíle, dřívější open FAIL a další starší metriky zůstávají v předáních. Benchmark s kompletním předchozím baseline, oddělená režie fixture, skutečný start/menu/idle CPU a úplná tabulka reakce při čekání nejsou nově prokázané. Výkon Contracts je odložen podle uživatele, audit jej neoptimalizoval.

Nový session broadcast na izolovaném desktopu zaznamenal query 1, end 2 a cleanup 62 ms, nula pozdních dialogů, retry a Disconnect. Není to skutečné odhlášení Windows. Nové cache/paint/lifecycle testy prošly, fyzických 20 cyklů změny DPI nebylo provedeno.

## PE, linkování a distribuce

Sedm kontrolovaných PE má původní T01 délku a SHA-256, AMD64 `0x8664`, stav NotSigned, žádnou hledanou ASCII/UTF-16 fixture značku. Šest z nich jsou aktuální tray/service/CLI v obou produkčních konfiguracích, sedmý je archivní WP06 baseline. Žádné z produkčních EXE nebylo spuštěno. PDB se do velikosti EXE nezapočítává.

| Skutečný Release tray | Před tímto auditem | Po | Delta B / % |
|---|---:|---:|---:|
| EXE | 842240 | 842240 | 0 / 0 |
| .text raw | 493056 | 493056 | 0 / 0 |
| .rdata raw | 260608 | 260608 | 0 / 0 |
| .rsrc raw | 55808 | 55808 | 0 / 0 |

SHA-256 Release tray: `A54F62B29B43822EA599A9DDFD28379A62CBA682031A52250F8CE3E6CD77B947`. RelWithDebInfo tray má 832000 B. Srovnatelný archivní WP06 Release tray má 841728 B, .text 492544 B, .rdata 260608 B a .rsrc 55808 B. Proti němu je současný rozdíl EXE +512 B, přibližně +0,0608 %, .text +512 B, přibližně +0,1040 %, .rdata/.rsrc nula. Tento krátký baseline neprokazuje úplnou deltu celé implementace od stavu před WP00. Ta zůstává částečně NOT RUN. Starý podepsaný distribuční EXE se nepoužívá jako srovnatelný baseline.

Znovu posouzeny hashově chráněné T01 linker mapy: service a CLI neobsahují `contracts_document.cpp.obj`, `contracts_model.cpp.obj` ani `password_key_model.cpp.obj`, unit test je obsahuje všechny tři. Současné explicitní CMake zdroje tento stav potvrzují a nespojují produkci s `dev_preview.inc` nebo `ui_diagnostics.inc`. Nevznikla DLL, nový framework nebo raster asset v produkčním EXE. Tabulky příkazů jsou shodné s HEAD, sedm původních IPC a 17 Wizard/config příkazů, žádný nový command nebo schema.

Dist obsahuje stejných 17 souborů se shodnými cestami, délkami a SHA-256. Žádná kopie do dist, podpis, manifest balíčku, ZIP, instalace, registrace služby nebo změna VPN/ProgramData neproběhla. `distribution.md` je nedotčený. `dist_old_do_not_use` nebyl opravován ani používán k vydání. Commit, push, větev ani worktree nevznikly.

## Matice A01–A32 a S8

PASS v tabulce znamená konkrétní uvedený rozsah důkazu. PARTIAL není úplný PASS a přijatá výjimka nezmění raw assertion. Zkratka N označuje nový běh tohoto auditu, H hashově ověřenou historickou evidenci a K aktuální zdrojovou kontrolu.

| ID | Stav pro první iteraci | Důkaz a omezení |
|---|---|---|
| A01 | PASS | N WP05 ManualLoad/Availability, K startConfigAction/showMenu. Menu 0 config read, ruční Contracts 1 get-config-document, reaktivace 0, bez přímého file API. |
| A02 | PASS | N 13 WP05 včetně celé availability matice. Valid otevře editor, ostatní stavy správná náprava, bez vytvoření/default fallbacku. |
| A03 | PASS | N ContractsModel a native rows. /24 252, /29 4, self vynechán a numerická identita zachována. |
| A04 | PASS | N config/full/repair, ConfigStore, payload CIDR, runtime preflight a Wizard filtry. K sdílené minimum 24 a maximum 29. Bez migrace či obejití přes Edit Config. |
| A05 | PASS | N model/default vektory. Zachování existujícího klíče a sdílený symetrický default, žádné nové odvození. |
| A06 | PASS | N model a NativeRows, whole Key budget, chyby mimo viewport. Aktivní invalid blokuje OK, unchecked nikoli. |
| A07 | PASS | N model/CryptoProvider a Random failure/partial create. No nic nemění, Yes crypto, failure uchová hodnotu, enabled beze změny. |
| A08 | PASS | N PasswordKey a PasswordUnicode. Prázdný SHA, 255 scalars včetně non-BMP, 256 celé odmítnuto, přesné UTF-8, vadné UTF-16/NUL bezpečně odmítnuty. |
| A09 | PARTIAL, NOT RUN | N immutable target/title, více stejných i různých oken, poslední OK, parent OK a generation cleanup, native MSAA/WS_EX_APPWINDOW. Skutečná čtečka obrazovky a Alt-Tab UI NOT RUN. |
| A10 | PASS | N WP03Save/WP05, K controller/close. Před OK bez publikace, frozen Save a dirty/unknown potvrzení, žádný automatický retry. |
| A11 | PASS v cíleném rozsahu | N ContractsDocument a ConfigStore, whole DOM/type/absence, 512 KiB a 4 MiB. Skutečná OS publikace ConfigStore a crash/recovery E2E NOT RUN. |
| A12 | PASS | N wp04 production Finish a wp06 publication, K confirm. 1 Finish, 1 close, 0 automatických Contracts/Connect/dodatečných config read v confirmed cestě. |
| A13 | PASS | N lost Finish, late result a ruční Contracts. Explicitní dohledání původního receipt, žádný druhý Finish, nové ruční otevření čte aktuální dokument. |
| A14 | PASS | N WP03Windows/Models, K details. Jediné get-config poskytuje uloženou URL/IP, get-status samostatně stav, chybný detail neovlivní bootstrap. |
| A15 | PASS | N visible paint/countdown a wp06 countdown. Původní 3/2/1 a 3000–3500 ms, žádný Disconnect při zavření. |
| A16 | PASS v cíleném rozsahu | N Progress model, native success/failure, K safe katalog šesti kategorií a status unavailable. Terminální freeze, žádný raw popis. |
| A17 | PASS | N TrayPolling/readers/native progress, K vlastní 5s a 500ms termíny. Progress nejvýše jeden outstanding, Statistics bez vlastního poll/timeru. |
| A18 | PASS | N nezávislé editory, unknown check/match/missing/repeat, K preserving kandidát. Poslední celá publikace vyhrává, bez rebase/CAS/automatického retry. |
| A19 | PARTIAL, NOT RUN | N 252×4 native controls, MSAA a Tab/Shift+Tab reveal, /29. Úplná interaktivní enumerace screen readerem NOT RUN. |
| A20 | PARTIAL, přijaté odchylky | Nová vizuální kontrola 11 referencí, N geometrické/resize/scroll testy, H snímky 144 DPI. 33 ostatních nativních DPI kombinací, úplná 6/8 DIP metrologie a skutečné HC NOT RUN. |
| A21 | PASS s přijatým T01 | N Wizard/EditorDocument/Statistics a fix2_ ve třech konfiguracích. T01 3 historické FAIL a 150 PASS zůstávají známá přijatá nestabilita, příčina neprokázána. |
| A22 | PASS | N ipc. a scan, K uzavřený classifier a diagnostika. 7+17 příkazů shodných, nulové nové log call sites v rozsahu, diagnostika bez payloadů/hesel/klíčů. |
| A23 | PASS dokumentačního výsledku | K celé normativní texty a H S7/ledger návaznost. Žádný vlastní normativní zásah. Historická procedurální mezera WP05 zůstává přiznaná. |
| A24 | PARTIAL, NOT RUN | N sedm PE a nulová delta tohoto auditu, H srovnatelný WP06 baseline a postupné reporty. Úplný srovnatelný optimalizovaný baseline před WP00 chybí. |
| A25 | PASS | N 17/17 dist, hashe zdrojů a fixture endpoint kontrola. Bez instalace, skutečné služby/VPN nebo ProgramData. |
| A26 | ODLOŽENO / PARTIAL | N warmup + 20 lokálních proxy vzorků a původní assertion. Výkon Contracts odložen uživatelem. Produkční start/menu/idle CPU, baseline a oddělení fixture NOT RUN. |
| A27 | PARTIAL, NOT RUN | N cache/500 paints/20 local cycles a změnový shell. Bez trvalého GDI/USER růstu. Skutečné fyzické DPI přechody 20 cyklů NOT RUN. |
| A28 | PARTIAL, NOT RUN | N geometrie priority/záporných souřadnic/clamp a native placement. Skutečný unplug/replug, změny rozlišení a monitorového DPI NOT RUN. |
| A29 | PARTIAL, NOT RUN | N přesné uint64 a UI copy, cs-CZ numeric/en-US UI, K bez localization ABI. Přepnutí Windows display language/system locale NOT RUN. |
| A30 | PASS s časovou výjimkou, fyzický NOT RUN | N common Exit/deadlines/modal reentry/48 Editor cyklů/Wizard/isolated broadcast. ASAN 125 ms raw FAIL, wall 132,148 ms, oboje <400. Skutečný OS logoff NOT RUN. |
| A31 | PASS | N model testy/build, K veřejné hlavičky/CMake a H znovu ověřené mapy bez modelových objektů v service/CLI. DOM v core/config, žádná nová DLL. |
| A32 | PASS v izolovaném rozsahu | N wp06 9/9, 20 scén, >45s preview, deadline, cleanup, skrytý Editor a nulový filtr. H úplný hidden runner 19/19. Žádné tvrzení o skutečné službě nebo distribuci. |
| S8 | READY FOR USER REVIEW | Nové build/test/text/integrity kontroly dokončeny, staré FAIL zachované, pouze přijaté výjimky, odložený výkon a transparentní NOT RUN. Bez nového blokátoru, bez prohlášení release kvality. |

## Co toto předání neověřuje

Nebyly spuštěny plné agregované CTest, síťové/integrace/lifecycle služby, Wintun/SCM, install/uninstall/upgrade, multiuser, distribuční/signature ani skutečné konfigurační file publication/crash/recovery E2E. Nebyla spuštěna produkční služba ani produkční tray, skutečné HTTP nebo VPN. Bezpečné fixture výsledky tyto cesty nenahrazují. Nepodporovaný starý OwnershipJournal /16 až /23 zůstává známým dopadem globálního zpřísnění, bez migrace a bez nového upgrade testu.

Další meze jsou fyzická DPI/monitorová/HC/locale matice, plná interaktivní accessibility, skutečný logoff, dlouhodobý provoz, libovolný memory pressure a kernel/driver failure. Best-effort mazání vlastněných bufferů neznamená garantované odstranění všech kopií v OS, alokátoru, parseru nebo uživatelském clipboardu. Tyto hranice se nepřekládají do PASS.

Pro převzetí tohoto konkrétního stavu ověř `final-files.json` (344 cest), `final-status.txt`, `evidence-files.json`, `scope-audit.json` a jejich hashe v `handoff-hashes.json`. Nový manifest obsahuje i tento report, evidence manifest nehashuje sám sebe a jeho hash je v odděleném handoff souboru. Poté lze provést uživatelskou kontrolu první iterace. Žádný původní FAIL ani NOT RUN se tímto předáním nemaže.
