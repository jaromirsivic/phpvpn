# 013 - Formátování JSONu a moderní editor v Edit Config

## Cíl

Upravit okno **Edit Config** tray aplikace tak, aby se načtená platná konfigurace automaticky zobrazovala jako přehledně odsazený JSON a víceřádková editační oblast vizuálně odpovídala moderním prvkům aplikace.

Editor má mít zaoblené rohy, neutrální rámeček bez fokusu a zřetelné modré orámování při fokusu. Po přesunu fokusu mimo editor se má vrátit do běžného stavu.

## Rozsah

Úprava zahrnuje pouze:

- prezentaci JSONu při jeho načtení do okna **Edit Config**,
- vizuální podobu víceřádkové editační oblasti,
- reakci jejího rámečku na získání a ztrátu fokusu,
- nezbytné úpravy layoutu, vykreslování, resize a DPI chování přímo dotčené komponenty.

Neměnit význam konfigurace, pravidla její validace, ukládací workflow, IPC kontrakt ani obsah ostatních oken.

## Automatické formátování načteného JSONu

- Pokaždé, když **Edit Config** načte konfigurační JSON a připravuje jej k zobrazení v editoru, pokusit se text rozparsovat jako JSON.
- Pokud je načtený obsah platný JSON, zobrazit jej v přehledné víceřádkové podobě s konzistentním odsazením podle úrovně zanoření objektů a polí.
- Použít stabilní běžný pretty-print formát vhodný pro ruční editaci. Preferované odsazení jsou dvě mezery na jednu úroveň zanoření, pokud stávající společná JSON infrastruktura projektu neurčuje jiný již používaný konzistentní formát.
- Zachovat datový význam JSONu. Formátování nesmí měnit názvy polí, řetězcové hodnoty, čísla, booleany, `null`, strukturu objektů ani pořadí členů, které poskytne použitý parser nebo existující datový model.
- Formátování se týká textu načteného do editoru při otevření nebo opětovném načtení dat. Nemá při každém stisku klávesy průběžně přepisovat uživatelský text, přesouvat caret ani narušovat rozpracovanou ruční editaci.
- Pokud načtený text nelze jako JSON rozparsovat, aplikace nesmí spadnout ani obsah tiše zahodit. Zachovat původní načtený text a stávající možnost uživatele chybu opravit nebo použít existující workflow.
- Nezavádět automatické doplňování, syntaktické zvýrazňování, skládání bloků ani jinou funkci editoru, která nebyla výslovně požadována.

## Moderní vzhled víceřádkového editoru

- Víceřádkovou editační oblast v **Edit Config** zobrazit jako jednu ucelenou moderní komponentu se zaoblenými rohy.
- Vizuální jazyk má navázat na současnou zaoblenou tabulku a na moderní jednořádkové textboxy zavedené mikro-updatem 009.
- Zachovat nativní víceřádkovou editaci a oba stávající posuvníky. Vlastní styl nesmí nahradit standardní funkce neúplnou ruční emulací.
- Odstranit nebo skrýt původní ostrý nativní rámeček tak, aby nevznikal dvojitý obrys, tmavá vnitřní hrana ani mezera mezi editorem a vlastním rámečkem.
- Zaoblení a obrys musí být čisté, spojité, symetrické a vyhlazené při podporovaném DPI.
- Editor a jeho rámeček musí zachovat současné rozměrové chování, dostupnost posuvníků a správný přepočet při změně velikosti okna.

### Běžný stav bez fokusu

- Použít jemný neutrální šedý obrys a čisté světlé pozadí odpovídající současnému stylu aplikace.
- Rohy mají být lehce zaoblené podobně jako u existujících moderních prvků.
- Text, caret a posuvníky nesmějí působit natěsno u rámečku. Zachovat vhodné vnitřní odsazení bez zmenšení použitelné plochy více, než je nezbytné.

### Stav s fokusem

- Jakmile fokus získá vlastní víceřádkové textové pole nebo relevantní vnitřní část editoru, změnit obrys celé komponenty na modrý.
- Modrý focus stav má vizuálně odpovídat moderním jednořádkovým textboxům aplikace. Může použít mírně zesílený obrys a jemné vnitřní modré zvýraznění.
- Focus indikace nesmí změnit vnější rozměr komponenty, posunout text, posuvníky ani okolní layout.
- Fokus musí být patrný při kliknutí myší i při přesunu klávesou Tab.

### Ztráta fokusu

- Jakmile fokus opustí editor a přejde na jiný ovládací prvek nebo mimo okno, modré zvýraznění odstranit.
- Komponenta se musí bez blikání a bez zbytkových pixelů vrátit do běžného neutrálního stavu.

## Zachování nativního chování editoru

Úprava musí zachovat zejména:

- víceřádkové zadávání a mazání textu,
- caret, výběr textu a scrollování,
- vertikální i horizontální posuvník,
- Ctrl+A, Ctrl+C, Ctrl+X a Ctrl+V,
- Undo a Redo poskytované stávajícím editorem,
- systémové kontextové menu,
- Tab a Shift+Tab podle současného dialogového chování,
- fokus myší i klávesnicí,
- chování při změně velikosti okna a DPI,
- stávající validaci, Save, Reset, Cancel a ochranu před ztrátou rozpracovaných změn.

## Vykreslování a životní cyklus

- Komponenta musí být v konečném moderním vzhledu už při prvním viditelném snímku okna. Starý nativní rámeček nesmí krátce probliknout.
- Získání a ztráta fokusu, resize, změna DPI, scrollování a překreslení musí invalidovat potřebnou oblast a vykreslit ji bez duchů, zdvojených hran nebo artefaktů.
- Pokud se pro styl použije hostitelský kontejner kolem nativního editoru, fokus v nativním editoru se musí promítnout do stavu rámečku hostitele.
- Správně ošetřit vytvoření a zničení okna i případné subclassing nebo uložené odkazy. Po zavření nesmí zůstat neplatný odkaz na editor nebo jeho hostitele.

## Zachování ostatního chování

- Neměnit cestu ke konfiguraci ani způsob jejího načítání a ukládání nad rámec prezentace platného načteného JSONu.
- Neměnit význam tlačítek **Save**, **Reset** a **Cancel**.
- Neměnit validační pravidla ani chybová hlášení, pokud to není nezbytné pro bezpečné zachování neplatného načteného textu.
- Neměnit jiné stránky, dialogy, tabulky ani textboxy tray aplikace.
- Neupravovat běžnou projektovou dokumentaci.

## Akceptační kritéria

1. Při každém načtení platného konfiguračního JSONu do **Edit Config** je obsah automaticky zobrazen v přehledné víceřádkové podobě s konzistentním odsazením podle zanoření.
2. Automatické formátování zachová datový význam konfigurace a nepoškodí řetězcové ani ostatní hodnoty.
3. Automatické formátování neprobíhá při každém stisku klávesy a nenarušuje ruční editaci, caret ani výběr textu.
4. Neplatný načtený JSON nezpůsobí pád ani tiché odstranění obsahu. Editor zachová původní text pro opravu existujícím workflow.
5. Víceřádkový editor má zaoblené rohy a v běžném stavu jemný neutrální obrys bez původního dvojitého nativního rámečku.
6. Po získání fokusu kliknutím nebo klávesnicí se rámeček celého editoru zřetelně změní na modrý.
7. Po ztrátě fokusu modré zvýraznění zmizí a editor se vrátí do běžného neutrálního stavu.
8. Přepínání focus stavu nemění geometrii, neposouvá text ani okolní prvky a nezanechává grafické artefakty.
9. Oba posuvníky, výběr, caret, schránka, Undo, kontextové menu a standardní víceřádková editace zůstávají funkční.
10. Editor i jeho obrys se správně přepočítávají a překreslují při resize okna a podporovaném DPI.
11. Stávající Save, Reset, Cancel, validace a ochrana rozpracovaných změn fungují beze změny.
12. Ostatní části tray aplikace nejsou mikro-updatem funkčně ani vizuálně změněny.

## Omezení implementace a ověření

- Neprovádět komplexní testy.
- Nevytvářet nové testy.
- Existující test upravit pouze tehdy, pokud je potřebná změna malá, přímočará a rychlá. Rozsáhlejší úpravy testů odložit.
- Neupravovat běžnou projektovou dokumentaci, včetně `protocol.md`, `client.md`, `tray_UX.md` a plánovacích dokumentů.
- Po implementaci zkompilovat tray aplikaci, vyřešit chyby způsobené změnou a zkopírovat výslednou sestavu do distribuce pro ruční instalaci a ověření uživatelem.
- Neprovádět commit ani push, pokud o to uživatel výslovně nepožádá.

## Hranice formalizační fáze

Tento soubor vytvořila hlavní koordinátorka osobně před založením implementačního úkolu. Vytvoření ani zápis formálního zadání nebyly delegovány na jiného agenta.

Implementující agent smí začít až podle tohoto zadání a po uživatelově výslovné volbě modelu a effortu. Formální zadání je jediná změna provedená v této fázi.
