# Python server PHP-VPN

`phpvpn.py` je samostatná implementace relay protokolu z `protocol.md`.
Stávající klient používá stejné URL, autentizaci a zprávy jako s `phpvpn.php`.
PHP soubor zůstává samostatně použitelný. Na jednom nasazení zvol jednu
implementaci a jednu instanci. Disková data PHP a Pythonu nejsou zaměnitelná.

## Instalace a spuštění

Ověřený runtime je Python 3.13. Závislosti instaluj pomocí `uv` ze zamčeného
`requirements-server.txt`. Server za běhu nic neinstaluje.

V nové serverové složce obsahující `phpvpn.py`, `requirements-server.txt`
a tento návod spusť:

```powershell
uv venv --python 3.13
uv pip sync requirements-server.txt
uv run --no-project python -B phpvpn.py
```

Příkazy spusť z této složky. `uv pip sync` synchronizuje celé zvolené prostředí,
proto pro server použij vlastní venv. Připravený server lze spustit také přímo
interpretem z tohoto prostředí, například `.venv/Scripts/python.exe -B phpvpn.py`
na Windows nebo `.venv/bin/python -B phpvpn.py` na Linuxu. Pro spuštění z jiné
pracovní složky použij absolutní cestu k interpretu i ke skriptu.

Výchozí adresa je `http://127.0.0.1:8000/phpvpn.php`. Přípona HTTP cesty je
záměrně `.php`, skutečný soubor se jmenuje **phpvpn.py**. Cestu nastavuje
`SERVER_PATH`, bind `LISTEN_HOST` a `LISTEN_PORT`. Ostatní cesty vracejí obecnou
404. Server neslouží statické soubory ze své složky.

Launcher předává instanci aplikace Uvicornu, nastavuje `workers=1`, vypíná reload,
WebSockety, důvěru proxy hlavičkám a access log. Používá `asyncio` a `h11`.
Proměnná `WEB_CONCURRENCY` toto explicitní nastavení nepřepíše.
Nepoužívej externí multiprocess launcher ani několik replik. Import souboru
nevytváří globální aplikaci a není podporován příkaz `uvicorn phpvpn:app`.
Pomocná vlákna pro storage patří témuž procesu. Na Windows může interpret ve
venv mít pomocný launcher a systémový `conhost`, nejde o další aplikační worker.

## Konfigurace

Všechna administrátorská nastavení jsou v souvislém bloku nahoře v `phpvpn.py`.
Odpovídající názvy a výchozí hodnoty jsou shodné s PHP, včetně čtrnácti
výchozích `ACCESS_KEYS` pro adresy `.2` až `.15`.

- Změň `SERVER_SECRET` a `ACCESS_KEYS`. Hodnota přístupového klíče je přesně
  64 malých hexadecimálních znaků. Secret je Python řetězec kódovaný UTF-8.
  Pouze přesně prázdný řetězec se normalizuje na `CHANGE_THIS_SERVER_SECRET`.
- `STORAGE_DRIVER = 'disk'` je výchozí, alternativou je `'memory'`.
- `BUFFER_DIRECTORY = './phpvpn_data'` je relativní vůči umístění skriptu,
  nikoli pracovní složce procesu. Použij vlastní adresář mimo webroot.
- `STATUS_PAGE_ENABLED` zapíná veřejnou stránku GET/HEAD. Vypnutí neomezuje API.
  Výchozí, prázdný, nulový a ASCII nulový secret zobrazí warning panel.
- `VPN_ADDRESS_PREFIX`, `VPN_HOST_MIN`, `VPN_HOST_MAX`, všechny `MAX_*`, TTL,
  autentizační okno, polling a sweep konstanty mají význam popsaný v protokolu.
- `PROTECT_BUFFER_DIRECTORY` vytváří best-effort diskové guards. Nenahrazují
  správné nastavení reverzní proxy. `INFO_TEMP_TTL_SECONDS` se týká pouze
  dočasných diskových publikací kontraktů.

V memory se `BUFFER_DIRECTORY`, `PROTECT_BUFFER_DIRECTORY` a
`INFO_TEMP_TTL_SECONDS` pro provozní storage nepoužívají. Import a validace
konfigurace nic nevytvářejí. Neplatná konfigurace API vrací obecnou
`503 server_misconfigured`, nesprávný driver se tiše nenahrazuje.

## Disk a paměť

Disk ukládá celé dávky v per-client adresářích přes atomické přechody
`receiving → data → sending`. Index `phpvpn-python-1` je interní Python formát.
Po úplném zápisu, flush a uzavření souboru se dávka publikuje přejmenováním.
Neprovádí se fsync, takže se neslibuje dodatečná odolnost proti výpadku napájení.
Po restartu přežijí živé `.data`, převzaté `.sending` se neobnovují. Opomenuté
soubory uklidí omezená údržba po expiraci. Jeden buffer nesmí sdílet více procesů.

Memory uchovává fronty, lastSeen, publikované kontrakty a stav údržby výhradně
v paměti. Nevytváří buffer, journal, checkpoint, temp soubory ani spool.
Po restartu celý tento stav zmizí. Parametr `-B` vypíná Python bytecode cache.
Instalace balíčků a uv cache nejsou provozní storage. Režim memory nezakazuje OS swap.

Oba drivery mají stejnou kapacitu počítanou z původních datových řádků včetně LF.
Přeplněná cílová skupina se celá odloží v úspěšné odpovědi, jiný cíl může být
přijat. Poll přebírá celou nalezenou živou frontu bez stránkování. Žádný zámek
se nedrží během síťového odesílání. Převzetí je nevratné i při odpojení klienta,
proto protokol poskytuje at-most-once, nikoli potvrzené doručení.

Limit fronty je na klienta, nikoli na celý proces. Pomalé odpovědi vlastní již
převzaté dávky, zatímco do front mohou přibývat další. RAM proto může přesáhnout
součet aktuálně zařazených dat. Dimenzuj ji podle počtu klientů, velikosti
provozu a souběžných pomalých spojení. Přenos je po dávkách, netvoří jednu kopii
celé odpovědi. Závěrečný MAC se při chybě čtení dávky neodešle.

## HTTPS a reverzní proxy

HTTPS ukončuj před lokálně naslouchajícím procesem. Proxy musí zachovat metodu,
query a přesné bajty těla. Nesmí přepisovat JSON, LF, dekomprimovat obsah na jiné
bajty ani odpovědi cachovat. Limit requestu nastav **nad** `MAX_REQUEST_BODY_BYTES`,
aby aplikační hranice vracela vlastní obecnou 413. Neomezuj odpověď na velikost
jednoho requestu, může obsahovat celou 16MiB frontu.

Příklad nginx lokace při výchozích hodnotách:

```nginx
location = /phpvpn.php {
    client_max_body_size 2m;
    proxy_pass http://127.0.0.1:8000;
    proxy_http_version 1.1;
    proxy_cache off;
    proxy_buffering off;
    proxy_request_buffering off;
    proxy_read_timeout 120s;
}
```

TLS certifikát a ostatní nastavení virtual hostu spravuje proxy. Zdrojový ani
datový adresář nesmí být jejím statickým webrootem. Autorizace nikdy nevychází
z TCP adresy, schématu, Host ani `Forwarded` či `X-Forwarded-*`.

## Ověření ze zdrojového repozitáře

Testovací balíčky přidej do vývojového prostředí přes uv:

```powershell
uv pip install pytest requests psutil
uv run --no-project python -B -m pytest tests-python -q
uv run --no-project python tools/test-python-server.py --profiles python-disk python-memory -- -q
uv run --no-project python tools/test-python-server.py --profiles php-disk python-disk python-memory --php D:/nginx/php8.5.9/php.exe --report-dir reports/python-server -- -q
```

Poslední příkaz používá lokální cestu k PHP, kterou přizpůsob prostředí. Každý
profil má vlastní port, kopii serveru, konfiguraci a úložiště mimo webroot.
Původní server ani sdílený `tests-integration/server.json` se nepřepisují.
Bez `-m "not slow"` běží i skutečná expirace. Memory přeskočí pouze čtyři
testy fyzického diskového layoutu. Společné frontové assertions jsou stejné.

`tests-python` pokrývá navíc přesné časy a limity, selhání storage, cancellation,
restart skutečného procesu, HTTPS proxy s privátní testovací CA a jediný worker
i při `WEB_CONCURRENCY=4`. Nemění systémové certifikáty ani provozní službu.
Volitelný `--contract-executable <phpvpn-contract-tests.exe>` v runneru spustí
skutečný C++/WinHTTP/CNG kontrakt nad stejnými izolovanými fixture. Přepínač
`--contract-only` přeskočí HTTP pytest sadu.

Volba `--measure-memory` vzorkuje RSS serverového procesu každých 10 ms a při
`--report-dir` uloží počáteční, vrcholovou a konečnou hodnotu do JSON. Pro srovnání
všech profilů použij stejné filtry testů, například `-- -q -m "not slow"`.
Latence vypisují společné performance testy. RSS je měření konkrétního běhu,
nikoli garantovaná mez spotřeby ani přesné zachycení každé krátké špičky.

Souběžné nečtené odpovědi a nové plnění stejných front lze změřit odděleně:

```powershell
uv run --no-project python -B tools/measure-python-queue-lifetime.py --output reports/python-server/queue-lifetime.json
```

Tento scénář pro disk i memory otevře tři odpovědi bez čtení těla, každou
s přibližně 9,4 MiB dat, a stejné fronty znovu naplní. Poté ověří všechny
původní i nově přijaté zprávy a jejich MAC. Měří pouze RSS serveru.

Protokol a jeho očekávání jsou společné. Interní diskové formáty a ztráta memory
stavu po restartu se testují zvlášť. Podrobné výsledky a neprovedené scénáře
patří do datovaného validačního reportu, nikoli do tvrzení o univerzálním výkonu.
