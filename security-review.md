# Bezpečnostní revize `phpvpn.php` proti `design2.md`

Datum revize: 2026-08-08  
Rozsah: statická, evidence-based revize `C:\git\phpvpn\phpvpn.php` proti `C:\git\phpvpn\design2.md`  
Kontrolovaný SHA-256 `phpvpn.php`: `BFF4892D5CC7ED784F7223D61DA3D70AA5401A779260C5A586A5C6A7563F23C4`  
Kontrolovaný SHA-256 `design2.md`: `E93DD19A8B6C4A7367FEF84F9B8141E0365639F1F5C2FC627192F29AA397E8A9`

## Závěr

Implementace má kvalitní základ: povinný první `auth` záznam, streamovaný hash přes přesné bajty zbytku NDJSON, konstantní porovnání podpisu, přísné skládání cest z validovaných hodnot, per-frontové `flock`, publikování přes `.writing` a atomický `rename`, at-least-once vyzvednutí přes `.sending`, malé obecné chyby a `no-store` hlavičky. Proti aktuálnímu `design2.md` však není připravena k bezpečnému produkčnímu nasazení bez oprav níže.

Souhrn priorit:

| Priorita | Počet |
|---|---:|
| Kritické | 0 |
| Vysoké | 5 |
| Střední | 7 |
| Nízké | 2 |

Nejvyšší rizika jsou obejití povinného HTTPS podvrženou proxy hlavičkou, závod v perzistentní ochraně nonce, nevynucovaná globální disková/inode kvóta, násobení per-lock timeoutu přes mnoho cílových front a neúplná validační brána konfigurace/tokenů.

## Nálezy

### V-01 — `REQUIRE_HTTPS` lze obejít klientským `X-Forwarded-Proto`

**Priorita:** Vysoké

**Konkrétní důkaz v implementaci:** `TRUST_PROXY_HEADERS` je ve výchozí konfiguraci `true` (`phpvpn.php:107-109`). `Http::isHttps()` považuje požadavek za HTTPS, pokud první hodnota `HTTP_X_FORWARDED_PROTO` je `https`, aniž ověří `REMOTE_ADDR`, seznam důvěryhodných proxy nebo to, zda požadavek skutečně přišel přes proxy (`phpvpn.php:335-354`). `SyncHandler::assertPreconditions()` pak tomuto výsledku důvěřuje (`phpvpn.php:1798-1805`). Přímý HTTP klient tedy může poslat hlavičku `X-Forwarded-Proto: https` a projít kontrolou.

**Dopad:** Pokud je origin dostupný přímo přes HTTP — běžná chyba na sdíleném hostingu nebo vedle reverzní proxy — útočník obejde požadavek na transportní TLS. Odposlechnutý platný požadavek lze při výchozím `PERSIST_NONCES=false` replayovat v časovém okně; zároveň unikají autentizační metadata, směrovací metadata a šifrovaný provoz pro traffic analysis. Samotný access token se v požadavku neposílá, což dopad omezuje, ale nenahrazuje HTTPS.

**Porušený návrh:** §2 ř. 26-31 (HTTPS jako provozní předpoklad), §4 ř. 71 a §11 ř. 184 (nasazení vyžaduje HTTPS).

**Doporučená oprava:** Nastavit výchozí `TRUST_PROXY_HEADERS=false`. Proxy hlavičku uznat pouze tehdy, když `REMOTE_ADDR` patří do explicitně nakonfigurovaného seznamu důvěryhodných proxy; jinak kontrolovat pouze `HTTPS`/port 443. Současně na originu zakázat přímý HTTP přístup na úrovni webserveru.

### V-02 — replay lze v režimu `PERSIST_NONCES=true` přijmout dvakrát přes hranici bucketu

**Priorita:** Vysoké

**Konkrétní důkaz v implementaci:** `NonceStore::claim()` zamyká pouze soubor aktuálního minutového bucketu (`phpvpn.php:930-942`). Starší buckety čte přes nezamčené `file_get_contents()` (`phpvpn.php:943-948`). Požadavek zahájený těsně před hranicí minuty zapisuje do bucketu N, zatímco replay zahájený těsně po hranici zamyká bucket N+1. Pokud druhý worker přečte N před zápisem prvního workeru, zapíše nonce do N+1; první worker bucket N+1 vůbec nekontroluje a následně zapíše stejnou nonce do N. Oba vrátí `true`. Časové okno 60 sekund stejný podepsaný požadavek přes hranici minuty připouští (`phpvpn.php:866-868`).

**Dopad:** Volitelný „tvrdý“ režim ochrany proti replay není pod souběhem tvrdý. Zachycený nebo duplicitně odeslaný platný požadavek může dvakrát vložit pakety nebo provést vyzvednutí/ack. Výchozí režim `false` replay výslovně připouští; tento nález se týká pouze režimu `true`.

**Porušený návrh:** §5 ř. 87-91 požaduje uložení a kontrolu pod per-klientským lockem; §12 ř. 193 a akceptační kritérium §13 ř. 207 požadují odmítnutí replay v perzistentním režimu.

**Doporučená oprava:** Použít jeden stabilní per-klientský lock nezávislý na čase (např. `_nonce/004/nonce.lock`). Pod tímto lockem atomicky zkontrolovat všechny relevantní buckety a zapsat aktuální záznam. Testovat dvě kopie stejného podepsaného těla na dvou workerech s `Ctx::$now` na obou stranách hranice bucketu.

### V-03 — globální limit diskových bajtů a inode je pouze orientační

**Priorita:** Vysoké

**Konkrétní důkaz v implementaci:** `Quota::allows()` při chybějícím nebo starém `global.stat` výslovně povolí zápis (`phpvpn.php:1374-1383`). Snapshot je v rámci requestu staticky cachovaný (`phpvpn.php:1352-1371`) a po každém úspěšném enqueue se neinkrementuje ani nerezervuje. Jeden požadavek tak může proti témuž snapshotu schválit mnoho cílových batchů. Agregát aktualizuje až oportunistický sweeper nejvýše jednou za 60 sekund (`phpvpn.php:1607-1618`, `1644-1702`); `Quota::store()` navíc ignoruje chybu neatomického zápisu (`phpvpn.php:1398-1407`). Reálný zápis kontroluje tvrdě pouze `MAX_BUFFERS` jednotlivé fronty (`phpvpn.php:1437-1451`).

**Dopad:** Autorizovaný nebo kompromitovaný klient může mezi dvěma snapshoty překročit `MAX_TOTAL_QUEUE_FILES` i `MAX_TOTAL_QUEUE_BYTES`; při chybějícím/starém agregátu je globální limit zcela fail-open. Horní skutečná mez se posouvá k součtu `MAX_BUFFERS` přes všechny klientské fronty, což může na sdíleném hostingu vyčerpat disk nebo inode a vyřadit PHP aplikaci i jiné aplikace stejného účtu.

**Porušený návrh:** §11 ř. 180-184 a akceptační kritérium §13 ř. 210 vyžadují explicitní limity disku/fronty a kontrolovaný růst.

**Doporučená oprava:** Zavést tvrdou rezervaci globální kvóty pod krátkým globálním lockem: před publikováním atomicky načíst, ověřit a navýšit počty; při selhání publikace rezervaci vrátit. Stav zapisovat přes dočasný soubor + `fflush` + atomický `rename` a při nečitelném/starém stavu fail-closed (`503`), nikoli fail-open. Per-frontové počítadlo může zůstat shardem, ale nesmí být autoritou pro okamžité přijetí bez rezervace.

### V-04 — 200ms lock budget se násobí počtem cílových front

**Priorita:** Vysoké

**Konkrétní důkaz v implementaci:** Každé `QueueLock::acquire()` opakuje `flock(LOCK_NB)` s `usleep()` až 200 ms (`phpvpn.php:1125-1142`). `SyncHandler::publish()` prochází všechny cíle a pro každý postupně volá `Queue::enqueue()`/nový lock (`phpvpn.php:1960-1975`). Validní rozsah dovoluje až 253 různých cílů. Teoretický samotný lock wait je tedy přibližně `253 × 200 ms = 50,6 s`, bez zápisů, GC a následného locku vlastní fronty. `REQUEST_BUDGET_MS=250` není ukončovací budget; používá se až při sestavení doporučení (`phpvpn.php:2086-2096`). `set_time_limit(30)` (`phpvpn.php:2351-2353`) není náhradou za jeden absolutní aplikační deadline a na různých SAPI/OS odlišně započítává blokující I/O/sleep.

**Dopad:** Několik autorizovaných souběžných požadavků může dlouho držet PHP workery a vyčerpat malý worker pool sdíleného hostingu. Request může být ukončen uprostřed publikování po úspěchu některých cílů, což zesiluje částečné výsledky a retry duplicity.

**Porušený návrh:** §4 ř. 73 vyžaduje rychlé ukončení bez polling/sleep; §11 ř. 180 a 184 požaduje krátké požadavky a pevný limit souběhu.

**Doporučená oprava:** Zavést jeden absolutní deadline pro celý request a před každým lockem použít jen zbývající čas. Omezit počet různých destinací na request nebo po vyčerpání budgetu další položky bezpečně odložit. Lock retry nesmí v součtu přesáhnout globální request budget. Stejný deadline použít pro nonce lock, enqueue, collect a oportunistický sweep.

### V-05 — neplatná/duplicitní konfigurace tokenů neblokuje `sync`

**Priorita:** Vysoké

**Konkrétní důkaz v implementaci:** `Cfg::assertRuntime()`, volané na každém `sync`, kontroluje délku secretu a jen část číselných limitů, nikoli celý `ACCESS_TOKENS`, jeho IP klíče, 64hex hodnoty ani duplicity (`phpvpn.php:195-215`, volání `1764-1767`). Tyto kontroly jsou pouze v `Cfg::diagnose()` (`phpvpn.php:235-274`), které se používá jen v defaultně vypnutém selftestu (`phpvpn.php:111-113`, `2182-2187`). `Auth::verify()` ověří pouze token právě tvrzeného klienta a ostatní chybné položky ignoruje (`phpvpn.php:886-905`).

**Dopad:** Konfigurace, kterou návrh označuje za neplatnou, může částečně obsluhovat provoz místo povinného `503`. Zvlášť nebezpečné jsou dva klienti se stejným validním tokenem: protože oba pro protokol znají i společný `SERVER_SECRET`, mohou vypočítat odvozený klíč pro IP druhého klienta a vydávat se za něj. Samotný kód duplicitu už považuje za diagnostický problém (`phpvpn.php:259-261`), ale bezpečnostní brána ji nevynucuje.

**Porušený návrh:** §3 ř. 59 vyžaduje validaci konfigurace při startu každého požadavku a `503` pro neplatnou konfiguraci; §5 ř. 85-93 staví identitu na kombinaci klientské IP a jejího tokenu.

**Doporučená oprava:** Před čtením těla `sync` provést úplnou validaci celé tabulky: přesný IP rozsah, přesně 64 lowercase hex, zákaz placeholderu, zákaz prázdné tabulky a zákaz duplicit tokenů. Jakákoli chyba musí vrátit obecné `503 server_misconfigured`; konkrétní diagnostiku ponechat jen lokálnímu/admin selftestu.

### S-01 — odvozený HMAC klíč neodpovídá přesnému volání v návrhu

**Priorita:** Střední

**Konkrétní důkaz v implementaci:** Návrh uvádí `hash_hmac('sha256', clientIp . "\n" . accessToken, SERVER_SECRET)` bez čtvrtého parametru (§5 ř. 85). V PHP má parametr `$binary` výchozí hodnotu `false`, tedy návrat je 64 lowercase hex znaků. Implementace předává `true` a jako druhý HMAC klíč používá 32 raw bajtů (`phpvpn.php:894-901`). Oficiální dokumentace PHP potvrzuje signaturu `bool $binary = false` a rozdíl hex/raw: <https://www.php.net/manual/en/function.hash-hmac.php>.

**Dopad:** Klient implementovaný přesně podle `design2.md` vypočítá jiný podpis a obdrží `401` u každého `sync`. Nejde o oslabení kryptografické síly raw varianty, ale o úplnou nekompatibilitu autentizačního protokolu a riziko více neslučitelných klientských implementací.

**Porušený návrh:** §5 ř. 85-90 (přesná derivace a podpis).

**Doporučená oprava:** Pro shodu s aktuálním návrhem odstranit `true` u prvního `hash_hmac` a doplnit pevné test vectors do návrhu/testů (secret, token, canonical body a očekávaný podpis). Pokud má být raw varianta záměrná, musí být nejprve explicitně změněna normativní specifikace; nelze ponechat obě interpretace.

### S-02 — selhání jedné cílové fronty vede k částečnému `200`, nikoli chybě celého requestu

**Priorita:** Střední

**Konkrétní důkaz v implementaci:** `publish()` pokračuje po selhání cíle, přesune jeho UUID do `deferred`, ale UUID již uložených cílů ponechá v `accepted` (`phpvpn.php:1973-1989`). `429`/`503` vyhodí pouze tehdy, když neuspěl žádný cíl (`phpvpn.php:1991-1996`). Samotný komentář v hlavičce tuto odchylku výslovně přiznává (`phpvpn.php:20-24`).

**Dopad:** Multi-destination request může skončit HTTP 200, i když byla některá fronta plná nebo nedostupná. `acceptedPacketUuids`/`deferredPacketUuids` sice brání tvrzení, že neuložený paket byl přijat, ale server porušuje transakční a chybovou semantiku aktuálního návrhu. Klienti, monitoring a backoff založené na požadovaných kódech `429`/`503` se chovají jinak.

**Porušený návrh:** §7 ř. 124-130 říká, že jedna neúspěšná cílová fronta je řízená chyba celého požadavku; §6 ř. 108 určuje `429` a `503`.

**Doporučená oprava:** Implementovat all-or-nothing publikování pomocí deterministicky seřazených rezervací/locků a dvoufázového stagingu; teprve po úspěšné rezervaci všech cílů publikovat `.data`. Při jakémkoli selhání odstranit staging a vrátit `429` nebo `503` bez přijatých paketů.

### S-03 — nedostupná vlastní fronta se maskuje jako úspěšný prázdný výsledek

**Priorita:** Střední

**Konkrétní důkaz v implementaci:** Když `Queue::collect()` nezíská lock, nastaví `busy=true`, `fill=1.0`, ale ponechá `claimed=[]` a `truncated=false` a nevyhodí chybu (`phpvpn.php:1491-1502`). `SyncHandler` výsledek vždy předá do úspěšné odpovědi (`phpvpn.php:1792-1795`, `2009-2026`); `busy` pouze sníží doporučení (`phpvpn.php:2011-2013`, `2080-2083`).

**Dopad:** Prázdný polling request při lock contention dostane `200` a `responseTruncated=false`, přestože server nebyl schopen zjistit nebo vyzvednout čekající data. Klient nedostane požadovaný `503`/backoff signál a řídicí záznam nevyjadřuje autoritativní stav fronty.

**Porušený návrh:** §6 ř. 108 (`503` pro nedostupnou frontu), §8 ř. 140-146 (přesný `responseTruncated`) a §9 ř. 160 (backoff na `503`).

**Doporučená oprava:** Pokud ještě nebyla provedena nevratná publikace, vrátit při collect lock timeoutu `503 queue_busy`. Pokud již byly pakety uloženy, protokol musí výslovně vrátit stav „výstupní fronta neověřena“; minimálně nesmí tvrdit `responseTruncated=false`. Čistší oprava je rezervovat/claimnout vlastní frontu před publikováním vstupu nebo přidat idempotentní request ID pro bezpečný `503` retry.

### S-04 — výchozí úložiště spoléhá na Apache `.htaccess` a neověřuje ochranu/práva

**Priorita:** Střední

**Konkrétní důkaz v implementaci:** Prázdné `QUEUE_ROOT` ukládá stav do `__DIR__/phpvpn`, tedy typicky pod document root vedle veřejného skriptu (`phpvpn.php:45-51`, `1015-1022`). `ensureRoot()` pouze zkusí vytvořit `.htaccess` a `index.html`, ignoruje chybu zápisu a přesto vrátí `true` (`phpvpn.php:1027-1047`). `.htaccess` nechrání Nginx/IIS a nemusí fungovat ani na Apache s `AllowOverride None`. `mkdirp()` u již existujícího adresáře nekontroluje ani neopraví mód, vlastníka nebo symlink (`phpvpn.php:1082-1094`). Selftest dokáže pouze varovat, neověří externí HTTP zákaz (`phpvpn.php:2312-2329`).

**Dopad:** Chybné nasazení na sdíleném hostingu může zveřejnit předvídatelný denní log `_logs/YYYY-MM-DD.log`, stavové soubory a při znalosti/odhalení názvu i šifrované batch soubory. Payload zůstává end-to-end šifrovaný, ale unikají IP/časová metadata, provozní informace a ciphertext; lokální spoluuživatel může číst data při příliš volných právech.

**Porušený návrh:** §2 ř. 26-35 deklaruje nezávislost na konkrétním webserveru; §7 ř. 120 vyžaduje adresáře 0700; §11 ř. 184 vyžaduje zákaz přímého HTTP přístupu a vlastnictví pouze účtem PHP procesu.

**Doporučená oprava:** Pro produkci vyžadovat explicitní absolutní `QUEUE_ROOT` mimo document root a při startu fail-closed ověřit, že nejde o symlink, adresář je zapisovatelný, má bezpečná práva a očekávaného vlastníka (na platformách, kde to lze). Pokud je fallback uvnitř webrootu nezbytný, musí instalační preflight provést skutečný externí HTTP probe a bez prokázaného 403/404 zakázat `sync`.

### S-05 — logy nemají byte/record limit a nejsou součástí kvóty

**Priorita:** Střední

**Konkrétní důkaz v implementaci:** Každá chyba se appenduje do denního souboru bez limitu velikosti nebo počtu záznamů (`phpvpn.php:400-430`). Retence je pouze časová (`phpvpn.php:1736-1752`). `_logs` se nezapočítává do `Quota`, která agreguje jen fronty pod `10_99_99_0` (`phpvpn.php:1645-1702`). Navíc jedno fan-out volání může vytvořit `enqueue_failed` pro každý odmítnutý cíl (`phpvpn.php:1984-1989`) a následně další top-level 429/503 log (`phpvpn.php:2359-2363`).

**Dopad:** Autorizovaný/kompromitovaný klient může po zaplnění front generovat velké množství logů a obejít všechny queue limity, až vyčerpá disk/inode. Logování tak při DoS stav zesiluje a může poškodit i ostatní aplikace účtu.

**Porušený návrh:** §11 ř. 180-186 požaduje ochranu diskové/inode kvóty a omezené provozní logování; akceptační kritérium §13 ř. 210 zakazuje nekontrolovaný růst.

**Doporučená oprava:** Přidat tvrdý denní/instalační byte limit, rotaci podle velikosti, sampling/rate-limit opakovaných kategorií a započítat logové bajty/inode do globální rezervace. Při dosažení limitu log bezpečně zahazovat, nikoli blokovat relay.

### S-06 — implementace vyžaduje volitelné `ctype`, ale nehlásí jej v preflightu

**Priorita:** Střední

**Konkrétní důkaz v implementaci:** `ctype_digit()` se volá při parsování `Content-Length` a při sweepu adresářů (`phpvpn.php:328-333`, `1652-1656`). Seznam požadovaných funkcí v selftestu `ctype_digit` neobsahuje (`phpvpn.php:2192-2209`) a hlavička souboru `ctype` rovněž neuvádí (`phpvpn.php:9-12`). PHP dokumentace potvrzuje, že extension je sice defaultně zapnutá, ale lze ji zkompilovat s `--disable-ctype`: <https://www.php.net/manual/en/ctype.installation.php>.

**Dopad:** Na jinak podporovaném PHP 8.2+ bez `ctype` skončí `sync` při čtení délky těla na zachyceném `Error` a vrátí obecné `500`; selftest přitom chybějící závislost předem neoznámí. To porušuje slib provozu bez nepovinných rozšíření.

**Porušený návrh:** §2 ř. 26-33 a akceptační kritérium §13 ř. 212.

**Doporučená oprava:** Nahradit `ctype_digit()` explicitní ASCII kontrolou bez extension (např. ukotveným regexem `[0-9]+` nebo ručním průchodem bajtů). Alternativní přidání `ctype` mezi požadavky by neodpovídalo aktuálnímu návrhu.

### S-07 — první batch může překročit `MAX_RESPONSE_BYTES`

**Priorita:** Střední

**Konkrétní důkaz v implementaci:** Výběr v `Queue::collect()` kontroluje překročení pouze tehdy, když už je alespoň jeden batch vybrán: `if ($claimed !== [] && $total + $size > $maxBytes)` (`phpvpn.php:1517-1536`). První batch větší než `MAX_RESPONSE_BYTES` se tedy vždy claimne a odešle. `Cfg::assertRuntime()` současně nepožaduje, aby maximální uložený cílový batch byl menší nebo roven `MAX_RESPONSE_BYTES` (`phpvpn.php:195-215`); platná konfigurace může mít velmi malou odpověď a velký přijímaný batch.

**Dopad:** Administrátorem deklarovaný response limit není tvrdý. Velký první batch může překročit limity reverzní proxy/hostingu, zvýšit dobu držení workeru a způsobit přerušenou odpověď/retry duplicity.

**Porušený návrh:** §3 ř. 43-57, §8 ř. 132-138 a §11 ř. 180 požadují vyzvedávání pouze do response stropu.

**Doporučená oprava:** Při enqueue rozdělit cílový batch na atomické soubory, z nichž každý se včetně delivery obálky vejde do `MAX_RESPONSE_BYTES`, nebo konfiguraci s možným oversized batchem odmítnout `503 server_misconfigured`. Collect nesmí claimnout soubor nad zbývající budget; oversized soubor má být řízeně odložen/označen, nikoli poslán nad limit.

### N-01 — několik validátorů je méně přesných než návrh

**Priorita:** Nízké

**Konkrétní důkaz v implementaci:** (1) Content-Type se testuje prefixem, takže projde například `application/x-ndjson-evil` (`phpvpn.php:1803-1806`), ačkoli návrh dovoluje jen dvě konkrétní media types. (2) UUID regex kontroluje pouze skupiny hex znaků a nevynucuje RFC 4122 version/variant bity (`phpvpn.php:451`, použití `1941-1945`). (3) timestamp regex dovoluje neexistující data a `gmmktime()` je normalizuje místo odmítnutí (`phpvpn.php:448`, `491-505`). (4) nonce regex ověřuje jen znakovou sadu a délku 16-64, nikoli kanonickou base64url délku (`phpvpn.php:449`, `858-863`).

**Dopad:** Nejde o přímé obejití HMAC — všechny přijaté hodnoty jsou stále podepsané — ale wire protokol přijímá hodnoty, které klient podle návrhu nemá generovat. To zvyšuje riziko nekompatibility, nekonzistentní deduplikace a rozdílného parsování mezi implementacemi.

**Porušený návrh:** §4 ř. 71, §5 ř. 79-87 a §6 ř. 97-106.

**Doporučená oprava:** Media type rozdělit na typ a parametry a typ porovnat přesně; timestamp parsovat striktním UTC parserem s kontrolou chyb a zpětného formátu; UUID validovat včetně RFC varianty/verze; nonce base64url dekódovat strict režimem a vyžadovat kanonické znovuzakódování.

### N-02 — endpoint nevynucuje, že URL obsahuje pouze `action=sync`

**Priorita:** Nízké

**Konkrétní důkaz v implementaci:** `Http::action()` načte pouze `$_GET['action']` a ostatní query parametry ignoruje (`phpvpn.php:316-321`). Router ani `assertPreconditions()` nekontrolují další klíče (`phpvpn.php:2372-2424`, `1798-1811`). Požadavek `?action=sync&token=...&nonce=...` je proto normálně zpracován, pokud tělo a podpis sedí.

**Dopad:** Implementace sama token/signature/nonce do URL nevkládá ani je nečte, ale nezastaví chybně implementovaného klienta, který je přidá. Takové hodnoty se pak mohou uložit v access logu hostingu, proxy nebo WAF, přesně čemu chce návrh zabránit.

**Porušený návrh:** §4 ř. 71 a §5 ř. 79, 93.

**Doporučená oprava:** Pro `sync` povolit přesně jeden scalar query parametr `action=sync`; jakékoli další parametry odmítnout obecnou `400 bad_request` ještě před čtením těla. Access logy infrastruktury současně nakonfigurovat tak, aby query string neukládaly.

## Ověřené splněné vlastnosti

Následující body byly ověřeny přímo statickým trasováním implementace; výjimky jsou výslovně navázány na nálezy výše.

1. **Povinný první HMAC záznam:** `SyncHandler` nejprve čte právě jeden řádek s limitem 4096 B a bez něj vrací 400 (`phpvpn.php:1770-1778`). `Auth::parseLine()` vyžaduje přesně pět očekávaných polí a `type=auth` (`phpvpn.php:840-877`). Druhý `auth` nebo neznámý typ je chyba celého requestu (`phpvpn.php:1893-1903`). Prázdný sync tvořený jen auth řádkem je podporován.
2. **Podpis váže akci a přesné tělo:** Hashování začíná přesně za newline prvního auth záznamu a zahrnuje již bufferované i následně načtené bajty (`phpvpn.php:703-719`, `780-821`). Canonical request obsahuje `POST`, `sync`, klienta, timestamp, nonce a hex SHA-256 zbytku těla; porovnání používá `hash_equals` (`phpvpn.php:886-905`). Odchylka formátu derived key je S-01.
3. **Token se nepřenáší:** Wire auth obsahuje pouze klienta, timestamp, nonce a podpis; access token se načítá jen z `ACCESS_TOKENS` (`phpvpn.php:840-877`, `888-894`). URL obsluhuje pouze akci; nevynucení dalších parametrů je N-02.
4. **Výchozí nonce režim je skutečně bez persistence:** `PERSIST_NONCES=false` je default (`phpvpn.php:77-78`); `NonceStore::claim()` se volá pouze v podmínce s tímto přepínačem a nonce sweeper je při `false` no-op (`phpvpn.php:1788-1790`, `1704-1708`). Při `true` se nonce kontroluje až po úspěšném HMAC a před publish/collect. Souběžná hranice bucketu je V-02.
5. **Základní input a DoS limity:** `Content-Length` nad limit se odmítá a streamovaný reader sám nepřečte více než `MAX_REQUEST_BYTES`, takže chunked/nespolehlivá délka limit neobchází (`phpvpn.php:785-810`, `1807-1810`). Řádek se odmítne před `json_decode`, pokud je příliš dlouhý (`phpvpn.php:728-751`). Počet datových řádků má hard limit a operational limit přesně rozděluje validní data na accepted/deferred kandidáty (`phpvpn.php:1836-1878`).
6. **Zdroj a cíl paketu:** `src` se musí přesně rovnat autentizované VPN IP a spoof vrací 401 (`phpvpn.php:1931-1940`). `dst` prochází jediným validátorem rozsahu `10.99.99.2-254` bez leading zero (`phpvpn.php:463-475`, `1947-1952`). Data musí být neprázdná base64 textová hodnota v řádkovém limitu (`phpvpn.php:482-489`, `1941-1945`). Přesnost UUID/date/media type má omezení N-01.
7. **Žádný path traversal z HTTP vstupu:** Frontové cesty se skládají pouze z interního rootu a numerického oktetu formátovaného `%03d` (`phpvpn.php:1050-1061`). Ack `batchId` musí odpovídat pevnému regexu/délce bez lomítek a maže se pouze `.sending` v adresáři autentizovaného klienta (`phpvpn.php:614-638`, `1881-1889`, `1504-1508`). Paketová UUID, `src`, `dst` ani `data` se nepoužívají ve filesystem cestě.
8. **Atomická publikace batchů:** Enqueue běží pod per-frontovým exkluzivním lockem, vytvoří náhodně pojmenované `.writing` přes `x+b`, obslouží short writes, provede `fflush` a teprve pak atomický `rename` na `.data` ve stejném adresáři (`phpvpn.php:1429-1479`, `1556-1593`). Reader vybírá jen `.data`, takže nevidí částečný batch.
9. **Souběžné claimnutí jednoho batchu:** Výběr a rename `.data` na `.sending` probíhá pod stejným queue lockem (`phpvpn.php:1491-1553`); dva běžné souběžné collectory proto nemohou oba úspěšně claimnout tentýž `.data` soubor. Lock není držen během `readfile` odpovědi (`phpvpn.php:2038-2048`).
10. **Ack a at-least-once zotavení:** Delivery zůstává jako `.sending` a smaže se až podepsaným ackem ve frontě stejného klienta (`phpvpn.php:1504-1508`). Staré `.sending` se po rezervě vrací na `.data`, staré `.data` expiruje podle `filemtime` s absolutním backstopem (`phpvpn.php:1243-1255`, `1282-1315`). Přerušení odpovědi tedy primárně vede k duplicitě, ne k okamžité tiché ztrátě.
11. **Tajemství a payload nejsou v aplikačních log polích:** `Log` zapisuje čas, request ID, akci, klienta, kategorii, latenci/lock wait a explicitní číselná pole; žádná cesta nepředává token, secret, signature, nonce ani paketová data (`phpvpn.php:386-430`, `1988`, `2051-2057`, `2359-2366`). Problém velikosti logů je S-05; infrastrukturové URL logy řeší N-02.
12. **HTTP cache a MIME ochrana:** Všechny běžné odpovědi a řízené chyby používají `Cache-Control: no-store, no-cache, must-revalidate`, `Pragma: no-cache` a `X-Content-Type-Options: nosniff` (`phpvpn.php:300-309`, `357-379`). Úspěšný sync vrací `application/x-ndjson; charset=utf-8` (`phpvpn.php:2024-2027`).
13. **Obecné chybové tělo:** Klient dostává pouze status a interně zvolený obecný kód; exception detail se nevrací (`phpvpn.php:357-370`, `2357-2367`). Přímé případy správně mapují chybný podpis/spoof na 401, syntaxi na 400, request/line limit na 413, neznámou akci na 404, metodu na 405 s `Allow` a nepodporovaný media type na 415. Odchylky frontových 200/429/503 jsou S-02 a S-03.
14. **Bez dlouhého packet pollingu/streamingu:** Sync nečeká na vznik nového paketu a po dostupných datech ihned sestaví konečný `sync-result`; neimplementuje long-poll ani nekonečný stream (`phpvpn.php:1760-1796`, `2009-2058`). Agregace krátkých lock sleepů však může překročit request budget, viz V-04.
15. **Bez DB, Composeru, shellu, cronu, OpenSSL/cURL/socket API:** Kontrolovaný soubor používá lokální filesystem a standardní PHP/hash/JSON funkce; server payload nedešifruje. Jediná zjištěná rozporuplná volitelná závislost je `ctype`, viz S-06.

## Omezení ověření

- Revize byla provedena staticky nad výše uvedenými hashi. V prostředí nebyl dostupný příkaz `php` ani PHP ve WSL, proto nebylo možné spustit `php -l phpvpn.php` ani `php tests/run.php`. Neúspěch spuštění je omezení revizního prostředí, nikoli výsledek testů.
- Nebyl proveden reálný dvouworkerový test `flock`, test atomicity konkrétního filesystému, HTTP probe ochrany `QUEUE_ROOT`, fault injection při plném disku/inode ani zátěžový test hostingu. Tyto testy zůstávají před produkcí nutné podle §12 návrhu.
- Posouzení se řídí normativním `design2.md`. Komentář v `phpvpn.php` a `PROTOCOL.md` dokumentují některé záměrné odchylky, ale nemění skutečnost, že vůči aktuálně zadanému návrhu jde o rozpory.

## Doporučené pořadí oprav a regresních testů

1. Opravit V-01, V-02, V-03 a V-04; přidat cílené souběžné a quota testy.
2. Udělat z úplné konfigurace/tokenů fail-closed bránu (V-05) a sjednotit HMAC test vector s `design2.md` (S-01).
3. Vyřešit frontovou transakční/chybovou semantiku (S-02, S-03) a oversized batch (S-07).
4. Zpevnit shared-hosting storage a log limity (S-04, S-05), odstranit `ctype` závislost (S-06).
5. Dotáhnout přesné wire validátory a query enforcement (N-01, N-02).
6. Poté spustit syntax check a celý test suite na PHP 8.2, 8.3, 8.4 a 8.5, dvouworkerový replay test přes hranici minuty, test 253 cílových lock contention, test překročení globální kvóty z více workerů a externí HTTP probe úložiště.
