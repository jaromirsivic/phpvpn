# T01: diagnostika nestabilních Wizard přechodů

Stav: **T01 NEUZAVŘEN, NOT READY**. Kořenová příčina tří historických selhání
nebyla prokázána. Nové úspěšné běhy nejsou důkazem opravy. Tento balíček zlepšuje
diagnostiku nativního testovacího hostitele, neoznačuje produkční Wizard za opravený.

Vlastní evidence je v `work/logs/wp04-t01`. Výchozí HEAD je
`f4b585ff2232d19626c870821a40218f525da213`. Před zápisem dvakrát ověřeno všech
341 položek posledního `wp08-final-review/final-files.json`, délky, SHA-256 a
přesný `final-status.txt`. Převzaté změny byly zachovány. Přečteny plán,
orchestrace, client.md, protocol.md, tray_UX.md a navazující WP08, geometry,
Connect, Editor lifecycle/timing a hidden-launch reporty. Zkoumány skutečný
WizardWindow, model, čtečky, obsluha zpráv, soukromý IPC server, testovací
backend, preview, launcher a související lifecycle testy.

## Co skutečně odhalily původní testy

`fix2_preferences_contract_preview` ověřuje editaci s 0, 1 a 13 existujícími
výchozími kontrakty, jejich náhled a přepínač doplnění, uložené preference,
automatickou IP/klíč, ruční změnu IP a zachování hodnot při Finish.
V původním `native-fix2_.log` první průchod selhal na `contracts step missing`.
URL a secret již prošly. Po Next z identity predicate do 3000 ms nepozoroval
současně existující Contracts checkbox ID716 a povolené Next ID701.
Původní log neříká, která podmínka nebyla splněna. V následném
`native-fix2-isolated.log` selhal některý z prvních dvou přechodů s
`Next did not reach expected step`. Tento text nerozlišoval URL → secret a
secret → identity. Nejde o důkaz chybného počtu kontraktů nebo ztráty preferencí,
k těmto assertions se dané provedení nedostalo.

`fix2_missing_suggestion_unsupported_cidr` ověřuje chybějící ověřený IP návrh,
zablokování dalšího HTTP pokusu při zapnuté automatické IP, explicitní ruční
fallback a nedostupné generování default kontraktů pro platnou /25.
V původním `native-fix2_.log` selhal již URL → secret nebo secret → identity
na `Next did not reach expected step`, tedy před kontrolou fallbacku a /25.
V následujícím isolated logu celý scénář prošel. Selhání neprokazuje vadu
/25, publikace konfigurace ani použití neověřené IP.

Původní čtyři provedení zůstávají **3 FAIL / 1 PASS**. Pozdější původní
pozorování WP08 mají 6/6 PASS a tehdejší diagnostické kopie 8/8 PASS.
Žádný jejich raw soubor nebyl přepsán nebo překlasifikován.

## Nová předem omezená reprodukce

Všechny scénáře používají skutečné produkční okno a soukromý UUID pipe backend.
Žádný běh není opakován do prvního PASS. Každý proces má nový pracovní adresář
a samostatný raw výstup. Původní dvojice se spouští v pořadí registry, jednotlivé
filtry v dalších čerstvých procesech. Cold zde znamená první otevření v novém
procesu, nikoli restart OS nebo vyprázdnění diskové cache. Warm jsou další
otevření v témže scénáři a druhý scénář dvojice.

| Před změnou testovacího hostitele | Rozsah | Výsledek |
|---|---:|---:|
| Neupravený skutečný Release | 3 × dvojice + oba jednotlivě | 12/12 PASS |
| Neupravený Debug | 3 × dvojice + oba jednotlivě | 12/12 PASS |
| Neupravený ASAN Release /O2 | 3 × dvojice + oba jednotlivě | 12/12 PASS |
| První diagnostická série, původní Release objekty | 6 dvojic | 12/12 PASS |
| Rozšířená diagnostická série, předem určeno 24 procesů | 24 dvojic | 48/48 PASS |
| Kontrolované SetActiveWindow(nullptr) před kliknutím, samostatná kopie | 1 dvojice | 2/2 PASS |

Celkem **96/96 běžných provedení** před změnou a dva samostatné kontrolované
testy neaktivního okna. Samotná neaktivita v této zkoušce selhání nevyvolala.
Trace zachycuje BM_CLICK, WM_LBUTTONDOWN/UP, WM_COMMAND, skutečné požadavky
a odpovědi backendu, přijetí zpráv do UI, timery, focus, capture a cílové prvky.
Nové záznamy neprokázaly ztracené kliknutí, ztracenou zprávu ani zamrznutí
přechodu. T01 scénáře samy netvoří dočasný konfigurační soubor ani screenshot,
backend drží konfiguraci v paměti. Oddělené cwd vylučují převzetí starých
relativních výstupů jako výsledku nového běhu.

První pokus spustit ASAN bez prostředí MSVC skončil před spuštěním testů
s prázdným výstupem. Není započítán jako PASS ani jako reprodukce T01.
Před ASAN maticí bylo zavedeno stejné prostředí MSVC x64 jako v původních
ASAN skriptech. První start regresního skriptu také neprovedl žádný test,
protože Windows odstranil koncovou tečku z názvu adresáře. Opravena pouze
lokální tvorba názvu logu. Tyto dva procedurální pokusy nejsou testové výsledky.

## Změna diagnostiky

Změny jsou omezeny na `client/tests/ui/tray_ui_tests.cpp`, `dev_preview.inc`,
nový `ui_diagnostics.inc` a README. `nextTo` rozlišuje přesný přechod v textu
chyby. Při timeoutu vždy vypíše původní limit a uplynulý čas, počty startů,
čtení operace a Finish, přítomnost modálního dialogu, focus a
present/visible/enabled pro Next a čtyři cílové prvky.

Volitelná proměnná `PHPVPN_UI_TRACE=1` zapíná omezený numerický záznam s
časem a thread ID, lokální WH_CALLWNDPROC hook, zprávy čerpané Gui, backend
request/reply a stavy preview. Trace se vypíše při návratu z main, včetně
počtu zahozených položek při dosažení kapacity 65536. Bez proměnné není hook
instalován ani rezervován buffer. Nezaznamenává text konfigurace, IPC payloady,
hesla, klíče nebo znakový obsah klávesových zpráv. Hook nemění doručování.

Původní predicate, 3000ms timeout, funkční pořadí, BM_CLICK, produkční čtečky,
limity, automatizace potvrzení a výsledek procesu zůstávají zachovány.
Nepřibyl retry, automatické opakování Next ani zpětná konverze FAIL na PASS.
Geometrie, hidden launcher a normativní dokumenty nemají vlastní změnu.

## Řízený negativní důkaz

Samostatné kopie v `timeout-proof` a `timeout-proof-observation` zdržují pouze
syntetickou odpověď operace o 3500 ms. Tato injekce není v checkoutu a není
tvrzením o příčině původního WP08. První kopie ověřuje skutečný 3000ms FAIL
a jeho diagnostiku. Druhá po zaznamenání původního FAIL pouze pozoruje stav
do 4× původního limitu a nakonec znovu vyhodí původní chybu. Neopakují kliknutí.
Tak lze odlišit pozdní skutečný přechod od úspěšného provedení testu.

Obě kopie skutečně skončily s exit 1 a 2/2 FAIL. Vždy byl zaznamenán
3000ms timeout URL → secret, jeden start, jedno čtení operace, žádný Finish,
žádný modal, přítomné zakázané URL/Next a chybějící secret. Pozorovací kopie
poté zachytila splnění původního predicate v 3640 a 3704 ms a stále skončila
se stejnými dvěma FAIL. Čtyři řízená selhání se nepočítají mezi přirozené
reprodukce T01. Záznamy jsou v `timeout-proof-results.json` a obou raw lozích.

## Regrese diagnostického doplnění

| Fáze a konkrétní rozsah | Výsledek |
|---|---:|
| První podoba diagnostiky, původní default matice Release/Debug/ASAN | 36/36 T01 PASS |
| První podoba, zapnutý trace ve všech třech konfiguracích | 6/6 T01 PASS |
| Unit: fix2., Wizard, WP04Lifecycle., TrayResources., TrayGeometry., TrayPlacement. | 30/30 PASS |
| Release + ASAN: wp04_, late_, happy_path, stored_identity, editor_reset_failure_generation, command_reader_private_disconnect | 34/34 PASS |
| Release + ASAN: Editor create/save/reset/end-session/destroy | 2/2 testy, 48 cyklů PASS |
| První úplný wp06_ se zapnutým trace | **8 PASS / 1 FAIL**, zachováno |
| První hidden launcher regrese | 19/19 kontrol PASS |
| Finální binárky: T01 dvojice s trace i bez něj, Release/Debug/ASAN | 12/12 PASS |
| Finální binárky: Password děti a zavření rodiče, trace, Release/Debug/ASAN | 3/3 PASS |
| Finální Release: celé wp06_ se zapnutým trace | 9/9 PASS, včetně všech 20 dev scén |
| Finální hidden launcher regrese | 19/19 kontrol PASS |

Selhání v první wp06_ regresi způsobilo **nové diagnostické čtení**:
`click` po synchronním Password OK předalo již zaniklý HWND do `state`, který
volal `child`. Chyba `test attempted to inspect a foreign or missing parent`
vznikla až po správném produkčním zavření. Oprava v `ui_diagnostics.inc`
před čtením potomků ověřuje `ownWindow(root)`. Neplatný handle pouze zaznamená
číselně a dál jej nečte. Žádný produkční postup se nezměnil. Původní
`dev-scenes-and-lifecycle-run/output.log` a exit 1 zůstávají zachovány.
Po změně proběhlo uvedené cílené ověření ve třech konfiguracích a celé wp06_.
Nejde o přepis výsledku ani opakování nezměněného testu do PASS.

Celkem je nově **150/150 běžných T01 provedení PASS**, dalších 2/2 PASS patří
kontrolované neaktivitě. Z toho posledních 12/12 běželo na konečné podobě
observeru. Žádný z těchto PASS nenahrazuje tři původní FAIL. Standardní matice
po první úpravě se po malé opravě výhradně zapnutého observeru neopakovala
celá. Finální dvojice s vypnutým observerem i jeho dotčené zapnuté cesty byly
ověřeny ve všech třech konfiguracích. Produkční zdroje pro unit/lifecycle
regresi byly po celou dobu stejné.

Editor lifecycle nejhorší cyklus měl 94 ms v Release a 63 ms v ASAN, tedy
prošel i původní mezí 100 ms. Dřívější historické časové FAIL jiných auditů
se tím nemění. Trace matice nezahodily žádný záznam. Nový úplný WP06 běh
ověřil reálný preview přes 45 sekund, 30s Save, soukromé endpointy, Exit,
Password a Wizard publikaci pouze v paměti. Nejde o měření plných Contracts
výkonnostních limitů, jejich odklad zůstává zachován.

## Integrita a reprodukce

Závěrečný scope obsahuje 343 souborů proti převzatým 341. Změněny jsou pouze
tři uvedené soubory UI harnessu, přidány `ui_diagnostics.inc` a tento report.
`scope-audit.json` dokládá nulovou nečekanou změnu, shodu všech 207 souborů
posledního auditu, všech 491 starších převzatých důkazů, 17 dist souborů a
7 produkčních PE. `client.md`, `tray_UX.md`, plány a produkční zdroje jsou
hashově stejné jako při převzetí. PHP/protocol mají stejné převzaté bajty
a jejich rozdíl proti HEAD je nadále pouze EOL. `git diff --check` má exit 0.
Při prvním volání auditu PowerShell považoval varování Git o CRLF za výjimku.
Zachováno v `diff-check-first.log`, následný audit vyhodnotil skutečný exit Git.

Nový PE scan má AMD64, stejné podpisové stavy a nulové fixture/trace markery.
Skutečný Release tray má 842240 B, `.text` 493056 B, `.rdata` 260608 B,
`.rsrc` 55808 B a SHA-256
`A54F62B29B43822EA599A9DDFD28379A62CBA682031A52250F8CE3E6CD77B947`.
Delta velikosti, sekcí i hashe proti převzetí je nulová. Čerstvé diagnostické
linker mapy znovu potvrzují nulové zatažení tří prezentačních modelů do
service/CLI a všechny tři v unit targetu. Relink vytvářel pouze kopie v evidence.

Připravené skripty jsou `run-matrix.ps1`, `build-diagnostic.ps1`,
`run-extended-trace.ps1`, `build-harness.ps1`, `run-regression.ps1`,
`run-final-validation.ps1`, oba timeout-proof skripty, `run-artifact-audit.ps1`
a `audit.ps1`. Fázové výsledky a raw logy zůstávají oddělené. Matice odmítají
přepsat existující adresář, nový běh vyžaduje nový název fáze. Původní tři
binárky jsou zachovány jako `before-*.exe` a původní zdroj jako
`tray_ui_tests.before.cpp`. Během práce se nic necommitovalo, nepushovalo,
neinstalovalo a nepoužil se worktree. Nebyla spuštěna skutečná služba, HTTP,
VPN, Wintun, SCM, produkční pipe nebo práce s ProgramData.

Pro další diagnostiku aktuálního hostitele lze použít:

~~~powershell
$env:PHPVPN_UI_TRACE='1'
./client/tools/dev-tray.ps1 -Mode Test -Configuration Release -Filter fix2_
Remove-Item Env:PHPVPN_UI_TRACE
~~~

Runner uloží vlastní unikátní stdout/stderr a nenulový výsledek zachová.

Finální hidden-launch evidence je
`work/logs/hidden-launch/20260908-165953-099-68d85c52e41b4b389ab611248cf84f2a`.
Obsahuje Debug/Release, skutečný zdrojový strom v cestě s mezerami, no-op,
nulový filtr, neplatné argumenty a kontrolované compile/PE/lock chyby.
Na konci nezůstal spuštěný vlastní testovací nebo preview proces.

## Omezení a předání

Třísekundová testovací mez je kratší než produkční rozpočet pokusu
`počet HTTP × responseTimeoutSeconds × 1000 + 15000`. Samotný tento rozdíl
nedokazuje příčinu historických selhání. Jejich raw logy postrádají potřebné
časové a stavové údaje. Ani 4× uživatelská časová výjimka neumožňuje FAIL
odstranit nebo tvrdit opravu bez důkazu.

Nebyla zjištěna reprodukovaná produkční chyba, a proto nebyl proveden
spekulativní WP04 zásah. K uzavření T01 je stále potřebný reprodukovaný FAIL
s rozlišujícími údaji a ověřenou příčinou. Přidaná diagnostika takový další
běh umožňuje zachytit bez změny scénáře. Konečná připravenost WP08 zůstává
**NOT READY**, toto předání není oprávnění k produkčnímu vydání.
