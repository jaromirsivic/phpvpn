# Report úlohy 9: D09

Stav: DONE. Datum: 12. září 2026. Rozsah: samostatná redakční konsolidace D09 podle [plánu v3](../../../Plans/2026-09-12-02-29-09/plan-v003.md), [T28](../../../Plans/2026-09-12-02-29-09/T28.md) a [rozhodnutí v4](../../../Plans/2026-09-12-02-29-09/rozhodnuti-v004.md). Společný časový limit byl předán jako neomezený. Práce proběhla přímo na větvi `tray_UX`, bez dalších agentů, worktree, přepnutí větve nebo zápisu indexu.

Při obnovení nebo předání kontextu nejprve obnov celý účinný globální blok instrukcí z reálného CODEX_HOME a jeho AGENTS.override.md nebo AGENTS.md, případně z úplného hostem dodaného bloku. Ověř obě přesné samostatné hranice před další věcnou prací. Tento report chráněný blok nenahrazuje.

## Změny

- [client.md](../../../client.md), Architektura: odstraněno historické omezení WP01. Zachováno rozdělení přenosných modelů, ContractsWindow, PasswordShaWindow a ContractsSaveController.
- Společný HTTP transport: přímé odkazy na implementované konfigurační, řídicí a Wizard meze z D02/D06/D07 nahrazují budoucí B01/B07. Maximum je 2147483 sekund, deadline requestu se neobnovuje. Čekání na HANDLE_CLOSING používá INFINITE a nemá doloženou univerzální horní mez. Připojeny konkrétní WinHTTP testy a platformní probe, bez tvrzení o novém běhu nebo úplném pokrytí DNS/send.
- Atomický zápis: vlastní publikační kontrakt a konkrétní ConfigStore/ConfigFiles testy nahrazují B01/B03/B04. Stávající ACL krok s Full Control SYSTEM/Administrators a Read & Execute Users zůstal zachován. Záměrné Users read se výslovně vztahuje také na zálohy, neznamená právo zápisu.
- Sdílená Win32 infrastruktura, Contracts a [tray_UX.md](../../../tray_UX.md) UX02: pracovní autorství nahrazeno existujícími komponentami. Model připravuje data, controller a nativní okno zajišťují IPC a lifecycle ukládání. Již dodané Windows části se znovu neplánují.
- Testovací oddíly a UX00/UX14: aktuální kontrakt oddělen od datovaných výsledků. Historický audit je označen datem jeho uložení do Git, 8. září 2026, nikoli jako nový PASS. Skutečné názvy filtrů, targetů a capture proměnných s WP zůstaly zachované. Vizuální tolerance i runtime meze zůstávají v UX14.
- Izolovaný vývoj GUI: zachovány parametry, postup, oddělené Debug a skutečné Release cache, UI README a provozní omezení. Odstraněna závislost na F09/A32.
- Doporučené pořadí další práce: stávající Windows implementace oddělena od budoucích portů, nedodaného pre-poll backpressure a odloženého R04. Zachováno upozornění na nekompatibilitu agregovaného payloadu se starým formátem.
- [Matice](../../../Plans/2026-09-12-02-29-09/priskok-matice.md): změněn pouze řádek T28. TT19 se tím neuzavírá.

## Podklady a statické důkazy

Zdrojová vazba D09 je S03 §11, S06 N01–N09, S08 bod 6, S09 WP07 a intent-v001. T07 vymezuje omezení WinHTTP callbacku, T12 publikační kontrakt a testy, T08 odložené R04. Historické reporty byly použity pro původ a meze evidence, nikoli jako zdroj nových technických požadavků.

- [transport_winhttp.cpp](../../../client/src/platform/windows/transport_winhttp.cpp): WINHTTP_FLAG_ASYNC, závěrečný callback a WaitForSingleObject s INFINITE.
- [test_winhttp_accounting.cpp](../../../client/tests/unit/test_winhttp_accounting.cpp): WinHttpDeadline.HeadersTlsAndTrickleBodyShareOneAbsoluteBudget, BoundarySettingsNeverBecomeInfinite a WinHttpLifecycle.ConcurrentCancellationThenReuse, dále účetnictví částečných a nadlimitních těl.
- [platform_plan3_probe.cpp](../../../client/tests/platform_plan3_probe.cpp): kontrola closing callbacku a měření rušení.
- [config_files_win.cpp](../../../client/src/platform/windows/config_files_win.cpp): kanonická ACL obsahuje FRFX pro BU. [ConfigStore testy](../../../client/tests/unit/test_config_store.cpp) a [ConfigFiles testy](../../../client/tests/unit/test_config_files_win.cpp) poskytují stávající storage pokrytí, včetně výslovného NOT RUN při nevhodném tokenu.
- [Historický audit](../../../client/tests/implementation-plan3-fix3-wp07.md) odděluje převzaté výsledky, syntetické proxy a nedokončenou plnou akceptaci. Datum uložení ověřeno přes `git log -1 --format=%aI -- client/tests/implementation-plan3-fix3-wp07.md`.

## Ověření a testovací rozhodnutí

D09 nemění kód, testy ani runtime kontrakt novou funkcí. Nový unit test kopírující dokumentaci by neověřoval chování produktu. Podle T28 a V06 proto stačí textová kontrola, dohledatelnost, odkazy a kontrola diffu. Build, unit/UI, skutečné Windows I/O, služba, VPN, instalace, runtime a vizuální brány jsou v tomto bloku **NOT RUN**. Žádný dřívější výsledek D01–D08 se nevydává za nový běh D09.

Před zápisem byl pořízen SHA256 snímek všech 447 sledovaných a neignorovaných nesledovaných souborů. Uložen je mimo projekt v dočasném adresáři `phpvpn-d09-baseline-01a09464`. Porovnání po změně potvrdilo pouze tři změněné původní soubory, client.md, tray_UX.md a matici. Tento report je jediný nový projektový soubor D09. Kód, testy, protocol.md, všech šestnáct historických zdrojů a předchozí pracovní artefakty mají shodný obsah s výchozím snímkem. Existující nekomitované změny D01–D08 jsou zachované.

Textová kontrola potvrdila nulový výskyt samostatných historických značek WP/N/F/B/A32 v obou cílových dokumentech. Skutečné filtry WP03Models, WP03Windows, WP05Windows, wp04_, wp06_, capture proměnné a editor_reset_failure_generation zůstaly zachované. Runtime tabulka UX14 a navazující metodika až k izolovanému runneru jsou obsahově shodné s výchozím stavem. Tolerance 6 DIP a 8 DIP, 100/125/150/200 % DPI, zahřívací běh a nejméně tři měřené běhy, 500 překreslení a 20 cyklů zůstaly zachované.

Finální kontroly:

- `git diff --check` a opakování s `git -c core.safecrlf=false diff --check`: exit 0. Běžný příkaz pouze upozornil na budoucí konverzi LF/CRLF v pracovním checkoutu, bez whitespace chyby.
- Kontrola 68 lokálních Markdown cílů ve čtyřech souborech, z toho 23 nových unikátních cílů počítaných po dokumentech: nula chyb. Ověřena existence souborů i odpovídající Markdown kotvy. Žádný nový externí URL odkaz nevznikl.
- Obsahové SHA256 porovnání: ze 447 výchozích souborů změněny pouze tři povolené dokumenty, jediným novým souborem je tento report. Ostatních 444 souborů zachováno.
- Textová kontrola: PASS pro číselné meze UX14, přesnou runtime tabulku a metodiku, skutečné historické testové identifikátory, úplný provozní postup GUI runneru a validní UTF-8. První pomocná kontrola hledala nepřesně zkrácený text Debug/Release a skončila assertion chybou. Po opravě kontrolního výrazu podle existujícího `CMAKE_BUILD_TYPE=Release` prošla opakovaná kontrola, dokument kvůli tomu změněn nebyl.

## Hranice předání

D09 neprovádí D10 ani další kroky. R04 zůstává odložené bez realizace a nových testů, backpressure zůstává nedodané. Věcné opravy dirty podle R09 a Statistics podle R10 patří pozdějším D16/D17, jejich dřívější text nebyl v tomto redakčním bloku měněn. Neproběhl commit, push, reset, checkout, mazání ani zásah do produkčního prostředí.
