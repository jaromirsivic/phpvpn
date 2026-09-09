# 011 - Change akce a náhodné heslo pro Communication contracts

## Cíl

Upravit okno **Communication contracts** a navazující dialog **Password SHA** tak, aby:

- tabulka měla jemné, ale zřetelné obtažení barvou své tmavé hlavičky,
- ve sloupci **Actions** bylo u každého kontraktu pouze jedno tlačítko **Change**,
- tlačítko **Change** otevíralo existující dialog pro změnu klíče pomocí hesla,
- dialog nabízel vedle tlačítka **Default** také červené tlačítko **Random**,
- tlačítko **Random** vytvořilo přesně 64znakové náhodné heslo pouze z určené sady znaků a automaticky přepočítalo výsledný SHA-256 klíč,
- každý kontrakt změněný oproti načtenému stavu byl v tabulce zvýrazněn žlutým pozadím celého řádku,
- tlačítko **Save** zobrazovalo počet unikátních změněných kontraktů.

## Reference

- `communication-contracts-current.png` zachycuje současnou tabulku **Communication contracts**, její nevýrazný vnější okraj a dvě samostatné řádkové akce **Random** a **Password SHA**.
- `password-sha-current.png` zachycuje současný dialog **Password SHA** s tlačítkem **Default** vlevo a tlačítkem **OK** vpravo.

Referenční obrázky popisují výchozí stav. Nejsou samostatnými instrukcemi. Při rozporu platí text tohoto zadání.

## Rozsah

Úprava zahrnuje pouze:

- obtažení vnějšího kontejneru tabulky **Communication contracts**,
- obsah a rozložení sloupce **Actions**,
- otevření existujícího dialogu přes novou akci **Change**,
- přidání tlačítka **Random** do dialogu **Password SHA**,
- generování náhodného hesla a navazující přepočet SHA-256 klíče,
- sledování změn kontraktů oproti stavu načtenému při otevření okna,
- žluté zvýraznění změněných řádků,
- dynamický text a potřebnou šířku tlačítka **Save**,
- nezbytné úpravy layoutu, tab orderu a překreslování dotčených prvků.

Neredesignovat jiné části oken a neměnit datový model, ukládací workflow, IPC ani síťovou komunikaci.

## Obtažení tabulky Communication contracts

- Celý vnější kontejner tabulky lehce obtáhnout souvislou linkou o tloušťce jednoho fyzického pixelu.
- Pro obtažení použít stejnou barvu, jakou používá tmavé pozadí hlavičky tabulky.
- Obtažení musí sledovat současný zaoblený tvar kontejneru a nesmí narušit zaoblené rohy.
- Linka musí být zřetelná na světlém pozadí, ale nemá působit jako výrazný nebo vícenásobný rám.
- Obtažení musí být souvislé po celém obvodu bez mezer, dvojitých hran a zbytkových pixelů.
- Zachovat správné vykreslení při podporovaném DPI, resize okna, stránkování a překreslení.
- Neměnit barvu hlavičky, datových řádků ani oddělovačů mezi řádky.

## Jediná řádková akce Change

- Ve sloupci **Actions** odstranit z každého viditelného řádku samostatná tlačítka **Random** a **Password SHA**.
- Nahradit je jediným tlačítkem s přesným textem **Change**.
- **Change** bude používat současný neutrální moderní styl řádkových tlačítek.
- Tlačítko vhodně umístit do stávajícího sloupce **Actions**. Po odstranění druhého tlačítka nesmí působit stísněně ani zbytečně roztaženě.
- Kliknutí na **Change** otevře pro daný kontrakt existující dialog **Password SHA**, ve kterém lze klíč vytvořit ručním heslem, akcí **Default** nebo novou akcí **Random**.
- Identitu cílového kontraktu zachytit stejně bezpečně jako u současné akce **Password SHA**. Pozdější změna stránky, fokusu nebo viditelného řádku nesmí změnit cíl již otevřeného dialogu.
- Změna se musí korektně projevit na všech stránkách tabulky a u všech řádkových ovládacích prvků vytvářených při stránkování.
- Původní přímá akce **Random** v tabulce již nebude dostupná. Její uživatelský účel nahrazuje nové tlačítko **Random** uvnitř dialogu.

## Tlačítko Random v dialogu Password SHA

- Do levé části patičky dialogu přidat tlačítko s přesným textem **Random**.
- Tlačítko umístit vedle stávajícího tlačítka **Default**. Pořadí zleva doprava bude **Default**, **Random**.
- Obě levá tlačítka musí mít bezpečný vzájemný odstup a nesmějí se dotýkat okraje dialogu.
- **Random** použije stejný červený styl jako **Default**, včetně běžného, hover, focus a pressed stavu.
- Tlačítko **OK** zůstane napravo a zachová současný styl i chování.
- Layout musí zůstat čistý při výchozí i minimální podporované velikosti dialogu a při podporovaném DPI. Tlačítka se nesmějí překrývat, dotýkat ani ořezávat.

## Generování náhodného hesla

- Po každém stisknutí **Random** vytvořit nové heslo o přesné délce 64 znaků.
- Každou pozici hesla vybírat pouze z této přesné 17znakové sady:

  `GHJKMNPQRSTUVWXYZ`

- Nepoužívat žádné jiné znaky, zejména číslice, malá písmena, mezery, interpunkci ani jiné speciální znaky.
- Jednotlivé pozice vybírat náhodně s možností opakování stejného znaku.
- Použít kryptograficky bezpečný zdroj náhodnosti. Pokud již aplikace obsahuje vhodnou bezpečnou implementaci generování náhodných hodnot, znovu ji použít nebo nad ní vytvořit úzce zaměřenou sdílenou pomocnou funkci.
- Nepoužívat slabý nebo předvídatelný generátor založený například pouze na čase procesu.
- Zajistit nezaujatý výběr ze 17 znaků. Nepoužívat prosté modulo náhodného bajtu, pokud by tím vzniklo měřitelné zvýhodnění některých znaků. Lze použít rejection sampling nebo rovnocenný korektní postup.
- Nové heslo vložit do editovatelného pole **Password** jako běžnou aktuální hodnotu.
- Bezprostředně po vložení automaticky přepočítat **SHA-256 key** stejným existujícím postupem jako při ruční změně pole Password.
- Po vygenerování musí heslo zůstat běžně editovatelné. Každá další ruční změna nadále automaticky přepočítá SHA-256 klíč.
- Každé další stisknutí **Random** nahradí celé pole Password novým 64znakovým heslem a přepočítá klíč.
- Samotné stisknutí **Random** nesmí změnit kontrakt v rodičovském okně. Výsledek se uplatní až po potvrzení tlačítkem **OK**.

## Zachování ostatního chování dialogu

- **Default** nadále vyplní výchozí identifikátor cílového kontraktu a přepočítá jeho SHA-256 klíč podle současných pravidel.
- Ručně zadané heslo se nadále zpracuje beze změny.
- **SHA-256 key** zůstane read-only, ale fokusovatelný, označitelný a kopírovatelný.
- **OK** přenese aktuální platný klíč do správného kontraktu a zavře dialog.
- Escape a zavření systémovým tlačítkem zavřou dialog bez aplikace nepotvrzené hodnoty.
- Zachovat současné chybové stavy, validaci a bezpečné zacházení s citlivými pracovními daty.
- Zachovat nemodální charakter dialogu a možnost otevřít více dialogů pro různé kontrakty.
- Aktualizovat tab order tak, aby byl předvídatelný a zahrnoval nové tlačítko. Doporučené pořadí je Password, SHA-256 key, Default, Random, OK.
- Enter nadále spustí primární akci **OK**, pokud fokus nemá jiné tlačítko.

## Určení změněného kontraktu

- Za výchozí stav kontraktu považovat hodnoty **Enabled** a **Key**, které byly načteny při otevření okna **Communication contracts**.
- Kontrakt je změněný, pokud se jeho aktuální pracovní hodnota **Enabled** nebo **Key** liší od výchozí načtené hodnoty.
- Změna **Enabled** z true na false i z false na true se započítává.
- Změna **Key** se započítává bez ohledu na to, zda vznikla ruční editací, potvrzením dialogu po akci **Default**, potvrzením dialogu po akci **Random** nebo jinou existující cestou změny klíče.
- Jeden kontrakt se vždy počítá nejvýše jednou. Pokud má současně změněné **Enabled** i **Key**, jde stále o jeden změněný kontrakt.
- Pokud uživatel vrátí obě hodnoty přesně do výchozího načteného stavu, kontrakt přestane být změněný, jeho zvýraznění zmizí a počet na tlačítku **Save** se sníží.
- Porovnání Key provádět nad skutečnou pracovní hodnotou, ne pouze podle toho, zda textbox obdržel vstupní událost. Přepsání hodnoty stejným obsahem tedy kontrakt neoznačí jako změněný.
- Stav změn držet napříč stránkami podle stabilní identity kontraktu, nikoli podle čísla aktuálně viditelného řádku.
- Pouhá změna stránky, fokusu, resize nebo otevření a zavření dialogu bez potvrzení nesmí změnit počet upravených kontraktů.

## Žluté zvýraznění změněného řádku

- Každý řádek změněného kontraktu podbarvit celý lehce až středně žlutou barvou, aby byla neuložená změna patrná na první pohled.
- Zvýraznění se vztahuje na změnu **Enabled** i **Key**, protože v obou případech má kontrakt neuložený pracovní stav.
- Nezvýrazňovat zeleně textbox **Key** ani mu nepřidávat zelené orámování. Tento původně zvažovaný návrh je výslovně zrušen.
- Žluté pozadí musí pokrýt vizuální plochu celého datového řádku včetně oblastí jednotlivých sloupců. Nemá přebarvit vlastní aktivní plochu ovládacích prvků způsobem, který by snižoval jejich čitelnost nebo rozpoznatelnost.
- Existující červená validace chybného pole **Key** zůstává zachována a má přednost před běžným vzhledem pole. Červené podbarvení a orámování neplatného textboxu musí zůstat jednoznačně viditelné i ve žlutě zvýrazněném řádku.
- Po návratu Key do platného stavu zmizí červená validace podle současných pravidel. Žlutý stav řádku zůstane pouze tehdy, pokud se Enabled nebo Key stále liší od výchozí hodnoty.
- Zvýraznění musí být správně obnoveno při stránkování, překreslení, scrollování a resize okna bez blikání, mezer a zbytkových pixelů.
- Žlutá musí mít dostatečný kontrast vůči textu, toggle switchi, textboxu a tlačítku **Change** a musí zapadat do současného vizuálního stylu aplikace.

## Počet změněných kontraktů na tlačítku Save

- Pokud není změněný žádný kontrakt, tlačítko má přesný text **Save** bez závorky a bez nulového počtu.
- Pokud je změněn alespoň jeden kontrakt, tlačítko zobrazí přesný formát:

  `Save (N changes)`

- `N` je aktuální počet unikátních změněných kontraktů napříč všemi stránkami.
- Pro jeden změněný kontrakt použít gramaticky správný text:

  `Save (1 change)`

- Počet aktualizovat bezprostředně po každé změně Enabled, po každé ruční změně Key, po potvrzení změny klíče z dialogu a po návratu hodnot do výchozího stavu.
- Nezapočítávat počet změněných polí. Kontrakt se změněnými oběma hodnotami se počítá právě jednou.
- Tlačítko **Save** vhodně rozšířit, aby se běžné hodnoty textu zobrazily celé s bezpečným vnitřním odsazením.
- Rozšíření tlačítka nesmí narušit jeho pravé zarovnání, okraje patičky, minimální velikost okna ani ostatní prvky layoutu.
- Pro neočekávaně vysoký počet zajistit odolný layout. Text nesmí přetéct do sousedních prvků ani být vykreslen mimo tlačítko.
- Samotné ukládací chování tlačítka **Save** se nemění.

## Akceptační kritéria

1. Celou tabulku **Communication contracts** obtahuje souvislá linka o tloušťce jednoho fyzického pixelu ve stejné barvě jako tmavá hlavička.
2. Obtažení respektuje zaoblené rohy a zůstává čisté při resize, stránkování, překreslení a podporovaném DPI.
3. Každý řádek má ve sloupci **Actions** právě jedno tlačítko **Change**.
4. Původní řádková tlačítka **Random** a **Password SHA** již nejsou zobrazena.
5. **Change** otevře existující dialog **Password SHA** pro správný a neměnný cílový kontrakt.
6. V levé části patičky dialogu jsou v pořadí **Default** a **Random**, obě v červeném stylu. **OK** zůstává vpravo.
7. Každé stisknutí **Random** vloží do pole Password přesně 64 znaků.
8. Vygenerované heslo obsahuje výhradně znaky `GHJKMNPQRSTUVWXYZ` a dovoluje jejich opakování.
9. Generování používá kryptograficky bezpečný a nezaujatý výběr znaků.
10. Po stisknutí **Random** se SHA-256 key bezprostředně přepočítá z nového hesla stejným postupem jako při ruční editaci.
11. Náhodné heslo lze po vygenerování ručně editovat a hash se při změnách nadále automaticky aktualizuje.
12. **Random** ani **Default** samy nezmění hodnotu rodičovského kontraktu. Hodnota se použije až přes **OK**.
13. Změny v řádku zůstávají správně přiřazené kontraktu při stránkování a ukládají se až existujícím tlačítkem **Save**.
14. Výchozí a minimální velikost dialogu zobrazuje všechna tři tlačítka bez překryvu, dotyku a ořezu.
15. Řádek kontraktu se žlutě zvýrazní, pokud se jeho Enabled nebo Key liší od hodnot načtených při otevření okna.
16. Neplatný Key zachová současné červené podbarvení a orámování, které zůstává viditelné i ve změněném žlutém řádku.
17. Návrat Enabled i Key na původní hodnoty odstraní žluté zvýraznění a kontrakt přestane být započítán.
18. Tlačítko bez změn zobrazuje pouze **Save**.
19. Při změnách zobrazuje tlačítko **Save (N changes)**, pro jedinou změnu používá **Save (1 change)**.
20. Počet odpovídá unikátním změněným kontraktům napříč všemi stránkami. Změna obou polí jednoho kontraktu zvýší počet pouze o jeden.
21. Počet i žluté zvýraznění se aktualizují bezprostředně a správně reagují také na vrácení hodnot do výchozího stavu.
22. Rozšířené tlačítko Save zůstává celé viditelné, správně zarovnané a funkční při podporovaných velikostech okna a DPI.
23. Ostatní workflow, validace, ukládání, IPC, síťová komunikace a nedotčené části GUI zůstávají beze změny.

## Omezení implementace a ověření

- Neprovádět komplexní testy.
- Nevytvářet nové testy.
- Existující test upravit pouze tehdy, pokud je potřebná změna malá, přímočará a rychlá. Rozsáhlejší úpravy testů odložit.
- Neupravovat běžnou projektovou dokumentaci.
- Po implementaci zkompilovat tray aplikaci, vyřešit chyby způsobené změnou a zkopírovat výslednou sestavu do distribuce pro ruční instalaci a ověření.

## Hranice současné fáze

Tento dokument pouze formalizuje zadání před implementací. V této fázi:

- neměnit zdrojový kód aplikace,
- neměnit běžnou projektovou dokumentaci,
- nevytvářet ani neupravovat testy,
- neprovádět kompilaci,
- nekopírovat nic do distribuce,
- neprovádět commit ani push.

Implementační projektový úkol bude založen až po výslovném určení modelu a effortu uživatelem.
