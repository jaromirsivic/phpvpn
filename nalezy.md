# Nálezy: analýza lokálnosti a přenositelnosti projektu PHP VPN

Datum analýzy: 2026-10-05 · Větev: `linux` (HEAD `62b819d`) · Zadání: `analýza kódu.md`

Analýza proběhla pouze jako analytická fáze. Zdrojové soubory projektu se neměnily, nespouštěla se kompilace ani testy a nevznikly žádné agenty, chaty ani worktree. Jediný zapsaný soubor je tento dokument. Rozpracovaná změna v pracovním stromu (neverzovaný soubor `analýza kódu.md`) zůstala beze změny.

Analýza začala na základě výslovného pokynu uživatele v chatu („Analyzuj zdrojové kódy podle zadání … a výsledek ulož do souboru …“). Ten pokyn je zde bráno jako potvrzení, které zadání požaduje.

---

## 1. Metoda

1. **Kontext z grafu Graft** (podle `AGENTS.md`). Příkaz `graft` není v `PATH`. CLI se našlo v instalaci `%LOCALAPPDATA%\Packages\OpenAI.Codex_*\LocalCache\Roaming\npm\node_modules\@nanonets\graft\dist\cli.js` a spouštělo se odtud: `graft map`, `graft ask` (nginx, absolutní cesty) a `graft grep`. Graf indexuje 434 souborů v jazycích C++, PHP, Python a JavaScript. Skripty PowerShell, sh/bash a BAT, soubory CMake, JSON, unit soubory a manuálové stránky v grafu nejsou, proto se prohledaly přímo.
2. **Vyčerpávající strojové vyhledávání** nad 550 soubory v rozsahu. Použil se vlastní skener v Pythonu, který rozpoznává UTF-8 i UTF-16. Hledalo se pět skupin vzorů:
   - absolutní cesty Windows (`X:\`, `X:/`, UNC),
   - absolutní cesty Linuxu (`/home`, `/root`, `/tmp`, `/etc`, `/usr`, `/var`, `/run`, `/opt`, `/mnt` …),
   - uživatelské adresáře (`C:\Users`, `AppData`, `$HOME`, `~/`),
   - URL a IP adresy (včetně LAN rozsahů a známých veřejných služeb),
   - osobní jména.
3. **Kontrola jmen** proběhla na úrovni bajtů ve všech 891 verzovaných souborech včetně binárních. Prohledávaly se kódování UTF-8, UTF-16LE, CP1250 a Latin-1 bez ohledu na velikost písmen a zvlášť i názvy souborů. Kontrola zahrnula také neverzované a ignorované adresáře `dist/`, `work/`, `graft/`, `Reports/`, `.claude/` a `.superpowers/`.
   - Několik shod, které nahlásil `grep -i` v binárních souborech v `dist/` a `Reports/`, bylo falešně pozitivních. Způsobilo je porovnávání bez ohledu na velikost písmen v locale UTF-8. Přesné bajtové hledání je vyvrátilo.
4. **Ruční čtení** všech skriptů pro sestavení, distribuci, nasazení a lokální prostředí a vybraných testů s nálezy.
5. **Lokální nginx** mimo repozitář (`D:\nginx`) se prověřil jen čtením: `conf\nginx.conf`, `fastcgi-supervisor.ps1` a výpis `html\`.

Rozlišení v doporučeních:

- **[Ověřeno]**: možnost je podložená existujícím kódem nebo mechanismem v projektu. Ověřeno statickou kontrolou, bez spuštění.
- **[Návrh]**: proveditelnost zatím není potvrzená.

---

## 2. Prověřený rozsah

| Oblast | Přibližný počet souborů | Obsah |
|---|---|---|
| Serverová část | 14 | `phpvpn.php`, `phpvpn.py`, `phpvpn.js`, `requirements-server.in/.txt`, `tools/server-python/*` (bootstrap, init, start, stop, server-control), `assets/index.html` (předloha stavové stránky) |
| Společné jádro klienta | 115 | `client/src/core/**`, `client/src/platform/iface/**` |
| Klient pro Windows | 107 | `client/apps/{tray,service,cli,package_gui}`, `client/apps/*.manifest`, `version.rc.in`, `client/src/platform/windows/**`, `client/third_party/wintun/**`, `client/tools/package/*` (instalátor a odinstalátor) |
| Klient pro Linux | 98 | `client/apps/tray-linux/**`, `client/apps/service/service_main_linux.cpp`, `client/src/platform/linux/**`, `client/packaging/linux/**` (deb, rpm, systemd, desktop, man, dist install/uninstall) |
| Sestavení, kompilace a distribuce | 36 | `client/CMakeLists.txt`, `CMakePresets.json`, `client/{apps,src}/CMakeLists.txt`, `client/tools/**` (build-x64, make-dist, dev-*, run-*, verify-package, `linux/*`), `tools/make-distribution.ps1`, `distribution-common.ps1`, `verify-distribution.ps1`, `test-distribution-documentation.ps1`, `copydistributiontovm.bat/.ps1`, `.vscode/settings.json`, `.gitattributes` |
| Testy a podpůrné skripty | 178 | `tests.php`, `tests-info-*.php`, `tests-integration/**`, `tests-node/**`, `tests-python/*.py`, `client/tests/**` (kód, skripty, README), `tests/test_graft_launchers.ps1`, `tools/test-*.py`, `tools/measure-*.py`, `tools/deploy-test.ps1`, `tools/start-test-server.ps1`, `tools/stop-test-server.ps1` |
| Lokální prostředí a vývojové nástroje | 8 | `.gitignore`, `.ignore`, `.intent-builder-active`, `AGENTS.md`, `graft start.bat`, `graft stop.bat`, `readme.txt`, `documentation/protocol.en.source.sha256` |
| Lokální nginx mimo repozitář | 2 | `D:\nginx\conf\nginx.conf`, `D:\nginx\fastcgi-supervisor.ps1` (jen čtení) |

Oblasti se mírně překrývají. Celkem šlo o **550 unikátních verzovaných souborů** plus 2 soubory lokálního nginxu.

### Části mimo rozsah nebo s omezenou kontrolou

| Část | Důvod | Jak byla zohledněna |
|---|---|---|
| `Intents/`, `Plans/`, `Reports/`, `micro_updates/`, dokumentace `*.md` v kořeni (`protocol.md`, `server-*.md`, `distribution.md`, `client.md`, `tray_UX.md`, `audit*.md`), `documentation/protocol.en.md`, reporty `client/tests/*.md` a `tests-python/*.md` | Projektová dokumentace a záznamy. Nejsou to zdrojové soubory serveru, klientů, sestavení ani testů. | Jména se prohledala i zde (0 výskytů). Absolutní cesty jsou jen souhrnně v příloze A. |
| Binární soubory (`*.png`, `*.ico`, `*.kra`, `*.svg`, `*.zip`, `*.dll`, `*.cer`) | Nejde o zdrojový text. | Prohledaly se na jména na úrovni bajtů (0 výskytů). U `phpvpn-development-test.cer` se ověřil subjekt `CN=phpvpn Development Test Signing`. |
| `work/`, `dist/`, `.venv/`, `graft/` (generovaný), `__pycache__/` | Výstupy sestavení a cache, ignorované v `.gitignore`. | Prohledaly se jen na jména (0 výskytů). |
| Obsah certifikátového úložiště `Cert:\CurrentUser\My`, Hyper-V VM „windows 11“, linuxový sestavovací hostitel přes SSH | Mimo souborový systém projektu, nebylo možné je prověřit. | Vazby na ně jsou popsané podle kódu, který je používá. |
| Chování nástroje `uv` při stahování Pythonu (zdroj binárek) | Není zapsané v kódu projektu, jde o výchozí chování nástroje. | Uvedeno jako neověřený předpoklad. |
| Spustitelnost testů a sestavení na jiném počítači | Zadání zakazuje spouštění. | Všechny závěry o dopadu vycházejí ze statické analýzy. |

---

## 3. Závěr o lokálnosti a přenositelnosti

**Projekt v současném stavu není plně přenositelný.** Závěr platí pro prověřený rozsah v kapitole 2.

- **Provozní kód serverů a klientů je přenositelný.**
  - Servery PHP, Node.js a Python ani klienti Windows a Linux neobsahují pevnou cestu vázanou na konkrétní počítač, osobní údaje ani funkční vazbu na externí portál.
  - Windows klient odvozuje cesty ze systému (`SHGetKnownFolderPath(FOLDERID_ProgramData)`, `%ProgramData%`, `%ProgramFiles%`).
  - Linuxový klient používá standardní systémové cesty FHS, které jsou součástí kontraktu balíčku.
  - Servery mají relativní výchozí úložiště (`./phpvpn_data`).
  - Wintun je vložený přímo v repozitáři a sestavení CMake nic nestahuje.
- **Osobní jména** `Jaromír`, `Jaromir`, `Šivic` a `Sivic` se **nevyskytují** v žádném z 891 verzovaných souborů ani v jejich názvech.
- **Porušení se soustředí do čtyř oblastí:**
  1. **Lokální testovací prostředí** je napevno svázané s `D:\nginx` (nginx, PHP 8.5.9, web root, buffer). Konfigurace nginxu není v repozitáři. Integrační testy a část regresí PHP na jiném počítači nepoběží nebo tiše vynechají kontrolu.
  2. **Sestavení distribuce** vyžaduje:
     - privátní klíč podpisového certifikátu v úložišti `Cert:\CurrentUser\My` tohoto uživatele (otisk je napevno v pěti skriptech),
     - konkrétní VM Hyper-V „windows 11“,
     - linuxového sestavovacího hostitele,
     - kontejnerové registry `registry.fedoraproject.org` a `docker.io`.
  3. **Funkční externí závislosti** při inicializaci Python serveru (`astral.sh`, stažení Pythonu, PyPI) a při běhu testovacích nástrojů (`uv run --with …` z PyPI, bez zámku verzí).
  4. **Konfigurace vývojových nástrojů** obsahuje absolutní cestu checkoutu `C:\git\phpvpn` (`.vscode/settings.json`, `.intent-builder-active`) a předpoklady o instalaci nástroje Graft a Node.js.
- **Dopad podle činnosti po klonování na jiný počítač nebo do jiné cesty:**
  - *Kompilace klienta a jednotkové testy CTest*: proveditelné. Presety jsou relativní, nástroje MSVC se hledají přes `vswhere`.
  - *Integrační a serverové testy*: vyžadují ruční přepsání cest nebo parametrů.
  - *Podepsaná distribuce*: bez přenosu podpisového klíče neproveditelná.
  - *Integrace CMake ve VS Code*: ukazuje na neexistující adresář.

Společná globální konfigurace podle kapitoly 6 je proveditelná pro sestavovací a testovací skripty. Nesmí ji ale číst artefakty, které se distribuují koncovým uživatelům (instalátory, serverové skripty). Přesunutí externích URL do konfigurace samo o sobě cíl lokálnosti nesplní; lokální náhrady jsou posouzené v řádcích E01–E06.

---

## 4. Souhrn výsledků jednotlivých kontrol

| Kontrola | Výsledek |
|---|---|
| 1. Pevné absolutní cesty | 66 výskytů cest Windows, 404 výskytů cest Linuxu. **Porušení:** 13 řádků N01–N09, N11–N13, N15. **Ostatní** jsou syntetická testovací data, systémové cesty FHS, cesty uvnitř kontejnerů a dočasné adresáře (příloha B). |
| 2. Vazby na konkrétní počítač | Podpisový certifikát v úložišti uživatele, VM Hyper-V, sestavovací hostitel přes SSH, venv v domovském adresáři uživatele VM, předpoklad vyhrazeného počítače (`stop-test-server.ps1` ukončí všechny procesy nginx/php), neverzovaná konfigurace nginxu. Instalace Graftu a Node.js jsou nízká priorita. **Řádky N10, N14, N16–N20.** |
| 3. Osobní jména | **0 výskytů** v 891 verzovaných souborech (text i binární, UTF-8/UTF-16/CP1250/Latin-1) a v názvech souborů. 0 výskytů také v ignorovaných adresářích `dist/`, `work/`, `graft/`, `Reports/`, `.claude/`, `.superpowers/`. Mimo soubory se jméno objevuje jen v metadatech autora commitů gitu a v neverzovaném souboru se zadáním (poznámka v kapitole 7). |
| 4. Externí webové portály | **Funkční závislosti (řádky E01–E06):** `astral.sh` (instalátor uv), zdroj binárek Pythonu pro `uv python install`, PyPI (server Python i testy), `registry.fedoraproject.org`, `docker.io`, repozitáře balíčků Fedora a Ubuntu (sestavení a instalace linuxového klienta). **Pouze informační odkazy (bez porušení):** `www.pilandrome.com` na stavové stránce serverů, jmenné prostory `schemas.microsoft.com` a `www.w3.org`, identifikátor formátu `debian.org`, odkazy v README Wintunu, `learn.microsoft.com` v komentáři testu. |

---

## 5. Tabulka nalezených porušení

Závažnost: **V** = blokuje sestavení, testy nebo provoz na jiném počítači; **S** = omezuje nebo vyžaduje ruční zásah; **N** = kosmetické, dokumentační nebo vývojový nástroj.

### 5.1 Absolutní cesty a vazby na konkrétní počítač

| Oblast | Soubor | Řádek nebo umístění | Nalezená hodnota či vazba | Účel a dopad | Lze použít relativní cestu nebo lokální řešení? | Lze vyčlenit do společné konfigurace? | Doporučené řešení |
|---|---|---|---|---|---|---|---|
| N01 · Lokální prostředí, testy | `tools/start-test-server.ps1` | 8–10 (použití 20, 29, 38) | `D:\nginx`, `D:\nginx\php8.5.9`, `D:\nginx\html` | Start nginx, php-cgi a vestavěného PHP serveru pro integrační testy. **V:** bez `D:\nginx` selže `Start-Process`. Parametry lze přepsat, výchozí hodnoty jsou ale napevno. | Částečně. Nginx a PHP jsou externí instalace, takže absolutní cesta je potřebná. Šlo by je umístit relativně do `work/` (přenosný nginx a PHP). | Ano (`nginx.root`, `tools.php`, `nginx.webRoot`). | [Ověřeno] Parametry `-NginxRoot`, `-PhpRoot` a `-WebRoot` už existují, stačí je plnit z konfigurace. [Návrh] Bez hodnoty skončit srozumitelnou chybou místo výchozí `D:\…`. |
| N02 · Lokální prostředí | `tools/stop-test-server.ps1` | 8; 14–20 | `D:\nginx`; `Get-Process nginx/php-cgi/php \| Stop-Process -Force` | Zastavení testovacího prostředí. **S:** výchozí cesta je vázaná na stroj. Skript navíc ukončí **všechny** procesy nginx, php-cgi a php na počítači, takže předpokládá vyhrazený vývojový počítač. Na sdíleném stroji ukončí cizí procesy. | Ano (cesta z konfigurace). | Ano (`nginx.root`). | [Návrh] Kořen brát z konfigurace. Ukončovat jen procesy spuštěné z konfigurované cesty, podobně jako to dělá `D:\nginx\fastcgi-supervisor.ps1` přes `ExecutablePath`. |
| N03 · Testy, nasazení | `tools/deploy-test.ps1` | 16–17; 18–19; 76–77 | `WebRoot='D:\nginx\html'`, `BufferDirectory='D:/nginx/phpvpn-buffer'`, `BaseUrl='http://127.0.0.1/phpvpn.php'` (port 80), zápis do verzovaného `tests-integration\server.json` | Nasazení `phpvpn.php` do web rootu lokálního nginxu a generování popisu nasazení pro testy. **V:** jinde selže zápis. Generovaný popis se zapisuje do verzovaného souboru, takže každý počítač mění repozitář (viz N04). | Ano pro výstup (např. `work/tests-integration/server.json`). Web root a buffer jsou absolutní podle instalace nginxu. | Ano (`nginx.webRoot`, `nginx.bufferDirectory`, `nginx.baseUrl`, `nginx.largeBodyUrl`). | [Ověřeno] Parametr `-ConfigurationPath` už existuje (ř. 21, 76–77) a `conftest.py` přijímá `--server-config` (ř. 26). Výstup lze přesměrovat do `work/`. [Návrh] Výchozí hodnoty brát z konfigurace. |
| N04 · Testy | `tests-integration/server.json` | 11–12 | `"bufferDirectory": "D:/nginx/phpvpn-buffer"`, `"deployedFile": "D:\\nginx\\html\\phpvpn.php"` | Verzovaný, generovaný popis lokálního nasazení. `buffer_directory` čtou testy `test_security.py:189–204`. **V:** na jiném počítači testy kontrolují neexistující nebo cizí adresář, případně selžou. | Ano. Soubor má být lokálním výstupem, ne verzovaným zdrojem. | Ano (hodnoty se generují z konfigurace). | [Návrh] Soubor vyřadit z verzování (`git rm --cached`, `.gitignore`) a generovat do `work/` pomocí `deploy-test.ps1` z konfigurace. |
| N05 · Testy (Python) | `tests-python/test_wire.py` | 174–178 | `Path('D:/nginx/php8.5.9/php.exe')` | Nezávislá kontrola shody pretty-JSON se skutečným PHP. **S:** pokud cesta neexistuje, kontrola se **tiše vynechá** a test projde. Na jiném počítači tak klesá pokrytí, aniž by to bylo vidět. | Ano, vyhledání v `PATH`. | Ano (`tools.php`). | [Ověřeno] Vzor `shutil.which("php")` už používá `tools/test-python-server.py:57`. [Návrh] Cestu brát z konfigurace, jinak z `PATH`. Při chybějícím PHP použít explicitní `pytest.skip` s důvodem. |
| N06 · Testy (regrese PHP) | `tools/test-status-variants.py` | 17 | `PHP = pathlib.Path('D:/nginx/php8.5.9/php.exe')` | Izolovaná HTTP regrese stavové stránky. **V:** na jiném počítači skript selže (PHP nenalezeno). | Ano (`PATH`). | Ano (`tools.php`). | [Ověřeno] Sousední `tools/test-info-storage.py:20` už přijímá `--php`. [Návrh] Přidat `--php`, výchozí hodnotu brát z konfigurace nebo `PATH`. |
| N07 · Testy (integrační) | `tests-integration/phpvpn_client.py` | 74–80 (`LEAK_PATTERNS`) | `b"D:\\"`, `b"D:/"`, `b"C:\\"`, `b"C:/"`, `b"/nginx/"`, `b"\\nginx\\"`, `b"phpvpn-buffer"` | Detekce úniku interních cest v odpovědích serveru. **S:** vzory odpovídají jen tomuto nasazení. Při web rootu jinde (`E:\…`, `/var/www/…`, jiný název bufferu) detekce úniku cest **nic nenajde** (falešně negativní). | Ano. | Ano. Vzory lze odvodit z `nginx.webRoot` a `nginx.bufferDirectory`. | [Návrh] Ponechat obecné vzory a doplnit vzory odvozené z konfigurace nasazení (web root, buffer, kořen PHP). |
| N08 · Testy (integrační) | `tests-integration/test_headers.py` | 142 | `b"d:/"`, `b"d:\\"`, `b"c:\\"` | Totéž jako N07 pro hlavičky. **S:** falešně negativní mimo disky C: a D: a mimo Windows. | Ano. | Ano. | [Návrh] Stejně jako N07, sdílet jednu funkci generující vzory. |
| N09 · Testy (dokumentace spouštění) | `client/tests/e2e/test_http_contention.py`; `tests-integration/README.md` | 4; 14, 49, 190, 203 | `--php D:/nginx/php8.5.9/php.exe`; `--node "C:/Program Files/nodejs/node.exe"` | Návody ke spuštění testů. **N:** příkazy nejdou zkopírovat a spustit na jiném počítači. Samotný kód má `--php` povinný (`test_http_contention.py:38`). | Ano (`php` a `node` z `PATH`). | Ano (`tools.php`, `tools.node`). | [Ověřeno] `tools/test-python-server.py:57,68` hledá `php` a `node` v `PATH`, když chybí argument. [Návrh] Upravit příklady na `--php php` nebo na hodnotu z konfigurace. |
| N10 · Lokální nginx (mimo repozitář) | `D:\nginx\conf\nginx.conf` | Celý soubor (`root D:/nginx/html`, `listen 0.0.0.0:80`, `fastcgi_pass 127.0.0.1:9000`, výchozí `client_max_body_size`) | Neverzovaná konfigurace prostředí, na které závisí testy | Testy předpokládají port 80 (`deploy-test.ps1:18`), FastCGI na 9000 (`start-test-server.ps1:11`) a limit těla 1 MiB (`test_limits.py:30, 165–174`; `conftest.py:19` druhý endpoint `:8081`). **V:** prostředí nelze z repozitáře zreprodukovat. Navíc `listen 0.0.0.0:80` vystavuje testovací server do sítě (bezpečnostní poznámka mimo zadání). | Ano. Šablona v repozitáři s relativními nebo dosazenými cestami. | Ano (port, web root, FastCGI adresa, limit těla). | [Návrh] Přidat do repozitáře šablonu `nginx.conf` a generovat z ní konfiguraci podle společné konfigurace. Naslouchat na `127.0.0.1`. Limit `NGINX_BODY_LIMIT` brát z konfigurace. |
| N11 · Lokální nginx (mimo repozitář) | `D:\nginx\fastcgi-supervisor.ps1` | 8 | `$Root = 'D:\nginx'` | Supervizor php-cgi. Repozitář na něj neodkazuje (ověřeno `git grep`). **N:** součást neverzovaného prostředí. | Ano. | Ano (`nginx.root`). | [Návrh] Pokud je supervizor potřebný, převzít ho do repozitáře (např. `tools/`) s cestami z konfigurace. Jinak ho zdokumentovat jako volitelný. |
| N12 · Konfigurace IDE | `.vscode/settings.json` | 2 | `"cmake.sourceDirectory": "C:/git/phpvpn/client"` | Zdrojový adresář pro rozšíření CMake Tools. **S:** při klonování do jiné cesty ukazuje VS Code na neexistující adresář (konfigurace CMake z IDE selže). | Ano (proměnná pracovní složky VS Code, např. `${workspaceFolder}/client`). | Ne. Jde o soubor IDE, relativní zápis stačí. | [Návrh] Nahradit `${workspaceFolder}/client`. Rozšíření CMake Tools tuto proměnnou podporuje podle své dokumentace; v tomto prostředí neověřeno. |
| N13 · Vývojové nástroje | `.intent-builder-active` | 3, 4, 7, 10, 12 | `C:\git\phpvpn`, `C:\git\phpvpn\Intents\…`, `C:\git\phpvpn\Plans\…` | Stavový soubor nástroje „intent builder“, verzovaný v gitu. **N/S:** na jiném počítači nebo v jiné cestě odkazuje na neexistující cesty. Nástroj může pokračovat nad nesprávným adresářem. | Ano (cesty relativní ke kořeni repozitáře). | Ne. Jde o stav nástroje, ne o konfiguraci projektu. | [Návrh] Ukládat relativní cesty, nebo soubor vyřadit z verzování (lokální stav). |
| N14 · Sestavení distribuce (podpis) | `client/tools/make-dist.ps1`; `client/tools/linux/make-linux-dist.ps1`; `tools/make-distribution.ps1`; `tools/distribution-common.ps1` | 24, 139; 26; 11; 387, 404–406 | Otisk `7430CCA24F5DE8A6614D582AFBFED7C80E0F2CA8` v `Cert:\CurrentUser\My` | Podpis `release-manifest.json` (a volitelně Authenticode). **V:** privátní klíč je jen v úložišti certifikátů tohoto uživatele na tomto počítači. Na jiném počítači `Write-SignedReleaseManifest` skončí chybou „not present in Cert:\CurrentUser\My“ a distribuci nelze sestavit. | Ne, cesta tu není. Lokální řešení je přenos klíče nebo vlastní vývojový klíč. | Ano pro otisk a úložiště (`signing.certificateThumbprint`, `signing.store`). Privátní klíč do konfigurace nepatří. | [Ověřeno] Otisk je parametrem `-CertificateThumbprint` ve všech třech skriptech. [Návrh] Výchozí hodnotu brát z konfigurace. Zdokumentovat bezpečný přenos klíče (PFX s heslem mimo repozitář), nebo postup vytvoření vlastního vývojového klíče s dosazením pinu při sestavení (viz N15). |
| N15 · Instalátory klientů (pin podpisu) | `client/tools/package/install.ps1`; `client/packaging/linux/dist/install.sh` | 30–31, 107; 35–37, 125 | `ExpectedThumbprint = "7430CCA2…"`; `PINNED_SPKI_SHA256=1ac5908c…` | Instalátory ověřují, že balíček podepsal připnutý klíč. **V (pro sestavení jinde):** balíček podepsaný jiným klíčem instalátor odmítne, takže i vlastní vývojový klíč na jiném počítači vyžaduje změnu zdrojů. Pro provoz u koncového uživatele je to záměrná bezpečnostní kotva, ne vada. | Ne. | **Jen v době sestavení.** Instalátory běží u koncového uživatele a nesmějí číst vývojovou konfiguraci. | [Návrh] Pin dosazovat do kopie instalátoru v distribuci při sestavení z konfigurace (`signing.certificateThumbprint` → SHA-1 a SPKI SHA-256). Zdrojový soubor by nesl jen zástupnou hodnotu ověřovanou v `verify-distribution.ps1`. |
| N16 · Sestavení distribuce (přenos do VM) | `copydistributiontovm.ps1` (volá ho `copydistributiontovm.bat:41`) | 3, 6 (použití 17–34) | `$vmName = "windows 11"`, `$destinationDirectory = "C:\Temp"` (v hostu VM) | Kopírování `dist` a `dist.zip` do konkrétní VM Hyper-V přes `Copy-VMFile`. **S:** vázané na VM s tímto názvem na tomto hostiteli, na modul Hyper-V a Guest Services. Jinde selže. | Částečně. Cílovou cestu ve VM lze nastavit, název VM je vlastnost prostředí. | Ano (`vm.name`, `vm.destinationDirectory`). | [Návrh] Název VM a cíl brát z konfigurace. Pokud hodnota chybí, krok přeskočit s hlášením. |
| N17 · Sestavení distribuce (linuxový hostitel) | `client/tools/linux/sync-to-vm.sh`; `tools/make-distribution.ps1` | 7–9, 18; 4–5, 10 | `<ssh-host>`, `PHPVPN_SSH_CONFIG`, vzdálený `$HOME/phpvpn`; povinný `-LinuxPackagesDirectory` z „Linux build host“ | Úplná distribuce vyžaduje sestavení linuxových balíčků na samostatném linuxovém hostiteli a přenos přes SSH. **S:** je to parametrizované, ale proces vyžaduje konkrétní infrastrukturu (hostitel, klíč SSH, podman nebo docker) mimo projekt. | Částečně. Lokální řešení je linuxové sestavení na témže počítači (např. WSL nebo podman), neověřeno. | Ano (`linux.buildHost`, `linux.remoteDirectory`, `linux.sshConfig`, `linux.containerEngine`). | [Ověřeno] Hostitel i SSH konfigurace jsou už parametry. [Návrh] Hodnoty brát z konfigurace. Ověřit sestavení balíčků lokálně ve WSL nebo podmanu na Windows. |
| N18 · Testy (Linux tray) | `client/tests/linux/tray/run-wizard-checks.sh` | 26, 35 | `PHPVPN_SERVER_PYTHON` s výchozí hodnotou `$HOME/phpvpn-wp06-venv/bin/python` | Interpret s `requirements-server.txt` pro relay v testu průvodce. **S:** výchozí hodnota odkazuje na ručně vytvořený venv v domovském adresáři uživatele konkrétní VM. Jinde test selže, dokud se nenastaví proměnná. | Ano (venv relativně k repozitáři, např. `work/…`). | Ano (`python.serverInterpreter`). | [Ověřeno] Ostatní linuxové skripty používají výchozí `python3` (`run-service-e2e.sh:38`, `cli-smoke.sh:24`, `installed-service-check.sh:26`, `gui-acceptance-env.sh:19`). [Návrh] Sjednotit výchozí hodnotu a brát ji z konfigurace. |
| N19 · Vývojové nástroje (Graft) | `graft start.bat`; `AGENTS.md` | 76–84, 98; blok `graft:start` | `C:\Program Files\nodejs\node.exe`; hledání CLI v `%APPDATA%\npm`, `%LOCALAPPDATA%\npm`, `…\Packages\OpenAI.Codex_*\…\@nanonets\graft` | Spuštění vizualizace grafu. `AGENTS.md` vyžaduje Graft pro práci s repozitářem. **N:** předpoklad o umístění Node.js a Graftu. Existují záložní varianty (`PATH`, `GRAFT_NODE`, `GRAFT_CLI`). Graft CLI není v repozitáři ani v `PATH` tohoto počítače (zjištěno při analýze). | Ano (`PATH`). | Ano (`tools.node`, `tools.graftCli`). | [Ověřeno] Přepsání přes `GRAFT_NODE` a `GRAFT_CLI` už existuje. [Návrh] Upřednostnit `PATH` před pevnou cestou, cesty volitelně z konfigurace. Do `AGENTS.md` doplnit instalaci Graftu. |
| N20 · Testy (skripty) | `tools/stop-test-server.ps1` (viz N02), `client/tools/run-service-e2e.ps1`, `client/tools/run-resource-tests.ps1` | 18–20; 49–50; 12–14, 50 | Kontrola nebo ukončování globálních prostředků počítače (služba `phpvpn`, výchozí trasy, DNS 127.0.0.2:53) | **N:** testy předpokládají vývojový počítač bez produkční instalace `phpvpn` a bez kolizních služeb. Skripty to samy kontrolují a porovnávají stav před a po (ověřeno čtením). | Netýká se. | Ne. | [Ověřeno] Ochranné kontroly existují. Doporučuje se jen zdokumentovat předpoklady v README testů. |

### 5.2 Funkční závislosti na externích portálech a službách

| Oblast | Soubor | Řádek nebo umístění | Nalezená hodnota či vazba | Účel a dopad | Lze použít relativní cestu nebo lokální řešení? | Lze vyčlenit do společné konfigurace? | Doporučené řešení |
|---|---|---|---|---|---|---|---|
| E01 · Server Python: inicializace a testy | `tools/server-python/bootstrap.ps1`; `tools/server-python/init.sh`; `readme.txt` | 68; 47, 49; 49 („Init requires Internet access“) | `https://astral.sh/uv/install.ps1`, `https://astral.sh/uv/install.sh` (stažení a **spuštění** instalátoru) | Instalace nástroje `uv`, když není v `PATH` ani v `.tools/uv`. **V:** při výpadku nebo zániku `astral.sh` nebo bez internetu nelze inicializovat Python server u uživatele ani spustit `tools/test-server-scripts.py:125`, který volá skutečný `init`. Spouštění staženého skriptu je navíc riziko dodavatelského řetězce. | Ano. Lokální `uv` už je podporovaný. | Přesun URL do konfigurace cíl nesplní. Do konfigurace patří jen volitelná cesta k `uv`. | [Ověřeno] Skripty upřednostní existující `uv` z `PATH` nebo `.tools/uv` (`bootstrap.ps1:51–57`, `init.sh:34–38`). [Návrh] Přibalit binárku `uv` s ověřeným SHA-256 do distribuce `server/Python/.tools/uv/` pro podporované platformy, nebo vyžadovat předinstalovaný `uv` a stahování odstranit. |
| E02 · Server Python: inicializace | `tools/server-python/bootstrap.ps1`; `tools/server-python/server-control.py`; `requirements-server.txt` | 77–78; 512–514, 519; celý soubor | `uv python install 3.13` (stažení Pythonu); `uv pip sync requirements-server.txt` (PyPI: cryptography, uvicorn, psutil, cffi, click, h11, pycparser) | Příprava izolovaného `.venv`. **V pro nasazení:** bez internetu nebo při nedostupnosti zdrojů nelze server zprovoznit. Po inicializaci běží server bez internetu (ověřeno: běh používá jen `.venv`). Zdroj binárek Pythonu není v kódu, je to výchozí chování `uv` (neověřeno). | Ano. | Přesun URL cíl nesplní. Do konfigurace lze dát cestu k lokálnímu úložišti balíčků nebo interpretu. | [Ověřeno] Verze jsou zamčené v `requirements-server.txt`. [Návrh] Přibalit offline sadu wheelů (wheelhouse) pro cílové platformy a použít `uv pip sync --offline --find-links <wheelhouse>`. Alternativně povolit systémový Python 3.13 místo stahování. Server PHP a Node.js tuto závislost nemají. |
| E03 · Sestavení klienta Linux (balíčky) | `client/tools/linux/build-packages-in-container.sh` | 44–45, 49–53; 149–150; 195 | `registry.fedoraproject.org/fedora:<N>`, `docker.io/library/ubuntu:24.04`, `dnf builddep`, `apt-get build-dep` (repozitáře distribucí) | Čisté sestavení RPM a DEB v kontejneru. **V pro distribuci:** při nedostupnosti registrů nebo zrcadel nelze sestavit linuxové balíčky, a tedy ani úplnou distribuci (`make-distribution.ps1` je vyžaduje). | Částečně. Lokální registr nebo zrcadlo, uložené obrazy (`podman save` a `podman load`), lokální zrcadlo balíčků. | Ano pro prefix registru a reference obrazů. Samotný přesun URL cíl nesplní. | [Návrh] Prefix registru a obrazy brát z konfigurace (výchozí veřejné), podporovat lokální zrcadlo. Archivovat použité obrazy a balíčky pro reprodukovatelnost (provenance už zaznamenává `imageId`, ř. 196, 210–211). |
| E04 · Testy instalace Linux | `client/tests/e2e/linux/package-install-container.sh` | 30–58 (31, 46–47) | `registry.fedoraproject.org/…`, `docker.io/library/…`, `apt-get` a `dnf` v připravovaném obrazu | Test instalace, upgradu a odinstalace balíčků v kontejneru. **S:** offline nebo při výpadku registrů test nepoběží. | Částečně (jako E03). | Ano (jako E03). | [Návrh] Stejné řešení jako E03, sdílet prefix registru z konfigurace. |
| E05 · Provoz a instalace klienta Linux | `client/packaging/linux/dist/install.sh` | 17, 269–279 | `dnf install` a `apt-get install` závislostí z repozitářů distribuce | Instalace runtime závislostí (libcurl, OpenSSL, GTK…) u uživatele. **S:** instalace na počítači bez přístupu k repozitářům (nebo k místnímu zrcadlu) selže. Je to standardní model distribučních balíčků, ne vazba na portál projektu. | Částečně. Místní zrcadlo repozitářů funguje bez změny projektu (konfigurace systému). | Ne. Jde o konfiguraci OS u uživatele. | [Ověřeno] `apt-get update` při chybě pokračuje s existujícími seznamy (ř. 275). [Návrh] Zdokumentovat offline instalaci (předem stažené závislosti nebo místní zrcadlo). Odstranění by vyžadovalo statické linkování nebo vložené knihovny, což je změna distribučního modelu. |
| E06 · Testovací nástroje | `tests-integration/README.md`; `client/tests/e2e/test_http_contention.py`; `tools/test-info-storage.py`; `client/tools/make_linux_icons.py`; `client/tools/check_icons.py` | 14, 49, 190, 203; 3; 4; 21–28; 3 | `uv run --with pytest --with requests --with cryptography --with psutil`, `--with pillow` (PyPI, **bez zámku verzí**) | Spouštění pytest testů a ikonových nástrojů. **S:** na novém počítači bez internetu nebo při výpadku PyPI testy nepoběží. Verze nejsou zamčené, takže výsledky nejsou reprodukovatelné. | Ano (lokální wheelhouse nebo předpřipravený venv). | Ano pro cestu k testovacímu interpretu nebo venvu (`python.testInterpreter`). Přesun URL cíl nesplní. | [Ověřeno] `requirements-server.txt` ukazuje, že zamykání přes `uv pip compile` projekt už používá. [Návrh] Zavést `requirements-test.in/.txt` se zámkem verzí a volitelnou offline sadu wheelů. Testy spouštět z jednoho venvu podle konfigurace. |

---

## 6. Návrh společné globální konfigurace

### 6.1 Proveditelnost

| Spotřebitel | Jazyk | Čtení JSON | Stav |
|---|---|---|---|
| Sestavovací a distribuční skripty (`tools/*.ps1`, `client/tools/*.ps1`) | Windows PowerShell 5.1 | `ConvertFrom-Json` | [Ověřeno] Projekt už používá (např. `make-distribution.ps1:64`, `verify-package.ps1:18`). |
| Testy a nástroje v Pythonu (`tests-integration`, `tests-python`, `tools/*.py`) | Python 3.13 | `json` | [Ověřeno] Používá se (`conftest.py`, `phpvpn_client.py:561`). |
| Linuxové skripty (`client/tools/linux/*.sh`, `client/tests/**/*.sh`) | POSIX sh / bash | Bez nativní podpory | [Návrh] Číst přes `python3 -c 'import json…'`. `python3` už linuxové testy vyžadují (`run-service-e2e.sh:61`). Alternativně generovat z JSON soubor `KEY=VALUE` do `work/`. |
| CMake (pokud bude potřeba) | CMake ≥ 3.28 | `file(READ)` + `string(JSON)` (od 3.19) | [Ověřeno] Presety vyžadují CMake 3.28 (`CMakePresets.json`, `cmakeMinimumRequired`). V současnosti CMake žádnou počítačově specifickou hodnotu nepotřebuje. |
| Testy PHP a Node.js (`tests.php`, `tests-node`) | PHP, Node.js | `json_decode`, `JSON.parse` | Netýká se. V těchto testech nebylo nalezeno porušení. |

**Závěr:** jeden soubor JSON v kořeni repozitáře lze číst ze všech míst, kde nálezy vyžadují konfigurovatelné hodnoty. Proveditelnost je potvrzená pro PowerShell, Python a CMake (podle existujícího kódu); pro sh jde o návrh přes `python3`.

### 6.2 Omezení

1. **Distribuované artefakty konfiguraci číst nesmějí.** Týká se instalátorů (`install.ps1`, `install.sh`), serverových skriptů (`tools/server-python/*`) a serverů. Běží na počítačích koncových uživatelů, kde vývojová konfigurace neexistuje. Hodnoty jako pin podpisového klíče (N15) se proto musejí do kopie v distribuci dosadit **při sestavení**.
2. **Napětí mezi „jeden společný soubor“ a „přenositelnost“.** Pokud bude soubor verzovaný, musí obsahovat přenositelné výchozí hodnoty: relativní cesty a prázdné hodnoty ve smyslu „najdi v `PATH`“. Počítačově specifické hodnoty (cesta k nginxu, název VM) pak patří do lokální kopie téhož souboru. Doporučení: verzovat šablonu `phpvpn.config.example.json` a lokální `phpvpn.config.json` zařadit do `.gitignore`. Skripty čtou vždy jen jeden soubor, jiný lze zvolit proměnnou `PHPVPN_CONFIG`. [Návrh]
3. **Kontejnery a vzdálené hostitele** (E03, E04, N17) konfiguraci nevidí. Konfiguraci čte strana hostitele a hodnoty předává proměnnými prostředí, jako to už dělá `build-packages-in-container.sh:203–212` (`-e PHPVPN_…`). [Ověřeno]
4. **Povýšené procesy** (`Start-Process -Verb RunAs` v `copydistributiontovm.bat:41`, `run-service-e2e.ps1:43`) musejí dostat cestu ke konfiguraci explicitně v argumentu.
5. **Tajemství.** Privátní klíč ani heslo PFX do konfigurace nepatří, jen otisk a název úložiště. Testovací tajemství (`phpvpn-integration-test-secret-2026`, výchozí klíče `.2–.15`) nejsou počítačově specifická a mohou zůstat v kódu.
6. **Konfigurace nginxu** má vlastní formát. Do repozitáře patří šablona, ze které se konfigurace vygeneruje podle hodnot ze společné konfigurace (N10). [Návrh]
7. **Externí URL.** Jejich přesun do konfigurace (prefix registru, zdroj `uv`) usnadní použití zrcadla, cíl lokálnosti ale nesplní. Lokální náhrady jsou posouzené samostatně v E01–E06.
8. **Opakovaný kód pro vyhledání MSVC** (`vswhere` a `VsDevCmd`) je v šesti skriptech: `build-x64.ps1:15`, `make-distribution.ps1:89`, `run-server-contract.ps1:42`, `run-service-e2e.ps1:29`, `run-tray-ui-tests.ps1:28` a `run-platform-probe.ps1:8`. Nejde o porušení (`vswhere` je ve standardním umístění Microsoftu). Hodí se ale sjednotit do jednoho pomocníka, který zároveň načte konfiguraci. [Návrh]

### 6.3 Navrhovaná struktura (nevytvořeno, jen návrh)

```json
{
  "formatVersion": 1,
  "paths": {
    "work": "work"
  },
  "tools": {
    "php": "",
    "phpCgi": "",
    "node": "",
    "graftCli": ""
  },
  "python": {
    "serverInterpreter": "python3",
    "testInterpreter": ""
  },
  "nginx": {
    "root": "",
    "webRoot": "",
    "bufferDirectory": "work/phpvpn-buffer",
    "listen": "127.0.0.1:80",
    "fastCgiAddress": "127.0.0.1:9000",
    "baseUrl": "http://127.0.0.1/phpvpn.php",
    "largeBodyUrl": "http://127.0.0.1:8081/phpvpn.php",
    "bodyLimitBytes": 1048576
  },
  "integrationTests": {
    "serverDescription": "work/tests-integration/server.json"
  },
  "signing": {
    "certificateThumbprint": "7430CCA24F5DE8A6614D582AFBFED7C80E0F2CA8",
    "store": "Cert:\\CurrentUser\\My"
  },
  "linux": {
    "buildHost": "",
    "sshConfig": "",
    "remoteDirectory": "phpvpn",
    "containerEngine": "",
    "registryFedora": "registry.fedoraproject.org",
    "registryUbuntu": "docker.io/library"
  },
  "vm": {
    "name": "",
    "destinationDirectory": "C:\\Temp"
  }
}
```

Pravidla návrhu:

- Relativní cesty se vztahují ke kořeni repozitáře.
- Prázdný řetězec u nástroje znamená vyhledání v `PATH`.
- Prázdná hodnota infrastruktury (`vm.name`, `linux.buildHost`) znamená, že se krok přeskočí s hlášením.

Dotčené nálezy: N01–N12, N14–N19, E03, E04 a E06. U E01, E02 a E05 konfigurace řeší jen cestu k lokální náhradě.

---

## 7. Poznámky ke kontrole osobních jmen

- **Výsledek: 0 výskytů** jmen `Jaromír`, `Jaromir`, `Šivic` a `Sivic` v libovolné kombinaci velikosti písmen.
- **Prohledaná místa:**
  - zdrojový kód, komentáře, řetězce, konfigurace a skripty v analyzovaném rozsahu (550 souborů),
  - všech 891 verzovaných souborů včetně binárních a dokumentace,
  - názvy všech verzovaných souborů,
  - ignorované adresáře `dist/`, `work/`, `graft/`, `Reports/`, `.claude/` a `.superpowers/`.
- **Metadata balíčků používají neutrální identitu:** `phpvpn maintainers <maintainers@phpvpn.invalid>` (`debian/control`, `debian/changelog`, `rpm/phpvpn.spec:151`), `Copyright: 2026 phpvpn contributors` (`debian/copyright`) a vývojový certifikát `CN=phpvpn Development Test Signing`.
- **Mimo analyzované soubory** (informativně, nejde o nález v rozsahu):
  - Jméno je v **metadatech autora všech 203 commitů** v historii gitu a v nastavení `user.name` lokálního gitu. Do souborů projektu ani do sestavených balíčků se nepropisuje: provenance linuxových balíčků zaznamenává jen hash commitu (`build-packages-in-container.sh:113–114`).
  - Jméno obsahuje i neverzovaný soubor se zadáním `analýza kódu.md`, který není součástí projektu.

---

## 8. Doporučené pořadí následné realizace (návrh)

1. **N04 a N03**: vyřadit generovaný `server.json` z verzování. Je to jednoduchý krok, který odstraní zápis do repozitáře závislý na počítači.
2. **Zavést společnou konfiguraci (kapitola 6)** a napojit na ni N01–N03, N05–N10 a N18. Tím zmizí vazba na `D:\nginx` a testy půjdou spustit jinde.
3. **N12 a N13**: relativní cesty v konfiguraci IDE a nástrojů.
4. **N14 a N15**: zdokumentovat a zautomatizovat přenos nebo vytvoření podpisového klíče a dosazování pinu při sestavení.
5. **E06**: zamknout verze testovacích závislostí a připravit offline sadu wheelů.
6. **E01 a E02**: přibalit `uv` a sadu wheelů do distribuce Python serveru, nebo stahování nahradit předinstalovanými nástroji.
7. **E03, E04 a N16, N17**: zrcadla registrů a konfigurovatelná infrastruktura distribuce.

---

## Příloha A: Absolutní cesty v dokumentaci mimo rozsah (souhrn)

Verzované dokumenty v `Intents/`, `Plans/`, `Reports/`, `micro_updates/`, kořenových `*.md` a reportech `client/tests/*.md` obsahují:

| Hodnota | Souborů | Výskytů |
|---|---|---|
| `C:\git\phpvpn` (nebo s `/`) | 52 | 1002 |
| `D:\nginx` (nebo s `/`) | 39 | 89 |
| `C:\Users\User` (nebo s `/`) | 7 | 55 + 18 |

Jde o historické záznamy. Na sestavení, testy ani provoz nemají vliv a jednotlivě se neuvádějí. Výjimkou je `server-python.md:190`: dokument se kopíruje do distribuce (`distribution-common.ps1:8`) a obsahuje příklad `--php D:/nginx/php8.5.9/php.exe`, takže vhodné řešení je stejné jako u N09.

## Příloha B: Prověřené výskyty bez porušení

| Kategorie | Příklady umístění | Důvod, proč nejde o porušení |
|---|---|---|
| Syntetické cesty Windows v jednotkových testech | `client/tests/unit/test_tray_autostart.cpp:8–34`, `test_wizard_ipc.cpp:14–15, 102, 426, 438–439`, `fake_config_files.h:10`, `test_plan3_fix2.cpp:182–183`, `test_tray_application.cpp:217`, `test_wp03_windows.cpp:364, 867, 902`, `test_wp03_save_windows.cpp:109`, `test_wp05_windows.cpp:183, 185, 459`, `test_tray_progress.cpp:90`, `ui/dev_preview.inc:156`, `resources/tray_registry.cpp:46`, `client/tests/test_package_upgrade.ps1:35–49` | Vstupní data testů (`C:/test`, `Z:\synthetic`, `C:\app`). Souborový systém se nečte, nejde o vazbu na počítač. |
| Útočné řetězce v bezpečnostních testech | `tests-integration/test_security.py:139–140, 162` (`/etc/passwd`, `C:\windows\system32`) | Testovací vstupy pro path traversal. |
| Systémové cesty Linux klienta (FHS) | `client/src/platform/linux/paths_linux.h:20–21`, `client/src/core/ipc/ipc_protocol.h:33`, `packaging/linux/systemd/phpvpn.service:9, 26, 30`, `debian/rules:19–20`, `debian/phpvpn.postrm:10`, `apps/CMakeLists.txt:23–31`, man stránky, `dist/install.sh`, `uninstall.sh` | Kontrakt produktu a distribuční konvence (`/etc/phpvpn`, `/var/lib/phpvpn`, `/run/phpvpn`, `/usr/libexec/phpvpn`). Absolutní cesty jsou tu nutné a stejné na každém počítači. Do vývojové konfigurace nepatří. |
| Cesty uvnitř dočasných kontejnerů | `build-packages-in-container.sh:51–60` (`/src`, `/out`, `/home/phpvpn-builder`), `package-install-container.sh:104–234` (`/root/release-*`) | Existují jen uvnitř kontejneru, který skript sám vytváří. |
| Dočasné a běhové adresáře testů | `mktemp -d /tmp/phpvpn-*` (linuxové testy tray), `/run/phpvpn-e2e`, `/run/phpvpn-wp10a`, `/run/phpvpn-cli-smoke`, unit testy `/tmp/phpvpn-*-XXXXXX` | Konvenční dočasná umístění, vytvářená a mazaná za běhu. |
| Pevné podsítě v izolovaných síťových jmenných prostorech | `run-service-e2e.sh:10–13, 46` (192.168.77.0/24), `run-wizard-checks.sh:9, 42–43` (192.168.91.0/24) | Leží v soukromých jmenných prostorech (namespace), hlavní jmenný prostor počítače se nemění (ověřeno v komentářích a kontrolách skriptů). |
| Cesty Windows klienta za běhu | `paths_win.cpp:53–64` (`SHGetKnownFolderPath`), `window_utils.h:15`, `install.ps1:46`, `uninstall.ps1:36` (`$env:ProgramData`) | Odvozené ze systému. |
| Relativní cesty sestavení | `CMakePresets.json` (`${sourceDir}/../work/build/${presetName}`), `client/apps/CMakeLists.txt:55`, všechny `client/tools/*.ps1` (`$PSScriptRoot`), `client/tools/linux/*.sh` (`dirname "$0"`) | Přenositelné. CMake nepoužívá `FetchContent` ani `ExternalProject`, Wintun je v `client/third_party/wintun`. |
| Umístění `vswhere.exe` | `${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe` (6 skriptů) | Standardní, Microsoftem garantované umístění. Doporučeno jen sjednotit (kapitola 6.2, bod 8). |
| Výchozí úložiště serverů | `phpvpn.php`: `BUFFER_DIRECTORY = './phpvpn_data'` (stejně v Pythonu a Node.js) | Relativní. |
| Informační odkaz `www.pilandrome.com` | `phpvpn.php:2090`, `phpvpn.js:1154`, `phpvpn.py:852`, `assets/index.html:1`. Testy jen porovnávají text: `tests.php:648`, `tests-node/http.test.js:77`. | Jen hypertextový odkaz v patičce stavové stránky, server ani prohlížeč z něj nic nenačítá (stránka nemá externí skripty, styly ani fonty). Vypnutí portálu sestavení, testy ani provoz neovlivní. |
| Jmenné prostory XML | `client/apps/app.manifest:36–39`, `package_gui/admin.manifest:16–19` (`schemas.microsoft.com`), `client/tools/make_linux_icons.py:42` (`www.w3.org/2000/svg`) | Identifikátory, nikdy se nestahují. |
| Identifikátor formátu | `client/packaging/linux/debian/copyright:1` (`debian.org/doc/…/copyright-format/1.0/`) | Povinný identifikátor formátu, nestahuje se. |
| Dokumentační odkazy | `client/third_party/wintun/README.md` (`wintun.net`, `git.zx2c4.com`, `wireguard.com`, `visualstudio.microsoft.com`, `docs.microsoft.com`), `client/tests/unit/test_winhttp_accounting.cpp:314` (`learn.microsoft.com`) | Informační odkazy v převzatém README a v komentáři. |
| Zástupné, testovací a smyčkové adresy | `http://MODIFY-THIS-URL/phpvpn.php` (`config_document.h:10`), `*.example`, `*.invalid`, `192.0.2.x` (TEST-NET-1), `127.0.0.1`, `localhost` | Záměrné zástupné hodnoty nebo lokální adresy. |
| Server Node.js a PHP | `phpvpn.js:6` („no npm install“), `readme.txt:34` (PHP bez Composeru) | Žádné externí závislosti při instalaci ani za běhu. |

---

## 9. Realizace: vyčlenění do globální konfigurace (2026-10-05)

Řešily se nálezy z kapitoly 5.1 kromě N13, N14 a N15, které podle zadání zůstaly beze změny. Funkční závislosti z kapitoly 5.2 se podle zadání neřešily. Změny nejsou commitnuté.

### 9.1 Globální konfigurace

- **`phpvpn.config.json`** v kořeni repozitáře je jediný zdroj počítačově specifických hodnot. Jiný soubor lze zvolit proměnnou `PHPVPN_CONFIG`.
  - Relativní cesty se vztahují ke kořeni repozitáře, `~` k domovskému adresáři.
  - Hodnota může být objekt `{"windows": …, "linux": …}`, takže stejný soubor slouží Windows hostiteli i synchronizovanému linuxovému hostiteli.
  - Prázdná cesta k nástroji znamená vyhledání v `PATH`.
  - Sekce: `tools` (php, phpCgi, nginx, node, graftCli), `testServer`, `linuxHost`, `hyperV`.
- **Čtecí pomocníci:**
  - `tools/phpvpn_config.py` pro Python, s CLI `get|path|tool` pro shellové skripty;
  - `tools/phpvpn-config.ps1` pro PowerShell.
- Distribuované artefakty (instalátory, serverové skripty, servery) konfiguraci nečtou.

### 9.2 Stav nálezů

| Nález | Stav | Provedené řešení |
|---|---|---|
| N01 | Vyřešeno | `start-test-server.ps1` bere nginx, php-cgi, php, adresář, web root, adresy a limit z konfigurace. Konfiguraci nginxu generuje ze šablony `tools/test-server/nginx.conf.template` do `work/test-server/conf` a spouští nginx s vlastním prefixem. Web root a buffer jsou relativní (`work/test-server/…`). Kontroluje obsazenost portů. |
| N02 | Vyřešeno | `stop-test-server.ps1` ukončí jen procesy zapsané startem (PID soubor, cesta k exe a značka v příkazové řádce). Ověřeno: uživatelův nginx na portu 80 a php-cgi na 9000 běží dál. |
| N03 | Vyřešeno | `deploy-test.ps1` bere výchozí hodnoty z `testServer`. Popis nasazení zapisuje do `work/test-server/server.json`. |
| N04 | Vyřešeno | `tests-integration/server.json` odstraněn z gitu (`git rm`). `conftest.py` čte cestu z `testServer.deploymentDescription`. |
| N05 | Vyřešeno | `test_wire.py` bere PHP z `tools.php` nebo z `PATH`. Bez PHP hlásí explicitní SKIP (ověřeno). |
| N06 | Vyřešeno | `test-status-variants.py` má `--php`, výchozí hodnotu bere z konfigurace nebo `PATH`. Totéž platí pro `test-info-storage.py`, `test_http_contention.py` a `test-python-server.py` (včetně `--node`). |
| N07, N08 | Vyřešeno | Pevné `C:`, `D:`, `nginx` a `phpvpn-buffer` nahradil obecný regex pro cestu s písmenem disku a vzory odvozené z popisu nasazení (buffer a web root v obou zápisech). |
| N09 | Vyřešeno | Příklady v `tests-integration/README.md` a `server-python.md` už neobsahují `D:/nginx` ani `C:/Program Files/nodejs`. README popisuje globální konfiguraci. |
| N10 | Vyřešeno v projektu | Konfigurace nginxu pro testy je verzovaná šablona s hodnotami z konfigurace (`listen 127.0.0.1:18080`, explicitní `client_max_body_size`). `NGINX_BODY_LIMIT` se čte z konfigurace. Soubor `D:\nginx\conf\nginx.conf` mimo repozitář se neměnil a projekt ho už nepoužívá. |
| N11 | Není potřeba | `D:\nginx\fastcgi-supervisor.ps1` je mimo repozitář a projekt ho nepoužívá. Testovací php-cgi spouští `start-test-server.ps1`. |
| N12 | Vyřešeno relativní cestou | `.vscode/settings.json`: `${workspaceFolder}/client`. Do globální konfigurace hodnotu vyčlenit nelze, protože VS Code čte jen svůj soubor. |
| N13–N15 | Neřešeno | Podle zadání. |
| N16 | Vyřešeno | `copydistributiontovm.ps1` čte `hyperV.vmName` a `hyperV.destinationDirectory`. Načtení hodnot je ověřené. Samotný přenos do VM se nespouštěl: vyžaduje UAC a zapisuje do uživatelovy VM. |
| N17 | Vyřešeno | `sync-to-vm.sh` bere hostitele, uživatele, klíč, known_hosts a vzdálený adresář z `linuxHost` a synchronizuje i konfiguraci a jejího čtecího pomocníka. Ověřeno skutečnou synchronizací na 172.30.199.240. |
| N18 | Vyřešeno | `run-wizard-checks.sh` bere relay Python z `linuxHost.serverPython`, jinak `python3`. Ověřeno během bez proměnné prostředí. |
| N19 | Vyřešeno | `graft start.bat`: pořadí `GRAFT_NODE`/`GRAFT_CLI`, pak konfigurace, pak `PATH`, nakonec `%ProgramFiles%\nodejs`. Pevná cesta `C:\…` odstraněna. |
| N20 | Netýká se konfigurace | Jde jen o dokumentaci předpokladů testů; ochranné kontroly ve skriptech existují. Bez změny. |

`tools.node` ukazuje na Node.js 24.21.0 v `work/node24-distribution-check`. Server deklaruje Node.js 24 LTS, systémový Node je v23.3.0.

### 9.3 Výsledky ověření

Logy jsou ve `work/verify-20261005/` (Windows) a `~/phpvpn/work/verify/` (Fedora VM).

| Oblast | Výsledek |
|---|---|
| Integrační sada proti privátnímu nginxu (PHP-FPM přes FastCGI) | 430 passed, 2 skipped (default profil, nenastalé 503), včetně pomalých testů |
| Serverové profily z repozitáře: PHP/disk, Python/disk, Python/memory, Node.js/disk | 431, 431, 427 (+5 skip pro diskové testy v memory) a 431 passed |
| `tests.php` | 546 asercí, všechny prošly |
| `tests-info-pretty-boundaries.php` | Prošlo |
| `tests-node` | 53/53 |
| `tests-python` | 189/189 |
| `test-status-variants.py` | PASS |
| `test-info-storage.py` | PASS |
| `test_http_contention.py` | PASS |
| `test_deploy_profiles.ps1` | PASS |
| `test_graft_launchers.ps1` | PASS |
| Python server skripty (init/start/stop) ze zdrojů | 30/30 (spuštěno s `-X utf8`, viz poznámka níže) |
| Windows klient release | CTest 10/10. C++ kontrakt proti PHP fixture 6/6. |
| Fedora 44 klient | linux-debug ctest 3/3, kontrakt PHP i Python, netns 12/12, service E2E 50/50, CLI smoke 28/28 |
| Fedora 44 GTK tray | tray fallback i sni, config 31 kontrol, info 7, statistiky 6, průvodce 14 + 2 + 2 (5 kontrol fokusu headless neprobíhá) |
| Linux balíčky v kontejnerech | Fedora 44 a Ubuntu 24.04, release 1 i 2: build, testy v balíčku a lint prošly |
| Distribuce | `tools/make-distribution.ps1 -SignBinaries` prošla včetně ZIPu a ověření rozbalené kopie. Kontrola dokumentace distribuce prošla. |
| Instalace podepsané linuxové složky v kontejneru (release 1 → 2) | Fedora 44 62/62, Ubuntu 24.04 62/62 |
| Servery z rozbalené distribuce | PHP: kontrakt 6/6 a 431 passed. Node.js: kontrakt 6/6 a 431 passed. Python: init/start/stop 30/30 na Windows i na Fedoře. |

### 9.4 Dokončení po commitu 1d0a399

Změny jsou commitnuté a pushnuté (`origin/linux`, commit `1d0a399`).

- **Linuxové balíčky:** release 1 a 2 se sestavily znovu z přesného `git archive` commitu, s provenance `sourceDirty=false`. Build, testy v balíčku a lint prošly na Fedora 44 i Ubuntu 24.04.
- **Finální distribuce:** `tools/make-distribution.ps1 -SignBinaries` prošla **bez** `-AllowLinuxProvenanceMismatch`.
  - `dist.zip`: SHA-256 `07BA2AE132184A16E38561100367B3BBE72C02E9E448AAADBF7D9A9F3F76142D`, 3 898 201 B.
- **Ověření finální distribuce:**
  - dokumentace 7/7;
  - instalace v kontejneru Fedora 44 62/62 a Ubuntu 24.04 62/62;
  - PHP z distribuce: kontrakt 6/6 a 431 passed;
  - Node.js z distribuce: kontrakt 6/6 a 431 passed;
  - Python z distribuce: 30/30 na Windows i na Fedoře.
- **Windows testy s povýšením:**
  - izolovaný SCM/Wintun E2E: 14 PASS, 0 FAIL; produkční služba, konfigurace i výchozí trasy beze změny;
  - resource testy (IPC RSS, process-local DNS): 2/2, hostitel beze změny.
- **Oprava kódování:** `tools/test-server-scripts.py` čte cestu z podprocesu jako UTF-8. Test teď prochází i bez `-X utf8` (30/30).
- **Přenos do Hyper-V VM:** `copydistributiontovm.ps1` načetl `hyperV.vmName` z konfigurace. `Copy-VMFile` ale odmítl přenos, protože VM „windows 11“ neběží. Přenos zbývá provést po spuštění VM.
  - Upozornění: `copydistributiontovm.bat` před přenosem přepíše ověřený `dist.zip` archivem celého `dist/`, včetně lokálního `.venv` a `.runtime` Python serveru.
- **Neprovedeno:** ruční grafická akceptace v uživatelské relaci. U průvodce na Linuxu headless režim nespouští 5 kontrol fokusu a SCM E2E nepokrývá dva uživatele, sleep/resume ani zpožděné DNS.
