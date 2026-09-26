---
intent_version: 1
created_at: 2026-09-26-15-17-22
source: intent-chat-v001.md
---

# Úpravy uživatelského rozhraní PHPVPN na Linuxu a odstranění cizí ikony

## Shrnutí

Upravit rozložení oken PHPVPN na Fedoře, aby byla použitelná při výchozím rozlišení 1024 × 768, a odstranit motiv štítu se zámkem ze všech obrazovek, kde se používá. Odstranění motivu platí pro Linux i Windows. Používat vlastní identitu PHPVPN. Čtyři přiložené snímky jsou součástí tohoto intentu a odkazy níže jsou relativní k této složce.

## Obrazové podklady

1. [Hlavní okno na Fedoře](01-hlavni-okno-fedora.png): motiv štítu se zámkem u stavu připojení, velký prostor kolem něj a tlačítko Exit mimo viditelnou část okna.
2. [Configuration Wizard v širokém okně](02-pruvodce-siroke-okno.png): levý panel kroků zabírá přibližně polovinu šířky, pravá část s formulářem je zbytečně stísněná.
3. [Configuration Wizard v užším okně](03-pruvodce-uzke-okno.png): kroky se přesunou nahoru, což uživateli vyhovuje, ale tlačítka Reset a Verify & continue leží v různých řádcích.
4. [Communication contracts](04-komunikacni-kontrakty.png): okno přesahuje použitelnou šířku obrazovky a část obsahu i ovládání je mimo viditelnou plochu.

## Výslovné požadavky uživatele

### Hlavní okno Linuxu

- Odstranit současný znak štítu se zámkem. Nezachovávat ho jako dekoraci ani jako náhradní ikonu jinde v okně.
- V hlavním okně použít vlastní výchozí modrou ikonu PHPVPN. Přednostně znovu použít existující vektorovou podobu, pokud je v projektu dostupná. Uživatel uvedl projekt `C:\git\smsweb` jako možné místo dřívějšího převodu. Vektor je možné vytvořit znovu z vlastního zdroje.
- Zmenšit nadměrný prostor kolem horní ikony a upravit rozložení tak, aby bylo tlačítko Exit viditelné bez posouvání ve výchozí velikosti okna na obrazovce 1024 × 768.
- Pokud to desktopové prostředí umožní, zobrazovat v pravé horní systémové oblasti informaci, že PHPVPN běží. Uživatel považuje tuto část za žádoucí, ale nepodmiňuje jí dokončení intentu. Ověřit, zda indikátor v daném prostředí funguje a zda se zobrazuje po spuštění aplikace.

### Configuration Wizard na Linuxu

- V širokém rozložení vyhradit levému panelu kroků jen relativně úzkou nebo pevnou šířku a většinu prostoru dát formuláři vpravo. Uživatel uvádí vzhled průvodce na Windows jako vzor tohoto poměru.
- Zachovat chování užšího rozložení, při němž se kroky přesunou do horního vodorovného pásu.
- V užším rozložení umístit spodní tlačítka Reset a Verify & continue do jednoho řádku, dokud se vedle sebe při podporované šířce vejdou, a zajistit jejich viditelnost.

### Communication contracts na Linuxu

- Zmenšit výchozí okno a upravit rozložení jeho obsahu tak, aby se na obrazovce 1024 × 768 vešlo celé okno včetně hlavního ovládání. Zachovat přístup k celé tabulce, stránkování a spodním akcím, případně pomocí posouvání uvnitř okna.
- Velikost okna má být přiměřená ostatním formulářům, zejména Configuration Wizardu.

### Motiv štítu se zámkem v Linuxu i Windows

- Najít použití motivu štítu se zámkem napříč uživatelským rozhraním obou platforem a odstranit jej. Uživatel výslovně uvedl hlavní okno a obrazovku Connect, kde se ukazuje connecting a odpočítávání sekund.
- Tam, kde motiv není nutný pro srozumitelnost, jej prostě odstranit. Kde je ikona aplikace potřeba, použít výhradně vlastní grafiku PHPVPN. Cílem je nepoužívat motiv, kvůli němuž uživatel nechce licenční závislost na Google Material Designu.
- Zachovat srozumitelné textové stavy připojení a fungování akcí při odstranění dekorativní ikony.

## Rozsah a priority

- Požadavky se týkají grafického klienta PHPVPN. Opravy konkrétních rozložení na Fedoře a odstranění motivu v Linuxu i Windows jsou hlavní částí práce.
- Edit Config, Show Server Info a Statistics uživatel při současné kontrole označil za vyhovující velikostí a vzhledem. Není požadován jejich plošný redesign. Pokud se v nich vyskytuje zakázaný motiv štítu se zámkem, vztahuje se na něj obecný požadavek na odstranění.
- Indikátor běhu v pravé horní systémové oblasti je podmíněný možnostmi prostředí, nikoli povinný výsledek.

## Projektový kontext pro realizaci

- Linuxové hlavní okno a jeho aktuální motiv štítu jsou v `client/apps/tray-linux/control_window.cpp` (`shieldFor` ř. 10–17, `ControlWindow::update` ř. 91–101). Názvy ikon indikátoru jsou v `client/apps/tray-linux/indicator_model.cpp` ř. 7–14. Toto jsou zjištěná místa, nikoli pokyn nahradit bez kontroly pouze tyto výskyty.
- Průvodce je v `client/apps/tray-linux/wizard_window.cpp`, mimo jiné `renderSidebar` ř. 525–549. Komunikační kontrakty jsou v `client/apps/tray-linux/contracts_window.cpp`, mimo jiné `applyKeyWidth` ř. 673–679. Okno průběhu připojení je v `client/apps/tray-linux/progress_window.cpp`.
- Windows má samostatnou implementaci grafického klienta v `client/apps/tray/`. Její okno průběhu připojení je v `progress_window.cpp`, rozložení v `progress_layout.h`. Vlastní aplikační ikony se aplikují také přes `ui_resources.cpp` ř. 81–86. Při realizaci provést úplnou inventuru výskytů motivu a souvisejících zdrojů na obou platformách.
- Generátory ikon v repozitáři zahrnují `client/tools/make_icons.py` a `client/tools/make_linux_icons.py`. Výběr konečného vlastního zdroje ověřit při realizaci.

## Akceptační kritéria

1. Na Fedoře při 1024 × 768 je hlavní okno ovladatelné a tlačítko Exit je vidět bez posunu.
2. Hlavní okno používá vlastní modrou ikonu PHPVPN s přiměřeným odsazením a nezobrazuje motiv štítu se zámkem.
3. Široký Configuration Wizard má úzký levý panel a širší pravý formulář. Užší varianta zachovává horní pás kroků a obě spodní tlačítka jsou na jednom řádku při zobrazené šířce z podkladu.
4. Communication contracts se při 1024 × 768 zobrazí celé včetně ovládání. Obsah tabulky, stránkování a akce zůstávají přístupné.
5. Motiv štítu se zámkem se nikde v relevantních obrazovkách Linuxu ani Windows nezobrazuje, včetně průběhu připojení a odpočítávání. Stav a akce zůstávají srozumitelné.
6. Možnost systémového indikátoru na Fedoře je ověřena a výsledek je zaznamenán. Pokud jej prostředí podporuje, ikona běžící PHPVPN je viditelná v horní systémové oblasti.

## Hranice rozhodnutí

Tento dokument je specifikace záměru. Samotné změny kódu a případný plán začnou až po volbě další cesty uživatelem. Technické volby vektorového zdroje, přesných rozměrů a chování systémového indikátoru se mají opřít o ověření v projektu a ve Fedora prostředí.
