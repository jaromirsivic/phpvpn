# Realizace a ověření distribučních skriptů Python serveru

Datum: 2026-09-25, Windows 11 Pro 10.0.22631 x64. Práce proběhla přímo ve větvi
`codex/python-server`, bez worktree a bez dalších agentů. Autoritativní
[intent](../Intents/2026-09-25-13-08-05/intent-v001.md) a
[plán](../Plans/2026-09-25-13-08-05/plan-v001.md) zůstaly beze změny.
Koordinační `.intent-builder-active` tento realizační úkol neupravoval.

## Výsledek

Implementováno všech šest vstupů, bootstrap uv/Python 3.13/venv/lock,
privátní řízení jediné instance, připravenost přes vlastní startup a ověřený
health, graceful stop a oba třísekundové bannery. Celkový sestavovací vstup
publikoval `C:\git\phpvpn\dist` a `C:\git\phpvpn\dist.zip`.

Distribuce má přesně 30 souborů a 9 podsložek. Python část má 11 souborů,
včetně šesti vstupů a dvou helperů. ZIP má 39 položek, obsahuje prázdnou
`server/Node.js/` a u všech tří `.sh` jsou uloženy executable atributy 0755.
Hash ZIP SHA-256:

```text
9F1A6E30F73ABEB567DD4EF769049EDA0C671E833A8E02A618CDD36B0847A960
```

Finální archiv byl rozbalen a ověřen v
`work/distribution-2374d6b3a78240869b451f118c5d490e/unpacked`.
Provozní testy kopírovaly právě jeho `server/Python` do izolovaného nasazení
`C:\Users\User\AppData\Local\Temp\phpvpn scripts vn0369un\Žluťoučký & (server)!`.
Porty i buffery patřily pouze těmto testům. Existující uživatelský server na
portu 8000 ani běžící Windows služba `phpvpn` nebyly zastaveny nebo upraveny.

## Změněné oblasti

| Soubory | Změna |
|---|---|
| `tools/server-python/init.bat`, `start.bat`, `stop.bat`, `init.sh`, `start.sh`, `stop.sh` | Šest distribuovaných uživatelských vstupů, cesty od umístění skriptu, bez aktivace venv. |
| `tools/server-python/bootstrap.ps1` | Windows bootstrap, souběžný init zámek, skutečný oficiální instalátor, explicitní interpreter a propagace chyb. |
| `tools/server-python/server-control.py` | OS zámky, privátní ACL/mode, atomický stav, PID + creation time + instance handshake, zrušitelný start, health, adresovaný stop a potvrzení lifespan i zániku procesu. |
| `phpvpn.py` | Sdílená `create_server`, jeden worker a zachovaný přímý start. |
| `requirements-server.in`, `requirements-server.txt` | Runtime psutil 7.2.2, univerzální lock přes uv pro Python 3.13. |
| `tools/make-distribution.ps1`, `distribution-common.ps1`, `verify-distribution.ps1` | Celkový build, explicitní allowlist, bezpečné cesty, klientský podpis, ZIP, kontrola rozbalení a publikace až po ověření. |
| `client/tools/make-dist.ps1`, `verify-package.ps1` | Dílčí packager vyžaduje bezpečný explicitní výstup, diagnostika odkazuje na celý distribuční vstup. |
| `tests-python/test_server_control.py`, `tools/test-server-scripts.py` | 27 deterministických testů controlleru a 30 skutečných provozních scénářů nad izolovanými kopiemi. |
| `distribution.md`, `server-python.md`, `client.md`, `.gitignore`, `.gitattributes` | Provozní a sestavovací návody, vyloučení provozního stavu, LF shellových vstupů. |

## Prostředí a příkazy

Vývojové prostředí `work/server-scripts-dev` vzniklo výhradně přes uv. Použitý
uv v PATH: 0.12.16. Oficiální bootstrap skutečně stáhl uv 0.12.19 a CPython
3.13.15. Běžné provozní a regresní testy používaly CPython 3.13.7.
Runtime lock obsahuje Uvicorn 0.54.0, cryptography 50.0.1 a psutil 7.2.2.
SHA-256 locku:

```text
CD0D45458CF9692A883B75AE2629EEC032623480E6C6A8DC9970FF9B1B96F603
```

Reprodukční příkazy z kořene repozitáře:

```powershell
uv venv --python 3.13 work/server-scripts-dev
uv pip sync --python work/server-scripts-dev/Scripts/python.exe requirements-server.txt
uv pip install --python work/server-scripts-dev/Scripts/python.exe pytest requests
./work/server-scripts-dev/Scripts/python.exe -B -m pytest tests-python -q
./work/server-scripts-dev/Scripts/python.exe -B tools/test-python-server.py --profiles python-disk python-memory --contract-executable work/build/release/tests/phpvpn-contract-tests.exe --report-dir work/server-scripts-protocol -- -q
./client/tools/build-x64.ps1 -Preset release -Test
./tools/make-distribution.ps1 -BuildDirectory ./work/build/release -SignBinaries
./tools/verify-distribution.ps1 -DistributionDirectory ./dist
./work/server-scripts-dev/Scripts/python.exe -B tools/test-server-scripts.py --distribution work/distribution-2374d6b3a78240869b451f118c5d490e/unpacked --report work/server-scripts-archive-runtime.json
graft build
```

## Výsledky

| Kontrola | Skutečný výsledek a rozsah |
|---|---|
| Python regrese | **177 passed**, včetně 27 nových testů controlleru. Log `work/server-scripts-unit-final.log`. |
| Protokol python-disk | **430 passed, 1 skipped**, včetně skutečných expirací. Skip: test pod zátěží nevyvolal HTTP 503. |
| Protokol python-memory | **426 passed, 5 skipped**. Stejný 503 skip a čtyři záměrně nerelevantní testy fyzického diskového layoutu. |
| C++/WinHTTP/CNG kontrakty | **5/5 pro disk a 5/5 pro memory**, skutečný klient proti izolovaným běžícím serverům. Logy a JUnit v `work/server-scripts-protocol*`. |
| C++ build a CTest | Release build aktuální. Osm běžných CTest targetů prošlo. První obecný CTest běh měl selhání kontraktového targetu, protože neměl připravený HTTP endpoint. Následný výše uvedený runner spustil tentýž kontraktový EXE s vlastní skutečnou fixture, oba profily prošly. Původní selhání není vykázáno jako úspěšný CTest běh. |
| Celková distribuce | Build, explicitní obsah, zdrojové hashe, LF, x64, podepsaný manifest, Authenticode, BAT VerifyOnly a produkční ICO resources prošly před i po rozbalení ZIPu. Vývojový podpis `CN=phpvpn Development Test Signing`, pin `7430CCA24F5DE8A6614D582AFBFED7C80E0F2CA8`, není veřejně důvěryhodným vydavatelským podpisem. |
| Čisté opakované sestavení | Záměrně přidaný `dist/obsolete-test-file.txt` po sestavení zmizel. Neočekávaný soubor v rozbaleném stromu ověřovač odmítl. |
| Selhání buildu | Neexistující podpisový certifikát vyvolal chybu před publikací. Hashový seznam dosavadního `dist` zůstal totožný. Dílčí packager odmítl kořenový `dist`, kontrola cest odmítla diskový kořen, kořen projektu i nadřazenou cestu. |
| Windows runtime z archivu | **30 scénářů PASS**, `cleanup_exit=0`. Úplný strojový report `work/server-scripts-archive-runtime.json`, log `work/server-scripts-archive-runtime.log`. Po běhu nebyl nalezen žádný testovací proces `server-control.py serve`. |
| Skutečný bootstrap download | Finální bootstrap v `work/bootstrap-final-5aa3e70aeb3d40c2bc9c563bfc9981e0` měl PATH pouze s Windows/PowerShell, vlastní prázdný Python install dir a uv cache. Stáhl oficiální uv a nový CPython 3.13.15, vytvořil venv a všech sedm zamčených balíčků. Není to nový OS účet ani čistá VM, tento limit důkazu se zachovává. |
| ACL | Skutečná ACL `.runtime` a `state.json` v izolovaném nasazení obsahovala pouze explicitní FullControl pro provozního uživatele, SYSTEM a Administrators. Test skutečně nezapisovatelné testovací složky vracel chybu initu a nevytvořil venv. |

### Vazba na akceptační scénáře plánu

| ID | Windows důkaz |
|---|---|
| A1 | Kompletní sestavení, hashová shoda, přesný strom, prázdný Node.js, README, podpis, opakovaný čistý build a ověření rozbaleného ZIPu. |
| A2 | Skutečné stahování v izolovaných adresářích bez dostupného uv/Pythonu z PATH, žádný mock instalátoru. Nový OS účet nebo čistá VM **NEPROVEDENO**. |
| A3 | Opakovaný init, privátní uv mimo PATH, skutečný cizí aktivní venv nezměněn, běžící server odmítnut, nekompatibilní venv zachován, nezapisovatelná složka odmítnuta, offline sync selhal bez úspěchu a následující init se zotavil. Násilné přerušení instalátoru během přenosu **NEPROVEDENO**. |
| A4 | BAT z jiné pracovní složky, cesta s mezerami, Unicode, `&`, `(`, `)` a `!`. Server odpovídal po skončení launcheru, skutečné aplikační PID bez dalších workerů i s WEB_CONCURRENCY=4. Ruční zavření viditelného okna terminálu **NEPROVEDENO**. |
| A5 | IPv4 wildcard, hostname, IPv6 loopback i wildcard, Unicode/mezery/percent znak v cestě, vypnutá status stránka, ignorování nefunkční HTTP_PROXY. Výchozí port 8000 nebyl přebírán cizí službě, výchozí URL kryje konstrukce URL a konfigurace. |
| A6 | Obsazený TCP port i jiný PHP VPN se stejným secret, neplatný port v konfiguraci, chybějící cryptography, timeout před publikací identity i před bindem. Pozdní potomek nebindoval a po doběhnutí nezůstal proces. |
| A7 | Dva starty, dva stopy, stop během startu, opakovaný start i stop. Jediná instance a odpovídající návraty. |
| A8 | Reálný cizí PID přežil podvržený stav, poškozený JSON a přenesený root odmítnuty. Jednotkové testy kryjí reuse PID/creation time, access denied, chybějící metadata při drženém zámku, symlink a souběžný atomický kanál. |
| A9 | Disk i memory, klidový stop i otevřený nedokončený data request, potvrzený lifespan a zánik původní identity. Reálný čtyřsekundový lifespan se zkráceným testovacím stop timeoutem vrátil chybu, server zůstal živý a následně se korektně ukončil, bez hard kill. |
| A10 | Čtení stdout po řádcích a monotónní čas od posledního rámečku do konce procesu. Zaznamenaná minima: start disk 3,0143 s, stop disk 3,0160 s, start memory 3,0156 s, stop memory 3,0156 s. Předem poslaný Enter prodlevu nezkrátil. Stop neměl URL. Ruční vizuální kontrola nativního okna **NEPROVEDENO**. |
| A11 | Python regrese, oba úplné protokolové profily, skutečný C++ klient, přímý start serveru v existujících procesních testech a distribuční VerifyOnly prošly. Produkční install/upgrade/uninstall a ruční klientské brány **NEPROVEDENO**, existující služba `phpvpn` běží a test by zasáhl uživatelovu instalaci. |

## Omezení a zbylý testovací stav

- **Linux: NEPROVEDENO. macOS: NEPROVEDENO.** Implementace obsahuje POSIX vstupy,
  ale tento Windows běh ani kontrola obsahu ZIPu nedokazují jejich nativní provoz.
  A2–A10 je nutné provést na odpovídajícím OS. Linux VM ani Mac nebyly dostupné.
- Čistý nový OS účet/VM a skutečné ruční zavření či vizuální kontrola terminálu
  nejsou nahrazeny provedenými izolovanými a monotónními testy.
- Plný instalační/upgrade/odinstalační průchod Windows klienta nebyl proveden
  na používané produkční službě. Stávající ruční brány popisuje
  [client/tests/e2e/manual-gates.md](../client/tests/e2e/manual-gates.md).
- První vývojová varianta bootstrapu dovolila uv zaregistrovat testovací CPython
  do `HKCU\Software\Python\Astral\CPython3.13.15`. Jeho InstallPath ukazuje pouze
  na `work/bootstrap-fresh-68458f44119643ee87888cbbe9e44a08/.tools/python/...`.
  Automatická kontrola odmítla odstranění tohoto ověřeného testovacího záznamu
  s obecnou zprávou **blocked by policy**. Odstranění nebylo obcházeno a záznam
  i cílové testovací soubory zůstaly zachovány. Finální oba bootstrapy používají
  `uv python install --no-bin --no-registry 3.13`. Další skutečný bootstrap
  prokazatelně nepřepsal dřívější registrační cestu ani systémové Python shimy.
- Pracovní stagingy, záloha předchozí distribuce, testovací venv a logy zůstávají
  pod `work`, izolované provozní kopie pod uživatelovým Temp. Nejsou součástí
  `dist` ani ZIPu. Všechny spravované testovací servery skončily.

Při realizaci byl obnoven Graft (`307` indexovaných souborů). `git diff --check`
prošel. Commit ani push nebyl součástí tohoto úkolu.
