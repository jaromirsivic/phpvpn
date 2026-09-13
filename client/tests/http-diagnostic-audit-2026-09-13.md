# Audit HTTP diagnostiky Wizardu, 13. září 2026

Autorita: `Plans/2026-09-13-02-28-22/plan-v001.md` a `Intents/2026-09-13-02-28-22/intent-v002.md`. Implementace proběhla přímo na `tray_UX` v `C:\git\phpvpn`, bez worktree, dalších agentů a commitu. Existující `.intent-builder-active` nebyl upravován. Serverový `phpvpn.php` ani původní lokální endpoint a Nginx konfigurace se nezměnily.

## Výsledek

Implementována bounded diagnostika konkrétní failed health/info výměny přes WinHTTP, ProbeClient, WizardService, striktní Wizard IPC, model a owned Win32 okno. Červené `Error detail` používá společnou patičku a tab renderer. Request a response mají nezávislé přesné bajtové řezy, reverzibilní IPC codec a inertní Unicode projekci. Nový pokus a invalidace odstraní lokální detail i službovou kopii prostřednictvím existujícího cancel-attempt.

Nové automatické testy, devět lokálních integračních scénářů a závěrečný externí test prošly. Celé historické sady nejsou zelené. Čtyři chyby ContractsDocument a dvě starší UI kontroly jsou reprodukovatelné také bez této implementace. Ctrl+C a aktivní přepnutí systémového high contrast zůstávají níže výslovně NOT RUN.

## Prostředí a sestavení

Windows, MSVC 19.44.35228, Visual Studio 2022 BuildTools, Ninja a CMake presets `debug`, `release`, `asan`. Native UI targety byly povolené přes `PHPVPN_BUILD_UI_TESTS=ON`. Skutečná DPI nativních oken byla 144. Geometrické testy pokrývají 96, 144 a 192 DPI.

| Kontrola | Výsledek |
|---|---|
| `cmake --build --preset debug` | PASS |
| `cmake --build --preset release` | PASS |
| `cmake --build --preset asan` | PASS |
| `ctest --preset debug --output-on-failure` | 342 registrovaných případů v hlavním targetu, 4 existující chyby, 2 explicitně vypnuté integrační případy. Logger target PASS. |
| `ctest --preset release --output-on-failure` | Stejný výsledek. Logger target PASS. |
| `php.exe -l client/tests/fixtures/http-diagnostic.php` | PASS |
| `git diff --check` a `graft build` | PASS |

Výchozí hlavní sada před úpravami měla 325 případů a stejné čtyři chyby:

- `ContractsDocument.absent_present_empty_and_unchecked_removal`, `config_invalid_key`.
- `ContractsDocument.preserves_all_unrelated_values_types_and_optional_presence`, `config_invalid_key`.
- `ContractsDocument.complete_snapshot_rejects_omissions_duplicates_foreign_self_and_bad_keys`, rozdílný očekávaný limitní error.
- `ContractsDocument.result_document_limit_and_actual_encoded_envelope`, `config_invalid_key` místo `config_too_large`.

## Automatické důkazy nové funkce

| Oblast a filtry | Výsledek a rozsah |
|---|---|
| `http_diagnostic.` | 3 PASS. Přesné řezy od nuly přes 65536/65537 po 2 miliony bajtů, různé bloky včetně jednotlivých bajtů, dlouhé headers, uint64 overflow, všech 256 octetů, UTF-8 hranice, NUL/controls/emoji, výpočet KiB a header fallbacky bez vymyšlených hodnot. |
| `WinHttpDiagnostic.` | 3 PASS. Opt-in health/info proti opt-out/data, 503, celý skutečně odeslaný request včetně binárních bajtů, implicitní cesta `/`, duplicitní response headers, nezávislé řezy, prázdné body, odmítnutý nadlimitní blok a timeout bez publikace. |
| `WinHttpAccounting.`, `WinHttpDeadline.`, `WinHttpLifecycle.` | PASS. Účetnictví částečných dat, cancellation, timeout, EOF/reset chyby a opětovné použití transportu zůstaly zachované. |
| `probe.diagnostics_belong_only_to_the_failing_received_exchange` | PASS. Selhání jednotlivých kontrol, správná výměna ve druhém kroku, lokální chyba před info, chyba info bez response, opt-out a access. |
| `wizard_ipc.diagnostic_` | 4 PASS. Uzavřené schéma, umístění jen ve failed url/secret, starý tvar bez pole, neplatné délky/typy/enums/duplicity, celý reply round-trip, error bounds a kontrola celého bezpečného error katalogu. Dva maximálně escapované směry v reálné obálce nepřesáhly 852232 bajtů ani 4 MiB. |
| `wizard_service.diagnostic_` | 2 PASS. Retence, nový start, starý cancel, reset/close/expiry/shutdown, pozdní worker outcome a dlouhá URL bez redundantního vložení do error. |
| `WizardModel.DiagnosticCorrelatesEveryIdentityAndClearsOnAllInvalidations` | PASS. Korelační identity, deadline, epochy a invalidace vstupem, krokem, novým pokusem a koncem relace. |
| Wizard layout | PASS. Hidden slot bez mezery, sousedství a gap 8 DIP, kompaktní šířky, měřené šířky a DPI 96/144/192. |
| `http_diagnostic_wizard_owned_window_cleanup` | PASS v debug, release a ASAN. Oba kroky, správný owner, dva taby, no footer, inertní obsah, šipky, Tab/Shift+Tab, zachování výběru, X a opětovné otevření, editace/Back a cleanup služby. |
| `WP05Windows.HttpDiagnostic` | 2 PASS v debug, release a ASAN. Skutečný text až do 262166 UTF-16 jednotek, výběr posledních čtyř znaků, read-only, resize, DPI zprávy, zánik vlastníka a pixelové kontroly normal/hover/pressed/disabled barvy. Ctrl+C podkontrola byla přeskočená z níže uvedeného důvodu. |

ASAN proběhl také nad celými filtry `probe`, `wizard_ipc`, `WizardModel` a `wizard_service`. Nález stack-use-after-scope v nově přidaném testu přetečení byl opraven zachováním životnosti dočasného optional a test znovu prošel. V otestovaných produktových cestách nezůstal ASAN nález.

UI regrese `happy_path_resize_keyboard_finish` a `plan_v001_server_information_tabs` prošly. Show Server Info ověřil 12 přepnutí myší/šipkami a jediný get-info. Širší skupina `wp04_` měla 11 případů, z nichž 9 prošlo včetně 24 create/read/check/commit/end-session cyklů, deadline a resource testu. Dvě chyby byly samostatně reprodukované nad nezměněným archivem `git archive HEAD client`, sestaveným bez vytvoření Git worktree:

- `wp04_gate_consent_idle`: očekává focus na již neexistujícím Cancel.
- `wp04_stable_fields_warning`: očekává viditelný Back v URL kroku, ačkoli jej již výchozí implementace skrývá.

Tyto produktové stavy nebyly měněny ani zakryty oslabením původních assertions. Testovací modal helper dostal podporu `TDM_CLICK_BUTTON`, protože tlačítka stávajícího TaskDialogu nejsou vždy přímými child HWND. Tím bylo odstraněno čekání testu na dialog, bez změny produktu.

## Skutečná lokální integrace

Read-only ověřeno `D:\nginx`, běžící Nginx a PHP 8.5.9 FastCGI, platná konfigurace, document root `D:\nginx\html` a dostupný PHP handler. Loopback port 1 neměl listener. Do document root byla zkopírována jen nová dočasná PHP vstupní fixture s unikátním názvem. Používala skutečný serverový health signer s veřejným testovacím secretem a samostatný control file. Původní endpoint ani konfigurace nebyly přepisované. Neproběhl restart cizích procesů.

Filtr `wizard_service.native_http_diagnostic_local_integration` prošel ve skutečném WizardWindow přes soukromý IpcPipeServer, WizardService, asynchronní diagnostický worker, ProbeClient a WinHTTP:

| Scénář | Výsledek |
|---|---|
| Poškozený health v Server URL s HTML/JS před i za původním obsahem | PASS |
| Platný první krok a poškozený health v Server secret | PASS |
| Platný health a poškozené info | PASS |
| Jediný syntakticky poškozený bajt | PASS |
| Celá response 65535 bajtů včetně headers | PASS, úplný výpis |
| Celá response 65536 bajtů včetně headers | PASS, úplný výpis |
| Celá response 65537 bajtů včetně headers | PASS, 64512 + separator + 1024, jediný vynechaný bajt |
| Kompletní prázdné response body | PASS, diagnostika hlaviček |
| Žádná HTTP response | PASS, bez tlačítka |

Každý diagnostický případ ověřil action, response projection v nativním editoru, owner a dostupnost tlačítka. Injekční scénáře ověřily zachování předpony i konce. Duplicitní hlavičky zůstaly zachované. Hranice byly kalibrované podle skutečné hlavičkové části, nikoli zaměněné za velikost body. Další Verify automaticky zavřelo původní detail.

Finally odstranilo vlastní endpoint i control file a ověřilo nezměněné SHA256:

| Soubor | SHA256 |
|---|---|
| `D:\nginx\conf\nginx.conf` | `728E6FD9A49D04ACB68CF1C5733F471B824353E28096D7C59E7198811E5E8295` |
| `D:\nginx\html\phpvpn.php` | `33D0394B0FB2FB893CC5A9BB5C2EA6DA878300DB6AA153A58E1AF8C232E26D46` |
| `C:\git\phpvpn\phpvpn.php` | `9DE6695705D6C46AF4B2BEACF328C17D31BBBD2E04DF040108838280FB421021` |

Raw autentizované zprávy nebyly zapsané do reportu ani síťového záznamu. Vizuální snímky vznikly pouze z oddělených, pevně zadaných syntetických UI dat.

## Externí poslední kontrola

Po lokálních testech proběhl `wizard_service.native_http_diagnostic_external_best_effort` s `PHPVPN_DIAGNOSTIC_EXTERNAL=https://phpvpn.freehosting.dev/phpvpn.php`. Výsledek PASS: skutečně přijatá response způsobila bezpečnou chybu `response_invalid`, Wizard zobrazil Error detail a jeho request/response text přesně odpovídal bounded Unicode projekci. Ověřeno owned okno a správná health akce. TLS ověřování zůstalo zapnuté. Obsah externí response není v tomto auditu ani v logu.

## NOT RUN a meze důkazu

- Ctrl+C: původní schránka nabízela chráněný `EnterpriseDataProtectionId`, ale `GetClipboardData` pro něj nevrátil data. Nebylo možné garantovat úplnou obnovu všech jejích formátů, proto test nic nekopíroval. Pokus o oddělenou testovací window station skončil `CreateWindowStation`, Win32 error 5. Výběr textu a read-only chování jsou ověřené, obsah uživatelovy schránky zůstal zachovaný.
- Aktivní změna systémového high contrast, fyzické přepojování monitorů a screen reader nebyly provedené. High contrast používá systémovou paletu, běžné barvy byly ověřené pixely. Native běh byl na 144 DPI, další DPI mají geometrické a zprávové testy.
- OS-specifické vynucení selhání WinHttpQueryHeaders a dostupnosti trailers nebylo provedeno. Fallback rekonstrukce je deterministicky testovaná přímo ve stejných helperech používaných transportem, skutečné WinHTTP hlavičky a duplicity mají socketové i PHP integrační důkazy.
- Celá historická sada není PASS kvůli výše doloženým existujícím chybám. Není zde tvrzena úplná zelená regrese projektu.

## Akceptační kritéria intent-v002

| Číslo | Stav | Důkaz |
|---|---|---|
| 1 | PASS | Červené tlačítko, oba kroky, sousedství a 8 DIP, native a geometrie. |
| 2 | PASS | Lokální negativní síťový scénář a timeout/cancellation testy. |
| 3 | PASS s uvedenou mezí | Dva read-only multiline taby, no footer, X. Ctrl+C podkontrola NOT RUN. |
| 4 | PASS | Zachycené skutečně odeslané bajty, failed action, header duplicity a native projekce. |
| 5 | PASS | Přesné oracle řezy, jediný chybějící bajt a celočíselné KiB. |
| 6 | PASS | Skutečná maximální escaped obálka pod 852232 bajtů a 4 MiB. |
| 7 | PASS | Health/info parser, crypto, lokální chyba a info bez response. |
| 8 | PASS | Korelace, model/služba, Back/edit, další Verify, close/expiry/epoch a owner destruction. |
| 9 | PASS v kontrolovaném rozsahu | Inertní native EDIT, diagnostika mimo Error/log/store, bez automatického clipboardu. |
| 10 | PASS v regresním rozsahu | Nezměněný serverový soubor, data/accounting/deadline/reuse/probe testy a kontrola diffu. |
| 11 | ČÁSTEČNĚ | Nové testy a lokální integrace PASS, celé historické sady mají doložená stávající selhání a výše uvedené NOT RUN. |
| 12 | PASS | Aktualizovaný client.md, protocol.md a tento audit. |
| 13 | PASS | Práce přímo na tray_UX, žádný worktree. |
| 14 | PASS | Poslední externí Wizard kontrola obdržela chybnou response a zobrazila detail. |
