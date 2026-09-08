# 003 - Anti-aliasing veškerého vlastního vykreslování tray aplikace

## Cíl

Odstranit zubaté hrany a hrubé vykreslení všech grafických prvků, které si tray aplikace kreslí sama. Vlastní tvary a texty mají být vykreslené kvalitním anti-aliasingem, aniž by se měnilo rozložení, barvy, význam nebo funkční chování aplikace.

## Princip rozsahu

- Úprava platí pro veškeré vlastní kreslení v tray aplikaci.
- Prvky kompletně vykreslované operačním systémem zůstávají nativní a tato úprava je nenahrazuje.
- Prosté zapnutí jiného příznaku GDI nestačí, pokud nezajistí skutečně hladké hrany vlastních vektorových tvarů.
- Implementace má použít jednu sdílenou a konzistentní vyhlazovanou vykreslovací cestu, pokud je to pro dané prvky technicky možné.

## Vlastní grafické prvky v rozsahu

### Sdílené switche

- stopa a kruhový jezdec všech switchů ve wizardu.
- stopa a kruhový jezdec všech switchů v Communication Contracts.
- obrys nebo zvýraznění keyboard focusu, pokud je vykreslované aplikací.
- textové labely kreslené uvnitř vlastního ovládacího prvku, nikoli nativní text vykreslený Windows.

### Configuration Wizard

- kruhové body jednotlivých kroků.
- čísla a fajfky uvnitř bodů.
- názvy a stavové texty v levém sidebaru.
- zvýraznění aktivního kroku, pokud je vykreslované aplikací.
- zaoblené karty a jejich vlastní okraje.
- vlastní oddělovače a dělicí čáry.
- vlastní primární tlačítko a další tlačítka, která aplikace sama vykresluje.

### Communication Contracts

- vlastní vykreslení tabulky, jejího záhlaví a textů řádků.
- oddělovače řádků a další vlastní linky.
- vlastní primární tlačítko.
- všechny další tvary nebo texty kreslené přímo do plochy okna.

### Ostatní okna tray aplikace

- zaoblené karty, stavové panely a jejich okraje v Progress a Statistics.
- vlastní primární tlačítka v Edit Config, Communication Contracts, Password SHA a dalších oknech.
- vlastní stavové plochy, zvýraznění, oddělovače a dekorativní linky.
- vlastní texty kreslené přímo do plochy okna nebo ovládacího prvku.
- další aplikací kreslené kruhy, zaoblené obdélníky, symboly nebo ikony vytvořené z vektorových primitiv.

## Prvky mimo rozsah

- systémový titulkový pruh a jeho tlačítka.
- nativní textová pole a jejich rámečky.
- nativní scrollbary.
- systémová menu.
- běžná systémová tlačítka, pokud je kompletně vykresluje Windows.
- tray ikony a ostatní hotové rastrové nebo ikonové zdroje. Ty se nemají překreslovat ani uměle vyhlazovat za běhu.
- nativní text, který již Windows vykresluje pomocí ClearType a aplikace do něj nevstupuje vlastním paint handlerem.

## Požadovaná kvalita vykreslení

- Křivky, kruhy, pill tvary a zaoblené rohy musí mít hladké hrany bez viditelných schodů.
- Vlastní text musí používat vhodné vyhlazování písma a nesmí být méně čitelný než současný nativní ClearType text.
- Čísla a fajfky v bodech kroků musí být ostré, správně vystředěné a bez roztřepených hran.
- Tenké vodorovné a svislé čáry musí zůstat ostré. Anti-aliasing je nesmí změnit na rozmazané šedé pruhy.
- Výsledek musí být stabilní při běžných hodnotách DPI, změně DPI za běhu a resize okna.
- Vykreslení nesmí blikat, zanechávat artefakty ani zobrazovat části předchozího snímku.
- Výsledek má vizuálně odpovídat kvalitě referenčních mockupů, ne nutně jejich přesné geometrii nebo jiným prvkům mimo rozsah této úpravy.

## Funkční požadavky

- Tato změna nesmí měnit hit testing, klikací plochy, tab order, keyboard focus ani ovládání klávesnicí.
- Accessibility názvy, role, stavy a akce všech controls musí zůstat zachované.
- Stavové barvy a jejich význam musí zůstat zachované.
- Zakázané, aktivní, zaměřené, stisknuté, zapnuté a vypnuté stavy musí zůstat rozlišitelné.
- Vykreslovací zdroje musí být korektně vytvořené, znovu vytvořené při ztrátě zařízení nebo změně DPI a uvolněné při zavření okna.
- Implementace nesmí zavést neomezený růst GDI, USER, Direct2D, DirectWrite ani jiných grafických prostředků.
- Selhání inicializace vyhlazované vykreslovací cesty nesmí způsobit pád aplikace. Musí skončit bezpečným a použitelným fallbackem.

## Beze změny

- Rozložení oken, rozměry a pozice prvků s výjimkou zanedbatelné technické korekce nutné pro pixelové zarovnání čar.
- Texty, barvy, workflow a aplikační logika.
- Modely, validace, ukládání konfigurace, IPC a síťová komunikace.
- Obsah a formát tray ikon a ostatních existujících rastrových zdrojů.
- Nativní systémové prvky uvedené mimo rozsah.

## Akceptační kritéria

1. Všechny aplikací kreslené kruhy, pill tvary, zaoblené rohy a symboly mají při běžném zobrazení hladké hrany.
2. Switche ve wizardu i Contracts nemají zubatý obrys ani nepravidelný jezdec.
3. Body kroků wizardu, jejich čísla a fajfky jsou hladké a správně vystředěné.
4. Vlastní texty sidebaru a ostatních paint ploch jsou čitelné a kvalitativně nezaostávají za nativním ClearType textem.
5. Karty, panely a vlastní primární tlačítka mají hladké zaoblené hrany.
6. Tenké oddělovače a tabulkové linky zůstávají ostré a nejsou rozmazané.
7. Vzhled je stabilní při resize a podporovaných hodnotách DPI bez blikání a paint artefaktů.
8. Všechny interakce, stavy a accessibility vlastnosti zůstávají funkčně beze změny.
9. Opakované otevření a zavření oken nezpůsobí trvalý růst grafických prostředků.
10. Nativní systémové prvky a hotové ikonové zdroje zůstanou nezměněné.

## Lehká kontrola po implementaci

- Vizuálně porovnat switch a sidebar wizardu s referenčními obrázky při 100% a jednom zvýšeném DPI.
- Krátce projít okna Wizard, Communication Contracts, Progress, Statistics, Edit Config a Password SHA a zkontrolovat jejich vlastní kreslené prvky.
- Změnit velikost alespoň dvou hlavních oken a ověřit absenci blikání a artefaktů.
- Ověřit základní stavy switche a jednoho vlastního primárního tlačítka.
- Krátce opakovaně otevřít a zavřít několik dotčených oken a zkontrolovat, že počet grafických prostředků nekontrolovaně neroste.
- Nespouštět těžké, plošné ani dlouhé testovací sady.
- Po úspěšné lehké kontrole sestavit Release tray EXE a vložit ho do standardní distribuce projektu.

## Referenční obrázky

- `current-jagged-rendering.png` zachycuje současné zubaté vykreslení switche a sidebaru.
- `target-antialiasing-reference.png` zachycuje požadovanou kvalitu vyhlazených tvarů a textu v referenčním mockupu.

## Mimo aktuální krok

Tento dokument pouze formalizuje požadavek. Implementace, sestavení a nasazení začnou až na výslovný pokyn uživatele.
