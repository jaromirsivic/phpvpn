# WP06: oprava skrytého spuštění editoru

**Opraveno ve vývojovém runneru, připraveno k novému finálnímu převzetí WP08.** Produkční editor ani jeho lifecycle se nemění. Toto předání uzavírá nález skrytého spuštění. Neuzavírá zbývající vizuální/prostředové mezery A20 ani úplný runtime benchmark A26. Historická E02/E03 v původním WP08 reportu označují širší benchmark/prostředí, jejich význam se tímto reportem nepřepisuje. S8 z těchto dalších důvodů zůstává NOT READY.

Práce 8. září 2026 přímo v C:\git\phpvpn, HEAD f4b585ff2232d19626c870821a40218f525da213. Bez commitu, push, worktree, registrace/spuštění služby, VPN, instalace nebo změny distribuce. Evidence: [work/logs/wp06-hidden-editor](../../work/logs/wp06-hidden-editor).

## Převzetí a rozsah

Před prvním zápisem ověřeno všech 338 převzatých souborů podle SHA-256 a délky, přesný inventář Git i krátký status proti wp08-editor-lifecycle/final-files.json a final-status.txt. V baseline/files jsou úplné bajtové kopie všech 338 souborů, nikoli pouze vlastní dotčené soubory. Převzatá evidence má samostatný hashový inventář.

Podklady zahrnují celé plan3_fix3.md, plan3_fix3_work_for_agents.md, tray_UX.md, client.md, protocol.md, reporty WP08, geometry, connect-geometry a editor-lifecycle-timing, zdroje runneru, EditorWindow, TrayApplication, relevantní čtečky, UI harness, preview a testy. Rozsáhlé výpisy se musely dočíst po menších částech kvůli zkrácení nástrojem. Úplná četba některých těchto částí byla doplněna během ověřování po první úzké opravě. Předopravové ověření integrity 338 souborů tím nebylo dotčeno.

Vlastní změny tvoří pět převzatých souborů a dva nové:

- client/tools/dev-tray.ps1: pouze způsob spuštění a převzetí stdout/stderr/exit testovacího hostu.
- client/tests/ui/tray_ui_tests.cpp: čtyři diagnostické hodnoty v existujícím editor_reset_failure_generation, bez změny jeho akcí, podmínek, čekání nebo životnosti.
- client/tests/ui/test-hidden-launch.ps1: nová explicitní desktopová regrese runneru.
- client/tests/ui/README.md, client.md, tray_UX.md: workflow F09/A32 a jeho ověření.
- Tento nový report.

Žádná změna produkčních zdrojů, CMake, Contracts, Wizard Finish, Save/Reset/Exit/logoff, IPC, globální validace nebo síťových cest. Zachovány Connect geometry se čtyřmi hranami karty, editorový top 88 DIP a schválené hranaté rohy.

## Příčina a kontrolovaný důkaz

Původní runner spouštěl konzolový UI harness přes Start-Process -WindowStyle Hidden. Windows přitom předalo STARTF_USESHOWWINDOW/SW_HIDE. EditorWindow::open vytvoří skutečný top-level HWND bez WS_VISIBLE a zavolá ShowWindow jednou. Parametr první ShowWindow je přepsán startup informací procesu, proto editor zůstal neviditelný. Test selhal už při kontrole viditelnosti patičky, před Resetem a jeho dialogy.

Stejná binárka, filtr a cwd poskytly tento párový výsledek:

| Spuštění | Debug | Release | Diagnostika |
|---|---|---|---|
| Přímé EXE | 1 test PASS, exit 0 | 1 test PASS, exit 0 | startupHidden=0, console=0, visible=1, focus=1 |
| Původní Start-Process Hidden | 1 očekávaný FAIL, exit 1 | 1 očekávaný FAIL, exit 1 | startupHidden=1, console=1, visible=0, focus=1 |
| Opravený runner spuštěný Hidden | 1 test PASS, exit 0 | 1 test PASS, exit 0 | startupHidden=0, console=0, visible=1, focus=1 |

Zachovaný původní Debug důkaz je baseline-debug-direct.log a baseline-debug-hidden.*. Nové párové logy jsou control-Debug/Release-direct.log a control-Debug/Release-sw-hide.log. Skryté okno může mít focus, takže samotné GetFocus není důkaz jeho viditelnosti. První kontrolní skript chybně očekával focus=0, jeho výstupy zůstávají v control-attempt1. Opravena byla jen tato chybná očekávaná diagnostická hodnota, nikoli produkční nebo původní testová podmínka.

Příčina nezávisí na Reset workeru, modálním potvrzení, WM_QUIT ani zavírání TrayApplication. Selhání nastává před těmito kroky. Přímý a opravený běh následně ověřují Reset No/Yes, zachování textu při chybě, pozdní generaci po close/reopen a nulový Save. Ověřen byl i WP05Windows.RandomFailureAndPartialCreateFailureRetainSafeState, 1 PASS. První ShowWindow je zvláštní startup pravidlo, nikoli obecné pravidlo nefunkčního GUI ve skrytě spuštěném procesu.

DevPreview::advance a legacy interactive již obsahovaly dodatečné ShowWindow. Proto dřívější matice scén prošla, přestože samostatný editorový filtr selhal. Tyto existující cesty nebyly upraveny a nebyla přidána další ShowWindow k zakrytí regrese.

Chování první ShowWindow potvrzuje [dokumentace Microsoftu](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-showwindow). Potlačení konzole řídí [ProcessStartInfo.CreateNoWindow](https://learn.microsoft.com/en-us/dotnet/api/system.diagnostics.processstartinfo.createnowindow?view=netframework-4.8.1).

## Oprava a normativní změny

Runner nyní vlastní System.Diagnostics.Process s UseShellExecute=false a CreateNoWindow=true. Konzole nevzniká, první zobrazení GUI nedostává SW_HIDE. Pracovní adresář, MSVC prostředí, přesná cesta kontrolovaného AMD64 EXE a bezpečné quotování argumentů zůstávají stejné. Oba přesměrované proudy se současně kopírují přímo do souborů. Runner čeká na proces i dokončení obou kopií a propaguje skutečný exit. Dispose uklidí jeho vlastní handly. Nevzniká neomezený paměťový buffer, sériové čtení dvou pipe nebo plošné ukončování procesů.

Jde o záměrnou technickou náhradu doslovného Start-Process Hidden uvedeného v návrhu F09. Zachovává jeho účel, tedy skrytou konzoli, viditelné skutečné GUI, vlastněný proces, logy a převzetí výsledku. Reprodukce prokázala, že původní mechanismus tyto požadavky pro první okno nedodržel. Historický plán zůstává beze změny.

Přesné čtyři Before/After náhrady jsou v normative-replacements.json a jejich rekonstrukce v normative-check.json:

- client.md, Izolovaný vývoj a náhled GUI: nahrazen popis Start-Process Hidden novým vlastnictvím procesu a výstupů, doplněna autoritativní regrese.
- tray_UX.md, UX14 vývojový workflow: upřesněno potlačení konzole bez ovlivnění první ShowWindow a rozsah nové regrese.
- V obou těchto částech opraven počet 21 na 20 kombinací View/Scene, přesně podle nezměněné tabulky a parseru. Historické reporty se nepřepisují.

Limity UX14/N09b, protocol.md a orchestration plán jsou bajtově shodné s převzetím. Původní editorový lifecycle gate zůstává pod 100 ms. Uživatelská dočasná výjimka pod 400 ms i odložení výkonu Communication Contracts zůstávají evidované v předchozím reportu, nebyly využity k oslabení testu.

## Autoritativní regrese runneru

Spuštění: powershell -NoProfile -ExecutionPolicy Bypass -File client/tests/ui/test-hidden-launch.ps1. Výsledný kompletní běh je v [hidden-launch/20260908-153956-284-29911926318a4f6a8ed1610489c929a2](../../work/logs/hidden-launch/20260908-153956-284-29911926318a4f6a8ed1610489c929a2), results.json má 19/19 PASS.

Vnější PowerShell je skutečně spuštěn přes Start-Process Hidden, vždy z jiné cwd. Regrese postupně provede Debug/Release v checkoutu i v čerstvé kopii client pod cestou project with spaces. V každé kombinaci ověří skutečný editorový test, startupHidden=0, console=0, visible=1, focus=1, exit 0, ukončení dítěte, následný no-op build a nulový filtr s exit 1.

Další případy odmítají neplatnou scénu, chybějící filtr, kombinaci Build/View, chybu kompilace, neplatný MZ a zamčené EXE. Negativní předspouštěcí případy kontrolují nepřítomnost ChildProcessId, tedy žádný spuštěný starý preview. Poškození zdroje/EXE a zámek se týkají výhradně jedinečné kopie, původní bajty se vracejí ve finally. Po chybě kompilace je ověřeno úspěšné obnovení buildu. Script nevstupuje do CTest a nic nemaže.

První dva vývojové běhy tohoto nového regresního skriptu zůstávají v sousedních adresářích 153453 a 153516. Zastavily se kvůli vlastním chybám PowerShell wrapperu: stavový Write-Output kontaminoval návrat summary a prázdná kolekce neměla FullName ve StrictMode. Obojí je opraveno, poslední kompletní běh prošel bez těchto výjimek. Žádný skutečný neúspěch editoru nebyl změněn na PASS.

## Další ověření

Windows 11 Pro 10.0.22631, Intel Core i9-13900HX, přibližně 32 GiB RAM, skutečné nativní DPI 144, Windows PowerShell 5.1.22621.6133. Použity existující MSVC AMD64, CMake a Ninja bez instalace balíčků. GUI testy běžely postupně, bez souběžných UI sad.

| Kontrola | Výsledek / evidence |
|---|---|
| Produkční tray/service/CLI/unit, Release UI a ASAN explicitní cíle | PASS, build-production.log, build-ui.log, build-asan.log. Produkční build byl no-op. |
| Cílené unit | 28 filtrů, 164 spuštění, 161 unikátních případů, 0 failed. unit-results.json a test-summary.json. |
| Nativní regrese | 41 případů, 0 failed: WP03Windows 8, WP03SaveWindows 4, wp04 11, wp06 9, další původní filtry 9. native-results.json. Navíc WP05 Random/create-failure 1 PASS. |
| ASAN | 35 unit a 13 native případů PASS, halt_on_error=1:abort_on_error=1. Zahrnuje editor_reset_failure_generation. Žádné sanitizer hlášení. |
| Editorový lifecycle | Release i ASAN: 24 cyklů, nejhorší původní gate 16 ms. Původní podmínka pod 100 ms beze změny. Jednorázová regrese není novým úplným benchmarkem ani zrušením historických červených běhů. |
| Deadline Save | Release/ASAN převzetí +53 ms, finishSave s nativním dialogem 31/16 ms. Původní podmínky PASS. |
| Connect countdown | Release 3375 ms, ASAN 3359 ms, uvnitř 3000 až 3500 ms, bez Disconnect. |
| Geometrie a resources | Reference-based geometrie, focus/selection/undo, malé plochy, editorový NUL/Show Info PASS. 500 paintů a 20 open/close cyklů, GDI/USER delta 0/0 v Release i ASAN. |
| Runner scény a Preview CLI | Samostatné runner-Debug/Release-scenes.log ověřují po 20 scénách, runner-Debug/Release-preview.log skutečný Preview/Connect/Connected a přirozený konec procesu. |
| Produkční PE a linkage | AMD64, NotSigned, žádné fixture/preview markery. Čerstvé diagnostické link mapy service/CLI neobsahují žádný ze tří modelových objektů, unit obsahuje všechny tři. |

Časování dlouhého preview a skutečného 30s Save deadline zůstává v úplné wp06 sadě. Legacy parser a --interactive/--preview zůstaly ve zdroji beze změny. Jejich samostatné interaktivní spuštění nebylo znovu prováděno. Nativní testy nad jedním skutečným 144DPI monitorem nenahrazují chybějící ostatní DPI, high contrast, čtečku obrazovky ani fyzické přechody mezi monitory.

## Integrita a předání

Závěrečný audit PASS: všech 338 převzatých souborů rekonstruováno přesně, právě pět vlastních změn a dva nové soubory, nový inventář má 340 položek. Beze změny zůstalo všech 160 převzatých souborů evidence, 17/17 souborů dist a hash/velikost sedmi převzatých PE. Přesné normativní náhrady mají nula neshod. Výsledky a nový inventář jsou scope-audit.json, reconstruction-check.json, inherited-evidence-check.json, pe-integrity.json, final-files.json a final-status.txt. own-changes.diff odděluje vlastní změny od kumulativního final-diff.txt. Kontroly konfliktů, produkčních preview markerů a logování mají nula nálezů, git diff --check skončil 0. Po testech nezůstal žádný proces obou nativních UI harnessů.

Produkční Release tray zůstává 842240 B, .text 493056 B, .rdata 260608 B, .rsrc 55808 B. Absolutní i procentní delta všech produkčních EXE a těchto sekcí je nula, SHA-256 jsou shodné s převzetím. Výstupy zůstávají pod work/build a work/logs. Nevznikla distribuční kopie ani změna manifestu.

Koordinátorka může zadat nový finální audit WP08 nad final-files.json tohoto předání. Není třeba pokračovat změnou produkčního editoru nebo vyřadit původní regresní filtr. Výkon Contracts a zbývající vizuální/prostředové brány zůstávají oddělené podle rozhodnutí uživatele.
