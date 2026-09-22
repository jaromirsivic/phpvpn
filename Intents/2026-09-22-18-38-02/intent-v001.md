---
intent_version: 1
created_at: 2026-09-22-18-52-06
source: intent-chat-v001.md
---

# HTTP headers v Configuration Wizardu a prázdné výchozí kontrakty v Edit Config

## Shrnutí

Rozšířit Windows tray klienta PHP VPN o editaci vlastních HTTP hlaviček přímo v prvním kroku Configuration Wizardu. Hlavičky se upravují v samostatném resizovatelném dialogu, validují se při potvrzení a až do dokončení celého průvodce zůstávají pouze v paměti. Současně upravit Reset v Edit Config tak, aby výchozí konfigurační dokument neobsahoval žádný vzorový komunikační kontrakt. Stávající generování výchozích kontraktů v Configuration Wizardu se nesmí změnit.

## Kontext projektu

- První krok průvodce `Server URL` se vykresluje v `client/apps/tray/wizard_window.cpp` a již obsahuje kartu se dvěma částmi oddělenými horizontálním separátorem.
- Výchozí HTTP hlavičky jsou nyní centralizované v `kDefaultHttpHeaders` v `client/src/core/config/config.h`.
- Existující `normalizeHttpHeaders` v `client/src/core/protocol/http_headers.cpp` převádí konce řádků na CRLF, kontroluje syntaxi, duplicity a zakazuje case-insensitive přepsání hlaviček spravovaných aplikací nebo transportem: `Content-Type`, `Content-Length`, `Connection`, `Host` a `Transfer-Encoding`.
- Průvodce načítá HTTP hlavičky do `WizardContext`, ale `buildWizardDocument` je zatím při dokončení průvodce nezapisuje zpět do `masterSetup.httpHeaders`.
- Edit Config Reset načítá sdílený výchozí dokument. Ten nyní obsahuje jeden vzorový záznam v `communicationContracts`.
- Relevantní uživatelská dokumentace je zejména `client.md` a `tray_UX.md`.

## 1. Sekce HTTP headers v prvním kroku Configuration Wizardu

### Umístění a text

V kroku `Server URL` vložit uvnitř existující karty novou sekci mezi dosavadní první horizontální separátor a závěrečnou část s pravidly URL, která začíná textem typu „HTTP and HTTPS are supported...“.

Sekce musí být od obou sousedních částí oddělena horizontálním separátorem. Ve výsledku bude pořadí:

1. pole a nápověda pro Server URL,
2. horizontální separátor,
3. nová sekce HTTP headers,
4. horizontální separátor,
5. stávající pravidla a patičková nápověda pro URL.

Veškerý nový uživatelský text bude anglicky. Použít tento krátký, profesionální text:

- nad tlačítkem: `Configure additional HTTP headers for this server.`
- tlačítko: `HTTP headers`
- pod tlačítkem: `Some web servers and hosting providers require additional headers, especially Cookie or User-Agent. If you are unsure what to enter, keep the default values.`

Text může být zalomen podle dostupné šířky, ale nesmí významově nabádat nezkušeného uživatele k nahrazení výchozích hodnot prázdným obsahem.

### Otevření editoru

Kliknutí na `HTTP headers` otevře vlastněný modální dialog nad Configuration Wizardem. Dialog musí:

- mít titul `HTTP headers`,
- používat stejný vizuální jazyk, fonty, DPI škálování, padding, mezery, ovládací prvky a styl patičky jako Configuration Wizard,
- obsahovat hlavní multiline textbox, který vyplňuje celou dostupnou klientskou plochu nad patičkou,
- zobrazovat každou HTTP hlavičku na samostatném skutečném řádku, nikoli jako viditelný text `\r\n`,
- být resizovatelný a maximalizovatelný,
- mít rozumnou minimální šířku a výšku zabraňující rozbití rozložení,
- nemít aplikační maximální rozměr; horním limitem je běžné chování okna a dostupná obrazovka.

### Patička dialogu

Patička obsahuje:

- vlevo červené tlačítko `Reset` ve stávajícím reset stylu tray aplikace,
- vpravo tlačítko `OK`,
- bezprostředně vlevo od `OK` tlačítko `Cancel`.

Výchozí tlačítko má být `OK`. Standardní zavření okna a `Esc` se chovají jako `Cancel`.

### Hodnoty, Reset, OK a Cancel

- Při otevření se textbox naplní aktuální rozpracovanou hodnotou HTTP hlaviček průvodce.
- U existující konfigurace jde o hodnotu načtenou z `masterSetup.httpHeaders`.
- U nové nebo resetované konfigurace jde o centralizovanou hodnotu `kDefaultHttpHeaders`.
- `Reset` pouze nahradí obsah textboxu hodnotou `kDefaultHttpHeaders`, zobrazenou jako jednotlivé řádky. Reset dialog nezavírá a nic nezapisuje na disk.
- `Cancel`, `Esc` nebo zavření dialogu zahodí změny provedené od jeho otevření.
- `OK` provede validaci. Při úspěchu uloží kanonickou hodnotu do rozpracovaného stavu průvodce v paměti a dialog zavře.
- Při chybě zůstane dialog otevřený, zachová uživatelský text a zobrazí srozumitelnou chybu s číslem řádku a důvodem, pokud jsou dostupné.

### Validace a kanonizace

Použít jednu sdílenou validační a normalizační logiku, nikoli její druhou odlišnou kopii. Validace při `OK` musí zachovat současná pravidla `normalizeHttpHeaders`, zejména:

- každý neprázdný řádek má syntakticky platný název, dvojtečku a hodnotu,
- žádné prázdné řádky ani nepovolené řídicí znaky,
- žádné duplicitní názvy hlaviček bez ohledu na velikost písmen,
- žádné case-insensitive přepsání nativně spravovaných hlaviček `Content-Type`, `Content-Length`, `Connection`, `Host` a `Transfer-Encoding`,
- stejné omezení velikosti jako u stávající konfigurace.

Textbox pracuje s běžnými řádky. Při potvrzení `OK` se konce řádků normalizují na CRLF. V konfiguračním JSON se po finálním uložení objeví jako standardně escapované `\r\n` uvnitř JSON stringu, nikoli jako ručně psané oddělovače v UI.

### Životní cyklus a persistence

- Rozpracované HTTP hlavičky musí být součástí stavu konkrétní relace Configuration Wizardu a musí přežít pohyb Back/Next i opětovné otevření dialogu.
- Potvrzení `OK` v dialogu nesmí měnit konfigurační soubor.
- `buildWizardDocument` nebo ekvivalentní finální sestavení musí zapsat rozpracované hlavičky do `masterSetup.httpHeaders` až při dokončení celého průvodce tlačítkem `Finish`/`Save`.
- Pokud uživatel zruší nebo zavře celý Configuration Wizard před finálním uložením, změny HTTP hlaviček se nesmí propsat na disk.
- Stávající optimistická kontrola revize, validační pravidla, chování neznámého výsledku commitu a ostatní bezpečnostní vlastnosti průvodce musí zůstat zachované.

## 2. Prázdné komunikační kontrakty po Reset v Edit Config

- Výchozí dokument načtený tlačítkem `Reset` v Edit Config musí obsahovat `"communicationContracts": {}`.
- Odstranit současný vzorový peer a `payloadKey` z výchozího editorového dokumentu.
- Reset nadále pouze nahradí rozpracovaný text editoru. Soubor se změní až po samostatném `Save`, stejně jako dnes.
- Změna nesmí upravit samostatnou logiku Configuration Wizardu, která při vytvoření nebo resetu konfigurace generuje větší sadu výchozích komunikačních kontraktů. Parametr a chování `createDefaultContracts` zůstávají funkčně beze změny.
- Pokud je stejný výchozí dokument sdílen i s dalšími cestami vytvoření konfigurace přes Edit Config, musí tyto cesty zůstat konzistentní a jejich dopad musí být pokryt testy. Nesmí se tím však obejít ani měnit generátor kontraktů Configuration Wizardu.

## 3. Dokumentace

Aktualizovat dokumentaci podle skutečného výsledku implementace, minimálně:

- `tray_UX.md`: rozložení nové sekce a dialogu, texty, tlačítka, resize/maximize, validace, Cancel/Reset/OK a odložené uložení,
- `client.md`: význam `masterSetup.httpHeaders`, sdílené výchozí hlavičky, normalizace CRLF, nativně spravované zakázané hlavičky a prázdné výchozí `communicationContracts` pro Edit Config Reset.

Pokud testovací nebo distribuční dokumentace popisuje starý vzorový kontrakt či staré chování průvodce, aktualizovat i ji.

## Pravděpodobně dotčené oblasti

- `client/apps/tray/wizard_window.*` a související layout, textové konstanty a zdroje,
- nový nebo vhodně sdílený dialog pro editaci HTTP hlaviček,
- `client/src/core/wizard/wizard_model.*` a typy stavu průvodce,
- `client/src/core/config/config_document.*` pro finální zápis hlaviček,
- `client/src/core/protocol/http_headers.*` pro opětovné použití validace a popisu chyby,
- `client/src/core/config/config.h` a případně `config_defaults.*` pro výchozí dokument,
- unit, UI a end-to-end testy průvodce, editoru a serializace,
- `client.md` a `tray_UX.md`.

## Akceptační kritéria

1. První krok Configuration Wizardu zobrazuje novou sekci ve správném pořadí mezi dvěma separátory a s přesně anglickým, stručným textem.
2. Tlačítko `HTTP headers` otevře modální, resizovatelný a maximalizovatelný dialog se stejným stylem jako průvodce.
3. Multiline textbox zobrazuje jednu hlavičku na řádek a korektně reaguje na změnu velikosti i DPI.
4. `Reset` vlevo obnoví centralizované výchozí hlavičky, ale dialog nezavře a nic neuloží.
5. `Cancel`, `Esc` a zavření dialogu zahodí lokální změny.
6. `OK` validuje a normalizuje hlavičky. Chybný vstup zůstane v otevřeném dialogu s vysvětlením.
7. Pokusy přepsat `Content-Type`, `Content-Length`, `Connection`, `Host` nebo `Transfer-Encoding` v libovolné velikosti písmen jsou odmítnuty.
8. Platné změny přežijí navigaci průvodcem, ale na disk se zapíší teprve při finálním `Finish`/`Save`.
9. Zrušený Configuration Wizard nezmění konfigurační soubor.
10. Edit Config Reset vytvoří rozpracovaný dokument s prázdným objektem `communicationContracts` a soubor změní až po `Save`.
11. Generování výchozích kontraktů v Configuration Wizardu je regresně nezměněné.
12. Automatizované testy pokrývají výchozí hodnoty, Reset, Cancel, úspěšné OK, každou třídu validační chyby, CRLF serializaci, odložené uložení a prázdné kontrakty Edit Config.
13. Relevantní dokumentace odpovídá výslednému chování.

## Realizační zásady

- Nevytvářet druhou hardcodovanou kopii výchozích HTTP hlaviček. Všechny dotčené cesty musí používat `kDefaultHttpHeaders` nebo jeho jednoznačný sdílený ekvivalent.
- Nevytvářet druhou nezávislou implementaci parseru a validátoru HTTP hlaviček.
- Zachovat přístupnost klávesnicí, pořadí tabulátoru, default/cancel semantics, owner-draw styly, DPI škálování a existující pravidla fokusů tray UI.
- Přidat regresní testy před změnou dokumentace a ověřit, že výchozí kontrakty průvodce zůstaly funkčně stejné.
