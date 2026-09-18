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
