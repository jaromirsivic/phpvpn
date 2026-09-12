---
intent_version: 2
created_at: 2026-09-13-01-02-31
based_on: intent-v001.md
source: intent-chat-v002.md
---

# Úprava tabů Server information a rozložení IPv4 traffic

## Shrnutí

Provést dvě přesně vymezené vizuální změny ve Windows tray aplikaci PHPVPN, aktualizovat přímo související dokumentaci a cílené testy, sestavit pouze tray executable a vložit výsledný soubor do stávající distribuční složky včetně nutné aktualizace manifestů.

## Kontext projektu

- Okno Server information vytváří `EditorWindow` v `client/apps/tray/editor_window.cpp`.
- Taby `Who is online` a `Raw server information` jsou nativní Windows tab control. Přepínání dnes zachovává obsah, fokus, ovládání myší i šipkami.
- Tlačítko OK v tomto okně používá roli primárního tlačítka. Projektová paleta obsahuje tmavě modrou `ui::Color::primary`, bílou `ui::Color::primaryText`, velmi světle modrou `ui::Color::selectedStep` a tmavý text `ui::Color::text`.
- Rozložení Statistics se počítá v `client/apps/tray/statistics_layout.h`. Definice a datové vazby statistik jsou v `client/apps/tray/statistics_model.h`.
- Pole IPv4 jsou už v pořadí `Packets sent`, `Bytes sent`, `Packets received`, `Bytes received`. Široké rozložení je nyní rozděluje po sloupcích, takže levý sloupec obsahuje odeslané pakety a bajty a pravý sloupec přijaté pakety a bajty.

## Změna 1: hlavičky tabů Server information

### Funkční požadavky

- Změna platí pouze pro hlavičky tabů `Who is online` a `Raw server information` v okně Server information.
- Aktivní tab musí mít velmi světle modré pozadí a černý nebo projektově ekvivalentní tmavý text.
- Neaktivní tab musí mít tmavě modré pozadí a bílý text.
- Použít existující projektové barvy odpovídající záměru:
  - aktivní pozadí `ui::Color::selectedStep`
  - aktivní text `ui::Color::text`
  - neaktivní pozadí `ui::Color::primary`
  - neaktivní text `ui::Color::primaryText`
- Tmavě modrá a bílá mají vizuálně odpovídat základnímu stavu tlačítka OK na stejné stránce.
- Tělo stránky, textové pole, okolní pozadí a obsah jednotlivých tabů se touto změnou nesmějí přebarvit.
- Po přepnutí se musí vzhled obou hlaviček ihned aktualizovat.

### Zachování chování

- Zachovat nativní výběr tabů, přepínání myší a šipkami, pořadí tabulátoru, fokus a přístupnost.
- Přepnutí tabu nesmí znovu načítat serverová data, nahrazovat existující textové pole ani měnit jeho obsah, výběr či pozici posunu.
- Vzhled musí fungovat při běžném DPI i při škálování Windows a nesmí narušit vykreslování textu nebo hranic hlaviček.
- Zohlednit změnu systémových barev a tématu stejným způsobem jako ostatní vlastní prvky tray aplikace.

### Povinná eskalace

Pokud nelze požadované barvy aplikovat pouze na hlavičky tabů při zachování uvedeného chování, implementující agent musí uživatele okamžitě informovat. Nesmí potichu použít jiný design ani rozšířit přebarvení na tělo stránky.

## Změna 2: karta IPv4 traffic

### Široké rozložení

Čtyři položky uspořádat jako mřížku 2 krát 2:

| | Levý sloupec | Pravý sloupec |
|---|---|---|
| Horní řádek | Packets sent | Bytes sent |
| Dolní řádek | Packets received | Bytes received |

### Responzivní chování

- Změnit pouze vizuální umístění polí. Neměnit jejich popisky, hodnoty, formátování, datové vazby ani čítače.
- V úzkém jednosloupcovém rozložení zachovat stávající logické pořadí definic a současné responzivní chování, pokud k dosažení požadované široké mřížky není nutná cílená úprava.
- Výška karty, odsazení, zarovnání a chování při dlouhých hodnotách musí zůstat konzistentní s ostatními kartami Statistics.

## Dokumentace

- Aktualizovat dokumentaci klienta, minimálně `client.md`, pokud popisuje dotčené UI nebo jeho rozložení.
- Prověřit dokumentaci tray UX a upravit ji, pokud obsahuje popis nebo obrázek dotčených obrazovek.
- `protocol.md` měnit pouze tehdy, pokud se při realizaci prokáže přímá souvislost. Očekává se, že protokol změnou dotčen nebude.
- Dokumentovat jen skutečně provedené změny. Nerozšiřovat dokumentační zásah mimo jejich bezprostřední rozsah.

## Testy a ověření

- Upravit nebo doplnit pouze testy přímo související s hlavičkami tabů Server information a s rozložením IPv4 traffic.
- Zachovat existující testované chování tabů včetně ovládání myší a šipkami, fokusu, pořadí tabulátoru, stability dat a počtu načtení serverových informací.
- Ověřit správné mapování všech čtyř hodnot IPv4 a jejich široké i úzké rozložení.
- Spustit jen relevantní cílené testy. Nespouštět komplexní testování celé aplikace ani kompletní integrační sadu.

## Sestavení a distribuce

- Sestavit pouze tray executable potřebný pro tuto změnu.
- Zkopírovat výsledný tray executable do odpovídajícího existujícího adresáře v `dist`.
- Aktualizovat příslušné distribuční manifesty nebo kontrolní údaje tak, aby odpovídaly novému executable.
- Nevytvářet komplexní distribuci a nespouštět plný distribuční proces.
- Před změnou distribučních souborů přesně určit existující cílovou cestu a formát manifestů podle repozitáře.

## Pravděpodobně dotčené oblasti

- `client/apps/tray/editor_window.cpp`
- případně sdílené vykreslovací pomocné funkce v `client/apps/tray/ui_controls.cpp` a jejich deklarace
- `client/apps/tray/statistics_layout.h`
- relevantní testy v `client/tests/unit/test_wp03_windows.cpp`, `client/tests/ui/tray_ui_tests.cpp` a testy rozložení Statistics
- `client.md` a případná dokumentace tray UX
- odpovídající tray executable a manifesty ve stávající distribuční struktuře

Definice `statisticsFields` neměnit jen kvůli širokému rozložení, pokud lze požadované pozice vyjádřit přímo v layoutu. Tím se zachovají stabilní datové vazby a pořadí ovládacích prvků.

## Akceptační kritéria

1. Po otevření Server information je aktivní `Who is online` jasně odlišen světle modrou hlavičkou s tmavým textem.
2. Neaktivní `Raw server information` má tmavě modrou hlavičku s bílým textem.
3. Po přepnutí tabů se barvy přesně prohodí a změna se týká jen hlaviček.
4. Myš, šipky, fokus, tabulátor, přístupnost a stabilita obsahu fungují stejně jako před změnou.
5. Široká karta IPv4 traffic zobrazuje nahoře `Packets sent` a `Bytes sent`, dole `Packets received` a `Bytes received`.
6. Každý popisek nadále ukazuje správnou hodnotu a správný formát.
7. Úzké rozložení, vysoké DPI a dlouhé maximální hodnoty nevytvářejí překryvy, ořezání ani regresi posouvání.
8. Související dokumentace odpovídá výslednému UI. `protocol.md` zůstane beze změny, pokud se nepotvrdí jeho přímá souvislost.
9. Relevantní cílené testy projdou. Nebude spuštěna plošná testovací sada.
10. Tray executable se úspěšně sestaví, je zkopírovaný do správné složky `dist` a související manifesty odpovídají výslednému souboru.
11. Nebude vytvořena kompletní distribuce ani provedeno komplexní testování celé aplikace.

## Pokyny pro realizaci

- Před úpravou nativního tab control ověřit nejmenší vhodný mechanismus vlastního vykreslení, který zachová jeho existující chování a přístupnost.
- Znovu použít existující role barev z `ui_style.h`, nevytvářet duplicitní pevně zadané RGB hodnoty bez nutnosti.
- Pro Statistics preferovat změnu mapování řádků a sloupců v layoutu před změnou pořadí datového modelu.
- Udržet implementaci, dokumentaci, testy, sestavení i distribuční zásah striktně omezené na tyto dvě změny.
- Screenshot zmíněný uživatelem nebyl při zpracování intentu dodán. Specifikace proto vychází z přesně určených komponent a existující projektové palety.

