# Report úlohy 8: D08 a TT20

Stav: D08 a TT20 dokončeny. Závěrečný cílený běh má 1 test, 0 selhání. Produkční chyba nebyla prokázána a produkční kód se neměnil. D09 nebylo zahájeno. Společný časový limit je výslovně neomezený.

Při obnovení nebo předání kontextu obnov celý účinný globální blok instrukcí z reálného CODEX_HOME a jeho AGENTS.override.md nebo AGENTS.md, případně z úplného hostem dodaného bloku. Před věcnou prací ověř obě přesné samostatné hranice. Tento report chráněný blok nenahrazuje.

## Dokumentace a důkazy

Rozsah určují D00/D08/TT20 v [plan-v003.md](../../../Plans/2026-09-12-02-29-09/plan-v003.md), [T29.md](../../../Plans/2026-09-12-02-29-09/T29.md) a [rozhodnuti-v004.md](../../../Plans/2026-09-12-02-29-09/rozhodnuti-v004.md).

- [client.md, Logování a diagnostika](../../../client.md#logování-a-diagnostika) nově popisuje aktivní phpvpn-service.log pod logs v datovém adresáři služby odvozeném ze systémového ProgramData. Práh je 8 MiB včetně CRLF, rotace před celým řádkem při překročení, nikoli rovnosti, a nejvýše pět historií phpvpn-service.1.log až phpvpn-service.5.log.
- Dokumentace rozlišuje rotační politiku od záruky diskového I/O a od meze libovolně dlouhé zprávy. Logger text nezkracuje a není automatický filtr tajemství. Bezpečný obsah musí zajistit volající. Flush streamu není garance přežití výpadku napájení.
- Z předchozího bezpečnostního odstavce odstraněna pouze nedoložená část požadující viditelnost trustAnyTlsCertificate ve startovním logu. Konfigurační varování a popis přijatého rizika zůstaly. Event Log ani zvláštní startup warning nejsou dokumentovány jako dodané.
- [logger.cpp](../../../client/src/core/base/logger.cpp), configureFile, write a rotateLocked, dokládá mutex, formátovaný řádek, podmínku currentBytes + line.size + 2 > maxBytes a rotaci. [logger.h](../../../client/src/core/base/logger.h) poskytuje defaulty 8 MiB/5 a výslovnou odpovědnost call sites za bezpečný text.
- [service_host.cpp](../../../client/apps/service/service_host.cpp), ServiceRuntime::start, konfiguruje soubor po bezpečné přípravě storage. runInConsole používá sekundární sink pro stdout. [paths_win.cpp](../../../client/src/platform/windows/paths_win.cpp), dataDirectory a logDirectory, používá FOLDERID_ProgramData, PHPVPN a logs.
- [protocol.md](../../../protocol.md) a [tray_UX.md](../../../tray_UX.md) zůstaly beze změny. Místní disková rotace nemění wire ani UI smlouvu. V [priskok-matice.md](../../../Plans/2026-09-12-02-29-09/priskok-matice.md) se změnil pouze řádek T29.

## TT20 a rozsah důkazu

Před úpravou hledání Logger, setFile, rotate a log rotation v client/tests nenašlo samostatný logger test. Existující CMake registrace jej neobsahovala. Nový [test_logger.cpp](../../../client/tests/unit/test_logger.cpp) běží s existujícím harness.cpp v samostatném targetu phpvpn-logger-tests, registrovaném přes add_test v [CMakeLists.txt](../../../client/tests/CMakeLists.txt). Samostatný proces dovoluje jedinou konfiguraci globálního singletonu bez ovlivnění ostatních suites. CTest má timeout 30 sekund.

Jeden integrovaný test ověřuje:

1. Součet 64 + 961 bajtů vyvolá rotaci před druhým celým řádkem při prahu 1024 bajtů. Historie obsahuje přesně první řádek a aktivní soubor přesně druhý.
2. Dalších 63 bajtů naplní aktivní soubor přesně na 1024, bez nové rotace. Porovnává se úplný obsah včetně CRLF.
3. Jediný 1025bajtový řádek se po rotaci zapíše celý. To dokládá dokumentované omezení prahu.
4. Sedm dalších rotací uchová přesný obsah a pořadí posledních pěti historií. V adresáři je právě šest souborů včetně aktivního, žádná šestá historie.
5. Čtyři vlákna odstartovaná přes barrier zapíšou po čtyřech jedinečných řádcích. Výsledných šestnáct 64bajtových záznamů tvoří přesně 1024 bajtů. Test ověřuje CRLF, délky, úplnou sadu zpráv bez ztráty či duplikace a shodu se sekundárním sinkem.
6. Sekundární sink dostává úroveň a celé formátované řádky bez CRLF. Porovnání respektuje možné jiné pořadí callbacků. Callback také čte logger.level(), takže kontroluje volání mimo mutex loggeru. Debug zpráva při úrovni info není předána sinku.

Test používá injektovaný práh 1 KiB a skutečný výchozí počet pěti historií. Produkční práh 8 MiB je doložen staticky, nikoli generováním megabajtů dat. Fixture vzniká výhradně přes temp_directory_path v novém adresáři phpvpn-tt20-<náhodné číslo>. Kolize adresáře test ukončí bez použití existujícího obsahu. Malé fixture zůstávají k inspekci, agentka je nemaže. Rotace přirozeně odstraňuje vlastní nejstarší testovací historii.

## Spuštěné kontroly

Windows, AMD64, 12. 9. 2026, C:\git\phpvpn. Debug build, prostředí MSVC z Visual Studio 2022 BuildTools, VsDevCmd.bat -no_logo -arch=x64 -host_arch=x64. První pokus o obalení příkazu v cmd selhal na uvozovkách před spuštěním buildu. Import prostředí přes stávající vzor projektu problém vyřešil bez změny zdrojů.

| Příkaz | Výsledek |
|---|---|
| cmake --build work/build/debug --target phpvpn-logger-tests | exit 0, kompilace a link finálního testu |
| ctest --test-dir work/build/debug -R '^phpvpn-logger-tests$' --no-tests=error -V | exit 0, 1 CTest target, 1 test harnessu, 0 selhání |
| git diff --check | exit 0 |
| Kontrola nových lokálních Markdown odkazů | všechny cíle existují, nadpis Logování a diagnostika nalezen |

Finální běh vytvořil fixture C:\Users\User\AppData\Local\Temp\phpvpn-tt20-969230359. Dřívější úspěšný běh před doplněním součtových hran měl samostatnou fixture phpvpn-tt20-1998130679 a není započten jako další odlišný test. Podrobný poslední CTest výstup je v [LastTest.log](../../../work/build/debug/Testing/Temporary/LastTest.log), tento standardní soubor může příští běh přepsat.

## Rozsah, NOT RUN a rizika

- Checkout zůstal tray_UX, HEAD da788b71feac031d5572b906e5d3ac51d4e87ae4. Předchozí nekomitované změny D01–D07 zachovány. Blok upravuje client.md, jediný řádek matice, testovací CMake, nový test a tento report.
- SHA-256 protocol.md, tray_UX.md, rozpracovaných test_config.cpp a test_runtime_status.cpp a progress.md se proti začátku tohoto bloku nezměnily. Produkční logger, host a paths nejsou v diffu. Původní plány a notes nebyly upraveny.
- NOT RUN: plná CTest sada, Release/ASAN, disk-full/permission/rename failure injection, pád procesu či výpadek napájení, libovolné délky zpráv, více procesů zapisujících stejný log a dlouhodobý stress. Jediný souběžný běh není důkaz všech možných interleavingů.
- NOT RUN: skutečná služba, ProgramData, Event Log, explicitní startup warning, SCM, Wintun/VPN, instalace, reálný relay, GUI a výkonová/vizuální brána. Test neprokazuje bezpečnost všech log call sites ani odolnost diskového I/O vůči selhání.
- B01–B05 zůstávají plánované a R04 odložené. D09 ani další bloky neprovedeny. Bez commitu, push, resetu, checkoutu, worktree nebo subagentů.
