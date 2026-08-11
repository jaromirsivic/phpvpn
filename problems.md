# Analýza protokolu phpvpn v1

Revize dokumentu `protocol.md` (stav commit `21281c4`) před první implementací.

Cílem analýzy je připravit půdu pro jeden malý, snadno nasaditelný PHP soubor, který protokol implementuje, primárně nad souborovým systémem a sekundárně nad memcached, bez povinných PHP rozšíření, efektivně a s minimem IOPS.

Značení závažnosti:

| Značka | Význam |
|---|---|
| **K** | Kritická. Před implementací je nutné rozhodnout a specifikaci upravit. |
| **V** | Vysoká. Vede k reálné ztrátě dat, výpadku nebo bezpečnostnímu oslabení. |
| **S** | Střední. Zhoršuje provoz, výkon nebo interoperabilitu. |
| **N** | Nízká. Kosmetika, čitelnost, drobná nekonzistence. |

---

## 1. Shrnutí nejdůležitějších zjištění

| ID | Závažnost | Zjištění |
|---|---|---|
| R1 | **K** | Kapitola 1.1 nařizuje softwarovou implementaci AES, kapitola 15 ji zakazuje a nabízí místo ní `503`. Přímý rozpor. |
| R2 | **K** | Prázdný `SERVER_SECRET` je podle kapitoly 3.2 platný vstup, ale PHP funkce `hash_hkdf()` na prázdný klíč vyhodí `ValueError`. Referenční implementace tak nemůže vyhovět zároveň kapitole 3.2 a kapitole 15. |
| B1 | **K** | Replay pollingu v 60sekundovém okně nad HTTP umožňuje pasivnímu odposlechu trvale krást a nenávratně ničit příchozí frontu oběti. Protokol tento důsledek nikde nepojmenovává. |
| B2 | **V** | Odpověď `data` nemá MAC. Útočník může podvrhnout `data-result` a přinutit odesílatele zahodit data, která nikdy nedorazila. Náprava stojí jeden HMAC. |
| B3 | **V** | Nepovinný a neautentizovaný `health` určuje klientovi čas. Aktivní útočník tím posune klientovy podpisy do budoucnosti a zcela obejde 60sekundové čerstvostní okno. |
| B4 | **V** | Statický `PAYLOAD_KEY` a náhodné 96bitové IV znamenají reálné riziko kolize IV řádově po 2^32 paketech na klíč, což je u VPN dosažitelné za hodiny až dny. |
| R3 | **V** | Zaplnění fronty jednoho cíle vrací `429` a tím zablokuje i vlastní příchozí data odesílatele. Jeden odpojený peer degraduje celou síť. |
| V1 | **V** | `MAX_DATA_RECORDS_PER_REQUEST = 16` je tvrdý strop odchozí propustnosti okolo 2 Mbit/s, zatímco příchozí směr je neomezený. Nesymetrie bez opodstatnění. |
| B5 | **S** | Chybí normativní povinnost příjemce ověřit, že `dst` je jeho vlastní adresa. Vede k akceptaci odrazu vlastního paketu. |
| N1 | **S** | Chybí normativní pravidlo kanonického base64url. Protože se řetězec base64 vkládá do AAD, nekanonické kódování rozbije interoperabilitu. |
| V2 | **S** | Centrální `clients.json` čtený a zapisovaný v horké cestě je zbytečná IOPS zátěž. Lze ho nahradit `mtime` markeru za jeden metadata syscall. |

---

## 2. Rozpory a chyby ve specifikaci

### R1 (K) Rozpor o softwarové implementaci AES

**Kde:** kapitola 1.1 proti kapitole 15.

Kapitola 1.1 říká, že implementace nemá vyžadovat žádné rozšíření, ani OpenSSL, a že „algoritmus AES-256-GCM je implementovaný softwarově“. Kapitola 15 naopak doporučuje `openssl_encrypt()`, uvádí, že „vlastní neauditovaná implementace AES se nedoporučuje“, a předepisuje, že při chybějící bezpečné AES-GCM implementaci má `info` vracet `503 server_misconfigured`.

Obě věty nemohou platit současně. Implementátor nemá z dokumentu jak zjistit, zda má pure-PHP AES napsat, nebo zda má vracet chybu.

**Doporučení:** rozhodnout ve prospěch varianty, která AES na serveru vůbec nepotřebuje. Viz kapitola 6 tohoto dokumentu. Server AES potřebuje jen pro šifrování odpovědi `info`. Pokud se pro `info` použije AEAD postavená nad HMAC-SHA-256, server vystačí s jádrem PHP a rozpor zmizí. AES-256-GCM zůstane pouze pro end-to-end payload, kterého se server nikdy nedotkne.

### R2 (K) Prázdný `SERVER_SECRET` proti `hash_hkdf()`

**Kde:** kapitola 3.2, 5.1, 7.1 proti kapitole 15.

Specifikace opakovaně tvrdí, že prázdný řetězec i 32 nulových bajtů jsou platné hodnoty `SERVER_SECRET` a že autentizované endpointy je nesmí odmítnout. Zároveň kapitola 15 doporučuje `hash_hkdf()`. PHP dokumentace uvádí, že klíč `hash_hkdf()` nesmí být prázdný a že se v PHP 8 vyhodí `ValueError`. Doporučená realizace tedy na normativně platné konfiguraci spadne na neošetřenou výjimku a vrátí `500`.

**Doporučení:** implementovat HKDF ručně přes `hash_hmac()`. Extract i Expand jsou dohromady zhruba deset řádků, zvládají prázdný IKM, nemají limit 255 bloků na výstup a odstraní jedinou tvrdou závislost na chování konkrétní verze PHP.

```php
function hkdf(string $ikm, string $salt, string $info, int $len): string {
    $prk = hash_hmac('sha256', $ikm, $salt, true);   // Extract, prázdný $ikm je v pořádku
    $out = ''; $t = ''; $i = 1;
    while (strlen($out) < $len) {
        $t = hash_hmac('sha256', $t . $info . chr($i++), $prk, true);
        $out .= $t;
    }
    return substr($out, 0, $len);
}
```

Pozor na pořadí argumentů. V HKDF-Extract je klíčem HMAC **salt**, nikoli IKM. `hash_hkdf()` to řeší uvnitř, při ruční implementaci je to nejčastější chyba.

### R3 (V) `429 queue_full` blokuje příchozí data odesílatele

**Kde:** kapitola 10.2 proti kapitole 9.5.

Kapitola 9.5 říká, že server vyprázdní buffer „po úspěšném ověření požadavku a zpracování jeho odesílaných záznamů“. Kapitola 10.2 říká, že pokud nelze přijmout žádnou odesílanou položku kvůli kapacitě, vrací se `429`. Z toho plyne, že při `429` k vyprázdnění vůbec nedojde.

Důsledek je vážný. Stačí, aby byl jeden peer offline. Jeho fronta se do 60 sekund zaplní na 16 MiB. Každý, kdo mu posílá, dostává `429`, a tím současně přestává dostávat vlastní příchozí provoz od všech ostatních. Jeden mrtvý uzel tak vyřadí komunikaci mezi zdravými uzly.

**Doporučení:** oddělit odchozí a příchozí cestu. Server má vždy, když je požadavek autentizovaný a syntakticky platný, vrátit `200`, vyprázdnit buffer volajícího a nepřijaté položky uvést v `deferredMessageIds`. `429` má zůstat jen pro případ, kdy nelze vyprázdnit ani vlastní frontu. Explicitně dopsat, že vyprázdnění příchozího bufferu nikdy nezávisí na výsledku odesílání.

### R4 (S) Neomezená odpověď proti doporučenému timeoutu klienta

**Kde:** kapitola 9.5 a 11.1 proti kapitole 11.2.

Odpověď není stránkovaná a může mít až `MAX_QUEUE_BYTES_PER_CLIENT`, tedy 16 MiB. Doporučený celkový timeout klientského požadavku je 15 sekund. Přenos 16 MiB za 15 sekund vyžaduje trvale přes 8,9 Mbit/s. Klient s pomalejší linkou požadavek utne, a protože server data odstranil před odesláním, jsou nenávratně pryč. Tento scénář se pak opakuje při každém dalším pokusu, dokud fronta nevyprší.

**Doporučení:** buď zavést `MAX_RESPONSE_RECORDS` a stránkování, nebo výrazně snížit `MAX_QUEUE_BYTES_PER_CLIENT`. Vzhledem k TTL 60 sekund je 16 MiB stejně přestřelené. 1 až 2 MiB odpovídají provozu, který lze doručit v rámci TTL, a odstraní i tlak na `memory_limit`.

### R5 (S) Nesymetrie limitů odchozího a příchozího směru

**Kde:** kapitola 9.1 a 11.1 proti kapitole 9.5.

Klient smí v jednom požadavku odeslat 16 záznamů, ale přijmout jich může libovolně mnoho. Pro obousměrný IP provoz je to nekonzistentní. Viz V1.

### R6 (S) Povinný mechanismus zámku místo požadované vlastnosti

**Kde:** kapitola 10.5 proti kapitole 15 a 13.

Kapitola 10.5 nařizuje ovladači `disk` „zámek s omezeným čekáním“, kapitola 13 na to váže chybu `503 queue_busy` a kapitola 15 zakazuje neomezený globální zámek. Specifikace tím předepisuje konkrétní implementační mechanismus, přestože skutečným požadavkem je atomicita. Lepší diskové návrhy postavené na `rename()` žádný zámek nepotřebují a nikdy negenerují `queue_busy`.

**Doporučení:** přeformulovat na vlastnost. „Publikace a vyprázdnění musí být atomické vůči souběžným workerům. Prostředek je na implementaci.“ `queue_busy` ponechat jako volitelnou chybu pro ovladače, které zámek skutečně používají.

### R7 (S) `503 queue_busy` chybí v kapitole 10.2

Kapitola 10.2 vyjmenovává jen `429 queue_full` a `503 queue_unavailable`. `queue_busy` se objeví až v tabulce chyb. Doplnit křížový odkaz.

### R8 (N) Nekonzistentní kanonizace řetězců

**Kde:** kapitola 7.2 proti kapitole 8.3.

Kapitola 7.2 odděluje doménový řetězec a `src` bajtem `NUL`. Kapitola 8.3 skládá `info = "phpvpn/info-response/" || requestId` prostou konkatenací. Bezpečnostně to zde vadí málo, protože `requestId` má pevnou délku a pevný tvar, ale je to stylová nekonzistence, která svádí implementátora k chybě u budoucích rozšíření. Sjednotit na oddělovač `NUL` všude.

### R9 (N) `401` pro sémanticky autorizační chybu

**Kde:** kapitola 9.3.

Neshoda `src` v datovém záznamu se `src` v `auth-full` je popsána jako `401 unauthorized`, přestože požadavek byl autentizován korektně. Sémanticky jde o `403`. Ponechání `401` je obhajitelné kvůli minimalizaci úniku informací, ale zaslouží si větu s odůvodněním, jinak to vypadá jako chyba.

### R10 (N) Rozšiřitelnost proti uzavřené sadě polí

**Kde:** kapitola 6.1 proti kapitole 14.

Neznámé pole v jakémkoli protokolovém záznamu je `400`, zatímco kapitola 14 mluví o rozšiřování. Rozšiřování je fakticky možné jen uvnitř plaintextu `info`. To je legitimní volba, ale kapitola 14 to má říct rovnou a nemá vzbuzovat dojem širší rozšiřitelnosti.

---

## 3. Bezpečnostní nedostatky

### B1 (K) Krádež a zničení fronty přehráním pollingu

**Kde:** kapitola 1.2, 4.2, 9.2, 10.3.

Specifikace opakovaně a poctivě přiznává, že replay uvnitř 60sekundového okna není detekován. Nikde ale nepojmenovává jeho nejzávažnější důsledek v kombinaci s at-most-once vyprázdněním.

Scénář nad čistým HTTP:

1. Odposlech zachytí platný požadavek `data`, stačí i samotný `auth-full` polling bez dat.
2. Do 60 sekund jej beze změny odešle serveru znovu.
3. Server ověří HMAC, ten je platný, atomicky vyprázdní frontu oběti a všechny záznamy pošle útočníkovi.
4. Útočník obsah nepřečte, protože nemá `PAYLOAD_KEY`, ale oběť o data nenávratně přišla.
5. Útočník zachytí další polling a postup opakuje.

Výsledkem je trvalý, plně tichý výpadek příchozího směru oběti, dosažitelný bez jakéhokoli prolomení kryptografie a bez schopnosti měnit provoz. Stačí odposlech a schopnost odeslat vlastní HTTP požadavek. Klient nemá žádnou možnost útok detekovat, protože ztracená data vypadají stejně jako běžná ztráta paketů.

**Doporučení, seřazená podle poměru přínosu a nákladů:**

1. Zkrátit výchozí `AUTH_TIMESTAMP_SKEW_SECONDS` na 5 až 10 sekund. Klient si čas synchronizuje přes `health`, takže širší okno nepotřebuje. Zmenší útočné okno šest až dvanáctkrát a nestojí nic.
2. Doplnit volitelnou, ale doporučenou ochranu proti přehrání na jeden požadavek. Návrh s nulovou přídavnou čtecí IOPS je v kapitole 7.4. Vyžaduje změnu kapitoly 16.6 bodu 6, která existenci jakéhokoli nonce úložiště dnes zakazuje.
3. Doporučit HTTPS pro provoz mimo důvěryhodnou síť daleko důrazněji, než to dělá kapitola 4.2. Věta „zda jej administrátor použije, je provozní rozhodnutí“ podceňuje rozdíl mezi režimy.

### B2 (V) Odpověď `data` není autentizovaná

**Kde:** kapitola 1.2, 4.2, 9.5.

Kapitola 1.1 uvádí jako bezpečnostní cíl důvěrnost a integritu odpovědi `info`, ale odpověď `data` zůstává zcela bez ochrany celku. Aktivní útočník na trase může:

- odstranit libovolné datové řádky, což je kvůli at-most-once trvalá ztráta,
- přeuspořádat řádky a zhoršit chování TCP nad VPN,
- podvrhnout `data-result` a uvést do něj `messageId`, které server nikdy nepřijal.

Bod tři je nejhorší. Kapitola 9.5 říká, že `acceptedMessageIds` jsou „jediné položky, které smí odesílatel odstranit ze své odchozí fronty“. Útočník tedy může přinutit odesílatele smazat data, která se do relay nikdy nedostala, a to bez znalosti jakéhokoli klíče.

**Doporučení:** přidat poslední řádek s HMAC nad celým předchozím tělem, počítaný klíčem `fullAuthKey`, který server v tu chvíli už má odvozený. Náklad je jeden `hash_update_stream()` nad odesílanými bajty, tedy jednotky milisekund na megabajt, a získá se tím integrita i úplnost odpovědi včetně režimu HTTP.

```text
canonical = "PHPVPN-DATA-RESPONSE" LF
            "data" LF
            requestId LF
            SHA256HEX(všechny bajty odpovědi před tímto řádkem)

{"type":"data-mac","tag":"BASE64URL"}
```

Pokud je změna wire formátu nežádoucí, minimální náhradou je povinné `Content-Length` na odpovědi a povinnost klienta odmítnout neúplné tělo. To odhalí zkrácení, ale ne podvržení `data-result`.

### B3 (V) Neautentizovaný `health` jako zdroj času umožňuje pre-play

**Kde:** kapitola 5.2 a 12.1.

Kapitola 12.1 dělá z `health` povinný bootstrap krok a kapitola 5.2 říká, že klient z něj odvodí korekci vlastního časového offsetu. `health` přitom není nijak autentizovaný.

Aktivní útočník na HTTP trase odpoví na `health` časem posunutým například o hodinu dopředu. Klient si offset zapamatuje a začne podepisovat požadavky s budoucím timestampem. Útočník tyto požadavky zachytí, nepustí je dál a přehraje je za hodinu, kdy budou pro server čerstvé. Šedesátisekundové okno tím ztrácí jakýkoli smysl a útočník získá libovolně dlouhou zásobu platných požadavků, včetně pollingů použitelných pro útok B1.

**Doporučení:**

- Autentizovat časovou hodnotu. Nejlevnější varianta je přidat do `health` pole `tag = HMAC-SHA-256(semiKey, "PHPVPN-HEALTH" LF timestamp)`. Endpoint zůstane bez autentizace požadavku, ale klient si ověří, že čas pochází od serveru. Náklad je jeden HMAC.
- Omezit velikost akceptovatelné korekce. Klient by neměl přijmout offset větší než například 5 minut bez explicitního potvrzení uživatelem.
- Přeformulovat kapitolu 12.1. Klient se synchronizovanými hodinami by měl `health` používat pouze jako fallback, ne povinně před každým startem.

### B4 (V) Riziko kolize IV u statického payload klíče

**Kde:** kapitola 3.2 a 9.4.

`PAYLOAD_KEY` je pevný, nemá rotaci, není z něj odvozen relační klíč a IV je 12 náhodných bajtů. AES-GCM s náhodným 96bitovým IV má podle NIST SP 800-38D bezpečnostní hranici okolo 2^32 zašifrování na jeden klíč. VPN generuje jeden šifrovací úkon na paket. Při 50 000 paketech za sekundu se hranice dosáhne zhruba za den, při 5 000 paketech za sekundu zhruba za deset dní. Kolize IV u GCM není jen ztráta důvěrnosti dvou paketů, ale vede k obnovení autentizačního podklíče H a k možnosti padělat tagy pro daný klíč.

Specifikace navíc nikde nevyžaduje, aby `PAYLOAD_KEYS[A][B]` a `PAYLOAD_KEYS[B][A]` byly různé hodnoty. Pokud distribuce klíčů omylem použije jeden klíč pro obě strany dvojice, riziko kolize se násobí a útočník ho může aktivně vyvolat.

**Doporučení:**

1. Normativně vyžadovat, aby klíče pro obě pořadí dvojice byly nezávislé.
2. Nahradit čistě náhodné IV deterministickou konstrukcí. Osvědčený tvar je 4 bajty náhodného identifikátoru relace plus 8 bajtů monotónního čítače. Kolize je pak nemožná uvnitř relace a mimořádně nepravděpodobná napříč relacemi.
3. Nebo zavést odvození relačního klíče. `sessionKey = HKDF(PAYLOAD_KEY, salt, "phpvpn/payload" || NUL || src || NUL || dst)` s náhodným saltem měněným například po 2^20 paketech. Salt se přenese jen při jeho změně.
4. Doplnit do dokumentu explicitní číslo maximálního počtu paketů na klíč a povinnost rotace.

### B5 (S) Chybí kontrola, že `dst` je vlastní adresa příjemce

**Kde:** kapitola 9.4, kroky 1 až 4.

Příjemce má vyhledat klíč podle dvojice `src` a `dst`, ověřit tag a zkontrolovat shodu vnitřních IP adres s hlavičkovými poli. Nikde ale není řečeno, že `dst` se musí rovnat jeho vlastní VPN adrese a `src` se od ní musí lišit.

Bez této kontroly platí, že pokud se klientovi A do příchozího proudu dostane kopie jeho vlastního odchozího záznamu `src=A, dst=B`, ověří ho klíčem `PAYLOAD_KEYS[A][B]`, tag bude platný, kontrola vnitřních adres projde a A si vlastní paket vloží do své virtuální karty jako příchozí. Vzniká odraz a potenciální smyčka. Nad HTTP to dokáže vyvolat kdokoli na trase.

**Doporučení:** doplnit do kapitoly 9.4 nulový krok. „Příjemce MUSÍ odmítnout každý záznam, jehož `dst` není jeho vlastní nakonfigurovaná VPN adresa nebo jehož `src` je jeho vlastní adresa.“

### B6 (S) Zaplavení fronty legitimním klientem

**Kde:** kapitola 10.1.

Každý autentizovaný klient smí posílat komukoli. Bez znalosti `PAYLOAD_KEY` sice nevytvoří smysluplný obsah, ale ověření tagu probíhá až u příjemce. Kompromitovaný nebo nepřátelský klient tedy může naplnit 16MiB frontu libovolného jiného klienta náhodným balastem a vyhladovět reálný provoz. Server nemá žádný per-source podíl ani ACL.

**Doporučení:** zavést spravedlivé dělení fronty podle zdroje, například strop `MAX_QUEUE_BYTES_PER_CLIENT / počet nakonfigurovaných klientů` na jednu dvojici zdroj a cíl, nebo volitelnou matici povolených dvojic. Filesystémová implementace to zvládne bez extra IOPS, pokud se do názvu souboru zapíše i zdroj.

### B7 (S) Chybí globální strop úložiště

**Kde:** kapitola 10.1 a 9.3.

`dst` musí být syntakticky platná VPN adresa, nemusí ale patřit nakonfigurovanému klientovi. Lze tedy plnit fronty pro všech 253 adres. Při 16 MiB na adresu je horní mez 4 GiB, kterou lze na sdíleném hostingu snadno zaplnit celou diskovou kvótu.

**Doporučení:** odmítat `dst`, který není v `ACCESS_KEYS`, a zavést `MAX_TOTAL_QUEUE_BYTES`. Odmítání neznámého `dst` má drobnou nevýhodu, protože prozrazuje množinu klientů, ale tu už `info` prozrazuje každému držiteli `SERVER_SECRET`.

### B8 (S) `SERVER_SECRET` je jediné sdílené tajemství všech klientů

**Kde:** kapitola 3.2.

Kompromitace jediného klienta prozradí `SERVER_SECRET`. Tím padá autentizace `info`, útočník získá seznam všech klientů s časy poslední aktivity, umí dešifrovat cizí `info` odpovědi a autentizace `data` se redukuje na samotný `ACCESS_KEY`. Dokument to nikde neuvádí jako přijaté riziko.

**Doporučení:** doplnit do kapitoly 1.2 větu o dopadu kompromitace klienta. Zvážit oddělení tajemství pro `info` od tajemství pro odvození `authFullSalt`. Doplnit podporu rotace, tedy pole akceptovaných tajemství, aby výměna nevyžadovala současný výpadek všech klientů. Ověření proti více tajemstvím provádět vždy nad celou množinou, aby se nezavedl časovací postranní kanál.

### B9 (S) Nekonzistentní reprezentace tajemství

**Kde:** kapitola 3.2.

`ACCESS_KEY` se zapisuje jako 64 hexadecimálních znaků a dekóduje na 32 bajtů. `SERVER_SECRET` se používá „bez implicitního hex nebo base64 dekódování“. Administrátor, který do obou polí zkopíruje výstup `openssl rand -hex 32`, dostane pro `ACCESS_KEY` 256 bitů entropie a pro `SERVER_SECRET` 128 bitů. Rozdíl je neviditelný a nikde se neohlásí.

**Doporučení:** sjednotit reprezentaci na hex u obou hodnot, včetně kontroly délky při startu. Pokud má prázdná a nulová hodnota zůstat platná, ošetřit ji jako výslovnou výjimku, ne jako důsledek volného formátu.

### B10 (S) Výchozí nebezpečná konfigurace je plně funkční

**Kde:** kapitola 3.2 a 5.1.

Výchozí `SERVER_SECRET` je 32 nulových bajtů a specifikace výslovně zakazuje, aby tento stav blokoval `info` nebo `data`. Zároveň se veřejná stránka o tomto stavu chlubí každému návštěvníkovi. Vzniká nasazení, které je funkční, veřejně označené jako nezabezpečené a nikoho k nápravě nenutí.

**Doporučení:** ponechat varování na stránce, ale doplnit konfigurační přepínač `ALLOW_INSECURE_SECRET` s výchozí hodnotou `false`, který `info` a `data` při nulovém tajemství odmítne. Ještě lépe, umožnit vygenerování tajemství při prvním spuštění do souboru mimo document root, pokud je zapisovatelný.

### B11 (S) Chybí vazba požadavku na konkrétní server

**Kde:** kapitola 4.1.

Zákaz zahrnutí schématu, hostitele a portu do kryptografických výpočtů znamená, že platný požadavek je platný proti jakékoli instanci se stejnými tajemstvími. Kdo provozuje testovací a produkční instanci se stejnou konfigurací, umožní přenos požadavků mezi nimi.

**Doporučení:** doplnit konfigurovatelný řetězec realm a zařadit ho do doménového oddělení klíčů, například `"phpvpn/auth-full/key" || NUL || realm || NUL || src`. Vazba na skutečné jméno hostitele je nevhodná kvůli proxy, ale explicitní realm to řeší čistě.

### B12 (N) Neautentizované chybové odpovědi jako nástroj DoS

**Kde:** kapitola 13.

Útočník na trase může podvrhnout `429` nebo `503` a přinutit klienty k exponenciálnímu backoffu až 30 sekund. Dokument to zmiňuje jen implicitně.

**Doporučení:** doplnit do kapitoly 4.2 a doporučit klientům, aby podvrženému backoffu nedávali stejnou váhu jako reálné síťové chybě, například aby nezvyšovali interval nad určitou mez bez potvrzené odpovědi.

### B13 (N) `info` prozrazuje kompletní přehled o síti

Držitel `SERVER_SECRET`, což je každý klient, získá seznam všech adres a jejich `lastSeen`. Jde o metadata o tom, kdo je v síti aktivní. U VPN určené k obcházení odposlechu to nemusí být přijatelné.

**Doporučení:** učinit obsah `clients` konfigurovatelným, s možností vrátit pouze počet klientů nebo prázdné pole.

### B14 (N) Traffic analysis podle velikosti záznamu

Velikost datového řádku přesně odpovídá velikosti IP paketu. To umožňuje rozpoznat typ provozu i bez klíče. Vhodné zmínit v kapitole 4.2 a doporučit volitelný padding na straně klienta.

---

## 4. Nedospecifikované chování

Následující body nejsou chyby, ale mezery, které povedou k odlišným implementacím a k nekompatibilitě.

| ID | Kde | Chybí |
|---|---|---|
| N1 (S) | 6.1, 8.3, 9.4 | **Kanonický base64url.** Řetězec base64 vstupuje do AAD. Nekanonické kódování, tedy nenulové zbytkové bity v posledním znaku, dá jiný AAD a ověření selže. Musí být normativně zakázáno a server ho musí odmítat. |
| N2 (S) | 9.2, 9.3 | **Pořadí validace.** Není řečeno, zda se syntaktická chyba datového záznamu vyhodnocuje před ověřením HMAC nebo po něm. Vyhodnocování před ověřením dává neautentizovanému útočníkovi parser orákulum a plýtvá CPU. Doporučené normativní pořadí: limity a `413`, čtení a hash celého těla, ověření HMAC a `401`, syntaxe a `400`, kapacita a `429`. |
| N3 (S) | 9.3 | **Atomicita validace.** Není řečeno, zda jeden vadný datový záznam zneplatní celý požadavek, nebo se jen odloží. Doporučení: syntaktická chyba je fatální pro celý požadavek, `deferredMessageIds` slouží výhradně pro kapacitní důvody. |
| N4 (S) | 10.2 | **Determinismus částečného přijetí.** „Server zpracovává cílové skupiny deterministicky“ neurčuje jaké pořadí. Doporučení: vzestupně podle číselné hodnoty `dst`, uvnitř skupiny v pořadí výskytu v požadavku. |
| N5 (S) | 9.5 | **Pořadí vrácených záznamů.** Není vyžadováno FIFO. Přeuspořádání paketů výrazně zhoršuje TCP nad VPN. Doporučení: server MĚL BY zachovat pořadí publikace. |
| N6 (S) | 11.1 | **`Content-Length` odpovědi.** Není zmíněn. Bez něj klient nerozezná zkrácenou odpověď od prázdné fronty a nelze držet keep-alive. Doporučení: povinné `Content-Length` u všech odpovědí. |
| N7 (S) | 5, 13 | **Chování metod.** Není řečeno, co vrací `POST /phpvpn.php` bez `action`, `GET ?action=info`, `OPTIONS` nebo `HEAD ?action=info`. Doplnit tabulku metod a obsah hlavičky `Allow` pro každou akci. |
| N8 (S) | 5, 13 | **Tolerance `Content-Type`.** Kapitola 13 zmiňuje `415`, ale nikde není řečeno, zda je `charset` povinný, zda se porovnává case-insensitive a zda se toleruje jiné pořadí parametrů. Doporučení: porovnávat pouze media type, case-insensitive, parametry ignorovat. |
| N9 (S) | 9.3 | **Minimální velikost payloadu.** `ciphertext` „nesmí být prázdný“, ale nejmenší platný IPv4 paket má 20 bajtů. Doplnit dolní mez a ušetřit tím i práci příjemci. |
| N10 (S) | 3.1, 9.3 | **`dst` rovné `src`.** Kapitola 9.5 self-send implicitně povoluje, kapitola 3.2 pro něj ale nepředepisuje `PAYLOAD_KEYS[A][A]`. Rozhodnout a napsat to. |
| N11 (S) | 10.5, 10.6 | **`STORAGE_ROOT`.** Konstanta se používá v kapitole 10.6, ale není v tabulce limitů ani nikde definována její výchozí hodnota a požadovaná práva. |
| N12 (S) | 11.1 | **`MAX_INFO_PLAINTEXT_BYTES` a `AUTH_TIMESTAMP_SKEW_SECONDS` nejsou v `info.limits`.** Klient nemá jak zjistit skutečné okno tolerance času. Doplnit do publikovaných limitů. |
| N13 (S) | 3.1 | **Validace konfigurace.** Není řečeno, co dělat, když `ACCESS_KEYS` obsahuje adresu mimo nakonfigurovaný rozsah, duplicitu, klíč špatné délky nebo když je mapa prázdná. |
| N14 (S) | 16.3 | **Chybí testovací vektory.** Kapitola 16.3 vyžaduje known-answer testy pro HKDF, HMAC, AES-GCM, `semiKey`, `fullAuthKey` i obě kanonizace, ale žádnou konkrétní hodnotu neuvádí. Bez vektorů v dokumentu nelze nezávislé implementace ověřit proti sobě. Toto je největší praktická mezera dokumentu. Doplnit přílohu s hexadecimálními vektory. |
| N15 (N) | 10.1 | **Definice logických bajtů dávky.** Není řečeno, zda se do velikosti počítá ukončovací LF. Podle `acceptedBytes` v kapitole 9.5 ano, ale kapitola 10.1 to neopakuje. |
| N16 (N) | 3.1 | **Chybí strop počtu klientů** a zmínka, že adresní prostor menší než `/24` mění validační pravidla. |

---

## 5. Výkon, IOPS a škálování

### V1 (V) Strop odchozí propustnosti

`MAX_DATA_RECORDS_PER_REQUEST = 16` při typické MTU 1500 bajtů znamená zhruba 24 kB užitečných dat na požadavek. Při desíti požadavcích za sekundu je odchozí strop okolo 1,9 Mbit/s bez ohledu na kvalitu linky. Zvyšování frekvence pollingu je drahé, protože každý požadavek je nový PHP proces.

Zároveň je `MAX_SERIALIZED_DATA_RECORD_BYTES` nastaveno na 262 144 bajtů, přestože největší možný IPv4 paket je 65 535 bajtů, což po base64url dá 87 380 znaků. Limit je tedy třikrát vyšší, než je fyzicky potřeba, a zbytečně zvětšuje DoS povrch.

**Doporučení:** změnit dvojici limitů tak, aby o velikosti požadavku rozhodovala výhradně celková velikost těla.

| Konstanta | Nyní | Návrh | Zdůvodnění |
|---|---:|---:|---|
| `MAX_SERIALIZED_DATA_RECORD_BYTES` | 262 144 | 98 304 | Pokryje maximální IPv4 paket i base64url režii s rezervou. |
| `MAX_DATA_RECORDS_PER_REQUEST` | 16 | 512 | Odstraní umělý strop propustnosti, tělo stále omezuje `MAX_REQUEST_BODY_BYTES`. |
| `MAX_REQUEST_BODY_BYTES` | 4 198 400 | 1 052 672 | 1 MiB plus autentizační řádek. Menší tělo znamená menší paměť, menší latenci a menší DoS povrch. |
| `MAX_QUEUE_BYTES_PER_CLIENT` | 16 777 216 | 2 097 152 | 16 MiB nelze při TTL 60 s a klientském timeoutu 15 s doručit. Viz R4. |
| `AUTH_TIMESTAMP_SKEW_SECONDS` | 60 | 10 | Viz B1. |
| `MAX_INFO_PLAINTEXT_BYTES` | 262 144 | 32 768 | 253 klientů po zhruba 50 bajtech dá 13 kB. 256 kB je u softwarového AES několik sekund CPU. |

### V2 (V) `clients.json` v horké cestě

Kapitola 10.6 předepisuje při každém požadavku `data` načíst celý centrální soubor, dekódovat JSON, porovnat čas a nejvýše jednou za 30 sekund celý soubor přepsat. Při N klientech a pollingu po 100 ms to je 10N čtení a JSON dekódů za sekundu a globální bod sváru, u kterého sama specifikace přiznává, že si workeři navzájem přepisují data.

**Doporučení:** nahradit centrální JSON per-client značkou reprezentovanou časem změny souboru.

- Zápis `lastSeen` je `touch(STORAGE_ROOT . '/q/' . $id . '/.seen')`, tedy jeden metadata syscall bez zápisu obsahu.
- Čtení stáří je `@filemtime()`, tedy jeden `stat`, který je téměř vždy obsloužen z dentry cache.
- Odpověď `info` projde nakonfigurované adresy a přečte `filemtime`. Děje se to jen u vzácné akce `info`, ne v horké cestě.
- Race condition mezi workery zmizí, protože každý klient píše výhradně do svého vlastního souboru.
- Odpadá celá kapitola 10.6 včetně pravidel o poškozeném JSON, což zkrátí specifikaci i implementaci.

Přínos: z jednoho čtení a periodického zápisu vícekilobajtového souboru se stane jeden `stat` a nejvýše jeden `touch` za 30 sekund.

### V3 (V) IOPS profil pollingu

Dominantní operace celého systému je poll s prázdnou frontou. Návrh ovladače `disk` má být optimalizovaný právě na něj.

| Operace | Naivní implementace | Doporučená implementace |
|---|---|---|
| Poll s prázdnou frontou | `flock` + `scandir` + N × `stat` + `rename` + `unlink` | `opendir` + `readdir` a konec. Žádný zápis, žádný zámek. |
| Poll s daty | čtení souborů do paměti, JSON dekód, JSON enkód | `rename` adresáře, `readfile()` po jednotlivých souborech přímo do výstupu, `unlink` až po flushnutí |
| Enqueue 16 záznamů | 16 × open, write, close, rename | 1 × `file_put_contents` do tmp, 1 × `rename` |
| Zjištění velikosti fronty | N × `stat` | 0 syscallů, velikost je součástí názvu souboru |
| Kontrola TTL | N × `stat` | 0 syscallů, expirace je součástí názvu souboru |
| Zápis `lastSeen` | čtení, dekód, enkód a zápis sdíleného JSON | nejvýše 1 × `touch` za 30 s |

Klíčový trik: kódovat metadata do názvu souboru. Název `<expiryEpoch>-<bytes>-<src>-<uuid>.ndjson` umožní jediným `readdir` současně zjistit celkovou velikost fronty, vyřadit expirované dávky a spočítat podíl podle zdroje, a to bez jediného `stat`.

Druhý klíčový trik: uložený soubor obsahuje **doslovné bajty přijatých řádků**. Požadavkový a odpovědní záznam `data` mají identické schéma i pole, takže server nemusí nic dekódovat ani znovu serializovat. Vyprázdnění fronty je pak čistě `rename` plus `readfile`. Aby to bylo legální, musí specifikace v kapitole 9.5 doplnit, že vrácený záznam je bajtově shodný s přijatým. Dnes říká jen „ve stejném schématu“.

### V4 (S) Nepotřebné `fsync`

Data mají TTL 60 sekund a protokol výslovně přijímá jejich ztrátu. Volání `fflush()` do jádra stačí, `fsync()` nebo `fdatasync()` by byla čistá ztráta IOPS. Implementace nemá durability nikde vynucovat.

Navazující doporučení: umožnit nastavit `STORAGE_ROOT` na `/dev/shm` nebo jiný tmpfs. Diskové IOPS pak klesnou na nulu, sémantika protokolu zůstane nezměněna a jediným dopadem je ztráta front při restartu stroje, což je stav, který protokol už dnes připouští. U provozu na jediném uzlu je to nejlepší dostupná varianta.

### V5 (S) Oportunistický sweep

Kapitola 10.4 povoluje oportunistickou údržbu, kapitola 15 vyžaduje časový a početní rozpočet. Bez dalšího upřesnění bude implementátor sweepovat při každém požadavku, což je `scandir` přes všechny klientské adresáře.

**Doporučení:** sweep spouštět pravděpodobnostně, například `random_int(1, 200) === 1`, vždy pouze nad jedním náhodně vybraným klientským adresářem, s tvrdým stropem odstraněných souborů na jeden běh. Vlastní adresář volajícího se stejně čistí při každém drainu, protože expirace je čitelná z názvů souborů.

### V6 (S) Režie wire formátu

Pro paket o 1500 bajtech vypadá rozpočet takto.

| Složka | Bajty |
|---|---:|
| ciphertext v base64url | 2 004 |
| tag v base64url | 22 |
| iv v base64url | 16 |
| `messageId` jako UUID | 36 |
| `src` a `dst` | 22 |
| pevná JSON struktura a LF | zhruba 90 |
| **celkem** | **zhruba 2 190** |

Režie je tedy okolo 46 procent. Base64 sama o sobě dělá 33 procent a JSON zbytek. Pro v1 je to přijatelná cena za čitelnost a jednoduchost, ale stojí za zmínku v dokumentu, protože ovlivňuje kapacitní plánování. Pokud se v budoucnu bude řešit efektivita, největší jednotlivá úspora je binární rámování v samostatné akci, ne drobné úpravy JSON.

Levná dílčí úspora bez změny formátu: `messageId` nemusí být UUIDv4. Šestnáct náhodných bajtů v base64url dá 22 znaků místo 36 a validace je levnější než kontrola verze a variantních bitů UUID.

### V7 (S) Komprese

Obsah je ciphertext, tedy nestlačitelný. Zapnutá komprese jen pálí CPU. Implementace má aktivně vypnout `zlib.output_compression` a neposílat `Accept-Encoding` závislé odpovědi. Vedlejším přínosem je, že se nelze dostat do situací typu BREACH.

### V8 (S) Ověření před uložením vynucuje bufferování

Protože HMAC v `auth-full` pokrývá `SHA256HEX(body)`, musí server přečíst a zahashovat celé tělo dřív, než smí cokoli uložit. Neautentizovaný útočník tak vynutí přečtení a zahashování celého povoleného těla. Se sníženým `MAX_REQUEST_BODY_BYTES` na 1 MiB je to zhruba 2 až 3 ms CPU na požadavek, což je snesitelné.

Implementace má tělo číst proudově po řádcích, průběžně aktualizovat `hash_update()` a zároveň si ukládat doslovné bajty řádků do paměti pro pozdější zápis. Nikdy nenačítat celé tělo jedním `file_get_contents('php://input')` a nedělit ho `explode()`, protože to znamená dvě až tři kopie stejných dat v paměti.

### V9 (N) Cena PHP procesu

Při pollingu po 100 ms a deseti klientech to je 100 požadavků za sekundu, každý s vlastním bootstrapem PHP. Optimalizace samotné aplikace nepomůže, pokud běží bez OPcache. Implementace má být jediný soubor bez `require`, bez autoloaderu a bez `session_start()`, aby náklad bootstrapu byl minimální. Do dokumentace nasazení patří doporučení zapnout OPcache a nastavit `opcache.validate_timestamps=0`.

### V10 (N) Latence pollovacího modelu

Kapitola 11.2 zakazuje long-polling i `sleep`. Při intervalu 100 ms to znamená průměrně 50 ms navíc v každém směru, tedy zhruba 100 ms navíc k RTT. Interaktivní provoz přes takovou VPN bude nepříjemný, dávkový bude fungovat dobře.

Alternativa je volitelné krátké čekání na serveru, například až 500 ms s kontrolou přes `readdir` každých 25 ms. Sníží současně latenci i IOPS, ale drží PHP workera, což při vyšším počtu klientů vede k jejich vyčerpání. Pokud se to zavede, musí to být vypnuté ve výchozím stavu a omezené stropem souběžně čekajících workerů. Dokument by měl tento kompromis pojmenovat, protože implementátoři na něj narazí okamžitě.

---

## 6. Kryptografie bez PHP rozšíření

Tohle je nejdůležitější rozhodnutí celé implementace. Rozeberu, co je z jádra PHP skutečně dostupné, a nabídnu tři varianty.

### 6.1 Co je bez rozšíření k dispozici

| Funkce | Dostupnost | Poznámka |
|---|---|---|
| `hash()`, `hash_hmac()`, `hash_init()`, `hash_update_stream()` | ext/hash, od PHP 7.4 nelze vypnout | SHA-256 a HMAC-SHA-256 jsou tedy vždy k dispozici a běží v C. |
| `hash_equals()` | jádro | Porovnání konstantním časem. |
| `random_bytes()` | jádro | CSPRNG, může vyhodit `Random\RandomException`. |
| `hash_hkdf()` | ext/hash | **Nepoužívat.** Vyhodí `ValueError` při prázdném klíči, viz R2. |
| `json_encode()`, `json_decode()` | ext/json, od PHP 8 nelze vypnout | K dispozici vždy. |
| `openssl_encrypt()` s `aes-256-gcm` | ext/openssl, není garantované | Ve většině distribucí a oficiálních Windows buildů zapnuté, ale zaručit to nelze. |
| `sodium_crypto_aead_*` | ext/sodium, přibalené od PHP 7.2, ne vždy zapnuté | AES-GCM varianta navíc vyžaduje hardwarovou podporu AES, zjistitelnou přes `sodium_crypto_aead_aes256gcm_is_available()`. |

Závěr je jednoznačný. SHA-256, HMAC a CSPRNG jsou vždy dostupné. AES není nikdy zaručené.

### 6.2 Varianta A, doporučená: server AES nepotřebuje

Server potřebuje AES jedině pro zašifrování odpovědi `info`. Payload nikdy nedešifruje. Formát `info` je čistě mezi serverem a klientem tohoto protokolu, takže se dá zvolit libovolná konstrukce.

Nahradit AES-256-GCM v kapitole 8.3 konstrukcí encrypt-then-MAC nad HMAC-SHA-256:

```text
salt   = 32 náhodných bajtů
key    = HKDF-SHA-256(SERVER_SECRET, salt, "phpvpn/info-response/v2" || NUL || requestId, 64)
encKey = prvních 32 bajtů key
macKey = zbývajících 32 bajtů key

keystream = HKDF-Expand(encKey, "phpvpn/info-stream", délka plaintextu)
ciphertext = plaintext XOR keystream

aad = "PHPVPN-INFO" LF "info" LF requestId LF BASE64URL(salt)
tag = HMAC-SHA-256(macKey, aad LF ciphertext)
```

Vlastnosti:

- žádné AES, žádný IV, žádné rozšíření, veškerá práce běží v C přes `hash_hmac()`,
- rychlost okolo 32 bajtů keystreamu na jedno volání HMAC, což pro plaintext 13 kB znamená zhruba 400 volání, tedy jednotky milisekund,
- konstantní čas z podstaty, žádné tabulkové vyhledávání a tedy žádný cache postranní kanál,
- odpadá kapitola 15 s `503 server_misconfigured` při chybějícím AES a odpadá rozpor R1.

AES-256-GCM zůstane pouze v kapitole 9.4 pro end-to-end payload, kde ho provádí klient a kde se serveru netýká.

**Toto považuji za nejlepší poměr přínosu a rizika.** Odstraní to nejtěžší závislost a nejrizikovější kus kódu.

### 6.3 Varianta B: AES s detekcí backendu

Ponechat AES-256-GCM ve wire formátu a v implementaci detekovat backend v pořadí `openssl`, `sodium`, pure PHP. Konfigurační konstanta `CRYPTO_BACKEND` s hodnotou `auto`.

```php
if (function_exists('openssl_encrypt')
    && in_array('aes-256-gcm', openssl_get_cipher_methods(), true)) { /* openssl */ }
elseif (function_exists('sodium_crypto_aead_aes256gcm_is_available')
    && sodium_crypto_aead_aes256gcm_is_available()) { /* sodium */ }
else { /* pure PHP */ }
```

Nevýhodou je, že se udržují tři kódové cesty a tři sady chování. Pro jeden malý soubor je to zbytečná složitost.

### 6.4 Varianta C: pure-PHP AES-256-GCM

Pokud se AES ponechá a nesmí se spoléhat na rozšíření, platí následující konkrétní doporučení.

**Co implementovat:**

- Pouze šifrovací směr. GCM používá AES jen dopředně v režimu CTR, takže inverzní S-box ani inverzní tabulky nejsou potřeba. Ušetří to polovinu kódu.
- Klíčový rozvrh počítat jednou na klíč, ne na blok. Pro AES-256 to je 14 rund a 60 slov.
- Čtyři T-tabulky Te0 až Te3 po 256 položkách typu uint32, dohromady 4 kB, generované za běhu ze S-boxu při prvním použití. Jedna runda je pak 16 lookupů a 12 XORů místo bajtových operací.
- GHASH v GF(2^128) přes předpočítanou tabulku pro 4 bity, tedy 16 položek. Sestavení tabulky stojí 16 násobení a pak je jeden blok 32 lookupů. Osmibitová varianta s 256 položkami se pro krátké zprávy nevyplatí, protože její příprava je dražší než úspora.
- Prvky pole reprezentovat jako čtyři 32bitové limby uložené v PHP integerech, ne jako dva 64bitové. PHP nemá bezznaménkové celé číslo a `>>` je aritmetický posun, takže 64bitová reprezentace vede k chybám na nejvyšším bitu.

**Očekávaný výkon:** pure-PHP AES se pohybuje řádově ve stovkách kilobajtů za sekundu. Pro odpověď `info` o velikosti 13 kB to znamená desítky milisekund, což je přijatelné. Pro cokoli per-paket je to nepoužitelné. Proto v žádném případě nesmí být pure-PHP AES v datové cestě, což naštěstí protokol nevyžaduje.

**Postranní kanály:** T-tabulková implementace není konstantní v čase. V tomto konkrétním případě je riziko malé, protože klíč pro `info` se odvozuje z čerstvého náhodného saltu pro každou odpověď, takže útočník nemá nad jedním klíčem dost měření. Přesto to musí být v dokumentaci uvedeno a musí to být důvod k preferenci `openssl` nebo `sodium`, pokud jsou k dispozici.

**Povinná pojistka:** pure-PHP cesta musí při inicializaci projít known-answer testem proti oficiálním NIST GCM vektorům a výsledek si zapamatovat. Chybná implementace AES-GCM tiše produkuje pravidelně vypadající data, takže bez KAT se chyba pozná až u klienta.

---

## 7. Doporučená architektura implementace

### 7.1 Struktura souboru

Jeden soubor `phpvpn.php`, `declare(strict_types=1)`, PHP 8.2+, bez Composeru a bez autoloaderu. Členění navržené v kapitole 15 je rozumné, doplnil bych:

- **Konfiguraci oddělit do volitelného externího souboru.** Pokud existuje `phpvpn.config.php` vedle skriptu nebo na cestě dané proměnnou prostředí, načte se z něj. Jinak se použijí konstanty v hlavičce. Bez toho každý upgrade skriptu přepíše administrátorovi všechna tajemství, což je nejčastější příčina rozbitého nasazení u single-file aplikací.
- **Režim CLI pro self-test.** `php phpvpn.php selftest` spustí known-answer testy kryptografie, ověří zapisovatelnost `STORAGE_ROOT` a zkontroluje invarianty konfigurace. Umožní ověřit nasazení bez vystavení diagnostiky přes HTTP.
- **Jednotné výjimky.** `final class ProtocolError extends RuntimeException` s HTTP statusem a kódem z tabulky kapitoly 13. Jediný `set_exception_handler` mapuje cokoli neočekávaného na `500 internal_error` bez výpisu.
- **Nastavit na startu** `declare(strict_types=1)`, `date_default_timezone_set('UTC')`, `ini_set('display_errors', '0')`, `ignore_user_abort(true)`, `header_remove('X-Powered-By')` a vypnout výstupní buffering.

### 7.2 Ovladač `disk`

Rozvržení:

```
STORAGE_ROOT/
  .htaccess              Deny from all, pro Apache
  index.html             prázdný, proti listingu
  tmp/                   dočasné soubory pro rename ve stejném filesystému
  q/<id>/                jeden adresář na klienta, <id> je poslední oktet nebo hash adresy
    .seen                prázdný soubor, lastSeen je jeho mtime
    <expiry>-<bytes>-<src>-<uuid>.ndjson
  drain/<uuid>/          krátkodobě existující adresář claimnuté dávky
```

**Enqueue** je jeden `file_put_contents(tmp/x)` následovaný jedním `rename()` do `q/<dst>/`. Při `ENOENT` se jednou vytvoří adresář a `rename` se zopakuje. Žádný zámek, protože `rename()` v rámci jednoho filesystému je atomický. Kapacitní kontrola proběhne z názvů souborů získaných při `readdir`, což znamená TOCTOU okno a krátkodobé mírné překročení limitu. To je přijatelné a mělo by to být v dokumentu přiznané.

**Drain** má být takto:

1. `opendir` a `readdir` adresáře `q/<src>/`. Pokud v něm nejsou žádné datové soubory, konec. Toto je nejčastější případ a nestojí ani jeden zápis.
2. `rename('q/<src>', 'drain/<uuid>')`. Jediný atomický syscall, který celou frontu odebere z dosahu ostatních workerů. Tím je splněna podmínka at-most-once.
3. Znovu vytvořit prázdný `q/<src>` pro souběžné odesílatele.
4. Odeslat hlavičky včetně `Content-Length` spočítané ze součtu velikostí v názvech souborů plus délky řádku `data-result`.
5. Postupně `readfile()` jednotlivé neexpirované soubory přímo do výstupu. Žádné dekódování, žádné držení v paměti.
6. Odeslat `data-result`, případně `data-mac` z bodu B2.
7. Teprve po `flush()` soubory smazat a `drain/<uuid>` odstranit.

Bod sedm je důležitý. Data jsou z fronty klienta odebrána už v kroku dva, takže at-most-once platí, ale samotné mazání se odloží až za odeslání. Když worker mezitím spadne, zůstane osiřelý adresář v `drain/`, který uklidí sweep. Nikdy nedojde k opětovnému vydání.

Pozor na dvě věci:

- Souběžný odesílatel může přesně mezi krokem dva a tři narazit na neexistující adresář. Proto ta retry logika v enqueue.
- Na Windows `rename()` adresáře selže, pokud v něm má jiný proces otevřený soubor. Pokud má implementace běžet i na Windows, musí být selhání `rename` ošetřené jako „fronta je momentálně nedostupná“ a vést na retry nebo na prázdnou odpověď, nikoli na `500`.

**Sweep** projde `drain/` a jeden náhodně vybraný `q/<id>/`, expiraci vyčte z názvů a smaže. Spouštět pravděpodobnostně, viz V5.

### 7.3 Ovladač `memcached`

Memcached je pro tento protokol vhodný, ale má tři ostré hrany.

**Bez rozšíření.** `ext/memcached` i `ext/memcache` jsou rozšíření, takže odporují zadání. Textový protokol memcached je ale triviální a lze ho obsloužit přes `stream_socket_client()` z jádra. Doporučuji vlastní minimalistický klient na zhruba 120 řádcích, který umí `add`, `append`, `gets`, `cas`, `get` s více klíči a `delete`. Vedlejším přínosem je plná kontrola nad timeouty.

**Atomické vyprázdnění.** Memcached nemá operaci get-and-delete. Řeší se přes CAS:

1. `gets q:<id>` vrátí hodnotu a CAS token.
2. `cas q:<id> <flags> <exptime> 0 <cas>` s nulovou délkou hodnoty.
3. Pokud `cas` vrátí `EXISTS`, mezitím někdo připojil další data. Zopakovat od bodu jedna, s omezeným počtem pokusů.

Při úspěchu je zaručeno, že hodnotu dostal právě jeden worker. To je přesně sémantika, kterou kapitola 10.3 vyžaduje.

**Enqueue** je `add q:<id> "" <ttl>` a následně `append q:<id> <bajty>`. `append` je na straně serveru atomický, takže souběžní odesílatelé se nepřepíšou. Pokud `append` vrátí `NOT_STORED`, klíč mezitím vypršel a je nutné zopakovat `add`.

**Limit velikosti položky.** Výchozí maximum položky v memcached je 1 MiB. `MAX_QUEUE_BYTES_PER_CLIENT` 16 MiB je tedy nedosažitelné bez chunkování, které by ale rozbilo atomicitu vyprázdnění. Doporučuji chunkování nedělat a místo toho:

- nastavit efektivní `maxQueueBytesPerClient` na zhruba 900 kB,
- publikovat tuto skutečnou hodnotu v `info.limits`, což kapitola 8.2 už vyžaduje,
- chybu `SERVER_ERROR object too large for cache` mapovat na `429 queue_full`.

Limit fronty se tím vynucuje sám a nepotřebuje účtování velikosti. To je elegantní a ušetří to jednu celou vrstvu logiky.

**TTL** je nativní přes `exptime` nastavené při `add`, tedy od prvního publikování, což přesně odpovídá kapitole 10.4. Vedlejším efektem je, že vyprší celá položka najednou včetně později připojených záznamů. Je to drobná odchylka od per-dávkové sémantiky a patří do dokumentace.

**`lastSeen`** je klíč `s:<id>` s TTL a hodnotou času, zapisovaný nejvýše jednou za 30 sekund. `info` je načte jedním multi-get přes všechny nakonfigurované adresy, tedy jedním round tripem.

**Bezpečnost.** Memcached nemá v textovém protokolu autentizaci a historicky je to oblíbený cíl. Do dokumentace patří tvrdý požadavek na naslouchání jen na loopbacku nebo unixovém socketu a vypnutí UDP. Prefix klíčů odvodit z `HMAC-SHA-256(SERVER_SECRET, "phpvpn/keyspace")`, aby na sdílené instanci nedošlo ke kolizi ani k jednoduchému uhodnutí klíčů cizí aplikací.

**Trvanlivost.** Memcached položky bez varování vyhazuje kvůli LRU i před vypršením TTL. Protokol ztrátu připouští, takže to není blokující, ale musí to být napsané.

### 7.4 Volitelná ochrana proti přehrání s nulovou přídavnou čtecí IOPS

Reakce na B1. Diskový ovladač už při každém požadavku dělá `readdir` klientského adresáře. Stačí tedy do tohoto adresáře uložit stavovou značku jako prázdný soubor, jehož jméno stav nese:

```
.state-<lastTimestampEpoch>-<r1>-<r2>-<r3>-<r4>
```

kde `r1` až `r4` jsou prvních 8 hexadecimálních znaků čtyř naposledy přijatých `requestId`.

- Čtení stavu je zdarma, jméno přijde spolu s ostatními položkami z `readdir`.
- Ověření je: odmítnout, pokud je timestamp starší než uložený, nebo pokud se zkrácené `requestId` shoduje s některým ze čtyř uložených.
- Aktualizace je jeden `rename()` značky, tedy jediná metadata operace bez zápisu obsahu.

Náklad je tedy jeden `rename` na požadavek navíc a nula čtení. Přínos je úplné odstranění útoku B1 i replayu odesílaných dat. Omezení je nejvýše čtyři souběžné požadavky téhož klienta uvnitř jedné sekundy, což pro pollovacího klienta bohatě stačí.

Do specifikace to vyžaduje změnu, protože kapitola 16.6 bod 6 dnes existenci jakéhokoli nonce úložiště výslovně zakazuje. Doporučuji z toho udělat konfigurační volbu `REPLAY_PROTECTION` s hodnotami `off` a `strict` a zakázat pouze to, aby se stav ukládal do sdíleného globálního úložiště.

---

## 8. Čemu se při implementaci vyvarovat

| Nedělat | Proč | Místo toho |
|---|---|---|
| `hash_hkdf()` | `ValueError` při prázdném klíči, viz R2 | Vlastní HKDF nad `hash_hmac()` |
| `file_get_contents('php://input')` a `explode("\n", ...)` | dvě až tři kopie až megabajtového těla v paměti | proudové `stream_get_line($in, $max, "\n")` s průběžným `hash_update()` |
| `$_POST` nebo `$_REQUEST` | HMAC pokrývá doslovné bajty, PHP tělo pro formulářové typy zkonzumuje a `php://input` je pak prázdný | číst výhradně `php://input` a nepovolený `Content-Type` odmítnout dřív, než se tělo dotkne |
| spoléhat na `json_decode()` u detekce duplicitních klíčů | tiše přepíše, kapitola 6.1 to výslovně zakazuje | anchorovaný PCRE nad celým řádkem, který schéma vynutí, a `json_decode` jen jako záložní cesta |
| `scandir()` s výchozím řazením a `glob()` | zbytečné řazení a u `glob()` navíc drahé vyhodnocování vzoru | `opendir` a `readdir`, případně `scandir(..., SCANDIR_SORT_NONE)` |
| `flock()` na NFS a na síťových discích | nespolehlivé, hrozí tiché porušení atomicity | návrh postavený na `rename()`, `STORAGE_ROOT` na lokálním filesystému |
| `rename()` z `sys_get_temp_dir()` | jiný filesystém znamená kopii, ne atomický přesun | `tmp/` uvnitř `STORAGE_ROOT` |
| `fsync()`, `fflush()` s vynucením na disk | data mají TTL 60 s a jejich ztráta je přijatá | nechat práci page cache |
| `$src` přímo v cestě | riziko path traversal při chybě jediného znaku v regexu | validovat a použít pouze číselnou hodnotu posledního oktetu nebo hex hash |
| `sleep()`, `usleep()`, long-polling | drží PHP workera, kapitola 11.2 to zakazuje | okamžitá odpověď, případně volitelné krátké čekání se stropem podle V10 |
| vlastní AES v datové cestě | řádově stovky kilobajtů za sekundu | server AES v datové cestě nepotřebuje, viz varianta A |
| `session_start()`, cookies, `$_SESSION` | protokol je bezstavový, session by přidala zámek na soubor při každém požadavku | nic z toho nepoužívat |
| výpis výjimek, `var_dump`, `trigger_error` do výstupu | kapitola 4.1 zakazuje únik tajemství a cest | jednotný handler, `display_errors=0`, logovat jen kódy chyb |
| `Content-Encoding: gzip` | ciphertext je nestlačitelný, jen CPU navíc | explicitně vypnout `zlib.output_compression` |
| `DateTime::createFromFormat` v horké cestě | drahé a tolerantnější, než kapitola 8.1 dovoluje | anchorovaný regex, `checkdate()` a `gmmktime()` |
| `preg_match` na validaci base64url | pomalejší než potřeba | `strspn($s, 'A-Za-z0-9-_')` porovnané s délkou, plus kontrola zbytkových bitů |
| `==` na porovnání tagů | timing i typová záludnost PHP | výhradně `hash_equals()` nad dekódovanými bajty |
| odmítat `dst` mimo `ACCESS_KEYS` až při doručení | plnily by se fronty neexistujících klientů, viz B7 | odmítnout hned při validaci záznamu |

---

## 9. Co použít

**Kryptografie**

- `hash_hmac('sha256', $data, $key, true)` pro HMAC i pro ručně implementovaný HKDF.
- `hash_init('sha256')` a `hash_update()` pro proudový hash těla.
- `hash_equals()` pro každé porovnání tagu.
- `random_bytes()` pro salt, IV a identifikátory, vždy v `try` bloku kvůli `Random\RandomException`.
- `strtr(base64_encode($b), '+/', '-_')` a `rtrim(..., '=')` pro base64url. Zpět přes `strtr` a doplnění paddingu, s předchozí kontrolou abecedy a zbytkových bitů.

**Vstup a výstup**

- `fopen('php://input', 'rb')` a `stream_get_line()` s limitem.
- `readfile()` nebo `fpassthru()` pro odeslání uložených dávek bez načtení do paměti.
- Povinné hlavičky `Content-Type`, `Content-Length`, `Cache-Control`, `Pragma`, `X-Content-Type-Options` a pro HTML stránku navíc `Referrer-Policy: no-referrer` a restriktivní `Content-Security-Policy`.

**Úložiště**

- `file_put_contents()` do `tmp/` a `rename()` pro publikaci.
- `opendir` a `readdir` pro čtení fronty.
- `touch()` a `filemtime()` pro `lastSeen`.
- `clearstatcache(true, $path)` po každém `rename`, pokud se stat výsledek dále používá.
- `stream_socket_client()` s explicitním timeoutem pro memcached.

**Čas**

- `time()` pro srovnání a `gmdate('Y-m-d\TH:i:s\Z')` pro formátování.
- `date_default_timezone_set('UTC')` hned na začátku.
- Validace timestampu regexem `/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$/` a následně `checkdate()` a `gmmktime()`.

**Provoz**

- OPcache zapnutá, `opcache.validate_timestamps=0` v produkci.
- `STORAGE_ROOT` mimo document root, ideálně na tmpfs.
- `post_max_size` a limity proxy nastavené přísně nad `MAX_REQUEST_BODY_BYTES`.
- Kontrola, že se nenasadí `phpvpn.php.bak` nebo `phpvpn.php~`, protože takové soubory webserver vydá jako text i s tajemstvími. Do self-testu patří i kontrola oprávnění konfiguračního souboru.

---

## 10. Čitelnost a struktura dokumentu

Dokument je věcně velmi solidní a jeho poctivost ohledně nezaručených vlastností je nadstandardní. Slabinou je struktura.

1. **Opakování.** Téma replay se v normativní podobě objevuje v kapitolách 1.2, 2, 4.2, 8.1, 9.2, 10.3 a 16.6. At-most-once v 1.2, 2, 9.5, 10.3, 12.2 a 17. Každé opakování je jinou formulací téhož, což při implementaci nutí porovnávat, zda někde není skrytý rozdíl. Doporučení: jednou normativně, jinde jen odkaz.
2. **Chybí obsah a rychlá reference.** U dokumentu o 640 řádcích chybí obsah. Chybí i souhrnná tabulka wire formátu, ze které by implementátor viděl všechny typy zpráv a jejich pole na jedné obrazovce.
3. **Chybí testovací vektory.** Viz N14. Toto je nejzávažnější praktický nedostatek. Kapitola 16.3 vyžaduje known-answer testy, ale žádná known answer v dokumentu není.
4. **Nevyvážená hloubka.** Nejlépe specifikovanou částí dokumentu je best-effort statistika `lastSeen`, tedy věc, která nemá žádný vliv na funkci. Naproti tomu pořadí validace vůči autentizaci, determinismus částečného přijetí a pořadí vrácených záznamů specifikované nejsou. Doporučuji kapitolu 10.6 zkrátit zhruba na třetinu, ideálně současně se změnou podle V2, a uvolněné místo věnovat bodům z kapitoly 4 této analýzy.
5. **Terminologie.** Pojmy dávka, záznam, buffer a fronta se používají promíchaně. Zavést v kapitole 3 slovníček a držet se ho.
6. **Nedefinované konstanty.** `STORAGE_ROOT` se používá, ale není v tabulce limitů. `STORAGE_DRIVER` je zmíněn jen v kapitole 10.5.
7. **Chybí hlavička dokumentu.** Verze, datum, stav a odkaz na text licence MIT. Kapitola 1 licenci zmiňuje, ale v repozitáři žádný soubor `LICENSE` není.
8. **Míchání jazyků.** Normativní text je česky, wire řetězce anglicky. To je správně, ale u klíčových slov `MUSÍ` a `NESMÍ` chybí odkaz na jejich anglické ekvivalenty z RFC 2119, což ztíží případný překlad specifikace.
9. **Kapitola 16 je vlastně akceptační kritéria.** Pro implementátora by bylo užitečné mít u každého scénáře odkaz na kapitolu, kterou ověřuje, a naopak.

---

## 11. Doporučené pořadí prací

| Krok | Obsah | Blokuje implementaci |
|---|---|---|
| 1 | Rozhodnout R1 a R2, tedy zvolit variantu A z kapitoly 6 a ruční HKDF | ano |
| 2 | Rozhodnout R3, oddělit vyprázdnění bufferu od výsledku odesílání | ano |
| 3 | Upravit limity podle V1 a R4 | ano |
| 4 | Doplnit N1 až N8, tedy kanonický base64url, pořadí validace, atomicita, determinismus, FIFO, `Content-Length`, tabulka metod, tolerance `Content-Type` | ano |
| 5 | Doplnit B5, tedy kontrolu `dst` u příjemce | ano |
| 6 | Přidat `data-mac` podle B2 | ne, ale mělo by být ve v1 |
| 7 | Přidat `tag` do `health` podle B3 a zkrátit skew na 10 s podle B1 | ne, ale mělo by být ve v1 |
| 8 | Nahradit `clients.json` mtime markerem podle V2 | ne |
| 9 | Doplnit konstrukci IV a rotaci klíčů podle B4 | ne, ale nutné před produkčním nasazením |
| 10 | Doplnit `REPLAY_PROTECTION` podle 7.4 | ne, volitelné |
| 11 | Doplnit testovací vektory podle N14 | ne, ale nutné před druhou implementací |
| 12 | Redakční úpravy podle kapitoly 10 | ne |

Kroky 1 až 5 doporučuji vyřešit před napsáním první řádky kódu, protože každý z nich mění datové struktury nebo řídicí tok.
