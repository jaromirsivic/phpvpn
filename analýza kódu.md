## Formální zadání: analýza lokálnosti a přenositelnosti projektu PHP VPN

### Cíl

Detailně prověřit, zda lze projekt PHP VPN přenášet mezi počítači bez závislosti na konkrétním vývojovém počítači, osobních údajích uživatele nebo externích webových portálech, jejichž budoucí vypnutí by mohlo ovlivnit sestavení, testování či provoz.

První fáze zahrnuje **pouze analýzu a zpracování nálezů**. Změny zdrojových souborů, přepisování cest ani vytváření konfigurace nejsou součástí této fáze.

### Rozsah analýzy

Prověřit všechny zdrojové soubory projektu, které patří k:

- serverové části,
- klientské části pro Windows,
- klientské části pro Linux,
- sestavení a kompilaci aplikace,
- testům a jejich podpůrným skriptům,
- konfiguraci a spouštění souvisejícího lokálního prostředí, včetně lokálního nginxu.

Zmapovat soubory zahrnuté do analýzy a uvést případné části, které nebylo možné prověřit. Závěr o přenositelnosti musí odpovídat skutečně prověřenému rozsahu.

### Požadované kontroly

**1. Pevné absolutní cesty**

Vyhledat všechny natvrdo zapsané absolutní cesty pro Windows i Linux. U každého nálezu určit:

- k čemu cesta slouží,
- zda váže projekt na konkrétní počítač nebo jeho adresářovou strukturu,
- zda ji lze nahradit relativní cestou,
- zda je absolutní cesta potřebná a lze ji načítat ze společného konfiguračního souboru.

Cílovým stavem jsou relativní cesty všude, kde to dává smysl. Potřebné absolutní cesty mají být soustředěné v konfiguraci, ze které je příslušné části projektu načítají.

**2. Vazby na konkrétní počítač**

Prověřit další nastavení a odkazy, které mohou bránit přenosu mezi počítači, například pevné uživatelské adresáře, názvy počítačů či předpoklady o místním umístění nástrojů.

U každého nálezu vysvětlit konkrétní dopad na sestavení, testy nebo provoz.

**3. Výskyty osobního jména**

Vyhledat výskyty následujících jmen bez ohledu na velikost písmen, včetně libovolných kombinací velkých a malých písmen:

- `Jaromír`
- `Jaromir`
- `Šivic`
- `Sivic`

Kontrola zahrnuje zdrojový kód, komentáře, řetězce, konfigurace, skripty a názvy souborů v analyzovaném rozsahu. Každý výskyt uvést jako samostatný nález s přesným umístěním.

**4. Závislosti na externích webových portálech**

Vyhledat odkazy a funkční vazby na externí webové portály či služby. Posoudit, zda jejich nedostupnost nebo vypnutí může ovlivnit:

- sestavení a kompilaci,
- provádění testů,
- provoz serveru,
- provoz klientů na Windows a Linuxu.

Odlišit pouhé informační odkazy od skutečných funkčních závislostí. U skutečných závislostí uvést jejich účel a možnost lokálního řešení.

### Společná globální konfigurace

Pro nálezy, které vyžadují konfigurovatelné hodnoty, posoudit možnost jejich vyčlenění do **jednoho společného globálního konfiguračního souboru**.

Tento soubor má být v případné následné realizaci společným zdrojem konfigurace jak pro sestavení a kompilaci aplikace, tak pro testy. Analýza má posoudit proveditelnost a případná omezení tohoto řešení. V první fázi se konfigurace pouze navrhne, nevytváří se ani nezapojuje.

Samotné přesunutí externí závislosti do konfigurace nepovažovat za splnění cíle lokálnosti. U takové závislosti samostatně posoudit možnost jejího odstranění nebo lokální náhrady.

### Požadovaný výstup

Zpracovat přehled prověřených částí projektu, závěr o lokálnosti a přenositelnosti a tabulku všech nalezených porušení požadavků.

| Oblast | Soubor | Řádek nebo umístění | Nalezená hodnota či vazba | Účel a dopad | Lze použít relativní cestu nebo lokální řešení? | Lze vyčlenit do společné konfigurace? | Doporučené řešení |
|---|---|---|---|---|---|---|---|
| Vyplní se podle výsledků analýzy | | | | | | | |

U nálezů v názvu souboru uvést, že jde o název souboru, nikoli o konkrétní řádek. U každého doporučení rozlišit ověřenou možnost od návrhu, jehož proveditelnost zatím není potvrzená.

Pokud se žádné porušení nenajde, uvést tuto skutečnost společně s rozsahem provedené kontroly a případnými omezeními. Nálezy zatím neopravovat.

### Postup a hranice první fáze

Po potvrzení zahájit analýzu podle pravidel projektu, včetně získání kontextu z grafu Graft před procházením zdrojového kódu. Respektovat existující rozpracované změny.

V této fázi neupravovat projekt, nespouštět kompilaci ani testy a nevytvářet nové agenty, chaty nebo worktree. Případná realizace doporučení bude předmětem samostatného následného zadání.

**Potvrzuješ zahájení pouze této analytické fáze?**

Potvrzení požaduji podle pravidel režimu Poslouchej v tebou dodaném AGENTS.md, která vyžadují nejprve vypsat formalizované zadání a realizaci zahájit až po následném výslovném potvrzení.
