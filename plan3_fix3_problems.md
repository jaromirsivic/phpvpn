# Kritická revize `plan3_fix3.md` a `tray_UX.md`

## Rozsah a způsob revize

Tato revize byla provedena pouze read-only nad stavem projektu před vytvořením tohoto reportu. Ověřené názvy posuzovaných dokumentů jsou `C:\git\phpvpn\plan3_fix3.md` a `C:\git\phpvpn\tray_UX.md`, nikoli varianty `plan_3_fix_3.md` a `tray_ux.md`.

Byly přečteny celé oba posuzované dokumenty, celý `protocol.md`, celý `client.md`, `distribution.md`, relevantní zadání a poznámky, související zdrojové soubory tray aplikace, IPC, Wizardu, konfigurace, CMake a existující implementační reporty. Vizuálně bylo porovnáno všech 11 relevantních mockupů v `C:\git\phpvpn\assets`. Soubor `logo.png` není samostatný UX mockup a do počtu 11 není zahrnut.

Při revizi nebyl proveden build ani spuštěno GUI. Hodnocení implementační věrnosti, výkonu budoucího kódu a výsledné velikosti proto všude rozlišuje ověřený fakt od kvalifikovaného odhadu.

## Celkový verdikt

Plán zatím není připravený k bezpečné implementaci bez domýšlení kvůli významným rizikům uvedeným níže. Vizuální a doménová část je nadprůměrně konkrétní, zachovává `protocol.md`, současný wire protokol, bezpečnostní hranice i záměr lehké nativní Win32 aplikace. Oba původní blokující problémy jsou nyní rozhodnuté odstraněním obou navrhovaných nových IPC příkazů. V rámci tohoto UX plánu se nezavádí žádný nový IPC příkaz a původní službová build blokace tím odpadá.

Rozhodnutí bodů 3 až 8 již vymezila práci UI vlákna, měřitelné runtime limity, souběh konfiguračních oken, odstranění virtualizace zpřísněním `vpnCidr` na `/24` až `/29`, umístění přenosných modelů mimo Windows tray vrstvu a normativní UI texty. Zbývá rozhodnout Exit/logoff během nejasného zápisu.

## Blokující problémy a stav řešení

### 1. Nový progress IPC má dva vzájemně neslučitelné tvary odpovědi - rozhodnuto

**Původní auditní důkaz:** `plan3_fix3.md:109-120` předepisuje top-level odpověď `{ipcVersion, requestId, status, connectionDetails}`. Dosavadní obecné pravidlo pro nové IPC naopak říká `{ipcVersion, requestId, result}` v `client.md:513-517`. Plán u N05 toto obecné pravidlo výslovně nenahrazuje, zatímco N06 standardní obálku `result` výslovně zachovává v `plan3_fix3.md:132`.

**Původní auditní dopad:** service builder, tray parser a fuzz testy mohou implementovat různé uzavřené schema. Výsledkem by byla nekompatibilita přesně v cestě, která měla aktualizovat Connect progress každých 500 ms.

**Původně navržená lehká náprava:** zvolit jediný tvar. Nejméně změn vyžaduje výslovně prohlásit `get-connection-progress` za get-status-like výjimku se čtyřmi top-level členy a upravit odpovídající obecnou větu v `client.md`. Alternativou je vložit `status` a `connectionDetails` pod `result`. Přidat jeden přesný request/response JSON příklad a test přesného počtu i názvů členů.

#### Rozhodnutí

Tento původní auditní nález je vyřešen následujícím schváleným rozhodnutím:

- Nový IPC příkaz `get-connection-progress` se nebude zavádět a má se odstranit z návrhu a implementačního plánu.
- Po otevření Connect okna tray jednorázově zavolá existující `get-config`. Z platného konfiguračního JSON získá pouze zobrazované hodnoty `serverUrl` a lokální `virtualIp`. Celý přijatý config nebude logovat.
- Existující `get-status` zůstane schématem i významem beze změny. Nadále bude zajišťovat tři checkpointy a průběžný stav pomocí současného pollingu.
- Je akceptováno, že `get-config` přenese do tray celý platný config včetně citlivých hodnot.
- Je přijat tento trade-off: `get-config` čte aktuálně uložený dokument, zatímco Connect používá vlastní aktivní snapshot. Při souběžné externí změně konfigurace proto může jednorázově zobrazená URL nebo IP patřit novější uložené konfiguraci než právě aktivnímu spojení. Toto riziko je přijato za účelem zachování stávajícího IPC.
- Tím odpadá rozpor obálky nového progress příkazu i službová a parsovací práce specifická pro `get-connection-progress`.
- V okamžiku tohoto prvního rozhodnutí zůstával druhý navrhovaný příkaz pro Wizard handoff v rozsahu. Následné schválené rozhodnutí u problému 2 jej rovněž odstranilo. Tato věta zachycuje pořadí rozhodnutí, nikoli aktuální požadavek.

Původní odstavec **Lehká náprava** výše zůstává zachovaný pouze jako historie auditu. Nahrazuje jej toto schválené rozhodnutí a problém číslo 1 již není otevřený ani blokující.

### 2. Povinná službová polovina změny se v závěrečné bráně ani nesestaví - rozhodnuto

**Původní auditní důkaz:** před schválenými rozhodnutími F03 a F04 měnily IPC dispatcher, service host, stavový engine a `WizardService` v tehdejším `plan3_fix3.md:203-213`. Závěr ale sestavoval jen `phpvpn-tray` a testy v tehdejším `plan3_fix3.md:298-300` a kopíroval pouze tray EXE v `plan3_fix3.md:302-316`. Projekt výslovně vyžadoval společnou aktualizaci služby a tray v `client.md:831`. Původní plán sám přiznával nekompatibilitu staré služby v tehdejším `plan3_fix3.md:181` a `plan3_fix3.md:287`. Hash manifestu by po samostatné výměně tray stále přestal odpovídat, viz `distribution.md:56-60` a závěr plánu.

**Původní auditní dopad:** service-specific compile nebo link chyba mohla projít závěrečnou bránou. Nový tray uložený do `dist` by se starou službou nepodporoval nové příkazy a adresář mohl působit jako funkční distribuce, přestože jí nebyl.

**Původně navržená lehká náprava:** do rychlé brány přidat alespoň build cíle `phpvpn-service` bez kopírování nebo nasazení a component test obou nových dispatch cest. Kopii samotného tray jednoznačně označit jako neinstalovatelný vývojový artefakt, případně ji vůbec nedávat do `dist`. Funkční balíček vyžaduje samostatně povolenou službu a nový podepsaný manifest.

#### Rozhodnutí

Tento původní auditní nález je vyřešen následujícím schváleným rozhodnutím:

- Po úspěšném potvrzeném Configuration Wizard Finish se konfigurace uloží, úspěch se pravdivě oznámí, Wizard relace se uzavře a Wizard se zavře.
- Communication contracts se po Finish automaticky neotevře. VPN se rovněž nepřipojí.
- Editor se otevře až pozdější novou explicitní volbou uživatele z tray menu. Tato ruční akce použije jediný existující `get-config-document` a otevře model pouze pro `availability=valid` nad aktuálně uloženou konfigurací.
- Ruční Contracts není pokračování Wizardu ani handoff konkrétní publikace.
- `wizard-get-published-document` se nezavádí.
- Odstraňuje se publikovaný handoff buffer ve Wizard relaci, dodatečná až 4MiB handoff RAM, nové handoff error kódy, move-owned callback, handoff parser, builder a dispatch, obnovování lease během předání, idempotentní čtení publikovaného dokumentu i testy automatického předání.
- Chyba otevření Contracts po Finish již není relevantní, protože Finish editor neotevírá.
- Existující ruční Contracts workflow nadále používá jediné `get-config-document` bez přímého filesystem I/O v tray.
- Spolu s rozhodnutím u problému 1 se v rámci tohoto UX plánu nezavádí žádný nový IPC příkaz a nemění se serverový HTTP/wire protokol.

Původní důkaz, dopad a náprava výše zůstávají pouze jako auditní historie. Protože oba nové IPC návrhy byly odstraněny, nevzniká kvůli tomuto UX plánu nová service dispatch cesta a problém číslo 2 již není otevřený ani blokující. Samostatné riziko nepodepsané kopie tray a neaktuálního manifestu zůstává beze změny.

## Významná rizika

### 3. Není určeno, co smí běžet na UI vlákně při nejdražších operacích - rozhodnuto

**Rozhodnutí:** Password SHA bude omezené na 255 znaků a při psaní se může počítat synchronně v samostatném nemodálním dialogu. Do řádku Contracts se výsledek přenese až po potvrzení tohoto dialogu tlačítkem OK. Patch, serializace a validace konfiguračního JSONu po hlavním OK mohou také proběhnout synchronně, uživatel akceptuje jednorázové blokování nejvýše přibližně jednu sekundu. Po rozhodnutí bodu 6 vzniká model nejvýše pro 252 řádků. Tyto lokální operace mohou zůstat na UI vlákně, pouze pokud splní měřitelné hranice bodu 4. Čekání na službu musí zůstat asynchronní a UI během něj nesmí blokovat globální Disconnect. K pozdějšímu zapracování do normativních dokumentů zbývá přesně vymezit limit 255 znaků.

**Původní auditní důkaz:** editor připouští 4092 řádků v `tray_UX.md:263-265`, vytváří defaultní SHA-256 a celý model podle `plan3_fix3.md:195-201` a `plan3_fix3.md:217-219`, před Save patchuje, serializuje a validuje až 512KiB DOM podle `plan3_fix3.md:225-231`. Password SHA může při každé změně synchronně hashovat až 524288 UTF-8 bajtů podle `plan3_fix3.md:199` a `tray_UX.md:283-287`. F02 zmiňuje background variantu jen pro heslo a až po subjektivně „nevyhovující“ odezvě.

**Původní auditní dopad:** kliknutí na Contracts, vložení velkého hesla nebo OK mohlo zablokovat message loop, status, paint, focus i nezávislý Disconnect. Šlo o odhad rizika, ne o prokázanou regresi, protože budoucí implementace nebyla sestavena ani benchmarkována.

**Původně navržená lehká náprava:** výslovně rozdělit fáze. Na UI vlákně ponechat jen časově omezenou mutaci modelu a aplikaci hotového výsledku. Parse, generování 4092 hashů, úplnou validaci a serializaci provést jako jednu zrušitelnou worker úlohu s generation ID. Alternativně nejprve stanovit měřenou p95 hranici a synchronní cestu ponechat pouze tam, kde ji například běžný `/24` prokazatelně splní. Tuto variantu nahrazují schválená rozhodnutí výše.

### 4. Chybějí měřitelné runtime akceptační hranice - rozhodnuto

**Rozhodnutí:** v optimalizovaném Release buildu se každé časové měření provede po jednom zahřívacím běhu nejméně třikrát a hodnotí se nejhorší naměřený běh. Platí následující cíle a hranice selhání:

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

Dvě sekundy jsou absolutní strop pro jednorázovou lokální operaci, ne povolená odezva při každém znaku nebo scrollování. Čas samotného IPC čekání na službu se do dvousekundového limitu nepočítá. UI však musí během čekání zůstat ovladatelné a globální Disconnect funkční.

**Důkaz:** plán správně omezuje počet HWND a zakazuje vytváření GDI zdrojů při každém paintu v `plan3_fix3.md:187-191` a `plan3_fix3.md:217-219`. A19 ale měří jen počet HWND a neurčitou „paměť v reportu“ v `plan3_fix3.md:277`. Chybí limit pro cold start, otevření menu, otevření Contracts pro `/24` a `/20`, odezvu při psaní, Save, CPU během 500ms pollingu, working-set delta i GDI/USER handles.

**Dopad:** akceptační stav „funguje“ může projít s viditelnými záseky, opakovaným překreslováním nebo výraznou rezidentní pamětí.

**Lehká náprava:** přidat reprodukovatelná měření a srovnání baseline před/po. Minimum je start-to-tray, menu p95, Contracts-ready pro `/24` a `/20`, typing p95, OK-to-request, idle CPU s otevřeným progress oknem, working set a počet GDI/USER handles po 10 cyklech open/close, DPI a resize.

### 5. Modalita a opakované otevření nejsou úplně definované pro souběh místních oken - částečně rozhodnuto

**Rozhodnutí:** Configuration Wizard, Communication contracts a Edit Config mohou být otevřené a používané současně. Každé okno pracuje se svým vlastním konceptem celého dokumentu. Okna se navzájem nezakazují, změny automaticky neslučují ani si průběžně neobnovují. Pro všechny cesty publikující celý Config JSON platí obecné pravidlo, že poslední úspěšná publikace celého validovaného dokumentu vyhrává. Toto pravidlo musí být normativně a jednotně uvedené také v `client.md`. Současný `client.md` je již obsahuje u souběhových testů a příkazu `set-config`, při pozdějším zapracování se má pouze ověřit konzistence všech dotčených částí.

Password SHA nebude modální a nezakáže Communication contracts. Smí být současně otevřeno více Password SHA oken, také několik pro stejný kontrakt. Každé okno se váže na stabilní identitu cílového `remoteIp` a generaci rodičovského Contracts okna, nikoli na pozici nebo recyklovaný HWND řádku. Jeho OK zapíše aktuální hash do příslušného lokálního Key. Pro stejný kontrakt vyhrává poslední potvrzený Password SHA.

Hlavní OK v Communication contracts nejprve vytvoří neměnný kandidát z již potvrzených hodnot. Všechna dosud otevřená Password SHA okna patřící tomuto Contracts oknu se následně zavřou bez aplikace dalších změn. Potom se zakáže pouze ukládané Contracts okno a jeho ovládání. Jiné konfigurační editory, tray, status a globální Disconnect zůstávají funkční. Po potvrzeném úspěchu se Contracts zavře. Po potvrzeném odmítnutí nebo konečném timeoutu se vrátí do definovaného ovladatelného stavu včetně `saveUnknown`, je-li výsledek publikace nejistý.

Při pozdějším zapracování se ze všech normativních dokumentů, zejména `tray_UX.md`, `plan3_fix3.md` a navazujících částí `client.md`, musí odstranit nebo přepsat opačná tvrzení, že Contracts blokují Wizard či Edit Config, že Password SHA je modální nebo že zakazuje rodičovské Contracts. Doplní se výše uvedená pravidla souběhu, stabilní identity cíle, zavření Password SHA při hlavním OK a poslední úspěšné publikace. Samostatně ještě zbývá rozhodnout přesné chování Exit a logoff během `saving` nebo `saveUnknown`.

**Důkaz:** Contracts má blokovat lokální Wizard nebo editaci a opakované otevření má aktivovat existující formulář podle `tray_UX.md:94-96` a `plan3_fix3.md:217`. Není ale rozhodnuto, co se stane, když už je otevřený dirty Edit Config, Wizard ve stavu checking nebo committing, případně `saveUnknown`. Není ani určeno, co udělá Exit nebo logoff během `saving`. Tray menu zůstává dostupné a Exit vždy existuje v `client/apps/tray/main.cpp:154-170`.

**Dopad:** dva plné koncepty mohou soutěžit, owner může zůstat chybně disabled nebo proces skončí u nejasného zápisu bez pravdivé zprávy.

**Lehká náprava:** doplnit jednotný per-tray `ConfigurationUiCoordinator` a tabulku `open / activate / refuse` pro Wizard, Edit Config, Contracts a child dialog. Pro Exit a logoff definovat konečné cancel/wait chování a pravdivý `saveUnknown` text, nikdy příslib rollbacku, který nelze zaručit.

### 6. Virtualizace a přístupnost mají nedefinovaný kontrakt - rozhodnuto

**Rozhodnutí:** globální validační rozsah `vpnCidr` se zpřísní ze současných `/16` až `/29` na kanonické IPv4 prefixy `/24` až `/29`. Prefix kratší než `/24`, tedy `/16` až `/23`, bude neplatná konfigurace a musí být odmítnut před Connectem i publikací. `/29` zůstává nejdelším podporovaným prefixem, protože při současné rezervaci síťové adresy, první host adresy a broadcastu poskytuje ještě pět platných klientských adres a po vyloučení vlastní IP nejvýše čtyři peery. `/30` by poskytl pouze jednu platnou klientskou adresu a žádného vzdáleného peera, `/31` a `/32` současný adresní model nepodporuje.

Tím je maximem výchozí `/24` s 253 platnými klientskými adresami `.2` až `.254` a přesně 252 řádky po vyloučení vlastní IP. Communication contracts nebude virtualizovat řádky ani implementovat vlastní UI Automation provider. Vytvoří nativní přístupné ovládací prvky pro všech nejvýše 252 řádků ve scrollovatelném obsahu. Čtečka obrazovky tak může enumerovat všechny skutečné ovládací prvky a Tab/Shift+Tab zachová úplný vizuální pořádek.

Toto rozhodnutí nemění HTTP ani kryptografický wire protokol a nepřidává ani nemění IPC příkazy nebo jejich JSON schéma. Mění však význam validace existující hodnoty `vpnCidr` a zpětnou kompatibilitu konfigurací. Dosud platné konfigurace s `/16` až `/23` budou nově odmítnuty. Při pozdějším zapracování se musí společně upravit `client.md`, validátor, plán, `tray_UX.md`, testy, akceptační matice a všechny výpočty či texty předpokládající `/16` až `/23`, 4092 řádků, `/20`, virtualizovaný viewport nebo recyklaci HWND. Rozhodnutí bodu 4 se současně aktualizuje odstraněním měření pro 4092 řádků a novým pevným limitem nejvýše +1200 GDI/USER objektů pro plně materializovanou tabulku.

**Původní auditní důkaz:** UX vyžaduje role, hodnoty, chyby a informace pro screen reader v `tray_UX.md:90-92`. Současně existují HWND pouze pro viewport a nejvýše jeden rezervní řádek na každé straně podle `tray_UX.md:257-261`.

**Původní auditní dopad:** nebylo rozhodnuto, zda čtečka musí enumerovat všech 4092 off-screen položek přes UI Automation virtualized-item provider, nebo zda stačí právě materializované prvky s keyboard reveal. První varianta by byla podstatně větší implementace a mohla zvýšit velikost binárky. Druhá varianta nemusela splnit očekávanou úroveň přístupnosti.

**Původně navržená lehká náprava:** zvolit jednu úroveň jako normativní. Pro lehké řešení použít nativní přístupné controls jen pro materializované řádky, programový scroll/reveal při Tab a přístupný souhrn počtu řádků a chyb, bez vlastního plného UIA provideru. Tuto variantu nahrazuje plná materializace nejvýše 252 řádků podle rozhodnutí výše.

### 7. „Přenosné“ prezentační modely jsou plánované do Windows tray cíle - rozhodnuto

**Rozhodnutí:** čisté přenosné modely `contracts_model.*` a `password_key_model.*` budou umístěné v nové složce `client/src/core/presentation/` a přidané jako samostatné zdrojové objekty do existující statické knihovny `phpvpn-core`. Nebude kvůli nim vznikat nová knihovna, DLL, framework ani závislost třetí strany. `contracts_document.*` zůstane v `client/src/core/config/`, protože vlastní DOM patch, serializaci a konfigurační validaci.

Modelová vrstva nesmí zahrnovat Windows hlavičky ani znát `HWND`, message loop, GDI, konkrétní ovládací prvky nebo tray lifecycle. Smí používat pouze přenosné hodnotové typy a existující core rozhraní, například `Ipv4Address`, validační výsledky a `CryptoProvider`. Stabilní identita řádku a cíle Password SHA je datová hodnota `remoteIp` doplněná generační identitou konceptu, nikoli handle nebo index recyklovaného prvku.

V `client/apps/tray/` zůstanou pouze Windows okna, layout, focus, scroll, nativní accessibility a tenké adaptéry, které převádějí Win32 události na operace modelu a vykreslují jeho stav. Jednotkové testy modelů budou v běžné core testovací vrstvě bez vytváření oken. Protože jde o samostatné objekty existující statické knihovny, service ani CLI si nepřitáhnou nepoužitý modelový kód a samotné správné umístění nepřidává runtime ani významný binární overhead. Při pozdějším zapracování se musí opravit opačné cesty a vlastnictví v `plan3_fix3.md`, `tray_UX.md`, `client.md` a CMake seznamech.

**Důkaz:** `client.md:9` požaduje přenosné modely bez závislosti na tray procesu a `tray_UX.md:73` slibuje sdílení modelů. F02 ale umísťuje `contracts_model.*` a `password_key_model.*` pod `client/apps/tray` v `plan3_fix3.md:193-199`.

**Dopad:** budoucí macOS nebo Linux pohled by závisel na Windows app vrstvě, případně by model duplikoval.

**Lehká náprava:** přenést čisté modely do malé core/presentation knihovny. Win32 adapter, layout, HWND a Windows message handling ponechat v `apps/tray`.

### 8. Normativní copy se mezi dokumenty rozchází - rozhodnuto

**Rozhodnutí:** `tray_UX.md` je pro tyto přesné UI texty autoritou. Chyba jednoho konkrétního pole Key bude přesně `Key must contain exactly 64 lowercase hexadecimal characters.` v jednotném čísle. Potvrzení Random bude přesně `Replace the key for {contractId} with a new random key?`, kde se `{contractId}` nahradí skutečnou stabilní identitou kontraktu, aby uživatel vždy viděl, který řádek mění. Tyto řetězce jsou exact-match kontrakt a při pozdějším zapracování se musí beze změny sjednotit v `plan3_fix3.md`, `tray_UX.md`, příslušných částech `client.md`, implementaci a UI či snapshot testech. Rozhodnutí nepřidává runtime ani významný binární overhead.

**Důkaz:** aktivní chyba je v plánu množná `Keys must contain exactly...` v `plan3_fix3.md:73`, zatímco UX vyžaduje jednotné `Key must contain exactly...` v `tray_UX.md:279`. Random potvrzení je v plánu obecné `this contract` v `plan3_fix3.md:75`, zatímco UX vyžaduje konkrétní `{contractId}` v `tray_UX.md:281`.

**Dopad:** snapshot/UI testy a implementace nemají jedinou autoritu.

**Lehká náprava:** sjednotit dokumenty na UX znění s `contractId` a výslovně určit, které texty jsou exact-match kontrakt.

### 9. Definice „aktivního monitoru“ je nejednoznačná - rozhodnuto

**Rozhodnutí:** všechna tray okna použijí jediný sdílený helper a následující deterministické pořadí výběru monitoru. Okno otevřené jiným oknem použije monitor svého platného rodiče. Hlavní okno otevřené z tray menu použije monitor bodu, u kterého bylo menu skutečně zobrazeno. Akce bez rodiče a bez uloženého bodu menu použije monitor platného foreground okna, potom monitor pod kurzorem a jako poslední fallback primární monitor.

Nové okno se vystředí do pracovní plochy vybraného monitoru mimo taskbar. Všechny výpočty používají podepsané souřadnice a podporují monitory vlevo nebo nad primárním monitorem. Rozměry se počítají pro DPI cílového monitoru. Pokud se okno do pracovní plochy nevejde, platí již určený clamp, resize a scroll, aby žádná akce nezůstala mimo obrazovku.

Již existující okno se při pouhé reaktivaci nepřesouvá na monitor nového kliknutí. Je-li minimalizované, obnoví se. Je-li celé uvnitř použitelné pracovní plochy kteréhokoli dostupného monitoru, zachová přesnou polohu i velikost. Po odpojení monitoru nebo jiné ztrátě použitelné polohy se přesune na nejbližší dostupný monitor a upraví pouze o nezbytné minimum. Stejná kontrola proběhne při opětovné aktivaci a po změně DPI, rozlišení nebo pracovní plochy. Připojení dalšího monitoru samo o sobě viditelné okno nepřesouvá. Při pozdějším zapracování se toto pravidlo musí sjednotit v `tray_UX.md`, `plan3_fix3.md`, `client.md`, společném window helperu a geometry/UI testech.

**Důkaz:** UX požaduje aktivní monitor a podporu záporných souřadnic v `tray_UX.md:77-90`. Současný helper vybírá monitor podle kurzoru v `client/apps/tray/window_utils.h:37-43`.

**Dopad:** cursor monitor funguje při kliknutí na tray, ale neřeší keyboard invocation, ztrátu focusu ani znovuaktivaci okna po změně konfigurace monitorů.

**Lehká náprava:** určit pořadí owner monitor → foreground monitor → cursor monitor. Dále určit, zda reaktivace existujícího okna pouze aktivuje, nebo také znovu omezí jeho polohu do aktuální work area.

## Menší připomínky

### 10. Zbytečné překreslení a práce s ikonou nejsou přímo v akceptační matici - rozhodnuto

**Rozhodnutí:** každé okno vlastní malou cache všech opakovaně používaných fontů, ikon, štětců a per. Prostředky se vytvoří při inicializaci pro aktuální DPI, znovu se vytvoří pouze po skutečné změně DPI a jednou se uvolní při zániku okna. Běžná obsluha `WM_PAINT` nesmí načítat ikony ani vytvářet nový GDI prostředek. Vykreslování používá pouze prostředky z cache a invaliduje jen regiony, jejichž vizuální stav se skutečně změnil. Úplná invalidace zůstává povolená při vytvoření okna, změně DPI, zásadním přepočtu layoutu nebo obnově po chybě dávkového přesunu.

Tray si uchová poslední skutečně publikovanou identitu ikony a přesný tooltip. `Shell_NotifyIcon(NIM_MODIFY)` zavolá pouze tehdy, když se ikona nebo tooltip změnily. Samotný příchod nového `get-status` snímku bez vizuální změny nesmí vyvolat shell update.

Akceptační test provede nejméně 500 překreslení beze změny DPI a ověří, že počet GDI a USER objektů po ustálení neroste. Dále použije dvacet cyklů otevření, změny DPI a zavření a musí splnit obecný limit bodu 4, tedy nejvýše +5 trvale zbývajících objektů a +1 MiB. Fake nebo instrumentovaný shell adapter ověří nulové `NIM_MODIFY` pro shodnou ikonu i tooltip a právě jedno volání pro každou skutečnou výslednou změnu. Řešení nepřidá knihovnu, framework ani nový polling.

**Důkaz:** dnešní progress paint při každém paintu znovu načte ikonu a vytvoří tři fonty v `client/apps/tray/progress_window.cpp:92-143`. F01 správně požaduje cache v `plan3_fix3.md:187-191`. Běžný tray ale navíc volá `Shell_NotifyIcon(NIM_MODIFY)` po každém snapshotu i beze změny v `client/apps/tray/main.cpp:204-221`.

**Dopad:** samostatně jde nejspíš o malou režii, ale opakovaný progress paint spolu s dalšími painty může zbytečně vytvářet GDI objekty a zatěžovat shell.

**Lehká náprava:** cache per-DPI zdrojů, invalidovat jen změněné regiony, volat `NIM_MODIFY` jen při skutečné změně ikony nebo tooltipu a přidat handle-leak test.

### 11. Angličtina je fakticky jediná lokalizace, ale není označená jako non-goal - rozhodnuto

**Rozhodnutí:** tato verze tray aplikace bude pouze v angličtině. Lokalizace, volba jazyka a reakce na jazyk Windows jsou mimo rozsah. Nevznikne resource katalog, překladový framework ani stabilní ABI lokalizačních klíčů. Normativní anglické texty mohou být běžné konstanty v malém společném textovém katalogu, aby nevznikaly duplicitní řetězce mezi okny.

Rozložení musí nadále měřit skutečný vykreslovaný text, podporovat zalamování a nesmí předpokládat pevnou šířku konkrétní anglické věty. Tím zůstane budoucí lokalizace možná bez dnešní implementace překladového systému. Statistics budou záměrně používat anglické seskupení tisíců nezávislé na systémovém locale, jak již určuje UX. Budoucí lokalizace bude samostatná specifikovaná změna. Při pozdějším zapracování se toto omezení rozsahu výslovně doplní do `tray_UX.md`, `plan3_fix3.md`, `client.md` a akceptačních testů.

`tray_UX.md:5` a `client.md:595` stanovují anglické texty. Layout správně počítá s měřením delších labelů v `tray_UX.md:57` a `tray_UX.md:88`. Jde o konzistentní rozhodnutí, nikoli rozpor. Pro budoucí očekávání stačí výslovně dodat `localization is out of scope. No resource catalog ABI is promised.` Čísla ve Statistics jsou záměrně anglicky seskupovaná podle `tray_UX.md:176`.

### 12. Vizuální akceptace nemá toleranci ani přesný viewport - rozhodnuto

**Rozhodnutí:** referenční vizuální porovnání všech jedenácti mockupů se provede při 100 % DPI nad přesnou klientskou plochou bez systémového titulkového pruhu a vnějšího rámu. Použijí se již definované výchozí rozměry: Connect 940 × 680 DIP, Edit Config 1080 × 560 DIP, Statistics 1000 × 650 DIP, Wizard 1000 × 680 DIP, Communication contracts 1180 × 680 DIP a Password SHA-256 540 × 360 DIP. Všechny stavové mockupy stejného okna používají stejnou klientskou plochu.

Povolená odchylka polohy a rozměrů jednoho prvku je nejvýše 6 DIP. Rozdíl způsobený skutečnými metrikami nativního fontu, zalomením nebo baseline textu je nejvýše 8 DIP, nesmí však změnit pořadí, překrýt obsah, useknout významný text ani skrýt akci. Přítomnost sekcí, pořadí, přesné normativní texty, role prvků, sémantické barvy, dostupnost patičky a všechny povinné akce se hodnotí bez této tolerance. Systémový titulek, rámeček, anti-aliasing fontů a nativní kresba controls se neporovnávají pixel po pixelu.

Při 125, 150 a 200 % DPI se stejné DIP rozložení ověří po odpovídajícím škálování. Kontroluje se geometrie s uvedenými tolerancemi a funkčně také resize, scroll, focus, klávesnice, změna DPI mezi monitory a malá pracovní plocha. Pixel-perfect shoda celého screenshotu není akceptační podmínkou. Každý vizuální report uvede název mockupu, Windows build, DPI, skutečnou klientskou plochu, povolenou odchylku a výsledek. Při pozdějším zapracování se tato metodika musí sjednotit v `tray_UX.md`, `plan3_fix3.md`, `client.md` a vizuální akceptační matici.

UX14 hodnotí hierarchii a rozestupy v `tray_UX.md:317-321`, ale „stejná velikost klientské plochy“ není svázána s konkrétní dvojicí DIP/PNG ani tolerancí.

**Původně navržená lehká náprava:** pro každý mockup určit cílovou výchozí DIP plochu, testované DPI, povolené odchylky nativního titulku a font metrics a toleranci geometrie 2-4 DIP. Pixel-perfect test není nutný. Schválené rozhodnutí výše tuto původní toleranci nahrazuje 6 DIP pro geometrii a 8 DIP pro textové metriky.

## Věrnost všem 11 mockupům

Všechny uvedené reference existují v `C:\git\phpvpn\assets` a bylo možné je jednoznačně porovnat. Dokument odpovídá jejich hlavní hierarchii, kartám, patičkám a akcím podle přehledu v `tray_UX.md:9-20` a detailních kapitol UX05 až UX11.

1. **`communication_contracts1.png`:** odpovídá hlavnímu oknu Communication contracts, hlavičce s vlastní IP, tabulce `Contract / Enabled / Key / Actions`, fixní spodní chybě a primárnímu OK. Původní plán doplňoval numerické řazení, identitu řádku podle `remoteIp`, virtualizaci a chování při malé šířce v `tray_UX.md:249-265`. Schválené rozhodnutí bodu 6 virtualizaci odstranilo a zachovává nejvýše 252 plně materializovaných řádků.
2. **`communication_contracts2.png`:** odpovídá samostatnému Password SHA dialogu a práci s konkrétním kontraktem. Schválené rozhodnutí bodu 5 z něj záměrně dělá nemodální okno, které nezakazuje rodičovské Contracts, váže se na stabilní identitu cíle a zachovává pravidla citlivých bufferů. Exact copy bylo sjednoceno rozhodnutím bodu 8.
3. **`connect1.png`:** odpovídá stavu Connecting, třem pending kontrolám, oranžovým indikátorům a typografické kompozici bez velkého štítu. Přesné chování je popsáno v `tray_UX.md:126`.
4. **`connect2.png`:** odpovídá úspěšnému Connect stavu s URL, IP a třísekundovým odpočtem. Dokument správně vymezuje monotónní deadline +3000 ms, zaokrouhlení nahoru a zavření nejpozději o jeden 500ms tick později v `tray_UX.md:130`. URL/IP jsou omezeny na dvě určená pole podle `tray_UX.md:118`.
5. **`editconfig.png`:** odpovídá nativnímu multiline editoru, konfiguraci cesty, scrollbars a patičce Reset/Cancel/Save. Plán záměrně nahrazuje ukázkovou cestu skutečným `configPath` služby a zachovává read-only Show Info variantu v `tray_UX.md:150-158`.
6. **`statistics.png`:** odpovídá kartám se statistikami a dvousloupcovému IPv4 traffic rozložení. Dokument záměrně používá přesná celočíselná B čísla, anglické seskupení tisíců a přesnost až `UINT64_MAX` v `tray_UX.md:176-180`. Text používá „current connection“ místo nejasného „session“, což je přiznaná bezpečná odchylka od mockupu v `tray_UX.md:162`.
7. **`wizzard0.png`:** odpovídá úvodní obrazovce Wizardu, sidebaru `Configure this computer`, čtyřem krokům a patičce. Dokument výslovně doplňuje Cancel a zachovává bezpečný lifecycle, i když mockup není úplný, viz `tray_UX.md:184-190` a sekce kroku 0 od `tray_UX.md:209`.
8. **`wizzard1.png`:** odpovídá kroku Server URL s polem, hintem, příkladem a vysvětlením HTTP/HTTPS. Dokument stanovuje přesný obsah a ověřovací akci v `tray_UX.md:215-219`.
9. **`wizzard2.png`:** odpovídá kroku Server secret se switchem pod polem. Dokument podrobně zachovává default/literal hodnotu, nulové znaky, JSON string literal režim i žádnou Unicode normalizaci v `tray_UX.md:221-225`.
10. **`wizzard3.png`:** odpovídá kroku Virtual IP and access key. Dokument doplňuje zásadní permanentně viditelný warning, který v mockupu chybí, protože každý ověřovací pokus může trvale odstranit čekající zprávy. Jde o úmyslnou bezpečnostní odchylku vyžadovanou `protocol.md`, popsanou v `tray_UX.md:227-235`.
11. **`wizzard4.png`:** odpovídá Communication contracts summary s přepínačem a třemi sloupci. Dokument zachovává Finish, úplnou validaci a jedinou atomickou publikaci. Po schváleném rozhodnutí Finish pouze oznámí úspěšné uložení a zavře Wizard. Contracts se otevře až pozdější ruční volbou z menu nad aktuálně uloženým dokumentem podle `tray_UX.md:239-245`.

Další společné přiznané odchylky jsou podtržítko místo pomlčky a nativní title bar v `tray_UX.md:20`, skutečná konfigurační cesta v `tray_UX.md:150`, význam „current connection“ v `tray_UX.md:162` a bezpečnostní warning či Cancel ve Wizardu v `tray_UX.md:211-235`.

**Omezení revize:** skutečnou renderovanou věrnost nelze ověřit, protože implementace popsaného redesignu neexistuje a při této read-only revizi nebylo GUI sestaveno ani spuštěno. Dokumentační věrnost je vysoká, ale musí ji potvrdit runtime screenshoty při přesně stanoveném viewportu a DPI.

## Runtime overhead

### Prokázané vlastnosti návrhu

- Start tray nadále nemá číst konfiguraci. Načte tři existující ikony, založí 5s timer a provede první `get-status`, viz `tray_UX.md:100` a současný `client/apps/tray/main.cpp:94-119`.
- Menu je nativní a bez konfiguračního I/O do kliknutí podle `tray_UX.md:100-102`.
- Původní návrh v `plan3_fix3.md:122` a `plan3_fix3.md:275` počítal při otevřeném Connect s novým 500ms progress readerem a nezávislým 5s readerem, tedy nominálně s 2,2 krátkého paměťového IPC requestu za sekundu. Schválené rozhodnutí tento dodatečný progress reader a příkaz ruší. Nový runtime návrh přidá pouze jeden existující `get-config` při otevření Connect okna a dále využije nezměněný `get-status` se současným pollingem.
- Statistics nepřidává vlastní timer podle `tray_UX.md:178-180`.
- Schválený cílový návrh po rozhodnutí bodu 6 nepoužije virtualizaci. Pro všech nejvýše 252 řádků vytvoří nativní ovládací prvky a jejich počet ověří proti pevnému limitu bodu 4.
- Stávající maximální velikost jedné IPC zprávy je 4 MiB. Tato dřívější transportní hranice se návrhem nemění a není problémem této revize.
- Původně navržený Wizard handoff měl držet nejvýše 512 KiB publikovaného dokumentu na relaci, tedy až 4 MiB při osmi relacích. Schválené rozhodnutí handoff zrušilo, takže tato dodatečná RAM nebude alokována. Jde o historický odhad odstraněné varianty, nikoli vlastnost aktivního návrhu.

### Odhad, nikoli měření

- Idle start a otevření menu budou pravděpodobně prakticky beze změny.
- Jednorázový `get-config` při otevření Connect bude pravděpodobně představovat malý náklad. Přenese však celý platný config včetně citlivých hodnot, takže nesmí být logován a jeho dočasné kopie mají mít omezenou životnost.
- Otevřený Contracts pro `/24` bude pravděpodobně stát nízké jednotky MiB.
- Plně materializovaných 252 řádků znamená přibližně tisíc nativních ovládacích prvků. Windows je má zvládnout, ale skutečný working set, USER/GDI objekty a čas vytvoření musí potvrdit hranice bodu 4.
- Nejvýše 252 krátkých CNG SHA volání a úplné DOM operace budou pravděpodobně malé jednorázové náklady. Bez benchmarku to nelze tvrdit jako fakt.
- Synchronní Password SHA je omezené na 255 znaků. Největším zbývajícím rizikem interaktivní odezvy je vytvoření všech nativních řádků a lokální příprava Save, obojí podléhá hranicím bodu 4. Handoff buffer byl z návrhu odstraněn.

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
- Nové bajty budou především v `.text` a `.rdata`: Win32 layout/rendering, Contracts a Password model, nové použití existujících IPC čteček a anglické stringy.
- `phpvpn-core` a `phpvpn-platform` jsou statické knihovny podle `client/src/CMakeLists.txt:1-74`, linker ale přitáhne jen použité objekty.
- Odstraněný handoff nepřidá parser, builder, dispatch, callback ani testovací kód. Odhad růstu `.text` a `.rdata` je proto nižší než v původně auditované variantě.

### Odhad velikostního dopadu

Bez sestavení nelze poctivě určit přesnou deltu. Rozumný řád nárůstu tray EXE jsou desítky až nižší stovky KiB. Jde pouze o kvalifikovaný odhad podle rozsahu nového nativního kódu a stringů. Rizikem násobného růstu by byl vlastní plný UIA provider nebo omylem embedované PNG či fonty, nikoli současný základ návrhu.

Požadované before/after měření v UX14 je správné. Report má navíc uvádět samostatnou deltu `.text`, `.rdata` a `.rsrc`, absolutně i procentně, při stejné architektuře, konfiguraci a stavu podpisu. Tím se rychle odhalí nechtěný asset nebo framework růst.

## Otevřené otázky

1. Je kopie samotného tray do `dist` zamýšlená jen jako neinstalovatelný vývojový artefakt?
2. Jak se mají Exit a logoff zachovat během `saving` nebo `saveUnknown`?

## Minimální nutné změny před implementací

Schválené odstranění obou nových IPC příkazů, automatického Wizard handoff a otevření Contracts po Finish již bylo do aktuálních dokumentů zapracováno. Zbývají tyto minimální změny před implementací:

1. Zapracovat globální validaci `vpnCidr` pouze pro `/24` až `/29`, nejvýše 252 plně materializovaných řádků, nemodální Password SHA a synchronní lokální operace v mezích bodu 4. Čekání na službu ponechat asynchronní.
2. Doplnit schválené měřitelné limity a baseline/delta pro start, menu, Contracts do 252 řádků, typing, Save, idle CPU, working set a GDI/USER handles.
3. Zapracovat souběžné nezávislé editory podle pravidla poslední úspěšné publikace a rozhodnout už jen Exit/logoff během `saving` nebo `saveUnknown`.
4. Odstranit virtualizaci a vlastní plný UIA provider. Všechny nejvýše 252 řádky realizovat nativními přístupnými ovládacími prvky.
5. Zapracovat rozhodnuté umístění čistých modelů v `client/src/core/presentation/`, DOM logiky v `client/src/core/config/` a pouze Win32 adaptérů v `client/apps/tray/`.
6. Zapracovat rozhodnuté exact UI copy, zejména `Key must contain exactly 64 lowercase hexadecimal characters.` a Random potvrzení s konkrétním `{contractId}`.
7. Zapracovat rozhodnutý sdílený výběr monitoru, umístění nového okna a minimální clamp při reaktivaci nebo změně zobrazovací konfigurace.
8. Zapracovat rozhodnuté klientské plochy, testované DPI, toleranci geometrie 6 DIP a textových metrik 8 DIP pro všech 11 mockupů.
9. Do velikostního reportu přidat absolutní i procentní deltu celého EXE a sekcí `.text`, `.rdata`, `.rsrc`.

Po těchto změnách je plán implementovatelný bez změny VPN wire protokolu, bez nové velké závislosti a s realistickou šancí zachovat lehký tray proces i vysokou věrnost mockupům.
