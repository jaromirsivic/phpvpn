---
intent_version: 1
created_at: 2026-09-19-08-14-34
source: intent-chat-v001.md
---

# Automatický start, automatické připojení a ikony Windows aplikace

## Shrnutí

Rozšířit Windows klienta PHPVPN o tři související oblasti:

1. Tray aplikaci bezpečně registrovat pro automatické spuštění při přihlášení uživatele do Windows a registraci odstranit při odinstalaci.
2. Přidat do `masterSetup` konfigurační volbu `autoConnectOnStartup`, která při hodnotě `true` po startu služby zahájí běžné připojení.
3. Použít existující ICO soubory z kořenové složky `assets` jako aplikační a stavové ikony.

## Kontext projektu

Následující body jsou zjištěné skutečnosti o aktuálním projektu, nikoli nové požadavky uživatele:

- Produkčními Windows procesy jsou služba a tray aplikace.
- Instalaci a odinstalaci služby obsluhují příkazy `phpvpn-service.exe --install` a `phpvpn-service.exe --uninstall` v `client/apps/service/main.cpp`; vlastní SCM operace jsou v `client/apps/service/service_host.cpp`.
- Výchozí konfiguraci vytváří `client/src/core/config/config_defaults.cpp`. Povolené členy a jejich typy ověřuje `client/src/core/config/config_validator.cpp`. Wizard pracuje s dokumentem v `client/src/core/config/config_document.cpp` a reset používá nový výchozí dokument.
- Start služby, vytvoření `ConfigStore` a start `VpnEngine` probíhají v `client/apps/service/service_host.cpp`.
- Tray již rozlišuje stavy `disconnected`, `connecting`, `connected`, `recovering` a `error`. Aktuální mapování stavových ikon je v `client/apps/tray/tray_application.cpp` a načítání prostředků v `client/apps/tray/ui_resources.cpp`.
- Požadované zdrojové soubory existují jako `assets/default.ico`, `assets/connected.ico`, `assets/connecting.ico` a `assets/disconnected.ico`.

## 1. Automatický start tray aplikace

### Funkční požadavky

- Úspěšná instalace aplikace má vytvořit strojovou registraci, která spustí tray aplikaci pro každého interaktivně přihlášeného uživatele při jeho přihlášení do Windows.
- Tray aplikace se nesmí spouštět v servisní relaci ani pod účtem služby.
- Odinstalace musí odstranit pouze vlastní registraci automatického startu PHPVPN.
- Opakovaná instalace i odinstalace musí být idempotentní.
- Registrovaný příkaz musí používat absolutní, správně citovanou cestu k nainstalovanému `phpvpn-tray.exe` a nesmí umožnit záměnu spustitelného souboru nebo argumentů.

### Umístění a životní cyklus

- Registraci vlastnit ve stejném autoritativním instalačním a odinstalačním toku jako službu, aby ji používala balíčková instalace i přímé příkazy `--install` a `--uninstall`.
- Pro strojový start při přihlášení použít standardní 64bitovou hodnotu v `HKLM\Software\Microsoft\Windows\CurrentVersion\Run` s jednoznačným názvem PHPVPN.
- Při instalaci nejprve zachovat současnou kritickou instalaci služby. Registrace tray autostartu je doplňkový krok.

### Bezpečné selhání

- Selhání zápisu registrace automatického startu nesmí změnit úspěšnou instalaci služby na neúspěšnou instalaci celého produktu.
- Selhání odstranění registrace nesmí zablokovat odstranění služby ani dokončení odinstalace.
- Doplňkové selhání musí být viditelně zaznamenáno jako varování bez citlivých údajů. Příkaz může skončit úspěšně, pokud kritická část instalace nebo odinstalace uspěla.
- Chyba registrace nesmí ponechat neplatnou nebo částečně sestavenou hodnotu.

## 2. `autoConnectOnStartup`

### Konfigurační kontrakt

- Přidat boolean `masterSetup.autoConnectOnStartup`.
- V serializovaném výchozím dokumentu bude umístěn jako poslední člen `masterSetup`, bezprostředně za `useDefaultAccessKeyInWizard`.
- Výchozí hodnota je `false`.
- Výchozí konfigurace vytvořená při prvním startu nebo instalaci musí pole obsahovat.
- Konfigurace vytvořená resetem ve wizardu musí pole obsahovat s hodnotou `false`.
- Uložení nebo dokončení wizardu musí existující hodnotu zachovat. Pokud starší dokument pole neobsahuje, výstup wizardu jej doplní s hodnotou `false`.
- Starší konfigurace bez pole zůstává platná a chová se jako `false`.
- Přítomná neboolean hodnota je validační chyba stejným způsobem jako u ostatních boolean členů `masterSetup`.
- Tato změna sama o sobě nepřidává nové zaškrtávací políčko do wizardu. Volba je součástí konfiguračního dokumentu.

### Chování služby

- Při `false` zůstane současné chování po startu beze změny a služba sama připojení nezahájí.
- Při `true` služba po inicializaci úložiště konfigurace a VPN enginu bez zbytečného odkladu spustí stejnou připojovací operaci, jakou používá běžný příkaz Connect.
- Chování platí při startu služby, tedy po restartu operačního systému i po ručním restartu služby.
- Automatické připojení nesmí obcházet validaci konfigurace, bezpečnostní kontroly, stavový model, retry/recovery logiku ani existující ochranu proti souběžným connect operacím.
- Neúspěšný pokus nesmí shodit ani ukončit službu. Musí se projevit standardním stavem `error` nebo `recovering`, být dostupný přes současné status rozhraní a být bezpečně zalogován.
- Neplatná nebo nedostupná konfigurace nesmí blokovat dokončení startu služby; automatické připojení selže standardní chybovou cestou a služba zůstane ovladatelná.
- Změna hodnoty se projeví při příštím startu služby. Není požadován nový mechanismus živého sledování konfigurace.

## 3. Aplikační a stavové ikony

### Zdrojové soubory

- Použít přesně tyto soubory:
  - `assets/default.ico`
  - `assets/connected.ico`
  - `assets/connecting.ico`
  - `assets/disconnected.ico`
- Ikony vložit do Windows resources při sestavení. Nainstalovaný program nesmí být závislý na samostatných ICO souborech vedle EXE.

### Výchozí aplikační ikona

- `default.ico` bude ikonou všech dodávaných produkčních Windows EXE souborů, zejména `phpvpn-service.exe` a `phpvpn-tray.exe`, aby byla viditelná v Průzkumníku Windows a systémových vlastnostech souboru.
- `default.ico` bude nastavena jako velká i malá ikona všech top-level oken a dialogů tray aplikace, včetně levého horního rohu a přepínače oken.
- Stavová ikona nesmí nahrazovat aplikační ikonu formuláře. Stavové ikony patří do notification area.

### Mapování tray ikony

- `connected.ico`: pouze zdravý stav `connected`.
- `connecting.ico`: připojování, odpojování a stav `recovering`.
- `disconnected.ico`: stav `disconnected`, stav `error` a stav nedostupné nebo neplatné status odpovědi.
- Při lokálně probíhajícím odpojování musí tray ukázat `connecting.ico`, i když poslední autoritativní snapshot ještě hlásí `connected`.
- Po dokončení nebo selhání operace se ikona přepne podle nejnovějšího autoritativního stavu.
- Zachovat korektní výběr velikosti ikony pro DPI a notification area.

## Dotčené oblasti

Realizace pravděpodobně zasáhne zejména:

- `client/apps/service/main.cpp`
- `client/apps/service/service_host.cpp` a jeho rozhraní
- balíčkové instalační a odinstalační skripty v `client/tools/package/` a související ověřovací skripty
- `client/src/core/config/config.h` nebo příslušný datový model konfigurace
- `client/src/core/config/config_validator.cpp`
- `client/src/core/config/config_defaults.cpp`
- `client/src/core/config/config_document.cpp`
- startovací tok služby v `client/apps/service/service_host.cpp`
- `client/apps/tray/tray_application.cpp`
- `client/apps/tray/ui_resources.*`, `.rc` resources a CMake definice produkčních EXE
- testy konfigurace, wizardu, služby, tray UI, resources a balíčkové instalace.

Přesný seznam souborů má realizátor potvrdit podle aktuálního grafu závislostí a build systému.

## Nefunkční požadavky

- Zachovat bezpečnostní hranici mezi privilegovanou službou a uživatelskou tray aplikací.
- Nevytvářet proces tray aplikace ze služby.
- Zachovat zpětnou kompatibilitu existujících konfigurací.
- Nevnášet blokující práci do startu služby ani do hlavního UI vlákna tray aplikace.
- Všechny zápisy do registru, práce s cestami a resource identifikátory musí mít deterministické, testovatelné chování.
- Instalace a odinstalace musí být bezpečné při opakování a po částečném selhání doplňkového kroku.

## Akceptační kritéria

1. Po instalaci existuje jediná správně citovaná strojová autostart registrace a při přihlášení běžného uživatele se spustí `phpvpn-tray.exe` v jeho interaktivní relaci.
2. Opakovaná instalace nevytváří duplicitní položky. Odinstalace vlastní položku odstraní a opakovaná odinstalace nevytváří vedlejší škody.
3. Simulované selhání zápisu nebo odstranění autostartu nezabrání úspěchu kritické instalace nebo odinstalace služby a vytvoří ověřitelné varování.
4. Nová i wizardem resetovaná konfigurace obsahuje na konci `masterSetup` položku `"autoConnectOnStartup": false` hned za `useDefaultAccessKeyInWizard`.
5. Starší konfigurace bez položky se načte jako `false`; explicitní `true` a `false` se načtou správně; jiný typ je odmítnut.
6. Wizard zachová explicitní hodnotu a při uložení starší konfigurace doplní chybějící hodnotu `false`.
7. Při `false` služba po startu sama Connect nevyvolá.
8. Při `true` a platné konfiguraci služba po startu přejde standardní cestou přes `connecting` do `connected`.
9. Při `true` a neúspěšném připojení služba zůstane spuštěná a dostupná, zatímco status a log odpovídají standardnímu `error` nebo `recovering` chování.
10. Produkční EXE soubory i všechna top-level okna používají `default.ico`.
11. Tray ikona používá přesné mapování uvedené výše, včetně `connecting.ico` během odpojování a obnovování.
12. Sestavení a balíček nevyžadují přítomnost samostatných ICO souborů za běhu.
13. Automatizované testy pokrývají nové konfigurační chování, start služby, bezpečné selhání registrace, mapování ikon a resource integraci.
14. Stávající unit, UI, E2E, resource a packaging testy zůstávají úspěšné.

## Realizační vodítka

- Autostart držet jako malou, samostatně testovatelnou Windows platformní operaci. Kritický výsledek instalace služby a doplňkový výsledek autostartu vyhodnocovat odděleně.
- Pro automatické připojení znovu použít veřejnou doménovou operaci enginu, nevytvářet druhou implementaci connect sekvence.
- Konfigurační hodnotu přenést přes validační a dokumentový model tak, aby serializace zachovala pořadí a starší dokumenty získaly bezpečný default.
- ICO soubory vložit přes `.rc` resources a existující CMake resource tok. Stavové a aplikační ikony držet odděleně.
