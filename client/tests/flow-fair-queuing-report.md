# Ověření symetrického flow fair queuing

Datum: 19. 9. 2026, Windows x64, MSVC 2022, Ninja. Větev `tray_UX`, výchozí HEAD při závěrečné validaci `0c725c9b897e09dd0b169a015cfa12dcb2e7e261`. Změny jsou v pracovním stromu bez commitu a PR. Předem existující změna `.intent-builder-active` nebyla upravována.

Rozsah odpovídá [intentu v005](../../Intents/2026-09-18-19-22-52/intent-v005.md) a [plánu v001](../../Plans/2026-09-18-19-22-52/plan-v001.md). Implementace, dokumentace a níže uvedené automatické testy jsou dokončené. Skutečný SCM/Wintun přenos zůstává neověřený, protože runner odmítl kolizi existující VPN trasy ještě v preflight kontrole.

## Výsledné chování

- Každý směr používá stejnou implementaci 16 flow sub-bucketů na peer, přesnou směrovou pětici, procesní HMAC-SHA-256 fallback a bajtové DRR s kvantem 65535.
- Výchozí globální limity jsou 134217728 B, oddělená peer minima 262144 B a TTL 5000 ms. Chybějící pole se doplňují pouze v paměti. Starý společný název minima je neznámý člen.
- Logické rezervace zahrnují waiting i held pakety. Move-only lease drží kapacitu až do potvrzení. Rollback vrací stejné uzly bez alokace, kopie payloadu nebo obnovy stáří.
- Monotónní expirace odstraňuje pouze starý prefix. Index obsahuje jednu položku na neprázdný bucket. Převzetí vlastníka klasifikuje nejvýše 64 čekajících paketů.
- Diagnostika obsahuje čekající a rozpracovanou obsazenost, počet aktivních bucketů a směrové i celkové expirace bez flow klíčů nebo tajemství.
- Serverový PHP kód a drátové formáty se nezměnily. Lokální pořadí výběru mezi flow se změnilo, FIFO platí uvnitř sub-bucketu.

## Provedená validace

Příkazy CMake/CTest byly spuštěny z `C:\git\phpvpn\client` po načtení prostředí `VsDevCmd.bat -no_logo -arch=x64 -host_arch=x64`.

| Příkaz | Výsledek |
| --- | --- |
| `cmake --preset debug` a `cmake --build --preset debug` | PASS, všechny cíle |
| `ctest --preset debug` | PASS 5/5 CTest cílů, 53,84 s |
| Hlavní `phpvpn-client-tests` v Debug | PASS 419/419, 37,73 s |
| `cmake --preset asan` a `cmake --build --preset asan -j 4` | PASS, všechny cíle |
| `ctest --preset asan -R '^phpvpn-client-tests$' --output-on-failure` | PASS 419/419, 57,09 s, bez hlášení ASan |
| `cmake --preset release` a `cmake --build --preset release -j 4` | PASS, všechny cíle, RelWithDebInfo |
| `ctest --preset release -R '^phpvpn-client-tests$' --output-on-failure` | PASS 419/419, 35,99 s |
| Poslední doplnění přesných výchozích a asymetrických IPC hodnot | Rebuild `phpvpn-client-tests` a filtr `wizard_ipc` ve všech třech presetech, vždy PASS 15/15 |
| `client\tools\run-server-contract.ps1` | PASS 5/5 skutečných PHP scénářů, 0,57 s |
| PHP kontrakt v úplném Debug CTest | Znovu PASS 5/5, 0,54 s |
| `client\tools\run-service-e2e.ps1 -BuildOnly` | PASS, kontroler a testovací služba sestaveny |
| `client\tools\run-service-e2e.ps1` | FAIL v preflight, přenos SCM/Wintun NOT RUN |

Debug CTest obsahoval hlavní sadu, `phpvpn-package-upgrade`, `phpvpn-package-feedback`, `phpvpn-logger-tests` a `phpvpn-contract-tests`. Běžná nativní UI okna nejsou novým požadavkem flow změny. Sestavení UI cílů není tvrzením o ručním UX testu. Před změnami prošla hlavní Debug sada za 35,50 s. Žádné známé selhání baseline se nepoužilo k omluvení výsledku.

Kontrakt používal izolovaný `wizard-default` fixture na `http://127.0.0.1:18093/phpvpn.php`, generovaný přes `tools\deploy-test.ps1`, a `D:\nginx\php8.5.9\php.exe -S 127.0.0.1:18093 -t C:\git\phpvpn\work\flow-fairness-php`. `PHPVPN_BUILD_CONTRACT_TESTS=ON` a `PHPVPN_CONTRACT_URL` byly nastaveny v kontraktovém buildu. Vlastní PHP proces byl po testech zastaven.

Aktuální Wintun chyba je přesně `IPv4 route conflict: 10.99.99.255/32 on PHPVPN`, exit 1 dne 19. 9. 2026 v 00:24 místního času. Existující VPN nebyla odpojena a její trasy nebyly upraveny. Staré soubory `service-e2e.log` a `service-e2e.cleanup.log` z 14. 9. nejsou důkazem tohoto běhu. Pro uzavření této kontroly je třeba spustit stejný izolovaný runner na hostu bez kolize prefixu. Komponované testy níže používají produkční NetworkWorker, kryptografii, queue a TunnelOutputWorker, ale nenahrazují skutečný Wintun.

Uchované důkazy jsou v [work/flow-fairness-evidence](../../work/flow-fairness-evidence): úplné logy všech tří hlavních sad, CTest souhrny, poslední IPC testy, PHP kontrakt a aktuální Wintun preflight error/exit. Build logy jsou v `work/flow-final-*-build.log`. Adresář `work` je lokální a není verzovaný.

## Matice 31 akceptačních kritérií

Všechny uvedené testy prošly. Označení sady odpovídá skutečnému substring filtru jediného agregovaného executable `phpvpn-client-tests`, nikoli samostatnému CTest cíli.

| # | Ověření |
| --- | --- |
| 1 | `config.flow_queue_defaults_are_exact_and_only_materialized_in_memory` a `wizard_ipc.context_rejects_impossible_budgets_and_overflow`, oba limity přesně 134217728 |
| 2 | Stejné config/IPC testy, obě minima přesně 262144, explicitní asymetrie a nezávislé defaulty |
| 3 | `queues` a `queue_drr.heavy_flow_leaves_other_bucket_minima_and_remainders_are_exact`, 16 slotů, výchozí M=16384, včetně P=1/15/16/17 |
| 4 | Config raw JSON, `wizard_document.flow_queue_defaults_remain_omitted_and_explicit_runtime_survives_edit`, beze změny vynechaných polí |
| 5 | Config a strict Wizard IPC, samostatná hranice N*P a overflow pro oba směry |
| 6 | `flow_key.sixteen_owners_overflow_and_hash_errors_are_atomic_in_both_directions` |
| 7 | Tentýž test a stabilita skutečného CNG HMAC pro opakovanou pětici |
| 8 | `flow_key.hmac_is_stable_uses_all_sixteen_slots_and_has_collisions`, pevný vzorek pokrývá všech 16 indexů |
| 9 | `flow_key.owner_release_promotion_and_bounded_collision_backlog`, held reassignment a `promotion_never_duplicates_owner_in_another_bucket` |
| 10 | `queue_drr.scheduler_reaches_interactive_flow_before_bulk_backlog_is_drained` a produkční `flow_integration` |
| 11 | `tunnel_queues.real_output_worker_keeps_peer_rotation_and_flow_fairness` a produkční `flow_integration` |
| 12 | `queue_drr.equal_byte_quantum_not_equal_packet_counts_and_visit_continues`, 100 B proti 1000 B, pokračování návštěvy a deficit |
| 13 | `queue_drr.one_active_bucket_is_work_conserving_across_quantum_boundaries`, queue test skutečné půjčitelné kapacity |
| 14 | Rezervační hrany v `queues`, P a R v `queue_drr`, přijmutí do jiného bucket minima po vyčerpání excess |
| 15 | `queue_concurrency.barrier_producers_cannot_consume_rollback_capacity`, FIFO identifikátory, původní buffer pointer a pořadí před producenty |
| 16 | `flow_key.safe_transport_options_fragment_and_protocol_fallbacks`, MF, offset, DF, TCP/UDP délky, options, jiný protokol, neplatné IPv4 |
| 17 | Původní scheduler/runtime/response sady, peer rotace, oversized a přesné request limity, skutečný PHP transfer |
| 18 | `flow_key`, `queue_drr`, `queue_expiry`, `queue_concurrency`, `tunnel_queues`, `flow_integration` a původní `queues` |
| 19 | `flow_integration.production_network_and_tunnel_keep_interactive_flows_ahead_of_continuous_bulk`, outbound, inbound i současný provoz, doplňovaný bulk backlog |
| 20 | 419/419 ve třech buildech a 5/5 PHP kontraktů. Skutečný Wintun je samostatná neprovedená environmentální kontrola |
| 21 | Config a strict IPC: default 5000, explicitní hodnota, int64 maximum, nula, záporná, real, bool, string, null a overflow |
| 22 | `queue_expiry.exact_ttl_boundary_both_directions_and_clock_wraps`, 4999/5000/5001 ms, plus skutečný výstupní worker |
| 23 | `queue_expiry.only_expired_prefix_is_removed_and_fresh_tail_keeps_fifo` |
| 24 | Stejný prefix test a `tunnel_queues.expired_packets_never_reach_tunnel_but_fresh_tail_does` |
| 25 | `queue_expiry.rollback_preserves_original_age_and_capacity_until_ack` a 12 chybových cest `flow_integration.every_request_failure_rolls_back_original_nodes_age_and_order` |
| 26 | `queue_expiry.pressure_finds_other_peer_then_stops_as_soon_as_admission_succeeds`, referenční účetnictví a indexové invarianty |
| 27 | TestClock, unsigned a signed hranice, TTL=1 a int64 maximum, nezávislý wall clock, sequence compaction včetně held paketů |
| 28 | Audit runtime dokumentace v `client.md`, shoda s config, IPC, Wizard a lifecycle testy |
| 29 | Audit hierarchie, klasifikace, HMAC, DRR, A/L rezervací, paměti, lease/rollback, TTL a diagnostiky proti implementaci |
| 30 | `protocol.md` potvrzuje nezměněné formáty a odděluje lokální plánování/TTL od serverového protokolu |
| 31 | Audit aktivních dokumentačních ploch, `client.md`, `protocol.md` a E2E README. Historické intenty/plány zůstaly neměnné. Starý název minima v aktivním kódu zůstal pouze v negativním testu |

Test `flow_integration.partial_ack_commits_only_confirmed_peer_record` navíc ověřuje potvrzení jednoho ze dvou peer recordů. Chybová matice pokrývá tři generování náhodných hodnot, seal, cancellation, transportní chybu, HTTP 503, vadný MAC, deferred, výjimku transportu, chybějící kontrakt a chybu časového snímku. `flow_lifecycle` ověřuje změnu TTL až při novém Connectu, zachování hasheru a selhání inicializace před HTTP/tunelem. Původní recovery test ověřuje zachování přijatého TTL i po mezitím uložené změně.

Souběhový test používá čtyři producenty po 1000 paketech, konzumenta a souběžné diagnostické snímky. Referenční test s 3000 smíšenými operacemi používá seed `0x51f09a17` a přepočítává waiting/held bajty, položky, vlastníky, sequence, A/L a heap z aktuálních uzlů. Bariérový test ověřuje, že dva producenti nemohou obsadit kapacitu nezbytnou pro rollback.

## Měřený výkon a paměť

Release microbenchmark používá skutečný CNG HMAC, testovací G=1 GiB a P=0, 1 nebo 252 peerů, 1/16/256 možných flow klíčů a pakety 100 nebo 65535 B. Předplní nejvýše 16 paketů na peer a provede 2048 dvojic enqueue/select. Varianta 256 flow znamená proměnlivé klíče v tomto pracovním vzorku, nikoli 256 trvale plných backlogů každého peeru. Časy nezahrnují tvorbu testovacího paketu, šifrování payloadu, HTTP nebo tunel.

| Peery | Flow | B/paket | Enqueue součet µs | Select součet µs | Cleanup µs | HMAC volání |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 1 | 100 | 865 | 1114 | 3 | 0 |
| 1 | 1 | 65535 | 945 | 1548 | 32 | 0 |
| 1 | 16 | 100 | 1134 | 1269 | 7 | 0 |
| 1 | 16 | 65535 | 1196 | 1435 | 81 | 0 |
| 1 | 256 | 100 | 1392 | 1506 | 9 | 252 |
| 1 | 256 | 65535 | 1495 | 1547 | 109 | 254 |
| 252 | 1 | 100 | 1120 | 2010 | 468 | 0 |
| 252 | 1 | 65535 | 1364 | 2717 | 1718 | 0 |
| 252 | 16 | 100 | 1380 | 1675 | 2860 | 0 |
| 252 | 16 | 65535 | 2384 | 3108 | 36700 | 0 |
| 252 | 256 | 100 | 1879 | 2207 | 5217 | 347 |
| 252 | 256 | 65535 | 2044 | 2774 | 29066 | 350 |

Odvozená rychlost těchto samotných enqueue/select operací je přibližně 0,37 až 1,03 milionu dvojic za sekundu. Není to propustnost VPN a test nemá časový pass/fail práh. Instrumentace naměřila nejvýše 8438 klasifikací včetně předplnění a údržby vlastníků. Cleanup navštívil nejvýše počet indexovaných čel plus jednu konečnou kontrolu. Index nikdy nepřekročil 16*N. Samostatný test s kolizním backlogem 10000 paketů ověřuje omezení práce při převzetí vlastníka.

Release velikosti struktur: QueuedPacket 48 B, peer 1560 B včetně 16 bucketů po 96 B, heap položka 16 B, stav lease 40 B. Debug: 56/1688/104/16/48 B. Nezahrnují odkazy list uzlů, sentinelové alokace ani režii alokátoru. Payload buffer se přesouvá, rezervace nepředalokuje G nebo N*P.

Největší měřený naplněný Release vzorek měl RSS 286752768 B a po odstranění expirací 20209664 B. Předplnění 252*16 maximálních paketů představuje asi 252 MiB samotného payloadu, proto tento test překračuje běžný výchozí směrový limit záměrnou testovací konfigurací. Paměť celého procesu se nemusí vrátit přesně na původní hodnotu. ASan a Debug mají vlastní režii. Nejdelší zde měřený souhrnný úklid trval 36,7 ms, nejde o univerzální horní mez.

## Závěrečný audit

`git diff --check` prošel a `graft build` úspěšně obnovil graf 273 souborů. Serverový `phpvpn.php`, historické intenty/plány a cizí změna stavového markeru nebyly součástí implementačních úprav. Neproběhla instalace ani publikace klienta. Pro opakování Wintun kontroly zůstává jasná podmínka hostu bez kolize `10.99.99.0/24`.
