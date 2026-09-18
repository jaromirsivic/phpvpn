---
intent_version: 3
created_at: 2026-09-13-23-10-32
based_on: intent-v002.md
source: intent-chat-v003.md
---

# Oprava polling backoffu a úpravy klientského UI

## Shrnutí

Zjistit, prokázat a odstranit příčinu chování, při kterém Windows klient po ukončení skutečného síťového provozu nadále volá datový endpoint serveru v minimálním serverem určeném intervalu. Opravený backoff musí po skutečném provozu ponechat dva prázdné polly na minimálním intervalu a zdvojnásobování zahájit až třetím prázdným pollem. Současně upravit následnou akci po uložení Communication Contracts, vzhled a chování okna Server Information, zobrazení Raw Server Information a gramatiku počtu validních online klientů. Změny musí být pokryté přiměřenými testy, podle potřeby promítnuté do dokumentace a zakončené vytvořením aktuální distribuce.

## 1. Polling interval po ukončení provozu

### Pozorovaný scénář

1. Na lokálním počítači běží aktuální serverový skript `D:\Nginx\html\phpvpn.php`, dostupný jako `http://127.0.0.1/phpvpn.php`.
2. Lokální klient se k serveru připojí a spojení se jeví jako funkční.
3. Nejnovější dostupná klientská distribuce se nainstaluje také na jiný počítač, konkrétně na virtuální stroj, a připojí se ke stejnému serveru.
4. Mezi stroji proběhne úspěšný ping.
5. Po skončení pingu už nejsou odesílány ani přijímány další VPN pakety.
6. Přesto klient pokračuje v dotazování serveru přibližně každých 50 ms. Polling interval se při nečinnosti nezačne zvětšovat.

### Projektová fakta a pracovní hypotéza

- Serverem předávané limity jsou v klientovi reprezentovány hodnotami `minDataPollIntervalMs` a `maxDataPollIntervalMs`. Aktuální výchozí hodnoty v kódu jsou 50 ms a 3200 ms.
- `NetworkWorker::nextPollDelayMs(bool hadTraffic)` v `client/src/core/runtime/network_worker.cpp` resetuje interval na minimum při `hadTraffic == true` a při nečinnosti ho násobí dvěma až do maxima.
- `VpnEngine::runNetworkThread()` v `client/src/core/runtime/vpn_engine.cpp` nyní volá tuto funkci s podmínkou `packetsReceived_ > 0 || packetsSent_ > 0`.
- `packetsReceived_` a `packetsSent_` jsou v `runConnectedIteration()` kumulativní čítače za připojení. Po prvním přeneseném paketu proto podmínka zůstává pravdivá i v dalších prázdných iteracích.
- Toto je silná pracovní hypotéza, nikoli náhrada požadovaného ověření. Realizace ji musí reprodukcí nebo cíleným testem potvrdit, případně zdokumentovat jinou skutečnou příčinu.
- Přímé testovací pokrytí volání `nextPollDelayMs` z běhu připojeného klienta nebylo při přípravě intentu nalezeno.

### Definice provozu a prázdného pollu

- Za skutečný provoz v aktuální datové výměně se považuje alespoň jeden VPN paket skutečně odeslaný nebo přijatý klientem.
- Jakýkoli takový paket vynuluje čítač po sobě jdoucích prázdných pollů a vrátí polling interval na serverem povolené minimum.
- Za prázdný poll se považuje dokončená datová výměna, při které nebyl odeslán ani přijat žádný VPN paket.
- Samotný úspěšný HTTP požadavek, prázdná serverová odpověď, keepalive, health kontrola, časová synchronizace ani historický provoz se nepovažují za skutečný provoz aktuální datové výměny.

### Požadovaný backoff po provozu

- Po každé aktivní datové výměně je interval na serverem povoleném minimu a čítač prázdných pollů je nula.
- První následující prázdný poll zvýší čítač na jedna, ale další poll se stále naplánuje v minimálním intervalu.
- Druhý po sobě jdoucí prázdný poll zvýší čítač na dva, ale další poll se stále naplánuje v minimálním intervalu.
- Třetí po sobě jdoucí prázdný poll zahájí backoff. Následující interval se zvětší na dvojnásobek minima.
- Každý další po sobě jdoucí prázdný poll interval znovu zdvojnásobí až k serverem určenému maximu.
- Při minimu 50 ms je očekávaná posloupnost po poslední aktivní výměně: 50 ms, 50 ms pro první dva prázdné polly, poté 100 ms, 200 ms, 400 ms, 800 ms a dále až k maximu.
- Nový odeslaný nebo přijatý VPN paket v libovolné fázi backoffu okamžitě vrátí interval na minimum a vynuluje čítač prázdných pollů.
- Serverem určené minimum a maximum jsou závazné. Klient je nesmí podkročit ani překročit.

### Diagnostika a oprava

- Reprodukovat problém na aktuálním zdrojovém stavu nebo vytvořit deterministický test zachycující provoz a následné prázdné iterace.
- Prokázat, zda je příčinou použití kumulativních čítačů místo aktivity aktuální datové výměny. Pokud se hypotéza nepotvrdí, pokračovat až k doložené skutečné příčině.
- Implementovat explicitní, bezpečně resetovaný stav pro počet po sobě jdoucích prázdných pollů nebo ekvivalentní řešení se stejným pozorovatelným chováním.
- Rozhodnutí o backoffu musí vycházet z výsledku aktuální výměny, nikoli z kumulativních diagnostických čítačů.
- Oprava nesmí porušit zpracování paketů, recovery logiku, časovou synchronizaci, health kontrolu ani diagnostické kumulativní čítače.
- Stav prázdných pollů musí být správně inicializován a resetován při konfiguraci, novém připojení, odpojení, recovery a změně serverových limitů podle životního cyklu současného polling stavu.
- Nasazený `D:\Nginx\html\phpvpn.php` se nemá měnit, pokud diagnostika neprokáže podíl serveru. Případná nutná serverová změna musí vzniknout nejprve v autoritativním zdroji repozitáře.

### Povinné regresní scénáře

- Aktivní výměna s odeslaným paketem resetuje interval a prázdný čítač.
- Aktivní výměna s přijatým paketem resetuje interval a prázdný čítač.
- První a druhý prázdný poll po provozu ponechají interval na minimu.
- Třetí prázdný poll zvýší následující interval na dvojnásobek minima.
- Další prázdné polly pokračují v posloupnosti násobku dva až k maximu.
- Historický paket už po vyprázdnění provozu neudržuje polling trvale na minimu.
- Nový provoz uprostřed backoffu okamžitě vrátí interval na minimum a znovu poskytne dva prázdné polly na minimu.
- Mezní hodnoty serverového minima a maxima zůstávají dodržené.
- Regresní test musí selhat proti původnímu chybnému chování a projít po opravě.

## 2. Uložení Communication Contracts při aktivním připojení

### Současný stav

Po uložení změněného Communication Contract v připojeném stavu se zobrazuje pouze informace:

`Your changes have been saved. Disconnect and connect again to apply them.`

### Požadované chování

- Pokud je klient v okamžiku úspěšného uložení připojený, zobrazit potvrzovací dialog, který sdělí, že změny byly uloženy, že pro jejich použití je striktně vyžadováno odpojení a nové připojení, a zeptá se na okamžité odpojení.
- Výchozí anglický text dialogu má významově odpovídat:

  `Your changes have been saved. It is strictly required that you disconnect and connect again to apply them. Do you want to disconnect now?`

- Dialog musí mít volby `Yes` a `No`.
- `Yes` je výchozí odpověď.
- Volba `Yes` okamžitě spustí standardní odpojení klienta. Neprovádí automatické opětovné připojení.
- Volba `No` dialog zavře, ponechá klienta připojeného a zachová fakt, že nové contracts se uplatní až po budoucím odpojení a připojení.
- Dialog se zobrazí až po potvrzeném úspěšném uložení. Selhání nebo neurčitý výsledek uložení nesmí vyvolat odpojení.
- Pokud klient v okamžiku uložení připojený není, zachovat odpovídající současný tok bez zbytečné výzvy k odpojení.

## 3. Okno Server Information

### 3.1 Barvy řádků

- U validního online klienta změnit zelené pozadí řádku pouze v okně Server Information na o něco sytější a méně světlou zelenou než současná `successBackground` barva `#F0FAF2`.
- Nový odstín má zůstat vizuálně umírněný, zapadat do současné palety a zachovat dobrou čitelnost textu a stavů výběru.
- Žluté pozadí příslušného řádku v Server Information musí přesně používat stejnou paletovou barvu jako změněný řádek v Communication Contracts, tedy existující roli `contractsChanged` s hodnotou `#FFF2B3`, nikoli současnou světlejší `warningBackground`.
- Změna zelené je omezena na řádky Server Information. Nemá bez ověření globálně ztmavit úspěšné stavy v ostatních oknech.

### 3.2 Formátování Raw Server Information

- Na záložce `Raw server information` zobrazit serverový JSON čitelně s odsazením a řádkováním.
- Preferovat klientské formátování z již ověřených JSON dat, aby kvůli čistě prezentační změně nebylo nutné měnit wire formát ani server.
- Použít existující projektový JSON parser a serializaci, pokud zachová všechny hodnoty a jejich význam. Nezavádět ad hoc textové formátování.
- Zachovat read-only charakter pole, monospace font, možnost výběru a kopírování, scrollování a stávající zachování výběru a viditelné pozice při refreshi, pokud je to možné.
- Pokud by bezpečné formátování konkrétního ověřeného payloadu selhalo, nesmí se informace ztratit. Zobrazit původní ověřený obsah nebo jasný neblokující stav místo prázdného pole či pádu.
- Serverový skript kvůli samotnému odsazení JSON neměnit.

### 3.3 Tlačítko OK

- Tlačítko `OK` ve spodní části okna Server Information zobrazit modře jako primární akci.
- Zachovat jeho současnou funkci zavření okna, klávesovou obsluhu, focus a přístupnost.
- Ostatní tlačítka neměnit nad rámec toho, co vyžaduje konzistentní a ověřené rozložení nového Auto refresh prvku.

### 3.4 Auto refresh

- Vedle tlačítka `Refresh`, napravo od něj a na stejné vodorovné úrovni, přidat přepínač ve stylu posuvného checkboxu s viditelným názvem `Auto refresh`.
- Při každém novém otevření nebo zobrazení okna Server Information musí být Auto refresh vypnutý.
- Uživatel musí Auto refresh při každém otevření ručně zapnout.
- Stav existuje pouze v paměti po dobu života právě zobrazeného okna. Nesmí se zapisovat do konfigurace, registru, souboru ani jiné perzistentní vrstvy a nesmí se přenést do dalšího otevření okna.
- Po zapnutí spustit refresh každých pět sekund.
- Automatický refresh musí používat stejnou ověřenou cestu a mít stejný uživatelský výsledek jako ruční stisk tlačítka `Refresh`.
- Auto refresh funguje bez ohledu na zvolenou záložku `Who is online` nebo `Raw server information`.
- Nesmí spustit nový požadavek, dokud předchozí refresh stále probíhá. Po dokončení se pokračuje dalším řádným pětisekundovým intervalem bez hromadění zmeškaných požadavků.
- Ruční Refresh musí zůstat dostupný podle stávajících pravidel a nesmí vytvořit souběžný požadavek.
- Vypnutí přepínače okamžitě zastaví plánování dalších automatických refreshů. Zavření nebo zničení okna musí timer bezpečně ukončit a zahodit jeho in-memory stav.
- Selhání automatického refreshu má použít stávající chování okna, tedy ponechat předchozí informace a zobrazit neblokující informaci o selhání, pokud již model existuje.
- Přepínač musí být ovladatelný myší i klávesnicí, správně zařazený do tab orderu a přístupně pojmenovaný.

### 3.5 Počet validních online klientů

- Zachovat současný význam a strukturu nadpisu `Server information`, včetně počtu validních online klientů v závorce.
- Opravit anglické jednotné a množné číslo:
  - `Server information (1 valid online client)` pro přesně jednoho klienta,
  - `Server information (0 valid online clients)` pro nulu,
  - `Server information (N valid online clients)` pro všechny hodnoty jiné než jedna.
- V obsahovém nadpisu okna zobrazit celý text v závorce zeleně. Základní text `Server information` zůstane ve stávající barvě.
- Systémový titulek okna, kde nelze obarvit pouze část textu, musí alespoň používat stejnou gramaticky správnou textovou hodnotu bez požadavku na částečné obarvení.
- Zelená musí zůstat čitelná a odpovídat současnému stylu úspěšných stavů.

## 4. Testy a ověření

- Doplnit automatické testy polling regrese včetně přesné dvoupollové prodlevy před zahájením backoffu.
- Rozšířit testy Communication Contracts o úspěšné uložení v připojeném stavu, význam dialogu, výchozí volbu `Yes`, odpojení po `Yes`, setrvání po `No` a absenci odpojení při neúspěšném uložení.
- Rozšířit testy Server Information alespoň o:
  - správné barvy validního a žlutě zvýrazněného řádku,
  - čitelně odsazený Raw Server Information JSON,
  - zachování obsahu a stability při selhání formátování nebo refreshu,
  - modrou primární roli tlačítka `OK`,
  - přítomnost, výchozí vypnutý stav, neperzistování, přístupnost a tab order přepínače Auto refresh,
  - pětisekundový interval, oba taby, zákaz překryvu požadavků a zastavení timeru,
  - jednotné číslo pro hodnotu jedna a množné číslo pro nulu i více klientů,
  - zelenou barvu závorkové části obsahového nadpisu.
- Zachovat a spustit relevantní jednotkové, integrační, runtime a UI testy. Neprovedené testy nesmí být vykázány jako úspěšné.
- Pokud je dostupné odpovídající prostředí, prakticky ověřit polling se dvěma klienty nebo virtuálním strojem proti aktuálnímu serveru a ručně zkontrolovat výsledný vzhled v podporovaných DPI a motivech.

## 5. Dokumentace

- Zkontrolovat `client.md`, související UI a runtime dokumentaci a distribuční pokyny.
- Dokumentaci změnit pouze tehdy, pokud je současný popis po realizaci neúplný, nesprávný nebo pokud změna zavádí uživatelsky či provozně významné chování, které projekt dokumentuje.
- Pokud dokumentační změna není nutná, uvést tuto skutečnost v závěrečném reportu.

## 6. Distribuce

- Po úspěšné implementaci a ověření vytvořit novou distribuci z aktuálního zdrojového stavu podle závazné metodiky v `distribution.md`.
- Znovu vytvořit projektový strom `dist`, včetně aktuálního `dist/server/phpvpn.php`, kompletního Windows x64 balíčku, požadovaných platformních adresářů, licencí a podepsaného manifestu.
- Distribuční klientské binárky musí obsahovat všechny změny z tohoto intentu a odpovídat aktuálním zdrojům.
- Ověřit úplnost, architekturu, integritu, podpisové artefakty, instalační a provozní chování předepsané metodikou a shodu distribuovaného serverového skriptu s aktuálním zdrojem.
- Předat přesnou cestu k výsledné distribuci a pravdivý souhrn provedených kontrol.

## Pravděpodobně dotčené oblasti

- `client/src/core/runtime/vpn_engine.cpp` a `.h`
- `client/src/core/runtime/network_worker.cpp` a `.h`
- `client/apps/tray/contracts_window.cpp`
- `client/apps/tray/system_dialog.cpp` a `.h`, pokud současné API nepodporuje požadované Yes/No potvrzení
- `client/apps/tray/server_info_window.cpp` a `.h`
- `client/apps/tray/server_info_layout.h`
- `client/apps/tray/ui_style.h` nebo lokální prezentační role, pokud je potřebný nový zelený odstín
- relevantní testy pod `client/tests/`, případně scénáře pod `tests-integration/`
- `client.md` pouze při prokázané potřebě dokumentační změny
- `distribution.md` a distribuční nástroje pouze tehdy, pokud současný postup brání vytvoření úplné distribuce

## Souhrnná akceptační kritéria

1. Příčina polling závady je doložena kontrolovatelným důkazem.
2. Historické pakety po skončení provozu již neudržují polling trvale na minimálním intervalu.
3. Po jakémkoli odeslaném nebo přijatém VPN paketu zůstanou první dva prázdné polly na minimálním intervalu a třetí prázdný poll zahájí zdvojnásobování.
4. Idle interval dále roste násobkem dva až k serverovému maximu a nový provoz ho vrací na minimum s vynulovaným prázdným čítačem.
5. Po uložení contracts v připojeném stavu nabízí dialog `Yes` jako výchozí volbu a `Yes` klienta skutečně odpojí.
6. Server Information používá požadovanou žlutou, sytější zelenou, modré tlačítko OK a gramaticky správný počet klientů se zeleným textem v závorce.
7. Raw Server Information je čitelně naformátovaný bez změny významu nebo ztráty dat.
8. Auto refresh je při každém otevření vypnutý, existuje pouze v paměti otevřeného okna a po ručním zapnutí obnovuje každých pět sekund na obou tabech bez překrývajících se požadavků.
9. Relevantní automatické testy pokrývají nové chování a procházejí.
10. Praktické ověření je provedeno, pokud je potřebné prostředí dostupné, jinak je omezení výslovně uvedeno.
11. Dokumentace je aktualizována, nebo je doloženo, proč změnu nepotřebovala.
12. Je vytvořená a ověřená aktuální distribuce obsahující všechny změny a aktuální serverový skript.
13. Závěrečný report uvede změněné soubory, důkazy, provedené testy, omezení a přesnou cestu k distribuci.

## Realizační omezení

- Intent sám neautorizuje commit ani push.
- Změny se mají provést přímo v aktivní feature branch projektu.
- Neprovádět nesouvisející změny a zachovat existující protokolové, bezpečnostní, přístupnostní a instalační vlastnosti.
