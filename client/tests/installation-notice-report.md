# Oznámení po instalaci a oprava distribuce PHPVPN

Ověřeno 26. září 2026 podle intent-v002 a následného hlášení instalace ze starého `dist`. Práce proběhla přímo ve větvi `codex/python-server`, bez worktree. Historické intenty nebyly změněny.

## Příčina

Uživatel instaloval z `C:\git\phpvpn\dist\client\Windows\x64`. Tato cesta obsahovala starou distribuci z 25. září, protože předchozí publikování nedokázalo přejmenovat zamčený adresář. Nový balík tehdy skončil pod časovým názvem vedle.

Starý tray v `dist` i nainstalovaný `C:\Program Files\phpvpn\1.0.0\phpvpn-tray.exe` měly SHA-256 `4852150E230F8B8C564A6CC57A3BAACBC5E1A17DE4D7AC64A44F7E579813EFE6`. Starý instalační skript neměl parametr `--installation-complete`. Běžící tray skutečně pocházel z Program Files.

Chybějící oznámení i stará šířka HTTP headers odpovídaly starému balíku. Změna šířky z 760 na 950 byla v aktivní větvi zachovaná.

## Implementace

- Instalátor předává `--installation-complete` po registraci a spuštění služby a ověření řídicí pipe. Běžný start ani autostart parametr nepředává.
- Tray předá požadavek existující instanci přes pojmenovanou událost. Druhá instance nevytváří ikonu. Předání funguje i během inicializace. Nevzniká trvalý příznak prvního startu.
- Windows thread pool čeká bez pravidelného probouzení. Callback pouze zařadí zprávu do UI vlákna. Čekání se odregistruje před zánikem okna.
- `ShellIcon` používá nativní `Shell_NotifyIconW`, `NIF_INFO`, `NIIF_INFO` a `NIIF_RESPECT_QUIET_TIME`. Změny stavu ikony oznámení neopakují. Vlastní bublina ani časovač délky zobrazení se nepoužívají.
- `client/src/core/presentation/installation_notice.h` obsahuje schválený text v UTF-8 a rozhraní `InformationNoticePresenter` bez závislostí na Windows. Budoucí Linux/macOS porty mohou doplnit nativní adaptéry. Nynější klient zůstává pouze pro Windows.
- `phpvpn-service.exe --install` pouze registruje službu. Dokončení celé instalace oznamuje balíčkový instalátor až po dalších úspěšných krocích.

## Oprava publikování

`tools/make-distribution.ps1` nyní používá `Publish-Distribution` ze `tools/distribution-common.ps1`. Zachová adresáře a jejich otevřené handles, vymění distribuční soubory a při chybě obnoví staré soubory i ZIP. Po úspěšném ověření svého výsledku odstraní dočasný rollback.

`dist/server/Python/.runtime` a `.venv` jsou z výměny vyloučené a zůstávají na místě. Porovnání původních SHA-256 potvrdilo beze změny všech 312 souborů. Čistý ZIP je neobsahuje. Ověření pracovní složky používá explicitní `-AllowLocalRuntime`, staging a rozbalený ZIP se ověřují striktně.

## Skutečná instalace

Spuštěn přímo `C:\git\phpvpn\dist\client\Windows\x64\install-phpvpn.exe`. Instalátor dokončil aktualizaci i následnou opakovanou instalaci. Skutečné okno potvrdilo `Installation completed` a `PowerShell exit code: 0`.

Při opakované instalaci snímek z 08:23:48 Europe/Berlin zachytil skutečné nativní oznámení `PHPVPN installed` s celým schváleným textem. Oznamovací oblast s ikonou nebyla rozbalená.

Nainstalovaný tray, finální `dist` a Release build nyní mají shodný SHA-256:

`B1CDE21E60933BC0A63589E6FF84296D69112EBBD48E20FCA4C669F9E6AF3007`

Důkazy jsou v `work/installation-real-flow/`: `2026-09-26-06-22-51-monitor-01.png` zachycuje dokončený instalátor a `2026-09-26-06-23-48-monitor-01.png` oznámení. Snímky obsahují také okolní uživatelovu plochu.

## Ověření

- Debug i Release sestavení úspěšná, MSVC x64, Windows 11 Pro build 22631.
- Konečný Release CTest: všech devět samostatných sad prošlo, včetně 450 klientských testů, 34 scénářů instalátoru a BAT vstupů a šesti scénářů publikování. Log: `work/installation-final-ctest.log`.
- Sedm cílených testů oznámení pokrývá běžný start, první i opakovanou instalaci, běžící a inicializovanou instanci, absenci druhé ikony, UI vlákno a teardown, chyby inicializace, limity textu, aktualizace ikony a nezávislý adaptér.
- Testy publikování používají skutečné zámky Windows: nový adresář, výměna existující distribuce, zamčený kořen, podadresář, soubor a ZIP. Při chybách se obnoví původní soubory a runtime zůstane na místě.
- Nativní test HTTP headers prošel včetně 112 změn velikosti. Počáteční plocha měla 1425 × 630 pixelů při DPI 144, tedy 950 × 420 při 100 %. Log: `work/installation-http-headers.log`. Stejný zdroj je součástí ověřeného Release buildu a nainstalovaného binárního souboru.
- Nativní probe navíc ověřil `NIN_BALLOONSHOW` s ikonou skrytou pomocí `NIS_HIDDEN`.
- `git diff --check` prošel.

První Debug běh obsahoval také `phpvpn-contract-tests`. Pět jeho případů selhalo s `transport_connect_failed`, protože testovací PHP relay nebyl dostupný. Konečný Release běh externí cíl výslovně vynechal pomocí `-LE real-php`. Nejde o úspěšné ověření PHP integrace.

Viditelnost a dobu oznámení řídí Windows. Zakázaná oznámení, Nerušit nebo systémové zásady je mohou potlačit. Aplikace tato nastavení nemění. Viz [Microsoft](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/ns-shellapi-notifyicondataw).

## Konečná distribuce

- Složka: `C:\git\phpvpn\dist`
- Archiv: `C:\git\phpvpn\dist.zip`
- Instalační EXE: `C:\git\phpvpn\dist\client\Windows\x64\install-phpvpn.exe`

Staging, rozbalený ZIP i konečná kanonická složka prošly ověřením souborů, shody se zdroji, hashů, velikostí, podpisu manifestu, architektury, Wintun, ikon a `install.bat -VerifyOnly`. Manifest používá stávající certifikát `CN=phpvpn Development Test Signing`. Log: `work/installation-canonical-final.log`.

SHA-256 `dist.zip`: `2EF486FA1B4585B82B2EC15574BB8D9C9DAFB1BB655231E8E7F7B7962E12E864`.

## Zbývající dočasné kopie

Poslední distribuční běh svůj rollback odstranil. Úklid starších pracovních adresářů však automatická schvalovací kontrola nástroje odmítla s důvodem `blocked by policy`, i při explicitních cestách. Zůstávají:

- `C:\git\phpvpn\work\distribution-2e72709e56a542d5955fe810fd3a15df`: rozbalený balík.
- `C:\git\phpvpn\work\distribution-c6153ba2ba084601a3f41b6d1c027ff9`: staging, rozbalený balík, ZIP a prázdné adresáře neúspěšného pokusu.
- `C:\git\phpvpn\work\distribution-3cbed67dc4b443fbae704c2d32fc2350`: staging, rozbalený balík a starší rollback v `previous-dist` a `previous-dist.zip`.
- `C:\git\phpvpn\work\distribution-30d43c391ba14af296d72fc508ced644`: staging a rozbalený balík, bez rollbacku.

Nejde o používanou distribuci. Původní `.runtime` a `.venv` zůstaly beze změny přímo v `dist`.