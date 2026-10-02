---
plan_version: 1
created_at: 2026-10-02-21-01-37
source: zadání uživatele v chatu (bez samostatného intentu)
---

# Plán: tlačítko „Show HTTP Headers“ a řádek s autorem na provozní stránce `phpvpn.php`

## 1. Cíl a rozsah

Provozní stránka `phpvpn.php` (kořenový GET, `protocol.md` §5.1) dostane:

1. Tlačítko **Show HTTP Headers**, které otevře modální okno s HTTP hlavičkami, jež dorazily do PHP skriptu. Hlavičky jsou ve formátu, který lze beze změny vložit do tray aplikace, Configuration Wizard → **HTTP headers**.
2. V modálu: nadpis, stručnou anglickou poznámku (hlavičky viděné skriptem nemusí být úplné, přesné hodnoty jsou v prohlížeči po stisku F12 na kartě Network), pole s hlavičkami jen pro čtení, tlačítka **Copy** a **OK** a křížek pro zavření.
3. Pod panelem `main` šedý řádek: `Developed by www.pilandrome.com (MIT License)`, odkaz vede na `https://www.pilandrome.com`.

Mimo rozsah: `phpvpn.py`, `phpvpn.js`, jejich testy (`tests-python/`, `tests-node/`) a tray aplikace. Python a Node.js server se sjednotí v navazujícím kroku. Neměnit cizí rozpracované soubory z `git status` (`client/tests/contract/...`, `distribution.md`, `tests-integration/README.md`, `tools/distribution-common.ps1`, `tools/test-python-server.py`, `tools/verify-distribution.ps1`, `phpvpn.js`, `tests-node/`, `server-node.md`, `Intents/`, `.intent-builder-active`). Adresář `dist/` se ručně nemění.

Hlavní podmínka: co nejmenší nárůst velikosti `phpvpn.php`. Cíl je nejvýš asi 1,5 KB (nyní 100 005 B).

## 2. Ověřený stav (větev `linux`, HEAD `1b848ab`)

| Oblast | Zdroj | Dopad |
|---|---|---|
| Šablona stránky | `phpvpn.php:2069–2082` `StatusPageHandler::handle`, nowdoc `STATUS_HTML`, jeden řádek HTML+CSS+JS | Úpravy patří do tohoto řádku. Nowdoc nic neinterpoluje, hlavičky se vloží přes `str_replace`. |
| Routing | `phpvpn.php:2590–2598` `Application::handle` volá `(new StatusPageHandler())->handle($writer, $insecure)` | Sem se přidá předání přijatých hlaviček. |
| Request | `phpvpn.php:2020–2044` `Request::fromGlobals` nenese hlavičky | Hlavičky se čtou statickou pomocnou metodou handleru. `Request` zůstává beze změny, jeho konstruktor používají testy. |
| Mockup | `assets/index.html` = přesně šablona (bez koncového LF) | Testy porovnávají šablonu bajt po bajtu s mockupem. Mockup se aktualizuje se stejnou šablonou a prázdným `<textarea>`. |
| Jednotkové testy | `tests.php:591–646` (Routing, warning, „Embedded status template…“) | Volání `handle($writer, $insecure)` se rozšíří o pole hlaviček. Přibude test filtrování a escapování. |
| HTTP regrese | `tools/test-status-variants.py:31–76` porovnává tělo z `php -S` přesně s mockupem | Tělo nyní obsahuje skutečné hlavičky testu. Před porovnáním se obsah `<textarea>` vyprázdní a obsah se ověří zvlášť. |
| Validátor tray | `client/src/core/protocol/http_headers.cpp:58–121` `normalizeHttpHeaders` | Přijímá řádky `Name: value` oddělené LF nebo CRLF. Odmítá prázdné řádky, duplicitní názvy a názvy `Content-Type`, `Content-Length`, `Connection`, `Host`, `Transfer-Encoding` („managed by the application or transport“). Tyto názvy proto stránka vynechá, aby šel výstup vložit beze změny. |
| Python/Node | `phpvpn.py:851`, `phpvpn.js:1154` mají vlastní kopii šablony, `tests-node/http.test.js:58` testuje jen podřetězec | Mockup se tím pro ně dočasně rozchází. Žádný test Pythonu ani Node nečte `assets/index.html`, takže změna jejich testy nerozbije. |
| Specifikace | `protocol.md:131–135` §5.1 | Doplnit popis tlačítka, modálu, formátu, filtru a řádku s autorem. Uvést, že Python a Node.js stránku zatím nemají. |

## 3. Technická rozhodnutí

### 3.1 Získání hlaviček (PHP)

Nová statická metoda `StatusPageHandler::receivedHeaders(): array`:

- Pokud existuje `getallheaders()` (Apache, FPM od PHP 7.3, vestavěný `php -S`) a vrátí pole, použije ho. Zachová původní zápis názvů z požadavku.
- Jinak (CGI, CLI) sestaví hlavičky z `$_SERVER['HTTP_*']`: `HTTP_ACCEPT_LANGUAGE` → `Accept-Language` (`strtr` `_`→mezera, `ucwords(strtolower())`, mezera→`-`).
- Neprovádí další normalizaci. Případné hlavičky přidané webovým serverem nebo proxy (`X-Forwarded-For` apod.) se zobrazí, proto poznámka v modálu odkazuje na F12.

### 3.2 Sestavení textu a vložení do šablony

`StatusPageHandler::handle(ResponseWriter $writer, bool $insecure, array $headers): void`. Třetí parametr je povinný, aby žádné volání omylem nevynechalo hlavičky:

- Každá dvojice `(string) $name . ': ' . (string) $value` je jeden řádek. Pořadí odpovídá zdroji. Řádky spojí `"\n"`, bez koncového LF. Validátor tray přijímá LF i CRLF. RichEdit ve Windows i GtkTextView na Linuxu vložení LF zvládnou.
- Vynechat názvy odpovídající `/^(host|connection|content-(type|length)|transfer-encoding)$/i`, které tray odmítá.
- Escapovat `htmlspecialchars(..., ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8')`. Escapování brání XSS přes hlavičky (`User-Agent`, `Referer`) a `ENT_SUBSTITUTE` nahradí neplatné UTF-8.
- Vložení: `str_replace('readonly></textarea>', 'readonly>' . $text . '</textarea>', $html)`, až po odstranění `aside`. Tím nemůže obsah hlaviček ovlivnit regex warningu. Řetězec `readonly></textarea>` je v šabloně jen jednou.
- `Content-Length` se dál počítá ze skutečného těla. HEAD má stejné hlavičky odpovědi bez těla (stávající `ResponseWriter`).

### 3.3 HTML/CSS/JS (minimalisticky)

- Modál přes nativní `<dialog>` + `showModal()`. Escape, fokusová past a backdrop jsou zdarma. `<form method=dialog>` zavírá křížkem i OK bez dalšího JS. Copy má `type=button`, okno nezavře.
- Tlačítko v panelu: `<p>View HTTP headers for the Configuration Wizard: <button id=s>Show HTTP Headers</button></p>` za řádkem pro generování klíče.
- Dialog (text):
  - nadpis `<h2>HTTP headers</h2>`, křížek `<button id=x aria-label=Close>&times;</button>` vpravo,
  - poznámka: „Headers seen by this script may be incomplete. For exact values, press F12 and copy the request headers from the Network tab.“,
  - `<textarea id=t aria-label="HTTP headers" readonly>` (monospace, bez zalamování, svislý resize),
  - zarovnané vpravo `Copy` a `OK`.
- JS: společná kopírovací funkce pro klíč i hlavičky (`C=e=>{e.select();navigator.clipboard?…:document.execCommand('copy')}`), čímž se původní kód kopírování klíče zkrátí. `s.onclick=()=>{d.showModal();t.select()}`: po otevření je text vybraný a stačí Ctrl+C.
- Řádek s autorem pod panelem: `<p id=f>Developed by <a href=https://www.pilandrome.com>www.pilandrome.com</a> (MIT License)</p>`, šedá barva `#777`, menší písmo, odkaz zdědí barvu.
- Rozvržení: `body` přejde na `flex-direction:column;align-items:center;justify-content:center`. `main` ztratí `margin:auto`, aby řádek s autorem byl přímo pod panelem a celek zůstal vystředěný. `width:48rem;max-width:100%` se zachová.
- `<noscript>`: „JavaScript must be enabled to generate and copy a key or to show HTTP headers.“
- ID nekolidují se stávajícími `g,c,k,r`. Nová ID: `s,d,x,t,y,f`.

### 3.4 Bezpečnost a soukromí

- Stránka zobrazuje pouze hlavičky aktuálního požadavku zpět témuž klientovi. Konfiguraci serveru neprozrazuje (§5.1). Odpověď už má `Cache-Control: no-store`, takže se `Cookie` ani `Authorization` neukládají do sdílené cache.
- Hlavičky se vkládají do HTML vždy, nejen po kliknutí. Klient je jen zobrazí. Jiný zdroj přes CORS tělo nepřečte.
- Proxy může přidat interní hlavičky (`X-Forwarded-*`). To se uvede v protokolu jako vědomý důsledek. Stránku lze vypnout `STATUS_PAGE_ENABLED = false`.

## 4. Kroky implementace

1. **`phpvpn.php`**
   - Upravit řádek šablony podle §3.3.
   - Doplnit `receivedHeaders()` a třetí parametr `handle()` s filtrem, escapováním a vložením podle §3.1–3.2.
   - `Application::handle`: `(new StatusPageHandler())->handle($writer, $insecure, StatusPageHandler::receivedHeaders())`.
2. **`assets/index.html`** přepsat přesnou novou šablonou (prázdný `<textarea>`, s `aside`, bez koncového LF, jako dosud).
3. **`tests.php`**
   - V testu „Embedded status template…“ volat `handle($writer, $insecure, [])` a ověřit přítomnost `Show HTTP Headers`, `<dialog`, odkazu `https://www.pilandrome.com` a `MIT License`.
   - Nový test „Status page received headers“: vstup `['Host'=>…, 'User-Agent'=>'A<b>&"\'', 'Cookie'=>'a=1', 'content-type'=>…, 'Connection'=>…, 'Content-Length'=>…, 'Transfer-Encoding'=>…, 'X-Test'=>'']` → obsah textarea přesně `User-Agent: A&lt;b&gt;&amp;&quot;&#039;\nCookie: a=1\nX-Test: ` a sedící `Content-Length`. Dále `receivedHeaders()` v CLI (bez `getallheaders`) s dočasným `$_SERVER['HTTP_X_PHPVPN_PROBE']` → klíč `X-Phpvpn-Probe`. Původní `$_SERVER` obnovit ve `finally`.
4. **`tools/test-status-variants.py`**: před porovnáním s mockupem nahradit obsah `<textarea id=t …>…</textarea>` prázdným. Zvlášť ověřit, že obsahuje `User-Agent: Python-urllib/` a neobsahuje `Host:`, `Content-Type:`, `Connection:`.
5. **`protocol.md` §5.1** doplnit odstavec podle §3, včetně poznámky, že `phpvpn.py` a `phpvpn.js` zatím servírují předchozí verzi stránky a sjednotí se později.
6. Ostatní dokumentace (`distribution.md`, `server-*.md`, `tray_UX.md`) se nemění, popis stránky v nich není na úrovni prvků.

## 5. Ověření

1. `D:/nginx/php8.5.9/php.exe -l phpvpn.php`.
2. `D:/nginx/php8.5.9/php.exe tests.php`: vše PASS.
3. `uv run --no-project python tools/test-status-variants.py`: PASS (reálný `php -S`, čtyři varianty včetně HEAD).
4. `uv run --no-project python tools/test-python-server.py --profiles php-disk --php D:/nginx/php8.5.9/php.exe -- -q -k status`: sdílené integrační testy stránky pro PHP (mj. „hides the configuration“).
5. Ruční vizuální kontrola v prohlížeči přes `php -S 127.0.0.1:<port>`: tlačítko, otevření modálu, obsah, Copy, OK, ×, Escape, řádek s autorem a odkaz, úzké okno (375 px).
6. Kontrola vložitelnosti: zkopírovaný text projde `normalizeHttpHeaders` (ruční porovnání s pravidly z §2, tj. žádný zakázaný název, duplicita ani prázdný řádek).
7. Změřit nárůst velikosti `phpvpn.php` (`wc -c`) a uvést ho v závěru.

## 6. Rizika

- `getallheaders()` za některými SAPI chybí. Fallback z `$_SERVER` nevidí `Content-Type`/`Content-Length` (bez prefixu `HTTP_`), ty se ale filtrují tak jako tak. Apache bez `CGIPassAuth` nepředá `Authorization`, což pokrývá poznámka o F12.
- `navigator.clipboard` vyžaduje secure context (HTTPS/localhost). Na čistém HTTP se použije fallback `execCommand('copy')` nad vybraným textem, stejně jako u klíče.
- Mockup `assets/index.html` se do sjednocení dočasně liší od stránek Pythonu a Node.js. Zachytí to navazující úloha.
