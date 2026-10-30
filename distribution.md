# Metodika tvorby distribuce PHP-VPN

## Účel a závaznost

Tento dokument je autoritativní a závaznou metodikou sestavení distribuce PHP-VPN. Určuje okamžik sestavení, zdroje, adresářovou strukturu, obsah a ověření výsledku. Klíčová slova **MUSÍ**, **NESMÍ**, **MĚL BY**, **NEMĚL BY** a **SMÍ** mají stejný normativní význam jako v [protocol.md](protocol.md).

Pravidla tvorby distribučního stromu se řídí tímto dokumentem. [protocol.md](protocol.md) zůstává autoritativním zdrojem drátového protokolu a serverového chování. [client.md](client.md) zůstává autoritativním zdrojem chování klienta, instalace, konfigurace, platformní integrace a bezpečnostních požadavků. Metodika tato pravidla nemění ani neoslabuje. Starší popisy ploché složky `dist` a výchozí chování nástrojů NESMĚJÍ nahradit zde stanovenou strukturu.

## Kdy distribuci sestavit

Distribuce se sestavuje typicky po dokončení implementačních úprav určených k vydání nebo na výslovný pokyn uživatele. Sestavení MUSÍ vycházet z aktuálního stavu zdrojů určeného k předání, včetně zamýšlených dosud necommitnutých změn. Vytvoření commitu ani push nejsou podmínkou sestavení a samotné sestavení je neprovádí.

Při každém sestavení se obsah projektového adresáře `dist` MUSÍ znovu vytvořit z aktuálních zdrojů. Pouhé překopírování nových souborů přes starou distribuci nestačí, protože by ponechalo odstraněné nebo přejmenované součásti. Výsledný strom NESMÍ obsahovat neplánované soubory ze staršího sestavení. Výslovnou výjimkou z aktuálnosti dokumentace je uchovaný anglický překlad protokolu podle oddílu Dokumentace. Uchovávané starší balíčky, zálohy a pracovní podklady patří mimo `dist`.

### Verze

Každá nová distribuce MUSÍ mít novou verzi PHP VPN. Před sestavením se vždy spustí `./tools/bump-version.ps1`, který zvýší poslední číslo verze (`X.Y.Z` na `X.Y.Z+1`). Jedním během přepíše všechna místa verze podle `Get-VersionLocations` v `tools/distribution-common.ps1`:
- `VERSION` projektu v `client/CMakeLists.txt`, ze kterého ji přebírají tray, služba, CLI, Windows version resource i linuxové balíčky,
- `Version:` RPM specu a nový záznam `X.Y.Z-1` v jeho `%changelog` i v `debian/changelog`,
- hlavičky `.TH` manuálových stránek (verze a datum),
- hlavičku na druhém řádku `server/php/phpvpn.php`, `server/python/phpvpn.py` a `server/node.js/phpvpn.js` ve tvaru `PHP VPN X.Y.Z (MIT License, Copyright (c) www.pilandrome.com)`,
- `SERVER_VERSION` všech tří serverů, kterou server hlásí v odpovědích health a info,
- verzi WordPress pluginu v `server/wordpress/version7/vpn-server` (hlavička a `VERSION` v `vpn-server.php`, `Stable tag` v `readme.txt` a generované `includes/server-core.php`).

Pokud místa nemají stejnou výchozí verzi, skript nic nezmění. Protože linuxové balíčky nesou verzi a sestavují se z commitu, změna verze se MUSÍ commitnout před linuxovým buildem (`-Commit` vytvoří commit `Version X.Y.Z` jen z přepsaných souborů). Výjimka pro necommitnuté změny z oddílu Kdy distribuci sestavit se na změnu verze nevztahuje. `tools/make-distribution.ps1` odmítne sestavení, pokud se některé místo verze liší od verze klienta, a odmítne publikaci, pokud verze není vyšší než `releaseVersion` aktuálně publikovaného `dist`. S `-StagingOnly` tuto druhou neshodu pouze vypíše jako varování.

Před obnovením obsahu se MUSÍ ověřit, že cílem je skutečně adresář `dist` pod kořenem vybraného projektu. Sestavení SMÍ připravit nový strom v odděleném pracovním adresáři a poté jím nahradit obsah `dist`. Neúplný nebo neověřený výsledek NESMÍ být označen za hotovou distribuci.

## Povinná struktura

Cesty jsou relativní ke kořeni projektu. Velikost písmen platformních složek a označení architektury `x64` jsou závazné. Hlavní složky `client`, `server` a `documentation` a serverové složky `php`, `python`, `node.js` a `wordpress` MUSÍ mít názvy malými písmeny. Starší varianty s velkými písmeny (`PHP`, `Python`, `Node.js`) se NESMĚJÍ používat.

```text
dist/
  documentation/
    readme.txt
    protocol.md                 # volitelný úplný anglický překlad
  server/
    php/
      phpvpn.php
      README.md
    python/
      phpvpn.py
      requirements-server.txt
      README.md
      init.bat
      start.bat
      stop.bat
      init.sh
      start.sh
      stop.sh
      bootstrap.ps1
      server-control.py
    node.js/
      phpvpn.js
      README.md
    wordpress/
      vpn-server-<verze>.zip
      README.md
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
      arm64/                    # s balíčkem z CI (oddíl macOS)
        install.sh
        uninstall.sh
        README.md
        phpvpn-<verze>-arm64.pkg
        release-manifest.json
        release-manifest.sig
        release-signing.cer
      README.md                 # místo arm64/, jen bez balíčku z CI (placeholder)
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

Složka `client/macOS` obsahuje vždy právě jednu ze dvou variant: buď podsložku `arm64` s úplným macOS klientem, nebo samotný placeholder `README.md`. Smíšený nebo neúplný stav je chybou sestavení.

Uvedené Windows, Linux a macOS soubory tvoří základ aktuální klientské distribuce. Seznam se MUSÍ doplnit o všechny další skripty, programy, knihovny, distribuovatelné konfigurace, šablony a podpůrné soubory, které aktuální implementace potřebuje pro instalaci a provoz. Testovací binárky, build cache, pracovní logy a obsah provozních front do distribuce nepatří.

### Archivy klientů ke stažení

Při každém sestavení distribuce se MUSÍ vedle `dist` vytvořit také samostatná složka `dist-downloads`. Nejde o podsložku `dist`. Obsah pro zveřejnění pod `https://www.pilandrome.com/downloads/` má tuto strukturu:

```text
dist-downloads/
  phpvpn/
    client/
      1.x/
        phpvpn-win.zip
        phpvpn-macos.zip
        phpvpn-linux.zip
```

`phpvpn-win.zip` MUSÍ obsahovat úplný obsah ověřené složky `dist/client/Windows`, `phpvpn-linux.zip` úplný obsah ověřené složky `dist/client/Linux` a `phpvpn-macos.zip` úplný obsah ověřené složky `dist/client/macOS`. Obsah platformní složky bude přímo v kořeni jejího ZIPu, bez nadřazeného stromu `dist/client`, takže nejvyšší úrovní archivu jsou architektonické složky (`x64/`, u macOS `arm64/`). Až distribuce dostane další architekturu (například `arm64/` pro Windows a Linux), přibude v archivu vedle stávající a název ani struktura archivu se nezmění. Archivy zachovají relativní cesty, podpisy, manifesty a u linuxových i macOS shellových skriptů POSIX executable atribut. MUSÍ pocházet ze stejného sestavení jako `dist` a jejich názvy zůstávají stabilní pro odkazy ze serverové stránky.

**Placeholder pro macOS bez balíčku z CI:** macOS balíček nevzniká na Windows hostiteli, ale v GitHub Actions (oddíl macOS). Pokud sestavení distribuce žádný balíček nedostane, obsahuje `dist/client/macOS` pouze anglický `README.md`, který jasně uvádí, že klient není k dispozici, a `phpvpn-macos.zip` pak obsahuje jen tento `README.md` přímo v kořeni. Takový archiv se NESMÍ vydávat za funkční instalátor. Sestavovací vstup tento stav ohlásí varováním.

Před zveřejněním se MUSÍ ověřit obsah všech tří ZIPů proti příslušným platformním složkám. Archivy vytváří a ověřuje (seznam souborů, SHA-256 a POSIX režimy) [tools/make-download-archives.ps1](tools/make-download-archives.ps1). Celkový vstup jej volá nad ověřeným stagingem ještě před publikací, takže chyba archivů ponechá `dist` i `dist-downloads` beze změny. Po úspěšné publikaci a ověření `dist` nahradí předchozí obsah `dist-downloads`. S `-StagingOnly` zůstanou archivy v `work/distribution-<id>/dist-downloads`. Samostatné spuštění jen znovu vytvoří archivy aktuálního `dist`. Publikace obsahu `dist-downloads` na web je samostatný krok.

### Dokumentace

Distribuce MUSÍ mít tři hlavní složky `client`, `server` a `documentation`.
Kořenový [readme.txt](readme.txt), vedle `protocol.md` a `client.md`, je udržovaný zdroj stručného anglického instalačního návodu. Při KAŽDÉM sestavení se MUSÍ beze změny zkopírovat do `dist/documentation/readme.txt`. README je povinné, i když překlad protokolu chybí. Uvádí klientskou a serverovou část, skutečné postupy pro Windows, Linux a macOS a nasazení PHP, Node.js a Python serveru a WordPress pluginu. Každá ze čtyř serverových složek MUSÍ obsahovat vlastní anglický `README.md`, jehož zdrojem je `README.md` ve stejnojmenné složce `server/<implementace>` repozitáře. Samostatné návody `server-node.md` a `server-python.md` README nahradilo a do distribuce už nepatří.

Český [protocol.md](protocol.md) zůstává autoritativním zdrojem a NESMÍ se překladem přepsat. Úplný anglický překlad se udržuje mimo distribuci jako `documentation/protocol.en.md`. Aktualizovat jej podle celého aktuálního českého dokumentu se SMÍ pouze na výslovnou žádost uživatele o aktualizaci dokumentace při tvorbě distribuce. Nestačí shrnutí ani překlad změněných částí bez kontroly celého dokumentu. Překlad zachová normativní význam, identifikátory, vzorce, příklady a všechny kapitoly. Vedle něj se uchovává `documentation/protocol.en.source.sha256` s SHA-256 českého zdroje použitého pro poslední překlad. Tento záznam nepatří do distribuce a nevyžaduje se jeho shoda s budoucím českým zdrojem bez žádosti o aktualizaci.

Bez takové žádosti se nový překlad NESMÍ vytvářet ani automaticky aktualizovat. Sestavovací skript pouze zkopíruje existující `documentation/protocol.en.md` do `dist/documentation/protocol.md`. Není-li uchovaný zdroj dostupný, překlad vynechá. Již existující anglický protokol z předchozí distribuce lze nejprve uchovat na této zdrojové cestě beze změny, bez tvrzení o jeho aktuálnosti. Přepínač `-OmitProtocol` dovoluje překlad vynechat i při existujícím zdroji. Kontrola distribuce dovoluje jeho nepřítomnost, ale přítomný překlad MUSÍ bajtově odpovídat uchovanému anglickému zdroji. README se vždy ověřuje proti aktuálnímu kořenovému zdroji. Nový překlad pro aktuální sestavení se připraví před spuštěním sestavovacího skriptu. Relativní odkazy na vývojovou dokumentaci, například `client.md`, vyžadují soubory ze zdrojového repozitáře a v samostatné distribuci nejsou dostupné. Tato dokumentace se tím nestává povinnou součástí distribuce.

### Server

Zdrojovým umístěním každé serverové implementace je stejnojmenná složka repozitáře: [server/php](server/php), [server/python](server/python), [server/node.js](server/node.js) a [server/wordpress](server/wordpress). Při sestavení se z ní do `dist/server/<implementace>` kopíruje beze změny pouze distribuovaný výběr souborů podle `Get-DistributionSources` v `tools/distribution-common.ps1`. Testy implementace (`server/<implementace>/tests`) a vývojové soubory do distribuce nepatří.

`dist/server/php` MUSÍ obsahovat nejaktuálnější distribuovatelnou verzi serveru v PHP a jeho anglický návod `README.md`. Zdrojem je [server/php/phpvpn.php](server/php/phpvpn.php), který se zkopíruje beze změny názvu i obsahu jako `phpvpn.php`.

`dist/server/python` MUSÍ obsahovat aktuální zdrojový `phpvpn.py`, zamčené runtime závislosti `requirements-server.txt`, anglický provozní návod `README.md`, šest vstupů `init.bat`, `start.bat`, `stop.bat`, `init.sh`, `start.sh`, `stop.sh` a jejich helpery `bootstrap.ps1`, `server-control.py`. Vše se kopíruje beze změny ze [server/python](server/python). Python server je samostatně spustitelný podle tohoto návodu a implementuje stejné klientské zprávy jako PHP. Výchozí diskový a volitelný paměťový režim vyžadují jediný aplikační proces a worker. Distribuce NESMÍ obsahovat `.venv`, `.tools`, `.runtime`, `.bootstrap.lock`, uv cache, bytecode, logy, testovací konfiguraci, provozní fronty ani místní tajemství. Zdrojový `requirements-server.in` slouží k obnově locku ve vývojovém repozitáři, při nasazení se používá přibalený `.txt`.

`dist/server/node.js` MUSÍ obsahovat aktuální zdrojový [phpvpn.js](server/node.js/phpvpn.js) a anglický provozní návod [README.md](server/node.js/README.md), oba kopírované beze změny. Node.js server je jediný soubor bez npm závislostí, build kroku a assets a implementuje stejné klientské zprávy jako PHP. Distribuce NESMÍ obsahovat `node_modules`, konfigurační JSON s tajemstvími, logy ani provozní frontu `phpvpn_data`. Server spuštěný přímo z `dist` si výchozí `phpvpn_data` vytvoří vedle skriptu. Při publikaci se tento lokální stav stejně jako `.venv` a `.runtime` Python části nepřesouvá, nepublikuje ani nemaže.

`dist/server/wordpress` MUSÍ obsahovat instalovatelný ZIP WordPress pluginu VPN Server `vpn-server-<verze>.zip` a anglický návod [README.md](server/wordpress/README.md). `<verze>` je verze pluginu z hlavičky `vpn-server.php`. ZIP obsahuje právě všechny soubory složky [server/wordpress/version7/vpn-server](server/wordpress/version7/vpn-server) bajtově beze změny, každý pod cestou `vpn-server/`. Serverové jádro pluginu `includes/server-core.php` se generuje z `server/php/phpvpn.php` nástrojem `tools/build-wordpress-plugin.py`. Sestavení distribuce MUSÍ odmítnout zastaralé jádro (`--check`).

Publikace do existujícího `dist` přejmenuje složku, jejíž název se změnil jen velikostí písmen (například `server/Python` ze starší distribuce na `server/python`), protože Windows u existující složky původní velikost písmen zachovává. Lokální provozní stav Python a Node.js části se přitom přesune spolu se složkou. Pokud složku drží běžící server, publikace vypíše varování a složku je nutné přejmenovat ručně po zastavení serveru.

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

Adresář `dist/client/macOS` MUSÍ vzniknout při každém sestavení. Jeho obsah MUSÍ odpovídat skutečně dostupné implementaci a možnostem jejího ověření.

`dist/client/macOS/arm64` je úplná samostatně předatelná klientská distribuce pro macOS 14 a novější na Apple Silicon. Obsahuje jediný instalační balíček `phpvpn-<verze>-arm64.pkg` (služba `phpvpn-service` pod launchd, CLI `phpvpn`, launchd plist služby, menu bar aplikace `phpvpn Tray.app` a její LaunchAgent), instalační vstupy `install.sh` a `uninstall.sh`, anglický návod `README.md` a podepsaný manifest. Chování balíčku, instalace, služby a tray stanoví [client.md](client.md) a přibalený `README.md`. Konfigurace je `/Library/Application Support/phpvpn/config.json`, stav `/private/var/db/phpvpn`, log `/private/var/log/phpvpn`. Balíček tyto adresáře nedodává, vytváří je instalační skript balíčku nebo služba.

Programy nesou pouze ad hoc podpis kódu (`codesign -s -`) s hardened runtime, `.pkg` není podepsán Apple Developer ID ani notarizován. Jde o vývojový podpis, stejně jako vývojový certifikát Windows balíčku, a NESMÍ se vydávat za důvěryhodný podpis vydavatele. Podporovaná instalace je `sudo sh install.sh` z Terminálu, kterou Gatekeeper neblokuje. Dvojklik na stažený `.pkg` Gatekeeper blokuje a navíc obchází ověření manifestu; `README.md` oba důsledky uvádí.

macOS balíček se NESMÍ sestavovat na Windows hostiteli. Sestavuje jej workflow [client-macos](.github/workflows/client-macos.yml) na GitHub Actions runneru `macos-15` (Apple Silicon) nástrojem `client/tools/macos/package-pkg.sh` z téhož commitu, ze kterého se sestavuje Windows klient. Výstupem je artefakt `phpvpn-macos-arm64` s balíčkem, `SHA256SUMS`, `build-provenance.json`, nepodepsanými kopiemi `README.md`, `install.sh` (s nevyplněným pinem) a `uninstall.sh`, logy testů a složkou `screenshots/` se snímky stavové položky a oken tray z instalačního testu. `build-provenance.json` uvádí `sourceCommit`, `sourceDirty` (necommitnuté změny pod `client/`), verzi macOS runneru (`builtOn`, `buildVersion`), Xcode, SDK, kompilátor, CMake, verze OpenSSL (staticky linkovaný Homebrew `openssl@3`) a systémového libcurl, údaje o běhu GitHub Actions, `trayIncluded`, minimální verzi macOS a SHA-256 balíčku.

Artefakt se z dokončeného běhu workflow stáhne ručně (GitHub, záložka Actions, nebo `gh run download <run-id> -n phpvpn-macos-arm64 -D work/macos-packages/<id>`) a rozbalí beze změny do `work/macos-packages/<id>` Windows checkoutu. Celkový vstup jej převezme parametrem `-MacosPackagesDirectory`; bez parametru použije `work/macos-packages/latest`, pokud existuje. Bez balíčku zůstane `dist/client/macOS` placeholderem s jediným `README.md`, který výslovně uvádí, že klient není k dispozici. Přítomnost složky sama o sobě není tvrzením, že je platforma podporovaná nebo otestovaná.

Celkový vstup před sestavením Windows klienta ověří `SHA256SUMS`, právě jeden balíček `phpvpn-<verze>-arm64.pkg` verze klienta s xar hlavičkou a provenance (platforma, architektura, minimální macOS 14.0, shoda hashe). Stejně jako u Linuxu MUSÍ `sourceCommit` odpovídat `HEAD` checkoutu, `sourceDirty` MUSÍ být `false` a checkout NESMÍ mít necommitnuté změny pod `client/`. Přepínač `-AllowMacosProvenanceMismatch` je určen výhradně pro vývoj: neshodu vypíše jako varování a zaznamená do `work/distribution-<id>/macos-provenance-mismatch.txt`. Takový výsledek NESMÍ být předán jako vydání.

Složku skládá `client/tools/macos/make-macos-dist.ps1`. Balíček kopíruje z artefaktu, `install.sh`, `uninstall.sh` a `README.md` ze zdrojů `client/packaging/macos/dist` checkoutu (kopie v artefaktu se nepoužijí). Manifest `release-manifest.json` má stejný kanonický tvar a podpis `release-manifest.sig` stejný mechanismus i certifikát `release-signing.cer` jako Windows a linuxový balíček. Navíc uvádí `platform` `macos`, `architecture` `arm64`, `minimumOsVersion`, `sourceCommit`, `sourceDirty`, `builtOn` a `xcode`. Pokrývá pouze obsah `client/macOS/arm64`. `install.sh` pinuje SHA-256 veřejného klíče certifikátu stejně jako linuxový instalátor a ověřuje podpis nástrojem `openssl` (LibreSSL je součástí macOS) a hashe nástrojem `shasum`. `install.sh` a `uninstall.sh` jsou POSIX shell s LF bez BOM a v ZIP nesou POSIX executable atribut. Instalační postup NESMÍ stahovat nebo přijímat neověřenou náhradu některé součásti složky.

Ověření na Windows hostiteli (`tools/verify-distribution.ps1`) pokrývá přesný seznam souborů, shodu `uninstall.sh` a `README.md` se zdroji, `install.sh` se šablonou a pinem, LF shellových skriptů, právě jeden balíček verze klienta s xar hlavičkou, velikost a SHA-256 každé položky manifestu, jeho pole (verze a IPC verze shodné s Windows balíčkem, minimální macOS 14.0) a podpis stejným certifikátem jako Windows balíček. Odmítne napůl připravenou složku (`arm64` vedle placeholderu nebo neúplné `arm64`). Neověřuje obsah `.pkg`, ad hoc podpisy uvnitř ani instalaci. Ty pokrývá workflow (kontrola payloadu a skriptů v `package-pkg.sh`, `codesign --verify`, testy služby, instalace balíčku systémovým `installer -pkg` na runneru s ověřením launchd daemonu, CLI, LaunchAgentem spuštěného tray a Connect přes nainstalovaného daemona, snímky oken tray a `sudo sh uninstall.sh --remove-data`, viz [client.md, macOS](client.md#macos)) a ruční test na Macu: `sh install.sh --verify-only`, `sudo sh install.sh` s podepsaným manifestem, `phpvpn status`, tray v menu baru, upgrade se zachováním konfigurace, `sudo sh install.sh --headless` a `sudo sh uninstall.sh` bez `--remove-data`. Neprovedené testy se uvádějí v datovaném reportu.

## Postup sestavení a ověření

0. Spustit `./tools/bump-version.ps1` a novou verzi commitnout podle oddílu Verze.
1. Určit aktuální zdrojový stav určený k vydání a provést kontroly odpovídající změnám. Zaznamenat použité zdroje, verzi, platformu a výsledky ověření. Neprovedené testy se NESMĚJÍ vykazovat jako úspěšné.
2. Sestavit klientské programy z těchto zdrojů, na linuxovém build hostiteli z téhož commitu linuxové balíčky, a připravit aktuální PHP, Python a Node.js serverové soubory, aktuální jádro WordPress pluginu, Python lock a anglická README všech čtyř serverových částí. Připravit kořenový anglický `readme.txt`. Jen při výslovné žádosti o aktualizaci dokumentace přeložit celý aktuální `protocol.md` a uchovat překlad i hash zdroje. Pracovní build adresáře zůstávají mimo výslednou distribuci.
3. Znovu vytvořit obsah `dist` a celou povinnou strukturu. Naplnit serverovou část, platformní klientské složky a `documentation` podle pravidel výše. Vždy zahrnout `documentation/readme.txt`, volitelně uchovaný anglický `documentation/protocol.md`.
4. Připravit úplný Windows x64 balíček, linuxovou složku x64 a s balíčkem z CI macOS složku arm64, ověřit architekturu a závislosti, zahrnout licence a po dokončení obsahu vytvořit pro každou z nich odpovídající podepsaný manifest. Úroveň důvěry podpisu se uvede pravdivě, vývojový podpis se NESMÍ vydávat za veřejně důvěryhodný vydavatelský podpis.
5. Ověřit hashovou shodu povinného README s kořenovým zdrojem a přítomného protokolu s uchovaným anglickým překladem. Ověřit hashovou shodu obou souborů PHP části, všech jedenácti souborů Python části, obou souborů Node.js části a README WordPress části s aktuálními zdroji, bajtovou shodu všech položek ZIP WordPress pluginu se složkou pluginu, všechny požadované cesty, úplnost souborů, relativní odkazy skriptů a integritu klientských balíčků včetně podpisu linuxového a macOS manifestu a provenance linuxových a macOS balíčků. Python část ověřit čistým prostředím mimo `dist`, inicializací distribuovanými skripty a spuštěním distribuovaného serveru. Ověřit health, info a data v obou režimech pomocí izolovaných konfiguračních kopií. Node.js část ověřit spuštěním distribuovaného `phpvpn.js` z rozbalené kopie s izolovanou konfigurací (`--config`, vlastní port a root) a výměnou health, info a data. Skutečné instalační, provozní a další testy předepsané v [client.md](client.md) se při vytváření distribuce provedou, pokud uživatel výslovně neurčí, že se provádět nemají. Při takovém výslovném pokynu se tyto testy neprovedou.
6. Předat přesnou cestu k výsledku a stručný stav ověření. Pokud se vytváří ZIP, jeho obsah MUSÍ odpovídat ověřenému výsledku a zachovat příslušnou adresářovou strukturu. Archiv a pracovní reporty se ukládají mimo `dist`, aby se při dalším sestavení nemísily s jeho obsahem.

Kanonický vstup pro **celou distribuci** je [tools/make-distribution.ps1](tools/make-distribution.ps1). Vyžaduje explicitní existující CMake build z tohoto checkoutu a složku linuxových balíčků ze stejného commitu. macOS balíček ze stejného commitu je volitelný (oddíl macOS). Vždy znovu spustí sestavení Windows klienta, takže nebalí pouze nalezené staré binárky.

Nejdřív se na Windows hostiteli zvýší a commitne verze:

```powershell
./tools/bump-version.ps1 -Commit
```

Linuxové balíčky se sestaví na linuxovém build hostiteli v checkoutu téhož commitu bez necommitnutých změn. Oba běhy zapisují do stejné složky, `<id>` je podle konvence `<verze>-<release>-<krátký commit>`:

```sh
cd client
sh tools/linux/build-packages-in-container.sh fedora:44 ../work/linux-packages/<id>
sh tools/linux/build-packages-in-container.sh ubuntu:24.04 ../work/linux-packages/<id>
```

Kopie zdrojů bez `.git` MUSÍ předat skutečný commit a stav přes `--source-commit` a `--source-dirty`. macOS balíček sestaví workflow `client-macos` po pushi téhož commitu; jeho artefakt `phpvpn-macos-arm64` se rozbalí do `work/macos-packages/latest` (nebo `work/macos-packages/<id>` předaného přes `-MacosPackagesDirectory`):

```powershell
gh run download <run-id> -n phpvpn-macos-arm64 -D ./work/macos-packages/latest
```

Po přenesení linuxové složky do `work/linux-packages/<id>` Windows checkoutu z jeho kořene:

```powershell
./client/tools/build-x64.ps1 -Preset release -Configure -ResourceTests
./tools/make-distribution.ps1 -BuildDirectory ./work/build/release -LinuxPackagesDirectory ./work/linux-packages/<id> -MacosPackagesDirectory ./work/macos-packages/latest -SignBinaries
./tools/verify-distribution.ps1 -DistributionDirectory ./dist
```

Podpisovou identitu určuje `signing.certificateThumbprint` v `phpvpn.config.json`, případně v souboru vybraném proměnnou `PHPVPN_CONFIG`. Volitelný `-CertificateThumbprint` má před konfigurací přednost a předává se všem klientským packagerům i kontrolám distribuce. Vybraný RSA certifikát s privátním klíčem musí být v `Cert:\CurrentUser\My` účtu sestavujícího. Soukromý klíč se neexportuje.

Packager před vytvořením a podpisem manifestu vygeneruje instalační skripty ze zdrojových šablon. Do Windows `install.ps1` vloží thumbprint vybraného certifikátu a do Linux i macOS `install.sh` SHA-256 jeho DER SubjectPublicKeyInfo. GUI, BAT i shellový vstup tak ověřují tutéž identitu vydavatele. Pin se při instalaci neodvozuje z přibaleného certifikátu. Ověřovací skripty porovnávají celý vygenerovaný instalátor s šablonou a identitou z konfigurace nebo explicitního parametru, ostatní soubory nadále bajtově porovnávají se zdroji.

Příjemce zdrojové distribuce musí nastavit vlastní podpisovou identitu. Pro lokální test lze vytvořit nový certifikát příkazem `New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=phpvpn local test' -KeyAlgorithm RSA -KeyLength 3072 -CertStoreLocation Cert:\CurrentUser\My` a jeho `Thumbprint` uložit do konfigurace. Takový certifikát není veřejně důvěryhodným vydavatelským podpisem. Pro vydání použij vlastní vydavatelský certifikát a zajisti důvěryhodné doručení instalátoru příjemci. Samotný podpis balíčku tuto distribuci důvěry nenahrazuje. Windows manifest pokrývá pouze obsah `client/Windows/x64`, linuxový manifest pouze obsah `client/Linux/x64` a macOS manifest pouze obsah `client/macOS/arm64`.

Celkový sestavovací vstup vytvoří nový staging v `work/distribution-<id>`, kontroluje přesný seznam souborů včetně skrytých, hashe, architekturu, podpisy linuxového a případného macOS manifestu a ikony. Vytvoří ZIP, zachová POSIX executable atribut shellových skriptů včetně `client/Linux/x64/install.sh` a `uninstall.sh` (a `client/macOS/arm64/install.sh` a `uninstall.sh`), ověří tyto atributy přímo v archivu, rozbalí archiv do nového adresáře a znovu spustí ověření. Teprve potom publikuje `dist` a `dist.zip`. Předchozí strom ponechá v pracovním adresáři pro obnovu. Při chybě ověření stávající distribuci nemění. Rekurzivní přesuny odmítají reparse pointy a cíle mimo vymezené adresáře. Cesta k rozbalené ověřené kopii se vypíše pro následné izolované provozní testy.

Přepínač `-StagingOnly` skončí po ověřeném stagingu, ZIP a ověření rozbalené kopie v `work/distribution-<id>` a vypíše jejich cesty. `dist` ani `dist.zip` nemění. Slouží ke zkušebnímu sestavení a k testům před publikací.

Dílčí [client/tools/make-dist.ps1](client/tools/make-dist.ps1), [client/tools/linux/make-linux-dist.ps1](client/tools/linux/make-linux-dist.ps1) a [client/tools/macos/make-macos-dist.ps1](client/tools/macos/make-macos-dist.ps1) vyžadují `-OutputDirectory <cesta>` pod `work` nebo `dist`. Kořenový `dist` není povoleným cílem těchto dílčích nástrojů. Serverovou část a ostatní platformní složky vytváří výhradně celkový vstup.

Po sestavení spusť v odděleném vývojovém venv `python -B tools/test-server-scripts.py --distribution <rozbaleny-strom> --report <report.json>`. Runner vytváří vlastní kopie, porty a buffery, kontroluje Windows BAT nebo nativní POSIX SH vstupy, health/info/data, identitu, souběh, chyby, URL a nejméně třísekundové bannery. `server/python/tests/test_server_control.py` doplňuje deterministické případy recyklovaného PID, nedostupných oprávnění a timeoutu. Bootstrap bez uv a Pythonu se ověřuje zvlášť v čistém prostředí. Windows běh nenahrazuje Linux ani macOS, neprovedené platformy a ruční instalační testy se uvádějí v datovaném reportu.

Windows balíček se ověřuje také pomocí `client/tools/verify-package.ps1 -PackageDirectory <cesta>`. Tento read-only krok kontroluje shodu skriptů, povinnou přítomnost pěti EXE, manifest, podpisy, VerifyOnly a přesné vložené ICO resources všech pěti produkčních EXE včetně úrovní UAC. Vyžaduje sestavený `phpvpn-icon-resource-tests`, standardně z presetu `release` s `BUILD_TESTING=ON`. Jiný ověřovací build lze zadat přes `-IconResourceTest`. Čtyři zdrojové ICO se používají pouze při sestavení a ověřování, do provozního balíčku se samostatně nepřidávají.

Linuxovou složku ověřuje read-only `tools/verify-distribution.ps1`. Kontroluje přesný seznam souborů, shodu `install.sh`, `uninstall.sh`, `README.md` a licencí se zdroji, LF shellových skriptů, právě čtyři balíčky verze klienta se společným release a platnou hlavičkou RPM nebo DEB, velikost a SHA-256 každé položky manifestu a jeho pole (verze a IPC verze shodné s Windows balíčkem, právě Fedora a Ubuntu). Podpis ověří stejně jako Windows instalátor: `release-signing.cer` MUSÍ být shodný s Windows balíčkem, identita v manifestu i podpisu MUSÍ odpovídat certifikátu, veřejný klíč MUSÍ odpovídat pinu v `install.sh` a RSA PKCS#1 v1.5 SHA-256 podpis MUSÍ platit pro přesné bajty manifestu. Manifest se `sourceDirty` rovným `true` vyvolá varování, že nejde o vydání.

Na linuxových systémech se z rozbalené ověřené kopie dále provede `sh install.sh --verify-only` na Fedoře 44 a Ubuntu 24.04 (nevyžaduje root, nic neinstaluje) a na linuxovém hostiteli s podmanem kontejnerový instalační test obou distribucí:

```sh
cd client
sh tests/e2e/linux/package-install-container.sh fedora:44 <rozbalena-kopie>/client/Linux/x64 <slozka-release-2>
sh tests/e2e/linux/package-install-container.sh ubuntu:24.04 <rozbalena-kopie>/client/Linux/x64 <slozka-release-2>
```

Test ověřuje `--verify-only`, odmítnutí upravených a nezapsaných souborů, instalaci, běžící službu, `phpvpn status`, upgrade na vyšší release se zachováním konfigurace, balíček tray a odinstalaci s daty i bez nich. `<slozka-release-2>` je složka téže verze s vyšším release (balíčky sestavené s `PHPVPN_PACKAGE_RELEASE=2`, složené `make-linux-dist.ps1` pod `work`). Slouží pouze k testu upgrade a do distribuce nepatří. Instalace do skutečného systému, grafický tray a AppIndicator na Fedora GNOME se ověřují podle [client.md](client.md), neprovedené platformy a testy se uvádějí v datovaném reportu.

Status stránka je vložená přímo v `phpvpn.php`, `phpvpn.py`, `phpvpn.js` i v jádru WordPress pluginu. Žádná serverová distribuce nepotřebuje `assets/index.html`. PHP, Python a Node.js nepoužívají vzájemně čitelný diskový formát, výměna implementace nad rozpracovanými frontami není podporovaná migrace.

## distribuce zdrojových kódů do gitpilandrome

Tato kapitola je postup pro AI agenta při výslovném zadání zkopírovat zdrojovou
verzi projektu. Samotná úprava tohoto dokumentu distribuci nespouští.
Zdroj `D:\git\phpvpn` patří do cílového klonu `D:\gitpilandrome\phpvpn`
(remote `https://github.com/pilandrome/phpvpn.git`).

Pro tuto konkrétní operaci je autoritativní tato kapitola. Metodika
[create_source_code_distribution.md](create_source_code_distribution.md)
slouží jako podklad pro nalezení potřebných vstupů a závislostí. Nepřebírej z ní
cíl `C:\gitsigma`, rotaci `phpvpn_old` ani vyprázdnění cílového klonu.
Rozsah přenosu určuje nezbytnost podle pravidel níže. Anglické README a další
přibalené popisy a návody jsou zde povinné a jejich překlad je součástí zadání,
neuplatňuje se volitelný překlad s dalším souhlasem. Výslovně je povoleno zachovat
veřejné výchozí hodnoty PHPVPN potřebné ke spuštění: výchozí server secret,
výchozí tokeny, přístupové klíče a výchozí hesla. Tato výjimka zachovává současné
chování klienta a serverů, neopravňuje k přenosu skutečných lokálních tajemství.
Předchozí pravidla běžné binární distribuce v tomto dokumentu se nemění.

Zahrň potřebné zdroje klienta i serverů, build a instalační šablony, konfigurace,
prostředky, dependency locky a návody. Například `client/packaging/linux/dist`
obsahuje zdrojové vstupy a nelze jej vyřadit jen podle názvu `dist`.
Nepřebírej certifikáty a hotové DLL z binární distribuce. Dokumentuj získání
potřebných externích komponent a nastavení vlastní podpisové identity.
Pokud kompletní binární sestavení vyžaduje commit příjemce, uveď tento předpoklad
v README. Distribuční agent jej nesmí sám vytvořit v cílovém klonu ani nahrazovat
původní historií, chybějící ověření musí přiznat. Kořenový `readme.txt` určený
binárnímu packageru zachovej v jeho roli a pro zdrojovou kopii přidej anglický
`README.md`, pokud stávající soubor popisuje pouze instalaci hotových balíčků.

1. Nejdřív ověř skutečný zdroj, cílový Git remote a stav obou pracovních stromů.
   Název cílové složky neodvozuj automaticky od názvu zdroje. Kopíruj aktuální
   zamýšlený obsah včetně potřebných necommitnutých změn. Neměň zdrojovou větev,
   neprováděj reset, stash ani čištění. Při nejasném rozpracovaném obsahu vyjasni rozsah.
2. Sestav explicitní inventář nezbytných vstupů podle skutečných závislostí,
   včetně skrytých, nesledovaných a ignorovaných souborů. Zahrň jen zdroje,
   prostředky, konfigurace, generátory a skripty potřebné k sestavení nebo spuštění,
   dále README, potřebné návody a licence. Pokud je sestavení vyžaduje, zahrň také
   zdrojové testy a jejich bezpečné vstupy. Samotný seznam souborů v Gitu nestačí.
   Chybějící submodul nebo skutečný obsah Git LFS doplň, pouhý pointer nestačí.
3. Zachovej manifesty závislostí, existující lock soubory a potřebné licenční
   texty, copyright a NOTICE. Verze závislostí při kopírování neaktualizuj.
   Stažitelné závislosti nahraď dokumentovaným získáním konkrétní kompatibilní
   verze a ověřením integrity, nikoli kopií místní instalace nebo cache.
   Nejasná redistribuční práva vyřeš před předáním.
4. README a další přibalené popisy a návody musí být anglicky. Aktuální neanglické
   části při přípravě distribuční kopie přelož úplně do angličtiny, původní
   dokumenty neměň. Zachovej příkazy, identifikátory a význam, oprav a ověř odkazy.
   Povinné licenční texty svévolně nepřekládej. README musí uvést předpoklady,
   verze nástrojů, instalaci závislostí a přesné příkazy pro sestavení a spuštění.
5. Vnitřní cesty musí být relativní a fungovat po přesunutí celého projektu.
   Žádná část nesmí potřebovat absolutní cestu původního počítače ani soubor mimo
   předaný projekt. To zahrnuje konfigurace, skripty, prostředky a odkazy přes
   symlinky či junctiony. Dokumentované standardní nástroje a instalovatelné
   závislosti jsou předpoklady prostředí, ne vazba na autorovu lokální instalaci.
   Cesty zdroje a cíle uvedené zde slouží pouze pro kopírování.
6. Nekopíruj žádné certifikáty, soukromé klíče ani skutečná lokální hesla,
   přístupové tokeny, API klíče a jiné tajné údaje, a to ani uvnitř zdrojů,
   dokumentace či archivů. Výjimkou jsou pouze veřejné výchozí hodnoty PHPVPN
   potřebné ke spuštění podle vymezení výše. U každé ponechané hodnoty ověř její
   původ v aktuální výchozí implementaci a zaznamenej jej v reportu. Výjimku
   nerozšiřuj na jiné hodnoty stejného pole nebo souboru a nikdy na certifikáty
   či soukromé klíče. Prohlédni obsah, nestačí filtr přípon. Přibal bezpečné
   konfigurační šablony a popiš nahrazení veřejných výchozích hodnot vlastními
   údaji před skutečným nasazením.
   Pokud jejich odstranění naruší funkčnost, vyřeš přenositelnou konfiguraci
   a ověř ji před předáním, neobcházej bezpečnostní kontroly.
7. Vynech sestavené programy a balíčky, build a testovací výstupy, cache,
   virtuální prostředí, `node_modules`, logy, provozní data, zálohy a pracovní
   poznámky. Rozhoduj podle účelu, nikoli jen názvu adresáře nebo přípony.
   Nutné zdrojové obrázky, modely a textové spouštěcí skripty nejsou build odpad.
8. Připrav vybraný obsah v nové dočasné složce mimo zdroj i cílový klon.
   Nikdy nepřenášej zdrojové `.git` a nemaž ani nepřepisuj cílové `.git`.
   Před zápisem do cíle porovnej konkrétní soubory a znovu ověř jeho stav.
   Zachovej existující nesouvisející i rozpracované změny. Při kolizi vyžádej
   rozhodnutí o konkrétním souboru. Nepoužívej plošné mazání ani zrcadlení cíle.
   Odstranění prokazatelně zastaralých distribuovaných souborů řeš jednotlivě
   podle inventáře, nikdy podle předpokladu, že cílový klon je prázdný.
9. Úplnost a přenositelnost ověř z čisté kopie v jiné cestě podle anglického README,
   bez čtení zdrojového stromu a bez původních cache či prostředí. Ověř sestavení
   a základní spuštění aplikace, u modelů otevření a funkční ovládání s potřebnými
   texturami. Výstupy ověření ukládej mimo předávaný obsah. Předání zakonči
   kontrolou cílového diffu, inventáře, odkazů a nepřítomnosti zakázaných údajů.
   Uveď cílovou cestu, změněné soubory, provedené i neprovedené kontroly a překážky.
   Bez samostatného pokynu nevytvářej commit, push, release ani jinou publikaci.
