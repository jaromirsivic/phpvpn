# Nálezy z revize klientského plánu 3

Datum revize: 5. září 2026.

Předmět: `plan3_client.md`, související části `client.md`, `protocol.md` a relevantní implementace klienta.

Revize obsahuje 8 technických nedostatků a 4 významná přijatá rizika. Rozlišuje konkrétní rozpory od míst, kde plán zatím neposkytuje dostatečnou garanci. Jde o statickou revizi návrhu a relevantního kódu, nikoli o výsledky provedených zátěžových nebo souběhových testů. Čísla řádků odkazují na stav souborů při revizi.

## 1. Rušení síťových operací spoléhá na nebezpečný mechanismus

Závažnost: vysoká. Kategorie: stabilita, souběh.

Místo: `plan3_client.md`, řádek 105. Implementace: `client/src/platform/windows/transport_winhttp.cpp`, řádky 112–113 a 207–217.

Plán vyžaduje okamžité zrušení probe před Connect nebo stopem. Současný transport používá synchronní WinHTTP a ruší požadavek zavřením handle z jiného vlákna. Microsoft před zavíráním handle během probíhajícího synchronního volání výslovně varuje kvůli souběhovému závodu.

Dopad: hrozí nestabilita právě při Cancel, Disconnect nebo ukončování služby.

Doporučení: plán by měl výslovně zahrnout opravu transportu, například pomocí asynchronního WinHTTP s korektním řízením životnosti.

Rozhodnutí pro budoucí implementaci: tento bod se vyřeší čistě jako obecný mechanismus bezpečného rušení HTTP operací, nikoli jako lokální oprava jediného kroku Wizardu. Mechanismus musí pokrýt všechny následující události:

- příchod globálního Connectu během Wizard probe, kdy se probe bezpečně zruší, potvrdí se jeho skutečné ukončení a teprve potom začne bootstrap VPN;
- běžný globální Disconnect aktivní VPN, aby se ukončil případný provozní HTTP request a mohl proběhnout teardown;
- stop Windows Service při restartu, aktualizaci, odinstalaci nebo běžném ukončení;
- Cancel, Reset, Escape nebo zavření Wizardu během jeho síťové kontroly.

Požadovaná vlastnost není nulová prodleva. Zrušení musí mít krátkou a konečnou dobu, jednoznačné vlastnictví requestu a potvrzený terminální stav. Další síťová operace nesmí začít, dokud předchozí request nemůže být používán žádným vláknem ani callbackem. Řešení musí zabránit dvojímu zavření, použití handle po zavření, deadlocku a souběžným datovým requestům.

Zdroj: [Microsoft: WinHttpCloseHandle](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpclosehandle).

## 2. Předpokládaný celkový timeout ve skutečnosti chybí

Závažnost: vysoká. Kategorie: dostupnost, latence.

Místo: `plan3_client.md`, řádek 322. Implementace: `client/src/platform/windows/transport_winhttp.cpp`, řádky 118–123 a 270–289.

Plán spoléhá na transportní timeouty. Implementace ale pouze nastavuje `WinHttpSetTimeouts` a potom čte odpověď v cyklu bez celkového deadline. Receive timeout omezuje čekání na část odpovědi, nikoli automaticky celou dobu stahování.

Dopad: server posílající data dostatečně pomalu může obsadit jedinou globální Wizard operaci podstatně déle než deklarovaných 30 sekund.

Doporučení: doplnit monotónní deadline celého requestu a test pomalu dávkované odpovědi.

Zdroj: [Microsoft: WINHTTP_OPTION_RECEIVE_TIMEOUT](https://learn.microsoft.com/en-us/windows/win32/winhttp/option-flags#winhttp_option_receive_timeout).

## 3. Přesun get-info na jediné síťové vlákno může zastavit přenos VPN

Závažnost: vysoká. Kategorie: latence, dostupnost.

Místo: `plan3_client.md`, řádky 101–107. Implementace: `client/src/core/runtime/vpn_engine.cpp`, řádky 210–222.

Plán sdílí síťové vlákno mezi provozem a diagnostikou. Současný `get-info` provádí health a následně info. Jakmile taková synchronní úloha začne, samotná „přednost provozního spojení“ ji nepřeruší. I po opravě celkových timeoutů mohou dvě pomalé odpovědi zabrat přibližně 60 sekund.

Dopad: mezitím se hromadí odchozí pakety a nevybírá příchozí fronta.

Doporučení: definovat konkrétní pravidlo přerušování diagnostiky a maximální zdržení datového pollingu, včetně testu při zatížené VPN.

## 4. Starý editor může přepsat právě dokončený Wizard

Závažnost: vysoká. Kategorie: ztráta změn konfigurace, souběh.

Místo: `plan3_client.md`, řádek 170. Související smlouva: `client.md`, příkazy `get-config` a `set-config`.

Plán zavádí společnou serializaci zápisů a invalidaci Wizard baseline po `set-config`. Opačný směr ale chráněný není: editor načte konfiguraci A, Wizard uloží B a editor následně odešle upravenou A přes původní `set-config`, který neobsahuje očekávanou revizi.

Dopad: oba zápisy proběhnou korektně za sebou, přesto se změny B ztratí. Serializace není ochrana proti zastaralému obsahu.

Doporučení: nový editor potřebuje podmíněné uložení s revizí, případně musí dokument jasně přiznat omezení původního příkazu.

## 5. Idempotence požadavků nemá vyřešenou historii a její limity

Závažnost: vysoká. Kategorie: opakované vedlejší efekty, paměťové limity.

Místo: `plan3_client.md`, řádky 312 a 322.

Plán slibuje, že stejné `clientAttemptId` vždy vrátí stejnou operaci. Stanovuje ale pouze jednu aktuální operaci a neomezuje počet pokusů během relace.

Dopad: pokud se staré identifikátory zapomenou, opožděné opakování může znovu spotřebovat serverovou frontu. Pokud se všechny uchovají, dlouhodobě aktivní relace může neomezeně zvětšovat historii.

Doporučení: definovat omezený registr pokusů a pravidlo, že vyřazený pokus se odmítne, nikdy znovu nevykoná. Zahrnout Reset i přerušení přes Connect.

## 6. Doporučená oprava kontraktu je pro opravitelnou konfiguraci nedostupná

Závažnost: střední. Kategorie: rozpor v uživatelském postupu.

Místo: `plan3_client.md`, řádky 31, 163 a 359.

D13 a chybová hláška doporučují opravit konflikt přes Edit Config. Tabulka dostupnosti však editor pro `repairable` zakazuje. Například konfiguraci bez Access Key lze ve Wizardu opravovat, ale konfliktní kontrakt doporučenou cestou upravit nelze.

Dopad: uživatel dostává návod na nedostupnou funkci a nemůže doporučenou cestou dokončit opravu.

Doporučení: povolit editor pro čitelnou opravitelnou konfiguraci s úplnou validací při Save, nebo změnit návod na skutečně dostupný postup.

## 7. Omezený počet IPC workerů nezaručuje dostupný Disconnect

Závažnost: střední. Kategorie: dostupnost řízení služby.

Místo: `plan3_client.md`, řádek 109.

Plán navrhuje například čtyři workery a limit jednoho dlouhého `get-info`. Čtyři klienti ale mohou obsadit všechny workery neúplnými requesty nebo neodebíranými odpověďmi.

Dopad: konečný deadline jednotlivého spojení pomůže s úklidem, nezaručuje však včasnou obsluhu nového řídicího příkazu.

Doporučení: doplnit pravidla přijímání spojení, absolutní I/O deadline a zátěžový test dostupnosti řízení při obsazených workerech.

## 8. Pád tray nemá mechanismus pro deklarované zrušení relace

Závažnost: střední. Kategorie: životní cyklus, dostupnost.

Místo: `plan3_client.md`, řádky 286, 322 a 534, kritérium A33.

A33 slibuje zrušení konceptu a operací při pádu okna nebo ukončení tray. IPC ale používá jednorázová spojení, takže odpojení pipe nerozlišuje běžně dokončený příkaz od pádu aplikace. Popsaná expirace nastává až po 30 minutách.

Dopad: opuštěné relace mohou vyčerpat limit osmi průvodců.

Doporučení: definovat obnovovaný časový pronájem relace a lhůtu úklidu po ztrátě klienta, nebo odpovídajícím způsobem oslabit A33.

## Přijatá rizika

Následující rizika plán většinou přiznává. Nejde tedy o přehlédnuté požadavky, jejich praktické dopady jsou ale zásadní.

## 9. Kolize IP může poškodit komunikaci jiného počítače

Kategorie: přijaté riziko kolize identity a ztráty zpráv.

Místo: `plan3_client.md`, řádky 250–256.

Výběr podle `clients` při obsazení všech adres automaticky nabídne nejstarší záznam. Ten může patřit stále aktivnímu klientovi. Dva souběžné průvodce také mohou nabídnout stejnou chybějící adresu.

Dopad: následný test přístupu uspěje i pro sdílenou identitu a může odebrat cizí zprávy. Úspěšný check-access neprokazuje jedinečnost IP.

Doporučení: bez rezervací nelze kolize vyloučit. Při zaplněném rozsahu alespoň automaticky nevybírat potenciálně obsazenou adresu.

## 10. Výchozí payload klíče nejsou tajné

Závažnost: kritická pro důvěrnost. Kategorie: přijaté bezpečnostní riziko výchozích klíčů.

Místo: `plan3_client.md`, řádky 252 a 270–280.

SHA-256 dvojice IP umožní klíč vypočítat komukoli včetně relay serveru. U zachyceného provozu tak neplatí předpoklad, že server ani pozorovatel nedokážou payload dešifrovat. Předvídatelné Access Keys navíc umožňují držiteli společného server secretu vystupovat za ostatní výchozí IP.

Dopad: změna samotného `SERVER_SECRET` neopraví veřejně odvoditelné payload klíče.

Doporučení: bezpečný profil vyžaduje skutečně tajné distribuované klíče.

## 11. Reset může zhoršit dosavadní bezpečnost konfigurace

Kategorie: přijaté bezpečnostní riziko výchozí konfigurace, srozumitelnost UI.

Místo: `plan3_client.md`, řádky 24 a 150.

Reset obnoví HTTP URL, veřejný secret a `trustAnyTlsCertificate=true`. Uživatel může následně zadat HTTPS URL a vlastní secret, ale vypnuté ověřování certifikátu zůstane skryté mezi nezobrazenými hodnotami.

Dopad: resetovaná konfigurace může mít slabší bezpečnostní politiku než původní konfigurace. Jde o výslovně přijaté rozhodnutí D06.

Doporučení: přímo před Finish viditelně uvést výslednou TLS politiku a její změnu oproti původní konfiguraci.

## 12. Veřejné IPC ruší izolaci konfigurace mezi lokálními uživateli a aplikacemi

Kategorie: přijaté bezpečnostní omezení celého klienta.

Místo: `plan3_client.md`, řádek 336. Související smlouva: `client.md`, veřejné IPC a příkazy `get-config` a `set-config`.

Plán zachovává přístup bez autorizace. Libovolná lokální aplikace tak prostřednictvím původních příkazů může získat všechny klíče, změnit konfiguraci nebo globálně odpojit VPN.

Dopad: ACL konfiguračního souboru tuto cestu nechrání a náhodná UUID nejsou autorizace.

Doporučení: zachovat toto omezení výslovně uvedené v bezpečnostním modelu. Jde o vědomě přijaté bezpečnostní omezení celého klienta, nikoli o ochranu, kterou by vyřešila přísnější validace nových Wizard příkazů.
