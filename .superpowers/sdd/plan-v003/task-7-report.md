# Report úlohy 7: D07 a TT13

Stav: D07 a podmíněné doplnění TT13 dokončeny. Cílené testy prošly. Nový běh existující akceptace Finish se nedokončil kvůli testovacímu ovládání TaskDialog, není PASS. Produkční chyba nebyla prokázána a produkční kód se neměnil. D08 nebylo zahájeno. Společný časový limit je uživatelem schválený neomezený.

Při obnovení nebo předání kontextu obnov celý účinný globální blok instrukcí z reálného CODEX_HOME a jeho AGENTS.override.md nebo AGENTS.md, případně z úplného hostem dodaného bloku. Před věcnou prací ověř obě přesné samostatné hranice. Tento report chráněný blok nenahrazuje.

## Dokumentace a důkazy

Rozsah určují D00/D07/TT13 v [plan-v003.md](../../../Plans/2026-09-12-02-29-09/plan-v003.md), [T16.md](../../../Plans/2026-09-12-02-29-09/T16.md), [T17.md](../../../Plans/2026-09-12-02-29-09/T17.md) a R07 v [rozhodnuti-v004.md](../../../Plans/2026-09-12-02-29-09/rozhodnuti-v004.md).

- [client.md, Životnost a limity Wizard IPC](../../../client.md#životnost-a-limity-wizard-ipc): celkový rozpočet URL/secret/access je 1/2/3 × responseTimeoutSeconds × 1000 + 15000 ms, s defaultem 45/75/105 s. IPC start má 5000 ms uvnitř tohoto rozpočtu. Validace timeoutu a 64bitový výpočet chrání násobení, přičtení k jedinému nezápornému monotónnímu počátku má explicitní kontrolu overflow. Polling a keepalive termín neobnovují.
- Tentýž oddíl uvádí nejvýše 128 receiptů včetně předrezervovaných commitů. Živá relace je uchovává. Terminální záznam smí zmizet nejdříve po 600000 ms od uzavření relace. Když commit doběhne až po uzavření, finishTask nastaví počátek retence na jeho dokončení. Pending commit a neterminální operace se samotným časem neodstraňují.
- Zachována předrezervace před zápisem, odmítnutí plného registru, idempotence, neměnný historický význam potvrzení a zákaz automatického retry. Finish nadále neotevírá Contracts ani nepřipojuje VPN.
- Odkazy na budoucí B01 pro tyto konkrétní hodnoty ve společném HTTP odstavci a povinném testu 13 nyní vedou na vlastní nadpis. Ostatní B01 odkazy ke storage, měření a cancellation patří pozdější redakční konsolidaci D09. Nová implementační větev B01–B05 zůstává oddělená.
- Číselné důkazy: [wizard_model.cpp](../../../client/src/core/wizard/wizard_model.cpp), beginCheck. [wizard_reader.cpp](../../../client/apps/tray/wizard_reader.cpp), request. [wizard_types.h](../../../client/src/core/wizard/wizard_types.h), kWizardMaxReceipts a kWizardReceiptRetentionMs. [wizard_service.cpp](../../../client/src/core/wizard/wizard_service.cpp), closeLocked, expireLocked, finish a finishTask.
- [protocol.md](../../../protocol.md), §9.5, ponechán. Lokální limity nepatří do wire protokolu. Diagnostická výjimka, jediný empty data request, zákaz retry a riziko nevratného odebrání zpráv zůstávají shodné s [tray_UX.md](../../../tray_UX.md), UX08/UX09.
- [priskok-matice.md](../../../Plans/2026-09-12-02-29-09/priskok-matice.md): upraveny pouze stavy T16 a T17.

## TT13: mezera a doplnění

Existující nativní late_start_ack v [tray_ui_tests.cpp](../../../client/tests/ui/tray_ui_tests.cpp) používá Backend, který cancel_attempt pouze potvrdí. Službový cancelled_attempt_prevents_delayed_start_and_busy_sequence_never_replays sám neověřuje, že skutečné UI po ztrátě operationId pošle správný cancel. Chyběla jejich společná vazba.

Do [test_runtime_status.cpp](../../../client/tests/unit/test_runtime_status.cpp) přibyl jediný test `wizard_service.native_lost_start_reply_cancels_attempt_before_delayed_start_without_http`. Používá produkční WizardWindow a WizardReader, skutečnou privátní UUID Windows pipe, striktní IPC parser a produkční WizardService s existujícími fake filesystem/clock/transport závislostmi.

Obě varianty ztratí start reply před získáním operationId v UI. První odloží samotné doručení startu službě. Druhá jej přijme a ponechá diagnostiku ve frontě. Skutečné UI odešle právě jeden cancel-attempt se shodným serviceInstanceId, sessionId, clientAttemptId a attemptSequence a bez operationId. Po doběhnutí případné zrušené úlohy opožděný start vrátí wizard_attempt_closed. Počet HTTP je 0 a zápisů 0. UI zůstane na URL. Teprve další explicitní stisk primární akce provede jeden nový HTTP request a přejde na secret. Tato pozitivní kontrola ověřuje také funkčnost diagnostické cesty a počitadla.

Test zachytí odstranění UI cancel-attempt, chybnou korelaci, přijetí starého startu i spuštění zrušené diagnostiky. Nejde o test skutečného PHP relay, TCP ztráty nebo všech závodů při už běžícím HTTP. Existující probe testy samostatně ověřují single data, no401retry a zahazování payloadu bez GCM.

## Spuštěné kontroly

Windows 11 Pro build 22631, AMD64, 12. 9. 2026. Pracovní adresář C:\git\phpvpn. Pro build načteno prostředí `VsDevCmd.bat -arch=x64 -host_arch=x64` z instalovaného Visual Studio 2022 BuildTools. První prosté volání CMake bez tohoto prostředí selhalo na chybějícím standardním headeru functional. Po načtení prostředí kompilace i link uspěly, bez změny zdrojů nebo závislostí kvůli této chybě.

| Příkaz | Výsledek |
|---|---|
| `cmake --build work/build/debug --target phpvpn-client-tests` ve VS prostředí | exit 0 |
| `work/build/debug/tests/phpvpn-client-tests.exe native_lost_start_reply` | 1 test, 0 selhání, dvě varianty |
| `work/build/debug/tests/phpvpn-client-tests.exe wizard_service.` | 14 testů, 0 selhání, zahrnuje nový test |
| `work/build/debug/tests/phpvpn-client-tests.exe WizardModel.` | 4 testy, 0 selhání |
| `work/build/debug/tests/phpvpn-client-tests.exe probe.` | 6 testů, 0 selhání, substring zahrnuje 4 probe a 2 config_files_probe |
| `work/build/debug/tests/phpvpn-client-tests.exe wizard_ipc.` | 11 testů, 0 selhání |
| `work/build/debug/tests/phpvpn-client-tests.exe WP04Lifecycle.` | 3 testy, 0 selhání |
| `./client/tools/dev-tray.ps1 -Mode Test -Filter wp04_production` | UI build Debug uspěl, test přerušen, viz níže |

Celkem **38 různých dokončených testů, 0 selhání**. Samostatný první běh nového testu není započítán podruhé. Retenční predikát a přesná 600000ms hranice jsou v tomto bloku doloženy staticky. Stávající test pojmenovaný receipt_capacity_rejects_before_write_and_retains_closed_session_results přímo testuje naplnění 128 slotů a odmítnutí dalšího zápisu, nikoli celou časovou hranici. Jiné službové testy ověřují dostupnost potvrzení po close/expiraci a idempotenci unknown. Žádná další retenční suite nevznikla mimo schválené podmíněné doplnění TT13.

## Finish: převzatá evidence a nedokončený nový běh

Stávající `wizardProductionFinish(bool lost)` obsahuje assertions 1 Finish, 1 close, 0 config reads, 0 Connect, 0 Contracts a getCommit 0 nebo 1 podle ztráty odpovědi. Historické výsledky A12/A13 jsou v [implementation-plan3-fix3-wp08-user-review.md](../../../client/tests/implementation-plan3-fix3-wp08-user-review.md). Tyto počty jsou převzaty, nejde o nové PASS v tomto bloku.

Nový běh se zastavil na prvním `wp04_production_finish` v dialogu Configuration saved. Read-only enumerace oken konkrétního testovacího PID 57708 ukázala #32770 s vnořeným Button id=0 pod CtrlNotifySink/DirectUIHWND. Současný showSystemMessage používá TaskDialogIndirect, ale Gui::acknowledgeSuccess hledá přímé GetDlgItem(IDOK), případně IDCANCEL, a posílá BM_CLICK. Počítadlo notifications navíc zvyšuje před nalezením tlačítka. Harness tedy toto konkrétní potvrzení neuměl automaticky dokončit. Nejde o prokázanou chybu produkčního Finish.

Po ověření PID, názvu a přesné cesty byl ukončen pouze tento zablokovaný testovací proces. Runner skončil exit 1 s child exit -1 po 113,217 s. `wp04_production_lost_finish` se nespustil. Harness ani produkční dialog nebyly v tomto omezeném bloku opraveny. Další pokračování této akceptace vyžaduje samostatně vyřešit testovací ovládání TaskDialog a spustit oba scénáře znovu.

Logy: [test.stdout.log](../../../work/logs/tray-dev/20260912-084149-117-e4534f2a2b7c454db9c3fcdd5295f51b/test.stdout.log), [build.log](../../../work/logs/tray-dev/20260912-084149-117-e4534f2a2b7c454db9c3fcdd5295f51b/build.log). Po ukončení nebyl nalezen žádný běžící phpvpn-tray-ui-tests proces.

## Rozsah, validace a NOT RUN

- Checkout zůstal tray_UX, HEAD da788b71feac031d5572b906e5d3ac51d4e87ae4. Porovnání s obsahem před tímto blokem ukázalo pouze čtyři změněné řádky client.md a dva řádky matice. Po odečtení tří nových include a nového testu se test_runtime_status.cpp obsahově shoduje s původním stavem D01–D06. Řádkové konce byly při porovnání normalizovány.
- SHA-256 protocol.md, tray_UX.md, rozpracovaného test_config.cpp a progress.md se nezměnily. Široký snapshot zdrojů nevznikl. Dřívější reporty nebyly upraveny.
- `git diff --check`: exit 0. Nové lokální Markdown odkazy v upravených řádcích a tomto reportu ověřeny včetně cílového nadpisu Wizard IPC. Git upozorňuje na svou budoucí normalizaci LF/CRLF, nikoli na whitespace chyby.
- NOT RUN: plná CTest sada, Release/ASAN, skutečný PHP relay, WinHTTP cancellation za síťového provozu, SCM, Wintun/VPN, instalace, E2E, multiuser, sleep/resume a vizuální/performance brána všech DPI. Nový unit test s nativním oknem není tato širší akceptace.
- Finish UI akceptace: zahájená první varianta byla přerušena, druhá NOT RUN. Její historické počty nelze vydávat za výsledek aktuálního kódu po změně dialogů.
- B01–B05 zůstávají plánovanou implementací, R04 odložené. D08 a další bloky nebyly provedeny. Neproběhl commit, push, reset, checkout, vytvoření worktree ani subagentů, ani ruční mazání souborů. Testy použily vlastní dočasné fixture a jejich standardní úklid.
