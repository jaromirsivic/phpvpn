# Realizace intentu v003: autostart, startup Connect a ikony

Autorita: `Intents/2026-09-19-00-47-57/intent-v003.md` a `Plans/2026-09-19-00-47-57/plan-v001.md`, včetně potvrzeného pravidla pro nečitelnou konfiguraci.

Datum: 19. září 2026. Větev: `tray_UX`. Výchozí commit: `1b7cd4a6dd2e221ce3487e7c557b2d3c1819d55f`. Výsledkem jsou necommitnuté změny přímo v této větvi. Nevznikl worktree, další agent, commit, push ani PR. Původní intent, plán a koordinátorkou spravovaný marker zůstaly mimo vlastnictví této realizace.

Implementace je dokončena. Plná akceptace všech prostředí doložena není: skutečný logon/reboot a část ruční Windows matice nebyly provedeny. Obě úplné UI sady obsahují reprodukovatelné chyby už ve výchozím commitu. Finální změna proti této baseline nepřidává další selhání.

## Výsledek implementace

- Service EXE vlastní jediný běžný HKLM Run zápis i odstranění. Používá explicitní 64bitový pohled, REG_SZ, citovanou absolutní cestu bez argumentů a jednorázový úplný zápis. Kritický SCM výsledek je oddělený od bezpečného warning. Chybějící služba/hodnota při odinstalaci jsou idempotentní. Existující shodná SCM registrace při instalaci je úspěch.
- Package/dev skripty používají tento tok. Nouzový uninstall bez binárky odstraňuje pouze vlastní Registry64 hodnotu i při chybě SCM. Nový HKCU zápis nevzniká. Starý záznam aktuálního profilu se maže jen při přesné shodě známé cesty.
- `autoConnectOnStartup` prochází oběma modely a striktním validátorem. Chybějící pole znamená false. Nový/resetovaný dokument má požadovanou koncovou dvojici, edit/repair zachovává existující hodnotu a pořadí ostatních členů. Checkbox nepřibyl.
- Startup úloha se naplánuje po připraveném IPC a SCM RUNNING. Plná fronta se zkusí znovu bez čtení na řídicím vlákně. Čitelné true používá společný Connect admission a původní síťovou sekvenci. Nečitelné/poškozené vstupy publikují bezpečný error bez HTTP. Generace řízení chrání Disconnect/Stop, epocha chrání i novější ruční Connect, který už skončil chybou.
- Všechny tři produkční EXE obsahují default ID 100. Tray má navíc přesné stavové skupiny 101/102/103. Small/big ikony všech formulářů a společných dialogů jsou oddělené od notification area. Výměna resources zachovává staré handles do úspěšného přepnutí.
- Lokální odpojování mění ikonu okamžitě a nekončí samotným RPC potvrzením. Čtení před příkazem se zahazuje. Stav se sleduje i po zavření progress okna. Chyba/timeout ukončí čekání a vyžádá nový status.

Průběžně byly aktualizovány `client.md`, `tray_UX.md`, testovací README pro UI a E2E. `distribution.md` doplňuje nový PE ověřovací krok. Staré tvrzení o povinné shield_lock geometrii a původním pořadí zápisu Run bylo v klientské dokumentaci nahrazeno aktuálním chováním.

## Provedené kontroly a evidence

Cesty níže jsou relativní ke kořeni repozitáře. Logy a balíčky jsou pracovní artefakty ve `work`, nepředstavují publikované vydání.

| Kontrola | Výsledek | Evidence |
|---|---|---|
| x64 Debug CTest | PASS, 7/7 sad včetně plných unit, registru, PE, package, loggeru a skutečného PHP | `work/debug-final-ctest.log` |
| x64 Release CTest | PASS, 7/7 sad, 432 klientských unit testů | `work/release-final-ctest.log`, `work/build/release/Testing/Temporary/LastTest.log` |
| Dodatečné všechny wizard režimy | PASS, edit/repair/create/reset | `work/wizard-modes-final.log` |
| Startup cíleně pod ASAN | PASS, 7 testů včetně zrušení čtení a novějšího ručního selhání | `work/asan-startup.log` |
| UI resource lifetime pod ASAN | PASS, 6 testů | `work/asan-icons.log` |
| Skutečný Win32 registry backend | PASS, izolované přesměrování HKLM do privátního HKCU klíče, přesné REG_SZ bytes, opakování a sousední hodnota | `work/registry-native.log`, `work/asan-registry.log` |
| Package feedback/BAT | PASS, 34 scénářů, včetně doplňkového selhání fallback cleanupu a kritických selhání | `work/package-feedback-expanded.log` |
| Skutečný SCM/Wintun/PHP lifecycle | Finální PASS, exit 0 a cleanup PASS, včetně startup true, invalid true a false restartu | `work/build/service-e2e.log`, `service-e2e.exit`, `service-e2e-cleanup.log` ve stejném adresáři |
| DNS a IPC RSS | PASS, 2/2, host DNS/default routes/produkční služba beze změny | `work/resource-final.log`, `work/build/resource-cleanup.log` |
| Zdrojové ICO a PE | PASS, 16/32/128 frames, všechny skupiny, manifest a version resource | `work/package-final-verify.log`, `client/tools/check_icons.py` |
| Inkrementální resource build | PASS, znovu přeloženy RC všech tří produkčních EXE, následně přesná shoda PE | `work/icon-incremental-build.log` |
| Nové testy skutečných oken/odpojování | PASS, oba `WindowIcons`, včetně zavření progress během teardown a timeoutu okna | `work/wp03-ui-final.log` |
| Závěrečné zpřesnění čtení stavu při Disconnect | PASS, 2/2 WindowIcons a 1/1 tray actions/Exit. Živé progress okno je jediný čtenář, tray převezme čtení po jeho zavření | `work/tray-final-guard-windows.log`, `work/tray-final-guard-actions.log` |
| Plná tray UI sada | 45 testů, 5 selhání, přesně stejná množina jako baseline | `work/tray-ui-verified.log`, `work/tray-ui-baseline-full.log` |
| Plná WP03/WP05 sada | 38 testů, 10 selhání. Baseline 36 testů se stejnými 10 selháními. Dva nové testy prošly | `work/wp03-ui-final.log`, `work/wp03-ui-baseline.log` |
| Finální podepsaný Windows balíček | PASS, script parity, manifest/hash/signature, BAT VerifyOnly a PE oracle | `work/autostart-dist-final`, `work/package-final-build.log`, `work/package-final-verify.log` |

Výchozí commit byl pro srovnání UI exportován pomocí `git archive` do `work/autostart-baseline-source` a samostatně sestaven. Aktivní checkout se nepřepínal ani neobnovoval. Baseline původních PHP contract testů neměla dostupný server. Po vytvoření izolované loopback fixture na portu 18093 prošly všechny příslušné kontroly.

Jeden mezilehlý SCM běh selhal při post-crash opětovném otevření Wintun adaptéru s `tunnel_start_failed`. Cleanup síťového prostředí prošel. Runner následně správně odmítl přepsat zachovaná testovací data. Ta byla archivována do `work/autostart-e2e-failure-data`, bez změny ACL nebo produkčních dat. Čistý opakovaný běh dokončil celý lifecycle a cleanup s exit 0. Předchozí selhání je zachováno v `work/service-e2e-retry-failed.log`, `.stderr.log` a `work/service-e2e-retry-failed-service.log`. Nejde o tvrzení, že každý běh E2E prošel.

Inkrementální kontrola změnila pouze časy čtyř zdrojových ICO a po sestavení je obnovila. SHA-256 obsahu před a po byl shodný. Obsah ikon nebyl upraven. ASAN výsledky jsou cílené běhy, nejsou označeny jako plná ASAN regrese.

Úplné CTest/UI běhy předcházely poslednímu zpřesnění výběru čtenáře Disconnect statusu. Tento malý následný zásah byl ověřen cílenými native testy uvedenými v tabulce. Potom byly znovu sestaveny Debug/Release a znovu vytvořen i ověřen finální balíček. Ostatní implementace startup/SCM od finálního E2E běhu změněna nebyla.

## Realizační kroky 0 až 9

| Krok plánu | Stav |
|---|---|
| 0 Kontext | Intent, plán, AGENTS, Graft a výchozí stav načteny. |
| 1 Baseline | Výchozí testy zaznamenány, dodatečně obě celé UI sady ověřeny na nezměněném exportu HEAD. |
| 2 Konfigurace | Implementována, unit ověřena, klientská dokumentace aktualizována v kroku. |
| 3 Autostart a SCM | Implementováno s výslovným rozdílem pro přímý upgrade níže, fake a Win32 registry testy. |
| 4 Package/dev | Sjednoceny běžné vstupy a fallback, rozšířené package tests a dokumentace. |
| 5 Startup | Jednorázový executor, generation/epoch ochrana, SafeError, unit/ASAN/SCM E2E. |
| 6 Resources | Přesná ICO integrace, PE oracle, x64 build a inkrementální kontrola. |
| 7 Okna a tray | Default small/big, TaskDialog/fallback implementace, mapování a Disconnect lifecycle, UI/UX dokumentace. |
| 8 Regrese/VM | Automatizované sady provedeny. Baseline UI chyby a neprovedené VM scénáře výslovně evidovány. |
| 9 Balíček a předání | Čerstvý vývojově podepsaný Windows balíček ověřen. Audit, dokumentace, Graft a diff aktualizovány. Skutečná instalace balíčku NOT RUN. |

## Audit AK1 až AK14

| AK | Stav | Závěr |
|---|---|---|
| 1 | PARTIAL | Registrace a citování implementovány a automatizovaně ověřeny. Skutečný logon běžného uživatele NOT RUN. |
| 2 | PARTIAL | Opakování registry operací a package fixtures PASS. Skutečné produkční SCM install/uninstall/upgrade ve VM NOT RUN. |
| 3 | PASS | Simulované chyby zápisu/odstranění zachovají kritický výsledek a viditelné warning. Kritické chyby zůstávají chybami. |
| 4 | PASS | Default, create/reset, pořadí koncové dvojice a skutečný první SCM start ověřeny. |
| 5 | PASS | Absence, oba boolean stavy a string/int/double/null/array/object v obou modelech ověřeny. |
| 6 | PASS | Zachování/doplnění hodnoty v edit/repair a nový create/reset ověřeny. Existující atomické store/wizard testy prošly. |
| 7 | PASS | False/absence bez automatického připojení v unit a skutečném SCM lifecycle. |
| 8 | PASS | Standardní connecting a connected, jedna epocha, souběh s ručním Connect, skutečný SCM true restart. |
| 9 | PASS | Recovering i terminální error v unit, chybný config s SCM RUNNING a IPC v E2E, zrušení a bezpečný status. Jednotlivé síťové failure varianty nebyly všechny opakovány ve VM. |
| 10 | PARTIAL | Přesné PE resources a malé/velké HWND ikony automatizovaně ověřeny. Explorer/properties/Alt+Tab a fyzické přesuny monitorů NOT RUN. |
| 11 | PASS | Čisté mapování a skutečný tray přes privátní IPC/shell seam, okamžité odpojování, dokončení a chybná odpověď ověřeny. |
| 12 | PASS | Resources v EXE a balíčku, žádné externí ICO v provozním balíčku, inkrementální RC build ověřen. |
| 13 | PASS | Přidány automatizované konfigurační, startup, registry, UI a PE kontroly. |
| 14 | PARTIAL | Debug/Release CTest, resource, packaging a finální E2E PASS. Obě UI sady mají reprodukovatelná baseline selhání. Nelze tvrdit, že všechny existující testy jsou zelené. |

## Audit úplné matice plánu

PASS znamená provedenou uvedenou automatizovanou úroveň. PARTIAL výslovně odděluje automatizovanou evidenci od neprovedené skutečné instalace nebo ručního scénáře. Neprovedené úrovně se nepočítají jako PASS.

| ID | Výsledek a rozsah |
|---|---|
| I01 | PARTIAL. Helper a skutečný přesměrovaný registry backend PASS, produkční SCM install NOT RUN. |
| I02 | PARTIAL. Package preflight/fixture a podepsaný VerifyOnly PASS, skutečný install NOT RUN. |
| I03 | PARTIAL. Opakovaný zápis stejné hodnoty a package upgrade fixture PASS, skutečná SCM idempotence NOT RUN. |
| I04 | PARTIAL. Generační package tok a jeho fixture ověřeny, skutečný upgrade NOT RUN. |
| I05 | PASS U. Mezery/Unicode, absolutní citovaná cesta bez argumentů, Win32 roundtrip. Logon E NOT RUN. |
| I06 | PASS U. Prázdná/relativní/NUL/uvozovky/traversal/chybějící soubor/nadlimitní délka odmítnuty před zápisem. |
| I07 | PASS U/P. Denied fake zachová původní hodnotu, kritický úspěch a warning. Skutečné ACL selhání NOT RUN. |
| I08 | PARTIAL. Kritická chyba nevytvoří Run, package exit 1. Skutečná kolize SCM jména NOT RUN. |
| I09 | NOT RUN. 32bitový PowerShell. Kód používá explicitní Registry64/KEY_WOW64_64KEY. |
| I10 | NOT RUN. Dva běžní uživatelé, nový profil a session 0 oracle. |
| I11 | NOT RUN. Reboot a skutečný opakovaný logon. |
| I12 | PARTIAL. Package preflight/VerifyOnly/chyby/Exit fixtures PASS. Úplná ruční instalační matice NOT RUN. |
| U01 | PARTIAL. Unit a Win32 sousední hodnota, package fixture PASS. Produkční odinstalace NOT RUN. |
| U02 | PARTIAL. Opakované odstranění hodnoty i chybějícího klíče, absent-service fixture PASS. Skutečný SCM NOT RUN. |
| U03 | PASS U/P. Doplňkový denied nebrání kritickému úspěchu, warning ověřen. |
| U04 | PASS U/P. Obě operace nezávislé, fallback běží i po chybě SCM. |
| U05 | PARTIAL. Fallback bez EXE a selhání jeho cleanupu ve fixture PASS, skutečná instalace bez pointeru NOT RUN. |
| U06 | PASS P. Zachování dat a explicitní RemoveData ve fixture, žádné nové odstranění sdíleného driveru. |
| U07 | PARTIAL. Win32 zachování sousední hodnoty PASS. HKCU migrace zkontrolována ve zdroji, reálné staré profily NOT RUN. |
| U08 | PASS P. Viditelné native warning přes stdout a UAC/BAT exit kódy. Stderr nebylo použito pro doplňková warning, viz rozhodnutí níže. |
| C01 | PASS U/E. Přesný default včetně skutečného SCM vytvoření. |
| C02 | PASS U/E. Starý dokument zachován a absence znamená false. |
| C03 | PASS U. True/false v obou modelech. |
| C04 | PASS U. Všechny vyjmenované neboolean typy a původní strict whitelist. |
| C05 | PASS U. Edit/repair zachování obou hodnot, existující preservation testy. |
| C06 | PASS U. Absence doplněna při sestavení dokumentu, store/wizard cancel bez publikace. |
| C07 | PARTIAL. Nový/resetovaný draft a pořadí PASS U, ruční wizard reset nad true NOT RUN. |
| C08 | PASS U. Create i reset po doplnění identity a contracts drží poslední pole. |
| C09 | PASS U/E. Existující store/wizard revision/receipt/atomic failure sady a SCM lifecycle. |
| C10 | PARTIAL. Starý/nový dokument validní a UI bez nového checkboxu. Ruční editační matice NOT RUN. |
| A01 | PASS U/E. False/absence bez Connect. |
| A02 | PASS U/E. Automatická běžná sekvence a connected. |
| A03 | PASS U. Recoverable timeout, původní recovery/backoff testy. Skutečný nedostupný server ve VM NOT RUN. |
| A04 | PASS U. Terminální autentizační chyba, původní kryptografické testy. Všechny varianty ve VM NOT RUN. |
| A05 | PASS U/E. Neúplný true config odmítnut bez HTTP a SCM zůstává RUNNING. |
| A06 | PASS U/E. Read failure/JSON/UTF-8/type/exception a skutečný poškozený dokument. |
| A07 | PASS U/E. Neúplný nový default false nevyvolá Connect. |
| A08 | PARTIAL. Původní recovery/ownership unit a E2E PASS. Všechny varianty startup journal chyby s true NOT RUN. |
| A09 | PASS U/E. Blokovaný callback a health gate, IPC/SCM host readiness. |
| A10 | PASS U. Jedna epocha při ručním souběhu, také ochrana novějšího již neúspěšného Connect. |
| A11 | PASS U/E. Zrušení před callbackem/při čtení, běžné Disconnect při bootstrapu a skutečný lifecycle. |
| A12 | PASS U/ASAN. Zrušení fronty a blokovaného čtení, žádný pozdní HTTP. |
| A13 | PARTIAL. Bez watcheru, jednorázovost a SCM true/false restarty ověřeny. Reboot NOT RUN. |
| A14 | PARTIAL. Plná fronta/retry/exception bez replay PASS U, původní wizard hooks zachovány. Všechny souběhy reálného wizardu s auto startup NOT RUN. |
| A15 | PARTIAL. Ruční oprava po chybě PASS U. Konzolový host sdílí runtime, samostatný console restart NOT RUN. |
| T01–T05 | PASS U. Disconnected/healthy connected/bootstrap/recovering/error mapování. |
| T06 | PASS U/E. Nevalidní status po connected používá disconnected, skutečný privátní pipe scénář. |
| T07 | PASS E. Shell seam ověřuje connecting ihned po lokální akci před pumpou odpovědi. |
| T08 | PASS U/E. Connected RPC odpověď neukončí lokální průběh, stopping v čistém modelu. |
| T09 | PARTIAL. Dokončení/chybná odpověď a timeout progress okna PASS. Celý 30s timer tray nebyl samostatně odčekán. |
| T10 | PARTIAL. Duplicitní akce a abandonment testy PASS, superseded cesta zkontrolována. Každá kombinace pozdních zpráv nebyla samostatně přehrána. |
| T11 | PARTIAL. Lokální Connect je v čistém mapování a stávajících UI scénách. Úplná matice odmítnutí/cancel přes shell seam NOT RUN. |
| T12 | PARTIAL. Shell publish/remove a session lifecycle automatizovaně, restart Exploreru NOT RUN. |
| W01 | PASS E. Progress small/big ve všech testovaných stavech a timeoutu. |
| W02 | PASS E. Produkční okna v native UI harnessu, PE/default porovnání. |
| W03 | PASS E. Wizard včetně reopen, Contracts/Password v UI scénách. |
| W04 | PARTIAL. TaskDialog information/confirmation/startup PASS. Nucené selhání TaskDialog a skutečný fallback NOT RUN. |
| W05 | PARTIAL. 96/120/144/192 resource rozměry PASS U, skutečná okna DPI 144. Fyzické přesuny monitorů NOT RUN. |
| W06 | PARTIAL. GDI/USER cykly, ASAN, DPI/palette testy PASS. Ruční celá high contrast matice NOT RUN. |
| W07 | PASS U. Chybějící resource zachová původní ikony a HWND handles. |
| R01–R04 | PASS. Přesné produkční PE skupiny, frames, symboly a všechny x64 UI targety. |
| R05 | PARTIAL. Balíček neobsahuje externí ICO a PE oracle pracuje nad EXE balíčku. Ruční provoz v čisté VM bez source tree NOT RUN. |
| R06 | PASS. RC rebuild po změně timestampů, původní obsah a časy obnoveny. Nebyl prováděn obsahový zásah do zdrojových ICO. |
| R07 | NOT RUN. Explorer/properties/Alt+Tab na čerstvé cestě/VM. |
| R08 | PARTIAL. x64 Debug/Release a ASAN sestaveny. Instalovaný MSVC Hostx64 má x64/x86, ARM64 compiler chybí. Existující arm64 cache ukazuje x64 compiler a nebyla vydávána za ARM64 důkaz. |
| G01 | PASS. Debug/Release CTest, cílené dodatečné wizard režimy. |
| G02 | BASELINE FAIL. Pět tray UI a deset WP03/WP05 chyb je stejných před změnou i po ní. |
| G03 | PASS finální běh. Jeden mezilehlý post-crash Wintun failure zachován, viz evidence. |
| G04 | PASS. DNS/RSS a host cleanup baseline. |
| G05 | PASS automatizované stávající sady a finální reálný UDP/SCM lifecycle. |
| G06 | PASS P. Čerstvý podepsaný testovací balíček, VerifyOnly a BAT/UAC fixtures. |
| G07 | PARTIAL. Staré config fixtures PASS, kompatibilní rollback zdokumentován. Skutečný downgrade instalace NOT RUN. |
| G08 | PARTIAL. Nové pevné warning a SafeError, test soukromého exception textu PASS. Úplný audit všech historických failure injection logů NOT RUN. |

## Rozhodnutí proti doporučeným detailům plánu

Přímý `--install` přijme již shodnou registraci, ale jinou ImagePath bezpečně odmítne. Generační upgrade vlastní stávající package tok s ověřeným manifestem, který službu explicitně odregistruje a zaregistruje. Doporučené automatické přesměrování jiného existujícího SCM ImagePath z přímého CLI nebylo přidáno, protože samotné stejné jméno služby neprokazuje vlastnictví jiné instalace. Tento rozdíl proti doporučení 4.1.2 je výslovný, není vydáván za otestovaný přímý upgrade.

Warning se tiskne na stdout. Windows PowerShell s ErrorActionPreference Stop může native stderr považovat za fatální chybu. Kritické chyby zůstávají na stderr a mají exit 1. Tím je zachován požadovaný uživatelský výsledek U08 bez závislosti na nastavení PowerShell native error preference.

Jednorázový RegSetValueExW nepotřebuje předchozí diagnostické čtení ani následný rollback. Testy ověřují zachování předchozí hodnoty při selhání fake zápisu a přesné bytes skutečného zápisu v izolovaném registru. Po úspěšném zápisu se neprovádí další destruktivní kompenzace.

## Co zbývá pro plnou akceptaci

V připravené Windows VM provést produkční install/reinstall/upgrade/uninstall, skutečná denied ACL, 32bitový host fallbacku, logon dvou běžných uživatelů, nový profil a reboot. Dále Explorer/properties/Alt+Tab, fyzická DPI matice, nucený MessageBox fallback a kompatibilní rollback. Samostatně opravit nebo aktualizovat existující UI baseline testy. Chybějící ARM64 toolchain byl ověřen a sestavení nebylo simulováno x64 presetem.

Finální Windows balíček: `C:\git\phpvpn\work\autostart-dist-final`. Podepsán vývojovým certifikátem `CN=phpvpn Development Test Signing`. Není publikovaným ani veřejně důvěryhodným vydáním. Produkční služba, její konfigurace, DNS a výchozí routy nebyly změněny.

Závěrečný `git diff --check` prošel. Stav souborů je uložen v `work/autostart-final-git-status.txt`, Graft obnoven v `work/graft-final-build.log`. Vlastní PHP fixture byla ukončena, listener 18093 i testovací UI/service procesy jsou nepřítomné. Zachovány jsou výsledky a diagnostická data. Práce ukončena před děděným termínem 11:02:55 +02:00, který byl sledován během práce, nikoli automaticky vynucován.
