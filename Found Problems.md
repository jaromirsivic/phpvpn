# Bezpečnostní a architektonická revize návrhu PHP VPN

## Závěr

Návrh nelze bezpečně označit za VPN a není vhodný pro produkční provoz na běžném sdíleném hostingu. Jde o HTTP relay aplikačních dat s vlastním, nehotovým protokolem. I po opravě jednotlivých chyb nezíská vlastnosti standardního VPN řešení (spolehlivý tunel, řízení toku, bezpečné doručení, rotaci klíčů a provozní odolnost). Pro reálné VPN použití doporučuji WireGuard nebo ověřený overlay produkt; PHP může být pouze řídicí/API vrstva.

Níže jsou nedostatky konkrétního návrhu, seřazené přibližně podle závažnosti.

## Kritické problémy

### 1. Nejedná se o kompletní ani bezpečně specifikovanou VPN

- „AES-256“ není protokol. Chybí režim AEAD (např. AES-GCM nebo ChaCha20-Poly1305), nonce/IV, autentizační tag, pravidla jejich jedinečnosti, KDF, verze formátu, rotace klíčů a bezpečné chování při chybě ověření.
- Sdílené statické klíče mezi dvojicemi klientů neposkytují forward secrecy. Únik jednoho dlouhodobého klíče zpětně kompromituje zachycený provoz dané dvojice.
- Neexistuje specifikace ochrany proti replay na end-to-end vrstvě. UUID v neověřené obálce ji samo neřeší; příjemce potřebuje per-flow/per-peer perzistentní deduplikační okno a limity.
- Server ověřuje jen textové `src` v obálce. Příjemce musí po dešifrování validovat vnitřní IP paket (zdroj, cíl, délky, protokol a případné fragmenty), jinak může autorizovaný klient posílat spoofované vnitřní pakety.
- Neexistuje autorizace komunikace mezi dvojicemi. Každý platně přihlášený klient může zaplňovat frontu libovolného jiného klienta. Šifrování tomu nebrání: příjemce sice data nerozšifruje, ale utrpí DoS.

**Nutné řešení:** buď použít zavedený VPN protokol, nebo před implementací sepsat plnou, nezávisle auditovatelnou specifikaci kryptografického a paketového protokolu. Přidat explicitní ACL zdroj–cíl a povinnou validaci v klientovi.

### 2. `pull` ve skutečnosti negarantuje ani deklarované „at-least-once“ doručení

Návrh po `flush()` smaže soubor `.sending`. `flush()` pouze předá data lokálnímu webserveru/proxy; nepotvrzuje přijetí klientem. Při výpadku po flushi může klient data nikdy neobdržet, přesto budou smazána. To je ztráta, tedy ne at-least-once.

**Nutné řešení:** zavést aplikační potvrzení doručení a persistentní stav/acknowledgement pro každou dávku nebo sekvenci. Bez něj lze poctivě deklarovat jen best-effort doručení. Klientská deduplikace musí být trvalá přes restart a mít jasně určenou dobu retence.

### 3. Obnova „opuštěného“ `.sending` souboru může vyrobit souběžné duplicity a závody

GC po pevné době přejmenuje `.sending` zpět na `.data`. Původní PHP worker však může být jen pomalý nebo pozastavený a později stejný soubor odešle či smaže. Druhý pull jej mezitím převezme. Neexistuje lease s obnovou, fencing token ani bezpečný důkaz smrti původního workera.

**Nutné řešení:** návrh potvrzování a claimu přepracovat jako transakční frontu s lease/epochou a klientským ACK. Pro souborový systém je tato část zvlášť křehká; fronta/broker je vhodnější.

### 4. Cílová platforma je v rozporu s dlouhými streamy a paralelismem

- Dva 15sekundové requesty na klienta trvale obsazují PHP/FPM/Apache workery. Desítky klientů znamenají desítky až stovky současně obsazených workerů, což běžný hosting typicky neumožňuje.
- Reverzní proxy, FastCGI a hosting mohou request body i odpovědi bufferovat; `php://input` proto nemusí být skutečné průběžné čtení. Hlavička `X-Accel-Buffering` funguje pouze na vybraných konfiguracích nginxu.
- Limity `max_execution_time`, FPM request timeout, upload/body limity, proxy timeouty, procesní kill a odpojení klienta nelze z jednoho PHP souboru spolehlivě řídit.
- Okamžité reconnecty po 15 sekundách zvyšují režii, lock contention a riziko vyčerpání workerů.

**Nutné řešení:** cíl omezit na konkrétně otestovaný serverový stack se známými limity, nebo architekturu změnit na skutečný dlouho běžící síťový proces/frontu. „Většina hostingů“ není splnitelný předpoklad.

## Vysoké problémy

### 5. HMAC autentizace má špatný model správy tajemství

- Každý klient má znát `SERVER_SECRET`, a tedy kompromitace jednoho klienta odhalí společné tajemství celé služby. Toto tajemství navíc v canonical requestu nepřináší ochranu proti kompromitovanému klientovi.
- Nejsou pravidla pro generování, bezpečné nasazení, revokaci, expiraci ani rotaci `ACCESS_TOKENS` a `SERVER_SECRET`.
- Přechodový minutový bearer režim je replayovatelný a nemá být nikdy dostupný v produkci. Pouhé varování do logu není ochrana.
- Nonce soubory zavádějí další diskový DoS. Chybí limit počtu nonce, atomický postup vůči GC, oprávnění, limit délky/vstupu a chování při zaplnění disku.

**Nutné řešení:** použít per-client klíče nebo asymetrickou/mTLS autentizaci; společné serverové tajemství klientům nevydávat. Legacy režim odstranit z produkčního kódu. Zavést versionovaný key lifecycle a nouzovou revokaci.

### 6. Chybí integrita požadavkového těla a kontrola `data`

Podpis neobsahuje tělo streamu a validace výslovně neověřuje `data`. Není tedy stanoveno ani striktní Base64 dekódování, ani reálný limit po dekódování. Uvedený `MAX_PACKET_BYTES` se v postupu validace nepoužívá.

TLS chrání cestu mezi klientem a TLS terminátorem, ale není náhradou za autentizovaný paketový formát a nepomůže při chybné/kompromitované terminaci. Navíc nevalidní Base64 může neomezeně plnit frontu až do jiných limitů.

**Nutné řešení:** vyžadovat striktní Base64 a limit délky před i po dekódování. End-to-end payload musí být AEAD zpráva s verzí, nonce, tagem, adresami a sekvenčním číslem svázanými jako associated data.

### 7. Kapacita fronty není kapacita dat a umožňuje diskový DoS

`MAX_BUFFERS=128` omezuje počet souborů, nikoliv bajty. Jedna dávka může mít až několik MiB, takže jediná příjemcova fronta může narůst na stovky MiB. Souhrnný limit všech front, nonce, `.error` a `.expired` souborů ani disková kvóta nejsou definovány. Počet souborů se navíc při každém zápisu prochází, což je při zátěži drahé.

**Nutné řešení:** vynutit kvótu bajtů na příjemce i globální kvótu, limit počtu souborů všech typů, rate limit podle klienta a předem definovanou politiku při přetížení. Metriky musí upozornit dříve, než se vyčerpá disk.

### 8. Ztráta paketů je součástí návrhu, ale protokol neumí bezpečně zotavení

Fronta zahazuje data při zaplnění i po 60 sekundách bez potvrzení klienta. Odpověď `accepted`/`dropped` po dlouhém push requestu neříká, které pakety byly ztraceny, a při přerušení requestu klient neví, co server uložil. Pro IP provoz může být část ztrát očekávaná, ale je nutné ji explicitně definovat a klient musí mít odpovídající mechanismus.

**Nutné řešení:** rozhodnout, zda jde o best-effort datagramový relay, nebo o spolehlivou frontu. Pro první variantu zavést měřitelné drop policy a ochranu před head-of-line blokováním; pro druhou ACK, sekvence a persistentní retry.

### 9. Souběh a pořadí nejsou plně určeny

- `flock()` bez časového limitu může blokovat PHP worker neomezeně; chybí timeout a chování při contention.
- Globální pořadí není garantováno a lexikografické řazení názvu se časem a náhodným UUID není stabilní při souběhu nebo změně systémového času.
- Garbage collection běží pod stejným lockem a skenuje adresář; může zhoršit latenci všech writerů/pullerů jednoho příjemce.
- Návrh přiznává, že `flock` není vhodný pro NFS ani více instancí. To je v napětí s neurčitým pojmem hosting, kde filesystem a horizontální škálování nemusí být pod kontrolou aplikace.

**Nutné řešení:** jasně garantovat pouze per-flow pořadí se sekvenčním číslem, nebo pořadí negarantovat. Lockovat s timeoutem, změřit contention a provoz povolit jen na ověřeném lokálním filesystému jedné instance.

### 10. 60sekundová GC politika ničí legitimní offline provoz

Data pro klienta, který je odpojený déle než minutu, se bez potvrzení smažou. To koliduje s představou VPN klienta, který se může připojit znovu po výpadku. `filemtime` navíc není spolehlivý protokolový čas a může být ovlivněn operacemi na souboru.

**Nutné řešení:** retenci navázat na explicitní ACK a zdokumentovanou politiku kvóty; pro best-effort variantu tento zásadní limit jasně komunikovat a měřit ztráty.

## Střední problémy

### 11. Bezpečnost souborového systému a nasazení není dostatečně vyřešena

- Fronta pod `__DIR__` může být veřejně dostupná přes HTTP, pokud konfigurace serveru není přesně správná. Spoléhat na ruční změny „na hostingu“ je rizikové.
- Režim adresáře `0700` nezaručuje režim nově vytvořených souborů; výsledná práva závisí na `umask`. Chybí požadavek na `0600`, vlastnictví a bezpečná práce se symlinky.
- Při víceuživatelském hostingu mohou symlink/race útoky a nedůvěryhodný lokální účet narušit souborovou frontu, pokud nejsou používány bezpečné kontroly typu souboru a izolace účtu.
- `.error` a `.expired` mohou obsahovat provozní metadata či pakety; jejich retenční prostor a přístup nejsou specifikovány.

**Nutné řešení:** ukládat frontu mimo document root, nastavit explicitní vlastnictví a `0600/0700`, odmítat symlinky/nepravidelné soubory a otestovat webserverovou nedostupnost fronty.

### 12. Validace protokolu je neúplná a místy nejednoznačná

- Text „nepovolí neznámá povinná pole“ neurčuje přesnou množinu povolených klíčů ani způsob verzování.
- „Super rychlý quick test“ UUID není bezpečnostní validace. Je nutná přesná syntaxe a musí být jasné, zda UUID má být v4 a jak se používá pro deduplikaci.
- Content-Type kontrola musí přesně definovat povolené parametry, limity hlaviček a chování při `Transfer-Encoding`/chunking.
- Nejsou limity počtu různých cílů v jednom push, počtu řádků před flushem ani CPU budget JSON parseru. `MAX_TOTAL_BUFFER_BYTES` sám nezabrání CPU/IO DoS.

**Nutné řešení:** vydat versionovanou ABNF/JSON schema specifikaci, striktní limity všech vstupů a fuzz testy proti reálnému HTTP stacku.

### 13. Ochrana proti zneužití na síťové hraně chybí

Autentizace nepokrývá levný útok velkým počtem neautorizovaných TCP/HTTP spojení, velkými hlavičkami ani pomalým odesíláním těla. PHP kód sám nedokáže ochranu provést dříve, než webserver část zdrojů alokuje.

**Nutné řešení:** TLS, WAF/reverse proxy, limity spojení, rate limit podle IP i klienta, request/header/body timeouty a pevný limit workerů nakonfigurovat před PHP. Testovat slowloris a reconnect storm.

### 14. Pozorovatelnost a reakce na incident nejsou dostatečné

Jsou uvedeny logy, ale chybí metriky a alarmy pro obsazenost front, věk nejstarší dávky, dropy, lock wait, selhání claimu, replay, aktivní connectiony a diskovou kvótu. Bez nich se výpadek projeví až ztrátou provozu. Chybí postup pro kompromitovaný klientský klíč a forenzně bezpečný audit.

## Nedostatky zadání, které je nutné rozhodnout před implementací

1. Má být systém best-effort relay, nebo spolehlivá fronta? Současný text tvrdí at-least-once, ale implementuje ztrátové chování.
2. Jaké typy IPv4 paketů, fragmentace, MTU, MSS, DNS, broadcast/multicast a routování klient podporuje? Bez toho nelze tvrdit, že jde o VPN adaptér.
3. Jak se bezpečně distribuují, rotují a odvolávají klientské i párové klíče?
4. Jaká je cílová infrastruktura: konkrétní PHP SAPI, webserver/proxy, timeouty, filesystem, jedna instance a počet klientů?
5. Jaká je povolená ztrátovost, maximální offline doba, globální disková kvóta a chování při přeplnění?

## Doporučený postup

1. Neimplementovat tento návrh jako produkční VPN na sdíleném hostingu.
2. Pokud je požadavek skutečně VPN, nasadit WireGuard/OpenVPN nebo ověřený mesh VPN a PHP použít jen pro administraci.
3. Pokud jde pouze o interní HTTP relay, přejmenovat produkt, omezit rozsah a před kódováním uzavřít výše uvedená rozhodnutí.
4. Teprve potom provést threat modeling, kryptografický návrh od nezávislého odborníka a integrační/zátěžové testy na skutečné cílové infrastruktuře.
