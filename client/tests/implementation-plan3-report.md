# Implementace plan3_client.md

Zdroje byly 6. září 2026 přeneseny do `C:/git/phpvpn`. Tento report zachovává výsledky běhů v původním worktree. Odkazy na pracovní logy vedou do tohoto původního umístění, odkazy na distribuci vedou na novou kopii v hlavním projektu. Podrobnosti přenosu uvádí [předávací report](implementation-plan3-handoff.md).

Stav k 6. září 2026. Implementace průvodce, služby, editoru, protokolu a souvisejících testů je v pracovním stromu `C:/Users/User/.codex/worktrees/d6c8/phpvpn`. Původní `C:/git/phpvpn` sloužil pouze jako zdroj kontextu. Cílem je výhradně Windows AMD64. ARM64 ani 32bit x86 nebyly sestavovány.

Podrobná [matice implementace a důkazů](implementation-plan3-matrix.md) obsahuje všech 20 bloků B00–B19, 41 kritérií A01–A41 a mapování všech 28 rozhodnutí D01–D28. PASS u jednotlivého testu není prohlášení, že prošla celá širší akceptační brána.

## Výsledné chování

Služba při startu nevytváří ani nenačítá náhradní konfiguraci. Každý Connect načte skutečný aktuální soubor, již přijaté připojení používá vlastní neměnný snapshot. Wizard má čtyři kroky, oddělené autentizační kontroly, ruční nové pokusy, pronajaté relace, korelaci pokusů a idempotentní Finish s potvrzením publikace. Ověřovací provoz má vlastní transport a neprochází tunelovými frontami ani provozními čítači.

Finish uloží celý validovaný dokument, vyžádá uzavření relace a právě jednou otevře prázdné zavíratelné Communication Contracts. Neprovádí Connect. Edit Config nabízí samostatné výslovné vytvoření chybějícího souboru a Reset mění pouze koncept v editoru. Text se zpracovává jako striktní UTF-8, odstraní se pouze jeden počáteční BOM. Platný text obsahující NUL lze v editoru bezeztrátově upravit v označeném JSON string literal zobrazení.

Server, C++ a Python používají přesný default `CHANGE_THIS_SERVER_SECRET`, pouze nulová délka aktivuje jeho normalizaci. Server má výchozí klíče pro `.2` až `.15`. Normativní výchozí limity jsou 983040 bajtů a doporučené intervaly 50 až 3200 ms. Běžný datový provoz nadále vyžaduje ověření payload GCM. Výjimka patří pouze diagnostice, která přijaté zprávy zahodí.

## Doplnění po původním finálním běhu

Následně prošlo skutečné měření maximální obsazenosti IPC bufferů, vynucené pomalé DNS a HTTP 503 při contention. Poslední scénář odhalil a opravil serverovou chybu, timeout zámku nyní vrací normativní `503 queue_busy`. Po opravě prošlo 486 PHP assertions a úplná Python regrese 386 PASS / 1 SKIP za 233,85 s. Skip starého náhodného hammer testu doplňuje nový deterministický PASS se dvěma HTTP workery. Přesné metriky, pozitivní kontroly, neúspěšné mezikroky a úklid uvádí [navazující report](implementation-plan3-resource-report.md).

## Provedené testy

Prostředí: Windows 11 Pro 10.0.22631, MSVC 19.44.35228, toolset 14.44.35207, CMake/Ninja, PHP 8.5.9 v `D:/nginx/php8.5.9/php.exe`, Python 3.13 přes uv. Běžné testy běžely bez administrátorského tokenu.

| Běh | Výsledek | Evidence a rozsah |
|---|---|---|
| Debug po finální recovery opravě | 215 registrovaných případů, 0 failed, 12,65 s | [log](C:/Users/User/.codex/worktrees/d6c8/phpvpn/work/build/debug-plan3-verified.log), [výpis případů](C:/Users/User/.codex/worktrees/d6c8/phpvpn/work/build/debug/Testing/Temporary/LastTest.log) |
| Release, preset RelWithDebInfo | 215 registrovaných případů, 0 failed, 12,55 s | [log](C:/Users/User/.codex/worktrees/d6c8/phpvpn/work/build/release-plan3-verified.log), [výpis případů](C:/Users/User/.codex/worktrees/d6c8/phpvpn/work/build/release/Testing/Temporary/LastTest.log) |
| AddressSanitizer | 215 registrovaných případů, 0 failed, 15,81 s, bez nálezu ASan | [log](C:/Users/User/.codex/worktrees/d6c8/phpvpn/work/build/asan-plan3-verified.log), [výpis případů](C:/Users/User/.codex/worktrees/d6c8/phpvpn/work/build/asan/Testing/Temporary/LastTest.log) |
| Windows ConfigFiles s UAC | 13/13, exit 0, žádné NOT RUN | [log](C:/Users/User/.codex/worktrees/d6c8/phpvpn/work/build/config-files-admin.log), [exit](C:/Users/User/.codex/worktrees/d6c8/phpvpn/work/build/config-files-admin.exit), izolované NTFS adresáře |
| Skutečná Win32 okna | 7/7, 0 failed | [log](C:/Users/User/.codex/worktrees/d6c8/phpvpn/work/build/ui-plan3-verified.log), produkční okna/readery, vlastní PID a privátní pipe s testovacím backendem |
| C++ proti skutečnému PHP | 3/3, 0 failed, 0,46 s | [log](C:/Users/User/.codex/worktrees/d6c8/phpvpn/work/build/contract-plan3-verified.log), skutečný ProbeClient, NetworkWorker, WinHTTP a CNG |
| PHP vlastní testy | 486 assertions PASS | [log](C:/Users/User/.codex/worktrees/d6c8/phpvpn/work/build/php-plan3-final.log) |
| Deploy profily | PASS | [log](C:/Users/User/.codex/worktrees/d6c8/phpvpn/work/build/deploy-profiles-plan3-final.log), přesné integration a wizard-default profily, odmítnutí nečekané hlavičky před zápisem |
| Úplná Python integrace | 386 passed, 1 skipped, 228,41 s | [log](C:/Users/User/.codex/worktrees/d6c8/phpvpn/work/build/pytest-plan3-verified.log), [JUnit](C:/Users/User/.codex/worktrees/d6c8/phpvpn/work/build/pytest-plan3-verified.xml), izolovaný výchozí relay |
| Opravené limity a headers samostatně | 17 passed, 1 skipped | [log](C:/Users/User/.codex/worktrees/d6c8/phpvpn/work/build/pytest-plan3-corrected.log) |
| Skutečné SCM/Wintun včetně pádu | PASS, exit 0 | [controller](C:/Users/User/.codex/worktrees/d6c8/phpvpn/work/build/service-e2e.log), [exit](C:/Users/User/.codex/worktrees/d6c8/phpvpn/work/build/service-e2e.exit), [úklid](C:/Users/User/.codex/worktrees/d6c8/phpvpn/work/build/service-e2e-cleanup.log) |
| Distribuce VerifyOnly | PASS | [log](C:/Users/User/.codex/worktrees/d6c8/phpvpn/work/build/package-verify-plan3-verified.log), manifest, 11 hashovaných souborů, architektura, Wintun podpis a úplné API bez vytvoření adaptéru |

**Počty 215 nejsou 215 plně provedených privilegovaných scénářů.** V každém neadmin běhu bylo 10 privilegovaných větví označeno NOT RUN. Tyto větve pokryl samostatný administrátorský běh 13 případů ConfigFiles, nikoli neprivilegovaný ASan. CTest registruje jeden spustitelný micro-harness, počet případů je v jeho LastTest.log.

Původní Python skip je `test_headers.py`: fixture během náhodného contention nevytvořil HTTP 503. Následný deterministický test odhalil i chybné mapování lock timeoutu na 200/deferred. Server je opravený a skutečný 503 je nyní doložen v navazujícím reportu. Původní neúspěšné běhy jsou zachovány v `pytest-plan3.log` (384/2/1) a `pytest-plan3-final.log` (385/1/1). Příčinou byla zastaralá očekávání výchozích limitů a přesné normativní veřejné varování obsahující `phpvpn.php`, které obecný leak test zamítal. Test nyní vyjímá pouze toto přesné varování na veřejné status stránce. Ostatní leak kontroly zůstaly zachovány.

Kontraktní test ověřuje všech 14 výchozích identit, nesprávné secrety/klíče, diagnostické zahazování zprávy s vadným payload tagem a odmítnutí stejné zprávy normálním bootstrapem. Kontrola s nedostupným serverem skončila nenulovým kódem. Je to skutečný HTTP/crypto kontrakt, nikoli důkaz běžící služby nebo tunelu.

UI testy pokrývají Next/Back, checkboxy, přesný secret s řídicími znaky, Reset, Escape/X/Cancel, malé okno, Tab/Shift+Tab/Enter, pozdní start ACK a výsledek, 15s heartbeat s epochou a Resume, asynchronní Reset editoru a nezávislý Disconnect při čekajícím get-info. Po Finish navíc kontrolují právě jeden serverový close. Privátní Disconnect byl přijat i potvrzen v 0 ms v rozlišení GetTickCount64, zatímco get-info stále čekal. To neznamená nulovou fyzikální latenci ani měření skutečné služby.


Dvě průběžné souběžné Release/ASan regrese v `release-plan3-host.log` a `asan-plan3-host.log` selhaly pouze na testové dolní mezi `elapsed >= 900ms`. Následný samostatný běh prošel, konkrétní původ dřívějšího timeoutu nebyl změřen. Bezpodmínečná dolní mez byla odstraněna, protože nativní TCP/IP může ohlásit ERROR_WINHTTP_TIMEOUT nezávisle na nastavených WinHTTP intervalech. Test dál vyžaduje správnou timeout chybu, horní mez a skutečné částečné bajty, nově vypisuje režim a elapsed. Původní neúspěchy zůstávají evidované. [Microsoft: ERROR_WINHTTP_TIMEOUT](https://learn.microsoft.com/en-us/windows/win32/winhttp/error-messages#ERROR_WINHTTP_TIMEOUT).

## Vizuální kontrola

Nástroj computer-use se dvakrát nepodařilo inicializovat kvůli `failed to write kernel assets`, chyba OS 3. Následně byl úspěšně použit screen-master a snímky skutečných monitorů byly prohlédnuty. Nepoužila se externí PowerShell UI Automation.

Na monitoru 1920×1200 při 96 DPI byly prohlédnuty kroky identity a secret. Na monitoru 2560×1600 při 144 DPI bylo prohlédnuto malé okno identity a krok Contracts. Testovací přesun vlastního okna mezi monitory vyvolal skutečný WM_DPICHANGED. Kontrola odhalila šedé pozadí popisků a vedla k opravě bílého pozadí, barev stavů a víceřádkových checkboxů. Následné snímky ukazují přístupná čtyři tlačítka bez překryvu, čitelný obsah a svislé posouvání malé plochy. První identity snímek je historický před touto vizuální opravou.

Snímky a metadata jsou mimo repozitář v `C:/Users/User/.codex/visualizations/2026/09/06/01a076c4-0e06-7861-a023-e84e5f425c42/`:

- `plan3-ui-identity/2026-09-06-14-50-51-capture.json`, monitor 02, 100 %, před opravou barev pozadí.
- `plan3-ui-small/2026-09-06-14-53-22-capture.json`, monitor 01, 150 %, malá plocha po opravě.
- `plan3-ui-secret/2026-09-06-14-54-11-capture.json`, monitor 02, 100 %, po opravě.
- `plan3-ui-contracts/2026-09-06-15-02-32-capture.json`, monitor 01, 150 %, po opravě.

Výpočty geometrie mají testy pro 96/120/144/192 DPI. Skutečné monitory při 125 % a 200 %, všechny vizuální chybové stavy a kompletní ruční průchod všech textů nebyly provedeny. Screenshoty obsahují okolní plochu, proto nejsou součástí distribučního ZIPu.

## Implementované meze a změřená dostupnost

| Prostředek | Mez |
|---|---|
| Přijatá pipe spojení včetně listenerů | 32 |
| Jedna zakódovaná IPC zpráva | 4 MiB |
| Součet request/response bufferů | nejvýše 256 MiB plus 2 MiB pevných I/O bloků |
| Fronta krátkého řízení / souborových akcí | 16 / 4, samostatní pracovníci |
| Dlouhá diagnostika | jedna operace celkem, bez fronty |
| Příjem zprávy / odeslání odpovědi | 5 s / 5 s, absolutní monotónní termín |
| Konfigurační soubor | 512 KiB, čtení nejvýše jeden detekční bajt navíc |
| Wizard | 8 relací, keepalive 15 s, lease 60 s, tick 250 ms |
| Potvrzení publikace | 128, po dobu relace a alespoň 10 minut po close |
| Celkový Next | 1/2/3 HTTP rozpočty + 15 s lokálního/IPC rozpočtu |
| responseTimeoutSeconds | 1 až 2147483, vyšší hodnota odmítnuta před HTTP |

Skutečná privátní pipe byla testována s 8 neúplnými requesty, 8 klienty neodebírajícími odpověď, pomalou diagnostikou, pomalým souborem a souběžným krátkým řízením. Cílený běh naměřil get-status/Disconnect 16 ms a stop pipe serveru 16 ms. Obnova 32 slotů po absolutním timeoutu měla 5047 ms. Přenesena byla i přesná hraniční 4MiB zpráva.

Devět skutečných WinHTTP testů pokrývá pomalé hlavičky, TLS handshake, průběžné tělo, absolutní deadline, souběžný cancel a opakované použití transportu, účtování částečně přijatých bajtů a chyby/hranice nastavení timeoutu. Samostatné zadržení DNS bylo následně doloženo procesovým resolverem, skutečný timeout za 1016 ms, cancel a reuse prošly, viz navazující report. Finální uvolnění kontextu čeká na HANDLE_CLOSING. Bezpečnost tohoto pořadí závisí na dokončení callbacku Windows, kód při finálním drain používá INFINITE. Z naměřených rychlých cancelů se nevyvozuje pevná mez při zastavení OS nebo libovolném zahlcení.

ConfigFiles používá skutečné NTFS, připnuté identity/handly, chráněné ACL, omezené čtení, flush a journal před publikací. Finální backend používá FileRenameInfoEx s POSIX replacement. Starší B01 prototyp s ReplaceFileW je pouze historický průzkum API. Testy injektují selhání zápisu, flush, publikace a recovery. Nejde o skutečný výpadek napájení.

## Skutečné SCM/Wintun E2E

**PASS, exit 0.** [Controller log](C:/Users/User/.codex/worktrees/d6c8/phpvpn/work/build/service-e2e.log), [exit](C:/Users/User/.codex/worktrees/d6c8/phpvpn/work/build/service-e2e.exit), [úklid](C:/Users/User/.codex/worktrees/d6c8/phpvpn/work/build/service-e2e-cleanup.log), [baseline](C:/Users/User/.codex/worktrees/d6c8/phpvpn/work/build/service-e2e-baseline.json), [stav po testu](C:/Users/User/.codex/worktrees/d6c8/phpvpn/work/build/service-e2e-after.json).

Samostatný opt-in target `PHPVPN_BUILD_SERVICE_E2E` používá produkční service_host, engine, WizardService, ConfigFiles, WinHTTP, CNG a Wintun backend. Má vlastní SCM jméno `phpvpn-plan3-e2e-d6c8`, privátní pipe, adaptér `PHPVPN Plan3 E2E d6c8`, GUID `{4b67d77c-70a2-49f6-97db-a5f4d6c8e2e1}` a data pod build adresářem. Produkční executable nepřijímá přepínače těchto identit z CLI, JSON ani IPC.

Provedené scénáře:

1. Skutečný SCM start bez configu, platný disconnected status a neúspěšný Connect bez vytvoření souboru.
2. Reálný Wizard open a tři explicitní Next přes PHP, bez zápisu před Finish.
3. Finish se 13 kontrakty, potvrzená publikace, replay stejného commitId se stejným receipt, close bez implicitního Connectu.
4. Skutečný Connect, Wintun session, přesná IPv4 a split-tunnel routa na ověřeném LUID. Výchozí routa a DNS beze změn.
5. Ochrana phantom cleanup odmítla živý adaptér i nesprávné GUID a ponechala síťový stav beze změny.
6. Reálný Windows UDP socket `.2` poslal náhodný nonce přes Wintun, službu a PHP k produkčnímu peer NetworkWorker `.3`. Šifrovaná odpověď prošla zpět přes PHP a Wintun do původního socketu, obě strany čítačů vzrostly.
7. Disconnect přijat v 0 ms v rozlišení GetTickCount64, dokončen za 281 ms. Reconnect a SCM stop během connected za 188 ms. Čistý restart bez autoConnect a se stejným configem.
8. Skutečný pád pouze vlastní služby. Controller před TerminateProcess ověřil PID 30064, přesnou cestu binárky a znovu SCM identitu. SCM STOPPED nastal za 32 ms. Journal a config zůstaly zachované.
9. Restart obnovil přesné vlastnictví, odstranil osiřelý PnP záznam, journal a původní adresy/routy bez otevření adaptéru před autentizací. Poté prošel další skutečný Connect/Disconnect.

Runner odmítá existující testovací objekty včetně neaktivního PnP záznamu a konfliktní IPv4 prefix. Po běhu ověřil nepřítomnost testovací služby, adaptéru, PnP instance i prefixu. Produkční služba zůstala Running se stejným PID 42404, cestou, hashem configu a výchozími routami. Sdílený driver package nebyl odinstalován. Nejde o instalaci distribučního balíčku přes produkční install/upgrade/uninstall skript.

Crash test odhalil skutečnou chybu staršího startup recovery. Po pádu zůstala neaktivní PnP instance, kterou Get-NetAdapter neukazoval, ale alias/LUID lookup stále vracel. Pokus přes WintunOpenAdapter před autentizací porušoval normu a čekal přes 15 s. První neúspěch je v `pre-fix-crash-service-e2e.*`, následné bezpečné odmítnutí v `pending-cleanup-service-e2e.*` a preflight odmítnutí zbytku v `orphan-guard-service-e2e.*`. Explicitní oprava námi zanechaného záznamu má [vlastní log](C:/Users/User/.codex/worktrees/d6c8/phpvpn/work/build/phantom-remediation.log). První úspěšný běžný lifecycle s chybou PowerShell čtení exit kódu je zachovaný v `first-service-e2e.*`.

Finální recovery používá pouze metadata před autentizací. Po ověření instalačního journalu má 5s read-only toleranci pro úklid OS. Neaktivní zbytek smí odstranit jen po shodě současného GUID, aliasu, LUID, přesné SWD instance, Net class a `IsPresent=false`. Po odstranění znovu ověří nepřítomnost LUID a všech jeho řádků. Nejasnost, živé zařízení nebo požadavek restartu ponechá journal a startup chybu propaguje, nový Connect jej nepřepíše. Unit `runtime.ownership_recovery_never_opens_adapter_and_blocks_unresolved_identity` ověřuje zákaz driver load/open, zachování důkazu a blokaci startupu.

Nativní SetupAPI DIF_REMOVE nemá cancel/deadline ani atomický remove-if-phantom proti jinému administrátorovi. Identita a presence se kontrolují znovu těsně před voláním, vlastní lifecycle je serializovaný. Z měřeného úspěchu se nevyvozuje hard real-time záruka při zaseknutí OS. API podklady: [GetIfEntry2](https://learn.microsoft.com/en-us/windows/win32/api/netioapi/nf-netioapi-getifentry2), [SetupDiCallClassInstaller](https://learn.microsoft.com/en-us/windows/win32/api/setupapi/nf-setupapi-setupdicallclassinstaller), [rozsah odstranění jediné instance](https://learn.microsoft.com/en-us/windows/win32/api/setupapi/ns-setupapi-sp_removedevice_params).

## Distribuce

Finální ověřený balíček je v [dist](../../dist/plan3-x64-2026-09-06/package), ZIP [phpvpn-client-plan3-x64.zip](../../dist/plan3-x64-2026-09-06/phpvpn-client-plan3-x64.zip). Obsahuje tři EXE, originální x64 `wintun.dll` 0.14.1, licence, instalační/odinstalační skripty a podepsaný manifest. Manifest má 11 hashovaných souborů, s manifestem, podpisem a veřejným certifikátem celkem 14 souborů. Verze je 1.0.0, architektura x64.

Podepsáno existujícím místním certifikátem `CN=phpvpn Development Test Signing`, thumbprint `7430CCA24F5DE8A6614D582AFBFED7C80E0F2CA8`. Pinovaný RSA podpis manifestu i hashe prošly VerifyOnly. Authenticode EXE hlásí UnknownError, protože řetězec končí v nedůvěryhodné kořenové autoritě. To není produkční veřejně důvěryhodný podpis. Do trust store se nic nepřidávalo. Authenticode originálního Wintunu je Valid.

Balíček byl znovu sestaven po změnách produkčního klienta. Navazující oprava serverového lock timeoutu a nové testy nemění klientské binárky, PHP není v tomto ZIPu. Hash balíčku byl znovu ověřen a zůstal stejný. SHA-256 ZIPu: `3a7e7bba2ad30dd9432c90d2bb34cdc2d2302dec4cd0c2ea62ed62513477d5a8`. [Audit](../../dist/plan3-x64-2026-09-06/verification/package-audit-plan3.json) ověřil všech 14 rozbalených položek ZIPu proti distribučním souborům a všechny čtyři PE jako AMD64. [Soubor SHA-256](../../dist/plan3-x64-2026-09-06/phpvpn-client-plan3-x64.zip.sha256). Předchozí artefakty zůstaly v `work/build/distribution-before-e2e` a `phpvpn-client-plan3-x64-before-e2e.zip`.

## Reprodukce a otevřené hranice

Z kořene pracovního stromu: `client/tools/build-x64.ps1 -Preset debug -Configure -Test`, obdobně `release` a `asan`. Samostatné explicitní runner skripty jsou `run-tray-ui-tests.ps1`, `run-server-contract.ps1` a `run-service-e2e.ps1`. UI vyžaduje interaktivní desktop. Kontrakt a E2E vyžadují izolovaný PHP relay a nesmějí sdílet jeho fronty s jinými testy. Python se spouští přes uv, přesný příkaz je v README tests-integration.

Nedoložené zůstávají čistá instalace/upgrade/uninstall balíčku, dva skutečně přihlášení uživatelé, odhlášení/uspání/probuzení OS, skutečné přerušení napájení, skutečná škálování 125/200 % a současná maximální zátěž celé SCM služby včetně všech tunelových front. RSS maximálních IPC bufferů, zadržené DNS i skutečný HTTP 503 jsou doloženy navazujícím reportem. Řízené modelové testy těchto stavů se nepřejmenovávají na skutečný OS E2E. Přesný rozsah je u jednotlivých A/B v matici. Konkrétní interaktivní zásahy a jejich důvody popisují [zbývající brány](e2e/manual-gates.md).

Nebyl vytvořen commit ani push. Závěrečná `git diff --check` prošla, Git hlásí pouze obvyklá upozornění na LF/CRLF. Stávající instalovaná služba a její konfigurace nejsou součástí testovacích mutací.
