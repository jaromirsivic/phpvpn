---
plan_version: 1
created_at: 2026-10-02-22-56-53
source: zadání uživatele v chatu, navazuje na ../2026-10-02-21-01-37/plan-v001.md
---

# Společné kroky po úpravě Python a Node.js serveru

Provádějí se jednou, až po `plan-python-v001.md` a `plan-node-v001.md`.

## 1. `protocol.md` §5.1

Upravit odstavec o Show HTTP Headers:

- Odstranit větu, že část implementuje jen `phpvpn.php`.
- Zdroj hlaviček podle serveru:
  - PHP: `getallheaders()`, jinak `$_SERVER['HTTP_*']`,
  - Python: ASGI hlavičky s názvy v Title-Case,
  - Node.js: `req.rawHeaders` s původním zápisem.
- Opakované hlavičky jsou sloučené: hodnoty spojené `, `, u `Cookie` `; `. Hodnoty jsou dekódované jako UTF-8 s náhradou neplatných sekvencí.
- Všechny tři servery servírují stejnou šablonu `assets/index.html` a do textarea vkládají escapovaný text. Konkrétní tvar HTML entit se může lišit (`&#039;` × `&#x27;` × `&#39;`), dekódovaný text je stejný.

## 2. Sdílený integrační test

`tests-integration/test_status_and_routing.py`, nový test `test_status_page_shows_received_headers`:

- GET s hlavičkami `X-Phpvpn-Probe: a<b>&"` a `Content-Type: text/plain`,
- vyjmout obsah `<textarea …>…</textarea>` a po `html.unescape` ověřit řádek `X-Phpvpn-Probe: a<b>&"`. Název je zvolen tak, aby Title-Case v Pythonu dal stejný zápis.
- Žádný řádek nezačíná `Host:`, `Connection:`, `Content-Type:` ani `Content-Length:`. Názvy jsou unikátní bez ohledu na velikost písmen. V surovém těle není `a<b>`.

Proti nasazení za proxy, která hlavičku odstraní, by test selhal. Testované fixture jsou lokální, takže to nehrozí.

Spustit pro profily `php-disk`, `python-disk`, `python-memory`, `node-disk` (výběr `-k "status or routing"`):

```
uv run --no-project python tools/test-python-server.py --profiles php-disk python-disk python-memory node-disk --php D:/nginx/php8.5.9/php.exe -- -q -k "status or routing"
```

## 3. Ověření

- `node --test "tests-node/*.test.js"`,
- `pytest tests-python`,
- integrační běh výše,
- `php tests.php` (PHP se nemění, slouží jako kontrola, že se nerozbila sdílená šablona).
- Vizuální kontrola stránky Python i Node serveru v prohlížeči: otevření dialogu, obsah, řádek s autorem.

Distribuce se nevytváří, pokud o ni uživatel nepožádá.
