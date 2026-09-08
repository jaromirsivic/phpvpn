# WP03: dokončení hran karty Connect po WP08

**Zbývající geometrie hlavní karty E01 je opravena. Převzetí má samostatnou výhradu: jeden nezměněný editorový ASAN test opakovaně překročil časový limit zániku okna. S8 zůstává NOT READY.** Skrytý editorový runner nadále vyžaduje samostatný návrat WP06. Výkon Communication Contracts zůstává podle rozhodnutí uživatele odložený.

Práce přímo v C:\git\phpvpn dne 8. září 2026, HEAD f4b585ff2232d19626c870821a40218f525da213. Evidence: [work/logs/wp08-connect-geometry](../../work/logs/wp08-connect-geometry). Bez commitu, push, worktree, instalace nebo změny dist.

## Převzetí a rozsah

Před zápisy ověřeno 336 převzatých souborů podle final-files.json předchozí opravy, jejich délky a SHA-256, přesný Git inventář a status. Přečteny celé plan3_fix3.md, plan3_fix3_work_for_agents.md, tray_UX.md, client.md, protocol.md, reporty WP08 a předchozí geometrické opravy, relevantní layouty, produkční okna, společné resources/controls/placement a testy. Nová baseline uchovává převzatý manifest, status, kopie měněných souborů, PE a inventář distribuce. reconstruction-check.json ověřuje všech 336 původních obsahů kombinací těchto kopií a nezměněných souborů.

Změněno pět převzatých souborů a přidán tento report:

- progress_layout.h: pravý okraj 64 DIP při zachované levé hraně 48 DIP. Pod 600 DIP jsou oba okraje 16 DIP. Mezera nad kartou 12 DIP místo 24 DIP. Nadpisy sekcí mají krok 44 DIP místo 36 DIP, běžné řádky 40 DIP místo 36 DIP. Výška karty zůstává odvozena z obsahu, řádky na úzké ploše se skládají a celek scrolluje.
- progress_window.cpp: měření textu i umístění používají společnou pravou souřadnici progressContentRight. Změna šířky tak nemůže vytvořit rozdíl mezi měřením zalamování a skutečným HWND.
- test_tray_layout.cpp: nový test všech čtyř hran proti oběma referencím, 96/120/144/192 DPI, success/failure a rozsah výšky hlavního nadpisu 42 až 46 DIP. Druhý nový test kontroluje obsah karet při šířkách 320, 399, 400, 599, 600, 640, 759, 760 a 940 DIP, zalomených textech a obou výsledcích. Původní editorová kontrola zůstává zachována.
- test_wp03_windows.cpp: nový test měří přímo vykreslenou neutrální hranu pomocí PrintWindow a pixelového scanu přes klientskou plochu. Nepočítá ji z produkčního layout helperu. Zachovává detailní HWND a obsah, ověřuje focus a návrat na výchozí rozměr. Vznikají nové success/failure a úzké snímky.
- tray_UX.md: jediná přesná náhrada úvodního odstavce UX05. Before/After je v normative-replacements.json, navazuje na N09b/A20/UX14. Tolerance zůstává 6 DIP. client.md, protocol.md, UX06 a implementace editorového layoutu se nemění.

Nový nativní test odhalil nedostatek převzatého resize testu: sizeAndCenter u již vytvořeného okna pouze opraví jeho umístění a zachová velikost. Nový testovací resizeClient používá SetWindowPos, zohlední objevení scrollbaru a ověří skutečné client rozměry po každé změně. Touto opravou je doplněn i původní společný Connect/Editor test. Předchozí report se nepřepisuje. Produkční placement nebyl měněn.

Původní schválení hranatých rohů platí. Painter, rohy, fonty, stavové modely, čtečky, IPC, Contracts, Wizard, Save/Exit/logoff, služba, runner a CMake zůstávají převzaté.

## Nezávislá geometrie

Obě reference mají celkem 1463 × 1075 px. Vizuální kontrola rámu a horizontální linky pod titulkem spolu s reference-frame-samples.json potvrzuje převzatý crop x=1, y=79, šířka 1461, výška 995 px. Referenční rám má několik antialiasovaných pixelů, nejistota hrany je ±2 px a nepřičítá se k toleranci. Převod celé klientské plochy je xDIP=(x−1)×940/1461 a yDIP=(y−79)×680/995.

Nové nativní snímky obsahují pouze klientskou plochu, přesně 1410 × 1020 px při skutečných 144 DPI. Reference byly znovu změřeny v measure.ps1, úplné vzorky jsou v pixel-samples.json. Scan spodní hrany končí před samostatným barevným výsledkovým panelem. Neutrální pero má místy dva fyzické pixely, proto tabulka uvádí celý naměřený rozsah a konzervativní největší odchylku vůči oběma referencím.

| Hrana | Reference connect1 / connect2 DIP | Nově vykresleno DIP | Největší rozdíl DIP |
|---|---:|---:|---:|
| Levá | 50,1848 / 49,5414 | 47,3333 | 2,8515 |
| Pravá | 876,9473 / 874,3737 | 874,6667 až 875,3333 | 2,2806 |
| Horní | 157,8693 / 157,8693 | 156,6667 | 1,2027 |
| Spodní | 511,8794 / 511,1960 | 512,0000 až 512,6667 | 1,4707 |

Všechny čtyři hrany procházejí původní tolerancí 6 DIP. Nativní regresní test používá vnější pixely L/T/R/B = 71/235/1313/769 a ověřuje oba mockupy před skutečným zmenšením i po návratu. Nominální levá souřadnice zůstává 48 DIP. Editor má skutečný top 132 px, tedy 88 DIP, odchylku 1,2561 DIP od reference.

| DPI | Čistá geometrie a responsive obsah | Skutečné nativní vykreslení |
|---:|---|---|
| 96 | PASS | NOT RUN |
| 120 | PASS | NOT RUN |
| 144 | PASS | PASS |
| 192 | PASS | NOT RUN |

Na dostupném monitoru 144 DPI byla ověřena skutečná klientská plocha Connect 940 × 680, 640 × 480 a 320 × 280 DIP a návrat. Editor prošel 1080 × 560, 640 × 400 a 320 × 280 DIP a návratem, se zachovaným focusem, selection a undo. U menších ploch se neposuzují stejné absolutní souřadnice jako u výchozí reference, protože obsah se podle UX přeskupuje a scrolluje. Dostupnost polí a spodního výsledkového panelu byla ověřena testy a vizuálně.

Nové snímky: [success](../../work/logs/wp08-connect-geometry/captures-wp03/connect-reference-success.png), [failure](../../work/logs/wp08-connect-geometry/captures-wp03/connect-reference-failure.png), [úzký success](../../work/logs/wp08-connect-geometry/captures-wp03/connect-reference-success-narrow.png), [úzký failure](../../work/logs/wp08-connect-geometry/captures-wp03/connect-reference-failure-narrow.png), [editor](../../work/logs/wp08-connect-geometry/captures-wp03/edit-config.png). BMP originály a PNG hashe jsou zachovány.

## Opakované kontroly

Windows 11 build 22631, AMD64, cs-CZ numeric culture, en-US UICulture. MSVC 19.44 x64, CMake 4.4.2, Ninja 1.13.2. Pouze explicitní produkční/unit/UI cíle v existujících Release, RelWithDebInfo a ASAN buildech. Žádný agregovaný all/CTest/install/package.

| Kontrola | Výsledek |
|---|---|
| Předopravový test hran | 7 případů, právě nový test hran FAIL. geometry-red.log. Po opravě obě nové geometrické zkoušky PASS. |
| Produkční a UI build | PASS. Tray, service, CLI, unit a oba UI cíle. První kompilace nového testu odhalila nejednoznačný CheckpointResult, opravena kvalifikací namespace. |
| Unit | 164 spuštění, 161 unikátních případů, 0 failed. unit-results.json a unit-summary.json. |
| Native Release | 41 případů, 0 failed. WP03Windows 8, WP03SaveWindows 4, wp04 11, wp06 9 a sedm dalších filtrů s 9 případy. native-results.json. |
| ASAN unit | 35 případů, 0 failed, bez sanitizer hlášení. |
| ASAN native Connect/Editor/Statistics | WP03Windows 8 případů, 0 failed, bez sanitizer hlášení. |
| ASAN native editor Save | 4 případy, 1 časový FAIL. Samostatné opakování tohoto případu rovněž FAIL, podrobnosti níže. |
| Odpočet | Visible-to-close 3391 ms, v původním intervalu 3000 až 3500 ms, 3/2/1 a 0 Disconnect. |
| Zdroje | 500 paintů a 20 cyklů tří oken, GDI/USER delta 0/0, také pod ASAN. |
| Runner build/no-op | Debug inkrementální build PASS. Opakovaný build bez práce, 0,064 s. |
| Runner scénová matice | Debug i Release: každý 1 případ se všemi 21 scénami PASS. |
| Runner nulový filtr | Očekávaných 0 případů a exit 1. runner-zero-result.json. |
| PE a linkage | AMD64, NotSigned, bez hledaných fixture/preview markerů. Service/CLI 0 nepotřebných Contracts modelových objektů, unit všechny 3. |
| Integrita | Pět změněných převzatých souborů plus tento report. Distribuce 17/17 a historické reporty 16/16 beze změny. Přesný normativní řetězec WP03 až předchozí oprava i vlastní náhrada. diff --check a conflict/preview/logging scan čisté. |

## Samostatný ASAN nález

Nezměněný test WP03SaveWindows.TwentyFourCreateSaveResetEndSessionDestroyCycles kontroluje původní podmínku worst < 100 ms. První ASAN běh naměřil 125 ms, jediné samostatné opakování 109 ms. Ostatní kontrolované invarianty, IPC počty a generace prošly. ASAN nehlásil chybu paměti. Stejná Release zkouška prošla s nejhorším časem 16 ms. Důkazy jsou asan-native-WP03SaveWindows.log a asan-native-retry-WP03SaveWindows.TwentyFourCreateSaveResetEndSessionDestroyCycles.log.

Příčina časového rozdílu nebyla prokázána. Není vydáván za PASS ani za potvrzenou regresi geometrie. Zdroj editorového lifecycle a tohoto testu se proti převzetí nezměnil. Limit nebyl uvolněn a další opakování pro získání zeleného výsledku nebyla prováděna. Nález byl předán koordinátorce samostatně, není součástí odloženého výkonu Communication Contracts. Před bezvýhradným přijetím celé ověřovací sady vyžaduje rozhodnutí koordinátorky nebo navazující diagnostiku.

## Binárky a zbývající předání

Optimalizovaný Release tray má nadále 842240 B, .text 493056 B, .rdata 260608 B a .rsrc 55808 B. Všechny velikostní delty jsou 0 B / 0 %. Nový SHA-256 je A54F62B29B43822EA599A9DDFD28379A62CBA682031A52250F8CE3E6CD77B947. RelWithDebInfo tray má 832000 B, delta +512 B / +0,06158 %, stejná delta .text. Service/CLI velikosti a sekce jsou beze změny. Podrobnosti v production-pe.json, pe-delta.json a linkage-results.json.

Předchozí skrytý editorový runner nebyl měněn ani označen za opravený. Zachována celá evidence work/logs/wp08-geometry-fix včetně runner-debug-test.log, debug-editor-direct.log, release-editor-hidden.log a původního reportu. Její délky a hashe kontroluje inherited-evidence-check.json. Pro WP06 zůstává rozdíl mezi přímým spuštěním a startup show state při Start-Process -WindowStyle Hidden.

Celá A20, ostatní prvky jedenácti mockupů, benchmark E02 a fyzické prostředí E03 tím nejsou uzavřeny. Nativní vykreslení při jiných DPI, fyzické přechody monitorů, high contrast, interaktivní screen reader a Alt-Tab zůstávají NOT RUN. Výkon Contracts není blokátorem této úzké opravy a jeho limity se nemění.

Pro další převzetí použij scope-audit.json, final-files.json a final-status.txt tohoto balíčku. E01 pro všechny čtyři hrany hlavní karty lze převzít jako PASS s výše uvedenými DPI hranicemi důkazu. Celá ASAN sada má výhradu uvedenou výše a S8 zůstává NOT READY. Testy používaly pouze soukromé fixture endpointy a syntetickou paměť. Produkční služba, CLI ani tray nebyly tímto taskem spuštěny, ukončeny nebo registrovány. ProgramData, distribuce a existující uživatelské procesy zůstaly nedotčené.
