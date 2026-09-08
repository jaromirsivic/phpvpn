# 007 - Responzivní navigace kroků Configuration Wizardu

## Cíl

Upravit navigaci kroků v okně **Configuration Wizard** tray aplikace tak, aby měla stabilní a čisté rozložení v širokém i úzkém režimu. V úzkém režimu se nesmějí ořezávat názvy kroků ani posouvat navigace při přechodu mezi kroky.

## Rozsah

- Navigace kroků v okně **Configuration Wizard** tray aplikace.
- Široké rozložení s navigací vlevo a obsahem vpravo.
- Úzké responzivní rozložení s navigací nahoře a obsahem pod ní.
- Vizuální oddělovače mezi navigací, obsahem a patičkou.
- Zobrazení nedokončených, aktuálních a úspěšně dokončených kroků.

## Široký režim

- Navigace kroků zůstane vlevo a obsah aktuálního kroku vpravo.
- Mezi levou navigační oblast a pravou obsahovou oblast přidat jemnou svislou oddělovací čáru o tloušťce přesně 1 pixel.
- Zachovat plné textové názvy kroků a stávající informační texty kroků.
- Aktivní, dokončené a nedokončené kroky si zachovají srozumitelnou vizuální hierarchii.
- Rozložení nesmí ořezávat názvy kroků ani měnit polohu navigace při přechodu mezi kroky.

## Úzký responzivní režim

- Navigace kroků se přesune nad obsah.
- Mezi horní navigaci a obsah přidat jemnou vodorovnou oddělovací čáru o tloušťce přesně 1 pixel.
- Odstranit všechny textové názvy kroků, včetně názvu právě aktivního kroku.
- Zobrazit pouze kruhová označení kroků, rovnoměrně rozmístěná po dostupné šířce navigace.
- Nedokončené a aktuální kroky zobrazí číslo kroku.
- Úspěšně dokončené kroky zobrazí zelenou fajfku namísto čísla.
- Aktivní krok bude jednoznačně označen modrým kolečkem.
- Odstranit světle modré obdélníkové pozadí aktivní položky. Aktivní stav v úzkém režimu vyjadřuje samotné modré kolečko.
- Přechod mezi kroky nesmí měnit rozměry ani polohu horní navigace.

## Oddělovače

- Nová svislá čára v širokém režimu, nová vodorovná čára v úzkém režimu a stávající vodorovný oddělovač mezi hlavní obsahovou oblastí a patičkou musí používat stejný jemný šedý vizuální styl.
- Každý z těchto oddělovačů bude mít tloušťku přesně 1 pixel.
- Stávající oddělovač nad patičkou ponechat jemný a 1 pixel tlustý. Nezvětšovat jej na 2 pixely.
- Čáry musí zůstat ostré, souvislé a stabilní při změně velikosti okna i při podporovaném DPI.

## Stabilita rozložení

- Responzivní přepnutí mezi širokým a úzkým režimem musí vycházet z dostupné šířky okna a používat jeden konzistentní breakpoint.
- Po přepnutí režimu se navigace i obsah kompletně a čistě přepočítají bez překrytí, zbytkových pixelů nebo zdvojených oddělovačů.
- Navigace musí zůstat na stejném místě při přechodu mezi jednotlivými kroky stejného režimu.
- V úzkém režimu nesmí skrytý text nadále zabírat místo ani ovlivňovat výpočet rozložení.
- Cílové řešení musí odstranit vadu z reference, kde se název **Server secret** zobrazí pouze jako **Server**.
- Změna musí respektovat souvislé překreslování při resize a owner-draw pravidla zavedená předchozími GUI mikro-updaty.

## Beze změny

- Pořadí, počet, názvy a informační texty kroků.
- Obsah jednotlivých kroků a jejich ovládací prvky.
- Logika určující aktuální, dokončený a nedokončený krok.
- Funkce tlačítek, klávesové ovládání, tab order a aplikační workflow.
- Validace, ukládání konfigurace, IPC, síťová komunikace a ostatní aplikační logika.
- Vzhled ostatních oken tray aplikace.

## Akceptační kritéria

1. V širokém režimu je navigace vlevo, obsah vpravo a obě oblasti odděluje jemná svislá šedá čára tlustá 1 pixel.
2. V širokém režimu zůstávají viditelné celé názvy a stávající informační texty kroků bez ořezu.
3. V úzkém režimu je navigace nahoře a od obsahu ji odděluje jemná vodorovná šedá čára tlustá 1 pixel.
4. V úzkém režimu není viditelný žádný textový název kroku, a to ani u aktivního kroku.
5. V úzkém režimu jsou viditelná pouze rovnoměrně rozmístěná kruhová označení s čísly nebo zelenými fajfkami.
6. Aktivní krok je jednoznačně označen modrým kolečkem a nemá světle modré obdélníkové pozadí.
7. Úspěšně dokončené kroky zobrazují zelenou fajfku, nedokončené a aktuální kroky zobrazují číslo.
8. Oddělovač mezi hlavním obsahem a patičkou zůstává 1 pixel tlustý. Všechny tři typy oddělovačů mají shodný jemný šedý styl.
9. Názvy kroků se neořezávají a navigace se při přechodu mezi kroky neposouvá.
10. Přechod přes responzivní breakpoint nezpůsobí překrytí, blikání, zbytkové pixely, dvojité čáry ani nestabilní rozložení.
11. Široký režim zachová plný informační obsah navigace a změna nezasáhne workflow ani aplikační logiku.

## Referenční obrázky

- `wide-layout-left-navigation.png` zachycuje široké rozložení s navigací kroků vlevo.
- `narrow-layout-active-step-1.png` zachycuje úzké rozložení s aktivním prvním krokem.
- `narrow-layout-active-step-2-cropped-label.png` zachycuje úzké rozložení s aktivním druhým krokem a nežádoucím ořezem názvu **Server secret** na **Server**.

Referenční snímky dokumentují výchozí stav a konkrétní vady. Při implementaci mají být rozhodující požadavky tohoto zadání.

## Kontrola až po pozdější implementaci

- Ručně projít všechny kroky ve širokém i úzkém režimu a ověřit jejich stavy.
- Ověřit rozložení těsně nad a pod responzivním breakpointem a několikrát změnit šířku okna oběma směry.
- Zkontrolovat úplné názvy v širokém režimu, absenci všech názvů v úzkém režimu, rovnoměrné rozmístění koleček a stabilní polohu navigace.
- Zkontrolovat čísla nedokončených a aktuálních kroků, zelené fajfky dokončených kroků a modré kolečko aktivního kroku.
- Vizuálně ověřit, že všechny oddělovače jsou jemné, šedé, ostré a přesně 1 pixel tlusté.
- Neprovádět komplexní testy a nevytvářet ani neupravovat testy v rámci této mikroúpravy.

## Stav a hranice současné fáze

Tento dokument pouze formalizuje schválené zadání před pozdější implementací. V současné fázi se nesmí měnit zdrojový kód, testy ani běžná projektová dokumentace. Nesmějí se vytvářet nové testy ani upravovat stávající testy. Neprovádí se kompilace, sestavení, kopírování do distribuce ani nasazení.
