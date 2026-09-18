# Realizační audit PHPVPN

Implementace podle `Plans/2026-09-13-22-49-34/plan-v002.md` a `Intents/2026-09-13-22-49-34/intent-v007.md` je připravena v aktivní větvi `tray_UX`. Distribuční kandidát je v `C:\git\phpvpn\dist`, samostatný Windows balíček v `C:\git\phpvpn\dist\client\Windows\x64`. Jde o **neúplně ověřený kandidát**, nikoli hotové ověřené vydání. Akceptační brána 12 není splněna, protože část povinných instalačních a systémových scénářů nebyla provedena. Širší UI sady mají také níže popsaná selhání.

## Identita a rozsah

- Základ zdrojů: `d6c9c8a3ed1eee2698c0b389e03e9b5bf58af014` plus zamýšlené necommitnuté změny tohoto tasku.
- Nebyl vytvořen commit, push, worktree ani subagent. Původní marker `.intent-builder-active`, intenty a plány nebyly upraveny.
- Windows 11 Pro x64, verze 10.0.22631. Nativní UI bylo testováno při skutečném DPI 144, tedy 150 %.
- MSVC 14.44, Windows SDK 10.0.22621.0, CMake/Ninja, PHP 8.5.9. Python 3.13 a závislosti byly spravovány přes uv.
- Klientská verze 1.0.0 x64, Wintun 0.14.1. Aktuální release binárky obsahují změny pollingu, Contracts a Server Information.
- Přesné cesty a SHA-256 změněných souborů jsou v [source-files.json](C:/git/phpvpn/work/polling-verification/source-files.json), statistika změn v [changed-files-stat.txt](C:/git/phpvpn/work/polling-verification/changed-files-stat.txt), konečný obsah distribuce v [distribution-files.json](C:/git/phpvpn/work/polling-verification/distribution-files.json).
- Pracovní soubory, testy, screenshoty a starý balíček jsou mimo `dist`. Předchozí distribuce byla zachována v `previous-dist`.

## Implementované chování

Polling již nevyhodnocuje historické kumulativní čítače jako aktuální provoz. NetworkWorker předává monotónní okamžik skutečného TX při předání neprázdného requestu transportu, případně ověřeného RX přijatého do fronty, a okamžik dokončení výměny data. Celé pětisekundové okno používá minimum. Po jeho konci prázdné výměny exponenciálně zvyšují interval v neprázdném průniku lokálních mezí 20 až 5000 ms a serverových mezí. Nový paket obnoví minimum. Health ani prázdné řídicí výměny okno neobnovují. Inkompatibilní meze se odmítnou, reset i nový bootstrap inicializují stav. Diagnostické čítače a pravidla potvrzení/retry paketů zůstaly oddělené.

Contracts po potvrzeném Save přečte čerstvý korelovaný status. Při connected nabídne požadované Yes/No s opravdovým výchozím Yes. Po Yes znovu ověří identitu spojení a použije standardní Disconnect včetně progress, bez automatického Connect. No, Escape, neznámý stav, neurčitý nebo odmítnutý Save a změna generace či spojení nevedou k odpojení.

Server Information má samostatnou zelenou validních řádků, invalidní řádky používají přesně contractsChanged a ostatní success plochy se nezměnily. Titul i nadpis mají správný singulár a celou zelenou závorku. OK je modré primární tlačítko. Auto refresh je místní nepersistovaný přístupný toggle vedle Refresh. Obnovuje obě karty nejdříve za 5000 ms, další termín vzniká až po dokončení požadavku. Pending guard, monotónní deadline a generace chrání souběh a pozdní zprávy. Show Info jej resetuje.

Server vytváří pretty JSON přímo v autentizovaném plaintextu. Limit se stále měří nad skutečnými serializovanými bajty a redukce odebírá celé koncové položky. Klient plaintext znovu neserializuje. Dodatečný nativní test odhalil, že běžný EDIT původně LF pouze uchovával bez zalomení. Nyní pomocí EM_SETEXTENDEDSTYLE vykresluje LF i CRLF beze změny textu. Toto API vyžaduje Windows 10 1809 nebo novější, viz [Microsoft EM_SETEXTENDEDSTYLE](https://learn.microsoft.com/en-us/windows/win32/controls/em-setextendedstyle) a [podporované konce řádků](https://learn.microsoft.com/en-us/windows/win32/controls/edit-control-window-extended-styles). Ověřeno na uvedeném Windows 11, starší Windows nebyly testovány. Minimum Info 320 × 480 DIP ponechává viditelný řádek i při zalomeném nadpisu a patičce.

## Důkazy regrese a provozu

Původní test přes skutečné propojení VpnEngine a NetworkWorker selhal po historickém TX v čase 0 a prázdné výměně dokončené v čase 5000: interval byl 80 místo očekávaných 160 ms. Evidence je v `C:\git\phpvpn\work\polling-baseline-test.log`. Opravený runtime prochází a test ověřuje i skutečné čekání mezi dalšími requesty. Doprovodné deterministické testy pokrývají 0, 4999, 5000 a 5001 ms, TX/RX/deferred, odmítnutí celé dávky, prázdné výměny, pomalý health, meze a životní cyklus.

Rozšířený administrátorský SCM/Wintun E2E prošel. Zahrnuje skutečný UDP socket `.2`, Wintun, aktuální službu, izolovaný PHP relay a produkční NetworkWorker peer `.3`. Po obousměrném přenosu pozoroval deset sekund bez jakéhokoli dalšího VPN payloadu. Kolem pěti sekund byly pozorované mezery 62/63 ms, poté 125, 188, 406, 828 a 1594 ms. Nový skutečný UDP roundtrip obnovil 10 pollů v 600 ms. Jde o měření přes IPC s tolerancí plánovače a RTT, přesnou hranici dokazuje deterministický test. Peer `.3` nepoužívá druhý Wintun adaptér ani druhou VM.

E2E dále ověřil bootstrap, skutečné Next/Finish přes IPC a PHP, přesný adaptér/LUID/adresu/routu, Disconnect, reconnect, SCM stop/restart bez automatického Connect, skutečný pád vlastního procesu a reconciliation. Konečný cleanup potvrdil nepřítomnost testovací služby, adaptéru, PnP záznamu a prefixu a nezměněný produkční PID/stav/cestu/config hash/default routes. První start byl Windows zrušen při UAC, následné uživatelem potvrzené běhy prošly. Závěrečná evidence je [service-e2e.log](C:/git/phpvpn/work/polling-verification/service-e2e.log), `service-e2e.exit` = 0 a [service-e2e-cleanup.log](C:/git/phpvpn/work/polling-verification/service-e2e-cleanup.log).

## Spuštěné testy

| Sada | Výsledek a evidence |
|---|---|
| PHP syntaxe | PASS, [php-lint.log](C:/git/phpvpn/work/polling-verification/php-lint.log) |
| PHP tests.php včetně rozšířeného info | 537 assertions PASS, [php-tests.log](C:/git/phpvpn/work/polling-verification/php-tests.log) |
| Pretty hranice skutečného PHP builderu | PASS, [php-pretty-boundaries.log](C:/git/phpvpn/work/polling-verification/php-pretty-boundaries.log). Standard 1886/1885/1024 bajtů a extended 4976/4975/1024, deterministická redukce a odmítnutí příliš velkého základu či chybného UTF-8 |
| Python celá serverová sada | 392 PASS, 2 SKIP, [python-tests-final.log](C:/git/phpvpn/work/polling-verification/python-tests-final.log). Profilový skip byl zvlášť ověřen, druhý skip: nepozorovaná 503 contention větev |
| Python výchozí profil | 1 PASS se 14 identitami, [python-default-profile.log](C:/git/phpvpn/work/polling-verification/python-default-profile.log) |
| CTest debug | 3/3 PASS, uvnitř 378 unit/runtime, 1 logger a 5 CNG/WinHTTP/PHP contract testů, [debug-final.log](C:/git/phpvpn/work/polling-verification/debug-final.log), [ctest-debug-details.log](C:/git/phpvpn/work/polling-verification/ctest-debug-details.log) |
| CTest release | 3/3 PASS, uvnitř 378 unit/runtime, 1 logger, 5 CNG/WinHTTP/PHP contract testů, [release-final.log](C:/git/phpvpn/work/polling-verification/release-final.log), [ctest-release-details.log](C:/git/phpvpn/work/polling-verification/ctest-release-details.log) |
| Server Information debug a release | 6/6 PASS v každém, přesný raw text a copy včetně LF/CRLF a maxima 262144, reálné timery, MSAA, Tab, 252 peerů, resize, [ui-server-info-debug.log](C:/git/phpvpn/work/polling-verification/ui-server-info-debug.log), [ui-server-info-release.log](C:/git/phpvpn/work/polling-verification/ui-server-info-release.log) a závěrečné plné sady |
| Contracts uložené změny | 11 scénářů PASS přes skutečnou TrayApplication akci a standardní Disconnect, [ui-contracts-consent-debug.log](C:/git/phpvpn/work/polling-verification/ui-contracts-consent-debug.log), též plná release wp03 sada |
| Plná wp03 debug/release | V každé 36 testů, 10 FAIL. Stejných deset jmen selhává i v samostatně sestavené nezměněné kopii HEAD, 35 testů. Evidence [ui-wp03-debug.log](C:/git/phpvpn/work/polling-verification/ui-wp03-debug.log), [ui-wp03-release.log](C:/git/phpvpn/work/polling-verification/ui-wp03-release.log), [ui-wp03-baseline.log](C:/git/phpvpn/work/polling-verification/ui-wp03-baseline.log) |
| Plná tray UI release/debug | V každé 45 testů, 5 FAIL. Stejných pět selhání má plný běh nezměněného HEAD se 44 testy. [ui-tray-release-full.log](C:/git/phpvpn/work/polling-verification/ui-tray-release-full.log), [ui-tray-debug-full.log](C:/git/phpvpn/work/polling-verification/ui-tray-debug-full.log), [ui-tray-baseline-full.log](C:/git/phpvpn/work/polling-verification/ui-tray-baseline-full.log), [ui-baseline-comparison.log](C:/git/phpvpn/work/polling-verification/ui-baseline-comparison.log) |
| Izolovaný SCM/Wintun a skutečný idle polling | PASS, [service-e2e.log](C:/git/phpvpn/work/polling-verification/service-e2e.log), cleanup PASS |
| Izolované testy pořadí installeru | PASS, [package-lifecycle-unit.log](C:/git/phpvpn/work/polling-verification/package-lifecycle-unit.log). Jde o test AST a simulovaného lifecycle, nikoli skutečný upgrade |
| Balíček a VerifyOnly | PASS ze stagingu i konečné cesty, [package-integrity.log](C:/git/phpvpn/work/polling-verification/package-integrity.log), [package-verify-final.log](C:/git/phpvpn/work/polling-verification/package-verify-final.log) |
| Fyzické DPI / více monitorů / čtečka | Nativně pouze 150 %. Geometrie unit 96/120/144/192. Fyzické ostatní DPI, monitor migration, high contrast a čtečka NOT RUN |

Původní Python běh používal standardní PHP built-in 404, které odráželo název neexistujícího souboru a způsobilo jeden bezpečnostní test FAIL. Pouze izolovaná testovací fixture dostala router s prázdným 404, poté celá sada prošla. Provozní Nginx ani jeho phpvpn.php nebyly upraveny.

Deset shodných wp03 selhání na nezměněném HEAD i po změně:

1. `WP03Windows.DefaultProgressHeightsFitEveryVisiblePanelWithLightBottomGap`
2. `WP05Windows.HttpDiagnosticMaximumProjectionTabsAndOwnerDestruction`
3. `WP05Windows.OriginalDeadlineExitWaitAndIndependentEditor`
4. `WP05Windows.FailedCheckBlocksRepeatAndMissingRequiresFreshConfirmation`
5. `WP05Windows.CommonExitRevalidatesAllDraftsAfterModalReentry`
6. `WP05Windows.ReleaseResourcesAccessibilityAnd252RowTiming`
7. `WP05Windows.RealSessionBroadcastOnIsolatedDesktopNeverWaitsForReaders`
8. `WP05Windows.SmallCidrResizeTabOrderAndVisualCapture`
9. `WP05Windows.RandomFailureAndPartialCreateFailureRetainSafeState`
10. `WP05Windows.KeyInputLimitAndIndependentEditorsDoNotRebase`

Tato selhání se nevykazují jako PASS ani jako nově opravená. Jsou doloženým omezením širšího regresního výsledku. `modern_toggle_switches` také selhává shodně na nezměněném HEAD, viz [ui-toggles-baseline.log](C:/git/phpvpn/work/polling-verification/ui-toggles-baseline.log).

Plné tray běhy nad nezměněným HEAD i debug/release mají shodně tyto další výsledky FAIL: `modern_toggle_switches` (maximum 252 native rows missing), `wp04_gate_consent_idle` (gate focus is not Cancel), `wp04_stable_fields_warning` (URL Back state incorrect), `fix2_preferences_contract_preview` (footer button is missing) a `heartbeat_epoch_resume_cancel` (button missing or disabled). Rozdíl množin jmen i popisů je prázdný. Po konečném zvětšení minima Info pro nejdelší třícifernou závorku byly znovu sestaveny oba presety, spuštěny celé CTest a samostatná šestice relevantních nativních Info testů. Celé širší UI sady nebyly po této jediné změně minimální výšky potřetí opakovány.

Brány 1 až 8 mají implementaci a výše uvedenou cílenou evidenci. U brány 9 nelze tvrdit zcela zelenou širší regresi. Praktická brána 10 má skutečný SCM/Wintun/UDP důkaz a nativní 150% UI, ostatní fyzické prostředí je omezené. Dokumentační brána 11 a tento report 13 jsou dodány. Distribuční brána 12 zůstává nesplněna.

## Audit všech 35 povinných distribučních scénářů

Čísla odpovídají části „Povinné Windows integrační a lifecycle testy“ v client.md. NOT RUN u složeného scénáře znamená, že nebyl proveden **celý požadovaný rozsah**. Dílčí automatický důkaz je uveden, ale nenahrazuje úplné provedení.

| # | Stav | Rozsah, důkaz a chybějící ověření |
|---|---|---|
| 1 | NOT RUN | Skutečný bootstrap, jediný izolovaný Wintun a session E2E PASS. Čistá instalace finálního balíčku v samostatném Windows prostředí neprovedena |
| 2 | NOT RUN | Není připraven druhý uživatel Wintunu/WireGuard pro ověření souběhu |
| 3 | NOT RUN | Hash/podpis/missing/extra/architektura VerifyOnly odmítnuty a validní API ověřeno. Kompletní negativní DLL signature/API/running-version matice v izolovaném Windows neprovedena |
| 4 | NOT RUN | Reálné cykly a cleanup E2E PASS, runtime fault tests PASS. Není nativní injekce selhání v každé inicializační a teardown fázi |
| 5 | NOT RUN | Skutečný pád connected služby a reconciliation PASS. Pád v každé další fázi nebyl proveden |
| 6 | NOT RUN | Sleep/hibernate/resume v jednotlivých blokovaných stavech vyžaduje připravené systémové prostředí |
| 7 | NOT RUN | Windows upgrade a restart tohoto pracovního stroje nebyly provedeny |
| 8 | NOT RUN | VerifyOnly validní a šest negativních kopií PASS bez změn instalace. Skutečný install/upgrade/same-version upgrade/autostart/uninstall/RemoveData finálního balíčku neproveden |
| 9 | NOT RUN | Queue/FIFO/backpressure unit PASS. Skutečný overflow obou 4MiB ringů a paměťová bezpečnost ve všech stavech neověřeny |
| 10 | NOT RUN | E2E zachování výchozí sítě PASS. Souběh s další VPN a úplná matice kolizí v běžícím systému neprovedeny |
| 11 | NOT RUN | Uninstall s jinou aplikací používající Wintun nebyl proveden |
| 12 | PASS | E2E ověřuje vlastní IP/on-link routu s přesným LUID a zachování default routes/DNS, cleanup beze zbytků |
| 13 | NOT RUN | Loopback relay je dosažitelný během skutečného tunelu. Externí relay přes původní fyzické rozhraní nebyl samostatně ověřen před/během/po |
| 14 | NOT RUN | Preflight a síťové modely odmítají kolize. Není úplná nativní injekce všech cizích adres/prefixů/rout/adaptérů |
| 15 | NOT RUN | Ownership unit a skutečný crash/reconciliation PASS. Všechny planned/applied crash body nebyly nativně injektovány |
| 16 | NOT RUN | Souběžné Connect runtime a jediná síťová role PASS. Dvě tray relace a druhá SCM instance finální instalace nebyly provedeny |
| 17 | NOT RUN | Dvě současné interaktivní uživatelské relace nejsou připraveny |
| 18 | NOT RUN | Úplné odhlášení/pád klientů ve všech stavech a služba bez přihlášeného uživatele neprovedeny |
| 19 | NOT RUN | Licence jsou v balíčku, nativní prostředí 150 %. Kompletní světlo/tma/DPI matice všech tray ikon nebyla vizuálně provedena |
| 20 | NOT RUN | Skutečný resize ze všech hran při 150 % a příslušné UI regrese PASS. Fyzické 100 % a změny monitorů/DPI nebyly provedeny |
| 21 | FAIL | Skutečný paint/countdown a modely PASS, ale test výchozí výšky progress selhává i na nezměněném HEAD |
| 22 | PASS | SafeCatalog, bootstrap klasifikace, native FailureFreezes a bezpečné detaily PASS v unit/runtime/wp03 |
| 23 | NOT RUN | Nativní close bez Disconnect a nezávislý status test PASS. Dopad na druhou interaktivní relaci nebyl proveden |
| 24 | PASS | Worker handoff + retry + concurrent counters, invariant total=empty+payload, runtime a traffic testy PASS |
| 25 | PASS | Přesné body bytes a WinHTTP partial/oversize/status/error accounting testy PASS |
| 26 | PASS | Skutečné TX/RX a ověřené queue admission, odmítnuté dávky a pakety, runtime testy a UDP E2E PASS |
| 27 | PASS | New connect/idempotence/recovery/disconnect accounting runtime testy a reálný restart služby PASS |
| 28 | NOT RUN | uint64 hranice a konzistence souběžných snapshotů PASS. Dvě interaktivní relace nebyly testovány |
| 29 | PASS | Simultaneous status reads při živém ověřeném provozu a nativní Statistics/nezávislý polling PASS |
| 30 | PASS | Health syntax před kryptografií, recovery a atomické checkpoint reset testy PASS |
| 31 | PASS | Bootstrap timeout/error phase classification a bezpečné IPC/modely PASS |
| 32 | PASS | WinHTTP partial body timeout/cancel/reset/oversize a disconnect closing-accounting PASS |
| 33 | PASS | Worker handoff/retry test ověřuje účtování každého deferred pokusu při předání, PASS |
| 34 | PASS | Přesné uint64, sčítání nad UINT64_MAX, decimal rounding, nativní stale/focus/scroll Statistics PASS |
| 35 | NOT RUN | Nezávislé pollování, jediný config read a nativní resize/undo PASS. Kompletní fyzická změna DPI a všechny části složeného scénáře nebyly provedeny |

Instalační a upgrade ACL kontrola celého datového stromu, kopírování config bez UAC, odmítnutí přímého zápisu a uložení přes Edit Config/Wizard je **NOT RUN jako úplná instalační matice**. E2E potvrzuje jen svůj izolovaný bootstrap a bezpečný životní cyklus. Nebyl použit osobní config ani skutečný RemoveData na produkční instalaci. Tyto chybějící scénáře vyžadují připravené izolované Windows/VM prostředí, nejsou implicitně prominuty uživatelem.

## Distribuční audit

Nový strom byl připraven odděleně. Před přesunem byly ověřeny absolutní cesty, příslušnost k `C:\git\phpvpn` a nepřítomnost reparse bodů. Předchozí dist byl přesunut mimo cílový strom, nikoli překryt novými soubory. Konečný strom obsahuje 17 souborů, tedy server, 14 Windows souborů a po jednom pravdivém README pro macOS/Linux. Tyto platformy nejsou vydávány za podporované.

Serverová kopie je bajtově shodná s aktuálním kořenovým phpvpn.php. Všechny tři EXE i originální nepřejmenovaný wintun.dll jsou AMD64/0x8664. Imports mají jen Windows systémové závislosti, viz [package-dependencies.log](C:/git/phpvpn/work/polling-verification/package-dependencies.log). Balíček neobsahuje testovací EXE, privátní klíč, provozní config, fronty ani samostatné Wintun SYS/CAT/INF.

SHA-256 serveru je `2E9FF3D8BADCAE652E836C68F1E88DFADC3CF7E5E1D78761049A92FC68AEECCD`, manifestu `49CD75F8C6D62703DC85CDF788014585898E78CFCD99C60D4F6C5C66AAE53E8F`. [final-freshness.log](C:/git/phpvpn/work/polling-verification/final-freshness.log) potvrzuje skutečné porovnání serverových bajtů i shodu všech distribuovaných binárek s aktuálním release buildem.

Manifest je jednořádkový UTF-8 bez BOM, manifestVersion 1, architektura x64. Je ověřen RSA-PKCS1-SHA256 přes přesné bajty. Všechny seznamy, velikosti a SHA-256 odpovídají konečnému obsahu. Manifest, podpisový záznam, veřejný certifikát a očekávaný pin mají identitu `7430CCA24F5DE8A6614D582AFBFED7C80E0F2CA8`, `CN=phpvpn Development Test Signing`. Jde o **vývojový podpis**, nikoli veřejně důvěryhodné vydavatelské vydání. EXE nemají dodatečný Authenticode podpis, volba SignBinaries nebyla použita. Původní Wintun má platný podpis WireGuard.

VerifyOnly byl spuštěn nad stagingem i finální cestou a ověřil také povinné API. Negativní izolované kopie pokrývají změněný hash, poškozený podpis, chybějící soubor, změněnou PE architekturu, nadbytečný EXE a nesoulad cílové OS architektury. Změněná PE architektura se odmítne již na hash gate, samostatný případ s platným balíčkem a testově nastavenou OS architekturou ARM64 ověřuje architekturní gate. Není to test podepsaného nesprávně sestaveného vydání. Produkční služba, config, instalované soubory a default routes byly před/po VerifyOnly shodné.

## Dokumentace a další krok

Aktualizovány client.md, tray_UX.md, protocol.md, manuální/UI/E2E README a integrační README. Popsán rychlý polling, skutečný Save consent, raw serverový pretty plaintext, Auto refresh a patička. Kanonický kompaktní request E, NDJSON a kryptografické vstupy mimo info plaintext zůstaly beze změny. distribution.md a distribuční nástroje byly posouzeny, jejich změna nebyla nutná. Graft byl obnoven a `git diff --check` prošel.

Před vydáním zbývá připravit izolované Windows prostředí pro označené NOT RUN scénáře a vyřešit doložená selhání širších UI sad. Žádný PASS z tohoto reportu nenahrazuje tyto zbývající kroky. Další systémové zásahy do produkční instalace nebyly provedeny.

Pomocné PHP procesy na 18143, 18145 a 18093 byly po dokončení potřebných testů ukončeny až po ověření PID, executable path, portu a vlastní fixture cesty. Provozní Nginx a PHP procesy nebyly zastaveny.

Úvodní pokus nového copy testu přepsal clipboard syntetickým textem před doplněním jeho zálohování. Následné běhy už obsah zálohují a obnovují pomocí ClipboardSnapshot. Původní obsah z prvního pokusu není v dostupné evidenci zachován.

## Trvalý podklad pro další intent

Všechna známá selhání, validační mezery, reprodukce, dopady a doporučené kroky jsou samostatně v [reportu potíží](C:/git/phpvpn/Reports/2026-09-14-polling-ui-validation-issues.md). Nesouvisející opravy nebyly přidány do této realizace.

## Změněné implementační soubory, testy a dokumentace

- [client.md](C:/git/phpvpn/client.md)
- [client/apps/tray/contracts_window.cpp](C:/git/phpvpn/client/apps/tray/contracts_window.cpp)
- [client/apps/tray/contracts_window.h](C:/git/phpvpn/client/apps/tray/contracts_window.h)
- [client/apps/tray/server_info_layout.h](C:/git/phpvpn/client/apps/tray/server_info_layout.h)
- [client/apps/tray/server_info_window.cpp](C:/git/phpvpn/client/apps/tray/server_info_window.cpp)
- [client/apps/tray/server_info_window.h](C:/git/phpvpn/client/apps/tray/server_info_window.h)
- [client/apps/tray/system_dialog.cpp](C:/git/phpvpn/client/apps/tray/system_dialog.cpp)
- [client/apps/tray/system_dialog.h](C:/git/phpvpn/client/apps/tray/system_dialog.h)
- [client/apps/tray/tray_application.cpp](C:/git/phpvpn/client/apps/tray/tray_application.cpp)
- [client/apps/tray/ui_style.h](C:/git/phpvpn/client/apps/tray/ui_style.h)
- [client/src/core/runtime/network_worker.cpp](C:/git/phpvpn/client/src/core/runtime/network_worker.cpp)
- [client/src/core/runtime/network_worker.h](C:/git/phpvpn/client/src/core/runtime/network_worker.h)
- [client/src/core/runtime/vpn_engine.cpp](C:/git/phpvpn/client/src/core/runtime/vpn_engine.cpp)
- [client/src/core/runtime/vpn_engine.h](C:/git/phpvpn/client/src/core/runtime/vpn_engine.h)
- [client/tests/contract/test_server_contract.cpp](C:/git/phpvpn/client/tests/contract/test_server_contract.cpp)
- [client/tests/e2e/README.md](C:/git/phpvpn/client/tests/e2e/README.md)
- [client/tests/e2e/service_e2e.cpp](C:/git/phpvpn/client/tests/e2e/service_e2e.cpp)
- [client/tests/manual/README.md](C:/git/phpvpn/client/tests/manual/README.md)
- [client/tests/ui/README.md](C:/git/phpvpn/client/tests/ui/README.md)
- [client/tests/ui/tray_ui_tests.cpp](C:/git/phpvpn/client/tests/ui/tray_ui_tests.cpp)
- [client/tests/unit/test_runtime_status.cpp](C:/git/phpvpn/client/tests/unit/test_runtime_status.cpp)
- [client/tests/unit/test_server_info_layout.cpp](C:/git/phpvpn/client/tests/unit/test_server_info_layout.cpp)
- [client/tests/unit/test_tray_palette.cpp](C:/git/phpvpn/client/tests/unit/test_tray_palette.cpp)
- [client/tests/unit/test_wp05_windows.cpp](C:/git/phpvpn/client/tests/unit/test_wp05_windows.cpp)
- [client/tests/unit/wp03_dialog_test.h](C:/git/phpvpn/client/tests/unit/wp03_dialog_test.h)
- [phpvpn.php](C:/git/phpvpn/phpvpn.php)
- [protocol.md](C:/git/phpvpn/protocol.md)
- [tests-info-extensions.php](C:/git/phpvpn/tests-info-extensions.php)
- [tests-integration/README.md](C:/git/phpvpn/tests-integration/README.md)
- [tests-integration/phpvpn_client.py](C:/git/phpvpn/tests-integration/phpvpn_client.py)
- [tests-integration/test_info.py](C:/git/phpvpn/tests-integration/test_info.py)
- [tests-integration/test_info_extensions.py](C:/git/phpvpn/tests-integration/test_info_extensions.py)
- [tests.php](C:/git/phpvpn/tests.php)
- [tray_UX.md](C:/git/phpvpn/tray_UX.md)
- [client/tests/unit/clipboard_snapshot.h](C:/git/phpvpn/client/tests/unit/clipboard_snapshot.h)
- [tests-info-pretty-boundaries.php](C:/git/phpvpn/tests-info-pretty-boundaries.php)

Dále byly na výslovnou žádost uživatele přidány dva trvalé reporty v `C:/git/phpvpn/Reports`. Původní uživatelský marker, intenty a plány do tohoto seznamu změn nepatří.
