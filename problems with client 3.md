# Nálezy z revize klientského plánu 3

Datum revize: 5. září 2026.

Předmět: `plan3_client.md`, související části `client.md`, `protocol.md` a relevantní implementace klienta.

Revize obsahuje 8 technických nedostatků a 4 významná přijatá rizika. Rozlišuje konkrétní rozpory od míst, kde plán zatím neposkytuje dostatečnou garanci. Jde o statickou revizi návrhu a relevantního kódu, nikoli o výsledky provedených zátěžových nebo souběhových testů. Čísla řádků odkazují na stav souborů při revizi.

## 1. Rušení síťových operací spoléhá na nebezpečný mechanismus

Závažnost: vysoká. Kategorie: stabilita, souběh.

Místo: `plan3_client.md`, řádek 105. Implementace: `client/src/platform/windows/transport_winhttp.cpp`, řádky 112–113 a 207–217.

Plán vyžaduje okamžité zrušení probe před Connect nebo stopem. Současný transport používá synchronní WinHTTP a ruší požadavek zavřením handle z jiného vlákna. Microsoft před zavíráním handle během probíhajícího synchronního volání výslovně varuje kvůli souběhovému závodu.

Dopad: hrozí nestabilita právě při Cancel, Disconnect nebo ukončování služby.

Doporučení: plán by měl výslovně zahrnout opravu transportu, například pomocí asynchronního WinHTTP s korektním řízením životnosti.

Rozhodnutí pro budoucí implementaci: tento bod se vyřeší čistě jako obecný mechanismus bezpečného rušení HTTP operací, nikoli jako lokální oprava jediného kroku Wizardu. Mechanismus musí pokrýt všechny následující události:

- příchod globálního Connectu během Wizard probe, kdy se probe bezpečně zruší, potvrdí se jeho skutečné ukončení a teprve potom začne bootstrap VPN;
- běžný globální Disconnect aktivní VPN, aby se ukončil případný provozní HTTP request a mohl proběhnout teardown;
- stop Windows Service při restartu, aktualizaci, odinstalaci nebo běžném ukončení;
- Cancel, Reset, Escape nebo zavření Wizardu během jeho síťové kontroly.

Požadovaná vlastnost není nulová prodleva. Zrušení musí mít krátkou a konečnou dobu, jednoznačné vlastnictví requestu a potvrzený terminální stav. Další síťová operace nesmí začít, dokud předchozí request nemůže být používán žádným vláknem ani callbackem. Řešení musí zabránit dvojímu zavření, použití handle po zavření, deadlocku a souběžným datovým requestům.

Zdroj: [Microsoft: WinHttpCloseHandle](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpclosehandle).

## 2. Předpokládaný celkový timeout ve skutečnosti chybí

Závažnost: vysoká. Kategorie: dostupnost, latence.

Místo: `plan3_client.md`, řádek 322. Implementace: `client/src/platform/windows/transport_winhttp.cpp`, řádky 118–123 a 270–289.

Plán spoléhá na transportní timeouty. Implementace ale pouze nastavuje `WinHttpSetTimeouts` a potom čte odpověď v cyklu bez celkového deadline. Receive timeout omezuje čekání na část odpovědi, nikoli automaticky celou dobu stahování.

Dopad: server posílající data dostatečně pomalu může obsadit jedinou globální Wizard operaci podstatně déle než deklarovaných 30 sekund.

Doporučení: doplnit monotónní deadline celého requestu a test pomalu dávkované odpovědi.

Rozhodnutí pro budoucí implementaci: toto řešení je schválené a musí se dokončit společně s obecným mechanismem bezpečného rušení z bodu 1. Každý HTTP request dostane jediný absolutní deadline odvozený z monotónních hodin. Jeden časový rozpočet musí zahrnout DNS, navázání spojení, TLS handshake, odeslání requestu, přijetí hlaviček i celé čtení těla. Přijetí dílčího bloku dat nesmí deadline obnovit ani posunout.

Po dosažení deadline se request bezpečně zruší, počká se na potvrzený terminální stav a operace skončí jako timeout. Převod nakonfigurovaných sekund na časové typy WinHTTP musí být kontrolovaný, bez přetečení na zápornou nebo nekonečnou hodnotu, a chyba při nastavení timeoutů nesmí být ignorována. Testy musí zahrnout pomalé DNS, spojení a TLS, zdržení hlaviček, pomalu dávkované tělo, tělo bez korektního ukončení a hraniční konfigurační hodnoty. Po timeoutu musí být transport i síťové vlákno znovu bezpečně použitelné.

Zdroj: [Microsoft: WINHTTP_OPTION_RECEIVE_TIMEOUT](https://learn.microsoft.com/en-us/windows/win32/winhttp/option-flags#winhttp_option_receive_timeout).

## 3. Přesun get-info na jediné síťové vlákno může zastavit přenos VPN

Závažnost: vysoká. Kategorie: latence, dostupnost.

Místo: `plan3_client.md`, řádky 101–107. Implementace: `client/src/core/runtime/vpn_engine.cpp`, řádky 210–222.

Plán sdílí síťové vlákno mezi provozem a diagnostikou. Současný `get-info` provádí health a následně info. Jakmile taková synchronní úloha začne, samotná „přednost provozního spojení“ ji nepřeruší. I po opravě celkových timeoutů mohou dvě pomalé odpovědi zabrat přibližně 60 sekund.

Dopad: mezitím se hromadí odchozí pakety a nevybírá příchozí fronta.

Původní doporučení revize: definovat konkrétní pravidlo přerušování diagnostiky a maximální zdržení datového pollingu, včetně testu při zatížené VPN. Následující rozhodnutí zadavatele určuje výslednou architekturu a má přednost před variantou sdíleného VPN síťového vlákna.

Schválené rozhodnutí zadavatele pro budoucí implementaci: zachovat `get-info` mimo hlavní VPN síťové vlákno, s vlastním dočasným transportem a vlastním kontextem, jako v současné implementaci. Navržený přesun na hlavní VPN síťové vlákno se odmítá. Oprava se zaměří na IPC obsluhu, aby dlouhý diagnostický request neblokoval ostatní řídicí příkazy.

- Hlavní VPN síťové vlákno zůstane vyhrazené pro bootstrap, recovery, časovou synchronizaci a datový provoz VPN. Diagnostika nesmí sdílet jeho proměnný časový kontext, fronty ani provozní čítače.
- IPC obsluha umožní souběžně obsloužit krátké řídicí příkazy i během pomalého `get-info`. Musí zůstat dostupné `get-status` a Disconnect a nesmí být blokováno zastavení služby.
- Nejvýše jedna diagnostická operace `get-info` smí probíhat současně. Nevznikne neomezená fronta ani neomezený počet vláken pro síťovou diagnostiku.
- Diagnostický transport bude používat bezpečné rušení podle bodu 1 a celkový deadline každého HTTP requestu podle bodu 2.
- Stejnou oddělenou řídicí síťovou cestu může používat Wizard probe. Connect a stop služby musí umět probe bezpečně zrušit. Před bootstrapem po přerušení Wizardu se potvrdí skutečné ukončení jeho requestu, aby se jeho data polling nepřekryl s VPN data requestem.

Upřesnění vazby na bod 1: čekání na potvrzené ukončení se vztahuje na rušený request před jeho nahrazením nebo uvolněním jeho prostředků. Nezavádí globální zákaz souběhu samostatného diagnostického health/info a VPN datového provozu. Tři VPN pracovní role se tímto rozhodnutím nemění.

Ověření při implementaci: řízeně pomalý diagnostický health/info během aktivního VPN provozu nesmí zablokovat datové vlákno. Test současně ověří dostupnost krátkých IPC příkazů, omezení souběžné diagnostiky, timeout a bezpečné ukončení služby. Toto je záznam schváleného rozhodnutí, implementace zatím nebyla provedena.

## 4. Starý editor může přepsat právě dokončený Wizard

Závažnost: vysoká. Kategorie: ztráta změn konfigurace, souběh.

Místo: `plan3_client.md`, řádek 170. Související smlouva: `client.md`, příkazy `get-config` a `set-config`.

Plán zavádí společnou serializaci zápisů a invalidaci Wizard baseline po `set-config`. Opačný směr ale chráněný není: editor načte konfiguraci A, Wizard uloží B a editor následně odešle upravenou A přes původní `set-config`, který neobsahuje očekávanou revizi.

Dopad: oba zápisy proběhnou korektně za sebou, přesto se změny B ztratí. Serializace není ochrana proti zastaralému obsahu.

Původní doporučení revize: nový editor potřebuje podmíněné uložení s revizí, případně musí dokument jasně přiznat omezení původního příkazu. Následující rozhodnutí zadavatele tento požadavek na ochranu proti zastaralému obsahu nahrazuje.

Schválené rozhodnutí zadavatele pro budoucí implementaci: použít pravidlo „poslední úspěšné uložení vyhrává“. Zadavatel výslovně přijímá přepsání změn provedených od otevření editoru nebo Wizardu, a to v obou směrech:

- editor uloží konfiguraci a pozdější Finish otevřeného Wizardu ji může přepsat svým výsledným dokumentem;
- Wizard provede Finish a pozdější Save dříve otevřeného editoru může jeho výsledek přepsat;
- stejné pravidlo platí mezi více souběžně otevřenými editory nebo Wizard relacemi. Rozhoduje pořadí úspěšné publikace souboru, nikoli pořadí otevření oken.

Ztráta mezitím uložených změn v těchto scénářích je přijaté chování, nikoli závada vyžadující opravu. Zastaralá revize sama nesmí způsobit odmítnutí Save nebo Finish, vynucené znovuotevření průvodce ani dodatečné potvrzení přepsání. Neprovádí se automatické slučování. Wizard ukládá úplný dokument sestavený ze svého konceptu podle pravidel edit/repair/reset, editor svůj úplný zvalidovaný dokument.

Nadále zůstává povinná úplná validace výsledného dokumentu, serializace všech zápisů služby, atomická publikace, správné ACL, bezpečné ošetření chyb a publikace cache až po úspěšném zápisu. Toto rozhodnutí neruší pravidla rušení, connection epoch, ověření vstupů ani idempotence Finish. Samotná změna uložené revize však nesmí nepřímo blokovat Finish například zneplatněním důkazů pouze kvůli cizímu uložení.

Při následné úpravě plánu je nutné sladit podmíněný commit, invalidaci relací po `set-config`, kontrolu baseline a konfliktní akceptační scénáře s tímto rozhodnutím. Ochrana proti tichému přepsání cizí obsahové změny již není požadovanou vlastností. Bezpečnost souborových operací a konzistence při chybě nebo pádu zůstávají samostatnými požadavky. Bod 4 je tímto uzavřen rozhodnutím o požadovaném chování, nikoli provedenou implementací.

## 5. Idempotence požadavků nemá vyřešenou historii a její limity

Závažnost: vysoká. Kategorie: opakované vedlejší efekty, paměťové limity.

Místo: `plan3_client.md`, řádky 312 a 322.

Plán slibuje, že stejné `clientAttemptId` vždy vrátí stejnou operaci. Stanovuje ale pouze jednu aktuální operaci a neomezuje počet pokusů během relace.

Dopad: pokud se staré identifikátory zapomenou, opožděné opakování může znovu spotřebovat serverovou frontu. Pokud se všechny uchovají, dlouhodobě aktivní relace může neomezeně zvětšovat historii.

Původní doporučení revize: definovat omezený registr pokusů a pravidlo, že vyřazený pokus se odmítne, nikdy znovu nevykoná. Zahrnout Reset i přerušení přes Connect. Pro síťové kontroly následující rozhodnutí zadavatele toto doporučení nahrazuje.

Schválené rozhodnutí zadavatele pro budoucí implementaci, upřesněné v navazujícím rozhovoru: jedno stisknutí Next spustí jeden logický pokus o kontrolu. Tray předá pokus službě, služba provede předepsanou síťovou sekvenci a vrátí výsledek. Zelený výsledek a postup do dalšího kroku jsou dovoleny pouze po doručení a ověření úspěšného výsledku aktuálního pokusu v tray aplikaci.

Selhání kdekoli v řetězci tray → služba → server → služba → tray ukončí pokus chybou. Nedodaná odpověď v příslušném časovém limitu se zobrazí jako timeout, ostatní chyby podle skutečné příčiny. Wizard zobrazí červenou hlášku, zůstane na stejném kroku a zachová vyplněné hodnoty. Tray ani služba nesmějí automaticky znovu odeslat start operace nebo zopakovat síťovou kontrolu, včetně automatického retry po HTTP 401 nebo ztrátě IPC odpovědi. Nový pokus smí vzniknout až dalším výslovným stisknutím Next.

Zadavatel přijímá, že po tomto novém ručním pokusu se síťová operace vykoná znovu, získá nová data a u ověřovacího data pollingu může odebrat a zahodit další zprávy. Předchozí pokus totiž mohl na serveru uspět i tehdy, když se výsledek nedostal do tray. Není požadována historie všech pokusů sloužící k automatickému obnovování po ztrátě odpovědi ani nucené znovuotevření Wizardu po zaplnění takové historie.

Zachovat omezený počet současných operací, korelaci odpovědí, odmítání neplatných nebo expirovaných relací, bezpečné rušení podle bodu 1 a celkový deadline podle bodu 2. Po timeoutu nebo chybě nesmí opožděný úspěch původního pokusu změnit krok na zelený ani ovlivnit nový pokus. Pokud předchozí operace dosud běží, další pokus musí respektovat omezení souběhu a bezpečné ukončení předchozí práce. Průběžné čtení stavu dosud běžící operace v navrženém asynchronním IPC nevyvolává nový síťový pokus. Čekání na doručení výsledku do tray však musí být konečné, aby se pokus nezasekl při výpadku IPC ani po úspěšné odpovědi serveru.

Při následné úpravě plánu sladit pravidlo `clientAttemptId`, zpracování timeoutu IPC, text souhlasu s ověřovací kontrolou a akceptační kritérium A15 s pravidlem „žádný automatický retry, nový pokus pouze novým stisknutím Next“. Rozhodnutí se výslovně týká síťových kontrol. Pravidla opakování Finish, potvrzení zápisu a řešení nejasného výsledku commitu tímto záznamem změněna nejsou. Implementace zatím nebyla provedena.

## 6. Doporučená oprava kontraktu je pro opravitelnou konfiguraci nedostupná

Závažnost: střední. Kategorie: rozpor v uživatelském postupu.

Místo: `plan3_client.md`, řádky 31, 163 a 359.

D13 a chybová hláška doporučují opravit konflikt přes Edit Config. Tabulka dostupnosti však editor pro `repairable` zakazuje. Například konfiguraci bez Access Key lze ve Wizardu opravovat, ale konfliktní kontrakt doporučenou cestou upravit nelze.

Dopad: uživatel dostává návod na nedostupnou funkci a nemůže doporučenou cestou dokončit opravu.

Původní doporučení revize: povolit editor pro čitelnou opravitelnou konfiguraci s úplnou validací při Save, nebo změnit návod na skutečně dostupný postup. Následující rozhodnutí zadavatele rozšiřuje přístup do editoru a nahrazuje původní omezení Edit Config v plánu.

Schválené rozhodnutí zadavatele pro budoucí implementaci:

1. Pokud `config.json` existuje a lze jej přečíst, Edit Config otevře editor s jeho skutečným obsahem i tehdy, když je konfigurace nevalidní, opravitelná nebo obsahuje syntakticky chybný JSON. Platnost konfigurace není podmínkou jejího otevření k opravě. Při načtení se nesmí nevalidní obsah nahradit defaultem nebo starou cache.
2. Pokud soubor skutečně neexistuje, Edit Config zobrazí potvrzení Ano/Ne. Hláška obsahuje skutečně vyhodnocenou absolutní očekávanou cestu, vysvětlení, že aplikace používá tento konfigurační soubor, a otázku, zda jej vytvořit z výchozí šablony. Nerozbalené `%ProgramData%` nestačí.
3. Po Ano služba vytvoří chybějící soubor z výchozí šablony a editor jej otevře k úpravám. Jde o výslovně potvrzené vytvoření na disku už v tomto kroku, nikoli pouze o koncept čekající na Save. Pozdější Cancel editoru tento nově vytvořený soubor nemaže. Po Ne soubor nevznikne a editor pro něj neotevře nový dokument.
4. Editor dostane dole vedle Save a Cancel tlačítko Reset. Po jeho stisknutí se zobrazí potvrzení Ano/Ne s otázkou, zda obnovit výchozí konfiguraci. Ano nahradí pouze obsah editoru výchozí šablonou a označí dokument jako změněný. Disk se při Resetu nemění, změna se uloží až přes Save. Ne zachová rozpracovaný obsah. Cancel po Resetu zahodí neuložené úpravy.
5. Vytvoření i Reset použijí jednu společnou výchozí šablonu konfigurace definovanou v klientském kódu, například prostřednictvím konfiguračního hlavičkového souboru a společné továrny na defaulty. Nedržet rozdílné kopie defaultního JSON pro vytvoření a Reset. Hodnoty musí odpovídat schváleným projektovým defaultům.

Technické důsledky při implementaci: služba nadále vlastní čtení i zápis pevné konfigurační cesty. IPC musí umožnit načíst čitelný nevalidní dokument k editaci a výslovně vyžádat vytvoření chybějícího souboru ze službou definované šablony. Nevzniká obecný příkaz pro libovolné souborové cesty. Nečitelnost existujícího souboru se hlásí jako chyba přístupu, nesmí se zaměnit za neexistenci ani spustit vytvoření náhrady. Pokud soubor mezi potvrzením a vytvořením vznikne, operace vytvoření jej nesmí přepsat, editor načte jeho aktuální obsah nebo zobrazí konkrétní chybu.

Save i vytvoření výchozího souboru nadále vyžadují úplnou validaci dokumentu a atomický zápis podle bodu 4. Následující zpřesněná šablona obsahuje všechna povinná pole a konkrétní hodnoty správného formátu. Její syntaktická a konfigurační platnost nedokládá dostupnost relay ani shodu se skutečným serverovým nastavením. Síťové ověření při Connect zůstává povinné. Původně zvažovaná výjimka umožňující vytvořit konfiguračně nevalidní šablonu není pro tento návrh potřebná.

Zpřesněný návrh výchozí šablony Edit Config podle příkladu dodaného zadavatelem:

```json
{
  "masterSetup": {
    "serverUrl": "http://127.0.0.1/phpvpn.php",
    "trustAnyTlsCertificate": true,
    "serverSecret": "CHANGE_THIS_SERVER_SECRET",
    "vpnCidr": "10.99.99.0/24",
    "virtualIp": "10.99.99.2",
    "accessKey": "0202020202020202020202020202020202020202020202020202020202020202"
  },
  "communicationContracts": {
    "10.99.99.3": {
      "payloadKey": "918c488528c2bc41a90f000b0a63bab0ba418790058ea7b448a3760aec16349d"
    }
  }
}
```

Upřesnění návrhu:

- URL obsahuje prostý text bez Markdown odkazu. Loopback adresa označuje relay na stejném počítači, uživatel ji může v editoru změnit.
- `serverSecret` používá již schválený společný default `CHANGE_THIS_SERVER_SECRET` podle D16. Náčrt s `PLEASE_CHANGE_THIS_SERVER_SECRET` není zaveden jako druhý, odlišný default. Pokud bude požadována změna této konstanty, musí se provést společně na serveru, klientu a v dokumentaci.
- `accessKey` má 64 malých hexadecimálních znaků, tedy dvojici `02` opakovanou 32krát. Délka dodaného příkladu byla ověřena.
- `payloadKey` byl nezávisle vypočten jako SHA-256 přesných ASCII bajtů `10.99.99.2_10.99.99.3`, bez konce řádku, v souladu s dohodnutým algoritmem výchozích kontraktů.
- Sekce `runtime` je záměrně vynechána. Použijí se sdílené běhové defaulty, včetně timeoutu 30 sekund. Editor ji umožňuje ručně doplnit.
- Šablona pro vytvoření a Reset v Edit Config obsahuje podle příkladu jeden kontrakt. Tím se automaticky nemění pravidlo Wizardu pro volitelné generování 13 nebo 14 kontraktů. Obě cesty sdílejí základní konstanty a funkce pro odvození klíčů.
- Jde o předvídatelnou výchozí konfiguraci. Platnost formátu neznamená náhodnost nebo utajení klíčů.

Při následné úpravě plánu aktualizovat dostupnost Edit Config, chybové hlášky, IPC, rozložení editoru a akceptační testy. Novou výjimku promítnout do tvrzení, že soubor smí vzniknout pouze přes Finish Wizardu. Instalace, start služby ani samotný Connect nadále nesmějí automaticky vytvářet konfiguraci. Rozhodnutí upravuje Edit Config a jeho Reset, automaticky nerozšiřuje dostupnost ostatních položek menu. Toto je záznam rozhodnutí, implementace zatím nebyla provedena.

## 7. Omezený počet IPC workerů nezaručuje dostupný Disconnect

Závažnost: střední. Kategorie: dostupnost řízení služby.

Místo: `plan3_client.md`, řádek 109.

Plán navrhuje například čtyři workery a limit jednoho dlouhého `get-info`. Čtyři klienti ale mohou obsadit všechny workery neúplnými requesty nebo neodebíranými odpověďmi.

Dopad: konečný deadline jednotlivého spojení pomůže s úklidem, nezaručuje však včasnou obsluhu nového řídicího příkazu.

Doporučení: doplnit pravidla přijímání spojení, absolutní I/O deadline a zátěžový test dostupnosti řízení při obsazených workerech.

Schválené rozhodnutí zadavatele pro budoucí implementaci: oddělit práci podle účelu a použít asynchronní příjem a odesílání IPC. Samotné přidání několika synchronních workerů není dostačující řešení. Přesný počet obslužných vláken není tímto rozhodnutím pevně stanoven.

- Zachovat tři existující VPN pracovní role. Hlavní síťový pracovník zajišťuje VPN provoz, bootstrap, recovery a potřebnou časovou synchronizaci. Datové pakety se nepřenášejí přes řídicí IPC.
- Dlouhá síťová diagnostika má vlastní omezenou obsluhu mimo hlavní VPN síťové vlákno podle bodu 3, s vlastním transportem, bezpečným rušením a timeouty podle bodů 1 a 2.
- Pro úplné a ověřené krátké řídicí příkazy, zejména `get-status` a Disconnect, zajistit dostupnou obslužnou kapacitu nezávislou na dlouhé diagnostice. Přijetí Disconnectu předá požadavek příslušné části služby, samo ještě neznamená dokončený teardown. Wizard nadále čeká na skutečný odpojený stav.
- Čtení neúplného požadavku a čekání na odběr odpovědi realizovat asynchronně, aby tyto fáze nedržely pracovníka vyhrazeného pro vykonávání řídicích příkazů. Příkaz směrovat k příslušné obsluze až po úplném načtení a validaci. Priorita příkazu sama neřeší spojení, ze kterého ještě nelze příkaz rozpoznat.
- Omezit počet současných spojení, velikosti zpráv, paměť i fronty práce. Stanovit konečné absolutní časové limity IPC čtení a zápisu, které dílčí přenos bajtů neobnovuje. Při přetížení přebytečná spojení nebo práci řízeně odmítat, nevytvářet neomezený počet vláken či čekajících požadavků.
- Zastavení Windows Service musí mít vlastní cestu nezávislou na dostupnosti řídicí pipe a bezpečně ukončit čekající IPC i síťové operace.

Garance a ověření: při podporovaném zatížení nesmějí pomalí nebo zaseknutí IPC klienti vyčerpat obsluhu potřebnou pro řízení. Při přetížení musí služba zachovat omezenou spotřebu prostředků a řízené ukončení. Nejde o slib absolutní dostupnosti při libovolném zahlcení veřejného IPC. Testy obsadí spojení neúplnými požadavky a neodebíranými odpověďmi, souběžně spustí pomalou diagnostiku a změří odezvu `get-status`, přijetí i dokončení Disconnectu a zastavení služby. Konkrétní limity a měřitelné cíle odezvy se stanoví v implementačním návrhu. Toto je záznam schváleného rozhodnutí, implementace zatím nebyla provedena.

## 8. Pád tray nemá mechanismus pro deklarované zrušení relace

Závažnost: střední. Kategorie: životní cyklus, dostupnost.

Místo: `plan3_client.md`, řádky 286, 322 a 534, kritérium A33.

A33 slibuje zrušení konceptu a operací při pádu okna nebo ukončení tray. IPC ale používá jednorázová spojení, takže odpojení pipe nerozlišuje běžně dokončený příkaz od pádu aplikace. Popsaná expirace nastává až po 30 minutách.

Dopad: opuštěné relace mohou vyčerpat limit osmi průvodců.

Doporučení: definovat obnovovaný časový pronájem relace a lhůtu úklidu po ztrátě klienta, nebo odpovídajícím způsobem oslabit A33.

Schválené rozhodnutí zadavatele pro budoucí implementaci: zavést jednoduchý obnovovaný časový pronájem každé otevřené Wizard relace. Tray službě pravidelně potvrzuje používání dané relace. Pokud potvrzení dostatečně dlouho nepřijde, služba relaci považuje za neaktivní a provede bezpečný úklid. Nejde o důkaz ukončení procesu tray, stejný stav může způsobit zamrznutí, uspání nebo výpadek IPC.

- Výchozí návrh časování je potvrzení každých 15 sekund a expirace po 60 sekundách bez kontaktu. Intervaly vycházejí z projednaného příkladu, případné doladění při implementaci nemění schválený princip. Měřit uplynulý čas monotónními hodinami, nikoli počítat zmeškané pingy.
- Potvrzování se týká konkrétní otevřené Wizard relace. Platný běžný požadavek k této relaci může obnovit její lhůtu a nahradit samostatné potvrzení. Když uživatel jen vyplňuje formulář, tray odešle krátké potvrzení přes lokální IPC. Obecné dotazy na stav služby nesmějí udržovat cizí opuštěné relace.
- Potvrzení nezahajuje síťovou kontrolu, neposílá nic VPN serveru a nic nepersistuje. Není automatickým opakováním neúspěšného Next podle bodu 5. Samostatný heartbeat nesmí prodlužovat deadline síťového requestu ani čekání na výsledek kontroly v tray.
- Využít existující správu relací a vhodný časovač služby. Kvůli potvrzování nezavádět nové vlákno ani sledování PID. Počet relací a spotřeba prostředků zůstávají omezené.
- Po expiraci relaci označit za neplatnou, bezpečně zrušit její rozpracovanou kontrolu podle bodu 1 a po ukončení práce uvolnit její koncept a prostředky. Úklid nesmí uvolnit objekt stále používaný vláknem nebo callbackem. Opožděný požadavek expirovanou relaci neoživí.
- Normální zavření Wizardu nadále vyžádá úklid bez čekání na expiraci. Obnovená komunikace po delším výpadku nebo uspání zobrazí srozumitelnou informaci, pokud relace mezitím zanikla. Expirace jedné relace neodpojuje globální VPN a neovlivní ostatní Wizard relace.
- Probíhající commit a potvrzení zápisu mají vlastní bezpečnou životnost. Expirace nesmí vracet již provedený zápis ani úspěšný commit vydávat za neprovedený. Potřebný záznam výsledku commitu se uchová podle pravidel řešení nejasného Finish.

Při následné úpravě plánu nahradit původní třicetiminutovou neaktivní expiraci tímto mechanismem a upravit A33 na úklid v omezené lhůtě od ztráty potvrzování. Testy ověří pád tray, vyplňování bez síťových kontrol, opožděné potvrzení, nezávislost dvou relací, souběh expirace s kontrolou a Finish, změnu systémových hodin a chování při uspání a probuzení. Toto je záznam schváleného rozhodnutí, implementace zatím nebyla provedena.

## Přijatá rizika

Následující rizika plán většinou přiznává. Nejde tedy o přehlédnuté požadavky, jejich praktické dopady jsou ale zásadní.

## 9. Kolize IP může poškodit komunikaci jiného počítače

Kategorie: přijaté riziko kolize identity a ztráty zpráv.

Místo: `plan3_client.md`, řádky 250–256.

Výběr podle `clients` při obsazení všech adres automaticky nabídne nejstarší záznam. Ten může patřit stále aktivnímu klientovi. Dva souběžné průvodce také mohou nabídnout stejnou chybějící adresu.

Dopad: následný test přístupu uspěje i pro sdílenou identitu a může odebrat cizí zprávy. Úspěšný check-access neprokazuje jedinečnost IP.

Původní doporučení revize: bez rezervací nelze kolize vyloučit. Při zaplněném rozsahu alespoň automaticky nevybírat potenciálně obsazenou adresu. Zadavatel toto doporučení nepožaduje realizovat.

Rozhodnutí zadavatele: riziko kolize virtuálních IP adres a možného poškození komunikace jiného počítače výslovně bere na vědomí a přijímá. Bod 9 je uzavřen jako přijaté riziko bez požadavku na opravu. Zachovat navržený výběr IP včetně volby podle nejstaršího `lastSeen`, pokud jsou všechny kandidátní adresy přítomné. Nezavádět kvůli tomuto nálezu rezervace IP, novou detekci obsazenosti ani blokování tohoto návrhu adresy.

Dosavadní upozornění, že návrh IP není rezervací a kontrola může odebrat čekající zprávy, zůstávají podle plánu. Toto rozhodnutí nemění validaci formátu IP, příslušnosti do CIDR ani zákaz kontraktu na vlastní IP. Přijaté riziko se při realizaci nemá vykazovat jako nevyřešená povinná oprava.

## 10. Výchozí payload klíče nejsou tajné

Závažnost: kritická pro důvěrnost. Kategorie: přijaté bezpečnostní riziko výchozích klíčů.

Místo: `plan3_client.md`, řádky 252 a 270–280.

SHA-256 dvojice IP umožní klíč vypočítat komukoli včetně relay serveru. U zachyceného provozu tak neplatí předpoklad, že server ani pozorovatel nedokážou payload dešifrovat. Předvídatelné Access Keys navíc umožňují držiteli společného server secretu vystupovat za ostatní výchozí IP.

Dopad: změna samotného `SERVER_SECRET` neopraví veřejně odvoditelné payload klíče.

Původní doporučení revize: bezpečný profil vyžaduje skutečně tajné distribuované klíče. Zadavatel toto doporučení nepožaduje realizovat.

Rozhodnutí zadavatele: předvídatelnost výchozích payload klíčů a Access Keys i uvedené dopady výslovně přijímá. Bod 10 je uzavřen jako vědomě přijaté bezpečnostní riziko bez požadavku na opravu. Zachovat dosavadní algoritmy výchozích klíčů a dohodnutou šablonu. Kvůli tomuto nálezu nezavádět povinné náhodné klíče, jejich distribuci, rotaci ani další schvalování.

Dosavadní pravdivé označení výchozích klíčů jako veřejně odvoditelných zůstává. Rozhodnutí nemění validaci formátu klíčů ani předepsané kryptografické ověřování protokolu. Přijaté riziko se při realizaci nemá vykazovat jako nevyřešená povinná oprava.

## 11. Reset může zhoršit dosavadní bezpečnost konfigurace

Kategorie: přijaté bezpečnostní riziko výchozí konfigurace, srozumitelnost UI.

Místo: `plan3_client.md`, řádky 24 a 150.

Reset obnoví HTTP URL, veřejný secret a `trustAnyTlsCertificate=true`. Uživatel může následně zadat HTTPS URL a vlastní secret, ale vypnuté ověřování certifikátu zůstane skryté mezi nezobrazenými hodnotami.

Dopad: resetovaná konfigurace může mít slabší bezpečnostní politiku než původní konfigurace. Jde o výslovně přijaté rozhodnutí D06.

Původní doporučení revize: přímo před Finish viditelně uvést výslednou TLS politiku a její změnu oproti původní konfiguraci. Zadavatel toto doporučení nepožaduje realizovat.

Rozhodnutí zadavatele: bod 11 je uzavřen jako vědomě přijaté riziko bez požadavku na opravu. Zadavatel výslovně potvrzuje `trustAnyTlsCertificate=true` jako výchozí nastavení konfigurace vytvářené Wizardem. Stejná hodnota platí po Resetu Wizardu a ve společné výchozí šabloně pro vytvoření a Reset v Edit Config. Kvůli tomuto nálezu se nepřidává nové zobrazení změny TLS politiky před Finish ani další bezpečnostní schvalování.

Jde o výchozí hodnotu pro novou nebo resetovanou konfiguraci. Při běžné úpravě platné nebo opravitelné existující konfigurace nadále platí zachování nezobrazených hodnot podle D07. Existující explicitní `trustAnyTlsCertificate=false` se samotným průchodem Wizardem nepřepne na true. Při vynechaném poli se použije sdílený default true. Přijaté riziko se při realizaci nemá vykazovat jako nevyřešená povinná oprava.

## 12. Veřejné IPC ruší izolaci konfigurace mezi lokálními uživateli a aplikacemi

Kategorie: přijaté bezpečnostní omezení celého klienta.

Místo: `plan3_client.md`, řádek 336. Související smlouva: `client.md`, veřejné IPC a příkazy `get-config` a `set-config`.

Plán zachovává přístup bez autorizace. Libovolná lokální aplikace tak prostřednictvím původních příkazů může získat všechny klíče, změnit konfiguraci nebo globálně odpojit VPN.

Dopad: ACL konfiguračního souboru tuto cestu nechrání a náhodná UUID nejsou autorizace.

Doporučení: zachovat toto omezení výslovně uvedené v bezpečnostním modelu. Jde o vědomě přijaté bezpečnostní omezení celého klienta, nikoli o ochranu, kterou by vyřešila přísnější validace nových Wizard příkazů.

Rozhodnutí zadavatele: bod 12 je uzavřen jako vědomě přijaté bezpečnostní riziko bez požadavku na opravu. Zachovat veřejný lokální přístup k IPC podle dosavadní smlouvy. Kvůli tomuto nálezu nezavádět autorizaci uživatelů nebo aplikací, impersonaci, kontrolu PID ani omezení přístupu ke konfiguraci podle identity klienta.

Toto rozhodnutí nemění odmítání vzdálených IPC klientů, striktní validaci zpráv, pevnou konfigurační cestu ani schválené limity a dostupnost obsluhy podle bodu 7. Přijaté riziko se při realizaci nemá vykazovat jako nevyřešená povinná oprava.

## Podklady pro závěr sezení a aktualizaci plánu 3

Výslovný požadavek zadavatele: schválená rozhodnutí z tohoto sezení zahrnout do závěru a následně konzistentně promítnout do `plan3_client.md`, včetně souvisejících pravidel, textů UI, testů a akceptačních kritérií. Záznam v tomto souboru je podkladem pro tuto aktualizaci, sám neznamená, že plán nebo implementace již byly změněny. Původní doporučení revize, která zadavatel odmítl, se nesmějí znovu zavést jako povinné opravy.

Výslovně zahrnout rozhodnutí z bodu 11: `trustAnyTlsCertificate=true` je společný výchozí stav pro novou konfiguraci Wizardu a pro Reset i vytvoření podle výchozí šablony Edit Config. Běžná editace existující konfigurace nadále zachovává její explicitní TLS hodnotu. Bod 11 je přijaté riziko bez dodatečné opravy nebo nového schvalování.

Body 9, 10, 11 a 12 zadavatel výslovně uzavřel jako vědomě přijatá rizika bez požadavku na opravu. Aktualizace plánu musí respektovat jejich jednotlivá rozhodnutí uvedená výše.
