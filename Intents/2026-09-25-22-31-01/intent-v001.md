---
intent_version: 1
created_at: 2026-09-25-22-34-56
source: intent-chat-v001.md
---

# Úprava dialogu HTTP headers a směrování Python serveru

## Shrnutí

Upravit dvě části projektu PHPVPN: zvětšit výchozí šířku dialogu HTTP headers v klientském Configuration Wizardu a zajistit, aby Python server obsluhoval svou výchozí stránku i protokolové akce nezávisle na cestě URL.

## Výslovné požadavky

1. Dialog otevřený tlačítkem **HTTP headers** v Configuration Wizardu má být výrazně širší než nyní, orientačně o 20 až 25 %. Má se blížit výchozí šířce wizardu a umožnit pohodlně číst výchozí hlavičky včetně User-Agent bez přetékání kvůli úzkému formuláři. Zmíněné odsazení přibližně 10 až 20 pixelů od krajů wizardu je ilustrace, nikoli přesná geometrická podmínka.
2. Server `phpvpn.py` má zobrazit stejnou výchozí stavovou stránku při otevření kořene serveru i při otevření libovolné jiné cesty, včetně dosavadní cesty `/phpvpn.php`.
3. Protokolové požadavky s parametrem `action` v query mají na těchto cestách fungovat stejně jako na dosavadní cestě. Příkladem je `?action=health`. Existující pravidla pro metody, typ obsahu, tělo požadavku a známé akce mají zůstat zachována.

## Ověřený kontext projektu

- Dialog HTTP headers vzniká v `client/apps/tray/http_headers_window.cpp`, kde má výchozí rozměr 760 × 420 logických jednotek a stejný rozměr předává umístění okna. Relevantní jsou řádky 35 až 71.
- Výchozí rozměr Configuration Wizardu se nastavuje na 1000 × 680 v `client/apps/tray/wizard_window.cpp` na řádku 130. Šířka 760 vůči 1000 odpovídá popsanému rozdílu.
- `Application.preflight` v `phpvpn.py` nyní odmítá cestu odlišnou od nastavené `SERVER_PATH` odpovědí 404 na řádcích 869 až 872. Následující řádky 873 až 892 rozlišují výchozí stránku a akce. `Application.__call__` předává cestu i query do této kontroly na řádcích 1078 až 1084.

## Akceptační kritéria

- Po otevření HTTP headers je dialog zřetelně širší a výchozí hlavičky, zejména User-Agent, jsou čitelné v editoru bez přetékání způsobeného dosavadní šířkou okna. Dialog zůstává použitelný při běžném DPI a dostupné pracovní ploše.
- `GET /`, `GET /phpvpn.php` a `GET` na jiné cestě vrátí stejnou výchozí stránku, pokud je stavová stránka povolena. Stejné zacházení s cestami platí i pro dosavadní podporu `HEAD`.
- Požadavky na podporované akce `health`, `info` a `data` se zpracují nezávisle na cestě URL. Nepodporované akce a nepovolené metody si zachovají stávající protokolové chování.

## Realizační vodítko

Změnit výchozí velikost dialogu v místě jeho vytvoření i umístění a ověřit, že jeho vnitřní editor a tlačítka reagují na větší klientskou plochu. Ve vrstvě Python serveru odstranit závislost směrování na přesné cestě, aniž by se měnila existující validace metod a akcí. Ověřit chování na kořeni, původní cestě a jiné cestě.
