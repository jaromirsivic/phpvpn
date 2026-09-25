---
plan_version: 1
created_at: 2026-09-25-13-24-00
timezone: Europe/Berlin
source: ../../Intents/2026-09-25-13-08-05/intent-v001.md
---

# Realizační plán spouštěcích skriptů Python serveru

## Zadání a hranice

Autoritativním zadáním je [intent v001](../../Intents/2026-09-25-13-08-05/intent-v001.md). Tento plán je úplná neměnná verze. Pozdější věcné upřesnění vytvoří celý `plan-v002.md`, nikoli přepis této verze. Plánování neopravňuje k implementaci. Intent ani jeho zdrojový chat se nemění.

Výsledkem realizace bude šest uživatelských vstupů `init.bat`, `start.bat`, `stop.bat`, `init.sh`, `start.sh`, `stop.sh` v každé nově sestavené složce `dist/server/Python`. Připraví Python 3.13 přes uv, spustí jednu instanci na pozadí a bezpečně ji zastaví. Úspěšné bannery budou anglicky, po startu s ověřenou URL a po stopu bez URL, vždy s nejméně třemi sekundami viditelnosti.

Práce proběhne přímo v aktivní feature větvi, při přípravě plánu `codex/python-server`, HEAD `87bd3a7`. Nevytvářet worktree ani další agenty. Existující změny `.intent-builder-active` a `Intents/2026-09-25-13-08-05/` patří koordinačnímu workflow. Plánovací kontext vlastní pouze tento plán.

Předaný absolutní termín tohoto plánovacího úkolu je 2026-09-25 15:36:39 Europe/Berlin. Čas se kontroluje během práce, neexistuje však nezávislý automatický watchdog, který by vynutil zastavení při zablokování nástroje. Tento dokument takové automatické vynucování neslibuje. Případná pozdější realizace musí mít vlastní platné pověření a časový rámec.

## Ověřený výchozí stav

- `distribution.md:19–69,86–99` předepisuje kompletní serverový a klientský strom, čisté sestavení, shodu zdrojů a oddělení provozních dat. Python část nyní předepisuje tři soubory. Prázdná `server/Node.js` je povinná, nepodporované klientské platformy mají README.
- `client/tools/make-dist.ps1:18–46,301–343` sestavuje pouze plochý Windows klientský balíček, standardně do kořenového `dist`. Čistí svůj výstup a vytváří klientský manifest. Sestavení serveru je potřeba skutečně doplnit, nestačí upravit dokumentaci nebo současnou kopii v `dist`.
- `phpvpn.py:107–145` validuje konfiguraci včetně portu v rozsahu 1–65535 a cesty. Port 0 není podporovaný a plán jej nezavádí. Konfigurace je v Python souboru, nikoli ve všeobecném kontraktu environment proměnných.
- `phpvpn.py:869–892,915–925` poskytuje status stránku a health akci. GET může vrátit 404 při vypnuté status stránce. Health odpověď obsahuje identifikátor požadavku a kryptograficky ověřitelný tag, proto samotná odpověď HTTP 200 není dostatečná kontrola připravenosti.
- `phpvpn.py:1066–1076` při ASGI shutdown čeká na executor. `phpvpn.py:1135–1140` spouští Uvicorn s jedním workerem, bez reloadu, s asyncio, h11, bez WebSocketů, access logu, server headeru a důvěry proxy hlavičkám.
- `tools/test-python-server.py:41–115` už má izolované testovací kopie a upozornění na pomocný Windows venv launcher. PID vrácené při spuštění interpretu tedy nemusí označovat samotnou aplikaci.
- `requirements-server.txt` je univerzální lock pro Python 3.13, aktuálně s Uvicorn 0.54.0. `server-python.md` popisuje uv, oba storage drivery, ruční start a regresní sady.

Kontext byl získán přes Graft map, ask, skeleton a grep, neindexované distribuční dokumenty a PowerShell byly přečteny přímo. Na tomto stroji není dostupný Linux ani macOS. Jejich běhové ověření při plánování neproběhlo a příprava VM není podmínkou tohoto plánu.

## Rozhodnutí o návrhu

### 1. Zdrojové soubory a společná logika

Šest skriptů uchovávat jako zdrojové šablony v novém `tools/server-python/`. Distribuční sestavení je kopíruje beze změny do `dist/server/Python`. Neupravovat generované kopie ručně.

Přidat podpůrný `server-control.py` pro společné řízení startu, stavu, ověření a stopu. Windows bootstrap může použít podpůrný `bootstrap.ps1`, protože před inicializací nemusí být Python dostupný. Oba podpůrné soubory zahrnout do distribučního seznamu. Intent požaduje šest pojmenovaných vstupů, nezakazuje pomocné distribuované soubory.

Skripty `.bat` používají absolutní cesty od `%~dp0`, vypnutou delayed expansion a zachovají návratový kód. Shellové vstupy budou POSIX `sh`, s LF, bez BOM a bez závislosti na GNU `readlink -f`, GNU `timeout`, Bash nebo `flock` příkazu. Cesty určují z umístění skriptu. Pokrýt mezery, Unicode a shellové metaznaky. Všechny běhové operace spouštějí místní `.venv/Scripts/python.exe` nebo `.venv/bin/python`, bez aktivace venv a bez systémového fallbacku.

Pro řízení procesní identity přidat runtime závislost `psutil` do `requirements-server.in` a pomocí uv obnovit univerzální lock pro Python 3.13. Použije se pro zjištění času vytvoření, kontroly existence konkrétního procesu a čekání na jeho konec, nikoli pro hromadné ukončování procesů. Přesnou kompatibilní verzi vybere a zamkne realizace. Je to vědomá cena za jednotné chování Windows, Linuxu a macOS bez parsování lokalizovaného výstupu systémových utilit.

### 2. Inicializace

1. Ověřit distribuované vstupy, zapisovatelnost nasazené složky a nepřítomnost běžící spravované instance. Inicializace nesmí synchronizovat závislosti živého serveru. Souběžné inicializace serializovat bootstrap zámkem s timeoutem. Nejasný zastaralý bootstrap zámek nemaže automaticky cizí běh.
2. Vyhledat funkční uv v PATH a v privátním bootstrap umístění. Existující uv automaticky neaktualizovat. Pokud není k dispozici, stáhnout oficiální `https://astral.sh/uv/install.ps1` na Windows nebo `https://astral.sh/uv/install.sh` na Linuxu a macOS. Stáhnout nejprve celý skript do dočasného souboru a ověřit úspěch přenosu, teprve potom jej spustit. Shell využije curl, případně wget. Chybí-li oba, vrátí jasnou chybu s požadavkem na jejich doplnění.
3. Windows použije PowerShell `-NoProfile` a jednorázovou execution policy pro proces, bez změny systémové policy. Instalátor nasměrovat přes `UV_INSTALL_DIR` do místního `.tools/uv` a použít `UV_NO_MODIFY_PATH=1`. Nově získaný uv volat absolutní cestou hned v téže relaci. Žádný požadavek na otevření nového terminálu ani na administrátorská práva.
4. Připravit CPython řady 3.13 přes `uv python install 3.13`, vytvořit `.venv` přes `uv venv --python 3.13 <absolutní .venv>` a synchronizovat `uv pip sync --python <absolutní venv interpret> <absolutní requirements-server.txt>`. Nepřebírat náhodné aktivní `VIRTUAL_ENV` či nadřazené prostředí. Nástrojové nastavení musí explicitně určit cílové cesty.
5. Existující funkční venv 3.13 zachovat a synchronizovat. Při nekompatibilní nebo poškozené `.venv` vrátit chybu a návod k obnově po zastavení serveru, bez tichého smazání uživatelského adresáře. Ověřit verzi interpretu, importy a shodu závislostí s lockem. Úspěch zapisovat až po všech kontrolách. Po přerušené synchronizaci lze init bezpečně zopakovat.

Instalační síťové operace dostanou timeout a omezené opakování. Selhání TLS, proxy, stahování, oprávnění nebo synchronizace musí zachovat nenulový návratový kód. Žádný pip, python -m pip ani druhý správce. Start a stop nic nestahují ani neinstalují.

### 3. Vlastnictví instance a souběh

`server-control.py` bude mít uživatelské příkazy start a stop a interní režim serve. Režim serve běží v jediném aplikačním procesu s Uvicornem. Nevzniká trvalý další supervisor ani další worker.

Provozní soubory ukládat do `.runtime` nasazené kopie. Použít samostatný krátkodobý operační zámek pro serializaci start/stop a zámek držený po celou dobu života serveru. OS zámky realizovat přes `msvcrt` na Windows a `fcntl` na POSIX. Soubor zámku se za běhu neodstraňuje ani nenahrazuje. Při pádu proces uvolní OS zámek automaticky. Pořadí zámků musí být jednotné a timeouty konečné.

Atomicky publikovaný stav obsahuje verzi schématu, skutečné PID ze serverového procesu, čas vytvoření procesu, kanonickou cestu nasazené kopie, náhodný identifikátor instance, její fázi a skutečnou URL. Neobsahuje SERVER_SECRET ani ACCESS_KEYS. Identitu procesu potvrzuje také jeho vlastní komunikační odpověď, samotné PID, název programu nebo otevřený port nestačí.

Řízení ukončení použije privátní souborový kanál v `.runtime`: atomicky zveřejněný požadavek stop s identifikátorem instance a jedinečným ID požadavku. Monitor ve stejném serverovém procesu přijme pouze požadavek pro svůj aktuální běh. Při zjištění nastaví `uvicorn.Server.should_exit` bezpečně v jeho event loopu. Server zapisuje potvrzení s odpovídajícími identifikátory. Nevzniká nová veřejná HTTP administrační cesta ani další naslouchající port.

Adresář musí být přístupný jen provoznímu uživateli a administrátorům. POSIX mode 0700, soubory 0600. Na Windows explicitně ověřit nebo nastavit odpovídající ACL, nepředpokládat, že je zděděná ACL bezpečná. Odmítnout symlink či reparse point v cestě provozního stavu. Stejný OS uživatel je hranicí důvěry, nejde o izolaci proti jeho vlastnímu škodlivému kódu.

Poškozený či nesouhlasící stav znamená chybu bez zásahu do procesu. Stará metadata se smějí odložit až po získání zámku a potvrzení, že původní proces již neexistuje. Živý neidentifikovatelný běh se automaticky nepřebírá. Kopie `.runtime` přenesená do jiného nasazení nesmí umožnit zastavit původní server.

### 4. Start, připravenost a URL

Start bez hotového prostředí skončí chybou s odkazem na init. Pod zámkem odmítne druhý start stejné instance, včetně souběžného startu, a nezobrazí nový úspěšný banner. Samostatné kopie mohou běžet na různých portech a s oddělenými daty.

Nový proces spustit přes Python subprocess bez shellu. POSIX použije samostatnou session, Windows odpojený proces bez viditelného okna. Standardní vstup bude DEVNULL, stdout a stderr souborový log v `.runtime`, nikoli roura či terminál startovacího skriptu. Po ukončení skriptu i zavření jeho terminálu musí proces přežít. Pomocný venv launcher se nezaměňuje za aplikační PID. Server sám publikuje svou identitu a start ji spáruje se svým náhodným startovacím ID.

Minimálně upravit startovací část `phpvpn.py` tak, aby sdílela vytvoření Uvicorn serveru mezi přímým startem a spravovaným režimem. Zachovat samostatný `python -B phpvpn.py`, jednu instanci a všechna nynější explicitní nastavení. Spravovaný režim přečte konfiguraci jednou a ze stejného validovaného objektu vytvoří aplikaci, bind i URL. Parsování konfigurace regulárním výrazem v shellu se nepoužije.

Připravenost nejdéle 30 sekund: vlastní proces musí být živý, identita potvrzená, Uvicorn musí dokončit bind a startup, validace konfigurace musí projít a skutečný HTTP health požadavek na zamýšlený endpoint musí vrátit odpovídající requestId, status a platný kryptografický tag. Kontrola HTTP ignoruje externí proxy z prostředí a nenásleduje přesměrování. Tajemství se nedostanou do argv, logu ani stavu. Stejný health mechanismus funguje při `STATUS_PAGE_ENABLED=False`. Kontrola startupu vlastní instance zabrání přijetí cizí služby na již obsazeném portu i v případě shodné konfigurace health.

URL používá HTTP, port ze skutečného bindu a správně zakódovanou cestu SERVER_PATH. Pro 0.0.0.0 použije 127.0.0.1, pro :: použije [::1]. IPv6 musí mít hranaté závorky, scope ID případně správné URL kódování. Při konkrétní bind adrese nebo hostname ověří právě zobrazovanou lokálně dosažitelnou variantu. Neodvozuje veřejnou adresu reverzní proxy. Výchozí URL zůstává `http://127.0.0.1:8000/phpvpn.php`. Při vypnuté status stránce je URL nadále API endpointem, dokumentace výslovně uvede očekávanou 404 při běžném otevření v prohlížeči. Status stránku skript svévolně nezapíná.

Při selhání startu se nespustí banner. Pokud vlastní proces přežil, start požádá jeho řídicí kanál o úklid a ověří výsledek. Při neúspěšném úklidu zachová stav pro stop a vypíše přesnou diagnostiku. Proces nesmí zůstat skrytě běžet bez informace uživateli.

### 5. Stop a výsledek operace

Stop pracuje výhradně s potvrzenou instancí vlastní nasazené kopie. Získá operační zámek, načte stav, spáruje PID a čas vytvoření a vydá adresovaný stop požadavek. Uvicorn dostane příležitost uzavřít spojení, dokončit či korektně zrušit rozpracované požadavky a vyvolat lifespan shutdown včetně čekání na storage executor.

Nastavit 30 sekund pro graceful shutdown HTTP a 45 sekund pro celkovou stop kontrolu. Při překročení druhého limitu vrátit chybu a ponechat dohledatelný stav. Automatický taskkill, kill -9 ani obecný fallback terminate se nepoužijí. Windows TerminateProcess se nesmí vydávat za korektní shutdown.

Úspěch vyžaduje potvrzení dokončeného shutdownu pro daný požadavek a skutečné skončení původní procesní identity. Ověřit také uvolnění zámku instance. Pouhé zmizení stavového souboru, uzavření portu nebo ACK o přijetí požadavku nestačí. psutil se při čekání používá s identitou PID + creation time a zohledněním již skončeného zombie procesu na POSIX. Nelze-li identitu ověřit pro nedostatečná oprávnění, jde o chybu, ne o důkaz skončení.

Při opakovaném stopu nebo prokazatelně již neběžící instanci vrátit 0 a stručné `Server is not running.` bez úspěšného stop banneru. Náhlý pád místo potvrzeného graceful dokončení se nepovažuje za úspěšné provedení stopu. Žádná z těchto větví nesmí signalizovat cizí proces. Opakovaný start vrátí nenulový kód a `Server is already running.`.

### 6. Bannery

Oba bannery vytváří stejná Python funkce s ASCII rámečkem. Start obsahuje `Server started successfully.` a samostatný řádek `Open in your browser: <ověřená URL>`. Stop obsahuje `Server stopped successfully.` a žádnou URL. Šířka rámečku se přizpůsobí obsahu.

Po vytištění posledního řádku flushnout stdout a měřit nejméně 3,0 sekundy monotónním časem. Čekání nesmí ukončit běžný stisk klávesy. Skript po banneru nevypisuje další běžné hlášky. Zámek operačního příkazu držet i během této doby, aby souběžný stop či start okamžitě nezneplatnil výsledek. Vnější zavření okna nebo násilné ukončení skriptu nelze garantovaně zablokovat, třísekundová podmínka platí pro normální dokončení skriptu.

## Napojení na distribuci

Přidat kanonický celkový vstup `tools/make-distribution.ps1`. Bude přijímat a předávat explicitní build adresář a podpisové parametry klientského balíčku. Zdrojové soubory vezme z aktuálního checkoutu včetně zamýšlených necommitnutých změn. Sestavení klientských binárek zůstane předpokladem podle stávající metodiky, obal nesmí tiše použít zastaralé binárky.

Obal připraví nový prázdný staging strom mimo výsledný `dist`, zkopíruje PHP, Python a šest vstupů s helpery podle explicitního seznamu, vytvoří prázdnou Node.js složku a klientské platformní složky. Existující `client/tools/make-dist.ps1` zavolá s `-OutputDirectory <staging>/client/Windows/x64`. Manifest stále pokrývá pouze konečný klientský balíček. Serverové soubory se nevkládají do klientského podepsaného manifestu.

U klientského nástroje odstranit nebezpečný implicitní plochý výstup do kořenového dist, například vyžadováním explicitního OutputDirectory. Přímé klientské sestavení jasně označit jako dílčí. Aktualizovat jeho dokumentované volání a diagnostiku ověřovače tak, aby bylo zřejmé, který příkaz vytváří úplnou distribuci. Ověřit existující volající před změnou kontraktu parametru.

Před jakýmkoli rekurzivním mazáním nebo přesunem ověřit absolutní cíle a jejich příslušnost ke zvolenému stagingu nebo kořenovému dist. Odmítnout reparse point, kořen projektu, diskový kořen a nadřazené cesty. Nový strom ověřit před nahrazením původního, při chybě ponechat původní distribuci použitelnou a neoznačit staging za hotový.

Balit pouze explicitně povolené soubory. Nevkládat `.venv`, `.tools`, `.runtime`, uv cache, bytecode, logy, PID/stav/zámky, lokální konfigurace, klíče, testovací data ani fronty. PHP, Python, lock, návod i pomocné skripty musí hashově odpovídat zdrojům. Negativní kontrola odhalí i skryté provozní soubory.

Při tvorbě ZIP vytvořit archiv z ověřeného stagingu, včetně prázdné `server/Node.js`, s požadovanou strukturou a bez samotného archivu uvnitř stromu. U shellových souborů zachovat LF a pokud to archivátor podporuje také executable bit. V návodu garantovat i přenositelné spuštění `sh init.sh`, `sh start.sh`, `sh stop.sh`, protože Windows rozbalovač nemusí přenést POSIX oprávnění. Archiv rozbalit do nové složky a znovu ověřit obsah a provoz právě z rozbalené kopie.

## Pořadí realizace a dotčené oblasti

| Krok | Změna | Podmínka dokončení |
|---|---|---|
| 1 | Znovu ověřit aktivní větev, pravidla, intent, dostupnost uv, aktuální graf a volající make-dist. Zapsat výchozí stav. | Žádné přepsané cizí změny, potvrzené zdrojové cesty. |
| 2 | Vymezit stavový formát a zámky v `tools/server-python/server-control.py`. Přidat psutil přes `requirements-server.in/.txt`. | Test identity, falešného PID, souběhu a poškozeného stavu. |
| 3 | Sdílená konstrukce Uvicornu v `phpvpn.py`, interní serve, startup handshake, health kontrola, adresované graceful ukončení. | Windows skutečný proces přežije launcher a terminál, stop dokončí lifespan. |
| 4 | Šest vstupních skriptů, bootstrap PowerShell, init přes uv, bannery. | Čistý a opakovaný init, přenos chyb a měření tří sekund. |
| 5 | Celkový distribuční obal a ověřovač, bezpečné výstupní cesty, ZIP. | Čistě sestavený a rozbalený artefakt obsahuje úplný explicitní seznam. |
| 6 | Aktualizace `distribution.md` a `server-python.md`, případně odkazů volajících klientský packager a `.gitignore` pro nové lokální provozní cesty. | Návod odpovídá přesným příkazům a balíčku. |
| 7 | Automatické a skutečné platformní ověření, datovaný validační report. Po větších změnách `graft build`. | Každé kritérium má konkrétní důkaz nebo výslovně uvedené neprovedení. |

Pro nové ověření použít `tests-python/test_server_control.py` a samostatný integrační runner `tools/test-server-scripts.py`. Distribuční ověřovač navrhnout jako `tools/verify-distribution.ps1`, který volá stávající `client/tools/verify-package.ps1` na Windows podbalíčku. Nepřidávat testovací závislosti do runtime locku kromě výslovně zvoleného psutil používaného produkčním helperem.

## Validace a akceptační kontroly

Testy spouštět na izolovaných kopiích rozbalené distribuce, s vlastním portem a bufferem. Netestovat start/stop proti uživatelově živé instanci. Stávající testovací konfigurace se nepřepisují. Veškeré instalace testovacích nástrojů probíhají přes uv do odděleného vývojového prostředí.

| ID | Scénář a kontrola | Platformy | Kritérium intentu |
|---|---|---|---|
| A1 | Sestavení od prázdného stagingu, všech šest jmen i helpery, hashová shoda, Node.js složka, klientské README, manifest a podpis, žádný venv/cache/stav/tajemství. Opakované sestavení odstraní starý přebytečný soubor. Kontrola ZIP po rozbalení. | Windows build, kontrola rozbalení všech OS | 1, 6 |
| A2 | Nový OS uživatel nebo izolovaný čistý stroj bez uv a Pythonu. Opravdu stáhnout správný oficiální instalátor, dokončit init v témže terminálu, prokázat Python 3.13 a zamčené balíčky. | Windows, Linux, macOS | 2 |
| A3 | Opakovaný init, existující uv mimo standardní cestu, aktivní cizí venv, nekompatibilní venv, běžící server, síťový výpadek, nepřístupná složka a přerušená instalace. Cizí prostředí zůstane nedotčené. | Všechny | 2, 5 |
| A4 | Start z jiné pracovní složky a cest s mezerami, Unicode, &, závorkami a !. Po návratu startu a zavření jeho terminálu stále odpovídá health. Jedna aplikační instance i s WEB_CONCURRENCY=4. | Všechny, zvlášť Windows venv redirector | 3 |
| A5 | Výchozí URL, změněný port a cesta, IPv4 wildcard, IPv6 wildcard/loopback, hostname. Probe využívá skutečně vypsanou URL. Vypnutá status stránka stále dovoluje připravenost přes health. | Všechny, IPv6 dle dostupnosti | 3 |
| A6 | Obsazený port cizí HTTP službou i jiným PHP VPN serverem, neplatná konfigurace, chybějící závislost, pád před bindem, timeout readiness. Nenulový návrat, žádný úspěšný banner, žádné nezaznamenané osiřelé procesy. | Všechny | 3, 5 |
| A7 | Dva souběžné starty, stop během startu, dva stopy, druhý start a stop bez běhu. Konzistentní návraty a jediná instance. | Všechny | 4 |
| A8 | Stará PID metadata, PID cizího živého procesu, nesouhlas creation time, přenesená runtime složka, poškozený JSON, chybějící stav při drženém zámku, access denied. Cizí proces musí prokazatelně přežít. Reuse PID pokrýt deterministickým testem identity i skutečným cizím procesem. | Všechny | 4, 5 |
| A9 | Stop při klidném serveru i aktivním data/poll přenosu. Prokázat dokončení lifespan, konec původního procesu a uvolnění zámku. Zaseknutý shutdown vrací chybu bez úspěšného banneru a bez hard kill. | Všechny, disk i memory | 4, 5 |
| A10 | Zachytit okamžik posledního vypsaného řádku banneru a konec procesu skriptu pomocí monotónního měření. Rozdíl nejméně 3,0 s. Start má samostatný URL řádek, stop žádnou URL. Klávesa čekání nezkrátí. Ruční kontrola skutečného okna Windows a terminálu POSIX. | Všechny | 3, 4 |
| A11 | Regrese `tests-python`, integrační profily python-disk a python-memory, health/info/data distribuovaného serveru, přímý start phpvpn.py. Klientský verify-package a další předepsané distribuční kontroly podle distribution.md. | Windows nyní, ostatní na odpovídajícím hostu | 6 |

Na aktuálním Windows hostu lze při realizaci provést Windows scénáře a přenositelné jednotkové testy. Skutečně čistý bootstrap ověřit pod odděleným účtem nebo ve vhodné izolaci, neodinstalovávat uživatelův uv ani Python. Mock instalátoru nenahrazuje A2.

Linux a macOS musí být v reportu označeny jako **NEPROVEDENO**, dokud neproběhnou na skutečném odpovídajícím OS nebo CI runneru. WSL, Git Bash ani parsování shellového skriptu neověřují macOS. Linux VM lze připravit později, v této plánovací fázi ji nevyžadovat. Pro macOS naplánovat dostupný Mac nebo macOS CI runner. Report zaznamená verzi OS, architekturu, Python, uv, lock, příkazy a výsledky. Před deklarací úplného splnění napříč platformami musí A2–A10 projít na všech třech OS. Nedostupná platforma neblokuje sepsání plánu ani nezávislou implementační práci, omezuje tvrzení o dokončené validaci.

## Dokumentace a rizika

`server-python.md` doplní předpoklady PowerShell či POSIX shell + curl/wget, síť při init, tři příkazy pro každou platformu, životnost procesu, umístění logu a provozního stavu, návratové kódy, řešení chyb, konfiguraci URL, vypnutou status stránku a ruční přímý start. Uvede, že přesun nasazené kopie vyžaduje stop a obnovu venv, server není automaticky službou po restartu OS a veřejnou HTTPS URL určuje samostatně proxy.

`distribution.md` doplní všech šest souborů, helpery, nový kanonický sestavovací příkaz a rozšířené ověření. Zachová pravidla klientského podpisu a povinné struktury. Vývojový podpis se nevydává za veřejně důvěryhodný vydavatelský podpis.

| Riziko | Opatření a důkaz |
|---|---|
| PID launcheru nebo opakovaně přidělené PID | Identita zveřejněná vlastním serve procesem, creation time, identifikátor instance, adresovaný kanál. Žádné ukončení jen podle čísla. A4, A8. |
| Falešná připravenost cizího serveru | Vlastní bind/startup handshake a kryptograficky ověřený health s novým requestId. A6. |
| Zablokovaný storage executor | Konečný timeout stop příkazu, poctivé selhání bez hard kill a zachovaný stav. A9. |
| Změna interních detailů Uvicornu | Malý adaptační bod, připnutý lock, test startupu a graceful shutdown při každé aktualizaci. |
| Shellové rozdíly a quoting | POSIX sh bez GNU zvláštností, argumenty bez shellové interpolace, test cest na všech OS. A4. |
| Nedostatečná ACL nebo podvržený stav | Privátní runtime, odmítnutí symlink/reparse cest, atomické soubory a validace schématu i instance. A8. |
| Částečný build nebo nechtěné smazání | Validované absolutní cíle, nový staging, ověření před publikací, explicitní seznam souborů. A1. |
| Chybějící platformní prostředí | Otevřeně neprovedené kontroly a pozdější skutečné OS běhy, žádné tvrzení o úspěchu ze statické kontroly. |

## Technické zdroje

Externí zdroje ověřeny při plánování 2026-09-25. Konkrétní realizace musí respektovat verze distribuovaného locku.

- [Astral: instalace uv](https://docs.astral.sh/uv/getting-started/installation/) potvrzuje oficiální PowerShell a shell instalátory.
- [Astral: možnosti instalátoru](https://docs.astral.sh/uv/reference/installer/) popisuje UV_INSTALL_DIR a UV_NO_MODIFY_PATH.
- [Astral: prostředí](https://docs.astral.sh/uv/pip/environments/) popisuje venv a explicitní výběr interpretu.
- [Python 3.13: subprocess](https://docs.python.org/3.13/library/subprocess.html) je podklad pro oddělení procesu a standardních streamů.
- [Uvicorn 0.54.0: server.py](https://raw.githubusercontent.com/Kludex/uvicorn/0.54.0/uvicorn/server.py) potvrzuje should_exit, stav started a návaznost graceful shutdown na lifespan. Návrh adaptačního bodu je rozhodnutí tohoto plánu.
- [psutil: procesní API](https://psutil.readthedocs.io/stable/) je podklad pro kontrolu identity procesu a jeho životnosti.

## Předání

Plán je dokončený návrh k realizaci, nikoli záznam provedených implementačních testů. V této fázi nebyly vytvořeny provozní skripty, spuštěn server, měněny závislosti ani sestavena distribuce. Implementace začne až po výslovné volbě uživatele realizovat dokončený plán. Nový realizační kontext vyžaduje potvrzený model a úsilí podle workflow intent-builder.
