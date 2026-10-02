---
plan_version: 1
created_at: 2026-10-02-22-56-53
source: zadání uživatele v chatu, navazuje na ../2026-10-02-21-01-37/plan-v001.md
---

# Plán: Show HTTP Headers a řádek s autorem v `phpvpn.py`

## 1. Cíl

Python server bude mít stejnou stavovou stránku jako `phpvpn.php`: tlačítko **Show HTTP Headers** s modálním `<dialog>` a šedý řádek `Developed by www.pilandrome.com (MIT License)` pod panelem. Šablona je bajtově shodná s `assets/index.html`, který je od minulé úpravy referenční pro PHP. Mimo rozsah: PHP, Node.js (samostatný plán `plan-node-v001.md`), distribuce. Společné kroky (protokol a sdílený integrační test) popisuje `plan-spolecne-v001.md`.

## 2. Ověřený stav

| Oblast | Zdroj | Dopad |
|---|---|---|
| Šablona | `phpvpn.py:851` `STATUS_HTML = b'''…'''`, stará verze | Nahradit obsahem `assets/index.html` bez koncového LF. Šablona neobsahuje `\` ani `'''`, bytes literál ji pojme beze změny. |
| Routing | `phpvpn.py:869–882` `Application.preflight(method, action, content_type, path)` | Stavová stránka vzniká zde. Přidat volitelný parametr `headers=()` se surovými ASGI hlavičkami. Výchozí hodnota zachová všechna stávající volání. |
| ASGI vstup | `phpvpn.py:1082–1083` `__call__` | Předat `scope['headers']` (seznam dvojic bytes) do `preflight`. |
| Názvy hlaviček | uvicorn (h11 i httptools) předává názvy malými písmeny | Původní velikost písmen není k dispozici. Názvy se převedou na Title-Case po segmentech oddělených `-` (`accept-language` → `Accept-Language`), stejně jako záložní cesta PHP (`ucwords`). HTTP názvy na velikosti písmen nezávisí. |
| Duplicity | ASGI vrací opakované hlavičky samostatně | Tray odmítá duplicitní názvy. Sloučit podle názvu v pořadí prvního výskytu, hodnoty spojit `, `, u `cookie` `; ` (RFC 6265 §5.4), jak to dělá webový server pro `getallheaders()`. |
| Kódování | ASGI hodnoty jsou bytes | Dekódovat `utf-8` s `errors='replace'`, stejně jako PHP s `ENT_SUBSTITUTE` (neplatné UTF-8 → U+FFFD). |
| Testy | `tests-python/conftest.py:29–45` `exchange(..., headers=dict)` | Jednotkové testy přes ASGI. Pro duplicity sestavit scope se seznamem dvojic přímo. |

## 3. Implementace

1. `STATUS_HTML` nahradit přesnou novou šablonou.
2. Přidat konstantu a funkci vedle `STATUS_HTML`:
   - `CLIENT_MANAGED_HEADERS = {'host', 'connection', 'content-type', 'content-length', 'transfer-encoding'}`, které tray validátor (`client/src/core/protocol/http_headers.cpp`) odmítá,
   - `status_headers(raw) -> bytes`: sloučení podle §2, filtr, řádky `Name: value` spojené `\n` bez koncového LF a `html.escape(..., quote=True)` zakódovaný do UTF-8.
3. `preflight(..., headers=())`: po případném odstranění `aside` provést `body.replace(b'readonly></textarea>', b'readonly>' + status_headers(headers) + b'</textarea>', 1)`. Pořadí je stejné jako v PHP, takže obsah hlaviček nemůže ovlivnit regex warningu.
4. `__call__`: `self.preflight(..., scope['path'], scope['headers'])`.
5. Přidat `import html` do bloku importů v abecedním pořadí.

`Content-Length` se v `emit` počítá z těla, takže HEAD dostane stejnou délku jako GET se stejnými hlavičkami. Odpověď už nese `Cache-Control: no-store`.

## 4. Testy (`tests-python/test_wire.py`)

- Shoda šablony: `s.STATUS_HTML == (ROOT / 'assets/index.html').read_bytes().rstrip(b'\r\n')`, přičemž prázdná textarea v šabloně odpovídá mockupu.
- Hlavičky přes ASGI: scope se seznamem `host`, `user-agent: A<b>&"`, `content-type`, `connection`, dvakrát `cookie`, dvakrát `x-dup`, `x-test` s prázdnou hodnotou a jeden neplatný UTF-8 bajt. Po `html.unescape` musí obsah textarea přesně odpovídat `User-Agent: A<b>&"\nCookie: a=1; b=2\nX-Dup: 1, 2\nX-Test: \nX-Bin: �`. V surovém těle nesmí být `A<b>`. `Content-Length` musí odpovídat délce těla.

Spuštění: `uv run --no-project --with-requirements requirements-server.txt --with pytest python -m pytest tests-python -q` (nebo prostředí, které repo používá pro `tests-python`).

## 5. Dokumentace

`server-python.md`: jedna věta u `STATUS_PAGE_ENABLED`, že stránka obsahuje dialog s přijatými HTTP hlavičkami (odkaz na `protocol.md` §5.1).

## 6. Rizika

- Proxy před uvicornem (`proxy_headers=False`) mohou přidat `X-Forwarded-*`. Zobrazí se, což pokrývá poznámka o F12 v dialogu.
- Title-Case se může lišit od zápisu v prohlížeči (`Sec-Ch-Ua` × `sec-ch-ua`). Pro HTTP i tray validátor je to ekvivalentní.
