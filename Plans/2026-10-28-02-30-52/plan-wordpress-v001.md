---
plan_version: 1
created_at: 2026-10-28-02-30-52
source: zadání uživatele v chatu (WordPress plugin do server/wordpress/version7, splnit pravidla WordPress.org, udržet rychlost)
---

# WordPress plugin „VPN Server“ (slug `vpn-server`)

## Rozhodnutí uživatele

- Název **VPN Server**, slug a text domain `vpn-server` (2026-10-28 volný v API WordPress.org).
- Licence **MIT** jako server, copyright www.pilandrome.com. WordPress.org přijímá licence kompatibilní s GPL.
- Rychlost: **brzký hook `plugins_loaded`** a **volitelný mu-plugin** „fast route“, který admin zapne tlačítkem.
- Kód protokolu se **generuje build skriptem z `phpvpn.php`**, jediný zdroj pravdy zůstává `phpvpn.php`.

## Zjištěná omezení

1. Klient zakazuje query ve `serverUrl` (`client/src/core/config/config_parser.cpp`) a sám připojuje `?action=…`.
   Endpoint musí být čistá cesta, např. `https://web/vpn-relay/`. REST API s plain permalinky (`?rest_route=`)
   nevyhovuje. **Plugin vyžaduje zapnuté pretty permalinky** (webserver musí posílat neznámé cesty na `index.php`).
2. WordPress.org vyžaduje ochranu `ABSPATH` v každém PHP souboru. Samostatný endpoint bez WordPressu proto
   nepřipadá v úvahu. Nejrychlejší povolená cesta je mu-plugin, který obslouží požadavek před načtením
   ostatních pluginů.
3. Common issues WordPress.org zakazují heredoc a nowdoc. `StatusPageHandler` používá nowdoc s inline
   JavaScriptem, takže plugin bude mít vlastní stavovou stránku.
4. `Runtime::init()` mění globální nastavení PHP (`ini_set`, `set_time_limit`, globální error a exception
   handler). V pluginu se nepoužije. Plugin má vlastní bootstrap, jehož handler platí jen po dobu obsluhy
   vlastního požadavku.
5. AES-GCM v čistém PHP se používá jen pro `health` a `info`, ne pro datovou cestu. Rychlost určuje hlavně
   bootstrap WordPressu, ne kryptografie.
6. Seznam chráněných značek WordPress.org obsahuje `plugin` a `wordpress`. Tato slova nesmí být v názvu.

## Struktura

```
server/wordpress/version7/
  README.md                     vývojářské poznámky (nedistribuuje se)
  vpn-server/                   distribuovatelný plugin = obsah ZIPu
    vpn-server.php              hlavička pluginu, bootstrap, routing
    readme.txt                  readme pro WordPress.org
    uninstall.php
    includes/
      relay-core.php            GENEROVÁNO z phpvpn.php, needitovat ručně
      class-settings.php        načtení a validace nastavení (option vpn_server_settings)
      class-relay.php           obsluha požadavku: config z options, writer, error handling
      class-status-page.php     stavová stránka bez heredoc, s escapováním
      class-admin.php           Settings → VPN Server
      class-fast-route.php      instalace a odinstalace mu-pluginu
    assets/                     admin CSS a JS (enqueue)
    languages/vpn-server.pot
  tests/                        testy pluginu (nedistribuují se)
tools/build-wordpress-plugin.py generátor relay-core.php, --check, --zip
```

## Kroky

### 1. Úprava `phpvpn.php` (jediný zdroj protokolu)

- Označit hranice jádra komentáři `// @wp-core-begin` a `// @wp-core-end`. Konfigurační hlavička, `Runtime`,
  `StatusPageHandler` a autorun zůstanou mimo jádro.
- `Config::fromValues(string $secret, array $hexKeys, string $bufferDirectory)` jako jediné místo validace.
  `fromConstants()` ji volá. Chování a chybové kódy se nemění.
- `Application::handle()` dostane volitelné `?Config` a příznak stavové stránky, aby nemusel číst konstanty.
- Řádky, které Plugin Check oprávněně hlásí (výstup binárního protokolu, `base64_*`, přímé souborové operace
  s `flock`), dostanou `phpcs:ignore` s odůvodněním. Je to jen komentář, standalone server se nemění.
- Ověření: `tools/test-python-server.py --profiles php-disk` a stávající PHP testy beze změny výsledků.

### 2. Build skript `tools/build-wordpress-plugin.py`

- Vyřízne jádro mezi značkami, přidá hlavičku s ochranou `ABSPATH`, protokolové limity (konstanty) a
  `namespace PhpVpn`. Výsledek uloží do `vpn-server/includes/relay-core.php` s poznámkou „generated“.
- `--check` ověří, že vygenerovaný soubor odpovídá `phpvpn.php` (pro CI a před distribucí).
- `--zip` vytvoří `vpn-server.zip` bez vývojových souborů.
- Verzi pluginu převezme z `SERVER_VERSION`. `tools/bump-version.ps1` bude aktualizovat i hlavičku a
  `Stable tag`.

### 3. Runtime pluginu

- Nastavení v jedné option `vpn_server_settings` s autoloadem, takže datový požadavek nedělá žádný
  SQL dotaz navíc. Obsah: secret, access klíče, slug endpointu, absolutní cesta k úložišti, stavová stránka.
- Úložiště je ve výchozím stavu `wp-content/uploads/vpn-server-<16 hex>/`. Ochranné soubory zapisuje jádro
  (`.htaccess`, `web.config`, `index.html`). Admin může zadat absolutní cestu mimo web root.
- Routing: v `plugins_loaded` s prioritou `PHP_INT_MIN` porovná cestu `REQUEST_URI` s
  `home_url(<slug>/)`. Při shodě obslouží požadavek a skončí přes `exit`. Šablona, `init`, rewrite ani
  přihlašování uživatele se nespustí.
- Během obsluhy platí lokální error handler (bez výpisu do těla), `ignore_user_abort(true)` a try/catch
  s odpovědí 500 `internal_error`. Po skončení se nic globálního nemění.
- GET na endpoint vrátí stavovou stránku (escapovanou, JS přes `wp_print_script_tag`) nebo 404, když je
  vypnutá. Metody a akce se vrací stejně jako ve standalone serveru.

### 4. Fast route (mu-plugin)

- Tlačítko v administraci (nonce, `manage_options`) zapíše `wp-content/mu-plugins/vpn-server-fast-route.php`.
- Loader má ochranu `ABSPATH`, ověří aktivní plugin a existenci souborů a při shodě cesty obslouží
  požadavek stejnou třídou ještě před načtením ostatních pluginů. Jinak nedělá nic.
- Smaže se při deaktivaci i odinstalaci. Když chybí složka pluginu, loader se tiše vypne.

### 5. Administrace (Settings → VPN Server)

- Endpoint: slug, výsledná Server URL pro klienty, kontroly (permalinky, HTTPS, 64bit PHP, zapisovatelné
  a chráněné úložiště ověřené loopback požadavkem, stav fast route).
- Server secret: pole a tlačítko Generate na serveru (bez JS).
- Klienti: tabulka VPN adres a access klíčů, přidat (další volná adresa a náhodný klíč), obnovit klíč,
  odebrat. Rozsah `10.99.99.2` až `10.99.99.254`.
- Export `config.json` pro klienta (`masterSetup`: serverSecret, serverUrl, vpnCidr, virtualIp, accessKey).
  Payload klíče patří jen klientům, export je neobsahuje.
- Snippet pro nginx (`location ^~ /wp-content/uploads/vpn-server-… { deny all; }`).
- Settings API, nonces, kontrola oprávnění, sanitizace vstupů a escapování výstupů. Řetězce jsou
  přeložitelné (`vpn-server`).

### 6. readme.txt a náležitosti WordPress.org

- Hlavička: Contributors (uživatelské jméno na wordpress.org, **doplní uživatel**), Tags (nejvýš 5),
  Requires at least 6.6, Tested up to 7.1, Requires PHP 8.2, Stable tag, License MIT a License URI.
- Sekce: Description, Installation (permalinky, nginx), FAQ (hosting a zákaz relay, bezpečnost, rychlost),
  Privacy (co se ukládá: fronty max. 60 s, lastSeen klientů), External services (žádné), Changelog,
  Upgrade Notice.
- `uninstall.php` smaže option, mu-plugin a úložiště pluginu (jen adresář s vlastní značkou).
- `languages/vpn-server.pot`. Překlady dodá translate.wordpress.org, české `.po` a `.mo` volitelně.
- Bannery, ikonu a screenshoty (SVN `assets/`) dodá uživatel. Nejsou součástí ZIPu.

### 7. Ověření

- Plugin Check (PCP) přes WP-CLI na lokálním WordPressu (`D:\nginx_wordpress`): žádné chyby, varování
  zdůvodněná.
- Sdílená integrační sada `tests-integration` proti endpointu `http://localhost:5080/vpn-relay/`
  s deployment description, kterou nastaví skript přes PHP CLI (secret a klíče do option).
- C++ contract testy (`work/build/release/tests/phpvpn-contract-tests.exe`) proti stejnému endpointu.
- Měření latence `data` pollu: standalone `phpvpn.php`, plugin s `plugins_loaded`, plugin s fast route.
- Nezávislá revize souladu s pravidly a bezpečnosti (Fable 5.1, effort high).

## Výsledek (2026-10-28)

- Plugin Check 2.1.0 včetně experimentálních a runtime kontrol: bez chyb a varování.
- `tests-integration` proti `http://127.0.0.1:5080/vpn-relay/`: 430 passed, 2 skipped, přes `plugins_loaded` i přes fast route.
  Standalone `phpvpn.php` po refaktoringu: 431 passed, 1 skipped. `tests.php`: 546 kontrol.
- Medián prázdného data pollu (nginx + php-cgi, 300 požadavků): standalone 15,4 ms, plugin 29,9 ms, plugin s fast route 29,6 ms.
- Nezávislá revize (Fable 5.1, effort high): opraveny ochranné soubory u zadané složky, multisite a fast route,
  brzké překlady na stavové stránce, osiřelá spravovaná složka, citlivé hlavičky na stavové stránce, `sslverify` self-testu.

## Rizika a otevřené body

- Hostingy často zakazují provozovat proxy nebo relay. readme na to upozorní.
- Generický název „VPN Server“ a fakt, že plugin je relay, ne VPN server v klasickém smyslu: review se může
  ptát. Popis to musí vysvětlit.
- Zápis do `mu-plugins` není zakázaný, ale review ho může rozporovat. Je volitelný a spouští ho jen admin.
- Přímé souborové operace místo `WP_Filesystem` (fronty s `flock` a atomickým `rename`) budou nejspíš
  vyžadovat zdůvodnění.
- Pretty permalinky jsou povinné. Bez nich endpoint nefunguje a administrace to musí jasně říct.
- Distribuce (`tools/make-distribution.ps1`) zatím ZIP pluginu nebalí. Rozhodne uživatel.
