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
- `communication-contracts-border-after-011.png` zachycuje stav po první implementaci mikro-updatu 011. Dokumentuje nespojité a nedostatečně vyhlazené zaoblené rohy obrysu tabulky, zejména chybějící pixely vpravo dole, nečistý levý dolní roh a nepřesnou návaznost na tmavou hlavičku.

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

### Doladění obrysu po první implementaci

- Zaoblené rohy zachovat jako preferovaný výsledek. Rovné rohy použít pouze jako poslední záložní řešení, pokud by čisté zaoblení nebylo v použité Win32 architektuře spolehlivě realizovatelné.
- Opravit oba spodní rohy tak, aby linka tvořila uzavřený souvislý oblouk bez mezer, utržených konců, schodů a chybějících pixelů. Zvláštní pozornost věnovat výrazně poškozenému pravému dolnímu rohu.
- Opravit horní hranu a horní rohy tak, aby obrys pixelově přesně navazoval na tmavou hlavičku bez světlého vlasového proužku, přesahu nebo rozdílného oblouku.
- Zajistit konzistentní antialiasing všech oblouků. Levá hrana a levé rohy nesmějí působit zubatěji než pravá strana.
- Sjednotit geometrii viditelného antialiasovaného obrysu, výplně, hlavičky a ořezání child oken. Současná kombinace GDI+ cesty a několika celočíselných Win32 regionů s odlišnými rozměry, insety a poloměry nesmí odřezávat antialiasované pixely ani vytvářet mezery.
- Hlavička, viewport ani scrollbary nesmějí viditelný obrys překreslovat. Pravá a spodní hrana musí správně respektovat souřadnicové konvence použitých kreslicích API.
- Preferovat jediné místo odpovědné za finální vykreslení vnějšího tvaru namísto několika téměř shodných překrývajících se geometrií.

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

  `ghjkmnpqrstuvwxyz`

- Nepoužívat žádné jiné znaky, zejména číslice, velká písmena, mezery, interpunkci ani jiné speciální znaky.
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

### Dirty stav podle skutečného dopadu na JSON

- Dirty stav řádku musí odpovídat tomu, zda by Save skutečně změnil výsledný konfigurační JSON, nikoli pouze tomu, zda se během relace změnila interní pracovní hodnota.
- Pokud byl kontrakt v načtené konfiguraci vypnutý, tedy jeho Key v JSONu chyběl, a aktuálně je znovu vypnutý, považovat jej za nezměněný i tehdy, když jej uživatel mezitím zapnul a zadal pracovní Key.
- V tomto stavu se kontrakt nezapočítává na tlačítku Save, řádek není žlutě zvýrazněn a při absenci jiných změn je Save disabled s textem **Save**.
- Pracovní Key lze v paměti formuláře zachovat. Pokud uživatel kontrakt ve stejné relaci znovu zapne, hodnota se obnoví a kontrakt se znovu stane změněným.
- Pokud byl kontrakt v načtené konfiguraci zapnutý a uživatel jej vypne, kontrakt zůstává změněný bez ohledu na pracovní Key, protože Save odstraní jeho klíč z JSONu.
- Pokud je kontrakt aktuálně zapnutý, změna Key oproti načtené hodnotě se nadále započítává.
- Ekvivalentní pravidlo pro řádek je: kontrakt je dirty, pokud se změnil Enabled, nebo pokud je aktuálně Enabled a Key se liší od původního Key.
- Stejné pravidlo použít jednotně pro celkový dirty stav okna, počet změn na Save, žluté zvýraznění řádku, potvrzení při zavření a rozhodnutí, zda má Save co uložit.

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
- Pokud není změněný žádný kontrakt, tlačítko **Save** je disabled.
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
- Při alespoň jedné změně je **Save** enabled pouze tehdy, pokud současně vyhovuje všem dosavadním podmínkám pro bezpečné uložení. Existující blokace při neplatném klíči, probíhajícím ukládání nebo jiném nebezpečném stavu zůstává zachována.

## Vzhled validační zprávy v patičce

- Pokud je Save blokováno kvůli prvnímu zapnutému kontraktu s neplatným Key, patička nad tlačítky nadále zobrazí stávající dvouřádkovou zprávu:

  `Key must contain exactly 64 lowercase hexadecimal characters.`

  `First invalid peer: <IP address>`

- Text obou řádků vykreslit červenou chybovou barvou používanou aplikací.
- Červená barva je povinná a musí mít dobrý kontrast vůči pozadí patičky.
- Oba řádky preferovaně zobrazit tučně nebo polotučně, aby byla blokující chyba snadno rozpoznatelná. Pokud by zvolená váha způsobovala ořez nebo chybné zalamování při podporovaném DPI, upravit měření a layout tak, aby se celý text bezpečně zobrazil.
- Barva a váha musí platit pro oba řádky včetně textu **First invalid peer** a IP adresy.
- Zachovat současné přesné znění, pořadí řádků, IP adresu prvního neplatného kontraktu a možnost kliknutím na zprávu přesunout fokus na příslušný Key.
- Červený styl použít pouze pro tento blokující validační stav a další skutečné chyby, ne automaticky pro neutrální informační oznámení zobrazovaná stejnou oblastí.
- Při odstranění chyby musí červený text okamžitě zmizet nebo se vrátit do stylu následujícího relevantního oznámení bez zbytkových pixelů.

## Tlačítko Cancel

- Vedle tlačítka **Save** přidat sekundární tlačítko s přesným textem **Cancel**.
- **Cancel** umístit nalevo od **Save** a oddělit je běžnou malou mezerou používanou mezi souvisejícími tlačítky aplikace.
- **Cancel** použije současný neutrální moderní styl tlačítek. **Save** zůstane primární modré tlačítko.
- **Cancel** zůstane enabled i tehdy, když neexistuje žádná změna a **Save** je disabled.
- Kliknutí na **Cancel** bez neuložených změn okamžitě zavře okno stejnou bezpečnou cestou jako současné zavření křížkem.
- Kliknutí na **Cancel** při alespoň jednom neuloženém změněném kontraktu zobrazí stejný potvrzovací dialog jako současné zavření křížkem, včetně přesného textu:

  `Discard unsaved changes?`

- Potvrzení dialogu zahodí pracovní změny a zavře okno. Zamítnutí ponechá okno i všechny pracovní změny beze změny.
- Zavření systémovým křížkem a klávesou Escape nadále používá stejné existující potvrzovací workflow.
- Při existujícím neznámém nebo probíhajícím ukládacím stavu zachovat současné bezpečnostní chování lifecycle. Nové tlačítko nesmí umožnit obejít ochranu proti zavření během ukládání ani zvláštní hlášku pro neznámý výsledek.
- Tlačítka **Cancel** a **Save** musí zůstat celá viditelná, správně zarovnaná a nepřekrývající se při podporovaném DPI a minimální velikosti okna.

## Chování Key a Change při vypnutém Enabled

- Pokud je přepínač **Enabled** daného kontraktu v poloze vypnuto, jeho pole **Key** přepnout do read-only režimu.
- Read-only Key nesmí přijímat zápis z klávesnice, vložení ze schránky ani jinou editační operaci.
- Hodnotu Key při vypnutí Enabled nesmazat, nepřepsat ani jinak nezměnit. Vypnutí kontraktu pouze zabrání její další editaci.
- Read-only Key musí zůstat čitelný, fokusovatelný, označitelný a kopírovatelný, včetně Ctrl+C a standardního kontextového menu nativního pole.
- Pro read-only stav použít konzistentní vizuální styl již používaný aplikací pro pole pouze ke čtení. Text musí zůstat dostatečně kontrastní.
- Tlačítko **Change** daného řádku při vypnutém Enabled nastavit jako disabled.
- Po opětovném zapnutí Enabled okamžitě vrátit Key do editovatelného stavu a tlačítko Change znovu aktivovat.
- Zachovat aktuální pracovní hodnotu Key i při opakovaném vypínání a zapínání Enabled.
- Samotná změna Enabled se nadále počítá jako změna kontraktu, ovlivní žluté zvýraznění řádku a počet na tlačítku Save podle stávajících pravidel.
- Pokud uživatel před vypnutím Enabled změnil Key, zůstane tato rozpracovaná hodnota zachována a kontrakt zůstane změněný. Vypnutí nesmí vrátit Key na výchozí hodnotu.
- Stav read-only a disabled musí být správně obnoven při vytvoření řádku, změně stránky, návratu na dřívější stránku, překreslení a resize okna.
- Již otevřený dialog **Password SHA** pro kontrakt, který uživatel následně vypne, nesmí umožnit obejít zákaz změny Key. Dokud je cílový kontrakt vypnutý, dialog nesmí aplikovat nový klíč přes OK, Default ani Random.
- Preferovat řešení, které zachová případný rozpracovaný obsah již otevřeného dialogu a po opětovném zapnutí kontraktu umožní bezpečně pokračovat. Pokud architektura vyžaduje jiné řešení, nesmí dojít k tiché aplikaci změny ani ke změně Key vypnutého kontraktu.
- Omezení se vztahuje pouze na uživatelskou editaci Key v okně Communication contracts. Samotné přepnutí Enabled nesmí Key okamžitě odstranit. K odstranění z JSONu dojde až prostřednictvím existujícího Save workflow pro vypnutý kontrakt.

## Chování chybějícího Key při načtení a po uložení

- Při načtení okna přesně rozlišit klíč skutečně uložený v konfiguračním JSONu od klíče, který lze pouze odvodit výchozím pravidlem.
- Pokud pro kontrakt není v načteném JSONu uložen žádný Key, nevypočítávat při načtení automaticky `defaultPayloadKey` a nezobrazovat odvozenou hodnotu jako skutečný obsah konfigurace.
- Takový kontrakt načíst jako vypnutý s prázdnou pracovní i výchozí hodnotou Key.
- Vypnutý kontrakt s chybějícím klíčem zobrazí prázdný read-only textbox Key bez červené chybové indikace. Neaktivní prázdný klíč neblokuje uložení.
- Po zapnutí tohoto kontraktu se Key stane editovatelným, Change se aktivuje a prázdný Key se okamžitě zobrazí současným červeným validačním stylem jako neplatný.
- Zapnutý kontrakt s prázdným nebo jinak neplatným Key zablokuje Save podle současných validačních pravidel, dokud uživatel nevytvoří platný klíč.
- Uživatel může platný klíč zadat ručně nebo přes Change zvolit Default, Random či vlastní heslo. Odvozený výchozí klíč vznikne pouze po výslovném použití akce Default, nikoli automaticky při načtení.
- Pokud uživatel nově zapnutý prázdný kontrakt znovu vypne dříve, než nastaví klíč, vrátí se na původní prázdný read-only stav. Pokud se Enabled i Key shodují s načteným stavem, kontrakt přestane být dirty, zmizí žluté zvýraznění a sníží se počet změn.
- Pokud byl Key v načteném JSONu skutečně uložen, po dočasném vypnutí Enabled jej ponechat viditelný v read-only textboxu a držet jej v pracovním modelu.
- Opětovné zapnutí před Save obnoví editaci přesně stejné pracovní hodnoty Key. Dočasné přepnutí ji nesmí smazat ani nahradit defaultem.
- Uložení kontraktu ve vypnutém stavu odstraní jeho Key z výsledného konfiguračního JSONu podle současného významu vypnutého kontraktu.
- Po zavření a novém načtení takto uložené konfigurace je dříve odstraněný Key skutečně nepřítomný. Řádek se proto zobrazí jako vypnutý s prázdným read-only Key a nepoužije se automatická výchozí hodnota.
- Změna se vztahuje na prezentační pracovní model a workflow Communication contracts. Neměnit definici explicitní akce Default ani pravidlo `minNumericIp_maxNumericIp`, které tato akce používá.
- Zachovat bezpečné mazání citlivých pracovních bufferů a nepřidávat zbytečné kopie klíčů.

## Akceptační kritéria

1. Celou tabulku **Communication contracts** obtahuje souvislá linka o tloušťce jednoho fyzického pixelu ve stejné barvě jako tmavá hlavička.
2. Obtažení respektuje zaoblené rohy a zůstává čisté při resize, stránkování, překreslení a podporovaném DPI.
3. Každý řádek má ve sloupci **Actions** právě jedno tlačítko **Change**.
4. Původní řádková tlačítka **Random** a **Password SHA** již nejsou zobrazena.
5. **Change** otevře existující dialog **Password SHA** pro správný a neměnný cílový kontrakt.
6. V levé části patičky dialogu jsou v pořadí **Default** a **Random**, obě v červeném stylu. **OK** zůstává vpravo.
7. Každé stisknutí **Random** vloží do pole Password přesně 64 znaků.
8. Vygenerované heslo obsahuje výhradně malá písmena `ghjkmnpqrstuvwxyz` a dovoluje jejich opakování.
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
23. Pokud nejsou žádné změny, **Save** zobrazuje pouze tento text a je disabled.
24. Vedle Save je neutrální enabled tlačítko **Cancel**. Bez změn zavře okno přímo, s neuloženými změnami použije stejné potvrzení `Discard unsaved changes?` jako systémové zavření.
25. Všechny čtyři rohy obrysu jsou spojité, symetrické a antialiasované bez děr, useknutých pixelů a nečisté návaznosti na hlavičku.
26. Při Enabled vypnuto je Key read-only, ale lze jej fokusovat, označit a kopírovat, a Change je disabled.
27. Po opětovném zapnutí Enabled je Key znovu editovatelný a Change enabled, přičemž pracovní hodnota Key zůstane zachována.
28. Přepnutí Enabled nemění ani nemaže Key a nadále správně ovlivňuje dirty stav, žluté zvýraznění a počet změn.
29. Read-only a disabled stav zůstává správný napříč stránkováním, překreslením a resize.
30. Již otevřený Password SHA dialog nemůže aplikovat nový klíč na kontrakt, který byl mezitím vypnutý.
31. Chybějící Key v načteném JSONu se nezobrazuje jako automaticky odvozený default. Vypnutý řádek má prázdný read-only Key.
32. Zapnutí řádku s chybějícím Key zpřístupní editaci a Change, zobrazí červenou validaci prázdného pole a zablokuje Save do zadání platného klíče.
33. Explicitní akce Default nadále vytvoří správný výchozí klíč, ale pouze na vědomou volbu uživatele.
34. Dočasné vypnutí kontraktu s uloženým Key zachová jeho pracovní hodnotu a opětovné zapnutí před Save ji obnoví beze změny.
35. Uložení vypnutého kontraktu odstraní jeho Key z JSONu. Po novém načtení je pole prázdné a žádný default se automaticky nedopočítá.
36. Návrat nově zapnutého prázdného kontraktu do původního vypnutého stavu správně zruší dirty stav, žluté zvýraznění i jeho započtení na Save.
37. Původně vypnutý kontrakt, který byl zapnut, dostal pracovní Key a byl znovu vypnut, se nepovažuje za změněný, protože výsledný JSON zůstane stejný.
38. Zachovaný pracovní Key tohoto nezměněného vypnutého kontraktu se po opětovném zapnutí ve stejné relaci obnoví a kontrakt se znovu stane změněným.
39. Dirty stav řádku, celého okna, žluté zvýraznění, počet na Save a potvrzení při zavření používají stejné pravidlo podle skutečného dopadu na JSON.
40. Dvouřádková blokující zpráva o neplatném Key a prvním neplatném peeru je celá červená a preferovaně tučná nebo polotučná, bez ořezu při podporovaném DPI.
41. Kliknutí na validační zprávu nadále přesune fokus na Key uvedeného prvního neplatného peeru.
42. Ostatní workflow, validace, ukládání, IPC, síťová komunikace a nedotčené části GUI zůstávají beze změny.

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
