# Rizika nasazení PHP VPN na hostingových službách

## Závěr

Navržený server lze provozovat na **vlastním VPS/dedikovaném serveru** nebo na kvalitním managed hostingu s PHP-FPM a zapisovatelným lokálním diskem. Nelze jej ale spolehlivě označit za řešení pro „většinu“ běžných sdílených PHP hostingů. Zásadní překážkou není jeden soubor PHP, ale dlouhá HTTP spojení, současně obsazené PHP workery a fronta založená na souborovém systému.

Pro širokou kompatibilitu musí mít produkt dva jasně oddělené provozní profily:

1. **Standardní hostingový profil:** krátké requesty/polling, omezený počet klientů, přísná konfigurace a explicitně nižší propustnost.
2. **Plný VPN relay profil:** VPS nebo managed PHP-FPM s ověřenými limity; dlouhé `push`/`pull` streamy a souborová fronta jsou zde podporovaným předpokladem.

Bez tohoto rozdělení hrozí, že aplikace bude na mnoha hostinzích sice nahratelná, ale při zátěži bude náhodně ztrácet spojení, blokovat ostatní web nebo doručovat pakety se značnou latencí.

## Blokující a vysoce kritická rizika

| Riziko | Dopad | Proč je běžné na hostingu | Nutné opatření |
|---|---|---|---|
| Dlouhé `push` a `pull` requesty | Odpojování klientů, 502/504, vyčerpání workerů | Sdílené hostingy mají krátké limity requestu a nízký počet souběžných PHP procesů | Podporovat short polling; streamovací režim povolit jen po ověření prostředí |
| Dva workery na každého klienta | Web přestane obsluhovat další požadavky | Deset klientů znamená zhruba dvacet dlouho obsazených workerů; limity bývají podstatně nižší | Limit připojených klientů, separátní PHP-FPM pool/VPS, diagnostika kapacity |
| Proxy/CDN buffering | `pull` se k klientovi neflushuje průběžně | Apache/nginx/proxy/CDN mohou odpověď bufferovat; hlavička `X-Accel-Buffering` funguje jen pro některé nginx konfigurace | Neumisťovat endpoint za cache/CDN, vyžadovat end-to-end test streamu; mít polling fallback |
| Vynucené limity hostitele | PHP kód je nemůže bezpečně obejít | `max_execution_time`, `max_input_time`, timeout FastCGI, proxy timeout a limit těla se uplatňují na různých vrstvách | Instalační kontrola musí porovnat dostupné limity s profilem a odmítnout nepodporovaný režim |
| Souborová fronta a `flock` | Duplicity, poškozené pořadí nebo nedostupná fronta | Některé hostingy používají síťový/NFS filesystem, více webových uzlů nebo omezené sdílené úložiště | Oficiálně podporovat pouze jeden aplikační uzel s lokálním persistentním diskem; ověřit locking testem |
| Horizontální škálování hostingu | Každý request může vidět jinou, prázdnou frontu | Platformy s více instancemi a ephemeral diskem nejsou pro lokální frontu vhodné | Zakázat deployment na autoscaling/serverless platformách bez sdíleného, správně zamykatelného úložiště |

## Konkrétní omezení podle vrstev

### PHP runtime

- `max_execution_time` a případně `max_input_time` musí být bezpečně vyšší než 15 sekund plus síťová rezerva. Někteří poskytovatelé limity vynucují mimo `php.ini` nebo ignorují `set_time_limit()`.
- Funkce `set_time_limit`, `ignore_user_abort`, `fsync`, `random_bytes`, `hrtime` či možnost měnit runtime nastavení nemusí být na starších nebo restriktivních instalacích dostupná v očekávaném režimu. Návrh vyžaduje PHP 8.2+, což samo o sobě vyřadí část levných hostingů.
- `memory_limit` se sdílí s běžným webem. Nastavené aplikační limity nejsou náhradou za příliš malý limit PHP procesu ani za limit hostitele na velikost HTTP těla.
- PHP může běžet přes CGI/FastCGI s vlastním timeoutem a s ukončením procesu při nečinném čtení vstupu. To se projeví i když vlastní PHP timeout vypadá správně.
- `flush()` a `ob_flush()` nejsou zárukou, že data opustila PHP, webserver a reverzní proxy. Je nutný reálný test z externího klienta.

### Webserver, proxy, CDN a WAF

- `Connection: keep-alive` nelze v PHP vynutit; konečné chování řídí webserver a proxy.
- Web aplikační firewally a ModSecurity mohou blokovat NDJSON, neobvyklé hlavičky Authorization, binární/base64 obsah nebo dlouhé POST requesty. Chyba pak může být 403 bez možnosti opravy v aplikaci.
- CDN, cache a bezpečnostní proxy mohou request ukončit, request body limitovat nebo odpověď bufferovat. Endpoint nesmí být cacheovaný a musí jít z cache/CDN explicitně vyjmout.
- Některé hostingy nepovolují vlastní konfiguraci nginx/Apache, `.htaccess`, ani spolehlivý zákaz přímého HTTP přístupu k adresáři fronty. Pokud se `phpvpn/` nachází pod document rootem, je to bezpečnostní riziko.
- HTTPS je nutné pro ochranu přenosu i HMAC metadat, ale na levných hostinzích může být vynucování HTTPS, správná detekce reverse proxy a výjimka z CDN komplikovaná.

### Filesystem a oprávnění

- Aplikace zapisuje mnoho malých souborů. Sdílené hostingy běžně limitují počet inode/souborů, diskovou kvótu nebo I/O; limit 128 batchů na až 253 front může sám vytvořit desítky tisíc souborů včetně locků a diagnostických artefaktů.
- Adresář vedle skriptu nemusí být zapisovatelný, zejména při nasazení přes Git/deploy nástroj nebo u read-only release adresářů. Fronta má být konfigurovatelně umístěná mimo document root v persistentním, privátním a zapisovatelném úložišti.
- `chmod(0700)` nemusí na Windows hostingu, ACL filesystemu nebo sdílené platformě poskytovat zamýšlenou izolaci. Instalační kontrola musí ověřit skutečnou nepřístupnost přes web, ne pouze vracet úspěch z `chmod`.
- Atomické `rename()` je bezpečné pouze na témže filesystemu. Temporary soubor proto musí vznikat přímo v cílovém adresáři; nesmí používat systémový temp adresář na jiném volume.
- `flock()` není univerzální distribuovaný lock. Na NFS, SMB, multi-node storage nebo při nestandardním PHP handleru nemusí garantovat, co návrh předpokládá. Test existence locku nestačí; je potřeba souběžný integrační test.
- Antivirus, zálohování nebo indexace souborů mohou na některých prostředích zvyšovat latenci a dočasně blokovat rename/unlink, zvlášť na Windows hostingu.

### Kapacita, dostupnost a síťové chování

- Při návrhu dvou dlouhých spojení na klienta obsadí desítky klientů desítky PHP workerů téměř trvale. To je nekompatibilní s typickým modelem sdíleného hostingu, kde PHP worker patří celému webu a má malý procesní limit.
- Aktivní čekání v `pull` pomocí `usleep` je sice nenáročné na CPU, ale stále drží worker. Na hostingu se počítá jako aktivní dlouhý request.
- HTTP není přesně-doručovací transport. Design správně připouští at-least-once doručení, ale klient musí mít perzistentní deduplikaci UUID a jasná pravidla pro reorder/replay; jinak budou výpadky vypadat jako chyby VPN.
- Globální pořadí mezi více odesílateli nelze se souborovou frontou garantovat. U IP provozu je třeba explicitně rozhodnout, zda klient umí pracovat s out-of-order datagramy a duplicitami.
- Časové mazání po 60 sekundách může při přetížení, pomalém hostingu nebo dočasném výpadku klienta ztrácet legitimní data. Je nutná explicitní provozní politika: best-effort relay versus garance doručení.

## Nesoulady a rizika přímo v návrhu

- Výchozí požadavek mluví o „většině hostingů“, ale detailní návrh už předpokládá PHP 8.2+, vypnuté buffering/compression, řízení PHP-FPM/Apache workerů, HTTPS reverse proxy a privátní zapisovatelný disk. To jsou požadavky na spravované serverové prostředí, ne na univerzální sdílený hosting.
- `X-Accel-Buffering: no` není přenositelné řešení. Je to nginx-specifická nápověda a může být ignorována.
- `output_buffering=Off` a `zlib.output_compression=Off` aplikace nemusí smět měnit a nadřazená proxy může bufferovat i přesto.
- Návrh správně upozorňuje na NFS/multi-server omezení, ale musí z toho udělat formální hard requirement v dokumentaci i v instalátoru. Jinak bude uživatel předpokládat podporu běžného cloud hostingu.
- Cíl skrývat frontu pouze konfigurací webserveru není na hostinzích bez konfigurace dostačující. Nejbezpečnější varianta je úložiště mimo webroot; pokud ho hostitel neposkytuje, dané prostředí není vhodné pro produkci.
- Režim `legacy-minute` je pro produkční nasazení nevhodný kvůli replay oknu. Pokud zůstane kvůli kompatibilitě, musí být výslovně vypnutý ve výchozím produkčním nastavení a nemá být vydáván za bezpečný fallback.

## Doporučené změny pro širší kompatibilitu

1. Udělat transportní režim konfigurovatelný: výchozí **short polling** s krátkým limitem requestu, volitelný streaming pouze pro ověřený VPS/managed profil.
2. Přidat instalační/preflight endpoint nebo samostatný checker, který otestuje PHP verzi, zapisovatelnost, atomický rename, souběžný `flock`, dostupný disk, limity souborů a reálné streamování z externího klienta.
3. Zavést konfigurovatelný `QUEUE_ROOT` mimo document root a při chybě instalaci/provoz odmítnout, ne jen logovat varování.
4. Do dokumentace dát podporovanou matici: single-node local disk = podporováno; shared hosting bez záruky workerů = pouze polling/best effort; NFS/multi-node/serverless = nepodporováno.
5. Stanovit maximální počet klientů podle ověřeného počtu workerů a rezervy pro běžný web. Aplikace má odmítat nová dlouhá spojení dříve, než vyčerpá celou kapacitu.
6. Zajistit klientský reconnect s exponenciálním backoffem, jitterem, deduplikací UUID a telemetrií důvodů odpojení. Bez toho budou limity hostingu generovat synchronizované reconnect bouře.
7. Omezit počet souborů i celkový byte budget na frontu a celou instalaci; přidat bezpečný úklid diagnostických souborů a kontrolu volného místa.
8. Poskytnout konkrétní konfigurace pro Apache/mod_php, Apache+FPM, nginx+FPM a IIS/FastCGI, ale neprohlašovat je za univerzálně použitelné bez kontroly možností poskytovatele.

## Minimální produkční checklist hostingu

- PHP 8.2 nebo novější, `random_bytes`, `hrtime`, souborové zámky a bezpečný zápis/rename fungují v reálném účtu.
- Jediný aplikační uzel, persistentní lokální disk a soukromý queue root mimo veřejný web.
- Dostatečný limit PHP workerů pro zvolený počet klientů nebo použitý krátký polling režim.
- Všechny timeouty na trase klient → proxy → webserver → PHP jsou ověřeně vyšší než zvolený request timeout.
- Žádné CDN/WAF buffering či blokování na VPN endpointu, nebo je pro něj ověřená výjimka.
- HTTPS, vypnuté cacheování endpointů, skryté PHP chyby a omezený přístup k souborům fronty.
- Zátěžový test se souběžnými `push`/`pull`, simulací odpojení, plným diskem/frontou a restartem PHP workeru.

## Doporučené rozhodnutí

Projekt má být prezentován jako **PHP relay pro řízené serverové prostředí**, nikoli jako univerzální VPN server pro libovolný sdílený hosting. Pokud je obchodním požadavkem co nejširší hostingová kompatibilita, je nutné změnit výchozí transport na krátké polling requesty a zúžit deklarované provozní garance. Pokud je prioritou výkon a nízká latence, je správným cílem VPS/managed PHP-FPM, kde lze garantovat workery, timeouty, disk a konfiguraci proxy.
