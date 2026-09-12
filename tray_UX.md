# Normativní UX tray aplikace PHPVPN

## UX00: Platnost a podklady

Tento dokument je normativní specifikací tray UX. Stav implementace a skutečný rozsah ověření rozlišuje následující přehled. Vznikl 7. září 2026 podle [zadání](notes/2026-09-07-11-19-48_notes.processed.md). Současná implementace obsahuje globální validaci, přenosné modely, sdílenou Win32 infrastrukturu, specializovaná okna, společný lifecycle a izolovaný vývojový runner. Jejich kontrakt definují příslušné části tohoto dokumentu a `client.md`. Historické pořadí zachovává [plan3_fix3.md](plan3_fix3.md), minulý rozsah ověření [dokumentační audit uložený 8. září 2026](client/tests/implementation-plan3-fix3-wp07.md). Historické výsledky nejsou novým testovým během. Úplná vizuální, přístupnostní a produkční runtime akceptace podle UX14 není existencí implementace automaticky splněna. Distribuce se touto vývojovou cestou nemění.

Slova MUSÍ, NESMÍ a SMÍ jsou závazná. Identifikátory UX01 až UX14 jsou cíle testů. [protocol.md](protocol.md) zůstává autoritou wire protokolu, [client.md](client.md) autoritou ostatních klientských pravidel a [distribution.md](distribution.md) autoritou plného vydání. Aktuální kontrakt se čte z těchto specifikací. Historické plány a předávací reporty zachovávají původ změn a tehdejší evidenci, nejsou důkazem aktuálního kódu ani jediným místem definice požadavku.

Závazné vizuální reference:

| Okno | Reference | Určující kompozice |
|---|---|---|
| Communication contracts | [communication_contracts1.png](assets/communication_contracts1.png) | Nadpis a popis vlevo, vlastní IP vpravo, tabulka, pevná patička s OK. |
| Password SHA-256 | [communication_contracts2.png](assets/communication_contracts2.png) | Menší nemodální okno nad dostupným rodičem, dvě pole a OK. Titul podle UX11, bez další obsahové identity. |
| Connect failure / success | [connect1.png](assets/connect1.png), [connect2.png](assets/connect2.png) | Connection status, velký nadpis, karta detailů a kontrol, samostatný výsledkový panel. |
| Edit Config | [editconfig.png](assets/editconfig.png) | Nadpis a cesta nad editorem, Reset vlevo, Cancel a Save vpravo. |
| Statistics | [statistics.png](assets/statistics.png) | Dvě horní karty, spodní karta přes šířku, devět hodnot. |
| Wizard | [wizzard0.png](assets/wizzard0.png), [wizzard1.png](assets/wizzard1.png), [wizzard2.png](assets/wizzard2.png), [wizzard3.png](assets/wizzard3.png), [wizzard4.png](assets/wizzard4.png) | Levý sidebar, pravý obsah s kartou, spodní oddělené akce. |

Názvy `wizzard*.png` se nemění. Uživatelské texty používají výhradně **Wizard**. Ukázkové IP, klíče a čítače na obrázcích nejsou zdrojem skutečných dat. Pomlčka mezi IP v obrázku kontraktů se výslovně nahrazuje požadovaným podtržítkem. Hodnoty editorového JSON na obrázku neurčují novou defaultní šablonu. Nativní titulkový pruh nemusí reprodukovat kreslený hamburger ani grafické nepřesnosti mockupů.

Tato verze tray je pouze anglická. Lokalizace, volba jazyka a reakce na jazyk Windows jsou mimo rozsah. Nevznikne překladový framework, resource katalog ani stabilní ABI lokalizačních klíčů. Anglické konstanty smí sdílet malý společný textový katalog. Layout nadále měří skutečný text a umožňuje zalamování, Statistics používají anglické seskupení tisíců nezávisle na systémovém locale. Budoucí lokalizace vyžaduje samostatnou specifikaci.

## UX01: Typografie, barvy a geometrie

Použít Segoe UI, při nedostupnosti systémové UI písmo. Konfigurační JSON, celé klíče, URL a technické názvy čítačů používají Consolas nebo dostupné neproporcionální písmo. Následující velikosti jsou logické výšky písma v DIP při 96 DPI, nikoli typografické body. Nativní titulkový pruh používá systémovou metriku.

| Role | Výška / váha | Použití |
|---|---|---|
| Hlavní nadpis | 28 / 600 | Communication contracts, Statistics, Edit configuration, Wizard krok. |
| Výsledek Connect | 34 / 600 | Connecting, Connection failed, Successfully connected. |
| Nadpis karty / dialogu | 20 / 600 | Connection details, Data polling, Generate key from password. |
| Podnadpis | 17 / 400 | Connection status, úvodní stručná věta. |
| Popis a label | 15 / 400 | Texty nad poli, popisy akcí. |
| Hodnota | 16 / 400, důležitá 600 | IP, běžné hodnoty. |
| Čítač | 24 / 600 | Zarovnaná číselná hodnota. |
| Pomocný a technický text | 13 / 400 | Hint, jednotka, název JSON pole. |
| Chybový text | 15 / 400, nadpis 600 | Vždy současně text i ikona. |
| JSON a Key | 14 / 400 mono | Bez automatického zalamování klíče nebo JSON řádku. |

Normativní paleta je konkrétní implementační aproximace světlých mockupů, nikoli tvrzení o přesném odečtu jejich pixelů:

| Token | Hodnota |
|---|---|
| background / surface | `#F7F8FA` / `#FFFFFF` |
| text / secondaryText | `#0D1533` / `#4B5C82` |
| border / separator | `#D6DCE7` / `#E1E5ED` |
| primary / primaryHover / primaryPressed | `#0062E8` / `#0056CE` / `#0049AF` |
| primaryText | `#FFFFFF` |
| selectedStep | `#E5EDF8` |
| success / successBackground / successBorder | `#087C2B` / `#F0FAF2` / `#A8D8B1` |
| warning / warningBackground | `#995400` / `#FFF6E5` |
| error / errorBackground / errorBorder | `#C82020` / `#FFF2F2` / `#E9A4A4` |
| disabledText / disabledBackground | `#747C8B` / `#F0F2F5` |
| focus | `#005FCC` |

Základ rozestupů je 4 DIP. Mezi labelem a polem 8, mezi souvisejícími prvky 12, mezi sekcemi 24, vnitřní okraj karty 24 a okraj hlavního obsahu 32 DIP. Při malé ploše lze okraj snížit na 16 DIP. Řádky tabulky mají minimálně 48 DIP, hlavička 40 DIP. Jednopixelové linky se škálují na nejméně jeden fyzický pixel. Referenční karty mají roh 8 DIP, textová pole a tlačítka 4 DIP. Uživatelem výslovně schválené hranaté rohy nativního provedení jsou povolenou vizuální odchylkou a nejsou vadou. Tato výjimka nemění rozměry, rozestupy, dostupnost akcí ani ostatní tolerance UX14. Jemný stín lze vykreslit jednou linkou navíc, není nutná knihovna efektů.

Tlačítko má minimálně 36 DIP na výšku a 88 DIP na šířku, vodorovný padding 16 DIP. Delší labely určují větší šířku měřením textu. Pole má minimálně 36 DIP na výšku. Klikací plocha checkboxu/switche zahrnuje label a má nejméně 32 DIP na výšku. Na malé ploše se mění rozložení, nezmenšuje se písmo ani tlačítko pod čitelnou velikost.

## UX02: Sdílené komponenty a rendering

Primární tlačítko je modré s bílým textem, sekundární bílé s rámečkem, textové má průhledné pozadí a modrý text. Reset je textová akce, která má vlastní potvrzení. Destruktivní potvrzovací akce používá červený text nebo červené vyplnění a slovní popis důsledku. Zakázaná akce nepřijímá myš, klávesnici ani programové vyvolání příkazu. Výchozí potvrzení destruktivní operace je vždy No nebo Cancel.

Editovatelné pole je bílé s rámečkem. Read-only pole má lehce tónované pozadí, ale dovoluje focus, označení a explicitní kopírování. Zakázané pole není totéž co read-only. Validní pole používá běžný rámeček, nevalidní červený rámeček, textovou chybu a přístupný stav invalid. Validace nemaže ani neopravuje vstup bez pokynu uživatele.

Checkbox v tabulce má standardní zaškrtávátko. Wizard používá vizuální switch se stejnou dvoustavovou boolean sémantikou. Implementace smí upravit kreslení nativního BUTTON, musí zachovat roli, checked/disabled stav a ovládání Space. Stav nesmí být pouze namalovaný obrázek bez přístupného ovládacího prvku.

Stavové ikony mají tvar i textový význam: zelené kolečko s fajfkou a Success/Completed, červené s křížkem a Failed, pending jantarový bod a Pending, budoucí Wizard krok šedé číslo a Upcoming. Aktivní Wizard krok má modré číslo a tónovaný podklad. Zdvojená čísla z některých obrázků se nereprodukují.

Tabulka má pevnou hlavičku, vodorovné oddělovače, bílé řádky a viditelný focus právě ovládaného prvku. Svislý scroll posouvá jen řádky. Horizontální scroll tabulky synchronně posouvá hlavičku i obsah. Editory dlouhých klíčů mají vlastní horizontální posun, takže na úzké ploše neztrácejí data. UI nesmí řešit overflow ořezáním přístupu k Actions nebo OK.

Informační a chybový panel mají nadpis, krátký popis a případnou nápravu. Nemají raw stack trace, HTML ani celé konfigurační dokumenty. Překreslení pokryje všechny nově odkryté plochy bez blikání a pruhů. Win32 implementace využije existující `window_utils.h`, nativní EDIT/BUTTON/scrollbar a omezené GDI kreslení karet. Každé okno vlastní malou RAII cache opakovaně používaných fontů, ikon, štětců a per. Vznikne při inicializaci pro cílové DPI, obnoví se pouze po skutečné změně DPI a jednou se uvolní při zániku okna. Běžný `WM_PAINT` nesmí načítat ikony ani vytvářet GDI prostředky. Invalidují se jen vizuálně změněné regiony. Úplná invalidace je dovolena při vytvoření, změně DPI, zásadním přepočtu layoutu nebo obnově po chybě dávkového přesunu. Tray uchová poslední skutečně publikovanou identitu ikony a přesný tooltip, `Shell_NotifyIcon(NIM_MODIFY)` volá jen při jejich změně, nikoli při každém shodném status snímku. Při chybě dávkového přesunu HWND zůstává funkční individuální přesun a úplná invalidace odkrytého rodiče.

Přenosné jsou barvy a DIP konstanty, textové katalogy, geometrie s předanými metrikami textu, modely stavů, validace, hashování přes CryptoProvider a schémata IPC. HWND, kreslení, font handles, message loop, systémový focus, accessibility adaptéry, modality a tray integrace se na macOS a Linuxu implementují nativně. Tato etapa jim neslibuje podporovaný build. Nezavádí Qt, Electron, WebView ani jiný rozsáhlý framework nebo novou knihovnu třetí strany.

Přenosné jádro tvoří `core/config/contracts_document.*`, `core/presentation/contracts_model.*` a `password_key_model.*` jako samostatné objekty existující statické `phpvpn-core`. DOM a plná validace zůstávají v config, čisté řádkové modely a Password SHA v presentation. Sdílejí IPv4, JSON, `defaultPayloadKey` a `CryptoProvider`. Hlavičky neobsahují Windows typy, service/CLI nepřitahují nepoužité modelové objekty. Nativní ContractsWindow, PasswordShaWindow a ContractsSaveController toto přenosné jádro používají bez Windows závislostí v modelu.


Společný Win32 adaptér tvoří `window_utils.*`, `ui_resources.*` a `ui_controls.*`, přenosné konstanty a geometrie `ui_style.h` a `layout.*`. Text se měří skutečným fontem, controls zachovávají nativní role, jména, výběr, undo a clipboard řízený uživatelem. Při změně High Contrast nebo systémových barev se smí obnovit pouze paleta, bez zbytečné obnovy fontů a ikon. Systémové štětce jsou zapůjčené. `positionControls` po selhání kterékoliv části dávky zopakuje všechny jednotlivé přesuny a úplnou invalidaci rodiče s WS_CLIPCHILDREN. `invalidateChanged` běžnou shodnou hodnotu neinvaliduje. `TrayApplication` drží jeden explicitní endpoint pro všechny čtečky, vlastní mutex a shell adaptér, bez produkčního fallbacku. Sdílenou cache poskytuje `UiResources`, shell rozhraní `ShellIcon`, specializované vykreslování připojují jednotlivá okna v `apps/tray`. Testovací povinnosti a meze důkazu stanoví UX14, výsledky konkrétních běhů patří do datovaných reportů.

## UX03: Focus, modalita, DPI a malá plocha

Všechna tray okna používají jediný sdílený helper výběru monitoru. Nové dítě použije monitor platného rodiče. Hlavní okno z tray menu použije monitor uloženého bodu, u kterého se menu skutečně zobrazilo. Bez rodiče i bodu menu následuje monitor platného foreground okna, potom monitor pod kurzorem a nakonec primární monitor. Okno se vystředí do pracovní plochy mimo taskbar. Souřadnice jsou podepsané, včetně monitorů vlevo či nad primárním. Rozměry se počítají pro DPI cílového monitoru, vnější rám se přičte k níže uvedené klientské ploše a celek se omezí na dostupnou pracovní plochu.

Reaktivace existující okno nepřesune na monitor nového kliknutí. Minimalizované okno obnoví. Je-li celé v použitelné pracovní ploše kteréhokoli dostupného monitoru, zachová přesnou polohu i velikost. Po odpojení monitoru nebo ztrátě použitelné polohy přesune okno na nejbližší dostupný monitor a změní geometrii jen o nezbytné minimum. Kontrola se provede při reaktivaci a změně DPI, rozlišení či pracovní plochy. Samotné připojení monitoru viditelné okno nepřesune. Rozměry níže jsou přesné výchozí klientské plochy v DIP bez titulku a vnějšího rámu, nikoli fyzické rozměry PNG.

| Okno | Výchozí DIP | Přednost při zmenšení |
|---|---|---|
| Connect | 940 × 680 | Svislý scroll celého obsahu, bez změny dat a výsledku. |
| Edit Config | 1080 × 560 | Nadpis/cesta a akce zachované, zmenšuje se editor. |
| Statistics | 1000 × 650 | Horní karty pod sebe, potom svislý scroll. |
| Wizard | 1000 × 680 | Sidebar 230 DIP, obsah scrolluje, patička zůstává. |
| Contracts | 1180 × 680 | Scroll tabulky, patička a vlastní IP dostupné. |
| Password SHA-256 | 540 × 360 | Pole po šířce, delší hash horizontálně posuvný. |

Na šířce pod 760 DIP přejdou Statistics na jeden sloupec. Wizard sidebar se pod šířkou 760 DIP změní na kompaktní vodorovný seznam čtyř čísel s názvem aktuálního kroku. Resize a stavové překreslení zachovají HWND vstupních polí, text, výběr a undo. Back obnoví text i výběr příslušného pole. Sidebar není interaktivní navigací. Reset lze přesunout na vlastní řádek patičky, stejně tak delší primární akci. Výška patičky se odvodí od skutečných řádků tlačítek. Při 480 × 320 DIP musí být všechny akce dostupné, obsah může scrollovat. Je-li fyzická pracovní plocha ještě menší, clamp má přednost před minimem okna, použije se další zalomení a scroll, ne off-screen tlačítko. Nelze slíbit současné zobrazení všech sekcí na libovolně malé ploše.

Změna DPI znovu spočítá fonty, metriky, layout a scrollbar, zachová vstup, výběr textu, logický focus a viditelný řádek. Ověřit 100/125/150/200 %, přesun mezi monitory a opakovaný resize všech hran i rohů. Světlý klientský obsah odpovídá mockupům i při tmavém systémovém režimu, titulkový pruh smí být systémový. Samostatná tmavá paleta není požadavkem této etapy. High Contrast používá systémové barvy a viditelný focus místo vynucené palety. Běžný text má kontrast nejméně 4,5:1 a velký text i podstatné hranice ovládání nejméně 3:1.

Tab a Shift+Tab postupují vizuálním pořadím. Enter spouští povolenou primární akci, pokud focus nemá jiné tlačítko. V multiline editoru Enter vloží nový řádek. Escape odpovídá zrušení příslušného okna, nikoli Disconnectu. Viditelný focus má nejméně 2 DIP obrys. Přístupný název pole vychází z labelu, opakované Key/Random/Password SHA navíc z identity řádku. Čtečka obrazovky musí dostat názvy, role, hodnoty, stav a chybu také u kreslených prvků. Informační text nesmí být dostupný jen při hoveru.

Contracts je resizovatelný nemodální formulář. Configuration Wizard, Contracts a Edit Config lze současně používat nad jejich nezávislými celými koncepty. Vzájemně se nezakazují, neslučují ani neobnovují, poslední potvrzená publikace vyhraje. Opakované otevření aktivuje existující formulář bez nového čtení nebo resetu dirty. Základní status polling, překreslení a nezávislý globální Disconnect zůstávají funkční.

Pouze potvrzení Random dočasně zakáže svůj rodičovský formulář, s výchozím No. Password SHA je nemodální, rodiče nezakazuje a dovoluje více dětí i pro stejný remoteIp. Cíl, generace a systémový titul jsou zachycené při otevření a focus ani scroll je nemění. Tab zůstává v právě ovládaném okně. Běžné zavření dítěte vrátí focus na Password SHA stejného kontraktu. Selhání vytvoření nezmění koncept ani dostupnost rodiče. Hlavní OK nejprve zmrazí kandidáta, pak zavře své děti bez aplikace neconfirmovaných změn a zakáže pouze ukládaný formulář.

Společný ruční Exit používá lifecycle snapshoty všech tří editorů. Při saving/committing čeká asynchronně pouze do jejich původních konečných termínů a průběžně zohledňuje nové operace, bez zákazu nezávislé editace. Poté zobrazí jediný společný Yes/No dialog s výchozím No. Dirty používá Discard unsaved changes?, unknown The file may already have changed. Close without checking the save result?, při souběhu obě věty v témže dialogu. No ponechá všechny koncepty. Yes se vztahuje na přesné generace, před prvním zahozením se ověří celý soubor snapshotů. approvedDiscard nesmí otevřít druhý dialog. Dočasné veto potvrzeného Finish nezpůsobí opakování již platného společného souhlasu.

Systémový host je skryté top-level okno, nikoli HWND_MESSAGE. WM_QUERYENDSESSION vrací TRUE bez mutace a WM_ENDSESSION(FALSE) ponechá koncepty. WM_ENDSESSION(TRUE) provede terminální neblokující endSession všech účastníků před destrukcí. Žádné UI, save, retry, Disconnect, rollback, čekání na worker nebo prodlužování deadline. Čtečky vlastní shared stav bez this/UI callbacku, abandon odvolá delivery a signalizuje cancel. Existující polling a modely Progress/Statistics se nemění. Klíče, DOM, kandidát a vlastní pracovní texty se best-effort uklidí, nativní obsah i undo vyprázdní. Vzdálený Wizard uklidí lease, uložený config, služba a globální VPN zůstávají nedotčené. Úplné přepsání všech kopií v OS nebo alokátoru není garantované.

## UX04: Menu a datové hranice

Pořadí menu zůstává: Connect/Disconnect, oddělovač, Configuration Wizard, Communication contracts, Edit Config, oddělovač, Show Info, Statistics, oddělovač, Exit. Mění se pouze velikost písmena v `contracts`, v souladu s titulkem mockupu. Start, otevření menu ani základní pětisekundový timer nečtou konfiguraci. Položky se nezakazují na základě starého výsledku dostupnosti souboru.

Connect po otevření okna jednou zavolá existující `get-config`, z platného konfiguračního JSON převezme jen zobrazované `serverUrl` a lokální `virtualIp` a celý přijatý config neloguje. Show Info validuje soubor ve službové obsluze akce. Statistics provede jedno `get-config-state`, potom používá pouze `get-status`. Contracts se otevře pouze pozdější explicitní volbou uživatele z tray menu. Získá kontrolu i dokument jediným existujícím `get-config-document`, bez předřazeného `get-config-state`. Jen availability valid dovolí otevření modelu. Wizard Finish Contracts neotevírá, nečte další konfiguraci a dokument nepředává. Produkční tray akce Wizardu nemá callback otevírající jiný editor. Podrobnosti určují UX05, UX08 a UX10–UX12.

| Zjištěný stav | Contracts | Edit Config | Wizard |
|---|---|---|---|
| valid | Otevřít editor. | Skutečný dokument. | Edit. |
| repairable | Bez editoru, nabídnout Wizard pro doplnění identity. | Skutečný text. | Repair. |
| invalid | Bez editoru, návod k opravě. | Skutečný platný UTF-8 text i při chybě JSON. | Nabídka Reset nebo opravy. |
| unreadable | Chyba přístupu bez náhrady. | Bez dokumentu. | Reset neobchází přístup. |
| encoding_error | Chyba UTF-8 bez náhrady. | Bez dokumentu. | Nabídka Reset. |
| too_large | Chyba limitu 512 KiB. | Bez dokumentu. | Nabídka Reset. |
| missing | Varování bez vytvoření, návod na Wizard/Edit Config. | Yes/No k vytvoření. | Create. |
| služba / IPC nedostupné | Chyba služby, nikoli missing. | Totéž. | Bez kontrol a Finish. |
| config_directory_security_invalid | Administrátorský postup níže, žádné opravné editování ACL. | Totéž. | Žádný Reset ACL ani Wizard jako náprava ACL. |

Bezpečnostní chyba adresáře má přednost před obecnou radou vytvořit konfiguraci. Uvede pouze autoritativní `configPath` ze služby a návod požádat administrátora o restart PHPVPN service. Zřetelně uvede, že restart přeruší VPN a může vytvořit chybějící default, potom doporučí ověřit skutečný soubor v Edit Config. Selhání načtení descriptoru je unreadable, nikoli důkaz neplatného ACL. Tray neopravuje ACL, nemaže žurnál a neprovádí restart.

URL/IP v Connect patří pouze do určených dvou polí. Jednorázový existující `get-config` kvůli nim přenese do tray celý platný JSON včetně citlivých hodnot, což je vědomě přijaté. Tray z něj použije jen `serverUrl` a lokální `virtualIp`, celý config neloguje a omezuje životnost jeho kopií. `get-status`, Statistics, tooltip, obecné chyby a log nadále nesmějí obsahovat URL, IP konfiguraci, tajemství nebo cesty. Protože `get-config` čte aktuálně uložený dokument a Connect používá svůj neměnný aktivní snapshot, při souběžné externí změně může zobrazená URL/IP patřit novější uložené konfiguraci než právě aktivnímu spojení. Tento trade-off je přijatý za účelem zachování stávajícího IPC. Edit Config a konfigurační pole Wizard/Contracts smějí zobrazit své určené hodnoty včetně klíčů. Výjimka neplatí pro chybové popisy nebo logování.

## UX05: Connect

Levý okraj společně zarovnaného obsahu, karty a výsledkového panelu je 48 DIP, pravý 64 DIP. Pod šířkou 600 DIP se oba zmenší na 16 DIP. Svislý vnější okraj zůstává 32 DIP, na úzké ploše 16 DIP. Mezera mezi popisem a kartou je 12 DIP. Za 28DIP nadpisy sekcí karty následuje 16 DIP mezery, běžné řádky detailů a kontrol mají krok 40 DIP. Na úzké ploše se detaily a kontroly skládají pod sebe. Všechny čtyři hrany hlavní karty při výchozí klientské ploše splňují toleranci UX14 vůči referencím connect1/connect2. Výška se nadále odvozuje od změřeného textu, na menší ploše je celý obsah dostupný svislým posunem.

Titul je `PHPVPN connection`. Svislá kompozice je `Connection status`, velký nadpis, stručný popis, bílá karta a výsledkový panel. Karta obsahuje `Connection details`, dva řádky `Server URL` a `Virtual IP`, oddělovač a `Connection checks`. Kontroly jsou přesně `Server URL`, `Server secret`, `Access key`, s ikonou a `Pending`, `Success` nebo `Failed`. Jsou to nové labely existujících `serverUrlCheck`, `serverSecretCheck`, `accessKeyCheck`, nikoli nová kontrola nebo zveřejnění hodnot secretů.

URL se zobrazí jako doslovná validovaná konfigurační hodnota bez automatického otevření browseru, IP jako kanonický text. Dlouhá URL musí být celá dostupná označením a horizontálním posunem read-only pole. Chybějící kontext se zobrazí `Unavailable`, nikdy ukázkovou URL/IP. Vědomě je ale dovoleno, aby hodnoty z aktuálně uloženého dokumentu po externí změně byly novější než aktivní snímek spojení podle UX04. Nesmějí se zobrazit SERVER_SECRET, ACCESS_KEY, payloadKey, tag, ciphertext, paket, interní cesta nebo raw síťová/systémová chyba.

Při zahájení je nadpis `Connecting`, popis `Checking the server and preparing the VPN connection.` a oranžové pending kontroly. Nativní ikona okna/tray zachová existující štíty a licence. Velký štít uvnitř obsahu se nahradí typografickou kompozicí mockupu. Trojice kontrol se přebírá jako celek, bez kumulace předchozích zelených bodů. Nová epocha znamená nový pokus, recovery historii bootstrapu nepřepisuje. Progress používá nezávislý 500ms polling existujícího `get-status` podle UX04 a smlouvy `get-status` v `client.md`. Neprovádí HTTP ani opakované čtení konfigurace a nemění termíny základního pollingu. Jediné `get-config` proběhne při otevření okna pouze pro dva zobrazované detaily.

Úspěch je přípustný až při `connected`, `running` a připraveném tunelu, což služba publikuje až po spuštění pracovních rolí. Nadpis zní `Successfully connected`, popis `The VPN connection is ready.`. Všechny kontroly jsou zelené. Spodní zelený panel má `Connected successfully`, pod ním `This window will close automatically.` a napravo číselný odpočet `Closing in 3 seconds`, potom `Closing in 2 seconds` a `Closing in 1 second`. Na úzké ploše se odpočet přesune pod text panelu.

Čas začíná prvním skutečným zobrazením úspěchu. Konkrétní hranicí je dokončení WM_PAINT nadpisu Successfully connected, je-li viditelný, protíná klientskou plochu a rodič není minimalizovaný. Samotný příjem statusu ani WM_PRINT čas nespustí. Model používá monotónní deadline +3000 ms, zbývající sekundy jsou zaokrouhlené nahoru. Polling ani další paint deadline neobnovují. Po vypršení zavřít při nejbližší obsluze UI, nejpozději o jeden existující 500ms tick později při nezablokovaném UI. Po sleep/resume již prošlý deadline nevytváří nový odpočet. Nativní text ohlásí úspěch, tick mění pouze text odpočtu, nikoli znovu celou obrazovku.

Selhání má nadpis Connection failed, bezpečný popis a červený panel. Popis, nadpis panelu i náprava pocházejí z jediného pevného katalogu níže, vybraného podle bezpečného kódu ze služby. Raw errorDescription se nepoužije. Neznámý kód použije communication_failed. Katalog neinterpretuje jednotlivé kontroly ani raw serverovou odpověď.

| Kód příčiny | Popis | Panel / náprava |
|---|---|---|
| endpoint_unavailable_or_invalid | The server endpoint is not reachable or not valid. | Cannot connect to the server / Check the Server URL and network connection, then try again. |
| server_verification_failed | The server could not be verified with the configured SERVER_SECRET. | Cannot verify the server / Check the configured SERVER_SECRET and server settings. |
| access_key_rejected | The server rejected the configured ACCESS_KEY. | Cannot connect to the server / Check the configured ACCESS_KEY and try again. |
| tunnel_start_failed | The local tunnel could not be started. | Cannot start the VPN tunnel / Ask your administrator to check the PHPVPN service and tunnel setup. |
| communication_failed | Communication with the server could not be completed. | Cannot complete the connection / Check the network and server availability, then try again. |
| communication_timeout | The server did not respond in time. Verification could not be completed. | Connection timed out / Check the network and server availability, then try again. |
| chyba doručení existujícího status IPC | The connection status is unavailable. | Cannot read connection status / Check that the PHPVPN service is running. |

Endpoint selže v prvním bodě, ověření serveru ve druhém, access ve třetím. Při timeoutu selže právě prováděný bod. Pozdější neprovedené body zůstanou pending, při chybě tunelu mohou být všechny zelené. Chyba doručení IPC zachová poslední celou trojici, ukáže samostatnou nedostupnost a pokračuje v čekání na validní status, nesmí vymyslet chybu serverového bodu. Chyba jednorázového config čtení pouze nastaví detaily na Unavailable. Běžný provozní výpadek není automaticky tunnel_start_failed. Skutečné kódy i obnovitelnost nadále určuje služba. Timeout, 503 a neplatný MAC se nesmějí prezentovat jako odmítnutý klíč.

Konečný výsledek okna se uchová i během následné recovery, chybové okno se nikdy nezavře automaticky. Nový místní Connect resetuje prezentaci. X/Escape zavře pouze progress. Enter sám nic nepřipojí, neopakuje a neodpojí. Read-only detailní pole jsou v pořadí URL, IP a podporují kopírování. Cizí dokončený Disconnect zavře ještě neukončenou prezentaci podle dosavadního lifecycle. Žádný timer přeživší zánik okna nesmí měnit nové okno.

## UX06: Edit Config

Při výchozí klientské ploše začíná záhlaví 16 DIP od horního okraje. Má výšku 72 DIP včetně mezer mezi nadpisem, cestou a editorem, takže horní hrana textového pole leží na 88 DIP. Při omezené výšce se záhlaví omezí dostupným prostorem nad patičkou.

Titul `PHPVPN configuration`, nadpis `Edit configuration`, pod ním skutečný `configPath` vrácený službou. Pevně zapsaná ukázka `C:\ProgramData\phpvpn\config.json` se nepoužije jako náhrada autoritativní cesty. Pod hlavičkou je jediný velký nativní multiline editor s vertikálním i horizontálním scrollbarem a bez word wrap. Patička má Reset vlevo, Cancel a modré Save vpravo.

Zůstává `EditorDocument`: nezměněný vstup pochází z původních bajtů, nikoli z CRLF prezentačního převodu Win32. Syntakticky vadný JSON v platném UTF-8 je opravitelný text. Jediný počáteční BOM se normalizuje podle `client.md`. Raw NUL použije výslovně označený režim `JSON string literal`, striktní dekódování a žádné zkrácení na prvním NUL. Otevření a Cancel nemění disk.

Save pošle celý text službě, která validuje UTF-8, JSON, všechny konfigurační invarianty, velikost a bezpečnou atomickou publikaci. Úspěch zavře editor a oznámí `The configuration was stored. It takes effect on the next connection attempt.`. Save používá jedinou původní 30s deadline a generace okna, konceptu a operace. Odpověď musí být úplná, korelovaná a z platné generace. Neodeslaný požadavek a bezpečně potvrzené odmítnutí před publikací zachová text i undo a vrátí focus do editoru. Po pokusu o zápis přejde timeout, ztracené potvrzení, vadná/cizí odpověď nebo nejednoznačná chyba do unknown. Zpráva výslovně říká, že soubor už mohl být změněn, a doporučí zavřít a znovu otevřít Edit Config pro kontrolu. Koncept zůstane pouze pro čtení, Save a Reset nejsou dostupné. Ztracené potvrzení nesmí vést k automatickému přeposlání. Během ukládání jsou zablokované úpravy textu, druhý Save, Reset, Cancel, Escape i X. Původní deadline běží i při pozdním dokončení starého Resetu.

Reset použije Yes/No: `Restore the default configuration in this editor? The file will change only after you choose Save.`. Yes asynchronně načte `get-default-config`, teprve úspěch nahradí koncept a označí dirty. No, chyba nebo odpověď staré generace nemění text. Missing při otevření používá existující potvrzené `create-default-config`, které zapisuje ještě před otevřením editoru. Cancel již vytvořený soubor neodstraňuje.

Focus začíná v editoru. Tab přejde na Reset, Cancel, Save, Shift+Tab opačně. Enter v textu vloží nový řádek, Ctrl+A/C/V a běžné editační zkratky zůstávají. Escape/Cancel/X při dirty vyžádají Discard unsaved changes? s výchozím No. Unknown navíc upozorní na možnou změnu souboru a vyžádá potvrzení zavření bez kontroly výsledku. No zachová přesný koncept, Yes jej zavře pouze při stále shodném lifecycle snapshotu. Během Save zavření není dostupné. Čisté okno se zavře přímo. Opakovaná volba Edit Config pouze aktivuje již otevřené okno. Resize zachová oba scrollbary, caret, výběr, hlavičku i tlačítka a nepřidá původní svislý artefakt vedle Cancel. Hlavičku a skutečný configPath předává TrayApplication z odpovědi služby, bez druhého čtení souboru. Show Info sdílí pouze nativní textové okno EditorWindow a zůstává samostatnou read-only prezentací bez konfiguračního titulku, hlavičky, cesty a všech konfiguračních akcí.

## UX07: Statistics

Titul `PHPVPN Statistics`, nadpis `Statistics`, popis `Read-only global service counters for the current VPN connection.`. Slovo session z mockupu znamená globální logické spojení, nikoli Windows uživatelskou relaci. Použít tuto jednoznačnou adaptaci.

| Karta | Label | Pole |
|---|---|---|
| Data polling | Total polls | dataPollsTotal |
| Data polling | Empty polls | dataPollsEmpty |
| Data polling | Polls with payload | dataPollsWithPayload |
| Serialized data | Request body | serializedDataRequestBodyBytes |
| Serialized data | Response body | serializedDataResponseBodyBytes |
| IPv4 traffic, vlevo | Packets sent | ipv4PacketsSent |
| IPv4 traffic, vlevo | Bytes sent | ipv4BytesSent |
| IPv4 traffic, vpravo | Packets received | ipv4PacketsReceived |
| IPv4 traffic, vpravo | Bytes received | ipv4BytesReceived |

Technický název se zobrazuje menším mono písmem pod labelem, hodnota vpravo. Bajty se vypíší přesně v B, například `3,458 B`. Použít celočíselné anglické seskupení tisíců bez float převodu nebo zaokrouhlení na KiB. Zachovat přesnost až UINT64_MAX.

Každé překreslení vychází z jednoho úplného validního `get-status` snímku. Platí invariant pollingů, UI nic nesčítá ani nenuluje samo. Čekání bez snímku zobrazuje `Waiting for service status` a pomlčku místo hodnot. Chyba čtení zachová poslední celý snímek s viditelným `Service status unavailable. Showing the last received values.`. Další úspěšný snímek aktualizuje všechny hodnoty společně. Otevření dovoluje jednorázový paměťový odečet, pravidelně se používá existující pětisekundový timer bez posunu jeho termínů.

Neexistuje Reset counters, nové get-statistics, HTTP, diskový refresh, ukládání nebo tajné údaje. Statistics nemá vlastní timer ani reader, TrayApplication předává poslední validní snímek a samostatný freshness stav. Tab dovolí označit/kopírovat devět nativních read-only hodnot v pořadí tabulky, Escape/X zavře jen okno, Enter nemá mutující akci. Široká karta IPv4 traffic má dva sloupce s oddělovačem, na úzké ploše se rozloží svisle. Měřené dlouhé hodnoty se přesunou pod label a technický název. Pokud se celý obsah nevejde, svislý scroll a automatické odscrollování zaměřeného pole zachovají dostupnost. Jednořádková pole umožňují i horizontální posun, label nesmí překrýt nejdelší čítač.

## UX08: Wizard, společná struktura a lifecycle

Titul `Configuration Wizard`, sidebar `Configure this computer` a čtyři kroky `Server URL`, `Server secret`, `IP and access key`, `Contracts`. Sidebar je informativní. Completed a zelená značka znamenají doručené platné ověření současných vstupů. Chyba je Failed, zbývající kroky Upcoming. Aktivní krok má modrou značku a tónované pozadí.

Patička: Reset vlevo, Back, Cancel a primární akce vpravo. V prvním URL kroku je Back viditelný, ale zakázaný, čímž se zachová nevratnost a vzhled mockupu. V odpojovací bráně Back není. Tato výslovná změna nahrazuje starý požadavek na úplnou nepřítomnost Back. Na nultý krok se nikdy nenaviguje zpět.

Kroky URL/secret/identity mají primární label `Verify & continue`, čtvrtý `Finish`. Výraz Next v interních modelech a algoritmech označuje právě jednu tuto akci. Reset, Cancel a Escape nezmění disk před Finish. Při kontrole jsou ověřovaná pole a opakovaná primární akce zakázané, Cancel/Reset bezpečně ruší vlastní práci. Enter spouští jen povolenou akci. Focus je na prvním editovatelném poli/switchi, po validaci na první chybě, po návratu zachová rozpracovaný text a výběr.

Zachovávají se create/edit/repair/reset, loading, checking, waitingDisconnect, interruptedByConnect, sessionExpired, committing, commitUnknown, completed a closed. Vypršení 15/60s pronájmu, 500ms čtení operace a epochy, konečný rozpočet pokusu, attemptSequence, clientAttemptId, windowGeneration a bezpečné rušení mají stejný význam jako nyní. Neexistuje automatický retry po 401, timeoutu, chybě IPC ani po ztraceném start acknowledgement. Pozdní úspěch neobnovuje uzavřený pokus. Základní tray timer zůstane nezávislý. Potvrzený Finish provede jednu sekvenci receipt, oznámení, wizard-close a zavření, kterou timer nemůže znovu spustit. Ztracená odpověď close potvrzené uložení neruší. Nejasný Finish se ověřuje jedním wizard-get-commit až po výslovném Verify save, bez dalšího Finish.

Katalog stručných provozních textů:

| Stav | Text / akce |
|---|---|
| loading | Loading configuration… |
| checking URL / secret / identity | Checking the server… / Verifying the server… / Checking access… |
| waitingDisconnect | Waiting for the VPN to stop… |
| saving | Saving configuration… |
| timeout | The check timed out. Your entries have been kept. Choose Verify & continue to start a new check. |
| sessionExpired | Contact with the service was lost and this setup session expired. Open the wizard again. / Open again |
| commitUnknown | The save result is unknown. The file may already have changed. Verify the result before saving again. / Verify save |
| interruptedByConnect | A VPN connection was started while setup was open. Your entries are kept, but the checks must be repeated. |

Při cizím Connect se porovnává epocha, i když mezitím proběhl Disconnect. Zachovat koncept, zneplatnit důkazy a vrátit odpojovací bránu. Je-li již disconnected/idle, nabídnout `Continue` bez dalšího zbytečného Disconnectu a začít kontroly od URL. Connect přijatý před commitem brání Finish, Connect po commitu načte novou konfiguraci. Wizard nesmí uzamknout Connect na celou dobu vyplňování.

Wizard pro společný Exit poskytuje read-only lifecycle snapshot se stavy open, clean/draft-dirty, committing a commitUnknown. Obsahuje generace okna, relace, konceptu, pokusu a operace, attemptSequence, connectionEpoch a původní absolutní 15000ms commit deadline. Čtení snímku, heartbeat a polling nemění deadline, text, HWND, selection ani undo. Přijaté editace a volby, Reset a změna polí ověřeným IP návrhem posouvají draft generaci. Nový koordinátor při committing čeká asynchronně pouze do původního termínu nebo potvrzeného výsledku. Po jednom společném potvrzení zavolá approvedDiscard přesného nezměněného snapshotu. Stará generace ani committing se nezahodí. Úspěšné schválené zahození zavře místní okno bez druhého dialogu, publikace, get-commit, config read, retry, Connect, Contracts nebo Disconnect. Na této cestě se neposílá ani wizard-close, vzdálený úklid zajistí lease. Běžný Cancel a oba potvrzené Finish scénáře si zachovávají vlastní wizard-close.

Systémový dotaz WM_QUERYENDSESSION nemění koncept a povolí ukončení. WM_ENDSESSION(FALSE) nic nezahodí. WM_ENDSESSION(TRUE) vyvolá terminální neblokující endSession bez UI, IPC, wizard-close, čekání na reader join nebo prodloužení deadline. Vyřadí generace a doručování výsledků, zastaví timer, vyprázdní nativní text a undo a uvolní místní koncept. Wizard čtečky vlastní shared transportní stav bez this/UI callbacku. Worker bezpečně dožije ze svých hodnot, případná již přijatá službová publikace není ukončením tray odvolána. Pending request a transportní buffery se best-effort mažou, úplné přepsání všech kopií modelu/OS není garantováno. Toto je Wizard participant společného hostu a koordinátoru podle UX03. Jeho dílčí testy nenahrazují samostatný aplikační broadcast test.

## UX09: Jednotlivé kroky Wizardu

### Krok 0, wizzard0.png

Eyebrow `Before setup`, nadpis `Disconnect the VPN before continuing`, popis `Configuration requires the global VPN connection on this computer to be stopped.`. Karta má přesnou významovou adaptaci `The VPN will be disconnected for everyone.` a `This affects every user of this computer. The wizard will not reconnect it automatically.`. Množné číslo All VPN connections z obrázku neznamená odpojování cizích VPN produktů, phpvpn vlastní jedinou globální VPN.

Primární `Disconnect & continue` je výslovný souhlas s jedním globálním Disconnectem, bez dalšího duplicitního Yes/No. Reset a Cancel zůstávají dostupné, přestože obrázek Cancel neukazuje. Čekat na skutečné disconnected/idle, ne jen IPC acknowledgement. Při chybě zůstat v bráně s příčinou. Focus začíná na Cancel, aby náhodný Enter při otevření nepotvrdil globální odpojení.

### Krok 1, wizzard1.png

Nadpis `Server URL configuration`, popis `Set up this computer for your PHPVPN server. Changes are saved with Finish.`. V kartě label `Server URL`, pole, hint `Enter the complete address of the PHPVPN server script.`, tónovaný příklad `Example: https://vpn.example.com/phpvpn.php`, oddělovač a `HTTP and HTTPS are supported. The URL cannot contain a username, query, or fragment.` a `The endpoint will be checked before server authentication.`.

Nový/resetovaný koncept stále obsahuje `http://MODIFY-THIS-URL/phpvpn.php`, nikoli loopback opsaný z obrázku. Placeholder se odmítne před DNS s `Replace MODIFY-THIS-URL with your server address.`. Existující `parseServerUrl` nadále dovoluje jiný název skriptu. Kontrola je pouze syntaxe/dostupnost health, nezveřejní ověřený čas ani kryptografickou identitu. TLS politika zachovaného kontextu se nemění za účelem úspěchu kontroly.

### Krok 2, wizzard2.png

Nadpis `Server secret configuration`, popis `Use the secret configured on your PHPVPN server.`. V kartě label `Server secret (exact text, including any spaces)`, pole, `Enter the exact SERVER_SECRET value configured in phpvpn.php.`, switch `Use default server secret`, oddělovač a `The server response will be authenticated before continuing.`. Při defaultu navíc jediná stručná věta `The default secret is publicly known.`.

Switch je pod polem podle mockupu. True zobrazuje default v zakázaném poli, false doslovnou hodnotu. Přepnutí zapnuto nahrazuje hodnotu, vypnutí ji ponechá. `useDefaultServerSecretInWizard` má přednost před fallbackem. Žádný trim, změna velikosti písmen, normalizace Unicode nebo ztráta NUL. Při řídicích znacích zůstává zvláštní label `Server secret (JSON string literal)` a krátký hint `Keep the quotes. JSON escapes represent control characters.`. Kontrola provede nový ověřený health a info. Chyba zůstane na tomto kroku a zachová přesný vstup.

### Krok 3, wizzard3.png

Nadpis `Virtual IP and access key`. Popis: `Choose this computer's virtual IP inside {vpnCidr}. A suggested IP is not a reservation. Duplicate client IPs are allowed, but collisions are the operator's responsibility. A local contract cannot point to this computer itself.`.

Karta: `Virtual IP address`, pole, switch `Automatically detect IP address`, oddělovač, `Access key (64 lowercase hexadecimal characters)`, pole a switch `Use default access key`. Přemístění switchů pod pole mění layout, nikoli význam metadat. Automatická IP používá pouze platný ověřený návrh. Bez návrhu: `No verified IP suggestion is available. Turn off automatic detection to enter an IP, or go Back and verify the server again.`. Žádná rezervace ani aktualizace vzdálené mapy ACCESS_KEYS.

Při true se default accessKey okamžitě přepočítá při každé změně IP jako hex posledního oktetu opakovaný 32krát. Neplatná IP jej vyprázdní. Zapnutí přepíše vlastní hodnotu, vypnutí ponechá poslední vygenerovanou. Explicitní uložené příznaky mají přednost, fallback false pro existující platný klíč platí pouze při absenci příznaku. Validace self-kontraktu proběhne ještě před kontrolou access.

Pod kartou MUSÍ být vždy před akcí viditelný krátký panel, který v mockupu chybí: `Each Verify & continue check may permanently remove waiting messages for this IP, even if no result arrives. Messages are discarded, not delivered to the VPN. Checks are never retried automatically.`. Panel není schovaný v tooltipu nebo sbalené nápovědě. Primární akce je souhlasem s jediným pokusem. Zachovat úplnou diagnostickou validaci obálky a zahození payloadů bez GCM výhradně podle protocol.md 9.5.

Krátký hint u klíče: `Use the matching ACCESS_KEYS entry on the server. Ask your administrator if needed.`. Při defaultu doplnit `The default key is publicly predictable.`. Při 401 náprava odkazuje na nastavení této IP ve správě hostingu, ne na zdrojový PHP soubor údajně otevřitelný přes webovou URL. Timeout ani chybný MAC nejsou automaticky špatný klíč.

### Krok 4, wizzard4.png

Nadpis `Communication contracts`, popis `Review the default peer contracts that will be included in this configuration.`. Karta obsahuje switch `Add missing default communication contracts`, oddělovač a tři sloupce `Existing contracts`, `Missing defaults`, `To add on Finish`. Na malé ploše sloupce přejdou pod sebe.

Výpočty používají původní DOM a `.2` až `.15` pro podporované `10.A.B.0/24`, vyloučí self a existující peery. Kandidátů je 13/14, což se NESMÍ zaměnit s 252 řádky následného plného editoru. Nové otevření má volbu zapnutou, odškrtnutí nastaví doplnění na nulu bez odstranění původních kontraktů. Mimo podporovaný CIDR zůstane switch zaškrtnutý, zakázaný a efektivní request false.

Pod kartou stručně `Existing contracts and keys are kept. Default keys are publicly predictable.` nebo konkrétní vysvětlení nepodporovaného doplnění. Dále `Finish saves the configuration and closes this wizard. It does not connect the VPN.`. Finish provede úplnou validaci a jedinou atomickou publikaci bez dalšího data pollingu. Potvrzený úspěch nejprve zobrazí oznámení s titulkem Configuration saved a textem The configuration was saved. This wizard did not connect the VPN. Poté odešle jeden wizard-close a po dokončení jeho konečného požadavku zavře okno, i když se odpověď close ztratila. Text nečiní tvrzení o případném cizím Connect po publikaci. Communication contracts automaticky neotevře. Uživatel jej může později otevřít novou explicitní volbou z tray menu, která jednou použije existující `get-config-document` nad aktuálně uloženou konfigurací. Nejde o pokračování ani handoff konkrétní Wizard publikace. Nejasný Finish používá Verify save a existující receipt, nikdy slepý opakovaný Finish.

## UX10: Contracts, model a tabulka

Titul i nadpis `Communication contracts`, popis `Manage secure communication between this computer and other virtual IP addresses.`. Vpravo nahoře label `Your virtual IP` a IP z načteného dokumentu. Při aktivní VPN jde o uloženou konfiguraci pro příští spojení, její identita nesmí být zaměněna za identitu současného tunelu.

Tabulka má sloupce `Contract`, `Enabled`, `Key`, `Actions`. Pro výchozí `/24` má právě 252 řádků, vzdálené `.2` až `.254` po vyloučení lokální IP. Řazení je vzestupně podle numerické hodnoty remoteIp. Self řádek nevzniká. Identita řádku je remoteIp, nikdy jeho pořadové číslo nebo HWND.

Contract zobrazuje `minNumericIp_maxNumericIp`. Například vlastní `.3` a vzdálená `.2` dávají `10.99.99.2_10.99.99.3`. Do JSON patří klíč `communicationContracts[remoteIp]`, nikoli tento popisný identifikátor. `.2` je numericky před `.10`, ne lexikograficky za ní.

Enabled je true právě pro existující validní objekt s payloadKey. Chybějící kontrakt je unchecked, ale jeho Key je editovatelný a předvyplněný. Nevalidní existující konfigurace se nesmí tiše převést na unchecked řádek, vůbec se neotevře jako validní model.

`ContractsModel` vlastní jeden původní validní `ContractsDocument` a lehké řádky s textem Key, výchozí srovnávací hodnotou, enabled, validitou a dirty, bez kopie DOM na řádek. `configPath` a revisionToken jsou informační údaje, revize není CAS. API vrací řádky pouze pro čtení, mutace používají remoteIp. `ContractFocus` je remoteIp + `ContractColumn`, první blokující pole se získá z celé sady. Identita `ContractTarget` zachytí také rodičovský model a generaci, přepnutí focusu ji nezmění. Více potvrzených akcí stejného cíle aplikuje poslední výsledek. Příprava kandidáta a close staré cíle vyřadí.

Počáteční šířky sloupců jsou 250 / 80 / pružná nejméně 260 / 220 DIP, padding buněk 12 DIP. Na výchozí ploše má Key většinu zbývající šířky. Header i spodní chyba a OK zůstávají mimo svisle posouvané řádky. Při nedostatku šířky se použije synchronní horizontální scroll tabulky. Vlastní IP a popis mohou přejít pod sebe.

Každý řádek má logický stav enabled, text klíče, validitu a změnu vůči originálu. Nejvýše 252 řádků má všechny skutečné nativní přístupné ovládací prvky ve scrollovatelném obsahu, bez virtualizace, recyklace HWND nebo vlastního UI Automation provideru. Focus je dvojice remoteIp + sloupec/akce, zachovává se po scrollu, DPI a aktualizaci validace.

Tab order je po řádcích Enabled → Key → Random → Password SHA, potom další řádek a nakonec OK. Shift+Tab opačně. Při přechodu na neviditelný řádek se automaticky odkryje. Ctrl+End v navigaci tabulky přejde na poslední řádek, v textboxu zůstává textovou zkratkou. Wheel a PageUp/PageDown obsluhují tabulku, pokud není aktivní textová operace. Hromadná editace ani automatické přepnutí Enabled akcí Random/Password SHA nejsou součástí.

Jiné CIDR se odvozují přes `Ipv4Cidr::firstClientAddress`, `lastClientAddress`, `isValidClientAddress` a `clientAddressCount`, nikoli prefixem `10.99.99`. Globální sdílená validace přijímá pouze kanonické `/24` až `/29`, se zachovanou rezervací sítě, první host adresy a broadcastu. `/24` má nejvýše 252 vzdálených peerů, `/29` pět klientských adres a čtyři vzdálené peery. `/30` má pouze jednu klientskou adresu bez peera, `/31` a `/32` současný model nepodporuje. Dříve platné `/16` až `/23` jsou nově neplatné před Connectem i každou publikací, včetně Save, set-config, Wizard Finish a startup defaultu. Plný i repair režim používají stejnou bránu. Mění se kompatibilita konfigurací, nikoli HTTP, kryptografický wire formát nebo IPC schema. Wizard kandidáty 13/14 toto pravidlo nerozšiřuje.

Počet klientských adres minus jedna ověřit s kontrolou přetečení před alokací a generováním hashů, nejvýše 252 řádků. Rozsah mimo `/24` až `/29` je invalid config. Edit Config zpřístupní text k opravě, není výjimkou z validace Save. Neprovádět automatickou migraci, nezobrazit jen první část kontraktů a nezkrátit seznam při publikaci. I v podporovaném rozsahu se zvlášť kontroluje skutečná velikost kompletního výsledného dokumentu a oba rozpočty front.

## UX11: Key, Random a Password SHA

Existující payloadKey se načte přesně. Chybějící se zobrazí pomocí jediné sdílené `defaultPayloadKey`:

```text
lowercase_hex(SHA-256(ASCII(minNumericIp + "_" + maxNumericIp)))
```

Jde o kanonické IPv4 texty, jeden ASCII znak `_`, žádný LF, NUL, whitespace ani další prefix. Stejná funkce je použita ve Wizardu i defaultu. Tento veřejně odvoditelný klíč se nesmí popsat jako náhodné tajemství. Disk ani druhý peer se otevřením/odvozením nemění.

Key musí být přesně `[0-9a-f]{64}`. Žádný automatický lowercase, trim nebo tiché zkrácení vložené hodnoty na 64 znaků. Pole musí umožnit ukázat chybu nadbytečného znaku. Kontrola má konečný vstupní limit odvozený od 512KiB rozpočtu konceptu, nadlimitní vložení se odmítne celé s vysvětlením, ne částečně.

Nevalidní aktivní řádek má červený textbox a patičku `Key must contain exactly 64 lowercase hexadecimal characters.`. OK je disabled. První blokující remoteIp je uvedena bez klíče a lze ji zaměřit. Oprava okamžitě obnoví platnost bez modálního dialogu. Nevalidní unchecked řádek smí mít stejnou lokální značku chyby, ale OK NEBLOKUJE, protože se nepublikuje. Jeho zaškrtnutí začne OK blokovat. Klíče unchecked řádků nejsou kopírovány do výsledného JSON.

Random vyvolá Yes/No s výchozím No: `Replace the key for {contractId} with a new random key?`. Yes zavolá `CryptoProvider::randomBytes` pro přesně 32 bajtů a převede je do 64 lowercase hex znaků. Použití běžného PRNG nebo časového seedu je zakázané. No/Escape/X nic nezmění. Chyba CSPRNG zachová starý klíč a zobrazí `A random key could not be generated. The current key has been kept.`. Změna zůstává jen v příslušném konceptu.

Password SHA používá neměnný `ContractTarget` zachycený při otevření. Model poskytuje systémový titul `Password SHA - Contract {minIP_maxIP}` z kanonického numericky seřazeného páru, nikoli pozdějšího výběru nebo focusu. Pro stejný cíl smí být titul shodný. PasswordShaWindow zachovává tento neměnný cíl a titul, má nativní WS_EX_APPWINDOW a rodiče nezakazuje. Nadpis `Generate key from password`, popis `Enter a password to create a SHA-256 communication key.`. Následuje label `Password`, prázdné editovatelné pole s cue bannerem `CHANGE-THIS-PASSWORD`, hint `The key updates automatically as you type.`, label `SHA-256 key`, read-only hash a OK napravo v patičce. Běžný text hesla je viditelný jako v mockupu, nevytváří se nový povinný maskovací režim.

Hash je prostý SHA-256 přes přesné UTF-8 bajty aktuálního hesla. Není to heslový KDF, salt ani iterovaný hash. Neprovádí se trim, změna velikosti písmen nebo Unicode normalizace. Prázdné pole je platný vstup a zobrazí `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`. Placeholder není hodnota a nehashuje se. Při neplatném UTF-16 vstupu nebo chybě crypto zneplatnit výstup a zakázat OK, nikdy nepřenést předchozí hash jako aktuální.

Vstup je omezen na nejvýše 255 Unicode skalárních hodnot, nikoli UTF-8 bajtů, UTF-16 jednotek nebo grafémů. Validní surrogate pair se počítá jednou, kombinující znak samostatně bez normalizace. Nejvýše jde o 1020 UTF-8 bajtů nebo 510 UTF-16 jednotek. Nativní limit nesmí zkrátit platných 255 non-BMP hodnot. `PasswordKeyModel::update` dostane UTF-16 s explicitní délkou a synchronně vypočítá SHA-256 přes přesné UTF-8. Nadlimitní editace se odmítne celá s viditelnou chybou, zachová předchozí vstup, výběr a platný výsledek. Nespárované surrogate, C0 U+0000 až U+001F, DEL/C1 U+007F až U+009F a oddělovače U+2028/U+2029 se odmítnou bez opravy či useknutí, výstup se zneplatní stejně jako při crypto chybě. Heslo model neuchovává. Pracovní UTF-8 a digest se mažou po použití i při odmítnutí. Hash včetně emoji, combining a všech hranic se ověří nezávislými vektory. Rozpočet rozpracovaných Key zůstává 524288 bajtů. Úplná kompozice a rozestupy zůstanou jako na mockupu, nikoli jen MessageBox s hashem.

Focus je v Password, Tab přejde na read-only hash a OK. Enter potvrdí jen aktuální platný hash. OK přenese hash do řádku zachyceného při otevření dialogu, označí koncept dirty a zavře dialog, nezmění Enabled. Escape/X zavře bez změny řádku. Rodič zůstává dostupný, běžné zavření dítěte vrátí focus na Password SHA stejného kontraktu. Při Save nebo ukončení relace se focus neobnovuje.

## UX12: Uložení, souběh a dirty-state kontraktů

ContractsSaveController rozlišuje loading, editing, invalid, saving, saveUnknown a closed. `ContractsModel` řeší řádky, blokující chyby, dirty, focus, generace a zmrazeného kandidáta, ne transportní lifecycle. Dirty zachytí změnu Enabled, editaci klíče, Random i potvrzený Password SHA. U běžné editace se může vrátit do clean při obnovení všech původních hodnot včetně unchecked Key, potvrzená generační akce však vyžádá potvrzení při zavření i u shodného výsledku. Otevření a prostý scroll dirty nenastaví.

OK během editing provede lokální kontrolu relevantních řádků, patch původního DOM, kontrolu velikosti a odešle právě jeden `set-config`. Pro enabled řádky vloží `{ "payloadKey": "…" }` pod remoteIp, unchecked odstraní. Zachová všechny nesouvisející členy, hodnoty, typy a jejich přítomnost, včetně vynechaného runtime, CIDR, TLS a metadat Wizardu. Výsledný dokument se nerekonstruuje z ClientConfig. Původně chybějící communicationContracts zůstane chybějící, pokud se nic nezapíná, existující objekt po vypnutí všeho zůstane `{}`. Whitespace lze při DOM editaci změnit.

Lokální část poskytuje `prepareSave`: úplný snímek potvrzených řádků předá preserving patchi, serializaci a sdílené plné validaci v limitu 512 KiB. Povinný synchronní `ContractsEnvelopeSizer` změří skutečnou zakódovanou obálku existujícím builderem bez přenosu a bez změny modelu. Nad 4 MiB se kandidát odmítne. Až úspěch vytvoří sdílený const kandidát, zvýší save generation, vyřadí dřívější cíle a zakáže mutace modelu. Chyba zachová koncept i generaci. `resumeEditing` vyžaduje korelovanou save generation a rozhodnutí řadiče, staré dětské cíle se tím neobnoví. `matchesDocument` vyžaduje validní aktuální dokument a porovná celý DOM nezávisle na pořadí objektových členů, se zachováním typů a absent/present. `ContractsModel` připravuje data. IPC, zavírání oken a Save/unknown workflow řídí `ContractsSaveController` ve spolupráci s nativním oknem.

Přísné současné schéma nepovoluje libovolné neznámé členy. Zachování DOM není povolením neznámých nebo nevalidních polí. Úplnou validaci opakuje služba před temp souborem, všechny publikace používají jediný ConfigStore a serializační bránu. Úspěch zavře okno. Neprovede Connect, Disconnect ani reload aktivního snímku. Změna se použije až při novém logickém připojení, nikoli při recovery současného.

Při aktivní VPN zobrazit `Changes take effect on the next connection.`. Souběžné relace zapisují celé dokumenty, poslední úspěšná publikace vyhraje. Starší revisionToken není konflikt a nevyvolává CAS, automatické merge, refresh ani nové potvrzení přepsání. Změna nebo delete/recreate cíle během otevření nezmění koncept. Pokud je publikace bezpečná, OK může obnovit smazaný soubor nebo nahradit mezitím změněný obsah, stejně jako Save editoru. Nepřebírá části novější konfigurace.

Při potvrzeném odmítnutí před publikací se vrátí editing/invalid s celým konceptem a bezpečnou příčinou. Chyba write/flush/publish nebo ztracená, chybná či nekorelovaná odpověď po možném odeslání je saveUnknown, pokud není prokázán konečný nepublikovaný výsledek. V saving se pole, OK i zavření uzamknou pouze po konečný rozpočet IPC. Po jeho uplynutí se zobrazí `The save result is unknown. The file may already have changed. Check the current configuration before saving again.`.

Stávající set-config nemá dohledatelný commit receipt. Editor jej proto nesmí vymýšlet z requestId nebo aktuální revize. V saveUnknown nabídne `Check current configuration`, což je výslovná jednorázová akce `get-config-document`. Porovná celý DOM aktuálního validního dokumentu s odeslaným kandidátem, včetně typů a přítomnosti členů. Shoda dovolí oznámit `The current configuration matches your changes.` a zavřít, není historickým potvrzením původního zápisu. Neshoda nebo missing zachová koncept, zobrazí `The current configuration differs. Your draft has been kept.` a nabídne buď zavření, nebo výslovné `Save draft again` s potvrzením nahrazení současného celého dokumentu. Tato výjimka platí jen pro řešení unknown, nikoli běžné souběžné ukládání. Nejde o automatický retry. Nečitelnost/ACL/encoding_error/too_large se nejprve řeší podle UX04, nepovolují slepou novou publikaci.

X/Escape v clean zavře přímo. V dirty zobrazí `Discard unsaved changes?`, Yes je zahodí, No/Escape/X potvrzení se vrátí. V saveUnknown je text `The file may already have changed. Close without checking the save result?`, výchozí No. Zavření nevrací již publikovaný soubor. Pozdní odpověď po zavření nebo nové generaci neotevře dialog a neprovede další zápis.

## UX13: Bezpečnost a limity

Tray nikdy neotevře, nezapíše ani netestuje existenci `config.json` přes filesystem. Dočasný JSON, heslo, klíče a DOM jsou pouze v paměti do schválené službové publikace. Všechny logy, chyby, testové reporty a screenshoty skutečných konfigurací musí být bez tajemství. Testy používají výhradně syntetické hodnoty. UI samo nic nekopíruje do clipboardu.

Vlastněné citlivé buffery adaptéru a modelu se best-effort přepisují přes secureWipe/SecureBuffer před uvolněním, také při chybě crypto a zrušení. Nativní EDIT obsah a undo se před zánikem vyprázdní, nelze zaručit přepsání interních kopií Windows. Pracovní UTF-8/UTF-16 řetězce, přijatá IPC těla, původní DOM i kandidát mají omezený životní cyklus a co nejméně kopií. Best-effort mazání vlastněné paměti není záruka odstranění všech kopií builderu, parseru, OS, alokátoru nebo uživatelem vytvořeného clipboardu. Heslo nikdy nejde přes IPC ani do konfigurace, pouze výsledný klíč v potvrzeném JSON.

Limit uložené konfigurace je 524288 bajtů a zakódované IPC zprávy 4194304 bajtů včetně escapingu a obálky. Kontrola proběhne před alokací podle nedůvěryhodné délky i před odesláním/publikací. Existující validátor, UTF-8 a JSON hloubkové/tokenové meze se neoslabují. Editor má nejvýše 252 nativních řádků podle globálního rozsahu `/24` až `/29` a součet rozpracovaných Key textů nejvýše 524288 bajtů. Překročení limitu odmítne novou editaci s vysvětlením a zachová dosavadní koncept. Neexistuje neomezená historie klíčů nebo bufferů.

Řádkové Key, srovnávací výchozí klíče a text neměnného kandidáta vlastní SecureBuffer. Model čistí vlastní původní DOM a kandidát čistí svou kopii DOM až po posledním uživateli. Zapůjčený vstup, nativní pole/undo a dočasnou IPC obálku uklízí jejich adaptér. Změna Key se kontroluje před alokací náhrady, s bezpečným odečtením staré délky a kontrolou součtu bez přetečení, odmítnutí nemění hodnotu ani focus. Lokální volání jsou sériová, synchronní použití v UI musí samostatně splnit runtime akceptaci v UX14.

Veřejná lokální pipe, zákaz vzdálených klientů, uzavřené schéma, korelace, konečné deadline a oddělená kapacita Disconnect/get-status zůstávají. V rámci tohoto UX plánu se nepřidává žádný nový IPC příkaz. Connect, Contracts i Wizard používají pouze existující příkazy. SHA-256 a CSPRNG jsou lokální volání stávající kryptografické abstrakce. Konfigurační volby nemění wire formát, AAD, HKDF, IV ani end-to-end kontrolu.

## UX14: Ověření a velikost

Následující přehled určuje účel existujících testů a akceptační povinnosti. Samotná existence filtru není PASS. Každý skutečný běh má datovaný report s příkazem, nenulovým počtem testů, prostředím a výsledky. Historické reporty zachovávají tehdejší PASS/FAIL/NOT RUN a nenahrazují nový běh ani níže definovaný kontrakt.

Contracts a Password SHA používají opt-in filtr WP05Windows v phpvpn-wp03-ui-tests, mimo CTest. Skutečné produkční třídy běží nad soukromým syntetickým IPC a falešným shell adaptérem. Testy ověřují ruční availability matici, 252 nativních řádků po čtyřech controls, /29, MSAA jména a neměnné systémové titulky Password SHA, tab order, Unicode a Key rozpočty, Random, více dětí, chyby vytvoření, whole-document Save, unknown Check a potvrzené opakování. Společný Exit se kontroluje přes snapshoty i při modal reentry a s původním 30s deadline. Session broadcast se smí synteticky poslat pouze na nově vytvořeném neviditelném desktopu s ověřenými vlastními okny, nikdy na uživatelské ploše. Native test neprovádí skutečný logoff. Na malé ploše patička zůstává dostupná, při nedostatku místa scrolluje také obsah nad ní. Výkonová brána po plném zahřátí input, paint a MSAA cest měří nejhorší z 20 cyklů, pořizování snímků je mimo měření. Testový TrayApplication po ukončení zpracuje vlastní WM_QUIT před dalším scénářem. Report odděluje Release měření od ASAN a uvádí přesné DPI, klientské plochy, dostupné důkazy a NOT RUN pro úplnou vizuální, systémovou či produkční runtime bránu.

Wizard se ověřuje nenulovými filtry wizard, Wizard, fix2 a probe v phpvpn-client-tests. Opt-in phpvpn-tray-ui-tests používá pouze vlastní syntetickou in-memory pipe a testuje gate, URL, secret, identity, contracts summary, metadata, Cancel/Reset, pozdní odpovědi, 15s heartbeat, focus a resize. Filtry wp04_ ověřují také skutečnou produkční akci TrayApplication, jeden souhlas s globálním Disconnectem a současné disconnected/idle, zachování HWND, výběru a undo a celé varování při výchozí ploše. Potvrzený i dohledaný Finish musí mít 1 Finish, 1 wizard-close, 0 dalších config read, 0 Connect a 0 automatických Contracts oken. Ztracená odpověď Finish vyžaduje výslovné Verify save a právě 1 wizard-get-commit. Syntetické captures vznikají jen při nastavení PHPVPN_WP04_CAPTURE_DIR do work/logs. Report uvádí skutečné DPI a klientské rozměry, nenahrazuje tím plnou vizuální a runtime akceptaci níže, skutečnou službu, multi-monitor, high contrast ani screen reader.

Connect, Edit Config a Statistics používají nenulové filtry WP03Models, WP03Windows, TrayProgress, TrayPolling, TrayLayout, TrayStatus, EditorDocument a StatusContract. Modely ověřují celý odpočet s falešným monotónním časem, trojici úspěchových podmínek, bezpečný katalog, rozdíl uložených detailů a aktivního výsledku, přesných devět uint64 a stale. Filtr WP03Windows patří výhradně do samostatného opt-in cíle phpvpn-wp03-ui-tests při PHPVPN_BUILD_UI_TESTS=ON, který není registrován v CTest. Nativní testy používají pouze náhodně pojmenovanou soukromou in-memory pipe bez produkčního fallbacku. Měří jedno get-config na otevření, místní get-status s maximem jednoho požadavku, zavření bez Disconnectu a čas od skutečného paint. Ověřují úzký scroll a focus, původní raw NUL bajty, dirty a Save generation, oddělené Show Info, 500 překreslení a 20 cyklů oken. Stávající opt-in editor_reset_failure_generation nadále ověřuje Reset No, chybu, Yes a pozdní odpověď. Čisté layout testy pokrývají 96/120/144/192 DPI. Skutečné DPI a rozměry nativních captures se uvádějí v reportu, syntetický screenshot se ukládá jen při výslovném nastavení PHPVPN_WP03_CAPTURE_DIR do work/logs. Tyto testy nenahrazují úplnou vizuální toleranci, multi-monitor přechod, screen reader, high contrast ani plnou runtime bránu níže.

Globální CIDR brána se ověří úzkými IPv4/config/repair/ConfigStore/set-config/Wizard Finish a Connect preflight testy. `/24` musí odvodit 252 peerů, `/29` čtyři, bez hardcoded síťového prefixu. Ověřit všechny podporované prefixy, nekanonickou síť, rezervované adresy a odmítnutí `/16` až `/23` i `/30` až `/32` před sítí a publikací. Samostatně ověřit oba rozpočty front na přesné hranici, o bajt pod ní a při přetečení. Cílová tabulka se testuje s nejvýše 252 nativními řádky bez virtualizace, skutečný JSON/Key rozpočet 512 KiB a limit IPC 4 MiB zůstávají samostatné. Unit důkaz globální validace není důkazem hotového GUI.

Přenosné modely mají samostatné filtry `ContractsModel.`, `ContractsDocument.` a `PasswordKey.` pro datový kontrakt a oddělení platformních závislostí. Povinné jsou 252/4 peery a hrany IPv4, self a numerické `.2/.10`, přesný `.2_.3` vektor i oba směry, aktivní/neaktivní chyba včetně posledního řádku, dirty/focus, Random a crypto failure, více cílů a staré generace, nezávislý neměnný kandidát, preserving DOM se všemi kombinacemi absence volitelných členů, limity a overflow. Password vektory zahrnou prázdný vstup, 254/255/256 ASCII i emoji, combining bez normalizace, vadné surrogate a celý řídicí rozsah UX11. Testy neotevírají okna, UI/accessibility ani runtime akceptace tím nejsou splněné. Linkerové mapy service/CLI ověří nepřitažení nepoužitých modelových objektů.

Úzké testy sdíleného Win32 základu mají nenulové filtry `TrayGeometry.`, `TrayShell.`, `TrayResources.`, `TrayControls.`, `TrayPlacement.`, `TrayReaders.` a `TrayApplicationSeam.`. Ověřují pořadí rodič/menu/foreground/kurzor/primární, záporné souřadnice, cílové DPI a rám, zachování geometrie, unplug/resolution model a minimální clamp. Shell ověří nulové NIM_MODIFY pro shodu a jednu publikaci na změnu, včetně neúspěšného volání. Resource test ověří vlastnictví, přesuny, 20 cache create/DPI/destroy cyklů a 500 kreslení karty po warmupu. Nativní control test zachová výběr a text po selhání Begin/Defer/EndDeferWindowPos. Tato evidence není plnou runtime ani vizuální akceptací všech oken. Následující metodika je závazná pro finální bránu.
Vizuální akceptace všech jedenácti mockupů se provede při 100 % DPI nad přesnou klientskou plochou bez systémového titulku a vnějšího rámu. Výchozí plochy jsou Connect 940 × 680 DIP, Edit Config 1080 × 560 DIP, Statistics 1000 × 650 DIP, Wizard 1000 × 680 DIP, Communication contracts 1180 × 680 DIP a Password SHA 540 × 360 DIP. Všechny stavové mockupy stejného okna mají stejnou plochu. Reference se přiřadí celé této klientské ploše a tento převod se zaznamená, nikoli libovolnému screenshotu včetně rámu.

Poloha a rozměry jednotlivého prvku mají toleranci nejvýše 6 DIP. Rozdíl způsobený skutečnými metrikami nativního fontu, zalomením či baseline textu má toleranci nejvýše 8 DIP, nesmí změnit pořadí, překrýt obsah, useknout významný text ani skrýt akci. Přítomnost sekcí, pořadí, exact-match texty, role, sémantické barvy, dostupnost patičky a povinné akce toleranci nemají. Titulek, rámeček, anti-aliasing a nativní kresba controls se neporovnávají pixel po pixelu. Přesný text systémového titulu Password SHA a jeho dostupnost čtečce obrazovky i přepínači aplikací se ověří samostatně podle UX11. Titulek podle UX11 nevyžaduje další obsahový prvek ani změnu referenční klientské plochy.

Při 125, 150 a 200 % DPI se ověří stejné DIP rozložení po škálování, se stejnými tolerancemi. Funkčně se ověří resize, scroll, focus, klávesnice, změna DPI mezi monitory, malá pracovní plocha a high contrast. Pixel-perfect shoda screenshotu není podmínkou. Každý řádek vizuálního reportu uvede mockup, Windows build, DPI, skutečnou klientskou plochu, použitou toleranci, povolenou odchylku a výsledek. Významové adaptace UX00/03/08/09 se uvedou výslovně. Screenshot ani unit test geometrie nedokládá funkční ovládání nebo bezpečnou publikaci.

##### Runtime akceptace

V optimalizovaném Release buildu se každé časové měření provede po jednom zahřívacím běhu nejméně třikrát a hodnotí se nejhorší běh. Report zaznamená konfiguraci kompilace, Windows build, CPU, RAM, DPI a použitý syntetický dokument, aby šel baseline a výsledek opakovat. Zaznamenají se jednotlivé běhy, baseline před změnou a delta, nikoli subjektivní hodnocení odezvy.

| Operace nebo zdroj | Cílová hodnota | Hranice selhání |
|---|---:|---:|
| Start tray do zobrazení ikony | do 500 ms | nad 2 s |
| Otevření tray menu | do 100 ms | nad 250 ms |
| Odezva při psaní včetně SHA-256 | do 16 ms | nad 100 ms |
| Scroll, resize a překreslení | do 33 ms | nad 100 ms |
| Otevření Contracts pro 252 řádků | do 500 ms | nad 1 s |
| Lokální patch, serializace a validace po OK | do 1 s | nad 2 s |
| Reakce okna během čekání na službu | do 100 ms | nad 250 ms |
| CPU tray při klidovém pětisekundovém pollingu | průměr do 1 % | nad 2 % |
| Paměť navíc pro Contracts, 252 řádků | do 5 MiB | nad 10 MiB |
| GDI/USER objekty Contracts | všechny řádky, nejvýše +1200 | nad +1200 |
| Dvacet cyklů otevření a zavření | bez trvalého růstu | více než +5 objektů nebo +1 MiB |

Dvě sekundy jsou absolutní strop jednorázové lokální operace, nikoli povolená odezva každého znaku nebo scrollu. Samotné IPC čekání se do tohoto stropu nepočítá, reakce UI během čekání se měří samostatně a Disconnect zůstává funkční. Překročení cíle se uvede a vysvětlí, překročení hranice selhání neprojde akceptací. Pokud synchronní lokální cesta hranice nesplní, musí být optimalizována nebo přesunuta do omezené worker úlohy s generací, bez neomezené fronty a bez oslabení limitů.

Měřit procesní working set a GDI/USER počty vůči ustálenému tray bez Contracts. Paměť a objekty po zavření porovnat s týmž stavem. CPU je procesní průměr při základním pětisekundovém pollingu, s uvedenou délkou vzorkování a normalizací vůči celkové kapacitě CPU. Čas otevření Contracts odděleně zaznamená IPC načtení a místní přípravu plně ovladatelného okna. Scroll/typing a reakce při čekání se měří od vstupu do vykreslené odpovědi.

Nejméně 500 překreslení bez změny DPI nesmí po ustálení vykázat růst GDI/USER objektů. Dvacet cyklů otevření, změny DPI a zavření musí splnit stejný limit +5 trvale zbývajících objektů a +1 MiB. Fake nebo instrumentovaný shell adapter pro shodnou ikonu i tooltip ověří nula NIM_MODIFY, pro každou skutečnou výslednou změnu právě jedno volání. Nepřidává se další polling, knihovna ani framework.

Velikostní report doplní velikosti před/po a absolutní i procentní deltu celého EXE i samostatných sekcí .text, .rdata a .rsrc. Porovná stejnou architekturu, konfiguraci a stav podpisu, PDB nepočítá do EXE. Původní historické velikosti nejsou náhradou nového baseline.

Izolovaný vývojový runner client/tools/dev-tray.ps1 je implementovaný, příkazy a přesnou View/Scene tabulku uvádí client/tests/ui/README.md a klientská část Izolovaný vývoj a náhled GUI. Výchozí Preview otevírá skutečné Contracts s 252 řádky, Test vyžaduje nenulový filtr a Build nic nespouští. Debug a skutečný Release používají oddělené Ninja cache work/build/tray-dev-debug a work/build/tray-dev-release, produkční release preset zůstává RelWithDebInfo. Chybná kombinace parametrů se odmítá před buildem. Cizí cache se zachová a odmítne, chyba konfigurace/build/AMD64 PE nikdy nespustí staré EXE. Logy a časy vznikají pouze v jedinečném work/logs/tray-dev adresáři bez tajemství.

Opt-in phpvpn-tray-ui-tests zůstává mimo CTest a produkční linkage. Všechny preview akce používají skutečný TrayApplication, společné zdroje, resources a manifest nad syntetickou pamětí, privátní UUID pipe a mutexem. Jen View Tray publikuje skutečnou ikonu s tooltipem PHPVPN GUI preview - synthetic data. Neexistuje parametr libovolného endpointu ani fallback na službu. Launcher potlačuje konzoli pomocí CreateNoWindow bez předání SW_HIDE prvnímu ShowWindow. Skutečná produkční okna zůstávají viditelná. Explicitní test-hidden-launch.ps1 ověřuje editorový Reset v Debug i Release, skryté spuštění runneru z jiné cwd a cesty s mezerami, focus, nepřítomnost konzole, úklid procesu a chybové vstupy. Preview trvá do zavření, Password do zavření Contracts a Tray do Exit. Connected používá původní skutečný odpočet, Saving původní 30s deadline, SaveUnknown skutečné Check a potvrzené opakování. Filtry wp06_ ověřují 20 scén, delší než 45s náhled, odpočet, ukládání, děti a úklid privátního endpointu. Legacy Wizard --preview zůstává časově omezený a jeho contracts není samostatný editor.

[Historický audit uložený 8. září 2026](client/tests/implementation-plan3-fix3-wp07.md) zachovává také dřívější výsledky runneru a native testů odděleně od plné vizuální a runtime akceptace. Syntetický backend sdílí proces, případná CPU/paměťová měření jsou proxy a nenahrazují produkční baseline ani fyzické monitor/DPI, high contrast nebo screen-reader ověření. Vývojový výstup zůstává ve work/build bez kopie do dist. Rychlé ověření není vydáním ani důkazem reálné služby, instalace nebo VPN. Plné vydání se nadále řídí distribution.md.
