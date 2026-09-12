# Report úlohy 3: D03 a TT03

Stav: DONE. Dokončen pouze třetí sekvenční blok plan-v003. Předaný společný časový limit je neomezený. D04 ani další bloky nebyly zahájeny.

Při obnovení nebo předání kontextu nejprve obnov celý účinný globální blok instrukcí z reálného CODEX_HOME a jeho AGENTS.override.md nebo AGENTS.md, případně z úplného hostem dodaného bloku. Ověř obě přesné samostatné hranice před další věcnou prací. Tento report chráněný blok nenahrazuje.

Změny:

- `client.md`, Konfigurační soubor: jediný payloadKey kontraktu peeru tento klient používá při odesílání i příjmu. Směr rozlišuje AAD obsahující src a dst. Odkaz na protocol.md, kapitolu 9.4, ponechává obecný wire kontrakt autoritativní. Pravidlo atomického odmítnutí vadné IPv4 dávky v části Příjem dat zůstalo zachované.
- `client/tests/unit/test_runtime_status.cpp`: dva nové integrační testy nad skutečným NetworkWorkerem a platformní CNG kryptografií, se stávajícím syntetickým ScriptedTransport. Přibyl pouze přepínač testové fixture pro obrácené payload AAD. Produkční kód se neměnil.
- `Plans/2026-09-12-02-29-09/priskok-matice.md`: pouze stav T03 a odkaz na tento report.
- Tento report. protocol.md ani tray_UX.md nevyžadovaly změnu.

Důkazy a pokrytí před doplněním:

- `network_worker.cpp::buildDataBody` vybírá kontrakt podle odchozího peeru a šifruje jeho payloadKey. `ingestRecord` vyžaduje lokální dst, vybírá kontrakt podle src a otevírá payload stejným payloadKey. V obou cestách používá dataAad s messageId, src, dst a IV.
- `payload_batch.cpp::decodeAndValidate` ověří všechny IPv4 hlavičky i vnitřní adresy před návratem paketů. NetworkWorker volá tuto funkci před první enqueue a před recordAcceptedPacket.
- `test_payload_batch.cpp` již měl izolované odmítnutí adresního nesouladu a celé dávky s vadným checksumem. `runtime.worker_counts_handoff_retry_and_verified_queue_admission` již ověřoval platný příjem, vadný MAC, payload tag, vadnou dávku a plnou frontu. Jeho badPacket fixture poškodí první paket, takže nedokazuje validní prefix následovaný adresní vadou.
- `test_crypto.cpp` má NIST/RFC vektory, poškozený tag a kanonické auth řetězce. `test_wire.cpp` má parser, korelaci a MAC/trailer. `test_base64url.cpp` a `test_iso8601.cpp` pokrývají kanonické formáty. Vyhledání dataAad a směrových scénářů v unit suite nenašlo explicitní obousměrné dešifrování jedním peer klíčem s negativní záměnou src/dst. Tyto mezery doplňují nové testy, základní vektory se neopakují.
- Harness `client/tests/unit/harness.cpp` používá substring filtr a umí skončit úspěšně i s nulou testů. Níže jsou proto uvedeny skutečné nenulové počty. Soubor test_runtime_status.cpp už je registrovaný v client/tests/CMakeLists.txt, registrace se neměnila.

Nové scénáře:

1. `runtime.worker_rejects_whole_batch_when_second_packet_has_wrong_addresses`: jedna autentizovaná dávka obsahuje validní příchozí paket a druhý paket s opačnými adresami. Oba mají samostatně validní strukturu i checksum. Výsledek exchangeData je úspěšně ověřená obálka, jedna odmítnutá dávka, nula přijatých paketů, prázdná fronta a nula přijatých bajtů i paketů v TrafficCounters. Jediný drop má důvod batch_invalid.
2. `runtime.worker_uses_one_peer_key_in_both_directions_and_binds_aad`: skutečně odeslaný záznam se dešifruje stejným klíčem kontraktu, kterým fixture šifruje odpověď přijatou NetworkWorkerem. Ověří se přesné pakety i směrovací adresy. Očekávané odchozí AAD je sestavené nezávisle podle wire formátu, včetně domény a řádku data. Obrácení src/dst v AAD odmítne odchozí ciphertext. Druhá příchozí odpověď má správné routingové údaje a platný data-mac, ale payload tag vytvořený nad obráceným AAD. Worker odmítne právě payload_tag_invalid, fronta zůstane prázdná a příjmové čítače zachovají pouze dřívější validní paket.

Provedené příkazy z C:\git\phpvpn, build s MSVC x64 prostředím načteným přes VsDevCmd.bat:

| Příkaz | Výsledek |
|---|---|
| `cmake --build work/build/debug --target phpvpn-client-tests` | exit 0, aktuální Debug testy sestaveny a slinkovány |
| `work/build/debug/tests/phpvpn-client-tests.exe runtime.worker_` | 4 testy, 0 selhání, exit 0, včetně obou nových testů |
| `work/build/debug/tests/phpvpn-client-tests.exe crypto.` | 13 testů, 0 selhání, exit 0 |
| `work/build/debug/tests/phpvpn-client-tests.exe wire.` | 14 testů, 0 selhání, exit 0 |
| `work/build/debug/tests/phpvpn-client-tests.exe payload.` | 12 testů, 0 selhání, exit 0 |
| `work/build/debug/tests/phpvpn-client-tests.exe base64url.` | 8 testů, 0 selhání, exit 0 |
| `work/build/debug/tests/phpvpn-client-tests.exe iso8601.` | 6 testů, 0 selhání, exit 0 |

Celkem 57 testů, 0 selhání. Produkční chyba nebyla zjištěna, stav BLOCKED nenastal.

Kontrola rozsahu a formátu:

- Větev tray_UX, HEAD da788b71feac031d5572b906e5d3ac51d4e87ae4. Výchozí nekomitované změny D01/D02 byly zachované. Úkol neměnil index ani historii.
- Výchozí SHA256 tří cílových dokumentů a šestnácti zdrojů byly zaznamenané do ignorovaného work/task-3-baseline-hashes.json. Stejný soubor navíc chrání test_config.cpp a progress.md před nechtěným přepisem předchozí práce. Kopie work/task-3-client-before.md umožňuje porovnat samotný blok D03.
- Porovnání hashů po úpravách potvrzuje shodu všech dvaceti chráněných souborů kromě záměrně změněného client.md. V client.md se proti počátku tohoto bloku změnil pouze výklad payloadKey v jednom odstavci. Produkční soubory nejsou v diffu.
- `git diff --check` prošel bez whitespace chyb. U existujících souborů Git upozorňuje na normalizaci LF na CRLF.
- Nové lokální Markdown odkazy z client.md na protocol.md a z řádku T03 na report byly rozbaleny vůči umístění zdrojových dokumentů a oba cíle existují. Odkazovaný nadpis 9.4 v protocol.md existuje.

NOT RUN: plná CTest sada, Release/ASAN, skutečný HTTP/PHP relay, WinHTTP transportní integrace, služba, Wintun a VPN, GUI, instalace, E2E a výkonová či vizuální akceptace. Použitý transport je syntetický, výsledek dokazuje vazbu NetworkWorker/parser/CNG/fronta/čítače, nikoli síťovou interoperabilitu. Mutace produkčního kódu pro red-green důkaz nebyla prováděna. Žádná nová produkční záruka nevzniká.

Nebyl vytvořen worktree ani další agent a nebyl proveden commit, push, reset, checkout ani mazání. R04 zůstává odložené a B01–B05 bez realizace.
