# Plan3 fix3: pracovní rozdělení pro samostatné agenty

## Účel a autorita

Tento dokument je orchestrace budoucí implementace [plan3_fix3.md](plan3_fix3.md). Nemění žádné technické ani UX rozhodnutí problémů 1 až 12. Zdroj pravdy pro cílové chování zůstává `plan3_fix3.md`, zejména N01 až N09, F00 až F09 a A01 až A32. `protocol.md` zůstává autoritou wire protokolu, `client.md` autoritou ostatních klientských pravidel, `tray_UX.md` autoritou cílového tray UX a `distribution.md` autoritou vydání.

Dokument slouží koordinátorce nebo koordinátorovi, který spouští jednotlivé agenty s novým nebo téměř prázdným kontextem. Každá pracovní karta níže je samostatně použitelný delegační prompt. Agent nesmí nahrazovat četbu uvedených zdrojů shrnutím předchozí konverzace.

Při rozporu platí toto pořadí:

1. aktuální uživatelské a projektové instrukce,
2. schválená rozhodnutí v `plan3_fix3.md`,
3. normativní `protocol.md`, `client.md`, `tray_UX.md` a `distribution.md` po zapracování výslovných náhrad N01 až N09,
4. tento organizační dokument,
5. historické plány a implementační reporty pouze jako evidence tehdejšího stavu.

Tento dokument neopravňuje k wire změně, novému IPC příkazu, instalaci služby, zápisu do ProgramData, změně distribuce, commitu ani pushi. Takové kroky vyžadují samostatné výslovné zadání. Při implementaci se nemění `protocol.md` ani `distribution.md`, pokud nový zásadní problém nejprve výslovně neschválí uživatel.

### Neměnné významy problémů 1 až 12

Rozdělení práce nesmí změnit následující rozhodnutí. Čísla jsou totožná s mapováním v `plan3_fix3.md`.

| Problém | Neměnný význam napříč balíčky |
|---|---|
| 1 | Connect jednou použije existující `get-config` pro zobrazené URL/IP. `get-status` zůstane beze změny a rozdíl uloženého a aktivního snímku je přijatý. |
| 2 | Wizard Finish pouze uloží a zavře. Contracts se otevírá později ručně nad aktuálním dokumentem, bez handoff IPC nebo bufferu. Rychlá vývojová cesta nekopíruje do dist. |
| 3 | Omezené lokální Password SHA a příprava kandidáta mohou být synchronní, IPC vždy zůstává asynchronní. Jednotkou limitu hesla je Unicode skalární hodnota. |
| 4 | Výkonová brána používá přesné cíle, warmup a nejhorší z nejméně tří měřených běhů. |
| 5 | Editory jsou nezávislé, Password SHA je vícekrát nemodální, změny se vážou na generaci a remoteIp, poslední potvrzená publikace vyhrává a Exit/logoff má přesnou společnou sémantiku. |
| 6 | Globální validace přijímá jen `/24` až `/29`. Contracts má nejvýše 252 skutečných nativních řádků bez virtualizace a dříve platné širší konfigurace se automaticky nemigrují. |
| 7 | DOM patří do `core/config`, čisté modely do `core/presentation`, Win32 adaptéry do `apps/tray` a vše zůstává v existující statické knihovně bez nové DLL nebo frameworku. |
| 8 | Chyby Key a Random používají přesné jednotné texty s konkrétním `contractId`, jejich autoritou je UX11 po předepsané aktualizaci. |
| 9 | Monitor se vybírá deterministicky a reaktivace zachovává geometrii nebo provede jen nezbytný clamp. |
| 10 | Prostředky se cachují na okno/DPI, invalidace a `NIM_MODIFY` jsou změnové a platí brány 500 paintů a 20 lifecycle cyklů. |
| 11 | UI je pouze anglické a nezávislé na systémovém locale, bez nového lokalizačního frameworku. |
| 12 | Vizuální brána používá přesné klientské plochy, DPI, tolerance 6/8 DIP a samostatný záznam všech jedenácti mockupů. |

## Doporučená topologie

Výchozí a doporučený způsob je **devět sekvenčních balíčků v jediném uloženém checkoutu**. Každý balíček dostane nový agentní kontext. Čisté kontextové okno se tím využije bez rozdělení technicky soudržných změn mezi soupeřící vlastníky.

```text
WP00 Baseline a globální CIDR brána
  |
  v
WP01 Přenosné Contracts jádro
  |
  v
WP02 Sdílená Win32 infrastruktura a TrayApplication seam
  |
  v
WP03 Connect, Edit Config a Statistics
  |
  v
WP04 Wizard UX a Finish
  |
  v
WP05 Contracts end-to-end, Save, souběh a Exit/logoff
  |
  v
WP06 Izolovaný vývojový runner a UI harness
  |
  v
WP07 Normativní dokumentace a trasovatelnost
  |
  v
WP08 Finální integrační a regresní brána
```

### Proč je zápisová práce sekvenční

- `main.cpp`, pozdější `tray_application.*`, menu, IPC čtečky a životní cyklus oken tvoří jeden koordinační uzel. Connect, Wizard, Contracts, Exit i preview potřebují stejný message loop a stejné injektované endpointy.
- `client/src/CMakeLists.txt`, `client/apps/CMakeLists.txt` a `client/tests/CMakeLists.txt` jsou krátké explicitní seznamy. Paralelní přidávání zdrojů má vysokou pravděpodobnost textového i významového konfliktu.
- `tray_application.*`, `wizard_window.*`, `contracts_window.*` a UI harness sdílejí focus, DPI, generace, čtečky a ukončování. Dočasně rozdílné kontrakty životního cyklu by vytvářely chyby obtížně odhalitelné při prostém sloučení.
- `client.md` a `tray_UX.md` obsahují stará protichůdná pravidla. Mají jediného pozdního vlastníka, aby několik agentů nevkládalo nové odstavce vedle neodstraněných starých vět.
- Projektové instrukce preferují práci přímo v uloženém Git projektu. Bez výslovného povolení worktree nelze dvě zapisující úlohy bezpečně izolovat.

### Co lze paralelizovat jen po výslovném rozhodnutí koordinátora

Read-only audit zdrojů, kontrola textových rozporů a příprava seznamu testů mohou běžet souběžně, pokud žádná větev nezapisuje. Po WP00 by šlo v oddělených větvích nebo výslovně povolených worktrees paralelně připravovat WP01 a čistou část WP02, ale pouze při těchto omezeních:

- WP01 vlastní `client/src/core/config/contracts_document.*`, `client/src/core/presentation/*` a své nové unit testy.
- WP02 vlastní pouze nové obecné tray utility a samostatné testy geometrie. Nesmí sahat na Contracts model, `main.cpp`, CMake ani sdílené existující testy.
- CMake registraci, `TrayApplication` seam a integraci obou větví provede až jeden další agent.

Tato možnost se nedoporučuje. Úspora času je malá a přesun CMake a integračního seam do třetího kroku zvyšuje riziko, že každá větev ověří jiný neúplný target. WP03 a WP04 se také nemají paralelizovat, i když jejich hlavní okna jsou odlišná, protože obě potřebují měnit centrální tray koordinaci a společný UI základ.

## Globální pravidla provedení a předání

### Vstupní brána každého balíčku

Agent před první změnou:

1. přečte celý svůj delegační prompt, celý `plan3_fix3.md` a všechny konkrétní části dokumentů uvedené v kartě,
2. ověří, že pracuje v očekávaném adresáři a načte případný `AGENTS.md`,
3. spustí read-only `git status --short`, `git diff --name-only`, `git diff --stat` a `git rev-parse HEAD`,
4. porovná stav s předávacím reportem předchozího balíčku,
5. přečte aktuální diff v souborech, které přebírá, a zachová všechny cizí nebo dřívější změny,
6. ověří, že předchozí balíček splnil svou dokončovací bránu a že jeho report neobsahuje nevyřešený blokátor.

Neshoda seznamu změněných souborů, chybějící předávací report, konflikt v převzatém kontraktu nebo neúspěšná povinná brána znamenají **STOP před zápisem**. Agent popíše konkrétní rozdíl koordinátorovi. Nemá resetovat, checkoutovat ani přepisovat cizí změny.

### Pravidla práce

- Změny musí zůstat v rozsahu balíčku. Zjištěná potřeba mimo vlastnictví se uvede jako integrační požadavek, neprovede se potají.
- Agent nevytváří nový IPC příkaz, wire formát, službový dispatcher ani nový build systém. Výjimku nelze odvodit z pohodlí implementace.
- Testy používají syntetická data. Logy, screenshoty a reporty nesmějí obsahovat skutečné konfigurace, hesla ani klíče.
- Žádný balíček neinstaluje nebo neregistruje službu, nespouští plnou distribuci, nepíše do `dist`, `dist_old_do_not_use` ani ProgramData a nemanipuluje s reálnou VPN.
- Široké testy, CTest celé agregované sady, instalační a distribuční testy se nespouštějí automaticky. Každá karta uvádí úzké povinné ověření.
- Agent nesmí tvrdit PASS u neprovedeného testu. Uvede `NOT RUN` a důvod.
- Agent neprovádí commit ani push, pokud to jeho aktuální výslovné zadání nepovolí.
- Před opuštěním balíčku agent spustí `git diff --check` a ověří rozsah přes `git diff --name-only`.

### Povinný předávací report

Každý agent vrátí koordinátorovi tento strukturovaný report:

```text
Balíček:
HEAD a výchozí stav:
Převzaté změny předchozích balíčků:
Změněné soubory:
Implementované kontrakty N/F/A:
Klíčová rozhraní a nové symboly:
Provedené příkazy a výsledky:
NOT RUN a důvod:
git diff --check:
Odchylky od plánu nebo otevřené otázky:
Citlivé údaje / dist / ProgramData / služba: potvrzení nulového zásahu
Přesný stav, který musí další agent ověřit:
```

Pokud report uvádí blokátor, další balíček nezačíná. Pokud report uvádí pouze omezení prostředí, koordinátor rozhodne, zda lze pokračovat s jasně označeným `NOT RUN`.

## Vlastnictví souborů a převody odpovědnosti

Vlastnictví znamená právo zapisovat během daného balíčku. Read-only kontrola jiných souborů je dovolena.

| Oblast | Primární vlastník | Pozdější povolený vlastník | Pravidlo |
|---|---|---|---|
| `core/protocol/ipv4.*`, globální CIDR validace a příslušné stávající config testy | WP00 | WP08 jen cílená oprava po prokázané regresi | WP01 až WP07 rozhraní používají, nemění rozsah. |
| `core/config/contracts_document.*`, `core/presentation/contracts_model.*`, `password_key_model.*` a jejich nové unit testy | WP01 | WP05 pro nutnou korekci potvrzenou testem, WP08 integrační oprava | WP02 až WP04 model pouze konzumují. |
| Obecné `window_utils.*`, `ui_style.*`, `ui_resources.*`, `ui_controls.*`, obecný layout a `tray_application.*` seam | WP02 | WP03 až WP06 pouze ve svém přesném integračním rozsahu | Změna společného kontraktu musí být popsána v reportu pro všechny další balíčky. |
| `progress_*`, `editor_window.*`, `statistics_window.*`, jejich specializované layouty a testy | WP03 | WP06 jen preview adaptér, WP08 oprava regresí | WP04 a WP05 je nemění. |
| `wizard_window.*`, `wizard_layout.*`, Wizard prezentační texty a Finish callback | WP04 | WP06 jen preview seam, WP08 oprava regresí | Lifecycle Wizard služby, lease a receipts se nepřepisují. |
| `contracts_window.*`, `password_sha_window.*`, `contracts_save_controller.*`, Contracts část tray koordinace a jejich testy | WP05 | WP06 jen fixture/preview adaptér, WP08 oprava regresí | WP05 je jediný vlastník celé saveUnknown a child-generation logiky. |
| `client/tools/dev-tray.ps1`, `client/tests/ui/*`, preview backend a preview CLI | WP06 | WP08 oprava brány | Produkční UI zdroje se jen sdílejí, nekopírují. |
| `client.md`, `tray_UX.md`, nový implementační report a trasovací matice | WP07 | WP08 pouze oprava zjištěného nesouladu | WP00 až WP06 dodají textový delta report, ale tyto dokumenty nezapisují. |
| `client/src/CMakeLists.txt` | WP01 | WP08 | WP01 zaregistruje core modely. Další potřeba jde do integračního seznamu. |
| `client/apps/CMakeLists.txt` | WP02 | WP03 až WP06 jen při přidání vlastních produkčních tray zdrojů, WP08 finální audit | Každý agent zachová předchozí explicitní seznam a ověří produkční target. |
| `client/tests/CMakeLists.txt` | WP01 | WP02 až WP06 pouze pro vlastní nové testy/zdroje, WP08 finální audit | Před editací načíst aktuální diff, neduplikovat zdroje a zachovat opt-in UI target mimo CTest. |
| `protocol.md`, `distribution.md`, `dist/**`, `dist_old_do_not_use/**` | nikdo | nikdo bez nového výslovného rozhodnutí | Pouze read-only audit. |

Centrální soubor `tray_application.*` se úmyslně předává sekvenčně. WP02 vytvoří injekční a lifecycle seam, WP03 připojí existující okna, WP04 změní Wizard Finish a WP05 připojí Contracts načtení, ukládání a ukončení. Každý následující agent musí před zápisem přečíst celý aktuální diff tohoto souboru.

## WP00: Baseline a globální CIDR brána

### Hotový delegační prompt

> Pracuj přímo v uloženém projektu phpvpn. Jde o první implementační balíček `plan3_fix3`, bez commitu, push, instalace, distribuce nebo zápisu do ProgramData.
>
> **Cíl:** vytvořit reprodukovatelný baseline a zavést jedinou globální podporu kanonických IPv4 prefixů `/24` až `/29` ve sdíleném jádře. Odmítnutí `/16` až `/23` a `/30` až `/32` musí platit před Connectem i ve všech publikačních cestách. Neměň rezervaci síťové adresy, první host adresy a broadcastu, nejdelší prefix `/29`, Wizard kandidáty 13/14 ani wire/IPC schema.
>
> **Povinně načti:** celý `plan3_fix3.md`, zejména mapování problému 6, N03, F00, F00a, A04, A11, A22, rizika a zrychlený závěr. Z `client.md` načti Architekturu, Konfigurační soubor, Správu uložené konfigurace, Wizard a jeho testy. Z `protocol.md` ověř, že změna není wire změna. Načti `client/src/core/protocol/ipv4.*`, `client/src/core/config/config_validator.cpp`, config parser/store/document/defaults, Wizard defaults/service a všechny testy používající širší CIDR.
>
> **Rozsah zápisu:** `client/src/core/protocol/ipv4.h`, případně jen nutná korekce `ipv4.cpp`, `client/src/core/config/config_validator.cpp` a existující unit testy či syntetické fixture, které přímo dokazují globální rozsah. Smíš upravit další core call site pouze pokud read-only trasování prokáže, že obchází sdílenou validaci. Každý takový soubor uveď zvlášť v reportu.
>
> **Nesmíš měnit:** `client.md`, `tray_UX.md`, `protocol.md`, `distribution.md`, tray UI, CMake strukturu, IPC příkazy, službový dispatcher, Wizard lifecycle, distribuci ani historické reporty. Pozitivní širší fixture, která testuje jinou vlastnost, převeď na `/24` až `/29` bez oslabení původního účelu. Zachovej samostatné negativní testy všech nepodporovaných prefixů.
>
> **Předpoklady a vstup:** pracovní strom může obsahovat uživatelské změny. Nejprve zaznamenej HEAD, status, diff, toolchain dostupný bez instalace, read-only inventář a hash `dist`. Výchozí auditní revize v plánu je historická, skutečný aktuální HEAD zaznamenej zvlášť.
>
> **Implementace:** nastav `Ipv4Cidr::kMinPrefixLength` na 24, sjednoť bezpečný validační text a ověř všechna místa, která používají `parseAndValidateConfig`, repair analýzu, `ConfigStore`, startup default, `set-config` a Wizard Finish. Nepřidávej lokální tray gate. Ověř overflow před počítáním peerů a zachovej `/29` jako čtyři vzdálené peery pro jednu lokální klientskou adresu z pěti.
>
> **Testy a ověření:** spusť úzké config/IPv4/Wizard testy s prokazatelně nenulovým filtrem podle skutečného harnessu. Ověř `/24`, `/29`, nekanonickou síť, obě rezervované hrany, `/16`, `/23`, `/30`, `/31`, `/32`, full i repair režim, set-config a Finish. Sestav relevantní core test target a službový target bez registrace nebo spuštění služby. Pokud prostředí build neumožní, uveď přesný `NOT RUN`.
>
> **Dokončeno je, když:** existuje jeden sdílený rozsah `/24` až `/29`, žádná publikační nebo Connect cesta jej neobchází, zamýšlené negativní testy procházejí, `git diff --check` je čistý a nebyl změněn dokument ani distribuce.
>
> **Předání:** vrať povinný report z tohoto orchestration dokumentu. Navíc vypiš všechny změněné fixture, seznam call sites globální validace, přesné filtry testů a seznam textových změn, které má WP07 později provést v `client.md` a `tray_UX.md`.

### Brána pro WP01

WP01 smí začít až po ověření, že `Ipv4Cidr::kMinPrefixLength == 24`, validační text je sjednocený, širší prefixy nejsou pozitivními runtime fixture a úzké testy nebo jasně zdůvodněné `NOT RUN` jsou v reportu. Jakýkoli zjištěný bypass validace vrací práci do WP00.

## WP01: Přenosné Contracts jádro

### Hotový delegační prompt

> Pracuj přímo v aktuálním checkoutu projektu phpvpn. Převzal jsi dokončený WP00. Neprováděj commit, push, instalaci ani distribuci.
>
> **Cíl:** implementovat přenosný DOM patch, ContractsModel a PasswordKeyModel bez HWND a bez Windows hlaviček. Tento balíček nevytváří okno ani neposílá IPC. Vytváří stabilní, unit-testované rozhraní, nad kterým může pozdější tray agent bezpečně postavit celý editor.
>
> **Povinně načti:** celý `plan3_fix3.md`, zejména N03, datovou část N04, N09, F02, A03 až A09, A11 a A31. Z `client.md` načti Architekturu, Konfigurační soubor, Správu uložené konfigurace, Wizard Contracts/defaults, limity a bezpečné mazání. Z `tray_UX.md` načti UX10 až UX13 pouze jako spotřebitelský kontrakt modelu. Načti výsledek a report WP00, aktuální diff a zdroje `config_document.*`, `config.*`, `json.*`, `ipv4.*`, `wizard_defaults.*`, `crypto_provider.*`, `secure_buffer.*` a relevantní test harness.
>
> **Rozsah zápisu:** vytvoř `client/src/core/config/contracts_document.*`, `client/src/core/presentation/contracts_model.*` a `client/src/core/presentation/password_key_model.*`, případně malé přenosné pomocné typy v témže adresáři. Přidej samostatné unit testy, například `test_contracts_document.cpp`, `test_contracts_model.cpp` a `test_password_key.cpp`. Zaregistruj je v `client/src/CMakeLists.txt` a `client/tests/CMakeLists.txt`.
>
> **Nesmíš měnit:** tray okna, `main.cpp`, readers, UI harness, `client.md`, `tray_UX.md`, IPC schema, ConfigStore zápis, Wizard lifecycle, síť, service dispatcher ani distribuci. Neduplikuj `defaultPayloadKey`, IPv4 pravidla, JSON parser nebo CryptoProvider. Service a CLI nesmějí kvůli přidání objektů statické knihovny přitáhnout nepoužívaný prezentační kód.
>
> **Předpoklady a artefakty:** WP00 dodal globální `/24` až `/29`. Model musí používat toto sdílené pravidlo, ne vlastní konstantu. Aktuální `ConfigDocument` a JSON DOM zachovávají absent/present rozdíly. `defaultPayloadKey` ve Wizard defaults je jediná autorita pro odvozený klíč.
>
> **Implementační požadavky:** před alokací ověř počet nejvýše 252. Řádky řaď numericky podle `remoteIp`, vynech self a identifikuj je adresou, ne indexem. Zachovej přesný existující payloadKey. Chybějící řádek předvyplň sdíleným defaultem bez automatického enabled. Aktivní key validuj přesně `[0-9a-f]{64}`. Neaktivní nevalidní key neblokuje publikaci. Sleduj dirty, blokující chyby, focus identitu `remoteIp + column`, generaci save a immutable kandidát bez kopie DOM v každém řádku. DOM patch zachová všechny nesouvisející hodnoty, typy a přítomnost členů. Unchecked odstraní kontrakt. Chybějící objekt zůstane chybějící, pokud se nic nepřidává, existující objekt po odstranění všeho zůstane `{}`. Kontroluj 512 KiB dokument, 4 MiB zakódovanou obálku na integračním seam a 512 KiB součet Key textů bez tichého truncování.
>
> Password model počítá synchronní SHA-256 přes přesné UTF-8 bajty nejvýše 255 Unicode skalárních hodnot. Validní surrogate pair počítej jednou, nespárovaný odmítni, nekontrolní combining znak počítej samostatně, nic nenormalizuj. Ověř prázdný hash, 254/255/256 ASCII i non-BMP, vadný UTF-16 a NUL nebo řídicí znak nepodporovaný jedním řádkem. Pracovní buffer bezpečně smaž po použití.
>
> **Testy a ověření:** spusť nové modelové testy a relevantní config/Wizard default testy úzkým nenulovým filtrem. Povinné jsou vektory A03 až A09 a A11 včetně `.2_.3` hashe z plánu, obou směrů páru, přetečení, pořadí `.2/.10`, 252/4 peerů, preserving DOM a limitů. Ověř, že headers modelů neobsahují `windows.h`, HWND ani GDI typy. Sestav core, unit test target, service a CLI bez spuštění.
>
> **Dokončeno je, když:** čisté modely mají úplné unit důkazy, public API je dostatečné pro WP05 bez znalosti HWND, všechny nové zdroje jsou v CMake, `git diff --check` projde a žádný dokument nebo UI soubor se nezměnil.
>
> **Předání:** vrať povinný report. Přilož stručný public API kontrakt, seznam invariancí, které má UI zachovat, přesné vlastnictví citlivých bufferů, výkonové předpoklady a dokumentační deltu pro WP07.

### Brána pro WP02

WP02 ověří, že nové hlavičky modelů jsou platformně čisté a že celý target linkuje. Nevyřešený význam DOM patch, počítání skalárních hodnot nebo výchozího klíče blokuje další práci, protože UI jej nesmí znovu implementovat.

## WP02: Sdílená Win32 infrastruktura a TrayApplication seam

### Hotový delegační prompt

> Pracuj přímo v aktuálním checkoutu phpvpn po dokončených WP00 a WP01. Neprováděj commit, push, instalaci ani distribuci.
>
> **Cíl:** vytvořit lehký sdílený Win32 základ pro DPI, monitor placement, resources, texty, cards, invalidaci a budoucí preview. Vyčleň `TrayApplication` z monolitického vstupu tak, aby produkční `wWinMain` zůstal tenký a testovací host mohl později bezpečně injektovat soukromou pipe, vlastní mutex a shell adaptér. Neměň ještě význam jednotlivých oken.
>
> **Povinně načti:** celý `plan3_fix3.md`, zejména N09/N09a/N09b, F01, architektonickou část F09, A20, A27, A28, A29, A31 a A32. Z `tray_UX.md` načti UX00 až UX03. Z `client.md` načti Architekturu, Win32 UI, Windows tray lifecycle a relevantní testy. Načti reporty WP00/WP01, celý aktuální diff, `main.cpp`, `window_utils.h`, `layout.*`, `wizard_layout.*`, všechna window headers, readers, `apps/CMakeLists.txt`, `tests/CMakeLists.txt` a UI harness.
>
> **Rozsah zápisu:** obecné utility pod `client/apps/tray`, například `ui_style.h`, `ui_resources.*`, `ui_controls.*`, rozšíření `window_utils.*`, obecné `layout.*`, nové `tray_application.*` a ztenčení `main.cpp`. Uprav `client/apps/CMakeLists.txt` a `client/tests/CMakeLists.txt` jen pro registraci těchto sdílených zdrojů a úzkých testů. Přidej čisté geometry/resource/shell-adapter testy podle potřeby.
>
> **Nesmíš měnit:** význam Connect, Wizard Finish, Contracts model nebo Save, editorový NUL model, Statistics counter semantics, `client.md`, `tray_UX.md`, core Contracts model, IPC schema, službu ani distribuci. Nevytvářej vlastní GUI framework, bitmapové pozadí, resource katalog lokalizace nebo novou knihovnu.
>
> **Předpoklady a artefakty:** WP01 poskytuje modely, ale tento balíček je nemusí zapojit. Stávající produkční pipe a mutex musí zůstat výchozími hodnotami pouze tenkého produkčního vstupu. Všechny readers musí být konstruovatelné s explicitním endpointem, aby WP06 nemohl omylem spadnout na produkční službu.
>
> **Implementační požadavky:** implementuj přesné pořadí monitoru rodič, uložený bod menu, foreground, cursor, primární. Ulož bod skutečného zobrazení menu. Použij signed coordinates, cílové DPI, pracovní plochu a klientskou velikost plus rám. Reaktivace nesmí okno přesouvat, pokud je stále použitelné. Po ztrátě monitoru proveď minimální clamp. Cache fontů, ikon, štětců a per je RAII na okno/DPI, ne vznik při paintu. Přidej změnový shell seam, aby shodná ikona a tooltip nevolaly `NIM_MODIFY`. Zachovej přístupnost nativních controls, selection, clipboard řízený uživatelem a fallback po selhání `DeferWindowPos`. Připrav textový katalog pouze pro anglické prezentační konstanty, bez lokalizačního ABI.
>
> `TrayApplication` musí přijmout explicitní runtime závislosti potřebné později pro fixture. Produkční `wWinMain` předá stávající pipe a mutex. Žádný testovací parser nebo fake backend se nesmí linkovat do produkčního EXE. Nepřepisuj celý lifecycle, pouze vytvoř čistý seam a zachovej stávající chování.
>
> **Testy a ověření:** geometry unit testy zahrnou záporné souřadnice, rodiče, bod menu, fallbacky, reaktivaci, unplug, rozlišení/DPI a minimální clamp. Shell adapter test zahrne shodný a změněný status. Resource test ověří opakovaný create/destroy bez double-free. Sestav `phpvpn-tray`, unit test target a opt-in UI target, pokud je dostupný. Nespouštěj celé CTest.
>
> **Dokončeno je, když:** produkční chování je zachované, `main.cpp` je tenký vstup, explicitní endpoint nemá skrytý fallback, sdílený UI základ je použitelný dalšími okny, úzké testy procházejí, `git diff --check` je čistý a dokumenty se nezměnily.
>
> **Předání:** vrať povinný report. Navíc popiš konstruktor a ownership `TrayApplication`, všechny injekční body, resource lifecycle, společné layout API a přesné soubory, které další balíčky smějí rozšířit bez změny kontraktu.

### Brána pro WP03

WP03 před prací ověří, že produkční tray lze sestavit po extrakci, všichni readers dostávají endpoint z jedné explicitní závislosti a shell/resource testy nevyžadují skutečnou službu.

## WP03: Connect, Edit Config a Statistics

### Hotový delegační prompt

> Pracuj přímo v aktuálním checkoutu phpvpn po WP00 až WP02. Neprováděj commit, push, instalaci ani distribuci.
>
> **Cíl:** dokončit tři technicky související existující obrazovky nad sdíleným UI základem: Connect progress včetně jednorázových detailů a odpočtu, Edit Config bez ztráty stávajícího dokumentového modelu a Statistics z jediného status snímku.
>
> **Povinně načti:** celý `plan3_fix3.md`, zejména N01, N05, N08, F03, relevantní části F07, N09b, A14 až A17, A20 až A22, A27 až A29. Z `tray_UX.md` načti UX03 až UX07 a UX13/14. Z `client.md` načti Správu konfigurace, Edit Config, Windows tray, progress, statistics a testy 20 až 35. Načti report WP02, celý diff sdíleného UI a `TrayApplication`, `status_reader.*`, `status_model.*`, `progress_model.*`, `progress_window.*`, `editor_document.h`, `editor_window.*`, `statistics_window.*`, `poll_schedule.h` a jejich testy.
>
> **Rozsah zápisu:** uvedené progress, editor a statistics soubory, jejich specializované layout/model testy, nezbytné připojení v `tray_application.*` a vlastní záznamy v apps/tests CMake. Smíš přidat malý čistý model pro Connect details nebo safe error mapu, pokud zůstane bez HWND.
>
> **Nesmíš měnit:** Wizard okno/model/lifecycle, Contracts okno/model/Save, globální CIDR, IPC schema/parser/dispatcher, službový status engine, counter semantics, síť, `client.md`, `tray_UX.md` ani distribuci. `get-status` nesmí dostat URL/IP. Nevytvářej nový timer Statistics ani nový progress příkaz.
>
> **Předpoklady a artefakty:** WP02 dodal endpoint injection, monitor helper, resources a layout primitives. Status polling tray zůstává 5 s. Progress má vlastní 500 ms reader s nejvýše jedním požadavkem. Statistics dostává poslední validní `TrayStatus` a oddělený freshness stav.
>
> **Implementační požadavky:** při otevření Connect jednou zavolej existující `get-config`, z validního JSON vyzvedni pouze `serverUrl` a `virtualIp`, celý config neloguj a omez životnost kopií. Stav a tři checkpointy vždy ber z jednoho nezměněného `get-status`. Přijmi rozdíl uloženého a aktivního snímku. Úspěch až při autoritativním connected/running/tunnel-ready, terminální výsledek zmraz. Od prvního skutečného zobrazení počítej monotónní 3/2/1 přes `ceil`, zavři 3000 až 3500 ms, nikdy neodesílej Disconnect. Implementuj jedinou safe mapu šesti kategorií a IPC nedostupnosti bez raw error.
>
> Edit Config uprav jen prezentačně. Zachovej skutečný dokument, raw NUL režim, oba scrollbary bez wrap, Reset generation, dirty a asynchronní Save. Show Info má samostatný prezentační režim bez config title/path/actions. Statistics rozlož devět přesných `uint64` do 3/2/4 karet, bez double konverze a automatických jednotek. Po výpadku ukaž stale, nenuluj hodnoty a nevytvářej vlastní poll.
>
> **Testy a ověření:** rozšiř `test_tray_progress.cpp`, `test_tray_layout.cpp`, editorové testy a vhodné status testy. Ověř config call count, lifetime/logging audit, snapshot mismatch, šest chyb, celý odpočet s fake monotonic clock, nezávislost timerů, close cleanup, hodnoty nad `2^53`, stale a editor resize/NUL/Reset. Proveď úzký UI smoke syntetickými daty pro relevantní okna a DPI, pokud harness dovoluje. Sestav tray a cílené testy.
>
> **Dokončeno je, když:** A14 až A17 a relevantní A20/A21/A22 mají konkrétní důkaz, neexistuje nový IPC nebo timer, editor neztrácí data, Statistics neztrácí přesnost, `git diff --check` projde a nebyly změněny Wizard/Contracts dokumenty ani dist.
>
> **Předání:** vrať povinný report. Uveď public modely, safe error mapu, přesný okamžik success deadline, všechny config/status call counts a dokumentační deltu N01/N05/N08 pro WP07.

### Brána pro WP04

WP04 ověří, že `TrayApplication` stále sestavuje, endpoint injection zůstala úplná a Connect neobsahuje nový IPC. Regrese editorového lifecycle nebo základního pollingového plánu blokuje pokračování.

## WP04: Wizard UX a Finish

### Hotový delegační prompt

> Pracuj přímo v aktuálním checkoutu phpvpn po WP00 až WP03. Neprováděj commit, push, instalaci ani distribuci.
>
> **Cíl:** upravit stávající Wizard na cílový UX a změnit potvrzený Finish tak, aby pouze pravdivě oznámil uložení, zavřel vzdálenou Wizard relaci a zavřel okno. Contracts se automaticky neotevře, nepředává se dokument a nevzniká handoff.
>
> **Povinně načti:** celý `plan3_fix3.md`, zejména N06, N07, relevantní N04a, F04, Wizard část F07, A12, A13, A20 až A22, A29/A30. Z `tray_UX.md` načti UX03, UX04, UX08, UX09 a UX13/14. Z `client.md` načti Správu konfigurace, celý Configuration Wizard, Wizard IPC, Win32 UI a příslušné testy. Načti reporty WP00 až WP03, aktuální `tray_application.*`, `wizard_window.*`, `wizard_layout.*`, `wizard_reader.*`, `core/wizard/*`, `core/ipc/wizard_ipc.*` a všechny Wizard unit/UI testy.
>
> **Rozsah zápisu:** `wizard_window.*`, `wizard_layout.*`, čisté Wizard prezentační modely/texty, nutné safe action texty v `core/base/error.*` nebo `core/ipc/wizard_ipc.*`, cílené Wizard testy a úzké připojení v `tray_application.*`. CMake měň jen při přidání vlastního zdroje/testu.
>
> **Nesmíš měnit:** lease 15/60 s, max osm relací, attemptSequence, epochy, receipts, síťové důkazy, žádný auto retry, globální Disconnect semantics, Contracts model/window/Save, Connect/Editor/Statistics, IPC seznam nebo schema, službový dispatcher, `client.md`, `tray_UX.md` a distribuci. Neodstraň varování o trvalé spotřebě čekajících zpráv ani informace o kolizích IP.
>
> **Předpoklady a artefakty:** WP00 již globálně odmítá nepodporované CIDR. WP02 dodal sdílený layout/resources a WP03 stabilní `TrayApplication`. Pozdější ruční Contracts akci implementuje WP05, proto Finish pouze skončí a nesmí volat placeholder.
>
> **Implementační požadavky:** Server URL má viditelný disabled Back, odpojovací gate Back nemá. Síťové akce zobrazí `Verify & continue`, gate `Disconnect & continue`. Explicitní gate tlačítko je jediný souhlas, odstraň druhé duplicitní Yes/No. Gate čeká na globální disconnected/idle. Sidebar má čtyři nepřeskakovatelné kroky, patička Reset vlevo a Cancel/Back/primární akce vpravo. Switche přesuň pod pole a zachovej metadata precedence. U identity ponech celé varování o spotřebě zpráv viditelné, ne v tooltipu. Contracts summary Wizardu zůstává 13/14 aditivních defaultů, ne plný editor 252 řádků. Confirmed Finish nesmí otevřít Contracts ani Connect. Lost reply se dál dohledává jediným `wizard-get-commit`, bez druhého Finish.
>
> **Testy a ověření:** uprav unit/UI testy pro nové texty, Back/gate, metadata, layout, focus, Cancel/Reset, lost reply a confirmed Finish call counts. Výslovně dokaž 0 automatických Contracts oken, 0 dalšího config read, 1 Finish a 1 wizard-close. Ověř žádný nový IPC příkaz a zachování všech existujících lifecycle testů úzkými nenulovými filtry. Proveď syntetický UI smoke relevantních kroků, pokud harness dovoluje.
>
> **Dokončeno je, když:** A12/A13 a relevantní A20/A21/A22 jsou prokazatelné, potvrzený Finish končí bez handoffu, bezpečnostní copy zůstala, úzké testy procházejí, `git diff --check` je čistý a Contracts implementace ani dokumenty se nezměnily.
>
> **Předání:** vrať povinný report. Uveď přesnou Finish sekvenci, počty IPC/callbacků, zachované Wizard invariance, změněné safe texty a dokumentační deltu N06/N07/N08 pro WP07.

### Brána pro WP05

WP05 smí začít až po testu, že Wizard Finish neotevírá Contracts a nečte dokument. Pokud stále existuje callback `Finish -> ContractsWindow::show`, vrací se práce WP04.

## WP05: Contracts end-to-end, Save, souběh a Exit/logoff

### Hotový delegační prompt

> Pracuj přímo v aktuálním checkoutu phpvpn po WP00 až WP04. Neprováděj commit, push, instalaci ani distribuci.
>
> **Cíl:** implementovat jeden soudržný end-to-end celek Communication contracts včetně ručního načtení, plného nativního editoru, Random, více nemodálních Password SHA oken, DOM kandidáta, asynchronního set-config, saveUnknown, souběhu a společného Exit/logoff. Tento balíček vlastní celý Contracts lifecycle, aby se generace a citlivé buffery nerozdělily mezi různé agenty.
>
> **Povinně načti:** celý `plan3_fix3.md`, zejména N02, N03, celý N04/N04a, F05, F06, A01 až A11, A18 až A20, A22, A26, A29/A30. Z `tray_UX.md` načti UX03/04 a UX10 až UX14. Z `client.md` načti Správu uložené konfigurace, Wizard příkazy, Win32 UI, bezpečnost, lifecycle a testy. Načti reporty WP01, WP02 a WP04, aktuální Contracts model API, `tray_application.*`, readers, `contracts_window.*`, `window_utils.*`, editor save flow, IPC protocol/parser a UI harness.
>
> **Rozsah zápisu:** přepiš `contracts_window.*`, přidej `password_sha_window.*`, `contracts_save_controller.*` a malé Contracts-specific adaptéry/layouty. Zapoj ruční Contracts a společný Exit/logoff v `tray_application.*`. Uprav `menu_model.h` jen kvůli odstranění nesprávného `requiresConfigState` kontraktu. Doplň vlastní unit/UI testy a CMake registrace. Core model z WP01 měň jen při testem prokázané chybě a každou takovou změnu vysvětli.
>
> **Nesmíš měnit:** Wizard Finish nebo lifecycle, Connect/Editor/Statistics semantics, globální CIDR pravidlo, IPC seznam a uzavřená schémata, service dispatcher, ConfigStore filesystem cestu, wire protokol, `client.md`, `tray_UX.md`, distribuci ani reálnou službu. Nevytvářej `wizard-get-commit` pro Contracts a nepoužívej revisionToken jako CAS.
>
> **Předpoklady a artefakty:** WP01 poskytuje DOM/model/password logiku, WP02 UI resources a monitor helper, WP04 odstranil automatické otevření po Finish. `get-config-document` a `set-config` jsou existující příkazy s přesnými schématy a deadline 5000/30000 ms uvedenými v plánu.
>
> **Implementační požadavky:** menu samo nečte config. Explicitní Contracts akce pošle právě jeden `get-config-document` bez předchozího `get-config-state`. Availability matici implementuj přesně podle N02. Pouze valid otevře model. Opakované otevření aktivuje tutéž instanci bez čtení a resetu dirty.
>
> Vytvoř všech nejvýše 252 skutečných nativních controls bez virtualizace a recyklace. Header/footer zůstanou pevné, tabulka scrolluje, tab order je po řádcích a odkrývá focus. Identita akcí je `remoteIp + generation`, ne index nebo HWND. Random potvrzení je dočasně modální jen vůči rodiči a výchozí No. Password SHA je nemodální, dovolí více oken i pro stejný řádek, poslední potvrzené OK vyhrává. Hlavní OK zmrazí kandidáta, zavře vlastní děti bez aplikace jejich dalších změn a zakáže jen ukládané okno.
>
> `ContractsSaveController` použije existující `set-config` se čtyřmi členy a striktní korelací. IPC čekání je vždy asynchronní. Confirmed success zavře jen daný editor. Prokázané neodeslání vrátí editing, možná publikace nebo chybná/pozdní reply přejde do saveUnknown. `Check current configuration` provede jediné explicitní `get-config-document` a porovná význam celého DOM včetně typů a absent/present. Shoda je stavová evidence, ne receipt. Neshoda zachová draft. `Save draft again` je nová potvrzená generace, nikdy automatický retry.
>
> Více editorů se neslučuje ani nerebaseuje, poslední potvrzený save vyhrává. Ruční Exit v clean zavře, při dirty/unknown se ptá jednou společně, No zruší Exit. Během saving čeká pouze do původního deadline a potom použije unknown potvrzení. Logoff/`WM_ENDSESSION` neblokuje ani nezobrazuje UI a neprodlužuje deadline. Zavření tray nesmí měnit config, službu nebo VPN. Vyřaď pozdní generace a best-effort smaž vlastněné citlivé buffery a EDIT undo.
>
> **Testy a ověření:** unit testy pokryjí A01 až A11, A18 a A30. UI/accessibility smoke pokryje `/24` i `/29`, 252 controls, tab/shift-tab, scroll, resize, DPI, focus, multiple Password, Random No/Yes/failure, hlavní OK s neaplikovaným dítětem, create failure, late generation, two editors, unknown workflow, Exit a logoff. Fake writer/reader změří call counts a deadline. Audituj 512 KiB, 4 MiB a +1200 GDI/USER hranici. Sestav tray a cílené testy, nespouštěj reálnou službu.
>
> **Dokončeno je, když:** Contracts funguje pouze z explicitní ruční akce, celý lifecycle má jednotné generace, žádná větev neblokuje UI čekáním na IPC, akceptace mají konkrétní důkazy, žádný nový IPC příkaz nevznikl, `git diff --check` projde a dist/ProgramData zůstaly beze změny.
>
> **Předání:** vrať povinný report. Přilož stavový automat editoru, seznam generací a jejich invalidačních bodů, přesná request/response schema a deadline, call counts, accessibility/object výsledky, bezpečnostní cleanup omezení a dokumentační deltu N02/N03/N04/N04a pro WP07.

### Brána pro WP06

WP06 ověří unit důkazy všech Contracts stavů, zejména saveUnknown a více dětí, a že každý reader používá injektovanou pipe. Jakýkoli produkční fallback z fixture endpointu blokuje preview práci.

## WP06: Izolovaný vývojový runner a UI harness

### Hotový delegační prompt

> Pracuj přímo v aktuálním checkoutu phpvpn po WP00 až WP05. Neprováděj commit, push, instalaci ani distribuci.
>
> **Cíl:** implementovat F09 jako jeden bezpečný příkaz `client/tools/dev-tray.ps1` a rozšířit existující UI harness tak, aby otevíral všechna produkční okna nad izolovanou syntetickou fixture. Produkční EXE nesmí obsahovat preview parser ani fake backend.
>
> **Povinně načti:** celý `plan3_fix3.md`, zejména celý F09, N09b, A20, A24 až A27 a A32, zrychlený závěr a rizika. Načti `client/CMakePresets.json`, `client/tools/build-x64.ps1`, `run-tray-ui-tests.ps1`, `client/tests/ui/tray_ui_tests.cpp`, jeho README, všechny CMake soubory, aktuální `tray_application.*`, readers a reporty WP02 až WP05. Z `distribution.md` načti pouze hranice zakazující záměnu preview za vydání.
>
> **Rozsah zápisu:** nový `client/tools/dev-tray.ps1`, `client/tests/ui/tray_ui_tests.cpp`, případné malé nové soubory pod `client/tests/ui/`, `client/tests/ui/README.md`, vhodný odkaz z existující klientské vývojové dokumentace a nutné záznamy v `client/tests/CMakeLists.txt`. Produkční `tray_application.*` a malé seams oken smíš měnit jen pokud je to nutné pro explicitní injection nebo otevření skutečného pohledu, bez změny jejich semantics.
>
> **Nesmíš měnit:** produkční `wWinMain` CLI, IPC schema, službu, ConfigStore, síť, ProgramData, instalaci, release preset význam, dist, `protocol.md`, `distribution.md`, `client.md` a `tray_UX.md`. Nevytvářej druhý frontend, kopii UX konstant, fallback na produkční pipe, hot reload, watcher, nový build systém nebo admin workflow.
>
> **Předpoklady a artefakty:** WP02 poskytuje explicitní pipe/mutex/shell injection a WP03 až WP05 hotová produkční okna. UI target je opt-in a zůstává mimo běžný CTest. Stávající `--interactive`, staré `--preview` a substring testy musí zůstat kompatibilní.
>
> **Implementační požadavky:** implementuj přesné parametry, validaci a adresáře z F09. Debug a skutečný Release mají oddělené Ninja cache. Runner funguje z libovolné cwd, bezpečně načte MSVC AMD64 prostředí, ověří CMake >= 3.28, Ninja, cl a PE 0x8664. Sestavuje pouze `phpvpn-tray-ui-tests`, při chybě nikdy nespustí staré EXE, nic nemaže a nepoužívá taskkill. Log ukládá bez tajemství do jedinečného `work/logs/tray-dev` adresáře.
>
> Implementuj `--dev-preview <View> --scene <Scene>` přesně pro tabulku F09. Všechny pohledy používají skutečné produkční třídy a skutečné aplikační akce. Každé spuštění má novou soukromou pipe a mutex. Fixture je pouze in-memory, bez filesystemu, HTTP, SCM, Wintun, rout nebo produkční pipe fallbacku. View Tray má odlišný tooltip. Preview trvá do zavření, kromě skutečného Connect success odpočtu. Hidden launcher nesmí skrýt GUI. Po konci nesmí zůstat proces, pipe, reader ani timer.
>
> **Testy a ověření:** proveď všech sedm bodů F09 `Ověření a předání vývojové cesty`. Povinně dokaž první build, no-op iteraci, změnu jednoho GUI zdroje bez service/CLI, compile failure bez starého preview, validní a nevalidní View/Scene, neprázdný i nulový test filter, jinou cwd, cestu s mezerami, preview delší než 45 s, vlastní mutex/tooltip a nulový přístup k reálné pipe. Sestav produkční targety a zkontroluj, že neobsahují fixture nebo `--dev-preview`. Nevytvářej dist.
>
> **Dokončeno je, když:** A32 je doložen konkrétními příkazy a časy, všechny View/Scene se otevřou nebo mají pravdivé `NOT RUN`, izolace je fail-closed, produkční target je čistý, README popisuje pouze skutečně ověřené příkazy, `git diff --check` projde a dist/ProgramData/služba jsou beze změny.
>
> **Předání:** vrať povinný report. Uveď tabulku View/Scene výsledků, build adresáře, první/no-op/incremental časy a Ninja kroky, negativní scénáře, důkaz endpoint/mutex izolace, produkční linkage audit a dokumentační deltu F09/A32 pro WP07.

### Brána pro WP07

WP07 smí začít až po potvrzení, že runner a README popisují skutečně existující příkazy a že produkční EXE neobsahuje preview cestu. Neověřené scény se označí jako `NOT RUN`, ne jako hotové.

## WP07: Normativní dokumentace a trasovatelnost

### Hotový delegační prompt

> Pracuj přímo v aktuálním checkoutu phpvpn po WP00 až WP06. Jde o jediný balíček s primárním vlastnictvím `client.md` a `tray_UX.md`. Neprováděj commit, push ani distribuci.
>
> **Cíl:** synchronizovat normativní dokumentaci se skutečně implementovaným řešením bez změny schváleného významu N01 až N09. Odstraň staré protichůdné věty, nevkládej nové pravidlo vedle opačného textu a nevydávej historické testy za nově provedené.
>
> **Povinně načti:** celý `plan3_fix3.md`, zejména tabulku povinných aktualizací, celé N01 až N09, A01 až A32, F08/F09 a zrychlený závěr. Načti celý relevantní diff kódu a předávací reporty WP00 až WP06. V `client.md` načti všechny sekce citované tabulkou plánu, v `tray_UX.md` celý UX00 až UX14. `protocol.md` a `distribution.md` načti read-only pro kontrolu hranic.
>
> **Rozsah zápisu:** `client.md`, `tray_UX.md` a nový textový implementační report/matice pod `client/tests/` s jednoznačným novým názvem, pokud jej aktuální zadání dovolí. Existující historické reporty neměň. Smíš opravit `client/tests/ui/README.md` pouze při zjištěném dokumentačním rozporu se skutečným WP06, ne k přidání neověřeného příkazu.
>
> **Nesmíš měnit:** C++/PowerShell/CMake kód, unit/UI testy, `protocol.md`, `distribution.md`, historické reporty, assets, dist ani ProgramData. Neprováděj implementační opravu. Zjištěnou chybu vrať vlastníkovi nebo označ jako blokátor WP08.
>
> **Předpoklady a artefakty:** technický význam určuje `plan3_fix3.md`, ale dokumentace musí odpovídat skutečnému kódu. Reporty předchozích balíčků obsahují textové delty a skutečné testy. `NOT RUN` zůstává `NOT RUN`.
>
> **Implementační požadavky:** proveď přesné náhrady uvedené v tabulce plánu pro UX00, UX02 až UX14 a `client.md`. Odstraň aktivní tvrzení o placeholderu, modalitě celého Contracts, zákazu souběhu, 4092 řádcích, virtualizaci/recyklaci, `/16` až `/23`, 524288 bajtech hesla, zákazu odpočtu, neurčitém aktivním monitoru, automatickém Contracts po Finish, handoffu a kopii tray do dist. Zachovej všech 12 schválených rozhodnutí, Wizard lifecycle, wire invariance a distribuční hranice. Implementační report mapuje každý A01 až A32 na PASS/FAIL/NOT RUN s konkrétním důkazem.
>
> **Testy a ověření:** proveď cílené `rg` pro všechny staré konfliktní formulace i nové závazné termíny. Porovnej IPC seznam před/po, diff dokumentů s kódem a všech 12 řádků mapování problému. Ověř, že `protocol.md` a `distribution.md` nejsou změněné. Spusť `git diff --check`.
>
> **Dokončeno je, když:** v aktivní normativní části nezůstal žádný starý konflikt, všech 12 rozhodnutí má nezměněný význam, A01 až A32 mají pravdivý stav, historické reporty jsou nedotčené a rozsah změn je pouze dokumentační.
>
> **Předání:** vrať povinný report. Přilož tabulku odstraněných konfliktů, seznam normativních náhrad, A01 až A32 stav, `rg` příkazy a případné implementační nesoulady, které musí WP08 vrátit původnímu vlastníkovi.

### Brána pro WP08

WP08 ověří nulový výskyt aktivních protichůdných pravidel. Pokud dokumentace popisuje neimplementované chování nebo kód odporuje schválenému rozhodnutí, brána se neobchází úpravou evidence na PASS.

## WP08: Finální integrační a regresní brána

### Hotový delegační prompt

> Pracuj přímo v aktuálním checkoutu phpvpn po WP00 až WP07. Jde o finální integrační audit, ne o novou feature fázi. Neprováděj commit, push, instalaci, distribuci ani zápis do ProgramData.
>
> **Cíl:** ověřit konzistenci celého řešení, přesné závislosti, produkční linkage, dokumentaci, bezpečnost, cílené testy, runtime/visual evidence a nulovou změnu distribuce. Brána má najít chyby, ne pouze znovu deklarovat výsledek jednotlivých agentů.
>
> **Povinně načti:** celý `plan3_fix3.md`, celý tento orchestration dokument, všechny předávací reporty WP00 až WP07, konečný `git diff`, `client.md`, `tray_UX.md`, relevantní části `protocol.md` a `distribution.md`, všechny změněné CMake/build soubory a produkční/testovací linkage.
>
> **Rozsah zápisu:** výchozí režim je read-only plus finální integrační report/matice. Triviální lokální opravu můžeš provést pouze v jasném vlastnictví, pokud nemění kontrakt a lze ji plně ověřit. Významovou chybu vrať konkrétnímu balíčku. Neřeš ji širokým refaktorem. Po každém vrácení musí být tato brána spuštěna znovu od kontroly diffu.
>
> **Nesmíš měnit:** schválená rozhodnutí 1 až 12, wire/IPC schema, službový lifecycle mimo F00a, distribuci, podpisy, reálnou instalaci nebo stav VPN. Nevydávej fake backend za důkaz reálné služby. Nespouštěj plné integrační, síťové, lifecycle, install/uninstall, multiuser nebo distribuční testy bez nového výslovného pokynu.
>
> **Předpoklady a artefakty:** WP00 až WP07 jsou označeny jako dokončené a jejich předávací reporty, konkrétní příkazy, výsledky, seznamy `NOT RUN` a konečný diff jsou dostupné. Nepřebírej jejich tvrzení bez kontroly. Pokud chybí report, synchronizační brána nebo vysvětlení změněného souboru, zastav před zápisem a vrať úkol koordinátorovi.
>
> **Integrační kontroly:**
>
> 1. zaznamenej HEAD, celý status a diff. Porovnej změněné soubory s vlastnictvím balíčků a vysvětli každou odchylku,
> 2. spusť `git diff --check` a audit konflikt markers, zakázaných nových IPC názvů, tajemství v log call sites a produkčního `--dev-preview`,
> 3. ověř globální `/24` až `/29` přes config, repair, set-config, Wizard Finish a Connect preflight,
> 4. ověř core modely bez Windows typů, DOM preserving a nepřitažení nepoužitých modelových objektů do service/CLI,
> 5. ověř endpoint injection všech readers a nulový fallback fixture na produkční pipe,
> 6. ověř sekvenci Wizard Finish, ruční jediné Contracts read, saveUnknown, multiple editor/Password generace a Exit/logoff,
> 7. ověř nezávislý 5 s tray poll a 500 ms progress poll, žádný Statistics poll a změnové `NIM_MODIFY`,
> 8. spusť úzké nenulové testovací filtry pro každý změněný subsystém a negativní nulový filtr runneru,
> 9. sestav release preset targety `phpvpn-tray`, `phpvpn-service`, `phpvpn-cli`, `phpvpn-client-tests` bez spuštění služby a oddělený skutečný Release preview/build podle F09,
> 10. zkontroluj PE AMD64, produkční absenci fixture a dostupné velikostní, výkonové, GDI/USER a vizuální důkazy podle N09b. Chybějící prostředí označ `NOT RUN`,
> 11. porovnej read-only inventář a hash dist před/po. Musí být nulová změna,
> 12. proveď textový audit N01 až N09, UX00 až UX14, A01 až A32 a všech 12 rozhodnutí proti kódu.
>
> **Testovací politika:** použij povinný zrychlený závěr `plan3_fix3.md`. Nespouštěj celé `ctest -R phpvpn-client-tests`, pokud by spustilo agregovanou sadu. Každý filtr musí mít nenulový počet. Výkonnost měř po warmupu nejméně třikrát a hodnotí se nejhorší běh. Fake výsledky označ jako proxy. Produkční baseline musí mít stejnou konfiguraci, architekturu a podpisový stav.
>
> **Dokončeno je, když:** všechny dostupné cílené brány jsou PASS, každý neprovedený důkaz je explicitní `NOT RUN`, neexistuje FAIL nebo nevysvětlený diff, dokumentace odpovídá kódu, dist je beze změny a finální report výslovně uvádí přeskočené plné testy a zbytková rizika.
>
> **Předání:** vrať finální report se souhrnem změněných modulů, příkazy a výsledky, A01 až A32, runtime/visual/PE metrikami, nulovou změnou dist, seznamem `NOT RUN`, zbytkovými riziky a jednoznačným výsledkem `READY FOR USER REVIEW` nebo `NOT READY` s konkrétními vlastníky oprav.

## Synchronizační body a pravidla návratu

| Bod | Musí být hotovo | Spotřebitel | Při selhání |
|---|---|---|---|
| S0 | Baseline, aktuální HEAD, rozlišení cizích změn, globální `/24` až `/29` | WP01 až WP08 | Vrátit WP00, další modely se nestaví nad starým rozsahem. |
| S1 | Platformně čistý Contracts/Password model a DOM patch | WP02, hlavně WP05 | Vrátit WP01, UI nesmí duplikovat business logiku. |
| S2 | Sdílené monitor/resource/layout API a explicitní app dependencies | WP03 až WP06 | Vrátit WP02, feature agent nesmí vytvořit druhou infrastrukturu. |
| S3 | Connect/Edit/Statistics bez nového IPC a timeru | WP04 až WP08 | Vrátit WP03, centrální tray se nepřebírá v rozbitém stavu. |
| S4 | Wizard Finish bez Contracts/handoff, zachované receipts | WP05 až WP08 | Vrátit WP04, ruční Contracts nelze integrovat vedle starého callbacku. |
| S5 | Contracts kompletní lifecycle a bezpečný Exit/logoff | WP06 až WP08 | Vrátit WP05, harness nesmí maskovat nehotové produkční chování. |
| S6 | Izolovaný runner, všechna View/Scene, produkční target bez fixture | WP07/WP08 | Vrátit WP06, dokumentace nesmí slibovat neexistující workflow. |
| S7 | Normativní dokumenty sjednocené s kódem | WP08 | Vrátit WP07 nebo konkrétní feature balíček podle příčiny. |
| S8 | Cílené build/test/runtime/visual/textové brány a nulová změna dist | uživatel | Výsledek `NOT READY`, žádné zakrytí FAIL jako zbytkového rizika. |

Oprava vrácená dřívějšímu balíčku musí dostat nový čistý agentní kontext, aktuální diff a přesný failing důkaz. Po opravě se znovu provede jeho dokončovací brána a všechny následné synchronizační body, které mohl ovlivnit.

## Kontrolní seznam koordinátora

Před spuštěním balíčku:

- vlož celý odpovídající delegační prompt,
- přilož předávací report bez jeho interpretačního přepisování,
- uveď aktuální seznam změněných souborů a známé `NOT RUN`,
- potvrď, že agent pracuje přímo v uloženém projektu a ne ve worktree, pokud uživatel neurčil jinak,
- nezadávej paralelní zápis do stejného checkoutu.

Po dokončení balíčku:

- porovnej report se skutečným `git status --short` a `git diff --name-only`,
- zkontroluj `git diff --check`,
- ověř předepsanou synchronizační bránu,
- předej přesná rozhraní, výsledky a `NOT RUN` následujícímu agentovi,
- při FAIL vrať práci vlastníkovi, nepokračuj pouze na základě ústního ujištění.

Konečný stav je vhodný k uživatelské kontrole až po WP08. Dokončení WP06 není release, WP07 není důkaz runtime kvality a izolovaný preview není důkaz skutečné služby, VPN, instalace nebo distribuce.
