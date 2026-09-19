---
plan_version: 1
created_at: 2026-09-19-15-04-00
source: ../../Intents/2026-09-19-14-39-38/intent-v001.md
---

# Realizační plán: reconnect, Server Information a instalační GUI

## 1. Závaznost a výchozí stav

Autoritativní zadání je celý [intent-v001.md](../../Intents/2026-09-19-14-39-38/intent-v001.md). Tento soubor je úplná neměnná verze plánu. Pozdější věcná změna vyžaduje novou úplnou verzi plánu. Plánování nic neimplementuje a samo nepovoluje realizaci. Koordinátor po předání získá výslovnou volbu realizace a model i úsilí pro každého nového realizačního agenta.

Průzkum proběhl přímo v `C:\git\phpvpn`, ve větvi `tray_UX`, nad HEAD `1e3ac0d221c363955f22185ab950082c964619f6`. Existovaly již změny `.intent-builder-active` a nesledovaný adresář zdrojového intentu. Patří koordinátorovi a nesmějí se zahrnout do implementace, přepsat ani uklidit. Při zahájení každého bloku znovu zaznamenat skutečný HEAD, větev a pracovní změny. Čísla řádků níže jsou orientační kotvy z tohoto HEAD, názvy symbolů jsou trvalejší navigace.

Před průzkumem použít `graft map`, potom cílené `graft ask ... --source`, `graft skeleton` a pro úplný výskyt `graft grep`. Nezakládat worktree, neměnit větev, nevytvářet subagenty ani další tasky. Oba realizační agenti budou pracovat postupně v téže aktivní feature větvi. Žádný agent nezačne druhý blok, dokud první agent nepředá splněnou bránu a neukončí zápisy i testovací procesy.

Pro současný delegovaný strom platí absolutní termín **2026-09-19 17:09 Europe/Berlin**, případnou žádost o prodloužení předává koordinátor nejpozději v 16:39. Automatické vynucení není garantováno, čas se kontroluje aktivně. Pokud budou realizační agenti pokračovat ve stejném stromu, dědí tento termín a musí ho před začátkem potvrdit. Po uplynutí mohou začít či pokračovat jen po výslovném prodloužení nebo stanovení neomezené doby koordinátorem podle pokynu uživatele.

## 2. Rozdělení vlastnictví a sekvenční předání

| Oblast | Blok 1 | Blok 2 |
| --- | --- | --- |
| Reconnect, Contracts, Edit Config, Server Information | Implementace a úplné klientské testy | Jen integrační regrese, případná doložená oprava zjištěné chyby |
| `client/apps/CMakeLists.txt`, `client/tests/CMakeLists.txt` | Beze změny | Výhradní vlastník nových targetů, resources a registrace testů |
| Ostatní CMake soubory a presets | Beze změny | Změna pouze při prokázané nutnosti |
| `client/tools/make-dist.ps1`, `verify-package.ps1`, package skripty, dokumentace distribuce | Beze změny | Výhradní vlastník |
| Existující klientské testovací `.cpp` soubory | Rozšíření existujících suites | Spustit znovu, nepřepisovat nové testy |
| Nové `client/apps/package_gui/`, testy obalu a PE resources | Žádné | Výhradní vlastník |
| `graft/` | Refresh po větší změně před předáním | Navázat a refresh po vlastní změně |

Aby první blok skutečně prošel samostatným buildem bez změn společného CMake, sdílenou reconnect politiku umístit do nového headeru `client/apps/tray/reconnect_controller.h`, malé čisté části definovat inline a aplikační orchestrace doplnit do již kompilovaného `tray_application.cpp`. Testy doplnit do již registrovaných souborů. Není to důvod vytvářet velkou headerovou implementaci GUI. Win32 obsluha zůstane v existujících `.cpp`.

Potenciálně konfliktní soubory jsou zejména `client/apps/CMakeLists.txt`, `client/tests/CMakeLists.txt`, `client/apps/tray/window_utils.*`, `client/tests/ui/tray_ui_tests.cpp` a generované uzly `graft/`. První dva vlastní pouze blok 2. Blok 2 využije stávající geometrické helpery bez změn, případné nové chování bude lokální v package GUI. Blok 2 se vyhne klientskému UI testovacímu souboru a použije vlastní testovací host. Graft se obnovuje až po ukončených změnách každého bloku, nikdy současně.

Předání tvoří malý samostatný report `Reports/2026-09-19-14-39-38/block-1-handoff-v001.md`, který první realizační agent nově vytvoří. Není-li možné předat čistý implementační commit v rámci autorizace realizace, uvede přesný seznam svých pracovních změn a jejich diff. Druhý agent převezme tento stav beze stash, resetu, cherry-picku nebo založení jiné větve. Odlišný HEAD či další změny nejprve vyhodnotí podle předávacího reportu, nikoli automatickým přepsáním. Report ani plán nesmějí obsahovat tajnou konfiguraci.

## 3. Blok 1: klientská část

### Vstup a rozsah

Vstupem je tento plán, celý zdrojový intent, aktuální větev a nezměněná build/package infrastruktura. Cílem jsou akceptační body intentu 1 až 5 a klientská část bodu 11. Vlastní instalace, package resources a PowerShell skripty jsou mimo tento blok.

### Ověřené kotvy a dotčené soubory

| Soubor a symbol | Zjištění a plánovaná změna |
| --- | --- |
| `client/apps/tray/contracts_window.cpp:301-344`, `poll`, `finishSavedStatus`, odpovídající `.h` | `saved` zahajuje čtení stavu a dialog. `matched` má vlastní větev. Přesměrovat potvrzené uložení do společné orchestrace, zachovat lifecycle kontroly |
| `client/apps/tray/editor_window.cpp:85-108`, `finishSave`, odpovídající `.h` | Potvrzený save dnes vždy ukazuje starý informační text. Nahradit předáním potvrzené operace společné orchestraci |
| `client/apps/tray/tray_application.cpp:42-56,210-352,445-510`, `.h` | Wiring callbacks, `onSnapshot`, `updateIcon`, `startCommand`, `finishCommand`, `finishDisconnect`, `requestExit`, `endSession`, obsluha aplikačních zpráv |
| Nový `client/apps/tray/reconnect_controller.h` | Malý testovatelný stavový automat, identita, generační token a přesný sdílený dialogový text |
| `client/apps/tray/progress_window.cpp:105-124,213-228`, `.h` | Integrace s `onDisconnectReply`, timeout a callback `disconnectSnapshot`. Měnit jen pokud to vyžaduje bezpečné rozlišení zdroje nebo lifecycle |
| `client/src/core/presentation/server_info_model.h:11-15`, `.cpp:18-49` | Přidat `onlineClients`, zachovat `validOnlineClients`, případně společný čistý formatter souhrnu |
| `client/apps/tray/server_info_window.cpp:236-294` | `display` používá jeden souhrnný text pro titulek a vnitřní souhrn |
| `client/tests/unit/test_tray_application.cpp`, `test_wp03_save_windows.cpp`, `test_wp05_windows.cpp` | Testy politiky a skutečné produkční orchestrace přes soukromý IPC backend |
| `client/tests/unit/test_server_info_model.cpp`, `test_server_info_layout.cpp`, `client/tests/ui/tray_ui_tests.cpp` | Počty, pluralizace, refresh, stránkování a rozměry textu |

Read-only důkazy služby: `client/src/core/runtime/vpn_engine.cpp:248-316,607-621` a `client/src/core/ipc/ipc_dispatcher.cpp:49-63`. Connect načítá čerstvou konfiguraci. Disconnect pouze zařadí příkaz a jeho IPC odpověď nemusí znamenat hotový teardown. Dokončený Disconnect zachová `connectionEpoch` a `connectAttemptId`, ale vynuluje `connectionStartedAtUtc`. `applyStatusSnapshot` v `status_model.cpp:133-137` samo neodmítá starší validní snapshot, proto ochranu nového toku nelze delegovat na tuto cache.

### Rozhodnutí pro sdílenou orchestrace

Orchestraci vlastní `TrayApplication`. Obě okna oznamují pouze vlastní potvrzený save spolu s hodnotovou identitou ukládací operace a kontrolou platnosti otevřeného sezení. Neukládají trvalý příznak typu „při příštím disconnect připoj“. Společný tok má fáze `none`, `readingSavedStatus`, `confirming`, `revalidating`, `disconnecting`, `connectQueued`. Názvy jsou návrh interních symbolů, ne nový IPC protokol.

1. Přijmout jen `ContractsEvent::saved` nebo validovaný `SaveOutcome::confirmed` odpovídající živému oknu a generacím. `matched`, unknown, rejected a notSent samy automatický reconnect neaktivují. Během následného status/dialog kroku nesmí nové editování přepsat již potvrzenou identitu, stav „save zapsán, dokončuje se následná akce“ odlišit od neuloženého draftu.
2. Vyžádat nový autoritativní `get_status` asynchronním readerem určeným pro tento tok. Nepoužívat pouze `snapshot_`, tray ikonu ani `connectedOrConnecting()`. Validní stav jiný než přesně `connected` znamená bez reconnect dialogu dokončit ukládací sezení. Edit Config se tiše zavře i při validním Connecting/Recovering/Error, protože není Connected. Neplatný nebo nedostupný status není důkaz odpojení. Neprovést reconnect, bezpečně oznámit nemožnost ověření stavu, uložená data nevracet a save neopakovat.
3. Pro Connected zachytit epochu, ID pokusu i čas začátku. Oba vstupy použijí `Configuration saved` a přesně tento text:

   `Your changes have been saved. It is strictly required that you disconnect and connect again to apply them. Do you want to disconnect and reconnect now?`

4. No, Escape nebo zavření potvrzení ukončí ukládací sezení bez změny spojení. Yes sám ještě neposílá Disconnect. Po návratu modální smyčky ověřit životnost a generace okna, existenci stejné potvrzené operace a stav aplikace. Novým `get_status` ověřit přesný Connected a shodu všech tří položek identity. Změna identity, neplatná odpověď či zánik sezení záměr ruší.
5. Po úspěšné revalidaci předat záměr do konkrétní Disconnect operace a použít dosavadní cestu `startCommand(IpcCommand::disconnect)`. Zjistit skutečné přijetí operace, například explicitním výsledkem interní metody. Odmítnutý start nesmí ponechat záměr aktivní. Souběžný reconnect nepřepisuje již aktivní operaci. Běžné bezpečné odmítnutí je lepší než fronta automatických reconnectů.
6. Ukládací okno pak dokončí normální zavření. Toto očekávané zavření již nemá rušit záměr, který převzala aplikace. Naopak zavření nebo nová generace před předáním jej ruší. Token nese zdroj a generace operace, žádný worker nesmí držet syrový ukazatel na zničené okno.
7. Odpověď na Disconnect musí patřit právě zahájené operaci a musí být validní. Connected nebo Stopping není hotovo. Pokračovat stávajícím pollingem a respektovat existující deadline 30 sekund. Za bezpečně dokončené přijmout pouze validní `state == disconnected`, `phase == idle`, `tunnelReady == no`, bez chyby, se shodnou epochou a ID pokusu. Čas začátku zde musí odpovídat protokolu pro Disconnected, tedy být prázdný, nikoli roven starému Connected času.
8. Přijímat pouze výsledky readerů zahájených pro tuto operaci. Opustit staré status readery při přechodech. Token ověřit i na pozdních a duplicitních callback zprávách. Restart služby, cizí epochu/UUID, chybu, timeout, nevalidní odpověď, odmítnutý command, ruční supersedující akci a Exit/EndSession řešit zrušením jednorázového záměru. Pozdější Idle ho nesmí obnovit.
9. Po potvrzeném Idle označit záměr spotřebovaným a naplánovat soukromou aplikační zprávu s generačním tokenem. Connect nespouštět reentrantně uvnitř `disconnectProgress_.onDisconnectReply` či callbacku `disconnectSnapshot`, protože `finishDisconnect` ještě dokončuje práci s okny a readery. Při odbavení zprávy znovu ověřit token, nepřítomnost nové ruční operace/Exit, autoritativní identitu posledního přijatého stavu a dostupnost command readeru. Je-li stav mezitím zneplatněný, automatický krok zrušit.
10. Po dokončení obsluhy Disconnect zavolat přesně `startCommand(IpcCommand::connect)` bez payloadu se starou konfigurací. Zachovat `finishCommand`, běžné Connect progress okno, ikony, diagnostiku a chyby. Nechat dosavadní Disconnect okno bezpečně přejít do úspěšného konce, jeho stav nesmí blokovat nový Connect. Nezavádět opakování Connect, pokud selže nebo je odmítnutý jako busy.

Identitní kontroly brání reconnectu při pozorované změně spojení a zachovávají dnešní bezpečnostní model. Stávající Disconnect IPC nemá atomický parametr očekávané identity. Mezi status revalidací a serverovým zpracováním může jiný proces změnit spojení. Odpověď či následný status s jinou identitou musí automatický Connect zrušit. Tento plán nezavádí nevyžádaný nový IPC protokol a neslibuje atomickou ochranu před konkurenčním externím CLI Disconnect/Connect.

### Server Information

V `buildServerInfoModel` zvýšit `onlineClients` ve stejné větvi `generatedAt - lastSeenMs <= 60000`, ve které dnes probíhá kontrola kontraktu. Lokální adresu přeskočit před oběma čítači. `validOnlineClients` zůstává počet validních online vzdálených klientů podle dnešní klasifikace, nikoli součet všech položek kontraktů. Neměnit 60sekundovou hranici, řazení, zdrojový snapshot ani stránkování.

Jedním formatterem sestavit například `(4 online clients, 1 valid contract)`, `(1 online client, 1 valid contract)` a `(0 online clients, 0 valid contracts)`. Každé podstatné jméno přepínat nezávisle pouze při hodnotě 1. Totéž použít pro souhrn i titulek `Server information ...`. Zkontrolovat delší text při nejmenší šířce a vyšším DPI.

### Implementační pořadí

1. Zaznamenat baseline a spustit relevantní stávající klientské testy. Vyjasnit předchozí selhání, nevydávat je za důsledek změny.
2. Přidat testovatelnou reconnect politiku, tokeny a unit testy v již kompilovaných souborech. Připravit aplikační wiring, invalidaci a deferred Connect.
3. Převést Contracts na společný post-save tok. Zachovat unknown/check/repeat větve a generační ochranu.
4. Převést Edit Config, odstranit starý úspěšný text, oddělit potvrzené uložení od následného ověření připojení. Zachovat chybové větve a drafty.
5. Opravit syntetické backendy tak, aby Disconnect zachovával epochu a ID a mazal čas jako skutečná služba. Backend `test_wp05_windows.cpp:111-154` dnes po odpojení vrací výchozí nulovou identitu, což pro nové testy nestačí.
6. Doplnit oba počty, společné formátování a odpovídající testy Server Information.
7. Projít níže uvedenou testovací bránu, zkontrolovat diff, obnovit Graft a vytvořit předávací report. Zastavit testovací hosty před předáním.

### Testovací brána B1

Automatická matice přes skutečnou `TrayApplication` a izolovaný named-pipe backend musí ověřovat počet a pořadí commandů, nikoli jen nový text:

| Scénář | Očekávané chování |
| --- | --- |
| Oba vstupy, confirmed Connected, Yes | Jeden Disconnect, žádný Connect před Idle, potom právě jeden běžný Connect a jeho okno |
| No, Escape, zavření dialogu | Uložená konfigurace zůstane, žádný Disconnect/Connect |
| Edit Config confirmed v každém validním ne-Connected stavu | Bez úspěšného/reconnect dialogu, editor se zavře |
| Stav se během Save změní oběma směry | Rozhoduje nový status po potvrzení Save |
| Reject, notSent, unknown, opožděný save cizí generace | Žádný reconnect, odpovídající dosavadní zachování draftu a diagnostika |
| Během dialogu změna epochy, UUID, času, zánik okna nebo EndSession | Žádný Disconnect vyvolaný zastaralým souhlasem |
| Disconnect reply ještě Connected/Stopping, opožděný Idle | Čekání, jeden Connect až na bezpečně dokončený stav |
| Idle s chybou, chybným tunnelReady, cizí identitou, nevalidní status či timeout | Nula automatických Connectů, bezpečné chybové/stavové vyhodnocení |
| Duplicity, staré zprávy, znovuotevření okna, ruční operace, Exit mezi Idle a queued Connect | Záměr nelze znovu spotřebovat ani převést do další operace |
| Nový Connect odmítnut, transportní nebo konfigurační chyba | Běžná chyba Connect, žádná smyčka a žádný falešný úspěch |

Stávající `WP05Windows.ContractsSavedDisconnectConsentAndIdentity` dnes výslovně čeká `connects == 0` a starý text. Aktualizovat jeho očekávání podle scénáře, nesmazat identitní případy. `WP03SaveWindows` musí ověřit potlačený úspěšný dialog skutečného editoru a zachování chybových save větví.

Modelové testy Server Information pokryjí 0/1/více online, online invalid s žádným validním kontraktem, mix valid/invalid/offline, lokální adresu, hranici přesně 60000 a 60001 ms, všechna nezávislá jednotná/množná čísla. U 252řádkové současné fixture je očekávání 168 online a 84 valid. UI regrese ověří shodu titulku a souhrnu, poslední stránku, refresh se změnou počtů a zachování starého snapshotu při selhání.

Příkazy spouštět z kořene repozitáře v interaktivním Windows prostředí s MSVC:

```powershell
./client/tools/build-x64.ps1 -Preset debug -Configure -UiTests -Test
./work/build/debug/tests/phpvpn-wp03-ui-tests.exe
./client/tools/run-tray-ui-tests.ps1
git diff --check
graft build
```

UI targety nejsou registrované do běžného CTest, proto je explicitní spuštění povinné. Testy se soukromým IPC nesmějí sahat na reálnou službu, konfiguraci ani síťové adaptéry. Pokud interaktivní desktop není dostupný, uvést bránu jako nesplněnou, nikoli jako prošlou na základě buildu.

### Kritéria dokončení a předávací balíček B1

Akceptační body 1 až 5 a klientské automatické testy prošly. Společné build/package soubory mají nulový diff proti baseline. Nikde v novém úspěšném toku editoru nezůstává starý informační text. Oba vstupy sdílejí orchestraci, žádný globální reconnect příznak nepřežívá konec operace.

Report předá pouze: větev a výchozí/výsledný HEAD, seznam implementačních souborů či přesný necommitnutý diff, názvy nových symbolů, stručnou smlouvu post-save toku a identity Idle, příkazy/výsledky testů včetně počtu skutečně provedených testů, cesty k logům, stav Graftu, zbývající rizika a potvrzení ukončení zápisů. Pro agenta B2 stačí číst tento report, intent a kapitolu B2 tohoto plánu, klientské zdroje otevře jen při integrační regresi.

## 4. Blok 2: administrační EXE, balení a celková integrace

### Vstup a rozsah

Vstupem je prokazatelně dokončený blok 1 v téže větvi a jeho předávací report. B2 nepřekopává reconnect. Implementuje body 6 až 10, zbývající automatické testy bodu 11 a závěrečnou regresi celého intentu.

### Soubory a navržená struktura

| Soubor | Odpovědnost |
| --- | --- |
| Nové `client/apps/package_gui/main.cpp`, `package_window.h/.cpp` | Dva WIN32 targety nad stejným nativním GUI, režim install/uninstall určen při kompilaci |
| Nové `client/apps/package_gui/script_runner.h/.cpp`, `operation_model.h` | Spuštění procesu, zachycení výstupu, stav výsledku, testovatelné závislosti |
| Nový `client/apps/package_gui/admin.manifest` | `requireAdministrator`, DPI a Common Controls konvence projektu |
| `client/apps/CMakeLists.txt`, případně `version.rc.in` | Targety, výstupní názvy, per-target manifest a ICO, version resource |
| `client/tools/make-dist.ps1`, `verify-package.ps1` | Úplný seznam binárek, podpisy, manifest, ověření hotového balíčku |
| `client/tools/package/install.ps1`, `uninstall.ps1` | Jen nutné zpřesnění výstupu/návratové smlouvy, např. explicitní kódování. Instalační logika zůstává zde |
| `client/tests/resources/icon_resources.cpp` | Přesné ICO frames, manifesty, verze a očekávané úrovně oprávnění pěti EXE |
| Nové `client/tests/unit/test_package_gui.cpp`, `client/tests/ui/package_gui_tests.cpp`, `client/tests/test_package_gui.ps1` | Stavová politika, procesní runner se syntetickým skriptem, skutečné GUI a obsah distribuce |
| `client/tests/CMakeLists.txt` | Samostatné runner/model testy, explicitní UI host, registrace package testu |
| `client/tests/test_package_feedback.ps1`, `test_package_upgrade.ps1` | Zachování dosavadní exit-code, BAT, VerifyOnly a upgrade smlouvy |
| `distribution.md` | Dva nové vstupy, data retention a požadavek spouštět z rozbaleného distribučního adresáře |

Nové názvy souborů jsou návrh realizace. Test-only injekce prostředí nebo syntetického skriptu nesmí vytvořit produkční parametr pro spouštění libovolného administrátorského skriptu. GUI sdílí runner a model, rozdíly režimů jsou jen pevný název skriptu, ikona a texty.

### Implementační rozhodnutí

**Proces a cesta.** Skutečný adresář EXE zjistit module-path API, ne z pracovního adresáře. Připojit pevné `install.ps1` nebo `uninstall.ps1`, ověřit existenci souboru. Windows PowerShell vyhledat absolutně v systémovém adresáři, nikoli přes neověřený PATH. Použít `CreateProcessW`, explicitní application name a korektně citované argumenty `-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File <absolutní skript>`. Nenavazovat na BAT, `cmd /c` ani `ShellExecute runas`. Child zdědí administrátorský token EXE a nevytvoří konzolové okno. Pracovní adresář nastavovat explicitně na distribuční adresář. GUI nepředává `-RemoveData` ani vlastní instalační parametry.

**Výstup a odpovědnost.** Stdout a stderr připojit do společného zapisovacího konce roury, aby se oba proudy kontinuálně četly a jeden neblokoval druhý. Rodič nezdědí zapisovací konec jako trvale otevřený handle, dítě dostane pouze potřebné standardní handles. Worker spravuje proces a čtení, GUI thread přebírá dávky přes vlastněnou frontu a notifikaci. Zprávy neobsahují ukazatele na již zrušené objekty. Nečekat na proces před čtením, plná roura by mohla uváznout.

Explicitně sjednotit textové kódování PowerShell hostu a dekodéru na UTF-8. Nechat oba skripty nastavit konzolové výstupní kódování před prvním textovým výstupem, pokud to runner nedokáže nastavit bez změny smlouvy `-File`. Ověřit reálné Write-Host, Write-Output, Write-Warning a stderr Windows PowerShellu 5.1. Dekodér uchovává neúplné vícebajtové znaky mezi chunky. Neočekávanou chybu dekódování či capture uvést v textboxu a označit výsledek neúspěšně. Nedělat heuristické rozhodování podle slov „success“ či „error“.

Čekat pouze na přímo spuštěný PowerShell. `install.ps1:374` spouští tray, který může přežít instalační proces. Po ukončení PowerShellu dočíst dostupné buffered bajty a ukončit capture bez nekonečného čekání na EOF držené potomkem. Testovat descendant s otevřeným standardním výstupem. Žádný timeout obalu nesmí násilně zabít úspěšně spuštěný produkční tray. Finální úspěch je přípustný až po získání exit code 0 a úspěšném dokončení capture. Selhání CreateProcess, pipes, čtení, čekání nebo GetExitCodeProcess znamená Failed i při jinak nulovém kódu.

**Stavová politika.** `Running` je jantarová, `Succeeded` zelená, `Failed` červená. Exit code je jediným signálem výsledku skriptu. Vlastní chyby obalu mají přednost před nulovým exit code. Nenulový kód zobrazit číselně s krátkou bezpečnou diagnostikou. Úspěch neodvozovat ani z přítomnosti stderr. Skript může napsat warning a přesto podle své vlastní smlouvy uspět. Žádnou kontrolu SCM, Wintun, konfigurace nebo odinstalace neduplikovat do C++.

**Životnost.** Operace se spustí právě jednou po vytvoření a prvním zobrazení okna. Žádné implicitní retry. Během Running nepovolit běžné zavření, které by přerušilo instalaci nebo skrylo výsledek. Pokus o zavření stručně vysvětlit bez blokování workeru. Po dokončení obnovit běžné zavření a ponechat výstup otevřený. OS ukončení sezení bezpečně uvolní lokální handles, pozdní zprávy nesmějí přistoupit ke zničenému oknu. Nenavádět libovolné ukončení procesu na falešný úspěch.

**Okno.** Klientská velikost 760 × 520 DIP, minimum 520 × 360 DIP, bez vlastní maximální velikosti, standardní maximalizace do work area. Cílový monitor zachytit při startu podle kurzoru, s fallbackem na aktivní okno a dostupný monitor. Využít `window_utils` (`sizeAndCenter`, `setMinimumClientTrackSize`, `applyDpiRect`) a existující layout utility v rozsahu jejich současné smlouvy. Při změně DPI přepočítat rozměry, font a okraje. Průběžný log je read-only multiline edit/RichEdit s oběma scrollbary, bez word-wrap, přístupným názvem, klávesovou navigací a kopírováním. Na všech stranách zůstává nejméně 20 DIP barevné plochy. Nastavit dostatečný textový limit a otestovat log překračující běžný limit standardního EDIT, nezahazovat staré řádky potichu. Dávkované doplňování nesmí při velkém logu vyhladovět message loop.

Pevné titulky: `Installation in progress...`, `Uninstallation in progress...`, po dokončení `Installation completed`, `Installation failed`, `Uninstallation completed`, `Uninstallation failed`. Stav musí být poznat i z textu, ne jen z barvy. Souborová i malá/velká okenní ikona instalátoru je `assets/connected.ico`, odinstalátoru `assets/disconnected.ico`.

**Resources.** Stávající společný `app.manifest` má `asInvoker` a tak zůstane. Nový manifest použít pouze pro dva nové targety. Doplnit volitelné per-target parametry existujícího `phpvpn_add_version_resource`, případně vytvořit úzký wrapper, zachovat defaulty tří původních EXE a `/MANIFEST:NO`. V každém EXE právě jeden RT_MANIFEST a správný RT_VERSION. Nezvyšovat oprávnění tray, CLI, služby ani běžných testovacích hostů.

**Distribuce.** Rozšířit `$binaries` v make-dist ze tří na pět názvů a zahrnout nové EXE do kopií, `-SignBinaries` i hashovaného podepsaného release manifestu. Ověření musí mít explicitní povinný seznam souborů, pouhá shoda se seznamem vlastního manifestu nedokazuje, že nové EXE nebylo vynecháno. Stávající BAT/PS1 a licence zůstávají. Manifest generovat až z konečných podepsaných bajtů. Ověřit PE architekturu nových EXE proti balíčku.

Instalační skript již ověřuje hash každého manifestového souboru a kopíruje všechny položky manifestu. Rozšíření seznamu runtime `$binaries` uvnitř `install.ps1` proto není samo o sobě nutné. Zachovat kompatibilitu starých BAT/VerifyOnly toků. Skriptový návrat 0 zůstává autoritativní podle jeho dnešní validace a warning politiky, nepředefinovat všechny warningy jako selhání.

Spouštění z rozbalené distribuce mimo `%ProgramFiles%\phpvpn` je základní ověřovaný tok. Kopírovaný launcher v nainstalované generaci může při upgrade/odinstalaci zamknout vlastní soubor. Tuto variantu zahrnout do testů jako bezpečně popsané selhání se skriptovým nenulovým kódem, nikoli obcházet přes mazání běžícího EXE či přidat druhou instalační logiku. Samopřesun do temp ani self-delete nejsou součástí intentu.

### Implementační pořadí

1. Převzít report B1, ověřit HEAD/diff a ukončení předchozí práce. Zkontrolovat jeho testovací logy, nic nepřebírat jen podle ústního „hotovo“.
2. Implementovat čistý výsledek operace a procesní runner, od začátku testovat syntetické skripty bez instalačních účinků. Vyřešit plné roury, konec procesu bez EOF potomka a kódování.
3. Přidat společné nativní okno, režimy, DPI, log, barvy, titulky, správu ukončení a běžnou přístupnost.
4. Přidat dva CMake targety s přesnými OUTPUT_NAME `install-phpvpn` a `uninstall-phpvpn`, resources a novým manifestem. Testovat shared implementation v neprivilegovaném testovacím hostu, neoslabovat produkční manifest kvůli testům.
5. Provést pouze nutné změny PS výstupní smlouvy a spustit stávající package-feedback/upgrade testy. Aktualizovat fixture parsery jen pokud to změna skutečně vyžaduje.
6. Doplnit package seznam, explicitní verifier, PE resource testy a dokumentaci. Vyrobit nový testovací balíček do odděleného adresáře pod `work`, aby se nepřepsal existující `dist`.
7. Projít bránu B2, explicitní UI a VM kontrolu, potom klientskou regresi B1 proti výslednému buildu. Obnovit Graft a napsat konečný report.

### Testovací brána B2

Automatické testy musí používat skutečný runner se syntetickými `.ps1` fixture a injektovat chyby OS přes úzké testovací rozhraní:

| Oblast | Povinné případy |
| --- | --- |
| Výsledek | exit 0, exit 1 a jiný nenulový kód, výjimka, úspěšná věta s exit 1, chybová věta/stderr s exit 0 |
| Spuštění | Chybějící skript/PowerShell, chyba pipes/CreateProcess/wait/exit-code, přesná volba install versus uninstall |
| Cesty | Mezery, české znaky, apostrof a metaznaky v cestě, jiné CWD, nemožnost přesměrovat launcher argumentem na cizí skript |
| Streaming | Zpožděné chunky viditelné ještě před koncem, stdout i stderr, vícebajtový znak přes hranici chunku, velký výstup nad kapacitou roury a EDIT |
| Životnost | Potomek přežívající PowerShell a držící handle, finální odvod buffered dat, žádný dvojitý start, pozdní zpráva, pokus o zavření během Running |
| GUI | Tři barvy a titulky, read-only, scroll, 20 DIP okraje, resize během běhu, výstup dostupný po konci a ruční zavření |
| Resources | Přesné ICO frames, okenní ikony, requireAdministrator jen u obou launcherů, DPI/visual styles a version resource |
| Package | Pět EXE plus původní vstupy, explicitní detekce chybějícího EXE, hash/velikost, podpis manifestu, staré VerifyOnly a BAT exit codes |

Model a runner testy registrovat do CTest. GUI testy spouštět explicitně v interaktivní relaci. Skutečné produkční EXE s UAC ověřovat odděleně od neprivilegovaného test hostu.

Základní příkazy po doplnění targetů:

```powershell
./client/tools/build-x64.ps1 -Preset debug -Configure -UiTests -Test
./work/build/debug/tests/phpvpn-wp03-ui-tests.exe
./client/tools/run-tray-ui-tests.ps1
./work/build/debug/tests/phpvpn-package-gui-ui-tests.exe
./client/tools/build-x64.ps1 -Preset release -Configure -Test
./client/tools/make-dist.ps1 -BuildDirectory ./work/build/release -OutputDirectory ./work/intent-2026-09-19-package
./client/tools/verify-package.ps1 -PackageDirectory ./work/intent-2026-09-19-package
git diff --check
graft build
```

`phpvpn-package-gui-ui-tests` je nový target, který B2 založí. Přidat konkrétní nový package test do CTest a případný samostatný runner zapsat do předávacího reportu. Před make-dist ověřit, že absolutní cílový adresář je skutečně vyhrazený pod workspace, protože skript maže jeho existující obsah. Podepisovací certifikát a Wintun signature jsou reálné předpoklady package brány. Jejich nedostupnost neobcházet vypnutím validace, uvést přesně nesplněnou část.

Ruční integrační kontrola patří do vyhrazené testovací VM, nikoli na pracovní systém uživatele. Z hotového balíčku spustit instalaci, ověřit jeden UAC prompt pro EXE a žádný druhý pro PowerShell, živý log, zelený konec a ponechané okno. Ověřit upgrade, zachování konfigurace a odinstalaci bez `-RemoveData`, přítomnost ponechaných dat a neodstranění sdíleného Wintun driveru. UAC odmítnutí znamená, že GUI ještě neběží a nemůže samo zobrazit červené okno. To nesmí být vyhodnoceno jako úspěšná operace.

Vizuálně zkontrolovat 100/150/200 % DPI, druhý monitor včetně záporných souřadnic, aktivní monitor při startu, minimální a maximalizované okno a přesun mezi různými DPI. Zachovat screenshoty a logy bez tajných údajů. Reálné instalační/destruktivní testy provést až v dostupném vyhrazeném prostředí, jeho absence znamená otevřenou integrační bránu.

### Kritéria dokončení a předání B2

Obě nové EXE se sestaví, mají požadované manifesty a ikony, spouštějí správné skripty bez druhého UAC, plynule zobrazují oba proudy a správně vyhodnotí ukončení. GUI splňuje všechny rozměry, stavové barvy, titulky a životnost. Ověřený balíček obsahuje konečné binárky včetně jejich hashů. Klientská regrese B1 prošla na výsledném stavu větve.

Nový report `Reports/2026-09-19-14-39-38/block-2-handoff-v001.md` předá výsledný HEAD nebo přesný diff, úplný seznam změn, názvy targetů, skutečné build/test příkazy a výsledky, cestu a SHA-256 distribučního artefaktu, manifestovou/signing identitu bez soukromého materiálu, UI/VM důkazy, případné nesplněné brány a potvrzení ukončení práce. Nevydávat pouhý build za ověřenou instalaci.

## 5. Rizika a konečná kontrola celého intentu

| Riziko | Opatření a důkaz |
| --- | --- |
| Modální dialog zpracuje Exit nebo změnu generace | Kontroly před dialogem, po dialogu i po status read, UI test s reentrancí |
| Odpověď Disconnect není dokončení teardown | Přijetí pouze ověřeného Idle bez chyby, s odpovídající epochou/UUID |
| Čas začátku se při Disconnect smaže | Jiná identitní podmínka před odpojením a po něm, fixture odpovídá službě |
| Pozdní callback znovu spustí Connect | Jednorázový token, invalidace a odložená zpráva, test duplicit a ručního zásahu |
| Zastaralá tray cache se tváří validně | Nový status pro save, samostatné operace readerů, žádný úspěch z cache při unavailable |
| GUI se zasekne na výstupu nebo potomkovi | Worker od začátku čte, dočte buffered data po konci PowerShellu, nečeká na tray |
| Ztráta výstupu kvůli textovému limitu nebo dekódování | Explicitní UTF-8 smlouva, chunk decoder, velké logy, chyby viditelné |
| Globálně změněný manifest elevuje tray | Per-target manifest a PE test všech pěti EXE |
| Balíček obsahuje staré binárky nebo neúplný manifest | Nový release build, explicitní povinný seznam, verifier nad finálními bajty |
| Self-lock nainstalovaného launcheru | Základní tok z externí distribuce, selhání viditelně červené, žádná falešná úspěšná odinstalace |
| Nedostupný desktop, VM nebo signing certifikát | Pokračovat nezávislými testy, přesně uvést nesplněnou bránu, neoznačit intent jako dokončený |

Závěrečný report musí pro každý akceptační bod intentu 1 až 11 obsahovat odkaz na konkrétní automatický test nebo UI/VM důkaz. Body 1 až 5 převzít z B1 a potvrdit regresí na výsledném HEAD. Body 6 až 10 doložit konečným balíčkem a chováním produkčních EXE. Bod 11 doložit testy skutečné orchestrace, modelu, obalu a obsahu balíčku. Nedostupná validace zůstává explicitně otevřená.

Poslední kontrola diffu potvrdí, že intent, jeho chat snapshot, pravidla AGENTS.md a koordinátorský marker nebyly implementačními agenty změněny, nebyla zavedena druhá instalační logika, zůstaly BAT/PS1 vstupy a zachovalo se běžné chování manuálního Connect/Disconnect. Teprve po splnění obou sekvenčních bran a celkové matice lze nahlásit realizaci celého intentu jako dokončenou.
