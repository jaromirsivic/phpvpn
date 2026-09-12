# Report úlohy 2: D02 a TT02

Stav: DONE. Realizován pouze druhý sekvenční blok podle plan-v003. Společný limit byl předán jako neomezený. D03 ani další kroky nepokračují.

Při obnovení nebo předání kontextu nejprve obnov celý účinný globální blok instrukcí z reálného CODEX_HOME a jeho AGENTS.override.md nebo AGENTS.md, případně z úplného hostem dodaného bloku. Ověř obě přesné samostatné hranice před další věcnou prací. Tento report chráněný blok nenahrazuje.

Změny a důkazy:

- `client.md`, pouze sekce Konfigurační soubor: kladné int64 hodnoty, zachované defaulty, přesné maximum responseTimeoutSeconds 2147483 s při defaultu 30 s a vztah maxNdjsonLineBytes <= maxResponseBodyBytes. Rovnost je dovolená, překročení obou mezí odmítá úplná validace i repair analýza. Absolutní monotónní deadline, /24–/29, ostatní typové a budget meze i timeSyncIntervalSeconds zůstaly zachované.
- `client/tests/unit/test_config.cpp:242`, nový test `config.runtime_boundaries_in_full_and_every_repair_mode`: NDJSON/body 4096/4096 a 4097/4096, timeout 2147483 a 2147484. Čtyři úplné validace a šestnáct repair analýz pokrývají přítomnou identitu, absenci IP, absenci klíče a absenci obojího. Překročení vyžaduje konkrétní config_invalid_limit. Přijaté hodnoty i stav identity se ověřují. Číselná maximum jsou nezávislá na implementačních konstantách.
- `Plans/2026-09-12-02-29-09/priskok-matice.md`, pouze stav řádku T02 a odkaz na tento report.
- Tento report. Produkční kód, protocol.md a tray_UX.md nebyly změněny.

`client/src/core/config/config_validator.cpp:51` kontroluje kladné int64. Řádky 253–259 odmítají NDJSON nad body a timeout nad kMaxHttpTimeoutSeconds. `parseAndValidateConfig` na řádku 320 volá validateConfigValues s false. `client/src/core/config/config_document.cpp:48` volá tutéž validaci s true pro repair. `client/src/core/config/config_document.h:9` stanoví 2147483. Defaulty jsou v `client/src/core/config/config.h`, prefixové hranice v `client/src/core/protocol/ipv4.h:51`. Protokolové doporučení 15 s není konfigurační timeout, proto protocol.md nepotřebuje změnu. UX meze zůstávají platné.

Existující pokrytí jsem ověřila před doplněním. `test_config.cpp` již pokrýval defaulty, nekladný timeout, CIDR ve všech repair režimech a rozpočty obou front včetně přetečení. `test_wizard_document.cpp:130` ověřuje skutečný DOM při Finish a `test_config_store.cpp:141` publikační CIDR gate. Tyto testy jsem neduplikovala. `wizard_ipc.context_rejects_impossible_budgets_and_overflow` pokrývá NDJSON +1 a timeout maximum/+1 v dekodéru IPC kontextu, `WinHttpDeadline.BoundarySettingsNeverBecomeInfinite` hranice nastavení transportu. Žádný z nich neověřoval požadované explicitní hranice v úplném konfiguračním parseru a repair analýze, proto vznikl jediný nový test.

Provedené sestavení a testy z C:\git\phpvpn, s MSVC x64 prostředím načteným přes VsDevCmd.bat:

| Příkaz | Výsledek |
|---|---|
| `cmake --build work/build/debug --target phpvpn-client-tests` | exit 0, aktuální Debug binárka sestavena a slinkována |
| `work/build/debug/tests/phpvpn-client-tests.exe config.` | 21 testů, 0 selhání, exit 0. Substring filtr zahrnul 19 config a 2 default_config testy |
| `work/build/debug/tests/phpvpn-client-tests.exe wizard_document.cidr_finish_revalidates_the_actual_dom_in_full_and_repair` | 1 test, 0 selhání, exit 0 |
| `work/build/debug/tests/phpvpn-client-tests.exe config_store.cidr_gate_precedes_publication_and_startup_never_migrates` | 1 test, 0 selhání, exit 0 |
| `work/build/debug/tests/phpvpn-client-tests.exe wizard_ipc.context_rejects_impossible_budgets_and_overflow` | 1 test, 0 selhání, exit 0 |

Celkem 24 testů, 0 selhání. Nový test byl skutečně spuštěn a vypsán jako OK. Produkční chyba nebyla nalezena, žádný BLOCKED.

Kontrola rozsahu a formátu:

- Větev tray_UX. Úvodní status zachytil nekomitované změny předchozí fáze. Následně zjištěný HEAD da788b71feac031d5572b906e5d3ac51d4e87ae4 obsahuje konsolidaci předchozí fáze. Rozdíl proti plánovému 529d1c4e53ec48ea95e96c663a62f99af479d5bc neobsahuje produkční kód ani testy. Tento úkol neprováděl žádnou operaci měnící Git historii ani index.
- Výchozí SHA256 tří cílových dokumentů a šestnácti zdrojů jsou v pracovním `work/task-2-baseline-hashes.json`. Porovnání po úpravě potvrzuje shodu všech osmnácti chráněných souborů, mění se pouze client.md. Kopie `work/task-2-client-before.md` umožňuje ověřit, že text mimo sekci Konfigurační soubor je shodný.
- `git diff --check`: exit 0, bez whitespace chyb. Git může hlásit normalizaci LF na CRLF podle existujícího nastavení.
- Nový relativní Markdown odkaz v řádku T02 byl rozbalen vůči adresáři matice a cílový report existuje. V client.md nebyl přidán žádný odkaz.
- Finální diff zahrnuje pouze client.md, test_config.cpp, řádek T02 matice a nový report. Pracovní snapshoty jsou pod ignorovaným work.

NOT RUN: plná CTest sada, Release a ASAN buildy, transportní WinHTTP test, GUI, služba, skutečná VPN, instalace, E2E, výkonová a vizuální akceptace. Tyto kontroly nejsou potřebné pro konfigurační hranice TT02 a nejsou zde vydávány za ověřené. Testy používají syntetické dokumenty a in-memory fixture. Nebyl vytvořen worktree ani další agent a nebyl proveden commit, push, reset, checkout ani mazání. R04 zůstává odložené, B01–B05 bez realizace.
