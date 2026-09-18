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
