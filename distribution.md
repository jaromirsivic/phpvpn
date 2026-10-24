# Metodika tvorby distribuce PHP-VPN

## Účel a závaznost

Tento dokument je autoritativní a závaznou metodikou sestavení distribuce PHP-VPN. Určuje okamžik sestavení, zdroje, adresářovou strukturu, obsah a ověření výsledku. Klíčová slova **MUSÍ**, **NESMÍ**, **MĚL BY**, **NEMĚL BY** a **SMÍ** mají stejný normativní význam jako v [protocol.md](protocol.md).

Pravidla tvorby distribučního stromu se řídí tímto dokumentem. [protocol.md](protocol.md) zůstává autoritativním zdrojem drátového protokolu a serverového chování. [client.md](client.md) zůstává autoritativním zdrojem chování klienta, instalace, konfigurace, platformní integrace a bezpečnostních požadavků. Metodika tato pravidla nemění ani neoslabuje. Starší popisy ploché složky `dist` a výchozí chování nástrojů NESMĚJÍ nahradit zde stanovenou strukturu.

## Kdy distribuci sestavit

Distribuce se sestavuje typicky po dokončení implementačních úprav určených k vydání nebo na výslovný pokyn uživatele. Sestavení MUSÍ vycházet z aktuálního stavu zdrojů určeného k předání, včetně zamýšlených dosud necommitnutých změn. Vytvoření commitu ani push nejsou podmínkou sestavení a samotné sestavení je neprovádí.

Při každém sestavení se obsah projektového adresáře `dist` MUSÍ znovu vytvořit z aktuálních zdrojů. Pouhé překopírování nových souborů přes starou distribuci nestačí, protože by ponechalo odstraněné nebo přejmenované součásti. Výsledný strom NESMÍ obsahovat neplánované soubory ze staršího sestavení. Výslovnou výjimkou z aktuálnosti dokumentace je uchovaný anglický překlad protokolu podle oddílu Dokumentace. Uchovávané starší balíčky, zálohy a pracovní podklady patří mimo `dist`.

Před obnovením obsahu se MUSÍ ověřit, že cílem je skutečně adresář `dist` pod kořenem vybraného projektu. Sestavení SMÍ připravit nový strom v odděleném pracovním adresáři a poté jím nahradit obsah `dist`. Neúplný nebo neověřený výsledek NESMÍ být označen za hotovou distribuci.

## Povinná struktura

Cesty jsou relativní ke kořeni projektu. Velikost písmen platformních složek a označení architektury `x64` jsou závazné.

```text
dist/
  documentation/
    readme.txt
    protocol.md                 # volitelný úplný anglický překlad
  server/
    PHP/
      phpvpn.php
    Node.js/
      phpvpn.js
      server-node.md
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
    macOS/
      README.md
    Linux/
      x64/
        install.sh
        uninstall.sh
        README.md
        phpvpn-<verze>-<release>.x86_64.rpm
        phpvpn-tray-<verze>-<release>.x86_64.rpm
        phpvpn_<verze>-<release>_amd64.deb
        phpvpn-tray_<verze>-<release>_amd64.deb
        release-manifest.json
        release-manifest.sig
        release-signing.cer
```

`<verze>` je verze klienta z `client/CMakeLists.txt`, `<release>` je společné číslo release všech čtyř linuxových balíčků.

Uvedené Windows a Linux soubory tvoří základ aktuální klientské distribuce. Seznam se MUSÍ doplnit o všechny další skripty, programy, knihovny, distribuovatelné konfigurace, šablony a podpůrné soubory, které aktuální implementace potřebuje pro instalaci a provoz. Testovací binárky, build cache, pracovní logy a obsah provozních front do distribuce nepatří.

### Dokumentace

Distribuce MUSÍ mít tři hlavní složky `client`, `server` a `documentation`.
Kořenový [readme.txt](readme.txt), vedle `protocol.md` a `client.md`, je udržovaný zdroj stručného anglického instalačního návodu. Při KAŽDÉM sestavení se MUSÍ beze změny zkopírovat do `dist/documentation/readme.txt`. README je povinné, i když překlad protokolu chybí. Uvádí klientskou a serverovou část, skutečné postupy pro Windows a Linux, připravovaný macOS klient a nasazení PHP, Node.js a Python serveru.

Český [protocol.md](protocol.md) zůstává autoritativním zdrojem a NESMÍ se překladem přepsat. Úplný anglický překlad se udržuje mimo distribuci jako `documentation/protocol.en.md`. Aktualizovat jej podle celého aktuálního českého dokumentu se SMÍ pouze na výslovnou žádost uživatele o aktualizaci dokumentace při tvorbě distribuce. Nestačí shrnutí ani překlad změněných částí bez kontroly celého dokumentu. Překlad zachová normativní význam, identifikátory, vzorce, příklady a všechny kapitoly. Vedle něj se uchovává `documentation/protocol.en.source.sha256` s SHA-256 českého zdroje použitého pro poslední překlad. Tento záznam nepatří do distribuce a nevyžaduje se jeho shoda s budoucím českým zdrojem bez žádosti o aktualizaci.

Bez takové žádosti se nový překlad NESMÍ vytvářet ani automaticky aktualizovat. Sestavovací skript pouze zkopíruje existující `documentation/protocol.en.md` do `dist/documentation/protocol.md`. Není-li uchovaný zdroj dostupný, překlad vynechá. Již existující anglický protokol z předchozí distribuce lze nejprve uchovat na této zdrojové cestě beze změny, bez tvrzení o jeho aktuálnosti. Přepínač `-OmitProtocol` dovoluje překlad vynechat i při existujícím zdroji. Kontrola distribuce dovoluje jeho nepřítomnost, ale přítomný překlad MUSÍ bajtově odpovídat uchovanému anglickému zdroji. README se vždy ověřuje proti aktuálnímu kořenovému zdroji. Nový překlad pro aktuální sestavení se připraví před spuštěním sestavovacího skriptu. Relativní odkazy na vývojovou dokumentaci, například `client.md`, vyžadují soubory ze zdrojového repozitáře a v samostatné distribuci nejsou dostupné. Tato dokumentace se tím nestává povinnou součástí distribuce.

### Server

`dist/server/PHP/phpvpn.php` MUSÍ obsahovat nejaktuálnější distribuovatelnou verzi serveru v PHP. Aktuální zdrojový soubor v repozitáři se jmenuje [phpvpn.php](phpvpn.php). Při sestavení se jeho aktuální obsah zkopíruje do složky `dist/server/PHP` beze změny názvu jako `phpvpn.php`. Jeho obsah se při kopírování nemění.

`dist/server/Python` MUSÍ obsahovat aktuální zdrojový `phpvpn.py`, zamčené runtime závislosti `requirements-server.txt`, provozní návod `server-python.md`, šest vstupů `init.bat`, `start.bat`, `stop.bat`, `init.sh`, `start.sh`, `stop.sh` a jejich helpery `bootstrap.ps1`, `server-control.py`. Vše se kopíruje beze změny. Zdrojem osmi provozních skriptů je `tools/server-python`. Python server je samostatně spustitelný podle tohoto návodu a implementuje stejné klientské zprávy jako PHP. Výchozí diskový a volitelný paměťový režim vyžadují jediný aplikační proces a worker. Distribuce NESMÍ obsahovat `.venv`, `.tools`, `.runtime`, `.bootstrap.lock`, uv cache, bytecode, logy, testovací konfiguraci, provozní fronty ani místní tajemství. Zdrojový `requirements-server.in` slouží k obnově locku ve vývojovém repozitáři, při nasazení se používá přibalený `.txt`.

`dist/server/Node.js` MUSÍ obsahovat aktuální zdrojový [phpvpn.js](phpvpn.js) a provozní návod [server-node.md](server-node.md), oba kopírované beze změny. Node.js server je jediný soubor bez npm závislostí, build kroku a assets a implementuje stejné klientské zprávy jako PHP. Distribuce NESMÍ obsahovat `node_modules`, konfigurační JSON s tajemstvími, logy ani provozní frontu `phpvpn_data`. Server spuštěný přímo z `dist` si výchozí `phpvpn_data` vytvoří vedle skriptu. Při publikaci se tento lokální stav stejně jako `.venv` a `.runtime` Python části nepřesouvá, nepublikuje ani nemaže.

Název distribučního souboru `phpvpn.php` odpovídá příkladům `/phpvpn.php` v protokolové dokumentaci. `serverUrl` klienta musí ukazovat na skutečnou URL tohoto souboru. Podpora jiného názvu skriptu a cesty se řídí [client.md](client.md). Distribuce NESMÍ přebírat starší nasazenou kopii nebo lokální provozní konfiguraci namísto aktuálního zdroje určeného k vydání.

### Windows x64

Grafické vstupy `install-phpvpn.exe` a `uninstall-phpvpn.exe` spouštějí odpovídající PowerShell skript ze stejné složky a vyžadují administrátorská práva přes vlastní UAC manifest. Zobrazují živý výstup, jantarový průběh a zelený nebo červený výsledek podle návratového kódu. Okno po dokončení zůstává otevřené. Během operace je běžné zavření blokované. Odmítnutí UAC nastane před spuštěním GUI, proto nemůže zobrazit červený stav v okně.

Oba launchery spouštěj z rozbalené distribuce mimo `%ProgramFiles%\phpvpn`. Spuštění kopie z instalované generace může při upgrade nebo odinstalaci zamknout vlastní EXE a operace pak může skončit chybou. Launcher neprovádí self-delete ani obcházení tohoto zámku. Standardní grafická odinstalace nepředává `-RemoveData`, konfigurace a logy tedy zůstávají zachované. Původní BAT a PowerShell vstupy včetně `-VerifyOnly` zůstávají dostupné.

`dist/client/Windows/x64` MUSÍ být úplnou samostatně předatelnou klientskou distribucí pro Windows x64. Binární soubory se MUSÍ sestavit z aktuálních klientských zdrojů a všechny architekturně závislé součásti MUSÍ odpovídat x64. `install.bat` je vstupní bod instalace, `install.ps1` a všechny další jím volané součásti MUSÍ být přítomné a dostupné přes správné relativní cesty uvnitř balíčku. Totéž platí pro odinstalační skripty.

Instalace a upgrade MUSÍ zachovat lokální read-only přístup podle `client.md`. Oprávnění Users Read & Execute MUSÍ dědit na celý datový strom včetně záloh, interních souborů a logů. Aktualizovaná služba při startu opraví ACL existujícího stromu bez ztráty konfigurace. Ruční ověření zahrnuje otevření složky a kopírování konfigurace bez UAC, zákaz přímého zápisu a následné uložení přes Edit Config a Wizard.

Úplnost zahrnuje potřebné konfigurační podklady, ale nemění pravidla vytváření `config.json` podle [client.md](client.md). Pokud je výchozí konfigurace součástí programu a žádná externí šablona není potřebná, samostatný provozní `config.json` se do balíčku nepřidává. Lokální uživatelská konfigurace a soukromá tajemství se do obecné distribuce nekopírují.

Požadavky na originální nepřejmenovaný `wintun.dll`, podpisy, architekturu, licence, manifest, instalaci a zákaz dodávání samostatných Wintun `.sys`, `.cat` a `.inf` stanoví [client.md](client.md). Manifest a jeho podpis MUSÍ odpovídat konečnému obsahu složky `x64`. Instalační postup NESMÍ stahovat nebo přijímat neověřenou náhradu některé součásti balíčku.

### Linux x64

`dist/client/Linux/x64` MUSÍ být úplnou samostatně předatelnou klientskou distribucí pro Fedoru a Ubuntu na x86_64. Obsahuje balíčky `phpvpn` (služba a CLI, bez grafických závislostí) a `phpvpn-tray` (tray aplikace) ve formátu RPM, sestavené na Fedoře 44, a DEB, sestavené na Ubuntu 24.04. Dále instalační vstupy `install.sh` a `uninstall.sh`, návod `README.md` a podepsaný manifest. Ikony tray jsou vlastní grafika PHPVPN pod licencí projektu, žádná licence ikon třetí strany se nepřibaluje. Chování balíčků, instalace, služby a tray stanoví [client.md](client.md) a přibalený `README.md`.

Linuxové balíčky se NESMĚJÍ sestavovat na Windows hostiteli ani z jiného zdrojového stavu než Windows klient. Sestavují se na linuxovém build hostiteli (podman nebo docker) nástrojem `client/tools/linux/build-packages-in-container.sh` v čistém kontejneru `fedora:44` a `ubuntu:24.04` ze stejného commitu, ze kterého se sestavuje Windows klient. Oba běhy zapisují do jedné výstupní složky `work/linux-packages/<id>` s balíčky, `SHA256SUMS` a `build-provenance.json` (commit, příznak `sourceDirty`, distribuce, image, kompilátor a verze knihoven). Tato složka se beze změny přenese do `work/linux-packages/<id>` checkoutu na Windows hostiteli a předá se celkovému vstupu parametrem `-LinuxPackagesDirectory`.

Celkový vstup před sestavením Windows klienta ověří `SHA256SUMS` všech souborů, přítomnost právě jednoho balíčku každého ze čtyř druhů a provenance. `sourceCommit` MUSÍ odpovídat `HEAD` checkoutu, `sourceDirty` MUSÍ být `false` a checkout NESMÍ mít necommitnuté změny pod `client/`, protože je balíčky nemohou obsahovat. Linuxová část vydání tedy vyžaduje commit, výjimka pro necommitnuté změny z oddílu Kdy distribuci sestavit se na ni nevztahuje. Při neshodě sestavení odmítne. Přepínač `-AllowLinuxProvenanceMismatch` je určen výhradně pro vývoj: neshodu vypíše jako varování, zaznamená do `work/distribution-<id>/linux-provenance-mismatch.txt` a podepsaný manifest nese skutečný `sourceCommit` a `sourceDirty`. Takový výsledek NESMÍ být předán jako vydání.

Složku skládá `client/tools/linux/make-linux-dist.ps1`. Manifest `release-manifest.json` má stejný kanonický tvar a podpis `release-manifest.sig` stejný mechanismus i certifikát `release-signing.cer` jako Windows balíček. Navíc uvádí `platform`, `packageRelease`, minimální verze distribucí, `sourceCommit` a `sourceDirty`. Pokrývá pouze obsah `client/Linux/x64`. `install.sh` pinuje SHA-256 veřejného klíče certifikátu, změna podpisového certifikátu proto vyžaduje současnou úpravu pinu v `install.sh`. `install.sh` a `uninstall.sh` jsou POSIX shell s LF bez BOM a v ZIP nesou POSIX executable atribut. Návod uvádí i spuštění `sh install.sh`, které na atributu nezávisí. Instalační postup NESMÍ stahovat nebo přijímat neověřenou náhradu některé součásti složky, závislosti balíčků pocházejí z repozitářů distribuce.

### macOS

Adresář `dist/client/macOS` MUSÍ vzniknout při každém sestavení, i když pro něj zatím není dostupná klientská implementace. Jeho obsah MUSÍ odpovídat skutečně dostupné implementaci a možnostem jejího ověření. Existující funkční implementace se předává se všemi součástmi potřebnými pro instalaci a provoz na dané platformě.

V aktuální etapě jsou implementovanými cíli Windows x64 a Linux x64. Pro macOS nejsou distribuovatelné součásti, složka proto MUSÍ obsahovat stručný `README.md`, který tento stav výslovně uvede. Tak zůstane základní platformní struktura zachována i při archivaci. Přítomnost složky sama o sobě není tvrzením, že je platforma podporovaná nebo otestovaná.

## Postup sestavení a ověření

1. Určit aktuální zdrojový stav určený k vydání a provést kontroly odpovídající změnám. Zaznamenat použité zdroje, verzi, platformu a výsledky ověření. Neprovedené testy se NESMĚJÍ vykazovat jako úspěšné.
2. Sestavit klientské programy z těchto zdrojů, na linuxovém build hostiteli z téhož commitu linuxové balíčky, a připravit aktuální PHP, Python a Node.js serverové soubory, Python lock a návody. Připravit kořenový anglický `readme.txt`. Jen při výslovné žádosti o aktualizaci dokumentace přeložit celý aktuální `protocol.md` a uchovat překlad i hash zdroje. Pracovní build adresáře zůstávají mimo výslednou distribuci.
3. Znovu vytvořit obsah `dist` a celou povinnou strukturu. Naplnit serverovou část, platformní klientské složky a `documentation` podle pravidel výše. Vždy zahrnout `documentation/readme.txt`, volitelně uchovaný anglický `documentation/protocol.md`.
4. Připravit úplný Windows x64 balíček a linuxovou složku x64, ověřit architekturu a závislosti, zahrnout licence a po dokončení obsahu vytvořit pro každou z nich odpovídající podepsaný manifest. Úroveň důvěry podpisu se uvede pravdivě, vývojový podpis se NESMÍ vydávat za veřejně důvěryhodný vydavatelský podpis.
5. Ověřit hashovou shodu povinného README s kořenovým zdrojem a přítomného protokolu s uchovaným anglickým překladem. Ověřit hashovou shodu PHP souboru, všech jedenácti souborů Python části a obou souborů Node.js části s aktuálními zdroji, všechny požadované cesty, úplnost souborů, relativní odkazy skriptů a integritu obou klientských balíčků včetně podpisu linuxového manifestu a provenance linuxových balíčků. Python část ověřit čistým prostředím mimo `dist`, inicializací distribuovanými skripty a spuštěním distribuovaného serveru. Ověřit health, info a data v obou režimech pomocí izolovaných konfiguračních kopií. Node.js část ověřit spuštěním distribuovaného `phpvpn.js` z rozbalené kopie s izolovanou konfigurací (`--config`, vlastní port a root) a výměnou health, info a data. Skutečné instalační, provozní a další testy předepsané v [client.md](client.md) se při vytváření distribuce provedou, pokud uživatel výslovně neurčí, že se provádět nemají. Při takovém výslovném pokynu se tyto testy neprovedou.
6. Předat přesnou cestu k výsledku a stručný stav ověření. Pokud se vytváří ZIP, jeho obsah MUSÍ odpovídat ověřenému výsledku a zachovat příslušnou adresářovou strukturu. Archiv a pracovní reporty se ukládají mimo `dist`, aby se při dalším sestavení nemísily s jeho obsahem.

Kanonický vstup pro **celou distribuci** je [tools/make-distribution.ps1](tools/make-distribution.ps1). Vyžaduje explicitní existující CMake build z tohoto checkoutu a složku linuxových balíčků ze stejného commitu. Vždy znovu spustí sestavení Windows klienta, takže nebalí pouze nalezené staré binárky.

Linuxové balíčky se sestaví na linuxovém build hostiteli v checkoutu téhož commitu bez necommitnutých změn. Oba běhy zapisují do stejné složky, `<id>` je podle konvence `<verze>-<release>-<krátký commit>`:

```sh
cd client
sh tools/linux/build-packages-in-container.sh fedora:44 ../work/linux-packages/<id>
sh tools/linux/build-packages-in-container.sh ubuntu:24.04 ../work/linux-packages/<id>
```

Kopie zdrojů bez `.git` MUSÍ předat skutečný commit a stav přes `--source-commit` a `--source-dirty`. Po přenesení složky do `work/linux-packages/<id>` Windows checkoutu z jeho kořene:

```powershell
./client/tools/build-x64.ps1 -Preset release -Configure -ResourceTests
./tools/make-distribution.ps1 -BuildDirectory ./work/build/release -LinuxPackagesDirectory ./work/linux-packages/<id> -SignBinaries
./tools/verify-distribution.ps1 -DistributionDirectory ./dist
```

Podpisovou identitu určuje `signing.certificateThumbprint` v `phpvpn.config.json`, případně v souboru vybraném proměnnou `PHPVPN_CONFIG`. Volitelný `-CertificateThumbprint` má před konfigurací přednost a předává se oběma klientským packagerům i kontrolám distribuce. Vybraný RSA certifikát s privátním klíčem musí být v `Cert:\CurrentUser\My` účtu sestavujícího. Soukromý klíč se neexportuje.

Packager před vytvořením a podpisem manifestu vygeneruje instalační skripty ze zdrojových šablon. Do Windows `install.ps1` vloží thumbprint vybraného certifikátu a do Linux `install.sh` SHA-256 jeho DER SubjectPublicKeyInfo. GUI, BAT i shellový vstup tak ověřují tutéž identitu vydavatele. Pin se při instalaci neodvozuje z přibaleného certifikátu. Ověřovací skripty porovnávají celý vygenerovaný instalátor s šablonou a identitou z konfigurace nebo explicitního parametru, ostatní soubory nadále bajtově porovnávají se zdroji.

Příjemce zdrojové distribuce musí nastavit vlastní podpisovou identitu. Pro lokální test lze vytvořit nový certifikát příkazem `New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=phpvpn local test' -KeyAlgorithm RSA -KeyLength 3072 -CertStoreLocation Cert:\CurrentUser\My` a jeho `Thumbprint` uložit do konfigurace. Takový certifikát není veřejně důvěryhodným vydavatelským podpisem. Pro vydání použij vlastní vydavatelský certifikát a zajisti důvěryhodné doručení instalátoru příjemci. Samotný podpis balíčku tuto distribuci důvěry nenahrazuje. Windows manifest pokrývá pouze obsah `client/Windows/x64`, linuxový manifest pouze obsah `client/Linux/x64`.

Celkový sestavovací vstup vytvoří nový staging v `work/distribution-<id>`, kontroluje přesný seznam souborů včetně skrytých, hashe, architekturu, podpisy obou manifestů a ikony. Vytvoří ZIP, zachová POSIX executable atribut shellových skriptů včetně `client/Linux/x64/install.sh` a `uninstall.sh`, ověří tyto atributy přímo v archivu, rozbalí archiv do nového adresáře a znovu spustí ověření. Teprve potom publikuje `dist` a `dist.zip`. Předchozí strom ponechá v pracovním adresáři pro obnovu. Při chybě ověření stávající distribuci nemění. Rekurzivní přesuny odmítají reparse pointy a cíle mimo vymezené adresáře. Cesta k rozbalené ověřené kopii se vypíše pro následné izolované provozní testy.

Přepínač `-StagingOnly` skončí po ověřeném stagingu, ZIP a ověření rozbalené kopie v `work/distribution-<id>` a vypíše jejich cesty. `dist` ani `dist.zip` nemění. Slouží ke zkušebnímu sestavení a k testům před publikací.

Dílčí [client/tools/make-dist.ps1](client/tools/make-dist.ps1) a [client/tools/linux/make-linux-dist.ps1](client/tools/linux/make-linux-dist.ps1) vyžadují `-OutputDirectory <cesta>` pod `work` nebo `dist`. Kořenový `dist` není povoleným cílem těchto dílčích nástrojů. Serverovou část a ostatní platformní složky vytváří výhradně celkový vstup.

Po sestavení spusť v odděleném vývojovém venv `python -B tools/test-server-scripts.py --distribution <rozbaleny-strom> --report <report.json>`. Runner vytváří vlastní kopie, porty a buffery, kontroluje Windows BAT nebo nativní POSIX SH vstupy, health/info/data, identitu, souběh, chyby, URL a nejméně třísekundové bannery. `tests-python/test_server_control.py` doplňuje deterministické případy recyklovaného PID, nedostupných oprávnění a timeoutu. Bootstrap bez uv a Pythonu se ověřuje zvlášť v čistém prostředí. Windows běh nenahrazuje Linux ani macOS, neprovedené platformy a ruční instalační testy se uvádějí v datovaném reportu.

Windows balíček se ověřuje také pomocí `client/tools/verify-package.ps1 -PackageDirectory <cesta>`. Tento read-only krok kontroluje shodu skriptů, povinnou přítomnost pěti EXE, manifest, podpisy, VerifyOnly a přesné vložené ICO resources všech pěti produkčních EXE včetně úrovní UAC. Vyžaduje sestavený `phpvpn-icon-resource-tests`, standardně z presetu `release` s `BUILD_TESTING=ON`. Jiný ověřovací build lze zadat přes `-IconResourceTest`. Čtyři zdrojové ICO se používají pouze při sestavení a ověřování, do provozního balíčku se samostatně nepřidávají.

Linuxovou složku ověřuje read-only `tools/verify-distribution.ps1`. Kontroluje přesný seznam souborů, shodu `install.sh`, `uninstall.sh`, `README.md` a licencí se zdroji, LF shellových skriptů, právě čtyři balíčky verze klienta se společným release a platnou hlavičkou RPM nebo DEB, velikost a SHA-256 každé položky manifestu a jeho pole (verze a IPC verze shodné s Windows balíčkem, právě Fedora a Ubuntu). Podpis ověří stejně jako Windows instalátor: `release-signing.cer` MUSÍ být shodný s Windows balíčkem, identita v manifestu i podpisu MUSÍ odpovídat certifikátu, veřejný klíč MUSÍ odpovídat pinu v `install.sh` a RSA PKCS#1 v1.5 SHA-256 podpis MUSÍ platit pro přesné bajty manifestu. Manifest se `sourceDirty` rovným `true` vyvolá varování, že nejde o vydání.

Na linuxových systémech se z rozbalené ověřené kopie dále provede `sh install.sh --verify-only` na Fedoře 44 a Ubuntu 24.04 (nevyžaduje root, nic neinstaluje) a na linuxovém hostiteli s podmanem kontejnerový instalační test obou distribucí:

```sh
cd client
sh tests/e2e/linux/package-install-container.sh fedora:44 <rozbalena-kopie>/client/Linux/x64 <slozka-release-2>
sh tests/e2e/linux/package-install-container.sh ubuntu:24.04 <rozbalena-kopie>/client/Linux/x64 <slozka-release-2>
```

Test ověřuje `--verify-only`, odmítnutí upravených a nezapsaných souborů, instalaci, běžící službu, `phpvpn status`, upgrade na vyšší release se zachováním konfigurace, balíček tray a odinstalaci s daty i bez nich. `<slozka-release-2>` je složka téže verze s vyšším release (balíčky sestavené s `PHPVPN_PACKAGE_RELEASE=2`, složené `make-linux-dist.ps1` pod `work`). Slouží pouze k testu upgrade a do distribuce nepatří. Instalace do skutečného systému, grafický tray a AppIndicator na Fedora GNOME se ověřují podle [client.md](client.md), neprovedené platformy a testy se uvádějí v datovaném reportu.

Status stránka je vložená přímo v `phpvpn.php`, `phpvpn.py` i `phpvpn.js`. Žádná serverová distribuce nepotřebuje `assets/index.html`. PHP, Python a Node.js nepoužívají vzájemně čitelný diskový formát, výměna implementace nad rozpracovanými frontami není podporovaná migrace.
