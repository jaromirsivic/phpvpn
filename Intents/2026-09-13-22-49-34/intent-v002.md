---
intent_version: 2
created_at: 2026-09-13-23-05-55
based_on: intent-v001.md
source: intent-chat-v002.md
---

# Oprava polling backoffu a drobné úpravy klientského UI

## Shrnutí

Zjistit, prokázat a odstranit příčinu chování, při kterém Windows klient po ukončení skutečného síťového provozu nadále volá datový endpoint serveru v minimálním serverem určeném intervalu. Současně upravit následnou akci po uložení Communication Contracts, vzhled a chování okna Server Information, zobrazení Raw Server Information a gramatiku počtu validních online klientů. Změny musí být pokryté přiměřenými testy, podle potřeby promítnuté do dokumentace a zakončené vytvořením aktuální distribuce.

## 1. Polling interval po ukončení provozu

### Pozorovaný scénář

1. Na lokálním počítači běží aktuální serverový skript `D:\Nginx\html\phpvpn.php`, dostupný jako `http://127.0.0.1/phpvpn.php`.
2. Lokální klient se k serveru připojí a spojení se jeví jako funkční.
3. Nejnovější dostupná klientská distribuce se nainstaluje také na jiný počítač, konkrétně na virtuální stroj, a připojí se ke stejnému serveru.
4. Mezi stroji proběhne úspěšný ping.
5. Po skončení pingu už nejsou odesílány ani přijímány další VPN pakety.
6. Přesto klient pokračuje v dotazování serveru přibližně každých 50 ms. Polling interval se při nečinnosti nezdvojnásobuje.

### Projektová fakta a pracovní hypotéza

- Serverem předávané limity jsou v klientovi reprezentovány hodnotami `minDataPollIntervalMs` a `maxDataPollIntervalMs`. Aktuální výchozí hodnoty v kódu jsou 50 ms a 3200 ms.
- `NetworkWorker::nextPollDelayMs(bool hadTraffic)` v `client/src/core/runtime/network_worker.cpp` resetuje interval na minimum při `hadTraffic == true` a při nečinnosti ho násobí dvěma až do maxima.
- `VpnEngine::runNetworkThread()` v `client/src/core/runtime/vpn_engine.cpp` nyní volá tuto funkci s podmínkou `packetsReceived_ > 0 || packetsSent_ > 0`.
- `packetsReceived_` a `packetsSent_` jsou v `runConnectedIteration()` kumulativní čítače za připojení. Po prvním přeneseném paketu proto podmínka zůstává pravdivá i v dalších prázdných iteracích.
- Toto je silná pracovní hypotéza, nikoli náhrada požadovaného ověření. Realizace ji musí reprodukcí nebo cíleným testem potvrdit, případně zdokumentovat jinou skutečnou příčinu.
- Přímé testovací pokrytí volání `nextPollDelayMs` z běhu připojeného klienta nebylo při přípravě intentu nalezeno.

### Požadovaná diagnostika a oprava

- Reprodukovat problém na aktuálním zdrojovém stavu nebo vytvořit deterministický test zachycující provoz v jedné či více iteracích a následné prázdné iterace.
- Prokázat, zda je příčinou použití kumulativních čítačů místo aktivity aktuální datové výměny. Pokud se hypotéza nepotvrdí, pokračovat až k doložené skutečné příčině.
- Rozhodnutí, zda je aktuální iterace aktivní, musí vycházet z paketů skutečně odeslaných nebo přijatých v dané iteraci, nikoli z kumulativních diagnostických čítačů za celé připojení.
- Samotný úspěšný HTTP poll, keepalive, health kontrola ani historický provoz nesmí být považován za aktuální VPN provoz.
- Iterace s alespoň jedním odeslaným nebo přijatým VPN paketem má polling interval vrátit na serverem povolené minimum.
- Každá následující iterace bez skutečného provozu má aplikovat existující násobek idle backoffu dva a respektovat serverem určené minimum a maximum.
- Oprava nesmí porušit zpracování paketů, recovery logiku, časovou synchronizaci, health kontrolu ani diagnostické kumulativní čítače.
- Nasazený `D:\Nginx\html\phpvpn.php` se nemá měnit, pokud diagnostika neprokáže podíl serveru. Případná nutná serverová změna musí vzniknout nejprve v autoritativním zdroji repozitáře.

### Povinné regresní scénáře

- Skutečný provoz resetuje interval na minimum.
- První prázdná iterace po provozu zahájí prodlužování intervalu.
- Další prázdné iterace interval zdvojnásobují až k serverem stanovenému maximu.
- Historický paket už po vyprázdnění provozu neudržuje polling na minimu.
- Nový provoz po období nečinnosti znovu vrátí interval na minimum.
- Test musí selhat proti původnímu chybnému chování a projít po opravě.

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
- Použít existující projektový JSON parser a serializaci, pokud to zachová všechny hodnoty a jejich význam. Nezavádět ad hoc nebo nebezpečné textové formátování.
- Zachovat read-only charakter pole, monospace font, možnost výběru a kopírování, scrollování a stávající zachování výběru a viditelné pozice při refreshi, pokud je to možné.
- Pokud by bezpečné formátování konkrétního ověřeného payloadu selhalo, nesmí se informace ztratit. Zobrazit původní ověřený obsah nebo jasný neblokující stav místo prázdného pole či pádu.
- Serverový skript kvůli samotnému odsazení JSON neměnit.

### 3.3 Tlačítko OK

- Tlačítko `OK` ve spodní části okna Server Information zobrazit modře jako primární akci.
- Zachovat jeho současnou funkci zavření okna, klávesovou obsluhu, focus a přístupnost.
- Ostatní tlačítka neměnit nad rámec toho, co vyžaduje konzistentní a ověřené rozložení nového Auto refresh prvku.

### 3.4 Auto refresh

- Vedle tlačítka `Refresh`, napravo od něj a na stejné vodorovné úrovni, přidat přepínač ve stylu posuvného checkboxu s viditelným názvem `Auto refresh`.
- Při každém novém otevření nebo zobrazení okna Server Information musí být Auto refresh vypnutý. Stav se mezi otevřeními nepamatuje.
- Uživatel musí Auto refresh při každém otevření ručně zapnout.
- Po zapnutí spustit refresh každých pět sekund.
- Automatický refresh musí používat stejnou ověřenou cestu a mít stejný uživatelský výsledek jako ruční stisk tlačítka `Refresh`.
- Auto refresh funguje bez ohledu na zvolenou záložku `Who is online` nebo `Raw server information`.
- Nesmí spustit nový požadavek, dokud předchozí refresh stále probíhá. Po dokončení se pokračuje dalším řádným pětisekundovým intervalem bez hromadění zmeškaných požadavků.
- Ruční Refresh musí zůstat dostupný podle stávajících pravidel a nesmí vytvořit souběžný požadavek.
- Vypnutí přepínače okamžitě zastaví plánování dalších automatických refreshů. Zavření nebo zničení okna musí timer bezpečně ukončit.
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

- Doplnit automatické testy polling regrese popsané v oddílu 1.
- Rozšířit testy Communication Contracts o úspěšné uložení v připojeném stavu, přesný význam dialogu, výchozí volbu `Yes`, odpojení po `Yes`, setrvání po `No` a absenci dialogu při neúspěšném uložení.
- Rozšířit testy Server Information alespoň o:
  - správné barvy validního a žlutě zvýrazněného řádku,
  - čitelně odsazený Raw Server Information JSON,
  - zachování obsahu a stability při selhání formátování nebo refreshu,
  - modrou primární roli tlačítka `OK`,
  - přítomnost, výchozí vypnutý stav, přístupnost a tab order přepínače Auto refresh,
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

- `client/src/core/runtime/vpn_engine.cpp`
- `client/src/core/runtime/vpn_engine.h`
- `client/src/core/runtime/network_worker.cpp`
- `client/src/core/runtime/network_worker.h`
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
2. Historické pakety po skončení provozu již neudržují polling na minimálním intervalu.
3. Idle interval se zdvojnásobuje až k serverovému maximu a nový provoz ho vrací na minimum.
4. Po uložení contracts v připojeném stavu nabízí dialog `Yes` jako výchozí volbu a `Yes` klienta skutečně odpojí.
5. Server Information používá požadovanou žlutou, sytější zelenou, modré tlačítko OK a gramaticky správný počet klientů se zeleným textem v závorce.
6. Raw Server Information je čitelně naformátovaný bez změny významu nebo ztráty dat.
7. Auto refresh je při každém otevření vypnutý, po ručním zapnutí obnovuje každých pět sekund na obou tabech a nevytváří překrývající se požadavky.
8. Relevantní automatické testy pokrývají nové chování a procházejí.
9. Praktické ověření je provedeno, pokud je potřebné prostředí dostupné, jinak je omezení výslovně uvedeno.
10. Dokumentace je aktualizována, nebo je doloženo, proč změnu nepotřebovala.
11. Je vytvořená a ověřená aktuální distribuce obsahující všechny změny a aktuální serverový skript.
12. Závěrečný report uvede změněné soubory, důkazy, provedené testy, omezení a přesnou cestu k distribuci.

## Realizační omezení

- Intent sám neautorizuje commit ani push.
- Změny se mají provést přímo v aktivní feature branch projektu.
- Neprovádět nesouvisející změny a zachovat existující protokolové, bezpečnostní, přístupnostní a instalační vlastnosti.
