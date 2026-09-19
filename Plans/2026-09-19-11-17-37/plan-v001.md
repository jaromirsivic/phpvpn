---
plan_version: 1
created_at: 2026-09-19-11-28-00
timezone: Europe/Berlin
source: ../../Intents/2026-09-19-11-17-37/intent-v001.md
---

# Realizační plán bezpečnostního a výkonnostního auditu PHP VPN

## 1. Zadání, výchozí stav a hranice realizace

Autoritativní zadání je [intent-v001.md](../../Intents/2026-09-19-11-17-37/intent-v001.md), jeho zdroj je [intent-chat-v001.md](../../Intents/2026-09-19-11-17-37/intent-chat-v001.md). Tento soubor je první úplná neměnná verze plánu. Následné materiální změny plánu patří do nové úplné verze. Intent vlastní koordinátor, plánovací agent jej nemění.

Plán vznikl v `C:\git\phpvpn`, v aktivní větvi `tray_UX`, nad HEAD `4f449fd33df9bd824a5a165cfeb2b582dc3d1399`. Při přípravě byly již rozpracované změny `.intent-builder-active`, `copydistributiontovm.bat`, `copydistributiontovm.ps1` a nový adresář zdrojového intentu. Nejsou výsledkem tohoto plánu a nesmějí se zahodit ani automaticky zahrnout do jiného commitu. `audit.md` při plánování neexistoval. Příprava plánu neprovádí audit, nespouští testy produktu, neimplementuje opravy, nevytváří audit.md a nic nenasazuje.

Realizace začne až po výslovném rozhodnutí uživatele. Výstupem realizace bude kořenový `audit.md` a podpůrné důkazy v `Reports/2026-09-19-11-17-37/audit/`. Zápis do této podpůrné složky nenahrazuje okamžitý zápis ověřeného nálezu do audit.md. Produktový kód, produkční konfigurace a normativní dokumentace se při auditu neopravují. Potřebné reprodukční skripty a měřicí adaptéry vzniknou pouze v podpůrné složce, s popisem odchylek od produkce.

Audit zahrnuje dodaný PHP server, celé relevantní klientské C++ jádro, Windows adaptéry, službu, CLI, tray, build, balení, instalaci, testovací infrastrukturu a společné komunikační toky. Linux a macOS jsou podle `client.md` a `distribution.md` návrhové cíle, nikoli hotové porty. Kontrolovat tvrzení o jejich podpoře a přenosné předpoklady, nevymýšlet audit neexistující implementace. Závislosti a binární Wintun posoudit z hlediska původu, verze, důvěry, načítání a známých relevantních zranitelností. Neoznačit to za úplný audit interního kódu cizího ovladače.

Plánovací deadline je **2026-09-19 13:55:45 Europe/Berlin**. Nejde o automaticky přenositelný časový rozpočet celého budoucího auditu. Před realizací koordinátor potvrdí absolutní deadline nebo výslovně neomezenou dobu pro celý sekvenční strom. Každý agent jej přijme před prací, kontroluje čas před blokem, před delším měřením a během práce nejméně po pěti minutách. Pokud neumí čas skutečně vynucovat, oznámí omezení a netvrdí automatické hlídání. Jen koordinátor žádá o prodloužení, nejpozději 30 minut před deadlinem. Bez prodloužení při dosažení termínu uložit checkpoint a bezpečně zastavit včetně vlastních testovacích procesů.

## 2. Sekvenční provádění a malý předávací kontext

Pevné pořadí je **B00 → B01 → B02 → B03 → B04 → B05 → B06 → B07 → B08 → B09 → B10 → B11 → B12 → B13 → B14**. Každý blok začíná až po uzavření nebo výslovně zdokumentovaném podmíněném předání předchozího. Stejný agent může vykonat více bloků, ale vždy uloží samostatný checkpoint. Nový chat/task se vytváří pouze na výslovný požadavek uživatele nebo aplikovatelného skillu. Nevytvářet subagenty, worktree ani paralelní auditní zapisovatele. Model, úsilí a název případného nového tasku ověřit podle projektových pravidel a případného skillu před vytvořením.

Každý agent načte společný minimální balíček: projektové instrukce, tento plán, autoritativní intent, `control.md`, `handoff/Bxx.md` bezprostředního předchůdce, index nálezů a pro svůj blok příslušné řádky registrů. Nenačítá historii chatů, celý graf, všechny starší plány ani všechny předchozí důkazy. Normativní oddíly potřebné k nálezu si vždy načte v originálu, pouhý předávací souhrn není doklad přijetí rizika.

Kontext kódu získá nejprve `graft map`, potom `graft ask "<konkrétní symbol nebo otázka>" --source`, `graft skeleton <soubor>` a pro vazby `graft callers <symbol>` s potřebným směrem. Exhaustivní hledání používá `graft grep "<literál>"`, ne top-N výsledky ask. Neindexované dokumenty, skripty a build soubory dohledá pomocí `rg`. Truncated span musí dočíst přesně na uvedených řádcích. Graf je navigace, nikoli důkaz úplného řádkového auditu. Zkontrolovat jeho shodu se zdrojovým stavem. Zastaralý graf obnovit nebo chybějící pokrytí výslovně doplnit inventurou.

### Podpůrné artefakty vytvářené teprve v B00

- `control.md`: audit run ID, zdrojový HEAD a dirty soubory s hashi, platformy, potvrzený deadline, aktivní blok, jediný zapisovatel, čas převzetí/předání, hash posledního audit.md, další volná ID a bod pokračování.
- `coverage.tsv`: každý relevantní soubor a nepřekrývající se rozsah řádků nebo definice, vlastník Bxx, stav `TODO/REVIEWED/EXCLUDED/BLOCKED`, hash zdroje, důkaz a zdůvodnění vyloučení. Zahrnout i globální kód, konstanty, makra, chyby, hlavičky, build volby a neindexované skripty. Součtem nesmějí vzniknout mezery.
- `decisions.md`: registr rozhodnutí `D-0001…`, téma, přesná citace místa, rozsah a podmínky přijetí, případné rozpory, závaznost a blok spotřebitele. Indexuje celý `protocol.md`, `client.md`, relevantní `tray_UX.md`, `distribution.md` a navazující dokumentaci.
- `interfaces.md`: kontrakty `I-0001…`, producent, konzument, důvěryhodnost vstupu, formát, autentizovaná pole, limity, vlastník paměti/stavu, čas, chyby a odpovědnost za úklid. Rozhraní obsahuje explicitně i nechráněné vlastnosti.
- `findings-index.tsv`: ID, typ, stav, kořenová příčina, hranice důvěry, dotčené symboly, vlastník prvního ověření, související ID a anchor v audit.md. Je odvozený index, autoritou obsahu zůstává audit.md.
- `questions.md`: hypotézy `Q-0001…`, rozpory `C-0001…`, chybějící důkazy a odpovědný následující blok. Hypotézy nejsou potvrzené nálezy.
- `evidence/Bxx/`: reprodukce, příkazy, verze nástrojů, sanitizované výstupy, měřicí data a testovací protokoly. Nevkládat skutečná tajemství, payload ani obsah produkčních konfigurací.
- `handoff/Bxx.md`: krátké předání podle šablony níže. Větší data patří do odkazovaných důkazů, ne do souhrnu.

### Povinná šablona předání pro každý blok

```text
Blok a stav: Bxx, DONE / CONDITIONAL / BLOCKED
Zdroje: HEAD, dirty manifest, odchylky od počátku
Pokrytí: řádky coverage.tsv, zbývající mezery
Kontrakty: nová/změněná I-ID a rozhodnutí D-ID, jen potřebné závěry
Nálezy: nová/aktualizovaná PERF/SEC ID, odkazy, bez kopírování celých textů
Validace: co skutečně proběhlo, výsledky, NOT RUN a důvody
Otevřené body: Q/C ID, vlastník, potřebný důkaz a dopad na další blok
Další agent: přesné soubory/oddíly/rozsahy a první krok
Zápis: hash audit.md, další ID, potvrzené uvolnění vlastnictví
Čas: deadline, poslední kontrola, checkpoint při přerušení
```

Souhrn cílit do 600 slov. Zkrácení nesmí odstranit neuzavřený rozpor, podmínku kontraktu nebo důležitou mezeru ověření. Podmíněné předání dovoluje nezávislý následující blok, nikoli vydávání závislého závěru za ověřený.

## 3. Jediné vlastnictví audit.md a jednotný zápis Z

Koordinátor je správcem pořadí, nikoli souběžným editorem. **Jediný aktivní agent uvedený v control.md je jediným zapisovatelem audit.md i registru ID.** Předchůdce uloží všechny ověřené nálezy, validuje dokument, zaznamená jeho hash a uvolní vlastnictví. Nástupce porovná hash a zdrojový manifest a potvrdí převzetí. Pokud nesouhlasí, nesmí přepsat cizí změnu. Nejprve určí rozdíl a zapíše konflikt, pokračuje jen v nezávislém čtení. Při havárii koordinátor nejprve ověří, že původní agent a testy už neběží, pak předá jediný checkpoint novému vlastníkovi. Zámek není automatický distribuovaný mechanismus, pokud takový skutečně není zaveden.

**Postup Z platí povinně v každém B00 až B14, pro každý jednotlivý ověřený nález:**

1. Ověřit dosažitelný mechanismus a dopad přes zdrojový tok, deterministický důkaz nebo reprodukci. Statický důkaz je platný, je-li úplný a je jasně označený. U výkonu oddělit ověřenou zbytečnou práci od dosud neměřeného přínosu návrhu.
2. Vyhledat shodu v indexu i v obou tabulkách audit.md. Deduplikační klíč je kořenová příčina + porušený invariant + hranice důvěry, nikoli název souboru nebo podobnost textu. Stejný problém na obou stranách má jeden kanonický záznam s více místy důkazů.
3. U bezpečnosti prohledat rozhodovací registr, `protocol.md`, `client.md` a relevantní další dokumentaci. `Dříve přijato = ano` jen s výslovným, platným a podmínkami odpovídajícím dokladem. Jinak `ne`, s poznámkou „přijetí nedoloženo“ a rozsahem hledání. Popis implementace, starý úspěšný test či absence zákazu nejsou samy přijetím rizika.
4. Přidělit stabilní `PERF-0001…` nebo `SEC-0001…`, samostatné rostoucí čítače bez opětovného použití. Okamžitě přidat řádek hlavní tabulky a detail s důkazem do audit.md, teprve potom pokračovat k dalšímu nálezu. Nečekat na konec bloku ani na konec benchmarkové kampaně.
5. Soubor uložit bez ztráty jiných sekcí, znovu načíst, ověřit jedinečnost ID, požadovaná pole, anchor, existenci a rozsah odkazů a syntaxi tabulky. Pak aktualizovat odvozený index a control.md. Při selhání zápisu další nález nezačínat, uložit lokální recovery důkaz a oznámit blokaci.

Nález s nezávislým bezpečnostním i výkonovým dopadem může mít dvě ID v příslušných tabulkách, ale musí se navzájem odkazovat a sdílet jeden kanonický detail příčiny. Nepočítat je jako dvě nezávislé chyby. Doplnění důkazu mění existující ID. Sloučené ID zůstane dohledatelné jako alias „sloučeno do…“. Vyvrácené ID zůstane v historii se zdůvodněním, ne v seznamu platných ověřených závěrů. ID nepřečíslovat při řazení dle priority.

Rozpor nezakrývat posledním přepisem. Založit C-ID, ponechat oba důkazy a určit konkrétní rozhodovací test nebo dokument. Aktivní agent jej vyřeší, případně předá B13. Materiální nerozhodnutelný rozpor v zadání eskalovat jednou konkrétní otázkou koordinátorovi. Nečekání na odpověď neznamená souhlas. Již ověřená část nálezu se zapisuje ihned, sporné tvrzení zůstává označené jako otevřené.

### Povinná struktura audit.md

Na začátku: stav auditu a zdrojový baseline, potom konkrétní vyloučené corner cases s důvodem, předpoklady, dopadem a podmínkou návratu do rozsahu. **První hlavní tabulka je výkonnostní**, **druhá bezpečnostní**. Pomocné souhrny ani inventura nesmějí pořadí těchto tabulek převrátit.

Výkonnostní tabulka: `ID | Popis | Očekávaný přínos | Bezpečnostní dopad | Důležitost | Důkaz`. Bezpečnostní tabulka: `ID | Popis | Doporučené řešení | Závažnost | Dříve přijato (ano/ne) | Doklad o přijetí | Důkaz`. U přijetí „ne“ doklad výslovně uvede „nedoloženo“, nikoli prázdnou buňku.

Detaily pod tabulkami: příčina, předpoklady útoku/provozu, zásah do důvěrnosti/integrity/dostupnosti či logiky, reprodukce, zdrojové odkazy, související ID, omezení důkazu, stav a historie změn. Hypotézy a neprovedená ověření mají vlastní oddíl, bez předstírání potvrzení. Závěr obsahuje úplnost, konzistenci, prioritizaci a zbývající mezery.

Odkaz na kód vždy nese cestu relativní ke kořenu, symbol a přesný aktuální rozsah řádků, plus commit a hash souboru z baseline, pokud jsou necommitnuté změny. Markdown odkaz vede na skutečný soubor, rozsah je čitelně uveden v textu. Dokumentační důkaz obdobně uvádí dokument, nadpis, řádky a krátký význam. Čísla řádků získat při realizaci z aktuálního souboru, nepřebírat slepě staré odkazy plánu či Graftu. Při změně zdroje znovu ověřit dotčený důkaz.

Závažnost `kritická/vysoká/střední/nízká/informační` odůvodnit dopadem, dosažitelností a oprávněními útočníka. Přijetí rizika závažnost nesnižuje. U výkonu `důležité` vyžaduje doložený relevantní mechanismus a nulový bezpečnostní dopad, nebo konkrétní malé přijatelné riziko doložené projektovým rozhodnutím. Nové přijetí rizika agent sám neuděluje. Jinak uvést `podmíněně důležité, vyžaduje rozhodnutí` nebo `nižší priorita`. Neměřený přínos vždy označit „odhad“ a uvést proč a za jakého workloadu vzniká.

## 4. Bezpečná hranice corner cases

B00 založí odůvodněný seznam, B13 jej znovu napadne. Kandidáti na vyloučení jsou prolomení standardní kryptografie hrubou silou bez konkrétní implementační slabiny, libovolně poškozený hardware, neexistující platformní porty a scénáře vyžadující již plně privilegovaného útočníka bez dalšího dopadu nad jeho stávající oprávnění. Každé vyloučení je podmíněné doloženým předpokladem, nikoli automatické pravidlo pro celou třídu útoků.

**Nikdy nevyloučit jen kvůli malé četnosti:** útočníkem vytvořitelný HTTP/IPC vstup, limit−1/limit/limit+1, běžnou chybnou konfiguraci a veřejné defaulty, replay, opakování/cancel/timeout, souběh více workerů nebo klientů, pomalého čtenáře, zaplněnou frontu či disk, přerušenou odpověď, restart služby, změnu času, ztrátu sítě, lokální neprivilegovaný proces, reparse point a cestu ovládanou tímto procesem, kompromitovaného autorizovaného peer klienta. Kvůli útočníkem řízenému opakování se i vzácný race může stát realistický.

Před hlubokou analýzou zapsat vstup, schopnosti útočníka, běžnou cestu a smysluplný dopad. U nepodložené hypotézy použít krátký průzkumný limit přibližně 20 minut. Po něm buď konkretizovat a pokračovat, nebo zapsat důvod odložení do questions.md. Časový limit není důkaz neexistence chyby. Realistický vysoký dopad se nepřesune mezi „nereálné“ kvůli nedostatku prostředí. Fyzický power-loss může zůstat neprovedeným testem se známou mezerou, zatímco běžný pád procesu a poruchy zápisu se ověří izolovaně. Dokumentované přijetí rizika není vyloučení z auditu.

## 5. Sekvenční bloky

Ve všech blocích jsou závazné pravidla Z, společná šablona předání, stabilní ID, odkazy, deduplikace a řešení rozporů z oddílů 2 a 3. Následující specifikace určuje navíc konkrétní vlastnictví a obsah každého bloku. Přípony `.*` u C++ páru znamenají implementaci a její hlavičku, nikoli libovolné soubory celého repozitáře.

### B00: Rozhodnutí, inventura, model hrozeb a založení reportu

- **Účel a hranice:** definovat auditovaný zdrojový stav, důvěryhodnost hranic, normativní pravidla a úplnou mapu vlastníků. Zatím neprovádět podrobný audit všech modulů.
- **Vstupy:** celé `protocol.md`, `client.md`, `distribution.md`, relevantní `tray_UX.md`, `AGENTS.md`, intent a snapshot. Inventura všech sledovaných i relevantních nesledovaných souborů. Dokumentační indexy `Intents/`, `Plans/`, `Reports/`, `micro_updates/*/zadani.md`, `client/tests/*.md`, `tests-integration/README.md`, `client/tests/e2e/{README,manual-gates}.md`. Ze starších dokumentů načítat pouze části k přijatým rizikům, rozhodnutím a aktuálnímu chování.
- **Závislosti:** první blok. Zaznamenat HEAD, branch, dirty hashe, verze PHP/Windows/MSVC/CMake a dostupnost izolovaných prostředí. Při změně aktivní větve nic nepřepínat bez vyjasnění baseline.
- **Otázky bezpečnosti a výkonu:** kdo je externí útočník, peer, lokální aplikace, správce, relay a proxy? Co chrání TLS, HMAC, GCM a ACL? Která dostupnost a replay rizika jsou přijata? Jaké jsou limity, cílová zatížení a podporované platformy? Které dokumenty si odporují, zejména historické tvrzení o čtení konfigurace proti pozdějším read-only pravidlům?
- **Nenačítat:** celé zdrojové soubory všech modulů, logy předchozích realizací a všechny verze intentů. Nejprve metadata a cílené normativní části.
- **Výstup a zápis:** vytvořit audit.md hned při zahájení realizace s prázdnými hlavními tabulkami a konkrétními podmíněnými výlukami. Založit všechny registry, rozdělit každý relevantní soubor/řádkový rozsah mezi B01–B10, rozhraní mezi B11–B13. Případný již ověřený nález zapisovat Z, pouhé známé riziko z dokumentace zatím evidovat jako D-ID pro ověření v kódu.
- **Dokončení a validace:** žádná nezařazená relevantní oblast, obě hlavní tabulky ve správném pořadí, dohledatelné rozhodovací dokumenty, jednoznačný vlastník a testovací prostředí. Generované kopie a assets mohou mít zdůvodněnou kontrolu původu místo opakované řádkové kontroly.
- **Předání:** standardní souhrn, navíc mapa hranic, D-ID pro auth/replay/defaulty, seznam serverových vstupů a testovací baseline pro B01. ID a rozpory dle Z, dokumentační rozpor dostane C-ID, ne domnělou novou normu.

### B01: Serverový vstup, wire formát, autentizace a kryptografie

- **Účel a hranice:** request → parsování → autentizace → dispatch → odpověď. Obsah `phpvpn.php` mimo diskový storage B02, se zaměřením na bezpečnost a režii zpracování každého requestu.
- **Vstupy:** `phpvpn.php`, přes Graft zejména konfigurační hlavička/globální vstup, `Runtime`, `Clock`, `Iso8601`, `Base64Url`, `Validators`, `JsonWire`, `AesGcm`, `Config`, `KeyDerivation`, `Authenticator`, `Request`, `Http*`, `*ResponseWriter`, `Application`, `StatusPageHandler`, `HealthHandler`, `InfoHandler`, `DataHandler`. Orientační Graft span v plánovacím baseline: `JsonWire` 501–786, `AesGcm` 792–1170, auth 1298–1359, handlery 2064–2667. Dočíst celé přidělené definice včetně chybových cest. `tests.php`, `tests-info-extensions.php`, `tests-info-pretty-boundaries.php`, integrační `test_{status_and_routing,headers,health,info,info_extensions,time_skew,data_errors,limits,crypto_vectors}.py`, `phpvpn_client.py`. `protocol.md` §§1–9, 11–16, odpovídající D-ID.
- **Závislosti:** B00. Storage zatím hodnotit jako kontrakt `Storage`, jeho implementaci předat B02.
- **Otázky:** ověřuje se HMAC nad přesnými bajty před vedlejšími účinky? Jsou identity, requestId, AAD, IV a účely klíčů svázané správně? Tagy, konstantní porovnání a náhodnost? Limity před alokací a parsováním, duplicitní klíče, JSON/NDJSON, UTF-8 a přetečení? Kde vzniká práce před auth, opakované HKDF/AES/JSON, kopírování a velké `info`? Neunikají tajemství přes chyby a status? Respektuje návrh optimalizace povinné čisté PHP AES místo neautorizované výměny za OpenSSL?
- **Nenačítat:** klientské UI a interní storage, kromě deklarovaného kontraktu a konkrétního volání potřebného k potvrzení nálezu.
- **Výstup a zápis:** Z pro každý nález, mapa serverového wire/auth kontraktu I-ID, přesná místa nákladů a seznam storage předpokladů. Neoznačovat interoperabilitu za dokončenou před B11.
- **Dokončení a validace:** úplné pokrytí přidělených definic a globálního kódu, PHP syntaxe, cílené existující testy a nezávislé kryptografické vektory v izolaci. Oddělit neprovedené PHP verze/varianty od potvrzených výsledků.
- **Předání:** standard + autentizované vstupy storage, limity a očekávané chyby pro B02. Stejnou auth příčinu napříč endpointy deduplikovat dle Z, spor o normu zachovat jako C-ID.

### B02: Serverové fronty, filesystem a souběh workerů

- **Účel a hranice:** od autorizovaného publikování po atomické vydání/odstranění dávky, metadata a úklid. Vlastní `Fs`, `ClaimedBatch`, `QueueBusy`, `Storage`, `DiskStorage` a storage větev `DataHandler`.
- **Vstupy:** `phpvpn.php` Graft oblasti `Fs` 192–260, storage 1365–1815, `DataHandler::publish`, `emitBatch`, `afterResponse` a volající. `tests.php`, `tests-integration/test_{queue_capacity,expiry,concurrency,security,data_transport,info_extensions}.py`, `tools/test-info-storage.py`, `tools/test-status-variants.py`. `protocol.md` §§10–12, 16.4–16.6. Z B01 pouze storage kontrakty, limity a otevřené Q-ID.
- **Závislosti:** B01. Sdílené handlery B01 čte celé jako vstupní tok, B02 uzavírá řádky storage integrace v coverage.
- **Otázky:** cesta od identity k adresáři, guard soubory a dostupnost přes web, symlink/reparse podle prostředí, lock pořadí a timeout, atomické rename/claim, souběžní odesílatelé/polly, short write/flush/rename chyby, disk full, cleanup budget, TTL a monotónnost? Zachovají se limity a částečné přijetí? Cena listování, třídění, statistik, skenů a lock contention při běžném růstu počtu peerů a backlogu? Je ztráta dat skutečně dokumentovaný at-most-once důsledek, nebo jiná chyba?
- **Nenačítat:** klientské implementace, celý crypto modul znovu, provozní obsah front.
- **Výstup a zápis:** Z, tabulka stavů dávky, vlastnictví zámků, cost model I/O a reprodukce poruch na vlastním fixture. Zapsat i ověřené přijaté riziko se závažností.
- **Dokončení a validace:** fronta prázdná/plná/expirující, více workerů, částečné chyby a přerušené vydání ověřeny testem či přesným statickým důkazem. Nedostupné fault injection jako mezera, ne PASS. Úklid omezený na vlastněná data.
- **Předání:** standard + doručovací sémantika a nákladové prahy pro B03/B11/B12. Storage příčinu nesplitovat na jednu chybu pro každého volajícího, použít Z.

### B03: Klientský protokol, kryptografie a přenosné základní typy

- **Účel a hranice:** serializace requestu a autentizované parsování odpovědi až k validnímu IP payloadu. Vlastní `client/src/core/protocol/`, `core/crypto/` a `core/base/` kromě loggeru přiřazeného B09.
- **Vstupy:** všechny `.cpp/.h` v těchto adresářích, `client/src/platform/windows/crypto_cng.cpp`, odpovídající `client/tests/unit/test_{crypto,base64url,json,iso8601,wire,payload_batch,info_extensions,http_headers,password_key}.cpp` a pomocné harness soubory. `protocol.md` §§3, 6–9, 11, `client.md` konfigurace, příjem a bezpečnostní model. Z B01/B02 jen I-ID autentizace, velikosti a doručení.
- **Závislosti:** B02. Windows CNG je zde vlastněn kvůli kryptografické hranici, nikoli znovu B08.
- **Otázky:** přesné bytes a kanonizace, RNG a nonce, klíčové domény a směrové klíče, bezpečné buffery, validace tagu před použitím dat, fail-closed při změně počtu/pořadí záznamů, limity parsování a aritmetika délek, validita IPv4, neznámé/duplicitní členy? Režie serializace, base64, batch kopírování, vícenásobné parsování a konstrukce klíčů?
- **Nenačítat:** celé PHP, UI, IPC a fronty. K porovnání načíst jen příslušný serverový span, pokud se liší I-ID.
- **Výstup a zápis:** Z, byte-level kontrakty a matice nezávislých vektorů PHP/Python/CNG, seznam bezpečně měřitelných hotspotů.
- **Dokončení a validace:** přidělené typy a error paths pokryté, cílené unit testy, negativní vektory a hraniční délky. Shoda dvou implementací bez nezávislého očekávání není dostatečný důkaz správnosti.
- **Předání:** standard + vlastnictví paketových bufferů, limity batch a validace před enqueue pro B04. Shodnou serverovou/klientskou chybu spojit nebo explicitně doložit odlišnou příčinu dle Z.

### B04: Klientské fronty, flow klasifikace a plánování paketů

- **Účel a hranice:** přijetí validního paketu, kvóty, DRR, lease, expirace a request plan. Vlastní `client/src/core/queues/`.
- **Vstupy:** `flow_key.*`, `packet_queues.*`, `scheduler.*`, příslušné typy `protocol/ipv4.*`, `payload_batch.*` jen přes potřebný kontrakt. Testy `test_{flow_key,queues,queue_drr,queue_expiry,queue_concurrency,tunnel_queues}.cpp`, `queue_test_support.h`, `client/tests/flow-fair-queuing-report.md`. `client.md` oddíly Symetrické flow fronty, Monotónní expirace, Plánování a Povinné testy front. Z B03 limity a vlastnictví bufferů.
- **Závislosti:** B03. Integrace tunnel workerů zůstává B05.
- **Otázky:** per-peer/global kvóty, atomická rezervace a uvolnění, rollback/partial ACK, FIFO a starvation, přetečení čítačů, expirace in-flight paketů, souběžné producenty/konzumenty? Cena locků, bucket lookup, hashe a alokací, fairness bulk vs interaktivní, paměť při plném inbound/outbound?
- **Nenačítat:** transport, službu, instalátor a GUI. Jen rozhraní dodávající čas a crypto hasher.
- **Výstup a zápis:** Z, invariants front a lease I-ID, cost model a scénáře pro měření B12.
- **Dokončení a validace:** cílené produkční komponentové testy limitů, souběhu, expiry, fairness, rollback a úplné řádkové pokrytí oblasti. Nevyvozovat reálnou síťovou latenci z fake clock testu.
- **Předání:** standard + přesné commit/rollback a časové kontrakty pro B05, Q-ID vyžadující celý běhový tok. ID a rozpor lease vs transport řešit podle Z, ne dvojím záznamem.

### B05: VPN runtime, čas, polling a životní cyklus

- **Účel a hranice:** spojení, tři pracovní role, network exchange, recovery a teardown. Vlastní `client/src/core/runtime/` a `platform/iface/` kromě konfigurace přiřazené B06.
- **Vstupy:** `vpn_engine.*`, `network_worker.*`, `tunnel_workers.*`, `tunnel_session.*`, `time_sync.*`, `traffic_counters.*`, `status_snapshot.*`, interface `transport.*`, `tunnel.h`, `netconfig.h`, `clock.h`. Testy `test_{runtime_status,time_sync,traffic_counters,status_contract,tunnel_queues}.cpp`, `recording_tunnel.h` a runtime části `runtime_info_extensions.inc`. `client.md` stavový automat, čas, polling, příjem a transport. Z B03/B04 kontrakty parsování, ACK, front a času.
- **Závislosti:** B04. Windows I/O se posuzuje jako rozhraní, reálné callbacky B08.
- **Otázky:** epochy, přechody a opakované Connect/Disconnect, tajemství/konfigurace během změny, stav po chybě auth a po neúplné odpovědi, synchronizace času a restart, stop při čekání, ordering locků, životnost workerů? Rychlý/pomalý polling, backoff, re-synchronizace, serializované úzké místo, redundantní info a přesnost měřených byte counterů?
- **Nenačítat:** celou tray, installer a implementaci filesystemu. Stačí I-ID potřebná k životnosti.
- **Výstup a zápis:** Z, mapa stavů a časových rozpočtů, ownership vláken/bufferů a scénáře přerušení. Counters označit, zda měří pouze data body, či jiné vrstvy.
- **Dokončení a validace:** unit testy přechodů a deterministického času, důkaz ukončení/vlastnictví, mapování retry a neobnovitelných chyb. Skutečné I/O deadline není potvrzené jen fake transportem.
- **Předání:** standard + snapshot konfigurace, epochy a service-operation kontrakty pro B06/B07/B08. Napojení na B04 citovat existujícím ID, rozpory předat jako C-ID.

### B06: Konfigurace, tajemství a persistované dokumenty

- **Účel a hranice:** dokument na disku → validovaný snapshot → atomické uložení. Vlastní `core/config/`, `platform/iface/config_files.h`, `platform/windows/config_files_win.*`, `paths_win.*`.
- **Vstupy:** všechny zdroje těchto oblastí, testy `test_{config,config_store,config_files_win,default_config,wizard_document,contracts_document,http_headers}.cpp`, `fake_config_files.h`, `client/tests/config-readonly-report.md`. `client.md` Konfigurační soubor a Správa uložené konfigurace, `distribution.md` read-only datový strom, související nejnovější intent/rozhodnutí vyhledané v B00. Z B05 I-ID pro snapshot a epochu.
- **Závislosti:** B05. Záměrně veřejný IPC přístup se nesmí zaměnit za přímý write přístup k ProgramData.
- **Otázky:** UTF-8, limity souboru, defaulty a opravitelné chyby, zachování polí, atomické replace a last-writer-wins, read-only ACL, dědění, reparse/TOCTOU, backup/tmp, nechtěné vytvoření konfigurace při čtení? Kolik se čte, parsuje, kopíruje a flushuje při akci a pollingu? Jak změna kontraktů invaliduje běžící snapshot?
- **Nenačítat:** layouty dialogů, HTTP engine a celou historii konfigurace. Pouze normativní oddíly k dané operaci.
- **Výstup a zápis:** Z, tabulka read/write operací, oprávnění a cest, atomicity kontrakt a mapa citlivých dat. Testovací obsah výhradně syntetický.
- **Dokončení a validace:** cílené testy malformed/missing/oversized dokumentů a file API chyb, exact bytes před/po read-only akcích, ACL/reparse ověření na izolovaném stromu. Žádný zásah do skutečné ProgramData konfigurace.
- **Předání:** standard + transakční a validační kontrakty pro IPC/Wizard. Historický dokumentační rozpor zachovat pod C-ID, přijetí rizika nerozhodovat podle pohodlnější verze.

### B07: IPC, Wizard a hranice neprivilegované aplikace/služby

- **Účel a hranice:** lokální požadavek → dispatch → operace služby, Wizard sessions a síťové probe. Vlastní `core/ipc/`, `core/wizard/`, `platform/windows/ipc_pipe.*`.
- **Vstupy:** všechny soubory uvedených oblastí, testy `test_{ipc,ipc_availability_win,wizard_ipc,wizard_model,http_diagnostic}.cpp`, runtime/config rozhraní z B05/B06, `client.md` Configuration Wizard, Rozšíření IPC, Životnost a limity, HTTP detail a Bezpečnostní model. Relevantní části `client/tests/{http-diagnostic-audit-2026-09-13,connect-http-diagnostic-audit-2026-09-13,implementation-plan3-resource-report}.md`.
- **Závislosti:** B06. WinHTTP vlastní B08, UI akce B09.
- **Otázky:** whitelist a verze zpráv, délky před alokací, session/receipt identity, heartbeat/lease, stale odpovědi a epochy, exactly-once commit receipt vs retry, saturace spojení a queue, vzdálený přístup k pipe versus zamýšlený lokální, impersonation a privilegované operace? Režie serializace velké konfigurace, polling, thread/handle leak, sdílené zámky a zablokování VPN probe? Diagnostická výjimka nezakládá plošný souhlas s logováním tajemství.
- **Nenačítat:** rendering UI, server crypto, installer. Originál dokumentovaného přijetí veřejné pipe načíst povinně.
- **Výstup a zápis:** Z včetně potvrzených přijatých rizik, matice příkaz → oprávnění → vedlejší účinek → limit, resource model a session lifecycle.
- **Dokončení a validace:** testy chybných/velkých zpráv, obsazenosti, timeoutu, expirace a opakování receipt. Lokální neautorizované ovládání posoudit s plnou závažností a dokladem, nepovažovat accepted za bezpečný obecný bypass všech kontrol.
- **Předání:** standard + cancel/deadline kontrakty transportu pro B08 a UI lifecycle pro B09. Deduplikace podle operace a příčiny, ne podle počtu IPC příkazů.

### B08: Windows transport, tunel, síť a služba

- **Účel a hranice:** přenosné rozhraní → OS zdroje, asynchronní WinHTTP, Wintun, route ownership, SCM a startup/shutdown.
- **Vstupy:** `platform/windows/transport_winhttp.cpp`, `tunnel_wintun.cpp`, `netconfig_iphlpapi.cpp`, `clock_win.cpp`, `tray_autostart.*`, celé `client/apps/service/`. `client/third_party/wintun/{README.md,include/wintun.h}` a metadata používané DLL. Testy `test_{winhttp_accounting,service_registration,tray_autostart}.cpp`, `client/tests/platform_plan3_probe.cpp`, `client/tests/e2e/{service_e2e,service_main,test_process_dns}.cpp`, `test_http_contention.py`, související runnery. `client.md` Společný HTTP transport a Windows včetně povinných lifecycle testů. Z B05/B07 vlastnictví/cancel/epochy, z B06 cesty.
- **Závislosti:** B07. Crypto, file API a pipe nečíst znovu celé, jejich vlastníci jsou B03/B06/B07.
- **Otázky:** TLS ověření a trustAny režim, proxy/redirect/URL, jeden absolutní deadline, closing callback a use-after-free, zrušení/timeout/reuse, DNS a zavírání handle? Bezpečné načtení DLL a identity/architektura/API, ring a copy lifetime, vlastnictví adapteru/rout a journal recovery, změny DNS/default route, SCM command quoting, autostart a privilegia? Latence cancel/stop, sdílený HTTP lock, ring backpressure a CPU probouzení?
- **Nenačítat:** celou distribuci, design tray a frontend modely. Jen požadované balíčkové invarianty z dokumentace pro B10.
- **Výstup a zápis:** Z, matice OS resources → vlastník → release, provozní limity a skutečné měřicí scénáře. Žádná univerzální horní mez callbacku se neodvodí z jediného úspěšného měření.
- **Dokončení a validace:** cílené unit/ASan testy, izolovaný platformní probe a SCM/Wintun test pouze po ověření konfliktů a vlastnictví fixture. Výsledek testu musí obsahovat baseline a úklid rout/DNS/služby. Dostupnost UAC/VM nepředpokládat.
- **Předání:** standard + datové/diagnostické kontrakty pro UI a požadavky na DLL/SCM pro B10. Rozdíl timeout vs skutečné dokončení rušení udržet pod jedním I-ID.

### B09: Tray, CLI, prezentace a diagnostika

- **Účel a hranice:** vstupy uživatele, IPC klienti, modely oken, lifecycle a logování. Vlastní celé `client/apps/tray/`, `client/apps/cli/`, `core/presentation/` a `core/base/logger.*`.
- **Vstupy:** všechny zdroje uvedených oblastí včetně hlaviček, resources a manifestových vazeb, testy `client/tests/unit/test_tray_*.cpp`, `test_wp*.cpp`, `test_{contracts_model,server_info_model,server_info_layout,password_key,logger}.cpp`, `client/tests/ui/` a jejich README. `tray_UX.md` zejména UX04–UX14, `client.md` UI, Contracts, Exit a logování, související micro_updates nalezené B00. Z B07/B08 jen odpovědi IPC, session a cancel invariants.
- **Závislosti:** B08. Vizuální kosmetika bez dopadu na logiku/výkon/bezpečnost není samostatným cílem auditu.
- **Otázky:** inertní zobrazení nedůvěryhodného HTTP textu, citlivý clipboard a diagnostika, UI→service změny, stale async výsledky a zavírání oken, double submit, dirty-state a nechtěné uložení/Connect? UI thread blokace, periodické dotazy, zbytečné invalidace/repaint, kopie velkých textů, handle/GDI leak, paging, synchronní flush loggeru a citlivé callsites?
- **Nenačítat:** PHP a crypto znovu, screenshotové archivy a neaktuální reporty bez cílené otázky.
- **Výstup a zápis:** Z, mapa akcí a lifecycle, audit logger callsites pomocí exhaustivního hledání v celé produkci a měřicí body odezvy/UI zdrojů.
- **Dokončení a validace:** cílené modelové a lifecycle testy, relevantní izolovaný UI runner, důkaz žádných opožděných callbacků do zničených oken a správných vedlejších účinků. DPI/sleep/multiuser neprohlašovat za ověřené bez reálného scénáře.
- **Předání:** standard + aplikace/resources potřebné k balíčku pro B10, I-ID citlivých dat pro B13. Duplicitní leakage napříč log/okno rozlišit dle sinku a přijatého rozsahu, pravidla Z platí.

### B10: Build, distribuce, instalace a auditní infrastruktura

- **Účel a hranice:** zdroje → binárky/balíček → instalovaná služba. Zároveň ověřit důvěryhodnost testovacího oracle a zbývající podpůrné soubory inventury.
- **Vstupy:** `client/CMakeLists.txt`, `CMakePresets.json`, `client/{src,apps,tests}/CMakeLists.txt`, `client/apps/app.manifest`, `version.rc.in`, `client/tools/` včetně `package/`, `tools/`, `copydistributiontovm.{ps1,bat}`, `client/third_party/wintun/`, `client/tests/test_{package_upgrade,package_feedback,deploy_profiles}.ps1`, `tests-integration/{conftest.py,pytest.ini,phpvpn_client.py}`. `distribution.md`, Windows install/upgrade oddíly `client.md`, `client/tests/package-feedback-report.md`, příslušné `Reports/2026-09-19-00-47-57/`. Graft launchery a `tests/test_graft_launchers.ps1` zařadit jako nástroje auditu s přiměřenou kontrolou. Z B08 ověřovací a instalační invarianty.
- **Závislosti:** B09. Vlastní všechny dosud nezařazené build/deploy/test harness oblasti, ne další plný audit už pokrytého produkčního kódu.
- **Otázky:** signature trust root a manifest, verze a původ Wintun, path/command injection a quoting, DLL search, architektura, downgrade/tamper, práva programu a dat, upgrade selhání a odstranění dat, únik secrets do dist/test logs, rozdíl zdroj/nasazená kopie? Build optimalizace a debug runtime, opakované kopírování/verifikace, náklady startu/instalace? Test oracle nekopíruje stejnou chybu jako produkt a nevynechává limity pomocí skip?
- **Nenačítat:** binární obsah do textového kontextu, všechny historické ZIPy a reporty. Binárky kontrolovat hash/signature/metadata, aktuální bezpečnostní advisories pouze z primárních zdrojů s datem a přesnou verzí.
- **Výstup a zápis:** Z, seznam skutečných závislostí/ověřovacích kořenů, mapa build a deploy artefaktů, kontrola testovacího harness, poslední mezery coverage. Neprovádět novou distribuci ani kopírování na VM jen proto, že se audituje skript.
- **Dokončení a validace:** inspekce skriptů a existující izolované testy, VerifyOnly na vlastním fixture, doložení obsahu balíčku proti zdrojům. Skutečný install/upgrade/uninstall jen na určeném testovacím stroji, nikoli náhodně nad používanou službou `phpvpn`. Pokud není prostředí, explicitní NOT RUN.
- **Předání:** standard + reprodukovatelné příkazy a prostředí pro B11/B12, známé mezery testů. Dodaný certifikát a jeho důvěryhodnost nerozporovat pouhým výsledkem „signature valid“, evidovat přesnou hranici a Z.

### B11: End-to-end komunikace a shoda mezi hranicemi

- **Účel a hranice:** ověřit celé řetězce, které nelze uzavřít jedním subsystémem. Neprovádět druhé lineární čtení repozitáře.
- **Vstupy:** I-ID a otevřené Q/C z B01–B10, `protocol.md` §§12, 16, 17, klientské scénáře, `tests-integration/test_*.py`, `client/tests/contract/test_server_contract.cpp`, `client/tests/e2e/README.md`, relevantní E2E/controller kód a runnery. Konkrétní zdrojové spany obou konců každé kontrolované hranice.
- **Závislosti:** B10 a kontrakty všech předchozích bloků. Před testy zkontrolovat shodu deploy/test/client source baseline.
- **Otázky:** proveď a dokumentuj (1) health→čas→info→auth→Connect, (2) IPv4→tunel→fronta→batch→crypto→HTTP→PHP→disk→poll→MAC/GCM→fronta→Wintun→cílový socket, oba směry, (3) full-auth info a A/B/C/D maskované kontrakty, (4) chyby/partial ACK/retry/TTL/replay a ztracená odpověď, (5) UI/CLI→pipe→config/Wizard→runtime, (6) Disconnect/cancel/stop/restart/startup Connect. Kde se mění identita, počítání délky, čas, ownership a význam chyby? Není end-to-end výkon uměle limitovaný fixture?
- **Nenačítat:** celé moduly bez otevřené otázky. Postačí producent, konzument, test a originál normy.
- **Výstup a zápis:** Z, matice tok × krok × očekávání × důkaz × výsledek. Každá hranice má bezpečnostní i výkonnostní závěr a otevřené mezery.
- **Dokončení a validace:** reálný roundtrip přes PHP/WinHTTP/CNG/Wintun a nezávislý Python oracle tam, kde je dostupný, HTTP i HTTPS/ověření certifikátu a proxy na izolovaném prostředí. Pokud něco nelze spustit, uvést přesně chybějící důkaz a proč statická kontrola nestačí. SKIP není PASS.
- **Předání:** standard + validní workloady, počítadla a oddělení režie pro B12. Nález známý z B01–B10 rozšířit o E2E důkaz pod původním ID.

### B12: Reprodukovatelné výkonové ověření

- **Účel a hranice:** potvrdit výkonové mechanismy a prioritizovat zrychlení bez implementace oprav. Měřit aktuální dodanou implementaci.
- **Vstupy:** PERF index, hotspoty B01–B10, validované toky B11, `tests-integration/test_performance.py`, `test_concurrency.py`, `client/tests/flow-fair-queuing-report.md`, `implementation-plan3-resource-report.md`, dostupné runnery a CMake release preset. Zdrojové spany pouze k naměřenému hotspotu.
- **Závislosti:** B11. Testovat optimalizovaný `RelWithDebInfo`, nikoli vydávat Debug/ASan latence za produkční výkon. Připravit omezený budget a stop podmínky měření podle oddílu 6.
- **Otázky:** která fáze dominuje latenci/throughput, kolik stojí PHP soft AES, disk scan/lock, JSON/base64 a kopie, CNG, queue/DRR, WinHTTP/DNS/TLS, idle polling, logger/UI/IPC contention? Jak se mění p95/p99, CPU, RSS, alokace, I/O a loss s více peery, batch a backlogem? Nevytváří navržená cache, batching či paralelizace nové nonce, replay, stale-key, fairness nebo memory riziko?
- **Nenačítat:** celou bezpečnostní tabulku v detailu, jen související SEC/D/I-ID. Nenahrávat raw traces do kontextu, nejprve agregace a konkrétní relevantní výřezy.
- **Výstup a zápis:** Z po každém ověřeném hotspotu, tabulka prostředí a workloadů, raw měření a zpracování, mechanismus návrhu, odhad/naměřená hodnota a bezpečnostní trade-off. Bez změny produktu nelze tvrdit dosažené procento zrychlení návrhu.
- **Dokončení a validace:** opakované běhy, reprodukovatelné parametry, kontrola correctness/loss a baseline, odlišení cache warm/cold, nejistota měření. Všechny „důležité“ položky mají doložený poměr přínosu/rizika, jinak jsou podmíněné. Nemožnost měření nezneplatní staticky potvrzenou zbytečnou práci, ale omezuje tvrzení o přínosu.
- **Předání:** standard + každý návrh výkonu a jeho bezpečnostní předpoklady pro B13. Aktualizovat původní PERF ID, nepřidávat druhou položku za naměření stejné příčiny.

### B13: Křížová bezpečnostní kontrola a rozřešení rozporů

- **Účel a hranice:** adversariálně přezkoumat spojení subsystémů, přijatá rizika, výkonové návrhy a oprávněnost výluk. Není to implementace nápravy.
- **Vstupy:** obě tabulky audit.md, decisions/interfaces, všechny neuzavřené Q/C, souhrny B00–B12, originální dokumentační doklady k jednotlivým SEC, cílené kódové spany. Model hrozeb B00, testovací mezery B10/B11 a trade-off B12.
- **Závislosti:** B12. Lze použít jiný výslovně vytvořený sekvenční task, nikdy souběžného zapisovatele.
- **Otázky:** skládá se přijatý HTTP replay s další chybou do nepřijaté eskalace? Co dokáže neprivilegovaný lokální proces, autorizovaný peer a síťový útočník? Neobchází validace jiné rozhraní? Je sdílení klíčů/důvěra relay/diagnostika správně omezená? Návrhy optimalizace zachovají autenticitu, nonces, limity, fairness, cancellation a izolaci? Existuje konkrétní reprodukce nebo úplný statický důkaz každého zásadního tvrzení?
- **Nenačítat:** celý kód znovu, nesouvisející UI a surové benchmarky. Otevřít jen hranice potřebné k napadení závěru.
- **Výstup a zápis:** Z i pro nově ověřené křížové nálezy, rozhodnutí u každého C-ID, revize výluk, závažností, přijetí a priorit. Vyvrácení zachová historii a ID.
- **Dokončení a validace:** každé SEC má závažnost a ano/ne s dokladem či nedoložením, každé důležité PERF prošlo bezpečnostní kontrolou, každá výluka přežila test realistické dosažitelnosti. Materiální otevřený rozpor brání bezvýhradnému dokončení, není tiše zavřen.
- **Předání:** standard + konsolidační seznam zbývajících mezer a rozhodnutí pro B14. Při přesahu přijetí oddělit dokumentovaný rozsah a nové riziko, nededuplikovat rozdílné invarianty jen kvůli podobnému slovu „replay“.

### B14: Úplnost, konzistence a závěrečný report

- **Účel a hranice:** uzavřít evidenci a přehledně předat výsledky. Nevytvářet opravy, commit, push ani distribuci.
- **Vstupy:** celý audit.md, coverage a manifest aktuálního zdroje, indexy, kontrolní výsledky, souhrny všech bloků a otevřené Q/C. Kód/dokumenty jen k ověření odkazů a posledních mezer.
- **Závislosti:** B13. Zdrojové změny během auditu znamenají znovu otevřít příslušné pokrytí a navazující kontrakty, nikoli jen aktualizovat číslo HEAD.
- **Otázky:** každý relevantní řádek a hranice má vlastníka a výsledek? Byly všechny ověřené nálezy uloženy právě jednou? Jsou závažnosti a přijetí nezávislé, odhady označené, měření odpovídají baseline a dokumentace si neodporuje bez přiznání? Nevynechala změna skriptu/hlavičky část scope?
- **Nenačítat:** historii chatů a všechny raw evidence. Použít manifesty a cílené důkazy.
- **Výstup a zápis:** dokončený audit.md se závěrečnou kontrolou, prioritami a realistickými omezeními. Z pro případný nově potvrzený nález, index a závěrečný handoff/B14.md se stavem práce a odkazy. Žádné nálezy pouze v závěrečné zprávě mimo audit.md.
- **Dokončení a validace:** kontrola všech akceptačních kritérií oddílu 7, existujících cest/řádků/anchorů, ID/aliasů a tabulek. `complete` pouze bez nepřiznané relevantní mezery. Jinak `dokončeno s omezeními` s konkrétním dopadem, nebo `blocked` u kriticky chybějícího důkazu. NOT RUN/FAIL/SKIP se nikdy nepřepíše na PASS při konsolidaci.
- **Předání:** standard + stručný výsledek koordinátorovi, přímý odkaz na audit.md, počet unikátních příčin podle typu a závažnosti, stav dokončení a první krok případného doplnění. Všechna ID a rozpory zachovány dle Z.

## 6. Testování, měření a bezpečný provoz auditu

Tato kapitola určuje budoucí validaci, nic zde nebylo spuštěno při plánování. Před použitím příkazů ověřit aktuální README a parametry runneru. Neodvozovat podporu argumentů z názvu souboru.

Základní existující vstupy pro realizaci:

```text
php -l phpvpn.php
php tests.php
php tests-info-extensions.php
php tests-info-pretty-boundaries.php
```

PHP executable zjistit v prostředí, nepředpokládat historickou cestu v dokumentaci. Pokud tests.php již zahrnuje další testy, nespouštět tutéž sadu zbytečně dvakrát. C++ ve správně připraveném x64 MSVC prostředí z adresáře `client/`:

```text
cmake --preset release
cmake --build --preset release
ctest --preset release --output-on-failure
```

Pro cílené testy nejprve zjistit registrované názvy přes CTest/harness. Presety debug/asan použít pro související validaci paměti a životnosti, pokud je prostředí podporuje. ASan ani statická analýza samy nepotvrzují absenci datových race nebo správnost protokolu.

Izolovaná Python integrace používá výhradně uv:

```text
uv run --python 3.13 --with pytest --with cryptography --with requests -- pytest tests-integration -v --server-config <izolovany-fixture.json> --large-body-url <izolovany-endpoint>
```

Skupiny `not slow`, `perf` a jednotlivé soubory volit dle bloku. Plný běh a `slow` testy jsou potřebné tam, kde se prokazuje TTL, nikoli při každém čtení kódu. Výchozí `tests-integration/server.json` nepřebírat bez kontroly cíle, cleanup fixture běžnými polly vyprázdňuje fronty. Použít vlastní PHP kopii, porty, storage mimo webroot a vlastní klíče. Zdokumentovat rozdíl originálu a fixture hlavičky, hash nasazené kopie a kapacitu PHP workerů. Vestavěný PHP server s jedním workerem nenahrazuje důkaz produkčního souběhu.

Před `run-service-e2e.ps1`, `run-platform-probe.ps1`, `run-resource-tests.ps1`, `run-server-contract.ps1` a UI runnerem načíst jejich parametry a izolační podmínky. Nespouštět produkční install/uninstall, kopírování do VM, uspání, logoff nebo power-loss bez konkrétně určeného testovacího prostředí a nutného oprávnění. Nedostupné prostředí nezastaví nezávislý statický audit, ale zůstane jako přesná mezera. Před rekursivním úklidem ověřit absolutní cesty a vlastnictví, používat jediný PowerShell a `-LiteralPath`, nikdy mazat podle jména produkčního procesu/služby.

### Minimální výkonová matice B12

| Scénář | Parametry a účel | Povinné metriky |
|---|---|---|
| Idle a znovu aktivní | Připojený klient bez payloadu, potom krátký burst, UI zavřená/otevřená | Poll gaps, CPU, request/s, přechod do rychlého pollingu, latence první zprávy |
| Interaktivní a bulk | Malé IPv4/UDP pakety a pakety blízko podporovaného MTU, jeden i více peerů | Užitečné B/s, packets/s, RTT p50/p95/p99, loss/duplicate, fairness |
| Batch a backlog | Jednotlivý record, výchozí maximum recordů, backlog prázdný/střední/u limitu | Serializační bajty, body overhead, PHP/C++ CPU, RSS, alokace/kopie, drain čas |
| Souběh | 1, 4 a až 14 testovacích identit dle fixture, více PHP workerů, současní sendeři/polly | Lock wait, disk I/O, throughput, p95/p99, deferred/dropped, paměť |
| Síť a transport | HTTP, HTTPS s ověřováním a proxy, loopback vs kontrolovaná latence, pomalé tělo/DNS | Fáze requestu, deadline/cancel/stop, transportní režie oddělená od payloadu |
| Řízení při provozu | IPC status/info/config, Wizard a diagnostika souběžně s VPN, bounded saturace IPC | UI/IPC latence, vlákna/handles/GDI, RSS, dopad na VPN, cleanup |
| Dlouhodobý stabilní stav | Přiměřeně dlouhý opakovaný provoz, cykly reconnect a expiry | Trend RSS/handles/front, úniky, stabilita throughput, logger I/O |

Výchozí měřicí protokol: nejméně 10 s warm-up a 5 opakování po alespoň 30 s pro běžné throughput/latency scénáře. U delších TTL/lifecycle testů délku odvodit od skutečného kontraktu a uvést odlišnost. p99 uvádět jen s dostatečným počtem vzorků, doporučeně alespoň 1000, jinak maximum/rozsah a nejistotu. Randomizovat pořadí srovnávaných workloadů nebo střídat baseline, zaznamenat hardware, OS, CPU, memory/storage, PHP verzi/SAPI/počet workerů, OPcache, C++ preset, síť a konfiguraci. Nezobecňovat loopback na WAN ani jeden stroj na všechny deploymenty.

Před měřením stanovit pracovní kapacitu fixture a konečný limit času, paměti a disku. Výchozí zastavení při volném disku pod 2 GiB, dostupné RAM pod 1 GiB, ztrátě kontroly nad testovací službou nebo neočekávané změně produkčních rout/DNS. Pokud je host menší nebo sdílený, zvolit konzervativnější profil před spuštěním a zapsat jej. Zátěž zvyšovat po krocích, nikdy ne neomezeně. Limity testů nesmějí vytvářet závěr, že aplikace nemá problém nad dosaženou zátěží.

Bez implementované optimalizace se očekávané zrychlení odvozuje z podílu ověřeného hotspotu a konkrétního návrhu. Uvést horní mez/rozsah a předpoklady, ne fiktivní A/B výsledek. Měřicí adapter musí popsat případnou vlastní režii. Pro každé měření současně kontrolovat integritu/doručení, protože vyšší throughput dosažený ztrátou paketů není zrychlení stejné služby.

## 7. Akceptační kontrola celé realizace

1. Auditovaný baseline je známý včetně dirty souborů a změn během auditu. Všechny relevantní soubory a řádkové rozsahy mají výsledek v coverage.tsv. Žádný výběr top-N z Graftu není vydáván za úplnost.
2. Pokryté jsou server, klient, privilegované i neprivilegované rozhraní, podpůrné skripty, závislosti, skutečné komunikační toky a rozhraní. Externí binární komponenty mají výslovně vymezenou úroveň kontroly.
3. Kořenový audit.md vznikl v B00 a každý ověřený nález byl zapsán před pokračováním k dalšímu. Historie evidence a checkpointy umožňují tento postup doložit.
4. Na začátku reportu jsou konkrétní výluky s podmínkami a přezkumem realistické zneužitelnosti. Omezené nebo neprovedené testy jsou od výluk jasně odlišené.
5. První hlavní tabulka obsahuje všechna ověřená výkonnostní zjištění a všechna požadovaná pole. Naměřené hodnoty a odhady jsou odlišené, důležitost odpovídá bezpečnostnímu dopadu.
6. Druhá hlavní tabulka obsahuje všechna ověřená bezpečnostní a zásadní logická zjištění, doporučení, vždy závažnost, vždy ano/ne u přijetí a přesný doklad nebo nedoložení.
7. ID jsou stabilní a jedinečná, aliasy zachované, společné příčiny deduplikované. Bezpečnostní/výkonové křížové odkazy nezvyšují falešně počet nezávislých problémů.
8. Každý závěr má dohledatelný důkaz, platné odkazy na kód a dokumentaci a uvedené podmínky. Statický důkaz, test PASS, FAIL, SKIP a NOT RUN se nesměšují.
9. Všechny rozpory a hypotézy jsou buď uzavřené s důkazem, nebo přiznané s dopadem na úplnost. Materiální mezera se neschová za obecné „audit hotov“.
10. Výkonové návrhy prošly B13. Nezavádějí automaticky změnu normy, klíčů, replay modelu nebo nově přijaté riziko. Opravy zůstávají doporučením pro samostatné rozhodnutí.
11. Testovací fixture je bezpečně uklizené nebo výslovně předané s přesným vlastníkem. Produkční konfigurace, služby a uživatelské změny zůstaly bez neautorizovaných zásahů.
12. B14 odeslal koordinátorovi stručný výsledek, odkaz na report, stav dokončení a zbývající práci. Plán ani intent se při realizaci nepřepisovaly.

## 8. Ověření připravenosti tohoto plánu

Plán je postup pro budoucí audit, ne seznam tvrzených zranitelností. Použité názvy souborů, subsystémy, testové vstupy a normativní dokumenty byly při plánování dohledány v repozitáři a přes Graft. Orientační rozsahy PHP slouží k navigaci, před skutečným auditem se znovu ověří proti jeho baseline. Všechny bloky mají účel, konkrétní vstupy, pořadí, otázky obou disciplín, omezení kontextu, povinný výstup/Z, validační kritéria i předání. Společná pravidla vlastnictví, ID a rozporů jsou závazná bez opakovaného přepisování do každého tasku.

Proveditelnost nevyžaduje nyní materiální rozhodnutí o změně zadání. Dostupnost testovacího stroje, HTTPS/proxy a manuálních bran se zjistí v B00. Pokud dostupné nejsou, plán zachovává přesné mezery a neprohlašuje chybějící validaci za dokončenou. Před zahájením realizace je nutné samostatné uživatelovo rozhodnutí o realizaci plánu.
