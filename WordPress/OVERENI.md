# Ověření distribuce VPN Server 1.0.4

Ověřeno 2. a 3. listopadu 2026. Níže jsou skutečně dokončené kontroly finálního balíčku. Testovací procesy a databáze jsou uklizené. Odstranění pracovních souborů zablokovala automatická kontrola nástrojů, přesný stav je uveden v oddílu Úklid.

## Balíček a zdroje

- Soubor: `vpn-server-1.0.4.zip`, 84 907 bajtů, 15 souborů pod jediným kořenem `vpn-server/`.
- SHA-256: `73d3634be9feed337ee1d4252c190adf128c89068951a70415ff896a47ef695c`.
- Udržovaný zdroj: `server/wordpress/version7/vpn-server`, aktivní větev `android`.
- Verze hlavičky, konstanty pluginu, generovaného core a Stable tag jsou 1.0.4. Text domain a kořen balíčku jsou `vpn-server`.
- Autor Jaromír Šivic, účet `pilandrome`, licence MIT. Oprávnění ke kódu a tutorial.png bylo výslovně potvrzeno uživatelem. Úplný licenční text je uvnitř ZIPu.

Build používá explicitní seznam souborů. Audit ověřil CRC ZIPu, přesnou shodu každého souboru se zdrojem i manifestem, velikosti, SHA-256, bezpečné cesty, metadata, délku krátkého popisu a počet tagů. Balíček neobsahuje testovací heslo ani náhodné server secrety z testovacích instalací. Neobsahuje konfigurace, `.git`, vnořené ZIPy, testy, registrační podklady ani desktopové binárky. Důkaz: [package-audit.txt](checks/package-audit.txt).

## Výsledky

| Kontrola | Prostředí | Skutečný výsledek | Důkaz |
| --- | --- | --- | --- |
| Plugin Check 2.1.0 včetně experimentálních a runtime kontrol | WordPress 7.1.4, PHP 8.5.9 x64 | Exit 0, bez nálezů | [plugin-check.txt](checks/plugin-check.txt) |
| Celá sdílená protokolová sada, fast route zapnutá | WordPress 7.1.4, PHP 8.5.9 x64 | 430 passed, 2 skipped | [protocol-wp71-fast.txt](checks/protocol-wp71-fast.txt) |
| Celá sdílená protokolová sada, běžný hook | WordPress 6.6, PHP 8.2.32 x64, OPcache | 430 passed, 2 skipped | [protocol-wp66-php82.txt](checks/protocol-wp66-php82.txt) |
| Cílené regresní kontroly konfigurace a oprav | WordPress 7.1.4, PHP 8.5.9 | 38 kontrol prošlo | [regressions-wp71.txt](checks/regressions-wp71.txt) |
| Cílené regresní kontroly konfigurace a oprav | WordPress 6.6, PHP 8.2.32 | 38 kontrol prošlo | [regressions-wp66.txt](checks/regressions-wp66.txt) |
| Reálné HTTP kontroly oprávnění a exportu | WordPress 7.1.4, PHP 8.5.9 | 23 kontrol prošlo | [http-wp71.txt](checks/http-wp71.txt) |
| Reálné HTTP kontroly oprávnění a exportu | WordPress 6.6, PHP 8.2.32 | 23 kontrol prošlo | [http-wp66.txt](checks/http-wp66.txt) |
| Skutečná deaktivace, reaktivace a uninstall hook | WordPress 7.1.4, WP-CLI na PHP 8.2.32 | Všechny fáze prošly | [lifecycle-wp71.txt](checks/lifecycle-wp71.txt) |
| Skutečná deaktivace, reaktivace a uninstall hook | WordPress 6.6, PHP 8.2.32 | Všechny fáze prošly | [lifecycle-wp66.txt](checks/lifecycle-wp66.txt) |
| Skutečná instalace a aktivace finálního ZIPu | WordPress 7.1.4, WP-CLI na PHP 8.2.32 | Exit 0, plugin nainstalován a aktivován, s varováním kontroly verze jádra popsaným níže | [install-zip.txt](checks/install-zip.txt) |
| Překontrolování směrování a přístupu k úložišti | WordPress 6.6, PHP 8.2.32 | 51 passed | [minimum-routing-recheck.txt](checks/minimum-routing-recheck.txt) |
| Sdílené jádro a testy samostatného PHP serveru | PHP 8.2.32 | 552 assertions, ALL TESTS PASSED | [php-unit82.txt](checks/php-unit82.txt) |
| PHP syntaxe všech 8 PHP souborů pluginu | PHP 8.2.32 | Bez syntaktických chyb | [php82-lint.txt](checks/php82-lint.txt) |
| Syntaxe admin.js a status-page.js | Node.js v23.3.0, `node --check` | Oba příkazy exit 0 | Provedeno při ověření, bez samostatného logu |
| Regenerace a všechny tři balicí cesty | Python přes uv, PowerShell | Šablony zachovány, dvě sestavení submission ZIPu bajtově shodná | [build-regression.txt](checks/build-regression.txt) |
| Shoda nainstalovaných souborů s finálním ZIPem | Obě izolované instalace | Všech 15 souborů v každé instalaci shodných bajtově | [installed-files.txt](checks/installed-files.txt) |
| Skutečný prohlížeč | Chromium, lokální HTTP instalace | Nadpis zachován, patička odstraněna, Download/Open a dialog hlaviček funkční | [status-page.jpg](checks/status-page.jpg) |

Protokolová sada zkouší autentizaci, nezávislé ověření kryptografických odpovědí, validaci formátu, směrování, přenos a doručování zpráv, limity, expiraci, metadata klientů, chyby a časové okno. Dva přeskoky v dokončeném běhu mají konkrétní důvod: nebyl zvolen profil se známým výchozím SERVER_SECRET a testovací deployment při souběhu nevrátil 503. Výchozí deterministické klientské klíče jsou zvlášť pokryty regresními kontrolami první aktivace. Přeskoky nejsou vydávány za úspěšné testy.

Před pozastavením práce proběhl také běžný hook na WordPressu 7.1.4 / PHP 8.5.9: 430 passed, 2 skipped. Tento historický běh byl před následným odstraněním X-Powered-By. Finální PHP opravu ověřují nové kontroly HTTP a aktuální běh přes fast route. Výsledky historického běhu se nepočítají jako další unikátní testy. Zachovaný log je [protocol-wp71-normal-before-header-fix.txt](checks/protocol-wp71-normal-before-header-fix.txt).

## Rozsah cílených kontrol

Regresní test ověřuje náhodný server secret při první aktivaci, zapnutou stavovou stránku, všech čtrnáct dohodnutých výchozích klientských klíčů a zachování nastavení při reaktivaci. U self-testu úložiště pokrývá selhání HTTP spojení, stavy 200/301/401/429/500/503, zamítnutí 403, chybějící soubor 404, skutečný únik obsahu, úklid testovacích souborů a neověřitelnou cestu mimo uploads. Ověřuje také odmítnutí přepsání cizího fast-route souboru a zachování tohoto souboru při odinstalaci.

HTTP kontroly ověřují veřejnou stránku bez úniku fixture credentials, lokální assety, nečinný přímý vstup do PHP souborů a administraci. Export konfigurace je povolen oprávněnému správci s platným nonce a blokován bez nonce, s chybným nonce, odběrateli i anonymnímu návštěvníkovi. Kontrolují se také správný klíč klienta, attachment a zákaz ukládání exportu do cache.

Lifecycle test používá skutečné příkazy WordPressu. Deaktivace zachová konfiguraci a data a odstraní spravovaný loader. Reaktivace konfiguraci zachová. Uninstall hook odstraní options, transient a spravované úložiště, ale ponechá výslovně zvolenou vlastní složku. Příkaz má `--skip-delete`, takže jsou pro kontrolu ponechány soubory pluginu. Nejde o tvrzení, že test smazal zdrojový strom pluginu.

První lifecycle běh na PHP 8.5 byl zastaven testovacím harness kvůli deprecation hlášením **WP-CLI 2.12.0 v cli/Colors.php**, nikoli kvůli hlášení z pluginu. Nepovažuje se za úspěšný. Stejná sekvence pak prošla přes PHP 8.2.32. Původní výstup je v [lifecycle-wp71-php85-tool-warning.txt](checks/lifecycle-wp71-php85-tool-warning.txt). HTTP runtime pluginu a Plugin Check na PHP 8.5 byly ověřeny samostatně.

Instalace finálního ZIPu přes `wp plugin install ... --force --activate` uspěla. WordPress při ní zároveň vypsal varování, že v tomto pomocném PHP prostředí nedokázal navázat zabezpečené spojení s WordPress.org pro `wp_version_check()`. Instalace lokálního balíčku a aktivace dokončily s exit 0. Varování není skryté ani označené za čistý výsledek. Síťovou kontrolu aktualizací jádra nepovažujeme za ověřenou.

První dokončená minimální sada měla 429 passed, 2 skipped a 1 failed. Selhal požadavek na neexistující URL podobnou úložišti, který místo 404 dostal domovskou stránku WordPressu s HTTP 200. Obsah úložiště se nevracel. Diagnostika prokázala, že vestavěný PHP server vložil URI obsahující `.json` do `SCRIPT_NAME`, které WordPress 6.6 považoval za cestu k hlavnímu skriptu. Pomocný router nyní nastavuje standardní front controller `/index.php` a zachovává původní `REQUEST_URI`, stejně jako běžné rewrite nasazení. Kód pluginu ani očekávání testu se kvůli tomu neměnily. Následných 51 cílených kontrol prošlo. Pro opakovatelné lokální testování je správný router uložený v `WordPress/tests/router.php`. Finální úplný běh je uveden samostatně v tabulce, původní chybný běh se nevydává za úspěch.

Router zároveň předává skutečné soubory a adresáře s vlastním index.php vestavěnému serveru, aby správně fungovalo přihlášení a `/wp-admin/`. Po doplnění této obsluhy prošlo znovu 51 kontrol směrování i všech 23 HTTP kontrol včetně přihlášení a exportu. Tato změna pomocného testovacího routeru není součástí plugin ZIPu.

## Trvalost změny patičky a nadpis

WordPress má vlastní šablonu `includes/class-status-page.php`. Jedinou vizuální změnou oproti původnímu zdroji je odstranění autorské patičky. Title i H1 zůstávají přesně `VPN server is running` a funkční klientské odkazy zůstávají viditelné.

Generátor z `server/php/phpvpn.php` mění jen protokolové jádro a výchozí intervaly. Skutečný běh generátoru zachoval obě šablony bajtově. Původní Python balení i `Write-WordPressPluginZip` z `tools/distribution-common.ps1`, kterou volá hlavní distribuční skript, zabalily šablonu bez WordPress patičky. Totéž ověřil nový submission build. Samostatný PHP server si svůj autorský kredit ponechal. Celá multiplatformní klientská distribuce ani podepisování desktopových balíčků se kvůli této kontrole nespouštěly.

Zastaralé testy očekávaly text `PHPVPN server is running`. Očekávání byla opravena na současný uživatelem potvrzený text. Samostatný PHP test navíc porovnával celou stránku se zastaralým návrhem v `assets/index.html`. Nadále porovnává celý výstup přesně, nyní proti aktuálnímu testovacímu snapshotu v `server/php/tests/fixtures/status-page.html`. Kontroly varování, délky odpovědi, generátoru klíče, dialogu hlaviček a samostatné patičky zůstaly zachované.

## Opakování kontrol

Z kořene repozitáře lze opakovat balení a jeho regresi:

```powershell
uv run --no-project python tools/build-wordpress-plugin.py --check
uv run --no-project python WordPress/tests/build-regression.py
```

Plugin Check spusť na samostatné instalaci WordPressu s aktivním Plugin Check a kopií pluginu rozbalenou z ověřovaného ZIPu:

```text
wp plugin check vpn-server --include-experimental --require=/ABSOLUTE/TEST-SITE/wp-content/plugins/plugin-check/cli.php
```

Použité Plugin Check 2.1.0 zde nepřijalo přímou Windows cestu k ZIPu jako vstup. Proto byly kontrolovány skutečné soubory rozbalené z instalačního ZIPu. Nepoužívej tento neúspěšný pokus jako důkaz kontroly archivu.

Pro HTTP a lifecycle testy připrav izolovaný WordPress na loopbacku, vlastní testovací účty `review-admin` a `review-subscriber` a soukromý soubor s testovacím heslem. Konfigurace protokolové sady používá `baseUrl`, `serverSecret`, `accessKeys`, `bufferDirectory` a `deployedFile` podle aktuálního nastavení této testovací instalace. Nikdy nepoužívej produkční tajné údaje. Regresní a lifecycle test vyžadují výslovnou testovací option `vpn_review_disposable_fixture = 1`.

Při použití vestavěného PHP serveru použij dodaný router, aby `SCRIPT_NAME` odpovídalo běžnému rewrite nasazení. Ve finálním minimálním prostředí byla zapnutá OPcache. Není to další kryptografická závislost pluginu.

```text
php -S 127.0.0.1:PORT -t DISPOSABLE_WORDPRESS_DIRECTORY /ABSOLUTE/REPO/WordPress/tests/router.php
```

```text
wp eval-file /ABSOLUTE/REPO/WordPress/tests/regressions.php
uv run --no-project --with requests python WordPress/tests/http-smoke.py --url http://127.0.0.1:PORT --password-file PRIVATE_PASSWORD_FILE --fixture PRIVATE_SERVER_JSON
uv run --no-project --with pytest --with cryptography --with requests python -m pytest tests-integration -q --server-config PRIVATE_SERVER_JSON --large-body-url http://127.0.0.1:PORT/vpn-server/
uv run --no-project python WordPress/tests/lifecycle.py --php PHP_EXECUTABLE --wp-cli WP_CLI_PHAR --site DISPOSABLE_WORDPRESS_DIRECTORY
```

Lifecycle test je záměrně destruktivní vůči nastavení testovacího pluginu. Ostatní testy nemají běžet současně proti stejné instalaci, pokud mohou měnit její nastavení. Přesné příkazy konkrétních dokončených kontrol jsou uložené také v záhlaví logů.

## Omezení a body před podáním

- Automatické kontroly nenahrazují ruční review WordPress.org a nejsou bezpečnostní certifikací. Požadavky byly ověřeny z oficiálních zdrojů uvedených v REGISTRACE.md.
- Výchozí funkční Download/Open odkazy zůstávají podle výslovného rozhodnutí uživatele. Jejich účel a upravitelnost jsou vysvětlené recenzentovi. Výklad pravidla č. 10 může tým pluginů posoudit jinak. Autorská patička je odstraněná.
- Generický název a požadovaný slug podléhají posouzení a přidělení. API při přípravě pro `vpn-server` vracelo 404, což není rezervace názvu.
- Účet `pilandrome` byl potvrzen uživatelem, ale do účtu jsme se nepřihlašovali. Kontaktní e-mail a aktivní 2FA musí vlastník před podáním ověřit sám. Přihlašovací tajné údaje se do dokumentace ani balíčku neukládají.
- Testy běžely na Windows x64 přes lokální HTTP a vestavěný PHP server s jedním workerem. Nejde o certifikaci všech hostingů, víceprocesové produkční zátěže, TLS infrastruktury, Apache/IIS/nginx pravidel nebo celé multisite sítě. Reálný hosting musí mít správně chráněné úložiště, HTTPS a odpovídající kapacity.
- Starší WordPress 6.6 slouží pouze ke kontrole deklarovaného minima. Pro produkci používej aktuální udržované vydání.
- Výchozí access tokeny jsou záměrně předvídatelné. Držitel sdíleného server secretu může jednat za klienta, jehož výchozí token nebyl změněn. Readme a administrace vysvětlují možnost New Key.
- Veřejné klientské odkazy a projektový web při přímé HTTP kontrole vracely 200. V rámci této práce nebyly z těchto ZIPů instalovány desktopové klienty na jednotlivých platformách.

## Úklid

Oba vlastní testovací HTTP servery na 5097 a 5098, testovací procesy i dočasné prohlížečové karty jsou zastavené. Bylo odstraněno všech 24 testovacích tabulek ze dvou ověřených prefixů `wpvpnreview261102_` a `wpvpnreview66_261102_`, včetně testovacích účtů. Ostatní databázové prefixy nebyly dotčené. Důkazy: [cleanup-wp71.txt](checks/cleanup-wp71.txt), [cleanup-wp66.txt](checks/cleanup-wp66.txt).

Automatická kontrola nástrojů odmítla smazání ověřené dočasné složky i následný užší pokus o odstranění konkrétních testovacích konfiguračních souborů. Nástroj uvedl pouze `blocked by policy`, podrobnější důvod neposkytl. Zákaz nebyl obcházen jiným mechanismem. Zůstávají pouze tyto pracovní cesty k ručnímu úklidu:

- `D:/git/phpvpn/work/wp-org-review/`, 9 815 souborů při závěrečné inventuře. Obsahuje pomocné WordPressy, stažené PHP, diagnostické logy, testovací konfigurace a kopie wp-config s lokálními databázovými přihlašovacími údaji. Nesdílet ani nepublikovat.
- `D:/git/phpvpn/work/server.json`, ověřený dřívější export této konkrétní testovací instalace. Nesdílet ani nepublikovat.

Obě cesty jsou mimo distribuci a pod ignorovanou složkou `work/`. Před pokusem o úklid byla ověřena jejich příslušnost k testu, nepřítomnost běžících testovacích procesů a nepřítomnost reparse pointů. Není důvod mazat jiný obsah `work/`. Původní uživatelova instalace `D:/nginx_wordpress` nebyla odinstalována ani zastavena.

Rozpracované uživatelské změny v `.intent-builder-active` a `Intents/2026-11-02-01-52-33/` zůstaly zachované. Nebyl vytvořen worktree, další agent ani commit. Nic nebylo podáno nebo publikováno.
