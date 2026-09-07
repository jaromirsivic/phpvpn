# Plan3 fix 2: audit AK01 až AK17

Autorita: [formální zadání](../../notes/2026-09-06-22-21-54_notes.processed.md), [plán](../../plan3_fix2.md). Prostředí, příkazy, logy a omezení obsahuje [report](implementation-plan3-fix2-report.md). Audit se vztahuje k aktuálním necommitnutým zdrojům ze 6. září 2026.

PASS označuje konkrétní provedený důkaz. „Částečně ověřeno“ znamená implementovaný požadavek s automatickým důkazem a neprovedenou systémovou částí. Žádné NOT RUN není skrytě započítané do PASS.

| AK | Implementace | Provedený důkaz | Závěr |
|---|---|---|---|
| 01 | `service_host.cpp`, `ConfigStore::ensureDefaultAtStartup` po bezpečné recovery | `fix2.startup_creates_one_shared_default_and_never_changes_existing_bytes`, host i E2E sestavené | Částečně ověřeno. Interní startup PASS, skutečný první SCM start/instalace NOT RUN. |
| 02 | Jediný `kDefaultConfigDocument` přes `makeDefaultEditorDocument`, tři true a původní `.3` contract | Startup porovnává celý dokument, default validátor, UI načítá true, službové a IPC testy sdílené šablony | PASS automatické. |
| 03 | Existující dostupnost nemá startup publish, žádná migrace | Test přesných původních bajtů a jedné klasifikace včetně starého dokumentu a BOM | Částečně ověřeno. Core PASS, chráněný NTFS restart a SCM scénář NOT RUN. |
| 04 | Pouze missing/config_not_found může vytvářet | Invalid, repair, unreadable, encoding a too_large vracejí nula dalších publikací | Částečně ověřeno. Řízené I/O PASS, skutečný nepřístupný soubor při SCM startu NOT RUN. |
| 05 | Sdílený backend createIfMissing a žádná jiná zapisovací cesta | Fake beforePublish vytvoří cizí invalidní soubor, zůstane přesně zachovaný a vrátí alreadyExists | Částečně ověřeno. Řízený závod PASS, chráněný NTFS rename závod NOT RUN. |
| 06 | Optional `WizardPreferences`, samostatné fallbacky v analýze, runtime beze změny | Všechny kombinace přítomnosti identity, absence jednotlivých voleb, explicitní false, všech osm bool kombinací | PASS automatické. |
| 07 | Uzavřený whitelist a isBool pro všechny tři členy | Null, čísla, string, objekt, pole pro každý příznak, duplicity a runtime/analyze odmítnutí | PASS automatické. |
| 08 | Secret příznak v fields, UI, evidence, DOM patch a reopen | Službový full flow všech osmi kombinací, UI true/default a přesný vlastní literal secret | PASS automatické na produkčním modelu/službovém fixture a HWND. Živá instalace není tvrzena. |
| 09 | Auto IP a default key v fields až do Finish | Osm kombinací full flow/reopen, změna každého příznaku ve fingerprintu, UI automatic/manual a odeslaná `.11`/klíč | PASS automatické. |
| 10 | Ověřený suggestedIp override pouze při true, zákaz ruční editace při true | Model true/false, HWND `.2` nahrazená `.10`, přepočet klíče, no-suggestion a ruční fallback | PASS automatické. |
| 11 | createDefaultContracts je jednorázový request mimo fields, konfigurační whitelist jej nezná | DOM testy nepřítomnosti příznaku a uzavřená konfigurační/IPC schémata | PASS automatické a audit zdroje. |
| 12 | Stejný chráněný backend se staging/flush/journal/rename, oddělená chyba inicializace | Řízené selhání/unknown bez retry, zákaz publikace a Connectu, odpovídající status/diagnostika/Disconnect. Neprivilegované ochrany backendu | Částečně ověřeno. Chráněné NTFS fault/crash scénáře a degradovaný skutečný SCM host NOT RUN. |
| 13 | Startup/create/get-default/Reset editoru volají stejnou factory | Přesná rovnost startup bytes, create-default a editorový default v konfiguračních, IPC a UI Reset testech | PASS automatické. Skutečné chráněné NTFS vytvoření NOT RUN. |
| 14 | Contracts UI bez mode podmínky, model při load vždy true | Model všech čtyř režimů, HWND edit s 0/1/13 původními kontrakty, checked/visible a počty | PASS automatické pro uvedené scénáře. Ruční vizuální průchod všech režimů a DPI NOT RUN. |
| 15 | Shared missing peers helper, append-only patch nad původním DOM | Edit/repair nulová/částečná/úplná sada, původní custom klíče, `.200`, vlastní IP mimo kandidáty a změna `.2` na `.10`, službový Finish | PASS automatické. |
| 16 | Prázdné M nemění obsah, receipt nevytváří další publikaci | Opakovaný build nad úplnou sadou, stejné Finish bez druhého zápisu, nové načtení s checked a nula missing | PASS automatické. |
| 17 | False zachová původní objekt i omission v edit/repair | Přesná obsahová rovnost contracts, chybějící objekt zůstane chybějící, preview plánuje nula, službové false flows | PASS automatické. |

## Doplňkové povinnosti P01 až P11 a T01 až T11

| Vazba | Audit a důkaz |
|---|---|
| P01/P02/P11, T03/T04 | Host volá jednu startup operaci interně, ne přes IPC. Žádný Connect při vytvoření. `operationalStatus` chrání runtime/publikaci i při čitelném validním cíli, pokud recovery není bezpečně uzavřená. Shutdown degradovaného engine je bezpečný. |
| P03, T02 | Úplný editorový default a nedokončený Reset Wizardu zůstávají odlišné dokumenty. Oba používají true pro nové volby, Reset Wizardu před Finish nepíše. |
| P04, T01 | Preferences patří dokumentovému ConfigValues, ne logice ClientConfig. Stejné konkrétní hodnoty s různými validními příznaky mají stejnou runtime identitu, klíč a CIDR. |
| P05/P06, T05/T07/T08/T09 | Sedm fields projde striktní IPC cestou, důkazy a fingerprintem. True auto IP musí odpovídat návrhu služby. False má přednost v repair. Model uchová volby přes Back/Next/resume, ale nepřenáší staré důkazy. Žádný automatický HTTP retry. |
| T06 | `existingDefaultContractPeers` obsahuje nejvýše 14 unikátních číselně seřazených kandidátů. Chybné typy, duplicity, jiné CIDR, pořadí, count limit i create s původními kontrakty se odmítají. Původní plný seznam zůstává pouze ve službě. |
| P07/P09, T11 | Viditelný checked/disabled prvek pro nepodporovaný CIDR odesílá false, služba podstrčené true odmítá. Nepersistovaná volba a tři počty jsou ověřené v modelu i skutečných HWND. |
| P08/P10, T10 | Preview a publication používají stejný kandidátní helper. Původní payload klíče se při změně vlastní IP nepřepočítají. Zůstávají SHA-256 vektory a číselné řazení `.2` a `.10`, poslední úplné uložení vyhrává bez dodatečného merge. |
| Protokol a bezpečnost | `suggestWizardIp` stále používá `10.99.99.2` až `.15`, nikoli libovolnou síť. Relay wire a kryptografická pravidla nezměněny. Žádný config ani soukromé tajemství v distribuci, žádná oprava ACL z tray. |
| B00/B08 a předání | Aktualizovaná normativní dokumentace a instalační rady, samostatný report/matice, Debug/Release a ověřená čerstvá distribuce. Původní reporty nemění své výsledky. |

## Neprovedené integrační brány

- Všech 15 chráněných NTFS případů vyžaduje zvýšený token. Jejich kód je sestavený, v logu výslovně NOT RUN.
- Skutečná instalace, reinstall, start/stop testovací SCM služby, reálný Wintun a produkční ProgramData nebyly spouštěné ani měněné.
- Scénáře s nesprávnými ACL a nevyřešeným žurnálem byly ověřené na core/fake hranici, ne jako nový skutečný SCM startup. Bezpečnostní ochrany nejsou označeny za nové plně provedené NTFS ověření.
- UI testy používají skutečná produkční okna při 96 DPI, ale testovací backend. Ruční kontrola všech režimů, focus/scroll a DPI 125/150/200 % zůstává NOT RUN. Automatický layout test těchto DPI není ruční vizuální kontrola.

Celkem: 17 z 17 požadavků má implementační vazbu a konkrétní audit. Zbývající systémové důkazy jsou uvedené otevřeně, nikoli nahrazené souhrnným tvrzením „vše otestováno“.
