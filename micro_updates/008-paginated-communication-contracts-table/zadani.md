# 008 - Stránkovaná tabulka Communication contracts

## Cíl

Upravit tabulku v okně **Communication contracts** tak, aby:

- se vzhledem přiblížila schválenému referenčnímu návrhu,
- zobrazovala nejvýše 20 kontraktů na jedné stránce,
- nevytvářela ani nevykreslovala řádkové GUI prvky pro všechny kontrakty současně,
- při stránkování bezpečně zachovala všechny rozpracované změny až do potvrzení tlačítkem **Save**.

Změna optimalizuje především množství současně existujících GUI prvků. Sama o sobě neslibuje rychlejší načítání dat ze serveru.

## Reference

Vizuálním vodítkem je soubor `communication-contracts-table-reference.png`, který je kopií schváleného obrázku `assets/communication_contracts1.png`.

Reference určuje zamýšlený charakter tabulky, zejména čistý kontejner, zaoblené rohy a vizuálně oddělené záhlaví. Není samostatnou instrukcí. Při rozporu platí text tohoto zadání.

Starší snímek s překryvem hlasového ovládání se nepoužívá.

## Rozsah

Úprava zahrnuje pouze:

- vnější kontejner tabulky,
- záhlaví tabulky,
- vykreslení datových řádků,
- stránkovací lištu,
- uchování rozpracovaných hodnot při změně stránky.

Zachovat stávající sloupce **Contract**, **Enabled**, **Key** a **Actions**, jejich pořadí, význam a funkce.

Ostatní části okna ani jiná okna aplikace neredesignovat.

## Vzhled tabulky

- Tabulku umístit do čistého vnějšího kontejneru se zaoblenými rohy.
- Záhlaví odlišit jemným světle šedým pozadím.
- Názvy sloupců zobrazit tučně.
- Zachovat čitelné oddělení záhlaví a jednotlivých řádků.
- Zachovat současný celkový vizuální jazyk aplikace a vzhled stávajících řádkových ovládacích prvků, pokud toto zadání výslovně neurčuje změnu.

## Minimální velikost okna a patička

- Nastavit minimální klientskou plochu okna na **920 × 520 DIP**.
- Minimální vnější rozměr okna odvodit z klientské plochy způsobem správným pro aktuální DPI a omezit jej dostupnou pracovní plochou monitoru, aby okno zůstalo dosažitelné i na menší obrazovce.
- Patičku s hlavním tlačítkem vizuálně oddělit od obsahové oblasti jemnou horizontální čárou ve stávající barvě oddělovačů a o tloušťce jednoho fyzického pixelu.
- Přejmenovat viditelný text hlavního tlačítka z **OK** na **Save**.
- Zachovat stávající identitu tlačítka, potvrzovací chování a ukládací workflow. Jde pouze o změnu popisku.

## Stránky a řádkové prvky

- Pevná velikost stránky je 20 kontraktů.
- Poslední stránka může obsahovat méně než 20 kontraktů.
- Nepřidávat volič velikosti stránky.
- Pro tabulku současně vytvořit a vykreslovat řádkové GUI prvky pouze pro kontrakty na aktuální stránce, tedy nejvýše pro 20 datových řádků.
- Nestačí pouze skrýt řádkové prvky ostatních stránek, pokud by nadále zůstaly všechny vytvořené a zatěžovaly GUI.
- Stránkování nesmí změnit pořadí ani identitu kontraktů.
- Při nejvýše 20 kontraktech stránkovací lištu nezobrazovat.

## Stránkovací lišta

Lištu umístit pod tabulku. V pořadí zleva doprava má obsahovat:

1. skok na první stránku,
2. předchozí stránku,
3. číslovaná tlačítka stránek a případné výpustky,
4. následující stránku,
5. skok na poslední stránku.

### Číslovaná tlačítka

- Současně zobrazit nejvýše 7 číslovaných tlačítek. Výpustky se do tohoto limitu nepočítají.
- Aktivní stránku jednoznačně zvýraznit.
- První a poslední číslo stránky ponechat v číselném rozsahu dostupné.
- Vynechané vzdálené rozsahy nahradit neaktivní výpustkou.
- Rozsah čísel kolem aktivní stránky dynamicky posouvat.
- Na první stránce deaktivovat skok na první a přechod na předchozí stránku.
- Na poslední stránce deaktivovat přechod na následující a skok na poslední stránku.

Příklad prostředního stavu:

`«  ‹  1  …  4  5  [6]  7  8  …  20  ›  »`

Hranaté závorky v příkladu pouze označují aktivní stránku. Nejsou požadovaným doslovným vzhledem tlačítka.

## Pracovní stav a ukládání

- Stránkování mění pouze viditelnou část tabulky. Samotná změna stránky nic trvale neukládá.
- Hodnoty **Key**, stavy **Enabled** a výsledky akcí **Random** a **Password SHA** uchovávat napříč stránkami v jednom paměťovém pracovním modelu okna.
- Pracovní stav vázat na stabilní identitu kontraktu, nikoli na číslo viditelného řádku.
- Po návratu na dříve navštívenou stránku musí být všechny neuložené změny u správných kontraktů zachované.
- Veškeré změny ze všech stránek trvale uložit jako jeden celek až prostřednictvím stávajícího workflow tlačítka **Save**.
- Zavření okna bez potvrzení nesmí rozpracované změny trvale zapsat, v souladu se stávajícím chováním okna.
- Nevytvářet nové průběžné ukládání při přechodu mezi stránkami.

## Zachování stávajícího chování

- **Random** a **Password SHA** musí na každé stránce upravit správný kontrakt a zachovat své dosavadní chování.
- Zachovat stávající validaci, ukládací workflow, chybové stavy, fokus, tab order, klávesové ovládání a přístupnost.
- Neměnit datový model kontraktů, IPC, síťovou komunikaci ani obsah ukládané konfigurace.
- Neměnit workflow jiných oken.

## Akceptační kritéria

1. Tabulka má zaoblený vnější kontejner, jemně šedé záhlaví a tučné názvy sloupců.
2. Na jedné stránce se zobrazuje nejvýše 20 kontraktů.
3. Současně existují řádkové GUI prvky nejvýše pro 20 zobrazených kontraktů.
4. Při více než 20 kontraktech se pod tabulkou zobrazí stránkovací lišta. Při nejvýše 20 kontraktech je skrytá.
5. Lišta umožňuje přechod na první, předchozí, následující a poslední stránku.
6. Současně zobrazuje nejvýše 7 číslovaných tlačítek, aktivní stránku zvýrazňuje a vzdálené rozsahy nahrazuje výpustkami.
7. Ovládací prvky, které na první nebo poslední stránce nemohou nic provést, jsou deaktivované.
8. Změna stránky nic trvale neukládá a nezahazuje žádnou rozpracovanou změnu.
9. Hodnoty **Key**, stavy **Enabled** a výsledky **Random** a **Password SHA** zůstávají napříč stránkami přiřazené správným kontraktům.
10. Tlačítko **Save** uloží změny ze všech stránek společně prostřednictvím stávajícího ukládacího workflow.
11. Zavření bez potvrzení nezapíše rozpracované změny.
12. Funkce mimo výslovně vymezený rozsah zůstanou beze změny.
13. Klientskou plochu okna nelze zmenšit pod 920 × 520 DIP, s korektním přepočtem pro aktuální DPI a respektováním pracovní plochy monitoru.
14. Patičku odděluje od obsahu jemná horizontální čára o tloušťce jednoho fyzického pixelu.
15. Hlavní tlačítko zobrazuje text **Save**, ale zachovává dosavadní potvrzovací a ukládací chování.

## Odložené ověření

Po pozdější implementaci ručně ověřit zejména:

- počty 0, 1, 20, 21 a více než 140 kontraktů,
- první, prostřední a poslední stránku,
- posouvání číselného rozsahu, výpustky a deaktivované krajní prvky,
- změny **Enabled** a **Key** na více stránkách,
- akce **Random** a **Password SHA** na více stránkách,
- zachování změn při opakovaném přecházení mezi stránkami,
- společné uložení přes **Save** a zavření bez potvrzení,
- resize, podporované DPI, fokus, tab order, klávesové ovládání a překreslování.

V rámci této fáze kontroly neprovádět. Nevytvářet nové testy a neupravovat stávající testy.

## Hranice současné fáze

Tento dokument pouze formalizuje zadání před implementací. V této fázi:

- neměnit zdrojový kód aplikace,
- neměnit běžnou projektovou dokumentaci,
- nevytvářet ani neupravovat testy,
- neprovádět kompilaci,
- nekopírovat nic do distribuce,
- neprovádět commit ani push.
