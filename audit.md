# Bezpečnostní a výkonnostní audit PHP VPN

Stav: **B00 DONE, B01 CONDITIONAL, B02 CONDITIONAL, B03 CONDITIONAL, audit na výslovný pokyn uživatele zaparkován po B03. Audit produktu dosud není dokončen.** Autoritativní [plán v001](Plans/2026-09-19-11-17-37/plan-v001.md) a [intent v001](Intents/2026-09-19-11-17-37/intent-v001.md). Baseline: `tray_UX`, HEAD `4f449fd33df9bd824a5a165cfeb2b582dc3d1399`. Rozpracované změny a hashe eviduje [control.md](Reports/2026-09-19-11-17-37/audit/control.md). Čas je výslovně neomezený. Vlastnictví zápisu je evidenční, nikoli automatický zámek.

## Podmíněně vyloučené corner cases

- EX-01: Hrubé prolomení standardního AES-256, SHA-256 či HMAC bez implementační slabiny. Předpoklad: standardní bezpečné primitivum, správné klíče a nonces. Samotná abstraktní kryptanalýza nemá realistický provozní přínos. Chybné RNG, opakovaný nonce, slabý/defaultní klíč, validace tagu a záměna klíčů zůstávají v rozsahu. Vrátit při konkrétní slabině primitiva či implementace.
- EX-02: Libovolně vadný hardware bez reprodukovatelné systémové cesty. Předpoklad: podporovaný funkční host. Náhodná fyzická závada sama neurčuje opravitelný tok produktu. Zaplněný disk, short write, pád procesu, ztráta sítě a změna času zůstávají v rozsahu. Power-loss je případná mezera testu, nikoli touto výlukou odmítnuté riziko. Vrátit při reálné provozní reprodukci.
- EX-03: Interní implementace dosud neexistujících Linux/macOS portů. Předpoklad: dodaný produkt je Windows klient. Kontrola přenosných předpokladů a pravdivosti tvrzení o podpoře zůstává v rozsahu. Vrátit, pokud inventura odhalí skutečný port.
- EX-04: Akce již plně privilegovaného správce pouze v rozsahu jeho stávající moci, bez dalšího dopadu na jiné identity či stroje. Samotné nahrazení lokálního programu takovým správcem nepřidává oprávnění. Eskalace z neprivilegovaného procesu, reparse point, cizí peer, vzdálené klíče a přesah mezi hranicemi zůstávají v rozsahu. Vrátit při doloženém dodatečném dopadu.

Tyto výluky přezkoumá B13. Žádný útočníkem realisticky vytvořitelný HTTP/IPC vstup, replay, souběh, opakování, timeout nebo vyčerpání zdrojů není vyloučen kvůli nízké četnosti. Externí binární Wintun se posoudí z hlediska původu, podpisu, verze, načítání a relevantních advisories, nikoli jako úplný audit interního zdrojového kódu.

## Výkonnostní zjištění

| ID | Popis | Očekávaný přínos | Bezpečnostní dopad | Důležitost | Důkaz |
|---|---|---|---|---|---|
| PERF-0001 | AES known-answer test se v ověřeném PHP cli-server procesu opakuje pro každý health/info request, ještě před levným odmítnutím vstupu. | Odhad: odstranit jednu sadu KAT na odmítnutý request. Pět probe mělo celou dispatch fázi 246,5–319,9 µs, nejde o naměřené zrychlení optimalizace ani WAN benchmark. | Odmítnout levné neplatné vstupy před AES inicializací, ale zachovat KAT a fail-closed před skutečnou kryptografií. Pure PHP musí zůstat. Trvalá cache vyžaduje bezpečnou vazbu na verzi kódu a další posouzení. | nižší priorita, běžný datový polling KAT nevolá | [Detail](#perf-0001) |
| PERF-0002 | Každý enqueue skenuje všechny soubory cíle pod zámkem, drain je znovu skenuje a třídí. Také expirované fyzické soubory zatěžují hot path. | Odhad: indexované účtování a postupná údržba odstraní opakované O(N) průchody. Naplnění fronty po jednom souboru má nyní O(N²) návštěv položek. Procento zrychlení dosud nezměřeno. | Index musí transakčně zachovat kapacitu, TTL, crash recovery a souběžné claimy. Prostá neuzamčená cache usage by mohla překročit limit. | podmíněně důležité, vyžaduje rozhodnutí a ověření recovery | [Detail](#perf-0002) |
| PERF-0003 | Obecný JSON parser hledá duplicitní názvy lineárně v dosud sestaveném objektu, tedy N(N−1)/2 porovnání pro N různých členů. | Odhad: pomocný uspořádaný index sníží kontrolu duplicit na O(N log N), zejména u velkých info/config objektů. Zrychlení celého klienta zatím nezměřeno. | Zachovat přesnou rovnost dekódovaných názvů, odmítnutí duplicit, hloubkové a bytové limity i pořadí serializace. Použít strom nebo odolný hash, aby se nezavedly kolize řízené vstupem. | nižší priorita pro běžné malé objekty, B12 ověří workload | [Detail](#perf-0003) |
| PERF-0004 | Ciphertext příchozího recordu prochází samostatnou kontrolou base64url v DataRecord::parse, znovu v decode a pak dekódovacím průchodem. | Odhad: odstranit jeden celý kontrolní průchod velkého ciphertextu použitím společné validované reprezentace nebo jediného striktního dekodéru. Procentní přínos zatím nezměřen. | Zachovat nulové unused bits, délky, odmítnutí celé nekanonické odpovědi a limity před alokací. Neoznačit data za důvěryhodná před HMAC/GCM. | nižší priorita, B12 určí podíl na CPU | [Detail](#perf-0004) |

Průběžné nálezy B01–B03 jsou uvedeny výše. Výkonnostní audit dalších bloků pokračuje.

## Bezpečnostní a zásadní logická zjištění

| ID | Popis | Doporučené řešení | Závažnost | Dříve přijato (ano/ne) | Doklad o přijetí | Důkaz |
|---|---|---|---|---|---|---|
| SEC-0001 | Veřejné defaulty dovolují podvrhnout klientskou autentizaci a číst info. I po změně server secretu může jeho držitel zneužít ponechané default access keys jiných adres. | Pro soukromé nasazení vyměnit server secret i všechny access keys za nezávislá náhodná tajemství. Zachovat výslovně zvolený veřejný režim jako zřetelnou volbu. | vysoká | ano | [protocol.md](protocol.md), §3.2 ř. 75–85 a §5.1 ř. 133–135 výslovně povolují veřejné hodnoty a neblokující warning. | [Detail](#sec-0001) |
| SEC-0002 | Zachycený platný data request lze uvnitř časového okna zopakovat. Znovu publikuje data nebo vyzvedne novou frontu oběti. | Pro soukromý provoz použít ověřované HTTPS. Replay cache nebo potvrzované doručení vyžaduje samostatnou změnu protokolu a kapacitních pravidel. | vysoká | ano | [protocol.md](protocol.md), §1.2 ř. 36–42 a §4.2 ř. 112–116 výslovně přijímají replay a ničení fronty. | [Detail](#sec-0002) |
| SEC-0003 | Každý držitel společného server secretu má kryptografickou autoritu nad health/info odpověďmi. Izolace full-auth info vztahů proto neodolává peerovi s přístupem k přenosu jiné identity. | Vyžadovat ověřované TLS pro autentizaci serveru. Při změně protokolu svázat soukromé full-auth info s klíčem konkrétní identity a pro health použít serverovou autentizaci, kterou peer neumí vytvářet. | střední | ne | Přijetí tohoto přesahu mezi peery nedoloženo. Popis sdíleného klíče ani přijetí replay/TLS kompatibility není přijetím této příčiny. | [Detail](#sec-0003) |
| SEC-0004 | Selhání zápisu backendu se zamění za plnou frontu. Server vrací podepsané HTTP 200 s deferred místo dokumentovaného 503 queue_unavailable. | Odlišit kapacitní odmítnutí od I/O chyby v kontraktu Storage a propagovat řízenou chybu backendu. Zachovat již publikované skupiny a pravidla retry. | nízká | ne | Přijetí nedoloženo. protocol.md ř. 573 požaduje 503 při nedostupnosti backendu, tests.php ř. 1174–1189 očekává 200. Test není doklad vědomého přijetí rizika. | [Detail](#sec-0004) |
| SEC-0005 | Převzetí dávky do sending je nevratné ještě před doručením. Pád workeru či neúplná odpověď ztratí data, následný poll je neobnoví. | Počítat s obnovou vyšší vrstvou. Pro požadavek spolehlivého doručení samostatně navrhnout potvrzování a idempotenci, nikoli automatický návrat sending do fronty. | střední | ano | protocol.md §1.2 ř. 38–42, §10.3 ř. 579–583 a §16.4 ř. 818 výslovně přijímají ztrátu po převzetí v optimistickém at-most-once modelu. | [Detail](#sec-0005) |
| SEC-0006 | Čekání na storage lock měří wall clock. Zpětná korekce času prodlouží deklarované maximum 200 ms, dopředná je zkrátí. | Měřit relativní lock a sweep budget přes monotónní hrtime. UTC čas expirace a autentizace ponechat odděleně. | nízká | ne | Přijetí nedoloženo. protocol.md ř. 643 a 827 požaduje nejvýše 200 ms čekání. Obecně přijatá nezaručená dostupnost neurčuje výjimku z tohoto limitu. | [Detail](#sec-0006) |
| SEC-0007 | Na ověřeném Windows/PHP 8.5.9 dva souběžné polly úspěšně převezmou tutéž data dávku. Společná sending cesta vede ke kolizi čtení/mazání a neúplným odpovědím. | Zajistit výlučný claim na podporovaném backendu, například krátkým per-client zámkem kolem kontroly a převzetí. Ověřit víceprocesovým testem na cílovém filesystemu. | střední | ne | Přijetí nedoloženo. protocol.md ř. 560, 579, 602 a 815–819 vyžaduje nejvýše jednoho vlastníka dávky. Přijatá síťová ztráta nebo replay není přijetí duplicitního claimu. | [Detail](#sec-0007) |
| SEC-0008 | Výchozí klientský limit 65535 recordů odmítne i úplnou platnou odpověď pod výchozí serverovou kapacitou 16 MiB. Odpověď s 65536 malými dávkami má pouze 13828379 B. | Volit count limit v souladu s kapacitou a nejmenším recordem, nebo v samostatné změně sjednotit serverový a klientský rozpočet. Zachovat omezení paměti a fail-closed. | nízká | ano | client.md ř. 277 a 287 výslovně požaduje count limit a přijímá kompromis i ztrátu po odmítnutí. protocol.md ř. 489 nemá serverový count limit. | [Detail](#sec-0008) |

Průběžné nálezy B01–B03 jsou uvedeny výše. Přijetí rizika nesnižuje závažnost.

## Detaily nálezů a historie

Každý ověřený nález je ukládán postupem Z jednotlivě. Kanonickou autoritou je tento soubor, nikoli odvozený index.

<a id="sec-0001"></a>

### SEC-0001: Veřejné výchozí autentizační klíče umožňují vydávat se za klienta

**Stav a důkaz:** CONFIRMED, úplný statický tok B01. Baseline HEAD `4f449fd33df9bd824a5a165cfeb2b582dc3d1399`, [phpvpn.php](phpvpn.php) SHA-256 `31c80f223a9b92b00eb641d7037462aa3cd6b46020f6f26eeea9f0eb45f9ecc4`. Konfigurační globály ř. 31–58 obsahují veřejný server secret a deterministické access keys. `Config::fromConstants` ř. 1193–1236 je přijímá, prázdný secret nahradí stejnou veřejnou hodnotou. `KeyDerivation::semiKey/fullAuthKey` ř. 1303–1313 deterministicky odvodí klíče, `InfoHandler::handle` ř. 2183–2234 a `DataHandler::handle` ř. 2341–2396 přijmou správný HMAC. `HealthHandler::handle` ř. 2091–2118 veřejně poskytuje potřebný čas. `Application::isConfiguredSecretInsecure` ř. 2649–2656 a `StatusPageHandler::handle` ř. 2066–2075 pouze ovládají warning.

**Předpoklad a dopad:** Endpoint dostupný útočníkovi a nezměněný veřejný server secret i access key dovolují vytvořit nové platné požadavky pro výchozí identity bez odposlechu. Dopad je ztráta autentizace, důvěrnosti info a autority klientských operací. Pokud správce změnil pouze server secret, který sdílí všichni legitimní klienti, kterýkoli jeho držitel stále odvodí autentizaci jiné adresy s default access key. Vysoká závažnost vychází z dosažitelného převzetí identity. Tento nález netvrdí prolomení nezávislého náhodného PAYLOAD_KEY ani posouzení skutečné produkční konfigurace.

**Přijetí:** ano pro tento přesně vymezený veřejný režim. [protocol.md](protocol.md), §3.2 ř. 75–85 výslovně popisuje defaulty jako veřejné a vyžaduje přijetí prázdného/nulového secretu, §5.1 ř. 133–135 zakazuje blokaci endpointů jen pro takovou hodnotu. D-0004/D-0005. Nejde o nové blanket přijetí pro soukromé nasazení. Závažnost přijetím nesnížena.

**Reprodukce a omezení:** Staticky dosadit publikované konstanty do dokumentované HKDF/HMAC a projít výše uvedené větve. Aktivní pokus vůči produkci NOT RUN a není potřebný. Izolovaný běh test_default_profile na vlastní kopii ověřil všech 14 defaultních identit, PASS1, [výstup](Reports/2026-09-19-11-17-37/audit/evidence/B01/http-default-tests.txt) a [parita/proces](Reports/2026-09-19-11-17-37/audit/evidence/B01/http-default-metadata.json). Příčina deduplikována napříč info/data, index i obě tabulky byly před přidělením prázdné. Historie: B01 první ověření a okamžitý zápis Z.

<a id="sec-0002"></a>

### SEC-0002: Replay platného requestu znovu provede operace klientské identity

**Stav:** CONFIRMED. Statický tok a izolovaný produkční PHP harness. [phpvpn.php](phpvpn.php) v HEAD `4f449fd33df9bd824a5a165cfeb2b582dc3d1399`, SHA-256 `31c80f223a9b92b00eb641d7037462aa3cd6b46020f6f26eeea9f0eb45f9ecc4`: `Authenticator::withinSkew` ř. 1355–1357 přijímá ±60000 ms, `DataHandler::handle` ř. 2341–2396 ověří stejný HMAC znovu a bez rezervace requestId předá request k publish a drainAll. `InfoHandler::handle` ř. 2183–2222 používá stejnou timestamp-only čerstvost i před opakovanou publikací kontraktů. Jde o jednu kanonickou příčinu absence replay ochrany, nikoli o samostatnou chybu každého endpointu.

**Předpoklad a dopad:** Útočník má kopii platného požadavku, například z nešifrované HTTP trasy. Nemusí znát žádný klíč ani změnit jediný podepsaný bajt. Replay odeslání duplikuje recordy, replay pollingu provede nové vydání fronty dané identity do útočníkova spojení. Legitimní klient tím může přijít o příchozí data. Vysoká závažnost je dána dosažitelným trvalým narušením dostupnosti při opakovaném odposlechu, bez potřeby kompromitovat autentizační klíč. Nezávislý PAYLOAD_KEY zůstává chráněn. Přesné chování diskových claimů a souběhu ještě vlastní B02.

**Reprodukce:** [tests.php](tests.php) ř. 1199–1217 dvakrát odešle přesně stejný podepsaný request a ověří dvě doručené kopie, ř. 1219–1251 ověří absenci nonce/replay artefaktů. Aktuální běh PHP8.5.9 CLI v privátním TEMP prošel, celá sada 537 assertions, [výstup](Reports/2026-09-19-11-17-37/audit/evidence/B01/php-tests.txt). Test není důkaz reálného odposlechu HTTPS ani souběhu workerů.

**Přijetí:** ano, D-0002. Originál [protocol.md](protocol.md) §1.2 ř. 36–42 a §4.2 ř. 112–116 přijímá replay, nenávratné vyprázdnění fronty a definitivní ztrátu UDP. Nedostupnost či přijetí rizika nesnižuje závažnost. Návrh replay cache není automatická autorizace změnit normu. Historie: B01 ověřeno a ihned uloženo Z po prvním testu. Před zápisem porovnáno s SEC-0001, odlišná příčina i předpoklad, nejde o defaultní klíče.

**Doplnění B02:** Opakování send/poll přes čtyři izolované PHP workery prošlo existujícími replay testy v [HTTP sadě](Reports/2026-09-19-11-17-37/audit/evidence/B02/http-tests.txt). Storage po claimu záznam nevrací, přijatá ztráta bez replay je SEC-0005. Nezávislé porušení výlučného claimu na Windows zachycuje SEC-0007. Závěr B01 o následném storage ověření je tímto doplněn, ne přepsán na univerzální PASS souběhu.

<a id="perf-0001"></a>

### PERF-0001: Requestová statická cache neuchová AES self-test mezi HTTP požadavky

**Stav:** CONFIRMED pro PHP8.5.9 cli-server bez ini, jeden proces. [phpvpn.php](phpvpn.php), HEAD `4f449fd33df9bd824a5a165cfeb2b582dc3d1399`, SHA-256 `31c80f223a9b92b00eb641d7037462aa3cd6b46020f6f26eeea9f0eb45f9ecc4`: `Application::dispatch` ř. 2629–2642 spouští selfTest před `HealthHandler::handle` ř. 2087–2099 a `InfoHandler::handle` ř. 2133–2159. `AesGcm` ř. 823–884 ukládá výsledek jen do statické PHP vlastnosti. V každém ze 5 samostatných HTTP požadavků se stejným PID byl stav před dispatch null a po dispatch true, ačkoli výsledkem byl 415. Každý request tedy provádí 3 známé vektory seal, validní open i odmítnutí vadného tagu a přípravu tabulek.

**Důkaz a reprodukce:** `uv run --python 3.13 Reports/2026-09-19-11-17-37/audit/evidence/B01/http_fixture.py kat`. [Adaptér](Reports/2026-09-19-11-17-37/audit/evidence/B01/http_fixture.py) přes ReflectionProperty pouze čte stav před/po skutečném Application::handle. [Výsledky](Reports/2026-09-19-11-17-37/audit/evidence/B01/http-kat-metadata.json): stejný PID60384, 5/5 null→true, 415. Časy 319,9/260,2/248,6/246,5/248,5 µs měří celou dispatch, nikoli pouze KAT. Žádná úprava kryptografie ani produktu, žádný zátěžový test.

**Doporučení a bezpečnost:** Přesunout levné kontroly média, velikosti a tvaru před inicializaci kryptografie, zachovat known-answer test před každou skutečně používanou inicializací a při chybě fail-closed. Toto nemění wire bytes ani autentizační invariant a nezavádí nové přijaté riziko. Pro další omezení práce napříč requesty by byl nutný bezpečně spravovaný runtime cache s invalidací podle implementace, nikoli prosté vypnutí testu nebo veřejně zapisovatelný soubor PASS. Takový návrh musí znovu posoudit B13.

**Norma a omezení:** [protocol.md](protocol.md) §15 ř. 743 žádá zapamatování v procesu tak, aby se test neopakoval pro každý request. Současná cache vyhoví opakovaným voláním uvnitř jednoho PHP skriptu, nikoli ověřenému HTTP běhu. FPM/FastCGI a OPcache varianty NOT RUN. Přínos v běžném workloadu není změřen a data cesta KAT nevolá, proto nižší priorita. B12 případně změří podíl v health/info provozu. D-0008 zakazuje náhradu za OpenSSL. Historie: B01 jediný mechanismus zapsaný ihned po 5-request reprodukci, před pokračováním.

<a id="sec-0003"></a>

### SEC-0003: Společný server secret neizoluje odpovědi health a info mezi členy sítě

**Stav a důkaz:** CONFIRMED, statický důkaz hranice klíčové autority. [phpvpn.php](phpvpn.php), HEAD `4f449fd33df9bd824a5a165cfeb2b582dc3d1399`, SHA-256 `31c80f223a9b92b00eb641d7037462aa3cd6b46020f6f26eeea9f0eb45f9ecc4`: `KeyDerivation::healthKey/infoKey` ř. 1315–1324 mají jako jediné tajemství společný serverSecret. RequestId a info salt jsou veřejné v obálce. `HealthHandler::handle` ř. 2102–2118 používá takto odvozený GMAC. `InfoHandler::handle` ř. 2222–2244 používá tentýž skupinový secret i pro odpověď vytvořenou po full-auth. `buildPlaintext` ř. 2248–2265 sice filtruje vztahy podle autentizovaného žadatele, ale přenos těchto údajů nemá klíčovou izolaci vůči jiným členům sítě. Důkaz stojí na úplném seznamu vstupů odvození a použití symetrických primitiv, ne na útočném testu.

**Předpoklad a dopad:** Jiný oprávněný peer zná společný server secret a má možnost pozorovat cizí HTTP přenos, nebo jej aktivně ovlivnit. Samotné členství v síti bez přístupu k přenosu tento předpoklad nesplňuje. Pozorovatel může znát obsah cizí info odpovědi včetně vyfiltrovaných maskovaných vztahů, a při aktivním přístupu není GMAC/GCM důkazem, že odpověď vytvořil výhradně server. To omezuje autenticitu času a konfigurace vůči škodlivému členovi i při náhodných neveřejných access keys. Dopad je střední kvůli další síťové podmínce a omezení na řídicí odpovědi. Nejde o prolomení AES, převzetí full-auth požadavků jiné identity ani získání PAYLOAD_KEY. Přijetí konkrétní odpovědi Windows klientem a následné runtime účinky nejsou v B01 ověřené.

**Norma a přijetí:** [protocol.md](protocol.md) ř. 25–33 uvádí cíle autenticity health a důvěrnosti/integrity info, ř. 75–77 sdílení server secretu a oddělení access/payload klíčů, ř. 342 omezení info vztahů na žadatele a ř. 358–376 společné odvození odpovědi. [client.md](client.md) ř. 300–308 výslovně přijímá replay a trustAny kompatibilitu, nikoli kryptografickou autoritu každého člena nad novými serverovými odpověďmi. V těchto rozhodnutích ani odpovídajících micro zadáních nebylo doloženo přijetí tohoto přesahu. Závažnost je hodnocena samostatně. B13 musí rozhodnutí přezkoumat, případný dodatečný doklad připojit pod stejné ID.

**Mitigace a omezení:** Ověřované TLS doplňuje nezávislou autentizaci serveru a ochranu přenosu vůči ostatním peerům. Změna aplikačního protokolu vyžaduje verzování a posouzení bootstrapu, zejména health před znalostí konkrétní identity. Pro full-auth info lze navrhnout odpovědní klíč navázaný na access key a src. Zachovat oddělení účelů, náhodné IV/salt a pure-PHP AES. Nebyl vytvářen ani spouštěn exploit, nebyl simulován zásah do cizího přenosu. Deduplikace proti SEC-0001/0002: nezávislé na veřejných defaultech a na přehrání staré zprávy, jiný invariant a trust boundary. Historie: B01 statické potvrzení, okamžitý zápis Z.

<a id="perf-0002"></a>

### PERF-0002: Opakované úplné skeny fronty v kritické sekci

**Stav:** CONFIRMED statickým cost modelem B02. [phpvpn.php](phpvpn.php), HEAD `4f449fd33df9bd824a5a165cfeb2b582dc3d1399`, SHA-256 `31c80f223a9b92b00eb641d7037462aa3cd6b46020f6f26eeea9f0eb45f9ecc4`. `Fs::listDir` ř. 244–258 materializuje celý adresář. `DiskStorage::enqueue` ř. 1456–1471 drží cílový lock přes `usage` ř. 1514–1530 a zápis. Při nedostatku kapacity přidá `sweepQueue` ř. 1533–1546 a druhý `usage`. `drainAll` ř. 1475–1511 provádí další sken, parsing názvů, sort a open každé hlavičky. `DataHandler::publish` ř. 2476–2527 toto opakuje pro každý cíl requestu.

**Mechanismus a workload:** N fyzických souborů znamená O(N) čtení adresáře a rozborů názvů pro každý běžný enqueue, při pressure až tři průchody. Postupné publikování N jednosouborových dávek bez pollu má součet N(N−1)/2 prohlížených existujících položek. Logický byte limit neomezuje práci s již expirovanými soubory. Dlouhý scan drží lock a zvyšuje pravděpodobnost 200ms QueueBusy ostatních workerů. O(N log N) třídění drainu a pole ClaimedBatch nesou další paměť a I/O. Jde o ověřenou opakovanou práci, nikoli naměřený WAN throughput.

**Doporučení:** Posoudit crash-safe transakční index nebo jiný storage driver s atomickou rezervací a claimem, případně bounded průběžnou údržbu. Bezpečnostní podmínky jsou přesná kvóta pod souběhem, nezapočítávání vydaných/expirací a recovery po pádu. Nezavádět slepou cache ani stránkování odpovědi, které by změnilo [protocol.md](protocol.md) §10 ř. 556–602. D-0012, D-0014, I-0005. Přínos je odhad, B12 změří reprezentativní workload a B13 posoudí návrh.

**Deduplikace a historie:** B02 porovnala index i obě tabulky. PERF-0001 je samostatná AES inicializace, žádný existující nález tuto příčinu nepokrývá. Zápis Z proběhl ihned po dokončení statického důkazu, před měřením a dalšími nálezy.

**Doplnění B02:** Omezené warm-filesystem microcost měření [storage-costs.json](Reports/2026-09-19-11-17-37/audit/evidence/B02/storage-costs.json) potvrdilo růst nákladů při 0/128/1024/4096 souborech. U 4096 souborů s celkem 1MiB logických dat stál usage 5,69–6,57ms, enqueue 7,10ms a claim/drain 2375,31ms, peak PHP 8MiB. Počty a bajty beze ztráty. Devět usage vzorků a jedno enqueue/drain na profil nejsou plná B12 kampaň ani naměřené zrychlení návrhu. Přesné parametry a bezpečnostní podmínky v [B02 review](Reports/2026-09-19-11-17-37/audit/evidence/B02/review.md). Bounded sweep má stejné materializované listDir před kontrolou budgetu, 256 je limit zpracování, nikoli všech přečtených položek. Stabilita fyzického backlogu Q-0019.

<a id="sec-0004"></a>

### SEC-0004: I/O chyba je prezentována jako běžná kapacitní deferral

**Stav:** CONFIRMED, statický tok a izolovaná fault injection. [phpvpn.php](phpvpn.php), HEAD `4f449fd33df9bd824a5a165cfeb2b582dc3d1399`, SHA-256 `31c80f223a9b92b00eb641d7037462aa3cd6b46020f6f26eeea9f0eb45f9ecc4`. `DiskStorage::enqueue` ř. 1446–1469 používá stejný false pro mkdir/open lock/write/rename chybu i nedostatek kapacity. `writeBatch` ř. 1684–1703 převádí neúspěšný fwrite/flush/rename na false. `DataHandler::publish` ř. 2503–2509 všechna false mapuje na deferred. `handle` ř. 2380–2430 vrací 503 pouze při neexistujícím rootu nebo QueueBusy, jinak podepsaný result a HTTP 200.

**Reprodukce:** [Adaptér](Reports/2026-09-19-11-17-37/audit/evidence/B02/storage_probe.php) vyžaduje nezměněný produktový soubor a pouze v privátním syntetickém fixture nahradí namespaced fwrite pro .receiving návratem false. Žádný plný disk ani produkční zásah. [Výsledek](Reports/2026-09-19-11-17-37/audit/evidence/B02/application-write-failure.json): HTTP 200, acceptedRecords 0, jediný záznam deferred, koncový data-mac přítomen.

**Dopad a závažnost:** Běžné selhání zápisu nebo přístupových práv maskuje nedostupnost backendu jako kapacitní backpressure. Volající a monitoring podle HTTP výsledku nerozpoznají I/O závadu a opakují provoz do porouchané fronty. Nízká závažnost: záznam není falešně potvrzen jako accepted, klient jej má ponechat. Nejde o auth bypass, ztrátu již potvrzené dávky ani prokázaný vzdálený způsob zaplnění disku. Přesné následné chování klienta ověří B05/B11.

**Norma a přijetí:** [protocol.md](protocol.md) §10.2 ř. 573 rozlišuje kapacitu 200 od nedostupnosti backendu 503 queue_unavailable. Ř. 623 povoluje best effort pouze statistice, nikoli payload zápisu. [tests.php](tests.php) ř. 1174–1189 naopak očekává 200 při blokovaném adresáři cíle. D-0012/D-0013 a [client.md](client.md) ř. 300–308 nepřijímají maskování I/O poruch. Přijetí nedoloženo, C-0009 ponechává rozpor testu a normy k přezkumu B13.

**Doporučení a historie:** Rozšířit interní výsledek o odlišnou I/O chybu, zachovat at-most-once a korektní partial commit/retry. Backend chybu nevydávat za nezpracovaný request po již přijatých skupinách. B02 ověřila jednu příčinu, deduplikace proti SEC-0001 až 0003 i PERF-0002 bez shody. Okamžitý zápis Z po jediném fault testu, žádná produktová oprava.

<a id="sec-0005"></a>

### SEC-0005: Přijatá nevratná ztráta po claimu před doručením

**Stav:** CONFIRMED. [phpvpn.php](phpvpn.php), HEAD `4f449fd33df9bd824a5a165cfeb2b582dc3d1399`, SHA-256 `31c80f223a9b92b00eb641d7037462aa3cd6b46020f6f26eeea9f0eb45f9ecc4`. `DiskStorage::drainAll` ř. 1486–1509 atomicky přejmenuje každou data dávku na sending a vrací celý seznam claimů. Další poll vybírá pouze data. `DataHandler::handle` ř. 2396–2425 převezme všechny nalezené dávky před prvním emit. `emitBatch` ř. 2548–2562 po copy soubor smaže, při neúplném přenosu chybí závěrečný MAC. `sweepQueue/sweep` ř. 1533–1599 expirované sending odstraní. Žádný ACK příjemce ani návrat sending do data neexistuje.

**Důkaz:** [storage-matrix.json](Reports/2026-09-19-11-17-37/audit/evidence/B02/storage-matrix.json), crash-after: úplná publikovaná dávka byla převzata nezávislým PHP procesem, ten skončil bez emit/doručení, další claim vrací prázdný seznam, pozdější expiry cleanup dávku odstraní. [Reprodukční adaptér](Reports/2026-09-19-11-17-37/audit/evidence/B02/run_storage.py) a [PHP probe](Reports/2026-09-19-11-17-37/audit/evidence/B02/storage_probe.php). Nejde o pokus vůči cizímu spojení ani exploitační kód.

**Dopad a přijetí:** Samotná ztráta spojení nebo běžný pád PHP po claimu může nenávratně odstranit celé převzaté dávky i při soukromých klíčích a ověřovaném HTTPS. Jednorázové UDP nemusí mít obnovu. Střední závažnost hodnotí integritu doručovací logiky a dostupnost, nikoli prolomení payload šifrování. [protocol.md](protocol.md) ř. 38–42, 579–583, 818 a [client.md](client.md) ř. 302–307 potvrzují přijatý model. D-0002/D-0012, I-0005/I-0006. Výslovné přijetí závažnost nesnižuje.

**Mitigace a deduplikace:** Spolehlivost může dodat vyšší vrstva. Automatický retry sending mění garantované at-most-once na možné duplicity, vyžaduje změnu normy, idempotenci, autentizované ACK a bounded state. SEC-0002 zachycuje samostatnou příčinu opakovaného provedení cizího requestu bez replay stavu, tento nález se projeví bez replay i bez útočníka. Oba se mohou složit, ale neopakují totožný invariant. B02 zapsala přijaté riziko okamžitě po potvrzení stavových přechodů a fixture testu.

**Doplnění B02:** [Short emit test](Reports/2026-09-19-11-17-37/audit/evidence/B02/partial-emit.json) s vlastním ResponseWriter vrátil pouze sedm bajtů, HTTP začalo 200, ale bez data-result/data-mac. Následný poll měl nula dávek. Jde o injekci krátkého copy, nikoli skutečné přerušení TCP. Windows souběžná chyba výlučnosti claimu je samostatně SEC-0007 a není přijata touto výjimkou.

<a id="sec-0006"></a>

### SEC-0006: Storage timeout není odolný vůči změně wall clock

**Stav:** CONFIRMED, deterministická injekce času bez změny hodin hosta. [phpvpn.php](phpvpn.php), HEAD `4f449fd33df9bd824a5a165cfeb2b582dc3d1399`, SHA-256 `31c80f223a9b92b00eb641d7037462aa3cd6b46020f6f26eeea9f0eb45f9ecc4`: `DiskStorage::acquireLock` ř. 1762–1774 počítá deadline i remainingUs přes microtime(true). Zpětná změna tohoto času při obsazeném locku přidá celý rozdíl k čekání. Také relativní budget `sweep` ř. 1566–1582 používá wall clock.

**Reprodukce:** Jeden vlastní PHP proces drží vlastní .lock, druhý spustí původní acquireLock. Namespaced adaptér pouze po vytvoření deadline simuluje korekci wall clock −150 ms. Měření skutečné délky používá hrtime, hodiny Windows se nemění. [Výsledek](Reports/2026-09-19-11-17-37/audit/evidence/B02/clock-back.json): QueueBusy až po 356,2142 ms místo 200 ms. Běžný čas bez injekce je ve [storage-matrix.json](Reports/2026-09-19-11-17-37/audit/evidence/B02/storage-matrix.json). [Adaptér](Reports/2026-09-19-11-17-37/audit/evidence/B02/extra_checks.py) a [PHP shim](Reports/2026-09-19-11-17-37/audit/evidence/B02/storage_probe.php) jsou mimo produkt.

**Dopad:** Při souběhu a běžné korekci systémového času může worker čekat déle, než slibuje storage kontrakt. Větší skok prodlouží čekání úměrně. Nízká závažnost, protože vzdálený klient sám hodiny PHP hosta neovládá a k dopadu je potřebný obsazený zámek. Test neprokazuje neomezenou blokaci, běžné plánovací zpoždění OS není tímto nálezem ani zárukou přesného hard-real-time limitu.

**Norma a přijetí:** [protocol.md](protocol.md) ř. 643 uvádí nejvyšší čekání 200 ms a ř. 827 požaduje řízený konec po 200 ms. D-0014, I-0005. Hledání v těchto pravidlech a [client.md](client.md) ř. 300–308 nedoložilo přijetí wall-clock výjimky. Timestamp/TTL v UTC jsou jiný kontrakt a tímto doporučením se nemění.

**Mitigace a historie:** Monotónní hrtime pro elapsed budget locku a sweepu. Změnu serverového UTC nepřenášet do relativního čekání, nezavádět změny autentizačního okna. Deduplikace: nezávislé na nákladech skenu PERF-0002, projevuje se i na prázdné frontě s drženým zámkem. Ihned zapsáno Z po jednom bounded testu. B13 přezkoumá závažnost a B11/B12 podmínky času.

<a id="sec-0007"></a>

### SEC-0007: Souběžný rename není na ověřeném Windows dostatečný výlučný claim

**Stav:** CONFIRMED pro Windows 11, lokální NTFS, PHP 8.5.9 NTS x64, čtyři izolované PHP cli-server procesy. [phpvpn.php](phpvpn.php), HEAD `4f449fd33df9bd824a5a165cfeb2b582dc3d1399`, SHA-256 `31c80f223a9b92b00eb641d7037462aa3cd6b46020f6f26eeea9f0eb45f9ecc4`: `DiskStorage::drainAll` ř. 1481–1509 nezávisle načte kandidáty a pro každý považuje true z `Fs::rename` ř. 213–215 za výlučné vlastnictví společné sending cesty. Zámek používaný enqueue se při claimu nepoužívá. `readClaimed` ř. 1720–1752 handle zavře a `emitBatch` ř. 2550–2562 cestu znovu otevře a odstraní.

**Důkaz:** Neinstrumentovaná existující HTTP sada skončila 90 PASS a 2 FAIL, konkrétně paralelní poll a souběžné send/poll, [výstup](Reports/2026-09-19-11-17-37/audit/evidence/B02/http-tests.txt). Opakování pouze čtyř concurrency testů s namespaced logováním skutečných filesystem volání opět 2 PASS/2 FAIL. [Souhrn trace](Reports/2026-09-19-11-17-37/audit/evidence/B02/double-claim-summary.json) zachytil 5 z 38 názvů s úspěšným data→sending rename ve dvou různých PID. Například PID 48756 a 67404 oba úspěšně přejmenovaly a otevřely stejnou dávku, první ji vypsal a smazal, druhému následné emit open selhalo. [Trace 48756](Reports/2026-09-19-11-17-37/audit/evidence/B02/fs-48756.jsonl), [trace 67404](Reports/2026-09-19-11-17-37/audit/evidence/B02/fs-67404.jsonl), [tvary odpovědí](Reports/2026-09-19-11-17-37/audit/evidence/B02/recheck-response-shapes.jsonl), [metadata](Reports/2026-09-19-11-17-37/audit/evidence/B02/recheck-http-metadata.json). Syntetická data, privátní klíče a storage mimo webroot. Žádná změna PHP produktu, proxy nemodifikuje NDJSON.

**Dopad a podmínky:** Dva legitimní souběžné požadavky téže autentizované identity mohou sdílet tentýž claim. Smazání druhým workerem ukončí odpověď bez data-mac, klient odmítne i další již vydané řádky. Dvojí vlastnictví zároveň dovoluje dvojí copy, jeho četnost samostatně neodhadujeme. Střední závažnost: porušení at-most-once a dostupnosti při běžném povoleném souběhu, bez nutného pádu či síťové ztráty. Neprokazuje přístup k jiné identitě, prolomení MAC ani chybu všech Unix/SAPI variant.

**Přijetí a mitigace:** [protocol.md](protocol.md) ř. 560, 579, 602, 815–819 výslovně vyžaduje jednoho vlastníka. [client.md](client.md) ř. 300–308 přijímá replay a ztrátu, nikoli konkurenční chybu claimu. D-0001/D-0012, I-0005. Nezavádět spoléhání pouze na kontrolu file_exists před rename, ta sama závod neřeší. Krátký zámek kolem kontroly a převzetí nebo ověřené backendové atomické claim API musí zachovat nově publikované dávky, TTL a bounded čekání.

**Omezení a historie:** Trace wrapper volá nativní rename beze změny výsledku, přidává pouze logování názvu, PID a času. Dřívější neinstrumentovaný FAIL potvrzuje, že příčinu nevytvořil logger. Oficiální PHP 8.5.9 [filesystem wrapper](https://github.com/php/php-src/blob/php-8.5.9/main/streams/plain_wrapper.c#L1241) a [virtual_rename](https://github.com/php/php-src/blob/php-8.5.9/Zend/zend_virtual_cwd.c#L1412) byly použity jen k orientaci, rozhodující je lokální reprodukce. Vnitřní závod OS API není bez dalšího zdroje prohlášen za univerzální vlastnost Windows. Od SEC-0005 odlišeno: zde se porušuje jediné vlastnictví za normálního souběhu, tam je vlastnictví jediné a teprve doručení selže. Zápis Z ihned po analýze trace, před dalším průzkumem.

<a id="perf-0003"></a>

### PERF-0003: Kvadratická kontrola duplicitních JSON členů

**Stav:** CONFIRMED, úplný statický důkaz B03. HEAD `4f449fd33df9bd824a5a165cfeb2b582dc3d1399`. [json.cpp](client/src/core/protocol/json.cpp), `Parser::parseObject` ř. 96–141, zvláště 112–116, porovnává každý nový dekódovaný název se všemi předchozími. SHA-256 `d49e6e76825ce1473e2f3183a4c0550d9b71b40a94494cdc69718367ae15ed23`. N různých členů nutně znamená N(N−1)/2 porovnání. Nejde o cenu samotného povinného odmítání duplicit.

**Dosažitelnost a přínos:** [response_parser.cpp](client/src/core/protocol/response_parser.cpp), `parseInfoPlaintext` ř. 172–181 předává obecný objekt do Json::parse po omezení na 262144 B. Neznámá pole plaintextu jsou tolerována. Velké objekty lze tedy zpracovávat i v tomto limitu. Config/IPC užití posoudí B06/B07, bez tvrzení o jejich celkovém dopadu. Odhad optimalizace je O(N log N) kontrola pomocí uspořádaného indexu při zachování původního vektoru pro serializaci. Lokální release měření 1000/2000/4000/8000 členů má mediány 0,845/2,252/9,060/38,891 ms, [log](Reports/2026-09-19-11-17-37/audit/evidence/B03/microcost-release.txt). Význam pro běžný workload a celý klient zůstává B12. Žádný A/B výsledek se netvrdí.

**Bezpečnost a přijetí:** Index nesmí odstranit kontrolu duplicit ani změnit porovnání po dekódování escape sekvencí. [protocol.md](protocol.md), §6.1 ř. 172–180 požaduje odmítnutí duplicit. Zachovat limity a deterministické pořadí. Nové slabé hashování by mohlo vytvořit další vstupem řízené nejhorší chování. Samostatný bezpečnostní dopad zatím není tímto výkonovým záznamem potvrzen. SEC-0003 popisuje jinou příčinu autority peerů nad info.

**Historie:** B03, okamžitý zápis Z po statickém ověření, před další analýzou nálezů. Doplněn bezpečný lokální in-process microcost s pěti opakováními, bez síťové zátěže. Produkt nebyl změněn.


<a id="perf-0004"></a>

### PERF-0004: Opakované úplné průchody base64url ciphertextu

**Stav:** CONFIRMED, statický tok B03 nad HEAD `4f449fd33df9bd824a5a165cfeb2b582dc3d1399`. [ndjson_reader.cpp](client/src/core/protocol/ndjson_reader.cpp), `parseAndVerify` ř. 111–123 nejprve volá DataRecord::parse a poté Base64Url::decode. [data_record.cpp](client/src/core/protocol/data_record.cpp), `parse` ř. 94–97 prochází celý ciphertext v isCanonical. [base64url.cpp](client/src/core/protocol/base64url.cpp), `decode` ř. 109–145 znovu volá isCanonical ř. 68–91 a následně všechny znaky převádí. Totéž schéma má [response_parser.cpp](client/src/core/protocol/response_parser.cpp), `parseInfo` ř. 143–157. Source hashe jsou v neměnném [manifestu B00](Reports/2026-09-19-11-17-37/audit/evidence/B00/source-manifest.tsv).

**Návrh a přínos:** Sjednotit striktní validaci s dekódováním, případně předávat explicitně ověřený view tak, aby byl jeden opakovaný průchod odstraněn bez možnosti volat neověřený dekodér na cizí text. Odhad ušetřené práce je jeden O(L) průchod abecedou na L znaků ciphertextu, nikoli naměřené zrychlení celého tunelu. Zachování vlastnictví výstupních bufferů je nutné, jejich běžná kopie není automaticky zbytečná.

**Bezpečnost:** [protocol.md](protocol.md), §6.1 ř. 182–192 výslovně vyžaduje kanoničnost včetně unused bits před použitím hodnot. Žádná optimalizace nesmí z toho slevit. Limit řádku zůstane před alokací. Selhání nesmí vrátit částečný výstup, přijatý text v AAD musí zůstat přesný. HMAC obálky a GCM payloadu mají stále oddělené povinnosti. Návrh nevyžaduje nové přijetí rizika, jeho implementaci musí znovu ověřit B13.

**Historie:** B03 okamžitý zápis Z po ověření toku a deduplikaci. Doplněn release microcost 983040 B, samostatný scan 0,421–0,577 ms a decode včetně vlastní validace 1,088–1,255 ms na operaci. Pět vzorků po 20 operacích, [log](Reports/2026-09-19-11-17-37/audit/evidence/B03/microcost-release.txt). Zrychlení optimalizované verze se netvrdí, produkt beze změny.


<a id="sec-0008"></a>

### SEC-0008: Klientský count limit nepokryje výchozí serverový buffer

**Stav:** CONFIRMED, B03 statický kontrakt a bezpečný lokální test. HEAD `4f449fd33df9bd824a5a165cfeb2b582dc3d1399`. [ndjson_reader.cpp](client/src/core/protocol/ndjson_reader.cpp), `parseAndVerify` ř. 103–108 odmítne celou odpověď při count+1 nad limitem před dekódováním překračujícího řádku. [ndjson_reader.h](client/src/core/protocol/ndjson_reader.h), `NdjsonLimits` ř. 47–50 nastavuje 65535. Produkční default shodně [config.h](client/src/core/config/config.h) ř. 33 a 74, předání parseru [network_worker.cpp](client/src/core/runtime/network_worker.cpp), `exchangeData` ř. 377–381. Hashe zdrojů jsou v [neměnném manifestu](Reports/2026-09-19-11-17-37/audit/evidence/B00/source-manifest.tsv).

**Důkaz a dopad:** [boundary_tests.cpp](Reports/2026-09-19-11-17-37/audit/evidence/B03/boundary_tests.cpp), `response_count_default_boundary`, vytvoří pouze v paměti platný IPv4 payload, GCM record a MAC obálky. Opakovaná identická platná dávka je dle replay kontraktu přípustná. Každý řádek má 211 B včetně LF. 65534 a 65535 recordů projde, 65536 recordů je odmítnuto s wire_too_many_records, i když celé tělo má 13828379 B, pod 16777216 B. [Výstup](Reports/2026-09-19-11-17-37/audit/evidence/B03/count-boundary.txt). Server dle [protocol.md](protocol.md), §9.5 ř. 489 a §11 ř. 640 vrací celý buffer bez samostatného count limitu. B02 již doložil nevratný claim. Při nahromadění dostatečného množství malých dávek se tak zahodí i zdravá úplná odpověď, žádný payload ani ACK parser nevrátí. B11 má ověřit celý běhový řetězec, zde se netvrdí nové síťové E2E měření.

**Závažnost a přijetí:** nízká, dostupnost při vysokém počtu malých recordů během backlogu, bez porušení kryptografické autenticity či nového přístupu k datům. Ano, [client.md](client.md), Příjem dat ř. 277 a 287 výslovně předepisuje počítání a přijímá vysoký default jako kompromis. Přijetí závažnost nesnižuje. Nejde o porušení implementované normy, nýbrž o doloženou cenu přijatého limitu.

**Mitigace a deduplikace:** Odvodit kompatibilní count limit z kapacity/minimálního recordu nebo vyjednat konzistentní meze se serverem v samostatné změně. Neodstranit count/memory limity bez náhrady, nevracet částečně neověřenou odpověď a neobnovovat serverové sending bez návrhu idempotence. SEC-0005 je samostatná příčina nevratného claimu před ACK, tento záznam popisuje count/byte nesoulad, který ztrátu vyvolá i bez chyby I/O. SEC-0007 je odlišná kolize vlastníků. Q-0021 tím má konkrétní odpověď, end-to-end závěr patří B11.

**Historie:** okamžitý zápis Z po limitním testu, bez dalšího nálezu mezitím. Produkt a produkční síť beze změny.

## Hypotézy a neprovedená ověření

Otevřené otázky a rozpory zůstávají v [questions.md](Reports/2026-09-19-11-17-37/audit/questions.md). B03 doplňuje C-0010 numeric overflow versus deklarovaný strict JSON, Q-0022 UTF-8 a Q-0023 extrémní délky/secure memory hardening. Jde o přesná pozorování a otázky dopadu, nikoli další potvrzené SEC nálezy. Q-0021 má parserový důkaz SEC-0008, server/client E2E zůstává B11. Dřívější Q/C z B01/B02 nebyly plošně uzavřeny. PHP8.2/SAPI, HTTPS/proxy, Windows klientské E2E, long-run fuzzing a B12 workload neprovedeny.

## Úplnost, konzistence a zbývající práce

Baseline má 594 souborů a 602 nepřekrývajících se úseků. B01 dokončila 18, B02 dalších 11 a B03 dalších 53, celkem 82 REVIEWED a 520 TODO jiných vlastníků. [B03 review](Reports/2026-09-19-11-17-37/audit/evidence/B03/review.md) a [file-review](Reports/2026-09-19-11-17-37/audit/evidence/B03/file-review.tsv) uvádějí každý úsek, hash, metodu a přesné meze. B04–B14 nebyly zahájeny.

Report obsahuje 4 PERF a 8 SEC. B03 přidala PERF-0003/4 a SEC-0008 jednotlivě postupem Z. SEC-0008 je nízký, výslovně přijatý count/byte kompromis, nezaměňuje se s SEC-0005 nevratnou ztrátou při claimu. SEC-0007 z B02 zůstává potvrzenou nepřijatou chybou souběhu na Windows/PHP8.5.9. Přijetí nesnižuje závažnost, opravy nejsou součástí auditu.

B03: 78 přidělených unit testů a 7 boundary testů PASS v release i MSVC AddressSanitizer, bez hlášení paměťové chyby. Nezávislé Python/PHP/CNG porovnání 10 GCM vektorů a 4 key profilů, PHP 196 kontrol PASS. Dílčí release microcost dvou PERF nálezů není procentní zrychlení optimalizace ani celá B12 kampaň. B02 dřívější HTTP 90 PASS/2 FAIL a opakované concurrency 2 PASS/2 FAIL zůstávají nezměněnými důkazy, nikoli SKIP.

Úklid, kontinuita všech 600 source/dirty hashů a konzistence artefaktů jsou v [B03 validaci](Reports/2026-09-19-11-17-37/audit/evidence/B03/validation.json). [Předání B03](Reports/2026-09-19-11-17-37/audit/handoff/B03.md) určuje další bod a uvolnění zápisu. Produkt, produkční služba/config/síť a původní dirty soubory nebyly změněny.

Celý audit není dokončen. Na výslovný pokyn uživatele je zaparkován po B03. B04 ani jiný blok se nyní nespouští. Pokračování vyžaduje nový výslovný pokyn uživatele a následné ověření hashů a RELEASED.
