# Kontrola oprav v `protocol.md`

Kontrola porovnává aktuální `protocol.md` s body v `problems.md`. Většina konkrétních návrhových chyb byla opravena: zejména rozpor kolem AES a prázdného `SERVER_SECRET`, autentizace `health`, `data-mac`, kanonické base64url, kontrola cílové adresy u příjemce a chování při plné cílové frontě.

Níže jsou body, které zůstávají nevyřešené, jsou jen výslovně akceptovaným rizikem, nebo jsou nově patrné.

## Neodstraněná rizika z původního seznamu

### P1 — Replay HTTP pollingu stále umožňuje ztrátu fronty (B1)

Kapitoly 1.2, 4.2 a 10.3 přesně popisují, že kopie platného `data` pollingu v 60sekundovém okně může vyzvednout a nenávratně odstranit frontu oběti. Dokument toto riziko už nezamlčuje, ale neodstraňuje je: výslovně zakazuje nonce, replay cache i obnovu fronty.

**Dopad:** pasivní odposlech čistého HTTP stačí k opakovanému odepření doručení; ciphertext útočník obvykle nepřečte.

**Náprava:** pro bezpečný provoz vyžadovat HTTPS, nebo přidat per-klientskou replay ochranu/potvrzované vydání. Pokud má HTTP zůstat podporované, bezpečnostní vlastnosti musí jasně říkat, že pro polling neposkytuje dostupnost.

### P2 — Unikátnost GCM IV není technicky zajištěna (B4)

Kapitola 9.4 požaduje, aby odesílatel zabránil opakovanému použití IV pro stejný `PAYLOAD_KEY`, ale současně předepisuje pouze náhodných 12 bajtů z CSPRNG a nepožaduje persistentní čítač, detekci kolizí ani rotaci klíče. Náhodné IV riziko jen snižuje; samo o sobě nesplňuje absolutní požadavek „MUSÍ zabránit“ a při dlouhodobém provozu nemá definovaný limit počtu šifrování na klíč.

**Náprava:** použít per-klíčový persistentní monotónní čítač/nonce konstrukci nebo zavést povinnou rotaci `PAYLOAD_KEY` s jasným limitem zpráv na klíč.

### P3 — Chybí globální limit úložiště a kvóty odesílatele (B6, B7)

Protokol omezuje frontu jednoho cíle, ale neurčuje celkový limit úložiště, rezervu pro systém ani kvótu, kterou může jeden autentizovaný klient obsadit napříč cíli. Kapitola 1.2 výslovně nepřisluhuje dostupnost při zaplnění úložiště.

**Dopad:** legitimní nebo kompromitovaný klient může zaplnit mnoho cílových front a vyčerpat disk, inody nebo další provozní zdroje relay.

**Náprava:** zavést globální hard limit, per-source kvótu/rate limit a deterministické chování při jejich dosažení.

### P4 — Společné `SERVER_SECRET` rozšiřuje dopad kompromitace klienta (B8)

Každý oprávněný klient zná stejný `SERVER_SECRET`. Držitel tohoto tajemství může ověřovat a vytvářet `auth-semi`, ověřovat či vytvářet odpovědi `health` a dešifrovat `info`. `ACCESS_KEY` sice brání vytvoření `auth-full` za jiného klienta, ale neomezuje tento širší dopad kompromitace.

**Náprava:** nahradit sdílené serverové tajemství per-klientským autentizačním materiálem, případně explicitně omezit `info` a `health` na klíče odvozené z `ACCESS_KEY`.

### P5 — Výchozí známé tajemství zůstává funkční (B10)

Výchozí řetězec nul je nadále platná a funkční konfigurace; veřejná stránka pouze varuje. Nasazení bez změny tajemství tedy dovoluje komukoli vytvořit platné `auth-semi` a ověřovat/vytvářet chráněné odpovědi závislé jen na `SERVER_SECRET`.

**Náprava:** v produkčním režimu odmítnout známé či prázdné hodnoty při startu; pokud je zachování kompatibility nutné, vyžadovat explicitní nebezpečný opt-in mimo výchozí konfiguraci.

### P6 — `info` stále prozrazuje síťová metadata všem držitelům `SERVER_SECRET` (B13)

Šifrování odpovědi `info` neřeší autorizaci uvnitř skupiny klientů, protože klíč vychází z jediného společného `SERVER_SECRET`. Klient tak může získat údaje o ostatních klientech, které obsah `info` publikuje.

**Náprava:** autorizovat `info` per klient přes `ACCESS_KEY` a vracet jen nezbytná data, nebo tuto viditelnost deklarovat jako záměrnou administrativní vlastnost.

### P7 — Analýza provozu zůstává možná (B14)

Čisté HTTP odhaluje adresy, velikosti, časování a identifikátory. I HTTPS běžně odhaluje časování a velikost přenosů. Protokol nepoužívá padding, mixování ani dávkování s ochranou metadat.

**Náprava:** pouze pokud je ochrana metadat cílem, doplnit padding a jednotnější odesílací režim; jinak ponechat jako výslovné omezení.

## Neodstraněné nedospecifikování

### P8 — Dokument neobsahuje konkrétní testovací vektory (N14)

Kapitola 16 nařizuje known-answer testy, ale neuvádí žádné pevné vstupy a očekávané hex/base64url výstupy pro HKDF, HMAC, AES-GCM, `data-mac` a `health`. Nezávislá implementace proto nemá normativní referenční hodnoty.

**Náprava:** přidat přílohu s kompletními vektory a přesnými bajty vstupů i výstupů.

### P9 — `Content-Length` úspěšných odpovědí není povinný (N6)

Specifikace stanoví typy těl a maximální velikosti, ale nikde normativně nevyžaduje `Content-Length` ani pravidlo pro odpovědi přenášené po částech. Klient tak nemá na úrovni HTTP jednoznačný kontrakt pro detekci předčasně ukončené odpovědi; `data-mac` chrání pouze úspěšně dočtené a syntakticky vyhodnocené tělo `data`.

**Náprava:** vyžadovat `Content-Length` pro všechny odpovědi, které nejsou záměrně chunkované, nebo přesně definovat podporované chunkované chování a povinnou reakci klienta na předčasné ukončení.

### P10 — Deterministické pořadí cílových skupin není definováno (N4)

Kapitola 10.2 říká pouze, že cílové skupiny se zpracovávají „deterministicky“. Neurčuje klíč pořadí ani pořadí položek ve výsledných polích při více cílech. Dvě kompatibilní implementace mohou dojít k jinému pořadí částečného přijetí.

**Náprava:** předepsat například řazení podle číselné hodnoty `dst` a uvnitř skupiny zachovat pořadí výskytu v requestu.

### P11 — Pořadí vydávání dávek není garantováno (N5)

Kapitola 10.3 stanoví at-most-once převzetí jednotlivých dávek, ale nevyžaduje FIFO podle času publikace. Diskový název sice nese čas expirace, nikoli jednoznačné pořadí publikace, a specifikace nevyžaduje jeho řazení.

**Dopad:** vyšší míra přehození IP paketů může zhoršit výkon TCP.

**Náprava:** vyžadovat FIFO v rámci zdroj–cíl nebo jasně deklarovat, že relay pořadí negarantuje.

## Nově nalezený problém

### P12 — Povinná čistě PHP AES-GCM implementace zvyšuje kryptografické riziko

Specifikace zakazuje OpenSSL, Sodium i jiné kryptografické rozšíření a vyžaduje vlastní AES-GCM v PHP. To zvyšuje riziko implementační chyby a typicky neposkytuje konstantní čas ani ochranu, kterou nabízí prověřené nativní knihovny. Povinný self-test s jedním známým vektorem neprokazuje bezpečnost celé implementace ani odolnost vůči postranním kanálům.

**Náprava:** preferovat prověřenou nativní kryptografickou knihovnu; pokud je čisté PHP neměnný požadavek, zdokumentovat postranní kanály jako omezení, dodat více nezávislých testovacích vektorů a podrobit implementaci samostatnému kryptografickému auditu.
