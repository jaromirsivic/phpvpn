# Vytvoření distribuce zdrojového kódu phpvpn

## Účel a rozsah

Tento dokument je postup pro agenta, který má vytvořit úplnou, přenositelnou distribuci **zdrojového kódu** projektu phpvpn. Platí i po budoucích změnách projektu. Uvedené názvy existujících součástí jsou orientační minimum, nikoli uzavřený seznam souborů k překopírování.

Samotný požadavek na vytvoření nebo úpravu tohoto dokumentu **neznamená pokyn provést distribuci**. Následující operace prováděj až při zadání vytvořit zdrojovou distribuci.

Výrazy **MUSÍ**, **NESMÍ** a **ZASTAV** jsou závazné. Při nejistotě, která ovlivňuje bezpečnost, úplnost, licenci nebo přenositelnost, nepředpokládej úspěch. Zastav dotčený postup a sděl konkrétní překážku uživateli.

| Úloha | Cesta |
| --- | --- |
| Výchozí zdrojový checkout | kořen tohoto repozitáře (složka s tímto dokumentem) |
| Nová zdrojová distribuce | `C:\gitsigma\phpvpn` |
| Záloha předchozího obsahu cíle | `C:\gitsigma\phpvpn_old` |
| Dočasný staging | Nový unikátní adresář vedle cíle, například `C:\gitsigma\phpvpn_source_stage_<id>` |
| Report a pracovní buildy | Samostatný unikátní pracovní adresář mimo zdroj, cíl, zálohu a staging |

Zdrojový checkout lze zvolit jiný, pokud to vyplývá ze zadání. Cíl a zálohu neměň bez výslovného pokynu. Absolutní cesty v této tabulce jsou parametry publikační operace na tomto stroji. NESMĚJÍ se stát předpokladem sestavení nebo běhu projektu u příjemce.

Dokument [distribution.md](distribution.md) popisuje sestavení binární distribuce. Do zdrojového balíku patří tento návod i jeho skripty, ale jeho požadavky na přibalení EXE, DLL, RPM nebo DEB se na zdrojový balík **nevztahují**. Zdrojová distribuce zachovává strukturu zdrojového projektu, nikoli výsledného stromu `dist`.

## 1. Nejdřív zmapuj aktuální projekt

1. Přečti platné instrukce repozitáře. Je-li dostupný kontextový graf `graft`, začni `graft map` a relevantními dotazy. Graf slouží k orientaci, nikoli jako úplná bezpečnostní kontrola všech souborů.
2. Zaznamenej skutečný kořen checkoutu, větev, commit a stav pracovního stromu včetně staged, unstaged, untracked a ignored souborů. Použij například `git status --short --untracked-files=all`, `git ls-files -z`, `git ls-files --others --exclude-standard -z` a samostatný soupis ignorovaných položek. Nulou oddělené výstupy zpracovávej jako takové, ne dělením po řádcích.
3. Zkontroluj build konfigurace, presety, dependency a lock soubory, submoduly, LFS, CI, testy, generátory, prostředky aplikace, instalační a distribuční skripty a jejich skutečné závislosti. Následuj relativní odkazy a volání až ke všem potřebným vstupům.
4. Zjisti všechny aktuálně podporované platformy, serverové implementace a postupy sestavení. Nespoléhej jen na tento dokument nebo starý seznam přípon. Nový jazyk, platforma nebo adresář se posuzuje podle své funkce.
5. Připrav explicitní inventář souborů s rozhodnutím zahrnout/vyloučit a důvodem. Samotné sledování Gitem neznamená, že soubor patří do distribuce. Samotné ignorování Gitem neznamená, že není potřebný.
6. Distribuuj současný zamýšlený obsah pracovního stromu včetně relevantních necommitnutých změn. Nevracej soubory na `HEAD`, nedělej automatický commit, stash, checkout ani čištění zdrojového repozitáře. U nejasných rozpracovaných součástí vyžádej upřesnění.
7. Po určení obsahu zachyť stabilní snímek a SHA-256 vstupů. Při změně zdrojů během kopírování postup přeruš a zopakuj přípravu z konzistentního stavu. Kombinace souborů z různých okamžiků NESMÍ být vydána jako jeden ověřený celek.

`git archive` samo o sobě nestačí. Vynechává necommitnutý obsah a neřeší úplnost submodulů, LFS, nevhodné sledované soubory ani kontrolu metadat.

## 2. Povinný obsah

Zahrň alespoň:

- Všechny vlastní zdrojové kódy klienta a serverů, hlavičky, potřebné prostředky, konfigurace sestavení, generátory, šablony, schémata a přenositelné ukázkové konfigurace.
- Testy a bezpečná testovací data potřebná k ověření distribuovaného projektu.
- Build, balicí, instalační a ověřovací skripty včetně všech helperů a šablon. Aktuálně mezi ně patří zejména `tools/make-distribution.ps1`, `tools/verify-distribution.ps1`, `tools/distribution-common.ps1`, `client/tools/` a `client/packaging/`. Podmínky zákazu binárek a kontroly lokálních vazeb platí i uvnitř těchto adresářů.
- Vstupy i lock soubory závislostí, například `server/python/requirements-server.in` a `server/python/requirements-server.txt`. Potřebné `.gitattributes`, `.gitignore` a další přenositelné skryté konfigurační soubory zachovej.
- Tento dokument a návod pro vytvoření binární distribuce. Pokud projekt má automatizaci zdrojové distribuce, zahrň její zdrojový skript, helpery, pravidla výběru a validátor. Nesmějí existovat pouze na stroji původního autora nebo odkazovat na nedodaný lokální nástroj.
- Dokumentaci sestavení, testování, získání externích závislostí a omezení podpory platforem.

### Povinné popisy projektu

| Dokument | Pravidlo |
| --- | --- |
| `protocol.md` | Úplná aktuální specifikace protokolu. |
| `client.md` | Úplný aktuální popis klienta. |
| `tray_ux.md` | Logický název dokumentace tray UX. V aktuálním repozitáři se soubor jmenuje `tray_UX.md`. Zachovej skutečné psaní názvu i shodné odkazy, nevytvářej druhou kopii lišící se pouze velikostí písmen. |
| Všechny serverové návody | Rekurzivně vyhledej bez rozlišení velikosti písmen alespoň `server.md`, `server-*.md` a `server_*.md`. Zahrň i další dokumenty popisující jednotlivé servery, pokud mají jiné názvy. Aktuálně sem patří anglická `README.md` ve složkách `server/php`, `server/python`, `server/node.js` a `server/wordpress` a vývojářský `server/wordpress/version7/README.md`. |
| `readme.txt` | Povinný soubor v kořeni výsledku, který srozumitelně popisuje zdrojovou distribuci. |
| `LICENSE` | Povinný kořenový soubor s úplným textem MIT licence pro vlastní projekt při splnění licenční kontroly níže. |

Dokumenty nepřejmenovávej ani nezplošťuj adresáře bez ověření všech odkazů a skriptů. Zahrň potřebné lokální obrázky a další přílohy dokumentace. Zkontroluj odkazy i na systému rozlišujícím velikost písmen. Chybějící povinný dokument nenahrazuj prázdným souborem nebo vymyšleným popisem.

Kořenový `readme.txt` musí vysvětlit, že jde o zdrojové kódy, uvést rozložení projektu, předpoklady a přesné příkazy pro získání závislostí, sestavení, testování a vytvoření binární distribuce, odkazy na povinné návody a licence. Uveď také potřebné externí nástroje, případný přístup k síti a skutečně ověřené platformy. Při prvotním vytvoření zachovej jazyk a styl projektu. Následný překlad manuálů se řídí oddílem 7 a vyžaduje výslovné povolení uživatele.

Současný kořenový `readme.txt` popisuje instalaci hotové binární distribuce. Je-li tomu při provádění stále tak, zachovej jeho původní obsah na místě očekávaném binárním packagerem a doplň kořenový README zdrojové distribuce tak, aby nebyl zavádějící. Prověř zejména skripty ověřující bajtovou shodu README. Jakákoli úprava balicího stromu nebo README musí být zaznamenaná, ověřená a nesmí tiše změnit význam binárního návodu. Zdrojový checkout tím bez samostatného zadání nepřepisuj.

### Sestavení binární distribuce z nového Git repozitáře

Do kořenového `readme.txt` zdrojové distribuce uveď tento předpoklad: před sestavením kompletní binární distribuce musí příjemce vložit dodané zdroje do svého Git repozitáře a vytvořit commit. Může jít o jiný repozitář s vlastní historií. Původní `.git` se nepřenáší a původní historie není k sestavení potřeba.

Linuxové balíčky i následná kompletní binární distribuce musí vzniknout ze stejného commitu nového repozitáře, bez necommitnutých změn distribuovaných zdrojů. `tools/make-distribution.ps1` čte aktuální `HEAD` a porovnává jej s původem linuxových balíčků. Balíčky sestavené z původního repozitáře proto nelze použít jen na základě shody obsahu, pokud uvádějí jiný commit. Kontrolu původu neobcházej přepínačem `-AllowLinuxProvenanceMismatch`.

Commit původního checkoutu v `source-distribution-manifest.json` zůstává záznamem původu dodaných zdrojů. Nenahrazuje commit nového repozitáře používaný při sestavení binárních balíčků a nesmí se za něj přepsat.

Pro ověření tohoto postupu je dovoleno inicializovat samostatný dočasný Git repozitář a vytvořit testovací commit pouze v externí ověřovací kopii podle oddílu 5. Použij neutrální testovací identitu. Tento repozitář ani jeho `.git` nepatří do stagingu, cíle nebo zálohy a nic se z něj nepublikuje. Inicializace či commit ve skutečném cílovém repozitáři nadále vyžadují samostatné zadání.

### Licence a externí komponenty

Cílová licence vlastního projektu je MIT. Nejprve ověř existující licenční prohlášení a oprávnění k jejich použití. V aktuálním stromu je text MIT v `client/packaging/linux/LICENSE`, jeho samotná existence však není důkazem licence každé součásti. Pro kořenové `LICENSE` použij příslušný úplný text a doložené copyright údaje. Nevymýšlej autora, neměň držitele práv ani neodstraňuj povinné atribuce.

Zachovej licence, copyright oznámení a případné NOTICE soubory třetích stran. MIT licence projektu je nenahrazuje. Konflikt licencí, nejasná práva nebo zakázané jméno v povinné atribuci znamenají **ZASTAV** a oznámení uživateli, nikoli automatickou anonymizaci nebo přelicencování.

## 3. Zakázaný obsah a úplnost bez binárek

Do výsledku NESMĚJÍ být zahrnuty:

- Zkompilované a nativní spustitelné soubory, knihovny, objektové soubory, ovladače, debug symboly a bytecode. Typicky EXE, DLL, SYS, COM, OBJ, O, LIB, A, SO včetně verzovaných názvů, DYLIB, PDB, PYC, PYO, CLASS a WASM.
- Instalační a hotové aplikační balíčky, například MSI, MSIX, DEB, RPM, AppImage, APK, JAR nebo WHL. Ani archiv nesmí sloužit k ukrytí zakázaného obsahu.
- Výsledky sestavení, CMake cache a generované build stromy, virtuální prostředí, závislosti stažené jako lokální cache, `node_modules`, nástroje dodávané jako binárky a předchozí distribuce.
- Žádné certifikáty, včetně veřejných, kořenových, vývojových, testovacích a self-signed certifikátů, certifikačních řetězců a úložišť certifikátů. Zákaz platí i pro certifikáty vložené do zdrojového kódu, dokumentace, prostředků, testovacích dat nebo archivů.
- Citlivé klíče, soukromé podpisové materiály, skutečná hesla, přístupové tokeny, API klíče a další přihlašovací údaje. Výjimku pro veřejné výchozí hodnoty phpvpn stanoví následující oddíl.
- Provozní konfigurace s citlivými údaji, relace, fronty, databáze, logy, dumpy, dočasné soubory a zálohy. Vývojovou a testovací konfiguraci, například `phpvpn.config.json`, zahrň podle výjimky v oddílu 4.
- `.git` ani jiná historie VCS, lokální konfigurace editorů, osobní nastavení, pracovní reporty a interní poznámky bez významu pro příjemce. Generovatelný kontextový graf a lokální stav agentů také nejsou součástí distribuovaného zdroje.

Rozhoduj podle obsahu a účelu, nikoli jen přípony nebo názvu složky. Například `client/packaging/linux/dist/` dnes obsahuje zdrojové instalační skripty a dokumentaci, které nelze vyřadit pouze kvůli názvu `dist`. Naopak nativní ELF bez přípony je stále zakázaná binárka. Kontrola musí poznat PE, ELF a Mach-O a jejich běžné varianty, knihovny a bytecode i po přejmenování.

Textové zdrojové a instalační skripty `.ps1`, `.sh`, `.bat`, `.py`, `.php` nebo `.js` jsou povinnými zdrojovými vstupy, pokud je projekt potřebuje. Zákaz spustitelných souborů se nevztahuje na tyto textové skripty ani na jejich POSIX execute bit. Zachovej potřebné režimy a konce řádků. Zdrojové ikony, obrázky a další nezkompilované prostředky lze přibalit po kontrole obsahu, metadat a licence.

V aktuálním repozitáři jsou například `client/third_party/wintun/*/wintun.dll`. **Tyto DLL se do zdrojové distribuce nekopírují.** Hlavičky, licence a návody zachovej. Pro tuto i každou budoucí binární závislost musí příjemce mít dokumentovaný, přenosný a ověřitelný způsob obstarání kompatibilní verze z oficiálního zdroje, případně sestavení ze zdrojů. Uveď verzi, integritu a očekávané umístění. Ověření závislostí nepřeskakuj a neoslabuj podpisové kontroly packagerů.

Jestliže stávající build nebo packager vyžaduje vyřazený soubor, nestačí jej smazat a prohlásit balík za hotový. Nejdřív zajisti a ověř reprodukovatelný postup získání závislosti. Potřebná oprava projektu přesahující zadání se předá uživateli jako překážka. Privátní podpisový klíč se nenahrazuje klíčem původního autora, jeho exportem ani vypnutím kontrol. Dokumentuj podporovaný způsob vytvoření či použití vlastního podpisového materiálu.

### Certifikáty, tajemství a povolené výchozí hodnoty

**Veřejná výchozí hesla a tokeny phpvpn jsou povolené.** Zachovej je, pokud jde o doložené výchozí hodnoty projektu určené k veřejnému šíření, nikoli o skutečné provozní údaje. To zahrnuje i veřejné výchozí hodnoty v polích označených jako secret nebo access key, pokud mají právě tuto funkci. Samotný název proměnné, nízká entropie nebo výskyt v testu či README není důkazem bezpečné výchozí hodnoty. README musí upozorňovat na jejich změnu před skutečným nasazením.

Při každé distribuci odvoď přesný seznam povolených hodnot z aktuálních zdrojů a dokumentace projektu a zaznamenej zdůvodnění výjimek. Nepovoluj plošně celý soubor, adresář nebo všechny hodnoty proměnné. Nejasný původ údaje blokuje publikaci do vyjasnění. Výjimka pro výchozí hesla a tokeny **nikdy nepovoluje certifikát ani soukromý podpisový klíč**.

Kontroluj obsah a metadata všech kandidátů i výsledku, včetně skrytých souborů, řetězců ve zdrojích, URL s přihlašovacími údaji a zakódovaných hodnot. Přípony jako `.cer`, `.crt`, `.pem`, `.der`, `.pfx`, `.p12`, `.p7b`, `.jks`, `.keystore` nebo `.key` slouží pouze jako indikátory. Kontrola musí rozpoznat certifikáty a citlivé klíče i bez přípony, v jiném formátu nebo vložené do textu. Certifikáty nelze přijmout jen proto, že neobsahují soukromý klíč.

Samostatné certifikáty a lokální tajné soubory vyřaď z inventáře. Pokud jsou certifikáty nebo nepovolená tajemství vložené do potřebných zdrojů, nebo jejich vyřazení znefunkční sestavení či testy, zastav publikaci a oznam překážku bez vypsání citlivé hodnoty. Neodstraňuj automaticky části kódu a neoslabuj ověřování podpisů. Potřebné certifikáty a klíče si musí příjemce obstarat nebo vygenerovat dokumentovaným postupem mimo distribuovaný balík. Stejně tak případné testovací certifikáty vznikají až při testu a nesmějí zůstat ve stagingu.

Zákaz přibalení se vztahuje na novou zdrojovou distribuci a její manifest i dokumentaci. Interní záloha předchozího cíle podle oddílu 6 musí zůstat úplná, nesmí se kvůli tomuto zákazu filtrovat a nikdy se nepřibaluje k distribuci. Do reportu zapisuj pouze umístění a druh citlivého nálezu, nikoli jeho hodnotu.

## 4. Povinná kontrola jmen, metadat a lokálních vazeb

Tuto kontrolu proveď **před spuštěním skriptů projektu a před jakoukoli změnou cíle nebo zálohy**, znovu nad stagingem a nakonec nad výsledkem. Audit musí pokrýt celý aktuální zdrojový strom včetně skrytých, nesledovaných a ignorovaných položek, nejen plánovaný obsah balíku. Položky určené k vyloučení nesmějí sloužit jako slepé místo pro nálezy ve zdrojích. Git historii není třeba exportovat ani přepisovat, ale metadata aktuálních souborů a použitá metadata Git zkontroluj. Nesleduj odkazy mimo prověřovaný strom.

### Zakázaná jména

Hledej oba uživatelem zakázané řetězce jako podřetězce bez ohledu na velikost písmen. Jejich přesná definice pomocí Unicode bodů je:

| Řetězec | Unicode body |
| --- | --- |
| Zakázané křestní jméno | `U+004A U+0061 U+0072 U+006F U+006D U+00ED U+0072` |
| Zakázané příjmení | `U+0160 U+0069 U+0076 U+0069 U+0063` |

Definice pomocí kódových bodů umožňuje distribuovat toto kontrolní pravidlo, aniž by jeho doslovná citace sama vložila zakázané jméno do balíku. Kontrolní nástroj z bodů vytvoří hledané řetězce v paměti. Text nejprve normalizuj do Unicode NFC a porovnávej bez rozlišení velikosti písmen. Navíc prověř varianty bez diakritiky, rozložené Unicode zápisy a escapované či zakódované hodnoty v podporovaných formátech. Tyto varianty také považuj za nález k vyjasnění.

Kontrola zahrnuje názvy souborů a adresářů, obsah textu, komentáře, copyright, autorství, resource a manifest soubory, konfigurační hodnoty, metadata obrázků a dokumentů, alternativní datové proudy NTFS a další metadata, která se mohou přenést. Neomezuj se na řádky zobrazované editorem. Zohledni UTF-8, UTF-16 i další skutečně použitá kódování. U kontejnerů kontroluj vnitřní názvy, obsah a metadata bez spuštění jejich obsahu a bez nebezpečného rozbalení cest mimo pracovní adresář.

Pouhý průchod `rg` nebo vyhledání ASCII řetězců v binárním souboru není úplný audit metadat. Pro každý ponechaný netextový formát musí být dostupná odpovídající kontrola. Nečitelný soubor, nepodporovaný relevantní formát, chyba dekódování nebo neprověřená metadata znamenají neúplný audit a blokují publikaci.

### Vazby na konkrétní počítač

**Absolutní cesty specifické pro daný počítač, zejména cesty pro testování, patří pouze do konfigurace, nikoli natvrdo do zdrojového kódu nebo skriptů.** Kód a skripty je musí načítat z konfigurace.

**Pro konfigurační soubory neplatí zákaz absolutních cest.** Soubor `phpvpn.config.json` lze přenést do nové zdrojové distribuce se stávajícími hodnotami, včetně cest k nástrojům, testovacím adresářům a SSH klíči i nastavení lokální VM. Samotná taková konfigurační hodnota není důvodem k zastavení distribuce a automaticky ji nepřepisuj ani neanonymizuj. Cesta ke klíči nepovoluje přibalit samotný soukromý klíč. Ostatní kontroly obsahu konfigurace, zejména zákaz certifikátů a citlivých přihlašovacích údajů, zůstávají v platnosti.

V kořenovém `readme.txt` vysvětli, že příjemce musí před sestavením nebo testováním přizpůsobit konfiguraci svému prostředí. Při ověření přenositelnosti uprav konfiguraci pouze v externí ověřovací kopii nebo použij podporovaný konfigurační přepínač `PHPVPN_CONFIG`. Do reportu zaznamenej použité nastavení bez tajemství. Tato výjimka neomlouvá skryté závislosti v kódu ani neprovedené kontroly sestavení.

Prověř zejména:

- Absolutní Windows a UNC cesty, `file://` odkazy, uživatelské profily, domovské adresáře, konkrétní checkouty, připojené disky, lokální VM a interní síťové adresy.
- Natvrdo zadané cesty ke kompilátoru, SDK, Pythonu, CMake, knihovnám nebo cache. Také dependency odkazy typu lokální `file:` či editable instalace mimo projekt.
- CMake cache, user presety, generované projektové soubory, proměnné prostředí, registry, názvy účtů, stroje, certifikáty či thumbprinty, bez nichž by jiný uživatel nedokázal projekt sestavit nebo zabalit.
- Skripty odkazující na nedodané helpery, soubory mimo kořen projektu, soukromé repozitáře nebo nastavení dostupné jen původnímu autorovi.

Nález absolutní cesty nejprve zařaď podle kontextu. Konfigurační hodnota podle výjimky výše, standardní systémová cesta, výslovný ilustrační placeholder, izolovaný testovací údaj nebo cílové cesty této publikační metodiky nejsou samy o sobě zakázanou vazbou na původní počítač. Výjimka musí mít konkrétní zdůvodnění v reportu. Nezaváděj plošné výjimky pro celé adresáře či všechny testy. Nevyřešená pochybnost o přenositelnosti mimo tyto výjimky znamená **ZASTAV**.

### Reakce na nález

Při nálezu zakázaného jména nebo podezřelé lokální vazby **okamžitě zastav tvorbu distribuce a oznam nález uživateli**. Uveď relativní cestu, řádek nebo název metadatového pole, typ problému, jeho dopad a návrh řešení. Cituj jen nezbytný úsek, tajemství maskuj. Neprováděj automatické nahrazení, anonymizaci, odstranění atribuce ani vynechání souboru za účelem obejití kontroly.

Neprováděj rotaci zálohy, mazání cíle ani publikaci. Pokud nález vznikne až při závěrečné kontrole, výsledek označ za vadný, zachovej ověřenou zálohu a postupuj podle obnovy níže. Pokračování v řešení nálezu vyžaduje výslovný pokyn uživatele a opakování dotčených kontrol.

## 5. Připrav a ověř staging

1. Vytvoř nový prázdný staging s unikátním názvem. Nekopíruj do něj starou distribuci a nepoužívej slepé zrcadlení zdrojového checkoutu.
2. Kopíruj pouze položky schváleného inventáře při zachování relativních cest. Vyhodnoť kolize názvů bez rozlišení velikosti písmen, nepřenosné názvy, neplatné relativní cesty a odkazy unikající mimo kořen. Kolize a nevyřešené odkazy blokují výsledek.
3. Symlinky, junctiony a jiné reparse pointy nekopíruj ani nenásleduj automaticky. Pokud je projekt potřebuje, stanov a ověř přenositelný způsob jejich dodání. Jinak zastav při nevyřešené závislosti. U submodulů a LFS ověř, že jsou přítomny skutečné zdroje v potřebné verzi, nikoli jen gitlink nebo pointer.
4. Přidej povinnou dokumentaci a licence. Obsah zdrojů musí odpovídat vstupnímu snímku, s výjimkou výslovně zaznamenaných úprav dokumentace a metadat balíku. Nepřenášej osobní ACL, vlastníka ani alternativní datové proudy. Potřebné skriptové režimy dokumentuj a ověř pro cílové platformy.
5. Vytvoř `source-distribution-manifest.json` s verzí schématu, verzí projektu, skutečným zdrojovým commitem nebo hodnotou nedostupnosti, příznakem necommitnutých změn a seznamem relativních cest, velikostí a SHA-256 všech distribuovaných souborů. Manifest samotný vynech ze svého seznamu, jeho hash zaznamenej do externího reportu. Nevkládej jméno autora, hostname, absolutní lokální cesty ani tajemství. Případné úpravy oproti zdrojovému snímku musí být dohledatelné v reportu.
6. Porovnej přesné množiny cest. Výsledek musí obsahovat právě deklarované soubory a manifest, včetně skrytých souborů. Každý povinný vstup musí být zahrnut nebo mít ověřený postup externího získání, pokud jej pravidla zakazují přibalit. Neznámý soubor znamená kontrolu, nikoli automatické přijetí.
7. Proveď kontrolu zakázaného obsahu, certifikátů, tajemství, jmen, metadat a cest podle předchozích oddílů. Výchozí hesla a tokeny přijímej jen podle doloženého seznamu povolených hodnot. Zkontroluj i nově vytvořené README a manifest.
8. Zkopíruj staging do nové ověřovací složky mimo původní checkout, ideálně s mezerami a neASCII znakem v cestě. Ověř sestavení podle dodaného návodu z této kopie bez `.git`, původní build cache, osobních presetů a přístupu k souborům původního checkoutu. Systémové nástroje jsou povolené pouze jako dokumentované předpoklady. Pro silné ověření použij čistý kontejner nebo VM, pokud je dostupný.
9. Získej závislosti dokumentovaným způsobem, nakonfiguruj a sestav podporované cíle, spusť relevantní testy a ověř, že dodané skripty dokážou vytvořit binární distribuci z této kopie. Pro kompletní binární distribuci nejprve vytvoř v ověřovací kopii nový testovací Git repozitář a commit podle oddílu 2. Nepřebírej původní `.git` ani historii. Pro Python používej `uv`, včetně instalace balíčků přes `uv pip`. Veškeré vzniklé binárky, prostředí a logy musí zůstat mimo staging a výsledný zdrojový balík.
10. Report musí rozlišit úspěch, chybu a neprovedenou kontrolu pro každou platformu. Chybějící prostředí ani přístup k závislosti neznamenají úspěch. Pokud nelze potřebnou přenositelnost nebo úplnost ověřit, nepublikuj, dokud uživatel výslovně nerozhodne o konkrétním omezení ověření. Zákaz binárek, jmen a nevyřešených lokálních vazeb zůstává v platnosti.
11. Po testech znovu ověř čistotu stagingu a shodu jeho hashů s manifestem. Teprve úspěšně ověřený staging smí postoupit k výměně cíle.

## 6. Bezpečná rotace a naplnění cíle

Požadované pořadí operací je závazné: **smazat předchozí `phpvpn_old`, zkopírovat stávající `phpvpn` do `phpvpn_old`, ověřit tuto zálohu, vymazat obsah `phpvpn`, naplnit jej novou distribucí**. Příprava stagingu a kontroly musí proběhnout před tímto pořadím.

### Před destruktivními operacemi

- Získej výhradní zámek publikační operace mimo cíl a zálohu. Samotné ověření existence lock souboru nestačí, zámek musí vzniknout atomicky. Zabraň souběžným publikacím a ověř, že do cíle právě nic nezapisuje.
- Kanonizuj absolutní cesty zdroje, cíle, zálohy, stagingu a pracovního adresáře. Ověř skutečné cíle v souborovém systému a všechny existující předky. Cíl a záloha musí být přesně očekávané dvě odlišné přímé děti `C:\gitsigma`, nikoli diskový kořen nebo alias jiné cesty. Zdroj, staging, cíl a záloha nesmějí být uvnitř sebe navzájem.
- Odmítni symlinky, junctiony, reparse pointy a mount pointy na cestách i uvnitř stromů určených k rekurzivnímu mazání nebo kopírování. Prověř i rizikové hardlinky. Neřešený alias souboru mimo určený strom operaci blokuje.
- Ověř oprávnění, čitelnost, volné místo pro úplnou zálohu a nový obsah a stabilitu stávajícího cíle. Zaznamenej jeho soupis a hashe v externím reportu ještě před rotací. Nečitelnost či změna cíle znamená zastavení.
- Existující cíl může obsahovat i skryté soubory, `.git`, staré binárky nebo soubory nesouvisející s novým inventářem. Záloha musí zachytit celý předchozí obsah. Nová distribuce jej nepřebírá. Je-li některá očekávaná adresářová cesta běžný soubor nebo se její stav během kontroly změní, zastav.
- Než smažeš předchozí `phpvpn_old`, musí být jisté, že není jedinou úplnou kopií po dříve přerušené operaci. Při záznamu nedokončené publikace, částečném cíli nebo nejasném stavu nejprve vyřeš obnovu. Opakované spuštění nesmí přepsat poslední platnou zálohu neúplným cílem.

### Přesný postup

1. Do externího stavového záznamu ulož identifikátor operace a stav `staging_verified`. Stav zapisuj atomickou výměnou souboru a aktualizuj při každém následujícím kroku, aby šlo rozpoznat přerušený běh.
2. Pokud `C:\gitsigma\phpvpn_old` existuje, po právě provedeném ověření přesné cesty jej celý odstraň. Jakákoli chyba mazání operaci ukončuje. Neignoruj ji.
3. Pokud `C:\gitsigma\phpvpn` existuje, zkopíruj jeho **veškerý obsah včetně skrytých položek** do nového `C:\gitsigma\phpvpn_old`. Zachovej i prázdné adresáře a metadata potřebná pro obnovu, včetně případných alternativních datových proudů a oprávnění. To se týká interní zálohy, nikoli zdrojové distribuce. Zálohu nefiltruj pravidly pro nový balík. Ověř přesné množiny cest, typy, délky, SHA-256, obnovitelná metadata a stabilitu původního cíle. Teprve potom zapiš `backup_verified`.
4. Pokud cíl při prvním vydání neexistuje, zaznamenej tuto skutečnost a přeskoč kopírování neexistujícího adresáře. Nevytvářej falešnou zálohu. Jeho nepřítomnost ale nesmí maskovat přerušenou předchozí publikaci.
5. Těsně před mazáním znovu ověř cestu cíle, jeho stav a nezměněnost vůči ověřené záloze. Vymaž všechny jeho děti včetně skrytých souborů. Kořenový adresář ponech, nebo jej vytvoř, pokud dosud neexistoval. Ověř, že je opravdu prázdný. Zapiš `target_empty`.
6. Zkopíruj do cíle obsah ověřeného stagingu bez další obalové složky. Zapiš `target_copied`.
7. Znovu porovnej přesnou množinu cest a hashe se stagingem a manifestem. Ověř povinné dokumenty, licence, absenci zakázaného obsahu, všech certifikátů, nepovolených tajemství, jmen a podezřelých lokálních vazeb. Nečekané soubory nebo odlišná metadata znamenají neúspěch.
8. Teprve po úspěšném ověření zapiš `source_distribution_verified`. Zdrojová distribuce je vytvořená, následuje rozhodnutí o volitelném překladu podle oddílu 7. Celý požadovaný postup označ `complete` až po dokončení povoleného překladu, nebo po zaznamenání, že překlad nebyl povolen a neproběhl. Platnou zálohu `phpvpn_old` ponech pro příští rotaci i jako referenci pro překlad.

V PowerShellu používej práci s doslovnými cestami (`-LiteralPath` tam, kde ji cmdlet podporuje), zahrnutí skrytých položek a ukončení při chybě. Kontroluj návratové kódy nativních nástrojů podle jejich skutečného významu. Nepoužívej `cmd /c` s vygenerovaným mazacím příkazem, `Invoke-Expression` ani široké wildcard mazání z neověřeného pracovního adresáře. Ověření před destruktivní operací musí proběhnout za drženého zámku. Zámek koordinuje publikační nástroje, nesmí být vydáván za ochranu proti všem cizím procesům.

### Selhání a obnova

- Před vymazáním cíle ponech cíl beze změny. Při neúspěšné kopii zálohy neoznač částečnou `phpvpn_old` za platnou a nezahajuj mazání cíle. Smazání předchozí generace zálohy je podle předepsaného pořadí nevratné, neslibuj její obnovu.
- Po vymazání cíle nebo při chybě publikace zachovej ověřenou zálohu i staging a přesný stav operace. Pokud je bezpečné cíl znovu vyprázdnit a nebyl mezitím změněn jiným procesem, obnov jej kopií z ověřené `phpvpn_old` a zkontroluj shodu. Zálohu nepřesouvej ani nemaž.
- Pokud bezpečnost obnovy nelze ověřit, nic dalšího nemaž. Oznam přesný stav cíle, umístění platné zálohy a bod pokračování. Při prvním vydání bez zálohy výslovně uveď, že není co obnovit.
- Staging, pracovní výstupy a vlastní zámek uklízej až po dokončení nebo bezpečně doložené obnově. Při přerušení uchovej vše potřebné k diagnostice a pokračování. Neukončuj cizí procesy.

## 7. Volitelný překlad všech manuálů do angličtiny

**Po vytvoření zdrojové distribuce projdi jednotlivé manuály a po výslovném povolení uživatele přelož všechny české manuály a české části smíšených manuálů do angličtiny.** Již anglické části nepřekládej znovu. Rozsah zahrnuje všechny distribuované návody a specifikace, zejména `protocol.md`, `client.md`, `tray_UX.md`, všechny serverové návody, `readme.txt`, distribuční metodiky včetně tohoto dokumentu a další manuály v podadresářích. Neomezuj soupis pouze na příponu `.md` nebo na zde vyjmenované soubory.

1. Po ověření vytvořené distribuce sestav soupis manuálů a jejich jazyků. **Samotný požadavek vytvořit distribuci ani existence tohoto pravidla nejsou souhlasem s překladem.** Není-li již překlad pro dané vydání výslovně povolen, požádej uživatele o povolení tohoto konkrétního kroku. Do odpovědi nepřekládej. Při odmítnutí nebo chybějící odpovědi zůstává ověřená distribuce v původních jazycích a report uvádí, že překlad neproběhl. Již udělené výslovné povolení nevyžaduj znovu.
2. Pokud uživatel překlad povolí, použij **primárně obraty, terminologii a styl anglických překladů předchozí verze v `C:\gitsigma\phpvpn_old`**. Vyhledej odpovídající manuály podle cesty i obsahu, včetně přejmenovaných dokumentů. Zálohu pouze čti a neměň ji. Před převzetím textu proveď příslušné kontroly jmen, tajemství a lokálních vazeb z této metodiky, starší verze není automaticky bezpečný zdroj.
3. Autoritou pro obsah je aktuální manuál nové distribuce. Dřívější anglické znění používej pro významově shodné pasáže a jako stylistickou předlohu. Změněné a nové části přelož podle aktuálního českého textu. Nepřenášej zastaralé požadavky, staré cesty ani odstraněné kapitoly. Pokud předchozí anglický překlad nebo záloha neexistuje, vytvoř po povolení úplný překlad aktuálního manuálu a drž jednotnou terminologii napříč všemi manuály.
4. Překládej úplný obsah, nikoli shrnutí. Zachovej normativní význam, strukturu, tabulky, identifikátory, názvy souborů, příkazy, kódové ukázky, konfigurační hodnoty a URL. Případné odkazy na kotvy aktualizuj podle přeložených nadpisů a ověř jejich funkčnost. Licenční texty a povinná právní oznámení svévolně nepřekládej ani neměň. U pasáže v jiném jazyce než češtině či angličtině vyjasni rozsah s uživatelem.
5. Překlady připrav v samostatné pracovní kopii právě vytvořené distribuce a nahraď jimi odpovídající manuály při zachování jejich cest a názvů. Zdrojový checkout a `phpvpn_old` zůstávají beze změny. Toto povolení platí pro manuály nové zdrojové distribuce, neopravňuje k aktualizaci uchovaného překladu `documentation/protocol.en.md` ve zdrojovém checkoutu ani k jiným změnám mimo distribuční kopii.
6. Porovnej každý přeložený manuál s úplným aktuálním originálem a zkontroluj, že nechybí žádná kapitola, požadavek, tabulka ani upozornění. Ověř jednotnou terminologii a že nezůstala nepřeložená česká próza. Prověř odkazy a pravidla skriptů závislá na obsahu dokumentace, zejména kontroly README. Neoslabuj tyto kontroly, nevyřešená závislost blokuje dokončení překladu.
7. Aktualizuj manifest a SHA-256 změněných souborů a znovu proveď kontroly finálního obsahu podle oddílu 5. V reportu zaznamenej povolení, seznam přeložených a již anglických manuálů, použité předlohy ze zálohy a výsledek ověření. Nepřebírej staré hashe ani tvrzení o ověření z předchozí verze.
8. Ověřenou přeloženou pracovní kopii použij jako nový staging. Za výhradního zámku ověř nezměněnost cíle, uchovej novou nepřeloženou distribuci zvlášť pro případ obnovy a publikuj přeložený obsah včetně manifestu. **Nespouštěj znovu rotaci `phpvpn_old`**, protože by zničila zálohu předchozího vydání. Znovu ověř přesnou shodu cíle s přeloženým stagingem. Při chybě zachovej nebo bezpečně obnov právě vytvořenou nepřeloženou distribuci a překlad označ za nedokončený.

## 8. Požadavky na případnou automatizaci

Tento dokument sám žádný nový skript neimplementuje. Pokud další agent postup automatizuje, musí skript a jeho validátor patřit do distribuovaných zdrojů a dodržovat všechna pravidla výše. Režim přípravy nebo `dry-run` nesmí mazat ani měnit cíl a zálohu. Přípravná fáze musí sestavit inventář a vypsat konkrétní plán změn, publikační fáze smí přijmout pouze znovu ověřený staging.

Skript musí být přenositelný, používat parametry pro umístění zdroje a pracovní data a sám nevyžadovat zde uvedené lokální cesty ke kompilaci projektu. Výchozí publikační destinace podle této metodiky smí mít pouze publikační nástroj. Výsledek nesmí záviset na pořadí výpisu adresářů. Řaď manifest stabilně a neupravuj bajty zdrojových souborů při kopírování.

Ověř alespoň následující poruchové scénáře v oddělených testovacích adresářích, nikdy na skutečném distribučním cíli:

- Chybějící cíl při prvním vydání a běžná rotace existujícího cíle i zálohy.
- Zakázané jméno v textu, cestě i metadatech, různé velikosti písmen a Unicode zápisy. Testovací řetězce vytvářej z kódových bodů za běhu, ne jako doslovné zakázané hodnoty ve zdrojích.
- Podezřelá lokální závislost, nečitelný vstup, chybějící povinný dokument a přejmenovaná binárka bez typické přípony.
- Certifikát bez soukromého klíče, certifikát vložený do zdroje a citlivý klíč nebo provozní token musí být odmítnuty. Doložené výchozí heslo či token phpvpn musí projít, jiná hodnota ve stejném poli nebo souboru nesmí zdědit tuto výjimku. Citlivé testovací vstupy generuj pouze v izolovaném pracovním adresáři.
- Reparse point, nebezpečný cíl, kolize názvů, souběžné spuštění a změna vstupu při kopírování.
- Selhání zálohy, přerušení po vyprázdnění cíle, neúplné naplnění cíle, obnova a opakované spuštění po havárii bez ztráty poslední platné zálohy.

## 9. Podmínky dokončení a předání

Distribuce je dokončená pouze tehdy, když:

- Obsahuje aktuální úplné zdroje a skripty potřebné k sestavení a vytvoření distribuce včetně ověřeného získání nepřibalených závislostí.
- Obsahuje všechny povinné návody, správný `readme.txt`, kořenovou MIT licenci a potřebná oznámení třetích stran.
- Neobsahuje zkompilované ani nativní spustitelné soubory, žádné certifikáty, citlivé klíče, nepovolená hesla či tokeny, soukromá data, zakázaná jména nebo nevyřešené vazby na původní počítač. Případná přibalená výchozí hesla a tokeny phpvpn mají doloženou výjimku podle oddílu 3.
- Prošla kontrola přenositelnosti a relevantní testy, případná výslovně schválená omezení ověření jsou přesně uvedena.
- Cíl přesně odpovídá ověřenému stagingu a manifestu. Záloha odpovídá celému předchozímu obsahu cíle, pokud existoval.
- Je zaznamenán stav volitelného překladu. Pokud jej uživatel povolil, všechny české manuály a české části manuálů jsou přeloženy do angličtiny podle oddílu 7 a finální manifest odpovídá přeložené distribuci. Bez povolení zůstaly manuály v původních jazycích.

Uživateli předej cestu k výsledku a záloze, verzi a identifikaci zdrojového stavu, počet souborů, hash manifestu, stručné výsledky ověření a odkaz na externí report. Uveď neprovedené kontroly a otevřené překážky. Nikdy neoznač částečný výsledek za hotovou distribuci.

Vytvoření ZIP, inicializace Gitu, commit, push nebo zveřejnění nejsou součástí této operace, pokud je uživatel samostatně nepožaduje. Jedinou výjimkou je dočasný testovací repozitář v externí ověřovací kopii podle oddílů 2 a 5. Případný později vytvářený archiv musí po bezpečném rozbalení projít stejnými kontrolami a zachovat potřebné režimy skriptů.
