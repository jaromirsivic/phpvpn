# Plan3 fix3: modernizace tray UX a editor Communication contracts

## Stav, rozsah a výchozí podklady

Tento dokument je plán následné implementace, nikoli zpráva o hotovém GUI. Zapracovává rozhodnutí problémů 1 až 12 z [plan3_fix3_problems.md](plan3_fix3_problems.md), následné potvrzení Password SHA a Exit/logoff a požadavek rychlého vývojového workflow. Podle posledního výslovného zadání se nyní mění pouze tento plán. client.md, tray_UX.md, kód, testy, build skripty, mockupy a distribuce zůstávají beze změny. Nebyl proveden build ani kopie EXE. Budoucí implementace musí současně aktualizovat dotčené normativní dokumenty podle N01 až N09 a odstranit jejich protichůdná pravidla. Schválené rozhodnutí má přednost před původním auditním důkazem nebo návrhem lehké nápravy.

Výchozí revize pro audit: `367355fb6802b52bf33547427c529e96dd5fe72b`, pracovní strom před dokumentační změnou čistý. Podklady: [protocol.md](protocol.md), celý [client.md](client.md), [distribution.md](distribution.md), [plan3_client.md](plan3_client.md), předchozí opravné plány, zdroje `client/apps/tray`, `client/src/core/config`, `client/src/core/wizard`, IPC, CMake a související unit/UI testy. Všech jedenáct obrazových referencí a jejich vazby na okna uvádí UX00. Současný kód je rozhodující pro odhad změn, historický plán není potvrzení implementace.

| Zjištěný stav | Důsledek pro implementaci |
|---|---|
| `contracts_window.*` poskytuje pouze `show(HINSTANCE)` a prázdné okno. | Doplnit přenosný model, vlastníka, řádkové ovládání, vnořený dialog a samostatný řadič ukládání. |
| `main.cpp` před Contracts používá `get-config-state`. | Nahradit jediným `get-config-document`, jeho výsledek použít zároveň jako gate i zdroj dokumentu. |
| Wizard po confirmed zavře relaci a vyvolá callback bez dat. | Zachovat uzavření bez dat a bez automatického otevření Contracts. Pozdější ruční volba Contracts je nová samostatná akce nad aktuálně uloženou konfigurací. |
| `set-config` vrací prosté acknowledgement, nemá Wizard commit receipt. | Zachovat existující zápisovou cestu, nejasný výsledek řešit explicitním čtením a rozhodnutím uživatele. |
| `get-status` záměrně neobsahuje konfigurační URL ani IP. | Ponechat schema i význam beze změny. Connect jednou použije existující `get-config` pouze pro zobrazené URL/IP a akceptuje časový rozdíl vůči aktivnímu snímku. |
| Progress má samostatný 500ms polling, model zmrazí terminální výsledek a zavírá po 3 s. | Zachovat nezávislost, doplnit viditelný odpočet a nové karty. |
| Editor zachovává skutečný dokument, řeší NUL a dva posuvníky. | Změnit pouze prezentaci a patičku, zachovat bezeztrátovou editaci. |
| Statistiky jsou devět hodnot z jednoho snímku. | Přeskupit do tří karet bez nového měření nebo timeru. |
| Wizard již má lease, attemptSequence, epochy, receipts a oddělené čtečky. | Přizpůsobit pohled, nepřepisovat celý lifecycle. |
| Auditovaný kód přijímá CIDR `/16` až `/29`, Wizard defaults jsou menší kandidátní sada. | Nově globálně zpřísnit na /24 až /29, editor nejvýše 252 řádků, Wizard kandidáty zachovat podle N03. |
| Preset `release` znamená RelWithDebInfo, architekturu zajišťuje prostředí x64. | Neodvozovat architekturu z názvu presetu, ověřit PE sestavených výstupů bez kopie do dist. |

## Jednoznačné mapování schválených problémů

Každý problém má právě jeden záznam. Normativní pravidlo, implementační fáze a akceptace jsou různé vrstvy téhož požadavku, nikoli souběžné alternativy.

| Problém | Závazné řešení a umístění | Implementace / důkaz |
|---|---|---|
| 1 | N01/N05: existující jednorázový get-config, nezměněný get-status, přijatý nesoulad uloženého a aktivního snímku. | F03, A14/A17/A22. |
| 2 | N06: Finish jen uloží a zavře, ruční Contracts nad aktuálním dokumentem, žádný handoff IPC/buffer. | F04/F06, A12/A13/A22. Novější vývojová cesta F09 nahrazuje původní kopii do dist. |
| 3 | N03/N04: omezený synchronní Password SHA a místní příprava kandidáta, vždy asynchronní IPC. Potvrzená jednotka = Unicode skalární hodnota. | F02/F06, A08/A26. |
| 4 | N09b: přesné cíle a hranice, warmup a nejhorší ze tří nebo více měřených běhů. | F08, A26. |
| 5 | N03/N04/N04a: nezávislí editoři, více nemodálních Password SHA, kandidát/generace, poslední publikace, potvrzený Exit/logoff. | F05/F06, A09/A18/A30. |
| 6 | N03: globální /24 až /29, nejvýše 252 nativních řádků bez virtualizace, zpětná nekompatibilita konfigurací. | F00a/F05, A04/A19. |
| 7 | F02 a N09: čisté modely v core/presentation, DOM v core/config, Windows adaptéry v apps/tray, existující statická knihovna. | F02, A31. |
| 8 | N03: přesná jednotná chyba Key a Random s konkrétním contractId, autorita UX11. | F05, A06/A07/A29. |
| 9 | N09a: deterministický výběr monitoru a zachování/minimální oprava geometrie při reaktivaci. | F01, A28. |
| 10 | N09a/b: cache prostředků, změnová invalidace a NIM_MODIFY, 500 paintů a 20 cyklů. | F01/F08, A27. |
| 11 | N09a: angličtina, nezávislost na locale, lokalizace mimo rozsah. | F01/F07, A29. |
| 12 | N09b: přesné klientské plochy, DPI, tolerance 6/8 DIP a report všech jedenácti mockupů. | F08, A20. |

## Povinné souběžné aktualizace normativních dokumentů při implementaci

Tato tabulka přesně vymezuje budoucí úpravy dokumentů, které jsou nyní záměrně beze změny. Budoucí agent nesmí přidat nové pravidlo vedle staré opačné věty a tvrdit, že jsou dokumenty sjednocené. N01 až N09 obsahují cílový význam, v tray_UX.md se zachovají ostatní vizuální podrobnosti.

| Dokument / místo | Přesná náhrada nebo doplnění |
|---|---|
| tray_UX.md UX00 | Aktualizovat stav implementační etapy a autority. Doplnit N09a angličtinu. V řádku Password SHA reference odstranit zakázaného rodiče a přiznat schválenou nemodální odchylku. |
| UX02 | N09a cache ikon/fontů/štětců/per, invalidace a shell update. Doplnit přesné cesty modelů a statické objekty F02, bez nové knihovny. |
| UX03 | První odstavec o neurčitém aktivním monitoru nahradit celým N09a. Rozměry označit jako klientské bez rámu. Odstavce o Contracts blokujících Wizard/Edit Config a Password SHA zakazujícím rodiče nahradit N03/04a. Opakované Contracts aktivuje stejnou instanci bez nového načtení. Random potvrzení zůstává dočasně modální pouze vůči svému rodiči s bezpečným obnovením focusu. |
| UX04/05/08/09 | Ověřit již zapracované N01/N05/N06: žádný nový progress IPC ani handoff, Finish neotevírá editor. Žádné vrácení odstraněného návrhu při přepisování okolních odstavců. |
| UX10 | Odstavce „nativní prvky jen pro viditelné řádky“, „recyklace“ a „limit 4092 /20“ nahradit N03. Odstranit chybu „This address range is too large ... Use Edit Config to manage this configuration.“ jako alternativu k platné široké konfiguraci. Mimo /24 až /29 jde o invalid config, Edit Config je pouze cesta k jeho opravě. Zachovat numericky řazené remoteIp, tab pořadí, header/footer, scroll a skutečný limit výsledného JSON. |
| UX11 | Přepsat modalitu, životnost více oken a návrat focusu podle N03/F05. Limit 524288 UTF-8 bajtů pro heslo nahradit přesným N03, nikoli změnit rozpočet rozpracovaných Key. Exact Key/Random texty zůstávají autoritou. |
| UX12 | Hlavní OK vytvoří kandidáta z potvrzených hodnot, zavře vlastní Password SHA a zakáže jen ukládané okno. Přidat povolené synchronní místní fáze z N03/04 v mezích N09b, asynchronní IPC a N04a Exit/logoff. Zachovat unknown workflow bez receipt či automatického retry. |
| UX13/14 | 4092 nahradit 252, zachovat 512KiB JSON/Key rozpočet a 4MiB IPC. UX14 nahradit metodikou N09b a přidat PE sekce. Poslední odstavec o kopii tray přepsat na vývojový výstup F09 bez změny dist. |
| client.md | Provést N01 až N09 v jejich určených sekcích. Zejména globální rozsah a kompatibilita F00a, souběh a Exit N04a, modely F02, monitor/cache/jazyk N09a, vizuální/runtime testy N09b. Zákazy odpočtu a stará pravidla focusu/Back nahradit podle N05/N07. |
| client.md testy a akceptační dokumenty | Rozšířit testy konfigurace/souběhu i Windows UI podle A01 až A32. Odstranit pozitivní očekávání širších prefixů, HWND podle viewportu, modálního Password SHA, obecného Random textu nebo automatického editoru po Finish. Historické implementační reporty ponechat jako historii, novou evidenci vytvořit pro tento plán. |
| Vývojový README po implementaci | F09 přesně určuje následnou aktualizaci client/tests/ui/README.md. Nyní se nemění. protocol.md a distribution.md se nemění, protože se nezavádí wire změna ani nové pravidlo plného vydání. |

## Přesné budoucí normativní změny client.md

Níže uvedené bloky jsou závazný obsah aktualizace. Umístění určují současné nadpisy a citované věty, orientační čísla řádků se mohou posunout. Zachovat ostatní pravidla dané sekce, zejména síťové ověřování, lifecycle a konfiguraci. Nevkládat jen nový odstavec vedle starého protichůdného pravidla. Při implementaci odstranit všechny uvedené konfliktní formulace i odpovídající staré testové očekávání.

### N01: Autorita UX, menu a bezpečnostní výjimky

Do části Windows tray vložit odkaz na normativní `tray_UX.md` jako specifikaci vzhledu, anglických textů, focusu, DPI a resize. Zachovat pořadí a funkce ostatních položek menu. Položku 4 menu Communication Contracts (dříve prázdný placeholder, nyní stručný popis skutečného editoru) sjednotit na:

> **Communication contracts** otevře nemodální editor platných komunikačních kontraktů podle samostatné specifikace níže. Jednorázové načtení a kontrolu provede služba. Otevření menu samo nečte konfiguraci. Okno není automatickým Connectem ani reloadem aktivní VPN.

Bezpečnostní odstavec kolem řádku 815 a zákaz kolem řádku 823 nahradit v dotčeném rozsahu:

> `get-status`, Statistics a logy neobsahují konfigurační URL, klíče ani celé dokumenty. Po otevření Connect okna tray jednou zavolá existující `get-config`, z platného dokumentu převezme pouze Server URL a lokální Virtual IP do karty Connection details a celý přijatý config nikdy neloguje. Je vědomě přijato, že aktuálně uložený dokument může být po externí změně novější než neměnný aktivní snímek připojení, takže zobrazené URL/IP nemusí patřit právě aktivnímu spojení. Connect nikdy nezobrazuje SERVER_SECRET, ACCESS_KEY, payload klíče, tagy, ciphertext, obsah paketů, interní cesty ani raw systémové či síťové chyby. Povolení URL není povolením logování URL ani rozšíření `get-status`. Edit Config smí v editoru zobrazit skutečný dokument včetně URL a klíčů podle své stávající funkce. Communication contracts smí zobrazit a upravit payload klíče jen ve svých určených polích, Password SHA heslo jen v příslušném dialogu. Wizard smí zobrazit zadávané hodnoty. Žádná z těchto výjimek nepovoluje tajemství v diagnostice nebo chybových zprávách.

Zachovat vlastní smlouvu Show Info a omezené existující výjimky URL/pevné konfigurační cesty ve strukturovaných konfiguračních chybách. Progress chyba tuto výjimku pro cestu nemá. URL projde existující validací bez userinfo, query a fragmentu. Zobrazí se plná hodnota, při nedostatku místa přístupný horizontální scroll nebo výběr v read-only poli, nikoli text chyby. Přijatý dokument z `get-config` má omezenou životnost a po získání dvou hodnot se uvolní.

### N02: Otevření editoru, dostupnost a vlastnictví dokumentu

V části Jednorázová kontrola po akci a reakce menu, ověřit již oddělené čtení Statistics a Contracts a sjednotit odstavec na:

> Statistics použije jednorázové get-config-state a potom pouze paměťový get-status. Communication contracts se otevírá výhradně novou explicitní volbou uživatele z tray menu a použije jediný get-config-document. Služba jednou načte pevnou konfigurační cestu a ze stejného snímku vrátí dostupnost i skutečný dokument. Tray nepředřadí další get-config-state ani nečte soubor. Pouze validní výsledek dovoluje vytvořit model kontraktů. Wizard Finish editor neotevírá a žádný dokument mu nepředává.

Rozšířit popis `get-config-document` kolem řádku 359 o druhého určeného konzumenta Contracts. Existující editor nadále smí pracovat s nevalidním textem, Contracts nikoli. Nepřidávat generický parametr cesty nebo náhradní cached dokument.

| Výsledek aktuální akce | Normativní chování Contracts |
|---|---|
| valid | Otevřít z obdrženého DOM, ukázat lokální IP a všechny řádky podle N03. |
| repairable | Neotevřít tabulku, nabídnout Configuration Wizard nebo Edit Config podle stávající matice. Neopravovat automaticky. |
| invalid | Bez tabulky, stručná chyba a možnost Edit Config. Žádné domyšlené klíče nad nevalidním dokumentem. |
| missing | Bez tabulky, nabídnout existující cestu vytvoření konfigurace nebo Wizard, bez zápisu pouhým otevřením. |
| unreadable | Chyba přístupu, žádná náhrada nebo interpretace jako missing. |
| encoding_error | Chyba kódování bez dokumentu, bez náhradních znaků nebo base64 fallbacku. |
| too_large | Chyba velikosti, nepřidělovat buffer podle nedůvěryhodné deklarované délky. |
| config_directory / bezpečnost adresáře | Konkrétní bezpečná chyba pevné cesty v rámci existujícího konfiguračního schématu. Neobcházet ACL ani nepoužívat jinou cestu. |
| IPC nedostupné, busy, deadline, neplatná odpověď | Ukončit loading, bezpečná chyba. Zachovat nezávislý Disconnect, žádné čtení disku tray procesem. |

Do části vytváření souboru doplnit, že OK již otevřeného validního editoru kontraktů může bezpečně obnovit mezitím smazaný cíl stejně jako Save. Je to explicitní publikace celého konceptu, nikoli automatická reakce na missing při otevření.

### N03: Normativní editor Communication contracts

Současný stručný odstavec začínající `Communication Contracts je skutečný editor` v části Win32 UI (historicky `Placeholder má titul`) rozšířit na plnou sekci s následujícím obsahem a odkazem na UX10 až UX12:

> Resizovatelný nemodální formulář má nadpis Communication contracts, krátký popis, Your virtual IP, tabulku Contract / Enabled / Key / Actions a pevnou patičku s validací a OK. Configuration Wizard, Contracts a Edit Config mohou být otevřené a používané současně. Každé okno vlastní svůj celý koncept, vzájemně se nezakazují, neslučují ani neobnovují. X/Escape respektují dirty-state. Více nemodálních Password SHA oken smí současně upravovat i stejný kontrakt, vždy podle remoteIp a generace rodiče. Poslední potvrzený Password SHA pro daný kontrakt vyhrává.
>
> Pro validní lokální adresu ve výchozím 10.99.99.0/24 vytvořit právě 252 řádků: .2 až .254 bez vlastní IP. Pro jiné runtime podporované CIDR použít sdílené funkce prvního a posledního klienta a stejné rezervované adresy, nikoli textový prefix 10.99.99. Řádky se řadí numericky podle remoteIp. Identita položky JSON je remoteIp. Zobrazená identita Contract je kanonické minIP_maxIP, obě IPv4 se porovnávají jako čísla. Self kontrakt se nevytváří.
>
> Globální konfigurační validace přijímá pouze kanonické IPv4 prefixy /24 až /29. Počet klientských adres minus jedna se ověří před alokací, s kontrolou přetečení. Nejvýše 252 řádků se realizuje všemi nativními přístupnými controls ve scrollovatelném obsahu, bez virtualizace, recyklace HWND nebo vlastního UI Automation provideru. /29 má pět klientských adres a čtyři vzdálené peery. /30 má pouze jednu klientskou adresu bez peera, /31 a /32 současný model nepodporuje. Dříve validní /16 až /23 jsou nově neplatné před Connectem i každou publikací, bez automatické migrace nebo zkrácení seznamu. Edit Config zpřístupní text k opravě, není výjimkou z validace Save. Mění se kompatibilita konfigurací, nikoli HTTP, kryptografický wire formát či IPC schema. Skutečná velikost dokumentu a rozpočty front se kontrolují i v podporovaném rozsahu.
>
> Enabled je při načtení true právě pro přítomný validní kontrakt. Jeho přesný payloadKey se zachová. Chybějící kontrakt dostane pouze do konceptu lowercase_hex(SHA-256(ASCII(minIP + "_" + maxIP))) voláním sdíleného defaultPayloadKey. Pouhé předvyplnění ho nezapne. Při OK se enabled řádky vloží pod remoteIp jako objekt s payloadKey, unchecked odstraní. Nezaměňovat tuto sadu s aditivními Wizard defaults 13/14 kontraktů.
>
> Key je editovatelné pole. Validní hodnota je přesně [0-9a-f]{64}, bez trimování a automatického převodu uppercase. Aktivní nevalidní klíč označí konkrétní pole červeně, zobrazí text `Key must contain exactly 64 lowercase hexadecimal characters.` a blokuje OK. Neaktivní nevalidní klíč se označí, ale OK neblokuje a nepublikuje se. Zaškrtnutí takového řádku ihned zablokuje OK, oprava nebo odškrtnutí blokaci odstraní, nejsou-li jiné chyby. Validace zahrnuje také řádky mimo viewport.
>
> Random zobrazí `Replace the key for {contractId} with a new random key?` s Yes/No, výchozí No. No, Escape a X nic nezmění. Yes získá 32 bajtů přes existující CryptoProvider::randomBytes a zapíše 64 lowercase hex znaků pouze do konceptu daného řádku. Crypto chyba zachová původní klíč. Enabled se nemění.
>
> Password SHA otevře samostatný nemodální dialog s titulem Password SHA-256, nadpisem Generate key from password, popisem účelu, polem Password a read-only SHA-256 key. Placeholder CHANGE-THIS-PASSWORD je jen cue text. Při psaní se počítá prostý SHA-256 přesných UTF-8 bajtů, bez normalizace, trimování, saltu, iterací nebo heslového KDF. Jde o kompatibilní deterministický komunikační klíč. Prázdné skutečné pole má hash e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855, nikoli hash placeholderu. OK přenese aktuální hash do cílového Key a nastaví dirty bez změny Enabled. X/Escape nezmění řádek. Heslo se neukládá, neloguje ani neposílá službě.

Připojit limity: uložený JSON nejvýše 524288 bajtů, zakódovaná IPC zpráva včetně obálky nejvýše 4194304 bajtů. Součet textů rozpracovaných Key nejvýše 524288 bajtů. Změna přes limit se odmítne celá s chybou a zachová předchozí hodnotu. Pole musí umožnit vložit nevalidní délku, například 65 znaků, aby nevznikl falešně validní výsledek tichým truncováním. Heslo nejvýše 255 Unicode skalárních hodnot podle přesného pravidla níže, neplatný UTF-16 převod a nepodporované řídicí znaky jednorádkového vstupu odmítnout bez ztrátové úpravy. Ostatní stávající JSON a IPC limity zůstávají.

Přesné doplnění Password SHA pro N03 a UX11: „znak“ znamená Unicode skalární hodnotu. Limit je 255 takových hodnot, nikoli 255 UTF-8 bajtů, UTF-16 code units ani grafémů. Validní surrogate pair se počítá jednou, nespárovaný surrogate se odmítne. Kombinující znak je samostatná skalární hodnota, bez Unicode normalizace. Nejvýše 255 hodnot tedy zabere nejvýše 1020 UTF-8 bajtů nebo 510 UTF-16 jednotek. Nativní vstupní limit nesmí platných 255 znaků mimo BMP zkrátit na 255 UTF-16 jednotek. Nadlimitní vložení či editace se odmítne celé s viditelnou chybou a zachová předchozí vstup, výběr a platný výsledek. NUL a další znak nereprezentovatelný bezeztrátově jednorádkovým textboxem se odmítne, nikoli usekne. Při selhání UTF převodu nebo crypto se aktuální výstup zneplatní a OK zakáže. Hash používá explicitní délku přesných UTF-8 bajtů, žádný trim ani změnu velikosti písmen. Zpracování je lokální synchronní v mezích N09b.

Přesná chyba Key a potvrzení Random výše jsou exact-match texty podle autority UX11, včetně jednotného čísla a skutečného dosazení kanonického minIP_maxIP za {contractId}. Test nesmí použít obecné „this contract“. Password SHA ani Random samy nepřepínají Enabled.

### N04: Publikace, souběh a nejasný výsledek

Za sekci editoru vložit následující závazná pravidla:

> Koncept vlastní původní validní DOM a řádkové změny. Kandidát vznikne patchem pouze communicationContracts, nikoli serializací ClientConfig. Zachovat nesouvisející hodnoty, typy a přítomnost členů, včetně vynechaného runtime a Wizard metadat. Původně chybějící communicationContracts může zůstat chybějící, jsou-li všechny řádky vypnuté. Původně přítomný objekt může zůstat prázdný. Přísné schéma nadále odmítá neznámá pole. Změna whitespace při DOM editaci je dovolena.
>
> OK nejprve vytvoří neměnný kandidát z již potvrzených hodnot. Pak zavře všechna Password SHA okna svého Contracts bez dalších aplikovaných změn a zakáže pouze ukládané Contracts. Lokální patch, serializace a úplná validace předcházejí jednomu asynchronnímu set-config. Při lokálním odmítnutí se bez zápisu obnoví ovládání a zachová koncept. Ostatní editory, tray, status a globální Disconnect zůstávají funkční. Služba zopakuje úplnou konfigurační validaci před temp souborem a publikuje přes stejný serializovaný atomický ConfigStore jako Save a Finish. Otevření, psaní, přepínače ani generátory nezapisují na disk. Potvrzený úspěch zavře okno. Potvrzené odmítnutí zachová koncept a umožní opravu.
>
> Poslední úspěšná publikace celého dokumentu vyhraje. revisionToken není CAS a jeho zastarání není důvodem odmítnutí nebo dalšího potvrzení běžného OK. Externí editace, smazání a znovuvytvoření se automaticky nepřimíchají do konceptu. Bezpečná explicitní publikace může přepsat novější dokument. Nepřepíná aktivní konfiguraci spojení, nevyvolá Connect ani Disconnect, a recovery zachová původní aktivní snímek. Změny se použijí při novém logickém Connectu. Při aktivní VPN zobrazit `Changes take effect on the next connection.`.
>
> Model rozlišuje loading, editing, invalid, saving, saveUnknown a closed. V saving nepovolit další zápis, editaci ani zavření jen po konečný IPC rozpočet. Chybějící, poškozené nebo nekorelované potvrzení po možném odeslání a nejednoznačná chyba publikace vedou do saveUnknown. Nikdy neprohlásit automaticky, že soubor zůstal nezměněný. Pozdní odpovědi mají korelaci instance okna a operace, nemění novější koncept.
>
> set-config nemá dohledatelný commit receipt. V saveUnknown se zápis automaticky neopakuje. Nabídnout Check current configuration, jednu explicitní akci get-config-document. Shoda celého validního DOM s odeslaným kandidátem dovolí oznámit `The current configuration matches your changes.` a zavřít. Není to historický receipt. Při neshodě nebo missing zachovat koncept a nabídnout zavření nebo Save draft again s výslovným potvrzením nahrazení současného celého dokumentu. Je to nový uživatelem schválený zápis. Nečitelnost, encoding_error, too_large a bezpečnostní chyba vyžadují nejprve nápravu. Žádný slepý retry, background kontrola ani automatický merge.
>
> Clean X/Escape zavře přímo. Dirty vyžaduje Discard unsaved changes? s výchozím No. Generační potvrzená akce označí koncept změněný i při náhodné shodě výsledku. V saveUnknown zavření výslovně upozorní, že soubor už mohl být změněn. Zavření není rollback. Vlastněné tajné buffery se best-effort mažou přes existující secureWipe/SecureBuffer, pole i jejich undo se vyprázdní a doba života DOM/IPC kopií se minimalizuje. Nelze slibovat vymazání kopií spravovaných OS. Žádné payload klíče, hesla ani celé dokumenty do logů nebo chyb.

#### N04a: Ruční Exit, logoff a vlastnictví konceptů

Tato následně schválená pravidla doplnit do UX03/12 a do client.md u Exit, souběhu a životnosti tray. Platí pro Contracts i Edit Config při saving/saveUnknown a analogicky pro Wizard committing/commitUnknown. Stavový automat se nesmí řídit jen tím, zda okno právě přijímá vstup.

- Ruční Exit při probíhajícím zápisu asynchronně odloží ukončení do potvrzeného terminálního výsledku nebo původního konečného deadline operace, podle toho, co nastane dříve. Nezakládá nový deadline, neblokuje message loop ani globální Disconnect a neodešle další zápis. Při více rozpracovaných zápisech čeká na jejich vlastní původní termíny, nerozbíhá je znovu sériově. Požadavek Exit nezakazuje nezávislé editory. Zápis zahájený uživatelem později je nová samostatná operace a před dokončením Exit musí být znovu zohledněn.
- Potvrzený úspěch se oznámí pravdivě a příslušné okno se uzavře obvyklou cestou. Potvrzené odmítnutí zachová dirty koncept, konečný timeout po možném odeslání vede do unknown. Nelze tvrdit, že ukončení tray zrušilo již odeslanou publikaci.
- Před dokončením ručního Exit musí uživatel potvrdit všechny zbývající neuložené koncepty, výchozí odpověď je No. Pro dirty použít text Discard unsaved changes? z UX12, pro unknown The file may already have changed. Close without checking the save result? Stejné potvrzení nesmí pro tentýž nezměněný koncept vzniknout podruhé jako následný X dialog. No/Escape/X potvrzení zruší požadavek Exit a ponechá tray i zbývající koncepty. Potvrzení nemění disk. Neaplikované heslo v Password SHA se při uzavření rodiče zahodí, nepřenese se automaticky do kandidáta.
- Systémový logoff nebo shutdown se neblokuje čekáním na zápis nebo potvrzovacím dialogem. WM_QUERYENDSESSION povolí ukončení, pouhý dotaz dosud nemaže koncept. WM_ENDSESSION s TRUE provede best-effort úklid místní paměti a ukončení, s FALSE zachová relaci. Neproběhne retry, automatický Save, rollback ani Disconnect. Odeslaná službová operace může dokončit svou publikaci nezávisle na zániku tray.
- Při skutečném ukončení zneplatnit generace oken/operací, ukončit místní čtečky a timery jejich bezpečnou cestou, vyčistit vlastní hesla, klíče, DOM, kandidáty i edit/undo buffery podle UX13. Uvolnění nesmí předběhnout uživatele bufferu či callback. Při logoff se úklid provede v dostupném čase bez prodlužování ukončení systému. Wizard-close je best-effort žádost o úklid, nedoručení dále pokryje existující lease. Uložený config, globální VPN, služba i ostatní uživatelské relace zůstávají nedotčené. Best-effort wipe není záruka smazání kopií v OS ani při násilném pádu procesu.
- Unknown se po novém startu nevydává za potvrzený historický výsledek a kvůli němu nevzniká perzistence konceptu či tajemství. Běžný start nadále pouze získá status, čtení konfigurace je až výslovná akce.

### N05: Connect, odpočet a použití existujícího IPC

Nahradit prezentační popis kolem řádků 817 až 823 a aktualizovat odstavce o pollingu:

> Connect má podle UX05 sekce Connection status, Connection details a Connection checks. Během připojování ukazuje Connecting a tři body Server URL, Server secret a Access key. Přijímá vždy celou trojici checkpointů z jednoho existujícího `get-status` snímku a neakumuluje dřívější úspěchy. Velký štít v obsahu nahrazují karty a typografie mockupů, nativní ikony se zachovají. Po otevření okna tray jednou zavolá existující `get-config` a z platného JSON zobrazí pouze `serverUrl` a lokální `virtualIp`. Celý config včetně citlivých hodnot může být IPC přenesen do tray, ale nesmí se logovat a jeho dočasné kopie mají omezenou životnost. Nejsou-li hodnoty dostupné, pole ukazuje bezpečný nedostupný stav.
>
> Successfully connected lze zobrazit až při skutečném connected, připraveném tunelu a běžících pracovních rolích podle autoritativního stavu služby. Ukázat zelený panel Connected successfully, This window will close automatically a Closing in 3 seconds. Od monotónního okamžiku skutečného zobrazení výsledku odpočítávat ceil(zbývající ms / 1000), tedy 3, 2, 1, se správným singular second. Zavřít při uplynutí 3000 ms nejpozději v dalším 500ms ticku. Neodvozovat zbývající čas odečítáním počtu ticků. Terminální výsledek daného okna je zmrazený, failure se sám nezavírá. Zavření místního okna nikdy neodpojuje službu.
>
> Failure používá Connection failed, bezpečný popis a dolní chybový panel podle UX05. Zachovat mapování endpoint_unavailable_or_invalid, server_verification_failed, access_key_rejected, tunnel_start_failed, communication_failed a communication_timeout. Odmítnutí Access key se tvrdí jen při skutečné autorizací potvrzené příčině. Transportní chyba poslední kontroly není automaticky odmítnutý klíč. Selhání tunelu může mít všechny kontroly zelené. Timeout označí právě vyhodnocovaný bod, následující zůstanou pending. Síťové pořadí health → info → data → tunnel, recovery a jejich limity se nemění.

Původní zákaz číselného odpočtu se odstraní, nikoli pouze doplní výjimkou. Zákaz URL v libovolném progress textu se nahradí N01. Konkrétní anglické popisy a nápravy všech šesti kategorií určuje tabulka UX05, raw Error::message se nesmí zobrazit náhradou za chybějící mapování.

Existující `get-status` zůstává schématem i významem beze změny a vlastní 500ms polling progress okna nadále získává stav a tři checkpointy. Jednorázový `get-config` pro detaily je samostatné čtení aktuálně uloženého platného dokumentu. Není atomicky svázané s aktivním snapshotem Connectu. Při souběžné externí změně konfigurace proto může zobrazená URL/IP patřit novější uložené konfiguraci než právě aktivnímu spojení. Tento trade-off je výslovně přijatý za účelem zachování stávajícího IPC. Tray nikdy nedopočítává checkpointy z configu a celý config neloguje.

V rámci tohoto UX plánu se nezavádí žádný nový progress příkaz. Nemění se službový stavový engine, parser nebo builder kvůli detailům Connect. Stávající 4MiB limit IPC zprávy a všechny existující validace zůstávají.

### N06: Wizard Finish pouze uloží a zavře Wizard

V úvodu Configuration Wizard kolem řádku 398 nahradit staré protichůdné tvrzení o navazujícím okně:

> Úspěšný potvrzený Finish uloží konfiguraci, pravdivě oznámí úspěch, uzavře Wizard relaci a zavře Wizard. Communication contracts automaticky neotevře a žádný dokument mu nepředává. VPN nepřipojí. Nevzniká rezervace IP ani nový wire endpoint.

Řádek `úspěšný Finish` v přechodové tabulce kolem řádku 420 nahradit: `receipt confirmed → oznámit úspěšné uložení → wizard-close → zavřít Wizard, bez otevření dalšího okna`. Ztracenou odpověď Finish nadále řeší existující `wizard-get-commit`, nikdy druhý Finish.

Pozdější otevření Communication contracts vznikne pouze novou explicitní volbou uživatele z tray menu. Tato ruční akce použije jediný existující `get-config-document` a při `availability=valid` otevře editor nad aktuálně uloženou konfigurací. Nejde o pokračování Wizardu, handoff konkrétní publikace ani čtení souboru tray procesem.

Žádný nový příkaz pro čtení dokumentu konkrétní Wizard publikace se nezavádí. Nevzniká publikovaný handoff buffer v živé relaci, dodatečná až 4MiB handoff RAM, nové handoff error kódy, parser/builder/dispatch, move-owned callback, obnova lease během předání, idempotentní čtení publikovaného dokumentu ani testy automatického předání. Chyba otevření Contracts po Finish není relevantní, protože Finish editor neotevírá.

### N07: Stručný Wizard, akce a zachované bezpečnostní informace

V úvodu a větě `První krok nemá Back` kolem řádků 398/404 nahradit pravidlem: na Server URL je Back viditelný, ale zakázaný, na odpojovací gate není Back a nelze se na ni vrátit. Ostatní přechody Back se nemění. Uživatelský popisek Next nahradit **Verify & continue** v síťových krocích, **Disconnect & continue** na gate. Interní název události Next, attemptSequence a význam ručního opakování mohou zůstat, text chyby musí použít skutečný nový popisek.

Nahradit dlouhé nápovědy sekce Win32 Wizard UI a její tabulku minimálních textů kolem řádků 601 až 622 obsahem UX08/UX09. Nadpisy kroků: Before setup, Server URL configuration, Server secret configuration, Virtual IP and access key, Communication contracts. Sidebar zachová čtyři hlavní kroky a nepovolí přeskakování. Reset vlevo, Cancel a aktuálně platné Back / Verify & continue / Finish v patičce. Přesné anglické copy jednotlivých polí je v UX09 a implementuje se jednou jako sada prezentačních konstant.

Gate musí stručně říci, že Disconnect odpojí VPN všem uživatelům počítače a Wizard ji automaticky nepřipojí. Explicitní Disconnect & continue je souhlas po zobrazení varování, odstranit současný druhý duplicitní Yes/No. Zachovat Cancel i když na mockupu není. Během skutečného odpojování čekat na globální disconnected/idle a teprve potom pokračovat. Reset/Cancel zachovají existující bezpečnou sémantiku.

V sekcích Server secret a Virtual IP přesunout switche pod příslušná pole podle obrázků, změnit `Před IP polem` a `Před Access Key` kolem řádků 467/469 na `Pod ... polem`. Checkbox může být vizuálně switch, sémantika boolean se nemění. Explicitní uložená `*InWizard` metadata mají vždy přednost. Odstranit případné starší věty, že existence klíče vždy vypíná default: false pro přítomný klíč a true pro chybějící platí pouze jako fallback při absenci příslušného metadata. Shoda hodnoty s defaultem sama switch nezapne.

Identity karta musí viditelně obsahovat upozornění:

> Each Verify & continue check may permanently remove waiting messages for this IP, even if no result arrives. Messages are discarded, not delivered to the VPN. Checks are never retried automatically.

Zachovat také stručné informace, že IP není rezervována, duplicity IP protokol dovoluje a jejich kolize jsou odpovědností provozovatele. Lokální IP nesmí být současně protějškem vlastního kontraktu. Nezavádět novou kontrolu povinné globální unikátnosti. Neodsunout tyto informace jen do tooltipu. URL nápověda doporučí phpvpn.php, ale nepřidá povinnou příponu. `MODIFY-THIS-URL` zůstává Wizard placeholder odmítnutý před DNS. Šablona Edit Config se nemění. Automatická IP zůstává podle současného algoritmu 10.99.99.2 až .15 s omezeními zachovaného CIDR, Wizard doplňuje pouze současné aditivní defaults 13/14. Poslední karta ukazuje Existing contracts, Missing defaults a To add on Finish. Vypnutí doplňování nic neodstraní.

Lease 15 s / 60 s, nejvýše osm relací, konečné deadline, nezávislé Cancel/Disconnect, clientAttemptId, attemptSequence, epochy, revokace důkazů, žádný automatický síťový retry a dohledání nejasného Finish zůstávají v platnosti. V rámci tohoto UX plánu nevzniká žádný nový IPC příkaz. Původní 500ms progress timer i jeho existující `get-status` zůstávají podle N05.

### N08: Normativní akceptační testy a prezentace ostatních oken

V `client.md` změnit konfiguraci/Wizard test 17 kolem řádku 651 tak, aby potvrzený Finish pouze oznámil úspěšné uložení, zavřel Wizard, neotevřel Contracts a neprovedl Connect. Doplnit testy N02 až N04 a N06 podle matice A níže. Zachovat test, že otevření menu nečte konfiguraci, a rozlišit pozdější explicitní otevření Contracts jedním `get-config-document` od Statistics jedním `get-config-state`.

Windows test 21 kolem řádku 859 přepsat na karty mockupu, friendly labels a viditelný odpočet 3/2/1 s měřením od zobrazení úspěchu. Test 22 ponechá bezpečné rozlišení příčin, ale požaduje Connection failed a dovolí URL/IP pouze v Connection details. Test 35 zachová nezávislé timery a editorové posuvníky, jeho plošný zákaz URL v progress nahradí N01/N05. Nadále testovat zákaz klíčů, raw chyb a cest. Žádný test nesmí očekávat odstraněný zákaz odpočtu nebo prázdný placeholder.

Edit Config podle UX06: zachovat výchozích 1080 × 560 DIP, skutečný dokument a autoritativní cestu ze služby, oba posuvníky bez wrap, Reset vlevo a Cancel/Save vpravo. Zachovat asynchronní Reset, bezeztrátový NUL režim, dirty-state, Save a ochranu proti pozdním odpovědím. Sdílená EditorWindow pro Show Info nesmí dostat konfigurační nadpis, cestu nebo akce.

Statistics podle UX07: Data polling se třemi čítači, Serialized data se dvěma, IPv4 traffic se čtyřmi. Vše z jednoho posledního validního get-status, beze změny jednotek a významu. Čísla zobrazit přesně jako uint64 bez převodu přes double, bajty s B a bez automatické změny jednotky. Při výpadku označit poslední data jako zastaralá, nenulovat čítače. Žádný vlastní timer, síť nebo disk po otevření. Zachovat současné invariance součtů a lifecycle čítačů.

### N09: Přenosnost, zdroje a rozsah validace

Do implementačních pravidel tray doplnit lehkou sdílenou vrstvu podle UX01 až UX03: nativní Win32 vstupy, minimální vlastní kreslení, bez nové velké závislosti. Designové konstanty, layout, ContractsModel, odvození klíčů a validační pravidla bez HWND. Win32 vlastní okna, message loop, DPI, focus a dostupnost. macOS/Linux mohou sdílet modely a testy, nativně přeimplementují pohledy při stejné sémantice, tento úkol tam nepřidává hotové GUI.

Vyžadovat srovnání velikosti zdrojů a tray EXE před/po, absolutně i procentně, stejnou architekturu a konfiguraci i informaci o podpisu. Bez libovolného numerického limitu, násobný růst kvůli pouhé vizuální vrstvě odmítnout. Rychlé ověření dle závěru tohoto plánu nenahrazuje plné release testy a nemění autoritu distribution.md pro řádně podepsané vydání.

#### N09a: Sdílené umístění oken, rendering a jazyk

Následující pravidla současně vložit do UX00/02/03 a příslušných Win32 částí client.md, nahradit opačná tvrzení. Helper je jediný pro všechna tray okna, přesná UI copy je autoritou tray_UX.md.

Všechna tray okna používají jediný sdílený helper výběru monitoru. Nové dítě použije monitor platného rodiče. Hlavní okno z tray menu použije monitor uloženého bodu, u kterého se menu skutečně zobrazilo. Bez rodiče i bodu menu následuje monitor platného foreground okna, potom monitor pod kurzorem a nakonec primární monitor. Okno se vystředí do pracovní plochy mimo taskbar. Souřadnice jsou podepsané, včetně monitorů vlevo či nad primárním. Rozměry se počítají pro DPI cílového monitoru, vnější rám se přičte k níže uvedené klientské ploše a celek se omezí na dostupnou pracovní plochu.

Reaktivace existující okno nepřesune na monitor nového kliknutí. Minimalizované okno obnoví. Je-li celé v použitelné pracovní ploše kteréhokoli dostupného monitoru, zachová přesnou polohu i velikost. Po odpojení monitoru nebo ztrátě použitelné polohy přesune okno na nejbližší dostupný monitor a změní geometrii jen o nezbytné minimum. Kontrola se provede při reaktivaci a změně DPI, rozlišení či pracovní plochy. Samotné připojení monitoru viditelné okno nepřesune. Rozměry níže jsou přesné výchozí klientské plochy v DIP bez titulku a vnějšího rámu, nikoli fyzické rozměry PNG.

Každé okno vlastní malou RAII cache opakovaně používaných fontů, ikon, štětců a per. Vznikne při inicializaci pro cílové DPI, obnoví se pouze po skutečné změně DPI a jednou se uvolní při zániku okna. Běžný `WM_PAINT` nesmí načítat ikony ani vytvářet GDI prostředky. Invalidují se jen vizuálně změněné regiony. Úplná invalidace je dovolena při vytvoření, změně DPI, zásadním přepočtu layoutu nebo obnově po chybě dávkového přesunu. Tray uchová poslední skutečně publikovanou identitu ikony a přesný tooltip, `Shell_NotifyIcon(NIM_MODIFY)` volá jen při jejich změně, nikoli při každém shodném status snímku.

Tato verze tray je pouze anglická. Lokalizace, volba jazyka a reakce na jazyk Windows jsou mimo rozsah. Nevznikne překladový framework, resource katalog ani stabilní ABI lokalizačních klíčů. Anglické konstanty smí sdílet malý společný textový katalog. Layout nadále měří skutečný text a umožňuje zalamování, Statistics používají anglické seskupení tisíců nezávisle na systémovém locale. Budoucí lokalizace vyžaduje samostatnou specifikaci.

#### N09b: Přesná vizuální a runtime brána

Následující metodiku vložit do UX14, v client.md ji závazně odkázat z testovacích povinností. Číselné limity v tabulce jsou jediným zdrojem pro implementaci a matici A, staré viewportové nebo subjektivní výkonové podmínky odstranit.

Vizuální akceptace všech jedenácti mockupů se provede při 100 % DPI nad přesnou klientskou plochou bez systémového titulku a vnějšího rámu. Výchozí plochy jsou Connect 940 × 680 DIP, Edit Config 1080 × 560 DIP, Statistics 1000 × 650 DIP, Wizard 1000 × 680 DIP, Communication contracts 1180 × 680 DIP a Password SHA-256 540 × 360 DIP. Tabulku UX03 s nimi sjednotit. Všechny stavové mockupy stejného okna mají stejnou plochu. Reference se přiřadí celé této klientské ploše a tento převod se zaznamená, nikoli libovolnému screenshotu včetně rámu.

Poloha a rozměry jednotlivého prvku mají toleranci nejvýše 6 DIP. Rozdíl způsobený skutečnými metrikami nativního fontu, zalomením či baseline textu má toleranci nejvýše 8 DIP, nesmí změnit pořadí, překrýt obsah, useknout významný text ani skrýt akci. Přítomnost sekcí, pořadí, exact-match texty, role, sémantické barvy, dostupnost patičky a povinné akce toleranci nemají. Titulek, rámeček, anti-aliasing a nativní kresba controls se neporovnávají pixel po pixelu.

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

## Pořadí následné implementace

### F00: Výchozí stav a kontrakty změny

1. Znovu přečíst aktuální instrukce a ověřit git status. Zachovat cizí změny, zaznamenat HEAD, dostupné build prostředí, stávající velikosti a hash distribuce. Nepracovat v dist_old_do_not_use.
2. Porovnat nové změny kódu proti výchozímu auditu výše. Vyhledat všechna tvrzení `placeholder`, `prázdné`, `Nesmí zobrazovat číselný odpočet`, `nemá Back`, zákazy URL a popisky Next v client.md a testech. Připravit seznam náhrad N01 až N09, zatím netvrdit implementované chování.
3. Potvrdit existující limity z ConfigStore, IPC a IPv4 helperů. Ověřit, že implementace používá pouze existující IPC příkazy a nemění jejich uzavřená schémata.
4. Zaznamenat, že N05/N06 nevyžadují nový IPC příkaz ani novou službovou dispatch cestu. Automaticky nenahrazovat distribuční service EXE. Případné jiné změny služby mimo tento UX plán nesměšovat s touto implementací.

Výstup: reprodukovatelný baseline a seznam dotčených míst. Závislosti dalších fází vycházejí z tohoto auditu.

### F00a: Globální validace CIDR a navazující dokumentace

Před vytvářením Contracts změnit client/src/core/protocol/ipv4.h (kMinPrefixLength z 16 na 24 a komentář), ověřit Ipv4Cidr::parse v ipv4.cpp a aktualizovat text v client/src/core/config/config_validator.cpp z „16 to 29“ na „24 to 29“. Neměnit rezervaci sítě, první host adresy a broadcastu ani nejdelší prefix 29. Sdílená validace musí odmítnout nepodporovaný prefix v plném i repair režimu, při načtení před Connectem a ve všech publikačních cestách včetně set-config, Wizard Finish a startup defaultu. Pravidlo není pouze gate v tray. Žádná nová IPC dispatch cesta ani změna JSON obálky.

V client.md uvést rozsah v Konfiguračním souboru a všechny konfigurační testy, upozornit na nekompatibilitu dříve platných /16 až /23 bez migrace. Výrok „staré konfigurace jsou nadále přijímány“ u Wizard IPC omezit na konfigurace splňující aktuální validaci, absence metadat zůstává podporovaná. Testy, které používají širší rozsah pouze jako fixture jiné vlastnosti, převést na platný /24 až /29 a zachovat účel. Samostatné negativní testy ponechat pro všechny nepodporované prefixy. Zkontrolovat runtime kvóty, Wizard kandidáty, dokumentové testy a akceptační matice, historické zprávy o již provedených testech nepřepisovat.

Build služby je nyní potřeba kvůli sdílenému validátoru, přestože problémy 1 a 2 nezavádějí nové IPC. Ověřit službový cíl bez registrace, spuštění nebo nasazení služby. Akceptace A04/A11/A22.

### F01: Lehký vizuální základ a layouty

Dotčené soubory: `client/apps/tray/window_utils.h`, `layout.*`, `wizard_layout.*`, `editor_window.*`, `progress_window.*`, `statistics_window.*`, `wizard_window.*`, `contracts_window.*`. Nové malé soubory například `ui_style.h`, `ui_resources.*`, `ui_controls.*`, bez třetí strany.

Oddělit constexpr DIP/geometrii a sadu textů od Windows resource cache. Fonts/pens/brushes držet RAII na okno/DPI, nevytvářet při každém WM_PAINT. Měřit text, definovat paint/invalidation hranice, použít WS_CLIPCHILDREN a současnou bezpečnou DeferWindowPos větev. Standardní EDIT/BUTTON zachovají výběr, clipboard ovládaný uživatelem a keyboard. Owner-draw nesmí odstranit přístupné jméno, roli, focus či checked stav. WM_DPICHANGED přepočítá metriky a použije doporučený rect jen v souladu s N09a, s minimálním omezením do použitelné pracovní plochy. Sdílený helper implementuje celé pořadí monitorů a reaktivaci N09a. Cache a NIM_MODIFY ověřit podle N09a/b.

Implementovat pevnou patičku, scroll obsahu, breakpointy a minimální dostupnou plochu podle UX03. Ve Wizardu neregenerovat všechny HWND při každé aktualizaci statusu, aby nezanikaly focus, výběr a undo. Aktualizovat N01 a N09 současně s použitelnou sdílenou vrstvou. Výstup ověřit cílenými geometry testy, ne pixelovým porovnáním samotných čísel bez reálného okna.

### F02: Přenosný model kontraktů a crypto

Nové moduly `client/src/core/config/contracts_document.*`, `client/src/core/presentation/contracts_model.*` a `client/src/core/presentation/password_key_model.*`. DOM patch, serializace a shared validace patří do config, čisté modely do presentation jako samostatné objekty existující statické `phpvpn-core`. Nevznikne nová knihovna, DLL, framework ani třetí strana. Modely nesmějí zahrnovat Windows hlavičky ani znát HWND, GDI, message loop, controls či tray lifecycle. Použijí přenosné hodnoty, Ipv4Address, validační výsledky a CryptoProvider. Service a CLI nepřitáhnou nepoužité modelové objekty. Windows okna, layout, focus, scroll a adaptéry událostí zůstanou v apps/tray. Zapojit explicitní zdrojové seznamy v `client/src/CMakeLists.txt`, `client/apps/CMakeLists.txt` a cílené testy v `client/tests/CMakeLists.txt`.

Datový model: původní DOM/text s omezeným životem, configPath a informational revision, numerické localIp/CIDR, vektor `ContractRow {remoteIp, enabled, keyText, keyValidity}`, přehled počtu aktivních chyb a dirty flags, identita focusu `remoteIp + column`, save generation a immutable odeslaný kandidát. Neuchovávat duplicitní kopii celého DOM v každém řádku. Identifikátor páru dopočítat nebo jednou uložit jako krátký řetězec. Řádky se netřídí podle zobrazeného textu.

Před vytvořením vektoru odmítnout rozsah mimo /24 až /29 a počet nad 252. Použít IPv4 helpery a `core/wizard/wizard_defaults.*::defaultPayloadKey`, ne duplikovaný hash. Crypto sdílet přes CryptoProvider, stávající CNG implementace zůstává. Password model pracuje s nejvýše 255 Unicode skalárními hodnotami podle N03, převádí striktně na přesné UTF-8 a počítá synchronní SHA-256, pracovní buffery po návratu maže. Tvorba modelu a místní patch/serializace/validace mohou zůstat na UI vlákně pouze při splnění N09b a cílového UX14, cílem Save je přibližně 1 s, absolutní strop 2 s bez času IPC. Nevyhovující cestu optimalizovat nebo přesunout do jedné omezené worker úlohy s generací, nikoli oslabit akceptaci. Čekání na službu musí být vždy asynchronní.

Sestavení kandidáta patchuje původní DOM a validuje celek, zachová absent/present rozdíly. Ukládání zatím není součást modelu. Přidat N03 a datovou část N04 do client.md spolu s implementací. Akceptace A03 až A09 a A11.

### F03: Connect nad existujícími `get-config` a `get-status`

Dotčené moduly jsou pouze tray čtečky/modely a prezentace Connect. Po otevření okna jednou použít existující `get-config`, z platného JSON převzít `serverUrl` a lokální `virtualIp`, celý dokument nelogovat a po extrakci omezit životnost jeho kopií. Stávající progress reader dál volá existující `get-status` každých 500 ms a z něj přebírá stav i tři checkpointy.

Nepřidávat parser, builder, dispatcher ani službový snapshot. Současné `get-status` testovat beze změny schématu a bez URL. Reader musí oddělit chybu jednorázového config čtení od výsledku bootstrapu. Testovat přijatý nesoulad uložených detailů vůči aktivnímu snapshotu a zákaz logování celého configu. Aktualizovat N05, N01 bezpečnost a odpovídající N08 Connect testy současně. Akceptace A14 až A17 a A22.

### F04: Finish bez automatického otevření Contracts

V `client/apps/tray/wizard_window.*` a `main.cpp` zachovat potvrzení úspěšné publikace a existující uzavření relace. Po confirmed zobrazit pravdivý úspěch, zavřít Wizard a neotevírat Contracts. Nevytvářet ani neuchovávat publikovaný dokument pro UI.

Neměnit callback na move-owned dokument a nepřidávat nový IPC, error kódy, handoff parser, dispatch ani lease chování. Pozdější ruční Contracts z menu je nezávislá akce podle F06. Přidat N06 a změny úvodu/přechodové tabulky `client.md` v této fázi. Akceptace A12/A13/A22.

### F05: ContractsWindow, všechny řádky a nemodální Password SHA

Přepsat `contracts_window.*`, přidat `password_sha_window.*`. show/open dostane owner a validovaný dokument z ruční menu akce. Opakované otevření aktivuje stejné okno bez čtení a bez resetu dirty modelu. Header a footer jsou pevné, tabulka scrolluje. Pro všech nejvýše 252 řádků vytvořit nativní přístupné controls, bez virtualizace, recyklace HWND nebo vlastního UIA provideru. Tab/Shift+Tab zachová úplný vizuální pořádek a odkryje cílový ovladač. Caret a focus se při resize/DPI zachovají. Identita každé akce je remoteIp a generace konceptu, nikoli index nebo handle.

Při 252 řádcích ověřit celou tabulku proti N09b a cílovému UX14, včetně +1200 GDI/USER objektů. Čtečka obrazovky enumeruje všechny skutečné controls. Defaulty neprovádějí filesystem ani HTTP. Random potvrzení váže cíl před otevřením a dočasně zakáže jen svůj rodičovský formulář. Password SHA je nemodální, více oken včetně stejného cíle nezakazuje Contracts. Každé OK ověří remoteIp a generaci rodiče, poslední potvrzený hash vyhrává. Hlavní Contracts OK zmrazí kandidáta z potvrzených hodnot, zavře všechny své Password SHA bez aplikace dalších změn a až potom zakáže ukládané Contracts. Zavření rodiče vyřadí jeho generaci, uklidí děti, pole/undo i tajné buffery. Pozdní výsledek nesmí změnit nové okno. Testy modelů běží v core bez vytváření HWND.

Propojit s F02 a F01, implementovat UX10/11 a N03. Akceptace A03 až A10, A19/A20. Na konci této fáze lze ověřit lokální koncept bez skutečného zápisu.

### F06: Načtení, Save controller a chyby

V `main.cpp` oddělit Contracts od `requiresConfigState`, použít jedinou WizardReader/get-config-document cestu a N02 matici. Není dovoleno nejdřív ověřit config a poté načítat nový dokument. Stávající `startCommand(set_config)` a `finishCommand` jsou svázané s editor generation/setSaving, nepřipojovat k nim Contracts přes sdílené nahodilé flagy. Vytvořit účelový řadič `ContractsSaveController` s jedním požadavkem, generací okna a striktním parserem výsledku. Lze extrahovat sdílenou transportní část, ne sloučit lifecycle editorů.

Použít existující `set-config` request schema s právě čtyřmi členy: `ipcVersion:1`, čerstvé UUIDv4 `requestId`, `command:"set-config"` a `config:string` s celým kandidátem, nikoli vnořeným JSON objektem. Úspěch je právě acknowledgement `{ipcVersion:1, requestId:<shoda>, result:"ok"}`. Odmítnout extra pole/duplicity/typové odchylky, Error parsovat podle existující bezpečné obálky. Nenavrhovat pro tento Save Wizard get-commit, který nepatří této operaci. Zachovat úplnou službovou validaci a serializační ConfigStore save, žádná nová zápisová filesystem cesta.

Existující get-config-document má pouze tři základní request členy. Jeho result obsahuje právě `configPath:string`, `availability:enum`, `reasonCode:string|null`, `revisionToken:string|null`, `document:string|null` podle současného striktního parseru. Missing má null dokument, valid/repairable/invalid skutečný text, nečitelnost/kódování/velikost se vrací jako existující strukturovaná chyba bez dokumentu. Tyto odpovědi se nepřevádějí na valid podle pouhé přítomnosti textu. Pro ruční Contracts načtení i Check current configuration zachovat 5000ms celkový deadline WizardReader. Pro set-config zachovat 30000ms celkový deadline CommandReader, včetně čekání, přenosu a čtení odpovědi. Pro progress použít konečný rozpočet současné `get-status` čtečky bez překryvu požadavků. Žádný dílčí read nebo polling tyto absolutní termíny neobnovuje.

Implementovat N04 unknown workflow, DOM rovnost porovnává význam objektů nezávisle na pořadí členů, ale zachová typy, pole a absent/present rozdíly. Nové potvrzené Save draft again je samostatná generace. Stav saving má stejný konečný transportní rozpočet jako současný editor, po deadline je řešitelný unknown, nikoli věčně zakázané X. Při zaručeném neodeslání vrátit editing, nejistotu nezaměnit za potvrzené odmítnutí.

Aktualizovat N02/N04/N04a v client.md a UX03/12, dokončit menu a implementovat společné ruční Exit/logoff pro všechny konfigurační editory. Akceptace A01/A02/A10/A11/A18/A22.

### F07: Dokončení vzhledu Connect, Edit Config, Statistics a Wizard

Propojit progress model s F03. Uložit deadline skutečně zobrazeného success, počítat label 3/2/1, ponechat failed do ručního zavření. Přesná mapa safe error → popis/panel je jedna sdílená tabulka, ukázkový odmítnutý Access key není univerzální text. Renderovat dle connect1/2 bez těžkého bitmapového pozadí.

V `editor_window.*` upravit title/path header a footer layout. Zachovat existující EditorDocument model, Reset generation a raw NUL reprezentaci. U Show Info zvolit samostatný prezentační režim. Aktualizovat layout testy, které nyní čekají Save/Reset/Cancel vedle sebe vpravo.

V `statistics_window.*` vytvořit tři karty z jedné předané hodnoty TrayStatus. Doplnit prezentační stav dostupnosti/čerstvosti, protože pouhé ignorování nevalidního snapshotu nestačí pro stale label. Counter model zůstává ve službě.

Ve `wizard_window.*`, layoutu a modelových textech zavést UX08/09 a N07. Nepřesouvat ověřování do UI. Odstranit duplicitní potvrzení Disconnect po explicitním gate tlačítku, nepřidat automatický disconnect při pouhém otevření. Nová srozumitelná copy zachová nevratné spotřebování zpráv, kolize, lease a unknown Finish. Aktualizovat N07/N08 a zkontrolovat všechny viditelné odkazy na Next. Akceptace A14 až A21.

### F08: Cílené ověření, dokumentace a měření

Přidat malé testy do stávajícího `phpvpn-client-tests`, například samostatné sady ContractsModel, ContractsDocument, PasswordKey a ContractsSave. Rozšířit relevantní současné `test_tray_progress.cpp`, `test_tray_layout.cpp`, `test_wizard_layout.cpp`, `test_wizard_document.cpp`, `test_wizard_ipc.cpp` a editorové testy. Nevytvářet sady pro odstraněný progress IPC nebo Wizard handoff. Nepsat testy pouze kopírující konstanty, testovat invarianty, hranice, příčiny chyb a chybné pořadí událostí.

`client/tests/ui/tray_ui_tests.cpp` používá fake pipe, nikoli skutečné ProgramData. Doplnit krátké smoke pro nové obrazovky a případné linkage progress/statistics v UI cíli. Zachovat čistě syntetická data. Vizualizaci všech jedenácti referencí ověřit přesně podle N09b, s jeho klientskými plochami, tolerancemi, DPI a metadaty reportu. Evidence OS, commit, build, DPI, scénář a výsledek může být textová, žádné nové binární screenshoty do repozitáře.

Provést audit N01 až N09 proti konečnému client.md, UX a skutečnému kódu. protocol.md se nemění: nejde o nový wire formát, AAD/HKDF/IV, serverový endpoint ani alokaci IP. Změny jsou v UI, použití stávajícího lokálního IPC a globální validaci konfigurace. Pokud implementace odhalí skutečně nutnou wire změnu, nejprve ji zdokumentovat jako nový problém, neprovést ji v této opravě automaticky. Dokončit měření a vývojové workflow F09 podle poslední části, bez kopie do dist.

### F09: Rychlá opakovaná iterace GUI bez distribuční kopie

Tato fáze je závazný návrh k budoucí implementaci, nikoli existující hotový příkaz. Navazuje na F01 až F07 a testovací infrastrukturu F08. Cílem je krátký cyklus upravit GUI → spustit jeden příkaz → prohlédnout skutečné okno → zavřít → opakovat. Při vytvoření základního runneru lze začít se současnými okny a zbývající preview doplnit společně s příslušnými fázemi. Finální akceptace vyžaduje všechny níže uvedené pohledy. Nepřidávat hot reload, watcher, automatické instalace, nový framework ani nový build systém.

#### Ověřený současný základ

| Existující součást | Co skutečně umí | Co je nutné doplnit |
|---|---|---|
| client/CMakePresets.json | Ninja/MSVC x64, debug a release, kde release znamená RelWithDebInfo. Výstupy work/build/<preset>. | Presety zachovat. Izolovanou GUI konfiguraci vytváří nový runner, nepřepisuje cache produkčních presetů. |
| client/tools/build-x64.ps1 | Načte MSVC prostředí, konfiguruje a sestavuje celý preset, volitelně celý CTest. | Pro běžnou GUI iteraci nepoužít all/CTest. Nový runner sestaví pouze explicitní UI target. |
| client/tools/run-tray-ui-tests.ps1 | Samostatný work/build/ui-debug, opt-in UI target, -Interactive nebo substring -Filter, soukromá náhodná pipe. | Zachovat stávající testovací použití, sdílet případné malé pomocné funkce, neudržovat dvě kopie backendu. |
| client/tests/ui/tray_ui_tests.cpp | Skutečné Wizard/Editor/Contracts třídy a deterministický in-memory backend. --interactive, interní --preview secret/identity/identity-small/contracts. | Současné preview contracts znamená krok Wizardu před Finish, nikoli plný editor. Časově omezený preview režim končí po 45 s. Chybí obecný výběr všech nových oken. |
| client/tests/CMakeLists.txt | PHPVPN_BUILD_UI_TESTS=ON vytváří phpvpn-tray-ui-tests, běžně mimo CTest. | Připojit skutečné Progress/Statistics/Password a sdílený tray controller/resources potřebné pro úplné preview. |
| client/tools/dev-install.ps1 | Administrátorská registrace skutečné služby z buildu. | Není běžnou GUI cestou, protože zasahuje do instalace a služby. |
| apps/tray/main.cpp | Pevný produkční mutex Local\phpvpn-tray-session, pevná službová IPC cesta. | Samotné spuštění druhého produkčního EXE není izolovaný preview. Vývojový host musí mít vlastní instanci a všechny readers směrovat do fixture. |

Uvedené vlastnosti byly v této dokumentační úloze zjištěny čtením zdrojů, nikoli novým buildem nebo spuštěním UI. Staré UI README, které slibuje automatické Contracts po Finish, při implementaci aktualizovat současně s testem podle N06, nepřebírat toto staré chování.

#### Jediný uživatelský vstup

Vytvořit client/tools/dev-tray.ps1 s těmito parametry:

| Parametr | Výchozí hodnota a smlouva |
|---|---|
| -Mode | Preview, povolené Preview / Test / Build. |
| -View | Contracts, jen pro Preview. Povolené Tray / Connect / Editor / Statistics / Wizard / Contracts / Password. |
| -Scene | Default, jen pro Preview. Povolené kombinace určuje tabulka níže. Neznámá kombinace skončí chybou před buildem. |
| -Configuration | Debug, povolené Debug / Release. Release je skutečné CMAKE_BUILD_TYPE=Release pro N09b, nemění význam stávajícího presetu release. |
| -Filter | Povinný neprázdný substring pouze pro Mode Test. Nesmí být ignorován, nula vybraných testů je chyba. |
| -Reconfigure | Volitelné, vynutí konfiguraci bez smazání cache a bez clean buildu. |

Runner funguje z libovolného pracovního adresáře. Kořen odvodí od svého souboru, cesty nepřebírá z cwd. Debug používá pouze work/build/tray-dev-debug, Release work/build/tray-dev-release. Při prvním běhu nebo chybějící cache/Ninja souboru provede konfiguraci, jinak inkrementální build. Změnu CMake zdrojů zachytí běžná autoregenerace Ninja. Nesprávný existující generator, architektura, source directory nebo konfigurace skončí konkrétní chybou s cestou, bez automatického mazání cizí cache.

Načtení MSVC AMD64 prostředí použije existující vswhere/VsDevCmd postup, ale ověří dostupnost a úspěšný návrat každého nástroje. Ověří CMake alespoň 3.28, Ninja a cl.exe. Nestahuje ani neinstaluje toolchain a nemění globální PATH nebo ExecutionPolicy. Konfiguruje explicitně -S client -B <izolovaný build> -G Ninja, CMAKE_CXX_COMPILER=cl.exe, zvolený CMAKE_BUILD_TYPE, BUILD_TESTING=ON a PHPVPN_BUILD_UI_TESTS=ON. Resource, service E2E a contract test option explicitně OFF. Sestavuje jen phpvpn-tray-ui-tests. Mode Build po úspěchu vypíše absolutní cestu a skončí, nespouští okno nebo testy.

Preview předá novému explicitnímu vstupu testovacího EXE --dev-preview <View> --scene <Scene>. Původní --interactive, --preview a substring testy zachovat kompatibilní, tento nový vstup je odlišný od dosavadního Wizard preview. Testovací target zůstává mimo běžný produkční EXE a CTest. Parser odmítne chybějící, neznámé či nadbytečné argumenty nenulovým kódem. Mode Test spustí jen zadaný filtr, ověří nenulový počet a propaguje nenulový exit při selhání.

Runner po neúspěšné konfiguraci, buildu nebo kontrole EXE nikdy nespustí starý výstup. Použije přesně tests/phpvpn-tray-ui-tests.exe z vlastního build adresáře a ověří PE AMD64. Log konfigurace/build/test/preview ukládá pouze do work/logs/tray-dev/<čas-a-unikátní-id>/ a vypíše cestu, konfiguraci, target a čas sestavení i spuštění. Argumenty předává bezpečně jako samostatné hodnoty. Log nesmí obsahovat config, hesla nebo klíče. Pokud náhled drží EXE otevřené a build nelze provést, sdělí „Close the existing GUI preview and run the command again.“ Žádné plošné taskkill, zastavování instalovaného tray či služby nebo automatické násilné ukončení procesu.

Při Preview spustí konzolový testovací host přes Start-Process -WindowStyle Hidden s vlastněným Process objektem, vyčká jeho konce a propaguje výsledek. Skutečné prohlížené GUI musí výslovně volat SW_SHOW, skrytý launcher je nesmí skrýt. Provozní údaje a chyby z child procesu patří do uvedeného logu. Před náhledem zobrazí runner v konzoli jednoznačné upozornění, že běží izolovaná syntetická data. Režim preview nemá 45s automatický konec, trvá do uzavření pohledu, v režimu Tray do Exit. Při chybě vytvoření okna vrátí nenulový výsledek a uklidí backend.

Budoucí příkazy z kořene repozitáře:

~~~powershell
# Běžná iterace, plný editor Contracts nad syntetickým /24
.\client\tools\dev-tray.ps1

# Vybrané okno nebo chybový stav
.\client\tools\dev-tray.ps1 -View Wizard -Scene Identity
.\client\tools\dev-tray.ps1 -View Connect -Scene Failed
.\client\tools\dev-tray.ps1 -View Contracts -Scene SaveUnknown
.\client\tools\dev-tray.ps1 -View Tray

# Úzký existující UI test, nikoli celá sada
.\client\tools\dev-tray.ps1 -Mode Test -Filter editor_reset

# Optimalizovaný náhled nebo pouze build
.\client\tools\dev-tray.ps1 -Configuration Release -View Contracts
.\client\tools\dev-tray.ps1 -Mode Build
~~~

Po prohlédnutí okno zavřít, upravit zdroj a zopakovat stejný příkaz. Není třeba admin shell, kopie binárky, registrace služby, změna manifestu ani přebalení. Výchozí Debug slouží ladění, výkon N09b se měří odděleně ve skutečném Release. Čas prvního sestavení není stejný jako inkrementální iterace a musí být v reportu oddělen.

#### Produkční pohledy a izolovaný backend

Rozšířit stávající UI harness, případně vyčlenit jeho backend a náhledový host do malých souborů pod client/tests/ui/. Žádný duplikovaný frontend napodobující screenshot, všechny preview používají stejné produkční třídy, layout, modely, texty, crypto abstrakci, resource ikony a manifest jako phpvpn-tray. CMake seznamy zajistí shodné zdroje a resources, bez kopie PNG v EXE nebo druhé sady UX konstant.

Pro View Tray vyčlenit současnou TrayApplication z main.cpp do sdíleného tray_application.*. Produkční wWinMain zůstane tenký vstup se stávající pevnou pipe a mutexem. Testovací host vytvoří stejnou aplikaci s explicitně předaným soukromým IPC endpointem a vlastním názvem mutexu, odvozenými z nového UUID. Předá je všem čtečkám včetně progress, Wizard, ukládání a nezávislého Disconnectu, žádný reader nesmí spadnout na produkční default. Host neposkytuje uživatelský parametr libovolné pipe ani fallback na skutečnou službu. Testovací ovládání, fixture backend a nový CLI parser se nelinkují do phpvpn-tray, phpvpn-service ani phpvpn-cli.

Každé spuštění má novou soukromou named pipe a nový in-memory validní dokument ze sdílených defaultů se syntetickými hodnotami, případně explicitní deterministickou fixture pro daný scénář. Službové operace fixture mění pouze paměť. Backend může parsovat a validovat skutečná schémata, ale nesmí použít produkční filesystem backend ConfigStore, ProgramData, skutečné HTTP, SCM, Wintun nebo routy. Kritéria reálné služby se nad ním nevydávají za splněná.

Preview shell má vlastní rozlišitelný tooltip „PHPVPN GUI preview - synthetic data“ pouze ve View Tray. Přesné titulky a klientské plochy jednotlivých oken zůstanou produkční kvůli vizuální akceptaci. Vývojové ovladače scénářů nesmějí měnit layout okna ani být zahrnuty do referenční klientské plochy. Pro přepnutí scénáře stačí zavřít a znovu spustit příkaz s jiným -Scene.

| View | Povolené Scene | Požadovaný efekt |
|---|---|---|
| Tray | Default | Skutečné tray menu a koordinace více oken proti fixture. Connect/Disconnect i všechny konfigurační akce pracují pouze s jejím stavem. |
| Connect | Default, Connecting, Connected, Failed | Default = Connecting. Connecting drží pending stav, Connected použije skutečný success a produkční odpočet, Failed bezpečnou kategorii endpoint_unavailable_or_invalid. Zavíraný success se neudržuje umělým vypnutím timeru. |
| Editor | Default, Invalid | Default = skutečný validní dokument, Invalid = syntakticky vadný, ale platný UTF-8 text určený k opravě. |
| Statistics | Default, Unavailable | Devět syntetických čítačů včetně hodnot nad 2^53, Unavailable předá ztrátu statusu po platném snímku, ověří stale zobrazení. |
| Wizard | Default, Gate, Url, Secret, Identity, Contracts | Default = Url. Pozdější kroky dosáhnout pomocí skutečných modelových přechodů a úspěšných fixture odpovědí, žádné produkční přeskočení validace. Contracts zde znamená souhrnný krok Wizardu, nikoli editor. |
| Contracts | Default, Invalid, Saving, SaveUnknown | Default = 252 řádků validního /24. Invalid vznikne editací aktivního Key po validním otevření. Saving vznikne skutečným OK s odpovědí drženou pouze do původního deadline. SaveUnknown simuluje možnou publikaci a ztracené ack, umožní skutečné Check current configuration/Save draft again. |
| Password | Default | Otevře validní Contracts a jeho skutečný nemodální Password SHA. Dovolí další heslová okna i hlavní OK podle N03/04. |

Samostatné View se otevírá stejnou aplikační akcí nebo adaptérem jako z tray menu, zejména Contracts musí použít jediné get-config-document. Nevytvářet skrytý diskový nebo nový IPC shortcut. Ukončení posledního relevantního okna uklidí fixture a proces. Ve View Password zůstává Contracts rodičem, proces končí po jeho zavření. View Tray dovoluje současnou práci Wizard/Contracts/Editor, proces končí produkční cestou Exit. Neplatná kombinace se nesmí tiše změnit na Default.

#### Ověření a předání vývojové cesty

Povinné konkrétní kontroly pro A32:

1. První spuštění na existujícím AMD64 toolchainu vytvoří pouze izolovaný build/log adresář, otevře výchozí Contracts a po zavření skončí. Bez admin oprávnění a bez nainstalované služby. Chybějící toolchain vrátí konkrétní chybu, nic neinstaluje.
2. Stejný příkaz po zavření znovu spustí inkrementální build, bez změny zdrojů Ninja nekompiluje ani nelinkuje. Změna jednoho GUI .cpp přeloží jen dotčené závislosti a relinkuje UI target, nikoli service/CLI/distribuci. Report doloží přesný počet kroků a wall-clock obou iterací, bez nepodloženého slibu univerzálního času kompilace.
3. Chyba kompilace nedovolí spuštění starého preview. Chybný View/Scene, chybějící či nulový test filter, chybné PE a zamčený výstup skončí nenulově s konkrétní příčinou. Spuštění z jiné cwd a cesta s mezerami fungují.
4. Všechna View/Scene se skutečně otevřou podle tabulky. Dlouhý preview přežije 45 s, produkční Connect success se přesto zavře svým odpočtem. Okna jsou viditelná i se skrytým launcherem. Po uzavření nezůstane fixture pipe, reader, timer ani vlastní proces.
5. View Tray vedle běžného tray má vlastní mutex a tooltip. Každý jeho IPC request jde pouze do soukromé pipe, včetně Disconnectu. Všechny publikace mění jen fixture dokument. Nemění se instalace, služba, VPN, ProgramData ani distribuce. Ověřit chybějící fixture bez připojení na reálnou pipe.
6. Spustit neprázdný úzký UI filtr, ověřit jeho počet a výsledek, plus negativní filtr s nulou. UI harness s novým workflow zachová původní explicitní testovací CLI a zůstane mimo běžný CTest.
7. Izolovaná Release konfigurace skutečně používá Release. Produkční release preset zůstává RelWithDebInfo. Produkční targety se sestaví bez fixture kódu a bez vývojových argumentů, viz závěrečná brána.

Při implementaci doplnit návod do client/tests/ui/README.md a odkaz z vhodné klientské vývojové dokumentace. Musí jasně odlišit dnešní příkazy od nových, které už byly skutečně implementované a ověřené. V této dokumentační úloze se tento README nemění. Samostatná zkouška s reálnou službou, instalace a release jsou pozdější výslovné úkoly podle distribution.md. Izolovaný náhled není důkazem skutečných souborových operací, Wintun teardown nebo distribučních podpisů.

## Akceptační a trasovací matice

Každý řádek musí mít při implementaci konkrétní výsledek nebo výslovně uvedené neprovedení s důvodem. Samotný build neprokazuje funkční kritéria. Nyní jde o plánované ověření, nikoli splněné testy.

| ID | Měřitelné kritérium | Specifikace | Cílený důkaz |
|---|---|---|---|
| A01 | Otevření menu má 0 config čtení, Contracts akce 1 get-config-document a 0 přímých file API. | UX04, N02 | Fake IPC počítadla a kontrola call sites. |
| A02 | Každý stav valid/repairable/invalid/missing/unreadable/encoding_error/too_large/directory/IPC má správnou reakci bez default fallbacku. | UX04, N02 | Parametrizovaný model/reader test. |
| A03 | /24 má přesně 252 řádků, .2/.254 a prostřední self vždy vynechá, pár .2/.10 se seřadí numericky. | UX10, N03 | ContractsModel test s hraničními IP. |
| A04 | Globální /24 až /29 bez hardcoded prefixu, /29 přesně 4 peery, /16 až /23 a /30 až /32 odmítnuty před Connectem i publikací. | N03, F00a, cílové UX10/13 | IPv4/helper, config, repair, ConfigStore/set-config a Wizard Finish testy včetně nekanonické sítě a obou rezervací front. |
| A05 | Existující klíč beze změny, chybějící sdílený default, oba směry stejný klíč. | UX10, N03 | Referenční vektor .2_.3: 918c488528c2bc41a90f000b0a63bab0ba418790058ea7b448a3760aec16349d. |
| A06 | 63/65 znaků, uppercase a nonhex aktivní blokují OK, unchecked nikoli, chyba mimo viewport se neztratí. | UX11, N03 | Validační test a keyboard smoke. |
| A07 | Random No/Escape/X nic nemění, Yes použije 32 crypto bajtů, failure zachová hodnotu a žádná akce nezapne řádek. | UX11, N03 | Fake CryptoProvider + dialog smoke. |
| A08 | Prázdný vstup má standardní hash, placeholder se nehashuje. 255 skalárních hodnot projde včetně emoji, 256 se odmítne celé, kombinující sekvence bez normalizace, vadný UTF-16/NUL bez truncation. | N03, cílové UX11 | Nezávislé hash vektory, 254/255/256 ASCII i non-BMP, explicitní délky, neplatný převod/crypto zakáže OK. |
| A09 | Více nemodálních Password SHA i pro stejný cíl, poslední potvrzený vyhraje. Hlavní OK zmrazí kandidáta a zavře děti bez aplikace, jiní editoři fungují. Stará generace nic nezmění. | N03/04, cílové UX03/11/13 | Modelové závody, focus, cleanup, Cancel, chyba vytvoření a hlavní OK s neaplikovaným heslem. |
| A10 | Žádná změna před OK nezapisuje, dirty/clean/unknown X a Escape mají stanovenou sémantiku. | UX12, N04 | Fake writer call count a přechodové testy. |
| A11 | DOM patch zachová runtime/TLS/CIDR/metadata a absent členy, unchecked odstraní, full validace předchází publikaci, 512KiB/4MiB hranice selžou bezpečně. | UX12/13, N04 | ContractsDocument + stub ConfigStore, escaping a nadlimitní vstupy. |
| A12 | Confirmed Finish pravdivě oznámí uložení, zavře Wizard, neotevře Contracts a neprovede Connect. | UX08, N06 | Wizard model/UI test s počítadlem otevřených oken. |
| A13 | Lost Finish reply použije existující receipt a nedělá druhý Finish. Pozdější ruční Contracts provede právě jeden get-config-document nad aktuálním souborem. | UX04/08, N02/N06 | Cílené in-process testy relace a fake IPC počítadla. |
| A14 | Details vzniknou jednorázovým get-config, checkpointy a stav z nezměněného get-status. Souběh uložené a aktivní konfigurace prokáže přijatý rozdíl bez logování configu. | UX04/05, N01/N05 | Reader/model test s řízenou externí změnou konfigurace. |
| A15 | Úspěch pouze skutečné connected, odpočet 3/2/1 od zobrazení, zavření 3000 až 3500 ms, žádný Disconnect. | UX05, N05 | Fake monotonic clock v TrayProgress + UI smoke. |
| A16 | Všech šest safe error kategorií má správné body, popis a nápravu, timeout není odmítnutý klíč, failure se nezavírá. | UX05, N05 | Tabulkové modelové testy a connect1 smoke. |
| A17 | Progress 500 ms a tray 5 s zůstávají nezávislé, close zastaví jen místní reader, maximálně 1 request na reader. | UX05, N05 | TrayPolling s falešným časem. |
| A18 | Poslední save vyhraje i se stale revision, edit/delete nerebaseuje koncept, aktivní snapshot/recovery beze změny, lost ack nemá retry. | UX12, N04 | Souběh dvou konceptů nad stub backendem a unknown workflow test. |
| A19 | Všech 252 řádků má skutečné nativní controls, screen reader enumeruje všechny, Tab/Shift+Tab odkrývá celý pořádek bez záměny kontraktu. | N03/09b, cílové UX03/10 | UI a accessibility smoke /24 a /29, počet HWND i GDI/USER, limit +1200 podle N09b. |
| A20 | Všech 11 mockupů, přesné výchozí klientské plochy, geometrie 6 DIP a textové metriky 8 DIP, 100/125/150/200 %, funkční keyboard/scroll/resize/HC. | N09b, cílové UX14 | Jeden řádek reportu na mockup/DPI, Windows build, klientská plocha, tolerance, odchylka, výsledek. |
| A21 | Wizard nové texty/Back/gate zachovají zprávové varování a metadata, editor oba scrolly/NUL/Reset, stats přesných 9 uint64. | UX06 až UX09, N07/08 | Vybrané Wizard/EditorDocument testy a smoke změněných oken. |
| A22 | V rámci UX změny nevznikne nový IPC příkaz. Existující parsery zachovají schema, config/zprávy/hesla/klíče se nelogují. | UX04/13, N01/05/06 | Seznam commandů před/po, stávající parser testy a read-only audit log call sites. |
| A23 | V client.md nezůstane prázdný placeholder, zákaz odpočtu ani plošný zákaz povolených detailů, wire invariance zachované. | UX00, N01 až N09 | Textový diff a cílené rg podle N seznamu. |
| A24 | Velikost EXE a .text/.rdata/.rsrc před/po, delta B a %, hash i AMD64 a podpisový stav doloženy. | N09b | Srovnatelný optimalizovaný build report, bez PDB v EXE metrice. |
| A25 | Rychlá iterace nepíše do dist, ProgramData ani instalace a nemění službu. | F09, závěr | Inventář/hash dist před/po, fixture-only IPC, žádná registrace, podpis nebo kopie release artefaktu. |
| A26 | Všechny časové, CPU, paměťové a objektové limity N09b splněny, warmup + nejméně tři běhy, rozhoduje nejhorší. | N09b | Optimalizovaný Release benchmark se syntetickým /24, baseline a delta, oddělené IPC čekání. |
| A27 | 500 paintů bez GDI/USER růstu, 20 open/DPI/close cyklů nejvýše +5 objektů/+1 MiB, shodný status bez NIM_MODIFY. | N09a/b | Instrumentovaná cache a shell adapter, počet skutečných výsledných změn. |
| A28 | Rodič → bod tray menu → foreground → kurzor → primární, záporné souřadnice, obnovení bez přesunu, minimální clamp při ztrátě monitoru. | N09a | Geometry testy a UI smoke včetně keyboard menu, unplug, změny rozlišení/DPI a pouhého připojení monitoru. |
| A29 | Pouze anglická UI copy i čísla nezávisle na Windows locale, žádný lokalizační framework/ABI. | N09a, N03 | Textový/UI test v jiném locale, měřený a zalamovaný text, exact Key/Random s konkrétním cílem. |
| A30 | Ruční Exit čeká do původního deadline, dirty/unknown žádá potvrzení, logoff neblokuje, config a VPN se nemění ukončením. | N04a | Fake clock a opožděné ack, více oken, No zruší Exit, WM_ENDSESSION TRUE/FALSE, žádný retry/rollback, vyřazená generace a cleanup. |
| A31 | Core modely bez Windows hlaviček/typů, samostatné objekty phpvpn-core, DOM v config. | F02, N09 | Core testy bez oken, audit CMake a includes, linker mapa service/CLI bez nepoužitých modelů. |
| A32 | Jeden příkaz otevře zvolený produkční pohled nad izolovanou fixture, druhá iterace je inkrementální, chyby neotevřou staré EXE. | F09 | První build, no-op rerun, změna jednoho UI zdroje, cílený test s nenulovým počtem, všechna preview a chybové varianty runneru. |

## Rizika a rozhodnutí pro implementátorku nebo implementátora

- V rámci tohoto UX plánu nevzniká žádný nový lokální IPC příkaz. Connect používá existující `get-config` a `get-status`, ruční Contracts existující `get-config-document`. Wizard Finish nepředává dokument a editor neotevírá. Z této změny nevzniká požadavek na nový service dispatcher.
- Finální potvrzení set-config není receipt. Kontrola shody aktuálního dokumentu je stavová evidence, nikoli důkaz historické publikace. Nepoužívat requestId jako commitId.
- Globální /24 až /29 je nekompatibilní zpřísnění uložených konfigurací. Validátor, IPv4 helper, chybové texty a všechny nové i dosavadní pozitivní fixture mimo tento rozsah musí změnit očekávání nebo přejít na podporovaný rozsah, pokud testují jinou vlastnost. Neprovádět migraci ani skrytě ořezat peery. Historické implementační reporty zůstávají evidencí tehdejšího kódu.
- Konfigurační DOM a zobrazené klíče jsou citlivé. Native EDIT/undo a alokátor mohou držet kopie, best-effort cleanup není absolutní garantované vymazání.
- Zkrácení Wizardu podle mockupů nesmí odstranit upozornění na nevratnou spotřebu zpráv. Vizuální rozdíl je zde záměrný a zdokumentovaný.
- Budoucí instalovatelný balíček vyžaduje výslovné rozhodnutí o cílené aktualizaci manifestu/podpisu, pokud bude tento požadavek vznesen. Tato dokumentační etapa mění jen tento plán, žádná otevřená otázka neblokuje dokončení. F09 nepřepisuje distribuci.

## Povinný zrychlený závěr následné implementace

Tato část se provádí až po implementaci. **Neprovádět úplnou sadu komplexních, integračních, lifecycle, instalačních ani distribučních testů. Nevytvářet ani nepřebalovat distribuci.** Následné zadání rychlé agilní iterace je konkretizováno F09 a nahrazuje původní závěrečnou kopii samotného tray do dist. Rychlý výstup zůstává ve work/build, dist ani jeho manifest se nemění. Neinstalovatelný smíšený balíček není potřebnou součástí GUI iterace.

1. Zaznamenat skutečný HEAD i necommitnuté změny, toolchain, Windows build, architekturu a build konfiguraci. Ověřit F09 nejprve jako inkrementální Debug workflow, cílený UI filtr a všechny preview. Široké sady nespouštět automaticky, úzký filtr musí doložit nenulový počet testů.
2. Pro kontrolu produkčního linkování ve VS AMD64 prostředí z adresáře C:\git\phpvpn\client provést cmake --preset release a cmake --build --preset release --target phpvpn-tray phpvpn-service phpvpn-cli phpvpn-client-tests. Preset je RelWithDebInfo. Služba se sestaví kvůli F00a a CLI kvůli sdílenému core a oddělení modelových objektů, ani jedna se neregistruje ani nespouští. Nevolat all/install/package ani distribuční skript. Produkční EXE nesmějí obsahovat fixture backend nebo podporovat --dev-preview.
3. V phpvpn-client-tests použít první poziční argument jako substring filtru. Harness nemá gtest_filter a při nulovém počtu může vrátit úspěch, proto ověřit nenulový počet i výsledek. Vybrat ContractsModel/ContractsDocument/PasswordKey/ContractsSave, IPv4/config validační testy F00a, dotčené TrayProgress/TrayPolling/TrayLayout/WizardLayout/EditorDocument a cílené případy IPC/souběhu podle skutečných změn. Nespouštět ctest -R phpvpn-client-tests, které by spustilo celou agregovanou sadu. Nepřidávat testy odstraněných IPC nebo automatického handoff.
4. Výkonovou bránu N09b měřit ve skutečné optimalizované konfiguraci Release podle F09, nikoli Debug nebo pouze podle názvu existujícího release presetu. Pro porovnání produkčního EXE sestavit phpvpn-tray také v této oddělené Release konfiguraci. Produkční start/idle CPU měřit nad skutečným TrayApplication ve View Tray se stejným zdrojovým kódem a zaznamenat procesní režii fixture odděleně. Pokud nelze metriky oddělit od fake backendu, označit je za proxy měření a neprohlásit příslušnou produkční bránu za PASS. Vizuální a funkční kontrolu všech jedenácti referencí provést podle N09b, syntetické hodnoty, skutečné klientské plochy, DPI a tolerované odchylky. Chybějící prostředí či DPI kombinace zapsat NOT RUN, nikoli úspěch.
5. Změřit velikost a SHA-256 produkční phpvpn-tray.exe, PE Machine 0x8664 a sekce .text/.rdata/.rsrc. Srovnat se stejně sestaveným baseline stejné architektury, konfigurace a podpisového stavu. Historický distribuční baseline z původního auditu je 650552 B a SHA-256 c736d74dfda1027d4cc166570b7ad92f0d8fe37e8ec5d765e72f4025dda153c5, .text 333824 B, .rdata 236544 B, .rsrc 55808 B. Slouží jen jako historická informace, při odlišné konfiguraci nebo podpisu neprokazuje velikostní regresi. Pro každou položku uvést před/po, rozdíl v B a 100 × (po − před) / před. Developer testovací EXE není velikostí produkčního tray.
6. Read-only inventář a hash dist před/po musí doložit nulovou změnu. Žádná kopie EXE, DLL, PDB, ZIP, manifestu či certifikátu. dist_old_do_not_use se nemění ani neprochází za účelem opravy. Instalovatelný balíček je samostatná operace podle distribution.md a vyžaduje kompatibilní službu i tray a odpovídající podepsaný manifest, nikoli spuštění dev runneru.
7. Provést textový audit client.md, tray_UX.md a této matice proti kódu. Odstranit všechny staré protichůdné výroky vyjmenované v tabulce aktualizací, ale nezměnit historii již provedených testů na nový PASS. Zkontrolovat nulový počet nových IPC příkazů i absenci tajemství v logu. Výstup uvede změněné moduly/dokumenty, konkrétní příkazy rychlé iterace, ověřené i neověřené testy a metriky.
8. Výslovně zaznamenat přeskočené plné integrační, síťové, lifecycle, install/uninstall, multiuser a distribuční/signature testy. Fake backend neověřuje skutečné ukládání, instalaci ani globální VPN. Zbytková rizika zahrnují tyto reálné cesty, dlouhodobý provoz a rozdíly DPI/accessibility systémů. Cílená rychlá brána není tvrzením o úplné release kvalitě.

## Kontrola této dokumentační změny

Tato etapa mění pouze plan3_fix3.md. Rozhodnutí mají po jednom trasovacím záznamu v mapování problémů, normativní znění je v příslušných N blocích a F/A na ně navazují. Body 1 a 2 byly již převážně zapracované, jejich odstraněné IPC varianty se nevracejí. Následná potvrzení vymezují Password SHA v N03, Exit/logoff v N04a a vývojovou iteraci v F09. Žádná rozhodovací otázka nezůstává otevřená.

Dokumentační kontrola zahrnuje textový diff, rozsah změny, odstranění aktivních požadavků na 4092 řádků/virtualizaci/modalitu a původní distribuční kopii, kontrolu hodnot runtime tabulky proti zdroji a návaznost všech dvanácti problémů na implementační a testovací kroky. Nejde o provedený build, runtime benchmark, test GUI ani měření nové binárky. Implementační výsledky se doplní až při provedení tohoto plánu.
