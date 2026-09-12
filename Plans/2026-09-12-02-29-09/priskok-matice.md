# Pracovní matice konsolidace

Stav: tematický průzkum a index pokrytí uzavřeny. Úplný plán je [plan-v002.md](plan-v002.md), předchozí v1 zachována. V2 opravuje přesnou cestu scheduler.cpp na core/queues a zachovává celý plán. Nesporné dokumentační kroky a samostatná testovací větev jsou oddělené od R01–R10. Celkové prohlášení o obsahové nadbytečnosti historických zdrojů čeká na vyjasnění rozporů. Autoritativní zadání tvoří intent-v001.md, intent-v002.md a intent-v003.md v odpovídající složce Intents. Kód, testy, cílové dokumenty a zdroje se nemění.

Společný deadline: 2026-09-12 05:05 Europe/Berlin (03:05 UTC). Žádní další agenti nevznikají. Při obnově kontextu nejprve načíst celý účinný globální blok instrukcí z CODEX_HOME/AGENTS.override.md nebo AGENTS.md, případně úplný hostem dodaný blok, a ověřit obě přesné hranice před další věcnou prací. Poté přečíst tuto matici a pokračovat prvním neuzavřeným tématem. Deadline se obnovou neposouvá.

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

| ID | Téma | Stav |
|---|---|---|
| T01 | Platforma, build, členění a přenosnost | uzavřeno, viz T01.md |
| T02 | Konfigurační schéma, CIDR a runtime limity | uzavřeno, viz T02.md |
| T03 | Kryptografie, wire parsery a payload IPv4 | uzavřeno, viz T03.md |
| T04 | Časová synchronizace | uzavřeno s R01, viz T04.md |
| T05 | Fronty, plánování a backpressure | uzavřeno s R02, viz T05.md |
| T06 | VPN role, bootstrap, recovery a checkpointy | uzavřeno s R03, viz T06.md |
| T07 | HTTP deadline, rušení a izolovaná diagnostika | uzavřeno, viz T07.md |
| T08 | Wintun, síťové vlastnictví a reconciliation | uzavřeno s R04, viz T08.md |
| T09 | Instalace, upgrade, rollback a distribuce | uzavřeno s R05, viz T09.md |
| T10 | IPC framing, dostupnost a bezpečné chyby | uzavřeno, viz T10.md |
| T11 | Provozní čítače a status/diagnostics | uzavřeno, viz T11.md |
| T12 | ConfigStore, UTF-8, repair a atomická publikace | uzavřeno, viz T12.md |
| T13 | ACL datového stromu a startup default | uzavřeno s R06, viz T13.md |
| T14 | Default secret, access klíče a testovací profily | uzavřeno, viz T14.md |
| T15 | Wizard metadata, identity a aditivní kontrakty | uzavřeno s R07, viz T15.md |
| T16 | Wizard probe, jednorázové pokusy, epochy a lease | uzavřeno, viz T16.md |
| T17 | Wizard Finish, receipts a návazné okno | uzavřeno s vazbou na R07, viz T17.md |
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
| T28 | Testovací brány, runtime/visual evidence a dokumentační autorita | uzavřeno, viz T28.md |
| T29 | Logování a provozní bezpečnost | uzavřeno, viz T29.md |

## Pravidla uzavření

Index [pokryti-zdroju.md](pokryti-zdroju.md) mapuje všech 16 zdrojů včetně číslovaných rozhodnutí, akceptací a historických/neimplementovaných částí. Všechny T01–T29 mají dopad na konstrukci testů. Konkrétní cesty testových souborů byly porovnány s inventářem. Výsledky buildů a runtime testů se netvrdí.

Další kontrola přímo proti cílovým nadpisům opravila T27 na ponechat existující návod a doplnila skutečně chybějící kapacity IPC, rozpočty Next a retenci receipts do T10/T16/T17. Dřívější obecné tvrzení o již uvedených číselných mezích se tím nahrazuje konkrétními zjištěními.

Závěrečná kontrola doplnila v T14 konkrétní oddělené deployment profily a již existující Python/PowerShell testy. V T12 a T19 opravila přesnou implementační cestu a zdrojovou sekci. T22 opraveno na R10: deset zobrazovaných hodnot a převod bajtů nemají oporu vůči devíti přesným hodnotám schválených zdrojů. Kontrola úplného pokrytí zdrojových bodů zatím pokračuje.

Každý TNN.md musí obsahovat chronologické zdroje a původní záměr, změny a přednost, konečný záměr, konkrétní důkaz kódu odděleně od existujících testů, klasifikaci, přesný dopad na každý cílový dokument, otevřená rozhodnutí a dopad na konstrukci testů včetně druhu, rozsahu a rizika. Neexistence testu sama neznamená neimplementováno. Před vytvořením plánu musí být uzavřena všechna témata a provedena kontrola pokrytí všech věcných bodů zdrojů.
