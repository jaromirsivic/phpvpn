# Pracovní matice konsolidace

Stav: revize plánu po schválení R01–R10 a intent-v004. R01–R03 a R05–R10 jsou uzavřená, R04 výslovně odložené. Úplný neměnný [plan-v003.md](plan-v003.md) je uložen a ověřen, viz [ověření v3](overeni-planu-v003.md). Předchozí v1/v2 zůstávají zachované. Dokumentační kroky, budoucí implementace backpressure a testovací větev jsou oddělené. Autoritativní zadání tvoří intent-v001 až intent-v004 a [nová uživatelská rozhodnutí](rozhodnuti-v004.md). Kód, testy, cílové dokumenty a původní zdroje se nemění.

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
| T04 | Časová synchronizace | uzavřeno s R01, viz T04.md |
| T05 | Fronty, plánování a backpressure | D04 doplněno, současné fronty/plánování v TT05 ověřeny, 14 testů PASS bez nového testu, B01–B05 a integrační deferred/error scénáře zůstávají nedodané/neověřené tímto blokem, viz [report úlohy 4](../../.superpowers/sdd/plan-v003/task-4-report.md) |
| T06 | VPN role, bootstrap, recovery a checkpointy | D05 doplněno, TT06 doplněno o dva lifecycle testy, závěrečných 38 cílených testů PASS, D13 zůstává samostatným krokem R03, viz [T06](T06.md) a [report úlohy 5](../../.superpowers/sdd/plan-v003/task-5-report.md) |
| T07 | HTTP deadline, rušení a izolovaná diagnostika | uzavřeno, viz T07.md |
| T08 | Wintun, síťové vlastnictví a reconciliation | analýza dokončena, R04 odloženo bez realizace, viz T08.md |
| T09 | Instalace, upgrade, rollback a distribuce | uzavřeno s R05, viz T09.md |
| T10 | IPC framing, dostupnost a bezpečné chyby | D06 doplněno v client.md, Implementační meze lokálního řízení. TT09 bez změny suite, běhy a meze důkazu viz [report úlohy 6](../../.superpowers/sdd/plan-v003/task-6-report.md). |
| T11 | Provozní čítače a status/diagnostics | uzavřeno, viz T11.md |
| T12 | ConfigStore, UTF-8, repair a atomická publikace | uzavřeno, viz T12.md |
| T13 | ACL datového stromu a startup default | uzavřeno s R06, viz T13.md |
| T14 | Default secret, access klíče a testovací profily | uzavřeno, viz T14.md |
| T15 | Wizard metadata, identity a aditivní kontrakty | uzavřeno s R07, viz T15.md |
| T16 | Wizard probe, jednorázové pokusy, epochy a lease | D07 doplněno: rozpočty Next 45/75/105 s, IPC start 5000 ms a odkazy. TT13 doplněna nativní UI/pipe/runtime vazba ztracený start → cancel-attempt → opožděný start, 0 HTTP. Cílené testy PASS, viz [report 7](../../.superpowers/sdd/plan-v003/task-7-report.md). |
| T17 | Wizard Finish, receipts a návazné okno | D07 doplněno: kapacita 128, živá relace a nejméně 600000 ms po uzavření, pending/neterminální záznamy neexpirují jen časem. R07 zachováno. Službové testy PASS, Finish call counts převzaty z historické akceptace. Nový UI běh nedokončen kvůli ovládání TaskDialog v harnessu, viz [report 7](../../.superpowers/sdd/plan-v003/task-7-report.md). |
| T18 | Menu, singleton oken a nezávislý Disconnect | uzavřeno s vazbou na R07, viz T18.md |
| T19 | Ikony, sdílený rendering, monitor/DPI a přístupnost | uzavřeno s R08, viz T19.md |
| T20 | Connect progress a bezpečná prezentace výsledku | uzavřeno, viz T20.md |
| T21 | Edit Config a Show Info | uzavřeno, viz T21.md |
| T22 | Statistics UI | uzavřeno s R10, viz T22.md |
| T23 | Wizard vzhled, texty a navigace | uzavřeno, viz T23.md |
| T24 | Contracts model, řádky a preserving Save | uzavřeno s R09, viz T24.md |
| T25 | Random, Password SHA a cílové identity | uzavřeno, viz T25.md |
| T26 | Ruční Exit a systémový konec relace | uzavřeno, viz T26.md |
| T27 | Izolovaný GUI runner | uzavřeno, viz T27.md |
| T28 | Testovací brány, runtime/visual evidence a dokumentační autorita | D09 redakčně doplněno v client.md a tray_UX.md, aktuální kontrakt oddělen od datované historie. UX14 zachovává runtime/vizuální meze. Bez nového testu, pouze textové a odkazové kontroly, runtime/visual NOT RUN. TT19 zůstává samostatné, R04 odložené a backpressure nedodané. Viz [T28](T28.md) a [report D09](../../.superpowers/sdd/plan-v003/task-9-report.md). |
| T29 | Logování a provozní bezpečnost | D08 doplněno v client.md, Logování a diagnostika. TT20 doplněno, izolovaný logger test PASS (1 test, 0 selhání). Event Log a zvláštní startup warning nejsou dodané. Viz [report 8](../../.superpowers/sdd/plan-v003/task-8-report.md). |

## Pravidla uzavření

Index [pokryti-zdroju.md](pokryti-zdroju.md) mapuje všech 16 zdrojů včetně číslovaných rozhodnutí, akceptací a historických/neimplementovaných částí. Všechny T01–T29 mají dopad na konstrukci testů. Konkrétní cesty testových souborů byly porovnány s inventářem. Výsledky buildů a runtime testů se netvrdí.

Další kontrola přímo proti cílovým nadpisům opravila T27 na ponechat existující návod a doplnila skutečně chybějící kapacity IPC, rozpočty Next a retenci receipts do T10/T16/T17. Dřívější obecné tvrzení o již uvedených číselných mezích se tím nahrazuje konkrétními zjištěními.

Závěrečná kontrola doplnila v T14 konkrétní oddělené deployment profily a již existující Python/PowerShell testy. V T12 a T19 opravila přesnou implementační cestu a zdrojovou sekci. Původní rozpor T22 byl nyní uzavřen rozhodnutím R10: deset zobrazovaných hodnot a převod bajtů je schválený stav. Kontrola pokrytí všech šestnácti zdrojů je dokončená.

Každý TNN.md musí obsahovat chronologické zdroje a původní záměr, změny a přednost, konečný záměr, konkrétní důkaz kódu odděleně od existujících testů, klasifikaci, přesný dopad na každý cílový dokument, otevřená rozhodnutí a dopad na konstrukci testů včetně druhu, rozsahu a rizika. Neexistence testu sama neznamená neimplementováno. Před vytvořením plánu musí být uzavřena všechna témata a provedena kontrola pokrytí všech věcných bodů zdrojů.
