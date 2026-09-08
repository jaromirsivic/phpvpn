# WP03: diagnostika editorového lifecycle timing po Connect geometry

**Editorový lifecycle je pro první iteraci přijatelný na základě výslovné uživatelské výjimky. Původní limit pod 100 ms není spolehlivě splněn a zůstává v testu. Produkční oprava nebyla provedena, protože se neprokázalo čekání na odpověď IPC, chybné vlastnictví workeru ani paměťová chyba. S8 zůstává NOT READY z ostatních důvodů.**

Práce přímo v C:\git\phpvpn dne 8. září 2026, HEAD f4b585ff2232d19626c870821a40218f525da213. Evidence: [work/logs/wp08-editor-lifecycle](../../work/logs/wp08-editor-lifecycle). Bez commitu, push nebo worktree.

## Uživatelská výjimka a rozsah přijetí

Koordinátorka v úloze 01a07e12-7db9-7de2-b9cf-03528fc6a750 předala během diagnostiky toto rozhodnutí uživatele:

> Uživatel nyní výslovně akceptuje pro první iteraci výsledky překračující původní časové limity až do čtyřnásobku. Pro editorový lifecycle s původním limitem pod 100 ms je tedy dočasně akceptovatelná hodnota pod 400 ms.

Současně výslovně požadovala zachovat původní normativní limit, testovací data a naměřené hodnoty, nezvyšovat limit v kódu a výsledky 109 až 125 ms vyhodnotit jako akceptované pro tuto iteraci, pokud není jiný důkaz skutečného bezpečnostního nebo lifecycle problému. Tento report výjimku eviduje, nepřepisuje normativní dokumenty ani původní neúspěšné logy. Platí striktní nerovnost pod 400 ms, nikoli nejvýše 400 ms.

Předchozí hodnoty 125 a 109 ms, nová reprodukce 219 ms i diagnostické maximum 141 ms jsou pod dočasnou hranicí. Jde o přijetí s výjimkou, nikoli o původní testový PASS nebo o odstranění příčiny časového kolísání. Výkon Communication Contracts zůstává odložený do samostatné úlohy. Jeho implementace ani limity se nemění.

## Převzetí a změny

Před prvním zápisem ověřeno všech 337 souborů předchozího final-files.json podle délky a SHA-256, přesný Git inventář i status. Závazné podklady: plan3_fix3.md, plan3_fix3_work_for_agents.md, tray_UX.md, client.md, protocol.md, předchozí report Connect geometry a jeho evidence. Prostudován editor, TrayApplication, Save/Reset čtečky, dialogový testovací helper a ukončení souvisejících oken.

Změněn pouze `client/tests/unit/test_wp03_save_windows.cpp`, přidán tento report. Test doplňuje diagnostiku do existujících případů:

- Čtyři vzorky QueryPerformanceCounter oddělují obsluhu editorového WM_ENDSESSION, snapshot a kontroly, destrukci TrayApplication včetně jejích členů. QueryThreadCycleTime zaznamenává CPU cykly UI vlákna. Všechny řádky se vypisují až po dokončení 24 cyklů, mimo měřenou oblast.
- Původní GetTickCount64 měření a `PHPVPN_CHECK(worst < 100)` zůstávají. Měřená oblast není zkrácena a obsahuje i malé náklady nových časových vzorků. Počty cyklů, fixture data, držený Save/Reset, pořadí cleanupu a původní invarianty se nemění.
- Deadline test nyní vypisuje čas převzetí výsledku z produkčního `reader.takeSave()` vůči původní deadline a dobu volání `editor.finishSave()`, které zahrnuje nativní MessageBox. Původní kontrola `GetTickCount64() < snapshot.deadlineMs + 250` zůstává po návratu z dialogu.

Produkční zdroje, CMake, runtime, reader API a normativní dokumentace jsou proti převzetí shodné. Connect geometry, editorový top 88 DIP a schválené hranaté rohy zůstaly zachovány. Diagnostické výpisy obsahují pouze časy, CPU cykly a index cyklu, žádný obsah konfigurace.

## Co test měří a co zjistila diagnostika

Původní název lifecycle testu odpovídá celému intervalu: synchronní odeslání WM_ENDSESSION TRUE editoru, čtení a kontroly terminálního snapshotu a `app.reset()`. Poslední krok ukončí i skrytý host a ostatní členy tray aplikace. Uvolnění syntetického backendu, zastavení serveru a následující pumpování zpráv leží až za měřením. Test proto neměří pouze vlastní Save/Reset abandon ani pouze zánik jednoho HWND.

Produkční cesta editoru nejprve terminálně vyřadí Save, zvýší generace, odstraní timer a zničí HWND. Reset worker vlastní sdílený task a hodnoty endpointu, při endSession se odpojí. TrayApplication před destrukcí členů volá abandon všech příslušných čteček. Následné stop vidí prázdný stav a nevstoupí do čekání na done. Krátké mutexy pro vyřazení doručení zůstávají, není zde čekání na dokončení požadavku služby. Test úspěšně zničil všechny UI vlastníky ještě při zadržených syntetických odpovědích Save i Reset. Pozdější uvolnění serveru a pumpování prošlo kontrolami generací, počtů IPC i nepřítomnosti dialogu.

Nová reprodukce na nezměněném zdroji skončila maximem 219 ms a navíc selhala post-dialogová deadline kontrola. Přesná délka tohoto původního dialogu nebyla zaznamenána, proto nelze zpětně prohlásit jeho příčinu za prokázanou. První dočasná diagnostika hrubými hodinami poté naměřila 93 ms. Oba logy jsou zachované, zelený běh nenahrazuje červený.

Ve finální předem omezené sérii ASAN run 1, cycle 2 naměřil původní gate 141 ms. QPC tentýž interval rozložil na:

| Část | Čas |
|---|---:|
| Odeslání WM_ENDSESSION a celé ukončení editoru | 118,329 ms |
| Snapshot a kontroly | 0,0025 ms |
| Destrukce TrayApplication | 10,6586 ms |
| Celkový QPC wall time | 128,990 ms |
| CPU cykly UI vlákna | 28 930 502 |

Počet CPU cyklů není výjimečný vůči ostatním ASAN cyklům, jejich měřený rozsah je 22 104 585 až 31 455 044. Průměr ASAN činil 25,05 milionu oproti 17,87 milionu v Release. Zvýšené náklady instrumentovaného běhu jsou pozorovatelné, samy však nevysvětlují tento wall-time výkyv. Výsledek podporuje vliv nativního UI čekání nebo plánování vláken v intervalu ukončení editoru. Bez ETW/stack trace nelze rozlišit scheduler, interní Windows synchronizaci a případnou vnější UI interakci. Konkrétní čekající funkce uvnitř Windows ani úplná kořenová příčina nebyly prokázány. CPU cykly se nepřevádějí na milisekundy.

GetTickCount64 má v naměřených hodnotách hrubší kvantování přibližně 15 až 16 ms. Release run 2 proto prošel původním gate s 93 ms, ačkoli QPC zaznamenal 101,938 ms. Zelený původní assert tedy sám nedokládá přesný fyzický interval pod 100 ms. I tento QPC výsledek je pod dočasnou hranicí 400 ms a zůstává výslovně uvedený.

Deadline diagnostika v osmi bězích pozorovala výsledek 37 až 53 ms po původní deadline. `finishSave` včetně automaticky potvrzeného nativního dialogu trvalo 31 až 93 ms. Všech osm původních post-dialogových kontrol prošlo. Čas po návratu z dialogu kombinuje polling, vytvoření a obsluhu dialogu a testovací message pump. Není to izolovaná latence rozhodnutí o vypršení deadline. Nové výpisy umožňují při dalším selhání toto rozlišit, bez oslabování původní kontroly.

Při závěrečné kontrole byl diagnostický výpis přesunut až za původní deadline assert, aby stdout nepřidával latenci před kontrolou. Po rebuild obou UI testů proběhla ještě tři cílená opakování deadline případu v Release a tři v ASAN. Všech šest prošlo, pozorování výsledku mělo offset 37 až 54 ms a dialog 47 až 125 ms. Logy deadline-final-* zachovávají tyto další výsledky. Lifecycle část testu ani její vzorky se při této poslední úpravě neměnily.

## Opakovaná měření

MSVC 19.44 AMD64, Windows 11 build 22631, nativní DPI 144. ASAN build používá optimalizovaný Release `/O2 /Ob2 /DNDEBUG` se sanitizerem, srovnávací build je Release bez ASAN. Pro každý build byl spuštěn jeden úvodní běh a tři další samostatné běhy celé čtyřpřípadové sady WP03SaveWindows. Každý obsahuje 24 lifecycle cyklů. Celkem 96 cyklů na build, z toho 72 v hodnocené trojici. Start procesu a vytvoření oken jsou mimo měřený interval. Běhy probíhaly postupně, bez souběžně spouštěné kompilace nebo další testovací sady. Nebyl přidáván další pokus pro získání zeleného výsledku.

| Build a běh | Původní gate maximum | QPC maximum | Původní sada |
|---|---:|---:|---|
| Release úvodní | 94 ms | 101,796 ms | 4/4 PASS |
| Release 1 | 62 ms | 62,7256 ms | 4/4 PASS |
| Release 2 | 93 ms | 101,938 ms | 4/4 PASS |
| Release 3 | 63 ms | 64,7556 ms | 4/4 PASS |
| ASAN úvodní | 78 ms | 68,0212 ms | 4/4 PASS |
| ASAN 1 | 141 ms | 128,990 ms | 3/4 PASS, lifecycle timing FAIL |
| ASAN 2 | 63 ms | 64,4224 ms | 4/4 PASS |
| ASAN 3 | 63 ms | 69,7015 ms | 4/4 PASS |

Všechny výsledky jsou pod uživatelskou hranicí 400 ms. ASAN nehlásil paměťovou chybu. Původní ASAN gate je nadále vyhodnocen jako FAIL, exit 1 v příslušném logu zůstal. Hraniční Release QPC hodnoty se nezamlčují. Pozdější standardní nativní Release regrese naměřila 16 ms a není používána k nahrazení horší série.

Strojově čitelné podklady: lifecycle-cycles.json se všemi 192 cykly, lifecycle-runs.json, lifecycle-summary.json a původní repeated-Release/ASAN-0 až 3.log. Baseline je v asan-native-WP03SaveWindows.log a první rozdělení v asan-native-WP03SaveWindows.TwentyFour.log.

## Regrese a integrita

| Kontrola | Výsledek |
|---|---|
| Build | Oba změněné UI testy Release a ASAN sestaveny. Produkční RelWithDebInfo build a ostatní explicitní cíle beze změny, no-op. |
| Unit | 164 spuštění, 161 unikátních případů, 0 failed. unit-results.json a unit-summary.json. |
| ASAN unit | 35 případů, 0 failed, bez sanitizer hlášení. |
| Native Release | 41 případů, 0 failed. WP03Windows 8, WP03SaveWindows 4, wp04 11, wp06 9 a ostatní související filtry 9. |
| ASAN native geometrie a resources | WP03Windows 8 případů, 0 failed, bez sanitizer hlášení. |
| ASAN editor lifecycle | Výše popsaný timing FAIL 141 ms v opakované sérii, přijatý uživatelskou výjimkou. Ostatní invarianty PASS, bez sanitizer hlášení. |
| Deadline po finálním umístění logu | Release 3/3 a ASAN 3/3 PASS. |
| Connect / Editor geometrie | Všechny čtyři referenční hrany PASS, skutečný Editor top 88 DIP při 144 DPI. Resize, focus, selection a undo PASS. Pure geometrie zachovává 96/120/144/192 DPI. |
| Odpočet | Release visible-to-close 3344 ms, původní interval 3000 až 3500 ms splněn. |
| Zdroje | 500 paintů a 20 cyklů oken, GDI/USER delta 0/0 v Release i ASAN. |
| Runner | Debug build a následný no-op PASS, no-op 0,041 s. Debug i Release mají 20 řádků SCENE OK a 1/1 případ PASS. Nulový filtr má očekávaný exit 1. |
| PE | Všech sedm kontrolovaných PE je bitově shodných s baseline, AMD64 a se stejným podpisovým stavem. Hledané fixture/preview markery v produkci nenalezeny. |
| Linkage | Service/CLI neobsahují žádný ze tří nepotřebných Contracts modelových objektů, unit obsahuje všechny tři. |
| Integrita | 337 původních obsahů přesně rekonstruovatelných, finální inventář 338 souborů. Změněn jediný test a přidán report. Distribuce 17/17 a převzatá evidence 160/160 beze změny. diff --check a conflict/preview/production logging scan čisté. |

Kontrola runneru opravila počet uvedený v předchozím reportu: mapa devScenes obsahuje 20 kombinací, nikoli 21. Také historické logy mají 20 řádků SCENE OK. Nový pomocný ověřovací skript nejprve očekával převzatých 21 a oznámil chybu vlastní kontroly až po úspěšném Debug procesu. Po kontrole zdroje a logu byl očekávaný počet opraven na 20 a ověřena zbývající Release matice i nulový filtr. Runner a jeho testovací scény se neměnily, historický report nebyl přepisován.

Release tray má stále 842240 B, .text 493056 B, .rdata 260608 B a .rsrc 55808 B, SHA-256 A54F62B29B43822EA599A9DDFD28379A62CBA682031A52250F8CE3E6CD77B947. RelWithDebInfo tray má stále 832000 B. Všechny nynější velikostní i sekční delty proti převzetí jsou 0 B, všechny kontrolované hashe shodné. Podklady jsou production-pe.json, pe-delta.json, pe-integrity.json a linkage-results.json.

Finální audit uchovává celý Git diff i vlastní diagnostic-only.diff. reconstruction-check.json ověřuje všech 337 převzatých obsahů pomocí baseline kopie jediného změněného testu a nezměněných souborů. inherited-evidence-check.json ověřuje všechny soubory balíčku Connect geometry. Normativní dokumenty i historické reporty zůstávají beze změny.

## Předání pro WP06

Tato úzká diagnostika neprokázala další skutečný lifecycle nebo paměťový problém, který by rušil uživatelské přijetí první iterace. Normativní výkonový požadavek zůstává nesplněnou výhradou, nikoli změněným limitem. Pro případné další cílené profilování jsou nyní dostupné samostatné intervaly a CPU cykly.

WP06 může navázat na původní problém skrytého editorového runneru E02/E03. Jeho startup show state, chování při Start-Process -WindowStyle Hidden ani produkční EditorWindow::open se zde neměnily. Průchod scénové matice tento problém neuzavírá. Výkon Contracts zůstává samostatně odložený. Ostatní fyzická DPI, přechody monitorů, high contrast, interaktivní screen reader a Alt-Tab nejsou touto diagnostikou doplněny. S8 se neoznačuje READY.

Testy používají pouze soukromé UUID endpointy a syntetická data. Produkční služba, tray a CLI nebyly spuštěny, ukončeny ani registrovány. ProgramData a distribuce nebyly měněny. Předchozí evidence zůstává zachovaná, pro další převzetí slouží nový final-files.json, final-status.txt a scope-audit.json.
