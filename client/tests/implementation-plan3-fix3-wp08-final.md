# WP08: nezávislý závěrečný audit po třech opravách

Výsledek: **NOT READY FOR USER REVIEW kvůli jedinému otevřenému bodu T01, nestabilitě dvou testů Wizardu.** Není prokázáno, že jde o chybu produkčního Wizardu. Neúspěšné pokusy však nemají vysvětlení ani časovou evidenci, která by dovolila jejich přijetí podle výjimky 4×. Pozdější úspěšná opakování původní selhání neruší. Tento audit žádá dořešit příčinu a spolehlivost testu, nikoli automaticky měnit funkční implementaci.

V01 a V02 jsou nově ověřené **PASS podle původní tolerance 6 DIP**. Nové větší geometrické odchylky jsou podle následného výslovného pokynu uživatele přijaté pro první iteraci. E01 až E03 ani nedostupné fyzické scénáře nejsou samy o sobě důvodem tohoto NOT READY. Nejde o schválení produkčního vydání, instalace nebo distribuce.

Audit proběhl přímo v `C:/git/phpvpn`, HEAD `f4b585ff2232d19626c870821a40218f525da213`, bez commitu, push, worktree, spuštění skutečné služby nebo VPN. Vlastní výstup je tento report a evidence v `work/logs/wp08-final-review`. Zdrojový kód, testy, normativní dokumenty a distribuční soubory audit neopravoval.

## Převzetí, četba a integrita

Před prvním zápisem bylo ověřeno všech **340/340** cest posledního `work/logs/wp06-hidden-editor/final-files.json`, přesná délka a SHA-256, celý Git status a inventář tracked i untracked cest. Rozdíl byl nulový. Důkaz je `baseline-check.json`, kopie manifestu `baseline-files.json` a celý vstupní diff `baseline-diff.txt`.

Byly načteny celé `plan3_fix3.md`, `plan3_fix3_work_for_agents.md`, `tray_UX.md`, `client.md`, `protocol.md`, reporty WP07, původního WP08 a oprav geometry, Connect geometry, Editor lifecycle timing a hidden Editor runner. Relevantní zdrojová kontrola zahrnula konfiguraci a přenosné Contracts/Password modely, CMake, TrayApplication, Save controller a reader klasifikaci, lifecycle Editoru/Wizardu, layout a paint cestu, infrastrukturu resources, produkční vstupní bod, dev runner a nativní harness. Doplňující četba po zkrácených výstupech byla provedena v menších blocích. Nejde o nový audit každé nezměněné implementace wire protokolu.

Při závěrečném ověření byly zachyceny dvě změny bajtů mimo vlastní zápisy auditu. Koordinátorka potvrdila, že nezná jiného zapisujícího agenta. Původ procesu se nepodařilo určit. Read-only diagnostika ale přesně prokázala pouze normalizaci konců řádků, kterou koordinátorka výslovně dovolila zachovat a zaznamenat:

| Soubor | Vstup → aktuální velikost | Konce řádků před → po | Obsah a Git |
|---|---:|---|---|
| phpvpn.php | 87782 → 87801 B | 2457 CRLF + 19 LF → 2476 CRLF | 2476 LF v baseline, HEAD i aktuálně, normalizovaný obsah přesně shodný |
| protocol.md | 91958 → 92804 B | 846 LF → 846 CRLF | 846 LF v baseline, HEAD i aktuálně, normalizovaný obsah přesně shodný |

Oba soubory jsou bez UTF-8 BOM a bez osamocených CR. `git hash-object` s filtry se přesně rovná příslušnému HEAD blobu. `git diff --raw` a `--numstat` jsou prázdné, `status --porcelain=v2` přesto eviduje `.M`. LastWriteTime je 2026-09-08 16:13:27.6208307 +02:00 pro PHP a 16:13:27.6119860 +02:00 pro protocol. Přesné SHA-256 před/po, Git hashe a počty jsou v `line-ending-diagnostic.json`, původní manifest nebyl přepsán.

**Integritní výhrada:** nelze tvrdit závěrečnou bajtovou shodu všech 340 vstupů. Shodných je 338, zbývající dva mají prokázanou ekvivalentní textovou reprezentaci. Aktuální bajty zůstaly zachované. Není zjištěna změna PHP nebo wire významu. Všech **491/491** souborů bezprostředně převzaté evidence, **7/7** evidovaných PE a **17/17** souborů `dist` zůstává hashově beze změny. Nový report rozšiřuje finální inventář na 341 cest.

## Výjimky použité při rozhodování

- Hranaté rohy jsou přijaté.
- Časové hodnoty do 4× původních časových mezí lze přijmout v první iteraci. Původní limity, asserty a raw FAIL zůstávají. Není dovoleno tím omluvit deadlock, leak, bezpečnostní nebo funkční chybu.
- Výkon Communication contracts je odložený. Naměřené hodnoty se uvádějí pravdivě, bez optimalizací a bez blokování jen kvůli tomuto výkonu.
- Pozdější pokyn přijímá i výrazně větší odchylky odsazení, velikostí a poloh karet. Nadále by blokovalo překrytí, ořez bránící přečtení či obsluze, nedostupná akce, rozbitý funkční tok nebo zásadně jiná struktura.
- Skutečné monitorové, screen-reader, locale a service scénáře, které nebyly provedeny, zůstávají NOT RUN. Bezpečný lokální user review se neposuzuje jako úplné release ověření.

## T01: otevřená nestabilita nativních Wizard testů

Vlastník: **WP04 pro rozlišení stavového přechodu a timeoutu, WP06 pro diagnostiku a spolehlivost harnessu**, závěrečné ověření WP08. Priorita P2.

Přesná reprodukce používá původní nezměněné `work/build/wp03-ui-release/tests/phpvpn-tray-ui-tests.exe fix2_`, vlastní procesová okna a UUID named pipe. Žádný skutečný server, konfigurace nebo produkční endpoint se nepoužije.

| Běh | preferences_contract_preview | missing_suggestion_unsupported_cidr | Evidence |
|---|---|---|---|
| První cílená native série | FAIL: contracts step missing | FAIL: Next did not reach expected step | native-fix2_.log |
| Samostatné opakování, stejná binárka, bez další GUI série | FAIL: Next did not reach expected step | PASS | native-fix2-isolated.log |
| Předem omezené další tři běhy originálu | 3/3 PASS | 3/3 PASS | fix2-original-observation-1 až -3.log |
| Diagnostická kopie, první běh a tři další | 4/4 PASS | 4/4 PASS | diagnostic-fix2.log, fix2-diagnostic-observation-1 až -3.log |

`Gui::until` má původní 3000ms limit. Fixture má `responseTimeoutSeconds=1`. To je kandidát na vysvětlení, nikoli prokázaná příčina. Původní neúspěšné logy neobsahují stav modelu ani skutečný okamžik dokončení probe. Není proto správné tvrdit, že se neúspěšná operace dokončila do 4×, nebo že šlo prokazatelně jen o pomalý stroj.

Diagnostika byla přidána do nové kopie `tray_ui_diagnostic.cpp` pod evidence, nikoli do repo testu. Používá stejné Release objekty produkčního UI. Teprve po původním timeoutu by vypsala přítomnost/enabled stav controls, fixture počítadla a statické popisky, pozorovala původní predikát do 4× a **potom stále vyhodila původní chybu**. Nemění původní prodlevu, configured timeout, predikát ani výsledek FAIL na PASS. Žádný z jejích čtyř běhů však timeout nevyvolal, takže hypotézu nepotvrdila. Reprodukční skript je `diagnose-fix2.ps1`.

Další nekonečné opakování až do PASS nebylo prováděno. K uzavření je potřeba zachytit a vysvětlit neúspěšný stav nebo odstranit prokázanou chybu testu či implementace. Samotné zvýšení timeoutu bez doložení příčiny by tento audit neuzavřelo. Nejde o povolení měnit původní normativní časové kontrakty.

## Nové build a runtime výsledky

| Kontrola | Nový výsledek | Hranice důkazu |
|---|---|---|
| MSVC AMD64 produkční targety a unit target | PASS | `work/build/release`, preset release ve skutečnosti znamená RelWithDebInfo |
| Skutečný Release tray/service/CLI a oba UI targety | PASS | `work/build/wp03-ui-release`, žádná služba ani CLI nebyly spuštěny |
| ASAN sestavení | PASS | `work/build/wp03-lifecycle-asan` |
| `cmake --preset release` v client + explicitní build | PASS | První volání z rootu chybně hledalo preset, opravený pracovní adresář prošel. Finální PE jsou stejné |
| 29 cílených unit filtrů | **186 spuštění, 172 různých testů, 0 selhání** | config, config_store, CIDR runtime/payload/Wizard, modely, čtení, Save, geometrie/resources/shell a 22 ipc testů |
| Základní native průchod | **54 testů, 52 PASS, 2 FAIL** | WP03Windows 8, WP03SaveWindows 4, WP05Windows 13, wp04_ 11, wp06_ 9, happy 1, stored 1, fix2_ 2, late_ 2 a tři jednočlenné filtry |
| ASAN unit | **35/35 PASS** | 7 cílených filtrů |
| ASAN native | **13/13 PASS** | WP03Windows 8, WP03SaveWindows 4, editor_reset_failure_generation 1 |
| Skryté spuštění runneru | **19/19 PASS** | Debug/Release, checkout/cesta s mezerami, skutečný Editor, no-op, zero/missing filter, chybné Scene/Mode, compile failure, rebuild, invalid PE a locked EXE |
| Dev scénářová matice | **20/20 scén PASS**, v rámci wp06_ 9/9 | Dřívější údaj 21 v historických reportech není přepisován ani opakován jako aktuální počet |
| Preview delší než 45 s | PASS | 46s test nad vlastní fixture |
| Původní 30s Save deadline, unknown/check/repeat, cleanup | PASS | Výhradně paměťový backend a vlastní named pipe |
| Společný Exit a systémový broadcast | PASS | Izolovaný desktop, žádné skutečné odhlášení |

Po T01 byl přerušený skript doplněn samostatnými zbývajícími nenulovými filtry. Jejich úplný soupis je `native-complete-results.json`, původní `native-results.json` zůstává přesným záznamem přerušeného prvního průchodu. Následná opakování T01 a lifecycle nejsou započítána do základních 54. Agregované CTest ani skutečné integrační/service testy nebyly spuštěny.

Hidden-launch evidence vznikla také v `work/logs/hidden-launch/20260908-160258-788-971f9ab6846a4490b9d12fafdaee8ec0`, což je vlastní výstup nezměněného regression runneru. Ten vytváří unikátní testovací kopii klienta v cestě s mezerami. Negativní compile/PE/lock scénáře zasahují pouze tuto kopii. Původní produkční soubory zůstaly shodné. `hidden-launch-run.log` obsahuje všechny návratové kódy včetně očekávaných nenulových výsledků.

## Editor lifecycle, čas a resources

Nově bylo samostatně provedeno warmup + tři měřené běhy pro každý build. Každý obsahuje 24 create/save/reset/WM_ENDSESSION/destroy cyklů. Původní assert `worst < 100 ms` nebyl změněn. Měří se terminální UI ukončení včetně kontrol snapshotu a destrukce aplikace, nikoli celý třicetisekundový Save.

| Build | Warmup gate / QPC maximum | Gate maxima tří měřených běhů | QPC maxima tří měřených běhů | Výsledek |
|---|---|---|---|---|
| Release | 16 / 19,7704 ms | 16, 16, 15 ms | 17,4633, 20,1858, 16,7648 ms | PASS původní meze |
| ASAN | 31 / 22,4271 ms | 31, 16, 32 ms | 19,0724, 14,0188, 19,1561 ms | PASS původní meze |

To je **192 nových opakovaných cyklů**, vedle 24 v základní Release a 24 v základní ASAN sérii. Základní série měly gate maxima 63 a 16 ms. QPC a `GetTickCount64` mají různé rozlišení, proto nejsou zaměnitelné. Raw per-cycle editor/checks/app-destroy/UI-thread-cycle údaje zůstávají v logách, strojový souhrn v `lifecycle-metrics.json`.

Historické naměřené hodnoty 109, 125 a 219 ms a jejich původní FAIL v opravě lifecycle timing zůstávají zachované. Pod dnešním rozhodnutím uživatele jsou přijatelné jen jako **výjimka do 400 ms**, nikoli zpětné splnění původních 100 ms. Nové zde uvedené měřené běhy výjimku nepotřebovaly. ASAN v provedeném rozsahu nezachytil paměťovou chybu.

Nová WP05 Release proxy provedla plný warmup a 20 cyklů nad /24:

| Metrika | Nejhorší hodnota | Původní cíl / mez selhání |
|---|---:|---|
| Otevření Contracts | 546,610 ms | 500 / 1000 ms, nad cílem, pod mezí, výkon odložen |
| Scroll včetně paint | 20,6355 ms | 33 / 100 ms |
| Key editace | 4,0102 ms | 16 / 100 ms |
| Password včetně paint | 7,5941 ms | 16 / 100 ms |
| Kandidát DOM | 0,9517 ms | 1000 / 2000 ms |
| Peak USER / GDI proti baseline | +1025 / +298 | každá kategorie nejvýše +1200 |
| Peak private bytes / working set | +4739072 / +4800512 B | původní memory cíle zachovány |
| Po 20 cyklech USER / GDI | +0 / +0 | nejvýše +5 |
| Po 20 cyklech private bytes / working set | −73728 / +180224 B | nejvýše +1 MiB při cycle testu |
| Izolovaný session cleanup | 78 ms | bez čekání na původní reader deadline |

Broadcast test zachytil query=1, end=2, late dialogs=0, retry=0, Disconnect=0. Native WP03 500 paintů a 20 open/close cyklů prošlo, stejně jako cache/shell testy včetně potlačení zbytečného NIM_MODIFY. Jedná se o místní proxy, ne úplnou produkční start/menu/idle CPU tabulku. Hodnoty Contracts nejsou podnětem k výkonové úpravě v tomto auditu.

## V01, V02 a vizuální výsledky

Native prostředí: Windows build 22631, AMD64 MSVC, skutečný Release, 144 DPI. Čisté layout/placement testy navíc ověřují 96/120/144/192 DPI. Tyto čisté výpočty nejsou vydávány za skutečné monitorové přechody. Byl vizuálně porovnán všech 11 referenčních pohledů s novými 144DPI captures. Výchozí klientské plochy odpovídají předepsaným DIP. Funkční native testy ověřují i malé viewporty, scroll/focus a skutečné controls.

| Nález | Nové měření | Rozhodnutí |
|---|---|---|
| V01, Connect failure/success | Levá paint hrana 47,3333 DIP, reference 50,1848 / 49,5414 DIP, rozdíl 2,8515 / 2,2081 DIP. Také pravá, horní a dolní hrana obou karet jsou pod 6 DIP | **PASS původní tolerance** |
| V02, Edit Config | Pole začíná na 132 px / 144 DPI = 88 DIP, reference `(202−55)×560/949 = 86,7439 DIP`, rozdíl 1,2561 DIP | **PASS původní tolerance** |
| Statistics, spodní hrana horní karty | 386,6667 proti 363,9463 DIP, rozdíl 22,7204 DIP | Raw FAIL 6 DIP, **přijatá geometrická výjimka** |
| Statistics, horní/spodní hrana dolní karty | rozdíl 20,5275 / 18,3857 DIP | Raw FAIL 6 DIP, **přijatá geometrická výjimka** |
| Wizard URL, levá/horní/spodní hrana karty | rozdíl 14,2360 / 26,0836 / 99,4545 DIP | Raw FAIL 6 DIP, **přijatá geometrická výjimka** |

Reference jsou převedeny z klientského výřezu, nikoli z celé bitmapy s titulkem. Crop, pixelové vzorky, skutečné plochy a vzorce jsou v `geometry-findings.json`, `editor-geometry.json`, `extra-geometry-findings.json` a příslušných measure skriptech. Nejistota určení referenční hrany ±2 px se nepřičítá k toleranci. `accepted-visual-deviations.json` odděluje původní výsledek od následného uživatelského přijetí, raw FAIL se nepřepisují.

V nově prohlédnutých výchozích pohledech nebylo zjištěno překrytí nebo zásadně jiná struktura bránící použití. Statistics zachovává dvě horní a jednu dolní kartu, Wizard sidebar/kartu/footer. Identity obsahuje povinné varování o nevratném odebrání zpráv, které má přednost před jednodušším mockupem. Contracts /29 capture používá jiné syntetické adresy, /24 počet všech 252 řádků a 1008 řádkových HWND je ověřen funkčním testem. U malých oken jsou části obsahu mimo viewport úmyslně dosažitelné rolováním, samotný takový výřez není důkazem ztraceného textu.

`visual-matrix.csv` má **44 řádků**, 11 referencí × 4 DPI. Jedenáct skutečných 144DPI pohledů má výsledek lokální vizuální kontroly s přijatými odchylkami. Zbývajících 33 skutečných DPI pohledů je NOT RUN. Úplná metrologie každého prvku a všech textových baseline podle 6/8 DIP je stále NOT RUN, není zaměněna za změřené V01/V02.

| Původní evidenční bod | Nové hodnocení pro tento review |
|---|---|
| E01 | Částečně doplněn 11 captures, dvě původní vady opraveny a změřeny. Kompletní 44pohledová/textová metrologie NOT RUN. Přijaté větší geometrické odchylky samy neblokují |
| E02 | Nové lifecycle a /24 resource/performance hodnoty doplněny. Srovnatelný baseline před celou WP00 až WP06 změnou a úplný start/menu/idle CPU s oddělením fixture režie stále chybí. NOT RUN, omezení evidence pro release, výkon Contracts odložen |
| E03 | Skutečné monitorové přechody/unplug, high contrast, interaktivní čtečka a Alt-Tab zůstávají NOT RUN. Nejsou důvodem tohoto NOT READY v bezpečném lokálním review rozsahu |

## Matice A01 až A32

PASS se vztahuje k uvedenému bezpečně ověřenému rozsahu. Obsahuje-li kritérium neprovedenou fyzickou nebo úplnou metrologickou podmínku, řádek zachovává NOT RUN a popisuje provedenou část. Tím se omezení evidence neskrývá ani nepřevádí na plošný zákaz lokálního user review.

| ID | Výsledek | Nový důkaz nebo konkrétní hranice |
|---|---|---|
| A01 | PASS | WP05 ManualLoadSingleRead a TrayApplication call sites. Menu bez config read, ruční Contracts jeden dokument, reaktivace bez nového čtení |
| A02 | PASS | WP05 availability matrix, missing/failure/check, bez vytvoření defaultu nebo fallbacku |
| A03 | PASS | ContractsModel 9 testů, numeric order/self, native 252 a 4 peery |
| A04 | PASS | config/config_store/CIDR/runtime/payload/Wizard filtry, sdílené /24 až /29, odmítnutí mimo rozsah před publikací, bez migrace |
| A05 | PASS | ContractsModel, sdílené defaultPayloadKey, referenční .2_.3 vektor a oba směry |
| A06 | PASS | Modelová validace a WP05 řádková/keyboard regrese, invalid aktivní Key blokuje i mimo viewport |
| A07 | PASS | Native RandomFailure a immutable target testy, výchozí No, bezpečný RNG failure, Enabled beze změny |
| A08 | PASS | PasswordKey 4 testy a native Unicode limit, empty hash, emoji/combining, 255/256, vadný UTF-16 a NUL, bez truncation |
| A09 | NOT RUN úplně | Native immutable titul, více dětí, poslední OK, generace, hlavní Save a MSAA PASS. Interaktivní čtečka/Alt-Tab NOT RUN |
| A10 | PASS | WP05 lokální dirty/clean/unknown, zero publication před OK, původní Save deadline |
| A11 | PASS | ContractsDocument 6 testů, config_store, preserved DOM/absence, celý kandidát a 512 KiB/4 MiB limity. Skutečná OS publikace NOT RUN |
| A12 | PASS | wp04_/wp06_/happy path: potvrzený Finish zavře, bez automatického Contracts, config čtení a Connect |
| A13 | PASS | Lost Finish a receipt regrese, žádný druhý Finish, pozdější ruční dokument čten jednou |
| A14 | PASS | TrayReaders/WP03Models, oddělený get-config details versus get-status, safe text a změna uloženého snímku |
| A15 | PASS | Fake clock a actual-paint native regrese. wp06 success po READY 3500 ms, žádný Disconnect |
| A16 | PASS | TrayProgress/WP03Models kategorií a native failure, bez automatického zavření chyby |
| A17 | PASS | TrayPolling/readers, nezávislých 500 ms a 5 s, jeden aktivní request na reader |
| A18 | PASS | WP05 independent editors, unknown/check/explicit repeat a ConfigStore testy, bez rebase/CAS/automatického retry |
| A19 | NOT RUN úplně | 252 × 4 nativních HWND, Tab/Shift+Tab, reveal a MSAA PASS. Úplná enumerace skutečnou čtečkou NOT RUN |
| A20 | PŘIJATO S VÝJIMKOU, část NOT RUN | V01/V02 PASS, další geometrie přijata uživatelem. 144DPI struktura a funkční resize/scroll PASS, úplných 44 nativních kombinací a textová metrologie NOT RUN |
| A21 | OTEVŘENÝ TESTOVÝ FAIL T01 | wp04_ 11/11, Editor/Statistics/NUL/Undo/Reset PASS. Dva legacy fix2 scénáře měly 3 selhání v prvních 4 provedeních, pozdější tři původní dvojice PASS, příčina neuzavřena |
| A22 | PASS | Nově porovnáno přesně 7 + 17 IPC commandů proti HEAD, 22 ipc testů, striktní Save ack parser. Nula nových tray/model logging call sites |
| A23 | PASS s historickou výhradou | WP07 chain/ledger audit přečten, poslední normativní náhrady přesně rekonstruují současný client/UX. Žádná vlastní normativní změna. Historická procedurální odchylka WP05 zůstává přiznaná |
| A24 | NOT RUN úplně | Nových 7 PE a srovnatelná poslední delta 0, sekce/hash/AMD64/podpis doloženy. Jednotný skutečný Release baseline před celou implementací chybí |
| A25 | PASS | 17 dist hashů stejných, vlastní UUID endpoints, žádná služba, VPN, instalace ani ProgramData |
| A26 | NOT RUN úplně, Contracts odloženo | Nová 20cyklová /24 proxy pod původními failure mezemi, lifecycle původní limit PASS. Start/menu/idle a jednotný před/po baseline nedoloženy |
| A27 | NOT RUN úplně | 500 paintů, 20 cyklů, resource/cache/NIM_MODIFY PASS, +0 USER/+0 GDI a +180224 B working set po WP05 cyklech. Fyzické DPI cykly NOT RUN |
| A28 | NOT RUN úplně | Čisté placement/geometry včetně záporných souřadnic, nativní resize/reopen PASS. Skutečné unplug/replug a přesuny mezi monitory NOT RUN |
| A29 | NOT RUN úplně | cs-CZ numeric prostředí, en-US UI, exact uint64/text/model testy PASS. Samostatná změna systémového locale a všechny kombinace zalamování NOT RUN |
| A30 | PASS v izolovaném rozsahu | Release/ASAN lifecycle, společný Exit, query/false/true broadcast, 192 opakovaných Editor cyklů, nulové retry/Disconnect. Skutečný logoff NOT RUN |
| A31 | PASS | Čisté model headers a explicitní CMake. Nové linkerové mapy: service/CLI žádný ze tří modelových objektů, unit target všechny tři |
| A32 | PASS | 20 dev scén, wp06_ 9/9, hidden regression 19/19 včetně chyb a no-op, žádné preview markery v produkčních zdrojích/PE |

## PE, linkage, dokumentace a bezpečnostní hranice

Skutečný Release tray má **842240 B**, `.text` 493056 B, `.rdata` 260608 B, `.rsrc` 55808 B, SHA-256 `A54F62B29B43822EA599A9DDFD28379A62CBA682031A52250F8CE3E6CD77B947`, Machine 0x8664 a NotSigned. Proti poslednímu předání je delta EXE i všech tří sekcí **0 B, 0 %**, hash a podpisový stav jsou stejné. RelWithDebInfo tray má 832000 B. Hodnoty pocházejí z PE, velikost PDB se do EXE metriky nezapočítává.

`production-pe.json` a `pe-delta.json` obsahují všech sedm binárek včetně baseline archivní kopie. Nebyly nalezeny ASCII/UTF-16 fixture markery. `linkage-results.json` a nové linkerové mapy ověřují, že `contracts_document.cpp.obj`, `contracts_model.cpp.obj` a `password_key_model.cpp.obj` nejsou zataženy do service/CLI. Diagnostický relink jde jen do evidence, nikoli přes produkční EXE.

Nová read-only kontrola seznamů IPC porovnává aktuální zdroje proti HEAD. Zůstává přesně 7 základních a 17 Wizard/config příkazů. Save potvrzení stále vyžaduje původní requestId/version/schema a včasné odpovídající `ok`, neurčitý výsledek se nepřeklasifikuje na historickou publikaci podle shody dokumentu. Worker vlastní hodnotový stav a při abandon nemůže doručit do uvolněné generace. End-session nečeká na IPC deadline ani neprovádí Disconnect/rollback.

Normativní kontrola rekonstruuje obě současné dokumentace z poslední baseline a jejího doslovného ledgeru. Textový scan aktivních starých konfliktních pravidel má 0 shod. Stejně tak konflikt markery, produkční preview markery, Win32 typy v přenosných model headers a nové tray/model logging call sites. `git diff --check` prošel. Dvanáct rozhodnutí plánu zůstává významově zachováno, výjimky geometrie/časů mají původ v aktuálním uživatelském pokynu, nikoli ve skrytě změněné normě.

Zvolený rozsah nespouštěl skutečný ConfigStore na produkční cestě, HTTP, VPN, SCM, Wintun, install/uninstall, multiuser nebo distribuční/signature E2E. Nenačítal skutečná uživatelská tajemství. Hesla a klíče ve screenshot fixture jsou veřejné syntetické hodnoty. Best-effort čištění bufferů není tvrzením o absolutním vymazání kopií OS, undo, alokátoru nebo clipboardu.

## Reprodukce a předání

Vlastní evidence obsahuje `build-production.ps1`, `build-ui.ps1`, `build-asan.ps1`, `configure-build.ps1`, `run-unit.ps1`, `run-native.ps1`, `run-asan-*.ps1`, `run-lifecycle.ps1`, `diagnose-fix2.ps1`, oba measure skripty, `visual-matrix.ps1`, `pe.ps1`, `linkage.ps1` a `audit.ps1`. Všechny binární testy byly spuštěny s konkrétním nenulovým filtrem. Konkrétní logy a agregované počty jsou uvedeny výše. K opakování pouze finální read-only kontroly integrity lze použít:

~~~powershell
& ([scriptblock]::Create([IO.File]::ReadAllText('C:/git/phpvpn/work/logs/wp08-final-review/audit.ps1')))
~~~

Před navazující prací ověř `final-files.json`, `final-status.txt`, `scope-audit.json` a obě line-ending evidence. Zachovej všechny převzaté necommitnuté změny. `Unexpected` ve scope vědomě uchovává obě pozorované změny bajtů, `ExternalLineEndingOnly` je vysvětluje a `Unexplained` je prázdné. Nesmí se vydávat za původní 340/340 závěrečnou hashovou shodu.

**K dořešení se vrací pouze T01.** Nedoporučuji nový geometry fix, výkonovou optimalizaci Contracts ani přepis normativních časů. Po vysvětlení nestability se má zopakovat příslušná úzká Wizard regrese a závěrečné převzetí WP08. Tento výsledek není READY a není oprávněním k produkčnímu vydání.
