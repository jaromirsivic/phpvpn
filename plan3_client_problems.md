# Nálezy z revize `plan3_client.md`

Datum revize: 6. září 2026.

Předmět revize: `plan3_client.md`, porovnání s `protocol.md`, `client.md` a relevantními částmi současné implementace klienta.

Jde o statickou revizi návrhu a kódu. Nejde o výsledek implementace, překladu, zátěžových testů ani bezpečnostního auditu běžícího systému. Čísla řádků odkazují na stav souborů při této revizi.

## 1. Výchozí klíče ruší deklarované bezpečnostní předpoklady protokolu

Závažnost: vysoká. Kategorie: důvěrnost, autentizace, rozpor se specifikací.

Místo: `plan3_client.md`, řádky 313, 331 a 341. Související specifikace: `protocol.md`, řádky 74–75.

Plán odvozuje výchozí Access Key z posledního oktetu IP adresy a výchozí payload klíč pouze z veřejné dvojice VPN adres. Protokol naproti tomu popisuje Access Key jako náhodný klíč unikátní pro klienta a payload klíč jako tajemství známé pouze komunikujícím klientům.

Kdokoli včetně relay serveru si výchozí payload klíče může vypočítat. Relay nebo pozorovatel se zachyceným ciphertextem pak může dešifrovat provoz používající tyto výchozí klíče. Držitel společného server secretu si také může vypočítat výchozí Access Key libovolné podporované adresy a vydávat se za tento klient.

Plán předvídatelnost přiznává jako produktové rozhodnutí. Dokumentační matice ale dostatečně neřeší rozpor s bezpečnostními cíli a tvrzením o utajení klíčů v `protocol.md`.

Doporučení: v `protocol.md` a `client.md` výslovně vymezit demonstrační výchozí profil a uvést, že u něj neplatí deklarovaná důvěrnost vůči relay ani výlučná znalost klíčů. Pro bezpečný profil vyžadovat náhodně vytvořené a bezpečně distribuované klíče. Samotná změna `SERVER_SECRET` veřejně odvoditelné payload klíče neopraví.

Rozhodnutí zadavatele: bod se nebude technicky řešit a zadavatel s popsaným bezpečnostním rizikem výslovně souhlasí. Zachovají se předvídatelné výchozí Access Keys i algoritmus odvození payload klíčů z dvojice VPN adres. Kvůli tomuto nálezu se nezavedou povinné náhodné klíče, jejich automatická distribuce, rotace ani nový bezpečný profil.

Zůstává vědomě přijaté riziko, že výchozí payload klíče může vypočítat kdokoli včetně relay serveru a že držitel společného server secretu může vypočítat výchozí Access Key podporované adresy. Dokumentace musí tento dopad popisovat pravdivě a nesmí výchozí klíče označit za tajné nebo bezpečně náhodné. Bod se při implementaci nemá vykazovat jako povinná technická oprava.

## 2. Pomalé Show Info může v tray zabránit odeslání Disconnectu

Závažnost: střední až vysoká. Kategorie: dostupnost řízení, stabilita uživatelského rozhraní.

Místo: `plan3_client.md`, řádky 117, 121, 491, 495 a 522. Současná implementace: `client/apps/tray/main.cpp`, řádky 187–196, a `client/apps/tray/status_reader.cpp`, řádky 111–125.

Plán správně odděluje dlouhou diagnostiku od krátkého řízení ve službě. Nevyžaduje však stejně jednoznačně oddělení na straně tray. Současná tray používá pro Show Info, Connect, Disconnect i další příkazy jediný `CommandReader`. Pokud již příkaz běží, `startCommand` další požadavek neodešle.

Scénář: uživatel spustí Show Info, server odpovídá pomalu a uživatel následně zvolí Disconnect. Požadavek Disconnect se vůbec nedostane ke službě, přestože služba má mít nezávislou kapacitu pro krátké řízení.

Doporučení: v plánu výslovně oddělit diagnostický kanál nebo čtečku tray od Connect/Disconnect a ostatního krátkého řízení. Doplnit regresní test, který během čekajícího Show Info ověří skutečné odeslání a dokončení Disconnectu.

Rozhodnutí zadavatele: službová část oddělení dlouhé diagnostiky od krátkého řízení se považuje za již vyřešenou v aktuálním plánu. Zbývající povinná oprava se musí provést v tray aplikaci. Tray musí umožnit odeslat Disconnect i v době, kdy čeká na dokončení Show Info nebo jiné dlouhé diagnostické operace. Společný obsazený `CommandReader` nesmí takový požadavek tiše zahodit ani zabránit jeho předání službě.

Implementace má pro Disconnect zajistit nezávislou řídicí cestu nebo ekvivalentní oddělení od diagnostického požadavku. Uživatelská akce musí skončit buď skutečným předáním Disconnectu službě, nebo viditelnou konkrétní chybou. Nesmí vzniknout stav, kdy kliknutí nemá žádný účinek a uživatel o tom není informován. Regresní test musí během čekajícího Show Info ověřit přijetí Disconnectu službou a jeho dokončení, ne pouze zpracování kliknutí v UI.

## 3. Časový rozpočet původního get-info není sladěný s tray

Závažnost: střední. Kategorie: latence, falešný timeout, nekonzistentní stav UI.

Místo: `plan3_client.md`, řádky 67, 111, 129 a 462. Současná implementace: `client/apps/tray/status_reader.cpp`, řádek 118.

Původní `get-info` má zachovat přímý formát odpovědi a provádí sekvenci health a info. Každý HTTP request může mít podle plánu vlastní celkový timeout 30 sekund. Současná tray ale čeká na celý IPC příkaz pouze 30 sekund.

Například health trvající 20 sekund a info trvající dalších 20 sekund jsou oba jednotlivě v limitu, ale tray oznámí timeout před dokončením platné operace. Pozdější výsledek může být zahozen a dočasná diagnostická kapacita služby zůstane obsazená déle, než UI očekává.

Doporučení: definovat jeden konečný rozpočet celého `get-info`, který zahrnuje oba HTTP requesty, lokální zpracování a doručení přes IPC. S tímto rozpočtem sladit tray i službu. Prodloužení timeoutu musí být spojeno s nápravou nálezu 2, jinak se prodlouží doba, po kterou tray blokuje další příkazy.

Rozhodnutí zadavatele: bod se nebude technicky řešit. Timeout příkazu Show Info v tray zůstane 30 sekund a kvůli tomuto nálezu se nebude měnit služba, zavádět delší zvláštní timeout ani předávat dynamický časový rozpočet. Přijímá se předpoklad, že health, info, lokální zpracování a doručení výsledku službou v běžném provozu skončí výrazně rychleji a celý výsledek se do tray dostane do 30 sekund.

Zůstává vědomě přijaté riziko, že pomalý, ale z pohledu jednotlivých HTTP deadline stále platný průběh může být v tray označen jako timeout a jeho pozdější výsledek nebude použit. Tento bod se při implementaci nemá vykazovat jako povinná oprava. Rozhodnutí nemění povinnou opravu bodu 2, tedy možnost odeslat Disconnect nezávisle na probíhajícím Show Info.

## 4. Změna vlastní IP při zachování kontraktů může rozbít komunikaci s peery

Závažnost: střední. Kategorie: funkční stabilita, konfigurace klíčů.

Místo: `plan3_client.md`, řádky 25, 174, 327 a 389. Související specifikace: `client.md`, řádek 73, a `protocol.md`, řádky 402–427.

V režimu edit nebo repair smí Wizard změnit vlastní IP, ale existující komunikační kontrakty zachová. Kontrola Access Key ověří pouze schopnost autentizace vůči relay. Neověří, zda protistrany používají pro novou dvojici adres odpovídající payload klíče.

Příklad: klient změní vlastní adresu z `.2` na `.4` a zachová kontrakt pro peer `.3`. Lokální kontrakt může stále obsahovat klíč odvozený nebo distribuovaný pro původní dvojici `.2/.3`. Kontrola přístupu projde, ale peer `.3` může pro adresu `.4` používat jiný klíč nebo vůbec nemít odpovídající kontrakt.

Doporučení: při změně vlastní IP výrazně upozornit, že všechny kontrakty a konfigurace protistran musí být koordinovaně aktualizovány. Pokud se zachová rozhodnutí neměnit kontrakty automaticky, musí být tento důsledek součástí UI, dokumentace a integračních testů.

Rozhodnutí zadavatele: bod se nebude technicky ani uživatelsky řešit a zadavatel s popsaným funkčním rizikem výslovně souhlasí. Při změně vlastní IP se zachovají existující komunikační kontrakty podle plánu. Kvůli tomuto nálezu se nebudou kontrakty automaticky přepočítávat nebo odstraňovat, nepřidá se další povinné varování a nevznikne nová síťová kontrola shody s konfiguracemi protistran.

Zůstává vědomě přijaté riziko, že kontrola Access Key úspěšně ověří přístup k relay, přestože komunikace s některými peery následně nebude fungovat kvůli neshodným payload klíčům nebo chybějícím kontraktům pro novou IP. Bod se při implementaci nemá vykazovat jako povinná oprava.

## 5. Pravidelná kontrola konfigurace nemá konkrétní limit diskového zpracování

Závažnost: střední. Kategorie: paměť, I/O latence, dostupnost služby.

Místo: `plan3_client.md`, řádky 141, 182–184 a 407. Současná implementace: `client/src/core/runtime/vpn_engine.cpp`, řádky 20–33.

ConfigStore má pro každý čitelný soubor uchovat přesný text, vypočítat SHA-256 a u platného nebo opravitelného dokumentu vytvořit DOM. Tray žádá kontrolu při startu, každých pět sekund, při otevření menu a po uložení. Více tray relací může požadavky násobit.

Limit 4 MiB IPC zprávy neurčuje, kolik dat smí služba načíst z disku před odmítnutím. Současné `readWholeFile` načte celý soubor bez předběžného velikostního omezení. Omylem zkopírovaný nebo úmyslně vytvořený velmi velký soubor tak může vyvolávat opakované velké alokace, hashování a parsování.

Doporučení: stanovit maximální velikost konfiguračního souboru a vynutit ji během omezeného čtení, ne až při serializaci IPC. Sloučit souběžné refresh požadavky, cacheovat výsledek podle bezpečně zjištěné identity a změny souboru a zajistit, aby pomalé diskové zpracování neblokovalo `get-status`, Disconnect ani stop služby.

Rozhodnutí zadavatele: odstranit pravidelný pětisekundový `get-config-state` i načtení stavu konfigurace při pouhém otevření tray menu. Položky menu závislé na konfiguraci zůstanou kliknutelné. Aktuální stav souboru načte služba až poté, co uživatel zvolí konkrétní položku, a před provedením této akce. Chybějící, nečitelná, opravitelná nebo neplatná konfigurace vyvolá odpovídající zprávu nebo nabídku nápravy až po kliknutí.

Pro jednu akci se nemá nejprve provést kontrola při otevření menu a následně druhá kontrola před jejím vykonáním. Zůstane pouze druhá, autoritativní kontrola bezprostředně vyvolaná konkrétní akcí. Pravidlo posledního úspěšného zápisu se nemění a uložená revize nadále neblokuje Save ani Finish. Disconnect se řídí paměťovým stavem aktivní VPN a musí zůstat dostupný bez ohledu na stav konfiguračního souboru.

Toto rozhodnutí nahrazuje části D02, tabulky 5.4 a akceptačních kritérií, které podle předem známé dostupnosti konfigurace zakazují Connect, Show Info, Statistics nebo Contracts. Tyto položky musí být možné zvolit a teprve jejich obsluha načte aktuální stav konfigurace a případně zobrazí chybu nebo návod. Configuration Wizard, Edit Config a Exit zůstávají dostupné podle svých dosavadních pravidel.

Odstraněním periodické a menu kontroly odpadá hlavní opakovaná I/O zátěž popsaná v tomto bodu. Jednorázové čtení po zvolení akce přesto musí mít rozumný velikostní limit, aby nepřiměřeně velký soubor nezpůsobil vyčerpání paměti nebo dlouhé blokování služby. Tato ochrana se týká jedné autoritativní kontroly, ne periodického pollingu.

## 6. Výjimka z ověřování payload GCM není dotažená do protocol.md

Závažnost: střední. Kategorie: normativní rozpor.

Místo: `plan3_client.md`, řádky 319–321, 470 a 498. Související specifikace: `protocol.md`, řádky 498–500.

Wizard má po ověření struktury a `data-mac` přijaté záznamy zahodit bez ověření payload GCM. Obecné pravidlo protokolu ale po úspěšném ověření `data-mac` požaduje ověřit každý datový řádek pomocí AES-GCM.

Navržené chování Wizardu dává smysl, protože Wizard nezná nebo nepotřebuje všechny per-peer payload klíče a nesmí data doručovat. Stále však jde o výjimku z dosavadní normativní formulace. Matice změn jmenuje zejména kapitoly 5.2, 11.2 a 13, ale povinnost v kapitole 9.5 výslovně neřeší.

Doporučení: doplnit do `protocol.md` omezený diagnostický režim. Musí být jasné, že ověření `data-mac` dokládá autentizaci serverové odpovědi a integritu uspořádaného seznamu, ale bez GCM ověření nedokládá autenticitu ani čitelnost payload plaintextu. Normální Connect a provoz musí nadále každý payload plně ověřit.

Rozhodnutí zadavatele: `protocol.md` nesmí zmiňovat tray aplikaci, Configuration Wizard, IPC příkaz ani ovládací prvky uživatelského rozhraní. Doplní pouze krátkou a úzce vymezenou obecnou možnost jednorázové diagnostické kontroly přístupu prováděné klientem. V této operaci klient odešle nejvýše jeden prázdný request `data`, který obsahuje pouze `auth-full` a LF. Po úplném ověření HTTP úspěchu, struktury NDJSON, korelace `requestId`, všech velikostních a početních limitů, nulového výsledku pro prázdný request a platného závěrečného `data-mac` smí diagnostická operace všechny přijaté datové záznamy zahodit bez ověření jejich payload GCM a bez dešifrování.

Protokol tuto možnost nepovolí pro normální bootstrap připojení, recovery ani pravidelný provozní polling `data`. Diagnostická kontrola nesmí žádný přijatý payload uložit, doručit síťové vrstvě, zařadit do provozní fronty, předat jiné části klienta ani jinak použít. Nesmí ani odstranit položku z provozní odchozí fronty nebo změnit provozní stav klienta.

Text protokolu současně uvede, že taková kontrola ověřuje Access Key a autentizovanou strukturu serverové odpovědi, ale neprokazuje autenticitu, integritu nebo čitelnost plaintextu jednotlivých payloadů. Normální bootstrap, recovery a běžný VPN provoz musí nadále po ověření `data-mac` provést úplné GCM ověření každého zpracovávaného datového záznamu a platné payloady zpracovat podle běžných pravidel.

Konkrétní vazba této obecné protokolové možnosti na `wizard-check-access`, `ProbeClient`, stisknutí Next a krok IP Address a Access Key Configuration patří výhradně do `client.md` a `plan3_client.md`. Tyto dokumenty stanoví, že právě tato Wizard operace používá obecný diagnostický režim, provede nejvýše jeden prázdný polling a nesmí ovlivnit tunel, fronty, čítače, kontrolní body ani aktivní stav VPN.

## 7. Ruční opakování Wizard kontroly nemá pravidlo pro Retry-After

Závažnost: střední. Kategorie: zatížení serveru, protokolová shoda.

Místo: `plan3_client.md`, řádky 271, 317 a 401. Související specifikace: `protocol.md`, řádek 611.

Protokol požaduje respektovat `Retry-After` jako dolní hranici čekání. Plán zakazuje automatické retry, ale dovoluje založit nový pokus dalším stisknutím Next. Neříká, zda a jak se `Retry-After` uplatní na tento nový ruční pokus.

Uživatel tak může po odpovědi `503` okamžitě opakovaně spouštět health, info a případně data sekvenci proti již přetíženému serveru.

Doporučení: po ověřeném a syntakticky platném `Retry-After` stanovit nejbližší povolený čas nového ručního pokusu. UI má zbývající čekání pravdivě zobrazit a Next do té doby zakázat. Neautentizovanou chybovou odpověď nelze použít jako bezpečnostní důkaz, lze ji však použít jako konzervativní omezení zatížení. Automatický retry se tím nezavádí.

Rozhodnutí zadavatele: bod se nebude technicky ani uživatelsky řešit a zadavatel s popsaným rizikem výslovně souhlasí. Wizard nebude mezi dvěma ručně vyvolanými pokusy vynucovat hodnotu `Retry-After`, dočasně zakazovat Next, zobrazovat odpočet ani plánovat nejbližší povolený čas dalšího pokusu.

Každé nové stisknutí Next nadále okamžitě založí nový samostatný pokus, i když předchozí odpověď `503` obsahovala `Retry-After`. Zůstává vědomě přijaté riziko dalšího zatížení již přetíženého serveru a odchylky od obecného doporučení protokolu. Automatické opakování se nezavádí. Bod se při implementaci nemá vykazovat jako povinná oprava.

## 8. „Každý čitelný soubor“ nelze beze změny přenést jako UTF-8 JSON string

Závažnost: nižší až střední. Kategorie: datová věrnost, editor, IPC kontrakt.

Místo: `plan3_client.md`, řádky 141, 216, 347 a 379.

Plán slibuje vrácení skutečného textu každého čitelného konfiguračního souboru, včetně syntakticky chybného JSON. IPC je přitom UTF-8 JSON a obsah dokumentu se přenáší jako JSON string.

Soubor může být na úrovni filesystemu čitelný, ale obsahovat neplatné UTF-8 nebo jiné kódování. Přímé vložení těchto bajtů poruší IPC. Náhrada neplatných sekvencí zase změní obsah a může způsobit, že editor při uložení poškodí původní data jiným způsobem.

Doporučení: přesně definovat podporované kódování. Nejjednodušší varianta je rozlišit `readable` a `text-editable`: soubor s neplatným UTF-8 je čitelný jako bajty, ale editor jej odmítne s konkrétní chybou kódování. Pokud je nutné přenášet libovolné bajty, musí IPC použít explicitní binární kódování a editor musí mít definovanou konverzi.

Rozhodnutí zadavatele: konfigurační soubor musí být platný a jednoznačně dekódovatelný text v UTF-8. Samotná možnost načíst jeho bajty z filesystému nestačí. Obsah v jiném kódování nebo s neplatnou UTF-8 sekvencí služba odmítne konkrétní chybou kódování a nepokusí se neplatné bajty nahrazovat, odhadovat jiné kódování ani je přenášet přes IPC pomocí base64.

Současná implementace přijímá volitelný UTF-8 BOM na úplném začátku konfiguračního souboru a před parsováním ho odstraní. Toto chování lze zachovat jako jedinou výslovně popsanou vstupní normalizaci. BOM nemění požadované kódování UTF-8 a nesmí být přijat uprostřed dokumentu. `client.md`, plán, klasifikace dostupnosti a testy musí rozlišit chybu přístupu od čitelného souboru s nepodporovaným nebo poškozeným kódováním.

Edit Config otevře syntakticky chybný JSON pouze tehdy, když je jeho obsah platný UTF-8 text. Testy musí zahrnout validní vícebajtové znaky, neplatné pokračovací bajty, zkrácené sekvence, jiné běžné kódování a pravidla BOM. Tím se odstraní rozpor mezi požadavkem na skutečný text dokumentu a UTF-8 JSON obálkou IPC.

## Významné přijaté důsledky návrhu

Následující body nejsou přehlédnuté kontradikce, protože je plán výslovně přiznává. Jejich praktický dopad je ale významný a musí zůstat viditelný v dokumentaci a testech.

### A. Hladký průchod Wizardem provede šest HTTP requestů

Krok URL provede jeden health, krok secret nový health a info, krok access další health, info a jeden data polling. Při výchozím 30sekundovém deadline každého requestu představuje samotná síťová část teoreticky až 180 sekund.

Opakování má bezpečnostní důvod, protože každý krok váže důkaz k aktuálním vstupům a access kontrola potřebuje čerstvý časový kontext. Cena musí být zahrnuta do očekávané odezvy UI a testů. Neměla by zvyšovat latenci běžného VPN provozu, pokud bude správně dodrženo oddělení a preempce popsaná v plánu.

### B. Návrh nejstarší IP může vybrat stále aktivní počítač

Pokud jsou všechny kandidátní adresy přítomné v best-effort seznamu `clients`, plán vybere nejstarší `lastSeen`. Tato adresa může stále patřit aktivnímu klientovi. Následná access kontrola může odebrat jeho čekající zprávy a pozdější připojení může vytvořit dva klienty se stejnou VPN identitou.

Bez serverové rezervace nebo autoritativní alokace nelze kolizi vyloučit. Plán riziko uživateli oznamuje a výslovně je přijímá.

### C. Veřejné lokální IPC a důvěra libovolnému TLS certifikátu jsou již přijatá rizika

Plán zachovává veřejné lokální IPC, přes které může každá lokální aplikace číst a měnit konfiguraci nebo globálně odpojit VPN. Zachovává také výchozí `trustAnyTlsCertificate=true`. Obě vlastnosti jsou již výslovně popsané v `client.md` a nejsou nově zavedenými chybami plánu 3.

## Celkové hodnocení

Plán řadu dřívějších rizik řeší důkladně, zejména životnost WinHTTP callbacků, absolutní deadline, serializovanou atomickou publikaci konfigurace, konečné pronájmy Wizard relací, omezené IPC a nejasný výsledek Finish.

Významné odchylky od současných `protocol.md` a `client.md` jsou převážně plánované. Patří sem změna normalizace prázdného secretu, rozšíření IPC v1, oddělená diagnostická cesta, jednorázové Wizard kontroly bez automatického recovery a nový způsob správy konfigurace. Před implementací musí být normativní dokumenty aktualizovány společně s přesnými schématy a testy.

Nad rámec již uvedené dokumentační matice je nutné vyřešit zejména bezpečnostní výjimku pro veřejně odvoditelné klíče, výjimku z payload GCM ověřování, odesílání Disconnectu během diagnostiky v tray a úplný časový rozpočet původního `get-info`.
