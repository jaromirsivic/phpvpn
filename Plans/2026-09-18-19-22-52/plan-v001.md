---
plan_version: 1
created_at: 2026-09-18-23-43-00
source: ../../Intents/2026-09-18-19-22-52/intent-v005.md
---

# Implementační plán symetrického flow fair queuing

## 1. Autorita, rozsah a výchozí stav

Autoritativní zadání je intent-v005.md z 18. 9. 2026, SHA-256 B786F71A481FD94D8E027F59333A64C348085942179DE1B34B4017B1E33E4FC0. Tento plán je úplnou první neměnnou verzí. Nemění intent a sám neopravňuje k zahájení realizace.

Plán vznikl nad aktivní větví tray_UX, HEAD c3a955d26fa4b5af06574cbec17074fa15ef880c, s přihlédnutím k pracovnímu stromu. Realizátor pracuje přímo v aktivní feature větvi. Nevytváří worktree ani další agenty bez výslovného zadání. Při změně výchozího kódu nejprve porovná relevantní oblasti, nikoli automaticky přepíše rozpracovanou práci.

Při plánování byly již rozpracovány soubory .intent-builder-active, grafické assety, client/tests/CMakeLists.txt, instalační a odinstalační skripty a nové package testy. Zvláště změny v client/tests/CMakeLists.txt je nutné při realizaci zachovat. Historické Intents, Plans a grafické soubory nejsou předmětem implementace.

Potvrzený deadline této plánovací práce je 2026-09-19 02:30:11 +02:00. Nejde o automaticky zděděný termín budoucí realizace. Při dosažení termínu by plánovací agentka zastavila práci a uložila bod pokračování bez vydávání neúplného plánu za hotový. Tento dokument je dokončený plán vytvořený před termínem.

### Výsledek realizace

Oba směry používají společnou implementaci DirectionQueues s přesně 16 sub-buckety na peer, omezeným adresářem vlastníků, flow klasifikací, rezervacemi, monotónní expirací a DRR podle bajtů. RequestScheduler a TunnelOutputWorker zůstávají samostatnými konzumenty. Peer-level rotace zůstává nad DRR.

Server, drátové datové struktury, pořadí autentizačních řádků, AES-GCM, HMAC autentizace protokolu, serverová TTL a význam accepted/deferredMessageIds se nemění. Nový klíč pro lokální flow hash je oddělený od autentizačních a payload klíčů.

## 2. Ověřené oblasti kódu a jejich dopad

Řádky uvádějí orientační stav při plánování. Před editací je znovu ověř přes Graft podle AGENTS.md. Graft map a cílené ask, grep a skeleton byly při plánování použity před čtením zdrojů.

| Oblast | Ověřený výchozí bod | Změna |
| --- | --- | --- |
| Runtime konfigurace | client/src/core/config/config.h:26–34, 63–72 | Nové směrové konstanty, dvě peer minima a maxQueuedPacketAgeMs |
| Validace | client/src/core/config/config_validator.cpp:52–68, 106–116, 216–265, 298–317 | Povolené klíče, kladná int64, oddělené násobení rezervací bez přetečení |
| Editor a průvodce | client/src/core/config/config.h:38–57, config_defaults.cpp:4, config_document.cpp:48–66 | Zachovat výchozí dokument bez runtime, výchozí hodnoty jen v paměti, správné runtime hodnoty ve WizardContext |
| Wizard IPC | client/src/core/ipc/wizard_ipc.cpp:273–309 | Dva směrové názvy, TTL, serializace, strict shape a oba validační invarianty |
| IPv4 | client/src/core/protocol/ipv4.h:76–86, ipv4.cpp:194–231 | Bezpečně zpřístupnit protokol, fragmentaci a porty pro klasifikaci |
| Fronty | client/src/core/queues/packet_queues.h:31–131, packet_queues.cpp:68–232 | Nový model front, atomické přijetí/výběr, účetnictví, expirace, ownership, DRR a lease |
| Scheduler | client/src/core/queues/scheduler.h:25–43, scheduler.cpp:60–182 | Metadata výběru, společné DRR, přesný rozpočet, zachování rotace peerů |
| Tvorba payloadu | client/src/core/protocol/payload_batch.cpp:17–52, network_worker.cpp:175–229 | Nekopírující pohledy na vybrané pakety bez přenosu lokálních metadat na drát |
| NetworkWorker | client/src/core/runtime/network_worker.cpp:231–413, 498–510 | Inbound enqueue, commit/rollback všech návratových cest, částečné přijetí a prázdná kontrolní fronta |
| Tunnel workers | client/src/core/runtime/tunnel_workers.cpp:63–112, 155–197 | Zachovat validaci vstupu, výstup přepnout na DRR, zachovat chování při chybě zápisu |
| Životní cyklus | client/src/core/runtime/vpn_engine.cpp:45–77, 338–480, 510–543 | Procesní hashovací klíč, hodiny, směrové limity, životnost front a tokenů |
| Diagnostika | client/src/core/runtime/status_snapshot.h:65–70, vpn_engine.cpp:773–816, ipc_protocol.cpp:46–54, 241–281 | Skutečné bajty, rozpracované bajty, aktivní buckety, expirace celkem a po směrech |
| Testy | client/tests/unit/test_queues.cpp, test_runtime_status.cpp, test_config.cpp, test_wizard_ipc.cpp, test_ipc.cpp, test_payload_batch.cpp | Aktualizace stávajících testů a nové cílené sady |
| Integrační adaptéry | client/tests/contract/test_server_contract.cpp:204–249, client/tests/e2e/service_e2e.cpp:259–393 | Nové dependency front a metadata, zachovat existující roundtrip |
| Build | client/src/CMakeLists.txt, client/tests/CMakeLists.txt | Registrace nových produkčních/testovacích souborů |
| Dokumentace | client.md, protocol.md, client/tests/e2e/README.md a relevantní příklady | Povinná konzistence s finálním chováním |

Nové soubory: client/src/core/queues/flow_key.h a flow_key.cpp pro identitu a hash, podle velikosti implementace interní helper packet_queue_drr.h pro DRR. Nové testy: client/tests/unit/test_flow_key.cpp, test_queue_expiry.cpp, test_queue_concurrency.cpp a test_tunnel_queues.cpp. Nový integrační scénář lze dát do client/tests/unit/test_flow_fairness_integration.cpp a připojit k existujícímu testovacímu executable. Název adresáře unit zde neznamená, že test nebude skládat skutečné produkční komponenty.

Při průzkumu Graft nenašel žádné skutečné volání canAdmitAnotherPacket. Metoda dnes existuje jako příprava pro pre-poll backpressure. Tento plán nepřidává samostatný dosud nedodaný B04 backpressure mechanismus. Metodu je však nutné přizpůsobit novému účetnictví a otestovat, aby nezůstala zavádějící.

## 3. Uzavřená implementační rozhodnutí

### 3.1 Konfigurace a životní cyklus

Použít přesně tyto veřejné JSON názvy:

| Pole runtime | Typ a jednotka | Default |
| --- | --- | --- |
| maxOutboundQueueBytes | kladné int64, bajty | 134217728 |
| maxInboundQueueBytes | kladné int64, bajty | 134217728 |
| minOutboundPerPeerQueueBytes | kladné int64, bajty | 262144 |
| minInboundPerPeerQueueBytes | kladné int64, bajty | 262144 |
| maxQueuedPacketAgeMs | kladné int64, milisekundy | 5000 |

Ostatní runtime pole a jejich defaulty zachovat. Zejména maxResponseBodyBytes a maxNdjsonLineBytes zůstávají 67108864. Není přípustná plošná náhrada všech čísel 67108864 v projektu.

Staré veřejné minPerPeerQueueBytes se odstraní z povoleného schématu a je odmítnuto jako neznámé pole. Nezavádět alias, migraci ani automatický přepis uloženého JSON. Interní společná DirectionQueues může mít obecně pojmenované peerMinimumBytes, ale nesmí znovu exportovat starý veřejný klíč.

Použít existující optionalPositiveInteger. Odmítnout 0, záporné číslo, desetinné číslo, bool, string, null a hodnotu mimo int64. TTL nemá režim vypnuto. Pro každý směr zvlášť ověřit N * P <= G kontrolovaným násobením nebo dělením před násobením. N je config.maxRemotePeers(), nikoli počet nakonfigurovaných kontraktů.

M = P / 16, R = P % 16. Sub-bucket minimum M není konfigurační pole. P menší než 16 zůstává platné kladné nastavení, výsledkem je M = 0 a kladný zbytek R. Nezavádět v rozporu s intentem novou minimální hodnotu P = 16.

Výchozí editorový dokument již runtime neobsahuje. Zachovat tuto vlastnost, upravit jeho doprovodný popis a testovat, že načtení i běžná úprava identit/kontraktů nepřidá defaultní runtime pole. Explicitně uvedená runtime pole se zachovají.

Wizard IPC context je naproti tomu úplná materializovaná struktura. Po změně obsahuje deset číselných runtime členů místo osmi. Serializátor a strict parser měnit společně. Runtime hodnoty pocházejí z autorizovaného dokumentu služby, nikoli z klientem podstrčeného kontextu.

TTL se předává nově vytvořeným frontám společně s ostatními runtime limity. Pouhé uložení konfigurace za běhu nemění aktivní fronty. Nový Connect načte validní konfiguraci, recovery stejné epochy zachovává aktivní konfiguraci. Testovat se stávajícím lifecycle, nezavádět samostatný živý TTL setter.

### 3.2 Flow klíč a hash

FlowKey má přesnou rovnost zdrojová IPv4, cílová IPv4, zdrojový port, cílový port, protokol. Adresy ani porty nekanonizovat mezi směry. Pro hash serializovat explicitně 13 bajtů v síťovém pořadí, bez paddingu C++ struktury.

Rozšířit výsledek bezpečné IPv4 validace. Protokol číst z bajtu 9, fragmentové pole z bajtů 6–7. Jestliže MF = 1 nebo fragment offset != 0, nastavit oba porty na nulu i u prvního fragmentu. Samotný DF není důvodem k fallbacku.

U nefragmentovaného TCP/UDP načíst porty jen tehdy, je-li v rozsahu IPv4 Total Length celá minimální transportní hlavička. Pro TCP alespoň 20 bajtů a platný data offset v dostupné délce. Pro UDP alespoň 8 bajtů a délka hlavičky odpovídající dostupnému datagramu. Není-li hlavička dostatečná pro bezpečnou klasifikaci, použít fallback s nulovými porty. Klasifikace nepřidává nové odmítání jinak validního IPv4 paketu. Kontrola transportního checksumu není součástí této změny. Neznámé protokoly mají stejný fallback.

Oddělit validateIpv4Packet od získání FlowKey z validovaného pohledu. Na hranici veřejného enqueue nesmí nevalidní testovací nebo omylem předaný paket způsobit čtení mimo buffer. Při vadném IPv4 zachovat invalid_packet.

Pro fallback použít HMAC-SHA-256 nad přesnou serializací FlowKey s novým 32bajtovým náhodným procesním klíčem přes existující CryptoProvider. Index je poslední bajt digestu & 0x0f. Je to konkrétní volba kvalitního klíčovaného hashe, která nepřidává nový kryptografický algoritmus ani externí knihovnu. Nepoužívat std::hash nebo náhodný výběr pro každý paket.

Klíč vytvořit jednou při startu služby/klienta přes CryptoProvider::randomBytes, uložit ve vymazatelném SecureBuffer v objektu se životností procesu a sdílet mezi oběma směry i reconnecty. Konstruktor front přijímá FlowHasher závislost, testy dodají pevný klíč nebo stub pro vynucenou kolizi. Selhání inicializace klíče je chyba startu. Selhání HMAC při enqueue se nesmí změnit na neklíčovaný hash. Změnit výsledek tryEnqueue na Result<bool>: false s DropReason pro běžné odmítnutí, Error pro interní kryptografickou chybu. Interní chybu worker eviduje bezpečně a ukončí příslušnou běhovou cestu podle existujícího chování pro interní chyby. Bez obsahů paketů a bez klíče v logu.

Hash je nutný pouze při obsazení všech 16 slotů a nepřítomnosti vlastníka. Většina enqueue pro existující vlastníky provede nejvýše 16 přesných porovnání. Výkon HMAC pod tlakem bude změřen, případná změna algoritmu je samostatné technické rozhodnutí v další verzi plánu, ne tichá změna této implementace.

### 3.3 Paket, peer a vlastník

QueuedPacket drží přesunutý PacketBytes, uint64 monotónní čas zařazení v ms, uint64 pořadové číslo prvního přijetí a kompaktní index původního bucketu. FIFO i lease používají std::list<QueuedPacket> se stejným alokátorem, takže přesun uzlu a návrat přes splice nealokují ani nekopírují paketová data. Celá pětice se u každého paketu neukládá. Při srovnání s vlastníkem se obnoví z neměnné hlavičky, což je omezená práce nad IPv4/transportní hlavičkou.

PeerQueue obsahuje std::array<SubBucket, 16>, kruhový kurzor DRR a aktuální DRR návštěvu. Každý sub-bucket má FIFO, waiting bytes/items, held bytes/items pro rozpracovaný request, volitelného vlastníka, ownerPacketCount, deficit a příznak začátku/pokračování návštěvy.

Adresář vlastníků představují přímo prohledávané sloty jako jednoznačný zdroj pravdy, nejvýše 16 vazeb. Nevytvářet mapu všech přetokových flow klíčů. Zvolená implementace používá přímo volitelné vlastníky v 16 slotech jako adresář, čímž odpadá druhá kopie vazeb. Všechny změny probíhají pod queue mutexem.

Přijetí probíhá pod jedním zámkem DirectionQueues:
1. Bezpečně klasifikovat, najít vlastníka.
2. Jinak vybrat nejnižší index prázdného FIFO jako nový vlastní slot.
3. Neexistuje-li prázdný slot, použít stabilní HMAC fallback.
4. Spočítat kandidátní účetnictví, případně uklidit expirace a znovu určit slot. Úklid mohl uvolnit slot nebo změnit vlastníka.
5. Předem připravit potřebný list uzel, peer strukturu a kapacitu heapu. Teprve při úspěšném přijetí publikovat vlastnictví, paket, čas, index expirace a účetnictví. Zamítnutý paket ani výjimka při alokaci nesmějí zanechat prázdného vlastníka nebo změněné bajty.

Čas odečíst uvnitř zámku při skutečném prvním přijetí, aby souběžní producenti nevložili starší timestamp za novější. Pořadové číslo sdílet v rámci směrové instance. Čas a pořadí se při retry nemění.

ownerPacketCount počítá pouze čekající pakety aktuálního vlastníka. Enqueue/dequeue/expiry/rollback jej aktualizují přes přesné porovnání klíče. Po dosažení nuly vazbu odstranit okamžitě. Při prázdném FIFO vždy odstranit vlastníka a resetovat deficit, i když pro starý request existuje held kapacita. Held bajty tedy drží místo v rozpočtu, nikoli výhradní flow vlastnictví.

Převzetí neprázdného bucketu řešit omezeným pravidlem:
- Kandidát je výhradně první čekající paket.
- Pokud jeho klíč vlastní jiný bucket, neprovést povýšení.
- Pokud ve FIFO zbývá více než 64 paketů, zatím ponechat bucket bez vlastníka.
- Při nejvýše 64 paketech spočítat přesný počet kandidátových paketů v celém FIFO a atomicky povýšit kandidáta.
- Znovu zkusit povýšení při následné změně čela nebo při rollbacku, pokud bucket nemá vlastníka.

Limit 64 není runtime pole. Toto pravidlo využívá povolenou volitelnost povýšení a zaručuje nejvýše 64 klasifikací na jednu změnu vlastníka, místo skenu dlouhého backlogu. Neprázdný bucket bez vlastníka není volný. Nenulový ownerPacketCount je přesný a díky němu je uvolnění vlastníka okamžité bez skenu.

Vyprázdněný peer odstranit pouze pokud nemá waiting ani held pakety. Tím zůstává možné vracet tokeny do původních indexů, aniž se fyzicky předalokují peery pro celý prefix.

### 3.4 Přesné logické rezervace a rozpracované requesty

Pro směr definovat:
- G = globální limit, N = maximální počet peerů, P = směrové minimum peeru.
- M = P / 16, R = P % 16.
- x[p,b] = waitingBytes[p,b] + heldBytes[p,b].
- Příspěvek existujícího peeru C[p] = R + sum(max(x[p,b], M)).
- Celková logická spotřeba L = N * P + sum přes existující peery a buckety max(0, x[p,b] - M).
- Skutečné paketové bajty A = sum x[p,b].

Musí platit A <= G a L <= G. Nevytvoření peerové struktury neruší její logickou rezervaci. R je součástí sdíleného příspěvku peeru podle vzorce intentu, nikoli další konfigurovatelné minimum bucketu. Nepřidávat R podruhé do globálního baseline.

Pro přijetí S bajtů do bucketu s x se mění excess o max(0, x + S - M) - max(0, x - M). Držet totalExcess průběžně, nescannovat všechny peer fronty na každý enqueue. Výpočty používat subtraction-before-add a checked převody size_t/int64. Po validaci baseline N*P lze přijímat, pouze když delta <= G - baseline - totalExcess a S <= G - A.

Důvody zamítnutí mají pevné pořadí: global_limit při fyzickém paketovém limitu, per_peer_reservation při nepřípustném počtu peerů nebo když neprojde ani peer-only referenční výpočet, jinak nový sub_bucket_reservation při porušení vnitřních rezervací. Peer-only výpočet lze udržovat pomocným agregátem sum(max(0, peerBytes-P)), bez skenu peerů. Každý odmítnutý nový paket má právě jeden drop důvod.

Výběr do odchozího RequestPlan převádí waiting na held, takže A a L neklesají. Producent proto nespotřebuje kapacitu nutnou pro rollback. Definitivní ACK uvolní held, rollback přenese held zpět do waiting bez nové admission a bez přírůstku A nebo L. Žádná cesta nesmí dříve přijatý vrácený paket odmítnout kvůli producentovi.

Při příchozím výběru do jediného tunnel send lze použít stejný jednopaketový lease. Úspěch i terminální drop při chybě zápisu lease definitivně uzavřou. Blokující send neprobíhá pod queue mutexem.

V diagnostice occupiedBytes znamená všechny skutečně vlastněné paketové bajty waiting + held, inFlightBytes zvlášť ukazuje held a itemCount zahrnuje obě skupiny. Přidat queuedItemCount pro samotné čekající FIFO. Limit tím zůstává srozumitelný i během pomalého HTTP requestu. Nedvojitě počítat tentýž buffer v RequestPlan a frontě.

canAdmitAnotherPacket zůstane konzervativní odhad pro neznámý příští peer/flow, nikoli autoritativní admission. Přepočítat jej s nejhorším možným bucketovým přírůstkem v existujících peerech i hypotetickém novém peeru. Před odhadem provést příslušný expirující úklid. Nezakládat na něm nové blokování pollingu v tomto rozsahu.

### 3.5 Monotónní expirace a index

DirectionQueues přijímá Clock& a DropCounters& se zaručenou životností přesahující frontu. Použít Clock::monotonicMs, nikoli systemUtcMs, čas serveru nebo dobu posledního retry.

Timestamp ukládat jako uint64 bitovou reprezentaci monotónních ms. Věk počítat unsigned rozdílem now - queuedAt, nepřičítat TTL k timestampu v signed int64. TTL je kladné int64, při porovnání bezpečně konvertované na uint64. Tím nevznikne signed overflow kolem INT64_MAX a funguje i jeden wrap uint64 čítače. Kontrakt hodin je monotónní průběh modulo 2^64, život žádného paketu nesmí zahrnout celý cyklus 2^64 ms. Jde o více než 584 milionů let, nikoli praktické provozní omezení. Skok wall-clock nesmí mít vliv.

Expirace je age >= maxQueuedPacketAgeMs. Před každým konkrétním výběrem číst nynější monotónní čas a odstraňovat jen souvislý expirovaný prefix zvoleného bucketu. První čerstvý paket ukončí úklid tohoto FIFO. Expirace neodebírá DRR kredit, ale při vyprázdnění nastane obvyklý reset.

Pro kapacitní cestu použít index čel neprázdných FIFO, nejvýše 16*N položek. Konkrétní implementace: indexed binary min-heap klíčů (originalEnqueueSequence, peer, bucket), pozice uložená v bucketu. Při změně čela odpovídající položku upravit nebo odstranit. Konstrukce fronty ponechá index prázdný bez rezervace pro všechny peery. Teprve při vytvoření nového peeru zvětšit kapacitu heapu na alespoň 16 krát počet skutečně alokovaných peerů, ještě před publikováním paketu. Kapacitu za běhu nezmenšovat. Peer s held pakety se nemaže, proto kapacita vždy stačí i pro návrat všech jeho bucketů. Jde nejvýše o 16*N malých indexových položek a žádné paketové buffery. Tato dynamická metadata alokace umožní bezalokační rollback. Nevytvářet neomezený lazy heap se stale záznamy.

Pořadové číslo zajišťuje stejné pořadí jako čas prvního přijetí, ale bez porovnávání timestampů přes wrap. Při rollbacku se staré čelo znovu objeví se svým původním pořadím. Pro hypotetické dosažení UINT64_MAX sekvence provést jednorázové kompaktní přečíslování všech živých paketů v pořadí původních sekvencí včetně lease metadat a obnovit index. Živých paketů je při nenulové minimální IPv4 délce méně než UINT64_MAX. Queue eviduje aktivní lease seznamy jako stabilní uzly. Sequence metadata se čtou i mění výhradně pod queue mutexem, zatímco serializátor čte jen neměnná paketová data. Přečíslování tak nezavádí data race s rozpracovaným requestem. Ošetřit tuto výjimečnou cestu v testu malým testovacím čítačem. Není to scan prováděný na každý paket.

Před konečným kapacitním odmítnutím:
1. Vzít nejstarší čelo indexu.
2. Je-li čerstvé, jsou všechna ostatní čekající čela čerstvá a úklid skončí.
3. Je-li expirované, odstranit jeho expirovaný prefix, obnovit index, účetnictví a vlastnictví.
4. Znovu vyhodnotit přijetí. Pokračovat jen dokud je přijetí blokováno a existuje další expirované čelo.

Tento úklid neprochází čerstvá FIFO nebo všechny neaktivní peery. Náklady jsou úměrné skutečně odstraněným paketům a aktualizovaným čelům, O(E + H log(16*N)), kde E je počet dropů a H počet dotčených bucketů. Prefixová práce je amortizovaně konstantní na odstraněný paket. Jednotlivý tlakový enqueue může odstranit delší expirovaný backlog, ale nemůže opakovaně skenovat neexpirované pakety. Nezavádět libovolný malý rozpočet úklidu, který by zbytečně odmítl nový paket, přestože uvolnitelné expirované bajty stačí.

Held pakety nejsou v indexu čekajících FIFO a během HTTP se z nich svévolně neodstraňují části již sestaveného requestu. Po rollbacku se obnoví původní čas a další výběr je může ihned expirovat. Garantovaná hranice TTL je okamžik lokálního výběru do requestu nebo tunnel send, nikoli konec HTTP přenosu či doručení druhému klientovi. TTL nepřerušuje již zahájený přenos.

DropReason::queue_expired se eviduje přes DropCounters právě jednou, se skutečným peerem. Cleanup při enqueue musí zaznamenat staré dropy, i když je nový paket nakonec přijat. Nedovolit souběžné dvojí účtování přes return value a DropCounters.

### 3.6 DRR a atomický výběr

Použít jednu produkční implementaci DRR pro oba směry. Kvantum Q odvodit ze společného aktuálního maxima tunelového IPv4 paketu. Ověřený kód dnes používá 65535 v kReceiveBufferBytes i PayloadBatch::kMaxPacketBytes a nemá konfigurované menší MTU. Pro tuto realizaci sjednotit zdroj této hodnoty a použít Q = 65535. Nevymýšlet skryté pevné MTU 1500. API může přijmout aktuální maximum při konstrukci pro budoucí menší tunelové maximum a testy.

DRR udržuje kurzor a rozpracovanou návštěvu:
- Při začátku návštěvy neprázdného bucketu přidat Q právě jednou.
- Dokud head.size <= deficit, lze z něj postupně vybírat pakety a odečítat jejich skutečnou IPv4 délku.
- Při nedostatku deficitu přejít na další aktivní bucket. Zbylý deficit ponechat do dalšího kola.
- Po prázdném FIFO resetovat deficit na nulu, ukončit návštěvu a postoupit.
- Jeden API výběr vrací nejvýše jeden paket, ale další návštěva téhož peeru může pokračovat ve stejném bucketu bez nového kvanta.
- Tento poslední bod je zásadní. Střídání jednoho paketu na bucket s přidáním Q při každém volání by bylo packet round robin, nikoli DRR podle bajtů.

Peer-level plánovač volá výběr střídavě mezi peery. Příchozí worker tedy nadále provádí nejvýše jeden send na peer při jednom vnějším průchodu. Více malých paketů téhož bucketu se rozloží přes další vnější průchody, ale souhrnně spotřebuje stejné bajtové kvantum jako velký paket jiného bucketu. Jediný peer a jediný bucket nezpůsobí čekání mezi koly.

Nahradit neatomickou dvojici frontPacketSize/popFront jedním queue API s neblokujícím rozhodovačem velikosti kandidáta. Pod zámkem provede expiry, DRR, sizing a vyjmutí. Rozhodovač smí jen spočítat velikosti nad hodnotovými parametry, nevolá queue API, I/O, kryptografii ani jiný zámek.

Rozhodnutí kandidáta:
- take: vznikne lease, deficit se odečte právě jednou,
- defer_for_current_request: paket zůstane na čele a žádný deficit se neodečte,
- permanently_oversized: paket se zahodí, eviduje oversized_for_request a pokračuje se dalšími kandidáty.

Dočasně nevhodný bucket se pro aktuální request označí jako blokovaný, zbývající buckety se dále zkusí. Jeho deficit se během téhož requestu znovu nezvyšuje. Kontext blokovaných bucketů je lokální k plánování requestu a nesmí zabránit jeho obsluze v dalším requestu. Deficit ukládat uint64 a přidávat kontrolovaně. Při Q >= největším přípustném paketu se za běžného DRR drží v omezeném intervalu, který testy ověří.

Odchozí vnější scheduler zachová první průchod peery a následné doplňování již založených záznamů. Jeden PlannedRecord patří jednomu peeru a může obsahovat více bucketů. Zachovat přesný recordSizeFor, base64, batch length entries, auth line a rezervu 1024 bajtů. Záznam nevzniká kvůli pouhým expirovaným nebo oversized paketům.

Při maxDataRecordsPerRequest nebo zaplnění těla uchovat peer rotaci. Pokud první peer nemá využitelný paket, pokračovat k dalším. Oversized drop se musí považovat za změnu čela, aby scheduler neskončil před dalšími použitelnými pakety. Na konci requestu ověřit skutečnou serializovanou délku před transportem.

### 3.7 Lease, rollback a DRR účetnictví

PlannedRecord drží peer a std::list<QueuedPacket> vybraných uzlů v pořadí DRR výběru. Původní bucket index je v uzlu. Výběr přes splice přesune uzel z bucketového FIFO do recordu. RequestPlan je move-only transakce vůči konkrétní queue instanci/epochě. Žádný rollback nesmí obnovit data do fronty nového připojení.

Implementovat explicitní commitRecord a rollbackRecord plus scope guard pro dosud nerozhodnuté záznamy. Jediný odchozí consumer má nejvýše jeden rozpracovaný RequestPlan. Producenti mohou přidávat nové pakety. Objekt fronty žije až do ukončení všech consumerů a uzavření lease. clear nesmí zneplatnit živé pointery, při teardown se nejprve ukončí konzumenti a vyřeší tokeny.

Rollback:
1. Procházet list vraceného recordu od konce, pro každý uzel použít jeho původní bucket index.
2. Přes splice vložit uzel před čelo příslušného FIFO. Obrácený průchod zachová původní pořadí každého bucketu před novějšími čekajícími pakety bez pomocného alokovaného seskupování.
3. Zachovat timestamp a sequence. Změnit held na waiting bez dalšího admission.
4. U již existujícího vlastníka zvýšit ownerPacketCount jen za shodné klíče. Ostatní vrácené pakety zůstávají přetokové.
5. U bucketu bez vlastníka použít pravidlo omezeného povýšení. Nevynutit staré vlastnictví, které mezitím mohl získat jiný bucket.
6. Obnovit index čela. Při následném výběru proběhne expirace.

DRR se účtuje za každý lokální výběr pro přenos, nikoli podle pozdějšího ACK. Při rollbacku deficit znovu neodečítat ani nerefundovat. Již vyprázdněnému a resetovanému bucketu nevracet starý snapshot. Další retry je nový DRR výběr s jedním novým odečtem. Tato politika je záměrná, aby neúspěšný provoz nehromadil kredit a nepoškodil mezitím přiděleného vlastníka. Je nutné ji uvést v komentáři a testovat.

Úspěšně ověřené acceptedMessageIds commitují příslušný peer record. Ostatní se vrátí. Zachovat stávající konzervativní chování při neověřené odpovědi. Nezavádět novou garanci exactly-once doručení.

Pokrýt návrat při chybě generování requestId, timestampu, kontraktu/serializace/šifrování, při cancellation před transportem, transportní chybě, ne-200 odpovědi, chybném MAC/NDJSON a deferred/nepotvrzeném messageId. Scope guard chrání i před výjimkou před rozhodnutím. Vektor recordů a nutná registrační metadata rezervovat před prvním přesunem uzlu, samotný rollback používá jen splice a existující peer struktury. Aktualizace expiry indexu při rollbacku používá indexed heap s již rezervovanou kapacitou z části 3.5. Nesmí alokovat ze scope guard destructor. Povýšení vlastníka používá pevný adresář a omezený scan bez alokace.

PayloadBatch dostane interní encoder nad pohledy na PacketBytes nebo iterátory uzlů, aby lease metadata nenutila vytvářet kopii vektoru paketových dat. Dosavadní API lze zachovat jako delegující overload. Samotná serializace plaintextu přirozeně vytvoří jeden batch buffer stejně jako dnes.

### 3.8 Diagnostika

Doplnit QueueStatistics a QueueDiagnostics o activeSubBucketCount, inFlightBytes a queuedItemCount. peerQueueCount počítá alokované peery včetně peeru s pouze held daty. To musí být popsáno.

Do DiagnosticsSnapshot/counters přidat queueExpiredPackets, outboundQueueExpiredPackets a inboundQueueExpiredPackets. Celkový údaj je součet směrových DropCounters pro queue_expired. packetsDropped již tyto dropy zahrnuje, nesmí se o ně podruhé zvýšit.

Zachovat existující agregaci DropCounters podle peeru a důvodu, rozšířit dropReasonName. Snapshot odebrat konzistentně s účetnictvím. Pevně dodržet lock order, queue mutex nesmí volat zpět do VpnEngine nebo IPC. Queue může zapisovat do DropCounters pouze v jednom dokumentovaném pořadí queue → drops, žádný opačný vnořený zámek.

Aktualizovat get-diagnostics JSON a jeho dokumentaci/testy. Nepřidávat obsah flow klíčů, porty, klíče hashe ani obsah paketů do logů nebo IPC. Nová GUI stránka nebo přepracování devíti traffic counters není součástí zadání.

## 4. Pořadí realizace a předávací body

### Krok 0: výchozí kontrola

Přečíst AGENTS.md, tento plán a intent v005. Ověřit větev, pracovní strom, zdrojový hash intentu a aktuální Graft kontext. Zapsat baseline stávajících testů. Zachovat cizí změny. Pokud aktuální stav mění význam plánu, vyjasnit konkrétní rozpor před závislou editací.

Výstup: seznam skutečně dotčených souborů a baseline, bez resetu větve.

### Krok 1: konfigurace a společné závislosti

Změnit RuntimeLimits, konstanty, oba validátory včetně Wizard IPC, výchozí testy a dokumentovaný JSON. Zavést společnou konstantu maximálního paketu. Připravit FlowHasher a clock injection. Výchozí dokument runtime automaticky nepřidává.

Výstup: projdou konfigurační a Wizard roundtrip testy včetně overflow a absence automatického zápisu.

### Krok 2: klasifikace a hash

Implementovat bezpečný FlowKey z validovaného IPv4 pohledu, fallback a HMAC. Napojit procesní klíč do životního cyklu a připravit deterministické testovací závislosti.

Výstup: klasifikační testy, exact equality/serialization, kolize, stabilita a rozložení všech 16 indexů.

### Krok 3: nový storage a účetnictví

Převést DirectionQueues na 16 bucketů, průběžné rezervace, ownerPacketCount a omezené povyšování. Připravit leases a konzistentní snapshots. Staré frontPacketSize/popFront/pushFront nenechat jako alternativní cestu obcházející nový model.

Výstup: přijetí a rollback mají přesné bajty/items, na každém kroku platí A <= G a L <= G, vlastník je jedinečný.

### Krok 4: expirace

Přidat timestamp/sequence, čelní index, prefix cleanup a pressure cleanup. Doplnit queue_expired a směrové čítače. Vyřešit odstranění posledního owner paketu, peeru a návrat staršího čela.

Výstup: řízené hodiny pokrývají všechny hraniční situace bez reálného čekání.

### Krok 5: společné DRR

Implementovat per-peer cursor, návštěvu, deficit a atomické rozhodování kandidáta. Testovat nejprve bez HTTP limitů, potom s blokovanými kandidáty a oversized.

Výstup: bajtová férovost pro různé délky, work-conserving chování a žádné nekonečné kolo bez pokroku.

### Krok 6: odchozí integrace

Převést RequestScheduler, PlannedRecord, PayloadBatch pohledy a NetworkWorker na leases. Zachovat přesné sizing funkce a konečnou kontrolu délky. Připojit všechny ACK/deferred/error cesty.

Výstup: chyby v libovolném bodě vrací přesně původní pořadí do původních bucketů, bez navýšení rozpočtu nebo obnovení stáří.

### Krok 7: příchozí integrace a lifecycle

Převést TunnelOutputWorker na stejné DRR. Zachovat one-send-per-peer-pass, counters, tunnel_stopped a stop při tunnel_io_failed. Upravit všechny konstrukce DirectionQueues včetně verifyAccessKey, contract a E2E testů. Zvlášť zachovat podporu interních nulových testovacích/zakázaných front, nepoužívat jejich nastavení jako veřejně validní konfiguraci.

Výstup: ověřený příchozí batch prochází klasifikací až po dnešní autentizaci a validaci, interaktivní flow dostává obsluhu i pod zatížením.

### Krok 8: diagnostika a dokumentace

Dovést nová pole přes snapshot a IPC, aktualizovat dokumentační plochy uvedené níže. Dokumentace je součástí stejného výsledku, nikoli následný volitelný úkol.

Výstup: shodné názvy, jednotky, defaulty a chování v kódu, JSON, průvodci i dokumentaci.

### Krok 9: kompletní validace

Spustit cílené testy, celý klientský regresní executable, ASan, kontraktový roundtrip a definované integrační fair-queuing scénáře. Vyhodnotit paměť/výkon a souběh. Po velké změně spustit graft build a ověřit diff grafu. Zaznamenat výsledky, nikoli jen seznam plánovaných příkazů.

Výstup: všechny akceptační kontroly jsou splněné nebo konkrétně označené jako neprovedené. S neprovedeným povinným bodem není realizace hotová.

## 5. Testovací návrh

### A. Konfigurace a persistence

Rozšířit test_config.cpp, test_default_config.cpp, test_wizard_document.cpp, test_wizard_ipc.cpp, test_contracts_document.cpp a relevantní config store testy.

Testovat chybějící runtime, prázdný objekt, každé chybějící nové pole, asymetrická minima a TTL jiné než default. Porovnat rawJson a uložené bajty před/po běžném načtení. Roundtrip průvodce nesmí materializovat chybějící pole do souboru. Starý veřejný název odmítnout.

Pro oba směry odděleně testovat přesnou hranici N*P a o bajt menší G, /24 i /29, největší reprezentovatelné součiny a násobení přes limit. Testovat P = 1, 15, 16, 17, 262144 a zbytek R. TTL testovat 0, -1, 1.5, true, null, string, INT64_MAX a INT64_MAX+1.

### B. Flow a vlastnictví

Použít builder validních IPv4 TCP/UDP paketů s přepočteným IPv4 checksumem. Dosavadní packetOf vyplňující libovolné bajty nahradit validními fixture pakety, nikoli vypnout validaci kvůli starým testům.

Pokrytí: různé adresy/porty/protokoly, obrácená pětice, IP options, neúplné IPv4 a transportní hlavičky, první fragment s MF, další fragment s nenulovým offsetem, poslední fragment, DF bez fragmentace, ICMP a neznámý protokol.

Naplnit všech 16 vlastníků v obou směrech, přidat 17. a další flow. S pevně zadaným klíčem najít deterministický vzorek toků pokrývající všech 16 hashových indexů a kolizní dvojici. Netestovat náhodně podmínku, která občas nevyjde.

Testovat odstranění vlastníka při posledním jeho paketu uprostřed mixu overflow, okamžité uvolnění prázdného slotu, povýšení prvního kandidáta, odmítnutí duplicitního vlastníka a chování 64/65 zbývajících paketů. Instrumentace počtu klasifikací musí prokázat omezený scan i pro velký kolizní backlog.

### C. Rezervace, paměť a leases

Ověřit vzorce A a L jednoduchým referenčním modelem po každé operaci enqueue/select/commit/rollback/expiry/clear. Randomizovaný test má pevný seed a tiskne reprodukovatelný seed při chybě.

S defaulty a N = 252 je baseline 66060288 bajtů. Jediný plný peer má horní příspěvek 68419584 bajtů, tedy 65.25 MiB. Při jediném aktivním bucketu a prázdných ostatních bucketech je jeho maximální x 68173824 bajtů, protože dalších 15 minim zůstává rezervovaných. Tyto hodnoty otestovat a správně rozlišit v dokumentaci.

Testovat těžký bucket až po sdílený limit a následně přijetí malého paketu do prázdného bucketu v rámci M. Testovat dosažení limitu více peery, M = 0, R != 0 a obrácené pořadí plnění.

Bariérový souběhový test: vybrat request, zastavit transport, mezitím produkovat do stejného i jiných bucketů až na mez, vynutit rollback. V každém snapshotu musí platit obě kapacitní nerovnosti. Žádná alokace paketového úložiště o velikosti G při konstrukci, žádná fyzická alokace pro všech N peerů. Změřit metadata režii, nikoli slibovat RSS <= 128 MiB.

### D. Expirace

TestClock je řízený a thread-safe. V obou směrech testovat 4999, 5000, 5001 ms, TTL = 1, velkou TTL, skok přes hranici, okolí INT64_MAX, unsigned wrap, hypotetický wrap sequence a wall-clock skok bez monotónní změny.

Směs starých a čerstvých paketů v témže bucketu musí odstranit jen starý prefix. Zkontrolovat bajty/items/owner/deficit/index po každém odstranění. Vrácený starý paket před čerstvými producenty nesmí dostat nový timestamp.

Pressure cleanup musí najít expirované čelo i jiného peeru, přijmout nový paket po uvolnění dostatečného prostoru a nezapočítat odmítnutí přijatému novému paketu. Pokud úklid nestačí, expirující dropy a následný kapacitní drop mají správné oddělené počty. Index nesmí růst nad počet neprázdných bucketů.

### E. DRR a scheduler

Použít více trvale neprázdných bucketů s velikostmi například 100, 1000 a Q bajtů. Po celých DRR kolech je rozdíl poskytnutých bajtů omezen nejvýše velikostí jednoho maximálního paketu pro pravidelně zásobené buckety. Počet paketů se smí výrazně lišit. Krátká fronta po příchodu musí dostat obsluhu nejpozději během dalšího úplného průchodu aktivními buckety při dostatečném transportním rozpočtu.

Testovat pokračování návštěvy přes API volání i requesty, reset při vyprázdnění, přenos deficitu, jeden aktivní bucket, více peerů a omezení jednoho recordu/requestu. Kvantum lze v testech zmenšit pro krátké přehledné trace.

Do zbytku requestu se nevejde velký head, ale vejde se malý head jiného bucketu. Ověřit jeho výběr a zachování velkého head. Testovat opakované malé requesty, přesnou hranici sizing, oversized následovaný platným paketem a všechna data expirovaná. Žádný tight loop bez změny stavu. FIFO se ověřuje po bucketu, nikoli jako zrušené společné peer FIFO.

### F. NetworkWorker, příchozí worker a konec spojení

Rozšířit ScriptedTransport a fake Tunnel. Vynutit každou chybovou cestu z části 3.7, smíšený accepted/deferred výsledek více peerů, neplatný MAC, chybějící potvrzení, cancellation a zastavení během requestu.

Příchozí test složí reálné ověření/dešifrování/ingestRecord, DirectionQueues, DRR a TunnelOutputWorker. Fake Tunnel zaznamená délky a identifikátory testovacích paketů. Při běžném úspěchu jeden commit, při send chybě jeden drop a zachování stávajících writeFailures/packetsDropped. Při tunnel_io_failed worker skončí, nevrací paket k nekonečnému retry.

Souběžní producenti se stejným i různými klíči, consumer a diagnostické snapshoty nesmějí vytvářet duplicitní vlastnictví nebo záporné/nesouhlasící počty. Synchronizovat bariérami, nikoli náhodnými dlouhými sleep. Teardown test musí prokázat, že opožděný token nedoplní novou queue instanci.

### G. Integrační férovost a regresní přenos

Povinný deterministický integrační scénář používá produkční queue, scheduler, payload kódování, NetworkWorker a příchozí worker s řízeným transportem/tunelem. Jeden peer má trvale doplňovaný bulk flow a sporadický interaktivní flow na jiné pětici. Provést zvlášť outbound a inbound i obousměrnou kombinaci.

Měřit pořadí obsluhy a bajty, ne pouze wall-clock latenci. Interaktivní pakety musí odejít v omezeném počtu úplných DRR/peer kol ještě při zbývajícím bulk backlogu. Požadovat žádné ztráty bez konkrétního kapacitního/expiry důvodu a žádný přesun mezi původními buckety při retry.

Existující real-PHP contract roundtrip po změně spustit proti izolovanému fixture. Ověřit nezměněné přijetí, dešifrování a odpovědní limity. Python-only serverový test sám o sobě neprokazuje klientské DRR.

Existující izolovaný SCM/Wintun UDP E2E je další reálná regresní kontrola. Spouštět přes dokumentovaný izolovaný runner, nikoli přidáním do běžného CTest. Při nedostupném prostředí uvést NOT RUN a přesnou příčinu. Neprohlašovat fake Tunnel test za skutečný Wintun průchod.

## 6. Příkazy a důkazy validace

Na Windows v připraveném x64 MSVC developer prostředí, pracovní adresář C:\git\phpvpn\client:

1. cmake --preset debug
2. cmake --build --preset debug
3. ctest --preset debug -R "^phpvpn-client-tests$" --output-on-failure
4. ctest --preset debug --output-on-failure
5. cmake --preset asan
6. cmake --build --preset asan
7. ctest --preset asan -R "^phpvpn-client-tests$" --output-on-failure
8. cmake --preset release
9. cmake --build --preset release
10. ctest --preset release -R "^phpvpn-client-tests$" --output-on-failure

CMake 3.28+ a Ninja odpovídají současným presetům. Neuvádět neexistující samostatný CTest název pro každou unit suite. Existující phpvpn-client-tests agreguje queues, scheduler, runtime, config a IPC.

Pro real-PHP kontraktový běh explicitně zapnout PHPVPN_BUILD_CONTRACT_TESTS=ON, nastavit PHPVPN_CONTRACT_URL na izolovaný wizard-default fixture a spustit phpvpn-contract-tests. Konkrétní adresa není produkční endpoint. Zachovat RUN_SERIAL a resource lock. Pokud jsou potřeba Python závislosti fixture, spravovat je pouze přes uv.

Pro izolovaný Windows E2E postupovat podle client/tests/e2e/README.md s PHPVPN_BUILD_SERVICE_E2E=ON a --run-isolated-with-udp. Tento běh spravuje zvláštní testovací službu, Wintun a baseline hostu. Nenahrazovat dokumentovaný runner improvizovanou instalací klienta.

Výkonnostní evidence: porovnat fronty s 1, 16 a 256 flow na peer, 1 a 252 peery, malé a maximální pakety. Zaznamenat enqueue/select/pressure-cleanup čas, počet hashování a klasifikací, metadata/RSS trend, počet navštívených čel a propustnost. Testy nesmějí vyžadovat křehký univerzální počet mikrosekund na různých strojích. Musí prokázat, že běžné enqueue neprochází backlog, index je omezený, payloady se při přenosu vlastnictví nekopírují a HMAC fallback nemění asymptotiku podle počtu overflow flow.

Finální evidence obsahuje revizi, konfiguraci buildu, přesné příkazy a výsledky, seed stres testů, počet testů, případné známé baseline chyby a neprovedené kroky. Po změně zdrojů spustit graft build v kořeni a git diff --check. Zkontrolovat, že změna nezasáhla serverový drátový kód.

## 7. Povinná dokumentace

### client.md

Aktualizovat zejména:
- pracovní role a dosavadní tvrzení o peer FIFO kolem řádků 38–40,
- JSON příklad a runtime pravidla kolem 74–120,
- admission invariant a příklady maximální obsazenosti,
- request sizing, oversized a scheduler kolem 231–235,
- akceptační kontroly a návaznosti na dosud nedodaný backpressure kolem 260–266,
- úplný Wizard context kolem 630,
- zmínky o FIFO ve Windows, macOS a Linux tunelech kolem 893–897, 1084, 1116 a 1126,
- diagnostické schéma get-diagnostics a význam čítačů.

Výslovně vysvětlit:
- runtime pole jsou nepovinná, defaulty se doplní pouze v paměti,
- M = P/16 je odvozené a zbytek není další minimum,
- fyzické paketové bajty, logical reservations, held bajty a skutečné RSS jsou odlišné údaje,
- pětice, fragmentový fallback, 16 vlastníků, stabilní overflow hash a omezené povyšování,
- DRR účtuje bajty a místní pokusy o přenos, peer-level rotace zůstává,
- FIFO se garantuje uvnitř bucketu, nikoli napříč celým peerem,
- flow může po změně vlastnictví získat jiný bucket podle pravidel intentu, nezavádí se globální reorder buffer ani garance pořadí celého flow přes takovou změnu,
- TTL začíná prvním přijetím, retry ho neobnoví, odstraňuje se jen expirovaný prefix,
- limit TTL se kontroluje při výběru, není to záruka end-to-end latence,
- diagnostika expirace celkem a v obou směrech.

### protocol.md

U lokálního dávkování, pořadí a retry doplnit explicitní odkaz na client.md jako autoritativní popis front. Potvrdit nezměněný wire format, kryptografii, API i význam datových záznamů. Zachovat pořadí paketů uvnitř již sestaveného batch a autentizačních záznamů. Nové lokální pořadí před sestavením batch ani po jeho ověření nesmí být vydáváno za změnu serverových garancí.

Oddělit klientských 5000 ms od serverového QUEUE_TTL_SECONDS a lokálního monotónního času od protokolového UTC timestampu. Metadata bucketu, vlastníka, DRR a času se neserializují.

### Další plochy

Pomocí graft grep pro indexovaný kód a rg pro neindexované dokumenty/JSON příklady provést finální inventuru starého názvu a tvrzení o jednom peer FIFO. Aktualizovat skutečné aktivní příklady, komentáře API, IPC popisy, testovací fixture a report/checklist nových testů.

Historické neměnné Intents a Plans se nemění. Výskyty původních čísel v nezměněných response limitech, serverových limitech nebo negativních testech nejsou chyba a nesmějí se slepě nahradit.

## 8. Rizika a jejich uzavření

| Riziko | Opatření a důkaz |
| --- | --- |
| Rollback překročí limit po souběžném enqueue | Held bajty zůstávají v A i L, bariérový test s plným rozpočtem |
| Obnovení starého vlastníka poškodí nového | Rollback rekonstruuje vztah k aktuálnímu vlastníku, nepřepisuje snapshot |
| Dvojí odečet DRR nebo nafouknutí kreditu retry | Jeden odečet při výběru, rollback nemění deficit, trace test |
| Jednopaketové RR se vydává za DRR | Pokračující návštěva, bajtové dlouhodobé testy různých délek |
| Hlava příliš velká pro zbytek requestu blokuje jiné buckety | Dočasný request-local block a zkoušení dalších bucketů |
| Expiry smaže i čerstvý zbytek | Prefixové testy a referenční účetnictví |
| CPU útok změnami vlastníků | Nejvýše 16 porovnání adresáře, limit 64 při povýšení |
| CPU režie při stovkách overflow flow | HMAC jen pro overflow, měření skutečné propustnosti, žádná neomezená flow mapa |
| Index expirace roste nebo skenuje všechny fronty | Jeden index entry na neprázdný bucket, explicitní mazání, instrumentované testy |
| Časová aritmetika přeteče | Unsigned elapsed výpočet, řízené boundary/wrap testy a sequence rebase |
| Deadlock queue, drops a engine | Dokumentovaný lock order, žádné I/O pod queue lockem, souběhové testy |
| Schema je jiné v průvodci a službě | Společná pole, strict roundtrip a stejná invariantní validace |
| Stará dokumentace slibuje globální peer FIFO | Povinná celková kontrola textu, ne jen přidání nové kapitoly |
| Testy dávají falešnou jistotu | Validní pakety, reálné produkční komponenty v integraci, oddělené výsledky fake a Wintun testů |

Není zde otevřená materiální otázka bránící realizaci. Pokud realizátor zjistí, že musí změnit serverový protokol, obnovovat TTL při retry, přidat třetí konfigurační minimum nebo zrušit část dokumentační povinnosti, jde o změnu intentu a musí ji vrátit koordinátorovi. Úprava implementačního detailu s materiálním dopadem na tento plán vyžaduje novou úplnou neměnnou verzi plánu.

## 9. Matice akceptace intentu v005

| Kritérium intentu | Konkrétní kontrola realizace |
| --- | --- |
| 1 | Oba defaultní globální limity přesně 134217728 v config a IPC testu |
| 2 | Obě směrová minima přesně 262144, asymetrické explicitní hodnoty |
| 3 | Pole 16 bucketů, M = 16384 a 16*M = P |
| 4 | Missing runtime/fields bez změny rawJson a uložených bajtů |
| 5 | N*P hraniční test pro každý směr a overflow |
| 6 | 16 současných flow získá 16 různých vlastnictví v obou směrech |
| 7 | Opakované overflow flow má stejný hash index při stejném klíči |
| 8 | Deterministický vzorek pokrývá všech 16 fallback indexů |
| 9 | Empty release, owner count, bezpečné povýšení a žádná duplicita |
| 10 | Outbound bulk + sporadické interaktivní flow, omezené DRR čekání |
| 11 | Stejný test pro inbound před tunnel send |
| 12 | Rovné kvantum podle bajtů pro různé velikosti, ne počty paketů |
| 13 | Jediný bucket využije sdílené excess bajty a dostupný přenos |
| 14 | Těžký bucket nechá ostatním M, přijetí burstu do prázdného bucketu |
| 15 | Identifikátory pořadí FIFO, rollback do původního bucketu před producenty |
| 16 | Fragmenty včetně MF prvního fragmentu, krátké hlavičky a fallback |
| 17 | Peer rotace, všechny drops, přesný request/response budget a wire regrese |
| 18 | Testovací sady B–F včetně oversized, concurrency a tunnel chyb |
| 19 | Produkční komponovaný integrační test v obou směrech se stálým bulk backlogem |
| 20 | Celý phpvpn-client-tests, real-PHP transfer a aktualizované konstruktory |
| 21 | TTL default/explicitní hodnota a všechny invalidní typy/hrany |
| 22 | 4999/5000/5001 ms před výběrem v obou směrech |
| 23 | Více starých head položek, první čerstvá a následující pořadí |
| 24 | Starý head nikdy nezahodí čerstvý tail |
| 25 | Failure/retry zachová timestamp a další výběr paket expiruje |
| 26 | Pressure cleanup uvolní dostupné staré bajty s přesným účetnictvím |
| 27 | TestClock, velký skok, signed/unsigned hrany a sequence wrap bez sleep |
| 28 | Runtime dokumentace v client.md odpovídá config a persistence testům |
| 29 | Dokumentované buckety, flow, hash, DRR, rezervace, leases, TTL a diagnostika |
| 30 | protocol.md potvrzuje nezměněný protokol a odkazuje lokální politiku |
| 31 | Finální audit všech aktivních dokumentačních ploch a starých názvů |

## 10. Kritérium dokončení a předání

Realizace je hotová až po splnění celé matice, kontrole dokumentace, ověření nezměněného drátového formátu a zaznamenání výsledků testů. Samotná kompilace nebo přidání testů nestačí.

Předávací zpráva uvede změněné chování, výsledky testů, skutečně měřená omezení výkonu/paměti a případné neprovedené environmentální kontroly. Nemá obsahovat žádné tajné hodnoty nebo paketové obsahy.

Při přípravě tohoto plánu nebyla provedena implementace ani spouštěny buildy či provozní testy. Testovací příkazy výše jsou závazný návrh validace pro realizátora, nikoli tvrzení o jejich úspěšném provedení.

