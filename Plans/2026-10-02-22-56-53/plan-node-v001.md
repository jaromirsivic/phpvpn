---
plan_version: 1
created_at: 2026-10-02-22-56-53
source: zadání uživatele v chatu, navazuje na ../2026-10-02-21-01-37/plan-v001.md
---

# Plán: Show HTTP Headers a řádek s autorem v `phpvpn.js`

## 1. Cíl

Node.js server bude mít stejnou stavovou stránku jako `phpvpn.php`: tlačítko **Show HTTP Headers** s modálním `<dialog>` a šedý řádek `Developed by www.pilandrome.com (MIT License)` pod panelem. Šablona je bajtově shodná s `assets/index.html`. Mimo rozsah: PHP, Python (samostatný plán `plan-python-v001.md`), distribuce. Společné kroky popisuje `plan-spolecne-v001.md`.

## 2. Ověřený stav

| Oblast | Zdroj | Dopad |
|---|---|---|
| Šablona | `phpvpn.js:1154` `const STATUS_HTML = '…'`, stará verze, exportovaná na ř. 1611 | Nahradit obsahem `assets/index.html`. Při převodu do JS literálu v jednoduchých uvozovkách escapovat `\` a `'`. Generovat skriptem, ne ručně. |
| Routing | `phpvpn.js:1188–1200` `Application.preflight(method, rawUrl, contentType)` | Přidat čtvrtý volitelný parametr `rawHeaders = []`. Stávající volání v testech zůstanou platná. |
| HTTP vstup | `phpvpn.js:1504` `handleRequest` | Předat `req.rawHeaders`. Obsahuje původní zápis názvů a pořadí. `req.headers` názvy převádí na malá písmena a některé duplicity zahazuje. |
| Kódování | Node dekóduje bajty hlaviček jako latin1 | Hodnotu převést `Buffer.from(v, 'latin1').toString('utf8')`. Výsledek je stejný jako u PHP a Pythonu (UTF-8, neplatné → U+FFFD). |
| Duplicity | `rawHeaders` je obsahuje samostatně | Sloučit podle názvu bez ohledu na velikost písmen. Ponechat zápis prvního výskytu, hodnoty spojit `, `, u `cookie` `; `. Stejné pravidlo jako v Python plánu. |
| Testy | `tests-node/http.test.js:39–67`, helper `h.request(port, { headers })` | Rozšířit test stavové stránky. Helper umí vlastní hlavičky, duplicity se pošlou polem. |

## 3. Implementace

1. Nahradit `STATUS_HTML` novou šablonou.
2. Vedle šablony přidat:
   - `const CLIENT_MANAGED_HEADERS = /^(host|connection|content-(type|length)|transfer-encoding)$/i;` (názvy, které tray validátor odmítá),
   - `function statusHeaders(rawHeaders)`: sloučení přes `Map` podle §2, filtr, řádky `Name: value` spojené `\n` bez koncového LF, HTML escapování `& < > " '` (`'` jako `&#39;`).
3. `preflight(method, rawUrl, contentType, rawHeaders = [])`: po případném odstranění `aside` provést `html.replace('readonly></textarea>', () => 'readonly>' + statusHeaders(rawHeaders) + '</textarea>')`. Náhrada funkcí brání interpretaci `$&` a podobných vzorů v hodnotách hlaviček.
4. `handleRequest`: `app.preflight(req.method, req.url || '/', req.headers['content-type'], req.rawHeaders)`.

`Content-Length` se počítá z těla (`emit`), HEAD má stejné hlavičky odpovědi bez těla.

## 4. Testy (`tests-node/http.test.js`)

- Shoda šablony: `STATUS_HTML === fs.readFileSync('assets/index.html', 'utf8').replace(/\r?\n$/, '')`.
- V testu stavové stránky: GET s hlavičkami `User-Agent: A<b>&"'`, `Cookie: ['a=1', 'b=2']`, `X-Dup: ['1', '2']`, `X-Test: ''` a `Content-Type`. Obsah textarea po odescapování musí přesně odpovídat `User-Agent: A<b>&"'\nCookie: a=1; b=2\nX-Dup: 1, 2\nX-Test: `, plus hlavičky, které přidá sám Node klient: `Host` a `Connection` se vynechají, žádné další se neočekávají. V surovém těle nesmí být `A<b>`. Dále ověřit tlačítko, `<dialog id=d>` a odkaz `https://www.pilandrome.com`.
- Stávající kontrola `head.headers['content-length'] === page.body.length` platí dál (stejné hlavičky požadavku).

Spuštění: `node --test "tests-node/*.test.js"`. Lokálně je k dispozici Node 23.3.0, deklarovaný runtime je 24 LTS (`server-node.md`).

## 5. Dokumentace

`server-node.md` ř. 19: doplnit, že stavová stránka obsahuje dialog s přijatými HTTP hlavičkami (odkaz na `protocol.md` §5.1).

## 6. Rizika

- Hlavičky přidané reverzní proxy (HTTPS terminace) se zobrazí. Pokrývá to poznámka o F12.
- `rawHeaders` u HTTP/1.1 obsahuje i `Host`, ten se filtruje.
