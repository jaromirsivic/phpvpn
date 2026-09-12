# Pre-poll admission backpressure Implementation Plan

> **For agentic workers:** Při budoucí autorizované realizaci použij `superpowers:executing-plans` a postupuj po úkolech s checkboxy. Tento plán neopravňuje vytvářet další agenty ani zahájit implementaci. Pracuj přímo v uloženém projektu bez worktree. Před pokračováním po předání nebo obnově kontextu obnov úplný účinný globální blok pravidel a ověř obě jeho přesné hraniční sentinelové řádky před jakoukoli věcnou akcí.

**Goal:** Doplnit konzervativní 20bajtovou admission gate před běžný provozní `data` request Windows služby a zajistit intervaly 20–5000 ms bez změny protokolu, diagnostiky nebo významu čítačů.

**Architecture:** `DirectionQueues` rozhodne pod vlastním mutexem pro nejhorší existující frontu a případného nového peera. `VpnEngine::runConnectedIteration` rozliší přeskočení od skutečného pokusu a síťová smyčka využije současné přerušitelné čekání. `NetworkWorker` normalizuje obě serverová doporučení před adaptací intervalu, vlastní exchange a per-packet admission zůstanou zachovány.

**Tech Stack:** C++20, MSVC AMD64, CMake 3.28+, Ninja, současný `PHPVPN_TEST` harness, fake transport/clock/tunnel, Windows platform crypto pro platné syntetické odpovědi.

**Spec:** [Autoritativní intent v001](../../Intents/2026-09-12-10-38-39/intent-v001.md). Historické identifikátory B01–B05 pocházejí z `Plans/2026-09-12-02-29-09/plan-v003.md`, tento plán je jejich konkretizací podle aktuálního kódu a nového intentu. Historické soubory se nepřepisují.

## Globální omezení

- „Nezavádět nové vlákno, timer, IPC příkaz, tray funkci ani změnu protokolu.“
- „Neřešit odložené R04 kolem Wintun vlastnictví a integrity.“
- „Neprovádět změny historických plánů, notes ani již hotové dokumentační konsolidace mimo nutný zápis výsledného chování.“
- „Nepovažovat konzervativní admission sondu za záruku bezeztrátovosti celé serverové odpovědi.“
- Práh je `PayloadBatch::kMinPacketBytes`, tedy 20 bajtů. Běžné intervaly jsou 20 až 5000 ms, blokace smí přeskočit více intervalů.
- Plánovací fáze vytváří pouze tento soubor. Nemění produkční kód, testy, cílové dokumenty, distribuci ani Git stav. Žádný commit, push, reset, checkout, mazání nebo nový kontext.
- Runtime testy nesmějí použít skutečnou VPN, relay, SCM instalaci ani Wintun. Nevzniká nový testovací framework.
- Společná doba delegované práce je podle zadání výslovně neomezená. Nevytvářej potomky.

## Zjištěný výchozí stav a mapa souborů

Kontrola kódu proběhla 12. 9. 2026 v `C:\git\phpvpn`. Projektový `AGENTS.md` nebyl nalezen. Účinný globální soubor `C:\Users\User\.codex\AGENTS.md` byl načten, obsahuje právě jednu počáteční a jednu koncovou samostatnou hranici. Chybějící projektový dokument se nenahrazuje historickým plánem.

| Soubor | Skutečný symbol a zjištění | Zamýšlený zásah |
|---|---|---|
| `client/src/core/queues/packet_queues.cpp` | `canAdmitAnotherPacket` volí nový peer, dokud zbývá slot, a teprve jinak největší existující frontu. `admitsLocked` už počítá rezervace všech možných peerů. | B01, obě přípustné kategorie ověřit pod jedním mutexem. |
| `client/src/core/queues/packet_queues.h` | `bool canAdmitAnotherPacket(std::size_t) const`, komentář nyní nepřesně mluví o největším paketu/requestu. | B01, přesný kontrakt boolean sondy, podpis zachovat. |
| `client/src/core/runtime/vpn_engine.cpp` | `runConnectedIteration` rovnou volá `exchangeData`, `runNetworkThread` vyhodnocuje `lastDataVerified_` a čeká přes `takeCommand`. | B02, gate a odlišení přeskočeného cyklu, společné čekání. |
| `client/src/core/runtime/vpn_engine.h` | Privátní `void runConnectedIteration()`, existující `friend struct VpnEngineTestAccess`. | B02, změnit pouze privátní návrat na `bool`, žádný veřejný status ani IPC typ. |
| `client/src/core/runtime/network_worker.cpp` | `performInfo` a `nextPollDelayMs` používají jednostranné min/max. `configure` již používá lokální clamp. | B03, společná normalizace doporučených mezí v obou místech. |
| `client/src/core/runtime/network_worker.h` | Komentář `nextPollDelayMs` slibuje průnik, který při disjunktních mezích neexistuje. | B03, komentář změnit na normalizovaná doporučení s lokálními tvrdými mezemi. |
| `client/tests/unit/test_queues.cpp` | `packetOf`, `address`, suites `queues`, `scheduler`. Sonda zatím nemá vlastní testy. | B01/B04, tabulkové hrany a test neměnnosti. |
| `client/tests/unit/test_runtime_status.cpp` | `TestClock`, `Gate`, `ScriptedTransport`, `IdleTunnel`, `FakeDriver`, `EngineFixture`, `VpnEngineTestAccess`, `WizardServiceFixture`. | B02–B04, rozšíření existujících fake objektů a runtime scénářů. |
| `client/tests/CMakeLists.txt`, `client/tests/unit/harness.cpp` | Oba testové soubory už jsou v `phpvpn-client-tests`. Filtr je jediný poziční substring a nula testů vrací úspěšný exit. | Pouze číst, registrace se nemění. V evidenci vyžadovat nenulový počet. |
| `client.md` | Pollingový interval, Příjem dat bod 7, testy front a závěrečný bod 2 výslovně uvádějí nedodané B. | B05, povýšit až po nové ověřené dodávce. |

Před realizací znovu přečíst dotčené soubory a `git status --short`, protože checkout je sdílený. Při plánování byly přítomny cizí změny `.intent-builder-active` a nový adresář intentu. Nezahrnovat je do vlastní dodávky.

## Pořadí a průběžná evidence

Realizační pořadí je B01, B02, B03, integrační dokončení B04, potom B05. B04 určuje testovací kontrakt už pro B01–B03. Není to dodatečné psaní testů po hotové implementaci. U každého chování nejprve přidat cílený test, sestavit jej proti původní implementaci, zaznamenat smysluplné RED, provést minimální změnu, zaznamenat GREEN. Kompilační chyba nebo prázdný filtr nejsou důkaz RED.

Budoucí realizátor založí novou datovanou evidenci ve svém novém adresáři `Plans`, s přesnými příkazy, časem, konfigurací, počty testů, návratovými kódy a odkazy na logy. Tento plán není realizační evidence. Commit není krokem tohoto zadání.

## B01: Oprava čisté kapacitní sondy

**Soubory:** `client/src/core/queues/packet_queues.h`, `.cpp`, `client/tests/unit/test_queues.cpp`.

**Rozhraní:** Konzument volá `bool DirectionQueues::canAdmitAnotherPacket(std::size_t packetSize) const`. Interně používá `bool admitsLocked(Ipv4Address peer, std::int64_t additionalBytes, DropReason& reason) const`. Nevytváří `PacketBytes` ani peer frontu.

**Invarianta:** Pro vložení velikosti S k libovolnému přípustnému peeru musí platit `totalBytes + S <= maxQueueBytes` a `sum(max(bytes_i + delta_i, minPerPeerQueueBytes)) <= maxQueueBytes`, včetně logických rezervací dosud nevytvořených peerů. Sonda je true pouze tehdy, projde-li největší existující fronta a také nový peer, pokud ještě zbývá slot.

- [ ] Přidat test, který zachytí dnešní chybu bez závislosti na pořadí adres:

```cpp
PHPVPN_TEST(queues, admission_probe_checks_largest_existing_with_free_slots) {
    DirectionQueues queues(1000, 100, 4);
    DropReason reason = DropReason::global_limit;
    PHPVPN_CHECK(queues.tryEnqueue(address("10.0.0.2"), packetOf(50), reason));
    PHPVPN_CHECK(queues.tryEnqueue(address("10.0.0.3"), packetOf(681), reason));
    PHPVPN_CHECK(!queues.canAdmitAnotherPacket(PayloadBatch::kMinPacketBytes));
    PHPVPN_CHECK_EQ(queues.statistics().occupiedBytes, std::int64_t(731));
    PHPVPN_CHECK_EQ(queues.statistics().peerQueueCount, std::size_t(2));
}
```

Rezervovaný součet je 981. Nový peer s 20 bajty se vejde do již účtované rezervace 100, největší existující peer ale po +20 vyžaduje 1001. RED musí selhat na negaci sondy, nikoli na přípravě fronty.

- [ ] Doplnit následující tabulku pomocí místního pole případů a `PHPVPN_CHECK_EQ`. Každý případ má nový objekt `DirectionQueues`, všechna uvedená vložení musí uspět. Čísla ve sloupci obsazení představují různé existující peery, pokud není uvedeno jinak.

| max / min / peers | Počáteční obsazení v bajtech | Sonda | Výsledek a důvod |
|---|---|---|---|
| 1000 / 100 / 4 | prázdné | 20 | true, nový peer |
| 20 / 0 / 1 | prázdné | 20 | true, přesná globální kapacita |
| 19 / 0 / 1 | prázdné | 20 | false, globální limit |
| 1000 / 100 / 4 | 680 | 20 | true, přesná rezervace 1000 |
| 1000 / 100 / 4 | 681 | 20 | false, rezervace 1001 |
| 1000 / 100 / 4 | 50, 681 | 20 | false, nejhorší není první peer |
| 100 / 0 / 2 | 40, 40 | 20 | true, všechny sloty, přesně 100 |
| 100 / 0 / 2 | 40, 41 | 20 | false, všechny sloty, 101 |
| 1000 / 100 / 4 | 700 | 101 | false, nový peer překračuje svou rezervaci i největší existující fronta limit |
| 0 / 0 / 0 | prázdné | 20 | false, žádný přípustný peer |

- [ ] Implementovat pod stávajícím `std::lock_guard` výběr maximálního `bytes` přes iterátor bez kopírování mapy. Je-li mapa neprázdná a `admitsLocked` pro maximum vrátí false, ihned vrátit false. Je-li `queues_.size() < maxPeers_`, nalézt chybějící číselnou adresu stejně jako dnes a vrátit výsledek `admitsLocked` i pro ni. Pokud další slot není a existující maximum prošlo, vrátit true. Prázdná mapa s nulovým počtem slotů vrací false.

```cpp
// Uvnitř canAdmitAnotherPacket, po získání stávajícího mutexu.
if (packetSize > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()))
    return false;
const auto bytes = static_cast<std::int64_t>(packetSize);
if (bytes > maxQueueBytes_ || totalBytes_ > maxQueueBytes_ - bytes)
    return false;
auto largest = queues_.end();
for (auto it = queues_.begin(); it != queues_.end(); ++it)
    if (largest == queues_.end() || it->second.bytes > largest->second.bytes)
        largest = it;
DropReason reason = DropReason::global_limit;
if (largest != queues_.end() && !admitsLocked(Ipv4Address(largest->first), bytes, reason))
    return false;
if (queues_.size() < maxPeers_) {
    std::uint32_t candidate = 0;
    while (queues_.find(candidate) != queues_.end()) ++candidate;
    return admitsLocked(Ipv4Address(candidate), bytes, reason);
}
return largest != queues_.end();
```

Přidat `<limits>`. Obrana převodu je lokální ke stávající sondě, nemění validátor konfigurace ani obecné admission API. Platná konfigurace již omezuje počet peerů a součin rezervací. Doplnit `SIZE_MAX` případ s očekávaným false na cílové 64bitové platformě.

- [ ] Opravit komentáře v hlavičce i implementaci. Vysvětlit, že nový peer neplatí celou rezervaci navíc, jeho neexistující rezervace už v součtu je. Přírůstek rezervace je monotónní podle obsazení, proto stačí největší existující fronta a případný nový peer.
- [ ] Pro true i false volat sondu opakovaně 100krát. Před a po porovnat všechny položky `statistics()`, `frontPacketSize`, `nonEmptyPeers` a související `DropCounters`. Následně vyprázdnit fronty a porovnat přesné `PacketBytes` v původním FIFO. Test neměnnosti nesmí spoléhat jen na počet bajtů. Přidat variantu po `popFront`, kdy zrušení prázdné fronty znovu uvolní slot.
- [ ] Sestavit a spustit filtry `queues.admission_probe` a `scheduler.`. Každý nový test pojmenovat prefixem `admission_probe`, vyžadovat nenulový počet a nula selhání.

**Rizika a ověření:** Žádné vnořené zamykání přes veřejné metody fronty, používat pouze `admitsLocked`. Žádné `operator[]`, `tryEnqueue`, `popFront` ani `pushFront` ze sondy. Statická kontrola těla potvrdí absenci alokací a mutací, runtime test neměnnosti doloží pozorovatelné účetnictví. Rozpočet celé odpovědi se nerezervuje.

## B02: Provozní gate a přerušitelné čekání

**Soubory:** `client/src/core/runtime/vpn_engine.cpp`, `.h`, `client/tests/unit/test_runtime_status.cpp`.

**Rozhraní:** Privátní `bool VpnEngine::runConnectedIteration()` vrací false pouze při kapacitním přeskočení, true při skutečném pokusu o exchange, ať uspěje nebo selže. Návrat true není synonymum úspěchu. `NetworkWorker::exchangeData` a `DataExchangeOutcome` se nemění.

**Invarianta:** Blokace před schedulerem neodebírá outbound, nevytváří request ID, ciphertext nebo tělo, nevolá transport, nemění datové čítače, dropy, `lastDataVerified_`, checkpointy ani recovery. Connected a running zůstávají zachovány. Kontrola kapacity je atomická uvnitř fronty, mutex se nedrží během HTTP ani čekání.

- [ ] Nejprve napsat runtime scénáře B04.R1, R2, R3 a R4 níže, sestavit a ověřit RED kvůli pokračujícím data requestům během skutečně prokázané blokace.
- [ ] Na začátek `runConnectedIteration` vložit gate a změnit privátní podpis:

```cpp
bool VpnEngine::runConnectedIteration() {
    if (!inbound_->canAdmitAnotherPacket(PayloadBatch::kMinPacketBytes))
        return false;
    // Následuje dosavadní exchangeData a zpracování jeho výsledku.
    // Dosavadní return v error větvi se změní na return true.
    // Za dosavadní notifyDataAvailable následuje return true.
}
```

- [ ] V connected větvi `runNetworkThread` uložit `const bool performed = runConnectedIteration()`. Dosavadní kontrolu `lastDataVerified_` a přechody error/recovering provést pouze v případě `performed`. Při false nic nepřepisovat na yes ani no. Chybové větve skutečného requestu musí nadále zachovat svůj `continue` po teardown.
- [ ] Zachovat periodický `performHealth` ve stávající sériové connected smyčce mimo podmínku `performed`, aby zůstal dostupný i při dlouhé blokaci. Jeho skutečné ověření smí aktualizovat pouze existující health/time stav. Přeskočený data poll z něj nesmí odvozovat úspěch dat, access checkpoint ani recovery. Neměnit pořadí synchronizace času vůči skutečnému úspěšnému data requestu. Rozdíl health stavů ve snapshotu při skutečně provedené resynchronizaci není vedlejší účinek přeskočeného data requestu.
- [ ] Použít společný konec connected větve a stávající obsluhu `takeCommand(remaining)`:

```cpp
const std::int64_t elapsed = clock_.monotonicMs() - before;
const std::int64_t delay = network_.nextPollDelayMs(
    performed && (packetsReceived_ > 0 || packetsSent_ > 0));
const std::int64_t remaining = performed
    ? (delay > elapsed ? delay - elapsed : 0)
    : delay;
```

Přeskočení se nepovažuje za traffic. Plný kladný delay na skipped větvi zabraňuje busy loopu také po delším health nebo při uměle posunutém clock. U skutečného requestu ponechat stávající odečet doby requestu. Nynější kumulativní význam `packetsReceived_`/`packetsSent_` pro skutečné cykly v tomto intentu nepřepisovat na jiný algoritmus adaptace.

- [ ] Nezměnit `takeCommand`, `pushCommand`, `disconnect` a `shutdown`. Zachovat probuzení přes `commandSignal_`, okamžité ukončení na stop a vrácení disconnect příkazu do hlavní větve. Nepoužít prostý `continue` přímo za gate, protože connected větev na začátku čeká nula ms.
- [ ] Spustit cílené B04 runtime testy a existující lifecycle/disconnect regrese. Ověřit, že se nevytvořila žádná nová role ani veřejné pole.

**Rizika:** Prostý návrat z dnešní void funkce bez rozlišení by síťovou smyčku odkázal na zastaralé `lastDataVerified_`. Zmíněný bool slouží jen k určení, zda má vyhodnotit skutečný výsledek. Po blokaci neprobíhá nový bootstrap. Gate nevkládat do `exchangeData`, protože tuto funkci používá `verifyAccessKey`. Periodické health může při dlouhé blokaci skutečně proběhnout, test datových čítačů musí odlišit tuto legitimní síťovou akci od data requestu.

## B03: Oboustranné omezení intervalů 20–5000 ms

**Soubory:** `client/src/core/runtime/network_worker.cpp`, `.h`, `client/tests/unit/test_runtime_status.cpp`.

**Rozhraní:** Zachovat `std::int64_t nextPollDelayMs(bool hadTraffic)`, `performInfo()` i původní `ServerLimits`. Doporučení se nepřepisují v uložené odpovědi serveru.

**Invarianta:** Obě efektivní meze i `currentPollIntervalMs_` leží v uzavřeném rozsahu 20–5000. Hodnoty mimo rozsah nesmějí uniknout z `performInfo` ani z traffic/reset/idle větve. Parser dnes načítá signed int64, ale nevynucuje pořadí pollingových mezí, a proto je potřeba deterministická lokální politika i pro obrácené doporučení.

- [ ] Rozšířit `ScriptedTransport` o `std::int64_t minPollMs = 80, maxPollMs = 1000`. V `info` sestavit plaintext se skutečnými číselnými hodnotami přes `std::to_string` před šifrováním. Nepřepisovat již autentizovaný ciphertext, takový test by testoval špatný GCM tag místo intervalů.
- [ ] Přidat `runtime.poll_interval_*` testy s `NetworkWorker`, `TestClock` a platným `performHealth`/`performInfo`. Nejdříve ověřit RED pro dvojici 6000/9000 a `nextPollDelayMs(true) == 5000`.
- [ ] Do anonymního namespace doplnit malý sdílený výpočet, užitý v `performInfo` i `nextPollDelayMs`:

```cpp
struct PollBounds { std::int64_t low; std::int64_t high; };
PollBounds pollBounds(std::int64_t serverMin, std::int64_t serverMax) noexcept {
    const auto low = clampToRange(serverMin, kMinPollIntervalMs, kMaxPollIntervalMs);
    const auto high = clampToRange(serverMax, kMinPollIntervalMs, kMaxPollIntervalMs);
    return {low, std::max(low, high)};
}
```

Obrácené doporučení se lokálně zúží na normalizované low, obdobně dnešnímu fallbacku `clampToRange`, ale vždy uvnitř 20–5000. Nevzniká nové pravidlo serverové validace. V `nextPollDelayMs` bez serverových limitů použít 20/5000. V `performInfo` clampovat dosavadní current do těchto mezí. Pro traffic nastavit low, pro idle ponechat dosavadní zdvojnásobování a finální clamp. Current nejprve udržovat v lokálních mezích, aby násobení nemohlo přetéct.

- [ ] Ověřit přesné hodnoty dle tabulky. Pro každou dvojici volat true a poté false alespoň 12krát, každá vrácená hodnota musí ležet v očekávaných mezích, poslední na high. Pak true musí vrátit low.

| server min / max | low / high | První hodnoty po true |
|---|---|---|
| 1 / 10 | 20 / 20 | 20, 20 |
| 20 / 20 | 20 / 20 | 20, 20 |
| 80 / 1000 | 80 / 1000 | 80, 160, 320, 640, 1000 |
| 20 / 5000 | 20 / 5000 | 20, 40, 80, následně až 5000 |
| 5000 / 5000 | 5000 / 5000 | 5000, 5000 |
| 6000 / 9000 | 5000 / 5000 | 5000, 5000 |
| 1 / 9000 | 20 / 5000 | 20, 40, následně až 5000 |
| 1000 / 80 | 1000 / 1000 | 1000, 1000 |
| INT64_MIN / INT64_MAX | 20 / 5000 | 20, 40, následně až 5000 |

- [ ] Navíc po `performInfo` zavolat false jako první, aby test pokryl clamp current už při přijetí info. Opakovat info s jinou dvojicí ve stejném workeru. Ověřit, že `worker.serverLimits()` nadále obsahuje původní serverová čísla, a reset/configure začne v lokálně platném intervalu.
- [ ] Aktualizovat pouze komentář `nextPollDelayMs` v hlavičce. Ověřit filtry `runtime.poll_interval` a stávající `runtime.worker_`.

**Rizika:** Oprava pouze `nextPollDelayMs` ponechá nesprávný current po `performInfo`. Parser nesmí být kvůli testům oslaben ani zpřísněn. Horní mez se týká zvoleného intervalu, není povinností odeslat při kapacitní blokaci a není horní mezí trvání HTTP timeoutu.

## B04: TDD runtime matice, synchronizace a regrese

**Soubory:** Testy pouze v `client/tests/unit/test_queues.cpp` a `client/tests/unit/test_runtime_status.cpp`. Použít stávající suites a build target. Rozšíření testových fake objektů nesmí změnit jejich výchozí chování pro ostatní testy.

**Testovací rozhraní:** `ScriptedTransport::hook` poskytuje pořadové číslo volání dle akce. První data je bootstrap access check. `TestClock::now` a `reads` jsou atomické. `Gate` poskytuje události, `eventually` má konečný limit. Přes existující `VpnEngineTestAccess` lze v testech sáhnout na queue a stav pouze v bezpečném bodě za transportní bariérou, před startem nebo po shutdown. Nekopírovat současně měněné unique_ptr bez synchronizace.

- [ ] Před B02 rozšířit `IdleTunnel` o opt-in řízený blokovaný `send`, seznam úspěšně zapsaných paketů pod mutexem a uvolnění v `signalStop`. `FakeDriver::startSession` předá testovou konfiguraci. `send` při blokaci uvolní mutex přes condition-variable wait, takže `signalStop` jej může přerušit. Zajistit RAII uvolnění všech gate i při selhání assertion, aby se teardown nezablokoval.
- [ ] Před startem fixture doplnit do `kConfig` top-level objekt `"runtime":{"minPerPeerQueueBytes":20,"maxInboundQueueBytes":5140}` a uložit jej do `EngineFixture::files->bytes`. Zachovat `/24` i stejné klíče, outbound limit zůstane výchozí. `config_validator.cpp::optionalPositiveInteger` vyžaduje kladná čísla, nulová rezervace v konfiguračním fixture není platná. Součet rezervací je 5040, maximální obsazení jediného peera je `5140 - 251 * 20 = 120` bajtů. Stejnou konfiguraci použít při tvorbě transportních odpovědí. Ověřit `parseAndValidateConfig` před startem.
- [ ] `VpnEngineTestAccess` smí pod `stateMutex_` získat stabilní queue pointer za transportní bariérou a přes veřejné `tryEnqueue` doplnit outbound. Inbound naplní skutečná ověřená odpověď. Žádné výměny queue objektů během běžících IO workerů. Snapshot sondy a front pořizovat ve fázi, kdy test garantuje běžící connected stav bez souběžného teardown.

### Přesné scénáře

| ID a jméno testu | Kroky a očekávaný výsledek |
|---|---|
| R1 `runtime.pre_poll_blocks_without_side_effects` | Bootstrap data číslo 1 vrátí platnou prázdnou odpověď. Provozní data číslo 2 vrátí validní 20bajtový IPv4 paket `.3` do `.2`. Writer jej odebere a zablokuje v send. Data číslo 3 vrátí 120bajtový paket téhož peera. Před jeho vrácením hook počká na signál, že první send je zadržen, poté bariéru opustí. Po zpracování ověřit `occupiedBytes == 120`, false sondy a dokončené čítače, potom baseline. Po alespoň třech 20ms intervalech žádné data číslo 4 ani request/response bytes. Porovnat všech devět `TrafficCountersSnapshot` polí, `responsesRejected`, `batchesRejected`, `peakResponseCiphertextBytes`, dropy, `lastDataVerified`, tři checkpointy, connected/running, recoveryCount a lastRecoveryAtUtc. Nevyžadovat shodu uptime. |
| R2 `runtime.pre_poll_blocks_outbound_and_resumes_fifo` | Stav z R1 doplnit o dva odchozí validní pakety 40 a 60 bajtů pro `.3`, vložené před baseline. Po dobu blokace zůstávají v původním FIFO a počtech, dataCalls nerostou. Uvolnit send, potvrdit pořadí obou inbound paketů v zaznamenaných send. Následující data request se objeví bez nového health/info bootstrapu nebo nového startSession. Zachytit jeho payload, ověřit/dešifrovat existujícími crypto helpers a `PayloadBatch::decodeAndValidate`, očekávat 40 a 60 v původním pořadí. Samotný počet data řádků FIFO neprokazuje. |
| R3 `runtime.pre_poll_wait_is_bounded_and_not_busy` | Držet blokovanou frontu, nastavit autentizované doporučení 20/20. Od baseline změřit přírůstek `TestClock::reads` bez dalších `status()` volání během 150 ms skutečného omezeného testového okna. Musí být konečný a menší než 200, dataCalls beze změny. Posunout fake now o 6000 ms, stále se nesmí odeslat data kvůli překročení 5 s. Poté uvolnit kapacitu a očekávat další request. Test měří nepřítomnost smyčky, ne počet CPU instrukcí. |
| R4 `runtime.pre_poll_disconnect_interrupts_wait`, `runtime.pre_poll_stop_interrupts_wait` | Dva oddělené fixture, doporučení 5000/5000, prokázaná blokace a předání do čekání. Zavolat disconnect, resp. shutdown. Do 1 s skutečného času ověřit disconnected/idle, resp. návrat shutdown a zastavené role. Fake tunnel se odblokuje na signalStop. Nesmí čekat 5 s ani být nutné posouvat fake čas o interval. Stav čítačů po disconnect se řídí existujícím resetem, porovnání neměnnosti patří před příkaz. |
| R5 `runtime.pre_poll_does_not_mask_verification` | Přes test access připravit blokaci s `lastDataVerified_ = unknown` a samostatně no, vyvolat jeden connected cyklus v bezpečně vlastněném fixture. Návrat false, stav a lastError beze změny. V živé smyčce blokace nesmí z této staré hodnoty vyvolat recovery. Skutečné neplatné data po uvolnění naopak musí vyvolat dosavadní rejection/recovery chování. |
| R6 `runtime.pre_poll_keeps_access_checks_real` | Přímý `NetworkWorker::verifyAccessKey` nad `DirectionQueues(0,0,1)` musí stále provést prázdný skutečný data request. Platný MAC uspěje, HTTP 401 a špatný MAC selžou. V engine bootstrap i recovery musí být před connected skutečné health, info a data. `ProbeClient::checkAccess` se stejnými chybami nemůže dostat úspěch z provozní gate. |
| R7 `runtime.pre_poll_preserves_per_packet_admission` | Sonda propustí 20, ale syntetická odpověď obsahuje více platných paketů nebo větší paket, který překročí zbylou kapacitu. Skutečný request se účtuje, přijaté pakety se účtují jednou, odmítnuté jen přes stávající drops. Již přijaté FIFO zůstává beze změny. |
| R8 `runtime.pre_poll_keeps_health_available` | Při blokaci posunout TestClock za timeSyncIntervalSeconds, vytvořit health se serverovým timestampem odpovídajícím fake času a novým requestId. Health skutečně proběhne sériově, dataCalls a data checkpointy zůstanou beze změny. Po failed health se zachová původní TimeSync snapshot a nesmí vzniknout recovery jen kvůli skipped data. |

**Poznámka k času:** `Clock` neřídí `std::condition_variable::wait_for` v `takeCommand`. Samotné zvýšení `TestClock::now` reálné čekání neukončí. Proto se fake clock používá pro časové hodnoty a detekci smyčky, skutečné čekání se ověřuje krátkým 20ms intervalem a skutečným command notify. Nevkládat produkční wait adapter ani nový timer jen pro testy. U R4 synchronizovat vstup do čekání pomocí testového pozorování po dokončení posledního cyklu a krátké rezervy, s dlouhým 5000ms intervalem. Konečný timeout je ochrana před visením testu, ne simulace funkčního chování.

**Poznámka k zadrženému writeru:** `TunnelOutputWorker::run` volá `popFront` ještě před `Tunnel::send`. Zablokování send proto snižuje účetnictví o paket v letu. Proto R1 používá dvě provozní odpovědi, první s 20 bajty zadržovanými v send, druhou se 120 bajty přesně vyplňujícími největší frontu. V její frontě zbývá nula rezervovaných bajtů. Kdyby se oba pakety přijaly v jediné odpovědi, výsledek by závisel na souběžném odčerpání writerem a nebyl by deterministický.

- [ ] Použít uvedenou dvouodpověďovou variantu jako hlavní R1/R2 fixture. Transportní hook neblokuje thread během měření skipped fáze, jinak by nula requestů dokazovala pouze bariéru.
- [ ] Doplnit test přesného návratu outbound FIFO po skutečném defer i po transportní chybě a špatném MAC. Použít pakety 40/60, během response hook zařadit třetí 80, po vrácení musí být FIFO 40/60/80. `exchangeData::returnEverything` a deferred větev se nemění.
- [ ] Po nových testech spustit existující `runtime.worker_counts_handoff_retry_and_verified_queue_admission`, `runtime.worker_accounts_partial_bodies_once_on_every_error`, `runtime.cancelled_before_transport_handoff_does_not_count_a_request`, `runtime.lifecycle_`, `runtime.disconnect_`, `runtime.connected_failure_`, `runtime.bootstrap_errors_`, `probe.`, `wizard_service.` a `traffic.`. Existující široká suite může obsahovat historické ownership testy, nepřidávat ani neměnit jejich obsah, R04 se neřeší.

**Rizika:** Nesynchronizovaný zápis `incoming`, flagů nebo hooků v fake transportu je datový race. Nastavovat před startem nebo za gate. Test R8 potřebuje časově konzistentní autentizovaný health, protože současný fixture health používá pevný timestamp. Žádný test nesmí tvrdit FIFO na základě velikosti fronty bez ověření obsahu.

## B05: Ověření dodávky a povýšení client.md

**Soubory:** Po nové ověřené realizaci `client.md` a nová realizační evidence. `protocol.md` a `tray_UX.md` pouze znovu porovnat s diffem. V plánovací fázi je žádný z nich neměněn.

**Invarianta:** Slovo dodáno a odstranění budoucího stavu až po nenulových cílených testech, regresích a sestavení. Existující staré reporty ani tento plán nejsou důkaz funkčnosti.

- [ ] V kořeni projektu spustit standardní Windows build, po jednotlivých RED/GREEN změnách bez potřeby skutečné VPN:

```powershell
& .\client\tools\build-x64.ps1 -Preset debug -Configure
& .\work\build\debug\tests\phpvpn-client-tests.exe queues.admission_probe
& .\work\build\debug\tests\phpvpn-client-tests.exe runtime.pre_poll
& .\work\build\debug\tests\phpvpn-client-tests.exe runtime.poll_interval
& .\work\build\debug\tests\phpvpn-client-tests.exe scheduler.
& .\work\build\debug\tests\phpvpn-client-tests.exe runtime.worker_
& .\work\build\debug\tests\phpvpn-client-tests.exe runtime.lifecycle_
& .\work\build\debug\tests\phpvpn-client-tests.exe runtime.disconnect_
& .\work\build\debug\tests\phpvpn-client-tests.exe runtime.connected_failure_
& .\work\build\debug\tests\phpvpn-client-tests.exe runtime.bootstrap_errors_
& .\work\build\debug\tests\phpvpn-client-tests.exe runtime.cancelled_before_transport
& .\work\build\debug\tests\phpvpn-client-tests.exe probe.
& .\work\build\debug\tests\phpvpn-client-tests.exe wizard_service.
& .\work\build\debug\tests\phpvpn-client-tests.exe traffic.
& .\client\tools\build-x64.ps1 -Preset debug -Test
```

Každý příkaz spouštět a vyhodnotit samostatně, nenavazovat další na selhání. Ověřit existenci exe v uvedeném standardním CMake výstupu a skutečný počet každého filtru. Build script sestaví všechny běžné cíle včetně core a služby. Před `-Test` ověřit CMake cache, aby nebyly zapnuté opt-in contract/service/UI cíle vyžadující vnější prostředí. Pokud zapnuté jsou, pro tuto akceptaci použít standardní unit CTest výběr `ctest --test-dir .\work\build\debug -R '^phpvpn-(client|logger)-tests$' --output-on-failure` a tuto odchylku uvést.

- [ ] Zaznamenat přesné RED/GREEN výsledky nových případů, počty regresí a build výsledek. Případné neprovedené platformní/distribuční testy označit NOT RUN, netvrdit úplnou produkční akceptaci. Tento intent nevyžaduje skutečný server ani Wintun.
- [ ] Teprve po úspěchu upravit `client.md` v těchto místech: `Pollingový interval` kolem řádků 210–223, konec `Velikostní a spravedlivé plánování` kolem 233, úvod `Příjem dat` a bod 7 kolem 235–251, `Povinné testovací scénáře front a per-peer kvót` kolem 253–262 a závěrečný bod 2 kolem 1066. Používat nadpisy jako stabilní kotvy, řádky jsou orientační.
- [ ] Nahradit jednostranný vzorec normalizací `low = clamp(serverMin,20,5000)`, `high = max(low,clamp(serverMax,20,5000))`. Popsat reset při provozu, idle adaptaci, dobu requestu a libovolný počet přeskočených intervalů při blokaci. Nevykládat 5000 jako záruku data requestu do pěti sekund.
- [ ] Příjem dat bod 7 přepsat do dodaného přítomného času: sonda pod mutexem pro 20 bajtů, nejhorší existující a případný nový peer, čisté přeskočení i s outbound backlogem, connected, přerušitelné čekání, obnovení bez bootstrapu, zachovaná admission jednotlivých paketů. Úvod oddělí tuto provozní gate od skutečných bootstrap/recovery access requestů a Wizard diagnostiky.
- [ ] Nahradit označení budoucí akceptace B04 odkazem na datovanou novou evidenci s přesnými výsledky. Odstranit pouze tvrzení o nedodaném B, zachovat text o otevřeném R04 a ostatních nepokrytých platformních branách.
- [ ] Provést `git diff --check` a věcnou kontrolu diffu. Porovnat všechny akceptační body intentu s B01–B05. Potvrdit, že nedošlo k úpravě historických plánů, serveru, tray, protokolu nebo distribuce. Ve výsledku uvést konkrétní soubory a nové testy, nikoli jen „testy prošly“.

### Výslovné vyhodnocení protocol.md a tray_UX.md

**`protocol.md`: nulová změna.** Oddíl u `limits` kolem řádku 316 výslovně označuje `minDataPollIntervalMs` a `maxDataPollIntervalMs` za doporučení, která nejsou pro klienta závazná. Lokální clamp a vynechání requestu tedy nemění serverový kontrakt. `NetworkWorker::buildDataBody`, `RequestBuilder`, `NdjsonResponseReader` i server `phpvpn.php` zůstávají beze změny. `verifyAccessKey` stále volá skutečný `exchangeData`. Wizard `ProbeClient::checkAccess` tvoří a odesílá vlastní `HttpAction::data`, provozní gate se na něj nevztahuje. Není nový field, akce, MAC vstup ani pravidlo serverové fronty.

**`tray_UX.md`: nulová změna.** Tray `StatusReader` v `client/apps/tray/status_reader.cpp` čte přes `readResponse(..., IpcCommand::get_status, ...)` paměťový stav služby. `client/apps/tray/tray_application.cpp` plánuje místní stavové čtení. Není vlastní VPN data scheduler, gate patří síťové smyčce služby. Formát `StatusSnapshot`, IPC i UI stav connected zůstávají stejné. Zastavený růst skutečných datových čítačů během blokace odpovídá stávajícímu významu statistik. Nevzniká nový tray stav ani akce a není důvod přepisovat UX03, UX06 nebo UX07.

Pokud následná realizace objeví skutečný dopad vyžadující jiný veřejný význam, nesmí jej maskovat nulovým diffem. Musí jej doložit konkrétním kódem a vrátit k posouzení rozsahu. Tento plán žádný takový dopad v kontrolovaném kódu nenalezl.

## Kontrola pokrytí intentu a stav tohoto artefaktu

| Akceptační bod intentu | Důkaz plánované dodávky |
|---|---|
| 1, blokace bez requestu a vedlejších účinků | B01, B02, R1, R2, R5 |
| 2, obnovení bez bootstrapu a zachování FIFO | R2, regresní deferred/error FIFO, B02 |
| 3, intervaly 20–5000, více skipped intervalů | B03 tabulka, R3, R4 |
| 4, cílené unit/runtime testy s přesnou evidencí | TDD u B01–B03, úplná B04, příkazy a report B05 |
| 5, dodané client.md a nulový dopad protocol/tray | podmíněné povýšení B05 a konkrétní vyhodnocení výše |

Plán je výsledkem čtení aktuální implementace a testů. Produkční změny, testové změny, build ani funkční testy nebyly v této plánovací fázi provedeny. Jejich stav je NOT RUN a implementace zůstává budoucí prací podle tohoto plánu.
