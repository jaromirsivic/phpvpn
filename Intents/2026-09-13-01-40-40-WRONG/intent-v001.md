---
intent_version: 1
created_at: 2026-09-13-01-54-56
source: intent-chat-v001.md
---

# Diagnostika HTTP requestu a response v konfiguračním wizardu

## Shrnutí

Rozšířit první dva kroky konfiguračního wizardu klienta o diagnostiku konkrétní HTTP výměny, jejíž přijatá odpověď způsobila chybu. Při takovém selhání se v patičce wizardu objeví červené tlačítko `Error detail`. Tlačítko otevře samostatné okno s taby `Request` a `Response`, které zobrazí úplnou aplikační podobu požadavku a odpovědi před protokolovým parsováním.

Cílem je umožnit rychlé odhalení odpovědí pozměněných webhostingem, například přidaným HTML nebo JavaScriptem, bez nutnosti externího síťového ladění.

## Kontext projektu

- Realizace proběhne přímo v aktuální feature větvi `tray_UX`.
- Pro tuto realizaci se nesmí vytvářet ani používat worktree.
- Síťovou komunikaci wizardu vlastní služba. Tray aplikace ovládá wizard přes lokální Wizard IPC, takže změna pravděpodobně zasáhne transportní diagnostiku, službu, Wizard IPC, model a Win32 UI tray aplikace.
- První krok `Server URL` provádí URL probe přes `health`.
- Druhý krok `Server secret` provádí ověřený `health` a následně `info`.
- Existující Wizard operation předává do UI bezpečnou chybu a HTTP status, nikoli úplný request a response.
- Referenční vzhled tabů je současné okno `Show Server Info`, realizované přes tab control a sdílené UI barvy.
- Relevantní dokumentace je v kořenových souborech `client.md` a `protocol.md`.

## Požadované chování

### 1. Rozsah

Funkce je dostupná pouze při selhání ověření v těchto krocích:

1. `Server URL`
2. `Server secret`

Nevztahuje se na další kroky wizardu, běžné VPN požadavky, `Show Server Info` ani jiné diagnostické akce.

### 2. Podmínka zobrazení tlačítka

Tlačítko `Error detail` se zobrazí jen tehdy, když:

- služba skutečně provedla HTTP výměnu,
- pro konkrétní chybující HTTP volání přijala odpověď,
- zpracování této přijaté odpovědi skončilo chybou, například kvůli chybnému statusu, neplatnému formátu, neočekávanému HTML či JavaScriptu, chybné korelaci nebo kryptografickému ověření.

Tlačítko se nezobrazí při:

- lokální validační chybě před odesláním,
- selhání DNS, TCP nebo TLS před přijetím HTTP odpovědi,
- timeoutu nebo přerušení bez přijaté odpovědi,
- chybě IPC nebo nedostupnosti služby,
- zrušení operace bez přijaté odpovědi.

Diagnostika musí patřit k aktuálnímu neúspěšnému pokusu. Při zahájení nového ověření, změně kroku, resetu nebo ukončení relace se starý detail odstraní a nesmí se vydávat za výsledek nového pokusu.

### 3. Volba konkrétní výměny

Pokud krok provádí více HTTP volání, uloží a zobrazí se pouze výměna, jejíž přijatá odpověď způsobila výslednou chybu.

- Pokud v kroku `Server secret` selže zpracování odpovědi `health`, zobrazí se `health` request a response.
- Pokud `health` uspěje a selže zpracování přijaté odpovědi `info`, zobrazí se `info` request a response.
- Úspěšná předchozí výměna se nesmí zobrazit jako detail pozdější chyby bez odpovědi.

### 4. Tlačítko v patičce wizardu

- Text tlačítka je přesně `Error detail`.
- Tlačítko používá červený chybový vizuální styl konzistentní se současným design systémem tray aplikace.
- Je viditelné pouze při splnění podmínky výše.
- V prvním kroku je bezprostředně vlevo od `Verify and continue` v pravé části patičky.
- Ve druhém kroku je bezprostředně vpravo od `Back`.
- Mezi sousedními tlačítky zůstane standardní drobná mezera. Tlačítka se nesmí vizuálně dotýkat.
- Layout musí zachovat stávající responzivní chování a tab order wizardu při běžných i kompaktních rozměrech.

### 5. Okno `Error detail`

Kliknutí otevře samostatné okno vlastněné wizardem.

- Nahoře jsou dva taby s přesnými názvy `Request` a `Response`.
- Vzhled tabů a jejich barvy odpovídají tabům v `Show Server Info`.
- Každý tab obsahuje víceřádkový read-only textbox.
- Aktivní textbox využije prakticky celý dostupný obsah okna.
- Okno nemá patičku ani tlačítka `OK`, `Cancel` nebo obdobné akce.
- Uživatel okno zavře standardním křížkem v titulku.
- Přepínání tabů nesmí měnit zachycená data.
- Obsah musí být čitelný, vybíratelný a kopírovatelný. Read-only režim nesmí bránit výběru textu ani standardnímu kopírování.

### 6. Obsah tabu `Request`

Zobrazí věrnou aplikační rekonstrukci konkrétního požadavku předaného transportu:

- HTTP metoda,
- cílová URL včetně akce,
- všechny hlavičky, které aplikace předala systému,
- dostupné hlavičky doplněné nebo potvrzené HTTP vrstvou systému, pokud je lze spolehlivě získat,
- prázdný řádek oddělující hlavičky a tělo,
- nezměněné tělo požadavku.

Obsah se nesmí přeparsovat, zkrášlovat, normalizovat ani nahrazovat již dešifrovanou či jinak odvozenou reprezentací. Nejde o zachycení TLS ciphertextu, paketů nebo interních HTTP/2 rámců.

### 7. Obsah tabu `Response`

Zobrazí věrnou aplikační podobu odpovědi vrácené HTTP vrstvou před protokolovým parsováním:

- HTTP verzi a status line, pokud je transport poskytuje, jinak jednoznačně rekonstruovaný status,
- všechny dostupné přijaté response headers,
- prázdný řádek oddělující hlavičky a tělo,
- nezměněné přijaté tělo odpovědi.

Právě tato vrstva musí zachytit a ukázat i HTML, JavaScript nebo jiný text přidaný hostingem. Obsah se nesmí nahradit pouze bezpečnou chybovou zprávou nebo parsovaným JSON objektem.

### 8. Přenos mezi službou a tray aplikací

- Služba zachytí diagnostiku pouze pro aktuální Wizard probe v prvních dvou krocích.
- Diagnostika se připojí pouze k terminálnímu neúspěšnému výsledku, který splňuje podmínku přijaté odpovědi.
- Wizard IPC přenese request a response jako volitelná data svázaná s identitou operace a pokusu.
- Tray model musí zachovat existující ochrany proti zastaralým odpovědím, přerušeným relacím a nesprávné identitě pokusu.
- Diagnostická data se nesmí zapisovat do logu, konfiguračního souboru ani jiného perzistentního úložiště.
- Data mají životnost pouze po dobu relevantního neúspěšného pokusu a otevřeného wizardu.
- Existující limit IPC zprávy musí být respektován. Návrh musí před implementací ověřit, že maximální transportní odpověď a její JSON escaping nemohou překročit IPC limit. Pokud je nutná změna limitu nebo samostatné načtení detailu, musí být navržena explicitně a pokryta testy, bez tichého ořezu diagnostiky.

## Bezpečnostní a technické hranice

- Požadavek je zobrazit kompletní aplikační request a response. Uživatelské rozhodnutí výslovně nepředpokládá redakci těchto dat v tomto diagnostickém okně.
- Diagnostika je dostupná jen po explicitním kliknutí v lokálním tray UI.
- Obsah requestu a response se nesmí automaticky logovat, telemetricky odesílat ani ukládat.
- Zachycení nesmí měnit odesílané bajty, pořadí síťových operací, autentizaci, timeouty, retry pravidla ani kryptografické ověřování.
- Chybná nebo nedůvěryhodná response je zobrazovaný text, nikoli instrukce k provedení. UI ji nesmí interpretovat jako HTML ani spouštět JavaScript.
- Textbox musí zobrazovat data jako prostý text.

## Dokumentace

Aktualizovat nejméně:

- `client.md`, zejména popis Wizard probe, Wizard IPC, životnosti diagnostických dat, UI chování a limitů,
- `protocol.md`, zejména vztah diagnostiky k HTTP requestu a response před protokolovým parsováním a skutečnost, že se nemění wire protokol mezi klientem a `phpvpn.php`.

Dokumentace musí jasně rozlišit:

- serverový HTTP protokol, který se tímto záměrem nemění,
- lokální Wizard IPC mezi službou a tray aplikací, který se rozšiřuje,
- aplikační rekonstrukci HTTP zprávy oproti paketovému nebo TLS záznamu.

## Testy

### Automatické testy

Přidat nebo rozšířit testy alespoň pro:

- zachycení requestu a response před parsováním,
- přenos volitelné diagnostiky přes Wizard IPC a jeho striktní schema,
- vazbu diagnostiky na správnou operaci, attempt ID a sequence,
- krok `Server URL` s přijatou syntakticky nebo kryptograficky chybnou odpovědí,
- krok `Server secret`, kde selže `health`,
- krok `Server secret`, kde `health` uspěje a selže `info`,
- chybný HTTP status s přijatou odpovědí,
- nezobrazení tlačítka při lokální validaci, DNS/TLS chybě, timeoutu bez response, IPC chybě a zrušení,
- odstranění starého detailu při novém pokusu a odmítnutí zastaralého výsledku,
- správnou viditelnost, text, červený styl, pořadí a mezery tlačítek v obou krocích,
- responzivní layout a tab order,
- dialog se dvěma taby, read-only editory, přepínáním obsahu a zavřením křížkem,
- zobrazení vloženého HTML a JavaScriptu jako inertního prostého textu,
- hraniční velikosti diagnostiky a IPC zprávy bez tichého ořezu.

Využít odpovídající existující vrstvy testů, zejména unit testy transportu, ProbeClientu, WizardService, Wizard IPC, WizardModelu a layoutu, plus tray UI testy.

### Lokální integrační ověření

Po automatických testech provést lokální ověření přes HTTP s Nginx instalací v `D:\nginx` a testovacím `phpvpn.php`, pokud je v uvedeném prostředí skutečně dostupný. Testovací server dočasně upravit nebo nakonfigurovat tak, aby do response přidal text, HTML nebo JavaScript a vyvolal chybu klientského zpracování.

Ověřit, že:

- wizard zobrazí běžnou chybu i tlačítko `Error detail`,
- `Request` obsahuje správný konkrétní požadavek,
- `Response` obsahuje přesně diagnosticky relevantní pozměněnou odpověď,
- vložený obsah je pouze text a nic se nespustí,
- nové ověření starý detail odstraní.

Dočasné změny mimo repozitář po testu bezpečně vrátit do původního stavu. Před úpravou ověřit přesný cíl a zachovat původní obsah tak, aby byl obnovitelný.

### Externí best-effort ověření

Až po úspěšných lokálních testech provést best-effort ověření proti:

[https://phpvpn.freehosting.dev/phpvpn.php](https://phpvpn.freehosting.dev/phpvpn.php)

Toto ověření není povinnou podmínkou dokončení, protože cizí hosting může být nedostupný, ukončovat TLS spojení nebo změnit své chování. Výsledek zaznamenat jako `PASS`, `FAIL` nebo `NOT RUN` s konkrétním důvodem. Z tohoto prostředí server během přípravy intentu ukončil TLS spojení před HTTP odpovědí, takže vložený obsah nebyl potvrzen.

## Akceptační kritéria

1. Při chybě zpracování skutečně přijaté HTTP odpovědi v kroku `Server URL` nebo `Server secret` je viditelné červené tlačítko `Error detail` na potvrzeném místě s běžnou mezerou.
2. Bez přijaté odpovědi se tlačítko nezobrazí.
3. Dialog obsahuje pouze taby `Request` a `Response` s read-only víceřádkovými textovými poli přes dostupný obsah a zavírá se křížkem.
4. Request a response odpovídají konkrétnímu chybujícímu HTTP volání a zachovávají aplikační obsah před parsováním.
5. U vícekrokového ověření se nezobrazí předchozí úspěšná výměna jako detail jiné chyby.
6. Nový pokus, změna kroku, reset nebo ukončení relace odstraní zastaralý detail.
7. Diagnostická data nejsou interpretována, logována ani perzistentně ukládána.
8. Stávající síťový protokol, kryptografie, timeouty, retry pravidla a bezpečnost životního cyklu operací zůstanou funkčně beze změny.
9. Automatické testy a lokální integrační test s pozměněnou odpovědí projdou.
10. `client.md` a `protocol.md` odpovídají implementovanému chování.
11. Realizace proběhne přímo na větvi `tray_UX` bez worktree.
12. Externí free-hosting test proběhne až jako poslední best-effort kontrola a jeho nedostupnost sama neblokuje dokončení.

## Realizační vodítka

- Zachycení řešit v transportní nebo ProbeClient vrstvě tak, aby byl k dispozici request i raw aplikační response ještě před tím, než parser vrátí chybu.
- Rozšířit výsledek Wizard operation a jeho IPC serializaci pouze o volitelný diagnostický objekt. Nepřidávat diagnostiku do běžné bezpečné chyby používané jinde.
- Zachovat striktní IPC validaci a existující korelaci `operationId`, `clientAttemptId`, `attemptSequence` a `connectionEpoch`.
- V tray UI znovu použít existující tab control, barvy, moderní textbox a layout helpery namísto paralelního vizuálního systému.
- Před realizací rozhodnout transportní reprezentaci response headers a velikostní strategii tak, aby splnila úplnost bez tichého ořezu a současně neporušila IPC limit.
- Neprovádět změnu serverového `phpvpn.php` jako součást produktové funkce. Jeho lokální úprava je pouze dočasný testovací prostředek.
