# Závěrečná validace V01–V06

Dokumentační větev D01–D17 je zkontrolována v rozsahu [plánu v3](../../../Plans/2026-09-12-02-29-09/plan-v003.md). V01–V06 mají PASS v tomto vymezeném rozsahu, se zachovaným odloženým R04. Cílená testová sada má **54 testů, 50 PASS a 4 FAIL**. Nejde o PASS celé testovací větve ani o akceptaci vydání. B01–B05 zůstávají **plánované, neimplementované**.

Datum 12. 9. 2026, Windows x64, přímý checkout `C:\git\phpvpn`, větev `tray_UX`, HEAD `1c25096111cdf20f901c58da6299ddde7450e229`. Společný časový limit byl předán jako výslovně neomezený. Nebyl vytvořen další kontext ani worktree, neproběhl commit, push, reset, checkout nebo ruční mazání. Projektový AGENTS.md chybí. Úplný účinný globální AGENTS.md z vyhodnoceného Codex home byl načten a obě samostatné hranice ověřeny.

Při obnovení nebo předání kontextu nejprve obnov celý účinný globální blok instrukcí z reálného CODEX_HOME a jeho AGENTS.override.md nebo AGENTS.md, případně z úplného hostem dodaného bloku. Ověř obě přesné samostatné hranice před další věcnou prací. Tento report chráněný blok nenahrazuje.

## Výsledky V01–V06

| Kontrola | Výsledek a přesná mez důkazu |
|---|---|
| V01 Rozsah | PASS pro vlastní změny a zachování zdrojů. Vstupní checkout již obsahoval dokumenty, testy a progress předchozích bloků. Porovnání proti vstupním 457 SHA-256 dovoluje pouze client.md, tray_UX.md a matici, nový je tento report. Produkční src/apps a protocol.md nemají diff ani vůči plánovému HEAD 529d1c4. Všech 16 zdrojů a protocol.md mají původní SHA-256 z D00. Celkový git status tedy záměrně není čistě dokumentační, cizí testové změny nejsou vydávány za změny V01–V06. |
| V02 Dohledatelnost | PASS. Načten plán, matice, rozhodnutí a reporty 1–17. Vazby D → T → chronologický zdroj → implementační soubor/symbol → cílový nadpis jsou níže. Novější R01–R03/R05–R10 mají přednost před historickými zjištěními. Historie a neimplementované výsledky zůstaly zachované. |
| V03 Odkazy a názvy | PASS po dokončení zápisu. Kontrola všech lokálních Markdown cílů v trojici dokumentů, matici a reportech 1–18 zahrnuje soubory, skutečnou velikost písmen a kotvy nadpisů. `tray_UX.md`, EXE `phpvpn.exe`, CMake target `phpvpn-cli` a existující helpery jsou rozlišeny. Neexistující příklady config_text.cpp/wizard_document.cpp ani phpvpn-cli.exe v cílech nejsou. |
| V04 Terminologie a čísla | PASS. B01/B03/B04/B07, WP01–WP07, F09/A32 a „před implementací“ zkontrolovány významově. B označuje výslovně budoucí backpressure, WP zůstává v reálných testových názvech a capture proměnných. „Horní strop“ správně rozlišuje int64, vztah NDJSON/body a timeout 2147483. Hodnoty maxDataRecordsPerRequest, 128 a 600000 odpovídají důkazům. |
| V05 Konzistence | PASS v rozsahu D s níže opraveným automatickým předvyplněním Contracts. /24–/29 a 252/4 peerů nejsou zaměněny za Wizard 13/14. 512 KiB dokumentu není 4 MiB zakódované IPC. Jeden peer klíč pro oba směry nemění obecnou uspořádanou wire dvojici ani AAD. Devět čítačů dává deset UI hodnot. Wizard receipt je historická publikace, shoda DOM po set-config jen současný stav. R04 a širší nativní Password UX zůstávají mimo potvrzení. |
| V06 Formát | PASS. `git diff --check` bez whitespace chyb. Lokální odkazy, UTF-8 a počty buněk Markdown tabulek ověřeny pomocným skriptem. Žádný nový unit test zrcadlící dokumentaci nevznikl. |

## Minimální opravy tohoto bloku

V [client.md](../../../client.md) byly upraveny Architektura a Přenosné jádro Communication contracts. V [tray_UX.md](../../../tray_UX.md) jde o UX02, UX10 a UX11. Osm přesných textových náhrad odstranilo tvrzení, že otevření Contracts automaticky odvozuje chybějící klíče a že model kvůli tomu používá defaultPayloadKey. Chybějící řádek má prázdný Key a Enabled false. Jeho zapnutí bez platného klíče blokuje uložení. Vzorec deterministického klíče zůstává správný pro Wizard, stejný výsledek dává explicitní akce Default v Password SHA. Původní řádkové konce byly zachovány.

Důkaz: [contracts_model.cpp](../../../client/src/core/presentation/contracts_model.cpp), `ContractsModel::create`, `blockingErrors`, `setEnabled` a `prepareSave`. Větev existingKey jako jediná načítá počáteční key buffer, chybějící zůstává prázdný. Create nevolá crypto. [test_contracts_model.cpp](../../../client/tests/unit/test_contracts_model.cpp) přímo ověřuje prázdné unchecked řádky, nulové hashCalls, přesné zachování existujícího klíče, blokující zapnutý řádek a výsledkové dirty. V [password_sha_window.cpp](../../../client/apps/tray/password_sha_window.cpp) obsluha defaultId vloží `target_.contractId()` a zavolá update. Nativní stisk tlačítka nebyl znovu testován.

Nejde o návrat k historickému UX ani o implementaci funkce. Rozpor již identifikoval report D16 a byl potvrzen v aktuálním zdroji a testech. V matici je opraven také zastaralý úvod plánovací fáze, který dosud tvrdil, že se cílové dokumenty a testy nemění. Předchozí reporty ani progress koordinátora se nepřepisují.

## Dohledatelnost dokumentačních kroků

Zdrojové sekce S01–S16 stanoví u každého D kroku neměnný plán a navázané T záznamy. Tabulka shrnuje kontrolované cíle a symboly, podrobné zdrojové přiřazení a původní běhy jsou v jednotlivých reportech.

| Krok / témata | Cílová část a implementační důkaz | Evidence |
|---|---|---|
| D01 / T01 | Architektura, Sestavení klienta. CMakeLists.txt, CMakePresets.json a apps OUTPUT_NAME. | [D01](task-1-report.md) |
| D02 / T02 | Konfigurační soubor. config_validator.cpp::validateConfigValues a kMaxHttpTimeoutSeconds. | [D02](task-2-report.md) |
| D03 / T03 | Konfigurační soubor, payloadKey. NetworkWorker buildDataBody/ingestRecord a payload_batch.cpp. | [D03](task-3-report.md) |
| D04 / T05 | Velikostní a spravedlivé plánování. scheduler.cpp::planNextRequest a info limity. | [D04](task-4-report.md) |
| D05 / T06 | Windows Service, konzole a tray, IPC connect. VpnEngine start/runConnectSequence/shutdown. | [D05](task-5-report.md) |
| D06 / T10 | Implementační meze lokálního řízení. ipc_pipe.cpp konstanty/submit a service_host.cpp klasifikace. | [D06](task-6-report.md) |
| D07 / T16–T17 | Životnost a limity Wizard IPC. WizardModel::beginCheck, WizardReader, WizardService closeLocked/expireLocked/finishTask. | [D07](task-7-report.md) |
| D08 / T29 | Logování a diagnostika. Logger::rotateLocked/write, ServiceRuntime::start a logDirectory. | [D08](task-8-report.md) |
| D09 / T28 | Architektura, HTTP transport, atomický zápis, sdílené UI, testy, izolovaný GUI návod a další práce, UX02/UX14. Skutečné komponenty a vlastní kontrakty nahradily pracovní označení. | [D09](task-9-report.md) |
| D10 / T14 | Testovací profily serveru. deploy-test.ps1 Profile/Replace-ExactlyOnce. | [D10](task-10-report.md) |
| D11 / T04, R01 | Konfigurace času, Synchronizace času a její testy. runNetworkThread/performHealth/TimeSync. | [D11](task-11-report.md) |
| D12 / T05, R02 | Pollingový interval, Příjem dat a testy front. nextPollDelayMs, canAdmitAnotherPacket bez runtime call site, tryEnqueue. | [D12](task-12-report.md) |
| D13 / T06–T07, R03 | Účel, Běhový model a stavový automat. VpnEngine::teardown/backoffDelayMs a WinHTTP HANDLE_CLOSING. | [D13](task-13-report.md) |
| D14 / T09, R05 | Distribuce/instalace, upgrade/rollback a bod 8 Windows testů. install.ps1, uninstall.ps1 a make-dist.ps1. | [D14](task-14-report.md) |
| D15 / T12–T13/T15/T17–T18/T20–T21/T23/T25, R06–R07 | ACL/startup a zachované současné UX. paths_win.cpp, ConfigStore, ServiceRuntime a nativní okna. | [D15](task-15-report.md) |
| D16 / T19/T24–T25, R08–R09 | Sdílená Win32 infrastruktura, Contracts, UX02/UX11/UX12. UiResources::update a ContractRow::dirty. Výše upřesněn prázdný počáteční Key. | [D16](task-16-report.md) |
| D17 / T22, R10 | Statistics, test 34, UX00/UX07/UX14. statisticsFields, StatisticsModel a přesné celočíselné formátování. | [D17](task-17-report.md) |

## Čerstvé cílené testy

Přečtený existující `work/d16-build.cmd` načte VsDevCmd pro x64 a spouští `cmake --build work/build/debug --target phpvpn-client-tests`. Běh `cmd /c work\d16-build.cmd` skončil exit 0, `ninja: no work to do.`. Testovací EXE je aktuální vůči této Debug konfiguraci, nevznikl nový Release build.

Každý filtr byl samostatným argumentem `work/build/debug/tests/phpvpn-client-tests.exe`. Logy jsou v `work/task-18-<filtr bez koncové tečky>.log`. Filtry jsou nenulové a vzájemně se nepřekrývají.

| Filtr | Počet | PASS | FAIL | Exit |
|---|---:|---:|---:|---:|
| ContractsModel. | 10 | 10 | 0 | 0 |
| ContractsDocument. | 6 | 2 | 4 | 1 |
| PasswordKey. | 4 | 4 | 0 | 0 |
| WP03Models. | 8 | 8 | 0 | 0 |
| config.runtime_boundaries_in_full_and_every_repair_mode | 1 | 1 | 0 | 0 |
| queues. | 7 | 7 | 0 | 0 |
| scheduler. | 7 | 7 | 0 | 0 |
| runtime.worker_ | 4 | 4 | 0 | 0 |
| StatusContract. | 2 | 2 | 0 | 0 |
| TrayResources. | 5 | 5 | 0 | 0 |
| Celkem | 54 | 50 | 4 | |

Čtyři opakovaně potvrzená selhání ContractsDocument:

- `absent_present_empty_and_unchecked_removal`, řádek 26, očekávaný úspěch končí config_invalid_key.
- `preserves_all_unrelated_values_types_and_optional_presence`, řádek 70, stejný config_invalid_key.
- `complete_snapshot_rejects_omissions_duplicates_foreign_self_and_bad_keys`, řádek 103, nenastane očekávaný config_too_large.
- `result_document_limit_and_actual_encoded_envelope`, řádek 141, místo config_too_large nastane config_invalid_key.

Příčina odpovídá D16: staré fixture spoléhají na vyplněný chybějící klíč. Produkční ContractsModel jej už při vstupu ponechával prázdný. `test_contracts_document.cpp`, produkční model a contracts_document.cpp nebyly tímto blokem změněny. Testové fixture se v čisté závěrečné validaci neopravují. Tato selhání blokují tvrzení o plně zelené TT17 nebo celé sadě, nikoli pravdivou dokumentační konsolidaci.

## NOT RUN a zachované hranice

- Fyzické OS/elevated scénáře: skutečný startup → default → editor Cancel → druhý start, chráněné NTFS ConfigFiles scénáře, SCM, služba, Wintun/session/routy, instalace, upgrade, uninstall/RemoveData, restart a skutečné sleep/hibernate/resume. V tomto bloku NOT RUN. D15 eviduje 20 ConfigFiles scénářů přeskočených kvůli tokenu, jejich harness OK není PASS.
- Nativní UI Finish: D07 eviduje přerušený `wp04_production_finish` kvůli ovládání TaskDialog a nenastartovaný lost-finish. Nový běh obou v tomto bloku NOT RUN. Nejde o prokázanou produkční chybu Finish.
- Fyzické HC, systémové barvy/DPI, multi-monitor/unplug, čtečka, klávesnicová akceptace, přepínač aplikací, systémový Exit, všech 11 vizuálních mockupů a úplná UX14 Release výkonová brána: NOT RUN. TrayResources používá řízené vstupy, modelové PASS je nenahrazuje.
- Skutečný PHP relay včetně všech `.2–.15`, HTTPS/proxy, izolovaná DNS/send cancellation a řízený závod cancel versus success: NOT RUN. Převzaté reporty zůstávají pouze datovanou evidencí.
- Celý CTest, ASAN, nový Release/ARM64 build, linkerové mapy a distribuční akceptace: NOT RUN.
- R04 zůstává odložené bez realizace a nových testů. Starší požadavky Wintun integrity, journalu a cleanup včetně věty požadující ukončení všech tří vláken při Disconnect zůstávají nedotčené. Nejsou potvrzením aktuální implementace. Aktuální engine lifecycle stanoví D05/D13. Tento záměrně ponechaný historický rozpor nelze vydávat za globální shodu celé specifikace.
- B01–B05 nejsou implementované ani testované jako dodané. `rg -n canAdmitAnotherPacket client/src` vrací pouze deklaraci a definici. Pre-poll gate a oboustranný clamp extrémů chybí, runtime per-packet admission je dodaná. B04 a souběžné deferred/error rozšíření zůstávají NOT RUN.
- UI README s původním „Nine exact uint64 counters“ zůstává navazující drobností mimo cílové soubory. Širší nativní Password UX, například vazba dostupnosti OK na enabled cílového řádku a doplňující akce, nebylo tímto blokem konsolidováno ani akceptováno. Oprava výše se týká jen doloženého chybného tvrzení o automatickém předvyplnění.

## Reprodukovatelnost a rozsah zápisu

`work/task-18-baseline.json` zachycuje 457 vstupních souborů, malé `work/task-18-before-*.md` zachycují tři upravované dokumenty. `work/task-18-check-docs.py` kontroluje lokální odkazy, kotvy, velikost písmen, tabulky a změněné hashe. Aktuální součty jsou v `work/task-18-doc-check.json`. D00 SHA-256 je převzat z tabulky reportu 1 a skutečně porovnán s aktuálními bajty, všech 17 chráněných položek se shoduje. Nové externí odkazy nevznikly, webová dostupnost historických externích URL nebyla kontrolována.

Finální běh `uv run --python 3.13 work/task-18-check-docs.py` ověřil 22 dokumentů, 322 lokálních odkazů, 29 fragmentů a 43 tabulek, nula chyb. `git -c core.safecrlf=false diff --check` skončil exit 0. Tři přesné ASCII SHA-256 vektory klientského dokumentu nezávisle přepočítané hashlib jsou shodné. Součet deseti uložených testových logů znovu potvrzuje 54 provedených testů a čtyři selhání. Diff client.md proti vstupu má změny na řádcích 28 a 728–730, tray_UX.md na řádcích 77, 277, 289, 293 a 299. Porovnání s plánovým HEAD pro client/src, client/apps, client/tools, tools a protocol.md je prázdné.

Pomocné přípravné příkazy měly opravené chyby prostředí, nikoli produktová selhání: alias python nebyl dostupný, použit uv s Python 3.13. Přenos českých literálů přes výchozí PowerShell pipeline porušil kontrolní řetězec, assertion zastavila zápis před první změnou. UTF-8 skript v souboru provedl následné přesné náhrady. Jedna rg cesta s wildcardem byla nahrazena dotazem nad skutečným adresářem. Průběžná kontrola také zachytila odkaz na právě vznikající report před jeho vytvořením, po dokončení zápisu se kontrola opakuje. Žádná z těchto přípravných chyb není testový PASS.

Vlastní výstup tvoří client.md, tray_UX.md, matice a tento report, pomocná evidence je v ignorovaném work. Zbývajících 454 vstupních souborů zůstává obsahově shodných. Další práce na testových selháních nebo nativních scénářích má vlastní rozsah. Dokumentační validace nezahajuje B větev a neuzavírá R04.
