# Report úlohy 13: D13 a přezkum TT06/TT07

Stav: DONE v rozsahu třináctého samostatného bloku. Společný časový limit byl předán jako uživatelem schválený neomezený. D14 ani B01–B05 nebyly zahájeny.

Při obnovení nebo předání kontextu nejprve obnov celý účinný globální blok instrukcí z reálného CODEX_HOME a jeho AGENTS.override.md nebo AGENTS.md, případně z úplného hostem dodaného bloku. Ověř obě přesné samostatné hranice před další věcnou prací. Tento report chráněný blok nenahrazuje.

Rozsah vychází z [plan-v003.md](../../../Plans/2026-09-12-02-29-09/plan-v003.md), D00/D13/TT06/TT07, [T06](../../../Plans/2026-09-12-02-29-09/T06.md), [T07](../../../Plans/2026-09-12-02-29-09/T07.md) a [rozhodnutí R03/R04](../../../Plans/2026-09-12-02-29-09/rozhodnuti-v004.md). Nejprve byl přezkoumán [report D05](task-5-report.md) a jím doplněné testy.

Změněno:

- [client.md](../../../client.md), Účel: obecný fault-proof slib nahrazen obsluhou vrácených provozních chyb a rozlišením recovery/error.
- Běhový model: ukončení dvou tunelových rolí a session, reset síťového kontextu, vyčištění obou front, nové vytvoření při dalším bootstrapu. Přijatý config a původní checkpointy se zachovávají, čekající pakety zrušených front nemají záruku uchování/doručení. Výslovná hranice chybějícího obecného dohledu a catch rolí.
- Stavový automat, body 6–8 a navazující odstavce: bootstrap provádí již existující síťová role, po něm vznikají dvě I/O role. Odstraněn slib zachování tunelu. Doplněna klasifikace skutečných chyb, zachování snapshotu a checkpointů i přes Save nebo smazání souboru, žebřík 1/2/4/8/16/32/60 s plus až 20 % jitter, první čekání 2–2,4 s kvůli inkrementaci před výpočtem. Odlišen polling 5 s a selhání periodického health. Přidán odkaz na skutečné lifecycle testy.
- [Matice](../../../Plans/2026-09-12-02-29-09/priskok-matice.md): pouze řádky T06/T07, oddělené výsledky a mezera TT07.
- Tento report. Produkční kód ani testy se neměnily. T06.md, T07.md, progress.md, protocol.md a tray_UX.md zůstaly proti vstupu beze změny.

Důkazy z implementace:

- [vpn_engine.cpp](../../../client/src/core/runtime/vpn_engine.cpp): `start` vytváří síťové vlákno, `runConnectSequence(false)` používá `activeConfig_` a vytváří nové fronty. Po health/info/access check zahájí session a dvě role. Příznak `initialBootstrap` řídí zápis úvodních checkpointů. `teardown` zastaví a zruší I/O role, ukončí aktivní session, resetuje NetworkWorker a vyčistí fronty. `runNetworkThread` volá teardown při chybě a rozhoduje podle `lastError.recoverable`. Nový logický Connect, úspěšný bootstrap a dokončený Disconnect resetují backoff. `backoffDelayMs` přičítá jitter k základnímu žebříku, chybové větve před voláním zvyšují index.
- [tunnel_workers.cpp](../../../client/src/core/runtime/tunnel_workers.cpp): receive error ukončuje vstupní roli, send s `tunnel_io_failed` ukončuje výstupní roli. Obecný catch běhových funkcí ani dohled jejich ukončení v engine nejsou implementovány. Catch při vytváření/spouštění rolí není záruka obnovy jejich libovolného pádu.
- [status_snapshot.cpp](../../../client/src/core/runtime/status_snapshot.cpp) a [error.cpp](../../../client/src/core/base/error.cpp): rozlišení timeoutu, DNS/spojení, obnovitelných serverových chyb a neobnovitelného ověření serveru či odmítnutého access checku. Dokument neslibuje obnovu všech autentizačních chyb.
- [transport_winhttp.cpp](../../../client/src/platform/windows/transport_winhttp.cpp): vlastník zvolí jediný výsledek pod mutexem, uzavře request a čeká na HANDLE_CLOSING pomocí INFINITE. Správný existující text Společný HTTP transport zůstal zachován, včetně omezení důkazu horní meze úklidu.
- [protocol.md](../../../protocol.md) ponechán jako wire specifikace. [tray_UX.md](../../../tray_UX.md) ponechává službové checkpointy a zmrazený konečný výsledek progress okna během recovery. Nebyla nalezena konkrétní nutnost jejich změny pro tento blok.

TT06 a TT07:

1. [Runtime testy](../../../client/tests/unit/test_runtime_status.cpp) z D05 již měří start/disconnect/reconnect/shutdown a recovery teardown/recreate. Ověřují počty vytvoření adaptéru/session, konce session, zavření a destrukce tunelu, skutečné receive/send role a trvající stejné síťové vlákno. Recovery scénář používá fake hodiny a brány, pozoruje osm čekání 2/4/8/16/32/60/60/60 s a meze jitteru. Doplnění dalšího stejného lifecycle scénáře by bylo duplicitní.
2. Existující runtime scénáře samostatně ověřují snapshot po Save a smazání souboru, úspěšné i neúspěšné původní checkpointy, idempotenci a účetnictví po cancel/Disconnect. Nový test není potřebný k dokumentačnímu upřesnění.
3. [WinHTTP testy](../../../client/tests/unit/test_winhttp_accounting.cpp) používají skutečný WinHTTP a privátní loopback socket. Pokrývají partial timeout/close/reset/cancel, oversized block, deadline hlaviček/TLS/trickle a souběžné rušení během TLS s následným úspěšným reuse. BodyServer používá IP 127.0.0.1, takže neizoluje DNS, jeho malý request neizoluje čekání send. Nemá řízenou bránu terminálního success proti cancel. Bez skutečného OS/WinHTTP prostředí by nový fake scénář ověřoval jen fake, nikoli tuto vazbu. Podmíněné doplnění TT07 proto nebylo provedeno, mezera zůstává výslovně NOT RUN.
4. [Platformní probe](../../../client/tests/platform_plan3_probe.cpp) staticky přezkoumáno, měří konkrétní closing callback a rušení. V tomto bloku nebylo spuštěno. Žádný běh se nevydává za univerzální důkaz bounded HANDLE_CLOSING.

Spuštěno z `C:\git\phpvpn`, Windows, existující Debug build:

| Příkaz | Výsledek |
|---|---|
| `cmake --build work/build/debug --target phpvpn-client-tests` | exit 0, Ninja: no work to do |
| `work/build/debug/tests/phpvpn-client-tests.exe runtime.` | 19 testů, 0 selhání, exit 0 |
| `work/build/debug/tests/phpvpn-client-tests.exe WinHttp` | 9 testů, 0 selhání, exit 0 |
| `git diff --check` | exit 0, bez whitespace chyb |

Celkem 28 různých testů, 0 selhání. WinHttpDeadline naměřil pro hlavičky 1010 ms, TLS 1006 ms a trickle body 1016 ms, všechny pod scénářovou mezí 2500 ms. ConcurrentCancellationThenReuse prošel pěti cykly se dvěma rušiteli a následným úspěšným requestem. Jde o výsledky tohoto prostředí, nikoli univerzální časové záruky.

Rozsah a kontrola:

- Větev `tray_UX`, HEAD `1c25096111cdf20f901c58da6299ddde7450e229`. Stav checkoutu se proti historickému D00 změnil, tematické důkazy byly znovu ověřeny v aktuálním kódu.
- Vstupních 451 souborů má SHA-256 v ignorovaném `work/task-13-baseline-hashes.json`. Vstupní client a matice jsou v `work/task-13-client-before.md` a `work/task-13-matrix-before.md`. Proti vstupu se změnily pouze client.md a dva řádky matice. Zbývajících 449 hashů je shodných, včetně produkce, testů, historických zdrojů a předchozí rozpracované práce. Nový soubor je pouze tento report.
- Diff vůči vstupním kopiím byl přezkoumán. Nové lokální Markdown odkazy byly rozbaleny vůči jejich dokumentům a cíle existují. Žádný nový webový odkaz ani odkaz s fragmentem nebyl přidán. Git upozorňuje pouze na obvyklou budoucí normalizaci LF na CRLF.
- Wintun integrita, journal a celé pořadí cleanup R04 zůstaly obsahově nedotčené. Známá starší věta v Disconnect, upgrade a rollback stále požaduje ukončení síťového i tunelových vláken. Tento odložený požadavek není potvrzením skutečného Disconnect lifecycle a D13 jej podle explicitní hranice zadání neopravuje ani neodstraňuje. Skutečná životnost síťové role je uvedena v Běhovém modelu a v již dodaném D05.

NOT RUN a rizika:

- Plná CTest sada, Release/ASAN, PHP relay, služba/SCM, skutečný Wintun, GUI, installer a E2E nebyly spuštěny. Fake driver nenahrazuje Windows kernel lifecycle ani R04.
- Nová izolovaná DNS/send cancellation a řízený závod cancel versus success nebyly provedeny. Existující WinHTTP suite tuto mezeru úplně neuzavírá.
- Test backoffu potvrzuje pozorované meze, nikoli distribuci všech jitter hodnot. Neprokazuje uchování čekajících paketů ani obecnou obnovu po pádu role. Closing callback může prodloužit návrat po deadline, univerzální horní mez není doložena.
- Nebyla zjištěna nová produkční chyba vyžadující opravu v rozsahu D13. BLOCKED nenastal. R04 zůstává odložené, B01–B05 plánované. Neproběhl commit, push, reset, checkout, mazání, vytvoření worktree ani subagentů. Práce končí po D13.
