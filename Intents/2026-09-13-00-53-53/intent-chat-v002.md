---
intent_version: 2
created_at: 2026-09-13-01-02-31
based_on: intent-v001.md
produces: intent-v002.md
---

**User:** Vytvoř nový intent

**AI:** Máš hotovo, nový intent je založený a zachytávání je aktivní. Nadiktuj mi, co má obsahovat, a kdykoli můžeš říct pozastav, pokračuj nebo zpracuj intent.

**User:** Chci upravit klientskou část PHP VPN, konkrétně tray aplikaci, a chci provést dvě relativně jednoduché změny. Za prvé, když uživatel klikne na... tray aplikaci a zobrazí show server info, tak ta tabulka, která tam je, respektive ty taby, konkrétně hned ten první tab, who is online, a hned vedle toho raw server information splývají s pozadím. Dám ti do chatu printscreen a tam je možné vidět, že ten tab, který je aktivní, tak je velice těžko rozlišitelný od neaktivního tabu a tohle je ještě těžce rozlišitelné od pozadí. Chtěl bych, aby aktivní tab měl v hlavičce, a teď se bavíme jenom o tom tabu, ne o těle té karty, ale jenom o té hlavičce toho tabu, té kartičky, tak aby měl... Eee... Světlé... Eee... Hodně světle modré pozadí a černý text a neaktivní taby budou mít tmavě modré pozadí a bílý text. De facto stejný design jako stejné barvy i jako má tlačítko OK na té stránce. Pokud by to nebylo možné, pokud by nebylo možné... Eee... Ty barvy takovýmhle způsobem upravit, tak mě agent, který to bude implementovat, o tom okamžitě informuje

**AI:** První změnu jsem si zapsala. Pokračuj prosím druhou změnou.

**User:** Druhá změna je na formuláři statistics... Chtěl bych tam prohodit v bloku IPv4 traffic jednotlivé položky. Na levé straně bude packet sent, teda packets sent, tak jako je doposud, pod tím bude packets received, a napravo vedle packets sent bude bytes sent a dole pod tím napravo dole vedle packets received bude bytes received, jako je doposud Tak, zpracuj intent

**AI:** Intent je zpracovaný do verze 1. Aktivní hlavička tabu má být velmi světle modrá s tmavým textem, neaktivní hlavička tmavě modrá s bílým textem podle tlačítka OK a změna se týká pouze hlaviček. IPv4 traffic má mít nahoře `Packets sent | Bytes sent` a dole `Packets received | Bytes received`. Pokud stylování tabů nepůjde provést bez narušení jejich chování a přístupnosti, implementující agent musí uživatele ihned informovat.

**AI vytvořený artefakt, plný obsah `intent-v001.md`:**

---
intent_version: 1
created_at: 2026-09-13-00-59-01
source: intent-chat-v001.md
---

# Úprava tabů Server information a rozložení IPv4 traffic

## Shrnutí

Upravit klientskou část PHPVPN, konkrétně Windows tray aplikaci, ve dvou vizuálních bodech:

1. Výrazně odlišit aktivní a neaktivní hlavičky tabů v okně Server information.
2. Přeskupit čtyři hodnoty v kartě IPv4 traffic ve formuláři Statistics do logické mřížky podle směru a jednotky.

## Kontext projektu

- Okno Server information vytváří `EditorWindow` v `client/apps/tray/editor_window.cpp`.
- Taby `Who is online` a `Raw server information` jsou nativní Windows tab control. Přepínání dnes zachovává obsah, fokus, ovládání myší i šipkami.
- Tlačítko OK v tomto okně používá roli primárního tlačítka. Projektová paleta obsahuje tmavě modrou `ui::Color::primary`, bílou `ui::Color::primaryText`, velmi světle modrou `ui::Color::selectedStep` a tmavý text `ui::Color::text`.
- Rozložení Statistics se počítá v `client/apps/tray/statistics_layout.h`. Definice a datové vazby statistik jsou v `client/apps/tray/statistics_model.h`.
- Pole IPv4 jsou už v pořadí `Packets sent`, `Bytes sent`, `Packets received`, `Bytes received`. Široké rozložení je však nyní rozděluje po sloupcích, takže levý sloupec obsahuje odeslané pakety a bajty a pravý sloupec přijaté pakety a bajty.

## Požadovaná změna 1: hlavičky tabů Server information

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

## Požadovaná změna 2: karta IPv4 traffic

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

## Pravděpodobně dotčené oblasti

- `client/apps/tray/editor_window.cpp`
- případně sdílené vykreslovací pomocné funkce v `client/apps/tray/ui_controls.cpp` a jejich deklarace
- `client/apps/tray/statistics_layout.h`
- testy v `client/tests/unit/test_wp03_windows.cpp`, `client/tests/ui/tray_ui_tests.cpp` a související testy rozložení Statistics

Definice `statisticsFields` neměnit jen kvůli širokému rozložení, pokud lze požadované pozice vyjádřit přímo v layoutu. Tím se zachovají stabilní datové vazby a pořadí ovládacích prvků.

## Akceptační kritéria

1. Po otevření Server information je aktivní `Who is online` jasně odlišen světle modrou hlavičkou s tmavým textem.
2. Neaktivní `Raw server information` má tmavě modrou hlavičku s bílým textem.
3. Po přepnutí tabů se barvy přesně prohodí a změna se týká jen hlaviček.
4. Myš, šipky, fokus, tabulátor, přístupnost a stabilita obsahu fungují stejně jako před změnou.
5. Široká karta IPv4 traffic zobrazuje nahoře `Packets sent` a `Bytes sent`, dole `Packets received` a `Bytes received`.
6. Každý popisek nadále ukazuje správnou hodnotu a správný formát.
7. Úzké rozložení, vysoké DPI a dlouhé maximální hodnoty nevytvářejí překryvy, ořezání ani regresi posouvání.
8. Automatické testy pro tray UI a Statistics projdou. Vizuální ověření zachytí oba stavy tabů a široké i úzké rozložení Statistics.

## Pokyny pro realizaci

- Před úpravou nativního tab control ověřit nejmenší vhodný mechanismus vlastního vykreslení, který zachová jeho existující chování a přístupnost.
- Znovu použít existující role barev z `ui_style.h`, nevytvářet duplicitní pevně zadané RGB hodnoty bez nutnosti.
- Pro Statistics preferovat změnu mapování řádků a sloupců v layoutu před změnou pořadí datového modelu.
- Screenshot zmíněný uživatelem nebyl při zpracování intentu dodán. Specifikace proto vychází z přesně určených komponent a existující projektové palety.
- Neprovádět v rámci této fáze samotnou implementaci.

**AI:** Intent je hotový a uložený. Chceš pokračovat přímou realizací, nebo nejdřív vytvořit realizační plán?

**User:** Je to natolik jednoduché, že bych pokračoval přímou realizací, jakmile bude realizace hotová, tak se samozřejmě musí upravit... dokumentace, to znamená pravděpodobně client tečka MD, možná tray UX, možná protocol MD, i když protocol MD nejspíš ne, a potom se musí opravit testy, pokud nějaké jsou. Nedělal bych komplexní testy na závěr, jenom vytvoř v distribuci, což znamená, že fakticky stačí jenom zkompilovat tray exe, a zkopírovat ho do adresáře z distribucí, opravit tam manifesty, a to je celé. Takže i žádné dělání komplexní distribuce, žádné komplexní testování celé aplikace, ehm vážně soustředit se jenom na tyhlety dvě změny, zdokumentovat je, udělat relativní testy vůči těmhletěm dvěma změnám, zkompilovat tray aplikaci a hodit ji do složky dist

