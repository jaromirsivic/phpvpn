---
intent_version: 2
created_at: 2026-09-13-18-22-02
based_on: intent-v001.md
source: intent-chat-v002.md
---

# Intent: status page, rozšířené info API a přehled komunikačních kontraktů

## Shrnutí

Rozšířit serverový skript `phpvpn.php` a Windows klienta tak, aby server měl novou stavovou stránku podle `assets/index.html`, podporoval volitelnou a zpětně kompatibilní publikaci maskovaných komunikačních kontraktů a Tray aplikace zobrazovala přehled online a offline klientů s ověřením shody komunikačních kontraktů.

Součástí změny je přejmenování výchozí datové složky, nový connect checkpoint a read-only tabulka na kartě `Who is online`.

## Ověřený současný stav projektu

- `phpvpn.php` je samostatně nasaditelný PHP 8.2+ relay server.
- `StatusPageHandler` dnes vrací pouze jednoduché HTML s textem `Server is running` a podmíněným bezpečnostním varováním.
- Požadovaný HTML mockup existuje v `assets/index.html`.
- Výchozí `BUFFER_DIRECTORY` je `./phpvpn`.
- Klientský soubor `info.json` je dnes prázdný marker. Jeho mtime představuje `lastSeen` a běžný touch je omezen konstantou `CLIENT_STATS_MIN_UPDATE_SECONDS`, nyní 30 sekund.
- Dnešní info request je `auth-semi`, neobsahuje identitu klienta a ověřuje se společným server secretem.
- Zašifrovaný info plaintext obsahuje `serverVersion`, `generatedAt`, `limits` a `clients`. Každý vrácený klient má `address` a `lastSeen`. Klient, který ještě nikdy server nekontaktoval, se nevrací.
- Connect sekvence používá `health`, běžné `info` a prázdný autentizovaný `data` request.
- Tray okno `Server information` má karty `Who is online` a `Raw server information`. První karta zatím nemá požadovanou tabulku.

## 1. Stavová stránka serveru

### 1.1 Nový vzhled

- Převzít HTML, CSS a JavaScript z `assets/index.html` a zabudovat je přímo do jediného souboru `phpvpn.php`.
- Výstup musí zůstat samostatný a nesmí za běhu záviset na externím souboru `assets/index.html`.
- Zachovat funkci generování bezpečného 32bytového klíče jako 64 hexadecimálních znaků a jeho kopírování podle mockupu.
- Pokud server používá výchozí nebezpečný server secret, zobrazit kompletní mockup včetně červeného warning panelu.
- Pokud je server secret považovaný za bezpečný, odstranit celý červený warning panel. Ostatní obsah stránky zachovat.

### 1.2 Zapnutí a vypnutí

- Do konfigurační hlavičky `phpvpn.php` přidat boolean konstantu `STATUS_PAGE_ENABLED`.
- Výchozí hodnota je `true`.
- Nastavení srozumitelně zdokumentovat přímo u konstanty v `phpvpn.php`, včetně dopadu hodnot `true` a `false`.
- Při `STATUS_PAGE_ENABLED === false` musí `GET` kořenové adresy vrátit standardní HTTP `404` s chybou `not_found`, bez HTML stavové stránky.
- `HEAD` se musí chovat konzistentně s `GET` a nesmí vracet tělo.
- Vypnutí stránky nesmí omezit akce REST API `health`, `info` ani `data`.
- Debug mode není součástí tohoto intentu.

## 2. Přejmenování výchozí datové složky

- Změnit výchozí `BUFFER_DIRECTORY` z `./phpvpn` na `./phpvpn_data`.
- Aktualizovat související komentáře, příklady ochrany webserverem a dokumentaci v `phpvpn.php`.
- Vlastní administrátorem nastavená hodnota `BUFFER_DIRECTORY` musí být nadále respektována.
- Neprovádět automatickou migraci, přesun ani kopírování obsahu staré složky. Případný přesun provádí správce ručně.

## 3. Maskovaný komunikační kontrakt

### 3.1 Kanonický identifikátor kontraktu

- Kontrakt tvoří dvě VPN IPv4 adresy seřazené numericky vzestupně.
- Textový identifikátor má tvar `<nižší_IP>_<vyšší_IP>`, například `10.99.99.2_10.99.99.3`.

### 3.2 Výpočet

- Vstup pro SHA-256 sestavit přesně jako:

  `<contract>_<communication_key>_<configured_server_secret>`

- `communication_key` je 64znaková lowercase hex reprezentace 32bytového payload key příslušného komunikačního kontraktu.
- `configured_server_secret` je přesně text zadaný v konfiguraci, bez normalizace či změny velikosti písmen. I prázdná hodnota se pro tento hash použije jako prázdná.
- Vstup hashovat jako UTF-8 bajty.
- Výstup SHA-256 reprezentovat jako přesně 64 lowercase hexadecimálních znaků.
- Seznam klienta obsahuje jeden hash pro každý jeho aktuálně platný komunikační kontrakt.
- Heslo ani komunikační klíč se do info API neposílají v otevřené podobě.

### 3.3 Odlišení od autentizace requestu

- Pravidlo bez normalizace platí pouze pro výpočet maskovaného kontraktu.
- Klientský autentizační tag rozšířeného info requestu musí zůstat kompatibilní s plnou autentizací `data`, včetně současného pravidla, že pouze prázdný server secret se při derivaci autentizačního klíče normalizuje na výchozí hodnotu.

## 4. Zpětně kompatibilní rozšíření info requestu

### 4.1 Volitelná pole

Info request může obsahovat tato nová pole:

- `maskedCommunicationContracts`: pole 64znakových lowercase hex hashů, které volající publikuje.
- `includeMaskedCommunicationContracts`: boolean. Hodnota `true` žádá rozšířenou odpověď s uloženými maskovanými kontrakty klientů.

Obě pole jsou volitelná. Při jejich absenci musí zůstat současný wire formát, autentizace, odpověď a chování kompatibilní s existujícími klienty a serverem.

### 4.2 Identita a klientská autentizace publikace

- Request obsahující `maskedCommunicationContracts` musí navíc obsahovat `src` s vlastní VPN IP a klientský autentizační tag.
- Server musí ověřit, že `src` je platná nakonfigurovaná klientská adresa a má odpovídající položku v `ACCESS_KEYS`.
- Derivace autentizačního klíče musí být kompatibilní s `data` full-auth. Použije server secret, access key a `src`, stejně jako existující `fullAuthKey`.
- Tag používá HMAC SHA-256 a stejnou canonical Base64URL reprezentaci jako `data`.
- Kanonický podepisovaný obsah musí být doménově oddělený pro HTTP `POST` a akci `info`, musí zahrnout `src`, timestamp, request ID a hash všech rozšířených polí kromě samotného tagu.
- Stávající `data` kanonizace a všechny existující `data` tagy musí zůstat byte-for-byte beze změny.
- Request, který pouze nastaví `includeMaskedCommunicationContracts` a nic nepublikuje, může nadále používat současnou semi-auth autentizaci.
- Žádné publikované pole nesmí být měnitelné bez zneplatnění autentizačního tagu.

### 4.3 Validace

- Přijímat pouze pole canonical 64znakových lowercase hex SHA-256 hodnot.
- Odmítnout neplatné typy, neplatné délky, nehexadecimální hodnoty, necanonical casing a překročení bezpečných velikostních limitů.
- Limity requestu a počtu kontraktů musí být explicitní, testované a slučitelné s existujícími maximy protokolu.
- Duplicity je vhodné odmítnout nebo deterministicky odstranit. Realizace musí zvolit jednu konzistentní a testovanou variantu bez změny významu seznamu.

## 5. Uložení do `info.json`

- Každý klient nadále používá svůj existující soubor `info.json` v adresáři odvozeném z jeho IP.
- Při autentizovaném requestu s `maskedCommunicationContracts` server atomicky zapíše jednoduchý JSON objekt s polem `maskedCommunicationContracts` a současně aktualizuje mtime používaný jako `lastSeen`.
- Nový seznam vždy nahrazuje předchozí seznam.
- Explicitní prázdné pole `[]` odstraní dříve publikované kontrakty tím, že uloží prázdný seznam.
- Publikace se zapíše okamžitě a obchází `CLIENT_STATS_MIN_UPDATE_SECONDS`, protože nese potenciálně změněná data.
- Běžná aktivita bez pole `maskedCommunicationContracts` pouze provede dosavadní best-effort touch. Zachová dříve uložený JSON a nadále respektuje třicetisekundové omezení zápisů.
- Zápis musí být atomický a nesmí krátkodobě vystavit neúplný JSON.
- Chybný nebo nečitelný uložený JSON se nesmí vydávat jako platný maskovaný seznam.

## 6. Rozšířená info odpověď

- Bez `includeMaskedCommunicationContracts: true` vracet přesně současnou strukturu info plaintextu. Objekty v `clients` zůstávají kompatibilní s dosavadními přísnými parsery.
- S `includeMaskedCommunicationContracts: true` přidat každému klientovi, pro kterého existuje platný uložený seznam, pole `maskedCommunicationContracts`.
- Rozšířená odpověď nadále obsahuje `serverVersion`, `generatedAt`, `limits` a `clients` s `address` a `lastSeen`.
- Klienta, který server ještě nikdy nekontaktoval a nemá `lastSeen`, vůbec nevracet. Nepoužívat `lastSeen: null` a nezobrazovat řádky `Never`.
- Rozšířený request smí získat maskované kontrakty všech vrácených klientů. Tray aplikace následně odfiltruje vlastní IP.
- Odpověď musí nadále dodržet `MAX_INFO_PLAINTEXT_BYTES`. Ořezávání klientů musí být deterministické a nesmí vytvářet neplatnou částečnou položku.

## 7. Publikace při connectu a nový checkpoint

Connect sekvence bude mít toto pořadí:

1. `health` ověří dostupnost a syntaxi server URL.
2. Běžné `info` ověří server secret a načte standardní server information a limity.
3. Stávající prázdný autentizovaný `data` request ověří access key a datový endpoint.
4. Nové plně autentizované rozšířené `info` publikuje všechny aktuálně platné maskované komunikační kontrakty, současně nastaví `includeMaskedCommunicationContracts: true` a ověří korektnost rozšířené odpovědi.
5. Zbytek connect sekvence pokračuje beze změny.

- Do connect progress obrazovky přidat čtvrtý check `Communication contracts`.
- Úspěšné rozšířené info musí tento check zobrazit zeleně.
- Při chybě musí connect bezpečně selhat, zobrazit správný stav checkpointu a neoznačit pozdější kroky jako úspěšné.
- Rozšířený connect request musí ověřit celý response formát, nikoli pouze HTTP úspěch.
- Maskované kontrakty se automaticky poprvé publikují v závěrečném rozšířeném info checkpointu connect sekvence. Běžné periodické info requesty je samy od sebe znovu neposílají.
- Získaná rozšířená odpověď při connectu může být ověřena a použita pro checkpoint, ale nesmí způsobit automatické pravidelné načítání rozšířených dat.

## 8. Tray: `Server information` a `Who is online`

### 8.1 Načítání a refresh

- Už samotné otevření `Show server info` odešle rozšířený info request s `includeMaskedCommunicationContracts: true`.
- Tento request slouží k načtení dat pro obrazovku. Nemá sám publikovat změněné lokální kontrakty. Ty se publikují při connectu.
- Do levé strany patičky přidat tlačítko `Refresh`.
- `Refresh` bude vizuálně modré jako primární nebo informační akce, nikoli červené.
- Kliknutí znovu odešle stejný rozšířený info request a atomicky aktualizuje zobrazená data po úspěšném ověření odpovědi.
- Mimo `Show server info`, jeho `Refresh` a explicitní connect checkpoint Tray aplikace rozšířená data nevyžaduje.
- Karta `Raw server information` nadále zobrazí úplný aktuální dešifrovaný JSON rozšířené odpovědi.

### 8.2 Nadpis a počet

- Nadpis bude mít tvar `Server information (N valid online clients)`.
- `N` počítá pouze cizí klienty se stavem `Online - valid communication contract`.
- Vlastní IP se nezobrazuje v tabulce ani nezapočítává.

### 8.3 Tabulka

- Na kartě `Who is online` vytvořit read-only tabulku ve stejném designovém systému jako tabulka `Communication Contracts`.
- Sloupce v pořadí:
  1. `Contract`
  2. `Last seen`
  3. `Status`
- Jeden řádek odpovídá jednomu klientovi vrácenému serverem, kromě vlastní IP.
- `Contract` se sestaví z vlastní IP a IP protistrany ve formátu menší IP, podtržítko, větší IP.
- `Last seen` zobrazí lokálně čitelný datum a čas odvozený z `lastSeen`.
- Tabulka nezobrazuje klienty, kteří server nikdy nekontaktovali, ani text `Never`.
- Pod tabulkou bude stránkování se stejnou velikostí stránky, vzhledem, ovládáním klávesnice a dostupností jako tabulka `Communication Contracts`.

### 8.4 Stav a barvy

- Online stav vypočítat výhradně z časů stejné serverové odpovědi. Klient je online, když `generatedAt - lastSeen` je nejvýše 60 sekund.
- Nepoužívat pro toto rozhodnutí lokální hodiny počítače.
- `Offline`: více než 60 sekund, šedé pozadí. U offline klienta shodu maskovaných kontraktů nevyhodnocovat.
- `Online - valid communication contract`: nejvýše 60 sekund, lokální kontrakt pro protistranu existuje a serverem vrácený seznam protistrany obsahuje maskovaný kontrakt shodný s aktuálně lokálně vypočtenou hodnotou. Řádek má světle zelené pozadí.
- `Online - invalid communication contract`: nejvýše 60 sekund a podmínka platného kontraktu není splněna, včetně chybějícího lokálního kontraktu, chybějícího publikovaného seznamu nebo neshody. Řádek má světle žluté pozadí.
- Texty stavů použít přesně v uvedené angličtině.

### 8.5 Řazení

1. Zelené řádky `Online - valid communication contract`.
2. Žluté řádky `Online - invalid communication contract`.
3. Šedé řádky `Offline`.
4. Uvnitř každé skupiny řadit numericky podle IP adres tvořících kontrakt.

### 8.6 Změna konfigurace bez reconnectu

- Zobrazení a refresh musí porovnávat data serveru s aktuálně uloženou lokální konfigurací, nikoli jen s konfigurací zachycenou při posledním connectu.
- Pokud uživatel za běhu upraví lokální komunikační klíč a nová hodnota odpovídá publikované hodnotě protistrany, tabulka může ihned zobrazit zelený stav bez reconnectu.
- Aktivní VPN spojení a seznam publikovaný tomuto klientovi na serveru se aktualizují až po uživatelem provedeném reconnectu. Tray aplikace nesmí reconnect provést automaticky.

## 9. Zpětná kompatibilita

- Starý klient musí být schopen poslat původní info request novému serveru a získat původní odpověď.
- Nový klient musí nadále zvládat původní standardní info odpověď v běžných tocích.
- Nová pole se nesmí objevit ve standardní info odpovědi bez explicitního include příznaku.
- Existující `health`, `data`, šifrování info odpovědi, `data` full-auth tagy a jejich canonical bytes zůstanou kompatibilní.
- Starý server, který rozšíření nepodporuje, musí vést k jasně klasifikované chybě nového `Communication contracts` checkpointu, nikoli k přijetí neověřených dat.

## 10. Dokumentace a testy

- Aktualizovat normativní protokol a klientskou dokumentaci pro nový info request, autentizaci, response shape, úložiště a connect checkpoint.
- Aktualizovat komentáře v `phpvpn.php`, zejména konfigurační hlavičku, cestu datové složky a ochranu složky před HTTP přístupem.
- Přidat serverové unit a integrační testy pro status page, vypnutí stránky, bezpečný a výchozí secret, přejmenovanou složku, validaci rozšířeného info requestu, klientskou autentizaci, atomický zápis a zpětnou kompatibilitu.
- Přidat klientské protocol a runtime testy pro výpočet maskovaných kontraktů, full-auth info, parser standardní i rozšířené odpovědi, connect checkpoint a chybové stavy.
- Přidat Tray model, layout, unit a UI testy tabulky, barev, stavů, řazení, stránkování, počtu v nadpisu, tlačítka `Refresh`, klávesnice, DPI a accessibility.
- Bezpečnostní testy musí prokázat, že klient se znalostí pouze server secretu nemůže publikovat seznam za jinou IP, že změna rozšířených polí zneplatní tag a že server nevrací maskované kontrakty bez explicitního include příznaku.

## 11. Akceptační kritéria

- HTML z `assets/index.html` je zabudované v `phpvpn.php` a vizuálně odpovídá mockupu.
- Warning panel je přítomný pouze při nebezpečném výchozím secretu.
- `STATUS_PAGE_ENABLED` je zdokumentovaný, defaultně `true`, a při `false` kořen vrací `404 not_found` bez HTML.
- Výchozí datová cesta je `./phpvpn_data` a žádná automatická migrace neprobíhá.
- Maskovaný kontrakt má deterministický, mezi klienty shodný 64znakový lowercase hex SHA-256 výsledek a používá přesně zadaný server secret.
- Standardní info protokol zůstává zpětně kompatibilní.
- Publikaci nelze podvrhnout bez správného access key klienta.
- `info.json` bezpečně uchovává poslední publikovaný seznam a jeho mtime nadále poskytuje `lastSeen`.
- Connect obsahuje funkční zelený checkpoint `Communication contracts` po úspěšném běžném info a data ověření.
- `Show server info` a `Refresh` načtou rozšířená data pouze na vyžádání.
- `Who is online` správně zobrazuje tři sloupce, přesné tři stavy, barvy, řazení, stránkování a výpočet online podle 60 sekund serverového času.
- Nadpis správně ukazuje počet cizích validních online klientů.
- Změna uloženého lokálního kontraktu se projeví v tabulce bez automatického reconnectu.
- Všechny relevantní existující i nové testy projdou.

## Mimo rozsah

- Obecný debug mode.
- Automatická migrace staré datové složky.
- Automatický reconnect po změně konfigurace.
- Pravidelné stahování maskovaných kontraktů mimo connect checkpoint a obrazovku `Server information`.


