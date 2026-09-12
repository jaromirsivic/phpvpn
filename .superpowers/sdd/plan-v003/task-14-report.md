# Report úlohy 14: D14 a TT08

Stav: DONE v rozsahu čtrnáctého samostatného bloku. Společný časový limit byl předán jako uživatelem schválený neomezený. Práce končí po D14, další D kroky ani B01–B05 nebyly zahájeny.

Při obnovení nebo předání kontextu nejprve obnov celý účinný globální blok instrukcí z reálného CODEX_HOME a jeho AGENTS.override.md nebo AGENTS.md, případně z úplného hostem dodaného bloku. Ověř obě přesné samostatné hranice před další věcnou prací. Tento report chráněný blok nenahrazuje.

Rozsah vychází z [plánu v3](../../../Plans/2026-09-12-02-29-09/plan-v003.md), D00/D14/TT08, [T09](../../../Plans/2026-09-12-02-29-09/T09.md) a [rozhodnutí R05](../../../Plans/2026-09-12-02-29-09/rozhodnuti-v004.md). [T08/R04](../../../Plans/2026-09-12-02-29-09/T08.md) zůstává odložené. Větev `tray_UX`, HEAD `1c25096111cdf20f901c58da6299ddde7450e229`.

Změny dokumentace:

- [client.md](../../../client.md), Distribuce a instalace ve Windows: offline obsah, licence, skutečné `phpvpn.exe`, tvorba manifestu a odděleného podpisu, kontrola identity/hashů/architektur/API, VerifyOnly, instalační kopie do releaseVersion, přepnutí pointeru před zastavením služby, registrace/start/čekání pipe a následný HKLM Run/autostart. Datový strom připravuje služba. Uninstall zachovává data, explicitní RemoveData je odstraňuje.
- Disconnect, upgrade a rollback: odstraněn slib plné transakce, testu adaptéru/session před potvrzením instalace, automatického kompatibilního rollbacku a společné crash-safe obnovy pointeru a SCM. Výslovně uvedeno chybějící startovní podpisové ověření celé generace. Zachován zákaz slepého downgrade a automatického WintunDeleteDriver.
- Povinné Windows integrační a lifecycle testy, bod 8: skutečný VerifyOnly a běžné instalační vazby místo nedodaného transakčního rollbacku. Ostatní požadavky Wintun integrity/journalu/cleanup a jejich testy z odloženého R04 obsahově zachovány, nejsou tím potvrzeny jako dodané.
- [Matice](../../../Plans/2026-09-12-02-29-09/priskok-matice.md): pouze řádek T09. T09.md, T08.md, plán a rozhodnutí ponechány jako podklady, jejich historická analýza se nepřepisuje.

Důkazy:

- [make-dist.ps1](../../../client/tools/make-dist.ps1): seznam tří EXE, DLL, licence a NOTICE, kopie skriptů, kompaktní manifest s SHA-256, RSA-PKCS1-SHA256 podpis a veřejný certifikát. SignBinaries je volitelný. Skript nebyl spuštěn, distribuční složka zůstala beze změny.
- [install.ps1](../../../client/tools/package/install.ps1): skutečné pořadí bloků 1–9. VerifyOnly končí před New-ProtectedDirectory. Kontrola nadbytečnosti zahrnuje pouze EXE/DLL v kořeni, proto dokument neslibuje odmítnutí každého textového souboru. Kopie používá Force a pointer se přesune před Get-Service/Stop-Service. Pozdější chyba nemá rollback větev. Start-Process tray potlačuje chyby, samotný konec skriptu nedokazuje ikonu.
- [uninstall.ps1](../../../client/tools/package/uninstall.ps1): Disconnect, dvousekundové čekání, ukončení tray, --uninstall nebo SCM fallback, odebrání Run a instalace, data pouze s RemoveData. [BAT vstup](../../../client/tools/package/install.bat) předává argumenty PowerShellu, včetně VerifyOnly.
- [main.cpp](../../../client/apps/service/main.cpp), verifyTunnelDriver: načítá a ověřuje API bez adaptéru/session, nulová verze je dovolena. [tunnel_wintun.cpp](../../../client/src/platform/windows/tunnel_wintun.cpp), loadAndVerify: loader a allowlist. [service_host.cpp](../../../client/apps/service/service_host.cpp) a main nemají startovní ověření podpisu celé generace. R04 se touto kontrolou nerealizuje.
- [test_service_registration.cpp](../../../client/tests/unit/test_service_registration.cpp) ověřuje pouze závislost Tcpip. [runtime ownership test](../../../client/tests/unit/test_runtime_status.cpp) používá fake a blokuje nejasnou/živou identitu bez otevření adaptéru. [service_e2e.cpp](../../../client/tests/e2e/service_e2e.cpp) skutečně vytváří vlastní SCM službu, proto nebyl spuštěn.

TT08, běh 12. 9. 2026 na Windows x64:

Harness `work/task-14-verify.ps1` vytvořil šest samostatných kopií existujícího podepsaného balíčku z `dist/client/Windows/x64`, release 1.0.0, 11 manifestových souborů. Žádný soubor se nemazal, chybějící soubor se do příslušné kopie vůbec nevložil. Každá kopie spustila pouze `powershell.exe -NoProfile -ExecutionPolicy Bypass -File <kopie>/install.ps1 -VerifyOnly`. Kopie install.ps1 i aktuální zdroj mají SHA-256 `FDEA3E483FDDA466C6EE0B766E65585C75A58FF3B32209AA7927A17B03E625C2`. Jde tedy o aktuální ověřovací skript, nikoli jeho mock. Balíčkové EXE nebyly nově sestaveny.

| Scénář | Výsledek a exit |
|---|---|
| Platná kopie | PASS, 0, manifest a 11 souborů ověřeno, Authenticode WireGuard/x64, API kompletní, nic nenainstalováno |
| Přidané bajty do phpvpn.exe | PASS, 1, odmítnutí neshody zaznamenaného hashe |
| Jeden změněný bit RSA podpisu | PASS, 1, odmítnutí neplatného podpisu manifestu |
| Vynechané phpvpn.exe | PASS, 1, odmítnutí chybějícího souboru |
| Neuvedená unexpected.dll | PASS, 1, odmítnutí nadbytečné binárky |
| Neuvedený extra.txt | PASS, 0, současný skript jej přijímá, tato mez je nyní popsána |

Platný běh a extra.txt provedly skutečné uživatelské načtení DLL/ověření API přes --verify-tunnel, bez vytvoření adaptéru nebo session. Výstup hlásil kompletní API a žádný načtený kernelový driver. Nešlo o reálný SCM/Wintun provozní scénář ani instalaci. Negativní scénáře skončily před tímto probe.

Před každým scénářem a po něm byly porovnány hashe jeho balíčku. Po každém scénáři se s výchozím snímkem porovnaly instalované soubory i data mimo logs, stav/PathName/StartMode služby, Run položka, adaptéry, IP adresy, routy a DNS. Všech šest porovnání shodných. Výchozí snímek se pořídil jednou před sekvencí a po každém scénáři se znovu porovnal. První přípravný pokus skončil ještě před kopírováním a spuštěním VerifyOnly na zamčeném logu aktivní služby. Harness poté logy výslovně vynechal. Jde o omezení měření aktivního hostu, nikoli zjištěnou chybu installeru. Snapshot není úplný audit všech zápisů Windows ani důkaz neměnnosti logů, ACL a certifikačních cache. Statická větev VerifyOnly navíc potvrzuje, že se instalační příkazy nevykonaly.

Surové logy, results.json a před/po snapshoty zůstávají v ignorovaném `work/task-14-verify-0374e5c4e8c4427fa67a8c06bf49dbf8`. Obsah snapshotů se nekopíruje do produktové dokumentace.

Další spuštěné kontroly z `C:\git\phpvpn`:

| Příkaz | Výsledek |
|---|---|
| `cmake --build work/build/debug --target phpvpn-client-tests` | exit 0, Ninja no work to do |
| `work/build/debug/tests/phpvpn-client-tests.exe service_registration.` | 1 test, 0 selhání |
| `work/build/debug/tests/phpvpn-client-tests.exe runtime.ownership_recovery_never_opens_adapter_and_blocks_unresolved_identity` | 1 test, 0 selhání |
| `git diff --check` | exit 0 |

Ze 452 vstupních souborů se změnily pouze client.md a matice, zbývajících 450 hashů je shodných. Ověřeno 76 lokálních souborových odkazů bez chyb a fragment instalačního nadpisu. Rozsah ověřen proti vstupnímu seznamu SHA-256 v `work/task-14-baseline-hashes.json` a kopiím `work/task-14-client-before.md`, `work/task-14-matrix-before.md`. Diff vůči vstupu omezen na uvedené tři oblasti client.md a jediný řádek matice. Novým sledovatelným artefaktem je tento report. Produkční kód, instalační skripty, testy, všechny šestnáct historických zdrojů, protocol.md, tray_UX.md i předchozí práce D01–D13 zůstaly proti vstupu zachovány. Nové Markdown odkazy a fragment instalačního nadpisu byly zkontrolovány, Git hlásí jen obvyklou normalizaci LF/CRLF. D14 nevytváří wire ani UX změnu.

NOT RUN a rizika:

- Reálný fresh install/upgrade, registrace/start/stop SCM, tray autostart v nové relaci, odinstalace, RemoveData a restart OS. Nebylo připraveno samostatné VM/E2E prostředí.
- Skutečný Wintun adaptér/session, routy, kernel lifecycle, souběh jiné VPN a veškeré nové scénáře R04. R04 zůstává odložené, stávající fake PASS je neuzavírá.
- Nové negativní Authenticode DLL, architektura a chybějící API, plná CTest sada, Release/ASAN a release build. Pozitivní VerifyOnly nepokrývá všechny chybové větve.
- Chybí plná transakce upgrade/rollback a obnova pointeru+SCM. Instalace může selhat po přepsání souborů či pointeru. Start služby neověřuje podpis celé generace. Tyto nedodané vlastnosti nejsou nově plánovány ani implementovány.
- Nenalezena nová reálná chyba vyžadující produkční opravu v rozsahu testu, BLOCKED nenastal. Produkční kód ani instalační skripty se neměnily. Bez commitu, push, resetu, checkoutu, mazání, worktree a subagentů. Práce končí po D14.
