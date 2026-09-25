# Validace Python serveru, 25. září 2026

## Výsledek a rozsah

Implementace je v `phpvpn.py` na větvi `codex/python-server`. PHP server,
protokol, produkční C++ zdroje, schválený intent a plán zůstaly beze změny.
Nevznikl commit ani push. Běžící nainstalovaná služba nebyla měněna.

Python používá jediný aplikační proces, Uvicorn, AES-GCM z cryptography,
výchozí diskový a volitelný paměťový backend. Společné HTTP testy používají
stejné protokolové assertions pro všechny tři profily. Samostatné Python
testy pokrývají interní chyby, přesné hranice, procesy a resource lifetime.

Sestavený strom je v `dist`. Python část a automatické kontroly Windows
balíčku prošly. **Celá distribuce nemá dokončenou release validaci**, protože
níže uvedené skutečné instalační a ruční provozní testy Windows nebyly provedeny.
Předchozí obsah distribuce je zachován mimo `dist` v
`work/dist-before-python-20260925-1250`.

## Prostředí

- Windows 11 x64, build 22631, Python 3.13.7, PHP 8.5.9 z `D:/nginx/php8.5.9/php.exe`.
- Runtime lock: cryptography 50.0.1, uvicorn 0.54.0, cffi 2.1.1,
  click 8.5.0, h11 0.16.0 a pycparser 3.0.
- Testy: pytest 9.1.1, requests 2.34.2, psutil 7.2.2.
- Python závislosti byly instalovány a zamčeny výhradně přes uv.
- Klient byl znovu sestaven ze současných zdrojů v presetu Release x64.
- Příkazy níže se spouštějí z kořene repozitáře. Logy jsou v ignorovaném
  pracovním adresáři `reports/python-server`, nikoli v distribuci.

## Provedené kontroly

| Kontrola | Výsledek |
|---|---|
| `php tests.php` | 537 assertions úspěšně, zahrnuje info extensions |
| `php tests-info-pretty-boundaries.php` | Všechny scénáře úspěšně |
| Samostatná Python sada | 150 passed |
| Plná společná sada PHP/disk, včetně slow expiry | 430 passed, 1 skipped |
| Plná společná sada Python/disk, včetně slow expiry | 430 passed, 1 skipped |
| Plná společná sada Python/memory, včetně slow expiry | 426 passed, 5 skipped |
| Produkční C++/WinHTTP/CNG kontrakt proti Python/disk | 5 testů, 0 chyb |
| Produkční C++/WinHTTP/CNG kontrakt proti Python/memory | 5 testů, 0 chyb |
| Release CTest klienta s izolovaným PHP fixture | 9 z 9 cílů úspěšně |
| Kontrola `ruff`, pravidla F a E9 | Úspěšná |
| Kontrola klientského balíčku `verify-package.ps1` | Úspěšná |
| Python distribuce v čistém prostředí ze svého locku | Disk i memory: health, info, send, poll, dešifrování |
| SHA-256 zdrojů a distribuovaných serverových souborů | Shodné všechny čtyři soubory |

Kontrola 503 při reálném souběhu se ve společné sadě přeskočila, protože
server v daném běhu žádnou 503 nevrátil. Přeskočení není úspěšný test 503.
Deterministické lokální testy zvlášť vynucují timeout zámku, nepoužitelné
úložiště a chyby publikace a ověřují jejich odpovědi.

Memory vynechává čtyři testy fyzického diskového layoutu. Stejné behaviorální
testy front, TTL, kapacity, statistik a kryptografie nad memory běží.
Nulové provozní zápisy se navíc ověřují zakázanými filesystem operacemi,
neexistencí bufferu a skutečným restartem izolovaného procesu.

## Důležité samostatné scénáře

Testy ověřují přesné časové okno ±60000/60001 ms, známý HKDF vektor,
oddělené derivace, prázdný, `0`, nulový a Unicode secret, původní data bajty,
přesnou pretty serializaci a ořezávání info. Společná sada doplňuje 37 testů
rozšířeného info včetně escapovaných duplicit, typů, hloubky a 251/252/253
kontraktů. Čtrnáct výchozích přístupových klíčů má shodné hodnoty s PHP.

Storage testy pokrývají atomické cílové skupiny, souběžnou kapacitu,
jediné převzetí, přesnou expiraci, jeden sweep a retry, lastSeen 30/31 s,
budoucí čas, neměnné snapshoty, bounded sweep a pořadí po částečné údržbě.
Standardní info čte jen metadata markeru. Selhání best-effort markeru nemění
úspěšný datový výsledek.

Fault injection zahrnuje mkdir, open, write, krátký i nulový write, flush,
rename, utime a read. Neúplné dávky nejsou viditelné, poškozená dávka nemá
platný závěrečný MAC a starý kontrakt zůstává při selhání replace zachován.
Samostatně je ověřena kolize s otevřeným souborem na Windows. Cancel čeká
na skutečné dokončení operace ve vlákně a uklidí převzaté dávky. Pomalý send
nedrží frontový zámek, dovoluje další publikaci a uvolňuje vydané dávky.

Procesové testy ověřují restart, ztrátu memory stavu, přežití živých diskových
dávek, neobnovení převzatých dávek a jediný worker i při `WEB_CONCURRENCY=4`.
HTTPS se testuje přes lokální reverzní proxy s privátní testovací CA bez změny
systémové důvěry. Zahrnuje falešné forwarded hlavičky a odpověď 48 záznamů,
větší než povolený jednotlivý request. C++ kontrakt běžel přes přímé HTTP,
nikoli přes tuto HTTPS fixture.

## Reprodukce

```powershell
uv run --no-project python -B -m pytest tests-python -q
uv run --no-project python tools/test-python-server.py --profiles php-disk python-disk python-memory --php D:/nginx/php8.5.9/php.exe --report-dir reports/python-server/final -- -q
uv run --no-project python tools/test-python-server.py --profiles python-disk python-memory --contract-only --contract-executable work/build/release/tests/phpvpn-contract-tests.exe
powershell -NoProfile -ExecutionPolicy Bypass -File client/tools/build-x64.ps1 -Preset release -Configure -Test
powershell -NoProfile -ExecutionPolicy Bypass -File client/tools/verify-package.ps1 -PackageDirectory dist/client/Windows/x64
uvx ruff check phpvpn.py tools/test-python-server.py tests-python tests-integration/test_info_validation.py --select F,E9
```

CTest kontrakt vyžaduje aktivní testovací server a `PHPVPN_CONTRACT_URL`.
První kompletní build měl osm úspěšných cílů a jeden chybějící server na
výchozím portu. Po spuštění izolovaného PHP fixture byl celý CTest zopakován
se správnou URL a prošlo všech devět cílů. Přímý Python kontrakt je samostatně
zelený pro oba drivery.

Runner používá nové dočasné kopie, porty a buffer mimo webroot. Náhrady
konstant ověřuje právě jednou, zastavuje jen vlastní proces a uklidí vlastní
fixture. Původní zdroje ani `tests-integration/server.json` nepřepisuje.

Čisté distribuční prostředí bylo vytvořeno mimo `dist` příkazy:

```powershell
uv venv --python 3.13 work/python-server-clean-env
uv pip sync --python work/python-server-clean-env/Scripts/python.exe dist/server/Python/requirements-server.txt
```

Izolované konfigurační kopie distribuovaného `phpvpn.py` pak běžely tímto
interpretem. Ověření používá nezávislý klient ze společné testovací sady.

## Měření

Stejné filtrování osmi společných testů pro všechny profily:

```powershell
uv run --no-project python tools/test-python-server.py --profiles php-disk python-disk python-memory --php D:/nginx/php8.5.9/php.exe --report-dir reports/python-server/performance-final --measure-memory -- -q -m "not slow" -k "performance or queue_capacity"
```

| Profil | Health medián | Info medián | Prázdný poll | Plný send | Plný drain | RSS začátek / vrchol / konec |
|---|---:|---:|---:|---:|---:|---:|
| PHP/disk | 12,8 ms | 2,7 ms | 14,9 ms | 27,1 ms | 18,0 ms | 19,9 / 23,8 / 21,8 MiB |
| Python/disk | 0,8 ms | 1,6 ms | 1,2 ms | 9,1 ms | 15,1 ms | 35,8 / 42,2 / 37,8 MiB |
| Python/memory | 0,9 ms | 1,0 ms | 0,8 ms | 7,2 ms | 2,2 ms | 35,6 / 55,7 / 36,8 MiB |

Plná zpráva obsahovala 16 záznamů, tělo 702054 bajtů. RSS serverového procesu
se vzorkovalo po 10 ms. Jde o konkrétní lokální měření, nikoli garantovaný
speedup nebo horní mez RAM. Nezachytí každou krátkou špičku. Zadržované odpovědi
mohou držet další data mimo právě zařazené fronty. Paměťové limity proto nelze
odvozovat pouze z limitu 16 MiB na klienta.

Další měření zadrželo tři HTTP odpovědi bez čtení těla a současně znovu
naplnilo stejné fronty. Každá generace obsahovala 29478624 datových bajtů,
tedy přibližně 28,1 MiB ve třech frontách. Všechny původní i nové záznamy
byly následně přijaty právě v očekávaném pořadí a jejich MAC byl ověřen.

```powershell
uv run --no-project python -B tools/measure-python-queue-lifetime.py --output reports/python-server/queue-lifetime.json
```

| Profil | RSS začátek | RSS při zadržení a doplnění | RSS vrchol | RSS po vyprázdnění |
|---|---:|---:|---:|---:|
| Python/disk | 36,6 MiB | 41,8 MiB | 45,5 MiB | 37,0 MiB |
| Python/memory | 36,5 MiB | 86,1 MiB | 86,1 MiB | 37,8 MiB |

Toto je měření konkrétního běhu s TCP backpressure, ne důkaz maximální
spotřeby pro libovolný počet spojení. Uvolnění vlastnictví jednotlivých
paměťových dávek navíc ověřuje lokální test pomocí slabých referencí.

## Úpravy testů a hranice ověření

Testovací HTTP klient nyní respektuje velikost písmen hlaviček přes
`CaseInsensitiveDict`. Původní převod na obyčejný dict chybně předpokládal
PHP kapitalizaci, přestože HTTP názvy jsou case-insensitive. Wire parser,
kryptografie ani očekávané výsledky nebyly kvůli Pythonu změkčeny.

Test názvu expirace sleduje jen nově publikované soubory. Původně mohl omylem
počítat starší expirované soubory, které omezená oportunní údržba ještě
neodstranila. PHP fixture také obsluhuje neznámé cesty obecným 404 routerem,
protože vestavěná vývojová PHP 404 stránka vypisuje požadovanou cestu.
Samotný `phpvpn.php` se nemění.

Windows balíček používá vývojový podpis `CN=phpvpn Development Test Signing`,
nejde o veřejně důvěryhodný vydavatelský podpis. Ověřeny byly podpis manifestu,
hashe a velikosti, shoda skriptů, BAT VerifyOnly, UAC manifesty a přesné ICO
resources všech pěti produkčních EXE. Wintun zůstal původní podepsanou knihovnou.

Neprovedeny zůstávají skutečné instalační, upgrade a odinstalační operace,
administrátorský service end-to-end a ruční provozní kontroly požadované
`client.md`, zejména ACL/Edit Config/Wizard, sleep/hibernate, Windows upgrade,
více uživatelů, souběh s dalšími VPN a DPI/GUI acceptance. Relace neběžela
s administrátorskými právy a na stroji běžela existující služba PHP-VPN.
Tyto položky nejsou vykázány jako úspěšné ani jako splněná release validace.
Python server se mimo Windows v tomto běhu netestoval. Migrace diskových dat
mezi PHP a Pythonem není podporována ani požadována schváleným zadáním.
