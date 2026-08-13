# Revize klientské specifikace phpvpn: obecná a Windows část

Tento dokument soustřeďuje zjištění z bezpečnostní a výkonnostní revize `client.md`. Posuzováno bylo platformně nezávislé jádro a Windows implementace; části pro macOS a Linux nejsou předmětem této revize.

## Závěr

Návrh má správný základ: oddělené jádro, omezené fronty, jeden vlastník TUN vstupu a výstupu, šifrování payloadu mezi klienty a záměrně stručné logování. Před produkční implementací ale potřebuje uzavřít několik zásadních bezpečnostních a provozních rozhodnutí.

Nejvyšší rizika jsou neověřované TLS jako výchozí stav, povolené HTTP, neurčité IPC mezi tray aplikací a Windows Service, příliš volně definovaná ochrana klíčů a chybějící kontrola vnitřních IPv4 paketů proti metadatům šifrovaného záznamu.

## Přehled zjištění

| Číslo | Oblast | Priorita | Stručné řešení |
|---:|---|---|---|
| 1 | TLS | Kritická | Ověřovat certifikát ve výchozím stavu. |
| 2 | HTTP replay | Kritická | HTTP zakázat v produkčním profilu. |
| 3 | Tray–service IPC | Kritická | Named pipe s ACL, autentizací a pevnou smlouvou. |
| 4 | Vnitřní IPv4 | Kritická | Vázat vnitřní adresy paketu na `src` a `dst` záznamu. |
| 5 | Klíče v konfiguraci | Kritická | Uzamknout ACL, atomické změny a použít DPAPI. |
| 6 | TUN ovladač | Vysoká | Vybrat konkrétní podepsaný L3 ovladač a popsat upgrade. |
| 7 | Služba a instalace | Vysoká | Podepisování, restriktivní ACL a minimální oprávnění. |
| 8 | Životní cyklus klíčů | Vysoká | Verze, rotace, revokace a recovery postup. |
| 9 | Editace konfigurace | Vysoká | Validovat a atomicky aktivovat novou konfiguraci. |
| 10 | Jeden request | Střední | Změřit, použít keep-alive a pravidla pro případnou souběžnost. |
| 11 | Polling | Střední | Adaptivní interval, jitter, backoff a limity. |
| 12 | Blokující paket | Střední | MTU a definitivní zahození neodeslatelných paketů. |
| 13 | Parsery | Střední | Pevné limity před alokací a průběžné parsování. |
| 14 | Fronty | Střední | Per-peer kvóty a férovost při přijímání. |
| 15 | Čas | Střední | Monotónní hodiny, RTT korekce a pravidelná obnova. |
| 16 | Routy a DNS | Střední | Transakční síťový plán s přesným rollbackem. |
| 17 | Session 0 | Střední | Službu plně oddělit od UI a definovat více relací. |
| 18 | `info.json` | Střední | Neukládat implicitně, případně restriktivní ACL a expirace. |
| 19 | Diagnostika | Střední | Zeroizace, bezpečná politika dumpů a hardening sestavení. |
| 20 | URL a proxy | Střední | Striktní URL, bez redirectů a implicitních proxy. |

## Kritické nálezy

### 1. Neověřované TLS je výchozí nastavení

`trustAnyTlsCertificate` má výchozí hodnotu `true`. To ruší běžnou ochranu HTTPS proti aktivnímu útočníkovi a zvyšuje pravděpodobnost, že útočník bude moci odposlouchávat a opakovat platné pollingové požadavky. V kombinaci s vědomě přijatým replay chováním relay pak může vyprázdnit příchozí frontu legitimního klienta.

**Požadovaná změna:** Výchozí hodnota musí být `false`. Přijetí neověřeného certifikátu má být dostupné pouze jako explicitní vývojový nebo nouzový režim, s trvalým výrazným varováním v tray, konzoli i logu. Produkční profil má tento režim odmítat.

### 2. HTTP je nebezpečné jako běžně podporovaný provozní režim

Specifikace správně popisuje, že útočník může zachycený `data` polling v platném časovém okně zopakovat a odebrat ciphertext z fronty oběti. Nejde jen o teoretickou slabinu: praktickým výsledkem je dostupnostní útok, který může dlouhodobě bránit doručení dat.

**Požadovaná změna:** Produkční klient má vyžadovat HTTPS s ověřením certifikátu. HTTP ponechat jen pro laboratorní či testovací profil, který vyžaduje samostatné explicitní povolení.

### 3. Lokální IPC mezi tray aplikací a službou není definováno

Formulace „lokální zabezpečené IPC“ není implementační bezpečnostní hranice. Chybná implementace named pipe může umožnit jinému procesu ovládat službu, vyvolat síťové operace nebo získat citlivé diagnostické údaje.

**Požadovaná změna:** Normativně popsat IPC protokol a jeho ochranu:

- named pipe s explicitním ACL pro příslušného uživatele a Service SID;
- ověření identity připojeného klienta a zákaz nečekaných impersonačních scénářů;
- pevně vyjmenované příkazy, jejich velikostní limity a validaci všech polí;
- ochranu proti pipe squattingu a konkurenci více tray procesů;
- žádné klíče, ciphertexty ani kompletní konfigurace v IPC odpovědích;
- audit odmítnutých požadavků bez citlivých hodnot.

### 4. Chybí výslovná validace vnitřních IPv4 paketů vůči metadatům

Příjem dat kontroluje `dst` datového řádku a volí payload klíč, ale specifikace nevyžaduje ověřit, že dešifrovaný IPv4 paket má očekávaný zdroj a cíl. Bez této vazby může klient s platným payload klíčem vložit do tunelu paket s podvrženou vnitřní adresou.

**Požadovaná změna:** Před zápisem do tunelu musí klient striktně ověřit IPv4 hlavičku, celkovou délku, IHL a hranice paketu, dále vnitřní zdroj shodný s `src` záznamu a vnitřní cíl shodný s lokální VPN adresou. Odchozí paket musí analogicky mít lokální zdroj a cíl odpovídající kontraktu, podle kterého byl zařazen.

### 5. Ochrana klíčů ve Windows není dostatečně určena

Konfigurace nese server secret, access key a payload klíče v otevřené podobě. Pouhé doporučení omezených přístupových práv nestačí pro přesnou implementaci ani audit instalace.

**Požadovaná změna:** Definovat vlastnictví a ACL souboru i adresáře, bezpečné vytvoření bez dědění volných práv, zákaz síťových cest, atomickou změnu konfigurace a chování záloh. Zvážit ochranu citlivých hodnot pomocí DPAPI v kontextu služby. Tray aplikace klíče nikdy nečte ani nedrží.

## Vysoké priority

### 6. Není určen konkrétní Windows TUN ovladač a jeho životní cyklus

„TUN/TAP kompatibilní ovladač“ je příliš neurčité. TUN a TAP mají odlišnou útočnou plochu, API i výkonnostní charakteristiky. Nejasná je rovněž instalace, upgrade, podepisování a rollback ovladače.

**Požadovaná změna:** Zvolit a normativně popsat konkrétní udržovaný L3 TUN ovladač, podporované verze, podpis, ověřování integrity, instalaci a upgrade. Pro návrh, který pracuje výhradně s IPv4 pakety, má být preferován L3 TUN před L2 TAP.

### 7. Windows Service a instalace potřebují hardening

Bez přesných požadavků lze omylem vytvořit přepisovatelnou service binary path, nezabezpečený instalační adresář nebo zneužitelný DLL search path.

**Požadovaná změna:** Podepisovat binárky i ovladač, uzamknout ACL instalačního adresáře, používat bezpečný DLL search path, spouštět službu s minimem oprávnění a vyhodnotit Service SID či restricted service token. Instalátor musí validovat cílové cesty a nesmí spouštět neověřené pomocné soubory.

### 8. Chybí rotace a revokace klíčů

Payload klíče jsou statické. Po kompromitaci není popsán řízený přechod na nové klíče ani možnost odebrat konkrétního klienta.

**Požadovaná změna:** Navrhnout verzování klíčů, krátké překryvné období pro rotaci, revokaci access key a jednoznačný postup obnovy po kompromitaci. Změna vyžaduje společnou úpravu protokolu i klienta.

### 9. Úprava konfigurace v Notepadu vytváří problém konzistence a důvěry

Přímá editace může skončit částečně zapsaným, špatně zakódovaným nebo podvrženým souborem. Nelze spoléhat jen na to, že se konfigurace načítá až při dalším připojení.

**Požadovaná změna:** Použít validaci nové konfigurace, bezpečné atomické nahrazení souboru a kontrolu ACL před přijetím. Služba musí odmítnout konfiguraci změněnou mimo očekávaného vlastníka nebo s nevyhovujícími právy.

## Výkon, odolnost a správa zdrojů

### 10. Jeden současný `data` request může omezovat propustnost

Pravidlo maximalně jednoho `data` requestu zjednodušuje stav, ale při vyšší RTT omezuje propustnost velikostí jediné dávky a latencí relay. Výkon může být výrazně nižší než kapacita linky.

**Požadovaná změna:** Nejdříve změřit model na reálné RTT a pod zátěží. Zachovat sériové vlastnictví front, přidat persistentní spojení a reuse spojení. Případné souběžné requesty povolit až po definici pořadí, recovery a limitů in-flight dat.

### 11. Polling po 20 ms může přetížit klienta i relay

Minimální interval znamená až desítky požadavků za sekundu na klienta, i když nejsou data. U většího počtu klientů roste zatížení PHP workerů, TLS, storage i logování.

**Požadovaná změna:** Používat jitter, adaptivní interval podle zatížení a odezvy, omezený backoff a metriky. Serverové limity a plánovač klienta musí společně zabránit synchronizovaným pollingovým špičkám.

### 12. Neodeslatelný paket může blokovat FIFO frontu

Pokud jeden paket nebo element přesáhne maximální velikost požadavku, může zůstat navždy na čele fronty daného cíle a blokovat další pakety.

**Požadovaná změna:** Stanovit maximální povolenou velikost IPv4 paketu, MTU a maximální velikost elementu. Paket, který nelze odeslat ani samostatně, trvale zahodit a pouze agregovaně evidovat.

### 13. Parsery potřebují přesné limity před alokací

Obecná zmínka o kontrole velikosti odpovědi nestačí. NDJSON, JSON, base64url a dávky mohou jinak vyvolat velké alokace nebo nadměrné kryptografické výpočty.

**Požadovaná změna:** Stanovit maximální velikost HTTP odpovědi, řádku NDJSON, počtu řádků, počtu paketů v dávce a velikosti paketu. Vše validovat před alokací a přidat timeouty i omezení CPU práce na request.

### 14. Celkové limity front nebrání monopolizaci jednoho cíle

Celkový limit chrání paměť, ale jeden vzdálený cíl může zaplnit celou odchozí frontu a vytlačit ostatní.

**Požadovaná změna:** Doplnit per-peer limity, pravidlo zahazování a metriky po cílech. Plánování již řeší férovost při odesílání, ale musí ji doplnit i férovost při přijímání do fronty.

### 15. Stav času potřebuje přesnější pravidla

Jednosekundová rezerva není definitivně stanovena a `timeDelta` není popsán pro vysokou latenci, změnu lokálního času ani dlouhý běh.

**Požadovaná změna:** Používat monotónní hodiny, měřit RTT a odhadovat čas podle středu přenosu, limitovat přijatý posun a periodicky ověřovat čas znovu. Definovat chování při neúspěšné obnově `health`.

## Windows provozní a bezpečnostní nedostatky

### 16. Model rout, DNS a restartu není vymezen

Specifikace říká, že se má síťová konfigurace obnovit, ale nedefinuje split tunnel versus full tunnel, DNS, kolize rout, více aktivních adaptérů ani důsledky pádu služby. To může vést k úniku provozu, nefunkční síti nebo neúplnému úklidu.

**Požadovaná změna:** Přesně popsat vlastněné routy, DNS změny, prioritu rozhraní, kill-switch politiku, záznam původního stavu a obnovu po pádu i restartu systému. Odstraňovat pouze změny vytvořené klientem.

### 17. Služba nesmí komunikovat s desktopem

Windows Service běží mimo uživatelskou relaci. Návrh musí počítat s více relacemi, odhlášením uživatele a tím, že služba nesmí zobrazovat UI ani spoléhat na tray aplikaci.

**Požadovaná změna:** Služba funguje samostatně. Tray je nepovinný klient IPC spuštěný v konkrétní uživatelské relaci. Specifikovat více uživatelů, restart tray aplikace, autostart a oprávnění operací Connect, Disconnect, Edit Config a Show Info.

### 18. Ukládání `info.json` může vytvářet další únik citlivých dat

I když `info` nemá obsahovat tajemství, může obsahovat provozní metadata. Soubor vedle konfigurace může zdědit nevhodná práva a dostat se do záloh.

**Požadovaná změna:** Ve výchozím stavu info neukládat; pokud uživatel uložením výslovně požádá, vytvořit soubor atomicky s restriktivním ACL a volitelnou dobou platnosti.

### 19. Chybí politika crash dumpů a diagnostiky

Klíče se v průběhu provozu nacházejí v paměti. Minidumpy, Windows Error Reporting, debugger nebo příliš podrobné logy je mohou zpřístupnit.

**Požadovaná změna:** Bezpečně mazat citlivé buffery, v produkci zakázat nebo chránit pády generující dumpy, nikdy nedávat citlivé hodnoty do výjimek a ověřit logovací knihovny. Sestavení má využít běžný Windows compiler hardening.

### 20. URL a transportní politika jsou příliš volné

Neurčené chování redirectů, automatických proxy z prostředí, URL s user-info a neočekávaných query parametrů může změnit cílový endpoint nebo odeslat provoz přes nechtěnou infrastrukturu.

**Požadovaná změna:** Povolit pouze očekávané schéma a tvar endpointu, odmítnout user-info a fragmenty, redirecty ve výchozím stavu zakázat a proxy používat jen po explicitní konfiguraci.

## Doporučené pořadí doplnění specifikace

1. Upevnit transportní profil: ověřované HTTPS jako výchozí, HTTP jen pro explicitní testovací režim.
2. Dopsat Windows bezpečnostní hranice: TUN ovladač, služba, instalace, ACL a named-pipe IPC.
3. Dopsat validaci vnitřních IPv4 paketů a limity parserů, MTU i front.
4. Navrhnout ochranu, rotaci a obnovu klíčů.
5. Definovat síťovou konfiguraci Windows, restart a recovery.
6. Udělat výkonnostní test s realistickou RTT, více klienty a pomalým relay úložištěm před uzamčením modelu jednoho requestu.

## Detailní návrhy řešení

### Řešení 1: bezpečný TLS profil

Konfigurace musí ve výchozím stavu vyžadovat `https` a standardní ověření řetězce certifikátů i názvu hostitele. Konfigurační volba pro neověřený certifikát má být výslovně vývojová, výchozí `false` a v produkčním sestavení ideálně úplně nedostupná. Klient nesmí potlačit chybu expirovaného certifikátu, nesouladu názvu ani nedůvěryhodné autority. Pro citlivé nasazení lze přidat volitelné pinování veřejného klíče certifikátu s postupem bezpečné rotace pinu.

### Řešení 2: oddělený laboratorní HTTP režim

HTTP nesmí být implicitní fallback po chybě TLS. Pokud je zachováno, konfigurace musí vyžadovat samostatný přepínač `allowInsecureHttp`, jehož aktivace je viditelná ve stavu služby i tray. Běžný instalátor a produkční politika jej nastaví na `false`. Dokumentace musí jednoznačně říct, že tento režim neposkytuje ochranu dostupnosti proti zachycení a replayi pollingu; vhodný je pouze pro lokální testy v důvěryhodné síti.

### Řešení 3: zabezpečené tray–service IPC

Použít jeden named pipe vytvořený službou před spuštěním tray klientů. Pipe má mít explicitní bezpečnostní deskriptor: přístup jen pro Service SID a pro interaktivního uživatele, kterému instalace či správce povolil ovládání. Po připojení služba ověří token klienta, jeho SID a případně integritní úroveň; tray nesmí určovat identitu pouze hodnotou zaslanou v těle zprávy. Protokol musí mít verzovaný pevný formát, maximální délku zprávy, timeout, limit současných klientů a jen povolené příkazy. Přenos cesty, konfigurace, klíčů a libovolných shell příkazů je zakázán. Všechny chyby se vrací jako bezpečné stavové kódy bez tajemství.

### Řešení 4: úplná validace vnitřních paketů

Před zařazením odchozího paketu ověřit minimální IPv4 hlavičku, verzi, IHL, total length, že buffer přesně odpovídá deklarované délce, a že zdroj je lokální virtuální IP. Cíl musí být platná VPN IP s existujícím kontraktem. Po dešifrování provést tutéž strukturální validaci a navíc vyžadovat: vnitřní zdroj je `src` datového řádku, vnitřní cíl je lokální virtuální IP a klíč byl vybrán pro stejnou uspořádanou dvojici. Fragmenty musí mít explicitní politiku: buď jsou správně podporovány jako běžné IPv4 pakety, nebo se bezpečně zahazují; nesmí se implementovat neúplné vlastní skládání fragmentů.

### Řešení 5: bezpečné úložiště konfigurace a klíčů

Konfigurační adresář vytvoří instalátor pod systémově určeným programovým datovým umístěním, nikoli v uživatelsky zapisovatelném instalačním adresáři. DACL povolí čtení pouze účtu služby a určenému správci; běžný tray uživatel dostane jen nutná práva pro požadovanou operaci. Citlivé hodnoty je vhodné uložit zašifrované DPAPI se scope odpovídajícím účtu služby; nechráněný JSON pak nesmí obsahovat použitelné klíče. Zápis používá nový dočasný soubor se správným ACL, úplnou validaci, flush na disk a atomické přejmenování. Služba před načtením ověří vlastnictví, reparse pointy a ACL souboru i rodičů.

### Řešení 6: pevný model TUN ovladače

Specifikace má zvolit konkrétní L3 TUN technologii a podporovaný rozsah verzí. Musí stanovit, odkud pochází podepsaný balíček ovladače, jak se ověřuje jeho podpis a jak se provede upgrade či rollback bez ponechání nefunkčního adaptérů. Platformní vrstva má ohraničené rozhraní pro vytvoření adaptéru, čtení, zápis, ukončení a získání chyb; zbytek klienta nesmí záviset na detailech ovladače. Testovací plán má pokrýt odpojení adaptéru, restart služby, sleep/resume, upgrade Windows a souběh s jinými VPN klienty.

### Řešení 7: hardening Windows Service a instalátoru

Binárky, ovladač a instalační manifest mají být autentikódově podepsané a instalátor musí před použitím ověřit jejich původ. Cesta služby nesmí být zapisovatelná neprivilegovaným uživatelem a musí být správně citovaná. Služba poběží s nejmenšími nutnými privilegii; vyhodnotit Service SID, restricted token a explicitní seznam požadovaných oprávnění. Aplikace nastaví bezpečné vyhledávání DLL a nebude načítat knihovny z aktuálního pracovního adresáře. Instalátor nesmí přijímat neověřené cesty ani spouštět obsah z zapisovatelných dočasných adresářů se zvýšenými právy.

### Řešení 8: rotace a revokace klíčů

Změna vyžaduje rozšíření protokolu. Každý přenosný klíč musí mít neprozrazující identifikátor verze. Klient při přechodu po omezenou dobu přijímá aktuální i předchozí povolenou verzi, odesílá však jen aktuální. Revokace access key musí mít rychlý a jednoznačný efekt na serveru; po ní klient přejde do chybového stavu bez automatického nekonečného retry. Specifikace má popsat nouzovou rotaci po kompromitaci, včetně výměny `SERVER_SECRET`, access key a dotčených payload klíčů.

### Řešení 9: transakční editace konfigurace

Položka Edit Config nemá pouze otevřít soubor a předpokládat správný výsledek. Po zavření editoru musí kontrolní komponenta načíst nový soubor, ověřit syntaxi, typy, délky, IP adresy, URL, jedinečnost kontraktů a ACL. Teprve validovaný soubor se atomicky stane aktivní konfigurací. Neplatná verze musí zůstat oddělená s bezpečným chybovým hlášením a aktivní konfigurace se nesmí poškodit. Aktivace je výhradně při odpojeném stavu nebo při explicitním řízeném reconnectu.

### Řešení 10: propustnost síťového pracovníka

První verze má používat persistentní HTTP spojení a connection reuse, aby každý polling neplatil nové TCP a TLS navázání. Výkon se musí změřit pro několik hodnot RTT, velikostí dávek, počtů peerů a rychlostí relay storage. Pokud jediný request nedosáhne cílového výkonu, návrh může povolit malý konfigurovaný počet in-flight requestů. To ale vyžaduje jednoznačný identifikátor dávky, omezení in-flight bajtů, zachování FIFO v rámci cíle a pravidla, co se stane při chybě jednoho z paralelních requestů. Bez těchto pravidel se paralelismus nesmí zavést.

### Řešení 11: adaptivní polling

Plánovač má po úspěšném odeslání dat pollovat rychleji, při prázdných odpovědích interval postupně prodlužovat až k limitu ze `info` a při chybách používat exponenciální backoff s náhodným jitterem. Jitter musí být nezávislý pro každého klienta, aby po obnově relay nevznikla hromadná špička. Interval nesmí nikdy obejít serverový limit ani otevřít paralelní request bez výše popsaného rozšíření. Metriky mají měřit RTT, prázdné pollingy, chybovost a skutečné intervaly.

### Řešení 12: MTU a neodeslatelné pakety

Konfigurace a Windows síťové nastavení musí používat jasně stanovené MTU vypočtené z maximálního requestu, šifrovací a serializační režie. Před vložením do fronty klient ověří, že paket lze vložit alespoň do samostatného datového elementu. Paket, který se nevejde, se okamžitě zahodí, zvýší se agregovaný čítač a fronta pokračuje dalším paketem. Specifikace má určit, zda se při příliš velkém paketu generuje vhodná ICMP signalizace, nebo zda se spoléhá na MTU virtuálního rozhraní.

### Řešení 13: bezpečné parsování a limity

Transport musí odmítnout HTTP odpověď podle hlavičky délky ještě před načtením celého těla, je-li delší než pevný limit. Pro chunked přenos se limit sleduje průběžně. NDJSON se parsuje po řádcích s maximální délkou řádku; JSON parser nesmí alokovat podle neověřeného počtu položek. Base64url se nejprve ověří syntakticky a délkově, až pak dekóduje. Každá vrstva má pevný maximální počet záznamů a bajtů a při překročení bezpečně ukončí konkrétní request bez poškození front.

### Řešení 14: férové řízení front

Vedle globálních limitů zavést limit pro jednoho peeru a případně rezervovanou minimální kapacitu pro aktivní cíle. Při zaplnění se má nová položka pro nadlimitního peera zahodit dříve, než vytlačí jiný provoz. Čítače dropů, délky front a nejstaršího paketu se vedou per-peer i globálně. Při změně konfigurace se fronty neplatných peerů bezpečně vyprázdní a zahozená data se pouze agregovaně zaznamenají.

### Řešení 15: robustní práce s časem

Klient si ukládá monotónní okamžik před a po requestu `health`, změří RTT a serverový čas vztáhne ke středu tohoto intervalu. Pro podpisy dále používá monotónní odvozený čas, aby ho nerozhodily skoky systémových hodin. Přijatý offset musí mít rozumný maximální rozsah a při jeho náhlé velké změně klient vyžádá nové ověření namísto okamžitého použití. Periodická obnova má probíhat i v provozu; po jejím selhání klient přejde do definovaného omezeného režimu a nakonec se znovu bootstrapuje.

### Řešení 16: transakční síťová konfigurace Windows

Před připojením služba vytvoří persistentní záznam pouze změn, které sama provedla: adaptér, IP adresu, routy, metriky a případná DNS pravidla. Všechny položky nesou jednoznačné označení klienta, aby rollback nikdy nemaže cizí konfiguraci. Specifikace musí vybrat split tunnel nebo full tunnel a přesně definovat výjimku pro relay endpoint, jinak by full-tunnel mohl odříznout vlastní spojení k relay. Po pádu, restartu služby nebo systému proběhne idempotentní reconciliation místo slepého mazání.

### Řešení 17: správný model služby a uživatelských relací

Windows Service je autonomní vlastník tunelu a může běžet bez přihlášeného uživatele. Tray aplikace je samostatný neprivilegovaný klient, který se spouští v uživatelské relaci a pouze zobrazuje stav či posílá autorizované IPC požadavky. Specifikace musí určit, který uživatel smí síť ovládat, jak se řeší více současných relací, co se stane po odhlášení a zda autostart tray běží pro každého uživatele nebo jen určeného správce. Služba nikdy nepoužívá interaktivní dialogy.

### Řešení 18: bezpečné zacházení s `info.json`

Výchozí Show Info může zobrazit dešifrovaný obsah přímo v tray UI nebo konzoli bez vytvoření souboru. Pokud je uložení požadováno, musí uživatel výslovně potvrdit export. Soubor se vytvoří atomicky v chráněném uživatelském nebo aplikačním umístění s restriktivním ACL, bez volných děděných práv. Obsah nemá obsahovat tajemství a má jasně stanovenou dobu uchování nebo možnost bezpečného smazání.

### Řešení 19: ochrana paměti, dumpů a sestavení

Citlivé buffery musí mít minimální životnost, nesmí se zbytečně kopírovat a po použití se mažou funkcí, kterou optimalizátor neodstraní. Logy a výjimky nesmí serializovat konfiguraci ani síťová těla. Produkční služba má mít definovanou politiku crash dumpů a Windows Error Reporting tak, aby se citlivá paměť automaticky neexportovala nechráněně. Sestavení má zapnout ASLR, DEP, stack cookies, Control Flow Guard a další standardní ochrany podporované zvoleným kompilátorem; CI má ověřit jejich skutečné nastavení ve výsledných binárkách.

### Řešení 20: striktní URL, redirect a proxy politika

Validátor přijme pouze absolutní URL s očekávaným schématem, hostem a cestou endpointu. Odmítne fragment, user-info, nečekané query parametry a nejednoznačné kódování. HTTP redirecty se ve výchozím stavu nesledují; změna cíle musí vyžadovat novou validovanou konfiguraci. Automatické proxy z prostředí nebo systému mají být vypnuté, pokud klient nemá explicitní proxy konfiguraci s dokumentovanými pravidly ověřování TLS. Do logu se nesmí zapsat URL obsahující citlivé údaje.
