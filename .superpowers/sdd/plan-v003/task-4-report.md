# Report úlohy 4: D04 a současná část TT05

Stav: DONE v rozsahu čtvrtého sekvenčního bloku. Společný časový limit je předaný jako neomezený. D05 nebylo zahájeno. Celá TT05 ani budoucí backpressure tímto blokem dokončené nejsou.

Při obnovení nebo předání kontextu nejprve obnov celý účinný globální blok instrukcí z reálného CODEX_HOME a jeho AGENTS.override.md nebo AGENTS.md, případně z úplného hostem dodaného bloku. Ověř obě přesné samostatné hranice před další věcnou prací. Tento report chráněný blok nenahrazuje.

Změny:

- `client.md`, Velikostní a spravedlivé plánování: doplněn současný limit počtu odchozích záznamů `maxDataRecordsPerRequest` z ověřené odpovědi `info` jako další mez vedle `maxRequestBodyBytes - 1024`. Přesné měření těla zůstává povinné. První kruhový průchod respektuje obě meze, další průchody doplňují pakety do vybraných záznamů podle bajtového rozpočtu. Zachované jsou FIFO, kruhová spravedlnost a permanentní drop neodeslatelného čela.
- Stejná část výslovně zachovává maximum běžného pollingu 5 s a označuje pre-poll admission backpressure B01–B05 za budoucí práci bez runtime zapojení. Odkazuje slovně na budoucí význam existujícího požadavku v části Příjem dat. Vlastní přepis této příchozí části patří D12.
- `Plans/2026-09-12-02-29-09/priskok-matice.md`: změněn jen řádek T05 a přidán odkaz na tento report.
- Tento report. Produkční kód, testy, protocol.md a tray_UX.md se neměnily.

Důkazy a rozhodnutí o testech:

- `client/src/core/queues/scheduler.cpp::planNextRequest` odečítá `SchedulerLimits::kBodyReserveBytes = 1024`, zastavuje výběr nových peerů při dosažení `maxDataRecordsPerRequest` a počítá přesnou serializovanou velikost auth řádku i datových záznamů. Při nedostatku místa ponechá odeslatelné čelo pro další request, samostatně neodeslatelné čelo odstraní s důvodem `oversized_for_request`. Kurzor posouvá podle prvního vybraného peeru.
- `client/src/core/runtime/network_worker.cpp::performInfo` zveřejňuje serverové limity až po úspěchu `ResponseParser::parseInfo`. `exchangeData` předává obě serverové meze plánovači. `buildDataBody` sestavuje skutečné NDJSON tělo. D04 zachovává existující požadavek konečného měření a nepřidává novou runtime garanci.
- `protocol.md`, 8.2–8.3 a 9.1, již uvádí aktivní limity v info, ověření GCM před použitím plaintextu a počet datových záznamů. Není nutná nová protokolová tabulka. Polling tray v tray_UX.md je samostatný prezentační mechanismus.
- `rg -n 'canAdmitAnotherPacket' client/src` nalezlo jen deklaraci a definici v packet_queues.h/.cpp, žádný runtime call site. Současná dokumentovaná odchozí omezení nejsou implementací budoucího B01–B05.
- Celý `client/tests/unit/test_queues.cpp` byl zkontrolován. `respects_the_record_count_limit` vybírá 3 záznamy z 8 peerů a ponechá 5 paketů. `rotates_the_starting_peer_between_requests` již přímo kombinuje `maxDataRecordsPerRequest = 1` se třemi postupnými requesty a střídáním peerů. Tento přesný současný scénář odpovídá povolené kombinaci početního limitu a spravedlnosti, proto nevznikl duplicitní test.
- `keeps_the_body_inside_the_budget` ověřuje bajtovou mez a zachování zbytku bez dropu. `permanently_drops_a_packet_that_can_never_fit` ověřuje odstranění velkého čela a odeslání následujícího paketu. `keeps_fifo_order_within_a_peer`, `push_front_restores_the_original_order` a `serves_every_non_empty_queue_before_topping_up` pokrývají izolované pořadí a kruhové doplňování. Netvrdíme nový test všech těchto vlastností současně při aktivních obou limitech.
- Prohlédnutý runtime test `worker_counts_handoff_retry_and_verified_queue_admission` má jiné integrační zaměření. Nebyl upravován ani rozšiřován o souběžné deferred/error scénáře. Harness používá substring filtr a připouští i nulový počet, proto jsou níže uvedeny skutečné počty.

Příkazy spuštěné z `C:\git\phpvpn`:

| Příkaz | Výsledek |
|---|---|
| `cmake --build work/build/debug --target phpvpn-client-tests` | exit 0, Ninja: no work to do, existující Debug target je aktuální |
| `work/build/debug/tests/phpvpn-client-tests.exe queues.` | 7 testů, 0 selhání, exit 0 |
| `work/build/debug/tests/phpvpn-client-tests.exe scheduler.` | 7 testů, 0 selhání, exit 0 |
| `git diff --check` | exit 0, bez whitespace chyb |

Build byl spuštěn s MSVC x64 prostředím načteným podle existujícího helperu `client/tools/build-x64.ps1` přes `vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath` a nalezený `Common7/Tools/VsDevCmd.bat -arch=x64 -host_arch=x64`, následovalo převzetí výstupu `set` do prostředí procesu. Nebyla měněna konfigurace buildu.

Celkem 14 cílených testů, 0 selhání. Produkční chyba nebyla odhalena, BLOCKED nenastal.

Kontrola rozsahu a formátu:

- Větev `tray_UX`, HEAD `da788b71feac031d5572b906e5d3ac51d4e87ae4`. Rozdíl vůči HEAD uvedenému v původním plánu byl ověřen podle aktuálního checkoutu, důkazy plánovače a runtime byly znovu načteny.
- Výchozí obsahové hashe 3 cílových dokumentů, 16 historických zdrojů, scheduler.cpp, network_worker.cpp, test_queues.cpp, test_config.cpp, test_runtime_status.cpp a progress.md jsou v ignorovaném `work/task-4-baseline-hashes.json`. Kopie `work/task-4-client-before.md` a `work/task-4-matrix-before.md` zachycují vstup včetně nekomitované práce D01–D03.
- Po úpravách zůstává shodných všech 24 chráněných souborů kromě záměrně změněného client.md. Jeho rozdíl proti začátku bloku je omezen na Velikostní a spravedlivé plánování. Rozdíl matice proti začátku bloku je jediný řádek T05.
- Nový lokální Markdown odkaz z T05 na tento report byl rozbalen vůči složce matice a cíl existuje. V client.md ani reportu další nový Markdown odkaz nevznikl. Název části Příjem dat a Velikostní a spravedlivé plánování v client.md existuje.
- `git diff --check` prošel. Git může u existujících souborů vypisovat upozornění na budoucí normalizaci LF na CRLF.

NOT RUN a rizika:

- Plná CTest sada, Release/ASAN, runtime suite, skutečný PHP relay, HTTP/WinHTTP integrace, služba, Wintun/VPN, GUI, instalace, E2E, výkonová a vizuální akceptace nebyly spuštěny. Izolované testy front a plánovače nejsou důkazem těchto vlastností.
- Budoucí B04 admission gate ani NetworkWorker deferred/error souběžné scénáře nebyly implementovány či nově ověřeny. B01–B05 zůstávají nedodané, R04 odložené.
- Starší normativní formulace příchozí ochrany v Příjem dat čeká na D12. Nová poznámka v upravené části její budoucí význam výslovně objasňuje, bez zásahu do dalšího sekvenčního bloku.
- Nebyl vytvořen worktree ani další agent, nebyl proveden commit, push, reset, checkout ani mazání. Neprovedla se mutace produkčního kódu ani nový testový scénář.
