# Pokrytí závazných zdrojů

Tento index propojuje zdrojové části s tematickými záznamy T01–T29. Není důkaz úspěšného běhu testů. Původní rozpory zůstávají zachované jako historie, aktuálně R01–R03 a R05–R10 uzavírají [nová uživatelská rozhodnutí](rozhodnuti-v004.md), R04 je odložené. Rozšíření o budoucí implementaci R02 je v intent-v004. Jejich novější autorita má přednost před historickými klasifikacemi v indexu.

Zdroje jsou S01–S16 podle priskok-matice.md. Historické instrukce k instalaci, delegaci, opravám zdrojových plánů, commitu, vydání nebo spuštění testů jsou údaje o tehdejším workflow. Nejsou autorizací je provést v této plánovací úloze. Historické baseline, odhady velikosti a tvrzení o tehdejším stroji se nepřenášejí jako současné výsledky.

## S01 plan_client.md

| Část | Tematické pokrytí |
|---|---|
| Úvod a §1 | T01 platforma, T02 CIDR, T03 atomická dávka, T10 veřejná pipe, T07 HTTP/TLS |
| §2.1 | T02, později /24–/29 |
| §2.2 | T06 role, T07/T16 pozdější oddělená diagnostika |
| §2.3–4 | T08 efemérní adaptér, GUID/LUID |
| §2.5–6 | T07 asynchronní HTTP, URL, no proxy/redirect |
| §2.7 | T03 jediný obousměrný payloadKey |
| §2.8 | T10 framing, později 4 MiB obálka |
| §2.9 | T06 aktivní snapshot, T12 uložený dokument, T08 ownership výjimky |
| §2.10–11 | T02/T03 parsery, T12 pozdější atomická publikace |
| §2.12 | T05 počet záznamů requestu |
| §2.13–14 | T09 LocalSystem, T06 nedodaná obecná recovery výjimek |
| §2.15, §3.1–5, §4–6 | T01 build/členění, T19 ikony, T28 testovací infrastruktura. Doporučené ladicí nástroje nejsou novou vlastností produktu. |
| §7 | T06 lifecycle, bootstrap, R03 |
| §8.1–2 | T02 defaulty/validace, T12 publikace |
| §9–10 | T03 crypto/wire, T14 pozdější secret |
| §11 | T07 transport |
| §12 | T04 čas a R01 |
| §13 | T05 FIFO, scheduler, rezervace a R02 |
| §14 | T03 IPv4 a atomická dávka |
| §15–16 | T08 Wintun, síť a R04 |
| §17.1–4 | T10 IPC, T11 status, T07 get-info, T12 get/set-config |
| §17.5 | T18 menu, T19 shield, T06 globální VPN, T26 konec tray |
| §18 | T29 rotace, chybějící Event Log/warning |
| §19 všechny řádky | T03 vadná data, T04 čas, T05 tlak front, T06 výpadek a pád role, T07 rušení, T08 ring/ownership |
| §20.1–5 | T09 manifest, instalace, upgrade, rollback, odinstalace a R05 |
| §21 celý testovací seznam | T28, konkrétní druhy testů také T01–T14 a T18–T22 |
| §22 | Historické pořadí implementace, T28. Není pokyn znovu implementovat hotové části. |
| §23 všechny otevřené body | T08 driver/ring/routa, T09 podpisový manifest. Nedoložený podporovaný rozsah verze zůstává R04. |

## S02 plan2_client.md

| Část | Tematické pokrytí |
|---|---|
| §1–3 | T11 čítače, T18 menu, T19 ikony, T20 progress, T21 editor, T22 Statistics |
| §4.1–4 | T10 obálka a bezpečné chyby, T11 čítače, T06 checkpointy |
| §5 | Historická delegace, pouze T28. Nevytváří se nové úkoly. |
| §6.1–4 | T19 ikony, generátor a vizuální ověření |
| §7.1–3 | T10 uint64 JSON, T11 čítače a snapshot |
| §7.4–6 | T03 health syntax, T06 checkpointy, T11 účtování |
| §7.7–9 | T10 serializace/whitelist, T11 testy |
| §8.1–4 | T01 členění, T18 stav, T20 progress a polling |
| §8.5 | T22 Statistics, pozdější R10 |
| §8.6–7 | T21 editor a T19 geometrie |
| §8.8–9 | T18 menu, T20–T22 akceptace |
| §9, §10.1–4, §11 | T28 integrace a testovací strategie, historické pořadí práce |
| §12.1–2 | T19 parametr rámečku, T20 close při cizím Disconnect a zachovaný terminální error |

## S03 plan3_client.md

| Část | Tematické pokrytí |
|---|---|
| §1, §3 | T01 platforma, T12–T18 nové konfigurační/Wizard chování. Tehdejší baseline není nový požadavek. |
| D01–D03 | T18 menu, T21 skutečný editor a cesta |
| D04–D07 | T12/T13 startup a zachování, T15 metadata/patch |
| D08 | T23 Back, později viditelný disabled Back |
| D09–D10 | T16 probe/epochy |
| D11–D13 | T14 klíče, T15 kandidáti a self konflikt |
| D14 | T10 bezpečná pole, T23 UX |
| D15–D17 | T14 default/mapa, T10 kompatibilita IPC |
| D18 | T06 aktivní snapshot, T18 jednorázová kontrola po akci |
| D19–D21 | T07 cancel/deadline/diagnostika, T18 nezávislý Disconnect |
| D22–D24 | T12 last-save-wins, T16 pokusy, T21 editor |
| D25–D26 | T10 kapacity, T16 lease |
| D27–D28 | T15 preference/automatická IP, kolize bez rezervace |
| §4.1–4 | T01 komponenty, T07 HTTP, T10 executory, T16 izolace |
| §5.1–3 | T12 text/revize/repair, T15 patch/Reset |
| §5.4 | T18 menu, T21 otevření editoru |
| §5.5–6 | T12 publikace, T21 create/Reset |
| §6.1–2 | T15/T16 model, přechody, epochy |
| §7.1–3 | T03 health/clients, T14 klíče, T15 návrh IP, T16 kontroly |
| §7.4 | T15 aditivní Wizard kontrakty |
| §8.1–4 | T10 schémata, T16 operace/lease, T17 commit/receipt |
| §9 | T23 Wizard texty/layout, T17 později odstraněný placeholder |
| §10 | T14 serverové defaulty, T10 kompatibilita, T13 startup změněný S05 |
| §11 | T03/T07/T10/T12–T18 obsahové tabulky, T28 autorita a testování. Zápisy do historických plánů jsou mimo současný rozsah. |
| B00–B01 | T28 dokumentace, T02/T07/T10/T16/T17 číselné meze |
| B02–B04 | T12/T15 dokument, ConfigFiles, ConfigStore |
| B05–B08 | T14 defaulty, T03 parsery, T07 transport, T16 probe |
| B09–B12 | T10 schémata/pipe, T16/T17 WizardService, T12 config příkazy |
| B13–B16 | T18 menu, T15 model, T23 Wizard UI, T21 editor/T17 placeholder |
| B17–B19 | T01 build, T28 unit/server/integrace |
| §13.1–4 | T28 strategie a prostředí, testovací pole všech dotčených T záznamů |
| A01–A07 | T12/T13/T15/T18/T21 dostupnost/startup/koncepty |
| A08–A10 | T23 gate, T03/T16 URL a secret |
| A11–A14 | T14/T15 metadata, návrh a kolize IP |
| A15–A17 | T16 jednorázová destruktivní diagnostika a izolace |
| A18–A19 | T15 aditivní defaults, T17 ověřená sedmice |
| A20–A21 | T12 celé atomické publikace, bezpečnostní výjimka R06 v T13 |
| A22–A24 | T17 receipt, T16 epochy/cancel před Connect |
| A25–A28 | T10 bezpečné chyby, kapacity, schémata a kompatibilita, T18 Disconnect |
| A29–A30 | T14 PHP/C++/Python a profily |
| A31 | T17 placeholder změněný S06/S09, pozdější diktát R07 |
| A32 | T19/T23/T28 DPI a ruční evidence |
| A33–A36 | T16 lease, T07 cancel/deadline/diagnostika |
| A37–A38 | T21 create/Reset, T16 terminální Next |
| A39 | T28 dokumentace, T03 diagnostická výjimka, T12/T13 startup |
| A40–A41 | T12 bounded read/UTF-8, T10 IPC limit |
| §15 | Omezení tehdejšího plánovacího výsledku, T28, nikoli doklad dnešního PASS. |

## S04 plan3_fix.md

| Část | Tematické pokrytí |
|---|---|
| §1–3 | T13 příprava adresáře, T21 srozumitelná oprava chyby |
| F01 | Historická reprodukce a prostředí, T28 |
| F02 | T13 konkrétní directory security chyba, R06 |
| F03 | T10 safe IPC, T21 zpráva a akce |
| F04, §5 kritéria 1–5 | T13 skutečný token/NTFS a startup později změněný S05, T28 testová evidence |
| §6 | Historický průzkum bez implementace, nepřenášet jako runtime důkaz. |

## S05 plan3_fix2.md

| Část | Tematické pokrytí |
|---|---|
| §1, P01–P03 | T13 startup, T12 no overwrite, T14 jediná šablona |
| P04–P06 | T15 metadata a návrh IP |
| P07–P10 | T15 náhled, aditivní rozdíl, nepersistovaná volba, preserving patch |
| P11 | T13 degradovaný start a dostupné IPC |
| §2.1–2 | T15 preference/fallback, T14 přesný default |
| §3.1–4 | T13 příprava/recovery/startup, T12 stavy obsahu, T06 bezpečný Connect |
| §4.1–5 | T10 uzavřená fields/view schémata, T15 preview, T16 evidence, T17 fingerprint/receipt |
| §5.1–3 | T15/T23 model a ovládání |
| §6.1–4 | T15 kandidáti, checkbox, patch a souběh |
| §7 B00–B08 | T28 historické pořadí, obsahově T10/T12–T17/T23 |
| §8 | T28 dokumentační přesahy, T13 startup, T15 metadata. Plány/installer/reporty se zde nepřepisují. |
| §9.1 všechny sady | Testovací pole T10/T12–T17/T23/T28 |
| §9.2 scénáře 1–4 | T13/T21 skutečný startup/missing/ACL, výjimka R06 |
| §9.2 scénáře 5–8 | T15/T17 první edit/opakování/receipt, T09 instalace |
| AK01–AK05 | T13 startup a zachování, T12 create race |
| AK06–AK10 | T15 typy, fallback, preference a návrh |
| AK11 | T15 žádný čtvrtý persistentní příznak |
| AK12–AK13 | T12/T13 filesystem a jediná šablona |
| AK14–AK17 | T15 checkbox ve všech režimech, zachování/doplnění/no-op |
| Doplňkové funkční 1.7, konzistence | T13 jediný startup/ConfigStore, T14 šablona |
| Doplňkové 2.5, 4.7, 5.4, 5.7, 5.10, 5.11 | T15 metadata mimo runtime, ruční IP, preview, T14 hash, T15 první edit/CIDR |
| §11 | Historická kontrola úplnosti, T28. Technické T01–T11 tohoto zdroje nejsou témata T01–T11 této matice. |

## S06 plan3_fix3.md

| Část | Tematické pokrytí |
|---|---|
| Stav/rozsah, mapování problémů, povinné aktualizace | T28 autorita, T01 lehký rozsah |
| N01 | T18 menu, T20 bezpečné details, T28 UX autorita |
| N02 | T24 ruční otevření a availability |
| N03 | T02 CIDR, T24 model/DOM, T25 generování |
| N04 | T12 publikace, T24 Save/unknown/dirty R09 |
| N04a | T26 Exit/logoff |
| N05 | T20 Connect |
| N06 | T17 Finish |
| N07 | T23 Wizard |
| N08 | T21 editor, T22 Statistics/R10 |
| N09, N09a | T01 přenosnost, T19 resources/placement/R08 |
| N09b | T28 přesné vizuální/runtime hranice |
| F00/F00a | T28 baseline, T02 CIDR |
| F01/F02 | T19 shared UI, T24/T25 přenosné modely |
| F03/F04 | T20 Connect, T17 Finish |
| F05/F06 | T24/T25 nativní editor, děti a Save |
| F07/F08 | T20–T23 UI, T28 evidence |
| F09 celý včetně tabulky, příkazů a sedmi ověření | T27 runner a T28 jeho akceptace |
| A01–A02 | T18/T24 config read/availability |
| A03–A06 | T02/T14/T24 řádky/CIDR/klíče/validace |
| A07–A09 | T25 Random/Unicode/identity, R07 modalita a R09 dirty |
| A10–A11 | T24/T12 no early write/DOM/limity |
| A12–A13 | T17 Finish/receipt, T24 pozdější explicitní open |
| A14–A17 | T20 details/success/chyby/timery |
| A18 | T24 last-save-wins a unknown |
| A19–A20 | T24 nativní řádky, T19/T28 accessibility a vizuální akceptace |
| A21 | T21/T23 editor/Wizard, T22 Statistics R10 |
| A22–A23 | T10 invariant IPC, T29 logy, T28 dokumentační audit |
| A24–A27 | T28 binární/runtime evidence, T27 runner, T19 cache/shell |
| A28–A29 | T19 geometrie/jazyk, T25 title |
| A30–A32 | T26 lifecycle, T01 core, T27 runner |
| Rizika, zrychlený závěr, kontrola dokumentu | T02 kompatibilita, T12/T24 unknown, T25 cleanup, T23 warning, T28 omezení běhů a distribuce |

## S07 plan3_fix3_problems.md

Každý bod obsahuje původní nález i rozhodnutí. Původní alternativy nejsou současně závazné.

| Bod | Tematické pokrytí |
|---|---|
| 1–2 | T10 žádný nový progress IPC, T20 get-config/status, T17 žádný handoff |
| 3–4 | T25 255 scalars, T24 synchronní místní práce, T28 měřitelné hranice |
| 5 | T24/T25 nemodální souběh, T26 následně uzavřený Exit |
| 6 | T02 /24–/29, T24 všechny native controls |
| 7 | T01 core/presentation |
| 8 | T25 exact Key/Random texty |
| 9–11 | T19 monitor/cache/angličtina |
| 12 | T28 přesná vizuální metodika |
| Věrnost 11 mockupům | T20–T25 a T28 |
| Runtime overhead, baseline/velikost, otevřené otázky, minimum změn | T28. Historické odhady ani velikosti nejsou novým měřením. |

## S08 plan3_fix3_problems2.md

| Bod | Tematické pokrytí |
|---|---|
| 1 | T26 odklad vlastníka, pozdější S09 výslovně přidělil společný lifecycle |
| 2 | T25 přijetí slabého/prázdného hesla bez dalšího warning |
| 3 | T25 neměnný systémový titul bez dalšího obsahového prvku |
| 4 | T25 odložený limit počtu Password oken, nepřidávat |
| 5 | T19 odklad dynamické HC cache, R08 |
| 6 | T28 průběžná dokumentace a závěrečný audit |
| Úvod a oblasti bez nálezu | Kontext auditu, ne nové produktové požadavky. |

## S09 plan3_fix3_work_for_agents.md

| Část | Tematické pokrytí |
|---|---|
| Autorita, tabulka problémů 1–12 | Shodně S07 body 1–12, výjimky S08 zachovány |
| Topologie, globální pravidla, předání a vlastnictví | T28 historické workflow. Nevznikají další kontexty ani delegace. |
| WP00 celý včetně brány | T02 a T28 |
| WP01 celý včetně brány | T01/T14/T24/T25 |
| WP02 celý včetně brány | T19/T18 a T27 injection |
| WP03 celý včetně brány | T20/T21/T22 a R10 |
| WP04 celý včetně brány | T17/T23/T26, nezměněný T16 lifecycle |
| WP05 celý včetně brány | T24/T25/T26, T10 nezměněné schema |
| WP06 celý včetně brány | T27 runner |
| WP07 celý včetně brány | T28 dokumentace/traceability a textové kontroly |
| WP08 celý | T28 skutečné Release targety, testové filtry, runtime/visual, nevydávat proxy za důkaz |
| Synchronizační body a checklist | T28 sekvenční integrace a evidence, historické instrukce neprovádět |

## S10–S16 notes

| Zdroj a obsahová jednotka | Tematické pokrytí a výsledek |
|---|---|
| S10 paměť/reset statistik | T11 |
| S10 původní statistiky a jejich zopakovaný fragment | T11/T22, R10 pro nedoloženou pozdější desátou hodnotu a jednotky |
| S10 Connecting, tři body, 3s success, volitelný odpočet | T20, základní záměr implementován. Volitelnost nevyžaduje odstranit existující odpočet. |
| S10 Cancel artefakt, širší editor | T21 |
| S10 dva shodné odstavce shield white/black | T19, jedna funkce, oba zdrojové výskyty zachovány |
| S10 žádost přibalit poznámku/screenshot | Historický pokyn, není funkce ani dnešní žádost o screenshot. |
| S11 rozhodnutí checkpoint/recovery | T06 |
| S11 fresh config a nezávislé časovače | T18/T20/T21 |
| S11 bezpečná příčina, timeout, partial bytes | T07/T10/T11/T20 |
| S11 URL syntax před kryptografií | T03/T06 |
| S11 povolený editorový obsah, šířka/výška | T21 |
| S11 opakovaný IPv4 pokus a pořadí IPC členů | T11/T10 |
| S11 duplicitní diktát a průběžné organizační věty | Sloučeno k témuž tématu, není nový produktový požadavek. |
| S12 celý soubor | Mimo rozsah: historická žádost o průzkum web serverů, rozšíření a limitů POST. Není implementované klientské chování, neprovádí se web research. |
| S13 Finish/placeholder, Reset maže disk, bez CIDR kontroly, staré menu | T17/T15/T18/T23, R07. Pozdější pořadí zdroje neskrývá rozpor se skutečným kódem. |
| S13 defaults .2–.15, hex oktet, plaintext payload placeholder | T14/T15. Platný hex klíč implementován, plaintext placeholder neimplementován. |
| S13 URL/secret/IP/kroky, budoucí platformy | T01/T15/T16/T23 |
| S14 celý odstavec startup a tři volby | T13/T15, žádný čtvrtý persistentní příznak |
| S15 všechny reference mockupů a úsporný vzhled | T19/T20/T21/T22/T23/T24/T25/T28 |
| S15 Contracts tabulka, numerický pár, lowercase64, Random | T24/T25 |
| S15 Password modalita a empty cue | T25, modalita R07, cue implementováno |
| S16 celý návrh dvou půlkruhů | T19, neimplementováno. Negenerovat asset ani nezaměnit dodaný shield. |

## Výsledek kontroly

Všech 16 původních zdrojů má tematické přiřazení včetně historických workflow a výslovně nerelevantního S12. Číslované D01–D28, A01–A41, AK01–AK17, N/F/A modernizace, oba seznamy problémů a WP00–WP08 mají dohledatelné pokrytí. Novější rozhodnutí jsou mapována níže. Odložené R04, budoucí B implementace a neprovedené testy se nevydávají za dokončené. Zdroje se nemažou.

## Novější autorita po plánu v2

| Rozhodnutí / zadání | Dotčená témata | Plán / aktuální výsledek |
|---|---|---|
| R01 | T02/T04 | D11, TT04, vyjmout nedoložené corner cases, neimplementovat je. |
| R02 + intent-v004 | T05 | D12 a budoucí B01–B05, TT05. 5 s schváleno, gate dosud nedodaná. |
| R03 | T06/T07 | D13, TT06/TT07, současný teardown/recreate. |
| R04 | T08 | Odloženo, žádná současná realizace ani nové testy. |
| R05 | T09 | D14, TT08, současný jednoduchý installer. |
| R06 | T13 | D15, TT10, Users read chtěné, admin-only nepřebírat. |
| R07 | T14/T15/T17/T18/T19/T20/T23/T24/T25 | D15, neimplementované notes vyřazené jako irelevantní. Historické odkazy zůstávají. |
| R08 | T19 | D16, TT15, dynamická HC paleta schválená. |
| R09 | T24/T25/T26 | D16, TT17, výsledkové dirty. |
| R10 | T22/T27/T28 | D17, TT16/TT19, 10 UI hodnot a jednotky, devět službových čítačů beze změny. |

Aktuální testovací dopady nahrazují staré podmínky „až po R“. Nezavádějí testy odloženého R04 nebo odstraněných garancí, přidávají konkrétní B04. Connected for nebylo součástí konkrétní volby R10 a zůstává mimo novou dokumentaci.
