---
plan_version: 1
created_at: 2026-09-29-22-15-00
source: ../../../Intents/2026-09-29-22-07-41/intent-v001.md
---

# Realizační plán serveru PHPVPN pro Node.js

## 1. Cíl, autorita a stav ověření

Vytvořit samostatný `phpvpn.js`, který poskytuje celý aktuální protokol PHPVPN v1 a spolupracuje s nezměněným produkčním C++ klientem. Jediný backend je disk. Nasazení vyžaduje jeden JavaScriptový soubor a Node.js, nikoli npm instalaci, build aplikace ani kopírování assets. Provozní data a případná externí konfigurace nejsou dalšími aplikačními moduly.

Autoritativní zadání: `C:/git/phpvpn/Intents/2026-09-29-22-07-41/intent-v001.md`. Normativní wire kontrakt: `C:/git/phpvpn/protocol.md`. PHP a Python implementace jsou reference, ne náhrada specifikace. Plán vychází ze stavu větve `linux`, HEAD `1b848abe65c5f8d4b480b389d41415d619af6cea`. Změny intentu a `.intent-builder-active` patří koordinátorovi.

Tento dokument je první úplná neměnná verze plánu. V plánovací fázi byl přečten celý intent, prozkoumán protokol, diskové implementace, společný testovací runner a skutečný klientský kontrakt. Nebyl vytvořen server ani spuštěn test klienta proti Node.js. Runtime interoperabilita je povinný výstup realizace, nikoli již dosažený výsledek plánování.

Pracovat přímo v aktivní feature větvi, bez worktree a dalších agentů. Neměnit cizí rozpracované soubory. Společný deadline plánovacího stromu je 2026-09-30 00:37:41 Europe/Berlin, sledovaný ručně. Tento termín neurčuje automaticky časový rozpočet později autorizované realizace.

## 2. Zjištění rozhodující pro rozsah

| Oblast | Ověřený zdroj a dopad |
|---|---|
| HTTP a kryptografie | `protocol.md:87–378`, `protocol.md:379–551`. Zachovat `/phpvpn.php`, POST akcí, přesné bajty, domény, oddělovače a délky tagů. Název JavaScriptového souboru nemění veřejnou URL. |
| Rozšířené info | `protocol.md:308–348`, `protocol.md:608–625`, `protocol.md:680`. Současný Windows Connect obsahuje full-auth publikaci maskovaných kontraktů. Rozšíření proto patří do první verze i přes označení „volitelné“ u formátu. |
| Disk a chyby | `protocol.md:554–666`, `protocol.md:683–717`. Atomické cílové skupiny, jednotlivé claimy, kapacitní sweep a at-most-once ztráta po claimu. Kapacita není HTTP 429. |
| Implementační reference | `phpvpn.php:1417–1820` obsahuje DiskStorage včetně markeru, publikace kontraktů a rename. `phpvpn.py:571–821` nabízí odlišný interní diskový formát. Ani jeden formát se nebude importovat nebo migrovat. |
| Skutečný transport | `client/src/platform/windows/transport_winhttp.cpp:113–393` tvoří POST z nakonfigurované cesty a `?action=`, zachovává tělo a používá celkový deadline. Proxy nesmí tělo transformovat ani zavádět přesměrování. |
| Klientské klíče | `client/src/core/protocol/request_builder.cpp:51–71`, `client/src/core/crypto/canonical.cpp:17–26`, `client/src/core/crypto/key_derivation.cpp:51–66`. Ověřené klientské sestavení semi HMAC a full-auth derivace odpovídá specifikaci. |
| Reálný C++ kontrakt | `client/tests/contract/test_server_contract.cpp:94–299`. Používá produkční CryptoProvider, Transport, ProbeClient a NetworkWorker. Test na řádcích 216–264 přenáší skutečně šifrovaný IPv4 paket z `.2` na `.3`, ověřuje výsledný obsah i vyprázdnění fronty. Řádky 265–299 testují publikaci, čtení a odvolání kontraktů. |
| Omezení kontraktního runneru | `client/tests/contract/test_server_contract.cpp:38–55` připouští jen lokální HTTP `127.0.0.1` s cestou `/phpvpn.php`. Neprokazuje HTTPS, skutečný virtuální adaptér ani celý GUI Connect. |
| Společná fixture | `tools/test-python-server.py:41–115,118–181`, `tests-integration/phpvpn_client.py:540–569`. Runner už vlastní izolovaný port, proces, kopii serveru a disk. Dnes má pouze profily php-disk, python-disk, python-memory. Přidat node-disk explicitní větví, nikoli ponechat Node v dosavadní větvi „else = Python“. |

Specifikace obsahuje starší testovací formulace, které bez kontextu odporují novějšímu rozšíření. Například §16.5 vyžaduje prázdný marker a nečtení obsahu, zatímco §10.6 výslovně zavádí potvrzenou JSON publikaci kontraktů. Výklad pro implementaci: běžná aktivita vytváří prázdný marker a touch obsah zachovává, standardní info čte jen metadata, rozšířené info čte obsah i mtime z jednoho handle. Specifická pravidla rozšíření platí pro rozšířený tok. Zachytit tento rozdíl regresním testem, neupravovat protokol za účelem oslabení požadavků.

## 3. Technická rozhodnutí

### 3.1 Balení, runtime a konfigurace

- Jeden ručně čitelný soubor `phpvpn.js`, CommonJS, vestavěné `node:http`, `node:crypto`, `node:fs`, `node:path` a další potřebné vestavěné moduly. Při importu pro testy neotvírat listener. Oddělit logické sekce konfigurace, parserů, kryptografie, DiskStorage, handlerů a HTTP adaptéru.
- Podporovaná základna Node.js 24 LTS, nejnovější opravené vydání této řady při realizaci. Řada 24 je ověřena jako LTS v [oficiálním přehledu](https://nodejs.org/en/about/previous-releases). Lokální `node --version` nyní vrací `v23.3.0`, proto se současný runtime nesmí považovat za přejímací prostředí. Podporu dalších major verzí netvrdit bez testu.
- Podporovat Windows a Linux na lokálním filesystému s atomickým rename v rámci jednoho adresáře. Sdílený NFS/SMB a současný přístup PHP/Python do stejného rootu nejsou ověřený provozní režim.
- Konfigurace má přehlednou hlavičku ve skriptu. Volitelný `--config <json>` umožní fixture i produkci oddělit konfiguraci bez změny serveru. Pořadí je výchozí hlavička, potom explicitní JSON. Neznámé volby odmítnout, nepřijímat tajemství jako argument příkazové řádky. Externí JSON není povinný pro nasazení.
- Nastavit listen host a port, `SERVER_SECRET`, mapu `ACCESS_KEYS`, VPN rozsah, `BUFFER_DIRECTORY`, status page a aktivní limity. Výchozí bind `127.0.0.1`, port 8080, veřejná cesta `/phpvpn.php`. Přímý vzdálený HTTP provoz vyžaduje explicitní bind. HTTPS ukončuje dokumentovaná reverzní proxy.
- Výchozí diskový root `./phpvpn_data` vyhodnocovat vůči souboru `phpvpn.js`, nikoli aktuálnímu pracovnímu adresáři. Použít nový prázdný root, nevytvářet kompatibilní import PHP dat. Kořen a klientské adresáře vznikají líně při způsobilé storage operaci.
- Normalizovat výhradně nulovou délku tajemství na `CHANGE_THIS_SERVER_SECRET`. Unicode převést na přesné UTF-8 bajty, netrimovat a implicitně nedekódovat hex. JSON konfigurace dovolí i NUL přes escape. ACCESS_KEYS jsou přesně 64 lowercase hex znaků. Payload klíče relay vůbec nepřijímá.
- Volba storage je pevná disková. Nezavádět registr backendů, memory driver, Redis ani Memcached. Pracovní Buffery požadavku, lokální seznam převzatých odpovědí a čekatelé na zámek jsou přechodné objekty, nikoli perzistentní fronta.

### 3.2 Wire, kryptografie a limity

- HTTP vrstva přijímá omezený Buffer přesných bajtů. Limit kontroluje při příjmu i při chybějícím Content-Length. UTF-8 dekódovat striktně, bez náhrady vadných sekvencí. Nedekomprimovat ani neserializovat přijaté tělo před HMAC.
- JSON parser musí detekovat duplicitní dekódované názvy na všech úrovních rozšířeného info, včetně `src` a `\u0073rc`. Samotné JSON.parse nestačí. Omezit hloubku podle povoleného schématu, odmítat neznámá pole, nesprávné typy, BOM a trailing obsah.
- Datový řádek musí přesně odpovídat kanonickému pořadí členů a bezescape ASCII formátu. NDJSON má LF i na konci, žádný CRLF ani prázdný řádek. Hashovat přesný zbytek za prvním LF. Celou syntaxi, počet a autentizaci ověřit před jakýmkoli zápisem nebo drainem.
- Base64url ověřit abecedou, délkou a decode–reencode shodou. Native Buffer dekodér je pouze převodník, nikoli validátor. UUID kontrolovat včetně verze a varianty. Kalendář ověřit explicitně, nespoléhat na tolerantní Date.parse. Časy pracují v celých milisekundách.
- Použít SHA-256, HMAC, HKDF a AES-256-GCM z node:crypto, CSPRNG a timingSafeEqual po validaci délky. GMAC health má prázdný plaintext. Zachovat oddělení všech domén, NUL a LF podle §7–9. Požadavek čistého PHP AES se na Node nevztahuje.
- Neznámé src zpracovat s pevným dummy 32bajtovým klíčem a obecným 401. Nezavádět nonce, historii requestId, deduplikaci messageId mezi requesty, session ani payload dešifrování.
- Výchozí limity: data 16 záznamů, auth řádek 4096 B včetně LF, tělo 983040 B, rozšířené info 65536 B a nejvýše limit těla, 252 kontraktů a zároveň velikost rozsahu minus self, plaintext info 262144 B, fronta 16777216 B, TTL 60 s, skew ±60000 ms, lock 200 ms, doporučený poll 50–3200 ms, info temp TTL 300 s. Počítat bajty, nikoli JS string.length.
- Výsledná info serializace má čtyřmezerné odsazení odpovídající současnému kontraktu. Numericky seřadit klienty, při překročení skutečných UTF-8 bajtů ubírat celé položky od nejvyšší IP a znovu serializovat. Pokud se nevejde základní objekt, 503 server_misconfigured. Aktivní hodnoty limitů zveřejnit, nezavádět zvláštní 16řádkový limit odpovědi.

### 3.3 Disk, souběh a restart

- Zachovat srozumitelnou strukturu adresářů `a_b_c_d`, stavové přípony `.receiving`, `.data`, `.sending` a normativní jméno s jediným časem expirace. Interní hlavička dávky bude verzovaná `phpvpn-node-1`, následovaná původními datovými řádky. Obsahuje logickou délku a uspořádané dvojice messageId/tag, které se při čtení ověří proti řádkům. Neimportovat PHP/Python hlavičky.
- Publikaci celé skupiny dst chránit per-client diskovým zámkem, který funguje i mezi Node procesy. Základní implementace bez nativních závislostí: atomické mkdir dedikovaného lock adresáře, jediný vlastník, uvolnění v finally. Čekání je asynchronní s monotónním deadlinem 200 ms, po vypršení 503 queue_busy. Jiné I/O selhání znamená queue_unavailable. Nikdy nedržet více cílových zámků současně a nedržet je během přenosu odpovědi.
- Neodstraňovat cizí lock podle stáří, TTL dávky ani samotného PID. Pád vlastníka může zanechat osiřelý lock. Dokumentovat konzervativní obnovu: zastavit všechny procesy používající root, ověřit zastavení, odstranit pouze lock adresáře tohoto formátu a spustit server. Běžný restart po korektním shutdownu zásah nepotřebuje. Automatické kradení locku a distribuované lease nejsou skrytou součástí první verze. Tento provozní kompromis musí být výslovně popsán a otestován v kroku 3.
- V zámku spočítat logické neexpirované nepřevzaté bajty, provést právě jeden kapacitní sweep a jeden další pokus. Kapacita, nedostupnost a busy jsou odlišné výsledky. Odložená skupina nezruší jinou přijatou skupinu. Změny již publikovaných skupin nevracet při pozdější chybě, klient odstraňuje své položky pouze po platném výsledku a MAC.
- Zápis je exclusive create, write-all s kontrolou krátkých zápisů, flush/sync a zavření, pak rename ve stejném adresáři. Čas expirace určit těsně při publikaci, nikoli při začátku příjmu požadavku. Chyba nesmí publikovat část skupiny. Garance pokrývá pád procesu, nikoli libovolné selhání disku nebo napájení bez odpovídajících garancí filesystému.
- Drain má jeden počáteční snapshot kandidátů a každý `.data` atomicky přejmenuje na `.sending`. Konflikt s jiným příjemcem přeskočí, skutečné I/O selhání nerozpozná jako prázdnou frontu. Po claimu není rollback ani po odpojení klienta. Validované řádky převést do omezeného lokálního seznamu a odstranit převzaté soubory před odesláním odpovědi. Zbývající `.sending` po pádu už nikdy nevydat, uklidit při expiraci. Nové dávky mimo snapshot zůstávají dalšímu pollu.
- Fronta na disku je autoritativní i po restartu. Nepoužívat paměťové počítadlo jako jediný zdroj kapacity ani in-memory seznam čekajících dávek. Marker a interní hlavička nejsou logická data, `.sending` už není nevydaná kapacita. Zbytky `.receiving` nejsou publikované a po pádu je bezpečně uklidit v offline obnově nebo při TTL, nikdy z nich nevytvářet `.data`.
- Bounded oportunistický sweep má rozpočet času, adresářů a souborů. Cílený kapacitní sweep se omezuje na dané dst a odstraní všechny expirované dávky všech tří stavů. Markery ani locky nesmí zaměnit za dávky. Kontrolovat symlinky/reparse body a držet root pod účtem serveru, bez veřejného zápisu.
- Paměť odpovědi se může dočasně blížit kapacitě fronty na aktivní request. Respektovat HTTP backpressure, nepoužívat opakované kvadratické Buffer.concat a při odpojení uvolnit lokální data. Změřit souběžné velké odpovědi a popsat provozní rozpočet. Nestránkovat protokolovou odpověď kvůli úspoře RAM.

### 3.4 Statistiky a rozšířené info

- Po úspěšném data provést nejvýše jedno čtení metadata a jeden best-effort touch pro chybějící/nečitelný marker nebo stáří nad 30 s. Přesně 30 s a budoucí mtime se nemění. Bez zámku, retry, čekání a vlivu na výsledek dat.
- Při vytváření markeru nikdy netruncovat současně publikovaný JSON. Použít bezpečné otevření bez truncation a změnu mtime. Standardní info čte pouze metadata nakonfigurovaných adres a nezakládá markery ostatních klientů.
- Přítomnost kteréhokoli rozšířeného pole včetně false a [] vyžaduje full-auth. Kanonizovat jen přítomná pole v předepsaném pořadí, zachovat pořadí pole kontraktů. Server maskovaný hash přijímá a validuje, nepotřebuje ani nedostává komunikační klíč.
- Potvrzenou publikaci zapsat do jedinečného `.info-<uuid>.tmp`, kontrolovat celý zápis, flush, nastavit mtime a atomicky nahradit info.json bez předchozího unlink. Selhání vrací 503 queue_unavailable a zachová starý marker. Explicitní [] smaže seznam logicky. Publikace obchází throttle.
- Extended snapshot čte obsah a mtime z jednoho handle. Při vadném obsahu použije [] a platný lastSeen, nevyzradí detaily chyby. Každý cizí klient vrací nejvýše kontrakt pro src žadatele, vlastní záznam [], žádné počty skrytých vztahů. Include-only požadavek není data poll a nesmí drainovat frontu. Vygenerovat generatedAt až po publikaci či příslušné aktualizaci.

## 4. Pořadí realizace a dotčené soubory

| Krok | Práce a soubory | Výstup a podmínka dokončení |
|---|---|---|
| 1. Inventura a prostředí | Znovu ověřit AGENTS, intent, protocol, stav větve a změny od uvedeného HEAD. Připravit Node 24 a C++ build nástroje. Nové testy `tests-node/*.test.js`, bez npm závislostí. | Matice požadavků §16/17 propojená s existujícími a chybějícími testy. Ověřený podporovaný runtime, žádné změny cizí práce. |
| 2. Základ serveru | Nový `phpvpn.js`: konfigurace, router, embedded status HTML z `assets/index.html`, striktní parser, validátory, crypto a health. | Known-answer vektory serverových funkcí, bad-input testy a veřejná stránka včetně disabled režimu. `node --check` projde, import nespouští server. |
| 3. Disková sémantika | Ve stejném souboru DiskStorage, zámky, staging, claim, restart, sweep, marker. `tests-node/storage.test.js`, `tests-node/faults.test.js`, procesové testy souběhu. | Dva procesy sdílející izolovaný root nepřekročí kapacitu ani nevydají dávku dvakrát. Pád a offline obnova locku zachovají publikované nepřevzaté dávky a neobnoví sending. Otestovaná práce na Windows i Linux. |
| 4. Handlery data/info | Ve stejném souboru auth-full, skupiny, drain, přesný result/MAC, základní i rozšířené info, pretty limit, privacy a chyby. | Každá změna storage až po validaci a autentizaci. Plná fronta vrací 200/deferred a stále provede inbound drain. Contract publish a ordinary touch mají rozdílné chyby. |
| 5. Sdílená fixture | `tools/test-python-server.py` rozšířit o `node-disk` a `--node`; existující název ponechat pro kompatibilitu příkazů. `tests-integration/README.md`, případně fixture metadata v `phpvpn_client.py`. | Spustí kopii JS a vlastní config/port/root, vypne cizí environment override, zachytí bezpečné logy, vždy ukončí vlastní proces. `--large-body-url` ukazuje na tuto fixture. Žádný zápis do globálního server.json ani produkčního storage. |
| 6. Integrace a nativní klient | Celé `tests-integration`, `client/tests/contract/test_server_contract.cpp`, případně testovací CMake a popisky. Produkční klientské parsery a transport neupravovat pro Node. | Stávající kontrakt běží proti node-disk přes PHPVPN_CONTRACT_URL. Doplnit opačný směr, provozní receive a restartovou výměnu, aby důkaz nebyl jen jednostranný bootstrap. |
| 7. Nasazení a dokumentace | Nový `server-node.md`, odkaz z `README.md`, ukázka proxy a konfigurace v dokumentaci. | Čistá složka pouze s JS spustí server, embedded HTML nepotřebuje assets. HTTP/HTTPS chování shodné. Popsána obnova locků, nový root bez migrace, podporovaná verze a omezení filesystému. |
| 8. Přejímka | Kontrola diffu, výsledků, zákazů úniku a podmínek níže. Po velké změně `graft build`. | Uložené test reporty s runtime, OS, profilem, počty pass/fail/skip a důvody. Rozlišit automatický C++ kontrakt a skutečnou tunelovou zkoušku. |

Pracovní závislosti: 1 → 2 → 3 → 4 → 5 → 6 → 7 → 8. Testy ke kroku vznikají současně s implementací jeho chování. Nepřevádět PHP interní soubory a nepřepisovat referenční implementace v rámci tohoto úkolu.

## 5. Validace

### 5.1 Jednotkové a deterministické testy Node

Použít vestavěný node:test a dependency injection pro hodiny, náhodu a filesystem. Testovací hooks jsou importované objekty, nikoli veřejné HTTP endpointy. Pouhé vektory Python verifikátoru neprokazují správnost Node derivace, proto volat také skutečné Node funkce nad pevnými vektory.

| Sada | Povinné případy |
|---|---|
| Crypto | Přesné SHA/HMAC/HKDF/health GMAC/info vektory, odlišné domény, NUL/LF, normalizace pouze empty, secret 0/ASCII zeros/NUL/Unicode, změněné tagy. |
| Parser a limity | Escapované duplicitní názvy i nested objekty, invalid UTF-8, BOM, JSON typy, trailing obsah, CRLF, chybějící LF, špatné pořadí data, base64url `_w`/`_x` a `__8`/`__9`, invalid datum, UUID varianta, count 16/17, všechny bajtové hranice limit−1/limit/limit+1. |
| Čas | Přesně −60000/0/+60000 ms přijmout, ±60001 odmítnout. TTL v bodě expirace nevydat ani nezapočítat. Marker 30000 ms netouchovat, 31000 ms touchovat, budoucí netouchovat. |
| Storage | Celá skupina nebo nic, právě jeden capacity sweep a dva pokusy, self-send v téže odpovědi, jiné dst zachová úspěch, drain i při úplném defer, více než 16 výstupních záznamů, souběžná publikace po snapshotu počká na další poll. |
| Fault injection | Short/zero write, flush, close, rename, stat/read/list/unlink a nedostatek místa. Busy deadline oddělený od unavailable. Žádná částečná publikace ani úspěšná falešně prázdná odpověď při nečitelném backendu. |
| Pád/restart | Kill před/po publish rename, před/po claim, před HTTP zápisem a přerušení klienta. Po offline obnově osiřelých locků přežijí platné data se zbývajícím TTL, receiving se nepublikuje, sending se nevrací. Čistý restart funguje bez zásahu. |
| Info | 251/252/253 položek, false/[] vs absence, full-auth bez extension odmítnout, snapshot z jednoho handle, souběžný touch nezničí seznam, atomický starý/nový obsah, Windows rename failure, přesný pretty limit a odebrání nejvyšší IP, vlastní [] a žádné cizí vztahy. |
| Bezpečnost a prostředky | Symlink/traversal/URL varianty neotevřou data, obecné chyby bez cest a tajemství, logy bez requestů/tagů/ciphertextu, pomalý a nedokončený upload nezablokuje event loop, omezená paměť při velkých paralelních odpovědích. |

### 5.2 Společné integrační testy

Spustit celou sadu včetně slow TTL a capacity scénářů na node-disk. Povinné jsou status/routing/headers, health, info, info_extensions, info_validation, data_transport, data_errors, limits, queue_capacity, expiry, concurrency, security, time_skew a default_profile. Performance použít jako měření regresí, ne slib konkrétního výkonu bez baseline.

Ponechat stejné protokolové očekávání napříč implementacemi. Případnou závislost testu na vnitřním PHP formátu oddělit do diskového adaptéru, ne přeskakovat protokolovou sémantiku. Diskový Node profil nemá dostat memory skips. Skips pro zdravý backend bez vyvolaného 503 musí mít deterministický protějšek v Node fault testech. Zkontrolovat i regresi php-disk a python-disk po úpravě sdíleného runneru.

Protokolovou chybovou matici otestovat pro 400/401/404/405+Allow/413/415/503 queue_busy/503 queue_unavailable/503 server_misconfigured/500 internal_error, vždy se správným content type a povinnými hlavičkami. Pro 503 nastavit konzistentní Retry-After odpovídající očekávání společné sady. Capacity vrací výhradně 200 s korektním result a MAC.

### 5.3 Skutečný klient a transport

1. Sestavit `phpvpn-contract-tests` se zapnutým `PHPVPN_BUILD_CONTRACT_TESTS`. Použít existující build konfiguraci projektu, neinstalovat do běžného systému testovací server.
2. Izolovaný runner nastaví PHPVPN_CONTRACT_URL, spustí nativní binárku a zachytí návratový kód. Nedostupný server musí znamenat fail, nikoli skip.
3. Vyžadovat health ověřený produkční kryptografií, rozšifrované základní info, prázdný data poll, všechny výchozí identity `.2` až `.15`, odmítnutí chybných credentials a rozšířený kontrakt publication/read/withdrawal.
4. Přenos dvou klientů musí ověřit accepted IDs/MAC, odeslání skutečného IPv4 payloadu, produkční GCM dešifrování a bajtovou rovnost na příjmu. Doplnit oba směry a běžný exchangeData příjem, ne pouze verifyAccessKey bootstrap. Další poll nic opakovaně nevydá.
5. Otestovat data odeslaná před restartem a přijatá po restartu před TTL. Kontraktní testy spouštět na čerstvé fixture, bez vzájemného znečištění pořadím testů.
6. Zachovat rozdíl diagnostika vs provoz: diagnostika smí spotřebovat špatně GCM podepsaný payload bez dešifrování, provoz jej odmítne a platné jiné řádky doručí. Absence/porušení data-mac nesmí potvrdit outgoing ani doručit inbound.
7. Windows WinHTTP a Linux libcurl prověřit na jejich nativních build cílech. HTTPS a reverzní proxy ověřit samostatnou izolovanou fixture, nebo rozšířením pouze testovacího URL guardu. Současný HTTP-only C++ test nevydávat za TLS důkaz. Testovací CA nevkládat do systémového trust store.
8. K dokumentaci připojit praktický postup pro dvě již nakonfigurované klientské instance s virtuálním adaptérem: Connect včetně čtvrtého checkpointu, obousměrný provoz, odpojení a reconnect. Pokud není takové prostředí dostupné, uvést tuto zkoušku jako neprovedenou a odlišit ji od úspěšného produkčního NetworkWorker kontraktu. Neprovádět svévolně změny systémového routování jen kvůli testu.

Příklady cílových příkazů po dokončení kroků 1–5, nikoli nyní existující Node podpora runneru:

```powershell
node --check phpvpn.js
node --test tests-node/*.test.js
uv run --python 3.13 --with pytest --with requests --with cryptography --with psutil python tools/test-python-server.py --profiles node-disk --node "C:/path/to/node24/node.exe" --report-dir reports/node-server -- -q
uv run --python 3.13 --with pytest --with requests --with cryptography python tools/test-python-server.py --profiles node-disk --node "C:/path/to/node24/node.exe" --contract-executable work/build/release/tests/phpvpn-contract-tests.exe --contract-only
```

Nahradit cestu k Node skutečně ověřeným runtime. Na Linuxu použít odpovídající cestu k binárce kontraktu. Python prostředí a balíčky spravovat výhradně přes uv. Přesné CMake configure/build příkazy převzít z aktivního presetu projektu při realizaci.

## 6. Nasazení a rizika

| Riziko | Řešení a důkaz |
|---|---|
| Tolerantní JS dekódování | Striktní UTF-8, JSON detekce duplicit, base64 round-trip, explicitní kalendář. Negativní vektory před HTTP integrací. |
| Chybně vynechané rozšířené info | Povinný C++ full-auth publish/read kontrakt a skutečný Connect postup. |
| Mtime/content race | Jeden handle pro extended read, netruncující touch, atomický replace. Test race a forced rename failure. |
| Osiřelý diskový lock | Nikdy krádež podle času. Konzervativní offline obnova po ověření zastavení všech vlastníků. Tuto omezenou dostupnost po tvrdém pádu uvést v návodu. |
| Windows sdílení handle | Krátké držení handle, žádný unlink-before-rename, bezpečné 503 při skutečném selhání. Ověřit na Windows, nepředpokládat POSIX chování. |
| Rozpor reference/specifikace | Rozhoduje protocol a specifická pravidla rozšíření. Neokopírovat potlačení I/O chyby jako prázdný drain ani zavedení statistického zámku proti zákazu běžného touch. |
| Proxy body/response limit | Request limit proxy přísně vyšší než 983040 B, možnost vrátit 16 MiB plus závěrečné řádky, vypnout cache a transformace. Testy HTTP i HTTPS. |
| Veřejný storage či tajemství | Server nemá obecný static file handler. Proxy nesmí zpřístupnit skript, konfiguraci ani diskový root. Samostatný účet, omezená oprávnění, sanitované logy. |
| Paměť při souběhu | Fronta zůstává disková, dočasnou spotřebu měřit při několika plných odpovědích, respektovat backpressure a timeouty. Zdokumentovat naměřené nároky. |
| False-positive přejímka | Nestačí status page, Python vektory ani mock transport. Vyžadovat výstup nativního C++ kontraktu proti skutečně běžícímu Node procesu. |

Node API pro asynchronní filesystémové operace, handle sync, utimes a rename je doloženo v [oficiální dokumentaci řady 24](https://nodejs.org/docs/latest-v24.x/api/fs.html). Konkrétní platformní crash a sharing garance se musí prokázat testy, dokumentace API sama je nenahrazuje.

Návod popíše spuštění `node phpvpn.js`, volitelnou konfiguraci, listen, reverse proxy, graceful shutdown, tvrdý restart s případnou obnovou locků a bezpečné přepnutí klientské URL na nový prázdný root. Zachování existujících PHP front ani migrace nejsou součástí výsledku.

## 7. Kritéria přijetí

- [ ] Nasazení aplikace tvoří jediný `phpvpn.js` bez npm dependencies, bez assets a bez build kroku. Podporovaný Node a testované OS jsou uvedeny.
- [ ] Provozní stránka GET/HEAD a všechny tři akce odpovídají aktuálnímu protocol.md včetně full-auth info rozšíření, status page warning a vypnutí stránky bez vypnutí API.
- [ ] Normativní klíče, kanonizace, validace, hlavičky, chyby a aktivní limity mají serverové i nezávislé integrační testy.
- [ ] Disk je jediný persistentní backend. Neexistuje memory driver, Redis, Memcached, payload key konfigurace ani replay databáze.
- [ ] Před autentizací a kompletní validací se nemění fronty ani markery. Částečné přijetí a kapacitní retry jsou přesné, drain běží i při úplném defer.
- [ ] Souběh a pády nevytvoří částečnou dávku ani dvojí vydání. Nepřevzaté publikované dávky přežijí restart do TTL. Ztráta po claimu je vědomá a data se neobnovují.
- [ ] Statistiky jsou best effort, běžný touch zachovává kontrakty. Potvrzená publikace má atomickou chybovou smlouvu, read respektuje privacy a pretty byte limit.
- [ ] Celá relevantní společná integrační sada projde na izolovaném node-disk. Povinné scénáře nejsou skryté za skip a deterministické testy doplní známé mezery sady.
- [ ] Skutečný C++ klient projde health/info/data i extended info. Dvě produkční klientské instance přenesou a dešifrují pakety s ověřeným obsahem, alespoň na úrovni NetworkWorker kontraktu. Test report jasně odlišuje virtuální adaptér/GUI zkoušku.
- [ ] HTTP, HTTPS za proxy, storage denial a nasazení do čisté složky jsou ověřené. Odpovědi ani logy neobsahují tajemství a interní cesty.
- [ ] Existuje návod se všemi soubory, konfigurací, runtime, provozními limity, postupem obnovy osiřelých locků a výslovným vyloučením migrace PHP dat.
- [ ] Závěrečné předání obsahuje skutečné výsledky testů a známá neprovedená ověření, ne pouze seznam příkazů. Relevantní graf repozitáře je aktualizován.

## 8. Předání

Plán neautorizuje implementaci. Realizaci zahájit až po výslovné volbě uživatele předané koordinátorem, s potvrzeným modelem a úsilím pro případný nový kontext. Materiální upřesnění tohoto plánu patří do nové úplné verze `plan-v002.md`, nikoli do přepisu tohoto souboru.
