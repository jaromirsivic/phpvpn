---
intent_version: 1
created_at: 2026-09-12-13-14-26
source: intent-chat-v001.md
---

# Zjednodušení tray dialogů a sjednocení patiček

## Shrnutí

Upravit Windows tray aplikaci PHPVPN ve třech souvisejících oblastech:

1. zjednodušit horní část oken průběhu připojení a odpojení,
2. rozdělit Server Information do dvou karet,
3. sjednotit všechny tlačítkové patičky tray oken podle prvních dvou kroků Configuration Wizardu.

Změna se týká pouze klientské tray aplikace. Nemění protokol, službu, kryptografii ani síťové chování.

## Kontext projektu a současný stav

- Okna Connect a Disconnect sdílejí `ProgressWindow`. Současné UI vytváří horní `eyebrow`, dynamický výrazný `heading` a dynamický `description`, viz `client/apps/tray/progress_window.cpp:35-216`.
- Jejich geometrie je centralizována v `client/apps/tray/progress_layout.h:1-75`. Na širším okně jsou nyní levý a pravý okraj rozdílné a výrazně větší než ve wizardu.
- Server Information je read-only režim `EditorWindow`. Sdílí layout s Edit Config, viz `client/apps/tray/editor_window.cpp:256-369` a `client/apps/tray/layout.cpp:108-137`.
- Referenční rozložení patičky Configuration Wizardu používá DPI škálovaný vnější okraj 16 px, výšku tlačítka 36 px a mezeru 8 px, s odpovídajícím dvouřádkovým režimem při nedostatku šířky, viz `client/apps/tray/wizard_layout.cpp:4-36`.
- Současná vlastní tray okna s tlačítkovou patičkou jsou Configuration Wizard, EditorWindow v režimech Edit Config a Server Information, ContractsWindow a PasswordShaWindow. Systémové TaskDialogy nejsou vlastní tray patičky.

## Požadované změny

### 1. Okno Connect

- Odstranit z viditelného rozložení dynamický velký nadpis, například `Connecting`, `Successfully connected` nebo `Connection failed`.
- Odstranit z viditelného rozložení popis pod nadpisem, například `The VPN connection is ready.` nebo chybový a průběhový text.
- Ponechat pouze dosavadní horní řádek ve vizuálně menším stylu.
- Obsah horního řádku musí být po celou dobu existence okna statický a přesně anglicky:

  `Connecting client to the server.`

- Stav, úspěch, chyba, doporučená akce a odpočet zůstávají zobrazovány v důležitých kartách a panelech níže. Odstranění horních textů nesmí odstranit ani skrýt tyto informace.
- Po odstranění dvou řádků posunout karty a panely nahoru, aby nevzniklo prázdné místo.

### 2. Okno Disconnect

- Stejným způsobem odstranit velký dynamický nadpis, například `Disconnect`, a menší popis `VPN connection status`.
- Ponechat pouze horní řádek ve vizuálně menším stylu.
- Obsah horního řádku musí být po celou dobu existence okna statický a přesně anglicky:

  `Disconnecting client from the server.`

- Žlutý, zelený nebo chybový stavový panel musí nadále obsahovat aktuální výsledek, průběh, doporučenou akci a případný odpočet.
- Po odstranění dvou řádků posunout stavový panel nahoru bez zbytečného prázdného místa.

### 3. Boční odsazení panelů Connect a Disconnect

- Nepoužívat dříve navržené mechanické zmenšení na polovinu.
- Levý okraj karet a panelů musí odpovídat vzdálenosti levé hrany tlačítka Reset od levého okraje Configuration Wizardu v prvním a druhém kroku.
- Pravý okraj karet a panelů musí odpovídat vzdálenosti pravé hrany tlačítka Verify and continue od pravého okraje Configuration Wizardu v prvním a druhém kroku.
- Odsazení musí být symetrické, DPI škálované a musí se chovat shodně při změně velikosti okna.
- Realizační vodítko vycházející ze současného kódu je sdílet stejnou výpočtovou zásadu jako wizard, tedy na běžné šířce 16 px při 96 DPI s bezpečným omezením pro velmi úzká okna. Toto je vodítko, rozhodující je vizuální shoda se vzorem.

### 4. Server Information se dvěma kartami

- Zachovat nadpis `Server information` a spodní patičku s tlačítkem OK.
- Nad obsah přidat ovladatelnou dvoukartovou navigaci v tomto pořadí:
  1. `Who is online`
  2. `Raw server information`
- Po otevření musí být výchozí karta `Who is online`.
- Karta `Who is online` zůstane v této změně obsahově prázdná. Musí však mít řádnou prázdnou obsahovou plochu a nesmí zobrazovat raw JSON.
- Karta `Raw server information` zobrazí stávající read-only multiline textové pole se serverovým JSONem beze změny obsahu, možnosti posouvání a kopírování.
- Přepínání karet nesmí znovu načítat ani měnit serverová data. Jde pouze o přepnutí zobrazení již načteného obsahu.
- Ovládání musí fungovat myší i klávesnicí, mít zřetelný vybraný stav a zapadat do současného vizuálního stylu tray aplikace.

### 5. Sjednocení všech tlačítkových patiček tray oken

- První a druhý krok Configuration Wizardu jsou normativní vizuální vzor pro:
  - výšku patičky,
  - vertikální vystředění tlačítek,
  - levý a pravý vnější okraj,
  - vodorovné mezery mezi sousedními tlačítky,
  - chování při nedostatku šířky a zalomení do více řádků.
- Prověřit a upravit všechna vlastní tray okna, která mají samostatnou spodní patičku s tlačítky. Aktuálně jde nejméně o:
  - Server Information,
  - Edit Config,
  - Communication Contracts,
  - Password SHA dialog.
- Configuration Wizard je referenční implementace. Měnit jej jen tehdy, pokud je nutná bezpečná extrakce nebo sdílení layout pravidel bez vizuální změny wizardu.
- Zachovat význam, pořadí, text, role, povolený stav a klávesové chování stávajících tlačítek.
- Šířka tlačítek se může nadále řídit textem a minimální šířkou. Vnější okraje a mezery mají odpovídat wizardu.
- U patiček s více akcemi zachovat logické seskupení. Pokud se tlačítka nevejdou do jednoho řádku, použít stejné principy bezpečného zalomení jako wizard, bez překryvu a ořezu.
- Vlastní obsah okna se nesmí překrývat s patičkou. Scrollbary, chybové panely a měnitelná velikost okna musí po změně zůstat funkční.
- Nativní systémové dialogy spravované Windows, které nemají vlastní layout tray aplikace, jsou mimo rozsah.

## Omezení a zachované chování

- Neměnit logiku Connect, Disconnect, automatického zavření, odpočtu, pollingu ani zpracování chyb.
- Neměnit význam ani obsah stavových panelů a checkpointů.
- Neměnit získávání, validaci ani zobrazovaný JSON Server Information.
- V této změně neimplementovat obsah `Who is online`.
- Zachovat DPI podporu, resize, scrollování, fokus, tab order, aktivaci Enter a Escape a přístupnost ovládacích prvků.
- Nevytvářet nový IPC příkaz ani měnit wire formát.

## Pravděpodobně dotčené oblasti

- `client/apps/tray/progress_window.cpp`
- `client/apps/tray/progress_window.h`
- `client/apps/tray/progress_layout.h`
- `client/apps/tray/editor_window.cpp`
- `client/apps/tray/editor_window.h`
- `client/apps/tray/layout.cpp`
- `client/apps/tray/layout.h`
- `client/apps/tray/contracts_window.cpp`
- `client/apps/tray/password_sha_window.cpp`
- případná společná pomocná vrstva tray layoutu
- geometrické, modelové, nativní UI a screenshotové testy v `client/tests/`
- relevantní UX dokumentace, zejména `client.md` a případné tray UX specifikace

Seznam je realizační vodítko, ne povinnost měnit každý uvedený soubor.

## Akceptační kritéria

1. Connect zobrazuje jediný horní statický text `Connecting client to the server.` a nezobrazuje původní velký dynamický nadpis ani jeho popis.
2. Disconnect zobrazuje jediný horní statický text `Disconnecting client from the server.` a nezobrazuje původní velký dynamický nadpis ani jeho popis.
3. Změna stavu Connect nebo Disconnect nikdy nezmění jejich horní statický text.
4. Všechny důležité stavové, chybové, akční a odpočtové informace zůstávají dostupné v kartách a panelech.
5. Karty a panely Connect a Disconnect mají vlevo i vpravo stejný vnější okraj jako odpovídající krajní tlačítka prvního a druhého kroku Configuration Wizardu.
6. Server Information se otevře na prázdné kartě `Who is online` a raw JSON není na této kartě viditelný.
7. Po přepnutí na `Raw server information` se zobrazí původní kompletní read-only JSON se zachovaným posouváním a kopírováním.
8. Přepínání karet funguje myší i klávesnicí a nezpůsobuje nový serverový požadavek.
9. Každé vlastní tray okno s tlačítkovou patičkou má tlačítka vertikálně vystředěná a vnější okraje i mezery odpovídající Configuration Wizardu.
10. Při podporovaných DPI a při opakované změně velikosti žádná patička, tlačítko, karta, panel ani scrollbar nepřetéká, nepřekrývá se, není oříznutý a nezanechává artefakty.
11. Stávající klávesové ovládání, fokus, tab order, enabled stav a význam tlačítek zůstávají zachovány.
12. Automatizované testy pokrývají statičnost obou horních textů, odstranění dvou nadbytečných řádků, shodu okrajů, výchozí a druhou kartu Server Information, viditelnost JSONu a geometrii všech vlastních tray patiček.
13. Existující relevantní testy tray aplikace projdou. Skutečné vizuální nebo Windows runtime scénáře, které nelze v realizačním prostředí spustit, budou výslovně označeny `NOT RUN`.

## Realizační doporučení

- Sdílet jednu DPI-aware definici vnějšího okraje, mezery a základní geometrie patičky tam, kde to sníží riziko budoucího rozcházení oken.
- Neodstraňovat dynamické stavové texty z modelu, pokud je nadále používají panely nebo testy. Odstranit pouze jejich nadbytečnou horní prezentaci.
- Přidat layout testy na 96 DPI i alespoň jednu vyšší hodnotu a na šířky před a po bodu zalomení.
- Doplnit nativní UI test Server Information, který ověří pořadí karet, výchozí výběr, skrytí a zobrazení JSON pole a funkční OK patičku.
