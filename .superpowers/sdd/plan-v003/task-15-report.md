# Report úlohy 15: D15, TT10 a TT12

Stav: DONE v rozsahu D15, s níže výslovně uvedenými NOT RUN. Společný časový limit byl předán jako uživatelem schválený neomezený. Práce končí po D15. D16/D17 ani B01–B05 nebyly zahájeny, R04 zůstává odložené.

Při obnovení nebo předání kontextu nejprve obnov celý účinný globální blok instrukcí z reálného CODEX_HOME a jeho AGENTS.override.md nebo AGENTS.md, případně z úplného hostem dodaného bloku. Ověř obě přesné samostatné hranice před další věcnou prací. Tento report chráněný blok nenahrazuje.

Autorita: [plán v3](../../../Plans/2026-09-12-02-29-09/plan-v003.md), D00/D15/TT10/TT12, [R06/R07](../../../Plans/2026-09-12-02-29-09/rozhodnuti-v004.md). Větev `tray_UX`, HEAD `1c25096111cdf20f901c58da6299ddde7450e229`. Vstup již obsahoval necommitované výsledky D10–D14 a koordinátorův progress.

## Změny a důkazy

- [client.md](../../../client.md), ochrana konfiguračního adresáře: výslovně sjednocena trvalá politika Users read pro celý strom včetně historie při každém startu. Přímý zápis zůstává privilegovaný, čtení neobchází validaci, serializaci ani atomickou publikaci. Ostatní již správné ACL/startup/UX pasáže ponechány.
- [Matice](../../../Plans/2026-09-12-02-29-09/priskok-matice.md): pouze T12/T13/T15/T17/T18/T20/T21/T23/T25. Neimplementované varianty notes S10/S13/S15 jsou explicitně nerelevantní pro konsolidaci podle R07. Zdrojové přiřazení a historické T dokumenty zůstávají zachované. T17 zachovává dosavadní D07 evidenci.
- [paths_win.cpp](../../../client/src/platform/windows/paths_win.cpp), kanonické SDDL obsahuje BA/SY Full Control a BU FRFX. Historický název `restrictDirectoryToAdministrators` neznamená zákaz čtení Users. [config_files_win.cpp](../../../client/src/platform/windows/config_files_win.cpp) používá stejnou politiku pro nové soubory a kontrolovanou publikaci.
- [service_host.cpp](../../../client/apps/service/service_host.cpp), `ServiceRuntime::start`: příprava stromu, ConfigStore recovery, jeden `ensureDefaultAtStartup`, teprve potom engine/IPC. [config_store.cpp](../../../client/src/core/config/config_store.cpp) klasifikuje missing a používá create-if-missing, ostatní bajty zachovává. Test VpnEngine není náhradou tohoto host lifecycle.
- [editor_window.cpp](../../../client/apps/tray/editor_window.cpp) odděluje Save od requestClose/Cancel. [wizard_window.cpp](../../../client/apps/tray/wizard_window.cpp), `confirm`, potvrzuje receipt a uzavírá session. Zachován Finish bez placeholderu a automatického Contracts/Connect, nedestruktivní Reset a sdílená CIDR validace. [menu_model.h](../../../client/apps/tray/menu_model.h) a současné pořadí akcí ponechány, [password_sha_window.cpp](../../../client/apps/tray/password_sha_window.cpp) a nemodální Password beze změny. [tray_UX.md](../../../tray_UX.md) ani [protocol.md](../../../protocol.md) nevyžadovaly změnu.

## TT12, jediný nový test

Do [test_runtime_status.cpp](../../../client/tests/unit/test_runtime_status.cpp) přidán `wizard_service.fix2_stored_false_survives_model_suggestion_finish_and_new_open`. Existující `fix2_all_preferences_persist_through_verified_finish_and_reopen` již ověřuje osm kombinací a zůstává beze změny. [test_plan3_fix2.cpp](../../../client/tests/unit/test_plan3_fix2.cpp) navíc již pokrývá fallbacky, typy, aditivní kontrakty a samostatné modelové automatické IP.

Nový scénář načte uložené tři explicitní false přes skutečný Wizard IPC serializer/parser a službu do WizardModel. Provede tři korelované kontroly přes existující ScriptedTransport. Na prezentačním rozhraní záměrně nahradí návrh IP hodnotou `.10`, odlišnou od uložené `.2`. Tato injekce je syntetická, nejde o tvrzení, že testovací server s prázdným seznamem klientů navrhl `.10`. Model má návrh k dispozici, ale zachová přesná pole i false. Před Finish není zápis. Finish publikuje jednou přes ConfigStore/FakeConfigFiles, confirmed receipt a uložený DOM zachovají metadata i původní kontrakty. Po close a novém open vznikne jiná session a nový model načte stejná pole bez starého návrhu, bez dalšího HTTP nebo zápisu.

Test propojuje existující komponenty v procesu. Nejde o reálný pipe transport, NTFS, nativní Wizard ani skutečný PHP server. Produkční kód se neměnil a nebyla přidána další kombinatorická matice.

## Provedené ověření, 12. 9. 2026

Build: po načtení `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat` příkaz `cmake --build work/build/debug --target phpvpn-client-tests`, exit 0. Výstup potvrdil kompilaci změněného testu a link EXE.

Každý následující filtr byl samostatným argumentem `work/build/debug/tests/phpvpn-client-tests.exe`:

| Filtr | Výsledek posledního běhu |
|---|---|
| `config_store.` | 8 PASS |
| `config_acl.` | 4 PASS, včetně AccessCheck s omezeným tokenem a nízkoúrovňového NTFS setteru |
| `fix2.` | 8 PASS |
| `WizardModel.` | 4 PASS |
| `wizard_document.` | 4 PASS |
| `wizard_ipc.` | 11 PASS |
| `wizard_service.fix2` | 4 PASS, včetně nového testu |

Celkem poslední cílená sada 43 testů, 0 selhání, všechny příkazy exit 0. Logy jsou v ignorovaném `work/task-15-final-*.log`.

Samostatný existující filtr `config_files.` vybral 21 scénářů, exit 0. Jen `rejects_unsafe_directory_and_nonfixed_paths_without_writing` skutečně proběhl. Zbývajících 20 vypsalo `NOT RUN`, protože SecureFixture vyžaduje zvýšený token. Harness je přesto počítá jako OK, proto zde nejsou vydávány za PASS. Log `work/task-15-config_files.log` obsahuje přesně 20 těchto markerů. Fixture používají vlastní dočasné adresáře a svůj standardní úklid, žádné produkční ProgramData se testem neměnilo.

Průběžné nedostatky vlastní testovací přípravy byly rozlišeny od produktové chyby: první skutečný rebuild bez vcvars neměl standardní MSVC include cestu (`functional`). První nový test měl dangling reference v CHECK_EQ kvůli dočasné odpovědi, opraveno uchováním lokálního receipt před porovnáním. Šlo o chybu nově psaného testu, nikoli produkční opravu. První filtr `wizard_model.` vybral nulu kvůli velikosti písmen, není počítán jako ověření. Následný přesný filtr `WizardModel.` provedl čtyři testy.

## NOT RUN a rizika

- Skutečný izolovaný startup missing → default → nativní editor Cancel → druhý start s porovnáním bajtů. Chybí zvýšený token potřebný k zabezpečení skutečného datového stromu. E2E service vstup používá SCM a vlastní prostředí, zde nebyl spuštěn. Fake startup ani oddělený editor model tento průchod nenahrazují.
- Dvacet chráněných ConfigFiles scénářů včetně skutečného čtení historie omezeným tokenem, publikace, crash hranic, recovery a startup migrace ACL. AccessCheck PASS je užší důkaz účinných práv descriptoru.
- Nový nativní GUI/čtečka/DPI test, vizuální akceptace, skutečný PHP, SCM/Wintun, plný CTest, Release a ASAN. Nevracejí se testy starého UX. R09/TT17 a Statistics zůstávají následujícím blokům.
- Users read záměrně zpřístupňuje také historické klíče a nepotvrzené soubory. Tato přijatá politika není zárukou lokální důvěrnosti. Publikační a přístupové chyby nesmějí být označeny za úspěch.

## Kontrola rozsahu

Vstupní SHA-256 seznam `work/task-15-baseline-hashes.json` obsahuje 453 souborů. Změnily se pouze client.md, matice a test_runtime_status.cpp, ostatních 450 hashů zůstalo shodných. Zachovány jsou všechny šestnáct historických zdrojů, produkční kód, protocol.md, tray_UX.md, progress a výsledky D01–D14. Nový sledovatelný artefakt je tento report. Diff proti vstupním kopiím potvrdil jeden odstavec client.md a uvedené řádky matice, testový diff přidává jediný scénář.

`git diff --check` prošel. Lokální souborové odkazy reportu, matice a client.md ověřeny proti filesystemu. Nebyl proveden commit, push, reset, checkout, vytvoření worktree ani dalšího agenta. Zdrojové notes se nemažou. D15 končí s pravdivě neprovedenou hostovou akceptací, bez tvrzení o dodání B01–B05.
