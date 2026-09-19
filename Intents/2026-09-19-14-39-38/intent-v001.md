---
intent_version: 1
created_at: 2026-09-19-14-52-07
source: intent-chat-v001.md
---

# Intent: reconnect po změně konfigurace, úplnější Server Information a GUI instalátory

## Shrnutí

Upravit desktopového klienta phpvpn ve čtyřech souvisejících oblastech:

1. Po uložení Communication Contracts nabídnout odpojení a automatické opětovné připojení.
2. Stejné chování použít po uložení Edit Config, ale jen pokud je klient právě připojený.
3. V Server Information zobrazit celkový počet online klientů i počet online klientů s platným kontraktem.
4. Přidat dvě malé nativní EXE aplikace pro instalaci a odinstalaci, které s administrátorskými právy spustí stávající PowerShell skripty a srozumitelně zobrazí jejich průběh a výsledek.

## Kontext projektu

- Potvrzení po uložení Communication Contracts je v `client/apps/tray/contracts_window.cpp`, metoda `ContractsWindow::finishSavedStatus` (`L319-L344`). Dnes nabízí pouze odpojení a před akcí ověřuje identitu stále stejného připojení.
- Potvrzení po uložení Edit Config je v `client/apps/tray/editor_window.cpp`, metoda `EditorWindow::finishSave` (`L85-L108`). Dnes vždy zobrazí text o příštím pokusu o připojení a následně editor zavře.
- Stav připojení a příkazy Connect/Disconnect spravuje `client/apps/tray/tray_application.cpp`, zejména `TrayApplication::finishCommand` (`L290-L352`) a `TrayApplication::finishDisconnect` (`L445-L460`).
- Model Server Information je v `client/src/core/presentation/server_info_model.h` (`L11-L15`) a `server_info_model.cpp`, funkce `buildServerInfoModel` (`L18-L49`). Dnes počítá jen `validOnlineClients`. Text se vykresluje v `client/apps/tray/server_info_window.cpp`, metoda `ServerInfoWindow::display` (`L236-L294`).
- Balíček již obsahuje `client/tools/package/install.ps1`, `uninstall.ps1` a odpovídající BAT spouštěče. Skripty používají nulový návratový kód pro úspěch, nenulový pro odmítnutí nebo chybu a samy provádějí potřebné instalační či odinstalační kontroly.
- Produkční nativní aplikace se sestavují v `client/apps/CMakeLists.txt`. Distribuci skládá `client/tools/make-dist.ps1`. Požadované ikony existují jako `assets/connected.ico` a `assets/disconnected.ico`.

## Funkční požadavky

### 1. Communication Contracts: Disconnect and Reconnect

- Po úspěšném uložení změn za stavu Connected zobrazit přesně tento anglický text:

  `Your changes have been saved. It is strictly required that you disconnect and connect again to apply them. Do you want to disconnect and reconnect now?`

- Nadpis dialogu může zůstat `Configuration saved`.
- Volba **No** pouze ukončí dialog a běžné ukládací sezení. Uložené změny se nevracejí a připojení se nemění.
- Volba **Yes** zahájí bezpečné odpojení současného připojení.
- Nový Connect se smí vyvolat až poté, co služba autoritativně potvrdí stav Disconnected/Idle pro odpojované připojení.
- Následný Connect musí použít stejnou aplikační cestu jako ruční kliknutí uživatele na Connect, včetně stejného průběhového okna, aktualizace ikony, zpracování chyb a čerstvého načtení konfigurace.
- Pokud odpojení selže, je neplatně potvrzené nebo se mezitím změnila identita připojení, automatický Connect se nesmí spustit. Uživatel musí dostat stávající bezpečné chybové nebo stavové vyhodnocení.
- Automatické znovupřipojení je jednorázový záměr navázaný na konkrétní potvrzenou ukládací operaci. Nesmí se omylem přenést do pozdější ruční operace.

### 2. Edit Config: stejné chování podle stavu připojení

- Po úspěšném potvrzeném uložení konfigurace zjistit autoritativní aktuální stav připojení.
- Pokud je klient Connected, zobrazit stejný dialog, stejný text a stejnou logiku Yes/No jako u Communication Contracts.
- **Yes** provede bezpečné Disconnect a po jeho potvrzení automatický Connect stejnou cestou jako ruční Connect.
- **No** ponechá konfiguraci uloženou, neprovede žádnou změnu připojení a editor zavře.
- Pokud klient není Connected, žádný úspěšný informační ani reconnect dialog se nezobrazí. Editor se po potvrzeném uložení tiše zavře.
- Dosavadní text `The configuration was stored. It takes effect on the next connection attempt.` se z tohoto úspěšného toku odstraní.
- Chybové, odmítnuté, neodeslané a neurčité výsledky ukládání zůstanou odlišeny a nesmějí se vydávat za úspěch.
- Řešení má sdílet reconnect orchestrace nebo její bezpečnou společnou abstrakci s Communication Contracts, aby oba vstupy neměly rozdílné závodní podmínky či chování.

### 3. Server Information: celkově online a validní

- Model rozšířit o celkový počet online vzdálených klientů. Online je každý vzdálený klient, který podle stávajícího pravidla posledního kontaktu není Offline, bez ohledu na platnost kontraktu.
- Dosavadní `validOnlineClients` zachovat jako podmnožinu online klientů s platným odpovídajícím komunikačním kontraktem.
- Do souhrnu v okně i titulku zobrazit oba údaje. Zvolený formát je například:

  - `(4 online clients, 1 valid contract)`
  - `(1 online client, 1 valid contract)`
  - `(0 online clients, 0 valid contracts)`

- Použít správné anglické jednotné a množné číslo pro `client/clients` a `contract/contracts`.
- Počty nezahrnují lokální adresu, stejně jako ji dnes nezahrnují řádky modelu.
- Seznam, stránkování a klasifikace řádků na Valid, Invalid a Offline zůstávají zachované.

### 4. Nativní instalační a odinstalační miniaplikace

#### Výstupy a balení

- Přidat dva samostatné malé nativní Windows GUI programy. Doporučené konkrétní názvy výstupů jsou `install-phpvpn.exe` a `uninstall-phpvpn.exe`.
- Oba programy zahrnout do distribučního balíčku vedle stávajících BAT a PowerShell skriptů. Stávající vstupní body se neruší.
- Instalační EXE používá `assets/connected.ico`, odinstalační EXE používá `assets/disconnected.ico` jako ikonu souboru i hlavního okna.
- Oba programy musí mít aplikační manifest s požadavkem `requireAdministrator`, aby se UAC zobrazilo při spuštění EXE a podřízený PowerShell už další zvýšení oprávnění nevyžadoval.

#### Spuštění PowerShellu a smlouva o výsledku

- Instalační aplikace spustí `install.ps1`, odinstalační aplikace `uninstall.ps1`, vždy ze stejného distribučního adresáře jako příslušné EXE a s bezpečně sestavenými argumenty.
- Zachytávat a průběžně zobrazovat standardní i chybový výstup PowerShellu. Výstup nesmí být určován parsováním konkrétních vět.
- Autoritativní smlouvou mezi GUI a PowerShellem je návratový kód procesu: `0` znamená, že skript dokončil své vlastní validační kroky a operace uspěla, nenulový kód znamená neúspěch.
- Toto rozdělení odpovědností musí zůstat kompatibilní s budoucím rozšiřováním skriptů. Validace instalace a odinstalace zůstává ve skriptech, GUI pouze spouští správný skript, zobrazuje celý výstup a vyhodnotí jeho ukončení.
- Neočekávané selhání při nalezení nebo spuštění skriptu, zachytávání výstupu či čekání na proces je neúspěch a musí se viditelně popsat v textboxu.
- Standardní odinstalace zachová uživatelská data podle dnešního výchozího chování `uninstall.ps1`. Existující volitelný režim `-RemoveData` není součástí požadovaného základního GUI toku.

#### Formulář a vizuální stavy

- Formulář se při spuštění vycentruje do pracovní plochy monitoru, který je v okamžiku startu aktivní nebo obsahuje kurzor/aktivní okno. Musí správně respektovat více monitorů a DPI.
- Výchozí klientská velikost má být přibližně `760 × 520` logických pixelů. Minimální velikost má být přibližně `520 × 360` logických pixelů.
- Horní maximální velikost se neomezuje. Okno lze maximalizovat na pracovní plochu monitoru.
- Uvnitř je roztažitelný read-only multiline textbox s vertikálním i horizontálním posuvem a průběžným výstupem skriptu.
- Mezi textboxem a okrajem klientské plochy ponechat nejméně 20 logických pixelů stavové plochy na všech stranách.
- Během operace je okolní plocha žlutá až jantarová. Po návratovém kódu `0` zezelená. Po nenulovém kódu nebo chybě spuštění zčervená.
- Titulek instalačního okna během běhu je `Installation in progress...`; odinstalačního `Uninstallation in progress...`.
- Po dokončení se titulek změní na jednoznačný anglický výsledek, například `Installation completed`, `Installation failed`, `Uninstallation completed` nebo `Uninstallation failed`.
- Formulář se po dokončení automaticky nezavírá. Uživatel musí mít možnost přečíst celý výstup a zavřít jej sám.
- Běh PowerShellu a čtení jeho výstupu nesmí blokovat překreslování, změnu velikosti ani odezvu GUI.

## Omezení a zásady

- Intent neautorizuje realizaci. Nejprve se musí zvolit přímá realizace nebo samostatné plánování.
- Zachovat stávající bezpečnostní kontroly identity připojení a generační ochrany oken. Reconnect nesmí použít zastaralý stav.
- Nezavádět druhou implementaci instalační nebo odinstalační logiky do EXE. PowerShell skripty zůstávají jediným zdrojem pravdy pro vlastní operaci a její validaci.
- Nepoužívat textový obsah výstupu jako indikátor úspěchu.
- Stávající BAT a PowerShell skripty zůstávají dostupné pro automatizaci a kompatibilitu.
- Nové GUI, texty a ovládání mají dodržet existující nativní Win32 a přístupnostní konvence projektu.

## Pravděpodobně dotčené oblasti

- `client/apps/tray/contracts_window.*`
- `client/apps/tray/editor_window.*`
- `client/apps/tray/tray_application.*`
- případná nová sdílená reconnect orchestrace v `client/apps/tray/`
- `client/src/core/presentation/server_info_model.*`
- `client/apps/tray/server_info_window.*`
- jednotkové a UI testy pro uvedené modely a okna
- nové zdrojové soubory instalačního GUI pod `client/apps/`
- `client/apps/CMakeLists.txt`
- nové resource/manifest soubory a ikony nových cílů
- `client/tools/make-dist.ps1`
- `client/tools/package/install.ps1` a `uninstall.ps1` pouze tehdy, pokud je nutné zpřesnit stabilní návratovou smlouvu nebo přesměrování výstupu
- testy balíčku v `client/tests/`

## Akceptační kritéria

1. Communication Contracts po uložení za aktivního připojení používá nový text s „disconnect and reconnect“.
2. Po volbě Yes se nejprve prokazatelně dokončí bezpečné odpojení a teprve potom se spustí běžný Connect. Volba No spojení nemění.
3. Edit Config po úspěšném uložení za aktivního připojení nabízí tentýž tok a po uložení ve stavu odpojeno se tiše zavře.
4. Selhání nebo neurčitý výsledek uložení, odpojení či připojení se nikdy nezobrazí jako úspěch a nespustí neplatný další krok.
5. Server Information ukazuje celkový počet online vzdálených klientů a počet validních kontraktů se správnou pluralizací, včetně nulových hodnot.
6. Distribuce obsahuje obě nová EXE, správné skripty i původní BAT vstupy.
7. Každé EXE vyvolá UAC jen pro sebe, spustí správný PowerShell se zděděnými administrátorskými právy a živě zobrazuje stdout i stderr.
8. Návratový kód `0` nastaví zelený úspěšný stav. Nenulový kód nebo chyba spuštění nastaví červený stav. Během práce je stav žlutý/jantarový.
9. Okno má nejméně 20px viditelný stavový okraj, rozumnou výchozí a minimální velikost, lze je maximalizovat a na více monitorech se otevře uprostřed aktivního monitoru.
10. Po dokončení zůstane okno otevřené, dokud je uživatel sám nezavře.
11. Automatické testy pokrývají počítání a pluralizaci Server Information, větvení reconnect toku, potlačení dialogu při odpojeném Edit Config, exit-code smlouvu GUI obalu a obsah distribučního balíčku.
