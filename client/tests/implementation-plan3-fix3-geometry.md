# WP03/WP02: cílená oprava geometrie po WP08

**V01 a V02 opraveny, připraveno k opakovanému převzetí WP08. S8 nadále NOT READY.** Oprava neuzavírá úplnou A20, chybějící benchmark E02 ani prostředí E03. Při kontrole byly kvantifikovány další hrany karty Connect a zjištěn samostatný problém skrytého spuštění testu přes runner. Tyto nálezy jsou předány koordinátorce bez rozšíření implementačního rozsahu.

Práce 8. září 2026 přímo v C:\git\phpvpn, HEAD f4b585ff2232d19626c870821a40218f525da213. Žádný commit, push, worktree, instalace ani kopie do distribuce. Evidence je v [work/logs/wp08-geometry-fix](../../work/logs/wp08-geometry-fix).

## Převzetí a vlastnictví

Před zápisy ověřeno všech 335 délek a SHA-256 podle WP08 final-files.json, přesný Git inventář a status. Přečteny plan3_fix3.md, plan3_fix3_work_for_agents.md, tray_UX.md, client.md, protocol.md, WP08 report/evidence a úplné relevantní zdroje layoutů, produkčních oken, společných controls/resources/placement a testů. Connect vlastní WP03, společný editorový layout WP02 s integrací WP03. Historické předání WP03 potvrzuje tuto hranici.

Výchozí hashe/status, kopie měněných souborů, PE a inventář dist jsou v baseline/. Závěrečný audit znovu rekonstruuje normativní řetězec WP03 až WP07 a přesné dvě vlastní náhrady. Historické reporty včetně WP08 se nepřepisují. Původní schválení hranatých rohů platí, vykreslování rohů nebylo změněno.

## Implementace

- progress_layout.h: společný vodorovný okraj 48 DIP, pod 600 DIP nadále 16 DIP. Svislý okraj zůstává 32/16 DIP. Nadpis, karta, vnitřní prvky a výsledkový panel zachovávají společné zarovnání.
- progress_window.cpp: měření zalamovaného textu používá stejný helper progressHorizontalMargin jako umístění. Nevzniká rozpor mezi změřenou šířkou a skutečným HWND.
- layout.cpp: pouze editable větev computeEditorLayout. Záhlaví začíná na 16 DIP a má 72 DIP včetně mezer. Textové pole začíná na 88 DIP. Na malé výšce se rozměry omezí dostupným prostorem. Patička, horizontální editorové okraje a Show Info zůstávají stejné.
- test_tray_layout.cpp: dvě nové reference-based zkoušky s původní tolerancí 6 DIP, obě podmínky selhaly před opravou ve všech čtyřech DPI. Dále kontrolují zarovnání, rozdělení úzkého obsahu, oddělené záhlaví a zachovanou patičku/Show Info.
- test_wp03_windows.cpp: nový skutečný HWND test přes default, střední a úzkou plochu a zpět. Měří skutečný client/DPI, hranu z umístění detailů a textového pole, viditelnost fokusovaných detailů a zachování editorového focusu, selection a undo.
- tray_UX.md: pouze dva nové odstavce UX05/UX06 konkretizují tyto rozestupy. Přesné Before/After jsou v normative-replacements.json. Tolerance UX14 se nemění. client.md již odkazuje na příslušný UX kontrakt a neobsahuje opravované souřadnice, jeho změna nebyla potřebná. protocol.md se nemění.

Celkem šest změněných převzatých souborů a tento nový report. Žádná změna Contracts, Wizard Finish, Save/Exit/logoff, readerů, IPC, služby, runneru ani CMake.

## Nezávislé měření

Reference mají vlastní titulek a rámeček, nativní PrintWindow snímky obsahují jen klientskou plochu. Ořezy ověřeny vizuálně a hranami pixelů. Connect: x=1, y=79, šířka 1461, výška 995 px. Editor: x=1, y=55, šířka 1563, výška 949 px. Soubor editconfig.png má celkem 1565×1005 px. Převod je xDIP=(x−cropX)×targetWidth/cropWidth a obdobně y. Nejistota určení referenční hrany ±2 px se nepřičítá k toleranci.

| Opravovaný prvek | Reference WP08 | Před opravou | Po opravě | Absolutní rozdíl |
|---|---:|---:|---:|---:|
| Connect levá hrana layoutu | 50,1848 DIP | 32 DIP | 48 DIP | 2,1848 DIP |
| Editor horní hrana textového HWND | 86,7439 DIP | 116 DIP | 88 DIP | 1,2561 DIP |

Nativní klientské plochy jsou přesně 1410×1020 px a 1620×840 px při skutečných 144 DPI. geometry-findings.json a pixel-samples.json navíc uchovávají nezávislé vzorkování vykreslených hran. Connect1 má nejsilnější referenční hranu x=79, Connect2 x=78. Nativní centrované pero karty zasahuje na x=71 místo nominálního x=72. Konzervativní porovnání samotných vykreslených hran proto dává odchylky 2,8515 a 2,2081 DIP. Editor má skutečnou pixelovou hranu y=132, tedy 88 DIP, rozdíl 1,2561 DIP. Všechny tři opravené hrany jsou pod 6 DIP. Nejde o měření textové baseline s tolerancí 8 DIP.

| DPI | Čistá geometrie V01/V02 | Nativní snímky V01/V02 |
|---:|---|---|
| 96 | PASS | NOT RUN |
| 120 | PASS | NOT RUN |
| 144 | PASS | PASS |
| 192 | PASS | NOT RUN |

Na prostředí je jeden monitor 144 DPI, pracovní plocha 0,0,2560,1528. Systémové měřítko nebylo měněno. Prohlédnuty nové success/failure/editor a úzký failure snímek. Zalamovaný spodní panel je v úzkém okně dostupný svislým posunem. Plná geometrie a textová metrologie těchto obrazovek ani ostatních osmi referencí tím není PASS.

Snímky: [Connect success](../../work/logs/wp08-geometry-fix/captures-wp03/connect-success.png), [Connect failure](../../work/logs/wp08-geometry-fix/captures-wp03/connect-failure.png), [Editor](../../work/logs/wp08-geometry-fix/captures-wp03/edit-config.png), [úzký Connect](../../work/logs/wp08-geometry-fix/captures-wp03/connect-narrow.png).

## Opakované kontroly

Windows 11 build 22631.6199, AMD64, cs-CZ numeric culture, en-US UICulture. MSVC x64 19.44, CMake 4.4.2, Ninja 1.13.2. Skutečný Release v wp03-ui-release, RelWithDebInfo v release, Release s /fsanitize=address v wp03-lifecycle-asan. Žádné nové závislosti.

| Kontrola | Výsledek a evidence |
|---|---|
| Build produkční tray/service/CLI/unit a obou UI cílů | PASS. build-production.log, build-ui.log, build-asan.log. Pouze explicitní cíle, žádné all/install/package. |
| Předopravové regresní testy | 6 případů, právě 2 očekávané FAIL. geometry-red.log. |
| Cílené unit | 184 spuštění, 170 unikátních případů, 0 failed. 29 filtrů včetně 22 ipc. případů. unit-results-complete.json. |
| Nativní funkční regrese | 53 případů, 0 failed. WP03Windows 7, WP03SaveWindows 4, WP05Windows 13, wp04 11, wp06 9 a sedm dalších původních filtrů s celkem 9 případy. native-results.json. |
| AddressSanitizer | 33 unit a 11 native případů, 0 failed, bez sanitizer hlášení. halt_on_error=1:abort_on_error=1. asan-unit-*.log, asan-native-*.log. |
| Connect countdown | Skutečný visible-to-close 3391 ms, uvnitř 3000 až 3500 ms, 3/2/1 a 0 Disconnect. native-WP03Windows.log. |
| Paint/objekty | 500 paintů a 20 open/close cyklů Progress/Editor/Statistics, GDI/USER delta 0/0. PASS také pod ASAN. |
| Runner build/no-op | Debug inkrementální sestavení PASS, druhý build ninja: no work to do, 0,060 s. |
| Runner scény | Debug i Release wp06_scene_matrix: po 1 případu se všemi 21 scénami PASS. Původní nativní wp06 navíc ověřila dlouhý preview, deadline a izolaci. |
| Runner nulový filtr | Očekávaný exit 1 a 0 případů, žádné předstírání úspěchu. runner-zero.log. |
| Runner editorový filtr | FAIL při skrytém spuštění, samostatný nález níže. Přímý Debug i přímý Release PASS. |
| Linkage/PE | AMD64, NotSigned, žádné hledané fixture/preview markery. Čerstvé diagnostické mapy: service/CLI 0 nepoužitých modelových objektů, unit všechny tři. |
| Integrita a dokumentace | Žádná změna mimo scope, 17/17 dist a 16/16 historických souborů, přesný normativní řetězec i vlastní dvě náhrady. diff --check 0, konflikt/preview/logging scan 0. |

Reprodukovatelné skripty build-production.ps1, build-ui.ps1, build-asan.ps1, run-unit.ps1, run-native.ps1, run-asan-unit.ps1, run-asan-native.ps1, measure.ps1, pe.ps1, linkage.ps1 a audit.ps1 zapisují pouze vlastní evidenci pod work. Unit runner používá první poziční substring, ověřuje nenulový počet a návratový kód. IPC filtr je navíc zaznamenán v unit-ipc.log.

## Výkon a binárky

Původní funkční native běh zahrnoval 20cyklovou WP05 proxy při souběhu s kompilací. Zachovaný výsledek měl nejhorší scroll 73,4646 ms, pod mezí 100 ms. Kvůli tomuto souběhu byl po dokončení všech buildů a ostatních UI procesů proveden samostatný warmup +20cyklový Release běh v performance-final.log. Jde nadále o in-process fixture UI proxy, nikoli celou A26.

| Nejhorší metrika finálních 20 cyklů | Hodnota | Cíl / mez selhání |
|---|---:|---|
| Otevření 252 řádků včetně paint | 653,685 ms | 500 / 1000 ms |
| Scroll/focus/paint | 28,0774 ms | 33 / 100 ms |
| Key vstup/paint | 7,6537 ms | 16 / 100 ms |
| Password SHA/paint | 5,8772 ms | 16 / 100 ms |
| Kandidát | 0,9762 ms | 1000 / 2000 ms |
| Peak working set navíc | 5 496 832 B | 5 / 10 MiB |
| Peak USER/GDI navíc | 1025 / 298 | nejvýše +1200 každého |
| Po cyklech USER/GDI navíc | 1 / 0 | nejvýše +5 každého |
| Po cyklech working set/private bytes navíc | 659 456 / 344 064 B | nejvýše +1 MiB |

Meze selhání jsou splněny. Open a peak working set jsou nad cíli. Uživatel výslovně odložil výkonové potíže Communication Contracts do samostatného budoucího tasku. Tyto nesplněné doporučené cíle neblokují tuto úzkou opravu V01/V02 a nevyvolávají další změny. Naměřené výsledky zůstávají zachovány. Žádné tvrzení o plném production start/menu/idle CPU, oddělené režii backendu nebo jednotné baseline celé WP00 až WP07.

| Produkční Release tray | Před opravou | Po opravě | Delta B / % |
|---|---:|---:|---:|
| EXE | 841728 | 842240 | +512 / +0,06083 % |
| .text | 492544 | 493056 | +512 / +0,10395 % |
| .rdata | 260608 | 260608 | 0 / 0 % |
| .rsrc | 55808 | 55808 | 0 / 0 % |

Stejný optimalizovaný Release AMD64, oba NotSigned, bez PDB v EXE metrice. Nový SHA-256 B47F8DD28C306E02840AE536673857B547BD759D14031C1148F0A4BCF9467FDE. RelWithDebInfo tray zůstává 831488 B a má nový hash 47DCBC717C58E36E4E5DDEDF845B30308E896779CD9CE924C4A6FD910BF358F2. Service/CLI velikosti a sekce beze změny. Podrobnosti v production-pe.json, baseline/production-pe.json, pe-delta.json a linkage-results.json.

## Další nálezy předané koordinátorce

**E01, další geometrie Connectu:** nezávislý pixelový scan odhaluje následující rozdíly, které existují mimo opravované V01/V02. Nejde o výjimku pro hranaté rohy. Ostatní hrany nebyly cíleně opravovány.

| Hrana hlavní karty | Reference connect1/connect2 DIP | Nově vykresleno DIP | Odchylka DIP |
|---|---:|---:|---:|
| Pravá | 876,95 / 874,37 | 890,67 | 13,72 / 16,29 |
| Horní | 157,87 / 157,87 | 168,67 | 10,80 / 10,80 |
| Spodní | 511,88 / 511,20 | 488,00 | 23,88 / 23,20 |

Důkaz geometry-findings.json a plné pixelové vzorky. Symetrické vodorovné odsazení zmenšilo i pravou odchylku, ale neuzavřelo ji. WP08 musí tyto prvky převzít jako samostatné FAIL v rámci E01. Plná 44řádková akceptace zůstává otevřená.

**Runner / WP06:** příkaz dev-tray.ps1 -Mode Test -Configuration Debug -Filter editor_reset_failure_generation selhal hláškou footer button is hidden. Stejné Debug EXE spuštěné přímo prošlo. Stejné aktuální Release EXE prošlo v nativní sadě a selhalo při kontrolním Start-Process -WindowStyle Hidden. Rozdíl je tedy reprodukován změnou způsobu spuštění téhož programu. Runner nastavuje Hidden, první ShowWindow editoru podléhá startup show state, test footer vyžaduje IsWindowVisible. Runner a tento viditelnostní kód jsou převzaté beze změny. Scénový preview kód má explicitní následné ShowWindow a jeho matice přes runner prošla. Tento nález není opraven a není vydáván za úspěšný runner editorový test. Důkazy runner-debug-test.log, debug-editor-direct.log, release-editor-hidden.log a runner-*-scenes.log. Rozsáhlé nové negativní mutační runner scénáře nebyly prováděny.

E02/E03 nad rámec této opravy zůstávají neověřené: plný srovnatelný benchmark, fyzické DPI přechody/odpojování, high contrast, interaktivní screen reader a Alt-Tab. Výkon Communication Contracts je podle výslovného rozhodnutí uživatele odložen do samostatného budoucího tasku a není blokátorem předání V01/V02. Neprováděny úplné agregované CTest, skutečné network/service/lifecycle/multiuser/instalační/distribuční sady ani OS logoff. Soukromá fixture není důkazem reálné VPN.

## Stav pro nové WP08

Ověřit scope-audit.json, final-files.json a final-status.txt tohoto předání proti aktuálnímu checkoutu. Převzít šest změněných souborů a tento report bez resetu dosavadních WP00 až WP07. V01/V02 mají konkrétní nové PASS důkazy, ostatní nálezy a chybějící brány se tím nepřepisují. Žádná produkční služba, CLI nebo tray nebyla spuštěna, registrována či ukončena. Žádný přístup do ProgramData ani do produkční pipe. Vlastní testovací procesy skončily, existující uživatelské procesy zůstaly nedotčené.
