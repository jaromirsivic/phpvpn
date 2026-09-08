# 004 - Jednotný moderní vzhled tlačítek tray aplikace

## Cíl

Nahradit nesourodou kombinaci nativních starých Win32 tlačítek a dílčího vlastního vykreslení jedním sdíleným moderním button systémem pro celou tray aplikaci. Všechna aplikační tlačítka mají mít lehce zaoblené rohy, jednotnou typografii a konzistentní stavy. Vizuální hierarchie rozliší hlavní workflow, Reset a neutrální vedlejší akce.

## Rozsah

Úprava platí pro všechna uživatelsky viditelná tlačítka uvnitř oken tray aplikace, zejména:

- tlačítka Configuration Wizardu.
- tlačítka Edit Config.
- hlavní tlačítko a všechna tlačítka jednotlivých řádků Communication Contracts.
- tlačítka Password SHA.
- případná tlačítka v Progress, Statistics a dalších oknech tray aplikace.
- další standardní Win32 `BUTTON` controls vytvořené tray aplikací, pokud nejde o switch z mikroúpravy 002.

## Vizuální kategorie

### Primární workflow tlačítko

- Použije se pro hlavní akci, která posouvá nebo potvrzuje aktuální workflow.
- Má modrou výplň a bílý text.
- Patří sem zejména **Verify & continue**, **Disconnect & continue**, **Continue**, **Finish**, **Verify save**, **Open again**, **Save** a potvrzovací **OK** v Communication Contracts a Password SHA.
- V jednom samostatném okně nebo kroku má být zpravidla pouze jedna primární akce.

### Reset

- Každé tlačítko **Reset** má samostatný výrazný varovný styl.
- Má středně sytou červenou výplň a bílý text.
- Červená nesmí být křiklavá ani příliš tlumená. Musí být zřetelná jako významná destruktivní nebo obnovovací akce a zároveň vizuálně ladit s modrou primární barvou.
- Hover a pressed stav použijí přiměřeně tmavší červený odstín.
- Změna vzhledu nesmí měnit současné potvrzení ani funkční chování Resetu.

### Neutrální vedlejší tlačítka

- Použijí se pro všechny ostatní akce, například **Back**, **Cancel**, **Random** a **Password SHA** v jednotlivých řádcích Contracts.
- Mají světle šedou neutrální výplň, tmavý text a jemný šedý obrys, který je oddělí od pozadí.
- Hover a pressed stav změní odstín dostatečně pro okamžitou zpětnou vazbu, ale nesmí konkurovat primárnímu nebo červenému Reset tlačítku.

## Společný vzhled

- Všechna tlačítka mají lehce zaoblené rohy s jednotným poloměrem přibližně 6 DIP.
- Hrany a rohy musí používat anti-aliasing zavedený mikroúpravou 003.
- Text je vodorovně i svisle vystředěný a používá společnou aplikační typografii.
- Zachová se minimální výška 36 DIP, minimální šířka 88 DIP a dostatečný vodorovný padding pro delší labely.
- Malá tlačítka v řádcích Contracts mohou zachovat kompaktní šířku danou tabulkou, ale musí používat stejné rohy, barvy, obrys, typografii a stavy.
- Geometrie tlačítka nesmí při hoveru, focusu, stisku ani zakázání poskakovat nebo měnit rozměr.

## Interakční stavy

Každá kategorie musí mít konzistentní podobu pro:

- normální stav.
- hover stav.
- pressed stav.
- keyboard focus.
- default action.
- disabled stav.

Focus musí být jasně viditelný a nesmí spoléhat pouze na změnu barvy. Disabled stav bude výrazně ztlumený, ale text a kategorie tlačítka zůstanou rozpoznatelné. Default action nesmí skrýt skutečný keyboard focus.

## Funkční a přístupnostní požadavky

- Zachovat nativní button sémantiku, klikání myší, Enter, Space, Tab a Shift+Tab.
- Zachovat existující default-button chování a spouštění hlavní akce klávesou Enter.
- Zachovat přístupný název, roli, enabled/disabled stav, default stav a výchozí akci.
- Vlastní vzhled nesmí být pouze obrázek nebo nepřístupná dekorace.
- Klikací plocha a hit testing zůstanou nejméně stejně velké jako nyní.
- Zakázané tlačítko nesmí reagovat na kliknutí nebo klávesnici.
- Změna vzhledu nesmí změnit aplikační workflow, validační pravidla, potvrzovací dialogy ani vedlejší efekty akcí.

## Communication Contracts

- Tlačítka **Random** a **Password SHA** v každém dynamickém řádku mají neutrální styl.
- Řádková tlačítka nesmí být modrá, protože nejsou hlavní workflow akcí celého okna.
- Hlavní **OK** okna Communication Contracts má primární modrý styl.
- Všechny dynamické řádky musí používat stejný sdílený button control bez míchání s nativním starým vzhledem.
- Scroll, resize, DPI, focus, tab order a maximální podporovaný počet řádků nesmí způsobit ořez, překryv ani záměnu stavu tlačítek.

## Prvky mimo rozsah

- switche z mikroúpravy 002.
- systémová tlačítka titulkového pruhu.
- tlačítka uvnitř standardních systémových dialogů nebo MessageBoxů kompletně vykreslovaná Windows.
- položky tray menu, scrollbary, textová pole a ostatní prvky, které nejsou tlačítky.
- změna textů, pořadí akcí nebo rozložení oken, kromě drobné nutné korekce pro sjednocení tlačítek.

## Akceptační kritéria

1. V oknech tray aplikace nezůstane žádné uživatelsky viditelné tlačítko ve starém nativním Win32 vzhledu, s výjimkou výslovně vyloučených systémových dialogů.
2. Každé hlavní workflow tlačítko je modré s bílým textem.
3. Každé tlačítko Reset je středně červené s bílým textem.
4. Back, Cancel, Random, Password SHA a ostatní vedlejší akce mají neutrální světle šedou výplň, tmavý text a jemný šedý obrys.
5. Všechna tlačítka mají jednotné lehce zaoblené a vyhlazené rohy.
6. Normal, hover, pressed, focus, default a disabled stavy jsou zřetelné a konzistentní.
7. Myš, Enter, Space, Tab, Shift+Tab, default action a accessibility fungují stejně jako před změnou.
8. Communication Contracts používá moderní neutrální tlačítka ve všech dynamických řádcích a modré hlavní OK.
9. Resize, podporované DPI a scroll nezpůsobí ořez, překryv, blikání ani návrat ke starému vzhledu.
10. Změna nezasáhne switche, systémové dialogy ani aplikační logiku.

## Lehká kontrola po implementaci

- Vizuálně projít Wizard, Edit Config, Communication Contracts a Password SHA.
- Ověřit alespoň jedno primární modré tlačítko, jedno červené Reset a několik neutrálních tlačítek.
- V Contracts zkontrolovat více řádků včetně Random a Password SHA.
- U každé kategorie krátce ověřit normal, hover, pressed, focus a disabled stav, pokud je snadno dostupný.
- Ověřit Enter, Space a základní průchod klávesou Tab.
- Krátce zkontrolovat resize a jedno zvýšené DPI.
- Nespouštět těžké, plošné ani dlouhé testovací sady.
- Po úspěšné lehké kontrole sestavit Release tray EXE a vložit ho do standardní distribuce projektu.

## Referenční obrázek

- `reference-edit-config-buttons.png` ukazuje cílovou vizuální hierarchii moderního okna, modrou hlavní akci, lehce zaoblené rohy a základní proporce tlačítek. Reset se od obrázku záměrně liší podle rozhodnutí uživatele a má být červeným tlačítkem s bílým textem.

## Opravný dodatek po ruční kontrole

Po první implementaci byly při ruční kontrole nalezeny následující vady. Oprava zůstává součástí mikroúpravy 004.

### Default a focus obrysy

- Primární tlačítko nesmí působit jako dvakrát obtažené. Nesmí se současně zobrazovat oddělený modrý vnější obrys, mezera v barvě pozadí a vlastní modrá plocha tlačítka.
- Default action musí zůstat rozpoznatelná a Enter musí zachovat své chování, ale její indikace má být vizuálně sjednocená s tlačítkem bez druhého odděleného rámečku.
- Keyboard focus musí být jednoznačný a přístupný, ale jeho barva musí odpovídat roli tlačítka.
- Neutrální tlačítko, například **Back**, nesmí po získání fokusu dostat modrý obrys. Použije tmavě šedý indikátor, který ladí s neutrální výplní a zůstává dobře viditelný.
- Primární tlačítko může použít fokus odpovídající jeho modré roli, Reset fokus odpovídající červené roli. Žádná kombinace stavů nesmí vytvořit dvojité nebo opticky oddělené obtažení.

### Barva Resetu

- Základní výplň tlačítka **Reset** bude přesně `#E02E2A` s bílým textem.
- Hover a pressed varianty budou přiměřeně tmavší odstíny téže červené, bez změny geometrie tlačítka.
- Disabled stav zůstane zřetelně ztlumený.

### Tažení myší mimo tlačítko

- Při stisknutí levého tlačítka myši nad tlačítkem a následném tažení kurzoru mimo jeho plochu se musí korektně měnit pressed a hover stav.
- Po opuštění tlačítka, návratu, uvolnění myši i ztrátě mouse capture musí být celá plocha tlačítka a jeho bezprostředního okolí správně překreslena.
- Nesmí zůstat žádné modré, šedé ani jiné čáry napravo, dole nebo mezi sousedními tlačítky. Nesmí vznikat ani částečný obrys, duch původního stavu nebo jiný grafický artefakt.
- Kliknutí se provede pouze při platném uvolnění uvnitř tlačítka. Nativní button sémantika a přístupnost zůstanou zachované.

### Owner-draw od okamžiku vytvoření

- Všechna moderní aplikační tlačítka se musí vytvořit přímo v owner-draw režimu, ne jako běžný `BS_PUSHBUTTON` nebo `BS_DEFPUSHBUTTON`, na který se vlastní kreslení připojí až dodatečně.
- Windows nesmí ani při prvním zobrazení okna, změně stavu, získání fokusu, stisku nebo synchronním překreslení vykreslit původní systémový vzhled tlačítka.
- Při otevření Configuration Wizardu, Edit Config, Communication Contracts, Password SHA ani jiného okna nesmí na okamžik probliknout klasické Windows tlačítko před zobrazením moderního vzhledu.
- Aplikace bude od prvního viditelného snímku vlastnit kompletní vykreslení výplně, obrysu, textu a všech stavů tlačítka.
- Zachová se nativní HWND tlačítka a jeho dialogová sémantika, mouse capture, Tab, Shift+Tab, Enter, Space, default action a přístupnost. Owner-draw nesmí tyto vlastnosti nahradit neúplnou vlastní emulací.
- Změna stylu nesmí vrátit dvojité obtažení ani artefakty při tažení opravené v předchozích bodech.

### Důkazní snímky

- `feedback-double-outline.png` zachycuje dvojité obtažení primárního tlačítka a nevhodné focus indikace.
- `feedback-drag-paint-artifact.png` zachycuje modré čáry, které zůstaly po stisku tlačítka **Back** a tažení myší mimo jeho plochu.

### Lehká kontrola opravného dodatku

- Ručně zkontrolovat primární, neutrální a Reset tlačítko v normal, hover, pressed, default a keyboard-focus stavu.
- U tlačítka **Back** i primární akce stisknout levé tlačítko myši, táhnout ve více směrech mimo ovládací prvek a uvolnit uvnitř i vně.
- Ověřit, že po přechodech nezůstávají grafické artefakty a sousední plocha je čistá.
- Ověřit základní barvu Resetu `#E02E2A`.
- Několikrát otevřít Wizard a Edit Config a ověřit, že před prvním moderním vykreslením neproblikne nativní Windows tlačítko.
- Testy opravovat jen tehdy, pokud je to rychlé a přímo souvisí s opravou. Nespouštět těžké ani plošné testovací sady.
- Neupravovat klientskou dokumentaci.
- Po opravě sestavit Release tray EXE, vložit ho do standardní distribuce a rychle ověřit distribuční soubor, manifest a podpis.

## Stav

Uživatel výslovně schválil implementaci tohoto opravného dodatku a požádal, aby ji provedl stejný agent, který implementoval původní mikroúpravu 004.

## Závazné pravidlo pro následující mikroúpravy

Toto pravidlo je důležitým výstupem mikroúpravy 004 a platí pro každou následující GUI mikroúpravu tray aplikace:

- Pokud tray aplikace nahrazuje vzhled nativní Win32 komponenty vlastním designem, musí se komponenta pokud možno vytvořit rovnou v owner-draw režimu.
- Není přijatelné nejprve vytvořit viditelnou komponentu v původním Windows vzhledu a vlastní kreslení připojit až dodatečně. Původní systémový vzhled nesmí probliknout při prvním zobrazení ani při žádné interakci nebo změně stavu.
- Aplikace musí vlastnit celý viditelný vzhled stylované komponenty od jejího prvního vykresleného snímku.
- Nativní HWND a systémová semantika se mají zachovat všude, kde poskytují správné chování klávesnice, dialogů, mouse capture a přístupnosti.
- Pokud u konkrétní komponenty owner-draw nelze bezpečně použít bez ztráty funkce nebo přístupnosti, musí se tato výjimka před implementací výslovně pojmenovat a navrhnout řešení, které rovněž zabrání probliknutí původního vzhledu.
- Lehká kontrola každé budoucí stylované komponenty musí zahrnout první zobrazení, změny stavu a základní interakci, aby se ověřilo, že nativní vzhled nikdy neprosvítá.
