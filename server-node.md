# Node.js server PHP-VPN

`phpvpn.js` je samostatná implementace relay protokolu z `protocol.md` pro
Node.js. Stávající klient s ní komunikuje beze změny: stejné URL
`/phpvpn.php`, stejné akce `health`, `info` a `data`, stejná kryptografie,
limity i chyby. Jediným perzistentním úložištěm je lokální disk. Paměťový
driver, Memcached ani Redis v této verzi nejsou.

## Co se nasazuje

| Položka | Hodnota |
|---|---|
| Aplikační soubory | právě jeden: `phpvpn.js` |
| Závislosti | žádné. Jen vestavěné moduly Node.js (`node:http`, `node:crypto`, `node:fs`, `node:path`, `node:perf_hooks`). Žádné `npm install`, build ani assets. |
| Runtime | Node.js 24 LTS (nejnovější opravené vydání řady). Jiné major verze nejsou deklarované jako podporované. |
| Volitelně | konfigurační JSON soubor mimo skript, reverzní proxy pro HTTPS |
| Provozní data | adresář `BUFFER_DIRECTORY`, výchozí `./phpvpn_data` vedle skriptu |

Stavová HTML stránka je vložená přímo ve skriptu, složka `assets` se
nenasazuje. Tlačítko Show HTTP Headers na ní ukáže přijaté hlavičky pro
Configuration Wizard (`protocol.md` §5.1).

## Spuštění

```sh
node phpvpn.js
```

Server poslouchá na `http://127.0.0.1:8080/phpvpn.php`. Do stderr zapíše
jediný řádek `listening http://127.0.0.1:8080/phpvpn.php`. Ukončuje se
signálem SIGINT (Ctrl+C) nebo SIGTERM. Server přestane přijímat spojení,
dokončí rozpracované požadavky a nejpozději po 10 sekundách skončí.

Další režimy:

```sh
node phpvpn.js --config /etc/phpvpn/server.json
node phpvpn.js --check-config --config /etc/phpvpn/server.json
node phpvpn.js --offline-recovery --config /etc/phpvpn/server.json
```

`--check-config` konfiguraci jen ověří a skončí kódem 0, chyba vrací kód 2.
`--offline-recovery` popisuje kapitola Obnova po tvrdém pádu.

Skript nezakládá OS službu. Pro systemd, NSSM nebo jiný správce procesů spouštěj
přímo `node /cesta/phpvpn.js --config /cesta/server.json` pod vyhrazeným
neprivilegovaným účtem. Na jednom stroji může běžet více instancí nad různými
rooty. Více procesů nad stejným rootem je také podporováno, viz Souběh.

## Konfigurace

Konfigurace má dvě vrstvy. Výchozí hodnoty jsou v hlavičce `DEFAULT_CONFIG`
na začátku `phpvpn.js`. Soubor předaný přes `--config` je JSON objekt se
stejnými názvy klíčů a jeho hodnoty mají přednost. Doporučený postup je
ponechat skript beze změny a tajemství držet v konfiguračním souboru čitelném
jen pro provozní účet. Neznámý klíč, špatný typ nebo nečitelný JSON zastaví
start s kódem 2. Server nečte proměnné prostředí a tajemství nepřijímá jako
argument příkazové řádky.

Minimální produkční soubor:

```json
{
    "SERVER_SECRET": "sem vlož dlouhou náhodnou hodnotu",
    "ACCESS_KEYS": {
        "10.99.99.2": "64 malých hex znaků",
        "10.99.99.3": "64 malých hex znaků"
    },
    "BUFFER_DIRECTORY": "/var/lib/phpvpn",
    "LISTEN_HOST": "127.0.0.1",
    "LISTEN_PORT": 8080
}
```

Hodnota `ACCESS_KEYS` v souboru nahrazuje celou výchozí mapu. Výchozí mapa
obsahuje veřejně odvoditelné klíče `.2` až `.15` podle protokolu. Nejsou
tajné a pro provoz se musí nahradit.

| Klíč | Výchozí | Význam |
|---|---|---|
| `SERVER_SECRET` | `CHANGE_THIS_SERVER_SECRET` | Přesné UTF-8 bajty, bez trimování a bez hex dekódování. Jen prázdný řetězec se normalizuje na výchozí hodnotu. Předvídatelná hodnota zapne červené varování na stavové stránce, API ale funguje. |
| `ACCESS_KEYS` | `.2` až `.15` | VPN adresa → přesně 64 malých hex znaků. |
| `BUFFER_DIRECTORY` | `./phpvpn_data` | Relativní cesta se vyhodnocuje vůči adresáři souboru `phpvpn.js`, ne vůči pracovnímu adresáři. |
| `STATUS_PAGE_ENABLED` | `true` | `false` vrací pro GET/HEAD `404`, API zůstává funkční. |
| `VPN_ADDRESS_PREFIX`, `VPN_HOST_MIN`, `VPN_HOST_MAX` | `10.99.99.`, 2, 254 | Adresní prostor. Musí se shodovat u všech klientů. |
| `LISTEN_HOST`, `LISTEN_PORT` | `127.0.0.1`, 8080 | Bind. Přímý vzdálený HTTP provoz vyžaduje explicitní `0.0.0.0` nebo konkrétní adresu. |
| `SERVER_PATH` | `/phpvpn.php` | Veřejná cesta, kterou používá klient. Jiné cesty vracejí `404`. |
| `REQUEST_TIMEOUT_SECONDS` | 30 | Nejdelší doba příjmu celého požadavku. Chrání před pomalými uploady. |
| `SYNC_WRITES` | `false` | `true` přidá fsync před publikací dávky a kontraktů. |
| Limity protokolu | podle `protocol.md` §11.1 | `MAX_DATA_RECORDS_PER_REQUEST`, `MAX_AUTH_RECORD_BYTES`, `MAX_REQUEST_BODY_BYTES`, `MAX_INFO_REQUEST_BODY_BYTES`, `MAX_MASKED_COMMUNICATION_CONTRACTS`, `MAX_INFO_PLAINTEXT_BYTES`, `MAX_QUEUE_BYTES_PER_CLIENT`, `AUTH_TIMESTAMP_SKEW_SECONDS`, `QUEUE_TTL_SECONDS`, `LOCK_TIMEOUT_MS`, `MIN_DATA_POLL_INTERVAL_MS`, `MAX_DATA_POLL_INTERVAL_MS`, `INFO_TEMP_TTL_SECONDS`, `CLIENT_STATS_MIN_UPDATE_SECONDS`. Aktivní hodnoty zveřejňuje `info`. |
| Údržba | 5 s, 8, 256, 20 ms | `SWEEP_MIN_INTERVAL_SECONDS`, `SWEEP_MAX_DIRECTORIES`, `SWEEP_MAX_FILES`, `SWEEP_MAX_MILLISECONDS`: rozpočet oportunistického úklidu po odpovědi `data`. |

Porušený invariant limitů (například minimální poll interval větší než
maximální) server nezastaví. Stavová stránka dál funguje a API vrací
`503 server_misconfigured`, stejně jako PHP implementace.

## HTTPS a reverzní proxy

Server sám mluví jen HTTP. HTTPS ukončuje reverzní proxy. Aplikační chování je
přes HTTP i HTTPS stejné a server nečte `X-Forwarded-*` ani `Forwarded`.

Proxy musí:

- předat tělo i odpověď bajtově beze změny, bez gzip nebo jiné transformace;
- mít limit těla požadavku přísně vyšší než 983040 B, například 1 MiB;
- umět vrátit odpověď až 16 MiB plus dva závěrečné řádky bez stránkování;
- nepřesměrovávat a necacheovat;
- zveřejnit jen cestu `/phpvpn.php`, nikoli adresář skriptu, konfiguraci ani
  `BUFFER_DIRECTORY`.

Ukázka nginx:

```nginx
upstream phpvpn_node {
    server 127.0.0.1:8080;
    keepalive 16;
    # Shorter than the 5 s keep-alive of the Node server, so nginx never
    # reuses a connection the server is just closing.
    keepalive_timeout 4s;
}

server {
    listen 443 ssl;
    server_name vpn.example.org;
    ssl_certificate     /etc/ssl/vpn.crt;
    ssl_certificate_key /etc/ssl/vpn.key;
    client_max_body_size 1m;
    server_tokens off;

    location = /phpvpn.php {
        proxy_pass http://phpvpn_node;
        proxy_http_version 1.1;
        proxy_set_header Connection "";
        proxy_request_buffering on;
        proxy_buffering off;
        proxy_read_timeout 60s;
        gzip off;
    }
    location / { return 404; }
}
```

Bez `upstream` s `keepalive` otevírá nginx pro každý požadavek nové spojení.
Při častém pollingu to na Windows vyčerpá efemérní porty (`10048`) a
nginx začne vracet `502`.

Node server nemá žádný obecný handler statických souborů, takže storage přes
něj dostupné není. Přesto drž `BUFFER_DIRECTORY` mimo jakýkoli document root
webserveru.

## Úložiště

Formát je privátní pro Node server a **není kompatibilní** s `phpvpn.php` ani
`phpvpn.py`. Při přechodu z PHP použij nový prázdný root a přepni klientům URL.
Stará PHP fronta se nepřevádí ani nemigruje. Protože data ve frontě žijí
nejvýše `QUEUE_TTL_SECONDS` (60 s), přepnutí znamená nejvýše ztrátu právě
čekajících paketů.

Struktura:

```text
BUFFER_DIRECTORY/
  10_99_99_3/                     fronta cílové adresy 10.99.99.3
    <expirace>_<bajty>_<src>_<uuid>.data       publikovaná dávka
    <expirace>_<bajty>_<src>_<uuid>.receiving  rozepsaná, nikdy nepublikovaná
    <expirace>_<bajty>_<src>_<uuid>.sending    převzatá, už se nevydá
    info.json                     marker lastSeen a publikované kontrakty
    .info-<uuid>.tmp              krátkodobý soubor publikace kontraktů
    .lock/                        zámek fronty, existuje jen během zápisu nebo převzetí
```

Dávka začíná jedním řádkem privátní hlavičky
`{"format":"phpvpn-node-1","bytes":N,"records":[[messageId,tag],...]}` a za ní
jsou původní datové řádky. Při čtení se hlavička ověří proti řádkům.

Adresáře vznikají líně až z validované adresy. Root patří provoznímu účtu
serveru, bez zápisu pro ostatní (POSIX například `0700`). Podporovaný je
lokální filesystém s atomickým rename v rámci adresáře (NTFS, ext4, XFS, APFS).
Sdílené NFS/SMB a souběžný přístup PHP nebo Python serveru do stejného rootu
nejsou ověřeným režimem. Symlink nebo reparse point místo klientského adresáře
server odmítne s `503 queue_unavailable`.

Garance publikace pokrývá pád procesu: dávka je po pádu buď celá `.data`,
nebo neexistuje. Ochrana proti výpadku napájení závisí na filesystému a na
`SYNC_WRITES`.

## Souběh

Nad jedním rootem smí běžet více Node procesů. Každá cílová fronta má zámek
tvořený atomickým `mkdir` adresáře `.lock`. Zámek drží jen zápis dávky a
převzetí dávek k odeslání. Čeká se nejvýše `LOCK_TIMEOUT_MS` (200 ms), pak
server vrátí `503 queue_busy`. Jiná I/O chyba znamená `503 queue_unavailable`.
Plná fronta není chyba. Odpověď je `200` a položky jsou v `deferredMessageIds`.

Převzetí dávky je přejmenování `.data` na `.sending` pod zámkem fronty. Na
Windows totiž několik souběžných přejmenování stejného zdroje na stejný cíl
uspěje současně, takže samotný rename exkluzivitu nezaručí.

## Obnova po tvrdém pádu

Běžný restart po SIGINT/SIGTERM nepotřebuje žádný zásah. Zámky se uvolňují ve
`finally` a publikované nevydané dávky přežijí restart do svého TTL.

Tvrdý pád (kill -9, pád stroje) uprostřed zápisu nebo převzetí může zanechat
osiřelý adresář `.lock`. Server ho z bezpečnostních důvodů nikdy neodstraní
sám podle stáří ani PID, protože nemůže ověřit, že jiný proces nad stejným
rootem neběží. Postižená fronta pak vrací `503 queue_busy`. Ostatní fronty
fungují dál.

Postup obnovy:

1. Zastav **všechny** procesy, které používají daný `BUFFER_DIRECTORY`.
2. Ověř, že opravdu neběží (`ps`, `systemctl status`, Správce úloh).
3. Spusť `node phpvpn.js --offline-recovery --config <tentýž soubor>`.
   Odstraní jen adresáře `.lock` a zbytky `.receiving` a `.sending` v
   klientských adresářích. Publikované `.data` a `info.json` ponechá. Do stderr
   vypíše jen počty.
4. Server znovu spusť.

Převzatá dávka (`.sending`) se po pádu už nikdy nevydá. To je vědomé
at-most-once chování protokolu.

## Paměť a logy

Fronta je vždy na disku. Odpověď `data` ale před odesláním načte převzaté
dávky volajícího do paměti, aby je mohla ze storage odstranit ještě před
přenosem. Jeden souběžný `data` požadavek tak může dočasně držet až
`MAX_QUEUE_BYTES_PER_CLIENT` (16 MiB) plus tělo požadavku (983040 B).
Rozpočítej RAM jako přibližně 17 MiB × počet současně vydávaných plných front
plus běžnou režii Node.js. Zápis odpovědi respektuje backpressure a při
odpojení klienta se data uvolní.

Naměřeno (Windows 11, Node.js v23.3.0, fronty po 16 267 040 B):

| Souběžně vydávané plné fronty | RSS před | RSS špička | Doba vydání |
|---:|---:|---:|---:|
| 1 | 74.8 MiB | 90.9 MiB | 0.36 s |
| 4 | 100.0 MiB | 154.2 MiB | 1.34 s |

Log jde do stderr a obsahuje jen čas, kód události a případně kód systémové
chyby, například `queue_unavailable EACCES`. Nikdy neobsahuje tajemství, tagy,
ciphertext, požadavky ani interní cesty. Chybové odpovědi mají jen obecný kód.

## Testy

Deterministické testy serveru, bez npm závislostí:

```sh
node --check phpvpn.js
node --test "tests-node/*.test.js"
```

Společná integrační sada proti izolované kopii serveru (Python prostředí přes uv):

```powershell
uv run --no-project --python 3.13 --with pytest --with requests --with cryptography --with psutil python tools/test-python-server.py --profiles node-disk --node "C:/Program Files/nodejs/node.exe" --report-dir reports/node-server -- -q
```

Skutečný C++ klient proti Node serveru (binárka sestavená s
`PHPVPN_BUILD_CONTRACT_TESTS=ON`):

```powershell
uv run --no-project --python 3.13 --with pytest --with requests --with cryptography python tools/test-python-server.py --profiles node-disk --contract-executable work/build/release/tests/phpvpn-contract-tests.exe --contract-only
```

Runner zkopíruje `phpvpn.js` do dočasné složky, předá mu vlastní JSON
konfiguraci s portem a rootem a po testech ukončí jen svůj proces.

## Známá omezení

- Osiřelý zámek po tvrdém pádu vyžaduje ruční offline obnovu.
- Diskový formát není kompatibilní s PHP ani Python serverem a nemigruje se.
- NFS/SMB a sdílení rootu s jinou implementací nejsou ověřené.
- HTTPS poskytuje jen reverzní proxy.
- Cílový runtime je Node.js 24 LTS, ale první ověření proběhlo jen na
  Node.js v23.3.0 (Windows 11). Linux a Node 24 zatím ověřené nejsou, viz
  `Reports/node-server/`.
