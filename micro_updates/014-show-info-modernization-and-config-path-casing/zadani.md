# 014 - Modernizace Server Information, cesta konfigurace a minimální rozměry oken

## Cíl

Vizuálně sjednotit okno **Show Info** s již modernizovaným oknem **Edit Config**, přejmenovat příslušnou položku tray menu na výstižnější `Show Server Info` a opravit textovou podobu cesty ke konfiguraci zobrazované v **Edit Config**.

Okno **Show Info** má nově působit jako samostatná moderní read-only prezentace serverových informací. Má obsahovat zřetelnou hlavičku, zaoblenou víceřádkovou informační oblast a patičku s jediným tlačítkem `OK`.

V **Edit Config** se má cesta zobrazit se stejnou velikostí písmen, jakou má skutečný adresář na disku. Současné zobrazení `C:\ProgramData\PHPVPN\config.json` vzniká z pevně zapsaného názvu produktového adresáře `PHPVPN`, a proto neodpovídá skutečnému názvu `phpvpn` viditelnému v Průzkumníku.

Okna **Server Information**, **Edit Config** a **Statistics** současně dostanou rozumné minimální rozměry. Uživatel je nesmí zmenšit tak, že jejich hlavní obsah přestane být prakticky čitelný nebo použitelný.

## Rozsah

Úprava zahrnuje pouze:

- hlavičku okna **Show Info**,
- vizuální podobu a read-only stav víceřádkové oblasti se serverovými informacemi,
- focus stav této oblasti,
- patičku okna **Show Info** a tlačítko `OK`,
- přejmenování položky tray menu `Show Info` na `Show Server Info`,
- nezbytné změny layoutu, vykreslování, resize a DPI chování přímo dotčeného okna,
- opravu velikosti písmen v cestě ke konfiguraci zobrazované v **Edit Config**,
- minimální šířku a výšku oken **Server Information**, **Edit Config** a **Statistics**.

Neměnit obsah ani význam odpovědi `get-info`, způsob jejího načítání, IPC kontrakty, ukládání konfigurace ani skutečné umístění konfiguračního souboru.

## Přejmenování položky tray menu

- Položku `Show Info` v hlavním tray menu přejmenovat na přesné znění `Show Server Info`.
- Nový název vyjadřuje akci a jednoznačně rozlišuje serverové informace od ostatních informačních nebo statistických obrazovek.
- Vnitřní hlavní nadpis otevřeného formuláře zůstává názvem obsahu `Server information`.
- Přejmenování nesmí změnit pozici položky v menu, její ikonu, command ID, dostupnost, klávesnicové chování ani obsluhu příkazu `get-info`.

## Hlavička Show Info

- Do horní části klientské plochy přidat výrazný hlavní nadpis `Server information`.
- Nadpis má typografií, velikostí, váhou a vodorovným zarovnáním navázat na nadpis `Edit configuration` v okně **Edit Config**.
- Pod nadpis nepřidávat cestu ke konfiguraci ani jiný smyšlený podnadpis. Show Info pracuje s výsledkem `get-info`, nikoli s konfiguračním souborem.
- Systémový titulek okna `PHPVPN server information` zachovat, pokud jeho změna není nutná pro technické sjednocení okna.

## Moderní read-only oblast serverových informací

- Serverové informace zobrazovat v jedné velké víceřádkové textové oblasti pod hlavičkou.
- Komponenta má vizuálně vycházet z moderního víceřádkového editoru v **Edit Config**, včetně zaoblených rohů, vhodného vnitřního odsazení a čistého napojení nativního textového pole na vlastní vykreslený povrch.
- Textová oblast musí zůstat skutečně pouze pro čtení. Uživatel nesmí její obsah měnit, mazat ani vložením přepisovat.
- Read-only stav vyjádřit lehce zašedlým povrchem celé vnitřní informační oblasti. Pozadí musí být dostatečně odlišné od editovatelného editoru, ale text musí zůstat dobře čitelný a kontrastní.
- Odstranit původní ostrý nativní rámeček a případný dvojitý obrys. Rohy a hrany musí být spojité, symetrické a vyhlazené.
- Zachovat nativní výběr textu, kopírování, `Ctrl+A`, `Ctrl+C`, kontextové menu a potřebné vertikální i horizontální posouvání.
- Zachovat stávající formátování a celý obsah serverových informací. Text se nesmí zkracovat ani funkčně měnit.

### Stav bez fokusu

- Pokud informační oblast nemá fokus, nemá kolem ní být výrazný samostatný obrys.
- Oddělení lehce zašedlé read-only plochy od pozadí okna má zůstat patrné díky tvaru, barvě povrchu a případně pouze velmi jemné neutrální hraně nutné pro čisté vykreslení.
- Nesmí vzniknout tmavý nativní vnitřní rámeček ani dvojitá hrana.

### Stav s fokusem

- Po získání fokusu kliknutím nebo klávesou Tab zobrazit kolem celé zaoblené komponenty decentní tmavě šedý obrys.
- Focus obrys má být zřetelný, ale méně akční než modrý focus stav editovatelného pole v **Edit Config**. Tím se zachová informace, že obsah lze označit a kopírovat, nikoli editovat.
- Přepnutí focus stavu nesmí změnit vnější rozměr komponenty, posunout text, posuvníky ani okolní layout.
- Po ztrátě fokusu se tmavě šedý obrys odstraní a komponenta se bez blikání vrátí do běžného read-only stavu.

## Patička a tlačítko OK

- Do spodní části okna **Show Info** přidat samostatnou patičku oddělenou od obsahu jemnou vodorovnou linkou stejně jako v **Edit Config**.
- Patička obsahuje pouze tlačítko `OK`.
- Tlačítko umístit vpravo a použít stejný moderní vzhled, rozměry a odsazení jako odpovídající tlačítka v ostatních modernizovaných oknech tray aplikace.
- `OK` zavře okno bez vedlejších akcí.
- Klávesa Enter má aktivovat `OK`, pokud tím nebude narušeno očekávané ovládání read-only textové oblasti.
- Escape a systémové zavření tlačítkem X mají nadále okno bezpečně zavřít.
- Patička i tlačítko musí zůstat správně umístěné při změně velikosti okna a DPI.

## Layout, vykreslování a životní cyklus

- Okno musí mít konečný moderní vzhled již při prvním viditelném snímku. Původní nativní rámeček nesmí krátce probliknout.
- Hlavička, informační oblast a patička se nesmějí překrývat ani při minimální podporované velikosti okna.
- Při resize se má měnit především plocha serverových informací. Hlavička a patička si zachovají stabilní výšku a odsazení.
- Zaoblený povrch, focus obrys a oddělovač patičky se musí správně překreslit při resize, změně DPI, aktivaci okna, změně fokusu a scrollování.
- Nesmějí zůstávat duchy, zdvojené hrany, pruhy nebo jiné grafické artefakty.
- Opakované otevření a zavření Show Info nesmí ponechat neplatné odkazy, subclassing ani GDI nebo USER prostředky.

## Minimální rozměry oken

### Společná pravidla

- Nastavit skutečná DPI-aware minima prostřednictvím nativního omezení velikosti okna, aby je nešlo obejít tažením za žádnou hranu ani roh.
- Minimální hodnoty definovat v logických pixelech neboli DIP a při použití přepočítat podle aktuálního DPI monitoru.
- Do minimální track velikosti správně započítat systémový titulek a vnější rám okna. Požadovaná použitelná velikost se vztahuje ke klientské ploše.
- Na pracovní ploše menší než definované minimum nesmí okno skončit mimo dosažitelnou oblast. V takovém případě bezpečně omezit minimum dostupnou pracovní plochou aktuálního monitoru a zachovat existující fallback layout nebo scrollování.
- Minima nesmějí měnit výchozí velikost oken. Ovlivňují pouze nejmenší velikost dosažitelnou uživatelem.
- Při změně DPI nebo přesunu mezi monitory musí být omezení přepočítané bez skoku do chybné velikosti či mimo pracovní plochu.

### Server Information a Edit Config

- Pro oba režimy sdíleného editorového okna použít minimální klientskou velikost **640 × 360 DIP**.
- Minimum musí v **Server Information** zachovat viditelný nadpis, použitelnou víceřádkovou oblast a celou patičku s tlačítkem `OK`.
- Minimum musí v **Edit Config** zachovat nadpis, jednořádkovou cestu, prakticky použitelnou část konfiguračního editoru a celou patičku s tlačítky `Reset`, `Cancel` a `Save`.
- Při minimální velikosti se tlačítka nesmějí překrývat, ořezat ani změnit na neovladatelné prvky. Oba scrollbary editoru musí zůstat dosažitelné.

### Statistics

- Pro okno **Statistics** použít minimální klientskou velikost **640 × 480 DIP**.
- Při této velikosti musí zůstat čitelná hlavička a smysluplná část statistických karet. Existující vertikální scrollování má zpřístupnit zbytek obsahu.
- Karty, popisky, klíče a hodnoty se nesmějí geometricky zhroutit, překrýt nebo zcela zmizet.
- Neměnit počet, pořadí, význam ani aktualizaci statistických hodnot.

## Velikost písmen v cestě Edit Config

### Zjištěná příčina

- Služba aktuálně sestavuje datový adresář připojením pevně zapsané konstanty `PHPVPN` k systémové cestě `%ProgramData%`.
- Takto sestavenou cestu následně vrací jako autoritativní `configPath` a tray ji v **Edit Config** zobrazuje beze změny.
- Běžný Windows souborový systém porovnává tuto cestu bez ohledu na velikost písmen, takže `C:\ProgramData\PHPVPN\config.json` a `C:\ProgramData\phpvpn\config.json` odkazují na stejné umístění. Text služby však nezachovává velikost písmen skutečného názvu adresáře viditelnou v Průzkumníku.

### Požadované chování

- V **Edit Config** zobrazovat cestu s velikostí písmen odpovídající skutečným názvům existujících částí cesty na disku.
- V aktuálním standardním umístění má být výsledkem `C:\ProgramData\phpvpn\config.json`, pokud se adresář na disku skutečně jmenuje `phpvpn`.
- Nepoužívat pouze slepé převádění celé cesty na malá písmena. Systémová část cesty, uživatelské nebo nestandardní adresáře mohou mít vlastní uloženou velikost písmen, kterou je třeba zachovat.
- Pokud skutečnou velikost písmen nelze bezpečně zjistit, zachovat platnou autoritativní cestu a neblokovat otevření **Edit Config**.
- Oprava je prezentační. Nesmí změnit fyzické umístění souboru, vytvářet druhý produktový adresář, přesouvat nebo přejmenovávat existující data ani měnit oprávnění.
- Neměnit význam nebo bezpečnostní validaci `configPath` v IPC. Pokud bude oprava provedena již při sestavení autoritativní cesty ve službě, musí zůstat zachována absolutnost, validace a použití stejného fyzického cíle všemi dotčenými operacemi.
- Ostatní texty, hlavičku, editor a tlačítka okna **Edit Config** v tomto mikro-updatu neměnit.

## Zachování ostatního chování

- Neměnit příkaz `get-info`, jeho timeout, validaci odpovědi ani bezpečnostní omezení zobrazovaných informací.
- Neměnit JSON serializaci serverových informací ani pořadí a hodnoty vrácených dat.
- Neměnit editační, validační, Reset, Save, Cancel ani lifecycle chování **Edit Config**.
- Neměnit umístění konfigurace, způsob jejího čtení a ukládání ani oprávnění adresáře.
- Neměnit jiná okna, dialogy nebo položky tray menu.
- Výjimkou z předchozího bodu je pouze výslovně požadované přejmenování `Show Info` na `Show Server Info` a minimální rozměry okna **Statistics**.
- Neupravovat běžnou projektovou dokumentaci.

## Akceptační kritéria

1. **Show Info** obsahuje v horní části zřetelný nadpis `Server information` vizuálně odpovídající nadpisu **Edit Config**.
2. Pod hlavičkou je jedna velká víceřádková oblast se zaoblenými rohy a celým dosavadním obsahem serverových informací.
3. Informační oblast je pouze pro čtení a její lehce zašedlé pozadí tento stav vizuálně odlišuje od editovatelného editoru.
4. Bez fokusu nemá informační oblast výrazný rámeček ani původní ostrou nebo dvojitou nativní hranu.
5. Při fokusu získá celá zaoblená oblast decentní tmavě šedý obrys, který po ztrátě fokusu zmizí.
6. Přepnutí focus stavu neposouvá obsah, posuvníky ani okolní layout a nezanechává grafické artefakty.
7. Výběr textu, `Ctrl+A`, `Ctrl+C`, kontextové menu a potřebné posuvníky zůstávají funkční, zatímco změna obsahu není možná.
8. Spodní patička je oddělena jemnou linkou a obsahuje pouze moderní tlačítko `OK` zarovnané vpravo.
9. Tlačítko `OK`, Escape a systémové X bezpečně zavírají Show Info bez dalších akcí.
10. Hlavička, obsah i patička se správně přepočítávají a překreslují při resize a podporovaném DPI.
11. **Edit Config** zobrazuje pro existující standardní adresář pojmenovaný `phpvpn` cestu `C:\ProgramData\phpvpn\config.json`, nikoli uměle sestavenou variantu s `PHPVPN`.
12. Zobrazená cesta respektuje skutečnou velikost písmen existujících částí cesty a nevzniká pouhým převedením celé cesty na malá písmena.
13. Oprava cesty nemění fyzický soubor, umístění dat, oprávnění, IPC bezpečnost ani operace čtení a ukládání konfigurace.
14. Položka tray menu se jmenuje přesně `Show Server Info`, zůstává na původním místě a spouští stejnou server-information akci jako dosud.
15. Server Information ani Edit Config nelze na běžně velké pracovní ploše zmenšit pod klientskou velikost 640 × 360 DIP.
16. Statistics nelze na běžně velké pracovní ploše zmenšit pod klientskou velikost 640 × 480 DIP.
17. Při minimálních velikostech zůstává hlavní obsah každého okna prakticky čitelný a dostupný bez překryvů či kolapsu ovládacích prvků.
18. Minima se správně škálují podle DPI, nepřesahují menší pracovní plochu a nemění výchozí velikost oken.
19. Ostatní chování Server Information, Edit Config, Statistics a tray aplikace zůstává beze změny.

## Omezení implementace a ověření

- Neprovádět komplexní testy.
- Nevytvářet nové testy.
- Existující test upravit pouze tehdy, pokud je potřebná změna malá, přímočará a rychlá. Rozsáhlejší úpravy testů odložit.
- Neupravovat běžnou projektovou dokumentaci, včetně `protocol.md`, `client.md`, `tray_UX.md` a plánovacích dokumentů.
- Po implementaci zkompilovat tray aplikaci, vyřešit chyby způsobené změnou a zkopírovat výslednou sestavu do distribuce pro ruční instalaci a ověření uživatelem.
- Neprovádět commit ani push, pokud o to uživatel výslovně nepožádá.

## Hranice formalizační fáze

Tento soubor vytvořila hlavní koordinátorka osobně před založením implementačního úkolu. Vytvoření ani zápis formálního zadání nebyly delegovány na jiného agenta.

Implementace smí začít až po uživatelově výslovné volbě modelu a effortu. Formální zadání je jediná změna provedená v této fázi.
