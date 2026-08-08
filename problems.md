# Posouzení nasazení návrhu `design2.md` na sdílený PHP hosting

## Výsledek

Server popsaný v `design2.md` **lze spustit na části běžných sdílených PHP hostingů**, protože jde jen o krátké HTTP požadavky a návrh nevyžaduje daemon, dlouho běžící proces, shell, cron, databázi ani nestandardní PHP rozšíření. Nelze ale zaručit funkční a bezpečné nasazení na libovolném nebo „typickém“ shared hostingu pouze podle údaje „PHP 8.2+“.

Pro malý počet klientů může návrh fungovat po preflight testu a úpravě limitů. Pro desítky klientů, výchozí interval 250 ms a vyšší datový provoz jsou limity requestů, I/O a PHP workerů na běžném sdíleném hostingu pravděpodobným praktickým omezením. Absolutním blokérem je prostředí, které neposkytne persistentní úložiště se spolehlivým `flock` a atomickým `rename`, neumí bezpečně skrýt frontu před HTTP, nepředá aplikaci autentizační hlavičky nebo nemá HTTPS.

Označení níže hodnotí stav na sdíleném hostingu. „Nelze doinstalovat uživatelem“ znamená, že změnu musí provést poskytovatel nebo je nutné zvolit jiný tarif/server; Composer ji nevyřeší.

## PHP rozšíření a systémové závislosti

| Závislost a odkaz na návrh | Dostupnost na shared hostingu | Lze ji dodat uživatelem? | Čistě PHP/Composer náhrada | Dopad |
|---|---|---|---|---|
| **PHP 8.2+** (§2, §12, §13.8) | **běžně dostupné**, ale ne na každém starším nebo levném tarifu | Pokud verzi nelze vybrat v panelu, **nelze doinstalovat uživatelem** | Ne; přepis pro starší PHP by byl změnou cílové platformy | **Zásadní blokér**, pokud hosting 8.2+ nenabízí |
| **Hash**: `hash_hmac`, `hash_equals`, SHA-256 (§2, §5) | **běžně dostupné**; v podporovaném PHP jde o vždy zapnuté core rozšíření | Není co instalovat; jeho případné zakázání uživatel neopraví | Bezpečná uživatelská reimplementace HMAC nedává smysl a není potřebná | Za normálního PHP 8.2+ **není blokér**; ověřit `function_exists` a `sha256` v `hash_algos()` |
| **JSON**: `json_decode`/`json_encode` (§2, §6, §8) | **běžně dostupné**; od PHP 8 je JSON vždy zapnuté core rozšíření | Není co instalovat | Není potřebná | **Není blokér** |
| **CSPRNG**: `random_bytes` (§2, §7) | **běžně dostupné** jako funkce PHP 7/8, používá zdroj náhody operačního systému | Zdroj náhody **nelze doinstalovat uživatelem**; selhání musí aplikace zachytit | Composer polyfill na PHP 8.2 nepomůže, pokud hostitelský OS neposkytne bezpečný zdroj | Prakticky **není blokér**; nefunkční CSPRNG je bezpečnostní hard-fail |
| **Souborové funkce**: `fopen`, `fwrite`, `fflush`, `rename`, `unlink`, `filemtime`, `mkdir`, `flock` (§2, §7, §8, §10) | Samotné funkce jsou **běžně dostupné** | Poskytovatel je může zakázat přes `disable_functions`; pak je **nelze doinstalovat uživatelem** | Composer nemůže nahradit zakázané syscalls ani mezi-procesový lock | Chybějící potřebná funkce je **zásadní blokér**; musí být součástí preflightu |
| **Persistentní lokální filesystem se spolehlivým `flock` a atomickým `rename`** (§2, §7, §8) | Zapisovatelný prostor je **běžně dostupný**, ale požadovaná lokální/single-node semantika **často chybí** nebo ji poskytovatel negarantuje (cluster, NFS, oddělené uzly) | **Nelze doinstalovat uživatelem** | Ne. Čistě PHP lock nezajistí správnost nad nevhodným filesystemem; náhradou by byla databáze/Redis/MQ, které návrh výslovně vylučuje | **Zásadní blokér**, pokud souběžný test neprokáže správné zamykání a viditelnost souborů všem PHP workerům |
| **Soukromý zapisovatelný adresář** (§2, §7, §11) | Zapisovatelný adresář je **běžně dostupný**; adresář mimo webroot nebo možnost účinného deny pravidla **často chybí** | Přístupová pravidla webserveru **nelze doinstalovat uživatelem**, pokud hosting nepodporuje `.htaccess` ani vlastní konfiguraci | PHP nemůže zabránit webserveru ve statickém vydání souboru, aniž se požadavek dostane do PHP | **Bezpečnostní blokér**, pokud jsou soubory fronty přímo dosažitelné přes HTTP |
| **HTTPS terminované hostingem/proxy** (§2, §4, §11) | **běžně dostupné** (typicky spravovaný certifikát), ale musí být aktivní a vynucené | TLS službu/reverse proxy obvykle **nelze doinstalovat uživatelem** | Ne; aplikační HMAC nechrání metadata a odpovědi proti odposlechu | Bez spolehlivého HTTPS **zásadní blokér** |
| **Předání hlavičky `Authorization` a vlastních `X-PHPVPN-*` hlaviček do PHP** (§5) | Vlastní hlavičky jsou **běžně dostupné**; `Authorization` u některých CGI/FastCGI/proxy konfigurací **často chybí** v PHP | Konfiguraci proxy/FastCGI často **nelze doinstalovat uživatelem** | Composer nepomůže. Možná čistě aplikační úprava je přesun podpisu do vlastní hlavičky, ale mění protokol §5 | **Zásadní blokér** pro návrh beze změny, pokud testovací request hlavičku nedoručí |
| **Správný čas hostitele** (§4, §5) | Synchronizovaný systémový čas je **běžně dostupný** | NTP/systémový čas **nelze doinstalovat ani opravit uživatelem** | Ne; `health` opraví čas klienta, ne chybný čas serveru | Výrazný drift způsobí plošné `401`; provozní blokér do opravy poskytovatelem |
| **Webserver/PHP worker a krátké POST požadavky** (§1, §4, §11) | **běžně dostupné** | Počet workerů, CPU/I/O kvóty a proxy limity **nelze doinstalovat uživatelem** | Ne; lze jen snížit provoz nebo změnit hosting | Pro malý provoz v pořádku, při vyšší zátěži pravděpodobné omezení |

### Co návrh na serveru skutečně nepotřebuje

Podle §2 a §13.8 nejsou serverovou závislostí OpenSSL rozšíření PHP, cURL, mbstring, sockets, FFI, APCu, Composer, databáze, Redis, message broker, shell ani cron. Jejich nedostupnost tedy **není problém a není potřeba náhrada**. HTTPS však stále vyžaduje systémovou TLS službu hostingu; tvrzení „není třeba ext-openssl“ neznamená „není třeba TLS“.

Server rovněž neotvírá raw socket, UDP port ani virtuální síťovou kartu, neprovádí IP forwarding a nepotřebuje root práva. Z pohledu §2 je pouze HTTPS relay šifrovaných paketů mezi klienty. Na shared hostingu proto může fungovat tato relay část, nikoli klasický serverový VPN gateway do internetu. To není chybějící závislost, ale hranice funkce návrhu.

## Zásadní blokery nasazení či správné funkčnosti

### 1. Negarantovaný typ filesystemu a více aplikačních uzlů

Sekce §2 podporuje právě jednu instanci se sdíleným **lokálním** diskem a výslovně vylučuje filesystem s nespolehlivým `flock`. Sekce §7 a §8 staví korektnost doručení na per-frontovém locku a atomických rename operacích. Shared hosting může uživateli ukázat jediný adresář, ale interně jej obsluhovat z více webových uzlů nad síťovým úložištěm. Samotný úspěšný návrat `flock()` nebo jednoduchý test v jednom requestu nestačí.

- Stav: požadovaná garance **často chybí** a **nelze doinstalovat uživatelem**.
- Náhrada: žádná čistě PHP/Composer náhrada. Databázová nebo Redis fronta by byla architektonická změna a odporuje §2.
- Nutnost: souběžný integrační test alespoň dvou PHP requestů musí prokázat vzájemné vyloučení, atomické publikování a společnou viditelnost souborů. Pokud provider nepotvrdí single-node/lokální úložiště a test není stabilní, prostředí odmítnout.

### 2. Fronta vedle veřejného skriptu nemusí jít bezpečně skrýt

Sekce §2 chce zapisovatelný adresář vedle `phpvpn.php`; §11 současně požaduje zákaz přímého HTTP přístupu do `phpvpn/`. Práva `0700` sama o sobě nebrání webserveru vydat statický soubor, pokud PHP i web běží pod stejným účtem. Na nginx hostingu bez uživatelské konfigurace nelze spoléhat na `.htaccess`.

- Stav: privátní adresář je **často dostupný**, ale možnost bezpečného deny pravidla **často chybí**; konfiguraci webserveru **nelze doinstalovat uživatelem**.
- Náhrada: Composer nepomůže. Praktická úprava je přidat konfigurovatelný `QUEUE_ROOT` mimo document root; pokud `open_basedir` takovou cestu nepovolí, je hosting nevhodný.
- Dopad: bez prokazatelné HTTP nedostupnosti jde o bezpečnostní blokér. E2E šifrování chrání payload, nikoli nutně metadata fronty, UUID, časy ani možnost cizího mazání/zahlcení přes chybnou konfiguraci.

### 3. HTTPS a autentizační hlavičky jsou mimo kontrolu PHP

HMAC v §5 je navázán na `Authorization` a `X-PHPVPN-*`; §11 vyžaduje HTTPS. TLS terminaci, zachování hlaviček a případná WAF pravidla provádí infrastruktura před PHP.

- Stav: HTTPS je **běžně dostupné**; spolehlivé předání `Authorization` a výjimka z restriktivního WAF **často chybí**. Tyto funkce **nelze doinstalovat uživatelem**.
- Náhrada: žádná Composer náhrada. Lze změnit protokol na vlastní podpisovou hlavičku nebo požádat poskytovatele o konfiguraci; obojí je změna oproti §5.
- Dopad: chybějící TLS nebo zahozená podpisová hlavička jsou zásadní blokery. WAF blokující NDJSON/base64 či vysokou frekvenci způsobí 403/429 ještě před během skriptu.

## Pravděpodobná omezení a nutné úpravy

### Frekvence requestů a kapacita workerů

`REQUEST_INTERVAL_MS = 250` (§1, §3, §9, §11) znamená přibližně **4 POST requesty za sekundu na každého stále připojeného klienta**, i když nejsou data. Deset klientů vytváří asi 40 requestů/s, padesát klientů asi 200 requestů/s. Request není dlouho běžící a neobsazuje worker trvale, takže staré námitky proti dlouhému streamingu zde neplatí. Přesto se každý cyklus musí spustit PHP, ověřit HMAC, pracovat s nonce soubory, zamykat frontu a provést GC.

- Stav: krátké PHP requesty jsou **běžně dostupné**; dostatečné CPU, worker/I/O kvóty a povolená frekvence **často chybí** a **nelze je doinstalovat uživatelem**.
- Náhrada: žádná Composer náhrada. Nutná je adaptace intervalu i při prázdném provozu, omezení počtu klientů, backoff a zátěžový test podle §12.8. Pro desítky klientů je realističtější VPS/managed hosting.
- Riziko: při zpomalení může přicházet nový `sync` dříve, než předchozí skončí; klient musí zabránit paralelním syncům téhož klienta nebo je pevně omezit.

### Výchozí osmimegabajtové limity nejsou přenositelné

Sekce §3 nastavuje `MAX_REQUEST_BYTES` a `MAX_RESPONSE_BYTES` na 8 MiB a zároveň požaduje, aby limity byly **pod** `post_max_size`, `memory_limit` a proxy limity. Standardní hodnota PHP `post_max_size` je 8M, takže výchozí `MAX_REQUEST_BYTES = 8 * 1024 * 1024` není pod ní a navíc nezohledňuje režii či nižší limit webserveru/WAF. `upload_max_filesize` není rozhodující pro raw NDJSON POST; relevantní jsou hlavně `post_max_size` a limity před PHP.

- Stav: přijímání POST je **běžně dostupné**; možnost navýšit PHP/proxy limity **často chybí** a **nelze ji doinstalovat uživatelem**.
- Náhrada: Composer nepomůže. Snížit všechny aplikační limity podle skutečně naměřeného minima a ponechat rezervu; preflight musí odeslat reálný NDJSON body přes celou cestu.
- Paměť: implementace musí skutečně hashovat a parsovat po částech. Pokud kvůli pořadí v §5 nejprve uloží celé tělo před ověřením HMAC, musí použít omezený dočasný soubor ve stejném privátním prostoru, ne neomezený řetězec v RAM.

### Inode, I/O a nonce ochrana

Sekce §5 ukládá každou nonce na dvě minuty a klient volá čtyřikrát za sekundu. To může znamenat přibližně 480 živých nonce záznamů na aktivního klienta, podle konkrétní implementace případně 480 souborů. K tomu přibývají `.writing`, `.data`, `.sending.*` a `queue.lock` (§7–§10). Shared hosting běžně účtuje inode a omezuje I/O i když zbývá dost bajtů.

- Stav: diskový prostor je **běžně dostupný**; vysoká inode/I/O rezerva **často chybí** a **nelze ji doinstalovat uživatelem**.
- Náhrada: Composer nepomůže. Čistě PHP úprava může ukládat nonce do malého počtu rotovaných, zamykaných časových bucketů namísto souboru na nonce; musí ale zachovat atomickou kontrolu replay a krátký lock. Je nutný globální byte/inode limit celé instalace, ne jen `MAX_BUFFERS` jednotlivé fronty.
- GC bez cronu je slučitelný se shared hostingem, ale uklízí jen při requestech. Fronta klienta, který se už nikdy neozve, může držet `.sending` a nonce artefakty déle, pokud implementace nemá oportunistický omezený globální úklid.

### Lock contention a timeout

`flock` v §7 a §8 je blokující, pokud implementace nepoužije krátký limitovaný retry. Při souběhu mnoha odesílatelů do jednoho cíle může request čekat na lock až do `max_execution_time`; zabitý request pak klient retryne a zvýší nápor.

- Stav: `flock` je **běžně dostupné**, ale dostatečný výkon úložiště **často chybí**; parametry filesystemu **nelze doinstalovat uživatelem**.
- Náhrada: žádná Composer náhrada. Implementace má použít `LOCK_NB` s krátkým, pevným časovým rozpočtem a při vyčerpání vrátit řízené `503`/`429`, ne držet worker neomezeně.

### WAF, rate limiting a veřejná dostupnost endpointu

Limity v §3 a validace v §5–§6 omezují práci jednoho requestu, ale veřejný endpoint musí před ověřením HMAC přijmout a zahashovat tělo, protože podpis zahrnuje jeho SHA-256. Útočník může opakovaně posílat maximální povolená těla. Shared hosting může naopak legitimní provoz 4 req/s/klient vyhodnotit jako abuse.

- Stav: základní ochrana hostingu je **běžně dostupná**, konfigurovatelná per-endpoint pravidla **často chybí** a **nelze je doinstalovat uživatelem**.
- Náhrada: Composer nenahradí ochranu před spotřebou přenosu před spuštěním PHP. Aplikace může kontrolovat `Content-Length`, streamovat s hard limitem a zavést vlastní rate limit po úspěšné autentizaci, ale nemůže zastavit provoz, který saturuje linku nebo proxy.
- Dopad: nejde vždy o blokér prvního spuštění, ale o významné bezpečnostní a provozní riziko veřejné produkce.

### Trvanlivost a charakter doručení

Sekce §7 používá `fflush`, nikoli vynucený zápis na fyzické úložiště, a §10 dovoluje zahodit data po `QUEUE_TTL_SECONDS = 60`. At-least-once deduplikace řeší přerušenou HTTP odpověď, nikoli výpadek hostingu, ztrátu lokálního disku nebo expiraci při delším omezení účtu.

- Stav: persistentní účetní úložiště je **běžně dostupné**, garantovaná durability/SLA **často chybí** a **nelze ji doinstalovat uživatelem**.
- Náhrada: žádná čistě PHP/Composer náhrada za garantované úložiště. Delší TTL pouze mění kompromis mezi ztrátou dat a inode/kvótou.
- Dopad: návrh je provozně best-effort relay, ne garantovaná fronta. Toto musí být deklarováno klientovi a provozovateli.

### Uložení tajemství v distribuovaném PHP souboru

`SERVER_SECRET` a všechny access tokeny jsou podle §3 přímo v `phpvpn.php`. Na shared hostingu je může číst administrátor poskytovatele, zálohovací systém a kdokoli s přístupem k účtu; při chybě PHP handleru může být zdrojový soubor vydán jako text.

- Stav: izolace souborů účtu je **běžně dostupná**, silnější secret management **často chybí** a **nelze jej doinstalovat uživatelem**.
- Náhrada: Composer nepomůže. Pokud hosting dovolí soubor mimo webroot nebo environment proměnné, načítat tajemství odtud; jinak nastavit minimální oprávnění, zakázat zálohování veřejnou cestou a počítat s důvěrou v poskytovatele. To je doporučená bezpečnostní úprava §3, ne podmínka syntaktického spuštění.

## Minimální preflight před povolením `sync`

1. Ověřit skutečnou verzi PHP 8.2+, `function_exists` pro všechny použité hashovací a souborové funkce, `sha256` a úspěšný `random_bytes`.
2. Ověřit zapisovatelnost, kvótu, vytvoření souboru v režimu `x+b`, úplný zápis, `fflush`, atomický rename a unlink v cílovém adresáři.
3. Spustit souběžný HTTP test `flock` a claimu jedné dávky minimálně ze dvou workerů; nestačí unit test v jediném procesu.
4. Z externího klienta dokázat, že queue root vrací 403/404 a nelze stáhnout ani vylistovat žádný artefakt. Preferovat umístění mimo webroot.
5. Přes HTTPS ověřit doručení `Authorization`, všech `X-PHPVPN-*` hlaviček, content typů a reálného NDJSON těla; ověřit, že proxy/WAF odpověď necachuje.
6. Změřit skutečné limity requestu, response, PHP času a paměti a snížit konstanty §3 s rezervou. Neodvozovat je jen z `ini_get`, protože proxy může mít nižší strop.
7. Zátěžovým testem §12.8 ověřit cílový počet klientů, 4 req/s/klient, prázdný polling, plnou dávku, lock contention, inode růst, `429`/`503` a klientský backoff.
8. Ověřit správný UTC čas serveru a chování po restartu PHP workeru, zaplnění fronty a krátké nedostupnosti disku.

## Doporučené rozhodnutí

- **Shared hosting, několik klientů, nízký provoz:** podmíněně podporovat po úspěšném preflightu, přesunutí fronty mimo webroot (nebo prokázaném deny pravidle) a snížení limitů/intervalu podle kvót.
- **Shared hosting, desítky klientů nebo VPN provoz s vyšší propustností:** nepovažovat za spolehlivě podporovaný profil. Hlavním omezením je počet requestů a souborové I/O, nikoli chybějící PHP extension.
- **VPS/managed single-node PHP hosting:** vhodnější produkční profil, protože lze garantovat filesystem, TLS/proxy konfiguraci, worker limity a monitoring, aniž by se měnila krátká request/response architektura návrhu.
- **Serverless, autoscaling nebo multi-node hosting bez společné korektní fronty:** nepodporovat; odporuje explicitnímu provoznímu modelu §2.

Technické opory: [Hash je v podporovaných verzích vždy zapnuté core rozšíření](https://www.php.net/manual/en/hash.installation.php), [JSON je od PHP 8 vždy zapnuté core rozšíření](https://www.php.net/manual/en/json.installation.php), [`random_bytes` a jeho systémové zdroje](https://www.php.net/manual/en/function.random-bytes.php), [`flock` je advisory file lock](https://www.php.net/manual/en/function.flock.php), [`post_max_size`, `memory_limit` a další core limity](https://www.php.net/manual/en/ini.core.php).
