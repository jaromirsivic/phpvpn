# 015 - Ostré systémové dialogy v celé tray aplikaci

## Cíl

Nahradit všechny produkční dialogy tray aplikace založené na klasickém Win32 `MessageBoxW` modernějším systémovým dialogem, který při podporovaných hodnotách DPI zobrazuje ostré a vizuálně kvalitní ikony.

Uživatel upozornil především na rozmazanou informační ikonu v potvrzení `Configuration saved`. Audit ale prokázal, že nejde o vlastní kreslení ani o izolovanou chybu jednoho okna. Tray aplikace obsahuje 24 produkčních volání klasického `MessageBoxW` a nepoužívá žádný moderní Task Dialog. Stejný problém se proto může projevit u informačních, varovných, chybových i potvrzovacích dialogů v celé aplikaci.

Referenční snímek uživatele je uložen v souboru `reference-blurry-messagebox-icon.png` v této složce.

## Zjištěný stav

- Potvrzení po dokončení Configuration Wizardu volá přímo `MessageBoxW` s příznakem `MB_ICONINFORMATION`.
- Aplikace tento dialog sama nekreslí ani neemuluje.
- Proces má v manifestu zapnuté Per-Monitor DPI v2, takže příčinou není chybějící základní DPI deklarace aplikace.
- Klasický Win32 MessageBox používá staré systémové rastrové ikony, které mohou být při vyšším škálování Windows viditelně rozmazané.
- V produkčním kódu tray aplikace je 24 přímých volání `MessageBoxW` a žádné použití `TaskDialog` ani `TaskDialogIndirect`.

## Rozsah

Úprava zahrnuje všechny produkční message boxy v `client/apps/tray`, zejména výskyty v:

- `main.cpp`,
- `tray_application.h` a `tray_application.cpp`,
- `wizard_window.cpp`,
- `editor_window.cpp`,
- `contracts_window.cpp`.

Je nutné zahrnout všechny současné významové varianty:

- informační oznámení,
- varování,
- chybová hlášení,
- potvrzovací otázky s tlačítky Yes a No,
- dialogy s výchozí bezpečnou volbou No,
- dialogy vlastněné konkrétním oknem i startovací chybu bez vlastníka.

Audit výskytů zopakovat bezprostředně před implementací, aby nebylo vynecháno žádné produkční volání přidané nebo přesunuté souběžnou prací.

## Požadované řešení

- Zavést jeden společný, znovupoužitelný helper pro moderní systémové dialogy tray aplikace.
- Helper má být postavený na nativním Windows Task Dialog API, přednostně `TaskDialogIndirect`, pokud to usnadní přesné zachování chování.
- Všechna produkční volání `MessageBoxW` v tray aplikaci převést na tento společný mechanismus.
- Pro informace, varování a chyby použít odpovídající ostré systémové ikony Task Dialogu.
- Potvrzovací otázky nesmějí používat rozmazanou legacy question-mark ikonu. Použít vhodnou moderní prezentaci Task Dialogu bez nekvalitní rastrové ikony. Nevytvářet vlastní napodobeninu starého MessageBoxu.
- Zachovat nativní systémový charakter dialogů. Nevytvářet samostatný rozsáhlý custom-painted framework ani vlastní emulaci systémového dialogu.
- Společný helper navrhnout tak, aby další dialogy nebylo nutné implementovat opakovaným nízkoúrovňovým kódem.

## Zachování textů a chování

- Zachovat přesné současné titulky a texty všech dialogů, pokud technické omezení Task Dialogu nevyžaduje pouze prezentační rozdělení stejného textu.
- Neměnit význam hlášení ani uživatelský tok.
- Dialogy s jediným `OK` musí nadále skončit po potvrzení `OK`.
- Potvrzovací dialogy musí zachovat dvojici voleb Yes a No a stejné mapování návratové hodnoty do stávající logiky.
- Kde je dnes výchozí tlačítko No, musí No zůstat výchozí bezpečnou volbou.
- Zachovat modalitu a správného vlastníka dialogu. Dialog nesmí zapadnout za své rodičovské okno ani umožnit neočekávanou interakci s blokovaným vlastníkem.
- Zachovat obvyklé klávesnicové ovládání, včetně Enteru, Escape a zavření přes systémové X podle významu konkrétního dialogu.
- Zavření dialogu přes Escape nebo X nesmí u potvrzovacích otázek nechtěně potvrdit destruktivní či měnící akci. Musí se chovat jako bezpečné odmítnutí.
- Startovací chyba volaná bez existujícího hlavního okna musí zůstat zobrazitelná i při časném selhání inicializace tray aplikace.
- Neměnit pořadí následných akcí, lifecycle oken, IPC volání, ukládání konfigurace, připojování, odpojování ani ukončování tray aplikace.

## Vizuální a DPI požadavky

- Informační, varovné a chybové ikony musí být ostré při všech DPI hodnotách podporovaných aplikací.
- Nesmí se použít ručně zvětšená nízkorizlišená rastrová ikona ani legacy ikona převzatá z klasického MessageBoxu.
- Dialogy musí správně reagovat na DPI monitoru svého vlastníka a na otevření po přesunu aplikace mezi monitory s odlišným škálováním.
- Text nesmí být oříznutý, rozmazaný kvůli bitmapovému škálování ani překrytý ikonou nebo tlačítky.
- Tlačítka, ikony a rozestupy musí pocházet ze systémového Task Dialogu nebo z jeho standardního chování, nikoli z ruční pixelové imitace.
- Referenční informační dialog `Configuration saved` musí po úpravě působit jako moderní systémový dialog a jeho informační ikona musí být zřetelně ostřejší než na dodaném snímku.

## Technické hranice

- Nezavádět novou knihovnu třetí strany.
- Použít stávající Win32 a Common Controls infrastrukturu projektu.
- Zkontrolovat dostupnost, inicializaci a linkování požadované pro Task Dialog API ve všech dotčených produkčních cílech.
- Nezhoršit chování při chybě inicializace Common Controls. Časná startovací chyba musí mít bezpečný a funkční způsob zobrazení.
- Pokud je pro skutečně časnou fatální chybu technicky nutný úzce omezený fallback na `MessageBoxW`, musí být tato výjimka zdůvodněná, izolovaná ve společném helperu a nesmí se použít pro běžné dialogy po inicializaci aplikace.
- Po změně nesmějí v běžných produkčních cestách tray aplikace zůstat přímá rozptýlená volání `MessageBoxW`.

## Co se nemění

- Neměnit texty, význam ani pořadí současných uživatelských hlášení.
- Neměnit logiku ukládání a validace konfigurace.
- Neměnit IPC kontrakty, síťový protokol, službu, Wintun ani bezpečnostní rozhodnutí.
- Neměnit hlavní okna, jejich layout ani modernizované ovládací prvky mimo nezbytné napojení dialogového helperu.
- Neupravovat běžnou projektovou dokumentaci.

## Akceptační kritéria

1. Všechny běžné produkční dialogy tray aplikace používají společný moderní systémový dialogový helper.
2. Audit produkčního kódu nenajde žádná přímá rozptýlená volání `MessageBoxW`. Případný jediný časný fallback je izolovaný a technicky zdůvodněný ve společném helperu.
3. Informační, varovné a chybové dialogy používají odpovídající systémové ikony Task Dialogu.
4. Ikony jsou vizuálně ostré při podporovaném DPI a nejsou zvětšenými legacy bitmapami.
5. Dialog `Configuration saved` zachová svůj titulek, text a tlačítko OK, ale už nezobrazuje rozmazanou ikonu z referenčního snímku.
6. Všechny původní dialogy s OK, Yes/No a výchozí volbou No zachovají stejné funkční rozhodování.
7. Escape a systémové X u potvrzovacích dialogů provedou bezpečné odmítnutí a nikdy nepotvrdí změnu.
8. Dialogy zůstávají správně modální vůči svému vlastníkovi a nezapadají za rodičovské okno.
9. Startovací chyba bez hlavního okna zůstává zobrazitelná.
10. Nedojde ke změně IPC, konfigurace, síťového chování, lifecycle ani ostatních částí GUI.
11. Tray aplikace se po změně úspěšně zkompiluje.
12. Výsledná aktuální sestava je zkopírována do distribuce a připravena k ruční instalaci a ověření uživatelem.

## Omezení implementace a ověření

- Neprovádět komplexní testy.
- Nevytvářet nové testy.
- Existující test upravit pouze tehdy, pokud je potřebná změna malá, přímočará a rychlá. Rozsáhlejší úpravy testů odložit.
- Neupravovat běžnou projektovou dokumentaci, včetně `protocol.md`, `client.md`, `tray_UX.md` a plánovacích dokumentů.
- Provést pouze přiměřené statické ověření všech produkčních výskytů, zkompilovat tray aplikaci a vyřešit kompilační chyby způsobené změnou.
- Po úspěšném sestavení zkopírovat tray aplikaci do distribuce stejným podporovaným postupem jako u předchozích mikro-updateů, aby ji uživatel mohl nainstalovat a ručně vizuálně ověřit.
- Neprovádět commit ani push, pokud o to uživatel výslovně nepožádá.

## Hranice formalizační fáze

Tento soubor vytvořila hlavní koordinátorka osobně před založením implementačního úkolu. Vytvoření ani zápis formálního zadání nebyly delegovány na jiného agenta.

Uživatel výslovně zvolil model Sol a effort High. Implementace smí být předána samostatnému projektovému úkolu až po dokončení tohoto zadání a uložení referenčního snímku.

## Následná kritická regrese po instalačním ověření

Po první implementaci uživatel odinstaloval předchozí instalaci pomocí distribučního `uninstall.bat` a následně spustil nový distribuční `install.bat`. PowerShellová část instalace doběhla až ke spuštění tray aplikace, ale Windows zobrazil tuto chybu:

> phpvpn-tray.exe - Ordinální číslo nebylo nalezeno
>
> Ordinální číslo 345 se nepodařilo v dynamicky propojované knihovně C:\Program Files\phpvpn\1.0.0\phpvpn-tray.exe nalézt.

Ordinál 345 odpovídá `TaskDialogIndirect` z `Comctl32.dll` verze 6. Nejde o chybu odinstalace ani o poškozenou kopii tray executable. Statický import výsledného `phpvpn-tray.exe` správně uvádí `COMCTL32.dll` a ordinál 345, distribuční executable se hashově shodoval s čerstvým buildem.

Diagnostika nainstalovaného executable pomocí Microsoft Manifest Tool však selhala s informací, že standardní manifestový resource v obrazu neexistuje. `client/apps/version.rc.in` používá `RT_MANIFEST`, ale zahrnuje pouze `winver.h`. Výsledný resource proto není dostupný jako standardní číselný `RT_MANIFEST` typu 24 a Windows vložený aplikační manifest při startu neaktivuje. Deklarovaná závislost na `Microsoft.Windows.Common-Controls` verze 6 se neuplatní, načte se starší Common Controls verze 5 a loader nenajde ordinál 345. Před zavedením přímého importu `TaskDialogIndirect` mohla tato dlouhodobá chyba zůstat skrytá.

Tato regrese je nadále součástí mikro-updatu 15. Nezakládat kvůli ní mikro-update 16.

### Požadovaný opravný audit

- Znovu odborně zkontrolovat celý diff mikro-updatu 15, nejen předpokládanou jednořádkovou opravu manifestu.
- Ověřit správnost společného Task Dialog helperu, převod všech původních dialogů, mapování návratových hodnot, bezpečné odmítnutí, vlastníky, startovací fallback a CMake integraci.
- Opravit resource skript nebo související buildové nastavení tak, aby každý relevantní executable obsahoval skutečný standardní aplikační manifest jako resource typu 24 s existující závislostí na Common Controls verze 6.
- Nevytvářet druhý konkurenční manifest a zachovat současné `asInvoker`, Per-Monitor DPI v2, supportedOS, long-path a UTF-8 deklarace.
- Ověřit výsledný produkční `phpvpn-tray.exe` přímo nástrojem schopným vyčíst resource `#1` typu `RT_MANIFEST`, nikoli pouze kontrolou zdrojového XML.
- Ověřit, že import ordinálu 345 je po aktivaci manifestu řešitelný proti Common Controls verze 6 a tray aplikace po instalaci skutečně nastartuje bez hlášky `Ordinální číslo nebylo nalezeno`.
- Dosavadní `install.ps1 -VerifyOnly` není dostatečný důkaz runtime funkčnosti. Po bezpečném sestavení a vytvoření distribuce provést přiměřené runtime ověření startu tray executable, pokud jej lze uskutečnit bez zásahu do uživatelovy aktivní VPN konfigurace. Pokud skutečnou instalaci nebo interaktivní ověření nelze bezpečně provést, výslovně to uvést a doložit alespoň vyčtení manifestu z finálního distribučního executable.
- Znovu vytvořit a ověřit distribuci. Finální balíček musí obsahovat opravený executable a být připravený pro opakovanou instalaci uživatelem.

### Dodatečná akceptační kritéria

13. Finální `phpvpn-tray.exe` obsahuje čitelný aplikační manifest jako standardní resource typu 24 a resource ID 1.
14. Vyčtený manifest finálního distribučního executable obsahuje závislost na `Microsoft.Windows.Common-Controls` verze 6 a zachovává ostatní původní deklarace.
15. Tray executable již při startu neselže na chybějícím ordinálu 345.
16. Audit celého mikro-updatu 15 neodhalí jinou regresi v dialozích, návratových hodnotách, vlastnících, CMake cílech ani distribuci.
17. Nově vytvořená distribuce projde podporovaným ověřením a obsahuje přesně opravený tray executable.
