# 006 - Souvislé překreslení všech oken při změně velikosti

## Cíl

Odstranit grafické stopy, roztrhané texty, opakované oddělovače a další staré pixely, které zůstávají při pomalém živém zmenšování nebo zvětšování oken tray aplikace. Oprava musí být společná a konzistentní, ne sada lokálních záplat pouze pro zachycené snímky.

## Potvrzená příčina

- Vlastně kreslené plochy, podřízené Win32 prvky a oddělovače se při `WM_SIZE` přesouvají a invalidují v různých okamžicích.
- Některá okna invalidují pouze část plochy nebo jen podřízený obsah, jiná požádají o úplné překreslení až po sérii viditelných mezikroků.
- Mazání pozadí je kvůli omezení blikání potlačené, ale odkrytá stará plocha není vždy nahrazena novým souvislým snímkem.
- Asynchronní invalidace během pomalého live resize dovolí Windows zobrazit kombinaci starého a nového rozložení.

## Rozsah

Prověřit a opravit všechna okna tray aplikace, jejichž velikost lze měnit nebo jejichž rozložení se přepočítává:

- Configuration Wizard včetně body oblasti, sidebaru, karet, oddělovače patičky a tlačítek.
- Statistics včetně nadpisů, popisu, hodnot, klíčů, karet a oddělovačů.
- Communication Contracts včetně horní oblasti, body, headeru, viewportu, tabulky, dynamických řádků, scrollbars a patičky.
- Edit Config.
- Progress.
- Password SHA.
- Každé další vlastní okno nebo podokno tray aplikace, které používá stejný resize, layout nebo paint mechanismus.

## Požadované chování

- Při každém kroku živého resize musí být po přepočtu layoutu viditelný jediný konzistentní snímek aktuálního rozložení.
- Stará i nová poloha přesunutých prvků, oddělovačů, karet a textů musí být korektně invalidována a překreslena.
- Nesmí zůstat horizontální nebo vertikální čáry, útržky znaků, duchy předchozího textu, části starých rámečků ani směs několika velikostí layoutu.
- Přesun většího množství podřízených HWND musí proběhnout dávkově bez viditelného překreslování každého prvku v mezilehlé poloze.
- Kde je to vhodné, použít zákaz kopírování starých pixelů při změně pozice a jednotné překreslení rodiče i podřízených prvků až po dokončení layoutu.
- Potlačení `WM_ERASEBKGND` lze zachovat pouze tehdy, pokud vlastní paint cesta vždy zcela vyplní invalidovanou plochu správným pozadím.
- Oprava nesmí zavést výrazné blikání, černé nebo bílé mezisnímky ani nadměrně pomalý resize.
- Owner-draw pravidlo z mikroúpravy 004 zůstává závazné. Původní Windows vzhled žádné stylované komponenty nesmí probliknout.
- Po dokončení resize musí zůstat správný fokus, tab order, checked stavy, hodnoty, scroll pozice a přístupnost.
- Oprava nesmí měnit aplikační workflow, obsah konfigurace ani síťové chování.

## Akceptační kritéria

1. Pomalé opakované tažení každé hrany a rohů okna nezanechá žádné staré pixely ani roztrhaný obsah.
2. Wizard nezanechá více historických pozic oddělovače patičky.
3. Statistics zachová čitelné nadpisy, popis, názvy, klíče a hodnoty při zmenšování i následném zvětšování.
4. Communication Contracts zachová čitelnou horní oblast, header i všechny viditelné řádky bez útržků textu.
5. Edit Config, Progress a Password SHA nevykážou stejnou třídu vady.
6. Resize je vizuálně souvislý a nevytváří nové blikání.
7. Scrollbars, layout breakpointy, minimální velikost, DPI a child controls se po resize nacházejí ve správné poloze.
8. Owner-draw tlačítka a switche zůstanou moderní od prvního snímku a jejich chování se nezmění.

## Důkazní snímky

- `footer-resize-artifacts.png` zachycuje opakované horizontální čáry v patičce Wizardu po pomalém vertikálním zmenšování.
- `statistics-resize-artifacts.png` zachycuje roztrhané texty, nadpisy, hodnoty a části karet po zmenšení a pomalém roztažení Statistics.
- `contracts-resize-artifacts.png` zachycuje útržky nadpisu a popisu v Communication Contracts po pomalé změně šířky.

## Lehká kontrola po implementaci

- Ručně nebo rychlým cíleným UI scénářem několikrát pomalu zmenšit a zvětšit Wizard, Statistics, Contracts, Edit Config, Progress a Password SHA vodorovně, svisle a přes roh.
- U Statistics překročit responzivní breakpoint oběma směry.
- U Contracts ověřit více řádků, scroll a resize tabulky.
- Krátce ověřit jedno zvýšené DPI.
- Zkontrolovat, že nezůstávají staré pixely a nevzniklo nové blikání.
- Testy vytvářet, upravovat nebo spouštět pouze tehdy, pokud je to opravdu rychlé a přímo zaměřené na změnu. Časově náročné a plošné testování přeskočit.
- Neupravovat klientskou ani jinou uživatelskou dokumentaci.
- Po opravě sestavit Release tray EXE, obnovit standardní distribuční strukturu a ověřit manifestové hashe a podpis.

## Stav

Uživatel výslovně schválil implementaci modelem Astra s úrovní Medium, bez těžkých testů a bez změny dokumentace.
