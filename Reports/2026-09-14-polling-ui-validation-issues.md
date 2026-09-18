# Potíže a zbývající validace pro navazující intent

Datum: 14. 9. 2026, Europe/Berlin. Tento samostatný report zachycuje všechny potíže zjištěné při realizaci `Plans/2026-09-13-22-49-34/plan-v002.md` podle `Intents/2026-09-13-22-49-34/intent-v007.md`. Je podkladem pro budoucí nový intent, **neschvaluje ani neprovádí nesouvisející opravy**.

Aktuální změna opravuje pětisekundové rychlé pollingové okno, potvrzení odpojení po Save Contracts, Server Information a pretty plaintext serveru. Nové cílené testy, debug/release CTest a skutečný SCM/Wintun/UDP test prošly. Přesto nelze distribuční kandidát označit za úplně ověřené vydání.

## Prostředí a síla důkazů

- Repozitář `C:\git\phpvpn`, aktivní větev `tray_UX`, základní commit `d6c9c8a3ed1eee2698c0b389e03e9b5bf58af014`.
- Windows 11 Pro x64 10.0.22631, skutečné DPI 144, tedy 150 %. MSVC 14.44, Windows SDK 10.0.22621.0, CMake/Ninja.
- Nezměněný základ byl získán přes git archive a samostatně sestaven v ověřovací kopii mimo zdrojové soubory. Nešlo o worktree. V této kopii nebyly implementační změny.
- Plná wp03 sada: základ 35 testů / 10 FAIL, upravený debug i release 36 / 10 FAIL. Deset názvů selhání se přesně shoduje.
- Plná tray sada: základ 44 / 5 FAIL, upravený debug i release 45 / 5 FAIL. Shodují se názvy i popisy všech pěti selhání.
- Závěrečná cílená sada Server Information má v debug i release 6 / 0 FAIL. Konečné minimum 320 × 480 DIP ověřuje i nejdelší třícifernou závorku. Širší UI sady nebyly po poslední změně samotné minimální výšky potřetí opakovány.
- Výsledek testu, který skončil na předpokladu fixture, **není důkazem stejné vady v produkčním UI**. Je důkazem chybějící spolehlivé regresní kontroly. Příčiny níže nebyly v tomto rozsahu diagnostikovány až ke kořeni.

Úplná místní evidence je v `C:\git\phpvpn\work\polling-verification`. Tento pracovní adresář je ignorovaný Gitem, proto report uvádí podstatné výsledky přímo a reprodukci nezakládá jen na jeho trvalé existenci. Souhrn a matice všech 35 scénářů jsou také v `Reports/2026-09-14-polling-ui-implementation-audit.md`.

## Reprodukce automatických selhání

V aktuální větvi, bez jiné současně běžící nativní UI sady, z kořene projektu:

```powershell
& client/tools/build-x64.ps1 -Preset debug -UiTests
& work/build/debug/tests/phpvpn-wp03-ui-tests.exe
& work/build/debug/tests/phpvpn-tray-ui-tests.exe
& client/tools/build-x64.ps1 -Preset release -UiTests
& work/build/release/tests/phpvpn-wp03-ui-tests.exe
& work/build/release/tests/phpvpn-tray-ui-tests.exe
```

Jeden scénář se vybere předáním níže uvedeného názvu nebo jeho jednoznačné části jako jediného argumentu odpovídajícího EXE. Nulový výběr není PASS. Pro první diagnostiku používat stejnou interaktivní relaci při 150 % DPI. Během měření neměnit focus jinými aplikacemi ani otevírat UAC.

Pro kontrolní základ exportovat přesně uvedený commit pomocí git archive do nového vlastního ověřovacího adresáře mimo `dist`. V aktivovaném MSVC AMD64 prostředí použít `cmake -S <kopie>/client -B <nový-build> -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=cl.exe -DBUILD_TESTING=ON -DPHPVPN_BUILD_UI_TESTS=ON`, sestavit targety `phpvpn-wp03-ui-tests` a `phpvpn-tray-ui-tests` a spustit stejným způsobem. Neprovádět reset aktivní větve ani přepis uživatelských souborů. Konkrétní již vytvořená kopie je `C:\git\phpvpn\work\polling-verification\baseline-source`, build `C:\git\phpvpn\work\polling-verification\baseline-build`.

### Deset již existujících selhání wp03

Pro všech deset je klasifikace stejná: selhání je doložené už na nezměněném základním commitu, nelze je připsat jako nově vzniklou regresi této úpravy. Dopad v produkci je třeba ověřit samostatně. Doporučená první priorita je obnovit spolehlivost fixture a rozlišit chybu testu od chyby aplikace, nikoli přepsat očekávání tak, aby sada pouze zezelenala.

| ID | Scénář a konkrétní důkaz | Dopad a doporučený postup |
|---|---|---|
| UI-01 | `WP03Windows.DefaultProgressHeightsFitEveryVisiblePanelWithLightBottomGap`. Nesouhlasí `client.bottom == p(expected.height)` a mezera `client.bottom - panelBottom <= p(24)`. Základ `test_wp03_windows.cpp:483,489` | Nesoulad požadované výšky nebo skutečné geometrie progress. Samostatně proměřit každý panel, dostupnou pracovní plochu a DPI. Teprve potom rozhodnout mezi opravou layoutu a zastaralé testovací specifikace |
| UI-02 | `WP05Windows.HttpDiagnosticMaximumProjectionTabsAndOwnerDestruction`. Nesouhlasí kopírovaná hodnota `std::wstring(value) == L"\\x00"`, základ `test_wp05_windows.cpp:327` | Neprůkazná kontrola projekce diagnostického textu, výběru nebo copy. Zjistit skutečnou projekci NUL, rozsah výběru a formát clipboardu. Zachovat testování destrukce vlastníka |
| UI-03 | `WP05Windows.OriginalDeadlineExitWaitAndIndependentEditor`. Výjimka `WP05 prerequisite failed`. Selhává i samostatný běh | Test nedojde k průkaznému ověření původní deadline a nezávislého editoru. Najít konkrétní selhaný předpoklad a přidat diagnostiku fixture bez klíčů či raw configu |
| UI-04 | `WP05Windows.FailedCheckBlocksRepeatAndMissingRequiresFreshConfirmation`. `WP05 prerequisite failed` | Chybí spolehlivý důkaz zákazu opakovaného Save a nového souhlasu po missing. Opravit vstupní stav testu, poté ověřit obě negativní větve |
| UI-05 | `WP05Windows.CommonExitRevalidatesAllDraftsAfterModalReentry`. `WP05 prerequisite failed` | Neověřená ochrana všech konceptů při návratu z modality. Nejprve zprovoznit více živých editorů v izolované fixture, potom ověřit generace a jeden společný souhlas |
| UI-06 | `WP05Windows.ReleaseResourcesAccessibilityAnd252RowTiming`. `WP05 prerequisite failed` | Chybí úplný nativní důkaz resource/accessibility chování a času vytvoření 252 řádků. Porovnat velikost CIDR, dokument fixture, dostupnost ovladačů a předpoklady testu |
| UI-07 | `WP05Windows.RealSessionBroadcastOnIsolatedDesktopNeverWaitsForReaders`. `WP05 prerequisite failed` | Neprůkazná kontrola skutečného session broadcastu a neblokujícího ukončení readerů. Obnovit izolovaný desktop a ověřit, zda selhává jeho příprava, otevření okna nebo samotný lifecycle |
| UI-08 | `WP05Windows.SmallCidrResizeTabOrderAndVisualCapture`. Nesouhlasí `GetFocus() == key` a později `GetFocus() == field(root,1002)`, základ řádky 648 a 669 | Možný nesoulad focusu, tab order nebo testovacího vstupu pro malý CIDR. Reprodukovat bez externích oken, logovat bezpečné control ID a generaci při jednotlivých krocích |
| UI-09 | `WP05Windows.RandomFailureAndPartialCreateFailureRetainSafeState`. Očekávané selhání `window.open` nenastane, lifecycle zůstává open a existuje `PhpVpnContracts`. Později nesouhlasí velikost požadavku random 32 a text chyby | Fault injection nebo cleanup neodpovídá testu. Nejprve ověřit místo injekce a počet native create operací, potom zachování starého klíče a úplné zavření částečně vytvořeného okna. Samotný log nedokazuje ztrátu klíče v produkci |
| UI-10 | `WP05Windows.KeyInputLimitAndIndependentEditorsDoNotRebase`. Nesouhlasí délka textu 128 a výběr 3 až 3, následuje `WP05 prerequisite failed`, základ řádky 704 a 707 | Neprůkazná hranice vstupu a zachování nezávislých konceptů. Porovnat vytvořený textbox, očekávaný limit a mechanismus vkládání. Zachovat atomické odmítnutí nadlimitního vstupu |

Podrobné logy: `ui-wp03-baseline.log`, `ui-wp03-debug.log`, `ui-wp03-release.log`, `ui-original-deadline-isolated.log`, `ui-baseline-comparison.log` ve výše uvedeném pracovním adresáři. Čísla zdrojových řádků výše se vztahují k nezměněnému základnímu commitu, nikoli k později doplněným testům.

### Pět již existujících selhání tray UI

Také těchto pět selhání je shodných na nezměněném základním commitu i upravených debug/release sestavách. Nejde o nově doloženou regresi aktuální úpravy. Některé mohou mít společnou příčinu s wp03 fixture, což je pouze pracovní hypotéza.

| ID | Přesný filtr a chyba | Dopad a doporučený postup |
|---|---|---|
| UI-11 | `modern_toggle_switches`: `maximum 252 native rows missing` | Nedokončený důkaz toggle/accessibility chování na všech řádcích Contracts. Porovnat očekávaný CIDR a nativní strukturu fixture. Nový Auto refresh má vlastní úspěšný test role, jména, stavu, Space a Tab |
| UI-12 | `wp04_gate_consent_idle`: `gate focus is not Cancel` | Nesoulad počátečního focusu gate. Reprodukovat samostatně a rozlišit změnu specifikace od aktivace jiným oknem. Stejný výsledek nastal i bez původního UAC v kontrolním běhu |
| UI-13 | `wp04_stable_fields_warning`: `URL Back state incorrect` | Neprůkazné zachování stavu Back na URL. Zjistit skutečný enabled stav, předchozí transition a odpovědi fixture |
| UI-14 | `fix2_preferences_contract_preview`: `footer button is missing` | Test hledá neexistující tlačítko nebo je footer skutečně neúplný. Zaznamenat control ID a aktuální stránku, porovnat s platnou specifikací a nativním stromem |
| UI-15 | `heartbeat_epoch_resume_cancel`: `button missing or disabled` | Nedokončená kontrola heartbeat/epoch/resume/cancel. Určit konkrétní očekávané tlačítko a stavový přechod. Neodstraňovat ochranu vůči zastaralé epoše kvůli průchodu testu |

Logy: `ui-tray-baseline-full.log`, `ui-tray-debug-full.log`, `ui-tray-release-full.log`, `ui-toggles-baseline.log`, `ui-baseline-comparison.log`.

## Neprovedené a omezené validační scénáře

Tyto mezery nejsou automaticky chybou implementace a nejsou regresí způsobenou aktuální změnou. Znamenají chybějící důkaz. Uživatel je neprominul. Brání označení kandidáta za úplně ověřenou distribuci. Čísla odkazují na všech 35 povinných scénářů v client.md, jejich úplná PASS/FAIL/NOT RUN matice je v realizačním auditu.

| ID | Chybějící rozsah | Existující důkaz, dopad a doporučený další postup |
|---|---|---|
| VAL-01 | Čistý install, běžný i same-version upgrade, autostart, restart, uninstall se zachováním dat a explicitní RemoveData finálního balíčku. Scénáře 1 a 8 | VerifyOnly a AST/simulovaný installer lifecycle PASS, skutečný izolovaný SCM bootstrap PASS. Není to instalace balíčku. Připravit snapshotovatelnou Windows VM a provést celou sekvenci bez osobního configu |
| VAL-02 | Instalační i upgrade ACL celého datového stromu včetně záloh/logů, oprava při startu, read/copy bez UAC, zákaz přímého zápisu, podporované Save | E2E má jen vlastní izolované úložiště. Ve VM ověřit efektivní ACL pod běžným uživatelem i po upgradu a restartu, uchovat bezpečné výpisy bez obsahu klíčů |
| VAL-03 | Souběh s WireGuard/jiným Wintunem nebo VPN a uninstall za jeho běhu. Scénáře 2, 10 a 11 | E2E zachoval cizí výchozí síť a uklidil vlastní adaptér. Druhá aplikace nebyla připravena. Ověřit funkčnost druhé VPN před/během/po a zákaz mazání jejího adaptéru či ovladače |
| VAL-04 | Úplná negativní signature/DLL/API/running-driver-version matice. Scénář 3 | Platný Wintun podpis a povinné API PASS, pozměněné balíčky odmítnuty. Neproběhl každý nativní případ chybějícího exportu či nepodporované běžící verze. Ve VM injektovat odděleně s ověřením nulové síťové mutace |
| VAL-05 | Každá init/teardown chyba, pád v každé fázi a všechny planned/applied reconciliation body. Scénáře 4, 5 a 15 | Runtime fault tests a skutečný connected crash/recovery PASS. Doplnit řízené nativní injekce a identitu vlastněných prostředků. Nezobecňovat jeden úspěšný crash na všechny body |
| VAL-06 | Sleep/hibernate/resume při blokovaném receive, HTTP requestu a plném ringu. Scénář 6 | Tento E2E je výslovně netestuje. Použít připravený systém, měřit probuzení, deadlock/busy-loop a zachování prostředků |
| VAL-07 | Podporovaný Windows upgrade a restart. Scénář 7 | Neprovedeno na pracovním stroji. Ověřit na snapshotu VM v dohodnutých verzích Windows |
| VAL-08 | Skutečný overflow receive/send 4MiB ringů a paměťová bezpečnost. Scénář 9 | Queue/FIFO/backpressure unit PASS. Doplnit řízenou zátěž reálného Wintunu, stop wakeup a paměťovou instrumentaci |
| VAL-09 | Úplné nativní kolize cizí adresy/prefixu/routy/adaptéru. Scénáře 10 a 14 | Preflight a modely chrání kolize, není kompletní živá injekční matice. V izolované síti porovnat celé snapshoty před/po odmítnutí |
| VAL-10 | Externí relay přes původní fyzické rozhraní před/během/po tunelu. Scénář 13 | Skutečný E2E používá loopback relay. Ověřit externí dosažitelnost a routu bez zavedení kill switche nebo změny DNS |
| VAL-11 | Druhá SCM instance, dvě interaktivní tray relace, shodné čítače a stav, odhlášení/pád libovolného klienta. Scénáře 16, 17, 18, část 23 a 28 | Souběžný runtime Connect a konzistentní uint64 snapshoty PASS. Dva současné přihlášené uživatele nejsou připravené. Doplnit VM nebo host s dvěma relacemi a zachováním služby bez přihlášeného uživatele |
| VAL-12 | Fyzické DPI 100/125/200 %, přesun mezi monitory, světlo/tma/high contrast, skutečná čtečka obrazovky. Scénáře 19, 20 a část 35 | Nativní 150 %, unit geometrie 96/120/144/192 a native MSAA/klávesnice PASS. To nenahrazuje čtečku ani migraci monitorů. Testovat přesné buildy a uchovat verze OS/DPI a bezpečné screenshoty |
| VAL-13 | Praktická dvojice plných klientů/VM a druhý Wintun adaptér | E2E prokázal skutečný UDP a polling se službou `.2` a produkčním NetworkWorker peerem `.3`, který adaptér nevytváří. Doplňující dvouklientský ping/idle/restart test provést až v dostupném odděleném prostředí |
| VAL-14 | Podpora LF/CRLF zobrazení na starších Windows | Použité EM_SETEXTENDEDSTYLE je dokumentováno od Windows 10 1809 a bylo ověřeno na Windows 11. Není nový důkaz pro starší Windows. V navazujícím intentu vyjasnit minimální podporovanou verzi a případný fallback bez změny autentizovaného textu |
| VAL-15 | 503 contention větev integrační Python sady | Celá sada má 392 PASS / 2 SKIP. Profilový skip byl nahrazen samostatným PASS pro všech 14 identit, 503 contention však nebyla pozorována. Připravit deterministicky držený lock/worker a samostatně ověřit tuto větev |
| VAL-16 | Zelená úplná UI regrese | Výše uvedených 15 selhání je reprodukováno na základním commitu. Nejprve diagnostikovat fixture/prerequisites a až následně případné produkční vady. Nesmí se označit za PASS jen proto, že nevznikly v této změně |

## Důvěra a omezení distribučního ověření

Kandidát je `C:\git\phpvpn\dist`, Windows x64 část `C:\git\phpvpn\dist\client\Windows\x64`, klient 1.0.0, Wintun 0.14.1. Všechny povinné soubory, přesné hashe a architektury byly ověřeny. Manifest má platný RSA-PKCS1-SHA256 podpis s pinem `7430CCA24F5DE8A6614D582AFBFED7C80E0F2CA8`, `CN=phpvpn Development Test Signing`.

Je to **vývojový podpis**, nikoli veřejně důvěryhodné vydavatelské podepsání. Dodatečný Authenticode podpis EXE nebyl použit. Původní wintun.dll má platný podpis WireGuard. Pro veřejné vydání je třeba samostatně určit důvěryhodnou podpisovou identitu, distribuci očekávaného pinu a požadavek na EXE podpis. V aktuálním rozsahu se neměnil trust store ani neexportoval privátní klíč.

Negativní kopie se změněnou PE architekturou byla odmítnuta již na hash gate. Samostatně prošel test odmítnutí platného x64 balíčku při testově nastavené OS architektuře ARM64. Tyto výsledky nejsou důkazem, že installer odmítne každou chybně vytvořenou, ale správně podepsanou směs architektur. Make-dist/VerifyOnly historicky primárně porovnávají service a Wintun. V tomto běhu byly nezávisle ověřeny **všechny tři EXE i DLL**, všechny mají 0x8664. Navazující intent může doplnit automatický požadavek této kontroly na všech binárkách a samostatné podepsané negativní fixture. Nesmí se kvůli testu přijmout nedůvěryhodná náhrada Wintunu.

## Potíže vzniklé a vyřešené při této realizaci

Tyto položky se nemají znovu zadávat jako dosud neopravené produkční vady, ale jsou důležité pro reprodukci a další údržbu.

| ID | Zjištění, řešení a zbývající dopad |
|---|---|
| RUN-01 | Původní polling regrese měla FAIL: po historickém TX a prázdném pollu v 5000 ms interval 80 místo 160 ms. Opraveno. Deterministický PASS i skutečný SCM/Wintun idle backoff a nový UDP návrat na minimum jsou doložené v realizačním auditu |
| RUN-02 | Původní obyčejný EDIT přesný LF text zachoval, ale nezalomil. Odhalil to nový EM_GETLINECOUNT test. Opraveno nativními EOL styly, nikoli reserializací. LF, CRLF, copy a maximum 262144 prošly. Kompatibilitní omezení je VAL-14 |
| RUN-03 | Při minimální šířce nově zalomený nadpis/count/footer omezil viditelnost tabulky. Konečné minimum je 320 × 480 DIP a nativní test nejdelšího count labelu potvrzuje viditelný řádek. Nesměšovat se starším selháním progress UI-01 |
| RUN-04 | První Python běh selhal v jednom testu, protože built-in PHP 404 odráželo žádaný název souboru. Pouze izolovaná fixture dostala router s prázdným 404, poté 392 PASS / 2 SKIP. Produkční Nginx a nasazený phpvpn.php se neměnily. Při reprodukci použít stejný bezpečný fixture routing |
| RUN-05 | První SCM start Windows ukončil jako zrušený při UAC. Uživatelem potvrzené opakované běhy uspěly. Rozšířený běh prokázal payloadDelta=0, rostoucí polling mezery až 1594 ms a po novém UDP 10 pollů za 600 ms. Cleanup PASS, provozní služba/config/síť beze změny |
| RUN-06 | Úvodní nový copy test přepsal clipboard syntetickým textem před doplněním zálohy. Následné běhy používají ClipboardSnapshot a obnovují obsah. Původní clipboard prvního pokusu není zachován. Další UI copy testy mají tento prostředek chránit od prvního běhu |

Logy a snímky byly ponechány mimo `dist`. Pomocné PHP procesy byly po ověření vlastní identity ukončeny. Distribuční balíček není produkčně instalován, nebyla provedena skutečná odinstalace ani RemoveData, nebyl změněn produkční config. Nebyl vytvořen commit ani push.

## Doporučené rozdělení budoucího intentu

1. **Spolehlivá UI regrese.** Nejprve zprovoznit fixture a explicitní diagnostiku předpokladů, potom určit skutečné opravy pro UI-01 až UI-15. Zachovat bezpečné Save/Exit, generace a limity. Vyžadovat plnou debug i release sadu a porovnání původního stavu.
2. **Windows release validační prostředí.** Připravit snapshotovatelné VM, další Wintun aplikaci a dvě uživatelské relace. Projít všech 35 scénářů client.md, včetně instalačních ACL, upgradu a RemoveData pouze na testovacích datech.
3. **Podporované platformy a distribuce.** Výslovně určit minimální Windows, ověřit raw text podle VAL-14, všechny PE architektury a důvěryhodný vydavatelský podpis. Neprohlásit kandidát za hotové vydání před uzavřením těchto bodů.

Při zpracování nového intentu znovu porovnat aktuální kód s uvedeným základním commitem. Tento report dokumentuje konkrétní provedené běhy, není zárukou stejného výsledku na jiném stroji ani náhradou diagnózy příčin.
