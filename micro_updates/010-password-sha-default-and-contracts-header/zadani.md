# 010 - Default Password SHA a kontrastní hlavička Communication contracts

## Cíl

Upravit okno **Password SHA**, které se otevírá akcí **Password SHA** u konkrétního řádku v okně **Communication contracts**, aby:

- bylo přibližně o 25 % širší a bezpečně zobrazilo celý SHA-256 klíč,
- nabízelo tlačítko **Default** pro obnovení výchozího hesla a klíče daného kontraktu,
- zobrazovalo výsledek **SHA-256 key** jako skutečný read-only textbox,
- mělo zřetelně oddělenou patičku s tlačítky **Default** a **OK**.

Součástí stejného mikro-updatu je také zvýšení kontrastu hlavičky tabulky v hlavním okně **Communication contracts**.

## Reference

Referenční obrázky zachycují jednotlivé stavy dialogu:

- `password-sha-current-dialog.png` dokumentuje původní šířku, původní rozložení polí a patičku pouze s tlačítkem **OK**.
- `password-sha-after-first-implementation.png` dokumentuje stav po první implementaci mikro-updatu 010. Ukazuje zejména neutrální tlačítko **Default** a výsledek SHA-256, který vizuálně neodpovídá modernímu read-only textboxu.
- `password-sha-excessively-narrow.png` dokumentuje nežádoucí možnost zmenšit dialog pod použitelnou šířku. Obsah se ořezává a tlačítka ztrácejí bezpečný vzájemný odstup.

Reference není samostatnou instrukcí. Při rozporu platí text tohoto zadání.

## Rozsah

Úprava zahrnuje pouze:

- velikost a rozložení okna **Password SHA**,
- počáteční obsah pole **Password**,
- nové chování tlačítka **Default**,
- zobrazení pole **SHA-256 key**,
- patičku okna **Password SHA**,
- vzhled hlavičky tabulky v okně **Communication contracts**.

Neměnit význam akce **Password SHA**, způsob potvrzení přes **OK**, ukládání kontraktů ani ostatní části okna **Communication contracts**.

## Šířka okna Password SHA

- Výchozí šířku okna zvětšit přibližně o 25 %.
- Pokud implementace vychází ze stávající cílové klientské šířky 540 DIP, nová cílová klientská šířka bude přibližně 675 DIP při 100% DPI.
- Výšku bezdůvodně nezvětšovat. Lze ji upravit pouze v rozsahu potřebném pro čisté rozložení nové patičky.
- Pole **Password** a **SHA-256 key** využijí novou dostupnou šířku.
- Celý 64znakový SHA-256 klíč se musí při běžném zobrazení bezpečně vejít bez ořezu. Textbox zároveň zachová standardní horizontální posun pro případ menší dostupné plochy nebo jiného DPI.
- Rozložení musí zůstat korektní pro podporované DPI a nesmí vytvářet překryvy, ořezané texty ani zbytkové vykreslení.

## Počáteční stav Password a SHA-256 key

- Při běžném otevření okna zůstane pole **Password** prázdné.
- Pole se nesmí automaticky předvyplnit výchozím identifikátorem kontraktu.
- Prázdný vstup nadále představuje platné heslo a výsledkem zůstává SHA-256 přes prázdný UTF-8 vstup podle současného chování.
- Výpočet při ručním psaní zůstane beze změny. SHA-256 key se automaticky aktualizuje z přesných UTF-8 bajtů aktuální hodnoty Password.

## Tlačítko Default

- Do levé části patičky přidat sekundární tlačítko **Default**.
- Akce **Default** se vztahuje výhradně ke kontraktu, pro který bylo konkrétní okno **Password SHA** otevřeno. Pozdější změna fokusu, stránky tabulky nebo jiného řádku nesmí změnit její cíl.
- Po stisknutí **Default** vyplnit do pole **Password** výchozí řetězec daného kontraktu ve formátu:

  `minNumericIp_maxNumericIp`

- Obě IP adresy musí být kanonické a seřazené podle číselné hodnoty celé IPv4 adresy, nikoli lexikograficky. Mezi nimi je právě jedno podtržítko a za řetězcem není konec řádku ani jiný skrytý znak.
- Příklad pro kontrakt mezi `10.99.99.2` a `10.99.99.10`:

  `10.99.99.2_10.99.99.10`

- Bezprostředně po použití **Default** přepočítat **SHA-256 key** jako lowercase hex SHA-256 přes ASCII, respektive totožné UTF-8 bajty tohoto řetězce. Výsledek musí být shodný se sdíleným pravidlem `defaultPayloadKey` používaným při vytváření výchozích komunikačních kontraktů ve Wizardu.
- **Default** musí fungovat pro každý platný kontrakt v podporovaném VPN adresním prostoru, včetně IP adres s posledním oktetem větším než 15. Omezení kandidátů `.2` až `.15`, které používá Wizard při automatickém doplňování kontraktů, se na tuto akci nevztahuje.
- Po použití **Default** zůstane vyplněný výchozí řetězec v poli **Password** běžně editovatelný. Každá další ruční změna znovu přepočítá hash standardním způsobem.
- Samotná akce **Default** nesmí změnit hodnotu kontraktu v rodičovském okně. Stejně jako ruční heslo se uplatní až po potvrzení tlačítkem **OK**.
- Tlačítko **Default** musí používat červený styl tlačítka. Červený vzhled se vztahuje na všechny jeho běžné interakční stavy včetně hover, focus a pressed stavu a musí zůstat čitelný a konzistentní se sdíleným vykreslováním tlačítek aplikace.

## Read-only textbox SHA-256 key

- Výsledek **SHA-256 key** zobrazit stejnou sdílenou moderní textboxovou komponentou, kterou používá editovatelné pole **Password**, ale v jejím read-only režimu. Nestačí pouze vytvořit nativní `EDIT` s read-only příznakem a původním hranatým rámečkem.
- Read-only varianta musí používat stejné zaoblené rohy a stejnou kvalitu vykreslení jako editovatelná varianta.
- Vnitřní plocha read-only varianty bude šedá a text tmavě šedý, aby byl read-only stav jednoznačný a současně dobře čitelný.
- Textbox nesmí dovolit editaci výsledku.
- Musí umožnit fokus, označení textu, kopírování přes Ctrl+C a standardní kontextové menu nativního Win32 read-only pole.
- Musí zobrazovat přesně 64 malých hexadecimálních znaků pro každý platný výsledek.
- Zachovat existující pravidla pro zneplatnění výsledku a zakázání **OK** při chybě převodu nebo kryptografické operace.
- Read-only textbox není disabled pole. Šedý vzhled nesmí vypnout fokus, označení textu, horizontální posun, kopírování ani kontextové menu.

## Patička Password SHA

- Oddělit patičku od obsahové části jemnou vodorovnou linkou ve vizuálním stylu ostatních oddělovačů aplikace.
- Oddělovací linka bude mít tloušťku jednoho fyzického pixelu a musí zůstat ostrá a souvislá při podporovaném DPI.
- Tlačítko **Default** umístit vlevo v patičce a vykreslit je červeným stylem.
- Tlačítko **OK** ponechat vpravo v patičce.
- Obě tlačítka použijí současný sdílený moderní styl tlačítek tray aplikace.
- **OK** zachová současné chování: přenese aktuální platný SHA-256 key do správného řádku kontraktu a zavře dialog.
- Escape a zavření přes systémové tlačítko nadále zavřou dialog bez aplikace nepotvrzené hodnoty.
- Tab order upravit tak, aby zahrnoval nový **Default** a zůstal předvídatelný: Password, SHA-256 key, Default, OK. Enter nadále spustí primární akci **OK**, pokud fokus nemá jiné tlačítko.

## Minimální velikost a resize dialogu

- Výchozí klientská velikost zůstane přibližně **675 × 360 DIP**.
- Nastavit minimální klientskou velikost dialogu na **480 × 240 DIP**.
- Minimální vnější rozměr odvodit z minimální klientské plochy způsobem správným pro aktuální DPI, systémový titulek a rám okna. Nepoužívat pevný počet fyzických pixelů nezávislý na DPI.
- Uživatel nesmí tažením hrany, rohu ani systémovým příkazem zmenšit okno pod vypočtený minimální rozměr.
- Při minimální šířce musí zůstat obsah čitelný, oba textboxy použitelné a tlačítka **Default** a **OK** se nesmějí překrývat, dotýkat ani narazit do okraje okna.
- Při minimální výšce musí zůstat viditelný nadpis, popis, label **Password**, celý textbox **Password** a použitelná patička s oběma tlačítky. Níže položený obsah smí být dostupný prostřednictvím existujícího vertikálního posunu obsahové oblasti.
- Po zmenšení a opětovném zvětšení nesmí vzniknout ořezání, překryv, dvojitá hrana, zbytkové pixely ani nesprávná pozice scrollu.
- Omezení musí fungovat při všech podporovaných hodnotách DPI a musí respektovat dostupnou pracovní plochu monitoru.

## Kontrastní hlavička tabulky Communication contracts

- Pod hlavním nadpisem **Communication contracts** změnit popisný text na přesné znění:

  `Manage secure communication between this computer and other virtual IP addresses using end-to-end encryption.`

- Text pouze rozšiřuje současný popis o informaci o end-to-end šifrování. Zachovat jeho současnou pozici, styl a funkci v layoutu a zajistit korektní zalomení při podporované velikosti okna.
- V hlavním okně **Communication contracts** změnit pozadí všech buněk hlavičky tabulky na jednotnou tmavě šedou barvu.
- Text názvů sloupců zobrazit bíle a tučně.
- Zachovat současné názvy, pořadí, zarovnání a význam sloupců.
- Zajistit dostatečný kontrast a čitelnost textu v běžném, aktivním i překresleném stavu.
- Tmavé pozadí musí souvisle pokrýt celou hlavičku bez světlých mezer mezi buňkami nebo u okrajů tabulky.
- Neměnit barvy datových řádků, stránkovací lištu, řádkové ovládací prvky ani datovou logiku tabulky.

## Maximální délka editovatelného Key

- V každém editovatelném poli **Key** v tabulce **Communication contracts** nastavit maximální délku na **128 znaků**.
- Pole nesmí nikdy obsahovat více než 128 znaků, ať už uživatel píše z klávesnice, vkládá text ze schránky nebo používá jinou standardní editační operaci nativního textboxu.
- Použít nativní limit vstupu nebo rovnocenné bezpečné omezení sdílené textboxové komponenty. Nadlimitní vstup nesmí způsobit přetečení, změnu jiného řádku ani ztrátu již přijatého obsahu.
- Stávající pravidlo platnosti se nemění. Platný klíč má nadále právě 64 malých hexadecimálních znaků `0-9` a `a-f`.
- Obsah s jinou délkou v povoleném rozsahu 0 až 128 znaků nebo s neplatným znakem zůstane označen současným červeným validačním stylem.
- Limit musí být správně znovu nastaven také při vytváření řádkových GUI prvků po stránkování, aby jej měly všechny kontrakty a všechny stránky.

## Zachování stávajícího chování

- Neměnit výpočet SHA-256 pro ručně zadané heslo.
- Neměnit limity vstupu, Unicode validaci, chybové stavy ani zacházení s citlivými pracovními daty.
- Neměnit zachycení identity cílového kontraktu při otevření dialogu.
- Neměnit nemodální charakter okna, možnost otevřít více oken Password SHA ani práci rodičovského okna.
- Neměnit potvrzovací workflow, dirty stav, stránkování, ukládání konfigurace, IPC ani síťovou komunikaci.
- Neměnit běžnou projektovou dokumentaci.

## Akceptační kritéria

1. Okno **Password SHA** je přibližně o 25 % širší než původní dialog a celý 64znakový klíč má bezpečný prostor.
2. Při běžném otevření je **Password** prázdný a není předvyplněn výchozím identifikátorem kontraktu.
3. Patičku od obsahu odděluje jemná vodorovná linka o tloušťce jednoho fyzického pixelu.
4. **Default** je vlevo a **OK** vpravo v patičce.
5. **Default** vyplní Password přesným kanonickým řetězcem `minNumericIp_maxNumericIp` pro neměnný cílový kontrakt.
6. Po použití **Default** je SHA-256 key shodný s `defaultPayloadKey` pro danou dvojici IP adres.
7. **Default** funguje také pro platné kontrakty s posledním oktetem větším než 15.
8. **Default** sám nezmění rodičovský kontrakt. Hodnota se použije až po potvrzení přes **OK**.
9. **Default** používá červený styl ve všech běžných interakčních stavech.
10. **SHA-256 key** používá stejnou moderní zaoblenou textboxovou komponentu jako Password, pouze v read-only režimu se šedým pozadím a tmavě šedým textem.
11. Read-only SHA-256 key nelze editovat, ale lze jej fokusovat, označit, horizontálně posouvat a zkopírovat.
12. Ruční editace Password nadále automaticky přepočítává výsledek podle stávajících pravidel.
13. Dialog nelze zmenšit pod klientskou plochu 480 × 240 DIP, přepočtenou korektně pro aktuální DPI.
14. Při minimální velikosti se tlačítka nepřekrývají a zůstává viditelný celý textbox Password i použitelná patička.
15. Podnadpis Communication contracts přesně uvádí, že komunikace používá end-to-end encryption.
16. Hlavička tabulky **Communication contracts** má souvislé tmavě šedé pozadí a bílé tučné názvy sloupců.
17. Žádný editovatelný Key v Communication contracts nepřijme více než 128 znaků, včetně vložení ze schránky a řádků vytvořených po změně stránky.
18. Validace nadále přijímá pouze přesně 64 malých hexadecimálních znaků a ostatní povolený obsah označuje červeně.
19. Datové řádky, stránkování, ukládání a ostatní aplikační workflow zůstanou beze změny.
20. Rozložení je čisté při podporovaném DPI, bez ořezu, překryvů, dvojitých hran a zbytkových pixelů.

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
