# Metodika tvorby distribuce PHP-VPN

## Účel a závaznost

Tento dokument je autoritativní a závaznou metodikou sestavení distribuce PHP-VPN. Určuje okamžik sestavení, zdroje, adresářovou strukturu, obsah a ověření výsledku. Klíčová slova **MUSÍ**, **NESMÍ**, **MĚL BY**, **NEMĚL BY** a **SMÍ** mají stejný normativní význam jako v [protocol.md](protocol.md).

Pravidla tvorby distribučního stromu se řídí tímto dokumentem. [protocol.md](protocol.md) zůstává autoritativním zdrojem drátového protokolu a serverového chování. [client.md](client.md) zůstává autoritativním zdrojem chování klienta, instalace, konfigurace, platformní integrace a bezpečnostních požadavků. Metodika tato pravidla nemění ani neoslabuje. Starší popisy ploché složky `dist` a výchozí chování nástrojů NESMĚJÍ nahradit zde stanovenou strukturu.

## Kdy distribuci sestavit

Distribuce se sestavuje typicky po dokončení implementačních úprav určených k vydání nebo na výslovný pokyn uživatele. Sestavení MUSÍ vycházet z aktuálního stavu zdrojů určeného k předání, včetně zamýšlených dosud necommitnutých změn. Vytvoření commitu ani push nejsou podmínkou sestavení a samotné sestavení je neprovádí.

Při každém sestavení se obsah projektového adresáře `dist` MUSÍ znovu vytvořit z aktuálních zdrojů. Pouhé překopírování nových souborů přes starou distribuci nestačí, protože by ponechalo odstraněné nebo přejmenované součásti. Výsledný strom NESMÍ obsahovat soubory ze staršího sestavení. Uchovávané starší balíčky, zálohy a pracovní podklady patří mimo `dist`.

Před obnovením obsahu se MUSÍ ověřit, že cílem je skutečně adresář `dist` pod kořenem vybraného projektu. Sestavení SMÍ připravit nový strom v odděleném pracovním adresáři a poté jím nahradit obsah `dist`. Neúplný nebo neověřený výsledek NESMÍ být označen za hotovou distribuci.

## Povinná struktura

Cesty jsou relativní ke kořeni projektu. Velikost písmen platformních složek a označení architektury `x64` jsou závazné.

```text
dist/
  server/
    PHP/
      phpvpn.php
    Node.js/
    Python/
      phpvpn.py
      requirements-server.txt
      server-python.md
      init.bat
      start.bat
      stop.bat
      init.sh
      start.sh
      stop.sh
      bootstrap.ps1
      server-control.py
  client/
    Windows/
      x64/
        install.bat
        install.ps1
        uninstall.bat
        uninstall.ps1
        install-phpvpn.exe
        uninstall-phpvpn.exe
        phpvpn-service.exe
        phpvpn-tray.exe
        phpvpn.exe
        wintun.dll
        release-manifest.json
        release-manifest.sig
        release-signing.cer
        LICENSE-wintun.txt
        LICENSE-material-symbols.txt
        NOTICE-material-symbols.txt
    macOS/
    Linux/
```

Uvedené Windows soubory tvoří základ aktuální klientské distribuce. Seznam se MUSÍ doplnit o všechny další skripty, programy, knihovny, distribuovatelné konfigurace, šablony a podpůrné soubory, které aktuální implementace potřebuje pro instalaci a provoz. Testovací binárky, build cache, pracovní logy a obsah provozních front do distribuce nepatří.

### Server

`dist/server/PHP/phpvpn.php` MUSÍ obsahovat nejaktuálnější distribuovatelnou verzi serveru v PHP. Aktuální zdrojový soubor v repozitáři se jmenuje [phpvpn.php](phpvpn.php). Při sestavení se jeho aktuální obsah zkopíruje do složky `dist/server/PHP` beze změny názvu jako `phpvpn.php`. Jeho obsah se při kopírování nemění.

`dist/server/Python` MUSÍ obsahovat aktuální zdrojový `phpvpn.py`, zamčené runtime závislosti `requirements-server.txt`, provozní návod `server-python.md`, šest vstupů `init.bat`, `start.bat`, `stop.bat`, `init.sh`, `start.sh`, `stop.sh` a jejich helpery `bootstrap.ps1`, `server-control.py`. Vše se kopíruje beze změny. Zdrojem osmi provozních skriptů je `tools/server-python`. Python server je samostatně spustitelný podle tohoto návodu a implementuje stejné klientské zprávy jako PHP. Výchozí diskový a volitelný paměťový režim vyžadují jediný aplikační proces a worker. Distribuce NESMÍ obsahovat `.venv`, `.tools`, `.runtime`, `.bootstrap.lock`, uv cache, bytecode, logy, testovací konfiguraci, provozní fronty ani místní tajemství. Zdrojový `requirements-server.in` slouží k obnově locku ve vývojovém repozitáři, při nasazení se používá přibalený `.txt`.

Při každém sestavení MUSÍ vzniknout také složka `dist/server/Node.js`. Zůstává prázdná, tato serverová implementace zatím není dostupná.

Název distribučního souboru `phpvpn.php` odpovídá příkladům `/phpvpn.php` v protokolové dokumentaci. `serverUrl` klienta musí ukazovat na skutečnou URL tohoto souboru. Podpora jiného názvu skriptu a cesty se řídí [client.md](client.md). Distribuce NESMÍ přebírat starší nasazenou kopii nebo lokální provozní konfiguraci namísto aktuálního zdroje určeného k vydání.

### Windows x64

Grafické vstupy `install-phpvpn.exe` a `uninstall-phpvpn.exe` spouštějí odpovídající PowerShell skript ze stejné složky a vyžadují administrátorská práva přes vlastní UAC manifest. Zobrazují živý výstup, jantarový průběh a zelený nebo červený výsledek podle návratového kódu. Okno po dokončení zůstává otevřené. Během operace je běžné zavření blokované. Odmítnutí UAC nastane před spuštěním GUI, proto nemůže zobrazit červený stav v okně.

Oba launchery spouštěj z rozbalené distribuce mimo `%ProgramFiles%\phpvpn`. Spuštění kopie z instalované generace může při upgrade nebo odinstalaci zamknout vlastní EXE a operace pak může skončit chybou. Launcher neprovádí self-delete ani obcházení tohoto zámku. Standardní grafická odinstalace nepředává `-RemoveData`, konfigurace a logy tedy zůstávají zachované. Původní BAT a PowerShell vstupy včetně `-VerifyOnly` zůstávají dostupné.

`dist/client/Windows/x64` MUSÍ být úplnou samostatně předatelnou klientskou distribucí pro Windows x64. Binární soubory se MUSÍ sestavit z aktuálních klientských zdrojů a všechny architekturně závislé součásti MUSÍ odpovídat x64. `install.bat` je vstupní bod instalace, `install.ps1` a všechny další jím volané součásti MUSÍ být přítomné a dostupné přes správné relativní cesty uvnitř balíčku. Totéž platí pro odinstalační skripty.

Instalace a upgrade MUSÍ zachovat lokální read-only přístup podle `client.md`. Oprávnění Users Read & Execute MUSÍ dědit na celý datový strom včetně záloh, interních souborů a logů. Aktualizovaná služba při startu opraví ACL existujícího stromu bez ztráty konfigurace. Ruční ověření zahrnuje otevření složky a kopírování konfigurace bez UAC, zákaz přímého zápisu a následné uložení přes Edit Config a Wizard.

Úplnost zahrnuje potřebné konfigurační podklady, ale nemění pravidla vytváření `config.json` podle [client.md](client.md). Pokud je výchozí konfigurace součástí programu a žádná externí šablona není potřebná, samostatný provozní `config.json` se do balíčku nepřidává. Lokální uživatelská konfigurace a soukromá tajemství se do obecné distribuce nekopírují.

Požadavky na originální nepřejmenovaný `wintun.dll`, podpisy, architekturu, licence, manifest, instalaci a zákaz dodávání samostatných Wintun `.sys`, `.cat` a `.inf` stanoví [client.md](client.md). Manifest a jeho podpis MUSÍ odpovídat konečnému obsahu složky `x64`. Instalační postup NESMÍ stahovat nebo přijímat neověřenou náhradu některé součásti balíčku.

### macOS a Linux

Adresáře `dist/client/macOS` a `dist/client/Linux` MUSÍ vzniknout při každém sestavení, i když pro ně zatím není dostupná klientská implementace. Jejich obsah MUSÍ odpovídat skutečně dostupné implementaci a možnostem jejího ověření. Existující funkční implementace se předává se všemi součástmi potřebnými pro instalaci a provoz na dané platformě.

V aktuální etapě je implementovaným cílem Windows x64. Pokud pro macOS nebo Linux nejsou distribuovatelné součásti, příslušná složka MUSÍ obsahovat stručný `README.md`, který tento stav výslovně uvede. Tak zůstane základní platformní struktura zachována i při archivaci. Přítomnost složky sama o sobě není tvrzením, že je platforma podporovaná nebo otestovaná.

## Postup sestavení a ověření

1. Určit aktuální zdrojový stav určený k vydání a provést kontroly odpovídající změnám. Zaznamenat použité zdroje, verzi, platformu a výsledky ověření. Neprovedené testy se NESMĚJÍ vykazovat jako úspěšné.
2. Sestavit klientské programy z těchto zdrojů a připravit aktuální PHP a Python serverové soubory, Python lock a návod. Pracovní build adresáře zůstávají mimo výslednou distribuci.
3. Znovu vytvořit obsah `dist` a celou povinnou strukturu. Naplnit serverovou část a platformní klientské složky podle skutečně dostupné implementace.
4. Připravit úplný Windows x64 balíček, ověřit architekturu a závislosti, zahrnout licence a po dokončení obsahu vytvořit odpovídající podepsaný manifest. Úroveň důvěry podpisu se uvede pravdivě, vývojový podpis se NESMÍ vydávat za veřejně důvěryhodný vydavatelský podpis.
5. Ověřit hashovou shodu PHP souboru a všech jedenácti souborů Python části s aktuálními zdroji, přítomnost prázdné složky `dist/server/Node.js`, všechny požadované cesty, úplnost souborů, relativní odkazy skriptů a integritu klientského balíčku. Python část ověřit čistým prostředím mimo `dist`, inicializací distribuovanými skripty a spuštěním distribuovaného serveru. Ověřit health, info a data v obou režimech pomocí izolovaných konfiguračních kopií. Skutečné instalační, provozní a další testy předepsané v [client.md](client.md) se při vytváření distribuce provedou, pokud uživatel výslovně neurčí, že se provádět nemají. Při takovém výslovném pokynu se tyto testy neprovedou.
6. Předat přesnou cestu k výsledku a stručný stav ověření. Pokud se vytváří ZIP, jeho obsah MUSÍ odpovídat ověřenému výsledku a zachovat příslušnou adresářovou strukturu. Archiv a pracovní reporty se ukládají mimo `dist`, aby se při dalším sestavení nemísily s jeho obsahem.

Kanonický vstup pro **celou distribuci** je [tools/make-distribution.ps1](tools/make-distribution.ps1). Vyžaduje explicitní existující CMake build z tohoto checkoutu. Vždy znovu spustí sestavení klienta, takže nebalí pouze nalezené staré binárky. Příklad z kořene repozitáře:

```powershell
./client/tools/build-x64.ps1 -Preset release -Configure -ResourceTests
./tools/make-distribution.ps1 -BuildDirectory ./work/build/release -SignBinaries
./tools/verify-distribution.ps1 -DistributionDirectory ./dist
```

Volitelný `-CertificateThumbprint` se předává klientskému packageru. Výchozí certifikát je vývojový, není veřejně důvěryhodným vydavatelským podpisem. Soukromý klíč se neexportuje. Klientský manifest pokrývá pouze obsah `client/Windows/x64`.

Celkový sestavovací vstup vytvoří nový staging v `work/distribution-<id>`, kontroluje přesný seznam souborů včetně skrytých, hashe, architekturu, podpis a ikony. Vytvoří ZIP, zachová i prázdnou Node.js složku a POSIX executable atribut shellových skriptů, rozbalí archiv do nového adresáře a znovu spustí ověření. Teprve potom publikuje `dist` a `dist.zip`. Předchozí strom ponechá v pracovním adresáři pro obnovu. Při chybě ověření stávající distribuci nemění. Rekurzivní přesuny odmítají reparse pointy a cíle mimo vymezené adresáře. Cesta k rozbalené ověřené kopii se vypíše pro následné izolované provozní testy.

Dílčí [client/tools/make-dist.ps1](client/tools/make-dist.ps1) vyžaduje `-OutputDirectory <cesta>` pod `work` nebo `dist`. Kořenový `dist` není povoleným cílem tohoto dílčího nástroje. Serverovou část a ostatní platformní složky vytváří výhradně celkový vstup.

Po sestavení spusť v odděleném vývojovém venv `python -B tools/test-server-scripts.py --distribution <rozbaleny-strom> --report <report.json>`. Runner vytváří vlastní kopie, porty a buffery, kontroluje Windows BAT nebo nativní POSIX SH vstupy, health/info/data, identitu, souběh, chyby, URL a nejméně třísekundové bannery. `tests-python/test_server_control.py` doplňuje deterministické případy recyklovaného PID, nedostupných oprávnění a timeoutu. Bootstrap bez uv a Pythonu se ověřuje zvlášť v čistém prostředí. Windows běh nenahrazuje Linux ani macOS, neprovedené platformy a ruční instalační testy se uvádějí v datovaném reportu.

Windows balíček se ověřuje také pomocí `client/tools/verify-package.ps1 -PackageDirectory <cesta>`. Tento read-only krok kontroluje shodu skriptů, povinnou přítomnost pěti EXE, manifest, podpisy, VerifyOnly a přesné vložené ICO resources všech pěti produkčních EXE včetně úrovní UAC. Vyžaduje sestavený `phpvpn-icon-resource-tests`, standardně z presetu `release` s `BUILD_TESTING=ON`. Jiný ověřovací build lze zadat přes `-IconResourceTest`. Čtyři zdrojové ICO se používají pouze při sestavení a ověřování, do provozního balíčku se samostatně nepřidávají.

Status stránka je vložená přímo v `phpvpn.php` i `phpvpn.py`. Ani jedna serverová distribuce nepotřebuje `assets/index.html`. PHP a Python nepoužívají vzájemně čitelný diskový formát, výměna implementace nad rozpracovanými frontami není podporovaná migrace.
