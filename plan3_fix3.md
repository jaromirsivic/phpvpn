# Plan3 fix3: modernizace tray UX a editor Communication contracts

## Stav, rozsah a výchozí podklady

Tento dokument je proveditelný plán následné implementace, nikoli zpráva o hotovém GUI. Zahrnuje následná schválená dokumentační rozhodnutí zaznamenaná také v [tray_UX.md](tray_UX.md), [client.md](client.md) a [plan3_fix3_problems.md](plan3_fix3_problems.md). Zdrojový kód, testy, mockupy, build skripty a distribuce zůstávají v této dokumentační fázi beze změny. Nebyl proveden build ani kopie EXE. Při implementaci se N01 až N09 zapracují spolu s příslušným kódem a bez ponechání starých protichůdných pravidel.

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
| Runtime přijímá CIDR `/16` až `/29`, Wizard defaults jsou menší kandidátní sada. | Editor a Wizard nesměšovat, editor omezit před alokací podle N03. |
| Preset `release` znamená RelWithDebInfo, architekturu zajišťuje prostředí x64. | Neodvozovat architekturu z názvu presetu, ověřit PE před kopií. |

## Přesné budoucí normativní změny client.md

Níže uvedené bloky jsou závazný obsah aktualizace. Umístění určují současné nadpisy a citované věty, orientační čísla řádků se mohou posunout. Zachovat ostatní pravidla dané sekce, zejména síťové ověřování, lifecycle a konfiguraci. Nevkládat jen nový odstavec vedle starého protichůdného pravidla. Při implementaci odstranit všechny uvedené konfliktní formulace i odpovídající staré testové očekávání.

### N01: Autorita UX, menu a bezpečnostní výjimky

Do části Windows tray vložit odkaz na normativní `tray_UX.md` jako specifikaci vzhledu, anglických textů, focusu, DPI a resize. Zachovat pořadí a funkce ostatních položek menu. Položku 4 kolem řádku 804 `Communication Contracts, zavíratelné prázdné okno bez editoru kontraktů` nahradit:

> **Communication contracts** otevře modální editor platných komunikačních kontraktů podle samostatné specifikace níže. Jednorázové načtení a kontrolu provede služba. Otevření menu samo nečte konfiguraci. Okno není automatickým Connectem ani reloadem aktivní VPN.

Bezpečnostní odstavec kolem řádku 815 a zákaz kolem řádku 823 nahradit v dotčeném rozsahu:

> `get-status`, Statistics a logy neobsahují konfigurační URL, klíče ani celé dokumenty. Po otevření Connect okna tray jednou zavolá existující `get-config`, z platného dokumentu převezme pouze Server URL a lokální Virtual IP do karty Connection details a celý přijatý config nikdy neloguje. Je vědomě přijato, že aktuálně uložený dokument může být po externí změně novější než neměnný aktivní snímek připojení, takže zobrazené URL/IP nemusí patřit právě aktivnímu spojení. Connect nikdy nezobrazuje SERVER_SECRET, ACCESS_KEY, payload klíče, tagy, ciphertext, obsah paketů, interní cesty ani raw systémové či síťové chyby. Povolení URL není povolením logování URL ani rozšíření `get-status`. Edit Config smí v editoru zobrazit skutečný dokument včetně URL a klíčů podle své stávající funkce. Communication contracts smí zobrazit a upravit payload klíče jen ve svých určených polích, Password SHA heslo jen v příslušném dialogu. Wizard smí zobrazit zadávané hodnoty. Žádná z těchto výjimek nepovoluje tajemství v diagnostice nebo chybových zprávách.

Zachovat vlastní smlouvu Show Info a omezené existující výjimky URL/pevné konfigurační cesty ve strukturovaných konfiguračních chybách. Progress chyba tuto výjimku pro cestu nemá. URL projde existující validací bez userinfo, query a fragmentu. Zobrazí se plná hodnota, při nedostatku místa přístupný horizontální scroll nebo výběr v read-only poli, nikoli text chyby. Přijatý dokument z `get-config` má omezenou životnost a po získání dvou hodnot se uvolní.

### N02: Otevření editoru, dostupnost a vlastnictví dokumentu

V části Jednorázová kontrola po akci a reakce menu, kolem řádku 321, nahradit větu o společném `get-config-state` pro Statistics a Contracts:

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

Celý odstavec začínající `Placeholder má titul` kolem řádku 626 nahradit plnou sekcí s následujícím obsahem a odkazem na UX10 až UX12:

> Resizovatelný modální formulář má nadpis Communication contracts, krátký popis, Your virtual IP, tabulku Contract / Enabled / Key / Actions a pevnou patičku s validací a OK. Vlastní modalita neblokuje message loop, globální status ani samostatný Disconnect. X a Escape zavírají podle dirty-state, vnořený Password SHA-256 blokuje pouze rodiče a vrací focus na původní řádek.
>
> Pro validní lokální adresu ve výchozím 10.99.99.0/24 vytvořit právě 252 řádků: .2 až .254 bez vlastní IP. Pro jiné runtime podporované CIDR použít sdílené funkce prvního a posledního klienta a stejné rezervované adresy, nikoli textový prefix 10.99.99. Řádky se řadí numericky podle remoteIp. Identita položky JSON je remoteIp. Zobrazená identita Contract je kanonické minIP_maxIP, obě IPv4 se porovnávají jako čísla. Self kontrakt se nevytváří.
>
> Před alokací vypočítat počet klientských adres minus jedna, s kontrolou přetečení. Editor podporuje nejvýše 4092 řádků, což zahrnuje všechny validní /20 až /29. Větší rozsah odmítne s vysvětlením a cestou do Edit Config. Nezobrazí neúplný výběr a neomezí potichu runtime CIDR. /19 má již 8188 protějšků, samotné 64bajtové klíče by spotřebovaly 524032 bajtů bez JSON struktury, takže úplný zapnutý rozsah překročí 512 KiB. Omezení je rozpočtem tohoto editoru, ne změnou síťového protokolu. I u menšího rozsahu se kontroluje skutečná velikost konkrétního dokumentu.
>
> Enabled je při načtení true právě pro přítomný validní kontrakt. Jeho přesný payloadKey se zachová. Chybějící kontrakt dostane pouze do konceptu lowercase_hex(SHA-256(ASCII(minIP + "_" + maxIP))) voláním sdíleného defaultPayloadKey. Pouhé předvyplnění ho nezapne. Při OK se enabled řádky vloží pod remoteIp jako objekt s payloadKey, unchecked odstraní. Nezaměňovat tuto sadu s aditivními Wizard defaults 13/14 kontraktů.
>
> Key je editovatelné pole. Validní hodnota je přesně [0-9a-f]{64}, bez trimování a automatického převodu uppercase. Aktivní nevalidní klíč označí konkrétní pole červeně, zobrazí text `Keys must contain exactly 64 lowercase hexadecimal characters.` a blokuje OK. Neaktivní nevalidní klíč se označí, ale OK neblokuje a nepublikuje se. Zaškrtnutí takového řádku ihned zablokuje OK, oprava nebo odškrtnutí blokaci odstraní, nejsou-li jiné chyby. Validace zahrnuje také řádky mimo viewport.
>
> Random zobrazí `Replace the key for this contract with a new random key?` s Yes/No, výchozí No. No, Escape a X nic nezmění. Yes získá 32 bajtů přes existující CryptoProvider::randomBytes a zapíše 64 lowercase hex znaků pouze do konceptu daného řádku. Crypto chyba zachová původní klíč. Enabled se nemění.
>
> Password SHA otevře druhý modální dialog s titulem Password SHA-256, nadpisem Generate key from password, popisem účelu, polem Password a read-only SHA-256 key. Placeholder CHANGE-THIS-PASSWORD je jen cue text. Při psaní se počítá prostý SHA-256 přesných UTF-8 bajtů, bez normalizace, trimování, saltu, iterací nebo heslového KDF. Jde o kompatibilní deterministický komunikační klíč. Prázdné skutečné pole má hash e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855, nikoli hash placeholderu. OK přenese aktuální hash do cílového Key a nastaví dirty bez změny Enabled. X/Escape nezmění řádek. Heslo se neukládá, neloguje ani neposílá službě.

Připojit limity: uložený JSON nejvýše 524288 bajtů, zakódovaná IPC zpráva včetně obálky nejvýše 4194304 bajtů. Součet textů rozpracovaných Key nejvýše 524288 bajtů. Změna přes limit se odmítne celá s chybou a zachová předchozí hodnotu. Pole musí umožnit vložit nevalidní délku, například 65 znaků, aby nevznikl falešně validní výsledek tichým truncováním. Heslo nejvýše 524288 UTF-8 bajtů, neplatný UTF-16 převod a nepodporované řídicí znaky jednorádkového vstupu odmítnout bez ztrátové úpravy. Ostatní stávající JSON a IPC limity zůstávají.

### N04: Publikace, souběh a nejasný výsledek

Za sekci editoru vložit následující závazná pravidla:

> Koncept vlastní původní validní DOM a řádkové změny. Kandidát vznikne patchem pouze communicationContracts, nikoli serializací ClientConfig. Zachovat nesouvisející hodnoty, typy a přítomnost členů, včetně vynechaného runtime a Wizard metadat. Původně chybějící communicationContracts může zůstat chybějící, jsou-li všechny řádky vypnuté. Původně přítomný objekt může zůstat prázdný. Přísné schéma nadále odmítá neznámá pole. Změna whitespace při DOM editaci je dovolena.
>
> OK provede lokální validaci a jeden set-config. Služba zopakuje úplnou konfigurační validaci před temp souborem a publikuje přes stejný serializovaný atomický ConfigStore jako Save a Finish. Otevření, psaní, přepínače ani generátory nezapisují na disk. Potvrzený úspěch zavře okno. Potvrzené odmítnutí zachová koncept a umožní opravu.
>
> Poslední úspěšná publikace celého dokumentu vyhraje. revisionToken není CAS a jeho zastarání není důvodem odmítnutí nebo dalšího potvrzení běžného OK. Externí editace, smazání a znovuvytvoření se automaticky nepřimíchají do konceptu. Bezpečná explicitní publikace může přepsat novější dokument. Nepřepíná aktivní konfiguraci spojení, nevyvolá Connect ani Disconnect, a recovery zachová původní aktivní snímek. Změny se použijí při novém logickém Connectu. Při aktivní VPN zobrazit `Changes take effect on the next connection.`.
>
> Model rozlišuje loading, editing, invalid, saving, saveUnknown a closed. V saving nepovolit další zápis, editaci ani zavření jen po konečný IPC rozpočet. Chybějící, poškozené nebo nekorelované potvrzení po možném odeslání a nejednoznačná chyba publikace vedou do saveUnknown. Nikdy neprohlásit automaticky, že soubor zůstal nezměněný. Pozdní odpovědi mají korelaci instance okna a operace, nemění novější koncept.
>
> set-config nemá dohledatelný commit receipt. V saveUnknown se zápis automaticky neopakuje. Nabídnout Check current configuration, jednu explicitní akci get-config-document. Shoda celého validního DOM s odeslaným kandidátem dovolí oznámit `The current configuration matches your changes.` a zavřít. Není to historický receipt. Při neshodě nebo missing zachovat koncept a nabídnout zavření nebo Save draft again s výslovným potvrzením nahrazení současného celého dokumentu. Je to nový uživatelem schválený zápis. Nečitelnost, encoding_error, too_large a bezpečnostní chyba vyžadují nejprve nápravu. Žádný slepý retry, background kontrola ani automatický merge.
>
> Clean X/Escape zavře přímo. Dirty vyžaduje Discard unsaved changes? s výchozím No. Generační potvrzená akce označí koncept změněný i při náhodné shodě výsledku. V saveUnknown zavření výslovně upozorní, že soubor už mohl být změněn. Zavření není rollback. Vlastněné tajné buffery se best-effort mažou přes existující secureWipe/SecureBuffer, pole i jejich undo se vyprázdní a doba života DOM/IPC kopií se minimalizuje. Nelze slibovat vymazání kopií spravovaných OS. Žádné payload klíče, hesla ani celé dokumenty do logů nebo chyb.

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

## Pořadí následné implementace

### F00: Výchozí stav a kontrakty změny

1. Znovu přečíst aktuální instrukce a ověřit git status. Zachovat cizí změny, zaznamenat HEAD, dostupné build prostředí, stávající velikosti a hash distribuce. Nepracovat v dist_old_do_not_use.
2. Porovnat nové změny kódu proti výchozímu auditu výše. Vyhledat všechna tvrzení `placeholder`, `prázdné`, `Nesmí zobrazovat číselný odpočet`, `nemá Back`, zákazy URL a popisky Next v client.md a testech. Připravit seznam náhrad N01 až N09, zatím netvrdit implementované chování.
3. Potvrdit existující limity z ConfigStore, IPC a IPv4 helperů. Ověřit, že implementace používá pouze existující IPC příkazy a nemění jejich uzavřená schémata.
4. Zaznamenat, že N05/N06 nevyžadují nový IPC příkaz ani novou službovou dispatch cestu. Automaticky nenahrazovat distribuční service EXE. Případné jiné změny služby mimo tento UX plán nesměšovat s touto implementací.

Výstup: reprodukovatelný baseline a seznam dotčených míst. Závislosti dalších fází vycházejí z tohoto auditu.

### F01: Lehký vizuální základ a layouty

Dotčené soubory: `client/apps/tray/window_utils.h`, `layout.*`, `wizard_layout.*`, `editor_window.*`, `progress_window.*`, `statistics_window.*`, `wizard_window.*`, `contracts_window.*`. Nové malé soubory například `ui_style.h`, `ui_resources.*`, `ui_controls.*`, bez třetí strany.

Oddělit constexpr DIP/geometrii a sadu textů od Windows resource cache. Fonts/pens/brushes držet RAII na okno/DPI, nevytvářet při každém WM_PAINT. Měřit text, definovat paint/invalidation hranice, použít WS_CLIPCHILDREN a současnou bezpečnou DeferWindowPos větev. Standardní EDIT/BUTTON zachovají výběr, clipboard ovládaný uživatelem a keyboard. Owner-draw nesmí odstranit přístupné jméno, roli, focus či checked stav. WM_DPICHANGED použije doporučený rect a přepočítá metriky.

Implementovat pevnou patičku, scroll obsahu, breakpointy a minimální dostupnou plochu podle UX03. Ve Wizardu neregenerovat všechny HWND při každé aktualizaci statusu, aby nezanikaly focus, výběr a undo. Aktualizovat N01 a N09 současně s použitelnou sdílenou vrstvou. Výstup ověřit cílenými geometry testy, ne pixelovým porovnáním samotných čísel bez reálného okna.

### F02: Přenosný model kontraktů a crypto

Nové navržené moduly `client/src/core/config/contracts_document.*` a `client/apps/tray/contracts_model.*`, případně `password_key_model.*`. První pracuje s DOM a shared config validátorem, druhý s prezentačními stavy a bez Win32. Zapojit explicitní zdrojové seznamy v `client/src/CMakeLists.txt`, `client/apps/CMakeLists.txt` a cílené testy v `client/tests/CMakeLists.txt`.

Datový model: původní DOM/text s omezeným životem, configPath a informational revision, numerické localIp/CIDR, vektor `ContractRow {remoteIp, enabled, keyText, keyValidity}`, přehled počtu aktivních chyb a dirty flags, identita focusu `remoteIp + column`, save generation a immutable odeslaný kandidát. Neuchovávat duplicitní kopii celého DOM v každém řádku. Identifikátor páru dopočítat nebo jednou uložit jako krátký řetězec. Řádky se netřídí podle zobrazeného textu.

Před vytvořením vektoru odmítnout nadlimitní rozsah. Použít IPv4 helpery a `core/wizard/wizard_defaults.*::defaultPayloadKey`, ne duplikovaný hash algoritmus. Crypto sdílet přes `CryptoProvider`, CNG Windows implementace již existuje. Password model převádí validní vstup na přesné UTF-8 a synchronně spočítá nejvýše omezených 512 KiB přes existující sha256, po návratu maže pracovní buffery. Je-li naměřená odezva vstupu nevyhovující, lze přejít na jednu omezenou background úlohu s generací a potvrzováním pouze aktuálního výsledku, nikdy ne neomezenou frontu na každý znak.

Sestavení kandidáta patchuje původní DOM a validuje celek, zachová absent/present rozdíly. Ukládání zatím není součást modelu. Přidat N03 a datovou část N04 do client.md spolu s implementací. Akceptace A03 až A09 a A11.

### F03: Connect nad existujícími `get-config` a `get-status`

Dotčené moduly jsou pouze tray čtečky/modely a prezentace Connect. Po otevření okna jednou použít existující `get-config`, z platného JSON převzít `serverUrl` a lokální `virtualIp`, celý dokument nelogovat a po extrakci omezit životnost jeho kopií. Stávající progress reader dál volá existující `get-status` každých 500 ms a z něj přebírá stav i tři checkpointy.

Nepřidávat parser, builder, dispatcher ani službový snapshot. Současné `get-status` testovat beze změny schématu a bez URL. Reader musí oddělit chybu jednorázového config čtení od výsledku bootstrapu. Testovat přijatý nesoulad uložených detailů vůči aktivnímu snapshotu a zákaz logování celého configu. Aktualizovat N05, N01 bezpečnost a odpovídající N08 Connect testy současně. Akceptace A14 až A17 a A22.

### F04: Finish bez automatického otevření Contracts

V `client/apps/tray/wizard_window.*` a `main.cpp` zachovat potvrzení úspěšné publikace a existující uzavření relace. Po confirmed zobrazit pravdivý úspěch, zavřít Wizard a neotevírat Contracts. Nevytvářet ani neuchovávat publikovaný dokument pro UI.

Neměnit callback na move-owned dokument a nepřidávat nový IPC, error kódy, handoff parser, dispatch ani lease chování. Pozdější ruční Contracts z menu je nezávislá akce podle F06. Přidat N06 a změny úvodu/přechodové tabulky `client.md` v této fázi. Akceptace A12/A13/A22.

### F05: ContractsWindow, viewport a vnořený dialog

Přepsat `contracts_window.*`, přidat `password_sha_window.*`. `show/open` dostane owner a validovaný dokument výhradně z ruční menu cesty, jedno otevřené lokální okno znovu aktivovat, nikoli tiše resetovat jeho dirty model. Header a footer zůstávají pevné, tabulka scrolluje. Vektor má všechny řádky, HWND jen viditelný viewport a nejvýše jeden rezervní řádek nahoře a dole. Recyklované ovladače vždy mapovat na remoteIp, před recyklací zapsat aktuální editaci do modelu. Tab/Shift+Tab zajistí scroll na další logický ovladač, zachovat caret a focus při resize/DPI. Scroll nesmí zaměnit adresáta Random nebo Password SHA.

Při 252 řádcích počet HWND roste s viewportem, nikoli s celou sadou. Při 4092 řádcích ověřit omezenou paměť, bez blokujícího kreslení všech řádků. Využít nativní checkbox/switch a skutečné editovatelné pole pro dostupnost. Vyplňování defaultů neprovádí filesystem ani HTTP. Random potvrzení váže cílovou identitu ještě před otevřením. Vnořený dialog zakáže rodiče s RAII obnovou i při chybě CreateWindow, bezpečně vyprázdní pole/undo a vrátí focus.

Propojit s F02 a F01, implementovat UX10/11 a N03. Akceptace A03 až A10, A19/A20. Na konci této fáze lze ověřit lokální koncept bez skutečného zápisu.

### F06: Načtení, Save controller a chyby

V `main.cpp` oddělit Contracts od `requiresConfigState`, použít jedinou WizardReader/get-config-document cestu a N02 matici. Není dovoleno nejdřív ověřit config a poté načítat nový dokument. Stávající `startCommand(set_config)` a `finishCommand` jsou svázané s editor generation/setSaving, nepřipojovat k nim Contracts přes sdílené nahodilé flagy. Vytvořit účelový řadič `ContractsSaveController` s jedním požadavkem, generací okna a striktním parserem výsledku. Lze extrahovat sdílenou transportní část, ne sloučit lifecycle editorů.

Použít existující `set-config` request schema s právě čtyřmi členy: `ipcVersion:1`, čerstvé UUIDv4 `requestId`, `command:"set-config"` a `config:string` s celým kandidátem, nikoli vnořeným JSON objektem. Úspěch je právě acknowledgement `{ipcVersion:1, requestId:<shoda>, result:"ok"}`. Odmítnout extra pole/duplicity/typové odchylky, Error parsovat podle existující bezpečné obálky. Nenavrhovat pro tento Save Wizard get-commit, který nepatří této operaci. Zachovat úplnou službovou validaci a serializační ConfigStore save, žádná nová zápisová filesystem cesta.

Existující get-config-document má pouze tři základní request členy. Jeho result obsahuje právě `configPath:string`, `availability:enum`, `reasonCode:string|null`, `revisionToken:string|null`, `document:string|null` podle současného striktního parseru. Missing má null dokument, valid/repairable/invalid skutečný text, nečitelnost/kódování/velikost se vrací jako existující strukturovaná chyba bez dokumentu. Tyto odpovědi se nepřevádějí na valid podle pouhé přítomnosti textu. Pro ruční Contracts načtení i Check current configuration zachovat 5000ms celkový deadline WizardReader. Pro set-config zachovat 30000ms celkový deadline CommandReader, včetně čekání, přenosu a čtení odpovědi. Pro progress použít konečný rozpočet současné `get-status` čtečky bez překryvu požadavků. Žádný dílčí read nebo polling tyto absolutní termíny neobnovuje.

Implementovat N04 unknown workflow, DOM rovnost porovnává význam objektů nezávisle na pořadí členů, ale zachová typy, pole a absent/present rozdíly. Nové potvrzené Save draft again je samostatná generace. Stav saving má stejný konečný transportní rozpočet jako současný editor, po deadline je řešitelný unknown, nikoli věčně zakázané X. Při zaručeném neodeslání vrátit editing, nejistotu nezaměnit za potvrzené odmítnutí.

Aktualizovat N02/N04 v client.md, dokončit náhradu placeholderu a menu. Akceptace A01/A02/A10/A11/A18/A22.

### F07: Dokončení vzhledu Connect, Edit Config, Statistics a Wizard

Propojit progress model s F03. Uložit deadline skutečně zobrazeného success, počítat label 3/2/1, ponechat failed do ručního zavření. Přesná mapa safe error → popis/panel je jedna sdílená tabulka, ukázkový odmítnutý Access key není univerzální text. Renderovat dle connect1/2 bez těžkého bitmapového pozadí.

V `editor_window.*` upravit title/path header a footer layout. Zachovat existující EditorDocument model, Reset generation a raw NUL reprezentaci. U Show Info zvolit samostatný prezentační režim. Aktualizovat layout testy, které nyní čekají Save/Reset/Cancel vedle sebe vpravo.

V `statistics_window.*` vytvořit tři karty z jedné předané hodnoty TrayStatus. Doplnit prezentační stav dostupnosti/čerstvosti, protože pouhé ignorování nevalidního snapshotu nestačí pro stale label. Counter model zůstává ve službě.

Ve `wizard_window.*`, layoutu a modelových textech zavést UX08/09 a N07. Nepřesouvat ověřování do UI. Odstranit duplicitní potvrzení Disconnect po explicitním gate tlačítku, nepřidat automatický disconnect při pouhém otevření. Nová srozumitelná copy zachová nevratné spotřebování zpráv, kolize, lease a unknown Finish. Aktualizovat N07/N08 a zkontrolovat všechny viditelné odkazy na Next. Akceptace A14 až A21.

### F08: Cílené ověření, dokumentace a měření

Přidat malé testy do stávajícího `phpvpn-client-tests`, například samostatné sady ContractsModel, ContractsDocument, PasswordKey a ContractsSave. Rozšířit relevantní současné `test_tray_progress.cpp`, `test_tray_layout.cpp`, `test_wizard_layout.cpp`, `test_wizard_document.cpp`, `test_wizard_ipc.cpp` a editorové testy. Nevytvářet sady pro odstraněný progress IPC nebo Wizard handoff. Nepsat testy pouze kopírující konstanty, testovat invarianty, hranice, příčiny chyb a chybné pořadí událostí.

`client/tests/ui/tray_ui_tests.cpp` používá fake pipe, nikoli skutečné ProgramData. Doplnit krátké smoke pro nové obrazovky a případné linkage progress/statistics v UI cíli. Zachovat čistě syntetická data. Vizualizaci všech jedenácti referencí ověřit při stejné velikosti klientské plochy a 100/125/150/200 % DPI, s malou pracovní plochou, klávesnicí a high contrast. Evidence OS, commit, build, DPI, scénář a výsledek může být textová, žádné nové binární screenshoty do repozitáře.

Provést audit N01 až N09 proti konečnému client.md, UX a skutečnému kódu. protocol.md se nemění: nejde o nový wire formát, AAD/HKDF/IV, serverový endpoint ani alokaci IP. Změny jsou v UI a lokálním IPC. Pokud implementace odhalí skutečně nutnou wire změnu, nejprve ji zdokumentovat jako nový problém, neprovést ji v této opravě automaticky. Dokončit měření a omezenou kopii podle poslední části.

## Akceptační a trasovací matice

Každý řádek musí mít při implementaci konkrétní výsledek nebo výslovně uvedené neprovedení s důvodem. Samotný build neprokazuje funkční kritéria. Nyní jde o plánované ověření, nikoli splněné testy.

| ID | Měřitelné kritérium | Specifikace | Cílený důkaz |
|---|---|---|---|
| A01 | Otevření menu má 0 config čtení, Contracts akce 1 get-config-document a 0 přímých file API. | UX04, N02 | Fake IPC počítadla a kontrola call sites. |
| A02 | Každý stav valid/repairable/invalid/missing/unreadable/encoding_error/too_large/directory/IPC má správnou reakci bez default fallbacku. | UX04, N02 | Parametrizovaný model/reader test. |
| A03 | /24 má přesně 252 řádků, .2/.254 a prostřední self vždy vynechá, pár .2/.10 se seřadí numericky. | UX10, N03 | ContractsModel test s hraničními IP. |
| A04 | Jiný CIDR nemá hardcoded prefix, /20 má 4092 řádků, /19 a /16 se odmítnou před alokací, /29 respektuje runtime rezervace. | UX10/13, N03 | Rozsahové testy a kontrola bounded allocation. |
| A05 | Existující klíč beze změny, chybějící sdílený default, oba směry stejný klíč. | UX10, N03 | Referenční vektor .2_.3: 918c488528c2bc41a90f000b0a63bab0ba418790058ea7b448a3760aec16349d. |
| A06 | 63/65 znaků, uppercase a nonhex aktivní blokují OK, unchecked nikoli, chyba mimo viewport se neztratí. | UX11, N03 | Validační test a keyboard smoke. |
| A07 | Random No/Escape/X nic nemění, Yes použije 32 crypto bajtů, failure zachová hodnotu a žádná akce nezapne řádek. | UX11, N03 | Fake CryptoProvider + dialog smoke. |
| A08 | Prázdné heslo má standardní prázdný hash, placeholder se nehashuje, Unicode/mezery se nenormalizují. | UX11, N03 | Hash testy včetně neASCII UTF-8 a rozdílných Unicode sekvencí. |
| A09 | Password OK změní jen cílový Key a dirty, Cancel nic, rodič je modal, cleanup proběhne i při chybě. | UX03/11/13, N03 | Model test, instrumentovaný cleanup a focus smoke. |
| A10 | Žádná změna před OK nezapisuje, dirty/clean/unknown X a Escape mají stanovenou sémantiku. | UX12, N04 | Fake writer call count a přechodové testy. |
| A11 | DOM patch zachová runtime/TLS/CIDR/metadata a absent členy, unchecked odstraní, full validace předchází publikaci, 512KiB/4MiB hranice selžou bezpečně. | UX12/13, N04 | ContractsDocument + stub ConfigStore, escaping a nadlimitní vstupy. |
| A12 | Confirmed Finish pravdivě oznámí uložení, zavře Wizard, neotevře Contracts a neprovede Connect. | UX08, N06 | Wizard model/UI test s počítadlem otevřených oken. |
| A13 | Lost Finish reply použije existující receipt a nedělá druhý Finish. Pozdější ruční Contracts provede právě jeden get-config-document nad aktuálním souborem. | UX04/08, N02/N06 | Cílené in-process testy relace a fake IPC počítadla. |
| A14 | Details vzniknou jednorázovým get-config, checkpointy a stav z nezměněného get-status. Souběh uložené a aktivní konfigurace prokáže přijatý rozdíl bez logování configu. | UX04/05, N01/N05 | Reader/model test s řízenou externí změnou konfigurace. |
| A15 | Úspěch pouze skutečné connected, odpočet 3/2/1 od zobrazení, zavření 3000 až 3500 ms, žádný Disconnect. | UX05, N05 | Fake monotonic clock v TrayProgress + UI smoke. |
| A16 | Všech šest safe error kategorií má správné body, popis a nápravu, timeout není odmítnutý klíč, failure se nezavírá. | UX05, N05 | Tabulkové modelové testy a connect1 smoke. |
| A17 | Progress 500 ms a tray 5 s zůstávají nezávislé, close zastaví jen místní reader, maximálně 1 request na reader. | UX05, N05 | TrayPolling s falešným časem. |
| A18 | Poslední save vyhraje i se stale revision, edit/delete nerebaseuje koncept, aktivní snapshot/recovery beze změny, lost ack nemá retry. | UX12, N04 | Souběh dvou konceptů nad stub backendem a unknown workflow test. |
| A19 | Řádky mají bounded HWND podle viewportu, scroll/focus/recycle nikdy nezmění jiný kontrakt. | UX03/10, N09 | UI smoke 252 a 4092 řádků, počet HWND a paměť v reportu. |
| A20 | Všech 11 referencí odpovídá kompozicí, patička dostupná, 100/125/150/200 %, keyboard, malá plocha a HC čitelné. | UX00 až UX03/14, N09 | Krátká textová vizuální matice, včetně povolených odchylek. |
| A21 | Wizard nové texty/Back/gate zachovají zprávové varování a metadata, editor oba scrolly/NUL/Reset, stats přesných 9 uint64. | UX06 až UX09, N07/08 | Vybrané Wizard/EditorDocument testy a smoke změněných oken. |
| A22 | V rámci UX změny nevznikne nový IPC příkaz. Existující parsery zachovají schema, config/zprávy/hesla/klíče se nelogují. | UX04/13, N01/05/06 | Seznam commandů před/po, stávající parser testy a read-only audit log call sites. |
| A23 | V client.md nezůstane prázdný placeholder, zákaz odpočtu ani plošný zákaz povolených detailů, wire invariance zachované. | UX00, N01 až N09 | Textový diff a cílené rg podle N seznamu. |
| A24 | Velikost EXE před/po, rozdíl B a %, hash i PE architektura doloženy, růst vysvětlen. | UX14, N09 | Build report, hash a PE kontrola. |
| A25 | Kopie změnila pouze cílovou tray binárku, ostatní distribuce zachovaná, manifestový rozpor výslovně uveden. | UX14, závěr plánu | Porovnání inventáře a hashů před/po, bez distribučního test suite. |

## Rizika a rozhodnutí pro implementátorku nebo implementátora

- V rámci tohoto UX plánu nevzniká žádný nový lokální IPC příkaz. Connect používá existující `get-config` a `get-status`, ruční Contracts existující `get-config-document`. Wizard Finish nepředává dokument a editor neotevírá. Z této změny nevzniká požadavek na nový service dispatcher.
- Finální potvrzení set-config není receipt. Kontrola shody aktuálního dokumentu je stavová evidence, nikoli důkaz historické publikace. Nepoužívat requestId jako commitId.
- Vyšší CIDR jsou runtime validní, editor má výslovný limit 4092 a odkaz na Edit Config. Nepřidat tiché stránkování s mazáním skrytých kontraktů nebo zkrácený seznam.
- Konfigurační DOM a zobrazené klíče jsou citlivé. Native EDIT/undo a alokátor mohou držet kopie, best-effort cleanup není absolutní garantované vymazání.
- Zkrácení Wizardu podle mockupů nesmí odstranit upozornění na nevratnou spotřebu zpráv. Vizuální rozdíl je zde záměrný a zdokumentovaný.
- Budoucí instalovatelný balíček vyžaduje výslovné rozhodnutí o cílené aktualizaci manifestu/podpisu, pokud bude tento požadavek vznesen. Pro vytvoření těchto dvou dokumentů žádná otevřená otázka neblokuje dokončení.

## Povinný zrychlený závěr následné implementace

Tato část se provádí až po implementaci. **Neprovádět úplnou sadu komplexních, integračních, lifecycle, instalačních ani distribučních testů. Nevytvářet ani nepřebalovat celou distribuci.** Níže popsaná výjimka slouží rychlé iteraci GUI a není důkazem plné release kvality.

1. Zapsat skutečný commit a konfiguraci build prostředí. Ve VS prostředí pro Windows x64, s pracovním adresářem `C:\git\phpvpn\client`, použít `cmake --preset release` a `cmake --build --preset release --target phpvpn-tray`. Preset používá RelWithDebInfo a výstup pod `work/build/release`, přesnou cestu EXE zjistit z cíle a existujícího výstupu, ne hádáním ani hledáním první stejně pojmenované binárky. Ověřit x64 toolchain a úspěšný aktuální build. Nevolat all/install/package ani distribuční skript.
2. Pro relevantní unit sady sestavit jen `phpvpn-client-tests` v téže konfiguraci a spustit jeho skutečný EXE s prvním pozičním argumentem jako substring filtru. Současný harness nemá gtest_filter a při nulovém počtu testů může vrátit úspěch. Každý běh proto doloží nenulový počet a výsledek. Použít nové úzké sady ContractsModel/ContractsDocument/PasswordKey/ContractsSave a dotčené TrayProgress, TrayPolling, TrayLayout, WizardLayout, EditorDocument či konkrétní jednotlivé názvy. Nepřidávat testovací sady pro odstraněné IPC nebo automatický Wizard handoff. Nespouštět `ctest -R phpvpn-client-tests`, protože by provedl celý agregovaný test. Výběr upravit podle skutečně změněných částí, ne spouštět celé staré integrační nebo lifecycle balíky.
3. Provést pouze krátké manuální nebo filtrované `phpvpn-tray-ui-tests` smoke změněných obrazovek se syntetickými hodnotami a fake pipe. Stávající `client/tools/run-tray-ui-tests.ps1` má vlastní UI konfiguraci a Filter, respektovat jeho skutečné parametry. Pokud je potřeba UI target, zapnout PHPVPN_BUILD_UI_TESTS pouze pro toto cílené ověření. Neinstalovat kvůli smoke službu, neměnit skutečné ProgramData ani VPN. Ověřit pouze nové použití existujících IPC cest. Zaznamenat vizuální odchylky a neověřené kombinace, neskrýt chybějící GUI ověření za unit testy.
4. Do reportu uvést vědomě přeskočené plné integrační, síťové, lifecycle, install/uninstall, víceuživatelské a distribuční/signature testy i celé přebalení. Zbytková rizika zahrnují nové pořadí použití existujících IPC příkazů, dlouhodobý provoz, systémové DPI/accessibility rozdíly a neplatnost původního manifestu. Cílené kontroly nové logiky tato širší ověření nenahrazují.
5. Před jakýmkoli přepsáním ověřit resolved absolutní cíl přesně `C:\git\phpvpn\dist\client\Windows\x64\phpvpn-tray.exe`, existenci nadřazené distribuce a skutečný PE Machine zdroje i původního cíle `0x8664` (AMD64). Ověřit MZ, PE signature a platné offsety v mezích souboru, ne pouze název složky x64. Zdroj musí být právě výstup úspěšného aktuálního cíle, jiný než cíl. Při nesouladu nekopírovat a určit správný build, bez automatického nahrazení service/CLI. `dist_old_do_not_use` se neprochází za účelem opravy ani nemění.
6. Před přepsáním zaznamenat velikost a SHA-256 původního i nového EXE, konfiguraci, architekturu, podpisový stav a přesné zdrojové/cílové cesty. Původní při dokumentačním auditu měl 650552 bajtů a SHA-256 `c736d74dfda1027d4cc166570b7ad92f0d8fe37e8ec5d765e72f4025dda153c5`. Při implementaci vždy změřit znovu, tento historický údaj neslouží místo aktuální kontroly.

| Metrika | Dokumentační baseline | Doplnit po implementaci před kopií |
|---|---|---|
| Původní tray EXE | 650552 B | Aktuální změřená velikost cíle. |
| Nový tray EXE | Neměřeno, build nebyl proveden. | Velikost aktuálního sestaveného zdroje. |
| Absolutní rozdíl | Nelze zatím určit. | Nová velikost minus původní velikost v B. |
| Procentní rozdíl | Nelze zatím určit. | 100 × (nová minus původní) / původní. |
| Hash před/po | Původní SHA-256 výše, nový neexistuje v této fázi. | Oba SHA-256 před přepsáním, cílový po kopii. |
| Srovnatelnost | Release preset je RelWithDebInfo. | Obě architektury, build konfigurace a podpisový stav, případné nesrovnatelnosti vysvětlit. |
| Zdrojový dopad | Pouze nové Markdown dokumenty. | Počet a bajty změněných C++/resource souborů, nové závislosti a vysvětlení růstu EXE. |

7. Zkopírovat **pouze** tento výsledný soubor do `dist/client/Windows/x64/phpvpn-tray.exe`, například PowerShell `Copy-Item -LiteralPath $trayBuildPath -Destination $trayTargetPath -Force` až po výše uvedeném ověření konkrétních hodnot. Nezapisovat vedlejší EXE, DLL, PDB, ZIP ani manifest. Nepřebalovat distribuci. Po kopii pomocí `Get-FileHash -Algorithm SHA256` a délky ověřit přesnou shodu cíle se sestaveným zdrojem. Read-only porovnání inventáře/hashů ostatních distribučních souborů před/po doloží, že se nezměnily. Je to kontrola rozsahu kopie, nikoli spuštění distribučních testů.
8. Výsledná zpráva musí uvést: změněné zdroje a současně aktualizované client.md, cílené testy s výsledky, přeskočené testy, potvrzení nulového počtu nových IPC příkazů, velikosti/delta/hash a skutečný výsledek kopie. **Nahrazení EXE bez obnovení podepsaného distribučního manifestu může způsobit očekávané fail-closed odmítnutí instalátorem.** Rozpor nesmí být zatajen ani obejit vypnutím ověřování. Jestli má být balíček nadále instalovatelný přes existující ověřovací postup, vyžádat výslovné rozhodnutí, zda se smí cíleně změnit manifest a podpis. To přesahuje pouhou povolenou kopii tray binárky a nesmí se provést automaticky.
