---
intent_version: 5
created_at: 2026-09-18-23-28-01
based_on: intent-v004.md
produces: intent-v005.md
---

# Zdrojová konverzace

Poznámka: Hlasový přepis obsahoval průběžně opakované a překrývající se úseky. Níže jsou zachyceny všechny věcné uživatelské a AI příspěvky v chronologickém pořadí, přičemž pouze technické duplikáty vzniklé překryvem přepisu jsou sloučeny.

**User:** Ahoj, chci vytvořit nový intent. To znamená záměr na úpravu systému.

**AI:** Nový intent jsem založila a zachytávání je aktivní. Pokračuj v diktování. Kdykoli můžeš říct „pozastavit“, „pokračovat“ nebo „zpracovat intent“.

**User:** Aktuálně v systému na straně klienta dostáváme IP pakety do jednotlivých bucketů a už teď je tam buffrování podle klientské IP adresy. Děje se to před tím, než spočítáme hash z přijatých bufferů, zakódujeme je a předáme VPN serveru. Když se klient připojí k druhému klientovi prostřednictvím zabezpečeného spojení a jedna aplikace, například FTP, zcela vytíží paketovou komunikaci, ostatní aplikace se stanou nepoužitelné nebo se k nim pakety vůbec nedostanou. Potřebuji jednotlivé buckety dále rozčlenit tak, že každý bude mít šestnáct sub-bucketů.

**AI:** Detail o šestnácti dynamicky přidělovaných sub-bucketech mám zachycený v aktivním intentu.

**User:** V rámci každého bucketu bude dalších šestnáct sub-bucketů, tedy podbufferů, které se budou dynamicky přidělovat jednotlivým službám, pokud budou všechny využity. Asi je rozdělíme klasickou pěticí: zdrojová IP adresa, cílová IP adresa a protokol. Co tam je dál?

**AI:** Během zachytávání obsah nerozebírám. Chceš-li teď doplnit chybějící položky té pětice, řekni „pozastavit intent“ a odpovím ti.

**User:** Teď zastav zaznamenávání a diskutuj se mnou.

**AI:** Zachytávání jsem pozastavila. Dosavadní obsah zůstává součástí intentu a teď můžeme návrh normálně prodiskutovat. Klasická síťová pětice je zdrojová IP adresa, cílová IP adresa, zdrojový port, cílový port a transportní protokol. U protokolů bez portů bude potřeba určit náhradní klíč nebo je zařadit do společného sub-bucketu.

**User:** Přesně tak. Chci dál brainstormovat. Když běží hodně aplikací, chci uvnitř komunikace z jedné IP adresy na druhou sub-bucketový systém, aby jedna aplikace na nějakém portu neutlačovala jinou. Je to dobrý způsob, nebo máš lepší?

**AI:** Lepší než pouhé hashování do šestnácti front je fair queuing nad jednotlivými toky. Pětice je správný identifikátor toku, ale plánovač musí aktivně střídat neprázdné fronty a omezit, kolik dat z jedné odešle v jednom kole. Doporučila bych dvouúrovňové férové řazení: současný bucket pro cílového klienta ponechat a uvnitř dynamicky vytvářet fronty podle pětice. Ty obsluhovat algoritmem Deficit Round Robin podle počtu bajtů. Pevných šestnáct hashovaných front poskytuje pouze přibližnou férovost, protože kolidující toky se mohou znovu blokovat. Pětice zajišťuje férovost mezi spojeními, ne spolehlivě mezi aplikacemi.

**User:** Popiš mi fair queuing.

**AI:** Každý komunikační tok má vlastní frontu a plánovač jim pravidelně střídá prostor k odesílání. Deficit Round Robin přidává každé frontě v každém kole kredit v bajtech. Fronta může odesílat, dokud má dost kreditu, a nevyužitý kredit si převádí. Objemný přenos tak využije kapacitu, když je sám, ale po příchodu malého interaktivního toku jej nemůže vyhladovět. Uvnitř jednoho toku se zachová pořadí paketů.

**User:** To se mi líbí. Kdyby bylo více než šestnáct spojení, udělal bych patnáct samostatných front a šestnáctou vyhrazenou pro všechny ostatní. Je to v pořádku?

**AI:** Je to použitelné, pokud je více než patnáct toků výjimečné, ale všechny přetokové toky by společně dostaly podíl jediné fronty a znovu se blokovaly. Lepší je mít šestnáct skutečných flow front a další toky stabilně rozdělit mezi všechny. Pokud by zůstalo řešení patnáct plus jedna, sloty potřebují uvolňování, aby staré toky nezůstaly zvýhodněné.

**User:** Co kdybychom zkombinovali oba přístupy? Budeme mít šestnáct zpočátku volných front. První unikátní pětice obsadí první frontu, druhá pětice druhou a tak dále. Když jsou všechny obsazené a dorazí sedmnáctá pětice, vypočte se hash pětice a jeho poslední čtyři bity určí frontu. Je to lepší?

**AI:** Ano. Jde o rozumný hybrid mezi přesným fair queuingem a omezenou pamětí. Nová pětice získá volnou frontu, existující vazba zůstává stabilní a po vyčerpání front se další pětice stabilně přiřazují hashem. Mezi šestnácti frontami poběží Deficit Round Robin podle bajtů. Férovost bude přesná mezi frontami a přibližná uvnitř sdílené fronty. Hash musí kvalitně promíchat všechna pole a kvůli úmyslným kolizím by měl být náhodně klíčovaný při startu klienta.

**User:** Drobná úprava: když bude všech šestnáct front zablokovaných, sedmnáctý a každý další paket se uloží do náhodné fronty. Pokud je fronta prázdná, její alokace pětici se smaže. Pokud jsou ve frontě jen pakety jiných pětic než pětice v hlavičce fronty, fronta se alokuje pětici prvního paketu, který v ní je. Co si o tom myslíš?

**AI:** Automatické přebírání uvolněné fronty je dobré, ale náhodné přiřazení každého paketu by mohlo změnit pořadí paketů jednoho toku, zejména u TCP. Doporučené chování je: vlastník používá svou frontu, nová pětice získá volnou frontu, po obsazení všech front se použije stabilní hash celé pětice, prázdná fronta se uvolní a fronta bez paketů původního vlastníka může atomicky povýšit pětici svého prvního zbývajícího paketu. Před povýšením je potřeba ověřit, že daná pětice nevlastní jinou frontu.

**User:** Náhodné rozmisťování beru zpět, vracíme se k hashi. Jinak se mi návrh velice líbí.

**AI:** Přetokové pětice se tedy budou stabilně přiřazovat pomocí hashe, nikoli náhodně po jednotlivých paketech. Dynamické vlastnictví sub-bucketů a jejich přebírání zůstává podle posledního návrhu.

**User:** Vytvoř intent. Chtěl jsem říct: zpracuj intent.

**AI:** Pro jednoznačné zpracování zbývá určit směr provozu, zacházení s fragmenty a protokoly bez portů a DRR kvantum. Výchozí návrh je pouze odchozí směr, společný fallback podle adres a protokolu a kvantum odvozené od maximální velikosti tunelového paketu.

**User:** Co je DRR kvantum?

**AI:** Je to příděl v bajtech pro každou frontu v jednom kole. Fronta si jej nasčítá, odešle pakety, dokud má kredit, a nespotřebovaný zbytek si nechá na příště, aby dostaly šanci i větší pakety.

**User:** Souhlasím s výchozím návrhem.

## Pokračování konverzace po vytvoření v001

**AI-created artifact:** Po dokončení první verze byl uživateli zobrazen odkaz na následující úplný intent:

---
intent_version: 1
created_at: 2026-09-18-19-52-40
source: intent-chat-v001.md
---

# Flow fair queuing uvnitř odchozích peer front

## Shrnutí

Rozšířit klientské odchozí paketové fronty o druhou, vnitřní úroveň plánování. Každá současná fronta protistrany bude obsahovat přesně 16 sub-bucketů. Aktivní síťové toky budou podle možností dostávat výhradní sub-bucket a všechny neprázdné sub-buckety budou obsluhovány pomocí Deficit Round Robin podle bajtů. Cílem je zabránit tomu, aby objemný tok, například FTP přenos, vyhladověl interaktivní nebo jinak méně objemné toky stejné dvojice klientů.

## Kontext projektu

- Odchozí pakety jsou nyní uloženy v `DirectionQueues` podle IP adresy protistrany. Přijetí a účetnictví front je soustředěno v `client/src/core/queues/packet_queues.h` a `client/src/core/queues/packet_queues.cpp`.
- `RequestScheduler::planNextRequest` v `client/src/core/queues/scheduler.cpp` již rotuje pořadí peer front, aby dlouhá fronta jedné protistrany nevyhladověla ostatní.
- `NetworkWorker::exchangeData` v `client/src/core/runtime/network_worker.cpp` vrací po neúspěšném požadavku naplánované pakety zpět do odchozích front. Nové plánování musí umět obnovit i původní sub-bucket a pořadí.
- Současný `Ipv4PacketView` v `client/src/core/protocol/ipv4.h` obsahuje jen adresy a délky. Pro klasifikaci toku bude nutné bezpečně doplnit čtení protokolu, fragmentace a TCP/UDP portů.

## Rozsah

- Změna se týká pouze odchozího směru před sestavením, zašifrováním a odesláním datového požadavku VPN serveru.
- Příchozí fronty před zápisem do tunelu se v tomto intentu nemění.
- Zachovat stávající férovost mezi protistranami, limity požadavku, šifrování, formát přenosu a validaci paketů.
- Férovost je definována mezi síťovými toky, nikoli mezi procesy nebo skutečnými aplikacemi. Jedna aplikace používající více pětic může získat více dílů než aplikace s jedinou pěticí.

## Identita toku

### Běžný TCP a UDP

Tok je určen směrovou pěticí:

1. zdrojová IPv4 adresa,
2. cílová IPv4 adresa,
3. zdrojový port,
4. cílový port,
5. číslo IP protokolu.

Pětice se nekanonizuje mezi směry. V tomto intentu se používá jen odchozí směr.

### Fragmenty a protokoly bez portů

- Pro fragmentované IPv4 pakety a protokoly, u kterých nelze bezpečně přečíst TCP/UDP porty, použít fallback klíč tvořený zdrojovou adresou, cílovou adresou a protokolem, s oběma porty nastavenými na nulu.
- Všechny fragmenty odpovídající tomuto fallbacku musí procházet stejnou klasifikací. Intent nevyžaduje fragmentovou mapovací cache.
- Neznámý podporovaný IP protokol se nesmí zahodit pouze proto, že nemá porty.
- Čtení hlaviček musí být délkově bezpečné a nesmí číst porty z neúplného nebo nenultého fragmentu.

## Struktura šestnácti sub-bucketů

Každá odchozí peer fronta obsahuje právě 16 sub-bucketů. Každý sub-bucket má:

- FIFO posloupnost paketů doplněných o jejich klasifikační klíč,
- volitelného výhradního vlastníka, kterým je jeden flow klíč,
- počet uložených bajtů,
- DRR deficit v bajtech,
- metadata nutná pro bezpečné vrácení paketu a změnu vlastnictví.

Peer fronta udržuje omezený adresář vlastnických vazeb `flow key -> sub-bucket`. V adresáři jsou pouze aktuální výhradní vlastníci, nejvýše 16 položek. Přetokové flow klíče nepotřebují neomezenou stavovou mapu.

## Zařazení odchozího paketu

Zařazení paketu proběhne atomicky pod stejnou synchronizací, která chrání peer frontu a její účetnictví:

1. Paket se bezpečně klasifikuje na flow klíč.
2. Pokud klíč vlastní sub-bucket, paket se vloží na konec tohoto sub-bucketu.
3. Pokud klíč sub-bucket nevlastní a existuje volný sub-bucket, klíč jej získá jako výhradní vlastník a paket se vloží do něj.
4. Pokud není žádný volný sub-bucket, z celého flow klíče se vypočítá kvalitní, procesně stabilní a náhodně klíčovaný hash. Poslední čtyři rovnoměrně rozložené bity zvolí jeden ze 16 sub-bucketů.
5. Všechny pakety stejného přetokového toku musí při nezměněném procesním klíči zvolit stejný sub-bucket. Jednotlivé pakety se nikdy nerozmisťují náhodně.

Hashovací klíč vznikne při startu klienta, nemusí přežít restart a nesmí být ovlivnitelný vzdáleným protějškem. Implementace nesmí předpokládat, že nízké čtyři bity nekvalitního obecného hashe jsou rovnoměrné.

## Životní cyklus vlastnictví

- Jakmile se sub-bucket vyprázdní, jeho vlastnická vazba se odstraní, deficit se resetuje a sub-bucket se stane volným.
- Pokud v neprázdném sub-bucketu nezůstává žádný paket současného vlastníka, jeho vazba se odstraní.
- První zbývající paket může určit nového výhradního vlastníka sub-bucketu, pokud jeho flow klíč již nevlastní jiný sub-bucket. Adresář a hlavička sub-bucketu se aktualizují atomicky.
- Pokud flow klíč prvního paketu již vlastní jiný sub-bucket, nesmí vzniknout duplicitní vlastnictví. Implementace musí bezpečně pokračovat bez povýšení, případně zvolit první způsobilý klíč podle jednoznačně zdokumentovaného pravidla.
- Intent vyžaduje okamžité uvolnění po vyprázdnění, nikoli časový idle timeout.
- Samotná přítomnost přetokových paketů ve frontě neznamená, že je sub-bucket volný.

## Deficit Round Robin

- Uvnitř každé peer fronty plánovat neprázdné sub-buckety pomocí Deficit Round Robin.
- Účetní jednotkou jsou bajty paketu, nikoli počet paketů.
- Každý aktivní sub-bucket dostává stejné kvantum za kolo.
- Výchozí kvantum odvodit od aktuální maximální velikosti tunelového paketu tak, aby sub-bucket s běžným plným paketem mohl při dostatečném rozpočtu odeslat přibližně jeden takový paket za kolo.
- Nevyužitý deficit se přenáší do dalších kol, aby nebyly vyhladověny větší pakety.
- Po vyprázdnění sub-bucketu deficit resetovat.
- Zachovat FIFO pořadí uvnitř každého sub-bucketu.
- Peer-level rotace zůstává nad touto vrstvou. Žádná peer fronta ani aktivní sub-bucket nesmí být trvale vyhladověn.

## Sestavení požadavku a návrat paketů

- Výběr paketů musí stále respektovat přesný limit těla požadavku, maximální počet datových záznamů a chování pro paket, který se nevejde ani do jinak prázdného platného požadavku.
- Jeden datový záznam může nadále obsahovat více paketů stejné protistrany, i když pocházejí z různých sub-bucketů.
- Plán požadavku musí uchovat dostatek údajů k vrácení každého paketu do původního peeru a původního sub-bucketu.
- Při selhání nebo neověřeném přijetí požadavku se všechny pakety vrátí tak, aby se zachovalo jejich původní pořadí a DRR stav nebyl poškozen dvojím účtováním.
- Úspěšně přijaté pakety se nevracejí.

## Limity a účetnictví

- Zachovat současný globální limit odchozí fronty, minimální rezervaci na peer a maximální počet peerů.
- Šestnáct sub-bucketů nevytváří šestnáct nových minimálních bajtových rezervací. Sdílejí bajtový rozpočet své peer fronty.
- Součet bajtů a položek na peer i globální úrovni musí zůstat přesný při vložení, odebrání, vrácení, zahození i změně vlastníka.
- Při překročení existujících limitů zachovat současné důvody zahození, pokud změna nevyžaduje nový přesně zdokumentovaný důvod.

## Pozorovatelnost

Doplnit pouze takovou diagnostiku, která je potřebná pro ověření chování, minimálně možnost v testech zjistit:

- počet aktivních sub-bucketů,
- vlastnické a sdílené přiřazení,
- průběh DRR výběru,
- zachování bajtového účetnictví.

Produkční logování nesmí vypisovat každý paket ani nekontrolovaně zvyšovat objem logů.

## Testovací a akceptační kritéria

1. Prvních nejvýše 16 současně aktivních odchozích pětic jedné protistrany získá při dostupných volných sub-bucketech samostatné vlastnictví.
2. Sedmnáctá a další pětice se stabilně mapují hashem do jednoho ze 16 sub-bucketů. Opakované pakety stejné pětice se nerozptýlí mezi více sub-bucketů.
3. Hashované přetokové toky se rozkládají mezi všech 16 sub-bucketů a nejsou soustředěny v jediné přetokové frontě.
4. Prázdný sub-bucket okamžitě ztratí vlastníka a může jej získat nová pětice.
5. Pokud zmizí všechny pakety vlastníka, ale zůstanou cizí pakety, vlastnictví lze bezpečně převést na způsobilou pětici prvního zbývajícího paketu bez duplicitního vlastnictví.
6. Dlouhý objemný tok nedokáže trvale vyhladovět sporadický tok stejné protistrany.
7. Toky s různými velikostmi paketů získávají férový podíl podle bajtů, nikoli podle počtu paketů.
8. FIFO pořadí paketů v jednom sub-bucketu zůstane zachováno, včetně návratu po neúspěšném síťovém požadavku.
9. Protokoly bez portů a fragmentované IPv4 pakety jsou přijaty a použijí definovaný fallback klíč bez nebezpečného čtení hlavičky.
10. Stávající peer-level férovost, limity těla požadavku, maximální počet záznamů, drop účetnictví a globální i per-peer bajtové limity zůstanou funkční.
11. Nové jednotkové testy pokryjí alokaci všech slotů, hashovou kolizi, uvolnění, povýšení vlastníka, DRR deficit, různě velké pakety, oversized paket, návrat po chybě a souběžné zařazování.
12. Stávající testy paketových front, scheduleru, síťového workeru a integrační přenosové testy projdou beze změny očekávaného drátového formátu.

## Realizační vodítka

- Preferovat rozšíření stávajících abstrakcí `DirectionQueues`, `PeerQueue`, `RequestScheduler` a `RequestPlan` před vytvořením paralelního systému front.
- Oddělit parsování flow klíče od plánování, aby šlo klasifikaci samostatně testovat.
- Hashovací funkci a procesní klíč zapouzdřit za testovatelným rozhraním s možností deterministického klíče v testech.
- Zachovat jednoho konzumenta odchozí fronty, ale nepředpokládat, že producenti jsou jediní nebo nesouběžní.
- Nedělat změny serverového drátového protokolu, pokud testy neodhalí nevyhnutelnou potřebu. Taková potřeba by znamenala novou materiální změnu intentu.

**AI:** První úplná verze intentu je vytvořená a ověřená společně se zdrojovým přepisem.

**User:** Zajímá mě, zda se zvýší nároky na paměť klienta.

**AI:** Paketová data zůstanou pod globálním limitem, ale přibudou metadata sub-bucketů. Ukládání celé pětice ke každému paketu by bylo citelné zejména u malých paketů. Doporučením je obnovovat pětici z existující hlavičky nebo použít kompaktní metadata a paketová data nekopírovat.

**User:** Když máme šestnáct sub-bucketů, společný limit například 16 MB, jedna aplikace uloží v jednom slotu 2 MB a maximální request je 8 MB, jak to dopadne?

**AI:** Limit je společný a není pevně dělený mezi sub-buckety. Dva megabajty se přijmou, pokud je místo. Jediný aktivní tok může díky work-conserving DRR využít celý dostupný request. Při dalších aktivních tocích se data skládají po bajtových kolech. DRR řeší odesílání, nikoli obsazování paměti.

**User:** Znamená 64 MB a šestnáct sub-bucketů automaticky 4 MB na sub-bucket?

**AI:** Ne. Výchozích 64 MiB je společný limit celé odchozí struktury přes všechny peery. Čtyři MiB by platily jen při tvrdých kvótách. Měkká kvóta garantuje minimum, ale dovoluje využít volnou sdílenou kapacitu.

**User:** Jakou garantovanou velikost doporučuješ?

**AI:** Doporučená výchozí hodnota je 8 KiB na sub-bucket, tedy současných 128 KiB peer rezervace dělených šestnácti. Jde přibližně o pět běžných 1500B paketů a není to maximální velikost.

**User:** Byl by problém použít 16 KiB?

**AI:** Je to možné, ale zvýšilo by to peer rezervaci na 256 KiB. Při 64MiB globálním limitu by to podporovalo přibližně 256 protistran a omezilo sdílenou kapacitu.

**User:** Jaká je rezervace klasického bucketu bez sub-bucketů?

**AI:** Současná výchozí logická rezervace jednoho peer bucketu je 128 KiB.

**User:** Souhlasím s 8 KiB na sub-bucket. Zpracuj intent.

**AI:** Osm KiB na každý ze šestnácti sub-bucketů je potvrzené. Součet zůstává 128 KiB na peer bucket.

## Pokračování konverzace po vytvoření v002

**AI-created artifact:** Po dokončení druhé verze byl uživateli zobrazen odkaz na následující úplný intent:

---
intent_version: 2
created_at: 2026-09-18-20-08-20
based_on: intent-v001.md
source: intent-chat-v002.md
---

# Flow fair queuing uvnitř odchozích peer front

## Shrnutí

Rozšířit klientské odchozí paketové fronty o druhou, vnitřní úroveň plánování. Každá současná fronta protistrany bude obsahovat přesně 16 sub-bucketů. Aktivní síťové toky budou podle možností dostávat výhradní sub-bucket a všechny neprázdné sub-buckety budou obsluhovány pomocí Deficit Round Robin podle bajtů. Každý sub-bucket dostane výchozí logickou rezervaci 8 KiB odvozenou ze současné 128KiB rezervace peer fronty. Cílem je zabránit tomu, aby objemný tok, například FTP přenos, vyhladověl interaktivní nebo jinak méně objemné toky stejné dvojice klientů při odesílání i při přijímání do omezené fronty.

## Kontext projektu

- Odchozí pakety jsou nyní uloženy v `DirectionQueues` podle IP adresy protistrany. Přijetí a účetnictví front je soustředěno v `client/src/core/queues/packet_queues.h` a `client/src/core/queues/packet_queues.cpp`.
- `RequestScheduler::planNextRequest` v `client/src/core/queues/scheduler.cpp` již rotuje pořadí peer front, aby dlouhá fronta jedné protistrany nevyhladověla ostatní.
- `NetworkWorker::exchangeData` v `client/src/core/runtime/network_worker.cpp` vrací po neúspěšném požadavku naplánované pakety zpět do odchozích front. Nové plánování musí umět obnovit i původní sub-bucket a pořadí.
- Současný `Ipv4PacketView` v `client/src/core/protocol/ipv4.h` obsahuje jen adresy a délky. Pro klasifikaci toku bude nutné bezpečně doplnit čtení protokolu, fragmentace a TCP/UDP portů.
- Výchozí `maxOutboundQueueBytes` je 64 MiB a výchozí `minPerPeerQueueBytes` je 128 KiB. Rezervace jsou logické podmínky přijetí, nikoli předem alokovaná fyzická paměť.

## Rozsah

- Změna se týká pouze odchozího směru před sestavením, zašifrováním a odesláním datového požadavku VPN serveru.
- Příchozí fronty před zápisem do tunelu se v tomto intentu nemění.
- Zachovat stávající férovost mezi protistranami, limity požadavku, šifrování, formát přenosu a validaci paketů.
- Férovost je definována mezi síťovými toky, nikoli mezi procesy nebo skutečnými aplikacemi. Jedna aplikace používající více pětic může získat více dílů než aplikace s jedinou pěticí.

## Identita toku

### Běžný TCP a UDP

Tok je určen směrovou pěticí:

1. zdrojová IPv4 adresa,
2. cílová IPv4 adresa,
3. zdrojový port,
4. cílový port,
5. číslo IP protokolu.

Pětice se nekanonizuje mezi směry. V tomto intentu se používá jen odchozí směr.

### Fragmenty a protokoly bez portů

- Pro fragmentované IPv4 pakety a protokoly, u kterých nelze bezpečně přečíst TCP/UDP porty, použít fallback klíč tvořený zdrojovou adresou, cílovou adresou a protokolem, s oběma porty nastavenými na nulu.
- Všechny fragmenty odpovídající tomuto fallbacku musí procházet stejnou klasifikací. Intent nevyžaduje fragmentovou mapovací cache.
- Neznámý podporovaný IP protokol se nesmí zahodit pouze proto, že nemá porty.
- Čtení hlaviček musí být délkově bezpečné a nesmí číst porty z neúplného nebo nenultého fragmentu.

## Struktura šestnácti sub-bucketů

Každá odchozí peer fronta obsahuje právě 16 sub-bucketů. Každý sub-bucket má:

- FIFO posloupnost paketů doplněných o jejich klasifikační klíč nebo ekvivalentní způsob jeho bezpečného obnovení,
- volitelného výhradního vlastníka, kterým je jeden flow klíč,
- počet uložených bajtů,
- logickou minimální rezervaci odvozenou z peer rezervace, ve výchozí konfiguraci 8 KiB,
- DRR deficit v bajtech,
- metadata nutná pro bezpečné vrácení paketu a změnu vlastnictví.

Peer fronta udržuje omezený adresář vlastnických vazeb `flow key -> sub-bucket`. V adresáři jsou pouze aktuální výhradní vlastníci, nejvýše 16 položek. Přetokové flow klíče nepotřebují neomezenou stavovou mapu.

## Zařazení odchozího paketu

Zařazení paketu proběhne atomicky pod stejnou synchronizací, která chrání peer frontu a její účetnictví:

1. Paket se bezpečně klasifikuje na flow klíč.
2. Pokud klíč vlastní sub-bucket, paket se vloží na konec tohoto sub-bucketu.
3. Pokud klíč sub-bucket nevlastní a existuje volný sub-bucket, klíč jej získá jako výhradní vlastník a paket se vloží do něj.
4. Pokud není žádný volný sub-bucket, z celého flow klíče se vypočítá kvalitní, procesně stabilní a náhodně klíčovaný hash. Poslední čtyři rovnoměrně rozložené bity zvolí jeden ze 16 sub-bucketů.
5. Všechny pakety stejného přetokového toku musí při nezměněném procesním klíči zvolit stejný sub-bucket. Jednotlivé pakety se nikdy nerozmisťují náhodně.
6. Přijetí paketu musí současně projít globální, peer a novou sub-bucketovou rezervační kontrolou popsanou níže.

Hashovací klíč vznikne při startu klienta, nemusí přežít restart a nesmí být ovlivnitelný vzdáleným protějškem. Implementace nesmí předpokládat, že nízké čtyři bity nekvalitního obecného hashe jsou rovnoměrné.

## Životní cyklus vlastnictví

- Jakmile se sub-bucket vyprázdní, jeho vlastnická vazba se odstraní, deficit se resetuje a sub-bucket se stane volným.
- Pokud v neprázdném sub-bucketu nezůstává žádný paket současného vlastníka, jeho vazba se odstraní.
- První zbývající paket může určit nového výhradního vlastníka sub-bucketu, pokud jeho flow klíč již nevlastní jiný sub-bucket. Adresář a hlavička sub-bucketu se aktualizují atomicky.
- Pokud flow klíč prvního paketu již vlastní jiný sub-bucket, nesmí vzniknout duplicitní vlastnictví. Implementace musí bezpečně pokračovat bez povýšení, případně zvolit první způsobilý klíč podle jednoznačně zdokumentovaného pravidla.
- Intent vyžaduje okamžité uvolnění po vyprázdnění, nikoli časový idle timeout.
- Samotná přítomnost přetokových paketů ve frontě neznamená, že je sub-bucket volný.

## Deficit Round Robin

- Uvnitř každé peer fronty plánovat neprázdné sub-buckety pomocí Deficit Round Robin.
- Účetní jednotkou jsou bajty paketu, nikoli počet paketů.
- Každý aktivní sub-bucket dostává stejné kvantum za kolo.
- Výchozí kvantum odvodit od aktuální maximální velikosti tunelového paketu tak, aby sub-bucket s běžným plným paketem mohl při dostatečném rozpočtu odeslat přibližně jeden takový paket za kolo.
- Nevyužitý deficit se přenáší do dalších kol, aby nebyly vyhladověny větší pakety.
- Po vyprázdnění sub-bucketu deficit resetovat.
- Zachovat FIFO pořadí uvnitř každého sub-bucketu.
- Plánovač musí být work-conserving: pokud ostatní sub-buckety nemají data, jediný aktivní sub-bucket může využít celý dostupný rozpočet requestu a není omezen na jedno kvantum za celý request.
- Peer-level rotace zůstává nad touto vrstvou. Žádná peer fronta ani aktivní sub-bucket nesmí být trvale vyhladověn.

## Logická rezervace sub-bucketů

- Výchozí minimální rezervace jednoho sub-bucketu je 8 KiB, tedy 8192 bajtů.
- Hodnota není samostatným nezávislým runtime parametrem. Odvodí se jako celočíselný podíl `minPerPeerQueueBytes / 16`. Při současném výchozím `minPerPeerQueueBytes` 128 KiB vychází přesně 8 KiB.
- Případný zbytek po celočíselném dělení zůstává součástí sdílené kapacity peer fronty. Součet sub-bucketových minim nesmí překročit `minPerPeerQueueBytes`.
- Rezervace není fyzická předalokace a není maximálním limitem sub-bucketu. Aktivní sub-bucket může využít více, pokud tím neporuší rezervace ostatních sub-bucketů, peerů ani globální limit.
- Rezervační příspěvek peer fronty se počítá jako součet `max(skutečné bajty sub-bucketu, minimální rezervace sub-bucketu)` a případného nerozděleného zbytku peer rezervace.
- Prázdný nebo podlimitní sub-bucket si tak logicky zachovává prostor pro krátký burst, zatímco nevyužitá kapacita nad součtem minim zůstává sdílená.
- Těžký tok nesmí spotřebovat prostor logicky rezervovaný pro dosud prázdné nebo malé sub-buckety. Pokud je sdílená kapacita vyčerpána, další paket nad rezervaci těžkého toku se odmítne podle existujícího drop účetnictví.
- Výchozích 16 krát 8 KiB se rovná současným 128 KiB na peer, takže samotné zavedení sub-bucketových rezervací nezvyšuje minimální globální rezervační požadavek.

## Sestavení požadavku a návrat paketů

- Výběr paketů musí stále respektovat přesný limit těla požadavku, maximální počet datových záznamů a chování pro paket, který se nevejde ani do jinak prázdného platného požadavku.
- Jeden datový záznam může nadále obsahovat více paketů stejné protistrany, i když pocházejí z různých sub-bucketů.
- Plán požadavku musí uchovat dostatek údajů k vrácení každého paketu do původního peeru a původního sub-bucketu.
- Při selhání nebo neověřeném přijetí požadavku se všechny pakety vrátí tak, aby se zachovalo jejich původní pořadí a DRR stav nebyl poškozen dvojím účtováním.
- Úspěšně přijaté pakety se nevracejí.

## Limity, paměť a účetnictví

- Zachovat současný globální limit odchozí fronty, minimální rezervaci na peer a maximální počet peerů.
- Šestnáct sub-bucketů sdílí bajtový rozpočet své peer fronty. Nevytvářejí rezervaci navíc k `minPerPeerQueueBytes`, ale rozdělují její výchozí minimum.
- Výchozí 64MiB limit je společný pro celou odchozí `DirectionQueues` přes všechny peery, nikoli 64 MiB na jeden peer ani 4 MiB na jeden sub-bucket.
- Počet paketových bajtů zůstává omezen současným limitem. Implementace nesmí kopírovat paketová data jen kvůli sub-bucketům.
- Upřednostnit obnovení flow klíče z existující hlavičky paketu nebo kompaktní metadata před bezdůvodným duplikováním celé pětice u každého paketu. Zvolená reprezentace musí zachovat přesnou identitu a bezpečnost klasifikace.
- Součet bajtů a položek na sub-bucket, peer i globální úrovni musí zůstat přesný při vložení, odebrání, vrácení, zahození i změně vlastníka.
- Při překročení existujících limitů zachovat současné důvody zahození, pokud změna nevyžaduje nový přesně zdokumentovaný důvod.

## Pozorovatelnost

Doplnit pouze takovou diagnostiku, která je potřebná pro ověření chování, minimálně možnost v testech zjistit:

- počet aktivních sub-bucketů,
- vlastnické a sdílené přiřazení,
- skutečné bajty a logicky rezervované bajty sub-bucketů,
- průběh DRR výběru,
- zachování globálního, peer a sub-bucketového účetnictví.

Produkční logování nesmí vypisovat každý paket ani nekontrolovaně zvyšovat objem logů.

## Testovací a akceptační kritéria

1. Prvních nejvýše 16 současně aktivních odchozích pětic jedné protistrany získá při dostupných volných sub-bucketech samostatné vlastnictví.
2. Sedmnáctá a další pětice se stabilně mapují hashem do jednoho ze 16 sub-bucketů. Opakované pakety stejné pětice se nerozptýlí mezi více sub-bucketů.
3. Hashované přetokové toky se rozkládají mezi všech 16 sub-bucketů a nejsou soustředěny v jediné přetokové frontě.
4. Prázdný sub-bucket okamžitě ztratí vlastníka a může jej získat nová pětice.
5. Pokud zmizí všechny pakety vlastníka, ale zůstanou cizí pakety, vlastnictví lze bezpečně převést na způsobilou pětici prvního zbývajícího paketu bez duplicitního vlastnictví.
6. Dlouhý objemný tok nedokáže trvale vyhladovět sporadický tok stejné protistrany.
7. Toky s různými velikostmi paketů získávají férový podíl podle bajtů, nikoli podle počtu paketů.
8. Jediný aktivní sub-bucket dokáže využít celý dostupný request a dostupnou sdílenou kapacitu nad svou rezervací.
9. FIFO pořadí paketů v jednom sub-bucketu zůstane zachováno, včetně návratu po neúspěšném síťovém požadavku.
10. Protokoly bez portů a fragmentované IPv4 pakety jsou přijaty a použijí definovaný fallback klíč bez nebezpečného čtení hlavičky.
11. Při výchozí konfiguraci má každý sub-bucket logickou rezervaci přesně 8192 bajtů a všech 16 rezervací se rovná jedné 128KiB peer rezervaci.
12. Těžký sub-bucket může překročit 8 KiB pouze ze sdílené kapacity a nemůže spotřebovat logickou rezervaci ostatních sub-bucketů.
13. Paket pro prázdný nebo podlimitní sub-bucket lze přijmout do jeho rezervace i v situaci, kdy jiný sub-bucket vyčerpal veškerou půjčitelnou kapacitu.
14. Změna konfigurovaného `minPerPeerQueueBytes` bezpečně přepočítá sub-bucketové minimum, nerozdělený zbytek a všechny validační invarianty bez přetečení.
15. Stávající peer-level férovost, limity těla požadavku, maximální počet záznamů, drop účetnictví a globální i per-peer bajtové limity zůstanou funkční.
16. Nové jednotkové testy pokryjí alokaci všech slotů, hashovou kolizi, uvolnění, povýšení vlastníka, DRR deficit, různě velké pakety, work-conserving chování, rezervace, oversized paket, návrat po chybě a souběžné zařazování.
17. Stávající testy paketových front, scheduleru, síťového workeru a integrační přenosové testy projdou beze změny očekávaného drátového formátu.

## Realizační vodítka

- Preferovat rozšíření stávajících abstrakcí `DirectionQueues`, `PeerQueue`, `RequestScheduler` a `RequestPlan` před vytvořením paralelního systému front.
- Oddělit parsování flow klíče od plánování, aby šlo klasifikaci samostatně testovat.
- Hashovací funkci a procesní klíč zapouzdřit za testovatelným rozhraním s možností deterministického klíče v testech.
- Znovu použít princip současného logického peer reservation accounting také uvnitř peer fronty, aby sub-bucketové minimum nevytvářelo druhý nesouvisející model limitů.
- Zachovat jednoho konzumenta odchozí fronty, ale nepředpokládat, že producenti jsou jediní nebo nesouběžní.
- Nedělat změny serverového drátového protokolu, pokud testy neodhalí nevyhnutelnou potřebu. Taková potřeba by znamenala novou materiální změnu intentu.

**AI:** Intent v002 je vytvořený a ověřený společně se zdrojovým přepisem.

**User:** Jsou výchozích 64 MB celkové maximální paměti pro buckety a 128 KiB na bucket hardcodované v klientovi? Pokud server umožní například 256MB request, klient to nevyužije?

**AI:** Jde o výchozí hodnoty klienta, které může lokální runtime konfigurace přepsat. Serverový limit requestu je nezávislý a sám nezvětší klientskou frontu.

**User:** V jaké konfiguraci jsou hodnoty uvedené? Ukaž absolutní cestu a potvrď, zda jde o konfiguraci otevřenou přes Edit Config a zda jsou položky nepovinné.

**AI:** Edit Config pracuje s aktivním souborem C:\ProgramData\PHPVPN\config.json. Aktivní soubor neobsahoval sekci runtime ani obě limitní položky. Jsou nepovinné a parser použije výchozí hodnoty v paměti, ale automaticky je do JSON souboru nedopíše. Zdrojové výchozí hodnoty jsou v C:\git\phpvpn\client\src\core\config\config.h.

**User:** Alokuje se 64 MB dynamicky, nebo se při startu klienta zabere napevno?

**AI:** Paměť se alokuje dynamicky. Konstruktor si uloží pouze limit. Peer fronta vznikne s prvním paketem a po vyprázdnění se odstraní. Alokátor může uvolněnou paměť ponechat procesu pro další použití.

**User:** Přidej do intentu zdvojnásobení: místo 64 MB bude 128 MB, na každý bucket rezervace 256 KiB a na každý sub-bucket rezervace 16 KiB.

**AI:** Poměry jsou konzistentní, ale současná peer rezervace je sdílené runtime nastavení pro odchozí i příchozí frontu. Dosavadní intent měnil jen odchozí směr.

**User:** Co je lepší a bezpečnější?

**AI:** Při potřebě kompatibility by bylo bezpečnější měnit pouze odchozí směr a příchozí chování ponechat.

**User:** Klient ani server ještě nejsou nikde nasazené ani nainstalované. Jaké změny jsou tedy vhodné?

**AI:** Bez nasazení lze čistě oddělit příchozí a odchozí limity bez migrační logiky.

**User:** Kdybychom stejné sub-buckety dali také do příchozí fronty, mělo by to benefit?

**AI:** Ano. Velký příchozí tok může ve společném FIFO blokovat interaktivní provoz stejného peeru. Příchozí sub-buckety přidají férové střídání před zápisem do tunelu. Při 16KiB rezervaci je vhodné zvýšit také příchozí globální limit na 128 MiB a peer rezervaci na 256 KiB.

**User:** Udělejme to i pro příchozí pakety. Zavedeme sub-buckety symetricky. Zpracuj nový intent.

## Pokračování konverzace po vytvoření v003

**AI-created artifact:** Po dokončení třetí verze byl uživateli zobrazen odkaz na následující úplný intent:

---
intent_version: 3
created_at: 2026-09-18-20-25-34
based_on: intent-v002.md
source: intent-chat-v003.md
---

# Symetrický flow fair queuing v klientských paketových frontách

## Shrnutí

Rozšířit odchozí i příchozí klientské paketové fronty o druhou úroveň plánování. Každá peer fronta bude obsahovat přesně 16 sub-bucketů. Aktivní síťové toky budou podle možností dostávat výhradní sub-bucket a všechny neprázdné sub-buckety budou obsluhovány pomocí Deficit Round Robin podle bajtů.

V obou směrech bude výchozí globální limit 128 MiB, logická peer rezervace 256 KiB a logická rezervace jednoho sub-bucketu 16 KiB. Paměť se bude nadále alokovat dynamicky podle skutečně zařazených paketů. Cílem je zabránit tomu, aby objemný tok, například FTP přenos nebo velké stahování, vyhladověl interaktivní či méně objemné toky stejné protistrany v kterémkoli směru.

## Kontext projektu

- `DirectionQueues` v `client/src/core/queues/packet_queues.h` a `client/src/core/queues/packet_queues.cpp` dnes ukládá odchozí i příchozí pakety podle IP adresy protistrany.
- `RequestScheduler::planNextRequest` v `client/src/core/queues/scheduler.cpp` rotuje odchozí peer fronty při sestavování datového requestu.
- `TunnelOutputWorker::run` v `client/src/core/runtime/tunnel_workers.cpp` dnes obslouží při jednom průchodu jeden příchozí paket z každé neprázdné peer fronty, ale uvnitř peer fronty používá společné FIFO.
- `NetworkWorker::exchangeData` v `client/src/core/runtime/network_worker.cpp` vrací po neúspěšném requestu naplánované odchozí pakety zpět do fronty.
- `Ipv4PacketView` v `client/src/core/protocol/ipv4.h` dnes obsahuje adresy a délky. Pro flow klasifikaci je nutné bezpečně doplnit protokol, fragmentaci a TCP/UDP porty.
- Současné výchozí hodnoty jsou 64 MiB pro každou směrovou globální frontu a jedna společná 128KiB peer rezervace. Projekt dosud není nikde nasazený ani nainstalovaný, proto není požadována zpětná kompatibilita starého konfiguračního schématu.

## Požadované výchozí limity

Použít tyto nové výchozí hodnoty:

- `maxOutboundQueueBytes`: 134217728 bajtů, tedy 128 MiB,
- `maxInboundQueueBytes`: 134217728 bajtů, tedy 128 MiB,
- odchozí minimální peer rezervace: 262144 bajtů, tedy 256 KiB,
- příchozí minimální peer rezervace: 262144 bajtů, tedy 256 KiB,
- minimální rezervace jednoho sub-bucketu v obou směrech: 16384 bajtů, tedy 16 KiB.

Rezervace jsou logické podmínky přijetí, nikoli předem fyzicky alokovaná paměť. Globální limity počítají paketové bajty. Skutečná procesní paměť může být vyšší o kontejnery, metadata, alokátor a dočasná přenosová těla.

## Konfigurační model

- Nahradit současnou společnou peer rezervaci dvěma jednoznačnými směrovými hodnotami, například `minOutboundPerPeerQueueBytes` a `minInboundPerPeerQueueBytes`.
- Staré pole `minPerPeerQueueBytes` není nutné zachovat ani migrovat, protože neexistuje produkční ani instalované nasazení.
- Všechna limitní pole zůstanou v sekci `runtime` nepovinná. Pokud chybí celá sekce nebo jednotlivé pole, parser použije nové výchozí hodnoty v paměti, aniž by je automaticky zapisoval do JSON souboru.
- Průvodce, IPC kontext, výchozí dokument, validátor, diagnostika a testy musí používat směrové názvy konzistentně.
- Pro každý směr ověřit bez přetečení invariant `maxQueueBytes >= maxRemotePeers * minPerPeerQueueBytes`.
- Sub-bucketová rezervace nebude třetím nezávislým konfiguračním polem. Odvodí se jako celočíselný podíl směrové peer rezervace dělený 16. Při nových výchozích hodnotách vychází přesně 16 KiB.
- Případný zbytek po celočíselném dělení zůstane sdílenou kapacitou peer fronty. Součet minim sub-bucketů nesmí překročit směrovou peer rezervaci.

## Rozsah

- Zavést stejný model 16 sub-bucketů v odchozí i příchozí `DirectionQueues`.
- Odchozí plánování probíhá před sestavením, zašifrováním a odesláním datového requestu.
- Příchozí plánování probíhá po ověření, dešifrování a validaci paketu před jeho zápisem do tunelu.
- Neměnit serverový drátový protokol, formát datových záznamů, kryptografii ani směrovací význam peer front.
- Férovost je mezi síťovými toky, nikoli mezi procesy. Aplikace používající více pětic může získat více podílů než aplikace s jedinou pěticí.

## Identita toku

### Běžný TCP a UDP

Tok je určen směrovou pěticí:

1. zdrojová IPv4 adresa,
2. cílová IPv4 adresa,
3. zdrojový port,
4. cílový port,
5. číslo IP protokolu.

Pětice se nekanonizuje mezi směry. Odchozí a příchozí směr mají oddělené fronty a vlastnictví.

### Fragmenty a protokoly bez portů

- Pro fragmentované IPv4 pakety a protokoly, u kterých nelze bezpečně přečíst TCP/UDP porty, použít fallback klíč tvořený zdrojovou adresou, cílovou adresou a protokolem, s oběma porty nastavenými na nulu.
- Všechny fragmenty odpovídající fallbacku musí projít stejnou klasifikací. Intent nevyžaduje fragmentovou mapovací cache.
- Neznámý podporovaný IP protokol se nesmí zahodit jen proto, že nemá porty.
- Čtení hlaviček musí být délkově bezpečné a nesmí číst porty z neúplného nebo nenultého fragmentu.

## Struktura šestnácti sub-bucketů

Každá peer fronta v každém směru obsahuje právě 16 sub-bucketů. Každý sub-bucket má:

- FIFO posloupnost paketů,
- volitelného výhradního vlastníka reprezentovaného flow klíčem,
- počet uložených bajtů a položek,
- logickou minimální rezervaci odvozenou ze směrové peer rezervace, výchozích 16 KiB,
- DRR deficit v bajtech,
- metadata potřebná pro bezpečné vrácení odchozího paketu a změnu vlastnictví.

Peer fronta udržuje omezený adresář vlastnických vazeb `flow key -> sub-bucket`. V adresáři jsou pouze aktuální výhradní vlastníci, nejvýše 16 položek. Přetokové flow klíče nepotřebují neomezenou stavovou mapu.

Implementace nemá bezdůvodně duplikovat celou pětici u každého paketu. Preferovat její bezpečné obnovení z existující hlavičky nebo kompaktní reprezentaci, pokud zachová přesnou identitu a přijatelné časové náklady.

## Zařazení paketu

Zařazení odchozího i příchozího paketu proběhne atomicky pod synchronizací příslušné `DirectionQueues`:

1. Paket se bezpečně klasifikuje na flow klíč.
2. Pokud klíč vlastní sub-bucket, paket se vloží na jeho konec.
3. Pokud klíč sub-bucket nevlastní a existuje volný sub-bucket, klíč jej získá jako výhradní vlastník.
4. Pokud není žádný volný sub-bucket, z celého flow klíče se vypočítá kvalitní, procesně stabilní a náhodně klíčovaný hash. Poslední čtyři rovnoměrně rozložené bity zvolí jeden ze 16 sub-bucketů.
5. Všechny pakety stejného přetokového toku musí při nezměněném procesním klíči zvolit stejný sub-bucket. Pakety se nikdy nerozmisťují náhodně jednotlivě.
6. Přijetí paketu musí projít globální, peer a sub-bucketovou rezervační kontrolou pro příslušný směr.

Hashovací klíč vznikne při startu klienta, nemusí přežít restart a nesmí být ovlivnitelný vzdálenou protistranou. Implementace nesmí spoléhat na nízké čtyři bity nekvalitního obecného hashe.

## Životní cyklus vlastnictví

- Jakmile se sub-bucket vyprázdní, jeho vlastnická vazba se odstraní, deficit se resetuje a sub-bucket se stane volným.
- Pokud v neprázdném sub-bucketu nezůstává žádný paket současného vlastníka, jeho vazba se odstraní.
- První zbývající paket může určit nového vlastníka, pokud jeho flow klíč již nevlastní jiný sub-bucket v téže směrové peer frontě.
- Adresář a hlavička sub-bucketu se aktualizují atomicky.
- Nesmí vzniknout duplicitní vlastnictví jednoho flow klíče. Pokud první kandidát již vlastní jiný sub-bucket, implementace bezpečně pokračuje bez povýšení nebo vybere první způsobilý klíč podle zdokumentovaného pravidla.
- Vlastnictví se uvolňuje okamžitě po vyprázdnění, bez idle timeoutu.
- Sub-bucket obsahující přetokové pakety není volný jen proto, že v něm nezůstaly pakety původního vlastníka.

## Deficit Round Robin

### Společná pravidla

- Uvnitř každé peer fronty plánovat neprázdné sub-buckety pomocí Deficit Round Robin.
- Účetní jednotkou jsou bajty paketu, nikoli počet paketů.
- Každý aktivní sub-bucket dostává stejné kvantum za kolo.
- Kvantum odvodit od aktuální maximální velikosti tunelového paketu tak, aby sub-bucket s běžným plným paketem mohl při dostatečném rozpočtu odeslat přibližně jeden takový paket za kolo.
- Nevyužitý deficit se přenáší do dalších kol. Po vyprázdnění sub-bucketu se deficit resetuje.
- Zachovat FIFO pořadí uvnitř každého sub-bucketu.
- Plánování musí být work-conserving. Pokud ostatní sub-buckety nemají data, jediný aktivní tok může využít veškerou aktuálně dostupnou přenosovou kapacitu.
- Peer-level rotace zůstává nad sub-bucketovou vrstvou. Žádný aktivní peer ani sub-bucket nesmí být trvale vyhladověn.

### Odchozí směr

- `RequestScheduler` vybírá pakety z odchozích sub-bucketů v DRR kolech a stále respektuje přesný limit těla requestu a maximální počet datových záznamů.
- Jeden datový záznam může obsahovat pakety stejné protistrany z více sub-bucketů.
- Paket, který se nevejde ani do jinak prázdného platného requestu, se zpracuje stávajícím oversized chováním bez zablokování ostatních toků.

### Příchozí směr

- `TunnelOutputWorker` vybírá z každé příchozí peer fronty pakety pomocí stejného DRR principu místo společného FIFO.
- Úspěšný zápis do tunelu odstraní paket definitivně.
- Selhání zápisu zachová stávající chování čítačů, drop důvodu a zastavení workeru. Změna nesmí zavést nekonečné opakování vadného paketu.
- Příchozí plánovač nemá limit HTTP requestu, ale musí zůstat work-conserving a nesmí blokovat peer-level střídání.

## Logické rezervace

- Výchozí minimální rezervace sub-bucketu je v obou směrech 16384 bajtů.
- Rezervační příspěvek peer fronty se počítá jako součet `max(skutečné bajty sub-bucketu, minimum sub-bucketu)` a případného nerozděleného zbytku peer rezervace.
- Prázdný nebo podlimitní sub-bucket si logicky zachovává prostor pro krátký burst.
- Rezervace není fyzická předalokace ani tvrdý maximální limit. Aktivní sub-bucket může použít více, pokud neporuší rezervace ostatních sub-bucketů, peerů ani globální směrový limit.
- Těžký tok nesmí spotřebovat prostor logicky rezervovaný pro prázdné nebo malé sub-buckety.
- Pokud je sdílená kapacita vyčerpána, další paket nad rezervaci těžkého toku se odmítne podle konzistentního drop účetnictví.
- Výchozích 16 krát 16 KiB se rovná výchozí 256KiB peer rezervaci. Sub-buckety tedy nerozšiřují rezervaci nad směrovou peer hodnotu, ale rozdělují ji.

## Návrat odchozích paketů po chybě

- Odchozí plán requestu musí uchovat původní peer a sub-bucket každého vybraného paketu.
- Při selhání nebo neověřeném přijetí requestu se pakety vrátí do původních sub-bucketů v původním pořadí.
- Vrácení nesmí poškodit vlastnictví, bajtové účetnictví ani DRR deficit dvojím započítáním.
- Úspěšně přijaté pakety se nevracejí.

## Paměť a výkon

- Globální limity se při startu fyzicky nealokují. Konstrukce front uloží číselné limity a prázdné kontejnery.
- Peer a paketová úložiště vznikají dynamicky při prvním přijetí a po vyprázdnění se mohou odstranit. Alokátor může uvolněnou paměť ponechat procesu pro další použití.
- Paketová data se při přesunu mezi úrovněmi front nesmí zbytečně kopírovat.
- Režie nejvýše 16 sub-bucketů na peer je pevně omezená. Operace, která projde všech 16 položek, je z pohledu asymptotiky konstantní.
- Parsování flow klíče a DRR nesmí provádět neomezené skeny celé fronty na každý paket. Pokud je pro změnu vlastníka nutný omezený scan, musí být zdokumentovaný a testovaný proti patologickému zatížení.
- Diagnostika musí odděleně uvádět skutečně obsazené bajty, směrový limit, počet peer front a podle potřeby počet aktivních sub-bucketů. Logické rezervace se nesmí prezentovat jako fyzicky alokovaná RAM.

## Testovací a akceptační kritéria

1. Výchozí odchozí i příchozí globální limit je přesně 134217728 bajtů.
2. Výchozí odchozí i příchozí peer rezervace je přesně 262144 bajtů.
3. Výchozí rezervace každého z 16 sub-bucketů je přesně 16384 bajtů a jejich součet se rovná peer rezervaci.
4. Chybějící sekce `runtime` nebo jednotlivá limitní pole použijí nové výchozí hodnoty v paměti bez jejich automatického zápisu do JSON.
5. Validace každého směru odmítne konfiguraci, jejíž globální limit nepokrývá počet možných peerů krát směrovou peer rezervaci, a je bezpečná proti celočíselnému přetečení.
6. Prvních nejvýše 16 současně aktivních pětic jedné protistrany získá při dostupných volných sub-bucketech samostatné vlastnictví v obou směrech.
7. Sedmnáctá a další pětice se stabilně mapují hashem do jednoho ze 16 sub-bucketů. Pakety stejného toku se nerozptýlí náhodně.
8. Hashované přetokové toky se mohou rozložit mezi všech 16 sub-bucketů a nejsou soustředěny v jediné přetokové frontě.
9. Prázdný sub-bucket okamžitě ztratí vlastníka. Neprázdný sub-bucket bez paketů původního vlastníka lze bezpečně převést bez duplicitního vlastnictví.
10. Dlouhý objemný odchozí tok nevyhladoví sporadický odchozí tok stejné protistrany.
11. Dlouhý objemný příchozí tok nevyhladoví sporadický příchozí tok stejné protistrany před zápisem do tunelu.
12. Toky s různými velikostmi paketů získávají férový podíl podle bajtů, nikoli podle počtu paketů.
13. Jediný aktivní sub-bucket může v obou směrech využít volnou sdílenou kapacitu a dostupnou přenosovou kapacitu.
14. Těžký sub-bucket nemůže spotřebovat logickou rezervaci ostatních sub-bucketů. Paket prázdného nebo podlimitního sub-bucketu lze přijmout do jeho rezervace i po vyčerpání půjčitelné kapacity.
15. FIFO pořadí uvnitř sub-bucketu zůstane zachované, včetně návratu odchozích paketů po neúspěšném requestu.
16. Fragmentované IPv4 pakety a protokoly bez portů použijí fallback bez nebezpečného čtení hlavičky.
17. Stávající peer-level férovost, drop účetnictví, request limity, odezvové limity a drátový formát zůstanou funkční.
18. Jednotkové testy pokryjí klasifikaci, všech 16 vlastníků, hashovou kolizi, uvolnění, převzetí vlastníka, DRR deficit, různě velké pakety, rezervace, work-conserving chování, oversized odchozí paket, návrat po chybě, příchozí zápis do tunelu a souběžné producenty.
19. Integrační test prokáže, že objemný tok v každém směru neblokuje malý interaktivní tok stejného peeru.
20. Stávající testy konfigurace, paketových front, scheduleru, síťového workeru, tunnel workeru a integračního přenosu projdou po aktualizaci očekávaných výchozích hodnot.

## Realizační vodítka

- Rozšířit společné abstrakce `DirectionQueues` a `PeerQueue`, aby obě směrové instance používaly stejnou bezpečnou klasifikaci, vlastnictví, hashování, rezervace a DRR stav.
- Zachovat samostatné konzumenty: `RequestScheduler` pro odchozí směr a `TunnelOutputWorker` pro příchozí směr.
- Oddělit parsování flow klíče od plánování a samostatně je testovat.
- Hashovací funkci a procesní klíč zapouzdřit za testovatelným rozhraním s deterministickým klíčem v testech.
- Aktualizovat výchozí konfiguraci, validátor, editorový dokument, průvodce, IPC kontext, diagnostiku a dokumentaci jako jeden konzistentní celek.
- Nedělat změny serveru ani drátového protokolu, pokud testy neodhalí nevyhnutelnou potřebu. Taková potřeba by byla novou materiální změnou intentu.

**AI:** Intent v003 je dokončený a ověřený. Obsahuje symetrických 16 sub-bucketů a DRR v obou směrech, 128MiB globální limit, 256KiB peer rezervaci a 16KiB rezervaci sub-bucketu.

**User:** Co se stane, když aplikace posílá 20 MB/s, ale klient dokáže odesílat na server pouze 10 MB/s? Zaplní se fronta přibližně za dvanáct sekund a po zastavení se stejně dlouho vyprazdňuje?

**AI:** Ano. Fronta roste čistě o 10 MB/s, takže 128MB limit se zaplní přibližně za 12,8 sekundy. Po zastavení zdroje se při 10 MB/s podobně dlouho vyprazdňuje. Po zaplnění se přebytek zahazuje. DRR chrání ostatní toky, ale neodstraňuje zpoždění přetíženého toku; paket na konci může čekat přibližně třináct sekund.

**User:** Mohli bychom si u každého IP paketu zapamatovat čas zařazení a po konfigurovatelné době, například pěti sekundách, staré pakety před odesláním zahodit? Původně jsem navrhl zahodit také všechny pakety za prvním starým paketem.

**AI:** Časový limit je vhodný proti bufferbloatu. Ve FIFO jsou ale pakety za nejstarším paketem novější, ne starší. Má se proto odstranit jen souvislý expirovaný prefix do prvního čerstvého paketu. Použije se monotónní čas, kontrola před výběrem a při kapacitním tlaku. Při návratu po neúspěšném requestu se čas nesmí resetovat. Výchozí hromadné vyprázdnění celého sub-bucketu se nedoporučuje.

**User:** Souhlasím. Odstraní se jen pakety starší pěti sekund. Pět sekund bude výchozí konfigurace, kterou uživatel může změnit.

**AI:** Pravidlo je symetrické pro oba směry. Nepovinný runtime parametr bude v milisekundách, výchozí hodnota 5000, musí být kladná a změna se projeví po novém načtení konfigurace.

**User:** Zpracuj intent.

**AI:** Intent v004 je hotový. Doplňuje výchozí maximální stáří 5000 ms, nepovinný runtime parametr, zahazování pouze expirovaného prefixu, úklid při kapacitním tlaku, zachování původního času po neúspěšném requestu a testy i čítače pro oba směry.

**User:** Je v intentu zapsáno, že se musí upravit dokumentace, například `protocol.md` nebo `client.md`, kde jsou popsané konfigurační parametry, buckety a celé předávání paketů?

**AI:** Ano, ale ve v004 jen obecně. Pro realizační specifikaci je potřeba výslovně požadovat aktualizaci `client.md` o konfiguraci, výchozí hodnoty, sub-buckety, DRR, rezervace, expiraci a čítače. `protocol.md` se musí zkontrolovat a aktualizovat tak, aby bylo jasné, že se nemění drátový protokol a interní klientské plánování je popsáno v `client.md`.

**User:** Dobře, udělej novou verzi intentu.

