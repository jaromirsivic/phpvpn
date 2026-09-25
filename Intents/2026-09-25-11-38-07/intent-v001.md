---
intent_version: 1
created_at: 2026-09-25-11-48-46
source: intent-chat-v001.md
---

# Pythonová implementace serveru PHP-VPN

## Záměr

Vytvořit samostatnou serverovou implementaci v Pythonu s názvem `phpvpn.py`. Stávající `phpvpn.php` zůstane zachovaný. Oba servery musí obsluhovat stávající klienty podle stejného komunikačního protokolu. Pythonový server má navíc diskové úložiště jako výchozí režim a volitelné ukládání provozních dat pouze do paměti.

## Výslovné požadavky uživatele

1. Nový soubor se jmenuje přesně `phpvpn.py`, bez pomlčky, a obsahuje Pythonovou implementaci serverového chování odpovídajícího `phpvpn.php` včetně HTTP serverového vstupu. FastAPI je uživatelem navržená, nikoli povinná možnost.
2. Klientská komunikace a chování serveru musí být kompatibilní se současným protokolem. Referencí jsou `protocol.md` a dosavadní implementace `phpvpn.php`. Rozsah zahrnuje relevantní akce, odpovědi, autentizaci, chybové stavy, limity a časová pravidla, nikoli jen základní úspěšnou cestu.
3. Existující PHP implementaci zachovat. Na jednom nasazení poběží buď PHP server, nebo Python server. Současný běh obou nad jedním úložištěm není požadován.
4. Pythonový server používá standardně diskový režim. Konfigurační položka `STORAGE_DRIVER` nahoře v souboru umožní zvolit `disk` nebo `memory`.
5. V režimu `memory` se data, která jsou v diskovém režimu provozně ukládána na disk, ukládají, vyzvedávají a mažou v paměti. Paměťový režim zahrnuje fronty a další související provozní stav nutný pro stejné chování protokolu. Nesmí kvůli tomuto stavu vytvářet diskové soubory. Obsah paměti po ukončení procesu nepřetrvá.
6. Paměťový režim musí běžet v jediném procesu s jediným workerem, aby všechny požadavky pracovaly nad týmž stavem. Současné požadavky v rámci tohoto procesu musí bezpečně sdílet a měnit paměťový stav.
7. Všechny administrátorem upravované konfigurační hodnoty mají být v souvislém bloku na začátku `phpvpn.py`, před aplikační logikou. Odpovídající hodnoty mají mít stejné názvy velkými písmeny jako v PHP, zejména `SERVER_SECRET`, `ACCESS_KEYS`, `STORAGE_DRIVER` a `BUFFER_DIRECTORY`. Hodnoty se stejným významem mají mít obdobnou podobu a výchozí nastavení jako v PHP, s rozšířením `STORAGE_DRIVER = 'memory'`.
8. Python si může zvolit vlastní vnitřní formát diskového i paměťového úložiště. Nemusí číst data uložená PHP serverem ani umožňovat přímou výměnu serverového souboru při zachování rozpracovaných diskových front. Rozhodující je spolehlivá kompatibilita vůči klientům.

## Projektový kontext

- Aktuální PHP soubor je `phpvpn.php` v kořeni repozitáře. Jeho horní konfigurační blok obsahuje `SERVER_SECRET`, `ACCESS_KEYS`, `STORAGE_DRIVER = 'disk'`, `BUFFER_DIRECTORY` a další konstanty. Současná validace PHP připouští jen diskový driver.
- Autoritativní popis klientského protokolu je `protocol.md`. `distribution.md` nyní definuje `dist/server/PHP/phpvpn.php` a připravuje složku `dist/server/Python` pro budoucí soubor.
- V repozitáři jsou integrační klientské testy v `tests-integration/`, které mohou sloužit jako základ pro ověření stejného chování obou implementací.

## Doporučení pro realizaci

- Umístit zdrojový `phpvpn.py` vedle `phpvpn.php` a při sestavení distribuce jej zahrnout do `dist/server/Python/`. Upravit metodiku distribuce tak, aby po přidání Python serveru už Python neslibovala jako prázdnou složku.
- Konfigurační blok odvodit přímo od aktuálního PHP souboru. Kde je hodnota v paměťovém režimu nepoužitelná, například kořen diskového úložiště, popsat její účinek bez změny názvu.
- Navrhnout diskové a paměťové úložiště za společným rozhraním, aby oba režimy dodržely stejnou sémantiku fronty, expirace, mazání, kapacit a souběhu.
- Spuštění Python serveru udělat zřetelné a dokumentovat závislosti i omezení jediného workeru. Případné externí knihovny pro Python instalovat a spravovat přes `uv`, podle pravidel projektu.
- Spustit relevantní protokolové a integrační testy proti Python serveru v obou režimech a porovnat výsledky s PHP. Přidat scénáře souběžných požadavků a restartu paměťového režimu.

## Kritéria přijetí

1. Původní `phpvpn.php` zůstává funkční a Python server lze spustit samostatně z `phpvpn.py`.
2. Stávající klient komunikuje s Python serverem v režimu `disk` i `memory` bez úpravy protokolu na straně klienta.
3. Výchozí nastavení Python serveru používá disk. Změnou hodnoty `STORAGE_DRIVER` nahoře v souboru lze přepnout na paměť.
4. Paměťový režim při provozu zapisuje, čte, expirovaná data odstraňuje a fronty vyprazdňuje bez diskových operací pro tato data, při jediném workeru a správném souběhu.
5. Konfigurační blok a názvy položek odpovídají PHP tam, kde mají položky stejný význam.
6. Distribuce obsahuje Python server ve své Python složce a dokumentace pravdivě popisuje jeho spuštění, závislosti a oba režimy úložiště.
7. Protokolové a integrační testy prokazují kompatibilitu obou úložných režimů s existujícími klienty; přímá kompatibilita diskových dat mezi PHP a Pythonem se netestuje, protože není požadována.
