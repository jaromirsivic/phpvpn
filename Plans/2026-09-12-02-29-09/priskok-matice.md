# Pracovní matice konsolidace

Stav: dokumentační D01–D17 dokončeny a závěrečná kontrola V01–V06 provedena, viz [report V01–V06](../../.superpowers/sdd/plan-v003/task-18-final-validation-report.md). R01–R03 a R05–R10 jsou dokumentačně uzavřená, R04 výslovně odložené. Úplný neměnný [plan-v003.md](plan-v003.md) je uložen a ověřen, viz [ověření v3](overeni-planu-v003.md). Předchozí v1/v2 zůstávají zachované. Dokumentace, budoucí implementace backpressure a testovací větev jsou oddělené. Autoritativní zadání tvoří intent-v001 až intent-v004 a [nová uživatelská rozhodnutí](rozhodnuti-v004.md). Realizace upravila client.md, tray_UX.md a samostatně schválené testy. protocol.md, produkční kód a šestnáct původních zdrojů zůstaly beze změny. Celá testovací větev není PASS, přetrvávají čtyři selhání ContractsDocument a uvedené NOT RUN.

Časový limit: **unlimited**, výslovně schváleno uživatelem a předáno koordinátorem po původním bezpečném zastavení. Starý deadline se již nepoužívá. Žádní další agenti nevznikají. Při obnově kontextu nejprve načíst celý účinný globální blok instrukcí z reálného CODEX_HOME/AGENTS.override.md nebo AGENTS.md, případně úplný hostem dodaný blok, a ověřit obě přesné hranice před další věcnou prací. Poté přečíst aktuální matici a rozhodnutí.

## Zdroje v závazném pořadí

| ID | Soubor |
|---|---|
| S01 | plan_client.md |
| S02 | plan2_client.md |
| S03 | plan3_client.md |
| S04 | plan3_fix.md |
| S05 | plan3_fix2.md |
| S06 | plan3_fix3.md |
| S07 | plan3_fix3_problems.md |
| S08 | plan3_fix3_problems2.md |
| S09 | plan3_fix3_work_for_agents.md |
| S10 | notes/2026-09-04-18-59-01_notes.md |
| S11 | notes/2026-09-04-22-20-52_notes.md |
| S12 | notes/2026-09-05-08-25-34_notes.md |
| S13 | notes/2026-09-05-09-53-11_notes.md |
| S14 | notes/2026-09-06-22-21-54_notes.md |
| S15 | notes/2026-09-07-11-19-48_notes.md |
| S16 | notes/2026-09-09-20-43-00_notes.md |

Před zdroji byly načteny všechny tři cílové dokumenty. Jejich role: client.md klient/služba/IPC, protocol.md wire a interoperabilita, tray_UX.md prezentace a interakce. Historické reporty mohou sloužit jako orientace k testům, nikdy jako další schválený požadavek. Každý tematický detail se ukládá samostatně do TNN.md, tento registr udržuje jeho stav. Uzavřeno znamená dokončenou analýzu, nikoli vždy implementováno.

## Registr témat

Aktuální stav rozhodnutí má přednost před historickými komentáři níže: R01/T04 uzavřeno vyjmutím nedoložených corner cases, R02/T05 uzavřeno s budoucí B větví, R03/T06 uzavřeno na současné recovery, R04/T08 odloženo, R05/T09 současný installer, R06/T13 Users read, R07 všechny dotčené UI notes bez implementace irelevantní, R08/T19 dynamická HC, R09/T24 výsledkové dirty, R10/T22 deset hodnot a jednotky. Jednadvacet dotčených témat má novou vrchní část Aktuální rozhodnutí po intent-v004, historická analýza pod ní se zachovává pro dohledatelnost.

| ID | Téma | Stav |
|---|---|---|
| T01 | Platforma, build, členění a přenosnost | analýza uzavřena, D01 doplněno v client.md (Architektura, Sestavení klienta), TT01 bez nového testu, build a linkerové mapy NOT RUN, viz [T01](T01.md) a [report D01](../../.superpowers/sdd/plan-v003/task-1-report.md) |
| T02 | Konfigurační schéma, CIDR a runtime limity | D02 doplněno v Konfiguračním souboru, TT02 doplněno a ověřeno, 24 testů bez selhání, viz [report T02](../../.superpowers/sdd/plan-v003/task-2-report.md) |
| T03 | Kryptografie, wire parsery a payload IPv4 | D03 doplněno, TT03 ověřeno, 57 cílených testů PASS, viz [report úlohy 3](../../.superpowers/sdd/plan-v003/task-3-report.md) |
| T04 | Časová synchronizace | D11 upraveno podle R01, perioda od úspěšného snímku a refresh po datovém cyklu, bez recovery po samotném health failure a bez OS triggerů. TT04 zachovává existující testy, 9 TimeSync + 19 runtime + 1 konfigurační test PASS. Periodický lifecycle a extrémní hodnoty tím nejsou ověřené. Viz [T04](T04.md) a [report D11](../../.superpowers/sdd/plan-v003/task-11-report.md). |
| T05 | Fronty, plánování a backpressure | D04 doplněno, původní ověření viz [report úlohy 4](../../.superpowers/sdd/plan-v003/task-4-report.md). D12/R02 dokumentačně dokončeno: maximum běžného pollingu 5 s, bez povinnosti odeslat při kapacitní blokaci, současné low/high bez záruky oboustranného clampu extrémů. Dodané per-packet admission odděleno od plánované pre-poll ochrany. 18 existujících queue/scheduler/runtime testů PASS, bez nového testu. B01–B05 zůstávají budoucí poslední samostatný implementační blok, B04 a souběžný deferred/error scénář NOT RUN. Viz [T05](T05.md) a [report D12](../../.superpowers/sdd/plan-v003/task-12-report.md). |
| T06 | VPN role, bootstrap, recovery a checkpointy | D05 a D13 doplněno, R03 popsáno podle teardown/recreate, snapshotu, checkpointů a backoffu. TT06 znovu přezkoumáno bez duplicitních testů, 19 runtime testů PASS v D13. Viz [T06](T06.md), [report úlohy 5](../../.superpowers/sdd/plan-v003/task-5-report.md) a [report úlohy 13](../../.superpowers/sdd/plan-v003/task-13-report.md). R04 zůstává odložené. |
| T07 | HTTP deadline, rušení a izolovaná diagnostika | D13 přezkoumáno, správný HTTP text ponechán. TT07 bez nového fake testu, 9 existujících WinHTTP testů PASS. Izolované DNS/send a řízený cancel versus success zůstávají NOT RUN, nikoli univerzální důkaz bounded HANDLE_CLOSING. Viz [T07](T07.md) a [report úlohy 13](../../.superpowers/sdd/plan-v003/task-13-report.md). |
| T08 | Wintun, síťové vlastnictví a reconciliation | analýza dokončena, R04 odloženo bez realizace, viz T08.md |
| T09 | Instalace, upgrade, rollback a distribuce | D14/R05 doplněno v client.md, skutečné pořadí instalace a omezení upgrade/rollback. TT08: 6 izolovaných VerifyOnly scénářů a 2 existující unit testy PASS. Reálný install/SCM/Wintun E2E a RemoveData NOT RUN. R04 zůstává odložené. Důkazy a meze kontroly vedlejších účinků viz [report úlohy 14](../../.superpowers/sdd/plan-v003/task-14-report.md) a [T09](T09.md). |
| T10 | IPC framing, dostupnost a bezpečné chyby | D06 doplněno v client.md, Implementační meze lokálního řízení. TT09 bez změny suite, běhy a meze důkazu viz [report úlohy 6](../../.superpowers/sdd/plan-v003/task-6-report.md). |
| T11 | Provozní čítače a status/diagnostics | uzavřeno, viz T11.md |
| T12 | ConfigStore, UTF-8, repair a atomická publikace | D15/TT10 ověřeno, 8 ConfigStore testů PASS. Z 21 ConfigFiles scénářů 20 NOT RUN kvůli tokenu, nejde o NTFS publikační PASS. Viz [report D15](../../.superpowers/sdd/plan-v003/task-15-report.md) a [T12](T12.md). |
| T13 | ACL datového stromu a startup default | D15/R06 sjednoceno, záměrné Users read celého stromu včetně historie, zápis a publikace chráněné. 4 ACL testy PASS, plný startup → default → editor Cancel → druhý start NOT RUN. Viz [report D15](../../.superpowers/sdd/plan-v003/task-15-report.md) a [T13](T13.md). |
| T14 | Default secret, access klíče a testovací profily | D10 doplněno v client.md, Testovací profily serveru. TT11 bez duplicitních testů, 24 C++ a 46 Python testů PASS, PowerShell regrese profilů PASS. Autentizace všech 14 adres proti skutečnému PHP existuje, nový síťový běh NOT RUN, připravená fixture má pouze 4 integrační identity. Viz [T14](T14.md) a [report D10](../../.superpowers/sdd/plan-v003/task-10-report.md). |
| T15 | Wizard metadata, identity a aditivní kontrakty | D15/R07, notes S13 bez implementace jsou pro konsolidaci nerelevantní. Metadata a nedestruktivní Reset zachovány. TT12 doplněna jediná vazba uložené false → modelový auto návrh → Finish → nové open, bez další osmikombinační matice. Viz [report D15](../../.superpowers/sdd/plan-v003/task-15-report.md) a [T15](T15.md). |
| T16 | Wizard probe, jednorázové pokusy, epochy a lease | D07 doplněno: rozpočty Next 45/75/105 s, IPC start 5000 ms a odkazy. TT13 doplněna nativní UI/pipe/runtime vazba ztracený start → cancel-attempt → opožděný start, 0 HTTP. Cílené testy PASS, viz [report 7](../../.superpowers/sdd/plan-v003/task-7-report.md). |
| T17 | Wizard Finish, receipts a návazné okno | D07 doplněno: kapacita 128, živá relace a nejméně 600000 ms po uzavření, pending/neterminální záznamy neexpirují jen časem. R07 zachováno. Službové testy PASS, Finish call counts převzaty z historické akceptace. Nový UI běh nedokončen kvůli ovládání TaskDialog v harnessu, viz [report 7](../../.superpowers/sdd/plan-v003/task-7-report.md). D15/R07: placeholder z notes S13 je nerelevantní pro konsolidaci, Finish bez automatického Contracts/Connect zachován, viz [report D15](../../.superpowers/sdd/plan-v003/task-15-report.md). |
| T18 | Menu, singleton oken a nezávislý Disconnect | D15/R07, neimplementované pořadí menu z notes S10/S13 nerelevantní pro konsolidaci, současné menu zachováno. Viz [report D15](../../.superpowers/sdd/plan-v003/task-15-report.md) a [T18](T18.md). |
| T19 | Ikony, sdílený rendering, monitor/DPI a přístupnost | D16/R08 dokončeno: client.md Sdílená Win32 infrastruktura a UX02 popisují dynamickou paletu při stabilních aktivech. TT15: 19 testů Win32 základu/geometrie PASS a 24 bitmap PASS, fyzické HC/DPI a accessibility NOT RUN. Viz [report D16](../../.superpowers/sdd/plan-v003/task-16-report.md) a [T19](T19.md). |
| T20 | Connect progress a bezpečná prezentace výsledku | D15/R07, neimplementované vzhledové varianty notes S10 nerelevantní pro konsolidaci, současné karty a odpočet zachovány. Nový vizuální běh NOT RUN. Viz [report D15](../../.superpowers/sdd/plan-v003/task-15-report.md) a [T20](T20.md). |
| T21 | Edit Config a Show Info | D15/TT10, editor Cancel staticky ověřen jako zavření bez Save. Skutečná spojená startup/editor akceptace NOT RUN. Viz [report D15](../../.superpowers/sdd/plan-v003/task-15-report.md) a [T21](T21.md). |
| T22 | Statistics UI | D17/R10 dokončeno: client.md Statistics/test 34 a tray_UX.md UX00/UX07/UX14 popisují deset hodnot z devíti čítačů, 3/3/4, Total size a desetinné jednotky. Dva cílené testy doplnily vyšší hranice a přesný součet před formátováním, 26 testů PASS. Nativní WP03Windows a vizuální/Release brána NOT RUN. Starý UI README zůstává navazující drobností mimo rozsah. Viz [T22](T22.md) a [report D17](../../.superpowers/sdd/plan-v003/task-17-report.md). |
| T23 | Wizard vzhled, texty a navigace | D15/R07, destruktivní Reset a vypnutí CIDR validace z notes S13 nerelevantní pro konsolidaci, současná validace a navigace zachovány. Viz [report D15](../../.superpowers/sdd/plan-v003/task-15-report.md) a [T23](T23.md). |
| T24 | Contracts model, řádky a preserving Save | D16/R09 dokumentace a dirty regrese dokončeny: client.md Contracts a UX12 používají výsledkový stav, včetně disabled-only a shodného výsledku. TT17: ContractsModel 10 PASS. Širší ContractsDocument 2 PASS / 4 FAIL kvůli starým předpokladům vyplněného defaultu, bez změny produkce. Viz [report D16](../../.superpowers/sdd/plan-v003/task-16-report.md) a [T24](T24.md). |
| T25 | Random, Password SHA a cílové identity | D15/R07, neimplementovaná modalita notes S15 nerelevantní pro konsolidaci, nemodální Password zachován. D16/R09: UX11/12 a client.md odvozují dirty od výsledku, PasswordKey 4 PASS, nativní čtečka/přepínač NOT RUN. Viz [report D15](../../.superpowers/sdd/plan-v003/task-15-report.md), [report D16](../../.superpowers/sdd/plan-v003/task-16-report.md) a [T25](T25.md). |
| T26 | Ruční Exit a systémový konec relace | uzavřeno, viz T26.md |
| T27 | Izolovaný GUI runner | uzavřeno, viz T27.md |
| T28 | Testovací brány, runtime/visual evidence a dokumentační autorita | D09 redakčně doplněno v client.md a tray_UX.md, aktuální kontrakt oddělen od datované historie. UX14 zachovává runtime/vizuální meze. Bez nového testu, pouze textové a odkazové kontroly, runtime/visual NOT RUN. TT19 zůstává samostatné, R04 odložené a backpressure nedodané. Viz [T28](T28.md) a [report D09](../../.superpowers/sdd/plan-v003/task-9-report.md). |
| T29 | Logování a provozní bezpečnost | D08 doplněno v client.md, Logování a diagnostika. TT20 doplněno, izolovaný logger test PASS (1 test, 0 selhání). Event Log a zvláštní startup warning nejsou dodané. Viz [report 8](../../.superpowers/sdd/plan-v003/task-8-report.md). |

## Pravidla uzavření

Index [pokryti-zdroju.md](pokryti-zdroju.md) mapuje všech 16 zdrojů včetně číslovaných rozhodnutí, akceptací a historických/neimplementovaných částí. Všechny T01–T29 mají dopad na konstrukci testů. Konkrétní cesty testových souborů byly porovnány s inventářem. Původní plánovací analýza netvrdila výsledky buildů a runtime testů. Následné skutečné běhy a jejich meze evidují jednotlivé reporty D01–D17 a závěrečný report, jejich PASS nenahrazuje neprovedené scénáře.

Další kontrola přímo proti cílovým nadpisům opravila T27 na ponechat existující návod a doplnila skutečně chybějící kapacity IPC, rozpočty Next a retenci receipts do T10/T16/T17. Dřívější obecné tvrzení o již uvedených číselných mezích se tím nahrazuje konkrétními zjištěními.

Závěrečná kontrola doplnila v T14 konkrétní oddělené deployment profily a již existující Python/PowerShell testy. V T12 a T19 opravila přesnou implementační cestu a zdrojovou sekci. Původní rozpor T22 byl nyní uzavřen rozhodnutím R10: deset zobrazovaných hodnot a převod bajtů je schválený stav. Kontrola pokrytí všech šestnácti zdrojů je dokončená.

Každý TNN.md musí obsahovat chronologické zdroje a původní záměr, změny a přednost, konečný záměr, konkrétní důkaz kódu odděleně od existujících testů, klasifikaci, přesný dopad na každý cílový dokument, otevřená rozhodnutí a dopad na konstrukci testů včetně druhu, rozsahu a rizika. Neexistence testu sama neznamená neimplementováno. Před vytvořením plánu musí být uzavřena všechna témata a provedena kontrola pokrytí všech věcných bodů zdrojů.

## Závěrečná validace V01–V06, 12. 9. 2026

[Závěrečný report](../../.superpowers/sdd/plan-v003/task-18-final-validation-report.md) dokládá PASS V01–V06 v rozsahu dokumentační větve, nikoli shodu všech historických požadavků nebo akceptaci vydání. Všech šestnáct zdrojů a protocol.md má shodný SHA-256 s D00. Produkční kód zůstává beze změny. Vstupní rozpracované testy a progress koordinátora byly zachovány.

T24/T25: při V05 minimálně opraven již hlášený rozpor automatického předvyplnění chybějícího klíče. client.md Architektura/Přenosné jádro a tray_UX.md UX02/UX10/UX11 nyní rozlišují prázdný unchecked řádek od explicitního odvození deterministického klíče. Existující klíč se zachová, zapnutí prázdného řádku blokuje Save. Čerstvých 10 ContractsModel testů PASS. Širší nativní Password UX tím není akceptované ani úplně konsolidované.

Čerstvá závěrečná sada: 54 testů, 50 PASS a 4 FAIL. Stejná čtyři selhání ContractsDocument jako v D16 trvají, testové fixture se neopravovaly. Úplná TT17 a celá testovací větev nejsou PASS. Fyzické OS/elevated, plná vizuální/Release brána a další neprovedené scénáře jsou NOT RUN. Nedokončený UI Finish z D07 se nepřeznačuje na PASS.

R04 zůstává odložené, jeho historický cleanup rozpor zůstává výslovně mimo tuto akceptaci. B01–B05 jsou plánované, neimplementované a B04 NOT RUN. Dokumentační dokončení tyto dva stavy nemění.
