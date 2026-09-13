---
intent_version: 3
created_at: 2026-09-13-18-37-37
based_on: intent-v002.md
produces: intent-v003.md
---

# Konverzační podklad intentu

**User:** Ahoj, chci vytvořit nový intent.

**AI:** Nový intent je aktivní. Popiš mi, co má zachytit. Můžeš kdykoli říct „pozastavit“, „pokračovat“ nebo „zpracovat intent“.

**User:** V rámci `phpvpn.php` chci upravit úvodní obrazovku vracenou do webového prohlížeče při otevření stránky skriptu. Mockup je v `assets/index.html`. Mockup představuje variantu s výchozím nebezpečným server secretem. Má se převzít a zakomponovat do `phpvpn.php`. Pokud server secret není výchozí nebezpečná hodnota, odstraní se červený panel s varováním začínajícím „Warning: The default SERVER_SECRET value is…“.

**AI:** Zachytila jsem to. Pokračuj, až budeš připravený.

**User:** Chci také proměnnou, která při hodnotě `false` zabrání zobrazení hlavní stránky v prohlížeči. Debug mode zatím nechci řešit. Jaký název bys navrhla?

**AI:** Zachytila jsem to. Pokračuj prosím.

**User:** Teď nezachytávej a řekni mi návrh názvu.

**AI:** Zachytávání je pozastavené. Navrhla bych `STATUS_PAGE_ENABLED`. Je to přesné, protože existující kód stránku označuje jako status page. Při `false` se stránka nezobrazí, výchozí hodnota může být `true` a debug režim zůstane oddělený.

**User:** Souhlasím. Začni znovu zachytávat a zahrň toto chování do intentu.

**AI:** Zachytávání znovu pokračuje a tento požadavek jsem zahrnula.

**User:** Data paketů a informační soubory, nad kterými se provádí touch pro sledování aktivity IP adres, se dnes ukládají do složky `phpvpn` vedle `phpvpn.php`. Chci ji přejmenovat na `phpvpn_data`.

**AI:** Zaznamenala jsem to.

**User:** Chci volitelně rozšířit info REST API. Po úspěšném connectu klient v prvním info requestu odešle všechny aktuálně platné maskované komunikační kontrakty. Komunikační kontrakt je dvojice IP adres seřazená tak, že menší IP je vlevo, například `10.99.99.2_10.99.99.3`. Za kontrakt se připojí podtržítko, klíč, podtržítko a server secret a z celku se vypočítá SHA-256. Výsledek se nazývá maskovaný komunikační kontrakt. Rozšíření musí být zpětně kompatibilní a běžné info requesty budou většinou bez těchto dat. Server uloží maskované kontrakty do klientského `info.json`, který je dnes prázdný a slouží hlavně jako touch marker. Soubor bude obsahovat jednoduchý JSON s anglicky pojmenovaným polem maskovaných komunikačních kontraktů.

**AI:** Zaznamenala jsem to. Pokračuj, až budeš připravený.

**User:** Na stránce `Server information`, otevřené přes `Show server info`, chci v kartě `Who is online` read-only tabulku ve stejném designu jako `Communication Contracts`. Sloupce budou `Contract`, `Last seen` a `Status`. Konečné texty tří stavů jsou `Offline`, `Online - valid communication contract` a `Online - invalid communication contract`. `Last seen` zobrazí datum a čas posledního info o daném počítači, které je dnes dostupné v raw server information.

**AI:** Zaznamenala jsem tuto část včetně posledních oprav.

**User:** Do levé strany patičky stránky `Server information` přidej modré tlačítko `Refresh`. Kliknutí znovu vyžádá server information, tentokrát rozšířené. Rozšířená odpověď kromě současných `limits`, `serverVersion`, `generatedAt` a `clients` vrátí u klientů i maskované komunikační kontrakty. Ty se vrátí jen při explicitním požadavku. Tray aplikace o ně požádá jen na stránce `Server information`, nikde jinde.

**AI:** Zaznamenala jsem to a intent zatím nezpracovávám.

**User:** Info request má mít dva volitelné příznaky či rozšíření. Jedním klient publikuje své maskované komunikační kontrakty, druhým žádá server o maskované kontrakty všech klientů. Díky tomu Tray aplikace zobrazí tabulku se sloupci `Contract`, `Last seen` a `Status` a se stránkováním. Online je klient, který komunikoval během poslední minuty. Řádek má světle zelené pozadí, pokud je klient online, existuje s ním lokální komunikační kontrakt a jeho publikovaný maskovaný kontrakt se shoduje s lokálně vypočteným maskovaným kontraktem.

**AI:** Zaznamenala jsem to včetně poslední úpravy.

**User:** Klient, který během poslední minuty komunikoval se serverem, ale nemám s ním komunikační kontrakt nebo se jeho maskovaný kontrakt neshoduje, bude světle žlutý. Ostatní jsou offline a budou šedí. U offline klienta už shodu kontraktu nevyhodnocujeme.

**AI:** Zaznamenala jsem to.

**User:** Zpracuj intent.

**AI:** Začala jsem ověřovat současné chování serveru, klienta a mockupu. Zjistila jsem, že současný server má jednoduchý `StatusPageHandler`, `BUFFER_DIRECTORY` je `./phpvpn`, `info.json` je prázdný touch marker, info request používá semi-auth a odpověď obsahuje klienty s `address` a `lastSeen`. Stránka `Who is online` je zatím prázdná karta vedle raw JSONu.

**AI:** Navrhla jsem kanonizaci maskovaného kontraktu s normalizovaným server secretem a 64znakovým lowercase hex výstupem.

**User:** Nechci normalizovaný server secret. Do hashe se má vzít přesně tak, jak je zadaný. Velká a malá písmena jsou rozdílná. Výstup 64 lowercase hex znaků je v pořádku.

**AI:** Potvrdila jsem použití přesně zadané hodnoty `SERVER_SECRET` bez normalizace.

**AI:** Když je `STATUS_PAGE_ENABLED` vypnuté, navrhla jsem pro kořenovou adresu HTTP `404` se standardní odpovědí `not_found`, bez HTML. Ostatní API akce zůstávají dostupné.

**User:** Ano. Stránka musí být po vypnutí neviditelná a `STATUS_PAGE_ENABLED` bude ve výchozím stavu `true`. Nezapomeň nastavení zdokumentovat přímo v `phpvpn.php`.

**AI:** Potvrdila jsem toto chování a zeptala se na migraci datové složky.

**User:** Automatická migrace nebude. Udělá ji správce. Produkt zatím není nasazený, takže lze přejmenovávat.

**AI:** Potvrdila jsem změnu bez migrace a dokumentaci v konfigurační části.

**AI:** Navrhla jsem, aby tabulka spojovala lokální kontrakty s cizími klienty vrácenými serverem a vynechala vlastní IP.

**User:** Ujasněme si to. Chci vidět klienty s jinou IP, se kterými mám kontrakt a jsou online, klienty bez kontraktu, kteří jsou online, i offline klienty bez ohledu na existenci kontraktu.

**AI:** Upřesnila jsem, že vlastní IP se jen odfiltruje. Navrhla jsem pro klienta, který server ještě nikdy nekontaktoval, `lastSeen: null`, text `Never` a offline stav.

**User:** Takového klienta bych ze serveru vůbec nevracel a řádky `Never` bych vypustil.

**AI:** Potvrdila jsem, že klient bez jediného kontaktu nebude zahrnut a zachová se současné chování seznamu `clients`.

**AI:** Navrhla jsem volitelná pole `maskedCommunicationContracts` pro publikaci a `includeMaskedCommunicationContracts` pro vyžádání. Pokud jsou přítomná, budou součástí autentizovaného obsahu requestu. Jejich absence zachová současné chování.

**User:** Ano, s tím souhlasím.

**AI:** Navrhla jsem, aby request s `maskedCommunicationContracts` atomicky přepsal uložené pole a aktualizoval `lastSeen`, prázdné pole odstranilo předchozí seznam, běžný request bez pole zachoval seznam a jen aktualizoval `lastSeen`. Explicitní publikace obejde třicetisekundový limit pro touch.

**User:** Vysvětli třicetisekundové omezení.

**AI:** Vysvětlila jsem, že současný server nepřepisuje mtime při každém requestu. Pokud byl soubor aktualizovaný před 30 sekundami nebo méně, běžný touch se přeskočí. Publikace nových dat má tento limit obejít.

**User:** Souhlasím. Do nadpisu `Server information` přidej do závorky počet aktuálních validních online klientů, například `Server information (5 valid online clients)`.

**AI:** Navrhla jsem počítat jen cizí řádky se stavem `Online - valid communication contract`, bez vlastní IP.

**User:** Souhlasím.

**AI:** Navrhla jsem, aby otevření `Show server info` poslalo rozšířený info request a modré tlačítko `Refresh` stejný request zopakovalo. Jinde se rozšířená data nevyžádají.

**User:** Souhlasím. Pokud za běhu změním komunikační kontrakt na hodnotu, která odpovídá sousednímu klientovi, a bez reconnectu otevřu server info, očekávám zelený stav. Reconnect musí provést uživatel.

**AI:** Potvrdila jsem, že tabulka porovnává odpověď s aktuálně uloženými lokálními kontrakty, takže nová shoda může být zelená bez reconnectu. Reconnect je potřeba pro převzetí konfigurace aktivním spojením a publikaci nového seznamu serveru.

**AI:** Navrhla jsem online hranici nejvýše 60 sekund mezi `generatedAt` a `lastSeen` ze stejné serverové odpovědi.

**User:** Ano.

**AI:** Navrhla jsem řazení zelení, žlutí, šedí, uvnitř skupin numericky podle IP, a stránkování stejné jako u `Communication Contracts`.

**User:** Ano.

**AI:** Při kontrole protokolu jsem upozornila, že dnešní info request neidentifikuje klienta a používá jen společný server secret. Navrhla jsem, aby publikace obsahovala `src` a klientský autentizační tag založený na access key, server secretu a rozšířeném obsahu. Starý info request a include-only request zůstanou semi-auth.

**User:** Má konstrukce klientského tagu odpovídat a být kompatibilní s tagem pro REST API `data`?

**AI:** Ano. Navrhla jsem stejnou derivaci ze server secretu, access key a zdrojové IP, stejný HMAC SHA-256 a Base64URL. Liší se doménově oddělený kanonický obsah pro akci `info`. Stávající `data` tagy zůstávají beze změny. Autentizace zachová současnou normalizaci pouze prázdného server secretu, zatímco maskovaný kontrakt používá přesně zadaný server secret bez normalizace.

**User:** Souhlasím. Přidej do connect obrazovky nový check. Současné pořadí chápu jako `health` pro server URL, `info` pro server secret a `data` pro access key. Před `data` chci nové rozšířené `info`, které publikuje maskované kontrakty, vyžádá kompletní maskovaná data ostatních a ověří korektní odpověď. Úspěch bude nový zelený check.

**AI:** Potvrdila jsem současnou sekvenci a navrhla nový check `Communication contracts`.

**User:** Tak ho tam dej.

**User:** Nový checkpoint zároveň splní první přihlášení při connectu a předání komunikačních kontraktů.

**AI:** Doporučila jsem konečné pořadí `health`, běžné `info`, prázdný `data` request pro access key a až potom rozšířené `info`, aby se chybný access key neprojevil jako chyba komunikačních kontraktů. Rozšířené `info` publikuje vlastní maskované kontrakty, vyžádá ostatní a ověří odpověď.

**User:** Souhlasím.

**User:** Když žádám o komunikační kontrakty ostatních přes include příznak, server mi má z každé jiné IP vrátit jen kontrakt, který se týká mojí IP. Nesmím získat informace o kontraktech, které mají jiné IP adresy mezi sebou. Je to už v intentu?

**AI:** Ne. Verze 2 naopak dovoluje vrátit maskované kontrakty všech klientů a include-only request může být semi-auth bez identity volajícího. Vysvětlila jsem, že server nedokáže filtrovat samotný neprůhledný seznam hashů podle protistrany. Navrhla jsem proto svázat publikovaný hash s IP protistrany a vyžadovat u include requestu identitu `src` a full-auth kompatibilní s `data`. Server pak každému žadateli vrátí pouze záznamy, jejichž protistrana je právě tento žadatel.

**User:** Protože ještě nevznikl plán ani implementace, vytvoř souhrnný čistý intent, který agreguje verze 1 a 2 i právě doplněné soukromé filtrování, aby se mohl předat k plánování.

**AI:** Potvrdila jsem vytvoření čisté kumulativní verze při zachování starších neměnných verzí jako historie.

