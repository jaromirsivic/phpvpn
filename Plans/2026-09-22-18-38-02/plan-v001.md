---
plan_version: 1
created_at: 2026-09-22-19-02-43
timezone: Europe/Berlin
source: ../../Intents/2026-09-22-18-38-02/intent-v001.md
---

# Realizační plán: HTTP headers v průvodci a prázdné kontrakty Edit Config

## 1. Autorita, rozsah a výchozí stav

Autoritativní zadání je [intent-v001.md](../../Intents/2026-09-22-18-38-02/intent-v001.md). Tento úplný plán konkretizuje realizaci, nemění intent. Veškeré nové texty aplikace budou anglicky. Tato plánovací etapa vytváří pouze tento soubor. Realizace vyžaduje samostatnou výslovnou volbu uživatele prostřednictvím koordinátora.

Výchozí repozitář: `C:\git\phpvpn`, aktivní větev `tray_UX`, commit `89e6a12d488799a7a64d1f1cb4c83738d2022b1b`. Při průzkumu existovaly změny `.intent-builder-active` a nové soubory aktuálního intentu. Nejsou součástí implementačního diffu a nesmějí se přepsat. Práce bude přímo na aktivní feature větvi bez worktree. Plánovací deadline je 2026-09-22 21:08:02 Europe/Berlin. Byl potvrzen a průběžně kontrolován, nezávislé automatické vynucování není zajištěno.

Výsledné chování:

1. V prvním kroku průvodce přibude sekce a vlastněný modální editor HTTP hlaviček.
2. Reset a editace v dialogu mění pouze jeho lokální text. OK validuje a aktualizuje pouze draft průvodce.
3. Průvodce používá potvrzené hlavičky při svých HTTP kontrolách a uloží je až při Finish.
4. Edit Config Reset a ostatní odběratelé stejné výchozí šablony dostanou prázdný objekt `communicationContracts`.
5. Samostatný generátor kontraktů průvodce, jeho pravidla, klíče a parametr `createDefaultContracts` zůstanou funkčně stejné.

Použití upravených hlaviček v kontrolních požadavcích a vazba výsledků kontrol na hlavičky jsou technická konkretizace intentu. Bez nich by konfigurace pro hosting vyžadující Cookie mohla zůstat neověřitelná a Finish by mohl uložit jiné HTTP nastavení, než bylo prověřeno.

## 2. Ověřené opěrné body a dotčené oblasti

Kontext byl získán nejprve pomocí `graft map`, potom `graft ask --source`, `graft grep`, `graft callers` a `graft skeleton`. Rozsahy níže odpovídají výchozímu commitu a po realizaci se mohou posunout.

| Oblast | Ověřený stav a zásah |
|---|---|
| `client/apps/tray/wizard_window.cpp:484-723` | `render()` vytváří kartu Server URL s jedním separátorem. Vložit sekci a druhý separátor, připojit otevření dialogu a zohlednit jeho modalitu při překreslení/fokusu. |
| `client/apps/tray/wizard_window.cpp:136-209`, `819-870` | Sestavení požadavků, Finish, klávesnice a příkazy. Propagovat draft hlaviček a přidat bezpečnou obsluhu dialogu. |
| `client/apps/tray/wizard_window.cpp:63-98`, `399-416` | Lifecycle, dirty generation a diagnostický detail. Potvrzená změna musí označit draft jako změněný, ukončení vlastníka musí bezpečně ukončit dítě. |
| `client/src/core/config/config_document.h:15-30` | `WizardFields` zatím nemá hlavičky, `WizardContext` je má. Přidat editovatelné pole, zachovat kontext původního dokumentu jako snapshot. |
| `client/src/core/config/config_document.cpp:48-66`, `86-133` | Analýza načte hlavičky do kontextu, builder je dosud nepřepisuje. Inicializovat i draft a finálně dosadit jeho hodnotu do `masterSetup.httpHeaders`. |
| `client/src/core/wizard/wizard_model.cpp:10-44`, `97-140` | Načítání, zachování fields, invalidace a kontroly. Přidat setter s validací a invalidací od prvního checkpointu. |
| `client/src/core/ipc/wizard_ipc.cpp:113-128`, `181-380` | Uzavřené schéma požadavků a fields/view. Rozšířit přenos hlaviček pro všechny kontroly i Finish, odmítat nekompatibilní vstup. |
| `client/src/core/wizard/wizard_service.h:30-50`, `.cpp:312-539` | Operace zachycují fields, ale URL evidence obsahuje jen URL. Rozšířit evidence a předávat snapshot nových hlaviček do probe, zahrnout je do vstupu commitu. |
| `client/src/core/wizard/probe_client.cpp:11-33`, `105-145` | Transport nyní dostává `context.httpHeaders`, včetně access kontroly. Použít hlavičky konkrétní operace, ne původní kontext. |
| `client/src/core/protocol/http_headers.cpp:39-106` | Hotový bezpečný popis chyby a jediný parser/normalizátor. Opětovně použít. |
| `client/src/core/config/config.h:37-62`, `config_defaults.cpp:4-23` | Centrální hlavičky, úplná editorová šablona se vzorovým peerem a samostatný nový wizard dokument. Odstranit pouze vzorový kontrakt z úplné šablony. |
| `client/src/core/config/config_store.cpp:143-159` | `createDefault()` používá stejnou editorovou šablonu. Pokrýt vznik konfigurace při chybějícím souboru, zachovat create-if-missing. |
| `client/apps/tray/editor_window.cpp:172-252` | Reset přes `get_default_config` pouze naplní text editoru. Tento lifecycle zachovat. |
| `client/apps/tray/wizard_layout.cpp:5-25`, `server_info_layout.h:13-38` | Sdílený footer a DPI geometrie. Použít pro nový dialog bez kopírování stylů. |
| `client/apps/CMakeLists.txt`, `client/tests/CMakeLists.txt` | Zaregistrovat nové UI zdroje do tray aplikace i všech testovacích executable, které linkují WizardWindow. |
| `client/tests/unit`, `client/tests/ui`, `client/tests/e2e` | Rozšířit existující testy validace, dokumentu, modelu, IPC, služby a nativního UI. |
| `client.md`, `tray_UX.md`, `client/tests/ui/README.md` | Dokumentace chování a případných nových testovacích filtrů. |

Při implementaci začít kontrolou aktuálního diffu a opětovným Graft dotazem na změněné symboly. Uvedené řádky nepoužívat jako slepé patch offsety.

## 3. Stavový a datový návrh

### 3.1 Jednoznačné vlastnictví stavu

Rozlišit tři úrovně:

- **Původní dokument**: `WizardDocument.dom` a `WizardContext` představují načtený snapshot, jeho revizi, runtime limity a výchozí hodnoty. Dialog je nikdy nemutuje.
- **Draft průvodce**: přidat `std::string httpHeaders` do `WizardFields`. To je autoritativní editovatelná hodnota pro UI, požadavky, evidence a finální builder. Inicializovat explicitně v `analyzeWizardDocument` a `makeNewWizardDocument`. Nové/resetované dokumenty používají `kDefaultHttpHeaders`, existující dokumenty svou načtenou hodnotu.
- **Lokální draft dialogu**: vlastní text inicializovaný kopií `model.fields().httpHeaders`. Nikdy nesmí jít o přímou editaci modelu přes referenci.

Ponechání `WizardContext.httpHeaders` slouží kompatibilitě existujících kontextových rozhraní a původnímu snapshotu. Nesmí vzniknout druhý měnitelný draft. Všechny skutečné probe operace dostanou efektivní kontext vytvořený z původního kontextu a `operation.fields.httpHeaders`. Takový kontext je lokální snapshot operace. Případná úprava `ProbeClient::prepare` musí mít jediný zřejmý zdroj aktivních hlaviček pro URL, secret i access kontrolu.

Při otevření existujícího editoru převést LF/CRLF na skutečné řádky Win32 EDIT. Neprovádět dekódování doslovných znaků backslash-r/backslash-n v hodnotách hlaviček. JSON už byl dekódován JSON parserem.

### 3.2 Přechody stavu

| Událost | Dialog | Model průvodce | Disk |
|---|---|---|---|
| Otevřít HTTP headers | Kopie aktuální hodnoty, fokus do textboxu | Beze změny | Beze změny |
| Psát / vložit text | Změna lokálního textu | Beze změny | Beze změny |
| Reset dialogu | Nastavit `kDefaultHttpHeaders`, dialog zůstává otevřený | Beze změny | Beze změny |
| Cancel / Esc / X / Alt+F4 | Zahodit lokální draft a zavřít | Původní draft zachován | Beze změny |
| OK s chybou | Zachovat přesný text, zobrazit chybu | Beze změny | Beze změny |
| OK s platným textem | Vrátit kanonickou hodnotu, zavřít | Uložit CRLF, při změně dirty + invalidace | Beze změny |
| Back / Next / opětovné otevření | Nové otevření načte poslední potvrzenou hodnotu | Zachovat draft | Beze změny |
| Reset celého průvodce | Zavřít případné dítě jako Cancel | Nový reset draft s centrálními defaults | Beze změny |
| Zrušení celého průvodce | Zavřít dítě a zahodit lokální text | Stávající discard/lifecycle pravidla | Beze změny |
| Finish | Editor již není aktivní | Neměnný vstup stávajícího commitu | Jediná cesta uložení |
| Výsledek commitu unknown | Nepovolit další editaci draftu | Zachovat Verify save a receipt pravidla | Výsledek se nesmí odhadovat |

`WizardModel::setHttpHeaders` vrací výsledek validace, odmítá změnu během busy/committing a zapisuje až po úspěchu. Změna volá `invalidate(0)`, odstraní starou diagnostiku, chybu a odvozený návrh IP stejnou cestou jako změna vstupů prvního kroku. Potvrzení identické kanonické hodnoty nezvyšuje dirty generation a neruší platné výsledky. Pokud OK pouze kanonizuje dosavadní LF na CRLF, jde o změnu uloženého draftu.

Při `load(..., preserveFields=true)` zachovat hlavičky jen v případech, kdy se podle existujícího lifecycle zachovává tentýž draft. Explicitní nový začátek a reset inicializují nové hodnoty. Zajistit, že hlavičky předchozí relace neproniknou do nového průvodce.

## 4. Validace, normalizace a velikosti

Na OK převést text z UTF-16 do striktního UTF-8, zavolat existující `normalizeHttpHeaders` a přijmout pouze úspěšný návrat. Stejnou funkci opět použít na hranici IPC/služby a v builderu pro obranu proti obejití UI. Nevytvářet další parser, vlastní regulární výraz ani druhý seznam zakázaných hlaviček.

Zachovat současnou sémantiku parseru včetně méně nápadných pravidel:

- Prázdný blok je chyba. Prázdný vnitřní řádek, samostatné CR, chybějící dvojtečka a neplatný token názvu jsou chyba.
- LF a CRLF se normalizují na CRLF. Jeden ukončující konec řádku se odstraní podle současného parseru, další prázdný řádek se nepovoluje.
- Zakázané řídicí oktety a DEL jsou chyba, tabulátor v hodnotě se řídí stávajícími pravidly.
- Duplicitní názvy a `Content-Type`, `Content-Length`, `Connection`, `Host`, `Transfer-Encoding` jsou zakázané bez ohledu na velikost písmen.
- Současný parser připouští prázdnou hodnotu za syntakticky platným `Name:`. Nezavádět nové pravidlo povinné neprázdné hodnoty pod záminkou této změny.

Chybu zobrazit přes `httpHeadersErrorDescription`, v angličtině, s dostupným číslem řádku a důvodem. Hodnoty Cookie ani celé bloky neposílat do logů, toastů nebo nových diagnostických textů. Po zavření chybového systémového dialogu ponechat editor hlaviček otevřený s původním textem a vrátit fokus do něj. Systémová chyba musí mít editor jako vlastníka.

Rozlišit existující limity:

1. `normalizeHttpHeaders` hlídá transportní mez odvozenou od `INT_MAX` a rezervy pro aplikační Content-Type, včetně růstu LF na CRLF.
2. `kMaxConfigFileBytes = 524288` omezuje celý konfigurační JSON v UTF-8, nikoli počet UTF-16 znaků v textboxu.
3. `kIpcMaxMessageBytes = 4 * 1024 * 1024` omezuje celý zakódovaný IPC požadavek/odpověď, nikoli samotnou hodnotu hlaviček.

Dialog nesmí zavést nižší svévolný limit ani tiše ořezat vložený text. Explicitně nastavit dostatečný nativní limit EDIT, detekovat selhání převodu a přetečení textu a na OK hlídat alespoň nemožnost vejít se do stávajícího konfiguračního limitu. Definitivní limit celého dokumentu kontroluje finální builder přes stávající validaci. Na IPC počítat skutečný serializovaný počet bajtů včetně JSON escapování a obálky, stávající limit nezvyšovat. Přetečení na libovolné vrstvě zachová draft a zabrání uložení.

Bez otevření/potvrzení dialogu zachovat původní platné raw hlavičky existujícího dokumentu. Kanonizace při OK nesmí být záminkou pro globální přepis všech načtených konfigurací. Builder validuje předané fields a zapisuje jejich hodnotu, potvrzený dialog už mu předává CRLF. JSON serializer provede standardní escapování jednou.

## 5. Přenos přes IPC, kontrolní operace a Finish

### 5.1 Schéma a kompatibilita

Rozšířit `WizardFields`, `fieldsValue`, `parseFields`, view roundtrip a všechny jejich fixture o `httpHeaders`. Pro `check_url` a `check_secret`, které nyní nepřenášejí celý fields objekt, přidat povinné top-level `httpHeaders` naplněné ze stejného `request.fields.httpHeaders`. `check_access` a `finish` je přenášejí uvnitř fields. Žádné dvě hodnoty hlaviček v jednom požadavku.

Ponechat uzavřené schéma a kontrolu UTF-8, typů a velikosti. Nepovolit fallback na defaults při chybějící hodnotě. Výslovně rozpoznané historické kompletní schéma bez nového pole odmítnout jako nepodporovaného průvodce, poškozené payloady jako malformed. Zachovat rozpoznávání již podporovaných historických tvarů bez rekurzních pastí v `parseView`. Obecnou IPC verzi mimo wizard neměnit. Tray a službu vydávat společně. Starší služba nesmí být považována za schopnou uložit nové hlavičky jen proto, že odmítla nový payload.

`WizardView.context.httpHeaders` nadále popisuje původní snapshot, `WizardView.fields.httpHeaders` rozpracovaný vstup známý službě. Rozdíl po přijaté kontrole je přípustný a musí být popsaný/testovaný. Model při zachování lokálních fields nesmí přepsat novější draft starším view.

### 5.2 Evidence a kontrolní požadavky

`startProbe` přebírá hlavičky aktuálního requestu do `operation.fields` i pro URL/secret kontrolu. Každá operace používá svůj neměnný snapshot, nikdy později měnitelný stav okna ani `session.original` pro aktivní hlavičky. URL evidence musí obsahovat dvojici URL + hlavičky, secret evidence URL + secret + hlavičky, access evidence celý relevantní `WizardFields`.

Před přijetím secret/access/Finish vyžadovat shodu hlaviček s příslušnou úspěšnou evidencí. Změněné hlavičky vyžadují znovu URL kontrolu a všechny následující kroky. Zachovat pořadová čísla pokusů, generace, zrušení, connection epoch a ochranu před opožděným výsledkem. Nestačí invalidace pouze v UI, služba musí odmítnout také ručně sestavený request obcházející předchozí kontrolu.

Pro porovnání zachovat přesnou snapshot hodnotu stejně jako u ostatních fields. Transport může normalizovat při svém configure, tím se však nesmí zpětně měnit vstup evidencí. OK poskytuje kanonickou hodnotu, takže opakované potvrzení stejného textu je stabilní.

### 5.3 Jediná cesta persistence

`buildWizardDocument` rozšířit o dosazení `fields.httpHeaders` do existujícího členu `masterSetup` stejnou funkcí `set` jako ostatní pole. Zachovat pořadí existujících členů, nesouvisející runtime/TLS nastavení, identitu a kontrakty. Builder zůstává čistým sestavením a validací textu.

`finish` už vytváří fingerprint serializovaných fields + `createDefaultContracts`. Přidání pole do `fieldsValue` musí hlavičky zahrnout i do fingerprintu. Stejný commitId s jinými hlavičkami musí být odmítnut, stejný kompletní vstup musí zachovat idempotenci.

`finishTask` nadále jediný volá `ConfigStore::save`. Neměnit synchronizaci přechodů VPN, kontrolu revize, podmínky offline, kontrolu generace/session, rezervaci receipt ani rozlišení confirmed/failed/unknown. Dialogové OK ani Reset nesmějí volat save, createDefault, finish, get_default_config ani zahajovat síťovou kontrolu. Odeslání hlaviček do služby při pozdějším Next je stále jen práce v paměti a síti, není publikací konfigurace.

## 6. UI návrh

### 6.1 Karta Server URL

Ve stávající kartě zachovat URL pole a obě nápovědy. Po prvním separátoru vykreslit přesně:

1. `Configure additional HTTP headers for this server.`
2. Tlačítko `HTTP headers`.
3. `Some web servers and hosting providers require additional headers, especially Cookie or User-Agent. If you are unsure what to enter, keep the default values.`
4. Druhý horizontální separátor.
5. Dosavadní `urlRules` a `urlCheck`.

Textové konstanty umístit do existující vrstvy wizard textů. Použít měření a zalamování stávajících labels. Výška obsahu a scroll se musejí přepočítat, footer průvodce zůstává mimo scrollovaný obsah. Nové tlačítko patří do tab pořadí po URL poli a podléhá stejnému `locked` stavu jako ostatní editace.

### 6.2 Samostatný editor

Přidat `http_headers_window.h/.cpp` a čistou geometrii `http_headers_layout.h/.cpp` pod `client/apps/tray`, případně malý pomocný model draftu v testovatelné vrstvě. Použít stávající `ui::Resources`, textbox subclass, `attachModernButton`, `ButtonRole::reset`, `ButtonRole::primary`, `computeButtonFooterLayout`, standardní paint/layout infrastrukturu a aplikační ikony. Nekopírovat celý konfigurační editor ani jeho save workflow.

- Titul `HTTP headers`, owner HWND aktuálního WizardWindow, bez samostatné položky aplikačního taskbaru.
- Resizovatelný rám a maximize. Počáteční klientská velikost jako konkrétní implementační volba 760 × 420 DIP, minimální 480 × 260 DIP. Při otevření vystředit nad vlastníkem a přizpůsobit pracovní ploše běžnými společnými helpery.
- `WM_GETMINMAXINFO` nastavuje jen DPI přepočtené minimum včetně nonclient rozměrů. Nenastavovat vlastní maximální rozměry. `WM_DPICHANGED` přepne fonty a geometrii a respektuje doporučený obdélník.
- Jediný multiline EDIT vyplní prostor nad footerem se společným paddingem. Použít vertikální a horizontální scroll a vypnout soft wrap, aby jedna hlavička zůstala jedním fyzickým řádkem. Dlouhá hodnota se prohlíží vodorovně.
- Footer: Reset vlevo, Cancel a OK vpravo, Cancel bezprostředně vlevo od OK. Výchozí akce OK, červený reset styl.
- Počáteční fokus do textboxu, tab pořadí textbox → Reset → Cancel → OK → textbox, Shift+Tab opačně. Enter uvnitř multiline pole vloží řádek, mimo něj se uplatní výchozí OK. Esc, X a Alt+F4 znamenají Cancel.
- Chyba se zobrazí standardním vlastněným systémovým dialogem, takže hlavní plocha editoru zůstane pouze textboxem a footerem.

Implementovat vlastněnou modalitu se stávající hlavní message pumpou: během otevřeného dítěte zakázat interakci s vlastníkem, směrovat dialogovou klávesnici do dítěte a pumpovat potřebné zprávy. Nepozastavit nutné IPC/timer zpracování průvodce. Po zavření obnovit enabled stav vlastníka pouze pokud stále existuje a vrátit fokus na živé tlačítko HTTP headers. V případě překreslení vlastníka dohledat nový HWND tlačítka, neuchovávat zaniklý child handle.

Při destrukci vlastníka, chybě vytvoření dítěte, ztrátě relace nebo ukončení aplikace provést Cancel bez zápisu a bez callbacku do zničeného okna. Timerové překreslení průvodce nesmí krást fokus dítěti ani znovu aktivovat modalitou zakázaný owner. Při zavření vyprázdnit EDIT a undo buffer podle dosavadních UI pravidel, protože hlavičky mohou obsahovat Cookie.

## 7. Výchozí kontrakty a sdílená šablona

V `kDefaultConfigDocument` nahradit pouze objekt se vzorovým peerem a payloadKey prázdným objektem `"communicationContracts": {}`. Zachovat validitu a ostatní nastavení šablony. `makeDefaultEditorDocument()` nadále používá tento jediný sdílený dokument. Dialog hlaviček a nový/resetovaný wizard čerpají hodnoty z `kDefaultHttpHeaders`, nepřidávat další hardcodovanou kopii.

Zjištěný dopad je širší než samotné tlačítko Reset: `get_default_config`, explicitní vytvoření konfigurace přes editor a `ConfigStore::createDefault()` včetně vytvoření při startu služby budou konzistentně vytvářet prázdné kontrakty. Jde o zachování sdílené šablony požadované intentem. Existující soubor se při create-if-missing nesmí přepsat. Stávající Edit Config Reset potvrzení, async načítání, dirty stav a samostatný Save zachovat.

Nedotýkat se algoritmů `defaultContractCandidates`, `missingDefaultContractPeers`, `defaultPayloadKey`, `supportsDefaultContracts` ani významu `createDefaultContracts`. `makeNewWizardDocument` nadále nepoužívá úplnou editorovou šablonu. Výchozí kontrakty průvodce vzniknou až podle jeho dosavadních pravidel při Finish. Testy očekávající jeden kontrakt jako fixture doplní vlastní explicitní fixture, nikoli náhradní produkční vzorový kontrakt.

## 8. Pořadí realizace a závislosti

| Krok | Práce | Podmínka dokončení |
|---|---|---|
| 1 | Ověřit větev, aktuální diff, Graft kontext a dostupné MSVC/CMake/Ninja. Zachytit výchozí výsledky relevantních testů a testy generátoru kontraktů. | Známá baseline a oddělené předchozí chyby. |
| 2 | Přidat regresní scénáře pro načtení/editaci hlaviček, builder, prázdnou šablonu a nezměněný generátor. | Testy jednoznačně zachycují požadované nové chování a současné invarianty. |
| 3 | Rozšířit `WizardFields`, inicializace, model setter, serializaci a builder. Zachovat raw hodnotu bez OK. | Model/dokument testy pokrývají CRLF, reset a izolaci draftu. |
| 4 | Rozšířit IPC, service snapshots, evidence, probe vstupy a fingerprint commitu. Aktualizovat fixtures a syntetický UI backend. | Kontroly používají správné hlavičky, stará evidence neprojde, roundtrip/compatibility testy projdou. |
| 5 | Změnit výchozí editorovou šablonu na `{}` a aktualizovat závislé fixture. | Editorové/create-default cesty konzistentní, generátor průvodce beze změny. |
| 6 | Implementovat dialog, čistý layout, modalitu, DPI, validaci a připojení do Server URL. Zaregistrovat zdroje v CMake. | Nativní UI testy pro přechody, fokus a geometrii projdou. |
| 7 | Doplnit integrační a E2E ověření odloženého zápisu, cancel, revision conflict a commit unknown. Projít akceptační matici. | Prokazatelná absence předčasného zápisu a zachování save invariantů. |
| 8 | Až po regresních testech aktualizovat dokumentaci a po významných změnách spustit `graft build`. Prohlédnout výsledný diff a dodat důkazy kontrol. | Dokumentace odpovídá skutečné implementaci a výsledek má úplnou verifikaci. |

Kroky 3–4 tvoří datový základ kroku 6. Krok 5 je logicky samostatný, ale mění fixture používané ostatními testy. Práce probíhá sekvenčně v jednom aktivním kontextu. Tento plán nepověřuje vytvářením dalších agentů ani tasků.

## 9. Testovací strategie a konkrétní scénáře

### 9.1 Core a dokument

Rozšířit zejména `test_http_headers.cpp`, `test_wizard_document.cpp`, `test_wizard_model.cpp`, `test_default_config.cpp`, `test_config_store.cpp` a `test_plan3_fix2.cpp`:

- Nová, resetovaná, platná existující a repairable konfigurace inicializují správné hlavičky. Dva modely/relace si nepředávají stav.
- Reset → Cancel ponechá původní draft, Reset → OK dá přesně `kDefaultHttpHeaders`, opakované otevření a Back/Next zachovají potvrzené hodnoty.
- OK kanonizuje LF i CRLF, uvozovky/backslash a UTF-8 hodnoty projdou JSON roundtrip bez dvojitého escapování. Bez editace zůstane raw hodnota a pořadí masterSetup členů zachováno.
- Samostatně pokrýt prázdný blok, prázdný řádek, chybějící dvojtečku, prázdný/neplatný název, samostatné CR, řídicí znaky, DEL, duplicity různých velikostí a každou z pěti nativně spravovaných hlaviček v několika case variantách.
- Ověřit povolené hodnoty Cookie, User-Agent, prázdnou hodnotu za dvojtečkou, tabulátor a jeden finální konec řádku podle stávajícího parseru. Chybové texty obsahují řádek/důvod a neobsahují marker tajné hodnoty.
- Hraniční velikost finálního JSON, překročení o jeden bajt, růst LF → CRLF, UTF-8 a JSON escapování. Pro transportní overflow nepřidělovat gigabajty jen kvůli testu, pokud je třeba, ověřit pomocnou kontrolovanou aritmetiku použitou skutečným parserem.
- Výchozí editorový dokument se validuje a `communicationContracts` je objekt velikosti nula. Není v něm vzorový peer ani payloadKey. `createDefault()` vytvoří stejný dokument a zachová existující soubor.
- Builder create/reset/edit/repair s `createDefaultContracts` false/true zachová dosavadní množinu kandidátů, vyloučení vlastní IP, existující klíče a výpočet doplněných payloadKey. Nepodporovaný rozsah nadále odmítne zapnutí generátoru.

Existující test `http_headers_wizard.preserves_raw_value_position_repair_contracts_and_ipc` na řádcích 163–197 dnes vytváří fields z jiného dokumentu, protože hlavičky nebyly součástí fields. Po rozšíření musí fixture explicitně nést hlavičky původního dokumentu a stále testovat zachování raw hodnoty. Přidat vedle něj nový test skutečně změněných hlaviček. Nenahradit původní regresi pouhým odstraněním assertions.

### 9.2 IPC a služba

Rozšířit `test_wizard_ipc.cpp` a službové scénáře v `test_runtime_status.cpp`:

- Roundtrip fields, view a všech čtyř relevantních requestů, chybějící/špatný typ/extra člen, historický wizard, invalid UTF-8, forbidden header a limit obálky.
- URL/secret/access probe skutečně dostanou potvrzené hlavičky, originální kontext zůstává nedotčen. Na kontrolovaném transportu ověřit, že bez Cookie kontrola selže a s požadovaným Cookie uspěje.
- Změna A → B po úspěšné kontrole zakáže secret/access/Finish se starou evidencí, a to i bez UI. Nová URL kontrola pod B obnoví správný postup. Opožděný výsledek A nemůže schválit B.
- Stejný commitId + stejné fields se chová idempotentně. Stejný commitId + jiné hlavičky se odmítne. Po unknown se ověřuje receipt stávající cestou, neopakuje se automaticky zápis.
- Fake ConfigStore/Files počítá zápisy: dialogové operace, Next/Back a zrušení mají nulu, potvrzený Finish má jedinou publikaci s novou hodnotou. Konflikt revize, restart/expirace relace, aktivní VPN a přerušení zachovávají dosavadní ochrany.

### 9.3 Nativní UI a E2E

V `client/tests/ui/tray_ui_tests.cpp` přidat cílenou skupinu `http_headers_` a rozšířit stávající editor reset testy. Případné jednotkové testy Win32 dialogu připojit do existujícího native harnessu:

- Přesný text a pořadí sekce, dva separátory, správné zalamování, scroll a dostupnost tlačítek na úzkém okně průvodce.
- Dialog má správného ownera, blokuje jeho vstup, ale potřebné zprávy se nadále zpracovávají. Znovuotevření nezakládá další kopii dialogu.
- Skutečné řádky, dlouhý řádek bez soft wrap, resize ze všech hran/rohů, maximize/restore, minimum, přesun mezi DPI 96/144/192 a funkční tab/Shift+Tab/Enter/Esc/X/Alt+F4.
- Reset → Cancel, editace → Cancel, editace → OK, neplatné OK, opakované otevření a návrat fokusu. Při chybě zůstává text shodný, owner se neaktivuje předčasně.
- Změna potvrzená OK označí wizard dirty, Cancel nikoli. Během busy/commit/unknown nelze spustit editor. Zavření vlastníka a selhání vytvoření dítěte nezanechá zakázaný owner nebo visící okno.
- Edit Config Reset načte `{}`, Cancel ponechá původní soubor, teprve Save jej změní.

E2E použije izolovaný konfigurační soubor a privátní testovací pipe, nikdy produkční ProgramData konfiguraci. Před otevřením uložit přesné bytes/hash. Po OK, Reset, Back/Next, Cancel/X a zrušení celého průvodce prokázat shodu bytes i počet publikací. Po Finish znovu načíst JSON a ověřit nové hlavičky i ostatní pole. Zahrnout existující config a původně chybějící soubor. U create-default při startu oddělit očekávané počáteční vytvoření od zakázané publikace rozpracovaného wizard draftu.

### 9.4 Spuštění a důkazy

Plánovací etapa testy implementace nespouští. Při realizaci použít existující postupy z kořene repozitáře:

```powershell
./client/tools/build-x64.ps1 -Preset debug -Configure -Test
./client/tools/dev-tray.ps1 -Mode Test -Filter http_headers_
./client/tools/dev-tray.ps1 -Mode Test -Filter editor_reset
./client/tools/dev-tray.ps1 -Mode Test -Filter wp04_
./client/tools/dev-tray.ps1 -Mode Test -Filter wp06_scene_matrix
./client/tools/dev-tray.ps1 -Mode Test -Configuration Release -Filter http_headers_
./client/tools/build-x64.ps1 -Preset release -Configure -Test
```

Filtr `http_headers_` vznikne v této realizaci, nulový výběr musí být chyba. Nativní UI target je mimo CTest, samotné úspěšné CTest tedy nestačí. Pro geometrii zahrnout nový dialog i do relevantního `live_resize_repaint` scénáře a spustit jej. Service E2E zkompilovat přes podporovaný přepínač `-ServiceE2E` a spustit izolovaným postupem projektu s jeho nutnými závislostmi. Přesný aktuální runner ověřit před spuštěním, samotný build není důkaz E2E průchodu.

Do realizačního reportu uvést příkazy, exit kódy, počty vybraných testů a umístění logů. U vizuálního ověření uchovat syntetické screenshoty pro běžný rozměr, minimum, maximize a 150/200 % DPI. Nepoužívat skutečné Cookie. Chybějící interaktivní desktop, MSVC či E2E předpoklady uvést jako neprovedenou kontrolu, nikoli jako PASS. Nové chyby musí být vyřešeny, předchozí nezávislé chyby odděleny a doloženy.

## 10. Dokumentace

Po regresních testech upravit:

- `tray_UX.md`: přesné texty sekce, umístění mezi separátory, vlastněná modalita, resize/maximize/minimum bez vlastního maxima, multiline chování, footer, klávesnice/fokus, Reset/OK/Cancel, validace a odložená persistence.
- `client.md`: zdroj výchozích hlaviček, význam `masterSetup.httpHeaders`, skutečné řádky vs JSON escape, CRLF, case-insensitive zákazy/duplicity, stávající limity, použití hlaviček při kontrolách, prázdné kontrakty shared editor defaults a zachovaný generátor průvodce.
- `client/tests/ui/README.md`: nový filtr a případné nové testovací scény pouze pokud skutečně vznikly. Dokumentaci IPC aktualizovat tam, kde popisuje wizard fields a požadavky.

Před uzavřením dohledat přes Graft a pro neindexované markdown soubory přes `rg` odkazy na výchozí vzorový peer/payloadKey a staré reset chování. Upravit aktivní uživatelskou, testovací a distribuční dokumentaci. Historické Intents, Plans a Reports nepřepisovat. Neodstraňovat legitimní explicitní příklady kontraktů jen proto, že výchozí šablona je nyní prázdná.

## 11. Rizika a prevence

| Riziko | Konkrétní prevence / důkaz |
|---|---|
| UI uloží nový draft, probe používá původní headers | Snapshot hlaviček v každé operaci, transportní integrační test všech tří kontrol. |
| Finish schválí jiné hlavičky než ověřené | Evidence i fingerprint obsahují hlavičky, bypass testy přímo vůči službě. |
| Ztráta změny při poll/load nebo únik mezi relacemi | Jediný editovatelný fields stav a test preserveFields/new session/reset. |
| Modalita rozbije lease/timery, fokus nebo teardown | Hlavní pumpa pokračuje, owned lifecycle testy včetně destrukce a pozdních zpráv. |
| Tiše ořezaný text nebo dvojité JSON escapování | Explicitní limity EDIT, UTF převod, skutečné bajtové limity a JSON roundtrip. |
| Cookie unikne do nových logů | Sdílený bezpečný error formatter, syntetické markery tajemství v testech. |
| Nová šablona rozbije testy závislé na jednom peeru | Explicitní kontraktové fixtures, test create-default a nezávislý snapshot chování generátoru. |
| Nekonzistentní tray/služba | Striktní schéma, odmítnutí historického wizard payloadu, společné vydání. |
| Zbytečná změna raw hlaviček staré konfigurace | Kanonizovat draft na OK, ponechat test zachování hodnoty bez editace. |
| Překročení limitu celého JSON po přidání kontraktů | Finální validace sestaveného dokumentu před jedinou publikací, draft zachován při chybě. |

Nejsou otevřená produktová rozhodnutí nutná pro zahájení realizace. Konkrétní rozměry dialogu, umístění nových souborů a rozšíření evidence jsou technická rozhodnutí tohoto plánu. Pokud se během realizace objeví potřeba změnit samotný intent, vrátit ji koordinátorovi pro novou úplnou verzi intentu, neupravovat tento plán ani zdrojový intent potichu.

## 12. Akceptační matice a podmínka dokončení

| ID intentu | Prokazatelná kontrola |
|---|---|
| 1 | Screenshot a UI assertion dokazují přesné tři anglické texty, pořadí URL → separator → headers → separator → pravidla. |
| 2 | Nativní test owner HWND, modality, resize a maximize, vizuální shoda sdílených stylů. |
| 3 | Více skutečných řádků a dlouhá hodnota, test minimum/resize/DPI bez překryvu footeru. |
| 4 | Reset naplní přesnou centrální hodnotu, nezavře dialog, zápisový čítač zůstane nulový. |
| 5 | Cancel, Esc a X po editaci i Reset vrátí původní draft, včetně opětovného otevření. |
| 6 | Platné OK vrátí CRLF, každá třída chyby zachová text a otevřené okno s řádkem/důvodem. |
| 7 | Pět zakázaných názvů v case variantách selže v dialogu i při obejití UI. |
| 8 | Nové hlavičky přežijí Back/Next a používají se při kontrolách, disk se změní až při Finish. |
| 9 | Zrušení celého průvodce včetně potvrzeného dialogového OK zachová původní bytes/souborovou absenci. |
| 10 | Editor Reset dává prázdný objekt a nic nepublikuje, Save publikuje validní dokument. |
| 11 | Regrese create/reset/edit/repair a createDefaultContracts false/true dokazují stejný generátor, existující klíče a kandidáty. |
| 12 | Automatizované core/IPC/službové/UI/E2E scénáře z kapitoly 9 jsou skutečně spuštěné, výsledky doložené. |
| 13 | Aktualizovaná aktivní dokumentace odpovídá implementaci a neobsahuje starý default kontrakt jako aktuální pravidlo. |

Hotovo je až po splnění všech relevantních kontrol, bez nových regresí, po přezkoumání diffu a doložení omezení případných neprovedených testů. Samotný build, úspěšné OK v dialogu nebo uložený JSON nejsou dostatečným důkazem. Commit, publikace či nasazení nejsou tímto plánem samostatně nařízeny.

## 13. Předání a verzování

Tento soubor je úplná verze `plan-v001.md` a je neměnný. Pozdější materiální upřesnění vytvoří celý `plan-v002.md`, nikoli diff ani přepis. Marker `.intent-builder-active` spravuje výhradně koordinátor. Po předání plánovací kontext v implementaci nepokračuje. Koordinátor předloží plán uživateli a vyžádá samostatnou volbu realizace a požadovaný model/úsilí pro případný realizační task.

Konec úplného plánu v001.
