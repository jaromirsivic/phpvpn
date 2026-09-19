---
plan_version: 1
created_at: 2026-09-19-08-37-23
timezone: Europe/Berlin
utc_offset: '+02:00'
source: 'C:\git\phpvpn\Intents\2026-09-19-00-47-57\intent-v003.md'
branch: tray_UX
examined_commit: 1b7cd4a6dd2e221ce3487e7c557b2d3c1819d55f
planning_context_id: 01a0b85e-791c-7072-87e0-93575eae4b33
coordinator_context_id: 01a0b859-68da-7830-ab28-300d41f7c675
deadline: '2026-09-19 11:02:55 +02:00'
status: complete_plan_awaiting_realization_choice
---

# Realizační plán: autostart, automatický Connect a Windows ikony

## 1. Autorita, rozsah a závazné upřesnění

Autoritou je celý intent v003 uvedený v metadatech. Tento dokument je úplný plán, není autorizací implementace. Žádná implementační změna, instalace, změna registru, commit, push ani PR nebyly při plánování provedeny. Realizace má proběhnout přímo v aktivní feature větvi, bez worktree. Před zahájením ověřit aktuální větev, HEAD a necommitnuté změny. Uvedené řádky jsou orientační souřadnice ověřeného HEAD, nikoli trvalé identifikátory.

Značení v plánu:

- **[I]** explicitní požadavek intentu v003, včetně jeho akceptačních kritérií AK1 až AK14.
- **[U]** výslovné uživatelské upřesnění předané koordinátorkou během tohoto plánování.
- **[F]** ověřený fakt aktuálního projektu nebo dokumentace platformy.
- **[D]** doporučené realizační rozhodnutí. Konkrétní nové symboly a soubory označené jako nové dosud neexistují.

**[U]** Když je konfigurace nečitelná nebo syntakticky poškozená a `autoConnectOnStartup` nelze spolehlivě zjistit, služba zůstane ovladatelná bez automatického pokusu Connect. Chybu zpřístupní ve statusu a bezpečně zaznamená do logu. Automatický Connect se spustí pouze při spolehlivě načtené hodnotě `true`. Toto upřesnění uživatel výslovně potvrdil podle zprávy koordinátorky z tasku uvedeného v metadatech. Není odvozeno z mlčení a nepřepisuje uložený intent.

**[I]** Rozsah tvoří strojový autostart tray aplikace, nový boolean konfigurace a jeho startup chování, aplikační a stavové ICO resources a související testy. Nezavádí se checkbox, watcher konfigurace, druhá connect sekvence, proces tray spouštěný službou ani nová síťová či IPC autorizační cesta. Zdrojové ICO soubory zůstanou přesně soubory určenými intentem.

## 2. Ověřený výchozí stav

Kontext byl získán nejprve pomocí `graft map`, potom `graft ask --source`, `graft skeleton`, `graft callers` a úplných výskytů přes `graft grep`. Potřebné detaily byly ověřeny na odkazovaných úsecích zdrojů. CMake a PowerShell soubory, které graf neindexuje, byly čteny přímo. `graft callers connect` hlásí nejednoznačný symbol a chybějící hrany, proto není důkazem nepřítomnosti volání. Volání z IPC bylo potvrzeno přes `graft grep "engine_.connect"`.

| Fakt [F] | Ověřený integrační bod a důsledek |
|---|---|
| Přímý install/uninstall nyní řeší jen SCM | `client/apps/service/main.cpp:72–112`, `wmain`. `installService` v `service_host.cpp:285–323` vrací chybu při existující službě. `uninstallService:325–354` vrací chybu při chybějící službě. Idempotence vyžaduje skutečnou změnu, ne pouze doplnění registru. |
| Balíček již má autostart | `client/tools/package/install.ps1`, krok 8, zapisuje `phpvpn-tray` přes PowerShell provider do HKLM Run. `uninstall.ps1`, krok 4, jej maže. Při `$ErrorActionPreference = "Stop"` jde o fatální krok. Přímé CLI tento krok obchází. |
| Instalace používá verzované generace | `install.ps1` kopíruje do `%ProgramFiles%\phpvpn\<verze>`, přepíná `current.generation`, odregistruje starou službu a registruje novou. Ověření podpisů a hashů předchází změnám. Současné přepnutí generace není úplná transakce s automatickým rollbackem všeho. |
| Existuje nouzová odinstalace bez EXE | `uninstall.ps1` má větev `sc.exe delete`, pokud chybí service binary. Tuto cestu nelze při centralizaci registrace prostě odstranit. |
| Existuje i vývojový HKCU autostart | `client/tools/dev-install.ps1` nabízí `-WithTrayAutostart`. `dev-uninstall.ps1` odstraňuje HKCU položku. Po zavedení povinného HKLM autostartu je potřeba zabránit novému dvojímu registrování. |
| Produkční balíček obsahuje tři EXE | `client/apps/CMakeLists.txt` definuje `phpvpn-service`, `phpvpn-tray` a `phpvpn-cli` s výstupem `phpvpn.exe`. `install.ps1` vyžaduje všechny tři. Výchozí ikona tedy nepatří pouze do dvou EXE uvedených jako hlavní příklady v intentu. |
| Startup konfigurace už je tolerantní k části chyb | `ServiceRuntime::start`, `service_host.cpp:55–149`, vytvoří `ConfigStore`, provede recovery a `ensureDefaultAtStartup`, chyby loguje, vytvoří engine, wizard a IPC server. Chyba `engine_->start()` sama nezastaví publikování IPC. |
| SCM RUNNING přichází až po návratu runtime startu | `serviceMain:203–232`, zejména ř. 225. `runInConsole:260–283` používá stejný runtime. Nové čtení a Connect nesmějí být synchronně vloženy před dokončení tohoto startu. |
| Connect není neblokující veřejná metoda | `VpnEngine::connect`, `vpn_engine.cpp:224–279`, čte konfiguraci, synchronizuje přechody a čeká na přijetí síťovým vláknem. IPC jej zařazuje do file executor, viz `service_host.cpp:128–132`. |
| Connect chrání souběh a odděluje přijetí od síťového bootstrapu | `connect`, `disconnect:281–290`, `runNetworkThread:558–758` a `runConnectSequence:349–493`. Používají control generation, epochu, `connectCommandPending_`, `transitionMutex_`, původní validátor, wizard hooks a recovery. |
| Chyba před přijetím Connect není automaticky veřejný error stav | `connect:246–255` při vadném config vrací chybu. Naproti tomu `start:45–88` publikuje startup error a síťové vlákno klasifikuje bootstrap selhání na `error`/`recovering`. Pouhé ignorování návratu `connect()` nesplní AK9. |
| Store neponechává starou platnou konfiguraci po chybném čtení | `ConfigStore::readLocked:80–99` v `config_store.cpp`. `analyze:43–79` může uchovat čitelný dokument i při neplatné runtime konfiguraci. To umožňuje odlišit čitelný boolean od použitelnosti celého config. |
| Konfigurace má dva modely | `ClientConfig` v `config.h:93–120` a `ConfigValues` v `config_document.h:37–49`. `parseAndValidateConfig`, `config_validator.cpp:332–347`, je explicitně kopíruje. Nové pole musí projít oběma. |
| MasterSetup má whitelist | `validateConfigValues`, `config_validator.cpp:120–330`. Nové pole je dnes neznámé. Volitelné wizard booleany už mají správný vzor kontroly typu. |
| Wizard kopíruje DOM | `analyzeWizardDocument:48–66`, `buildWizardDocument:75–112` v `config_document.cpp`. `makeNewWizardDocument`, `config_defaults.cpp:5–22`, vytváří výchozí DOM. Reset v `WizardService::execute` na ř. 255 používá tentýž default. |
| Tray zaměňuje formulářovou a stavovou ikonu | `TrayApplication::updateIcon:210–219` mapuje síťový stav. `ProgressWindow::updateResources:132–157` a `refresh:158–206` nastavují stavovou ikonu jako malou ikonu okna. Totéž s connected ikonou dělají editor, statistiky a detail chyby. |
| Disconnect má více životních fází | `startCommand:220–260`, `finishDisconnect:413–424`, `ProgressWindow::disconnectPending`, `progress_window.h:19–21`. `disconnectPending_` pokrývá RPC, ale potvrzení RPC nemusí znamenat dokončený teardown. |
| Chybný status uchovává poslední snapshot odděleně od dostupnosti | `onSnapshot:199–209`, `applyStatusSnapshot`, `status_model.cpp:125–129`. Ikona proto nesmí vycházet jen ze starého `snapshot_.state`. |
| Resources už mají RAII a DPI | `UiResources::update:30–71` v `ui_resources.cpp`, `UniqueUiHandle` v `.h`, načítání pomocí `LoadImageW` a `GetSystemMetricsForDpi`. Některá okna volají `update(..., false)` bez stavových ikon. |
| Existující RC ikony jsou jiné | `tray.rc` odkazuje na `client/assets/icons/shield_lock_*.ico`, ID 101–103. `version.rc.in` má výchozí ikonu ID 100, ale tray její generování vypíná. |
| Přesné vstupy intentu existují | `assets/default.ico`, `connected.ico`, `connecting.ico`, `disconnected.ico` mají po 23 150 bytech. Přečtené ICO adresáře uvádějí velikosti 16, 32 a 128 px. Nezaměňovat s dalšími `assets/icon_*.ico` ani se starými shield soubory. |
| Testy s názvem resources nejsou kontrola PE ikon | `client/tools/run-resource-tests.ps1` a `phpvpn-resource-tests` měří IPC paměť a procesní DNS. `check_icons.py:15–36` ověřuje starou geometrii shield ikon, nikoli nové vstupy či výsledné EXE. Potřebné PE kontroly dosud nepokrývají požadavek. |
| Packaging test očekává dosavadní fatální autostart | `client/tests/test_package_feedback.ps1:163–176` řadí `autostart-failure` mezi exit 1. Očekávání se musí cíleně změnit, ostatní kritické chyby zůstanou chybami. |

Na začátku práce byly v Gitu změněný `.intent-builder-active` a nový adresář tohoto intentu. Tyto změny nejsou součástí implementace. Marker nyní spravuje koordinátorka a uvádí route `plan` a tento plánovací task.

## 3. Přesné vymezení změn a závislostí

Cesty v tabulkách jsou relativní k `C:\git\phpvpn`. Nové názvy jsou [D], nejsou tvrzením o existujícím API.

### 3.1 Instalace a registr

| Soubor | Symbol / zamýšlená změna |
|---|---|
| `client/apps/service/main.cpp` | `wmain`, `printUsage`: volat jednotný instalační tok, tisknout bezpečné warning zvlášť od kritické chyby, zachovat rozlišení exit 0/1. |
| `client/apps/service/service_host.h`, `.cpp` | `installService`, `uninstallService`: idempotentní SCM kritická část a doplňková autostart operace. Doporučený nový strukturovaný výsledek oddělí kritický `Status` a případné varování. |
| **nové** `client/src/platform/windows/tray_autostart.h`, `.cpp` | `buildTrayAutostartCommand`, `registerTrayAutostart`, `removeTrayAutostart`, malý injektovatelný registry backend. Konstanty: HKLM, 64bit view, Run, `phpvpn-tray`, REG_SZ. |
| `client/src/CMakeLists.txt` | Zařadit platformní zdroj. `advapi32` už platformní target linkuje. |
| `client/tools/package/install.ps1` | Zrušit druhý samostatný zápis Run. Krok vykoná `--install`. Neztratit warning na stderr a nenechat PowerShell převést doplňkové varování na fatální chybu. |
| `client/tools/package/uninstall.ps1` | Běžně i při již chybějící SCM službě volat existující service EXE `--uninstall`. Zachovat nouzový fallback při chybějícím EXE, s úzce omezeným best-effort odstraněním téže jediné 64bit hodnoty. |
| `client/tools/dev-install.ps1`, `dev-uninstall.ps1` | Sjednotit s přímým CLI. `-WithTrayAutostart` přijímat jako kompatibilní již nadbytečný parametr, nevytvářet další HKCU zápis. Ošetřit vlastní starou HKCU položku v právě dostupném profilu, viz níže. |
| `client/tests/unit/test_service_registration.cpp`, **nový** `test_tray_autostart.cpp` | Testy rozhodnutí SCM, opakování, registry volání a chyb. Skutečné HKLM integrační testy oddělit od běžné unit sady. |
| `client/tests/test_package_feedback.ps1`, `test_package_upgrade.ps1` | Aktualizovat native stuby, warning/exit oracle a pořadí instalačních kroků. |

**[D]** Jediným běžným vlastníkem zápisu je tok obsluhovaný service EXE. Nouzový skriptový cleanup bez tohoto EXE je dokumentovaná degradační větev téhož odinstalačního procesu, nikoli další registrační mechanismus. Nemůže být závislý na neexistujícím souboru. Použije `RegistryKey.OpenBaseKey(..., RegistryView.Registry64)` a odstraní pouze `phpvpn-tray`, nikdy celý Run klíč. Samostatný nový CLI přepínač kvůli fallbacku není nutný.

### 3.2 Konfigurace a start

| Soubor | Symbol / zamýšlená změna |
|---|---|
| `client/src/core/config/config.h` | `ClientConfig::autoConnectOnStartup = false`. |
| `client/src/core/config/config_document.h` | `ConfigValues::autoConnectOnStartup = false`. Nepřidávat pole do uživatelských `WizardFields`, které by vytvářelo nový ovládací prvek. |
| `client/src/core/config/config_validator.cpp`, případně jeho `.h` | Whitelist, boolean validace, explicitní přenos do `ClientConfig`. Sdílený malý čteč startup preference z parsovaného dokumentu pro rozlišení true/false/nezjistitelné hodnoty. |
| `client/src/core/config/config_defaults.cpp` | `makeNewWizardDocument`: append přesného pole jako posledního členu za `useDefaultAccessKeyInWizard`. |
| `client/src/core/config/config_document.cpp` | `buildWizardDocument`: existující true/false zachovat, chybějící doplnit false, nespoléhat na prázdný či neboolean DOM. |
| `client/apps/service/service_host.cpp` | `ServiceRuntime::start`, nový `requestStartupConnect`, `stop`, `serviceMain`, `runInConsole`: jednorázové asynchronní vyhodnocení po inicializaci, v SCM větvi po oznámení RUNNING. |
| **nové** `client/apps/service/startup_connect.h`, `.cpp` | Malý testovatelný koordinátor rozhodnutí a jednorázového naplánování nad Store/Engine. Bez HTTP či Wintun implementace. Zařadit do produkčního i E2E host targetu. |
| `client/src/core/runtime/vpn_engine.h`, `.cpp` | Znovupoužití `connect` admission, ochrana zpožděného startup požadavku control generation a bezpečné publikování chyby před bootstrapem. Žádný druhý `runConnectSequence`. |
| `client/tests/unit/test_config.cpp`, `test_default_config.cpp`, `test_config_store.cpp`, `test_wizard_document.cpp`, `test_wizard_ipc.cpp` | Validační, DOM, startup/default a wizard zachování/reset testy. |
| **nový** `client/tests/unit/test_startup_connect.cpp`, existující `test_runtime_status.cpp` | Jednorázové plánování, rozhodovací tabulka, error status, konkurence a cancellation se skutečným engine a fake platformou. |
| `client/tests/e2e/service_e2e.cpp`, podle potřeby `service_main.cpp` | Izolované SCM start/restart a dostupnost IPC při chybách, použití současného `ServiceHostEnvironment`. |

`ConfigStore` a `WizardService` mají zůstat hlavně integračními závislostmi. Změna jejich implementace je přípustná jen pro předání startup rozhodnutí nebo prokázaně nutnou opravu zachování pole. Jejich atomický commit, bezpečné soubory, revision tokeny a wizard receipts se nemají měnit.

### 3.3 Ikony, všechna okna a build

| Soubor | Symbol / zamýšlená změna |
|---|---|
| `client/apps/CMakeLists.txt`, `client/apps/version.rc.in` | `PHPVPN_RC_ICON`, `phpvpn_add_version_resource`: root `assets/default.ico` pro službu a CLI. Jediná aplikační skupina ID 100 v každém EXE. |
| `client/apps/tray/resource.h`, `tray.rc` | `IDI_PHPVPN_APP = 100` a stavová ID 101/102/103. Tray RC vloží default i tři přesné root assets, generovaná version RC pro tray dál nepřidá druhou skupinu 100. |
| `client/apps/tray/ui_resources.h`, `.cpp` | Oddělit `appIconSmall`, `appIconLarge` od stavových ikon. Nový `applyApplicationIcons(HWND)` nebo ekvivalent, který nastavuje obě velikosti a zaručuje životnost handle. `loadStateIcons=false` nesmí vypnout aplikační ikony. |
| `client/apps/tray/tray_application.h`, `.cpp` | `initialise`, `updateIcon`, `startCommand`, `finishCommand`, `finishDisconnect`, exit/session lifecycle. Oddělit životní cyklus lokálního odpojování od prosté odpovědi RPC. |
| `client/apps/tray/status_model.h`, `.cpp` | Doporučená čistá funkce `selectTrayIcon` vracející typ ikony z validního statusu a lokálního průběhu. Neměnit wire format v1. |
| `client/apps/tray/progress_window.h`, `.cpp` | `updateResources`, `refresh`, `onDisconnectReply`, `onSnapshot`, `disconnectPending`: odstranit stavové WM_SETICON a zpřístupnit tray terminální změnu odpojování. Platí pro Connect i Disconnect okno. |
| `client/apps/tray/statistics_window.cpp` | `open`, `updateResources` a DPI: default small/big. |
| `client/apps/tray/editor_window.cpp` | `open`, `updateFonts`: editace konfigurace i informační varianta, default small/big. |
| `client/apps/tray/http_error_detail_window.cpp` | `open`, `updateFonts`: totéž včetně oken otevřených z wizardu a progress. |
| `client/apps/tray/server_info_window.cpp` | `open`, `fonts`, `request`, `finish`: ikony hlavního okna a přesměrování tří přímých MessageBox volání přes společný dialog wrapper. |
| `client/apps/tray/wizard_window.cpp` | `open`, `render`, obsluha WM_DPICHANGED: aplikační ikony i při vypnutém načítání stavových ikon. |
| `client/apps/tray/contracts_window.cpp` | `open`, `fonts`, DPI: stejné ikony. |
| `client/apps/tray/password_sha_window.cpp` | `open`, `resources`, DPI: stejné ikony každého top-level okna klíče/hesla. |
| `client/apps/tray/system_dialog.cpp`, `.h` | `runTaskDialog`, `showSystemMessage`, `confirmSystemAction`, `showStartupError`: callback po vytvoření pro small/big aplikační ikonu, také časný MessageBox fallback. Významová error/warning ikona v těle dialogu se nemění. |
| `client/tests/CMakeLists.txt` | Sdílená RC integrace všech testů, které kompilují `tray.rc`, nové testy a samostatná PE resource kontrola. |
| `client/tests/unit/test_tray_application.cpp`, `test_tray_status.cpp`, `test_tray_infrastructure.cpp`, `test_wp03_windows.cpp`, `test_wp05_windows.cpp`, `client/tests/ui/tray_ui_tests.cpp` | Stavové mapování, všechny HWND, systémové dialogy, DPI a lifetime regresní scénáře. |
| **nový** `client/tests/resources/icon_resources.cpp` | Read-only PE resource test všech tří EXE přes Win32 resource API a dekódování ikon, bez spouštění produkčních EXE. |
| `client/tools/verify-package.ps1` | Vyvolat PE resource ověření nad balíčkem nebo provést rovnocenné kontroly. Zachovat hash/signature ověření i zdrojovou shodu skriptů. |
| `client/tools/check_icons.py` | Aktualizovat účel testu na čtyři přesné root ICO vstupy, jejich čitelné frames a správné zdrojové mapování. Nepoužívat starý oracle barev, masky a neprůhledného černého okraje na nové ikony. |

**[D]** CMake sjednotí přidání `tray.rc` a RC include cesty do kořenového `assets` pro produkční tray i všechny UI/unit targety. RC může používat názvy `default.ico` atd. s explicitní absolutní include cestou. Nenavázat rozlišení cesty na pracovní adresář procesu RC a neplést `${CMAKE_SOURCE_DIR}` (zde `client`) s kořenem repozitáře. V configure fázi ověřit existenci všech čtyř vstupů. Při změně ICO musí build znovu přeložit příslušný resource objekt. Žádné kopírování ICO vedle EXE za běhu.

Další pouze ověřované závislosti: `client/apps/tray/shell_adapter.h` (`ShellIcon`), `status_reader.*`, `window_utils.*`, `client/src/core/ipc/ipc_dispatcher.cpp`, `ipc_protocol.cpp`, `client/src/platform/windows/ipc_pipe.*`, `client/tools/make-dist.ps1`, `install.bat`, `uninstall.bat`. Neměnit je bez konkrétní integrační potřeby. `assets/*.ico` nejsou výstup generátoru této realizace. Testovací EXE nejsou produkční artefakty, ale musí obsahovat prostředky vyžadované vloženým UI.

## 4. Realizační návrh a bezpečné chování

### 4.1 Registrace a idempotence

1. **[I/D]** Kritická SCM instalace proběhne první. Při skutečné SCM chybě nevytvářet nový autostart a vrátit neúspěch. Existující správná služba se považuje za idempotentní úspěch, nikoli `service_already_running` chybu produktu.
2. **[D]** Existující registraci načíst a ověřit. Při totožné absolutní ImagePath není potřeba změna. Při známém PHPVPN upgrade použít explicitní SCM aktualizaci ImagePath na zamýšlenou generaci. Nepsat registry tray cestu k jinému EXE, než ke kterému ukazuje úspěšně připravená služba. Nesouvisející kolizi jména nebo nepřístupnou konfiguraci SCM neskrývat jako úspěch. Zachovat TCP/IP dependency a existující recovery nastavení.
3. **[I/D]** Absolutní cestu tray odvodit od ověřené cesty aktuálního service EXE, ne z CWD, PATH, uživatelského JSON, textu aktuální hodnoty Run ani shellové expanze. Cíl musí být existující běžný `phpvpn-tray.exe` vedle služby. Ošetřit mezery, Unicode, zakázané uvozovky, vložený NUL a délku. Sestavit celý příkaz `"<absolutní cesta>"` v paměti, bez argumentů a bez spouštění přes cmd/PowerShell.
4. **[I/D]** Otevřít HKLM Run s minimálními potřebnými právy a `KEY_WOW64_64KEY`, zapsat jedinou hodnotu `phpvpn-tray`, typ REG_SZ, celý UTF-16 řetězec včetně terminátoru jedním `RegSetValueExW`. Žádné mezikroky zapisující prázdnou, relativní nebo postupně skládanou hodnotu. Opakování přepíše tutéž položku, nevytvoří varianty názvu.
5. **[D]** Před zápisem si pro kontrolu a testy uchovat původní vlastní hodnotu a její typ. Při chybě před zápisem ji neměnit. Při úspěšném zápisu a následné chybě diagnostického čtení nevyrábět druhou destruktivní operaci, která by odstranila správný zápis. Produkční úspěch zápisu se řídí výsledkem Win32 API. Pokud konkrétní vícefázová implementace přidá mutace navíc, musí mít kompenzaci pouze vlastní hodnoty a musí zachovat cizí souběžnou změnu. Preferovaná jednofázová implementace se této potřebě vyhne.
6. **[I]** Registry selhání po úspěšné SCM operaci vrátí warning a kritický výsledek zůstává úspěšný. Zalogovat operaci, pevný bezpečný popis a číselný/symbolický kód, ne obsah config, klíče, secret, přihlašovací údaje ani celý libovolný systémový text. CLI warning musí být vidět i bez spuštěného souborového loggeru.
7. **[I/D]** Odinstalace zkusí odstranit službu a vlastní Run položku nezávisle. Chyba registry nesmí přeskočit SCM ani souborový cleanup. Nepřítomná služba i nepřítomná hodnota znamenají idempotentní úspěch, access denied jím není. `ERROR_SERVICE_MARKED_FOR_DELETE` vyhodnotit jako již probíhající odstranění, zachovat omezené čekání a jasný výsledek pro opakování.
8. **[D]** Zachovat název `phpvpn-tray` kvůli upgradu ze současného balíčku. Vývojové skripty přestanou zapisovat HKCU. Starou HKCU položku lze v právě obsluhovaném profilu odstranit jen po ověření, že jde o dosavadní vlastní PHPVPN příkaz pro známou instalaci/build. Neprocházet cizí profily a hive. Pokud staré vývojové profily nelze bezpečně migrovat, uvést je v release poznámce jako lokální cleanup, ne tvrdit, že byl globálně proveden.

**[F]** Windows Run spouští program při přihlášení a negarantuje přesný okamžik startu. Dokumentovaný limit příkazu je 260 znaků. Nadlimitní přímá instalace tedy skončí úspěšnou kritickou instalací s varováním, nikoli oříznutou autostart cestou. Nepoužívat RunOnce ani slib okamžitého startu před shellem. [Microsoft Run/RunOnce](https://learn.microsoft.com/en-us/windows/win32/setupapi/run-and-runonce-registry-keys)

**[F]** Explicitní volba 64bit view patří do otevření klíče. Nepoužívat ručně `Wow6432Node`. [Microsoft registry views](https://learn.microsoft.com/en-us/windows/win32/winprog64/accessing-an-alternate-registry-view). REG_SZ vyžaduje správnou délku v bytech včetně NUL. [RegSetValueExW](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regsetvalueexw)

### 4.2 Konfigurační dokument

**[I]** Chybějící hodnota znamená false, přítomné true/false se přenesou, string/číslo/null/pole/objekt se odmítnou stejným error kódem a stylem jako ostatní boolean členy. Whitelist zůstane striktní pro ostatní neznámá pole.

**[D]** Přidat obyčejný boolean do `ConfigValues` a `ClientConfig`, nikoli wizard preference s odvozovaným defaultem. Serializer default DOM přidá pole bezprostředně za `useDefaultAccessKeyInWizard`. `buildWizardDocument` uchová explicitní hodnotu původního DOM a doplní false pouze při absenci. U create/reset výstupu zkontrolovat i konečné pořadí, protože doplnění identity dnes může přidat další členy na konec masterSetup. Doporučené řešení normalizuje poslední dvojici `useDefaultAccessKeyInWizard`, `autoConnectOnStartup` až po všech ostatních wizard úpravách. Ostatní hodnoty a neobsluhovaná nastavení se zachovají.

**[I/D]** Čtení starého dokumentu nesmí automaticky přepsat soubor. Nové pole se doplní až při vytvoření defaultu nebo explicitním uložení/dokončení wizardu. Ruční editor nadále může uložit validní starý dokument bez pole, chování bude false. Reset využije nový default a následný standardní ověřený wizard commit, žádná zvláštní přímá manipulace se souborem.

### 4.3 Startup operace a publikování chyb

**[D]** Po `ConfigStore` recovery/default, `VpnEngine::start`, instalaci wizard hooks a úspěšném startu IPC serveru vytvořit přesně jeden startup požadavek na životní cyklus runtime. V SCM hostu jej neblokujícím způsobem naplánovat až po `SERVICE_RUNNING`, v konzolovém hostu po úspěšném runtime startu. Pro samotné čtení a veřejný Connect využít existující file executor `IpcPipeServer::tryPostFile`, nikoli hlavní SCM vlákno, UI nebo síťové vlákno enginu. Přímé volání Connect ze síťového vlákna by mohlo čekat samo na sebe.

**[D]** Naplánování se testuje odděleně od vykonání. Při plné frontě nesmí požadavek potichu zmizet: držet pouze jeden pending startup token a zkusit znovu při uvolnění kapacity, například z existujícího krátkého `onTick`, bez aktivního čekání, čtení config a bez druhého Connect. Stop jej definitivně ruší. Žádný periodický watcher config z tohoto mechanismu nevzniká.

| Stav čtení při vykonání jednorázového požadavku | Chování [I/U/D] |
|---|---|
| Validní config, explicitní false | Žádný automatický Connect. |
| Validní starý config bez pole | Žádný automatický Connect. Na disku beze změny. |
| Nový default či reset s false | Žádný automatický Connect, i když ještě chybí VPN identita. |
| Čitelný korektní JSON, spolehlivé true, validní celý config | Veřejná běžná Connect operace, stejná ochrana a bootstrap jako IPC. |
| Čitelný korektní JSON, spolehlivé true, vadný jiný člen config | Běžná Connect operace odmítne konfiguraci před sítí. Publikovat bezpečný error standardním status rozhraním. |
| Dokument nelze přečíst, není korektní JSON/UTF-8, masterSetup nelze bezpečně vyhodnotit | Bez automatického Connect. Publikovat konfigurační error a log, IPC/SCM zůstávají dostupné podle [U]. |
| Pole existuje s jiným typem než boolean | Validační chyba, žádný automatický Connect, bezpečný error a log. |
| Služba se zastavuje nebo novější Disconnect zneplatnil startup token | Zrušit opožděnou akci, neprovádět HTTP a nepřepsat novější stav chybou. |
| Engine už má startup chybu např. unresolved ownership journal | Zachovat jeho skutečnou chybu a ovladatelnost IPC, nepřeskakovat recovery ani opakovaně nestartovat engine. |

**[D]** Preference se čte bezpečně ze stejného omezeného a normalizovaného snapshotu Store. Případná pomocná extrakce nesmí používat regex, podřetězec nebo starý cache. Plná validace se nikdy neobchází: extrakce true pouze rozhodne, zda požádat běžný Connect, neopravňuje HTTP, změnu rout ani otevření tunelu. Sdílet boolean kontrolu s validátorem. Je-li pole spolehlivě false, samotná existence neúplné identity nového wizard defaultu nesmí vytvořit falešný Connect failure.

**[D]** Startup token zachytí generaci řízení už při naplánování. Zpožděný callback po ručním Disconnect nesmí znovu připojit. Případnou interní variantu `connect(expectedGeneration)` implementovat vyčleněním společného těla admission z existujícího `connect`, ne kopírováním logiky. Ruční Connect a startup Connect se přirozeně sloučí existujícími zámky a epochou. Automatika provede nejvýše jeden přijatý Connect na start, retry je nadále pouze stávající recovery logika enginu.

**[D]** Pro chyby před přijetím Connect doplnit malý engine mechanismus publikování startup výsledku přes současný `SafeError`, `lastError`, `configurationValid` a `error/idle`. Smí publikovat jen při stále aktuálním tokenu a bez novější aktivní operace. Nevytvářet fiktivní connect attempt ID, HTTP diagnostiku ani novou strukturu IPC. Existující ruční Connect kontrakt zbytečně neměnit. Opravená konfigurace a následný ruční Connect musí být stále použitelné. Cancel při shutdown není konfigurační porucha.

**[F/D]** IPC stop nejprve ruší handlers a následně drainuje přijaté callbacky (`ipc_pipe.cpp:407–423`). Proto startup callback ověří cancellation i před čtením a před admission. Host zneplatní token před `server_->stop()` a nesmí zničit Store/Engine dřív, než callback skončí. Zachovat současné pořadí teardown a testovat Stop při blokovaném čtení i při síťovém bootstrapu. Neočekávanou výjimku z callbacku bezpečně zalogovat, samotné obecné spolknutí výjimky v executor není dostatečná diagnostika.

### 4.4 Ikony a lokální odpojování

**[I/D]** Aplikační ikona všech formulářů je vždy default a nikdy se neodvozuje od stavu VPN. Pro každé top-level HWND nastavit `ICON_SMALL` i `ICON_BIG`. U nových resources zachovat staré platné handles až do úspěšného načtení a přepnutí HWND. Nezničit ikonu, kterou ještě používá jiné živé okno nebo ShellIcon. Při změně DPI načíst odpovídající malé/velké rozměry a teprve potom vyměnit. Změna palety nesmí zbytečně invalidovat handles.

**[D]** TaskDialog dostane aplikační ikony z callbacku `TDN_CREATED`, s vlastnictvím prostředků po celou dobu dialogu a aktualizací pro DPI. Pro časný MessageBox fallback použít thread-local omezený mechanismus při vytvoření dialogu, který pouze nastaví ikony tohoto dialogu a po návratu se odpojí. Nepoužívat globální systémový hook. Při selhání resource loading zachovat viditelnou původní chybovou zprávu. Běžné přímé MessageBox volání server info přesměrovat na existující společný wrapper, bez změny textu a tlačítek.

**[F]** Windows rozlišuje malou caption a velkou Alt+Tab ikonu pomocí WM_SETICON. [Microsoft WM_SETICON](https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-seticon). TaskDialog má callback po vytvoření okna. [Microsoft TDN_CREATED](https://learn.microsoft.com/en-us/windows/win32/controls/tdn-created)

**[D]** Mapování notification area vyčlenit do čisté funkce se stanovenou prioritou:

1. Neplatný/nedostupný aktuální status znamená `disconnected.ico`, i když v cache zůstal connected. Chybu komunikace nelze skrýt starou zelenou ikonou.
2. Platný status a přijatá právě probíhající lokální Connect/Disconnect operace nebo autoritativní `phase == stopping` znamenají `connecting.ico`.
3. `connecting` a `recovering` znamenají `connecting.ico`.
4. Zdravý `connected` znamená `connected.ico`. Pro konzistentní živý snapshot očekávat `phase == running`, `tunnelReady == yes` a bez chyby. Nekonzistentní zdánlivý connected nelze prezentovat jako zdravý.
5. `disconnected`, `error` a neznámá/neplatná kombinace znamenají `disconnected.ico`.

**[I/D]** Při přijetí Disconnect ihned publikovat connecting, nečekat na další status timer. Pouhé přijetí RPC odpovědi není konec operace, pokud engine stále odpojuje. Držet lokální příznak do autoritativního dokončení nebo terminální chyby, sladit jej s progress modelem a explicitně aktualizovat tray při každém přechodu. Nekombinovat ho jen s existencí otevřeného okna, které může po úspěchu ještě odpočítávat zavření. Při selhání odeslání operaci nepovažovat za přijatou. Při timeoutu/neplatné odpovědi ukončit lokální pending, označit status jako nedostupný, vyžádat nové čtení a zobrazit disconnected. Při platné terminální chybě se vrátit k nejnovějšímu autoritativnímu stavu. Starší čtení zahájené před příkazem zrušit nebo odfiltrovat generací.

**[D]** Zachovat funkci tooltipu, timer statusu, klávesového menu, shell re-add a odstranění ikony při Exit/session end. Nový čistý model nemá měnit `TrayStatus` wire validátor ani přidávat skutečný stav `disconnecting` do IPC.

## 5. Pořadí realizace a kontrolní brány

| Krok | Práce | Závislost a podmínka dokončení |
|---|---|---|
| 0 | Přečíst intent v003, tento plán, aktuální AGENTS, `graft map` a cílené dotazy. Zkontrolovat diff proti uvedenému HEAD a stav prostředí. | Nezasáhnout uživatelovy změny. Pokud se integrační body mezitím změnily, aktualizovat plán novou verzí, nikoli přepsáním v001. |
| 1 | Uložit baseline běžných unit/package testů a inventář produkčních EXE/resources. Připravit registry fake, startup scheduler fake a resource oracle. | Jasně odlišit již existující selhání od regrese. Testy skutečného SCM/Run jen v izolovaném prostředí. |
| 2 | Implementovat konfiguraci, defaults a wizard preservaci. | C01–C10 níže. Bez scheduleru, bez sítě, bez UI checkboxu. |
| 3 | Implementovat autostart helper, idempotentní SCM wrapper a warning výsledek. | I01–I12, U01–U08. Nejdřív unit/fake, pak integrační registry/SCM. |
| 4 | Sjednotit package/dev vstupy a fallback cleanup, upravit packaging oracle. | `-VerifyOnly` stále nic nemění, autostart warning dává exit 0 pouze při kritickém úspěchu, skutečné chyby zůstávají exit 1. |
| 5 | Přidat startup koordinaci a guarded engine error publication. | Závisí na kroku 2. A01–A15, nejdřív fake a pak izolované SCM E2E. |
| 6 | Vložit nové resources a oddělit aplikační small/big od stavových handles. | R01–R08, všechny produkční EXE a všechny targety obsahující tray UI se sestaví. |
| 7 | Připojit aplikační ikony do všech oken/dialogů, čisté tray mapování a lokální lifecycle. | Závisí na kroku 6. T01–T12 a W01–W07. |
| 8 | Kompletní regresní běh a ruční VM matice včetně reboot/logon/upgrade. | G01–G08, žádné neověřené povinné AK. |
| 9 | Vytvořit čerstvý balíček stávajícím tokem, ověřit manifest, podpisy, resources a instalaci z něj. Aktualizovat související dokumentaci chování. | Balíček nepoužívá staré skripty ani samostatné ICO. Po větší změně `graft build`, kontrola diffu a závěrečný report s konkrétními výsledky. |

Tento plán nepovoluje commit, push, PR nebo publikování balíčku. Tyto kroky se řídí zadáním následné realizace.

## 6. Selhání, kompenzace a návrat

| Situace | Požadovaný výsledek a bezpečný návrat |
|---|---|
| Kritická SCM instalace selže | Nezapisovat nový autostart. Vrátit kritickou chybu, zachovat původní vlastní platnou registraci a cizí hodnoty. |
| SCM uspěje, příkaz tray není bezpečný nebo registry zápis selže | Službu nerušit. Warning, exit 0 kritické části. Nevytvořit prázdnou/částečnou hodnotu. Opakovaný `--install` je oprava doplňkového kroku. |
| Upgrade přejde na novou generaci a zápis Run selže | Nová služba zůstává nainstalovaná. Případná stará úplná hodnota smí odkazovat jen na dosud zachovanou generaci, varování musí uvést potřebu opravy. Starou generaci neodstranit jen kvůli novému autostartu. Nepředstírat, že všechno běží na nové generaci. |
| Po registraci selže start služby nebo pipe readiness | Jde dál o kritickou instalační chybu stávajícího toku. Diagnostikovat dosažený stav. Není dovoleno hlásit úspěch pouze proto, že Run existuje. Nové registry změny nesmějí vytvářet další neplatnou cestu. |
| Odstranění Run selže, SCM uspěje | Odinstalace pokračuje, exit kritické části 0, viditelné warning se jménem vlastní hodnoty a bezpečným kódem. Může zůstat stale celá hodnota po odstranění souborů. Je to ohlášený stav vyžadující opravu práv a opakování cleanupu, ne důvod držet službu nainstalovanou. |
| SCM odstranění selže | Přesto se pokusit odstranit vlastní autostart. Celkově ohlásit kritickou chybu SCM, ne úspěšnou odinstalaci. Package nesmí bezmyšlenkovitě mazat soubory právě běžící služby. |
| Starý balíček / chybí service EXE | Použít úzký nouzový fallback pro SCM a vlastní Run. Selhání jeho registry operace pouze warning, stejné testy zachování sousedních hodnot. |
| Config nebo Connect selže | Nezastavit SCM a IPC. Žádná jiná cesta k síti. Chyba v existujícím statusu/logu, případná obnova jen standardním recovery enginu. Vadný config automaticky nepřepsat defaultem. |
| Stop přijde před startup callbackem nebo během něj | Zneplatnit pending token, přerušit standardní engine operaci, doběhnout bezpečné callbacky, uvolnit Store/Engine až potom. Žádný opožděný reconnect. |
| Nová ikona se při DPI reloadu nenačte | Zachovat původní platnou sadu a zřetelný diagnostický výsledek. Neprohodit connected/disconnected jako náhradní aplikační ikonu. |
| Návrat k předchozím binárkám | Nejdřív standardně Disconnect/Stop, uchovat config/journal/logs a původní generaci. Původní validátor nové pole odmítá. Před spuštěním staré verze vytvořit zálohu současného config a připravit kompatibilní dokument odebráním pouze nového pole nebo obnovit správnou předchozí zálohu. Nesmazat novější uživatelské změny naslepo. Znovu registrovat správnou generaci a ověřit Run, protože starý přímý CLI autostart nespravuje. |

**[D]** Plná transakční přestavba instalačního generačního systému není součástí intentu. Testy musí po dílčím selhání přesně zachytit SCM ImagePath, `current.generation`, Run a existenci příslušných souborů. Při opravě či návratu se změny provádějí jen v těchto známých vlastních objektech, nepoužívat plošné mazání registru nebo síťových nastavení.

**[I/F]** Zpětná kompatibilita požadovaná intentem znamená starý dokument v novém klientu. Neznamená, že stará binárka s whitelistem začne přijímat nové pole. Rollback proto vyžaduje výše uvedený explicitní datový krok.

## 7. Úplná ověřovací matice

Úrovně: **U** deterministický unit/fake, **P** PowerShell packaging fixture, **E** izolovaný Windows integrační/E2E, **V** ruční vizuální nebo přihlašovací ověření. Každý výsledek uložit s commitem, build konfigurací, časem, očekáváním, skutečným výsledkem a cestou k logu. „Sestavilo se“ nenahrazuje spuštěný test. Níže uvedené testy jsou plánované, při plánování nebyly spuštěny.

### 7.1 Instalace, autostart a odinstalace

| ID | Scénář | Oracle | Úroveň / AK |
|---|---|---|---|
| I01 | Čistý přímý `--install` | Kritický SCM úspěch, jedna REG_SZ `phpvpn-tray` v 64bit HKLM Run, přesně citovaná absolutní cesta ke stejnému adresáři. | U,E / 1,13 |
| I02 | Čistý podepsaný balíček přes `install.bat` | Stejný výsledek jako I01, ověření podpisů/hashů proběhlo před první změnou. | P,E / 1,14 |
| I03 | Opakovat přímý install 2× a stejnou verzi balíčku | Úspěch, žádné další hodnoty, služba a cesta konzistentní, config zachován. | U,P,E / 2 |
| I04 | Upgrade na jinou generaci | Run a ImagePath míří do správné nové generace, staré registry jméno znovu použito, žádný dvojí záznam. | P,E / 1,2 |
| I05 | Mezery a Unicode v umístění, jiný CWD/PATH | Registry příkaz se rozparsuje na právě jedno cílové EXE bez dodatečných argumentů. | U,E / 1,13 |
| I06 | Relativní, prázdná, NUL/uvozovky obsahující cesta, chybějící tray, nadlimitní příkaz | Není zapsána neplatná hodnota. SCM výsledek oddělen od doplňkového warning. | U,E / 3 |
| I07 | Denied open/set registry, selhání před sestavením příkazu | Kritická instalace uspěje, exit 0, jeden ověřitelný warning, původní celé registry bytes nebo absence zachovány. | U,P,E / 3 |
| I08 | Skutečné selhání SCM / cizí konflikt service name | Exit 1, žádný nový Run, žádné maskování chyby. | U,P,E / 3,14 |
| I09 | 32bit host PowerShellu na 64bit OS | Operace stále zasahuje 64bit Run, žádný omyl registry view. | U,E / 1 |
| I10 | Dva různí běžní uživatelé se přihlásí, včetně nového profilu | Tray v každé jejich interaktivní relaci, vlastník daný uživatel, žádný tray v session 0 nebo pod účtem služby. | V,E / 1 |
| I11 | Reboot s nainstalovanou službou, následný logon a odhlášení/přihlášení | Autostart je Run a opakuje se při logon. Nečeká se přesné pořadí proti SCM. | V / 1,2 |
| I12 | VerifyOnly, chybné podpisy/manifest/hash/driver API, zamčené soubory či odmítnutý Exit | Žádný nový Run před ověřením, původní fail-closed chování a uživatelské rozpracované změny zachovány. | P,E / 14 |
| U01 | Přímý uninstall i `uninstall.bat` po instalaci | Vlastní služba a vlastní Run odstraněny, sousední Run hodnoty/klíč beze změny. | U,P,E / 2 |
| U02 | Uninstall opakovaně, chybí jen služba, jen hodnota nebo obojí | Idempotentní úspěch, odstraní zbývající vlastní položku, nezapisuje nic nového. | U,P,E / 2 |
| U03 | Registry delete access denied | SCM/souborový tok pokračuje, exit kritické části 0, warning dostupný, stale hodnota přiznaná. | U,P,E / 3 |
| U04 | SCM stop/delete selže a zároveň registry uspěje/selže | Pokus o obě operace, kritická chyba zůstane exit 1, varování nezakryje příčinu. | U,P,E / 3,14 |
| U05 | Chybí service binary / generation pointer, fallback | Bez spoléhání na neexistující EXE, jen vlastní SCM/Run cleanup, bezpečná opakovatelnost. | P,E / 2,3 |
| U06 | Výchozí uninstall a explicitní `-RemoveData` | Zachování dosavadního datového kontraktu, bez odstranění sdíleného Wintun driveru. | P,E / 14 |
| U07 | Starší HKLM záznam, vývojový HKCU záznam, cizí sousední hodnoty | Nově pouze autoritativní HKLM zápis. Vlastní HKCU cleanup jen při prokázaném vlastnictví, bez cizích profilů a vedlejších škod. | U,P,E / 2,14 |
| U08 | Warning přes native stderr v PowerShellu a UAC relaunch | Výstup zůstane viditelný, kód 0 pro doplňkovou chybu, původní kritické exit kódy a čekání wrapperu zachovány. | P,E / 3,14 |

### 7.2 Konfigurace a wizard

| ID | Scénář | Oracle | Úroveň / AK |
|---|---|---|---|
| C01 | První start s chybějícím config | Default má přesné false jako poslední člen za požadovaným předchůdcem. | U,E / 4 |
| C02 | Start po instalaci s existujícím starým dokumentem | Soubor byte-for-byte zachován, effective false. | U,E / 5,14 |
| C03 | Explicitní true a explicitní false | Oba modely obsahují správnou hodnotu, bez implicitní konverze. | U / 5 |
| C04 | String, int, double, null, array, object | Validační chyba správného typu. Unknown master pole dál odmítnuto. | U / 5 |
| C05 | Wizard edit/repair/dokončení nad true a false | Hodnota zachována, ostatní master/runtime/contracts a tajné hodnoty nezměněny. | U,E / 6 |
| C06 | Wizard nad starým dokumentem bez pole | Po úspěšném uložení false doplněno, při cancel není disk změněn. | U,E / 6 |
| C07 | Wizard reset nad původním true | Nový draft false, finální dokument false a správné pořadí, původní disk až do commit nedotčen. | U,E,V / 4,6 |
| C08 | Create s doplněním identity a default contracts | autoConnect zůstane na konci za useDefaultAccessKeyInWizard i po doplnění dalších členů. | U / 4 |
| C09 | Atomic save failure, revision conflict, ztracený wizard receipt, opakovaný finish | Žádný částečný JSON, existující hodnoty a idempotence commitů zachovány. | U,E / 6,14 |
| C10 | Ruční editor starého/nového config a UI wizardu | Starý formát je stále validní, nový boolean lze editovat v dokumentu, nepřibylo checkbox UI. | U,V / 5,6 |

### 7.3 Start služby a Connect

| ID | Scénář | Oracle | Úroveň / AK |
|---|---|---|---|
| A01 | false a absence pole | Nula automatických Connect a HTTP požadavků, normální ovladatelná služba. | U,E / 7 |
| A02 | true + validní config | Jediný přijatý běžný Connect, connecting → connected, stejné checkpointy a bezpečnostní validace. | U,E / 8 |
| A03 | true + nedostupný server / obnovitelná síťová chyba | Standardní recovering/backoff, status dostupný, retry bez nové startup smyčky. | U,E / 9 |
| A04 | true + autentizační/MAC/TLS chyba či odmítnutý bootstrap | Standardní terminální error dle stávající klasifikace, bezpečný log, SCM RUNNING. | U,E / 9 |
| A05 | true lze přečíst, ale chybí identita/špatný CIDR/limit config | Connect odmítne před HTTP/tunelem, bezpečný error viditelný přes status. | U,E / 9 |
| A06 | Config nečitelný, access denied, neplatný JSON/UTF-8 | Nula automatických Connect, veřejný config error a bezpečný log, IPC ovladatelné. | U,E / 9,[U] |
| A07 | Nový neúplný default false | Žádný nevyžádaný Connect a žádná falešná chyba připojení pouze kvůli nehotovému wizardu. | U,E / 4,7 |
| A08 | Chyba storage recovery / ownership journal / engine start | Žádné obejití fail-closed ochrany, skutečná chyba zachována, IPC služba dostupná. | U,E / 9,14 |
| A09 | Pomalé čtení / síťový bootstrap | RUNNING a kontrolní IPC nemusí čekat na nový startup callback či síť, žádné UI blokování. | U,E / 9 |
| A10 | Startup Connect souběžně s ručním Connect | Jen jedna přijatá epocha a sekvence, žádné dva adaptéry nebo worker role. | U,E / 8,13 |
| A11 | Ruční Disconnect před vykonáním startup callbacku, během čtení i bootstrapu | Novější příkaz vítězí, zpožděný startup se neobnoví a nezapíše pozdní error přes novější stav. | U,E / 9,14 |
| A12 | Stop/shutdown ihned po RUNNING, během fronty i čtení | Bez use-after-free, deadlocku a opožděného HTTP. Žádná zbylá startup úloha. | U,E / 9,14 |
| A13 | Přechod config false→true a true→false za běhu | Změna sama nic nespustí. Po ručním restartu SCM a po reboot se použije nová hodnota. | U,E,V / 7,8 |
| A14 | Plná file fronta, callback výjimka, souběžný wizard | Jeden pending token, žádné tiché ztracení nebo replay. Error log a dostupný status, existující wizard hooks zachovány. | U,E / 9,13 |
| A15 | Start/restart konzolového hostu, oprava config po chybě | Stejná runtime politika, ruční Connect po opravě funguje, bez restartovací smyčky nové automatiky. | U,E / 7–9 |

### 7.4 Stavová ikona, aplikační okna a resources

| ID | Scénář | Oracle | Úroveň / AK |
|---|---|---|---|
| T01 | Validní disconnected | disconnected.ico. | U,E / 11 |
| T02 | Validní zdravý connected/running | connected.ico. | U,E / 11 |
| T03 | connecting a všechny bootstrap fáze | connecting.ico. | U,E / 11 |
| T04 | recovering, včetně čekání backoff | connecting.ico. | U,E / 11 |
| T05 | error | disconnected.ico, nikdy connected ze staré cache. | U,E / 11 |
| T06 | Neplatný JSON/status, timeout, nedostupná služba po dřívějším connected | disconnected.ico, správný unavailable tooltip. | U,E / 11 |
| T07 | Přijatý lokální Disconnect nad connected snapshotem | Okamžitě connecting.ico ještě před odpovědí a bez čekání na timer. | U,E,V / 11 |
| T08 | RPC Disconnect přijato, skutečný teardown ještě běží, phase stopping | Connecting trvá do dokončení, nekončí jen nulováním RPC pending. | U,E / 11 |
| T09 | Dokončení, platná chyba, timeout nebo neplatná Disconnect odpověď | Lokální pending se správně ukončí, nejnovější autoritativní stav nebo unavailable rozhodne, žádné trvale oranžové či staré zelené světlo. | U,E / 11 |
| T10 | Pozdní status zahájený před příkazem, connect superseded, opakovaný Disconnect | Žádný návrat staré ikony, žádný zdvojený příkaz. | U,E / 11,14 |
| T11 | Lokální Connect, jeho odmítnutí nebo cancellation | Pending se neponechá po chybě, zdravý connected pouze po autoritativním úspěchu. | U,E / 11 |
| T12 | Shell znovu potřebuje ikonu, Exit a session end | Správná aktuální ikona, korektní publish/remove, bez duplikace či uniklého handle. | U,E,V / 11,14 |
| W01 | Connect progress i Disconnect progress ve všech stavech | WM_GETICON pro small i big vrací default. Stav nikdy nemění formulářovou ikonu. | U,E,V / 10,11 |
| W02 | Statistics, editor/edit i info režim, server info, HTTP detail | Všechna top-level okna mají default small/big. | E,V / 10 |
| W03 | Wizard, Contracts, PasswordSha včetně více oken | Default small/big i při `update(..., false)`. | E,V / 10 |
| W04 | Informace, warning, confirmation, early startup TaskDialog/MessageBox fallback | Default aplikační ikona, původní významová ikona v těle a tlačítka zachovány. | E,V / 10 |
| W05 | DPI 100/125/150/200 %, přesun mezi dvěma monitory | Správné small/big i notification rozměry, žádné zmizení nebo změna na state ikonu. | E,V / 10,11 |
| W06 | Opakované otevření/zavření, změna palety/high contrast, DPI reload | Stabilní GDI/USER handle počty v toleranci po ustálení, žádné dangling HICON. | U,E,V / 14 |
| W07 | Umělé selhání načtení resource | Zachována platná původní sada, diagnostika a viditelný error dialog. | U,E / 13 |
| R01 | Service, tray a CLI výsledný PE | Každý má default jako aplikační skupinu ID 100, manifest a version block zachovány. | E / 10,12 |
| R02 | Tray PE stavové skupiny | 101=disconnected, 102=connecting, 103=connected, zdrojové pixely/frames odpovídají přesným ICO intentu. | E / 11,12 |
| R03 | ICO vstupy a jejich frames | Čtyři správné názvy, 16/32/128 px vstupy validní, bez použití `icon_*.ico` nebo shield místo nich. | U / 12,13 |
| R04 | Resource symboly a sestavení všech UI test targetů | Žádná duplicita ID 100 nebo chybějící app resource v test hostu. | E / 13,14 |
| R05 | Běh z balíčku bez zdrojového stromu a bez externích ICO | Všechny ikony fungují. Nevyžaduje přítomnost root assets za běhu. | E,V / 12 |
| R06 | Čistý i inkrementální build po kontrolované změně kopie assetu | Dependency přeloží RC a změní odpovídající resource. Produkční zdrojové assety test nepřepíše. | E / 12 |
| R07 | Explorer, properties a Alt+Tab, čerstvá cesta/VM kvůli cache | Všechny tři EXE a všechna okna vizuálně odpovídají default. | V / 10 |
| R08 | x64 Debug/Release a dostupný podporovaný ARM64 build | Stejné resources a identifikátory, žádný hardcoded build/CWD path. Chybějící ARM64 prostředí výslovně uvést. | E / 12,14 |

### 7.5 Regrese, bezpečnost a kompatibilita

| ID | Povinný běh / scénář | Očekávání |
|---|---|---|
| G01 | Celé `phpvpn-client-tests`, `phpvpn-logger-tests`, CTest package upgrade/feedback v Debug a Release | Stávající testy projdou. Změněná očekávání jen tam, kde se mění intentem požadované chování. |
| G02 | `phpvpn-tray-ui-tests` a `phpvpn-wp03-ui-tests` | Layout, focus, klávesnice, wizard a contracts lifecycle zachovány. UI testy nejsou automaticky součástí CTest. |
| G03 | Izolované SCM/Wintun E2E přes existující runner | Cleanup adres/rout/adapteru a host baseline po Stop, žádný zásah do produkční služby testovacím fixture. |
| G04 | Existující IPC RSS a process-local DNS resource testy | Stávající limity a DNS/default route baseline zůstávají platné. Tento běh nenahrazuje R01–R08. |
| G05 | Recovery, retry, diagnostika, souběžné Connect/Disconnect, wizard session a data traffic | Žádné nové role vláken, obejití bezpečnostních checkpointů ani ztráta dostupnosti kontrolního IPC. |
| G06 | Package manifest/hash/signature, VerifyOnly, obě BAT cesty, UAC cancel | Čerstvý konzistentní balíček. `-VerifyOnly` a odmítnuté ověření neprovádějí SCM/Run ani data změny. |
| G07 | Staré config fixtures bez pole, rollback na starou binárku s kompatibilním dokumentem | Upgrade čte staré dokumenty, rollback je explicitní a zachová uživatelské změny a zálohu. |
| G08 | Logy a warning/status výstupy včetně všech failure injection | Neobsahují serverSecret, accessKey, payloadKey, celý config nebo raw diagnostický obsah. Nové chyby jsou zároveň pozorovatelné a správně klasifikované. |

Pokrytí AK: AK1=I01,I02,I05,I09–I11. AK2=I03,I04,U01,U02,U05,U07. AK3=I06–I08,U03,U04,U08. AK4=C01,C07,C08,A07. AK5=C02–C04,C10. AK6=C05–C10. AK7=A01,A07,A13,A15. AK8=A02,A10,A13. AK9=A03–A06,A08–A12,A14,A15. AK10=W01–W05,R01,R07. AK11=T01–T12,R02. AK12=R01–R08. AK13=nové automatizované případy U/P/E v tabulkách. AK14=G01–G08 a regresní případy uvedené u jednotlivých oblastí.

## 8. Spuštění ověřování a potřebné prostředí

Následující jsou příkazy pro budoucí realizaci z `C:\git\phpvpn`, nikoli tvrzení o již provedených testech:

```powershell
& .\client\tools\build-x64.ps1 -Preset debug -Configure -Test
& .\client\tools\build-x64.ps1 -Preset release -Configure -Test
& .\client\tools\run-tray-ui-tests.ps1
& .\client\tools\build-x64.ps1 -Preset debug -UiTests
& .\work\build\debug\tests\phpvpn-wp03-ui-tests.exe
& .\client\tools\run-resource-tests.ps1 -Preset release
& .\client\tools\run-service-e2e.ps1
```

Podle ověřené konfigurace projekt používá CMake/Ninja/MSVC. Service E2E je explicitní AMD64 administrátorský běh, UI vyžaduje interaktivní plochu. Process-local DNS runner vyžaduje Windows 11 build alespoň 22000. Skutečný Run/logon/SCM packaging test provést v disposable VM se snapshotem, dvěma běžnými účty, původním stavem registry a síťových rozhraní. Neměnit ACL sdíleného HKLM Run na pracovním počítači kvůli simulaci chyby. Unit failure injection má fake backend, integrační denied test má izolované prostředí.

**[D]** Nový `phpvpn-icon-resource-tests` target bude přijímat adresář produkčních EXE a zdrojový adresář čtyř ICO pro porovnání. Jeho log má uvést ID skupin, dostupné rozměry a shodu dekódovaných frames, ne pouze přítomnost alespoň jedné ikony. Při zdrojovém CTest závisí na produkčních EXE. U balíčku jej spustit nad balíčkovým adresářem před instalací. Při ověřování chybějících runtime assets provést kopii hotového balíčku mimo source tree, nesmazat uživatelovy assets.

Python závislosti pro `check_icons.py` spravovat výhradně přes `uv`, například izolovaným `uv run --with pillow ...`. Pokud `uv` chybí, oznámit překážku a pokračovat nezávislou kontrolou PE, nikoli instalovat přes pip. Nepouštět starý `make_icons.py` nad novými zdrojovými ikonami.

Čerstvý balíček vytvořit pomocí existujícího `client/tools/make-dist.ps1` s ověřeným build/output adresářem a dostupnou oprávněnou podpisovou identitou. Následně `client/tools/verify-package.ps1 -PackageDirectory <konkrétní výstup>`. Chybějící certifikát či prostředí uvést jako neprovedené ověření, nezaměnit podpisový preflight za úspěch. Licence a notices existujících assets neodstraňovat bez podkladu. ARM64 je existující preset, jeho build je doplňková portability kontrola podle dostupného toolchainu, nikoli nově slíbená plná ARM64 E2E platforma.

## 9. Rizika a jejich uzavření

| Riziko | Opatření / důkaz před předáním |
|---|---|
| SCM/RPC je hotovo, ale síť nebo startup file operace blokuje | Oddělený executor, okamžik RUNNING před startup prací, A09 a A12. |
| Starý startup callback přepíše pozdější uživatelskou akci | Token generace z doby naplánování, společná admission synchronizace, A10–A12. |
| Čitelný boolean je zaměněn za validní celý config | Boolean pouze povoluje běžný Connect, plný validátor stále stojí před sítí, A05/A06. |
| Nový default je neúplný a falešně padá do error | False se vyhodnotí bez pokusu, A07. |
| Nedostupná konfigurace potichu vypadá jako false | Rozlišit nezjistitelnost a explicitní false, status + log podle [U], A06. |
| Povrchově úspěšná instalace skryje registry warning | Strukturovaný výsledek, viditelný CLI výstup a reálný PowerShell stderr test U08. |
| Upgrade ponechá autostart staré generace | Zachovat celou platnou původní hodnotu a soubory, upozornit, opakovat idempotentní registraci. I04/I07. |
| Jedna stará HKCU vývojová položka spustí tray navíc | Nevytvářet další HKCU registrace, omezená vlastní migrace, U07. Není oprávnění mazat cizí profily. |
| Tray zezelená před dokončením Disconnect nebo po chybě statusu | Explicitní lokální lifecycle, phase stopping, unavailable priorita, T06–T10. |
| Chybí app resource v test EXE nebo dojde ke kolizi ID | Sdílený RC CMake helper, jediný ID100, R01/R04. |
| Nové obrázky mají jen tři velikosti | Načítání s požadovanými DPI rozměry a kvalitní Windows scaling, W05. Neměnit zdrojové ICO bez změny zadání. |
| Explorer cache zkreslí vizuální ověření | PE test je primární technický oracle, vizuálně čerstvá cesta/VM. |
| RAII zničí ikonu ještě při používání HWND | Dvoufázové načtení a přepnutí všech uživatelů, lifetime testy W06/W07. |
| Starý klient odmítne nový dokument při rollbacku | Explicitní záloha a kompatibilní dokument, G07. |

## 10. Podmínky předání realizace

Za hotovou lze změnu označit až po pokrytí všech AK1–AK14 konkrétními výsledky. Report musí uvést zvlášť automatizované testy, ruční přihlašovací/DPI ověření a neprovedené kontroly s důvodem. Neprovedený povinný scénář nemá stav PASS. Rozdíly proti baseline musí být vysvětleny. Zachovat bezpečné síťové cleanup invariants a testovací isolation.

Tento plán vyřešil položenou materiální otázku uživatelským potvrzením [U]. Ostatní konkrétní technické postupy jsou označeny [D] a mají uvedené ověření. Při nové materiální nejasnosti v realizaci položit jednu konkrétní otázku, nepřepisovat intent a nevydávat vlastní domněnku za jeho požadavek.

Plánovací task vlastní jen tento nový plán. Starší intent a plan artefakty zůstaly beze změny. Koordinátorce se předává existující soubor a dokončená verze 1, aby mohla aktualizovat svůj marker a získat samostatnou volbu realizace. Uložený termín tohoto plánovacího stromu nebyl automaticky technicky vynucován, byl kontrolován během práce.
