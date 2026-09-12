# Report úlohy 5: D05 a TT06

Stav: DONE v rozsahu pátého sekvenčního bloku. Společný časový limit byl předán jako uživatelem schválený neomezený. D06 ani D13 nebyly zahájeny.

Při obnovení nebo předání kontextu nejprve obnov celý účinný globální blok instrukcí z reálného CODEX_HOME a jeho AGENTS.override.md nebo AGENTS.md, případně z úplného hostem dodaného bloku. Ověř obě přesné samostatné hranice před další věcnou prací. Tento report chráněný blok nenahrazuje.

Změny:

- [client.md](../../../client.md), Windows Service, konzole a tray: doplněno, že síťové vlákno vzniká při úspěšném startu engine a trvá do shutdown, v odpojeném stavu čeká bez VPN pollingu. Tunel, session a dvě I/O role vznikají po úspěšném health/info/prázdném data bootstrapu. Disconnect ukončí dvě tunelové role a session, síťová role zůstává pro další Connect.
- Stejný dokument, Veřejné lokální IPC, příkaz connect: odstraněno chybné spojení startu všech tří rolí až s dokončeným bootstrapem. Výslovně zachovaná idempotence při connecting/connected/recovering, přijatý konfigurační snapshot a pravidla checkpointů.
- [test_runtime_status.cpp](../../../client/tests/unit/test_runtime_status.cpp): dva nové lifecycle scénáře přes existující fake driver. Testovací instrumentace počítá vytvoření adaptéru, start a konec session, zavření adaptéru, první skutečné receive/send každé session, signalizaci stop a destrukci tunelu. Ověřuje odlišnost síťového, vstupního a výstupního vlákna. Existující friend umožňuje pozorovat síťové vlákno a vlastněné I/O role pouze ve stabilních bodech. Fake hodiny počítají čtení pro ověření průchodů recovery smyčky před deadlinem.
- [priskok-matice.md](../../../Plans/2026-09-12-02-29-09/priskok-matice.md): změněn pouze řádek T06.
- Tento report. Produkční kód, protocol.md, tray_UX.md, T06.md a progress.md se nezměnily.

Důkazy a existující pokrytí:

- [plan-v003.md](../../../Plans/2026-09-12-02-29-09/plan-v003.md), D00, D05 a TT06, [T06.md](../../../Plans/2026-09-12-02-29-09/T06.md) a rozhodnutí R03 určují hranice tohoto bloku. Přezkoumány byly relevantní části client.md, protocol.md a tray_UX.md včetně UX04/UX05.
- `vpn_engine.cpp::start` vytváří jediné síťové vlákno po úspěšné startup recovery. `runConnectSequence` provádí health, info, ověření prázdného data, potom start session a obou I/O rolí. `runNetworkThread` po Disconnectu pokračuje, `shutdown` síťové vlákno připojí.
- `teardown` zastaví a zruší obě I/O role, ukončí session, resetuje síťový kontext a vyčistí fronty. Nový bootstrap vytvoří session a role znovu. `tunnel_workers.cpp` má skutečná samostatná vstupní a výstupní vlákna. Nevznikl nový slib obecného catch ani dohledu pádu rolí.
- Před doplněním bylo spuštěno všech 17 existujících runtime testů, 0 selhání. Již existují konkrétní testy idempotence, snapshotu po Save a externím smazání konfigurace, bootstrap checkpointů, recovery historie, účetnictví a nového logického Connectu. Tyto testy zůstaly obsahově beze změny. Původní fake driver počítal start session, ale ne její konec, destrukci ani skutečná I/O vlákna. Proto doplnění není duplicitou.

Nové scénáře:

1. `runtime.lifecycle_keeps_one_network_role_and_recreates_two_io_roles`: před startem není síťové vlákno, po startu existuje bez tunelu a HTTP. Duplicitní start je odmítnut. Řízené brány samostatně zastaví health, info a první data před odpovědí a ověří nulový počet session a I/O rolí. Opakovaný Connect nevytvoří další bootstrap. Po úspěchu jsou dvě I/O role skutečně aktivní na různých vláknech. Disconnect zruší první session a obě role, síťové vlákno zůstává stejné. Nový Connect vytvoří druhou session a dvě nové role. Shutdown, včetně opakovaného volání, zanechá nulový počet vlastněných rolí a právě dva konce session, zavření adaptéru a zániky tunelu. Identita OS vlákna se mezi zaniklými I/O generacemi nemusí lišit, protože ji OS může znovu použít.
2. `runtime.lifecycle_recovery_tears_down_and_recreates_after_current_backoff`: provozní data timeout ukončí první session a tunel. Sedm následných health timeoutů nezaloží další session. Osm retry čekání ověřuje skutečně pozorovaný žebřík 2/4/8/16/32/60/60/60 s, žádný retry před základní mezí a další pokus nejpozději při základní mezi plus 20 %. Fake čas, čítač čtení hodin a transportní brány nahrazují čekání na reálné minuty. Poslední bootstrap uspěje a vytvoří druhou session a dvě I/O role na stále stejném síťovém vlákně. Závěrečný shutdown vše ukončí. Test nevolá ani nekopíruje soukromý výpočet jitteru.

Příkazy spuštěné z `C:\git\phpvpn`:

| Příkaz | Výsledek |
|---|---|
| `cmake --build work/build/debug --target phpvpn-client-tests` před úpravou | exit 0, Ninja: no work to do |
| `work/build/debug/tests/phpvpn-client-tests.exe runtime.` před úpravou | 17 testů, 0 selhání, exit 0 |
| `cmake --build work/build/debug --target phpvpn-client-tests` po doplnění | exit 0, sestaven upravený testovací objekt a testovací executable |
| `work/build/debug/tests/phpvpn-client-tests.exe runtime.lifecycle_` | 2 testy, 0 selhání, exit 0 |
| `cmake --build work/build/debug --target phpvpn-client-tests` po závěrečné úpravě životnosti testovacích lokálních proměnných | exit 0 |
| `work/build/debug/tests/phpvpn-client-tests.exe runtime.` závěrečný běh | 19 testů, 0 selhání, exit 0 |
| `work/build/debug/tests/phpvpn-client-tests.exe probe.` závěrečný běh | 6 testů, 0 selhání, exit 0 |
| `work/build/debug/tests/phpvpn-client-tests.exe wizard_service.` závěrečný běh | 13 testů, 0 selhání, exit 0 |
| `git diff --check` | exit 0, bez whitespace chyb |

Závěrečné běhy zahrnuly 38 různých testů, 0 selhání. Substring filtr `probe.` zahrnul vedle čtyř ProbeClient scénářů také dva `config_files_probe` testy. Jejich očekávané diagnostické rename error 183/32 nejsou selháním testů. Předchozí běhy 17 a 2 testů se do počtu 38 znovu nepřičítají.

MSVC x64 prostředí bylo načteno postupem existujícího `client/tools/build-x64.ps1`: `vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`, nalezený `Common7/Tools/VsDevCmd.bat -arch=x64 -host_arch=x64` a převzetí výstupu `set` do prostředí procesu. Konfigurace buildu se neměnila.

Kontrola rozsahu a formátu:

- Větev `tray_UX`, HEAD `da788b71feac031d5572b906e5d3ac51d4e87ae4`. Stav byl porovnán s D00 a relevantní důkazy znovu načteny pro aktuální checkout.
- V ignorovaném `work/task-5-baseline-hashes.json` jsou vstupní SHA-256 pro 188 souborů, včetně všech 16 historických zdrojů, tří cílových dokumentů, produkčního src/apps a předchozí rozpracované práce. Kopie `work/task-5-client-before.md`, `work/task-5-matrix-before.md` a `work/task-5-runtime-before.cpp` zachycují vstup tohoto bloku.
- Proti vstupu se změnily pouze client.md, test_runtime_status.cpp a jediný řádek T06 matice. Zbývajících 185 hashů je shodných. Po sjednocení CRLF/LF bylo obsahově porovnáno všech 34 původních testových těl, všechna zůstala shodná, přibyly právě dva testy. Veškerá předchozí práce D01–D04 zůstala zachovaná. Novým sledovatelným souborem je tento report.
- Všech sedm nových lokálních Markdown odkazů v reportu a řádku T06 bylo rozbaleno vůči dokumentu a jejich cíle existují. V client.md nevznikl žádný nový Markdown odkaz. `git diff --check` prošel. Git upozorňuje na obvyklou budoucí normalizaci LF na CRLF.

NOT RUN a meze závěru:

- Plná CTest sada, Release/ASAN, skutečný PHP relay, WinHTTP integrace, služba, Wintun/VPN, GUI, installer a E2E nebyly spuštěny. Fake driver nepotvrzuje skutečný Windows kernel lifecycle ani odložené R04.
- Backoff test potvrzuje dodané retry meze, nikoli distribuci všech možných jitter hodnot. Nepotvrzuje zachování tunelu nebo čekajících paketů při recovery ani obecné zotavení po výjimce nebo pádu libovolné role.
- D13 má samostatně upravit starší obecné recovery sliby a popis backoffu v client.md podle R03. Starší věta v části Disconnect, upgrade a rollback stále požaduje ukončení všech tří vláken a neshoduje se se skutečnou životností síťové role. Tento blok ji v souladu s úzkým cílem D05 a oddělením D13/R04 nepřepisuje a nepotvrzuje její cleanup pořadí. Koordinátor má rozpor zohlednit při následné konsolidaci lifecycle textu.
- Produkční chyba nebyla odhalena. BLOCKED nenastal. B01–B05 zůstávají budoucí prací a R04 odložené. Neproběhl commit, push, reset, checkout, vytvoření worktree ani subagentů. D06 nebylo zahájeno.
