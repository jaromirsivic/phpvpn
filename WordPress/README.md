# VPN Server pro WordPress.org

Tato složka obsahuje instalační balíček a podklady k prvnímu podání pluginu **VPN Server 1.0.4**. Autorem je **Jaromír Šivic**, samostatný vývojář, účet WordPress.org **pilandrome**. Licence je **MIT**.

Nic nebylo odesláno ani publikováno. Podání a publikace jsou samostatné následné kroky.

| Soubor | Účel |
| --- | --- |
| [vpn-server-1.0.4.zip](vpn-server-1.0.4.zip) | Instalační ZIP a soubor určený k podání |
| [REGISTRACE.md](REGISTRACE.md) | Potvrzené údaje, přihlášení, povinné 2FA, podání a následné SVN kroky |
| [REVIEWER-NOTES.txt](REVIEWER-NOTES.txt) | Anglické vysvětlení funkce, zabezpečení, úložiště, fast route a funkčních odkazů pro recenzenta |
| [OVERENI.md](OVERENI.md) | Skutečné výsledky kontrol, jejich rozsah a omezení |
| [SHA256SUMS.txt](SHA256SUMS.txt) | SHA-256 instalačního ZIPu |
| [manifest.json](manifest.json) | Přesný seznam souborů, jejich velikosti a SHA-256 |
| [checks/](checks/) | Protokoly dokončených kontrol a skutečný snímek stavové stránky |
| [build-release.py](build-release.py) | Opakovatelné sestavení s explicitním seznamem distribuovaných souborů |
| [tests/](tests/) | Cílené vývojářské kontroly, nepatří do instalačního ZIPu |

## Instalace a výchozí chování

Ve WordPressu otevři Pluginy → Instalace pluginů → Nahrát plugin a vyber ZIP. Plugin vyžaduje WordPress 6.6+, PHP 8.2+ na 64 bitech, hezké trvalé odkazy a zapisovatelné úložiště. Po aktivaci otevři Nastavení → VPN Server. Podrobný anglický návod a popis soukromí jsou v `vpn-server/readme.txt` uvnitř ZIPu.

Zachovaná rozhodnutí:

- Server secret se při první aktivaci generuje náhodně. Klienti `.2` až `.15` mají původní deterministické výchozí access tokeny. Pro oddělené ověřování klientů správce použije New Key.
- Stavová stránka s přesným nadpisem `VPN server is running` je zapnutá. Výchozí funkční Download/Open odkazy zůstávají viditelné a správce je může upravit nebo odstranit.
- Autorská patička je odstraněna pouze z WordPress varianty. Samostatný PHP server ji zachovává.
- Fast route je volitelná a standardně vypnutá. Její zapnutí přeskočí běžný bootstrap ostatních pluginů, což může ovlivnit jejich bezpečnostní a logovací kontroly.

Funkční klientské odkazy jsou vysvětlené v poznámkách pro recenzenta. Jejich výchozí viditelnost zůstává věcí výkladu pravidla č. 10 týmem WordPress.org. Upravitelnost URL sama není zárukou schválení. Tuto nejistotu uživatel výslovně přijal.

## Sestavení

Jediný udržovaný zdroj pluginu je [server/wordpress/version7/vpn-server](../server/wordpress/version7/vpn-server). Nekopíruj ruční opravy pouze do ZIPu.

Z kořene repozitáře spusť:

```powershell
uv run --no-project python tools/build-wordpress-plugin.py
uv run --no-project python tools/build-wordpress-plugin.py --check
uv run --no-project python WordPress/build-release.py
```

Při změně překládaných řetězců obnov i `languages/vpn-server.pot` přes WP-CLI i18n. Před podáním nové sestavení znovu ověř podle OVERENI.md.

Generátor přepisuje pouze `includes/server-core.php` a hodnoty výchozích intervalů. WordPress stavová stránka má vlastní udržovanou šablonu `includes/class-status-page.php`. Původní Python balení i PowerShell funkce používaná hlavní distribucí balí právě tuto šablonu. Test `tests/build-regression.py` provede skutečnou regeneraci a obě původní balení, ověří nepřítomnost WordPress patičky, zachování samostatné patičky a reprodukovatelnost submission ZIPu.

ZIP obsahuje jediný kořen `vpn-server/`. Registrační dokumenty, kontrolní logy, testy, desktopoví klienti, hesla a lokální konfigurace v něm nejsou.

## Co doplníš v účtu

Kontaktní e-mail účtu zůstává v registračních podkladech prázdný. Stav 2FA nebyl čten a před podáním ho musíš ověřit v účtu. Hesla, kódy 2FA ani SVN heslo se do podkladů nevyplňují. Grafický banner a vlastní ikona nejsou pro prvotní podání nutné, jejich požadované formáty jsou popsané v REGISTRACE.md.

Schválení závisí na ruční revizi WordPress.org. Úspěšné automatické kontroly ho nezaručují.
