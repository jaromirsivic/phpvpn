---
intent_version: 1
created_at: 2026-09-25-13-15-31
source: intent-chat-v001.md
---

# Spouštěcí skripty Python serveru v distribuci

## Cíl

Při vytváření distribuce PHP VPN připravit ve složce dist/server/Python šest provozních skriptů. Uživatel má být schopný na Windows, Linuxu a macOS připravit prostředí, spustit Python server na pozadí a bezpečně jej vypnout. Po úspěšném startu a zastavení uvidí výrazný ASCII banner alespoň tři sekundy. Po startu banner ukáže adresu, kterou má otevřít v prohlížeči.

## Projektový kontext

- Aktuální požadovaná struktura distribuce je v distribution.md. Složka dist/server/Python nyní obsahuje phpvpn.py, requirements-server.txt a server-python.md. Celkový postup sestavení serverové části není ve stávajícím client/tools/make-dist.ps1 implementovaný.
- server-python.md předepisuje Python 3.13, nástroj uv, vlastní virtuální prostředí a instalaci ze zamčeného requirements-server.txt. Distribuční balíček nemá obsahovat předem vytvořený venv ani uv cache.
- Python server spouští phpvpn.py přes Uvicorn s jediným workerem. Výchozí LISTEN_HOST je 127.0.0.1, LISTEN_PORT je 8000 a SERVER_PATH je /phpvpn.php. Výchozí lokální URL je http://127.0.0.1:8000/phpvpn.php. Přípona URL je .php, ačkoli skutečný zdrojový soubor je phpvpn.py.
- Oficiální instalátor uv od Astral používá na Windows instalační skript PowerShell a na Linuxu a macOS shellový skript. Odkaz na aktuální dokumentaci: https://docs.astral.sh/uv/getting-started/installation/.

## Výslovné požadavky a potvrzená upřesnění

1. Při tvorbě každé distribuce musí ve složce dist/server/Python vzniknout init.bat, start.bat, stop.bat, init.sh, start.sh a stop.sh spolu s již požadovanými serverovými soubory.
2. Skripty init.bat a init.sh připraví vše potřebné pro běh Python serveru. Pokud chybí uv, nainstalují jej oficiálním instalátorem Astral. Na Windows se použije PowerShellový instalační skript, na Linuxu a macOS odpovídající shellový skript. Připraví Python 3.13, lokální virtuální prostředí a závislosti podle distribuovaného requirements-server.txt.
3. Skripty start.bat a start.sh spustí Python server s připraveným virtuálním prostředím tak, aby se následně dal vypnout odpovídajícím skriptem stop. Server má po ukončení startovacího skriptu zůstat spuštěný.
4. Skripty stop.bat a stop.sh vypnou právě instanci serveru spuštěnou příslušným startovacím skriptem bezpečně a ověří, že skončila.
5. Po potvrzeném úspěšném startu se na konci výstupu startovacího skriptu vytiskne pěkně upravený ASCII banner s anglickým sdělením, že server byl úspěšně spuštěn. Na samostatném řádku uvede výzvu k otevření skutečně použitelné URL běžícího endpointu v prohlížeči. Banner zůstane viditelný nejméně tři sekundy před ukončením skriptu.
6. Po potvrzeném úspěšném zastavení se na konci výstupu zastavovacího skriptu vytiskne obdobný ASCII banner s anglickým sdělením, že server byl úspěšně zastaven. Zůstane viditelný nejméně tři sekundy. Výzva k otevření URL se po zastavení nevypisuje.

## Realizační vodítka odvozená z projektu a požadovaného chování

- Inicializace má fungovat opakovaně bez poškození již připraveného prostředí. Instalace uv, Pythonu a balíčků musí používat uv. Je nutné zachovat pravidlo, že venv a cache se nevkládají do vytvořené distribuce, ale vznikají až při inicializaci nasazené kopie.
- Start a stop mají fungovat při spuštění z libovolného pracovního adresáře. Mají používat cesty odvozené od umístění skriptu a interpreter z místního venv. Start nesmí tiše spustit druhou instanci stejného serveru. Stop nesmí ukončit cizí proces jen na základě zastaralého nebo znovu použitého PID.
- Zpráva o úspěchu se smí zobrazit až po ověření, že server opravdu přijímá požadavky, respektive že jeho proces skončil. Při selhání mají skripty skončit s nenulovým kódem, uvést srozumitelnou chybu a netisknout úspěšný banner.
- URL po startu se má sestavit ze skutečných nastavení LISTEN_HOST, LISTEN_PORT a SERVER_PATH, nikoli natvrdo předpokládat výchozí hodnoty. Je-li bind adresa zástupná, banner má ukázat použitelnou místní adresu pro prohlížeč. Skript nemá naznačovat existenci veřejné URL, kterou nelze z lokální konfigurace zjistit.
- Zachovat jednu aplikační instanci a worker podle server-python.md. Při ukončení dát serveru možnost korektně zavřít úložiště a síťové prostředky. Případné soubory se stavem procesu jsou provozní data a nemají být součástí distribučního balíčku.
- Aktualizovat distribution.md a server-python.md tak, aby popisovaly nové soubory, inicializaci, spuštění, zastavení a URL banneru. Navázat generování skriptů na skutečný postup tvorby celé serverové distribuce, protože stávající make-dist.ps1 serverovou část nevytváří.

## Akceptační kritéria

1. Nově vytvořená distribuce obsahuje všech šest správně pojmenovaných skriptů v dist/server/Python. Distribuční archiv stále neobsahuje venv, cache, lokální tajemství ani provozní stav.
2. Na čistém podporovaném Windows prostředí lze pomocí init.bat získat chybějící uv přes oficiální PowerShellový instalátor Astral, připravit Python 3.13 a synchronizovat zamčené závislosti. Totéž zvládne init.sh na Linuxu a macOS pomocí oficiálního shellového instalátoru Astral.
3. Startovací skript spustí jednu funkční instanci Python serveru, ověří dosažitelnost endpointu a ukáže banner s použitelnou URL na samostatném řádku. Výchozí URL odpovídá http://127.0.0.1:8000/phpvpn.php. Banner je po vytištění viditelný nejméně tři sekundy.
4. Zastavovací skript bezpečně zastaví vlastní instanci, ověří její konec a ukáže banner bez URL nejméně tři sekundy. Opakované spuštění nebo zastavení a zastaralý stav procesu se řeší bez zásahu do cizích procesů.
5. Pokud selže instalace, start, ověření připravenosti nebo stop, skripty nesmí hlásit falešný úspěch a vrátí nenulový kód.
6. Dokumentace a ověření tvorby distribuce odpovídají novému obsahu Python části na všech podporovaných platformách.
