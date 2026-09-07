# Kritická revize `plan3_fix3.md` a `tray_UX.md`

## Rozsah a způsob revize

Tato revize byla provedena pouze read-only nad stavem projektu před vytvořením tohoto reportu. Ověřené názvy posuzovaných dokumentů jsou `C:\git\phpvpn\plan3_fix3.md` a `C:\git\phpvpn\tray_UX.md`, nikoli varianty `plan_3_fix_3.md` a `tray_ux.md`.

Byly přečteny celé oba posuzované dokumenty, celý `protocol.md`, celý `client.md`, `distribution.md`, relevantní zadání a poznámky, související zdrojové soubory tray aplikace, IPC, Wizardu, konfigurace, CMake a existující implementační reporty. Vizuálně bylo porovnáno všech 11 relevantních mockupů v `C:\git\phpvpn\assets`. Soubor `logo.png` není samostatný UX mockup a do počtu 11 není zahrnut.

Při revizi nebyl proveden build ani spuštěno GUI. Hodnocení implementační věrnosti, výkonu budoucího kódu a výsledné velikosti proto všude rozlišuje ověřený fakt od kvalifikovaného odhadu.

## Celkový verdikt

Plán zatím není připravený k bezpečné implementaci bez domýšlení. Vizuální a doménová část je nadprůměrně konkrétní, zachovává `protocol.md`, současný wire protokol, bezpečnostní hranice i záměr lehké nativní Win32 aplikace. Dva rozpory je ale nutné vyřešit před implementací:

1. sjednotit přesný tvar odpovědi nového progress IPC,
2. do závěrečné brány zahrnout sestavení a cílené ověření povinné službové části změny.

Vedle toho je nutné přesněji vymezit práci UI vlákna, stanovit měřitelné runtime limity, koordinovat souběžná konfigurační okna, rozhodnout rozsah přístupnosti virtualizované tabulky, umístit přenosné modely mimo Windows tray vrstvu a sjednotit normativní UI texty.

## Blokující problémy

### 1. Nový progress IPC má dva vzájemně neslučitelné tvary odpovědi

**Důkaz:** `plan3_fix3.md:109-120` předepisuje top-level odpověď `{ipcVersion, requestId, status, connectionDetails}`. Dosavadní obecné pravidlo pro nové IPC naopak říká `{ipcVersion, requestId, result}` v `client.md:513-517`. Plán u N05 toto obecné pravidlo výslovně nenahrazuje, zatímco N06 standardní obálku `result` výslovně zachovává v `plan3_fix3.md:132`.

**Dopad:** service builder, tray parser a fuzz testy mohou implementovat různé uzavřené schema. Výsledkem by byla nekompatibilita přesně v cestě, která má aktualizovat Connect progress každých 500 ms.

**Lehká náprava:** zvolit jediný tvar. Nejméně změn vyžaduje výslovně prohlásit `get-connection-progress` za get-status-like výjimku se čtyřmi top-level členy a upravit odpovídající obecnou větu v `client.md`. Alternativou je vložit `status` a `connectionDetails` pod `result`. Přidat jeden přesný request/response JSON příklad a test přesného počtu i názvů členů.

### 2. Povinná službová polovina změny se v závěrečné bráně ani nesestaví

**Důkaz:** F03 a F04 mění IPC dispatcher, service host, stavový engine a `WizardService` v `plan3_fix3.md:203-213`. Závěr ale sestaví jen `phpvpn-tray` a testy v `plan3_fix3.md:298-300` a kopíruje pouze tray EXE v `plan3_fix3.md:302-316`. Projekt výslovně vyžaduje společnou aktualizaci služby a tray v `client.md:831`. Plán sám přiznává nekompatibilitu staré služby v `plan3_fix3.md:181` a `plan3_fix3.md:287`. Hash manifestu by po samostatné výměně tray přestal odpovídat, viz `distribution.md:56-60` a `plan3_fix3.md:316`.

**Dopad:** service-specific compile nebo link chyba může projít závěrečnou bránou. Nový tray uložený do `dist` nebude se starou službou podporovat ani jeden nový příkaz a adresář může působit jako funkční distribuce, přestože jí není.

**Lehká náprava:** do rychlé brány přidat alespoň build cíle `phpvpn-service` bez kopírování nebo nasazení a component test obou nových dispatch cest. Kopii samotného tray jednoznačně označit jako neinstalovatelný vývojový artefakt, případně ji vůbec nedávat do `dist`. Funkční balíček vyžaduje samostatně povolenou službu a nový podepsaný manifest.

## Významná rizika

### 3. Není určeno, co smí běžet na UI vlákně při nejdražších operacích

**Důkaz:** editor připouští 4092 řádků v `tray_UX.md:263-265`, vytváří defaultní SHA-256 a celý model podle `plan3_fix3.md:195-201` a `plan3_fix3.md:217-219`, před Save patchuje, serializuje a validuje až 512KiB DOM podle `plan3_fix3.md:225-231`. Password SHA může při každé změně synchronně hashovat až 524288 UTF-8 bajtů podle `plan3_fix3.md:199` a `tray_UX.md:283-287`. F02 zmiňuje background variantu jen pro heslo a až po subjektivně „nevyhovující“ odezvě.

**Dopad:** kliknutí na Contracts, vložení velkého hesla nebo OK může zablokovat message loop, status, paint, focus i nezávislý Disconnect. Jde o odhad rizika, ne o prokázanou regresi, protože budoucí implementace nebyla sestavena ani benchmarkována.

**Lehká náprava:** výslovně rozdělit fáze. Na UI vlákně ponechat jen časově omezenou mutaci modelu a aplikaci hotového výsledku. Parse, generování 4092 hashů, úplnou validaci a serializaci provést jako jednu zrušitelnou worker úlohu s generation ID. Alternativně nejprve stanovit měřenou p95 hranici a synchronní cestu ponechat pouze tam, kde ji například běžný `/24` prokazatelně splní.

### 4. Chybějí měřitelné runtime akceptační hranice

**Důkaz:** plán správně omezuje počet HWND a zakazuje vytváření GDI zdrojů při každém paintu v `plan3_fix3.md:187-191` a `plan3_fix3.md:217-219`. A19 ale měří jen počet HWND a neurčitou „paměť v reportu“ v `plan3_fix3.md:277`. Chybí limit pro cold start, otevření menu, otevření Contracts pro `/24` a `/20`, odezvu při psaní, Save, CPU během 500ms pollingu, working-set delta i GDI/USER handles.

**Dopad:** akceptační stav „funguje“ může projít s viditelnými záseky, opakovaným překreslováním nebo výraznou rezidentní pamětí.

**Lehká náprava:** přidat reprodukovatelná měření a srovnání baseline před/po. Minimum je start-to-tray, menu p95, Contracts-ready pro `/24` a `/20`, typing p95, OK-to-request, idle CPU s otevřeným progress oknem, working set a počet GDI/USER handles po 10 cyklech open/close, DPI a resize.

### 5. Modalita a opakované otevření nejsou úplně definované pro souběh místních oken

**Důkaz:** Contracts má blokovat lokální Wizard nebo editaci a opakované otevření má aktivovat existující formulář podle `tray_UX.md:94-96` a `plan3_fix3.md:217`. Není ale rozhodnuto, co se stane, když už je otevřený dirty Edit Config, Wizard ve stavu checking nebo committing, případně `saveUnknown`. Není ani určeno, co udělá Exit nebo logoff během `saving`. Tray menu zůstává dostupné a Exit vždy existuje v `client/apps/tray/main.cpp:154-170`.

**Dopad:** dva plné koncepty mohou soutěžit, owner může zůstat chybně disabled nebo proces skončí u nejasného zápisu bez pravdivé zprávy.

**Lehká náprava:** doplnit jednotný per-tray `ConfigurationUiCoordinator` a tabulku `open / activate / refuse` pro Wizard, Edit Config, Contracts a child dialog. Pro Exit a logoff definovat konečné cancel/wait chování a pravdivý `saveUnknown` text, nikdy příslib rollbacku, který nelze zaručit.

### 6. Virtualizace a přístupnost mají nedefinovaný kontrakt

**Důkaz:** UX vyžaduje role, hodnoty, chyby a informace pro screen reader v `tray_UX.md:90-92`. Současně existují HWND pouze pro viewport a nejvýše jeden rezervní řádek na každé straně podle `tray_UX.md:257-261`.

**Dopad:** není rozhodnuto, zda čtečka musí enumerovat všech 4092 off-screen položek přes UI Automation virtualized-item provider, nebo zda stačí právě materializované prvky s keyboard reveal. První varianta je podstatně větší implementace a může zvýšit velikost binárky. Druhá varianta nemusí splnit očekávanou úroveň přístupnosti.

**Lehká náprava:** zvolit jednu úroveň jako normativní. Pro lehké řešení použít nativní přístupné controls jen pro materializované řádky, programový scroll/reveal při Tab a přístupný souhrn počtu řádků a chyb, bez vlastního plného UIA provideru.

### 7. „Přenosné“ prezentační modely jsou plánované do Windows tray cíle

**Důkaz:** `client.md:9` požaduje přenosné modely bez závislosti na tray procesu a `tray_UX.md:73` slibuje sdílení modelů. F02 ale umísťuje `contracts_model.*` a `password_key_model.*` pod `client/apps/tray` v `plan3_fix3.md:193-199`.

**Dopad:** budoucí macOS nebo Linux pohled by závisel na Windows app vrstvě, případně by model duplikoval.

**Lehká náprava:** přenést čisté modely do malé core/presentation knihovny. Win32 adapter, layout, HWND a Windows message handling ponechat v `apps/tray`.

### 8. Normativní copy se mezi dokumenty rozchází

**Důkaz:** aktivní chyba je v plánu množná `Keys must contain exactly...` v `plan3_fix3.md:73`, zatímco UX vyžaduje jednotné `Key must contain exactly...` v `tray_UX.md:279`. Random potvrzení je v plánu obecné `this contract` v `plan3_fix3.md:75`, zatímco UX vyžaduje konkrétní `{contractId}` v `tray_UX.md:281`.

**Dopad:** snapshot/UI testy a implementace nemají jedinou autoritu.

**Lehká náprava:** sjednotit dokumenty na UX znění s `contractId` a výslovně určit, které texty jsou exact-match kontrakt.

### 9. Definice „aktivního monitoru“ je nejednoznačná

**Důkaz:** UX požaduje aktivní monitor a podporu záporných souřadnic v `tray_UX.md:77-90`. Současný helper vybírá monitor podle kurzoru v `client/apps/tray/window_utils.h:37-43`.

**Dopad:** cursor monitor funguje při kliknutí na tray, ale neřeší keyboard invocation, ztrátu focusu ani znovuaktivaci okna po změně konfigurace monitorů.

**Lehká náprava:** určit pořadí owner monitor → foreground monitor → cursor monitor. Dále určit, zda reaktivace existujícího okna pouze aktivuje, nebo také znovu omezí jeho polohu do aktuální work area.

## Menší připomínky

### 10. Zbytečné překreslení a práce s ikonou nejsou přímo v akceptační matici

**Důkaz:** dnešní progress paint při každém paintu znovu načte ikonu a vytvoří tři fonty v `client/apps/tray/progress_window.cpp:92-143`. F01 správně požaduje cache v `plan3_fix3.md:187-191`. Běžný tray ale navíc volá `Shell_NotifyIcon(NIM_MODIFY)` po každém snapshotu i beze změny v `client/apps/tray/main.cpp:204-221`.

**Dopad:** samostatně jde nejspíš o malou režii, ale 2Hz progress spolu s dalšími painty může zbytečně vytvářet GDI objekty a zatěžovat shell.

**Lehká náprava:** cache per-DPI zdrojů, invalidovat jen změněné regiony, volat `NIM_MODIFY` jen při skutečné změně ikony nebo tooltipu a přidat handle-leak test.

### 11. Angličtina je fakticky jediná lokalizace, ale není označená jako non-goal

`tray_UX.md:5` a `client.md:595` stanovují anglické texty. Layout správně počítá s měřením delších labelů v `tray_UX.md:57` a `tray_UX.md:88`. Jde o konzistentní rozhodnutí, nikoli rozpor. Pro budoucí očekávání stačí výslovně dodat `localization is out of scope. No resource catalog ABI is promised.` Čísla ve Statistics jsou záměrně anglicky seskupovaná podle `tray_UX.md:176`.

### 12. Vizuální akceptace nemá toleranci ani přesný viewport

UX14 hodnotí hierarchii a rozestupy v `tray_UX.md:317-321`, ale „stejná velikost klientské plochy“ není svázána s konkrétní dvojicí DIP/PNG ani tolerancí.

**Lehká náprava:** pro každý mockup určit cílovou výchozí DIP plochu, testované DPI, povolené odchylky nativního titulku a font metrics a toleranci geometrie 2-4 DIP. Pixel-perfect test není nutný.

## Věrnost všem 11 mockupům

Všechny uvedené reference existují v `C:\git\phpvpn\assets` a bylo možné je jednoznačně porovnat. Dokument odpovídá jejich hlavní hierarchii, kartám, patičkám a akcím podle přehledu v `tray_UX.md:9-20` a detailních kapitol UX05 až UX11.

1. **`communication_contracts1.png`:** odpovídá hlavnímu oknu Communication contracts, hlavičce s vlastní IP, tabulce `Contract / Enabled / Key / Actions`, fixní spodní chybě a primárnímu OK. Plán správně doplňuje numerické řazení, identitu řádku podle `remoteIp`, virtualizaci a chování při malé šířce v `tray_UX.md:249-265`.
2. **`communication_contracts2.png`:** odpovídá vnořenému Password SHA dialogu a práci s konkrétním kontraktem. Dokument správně zachovává modalitu vůči rodiči, focus a citlivé buffery v `tray_UX.md:96` a `tray_UX.md:283-287`. Nevyřešená zůstává exact copy mezi plánem a UX, viz nález 8.
3. **`connect1.png`:** odpovídá stavu Connecting, třem pending kontrolám, oranžovým indikátorům a typografické kompozici bez velkého štítu. Přesné chování je popsáno v `tray_UX.md:126`.
4. **`connect2.png`:** odpovídá úspěšnému Connect stavu s URL, IP a třísekundovým odpočtem. Dokument správně vymezuje monotónní deadline +3000 ms, zaokrouhlení nahoru a zavření nejpozději o jeden 500ms tick později v `tray_UX.md:130`. URL/IP jsou omezeny na dvě určená pole podle `tray_UX.md:118`.
5. **`editconfig.png`:** odpovídá nativnímu multiline editoru, konfiguraci cesty, scrollbars a patičce Reset/Cancel/Save. Plán záměrně nahrazuje ukázkovou cestu skutečným `configPath` služby a zachovává read-only Show Info variantu v `tray_UX.md:150-158`.
6. **`statistics.png`:** odpovídá kartám se statistikami a dvousloupcovému IPv4 traffic rozložení. Dokument záměrně používá přesná celočíselná B čísla, anglické seskupení tisíců a přesnost až `UINT64_MAX` v `tray_UX.md:176-180`. Text používá „current connection“ místo nejasného „session“, což je přiznaná bezpečná odchylka od mockupu v `tray_UX.md:162`.
7. **`wizzard0.png`:** odpovídá úvodní obrazovce Wizardu, sidebaru `Configure this computer`, čtyřem krokům a patičce. Dokument výslovně doplňuje Cancel a zachovává bezpečný lifecycle, i když mockup není úplný, viz `tray_UX.md:184-190` a sekce kroku 0 od `tray_UX.md:209`.
8. **`wizzard1.png`:** odpovídá kroku Server URL s polem, hintem, příkladem a vysvětlením HTTP/HTTPS. Dokument stanovuje přesný obsah a ověřovací akci v `tray_UX.md:215-219`.
9. **`wizzard2.png`:** odpovídá kroku Server secret se switchem pod polem. Dokument podrobně zachovává default/literal hodnotu, nulové znaky, JSON string literal režim i žádnou Unicode normalizaci v `tray_UX.md:221-225`.
10. **`wizzard3.png`:** odpovídá kroku Virtual IP and access key. Dokument doplňuje zásadní permanentně viditelný warning, který v mockupu chybí, protože každý ověřovací pokus může trvale odstranit čekající zprávy. Jde o úmyslnou bezpečnostní odchylku vyžadovanou `protocol.md`, popsanou v `tray_UX.md:227-235`.
11. **`wizzard4.png`:** odpovídá Communication contracts summary s přepínačem a třemi sloupci. Dokument zachovává Finish, úplnou validaci, jedinou atomickou publikaci a následné otevření Contracts z přesného potvrzeného dokumentu podle `tray_UX.md:239-245`.

Další společné přiznané odchylky jsou podtržítko místo pomlčky a nativní title bar v `tray_UX.md:20`, skutečná konfigurační cesta v `tray_UX.md:150`, význam „current connection“ v `tray_UX.md:162` a bezpečnostní warning či Cancel ve Wizardu v `tray_UX.md:211-235`.

**Omezení revize:** skutečnou renderovanou věrnost nelze ověřit, protože implementace popsaného redesignu neexistuje a při této read-only revizi nebylo GUI sestaveno ani spuštěno. Dokumentační věrnost je vysoká, ale musí ji potvrdit runtime screenshoty při přesně stanoveném viewportu a DPI.

## Runtime overhead

### Prokázané vlastnosti návrhu

- Start tray nadále nemá číst konfiguraci. Načte tři existující ikony, založí 5s timer a provede první `get-status`, viz `tray_UX.md:100` a současný `client/apps/tray/main.cpp:94-119`.
- Menu je nativní a bez konfiguračního I/O do kliknutí podle `tray_UX.md:100-102`.
- Při otevřeném Connect poběží 500ms progress reader plus nezávislý 5s reader. To znamená nominálně 2,2 krátkého paměťového IPC requestu za sekundu, nejvýše jeden rozpracovaný request na reader, viz `plan3_fix3.md:122` a `plan3_fix3.md:275`.
- Statistics nepřidává vlastní timer podle `tray_UX.md:178-180`.
- Contracts drží HWND jen pro viewport a nejvýše jeden rezervní řádek na každé straně podle `plan3_fix3.md:217-219`, nikoli pro všech 252 nebo 4092 řádků.
- Stávající maximální velikost jedné IPC zprávy je 4 MiB. Tato dřívější transportní hranice se návrhem nemění a není problémem této revize.
- Confirmed Wizard handoff nově může držet nejvýše 512 KiB publikovaného dokumentu na jednu živou relaci, tedy nejvýše 4 MiB při osmi relacích služby podle `plan3_fix3.md:136`. Jde o omezené dočasné buffery v RAM vázané na živé Wizard relace, nikoli o zvětšení IPC limitu, EXE nebo trvalých dat na disku. Tento maximální nárůst je přijatelný a není klasifikován jako problém ani blokace. Implementace pouze musí minimalizovat krátkodobé kopie citlivého obsahu a uvolnit buffery ve všech close, expiry a error cestách podle `plan3_fix3.md:211`.

### Odhad, nikoli měření

- Idle start a otevření menu budou pravděpodobně prakticky beze změny.
- 2Hz malé paměťové IPC a GDI paint budou pravděpodobně představovat nízké CPU náklady, pokud se repaint provede jen při změně snapshotu nebo countdownu.
- Otevřený Contracts pro `/24` bude pravděpodobně stát nízké jednotky MiB.
- `/20` může kvůli UTF-16 Key textům, původnímu DOM/textu, kandidátu, EDIT undo a resource cache stát několik až nižší desítky MiB. Orientační pracovní odhad je 5-15 MiB. Nejde o naměřenou hodnotu.
- 4092 krátkých CNG SHA volání a úplné DOM operace mohou zabrat desítky milisekund. Bez benchmarku to nelze tvrdit jako fakt.
- Největší riziko interaktivní odezvy není 4MiB handoff buffer. Je jím synchronní 512KiB password hash a UTF převod nebo sestavení a uložení `/20` modelu na UI vlákně.

## Baseline a dopad na velikost artefaktu

### Ověřený baseline

Aktuální `dist/client/Windows/x64/phpvpn-tray.exe` byl při revizi read-only změřen:

- velikost: **650552 B**,
- SHA-256: `C736D74DFDA1027D4CC166570B7AD92F0D8FE37E8EC5D765E72F4025DDA153C5`,
- PE architektura: AMD64 `0x8664`,
- `.text`: **333824 B**,
- `.rdata`: **236544 B**,
- `.rsrc`: **55808 B**.

Hodnoty potvrzují historický baseline v `plan3_fix3.md:303-313`. Tři existující ICO mají dohromady **51753 B** a jsou již zahrnuty v resources podle `client/apps/tray/tray.rc:3-5`.

### Prokázané zdroje růstu a zachované limity

- Návrh nepřidává Qt, Electron, WebView ani jinou třetí stranu podle `tray_UX.md:71-73`.
- Mockup PNG se do EXE nevkládají.
- Segoe UI a Consolas jsou systémové fonty, nikoli embedované assets, viz `tray_UX.md:24`.
- Nové bajty budou především v `.text` a `.rdata`: Win32 layout/rendering, Contracts a Password model, IPC parser/builder a anglické stringy.
- `phpvpn-core` a `phpvpn-platform` jsou statické knihovny podle `client/src/CMakeLists.txt:1-74`, linker ale přitáhne jen použité objekty.
- Nových maximálně 4 MiB handoff bufferů v RAM nezvětšuje binárku ani distribuovaný artefakt.

### Odhad velikostního dopadu

Bez sestavení nelze poctivě určit přesnou deltu. Rozumný řád nárůstu tray EXE jsou desítky až nižší stovky KiB. Jde pouze o kvalifikovaný odhad podle rozsahu nového nativního kódu a stringů. Rizikem násobného růstu by byl vlastní plný UIA provider nebo omylem embedované PNG či fonty, nikoli současný základ návrhu.

Požadované before/after měření v UX14 je správné. Report má navíc uvádět samostatnou deltu `.text`, `.rdata` a `.rsrc`, absolutně i procentně, při stejné architektuře, konfiguraci a stavu podpisu. Tím se rychle odhalí nechtěný asset nebo framework růst.

## Otevřené otázky

1. Má `get-connection-progress` top-level `status` a `connectionDetails`, nebo standardní `result`?
2. Je kopie samotného tray do `dist` zamýšlená jen jako neinstalovatelný vývojový artefakt?
3. Které `/20` modelové a save kroky musí být off-thread a jaká je jejich měřená hranice odezvy?
4. Jak se arbitrují Wizard, Edit Config, Contracts, jejich `dirty`, `checking`, `committing`, `saving`, `saveUnknown` stavy a Exit nebo logoff?
5. Má screen reader enumerovat i virtualizované off-screen řádky?
6. Je angličtina definitivní non-goal pro tuto verzi?
7. Je normativní fallback monitoru owner → foreground → cursor a má reaktivace okno znovu clampovat?
8. Jaké přesné DIP viewporty, DPI a geometrické tolerance budou použity pro vizuální akceptaci všech 11 mockupů?

## Minimální nutné změny před implementací

1. Sjednotit progress IPC obálku a doplnit přesný JSON příklad a schema test.
2. Přidat build a cílené component ověření služby. Pravdivě vymezit samostatnou tray kopii jako nefunkční dev artefakt, nebo ji nevkládat do `dist`.
3. Určit hranici UI thread versus worker pro otevření `/20`, generování hashů, Password SHA, úplnou validaci a serializaci. Přidat generation ID a zrušení tam, kde práce běží asynchronně.
4. Doplnit měřitelné limity a baseline/delta pro start, menu, Contracts `/24` a `/20`, typing, Save, idle CPU, working set a GDI/USER handles.
5. Definovat centrální koordinaci Wizard, Edit Config, Contracts, child dialogu a Exit/logoff včetně `saveUnknown`.
6. Rozhodnout konkrétní přístupnost virtualizovaných řádků. Pokud má řešení zůstat lehké, výslovně nepožadovat vlastní plný UIA provider.
7. Přesunout čisté přenosné modely mimo `client/apps/tray`. Ve Windows vrstvě ponechat pouze adapter a nativní UI.
8. Sjednotit exact UI copy, zejména `Key must contain exactly...` a potvrzení s `{contractId}`.
9. Definovat výběr aktivního monitoru a reaktivaci existujícího okna.
10. Pro všech 11 mockupů stanovit výchozí DIP viewport, testované DPI a toleranci geometrie 2-4 DIP.
11. Do velikostního reportu přidat absolutní i procentní deltu celého EXE a sekcí `.text`, `.rdata`, `.rsrc`.

Po těchto změnách je plán implementovatelný bez změny VPN wire protokolu, bez nové velké závislosti a s realistickou šancí zachovat lehký tray proces i vysokou věrnost mockupům.
