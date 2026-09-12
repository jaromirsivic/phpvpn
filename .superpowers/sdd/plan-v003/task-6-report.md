# Report úlohy 6: D06 a TT09

Stav: DONE v rozsahu šestého sekvenčního bloku. Společný časový limit je uživatelem schválený neomezený. D07 nebylo zahájeno.

Při obnovení nebo předání kontextu obnov celý účinný globální blok instrukcí z reálného CODEX_HOME a jeho AGENTS.override.md nebo AGENTS.md, případně z úplného hostem dodaného bloku. Před věcnou prací ověř obě přesné samostatné hranice. Tento report chráněný blok nenahrazuje.

## Změny a důkazy

- [client.md](../../../client.md#implementační-meze-lokálního-řízení): nová podčást Implementační meze lokálního řízení pod Společnými pravidly rozšíření IPC. Popisuje 32 slotů včetně listenerů, 16 čekajících shortControl, 4 čekající file a jednu diagnostic úlohu včetně běžící. První dva limity nezapočítávají aktivní úlohu. Každá třída má samostatný sériový executor a vlákno.
- Čtení a zápis mají každý absolutní monotónní I/O deadline 5000 ms. Dílčí přenos jej neobnoví, čekání na uzavření klienta sdílí původní výstupní deadline. Není to celkový rozpočet handleru. Maintenance tick je plánovaný po 250 ms, událost nebo bližší deadline probudí smyčku dříve. Zachovány 4 MiB zakódované zprávy včetně obálky/escapingu a pouze lokální pipe s odmítnutím vzdálených klientů.
- Tři související odkazy na IPC meze B01 ve společném HTTP textu, pravidlech IPC a povinném testu 14 nyní míří na nový nadpis. Počet slotů není vydáván za celkový RSS strop ani dostupnost při libovolném zahlcení. Zbývající historické odkazy k Next, receipts, storage a HTTP cancellation patří následným D07/D09 a zde nebyly konsolidovány.
- [priskok-matice.md](../../../Plans/2026-09-12-02-29-09/priskok-matice.md): změněn pouze řádek T10. Produkční kód, testy, protocol.md, tray_UX.md a progress.md tento blok neupravoval.
- Rozsah určují [plan-v003.md](../../../Plans/2026-09-12-02-29-09/plan-v003.md), D00/D06/TT09, [T10.md](../../../Plans/2026-09-12-02-29-09/T10.md) a rozhodnuti-v004.md. Kontrola protocol.md a tray_UX.md neodhalila důvod pro jejich změnu v D06.
- [ipc_pipe.cpp](../../../client/src/platform/windows/ipc_pipe.cpp): kSlots, kIoDeadlineMs, kTickMs, submit, runExecutor, read, write, awaitClientClose, runLoop a listen potvrzují čísla, účetnictví aktivní úlohy, I/O časování a PIPE_REJECT_REMOTE_CLIENTS. ipc_pipe.h potvrzuje veřejný kontrakt stats a neblokujícího onTick. ipc_protocol.h definuje kIpcMaxMessageBytes = 4 * 1024 * 1024.
- [service_host.cpp](../../../client/apps/service/service_host.cpp): options.classify zařazuje get-status/disconnect do shortControl, connect/get-config/set-config do file a get-info do diagnostic. WizardScheduler sdílí tryPostDiagnostic, údržbu napojuje onTick.

## Existující pokrytí a skutečně spuštěné testy

Suite nebyla změněna, nový test kvůli dokumentaci nevznikl. Podmíněné rozšíření TT09 není potřeba. Přesto byly pro cílenou regresi spuštěny existující availability a snapshot kontraktní testy.

[test_ipc_availability_win.cpp](../../../client/tests/unit/test_ipc_availability_win.cpp) obsahuje čtyři běžné scénáře: framing/roundtrip/restart/klientský deadline a maximum zprávy, podporovanou zátěž, přesné frontové limity s drain při stop a reclaim nečtených spojení. Supported-load používá skutečnou soukromou Windows pipe, osm klientů bez requestu, osm nečtených 1MiB odpovědí, zadrženou file a diagnostic úlohu a čtyři souběžné krátké příkazy, z toho jeden Disconnect. Handler je syntetický, nejde o skutečný teardown VPN nebo tray akci.

StatusContract ověřuje službový snapshot přes builder/parser do tray, plný unsigned rozsah, nezávislost pořadí objektových členů a zachování čítačů po neplatné odpovědi. Není to společný test snapshotu pod plným VPN provozem. IPC/Wizard testy ověřují uzavřená schémata, korelaci, bezpečné chyby, escaping a velikostní meze. Samostatný opt-in resource_ipc scénář v témže zdrojovém souboru existuje, ale nebyl spuštěn.

Závěrečné příkazy z C:\git\phpvpn dne 12. 9. 2026:

| Příkaz | Výsledek |
|---|---|
| `cmake --build work/build/debug --target phpvpn-client-tests` | exit 0, Ninja: no work to do |
| `work/build/debug/tests/phpvpn-client-tests.exe ipc_availability.` | 4 testy, 0 selhání |
| `work/build/debug/tests/phpvpn-client-tests.exe ipc.` | 22 testů, 0 selhání, substring zahrnuje 11 ipc a 11 wizard_ipc |
| `work/build/debug/tests/phpvpn-client-tests.exe StatusContract.` | 2 testy, 0 selhání |
| `git diff --check` | exit 0, bez whitespace chyb |

Celkem 28 různých testů, 0 selhání. Závěrečný supported-load naměřil status/Disconnect 31 ms a stop 15 ms. Reclaim 32 neúplných/nečtených spojení byl pozorován za 5062 ms. To je pozorovaný čas testu, nikoli změna 5000ms deadline. Před odstraněním diskového blokování proběhlo stejných 28 testů také úspěšně, do konečného počtu nejsou započítány znovu.

## Kontroly rozsahu a incident zápisu

Checkout zůstal na tray_UX, HEAD da788b71feac031d5572b906e5d3ac51d4e87ae4. První zápis selhal při 0 B volného místa a nástroj zkrátil client.md. Před zastavením byla přesunem předem pořízené kopie obnovena přesná původní verze, SHA-256 A4740B83CAE6AA2169A36DEC8E58886E1C988139E5FCDF679E88A3328073E090. Tehdejší porovnání 443 vstupních hashů potvrdilo nulovou změnu. Po pokynu koordinátora bylo ověřeno 128503660544 volných bajtů a neprázdný obnovený dokument. Následný zápis uspěl.

Po pokračování nevznikl široký snapshot ani opakované široké hashování. Osm cílených hashů potvrdilo zachování protocol.md, tray_UX.md, rozpracovaných test_config.cpp a test_runtime_status.cpp i reportů 2–5. Diff matice vůči původní malé kopii obsahuje jen T10. Úpravy client.md jsou omezené na D06 a zachovávají dřívější doplnění D01–D05. Novým sledovatelným souborem je tento report. Nové lokální Markdown odkazy v client.md, řádku T10 a reportu byly ověřeny včetně cílového nadpisu. Tabulka kapacit má konzistentní dva sloupce.

## NOT RUN a rizika

- Plná CTest sada, Release/ASAN, resource_ipc/RSS, WinHTTP účetnictví, provozní traffic counters, skutečný PHP relay, SCM stop, služba, Wintun/VPN, GUI a E2E: NOT RUN v tomto bloku. Existence testů není jejich běh.
- Dostupnost pod uvedenou syntetickou zátěží neprokazuje odezvu při libovolném floodu, přesnou realtime periodu údržby ani univerzální dobu dokončení OS cancellation. Fronty a sloty nejsou zárukou celkového RSS.
- B01–B05 pre-poll backpressure zůstávají samostatnou budoucí prací, R04 odložené. D07 a další dokumentační bloky nejsou součástí tohoto výsledku.
- Neproběhl commit, push, reset, checkout, vytvoření worktree, subagentů ani mazání souborů. Obnovovací přesun nahradil pouze prázdný client.md jeho původními bajty.
