# Report úlohy 12: D12 a R02

D12 dokumentačně dokončeno. Společný časový limit je předaný jako neomezený. Úloha končí po D12, B01–B05 zůstávají nedodané a posledním samostatným implementačním blokem. Produkční kód ani testy nebyly změněny.

Při obnovení nebo předání kontextu obnov celý účinný globální blok instrukcí z reálného CODEX_HOME a jeho AGENTS.override.md nebo AGENTS.md, případně z úplného hostem dodaného bloku. Před věcnou prací ověř obě přesné samostatné hranice. Tento report chráněný blok nenahrazuje.

## Rozsah a důkazy

Autorita: D00/D12, B01–B05 a TT05 v [plan-v003.md](../../../Plans/2026-09-12-02-29-09/plan-v003.md), [T05](../../../Plans/2026-09-12-02-29-09/T05.md), [intent-v004.md](../../../Intents/2026-09-12-02-29-09/intent-v004.md) a R02 v [rozhodnuti-v004.md](../../../Plans/2026-09-12-02-29-09/rozhodnuti-v004.md). Historický původ front a pollingu je S01 §2.12/§13 podle T05. Novější R02 zachovává maximum 5 s.

- [client.md, Pollingový interval](../../../client.md#pollingový-interval): zachováno schválené maximum 5000 ms. Současné low=max(20,serverMin) a high=min(5000,serverMax) nahrazují nepravdivý popis oboustranného clampu. Výslovně uvedeno omezení extrémů a budoucí oprava B03. Pět sekund není povinnost odeslat data při kapacitní blokaci ani pevný odstup zahájení HTTP requestů.
- [network_worker.cpp](../../../client/src/core/runtime/network_worker.cpp), performInfo, nextPollDelayMs a clampToRange: aktuální asymetrické meze, reset na low při provozu, zdvojnásobování při nečinnosti, návrat low při low > high. Serverové minimum nad 5000 tak může vrátit čekání nad 5 s. Jde o statický důkaz omezení, nikoli nový test hrany.
- [client.md, Příjem dat](../../../client.md#příjem-dat): úvod vyjímá plánovaný bod 7 z popisu dodaného zpracování. Bod 7 je výslovně plánovaný. Bod 8 s povinnou admission každého přijatého paketu a odmítnutím pouze nového paketu zůstává beze změny.
- [packet_queues.cpp](../../../client/src/core/queues/packet_queues.cpp), admitsLocked/tryEnqueue: globální limit a rezervace pod mutexem před vložením paketu. NetworkWorker::ingestRecord volá tryEnqueue pro každý validovaný paket a zvýší příchozí IPv4 čítače jen po přijetí. Při odmítnutí eviduje drop.
- `rg -n 'canAdmitAnotherPacket' client/src` nalezlo pouze deklaraci v packet_queues.h:104 a definici v packet_queues.cpp:198. [vpn_engine.cpp](../../../client/src/core/runtime/vpn_engine.cpp), runConnectedIteration, přímo volá exchangeData bez pre-poll gate. Existence helperu proto není dodaná ochrana a jeho současný výběr nového peeru nenahrazuje budoucí opravu B01.
- [Povinné testovací scénáře front a per-peer kvót](../../../client.md#povinné-testovací-scénáře-front-a-per-peer-kvót): bod 6 nyní popisuje současnou per-packet admission. Budoucí B04 je oddělený odstavec s odkladem/obnovou, FIFO, účty, stopem a hranami B03, bez tvrzení o proběhlé akceptaci.
- [protocol.md](../../../protocol.md), §8.2 a §15: serverové pollingové hodnoty jsou doporučení, výchozí 50–3200 ms, nejsou závazným klientským intervalem. Wire změna není potřebná. [tray_UX.md](../../../tray_UX.md) popisuje samostatný polling lokálního statusu, proto se nemění.
- Aktualizována pouze aktuální část T05 a řádek T05/R02 v [matici](../../../Plans/2026-09-12-02-29-09/priskok-matice.md). Historická analýza a evidence D04 zůstaly zachované.

## Existující testy a výsledky

Běh 12. 9. 2026, Windows, pracovní adresář C:\git\phpvpn, větev tray_UX, HEAD 1c25096111cdf20f901c58da6299ddde7450e229. Spuštěn existující Debug executable, bez nového buildu nebo změny konfigurace. Každý filtr měl nenulový počet testů.

Všechny příkazy níže mají prefix `work/build/debug/tests/phpvpn-client-tests.exe`:

| Argument příkazu | Počet | Výsledek |
|---|---:|---|
| `queues.` | 7 | PASS, 0 selhání |
| `scheduler.` | 7 | PASS, 0 selhání |
| `runtime.worker_counts_handoff_retry_and_verified_queue_admission` | 1 | PASS, 0 selhání |
| `runtime.worker_rejects_whole_batch_when_second_packet_has_wrong_addresses` | 1 | PASS, 0 selhání |
| `runtime.worker_accounts_partial_bodies_once_on_every_error` | 1 | PASS, 0 selhání |
| `runtime.cancelled_before_transport_handoff_does_not_count_a_request` | 1 | PASS, 0 selhání |

Celkem 18 různých existujících testů, 0 selhání. [test_queues.cpp](../../../client/tests/unit/test_queues.cpp) ověřuje globální/per-peer meze, FIFO, pushFront, drop počty, počet/velikost záznamů a kruhový výběr. [test_runtime_status.cpp](../../../client/tests/unit/test_runtime_status.cpp) v uvedených testech ověřuje deferred opakování a handoff účty, plnou incoming frontu bez přírůstku přijatých IPv4, odmítnutí celé vadné dávky, partial response accounting a zrušení před transportem se zachováním outbound paketu. Deferred test není souběžný test přírůstků během HTTP, cancel není test stopu při budoucí gate.

## Kontrola rozsahu a NOT RUN

- Vstupní SHA-256 všech 448 sledovaných souborů je uložen v work/d12-hashes.xml. Vstupní texty client.md, T05, matice, progress.md a reportů D10/D11 jsou v work/d12-docs.xml. Porovnání proti vstupu odděluje D12 od necommitovaných D10/D11. Diff D12 je omezen na tři uvedené dokumentační části client.md, aktuální část T05, jediný řádek matice a tento nový report. progress.md zůstává koordinátorovi.
- V01/V02: porovnání hashů potvrdilo 445 nezměněných sledovaných souborů a pouze tři zamýšlené změny proti vstupu. Diff chrání předchozí D01–D11, produkční kód, testy, historické zdroje, protocol.md a tray_UX.md. progress.md a reporty D10/D11 jsou obsahově shodné se vstupem. Důkazy odkazují na aktuální symboly a cílové části.
- V03/V06: ověřeno všech 77 lokálních odkazů v client.md, T05, matici a reportu, včetně 19 fragmentů. `git diff --check` bez whitespace chyb. Kontrola platí pro D12, není závěrečnou akceptací D00–D17.
- V04/V05: 5000 ms zachováno jako schválené maximum s pravdivou poznámkou o současných extrémech. Současná admission je oddělená od plánovaných B01–B05 a od serverového doporučení i tray timeru. R04 zůstává odložené.
- NOT RUN: build, plný CTest, Release/ASAN, skutečný PHP/WinHTTP relay, služba/SCM, Wintun/VPN, GUI, instalace, E2E, výkonová a vizuální akceptace. Existující Debug běh není ověřením nově sestaveného vydání.
- NOT RUN a nedodáno: nové B04, pre-poll odklad/obnova, stop při kapacitní blokaci, oboustranné krajní intervaly B03, souběžný příjem během HTTP s deferred/error návratem na čelo. Tyto scénáře nebyly přidány ani nahrazeny unit PASS.
- Rizika současného chování: gate chybí, stažené pakety mohou být při plné frontě odmítnuty. Extrémní serverové minimum může překročit schválených 5 s. Budoucí předběžná brzda nebude rezervací celé odpovědi ani zárukou bezeztrátovosti. D12 tyto vlastnosti neopravuje v kódu.
- Nebyl vytvořen další agent ani worktree, nebyl proveden commit, push, reset, checkout ani mazání. D13 nebylo zahájeno.
