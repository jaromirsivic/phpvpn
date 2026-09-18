---
intent_version: 1
created_at: 2026-09-18-17-15-15
source: intent-chat-v001.md
---

# Research 32bitové kompatibility serverového skriptu `phpvpn.php`

## Shrnutí

Provést pouze technický research současného serverového skriptu `phpvpn.php` a zjistit, zda může korektně a bezpečně běžet v 32bitovém PHP i v 64bitovém PHP. Research musí určit všechny překážky, dopady a případné oblasti budoucích úprav. Nesmí vytvářet implementační plán ani měnit kód.

## Kontext projektu

- Předmětem je serverový skript `phpvpn.php` v projektu `C:\git\phpvpn`, nikoli obecná 32bitová podpora klientské aplikace.
- Současná validace konfigurace výslovně odmítá runtime s `PHP_INT_SIZE < 8` v `Config::fromConstants()` (`phpvpn.php:1193-1237`, konkrétně kontrola na řádku 1202). Research musí zjistit, zda jde pouze o preventivní omezení, nebo zda zakrývá skutečné 64bitové předpoklady.
- Skript používá celočíselné milisekundové timestampy odvozené z `microtime(true)`, časové výpočty, metadata front, limity velikostí, souborové časy a souborové offsety. Tyto oblasti jsou známými kandidáty k prověření, nikoli předem potvrzenými závadami.
- Posuzovat běžně podporované 32bitové a 64bitové PHP na Linuxu i Windows. Jasně oddělit vlastnosti PHP runtime, operačního systému, webového serveru, souborového systému a PHP rozšíření.

## Požadovaný výstup research

Výsledkem má být samostatná písemná research zpráva, která obsahuje:

1. Jednoznačný závěr, zda aktuální `phpvpn.php` může běžet na 32bitovém PHP, co mu případně brání a zda současně zůstane zachována 64bitová kompatibilita.
2. Evidenční tabulku všech relevantních nálezů. U každého nálezu uvést oblast, konkrétní soubor, symbol a řádky, současné chování, 32bitové riziko, dopad, závažnost, navržený způsob řešení a způsob budoucího ověření.
3. Úplný seznam oblastí, které by se musely upravit, pokud je 32bitová podpora skutečně zapotřebí. Pokud některá oblast změnu nepotřebuje, uvést ji jako prověřenou a vysvětlit proč.
4. Doporučení „podporovat / nepodporovat 32 bitů“, včetně podmínek, kompromisů a zbývajících nejistot.

## Rozsah analýzy kompatibility

Research musí systematicky a pokud možno vyčerpávajícím způsobem prověřit alespoň následující oblasti:

- Přímé architekturní zábrany, zejména `PHP_INT_SIZE`, typové deklarace `int`, návratové hodnoty vestavěných funkcí a chování při přetečení, převodu na `float` nebo vzniku `TypeError`.
- Čas a datum: epochové hodnoty v sekundách a milisekundách, `microtime(true)`, zaokrouhlování a převody, ISO-8601 parsování a formátování, autentizační časové okno, TTL front, expirace, sweep, `filemtime`, `touch`, násobení sekund tisícem a problém roku 2038.
- Aritmetiku limitů a délek: sčítání, odčítání, násobení, `abs`, `intdiv`, počty záznamů, velikosti požadavků a odpovědí, kapacitu front a všechny hraniční hodnoty konfigurace.
- Souborové operace a diskové úložiště: velikosti souborů, offsety a `ftell`, čtení a zápis front, názvy souborů obsahující číselná metadata, zamykání, atomické operace a omezení souborového systému nebo operačního systému.
- JSON a protokol: reprezentaci a parsování čísel, přesnost, serializaci limitů a timestampů, interoperabilitu s existujícím 64bitovým klientem a zachování wire protokolu.
- Kryptografii a binární operace: hash/HMAC, derivaci klíčů, generování náhodných hodnot, hex/binární převody a případné architekturně závislé chování použitých PHP rozšíření. Ověřit, že navržené řešení nesnižuje kryptografickou sílu ani nemění autentizační význam.
- Paměť, výkon a provozní limity: nižší adresní prostor, spotřebu paměti PHP hodnot a polí, garbage collection, I/O, práci s řetězci, frontami a velkými requesty, dostupnost a limity webového serveru.
- Závislosti a nasazení: podporované verze PHP, dostupnost 32bitových buildů na Linuxu a Windows, požadovaná PHP rozšíření, rozdíly balíčků a omezení webového serveru či hostingu.
- Testy, dokumentaci a konfiguraci, které by při budoucí realizaci musely být doplněny nebo změněny. Zahrnout jednotkové, integrační, hraniční, bezpečnostní a regresní scénáře pro 32 i 64 bitů.
- Přesah do klienta pouze tehdy, pokud 32bitová změna serveru ovlivňuje protokol, publikované limity nebo interoperabilitu. Neanalyzovat obecnou 32bitovou portaci klientské aplikace.

## Bezpečnostní analýza

Research musí zvlášť posoudit, zda 32bitové omezení nebo navržené obcházení může způsobit:

- přetečení, oříznutí nebo ztrátu přesnosti;
- chybné ověření timestampu nebo obejití autentizačního časového okna;
- předčasnou či opožděnou expiraci dat;
- obejití kapacitních limitů fronty nebo nekontrolovanou spotřebu paměti a disku;
- denial of service, nekonečné či neúměrně dlouhé zpracování;
- kolize nebo změnu kanonické reprezentace protokolových hodnot;
- oslabení kryptografie, náhodnosti, porovnávání nebo ochrany tajných hodnot;
- rozdílné bezpečnostní chování mezi 32bitovým a 64bitovým nasazením.

U každého rizika uvést realistický scénář, dosažitelnost, závažnost a možnou mitigaci. Nepovažovat náhradu celočíselných hodnot za `float` automaticky za bezpečné řešení.

## Výkonová analýza

- Neinstalovat 32bitové PHP, nevytvářet virtuální stroj či kontejner a nespouštět benchmarky.
- Připravit pouze kvalifikovaný odhad výkonových dopadů opřený o konkrétní cesty v kódu a známé rozdíly mezi 32bitovým a 64bitovým PHP.
- Oddělit dopad na CPU, paměť, kryptografické operace, JSON, souborové I/O, fronty a maximální obsloužitelnou zátěž.
- Rozlišit očekávané zpomalení samotného 32bitového runtime od režie případného kompatibilního řešení, například stringové nebo vícečástové reprezentace velkých čísel.
- Každý odhad označit mírou jistoty a uvést, co bez měření nelze spolehlivě kvantifikovat. Nevytvářet neodůvodněné procentuální údaje.

## Omezení

- Jde výhradně o research. Neprovádět změny zdrojového kódu, konfigurace, testů ani dokumentace.
- Nevytvářet implementační plán a nezahajovat implementaci.
- Neinstalovat žádný 32bitový runtime a nevyžadovat od uživatele instalaci pro tuto fázi.
- Externí tvrzení o PHP, operačních systémech a rozšířeních ověřit v primárních a aktuálních zdrojích a uvést odkazy.
- Jasně oddělit zjištěná fakta, závěry odvozené z kódu, kvalifikované odhady a otevřené nejistoty.

## Akceptační kritéria

- Zpráva výslovně vysvětlí současnou kontrolu `PHP_INT_SIZE < 8` a doloží, zda ji lze bezpečně odstranit, nahradit, nebo musí zůstat.
- Zpráva pokryje všechny výše uvedené oblasti a obsahuje dohledatelné odkazy na konkrétní místa v kódu.
- Je popsáno očekávané chování na 32bitovém i 64bitovém PHP a dopad případných řešení na obě architektury.
- Bezpečnostní dopady jsou vyhodnoceny samostatně a žádná navržená kompatibilita nesmí tiše snížit bezpečnost.
- Výkon je popsán pouze kvalifikovaným odhadem, bez instalací a benchmarků, s přiznanou nejistotou.
- Je uveden úplný inventář budoucích úprav, testů a dokumentace, ale žádná z těchto změn není v rámci research provedena.
- Závěr umožní uživateli rozhodnout, zda má smysl následně vytvořit plán podpory 32bitového PHP.
