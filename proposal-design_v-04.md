# Návrh odstranění násobení lock budgetu při `sync`

> Stav: návrh k V-04, bez implementace.
>
> Rozsah: změna toku `sync`, politiky deadline/locků a odpovídající klientské a testovací smlouvy. Tento dokument nemění `phpvpn.php` ani `design2.md`.

## 1. Rozhodnutí

Doporučená varianta je **omezený fan-out s jedním absolutním deadline a jedním společným lock-wait budgetem pro celý `sync`**. Server nadále používá per-frontové `flock` a atomické publikování přes `.writing` → `.data`, ale:

- v jednom požadavku zpracuje nejvýše 8 různých cílových front;
- všechny pokusy o lock dohromady smějí dobrovolně čekat nejvýše 12 ms, nikoli 12 ms nebo 200 ms na každou frontu;
- žádný nový mutační krok nezačne po absolutním aplikačním deadline, doporučeně 100 ms od začátku requestu;
- nedostupnou, nadlimitní nebo již časově nezpracovatelnou cílovou dávku přesně označí jako odloženou; klient ji ponechá lokálně;
- již atomicky publikovanou dávku nikdy kvůli deadline nevrací zpět ani nemaže;
- vlastní příchozí frontu obslouží před fan-outem, aby mnoho cílů neblokovalo vyzvednutí a ack;
- oportunistický sweeper používá tentýž deadline a pouze okamžité `LOCK_NB`; po odpovědi si nevytváří nový řetězec timeoutů.

Doporučené výchozí hodnoty jsou záměrně konzervativní pro shared PHP hosting:

```text
SYNC_PROCESSING_BUDGET_MS       = 100
SYNC_LOCK_WAIT_BUDGET_MS        = 12
SYNC_LOCK_ATTEMPT_SLICE_MS       = 2
SYNC_RESPONSE_RESERVE_MS        = 10
MAX_DESTINATIONS_PER_SYNC        = 8
```

Hodnoty jsou laditelné po zátěžovém testu, ale jejich vztah je normativní: počet destinací ani počet locků nesmí násobit celkový čekací budget. Absolutní deadline je kooperativní aplikační mez, ne slib preempce jednoho blokujícího filesystemového syscallu; aplikace však po jeho dosažení nesmí zahájit další čekání, scan ani zápis.

## 2. Kořen problému

V aktuálním toku má každý `QueueLock::acquire()` vlastní deadline `now + 200 ms`. `SyncHandler::publish()` cíle prochází sériově a pro každý volá nový `Queue::enqueue()`. Pro 253 platných cílových adres je proto samotná teoretická doba čekání přibližně `253 × 200 ms = 50,6 s`. K ní se může přidat nonce lock, lock vlastní fronty, zápis, GC a per-frontové locky sweeperu.

`REQUEST_BUDGET_MS = 250` dnes není deadline. Čte se až při sestavení doporučení pro další request, tedy poté, co už bylo dlouhé čekání vykonáno. `set_time_limit(30)` rovněž není korektnostní mechanismus: na různých SAPI a operačních systémech nemusí započítávat sleep a blokující I/O stejně a může request ukončit mezi úspěšnými publikacemi.

Kořenovou příčinou tedy není samotná hodnota 200 ms, ale **lokální rozpočet vytvářený znovu pro každý člen sekvenční kolekce**. Pouhé snížení `LOCK_TIMEOUT_MS`, například na 10 ms, stále dává až 2,53 s a stejný problém jen zmenší.

## 3. Neměnné bezpečnostní a datové invarianty

Latence se nesmí snížit odstraněním locků ani fail-open přístupem. Návrh zachovává tyto invarianty:

1. V jednu chvíli je držen nejvýše jeden per-frontový lock. Nevzniká deadlock ani závislost na pořadí více současně držených locků.
2. Čtenář vidí jen kompletní `.data`; `.writing` není doručitelné.
3. UUID se objeví v `acceptedPacketUuids` teprve po úspěšném atomickém `rename` na `.data`.
4. Každý validní `data` záznam pracovního requestu je právě v jednom z `acceptedPacketUuids` nebo `deferredPacketUuids`; seznamy jsou disjunktní a zachovávají původní pořadí z requestu.
5. Deadline nikdy nezpůsobí rollback již publikovaného `.data`. Cross-frontový rollback by mohl závodit s collectorem a zavést ztrátu.
6. Pokud deadline nastane během zápisu `.writing`, zápis se bezpečně dokončí, nebo se soubor zavře a odstraní. Teprve pak se uvolní lock. Neúplný soubor se nikdy nepublikuje.
7. Pokud deadline nastane těsně po publikaci, dokončí se povinný krátký commit tail: evidence výsledku v paměti, rebuildovatelný sidecar/statistika a uvolnění locku. Deadline zastavuje nové operace, nikoli úklid právě dokončené kritické sekce.
8. `.sending` se maže pouze idempotentním ackem. Claimnutá dávka zůstává `.sending`, dokud není potvrzena nebo bezpečně reclaimnuta.
9. Selhání nebo ztráta HTTP odpovědi smí vytvořit duplicitu, nikdy tichou ztrátu. Příjemce nadále deduplikuje podle `uuid`.
10. HMAC, kontrola `src`, validace cest, HTTPS pravidla a fail-closed kontrola nonce při `PERSIST_NONCES=true` se kvůli rychlosti nepřeskakují.

`queue.stat` je rebuildovatelná cache, nikoli autorita existence batchů. Autoritou jsou atomicky pojmenované soubory a případná tvrdá kvótová rezervace řešená samostatně v rámci V-03. Deadline nesmí způsobit, že server oznámí přijetí před publikací souboru.

## 4. Porovnání variant

| Varianta | Latence | Propustnost na shared hostingu | Atomický rozsah | Složitost | Hodnocení |
|---|---:|---:|---|---:|---|
| A. Omezený fan-out + absolutní deadline + sdílený wait budget | Nízká a předvídatelná; čekání nejvýše 12 ms celkem | Dobrá, nesouvisející fronty zůstávají paralelní | Jedna cílová fronta; přesná částečná akceptace | Střední | **Doporučeno** |
| B. Přesně jedna cílová fronta na `sync` | Nejnižší per-request latence | Horší při fan-outu kvůli mnoha HTTPS requestům | Jedna cílová fronta | Nízká na serveru, vyšší na klientovi | Použitelná, ale příliš drahá na HTTP režii |
| C. Jeden globální mutační lock | Jeden lock wait, ale convoy efekt | Slabá; pomalý zápis blokuje všechny klienty | Lze snadněji koordinovat celý request, ne však atomicky commitnout více adresářů bez transakční vrstvy | Střední až vysoká | Nedoporučeno |

### 4.1 Varianta A — doporučená

Server přijme v requestu více destinací, ale do pracovního setu vybere prvních nejvýše 8 různých destinací v pořadí jejich prvního výskytu. Záznamy pro další destinace plně validuje a zahrne do HMAC, avšak jejich payload nebufferuje pro zápis a UUID rovnou označí jako deferred. Pozdější záznam pro již vybranou destinaci může být do stejné cílové dávky přidán.

Každý per-frontový lock má okamžitý pokus `LOCK_EX | LOCK_NB`; případné krátké retry je omezeno současně zbývajícím absolutním časem, zbývajícím společným wait budgetem a dvoumilisekundovým stropem jednoho pokusu. Nezískaný lock odloží jen danou cílovou dávku. Server pokračuje k jiné vybrané destinaci, dokud má budget.

Výhody:

- odstraní matematické násobení timeoutu;
- zachová souběh nezávislých front;
- omezuje počet filesystem operací i při autorizovaném fan-out DoS;
- využívá již existující bezpečnou smlouvu accepted/deferred;
- nevyžaduje databázi, daemon, cron ani spolehlivou sdílenou paměť.

Nevýhodou je vědomé opuštění all-or-nothing semantiky celého multi-destination requestu. Atomickou jednotkou je cílový batch, nikoli celý request. To je přijatelný kompromis jen proto, že klient dostane přesný seznam UUID a deferred data nesmaže.

### 4.2 Varianta B — jedna destinace na request

Klient rozdělí lokální odchozí frontu podle `dst` a do jednoho `sync` pošle data pouze pro jednu destinaci. Server získává nejvýše nonce lock, vlastní frontu a jeden cílový lock. Lock může být fail-fast nebo používat jeden krátký globální wait budget.

Výhodou je velmi jednoduchá latence a snadná korektnost. Nevýhodou je výrazně více TLS/HTTP requestů, opakované auth a parsování, více práce PHP workerů a vyšší celková doba doručení fan-outu. Na shared hostingu může být tato režie horší než osm krátkých per-frontových operací v jednom requestu. Varianta také vyžaduje tvrdší změnu klienta a jasné odmítnutí nebo odložení starších multi-destination requestů.

Tato varianta je vhodná jako klientská optimalizace při opakované kontenci, nikoli jako jediná serverová smlouva. Doporučený klient může sám dočasně snížit fan-out až na jednu destinaci.

### 4.3 Varianta C — globální mutační lock

Všechny enqueue, ack, collect a GC operace by používaly například `_state/queues.lock`. Jeden `sync` by čekal jen jednou a po získání locku by zpracoval omezený pracovní set.

Výhodou je jednoduchý důkaz, že wait se nenásobí, a snazší koordinace globální kvóty. Nevýhodou je globální head-of-line blocking: zápis velkého batchu pro jednoho klienta zadrží všechny ostatní klienty. Autorizovaný klient může dlouhou kritickou sekcí vyčerpat malý worker pool a dostupnost celého VPN. Ani tento lock sám neposkytne skutečný atomický commit napříč více adresáři; k tomu by byl potřeba transakční manifest a recovery protokol.

Pro lokální disk s více nezávislými frontami je globální serializace horší než malý, přesně omezený per-frontový pracovní set.

## 5. Přesný nový tok `sync`

### 5.1 Založení deadline

`Ctx::init()` vytvoří monotónní `processingDeadlineNs = startedAt + SYNC_PROCESSING_BUDGET_MS` a `remainingLockWaitNs = SYNC_LOCK_WAIT_BUDGET_MS`. Všechny následné vrstvy dostanou tentýž objekt/kontext; žádná z nich nesmí počítat vlastní deadline jako `now + timeout`.

Před každým čekáním nebo novou mutací se kontroluje:

```text
remainingProcessing = processingDeadline - now
remainingForNewWork = remainingProcessing - responseReserve
allowedWait = min(remainingLockWait, perAttemptSlice, remainingForNewWork)
```

Vždy se provede jeden okamžitý `LOCK_NB` pokus, i když `allowedWait = 0`; v takovém případě se už nespí. Každý skutečný sleep se odečte ze společného wait budgetu a omezí tak všechny další locky v requestu. Sleep se clampuje před zavoláním, aby nepřeskočil deadline.

Deadline začíná na vstupu do routeru a zahrnuje validaci, parsování, HMAC, nonce, collect, publish a přípravu odpovědi. PHP nedokáže preemptnout jeden pomalý `fwrite`, `fflush`, `rename`, directory scan nebo síťový výstup; deadline proto garantuje hlavně to, že aplikace sama nepřidá další neomezenou práci. Nasazovací SLO musí zvlášť měřit reálný disk.

Pokud se deadline vyčerpá během načítání těla, server tělo dočte a dokončí hash nutný pro autentizaci, ale po ověření podpisu vrátí `503 queue_busy` ještě před nonce claimem a před změnou front. Tím se drahý nebo pomalý vstup nepromění v pozdní částečnou mutaci.

### 5.2 Validace, parsování a omezení fan-outu

Pořadí zůstává bezpečnostně stejné: preconditions → úplné načtení a hash přes přesné bajty → syntaktická validace → ověření HMAC → teprve potom diskové mutace.

Během parsování server vede pořadí prvního výskytu destinací. Prvních `MAX_DESTINATIONS_PER_SYNC` cílů bufferuje v dnešní cílové struktuře. Pro další cíle:

- řádek nadále plně validuje;
- UUID přidá do deferred v původním pořadí;
- payload nezařadí do zápisového bufferu;
- nastaví `processingLimited=true`.

Stávající limity paketů a bajtů se aplikují současně. Výsledné accepted/deferred seznamy se na konci sestaví podle původních pořadových indexů, nikoli podle pořadí frontových locků. Tím je partition deterministická i při částečné akceptaci více destinací.

### 5.3 Persistentní nonce

Je-li `PERSIST_NONCES=true`, nonce lock používá tentýž deadline a tentýž sdílený wait budget; doporučený per-attempt strop je 2 ms. Kontrola zůstává fail-closed:

- replay → `401`;
- lock nezískán nebo deadline vyčerpán → `503 queue_busy`;
- chyba persistence → `503 queue_unavailable`.

Nonce se kvůli rychlosti nesmí přeskočit. Klient musí pro každý nový HTTP pokus, včetně retry stejné množiny UUID, vytvořit nový timestamp, nonce a podpis. Data zůstávají identická a přijímač případné duplicity odstraní podle UUID.

### 5.4 Ack a vyzvednutí vlastní fronty mají prioritu

Po nonce server jako první zkusí jediný lock vlastní fronty volajícího. Důvodem je, aby fan-out do cizích front nikdy neodebral možnost potvrdit a vyzvednout příchozí data.

Při získání locku:

1. idempotentně odstraní validované `.sending` uvedené v ack;
2. provede jen bounded lokální scan;
3. přejmenuje vybrané `.data` na `.sending` do `MAX_RESPONSE_BYTES`;
4. uvolní lock;
5. nastaví `inboundComplete=true`; `responseTruncated` zůstává výhradně informací, že server bezpečně zjistil další nevyzvednutá data.

Při nezískání locku nebo při nedostatku času:

- nesmaže žádný ack;
- neclaimne žádný batch;
- vrátí `inboundComplete=false` a `processingLimited=true`;
- může pokračovat k outbound publish, pokud zbývá bezpečný čas;
- klient ack ponechá a vyzvednutí zopakuje s jitterem.

`inboundComplete=false` nesmí být maskováno jako autoritativní prázdná fronta. Je odlišné od `responseTruncated=true`: první znamená „frontu se nepodařilo úplně prohlédnout“, druhé „fronta byla prohlédnuta a další data skutečně zůstala“.

### 5.5 Publikování cílových dávek

Vybrané destinace se zpracují v pořadí prvního výskytu. Server stále drží jen jeden lock. Pro každou destinaci:

1. ověří, že zbývá `SYNC_RESPONSE_RESERVE_MS` a společný processing budget;
2. provede okamžitý `LOCK_NB`, případně krátké retry omezené všemi třemi mezemi;
3. po nezískání locku označí všechna UUID této cílové dávky jako deferred a pokračuje bez dalšího čekání;
4. pod lockem ověří/rebuildne lokální kapacitu v bounded režimu;
5. při plné frontě označí cílová UUID jako deferred, starší data nemění;
6. zapíše `.writing`, provede `fflush` a volitelný `fsync` podle konfigurace;
7. před `rename` při vyčerpaném deadline `.writing` odstraní a dávku odloží;
8. po úspěšném `rename` považuje celý cílový batch za commitnutý a jeho UUID okamžitě eviduje jako accepted;
9. dokončí povinný krátký commit tail a lock uvolní v `finally`.

Jakmile nezbývá response reserve, všechny dosud nezpracované cílové dávky se bez pokusu o lock označí deferred. Tato operace je čistě paměťová a její cena je omezená maximálním počtem přijatých záznamů.

Server se nesnaží o rollback dříve commitnutých destinací, pokud pozdější destinace selže nebo vyčerpá deadline. Rollback již viditelného `.data` není na běžném filesystemu bezpečný a mohl by smazat batch právě claimnutý příjemcem.

### 5.6 Sestavení odpovědi

Úspěšný řídicí záznam rozšíří stávající smlouvu například takto:

```json
{"type":"sync-result","acceptedPacketUuids":["…"],"deferredPacketUuids":["…"],"acceptedPackets":12,"acceptedBytes":8400,"responseTruncated":false,"inboundComplete":true,"processingLimited":true,"recommendedRequestBytes":65536,"recommendedRequestPackets":100,"recommendedRequestDestinations":4,"retryAfterMs":50}
```

Význam nových polí:

- `inboundComplete`: server autoritativně dokončil ack/collect vlastní fronty;
- `processingLimited`: alespoň část jinak validní práce byla odložena kvůli fan-out limitu, deadline, kontenci nebo kapacitě;
- `recommendedRequestDestinations`: horní doporučení klientovi pro další request, nikdy vyšší než serverový hard limit;
- `retryAfterMs`: nejkratší doporučená prodleva před zrychleným retry; klient přidá jitter a při opakování exponenciálně zpomaluje.

Per-destination příčina se do odpovědi nepřidává. Klient potřebuje vědět, která UUID zůstala, nikoli zda konkrétní příjemce existuje, má plnou frontu nebo právě držel lock. Podrobnosti zůstanou v agregovaných serverových metrikách bez IP, UUID a payloadu.

HTTP politika:

- `200` s přesným sync-result se vrací, pokud lze bezpečně popsat částečný nebo úplný výsledek;
- `429` zůstává pro request, v němž nebylo nic publikováno ani doručeno a relevantní fronty jsou autoritativně plné;
- `503` zůstává pro stav před bezpečně popsatelným výsledkem, zejména nedostupný root, fail-closed nonce, vyčerpaný deadline před mutacemi nebo nečitelný storage stav;
- pokud už byla některá cílová dávka commitnuta, server se musí pokusit vrátit `200` s přesným accepted/deferred výsledkem; při ztrátě spojení klient zachová vše a případný retry vytvoří pouze povolené duplicity.

`Retry-After` u `429/503` může zůstat celočíselný v sekundách. Jemnější scheduling po částečném `200` používá `retryAfterMs`.

### 5.7 Odpověď a sweeper

Claimnuté `.sending` soubory se streamují bez frontového locku jako dnes. `SYNC_RESPONSE_RESERVE_MS` je rezerva na sestavení řídicího řádku a zahájení výstupu, nikoli záruka rychlosti klientovy sítě.

Sweeper nesmí po `fastcgi_finish_request()` vytvořit nový 200ms budget. Použije původní `processingDeadlineNs`, nejvýše zbývající čas a pouze jeden okamžitý `LOCK_NB` pokus pro každý bounded krok. Pokud čas nezbývá, skončí bez práce. To je důležité i s `fastcgi_finish_request()`: klient už sice odpověď dostal, ale PHP worker je do ukončení skriptu stále obsazen.

Pro shared hosting je vhodné snížit sweep slice na nejvýše jednu nebo dvě fronty na request a nikdy při foreground requestu nečekat na jejich lock. Neprovedená údržba je availability/kapacitní riziko, nikoli důvod držet worker; musí ji dohánět další requesty nebo doporučený externí cron, pokud je dostupný.

## 6. Reakce klienta

Klientská smlouva je součástí korektnosti:

1. Z lokální odchozí fronty odstraní pouze UUID z `acceptedPacketUuids`.
2. UUID z `deferredPacketUuids` ponechá beze změny pro další cyklus.
3. Při `processingLimited=true` respektuje `retryAfterMs`, přidá jitter a sníží příští počet destinací alespoň na `recommendedRequestDestinations`.
4. Při opakované kontenci rotuje destinace po jednotlivých requestech; jedna horká fronta nesmí trvale blokovat ostatní. Po několika omezených výsledcích může přejít na jednu destinaci na request.
5. Při `inboundComplete=false` ponechá neodeslané acky a zopakuje vyzvednutí. `responseTruncated=true` vyvolá zrychlený další sync i při `inboundComplete=true`.
6. Při `429` nebo `503` a při síťové/parse chybě ponechá celý odchozí pokus i acky a použije exponenciální backoff s jitterem.
7. Každý retry podepíše novým timestampem a nonce, ale zachová původní paketová UUID.
8. Každou delivery deduplikuje podle `uuid`; `batchId` používá pro ack, nikoli jako identitu IP paketu.
9. Lokální scheduler nesmí vždy posílat stejných prvních osm destinací. Použije round-robin nad cílovými frontami, jinak by hard limit mohl sám vytvořit starvation.

Klient smí za běžného úspěchu dál používat adaptivní interval. `retryAfterMs` není příkaz k nekontrolovanému tight loopu; minimální prodleva se kombinuje s lokálním rate limitem.

## 7. Dopad na at-least-once doručení

At-least-once zůstává zachováno po jednotlivých UUID a cílových batchech:

- **Před rename:** chyba, lock contention nebo deadline znamená deferred; server netvrdí přijetí a klient data drží.
- **Po rename, před odpovědí:** batch je bezpečně ve frontě. Pokud odpověď nedorazí, klient UUID zopakuje a může vzniknout duplicitní batch. Příjemce jej deduplikuje podle UUID.
- **Po claimu na `.sending`, před nebo během odpovědi:** batch zůstane `.sending`; bez acku se později reclaimne a odešle znovu.
- **Po acku:** smazání `.sending` je bezpečné, protože ack dokládá dřívější klientské zpracování. Opakovaný ack je no-op.
- **Částečný fan-out:** různé destinace mohou být přijaty v různých cyklech a může se změnit jejich vzájemné časování. Uvnitř jedné cílové dávky se obsah a atomické publikování nemění.

Návrh nezavádí exactly-once ani transakci napříč destinacemi. Existující TTL může i nadále odstranit nevyzvednutá data podle současné provozní politiky; tato vlastnost není změnou V-04 a musí být klientovi i provozu nadále známá.

## 8. Bezpečnostní kompromisy

Přijatý kompromis je omezen na dostupnost, fairness a atomický rozsah:

- krátký wait slice zvýší počet dočasně deferred batchů při kontenci;
- autorizovaný nebo kompromitovaný klient může cílenou kontencí způsobovat starvation konkrétní fronty, ale nemůže tím držet každý PHP worker desítky sekund;
- multi-destination request není all-or-nothing; přesná accepted/deferred odpověď se stává normativní transakční smlouvou;
- pořadí doručení mezi různými destinacemi není garantováno;
- fan-out limit lze obejít více requesty, takže nenahrazuje autentizované rate limiting na webserveru/WAF, ale omezuje cenu jednoho requestu.

Následující kompromisy jsou nepřípustné:

- odstranit per-frontový lock;
- publikovat nebo hlásit accepted před atomickým rename;
- přeskočit nonce kontrolu v režimu `PERSIST_NONCES=true`;
- po timeoutu mazat již publikované `.data`;
- vrátit `inboundComplete=true`, pokud vlastní fronta nebyla zamčena a prohlédnuta;
- zahodit deferred payload na serveru i klientovi;
- spoléhat na `set_time_limit()` jako na transakční rollback.

Kryptografická integrita, autentizace, izolace cest a ochrana obsahu front se doporučením neoslabují. Zhoršuje se pouze možnost garantovat rychlý progress jedné horké destinace; naopak odolnost worker poolu proti autorizovanému fan-out DoS se výrazně zlepší.

## 9. Konfigurační a provozní pravidla

Runtime validace musí odmítnout konfiguraci, kde:

- `SYNC_PROCESSING_BUDGET_MS` není kladný a rozumně nižší než hostingový `max_execution_time`;
- `SYNC_LOCK_WAIT_BUDGET_MS >= SYNC_PROCESSING_BUDGET_MS - SYNC_RESPONSE_RESERVE_MS`;
- `SYNC_LOCK_ATTEMPT_SLICE_MS > SYNC_LOCK_WAIT_BUDGET_MS`;
- `MAX_DESTINATIONS_PER_SYNC` není v rozsahu 1 až 253;
- response reserve nenechává žádný prostor pro aplikační práci.

Staré `LOCK_TIMEOUT_MS` se nesmí zachovat jako per-lock budget. Buď se odstraní, nebo přejmenuje na explicitně agregovaný `SYNC_LOCK_WAIT_BUDGET_MS`. Dokumentace nesmí administrátorovi radit zvýšit per-frontový timeout při častém `503`; správná reakce je zátěžové měření, menší request/fan-out, client backoff nebo větší worker/storage kapacita.

Logy/metriky mají obsahovat pouze agregáty:

```text
elapsedMs, totalLockWaitMs, destinationsSeen, destinationsAttempted,
destinationsCommitted, packetsAccepted, packetsDeferred,
processingLimited, inboundComplete, deadlineReached
```

Nemají obsahovat token, nonce, podpis, payload ani seznam cílových IP/UUID. Pro produkční shared hosting je cílem `totalLockWaitMs <= 12 ms` pro každý request a deployment SLO například p99 aplikační latence do 150 ms při testu kontence 253 front. Wall-clock SLO musí mít rezervu na konkrétní disk, PHP SAPI, TLS a síť.

## 10. Nové testy

### 10.1 Deterministické unit testy deadline/lock politiky

Čas a sleeper musí být testovatelně injektovatelné; testy nesmějí spoléhat jen na flaky wall-clock měření.

1. **Sdílený wait budget:** 253 po sobě jdoucích neúspěšných lock pokusů spotřebuje nejvýše `SYNC_LOCK_WAIT_BUDGET_MS` dohromady. Po vyčerpání se provádí jen okamžitý `LOCK_NB`, žádný sleep.
2. **Absolutní deadline:** clock posunutý za deadline zabrání zahájení dalšího enqueue, collect i sweep kroku.
3. **Clamp sleepu:** poslední sleep nikdy nepřeskočí zbývající processing deadline ani společný wait budget o více než měřitelnou režii scheduleru.
4. **Fan-out limit:** request s 253 cíli vybere přesně prvních osm destinací podle prvního výskytu; ostatní UUID jsou deferred a jejich payload se nebufferuje pro zápis.
5. **Partition pořadí:** accepted a deferred jsou disjunktní, jejich sloučení podle uloženého vstupního indexu přesně reprodukuje pořadí validních data řádků.
6. **Deadline před rename:** `.writing` je odstraněn, `.data` nevznikne a UUID je deferred.
7. **Deadline po rename:** UUID je accepted, `.data` zůstane, commit tail doběhne a lock je uvolněn.
8. **Collect bez locku:** žádný ack se nesmaže, žádný `.data` se neclaimne a `inboundComplete=false`.
9. **Sweeper:** po vyčerpání původního deadline nevytvoří žádný další lokální timeout ani sleep.

### 10.2 Integrační testy protokolu

1. **253 uzamčených cílů:** pomocné procesy drží všechny cílové `queue.lock`. Podepsaný request skončí řízeně; instrumentovaný součet lock wait nepřekročí 12 ms a wall time je pod širokou platformní tolerancí, nikoli přibližně 50,6 s.
2. **Smíšený fan-out:** část z prvních osmi front je volná, část zamčená a část plná. Accepted obsahuje jen atomicky publikované UUID, ostatní jsou deferred; v žádné frontě nezůstane `.writing`.
3. **Více než osm volných cílů:** server publikuje nejvýše osm cílových batchů a zbytek přesně odloží, i když žádný lock nekoliduje.
4. **Zamčená vlastní fronta:** outbound do volného cíle může uspět, ale odpověď uvádí `inboundComplete=false`; ack zůstane a další retry obdrží původní delivery.
5. **Deadline před diskovými mutacemi:** pomalé/parsačně drahé platné tělo vede k `503` bez nonce claimu a bez queue artefaktů.
6. **Persistentní nonce pod kontencí:** držený nonce lock vrátí `503` v rámci společného budgetu; žádná cílová fronta se nezmění. Nový nonce po uvolnění projde, replay úspěšného nonce je nadále `401`.
7. **Wire kompatibilita:** klient správně reaguje na nová pole i na plně úspěšný výsledek; neznámá budoucí řídicí pole bezpečně ignoruje podle explicitní protokolové politiky.
8. **HTTP statusy:** ověří se přesná hranice mezi částečným `200`, nulovým-progress `429` a pre-result `503`; žádná větev po commitu nesmí odeslat chybové tělo, které skryje accepted výsledek, pokud je spojení stále zapisovatelné.

### 10.3 Souběh, recovery a at-least-once

1. **Čtyři a více workerů, náhodná kontence:** opakované retry nakonec doručí každé unikátní UUID alespoň jednou; žádný accepted paket nechybí a žádný deferred paket není klientem odstraněn.
2. **Horký a studený cíl:** trvale kontendovaná fronta nezabrání publikování do ostatních vybraných front; client round-robin po uvolnění doručí i horký cíl.
3. **Ztráta odpovědi po částečném commitu:** klient retryne stejná UUID s novým nonce; vzniklé duplicity jsou detekovány, žádný paket se neztratí a frontové soubory zůstávají syntakticky kompletní.
4. **Pád v každém commit bodu:** fault injection po vytvoření `.writing`, po částečném write, po `fflush`, po `rename`, po stat update a před odpovědí ověří popsané invarianty.
5. **Dva collectory:** ani při vyčerpání deadline nesmějí oba claimnout stejný `.data`; neúspěšný collector hlásí `inboundComplete=false`.
6. **Ack + přerušená odpověď:** acknutý starý `.sending` může zmizet, nově claimnutý batch zůstane `.sending` a po reclaimu se doručí znovu.
7. **Sweeper pod kontencí:** zamčené sweep fronty neprodlužují život PHP workeru součtem timeoutů; foreground enqueue/collect data se nepoškodí.

### 10.4 Zátěž a akceptační metriky

Na PHP 8.2, 8.3, 8.4 a 8.5 a na cílovém hostingu se změří:

- p50/p95/p99 celkové a aplikační latence pro 1, 8 a 253 destinací;
- `totalLockWaitMs`, který musí být omezen konstantou nezávislou na počtu cílů;
- počet workerů obsazených ještě po `fastcgi_finish_request()`;
- podíl accepted/deferred, retry rate a počet deduplikačních zásahů;
- růst `.writing`, `.data`, `.sending`, inode a diskové kvóty;
- fairness mezi horkými a studenými destinacemi.

Akceptace V-04 vyžaduje současně:

1. žádná cesta nevytváří `now + perLockTimeout` uvnitř smyčky přes destinace;
2. součet dobrovolného lock sleepu jednoho requestu je nejvýše 12 ms bez ohledu na 1 nebo 253 cílů;
3. foreground mutační pokusy skončí po jediném absolutním deadline a sweeper ho neobnoví;
4. server zpracuje nejvýše osm cílových front na request;
5. accepted/deferred přesně popisuje trvalý stav po odpovědi;
6. žádný deadline, lock failure, abort ani retry nezanechá částečně čitelný batch, nesmaže neacknutou delivery ani tiše neztratí deferred data;
7. deployment test prokáže výrazný pokles proti dnešnímu teoretickému 50,6s lock waitu a splní stanovené p99 SLO cílového hostingu.

## 11. Dopad na budoucí změnu `design2.md`

Při pozdější implementaci je nutné normativně změnit tvrzení, že selhání jediné cílové fronty je chybou celého requestu. Nová smlouva má říkat:

- atomickou jednotkou je jeden cílový batch;
- multi-destination request smí skončit přesným částečným `200`;
- pouze `acceptedPacketUuids` opravňuje klienta k odstranění dat;
- deadline, fan-out limit a kontence jsou bezpečné důvody pro deferred;
- `inboundComplete` odděluje autoritativně prázdnou frontu od neprovedeného collectu;
- žádný lock timeout se nesmí násobit počtem front.

Tato změna je záměrná odchylka od all-or-nothing požadavku v současném `design2.md`. Poskytuje slabší transakční semantiku napříč příjemci, ale výrazně lepší, předvídatelnou dostupnost shared PHP hostingu a zachovává integritu každé jednotlivé fronty i at-least-once doručení.
