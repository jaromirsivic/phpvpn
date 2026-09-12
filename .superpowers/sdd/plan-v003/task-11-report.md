# Report úlohy 11: D11 a ověření TT04

D11 dokončeno podle R01. Kód a existující testy zůstaly beze změny. Společný časový limit byl předán jako výslovně neomezený. Úloha končí po D11.

Při obnovení nebo předání kontextu obnov celý účinný globální blok instrukcí z reálného CODEX_HOME a jeho AGENTS.override.md nebo AGENTS.md, případně z úplného hostem dodaného bloku. Před věcnou prací ověř obě přesné samostatné hranice. Tento report chráněný blok nenahrazuje.

## Rozsah a důkazy

Autorita: D00/D11/TT04 v [plan-v003.md](../../../Plans/2026-09-12-02-29-09/plan-v003.md), [T04.md](../../../Plans/2026-09-12-02-29-09/T04.md), rozhodnutí R01 v [rozhodnuti-v004.md](../../../Plans/2026-09-12-02-29-09/rozhodnuti-v004.md). Historický původ matematiky je S01 §12, změna health na vždy auth-none a izolace diagnostik je S03 §3/B06/B08. Pozdější R01 vyjímá nedoložené garance.

- [client.md, Konfigurační soubor](../../../client.md#konfigurační-soubor): zachován default 300 s, kladné int64 a absence dodatečného stropu. Přijetí validátorem není univerzální časová garance. Obecná věta o timeoutu nyní odlišuje data recovery a neúspěšný obnovovací health.
- [client.md, Synchronizace času](../../../client.md#synchronizace-času): zachována matematika RTT, serverTimeAtReceive, offsetu a rezervy max(1000 ms, RTT/2 + 250 ms). Místo univerzálního tvrzení o nereprezentovatelných výsledcích jsou uvedeny konkrétní implementované kontroly.
- [vpn_engine.cpp](../../../client/src/core/runtime/vpn_engine.cpp), runNetworkThread, řádky 551–604: nejprve runConnectedIteration a kontrola lastDataVerified, po chybě samostatná větev s continue. Teprve po úspěšném datovém cyklu se kontroluje stáří snímku, jeho absence a systemClockChanged. Perioda se měří od synchronisedAtMonoMs úspěšného snímku. Selhání health nastaví pouze lastHealthVerified na no, bez recovery/backoffu. Další kontrola může opakovat refresh po následujícím datovém cyklu.
- [network_worker.cpp](../../../client/src/core/runtime/network_worker.cpp), performHealth, řádky 82–116: každý health je auth-none s novým requestId. Teprve úspěšný parseHealth a makeSnapshot vedou k publish. Chybový návrat před publish zachová původní snímek.
- [time_sync.cpp](../../../client/src/core/runtime/time_sync.cpp), makeSnapshot/requestTimestampMs/safetyMarginMs/systemClockChanged, a [time_sync.h](../../../client/src/core/runtime/time_sync.h): monotónní projekce, úplný kandidát, tolerance wall clock 2000 ms. Detekce se volá v sériové smyčce, není to OS událost před každým requestem. Odstraněny synchronizační sleep/hibernate/resume a network-change triggery i jejich testovací povinnosti. Nesouvisející UI odpočet, Wizard lease a Wintun scénáře zůstaly zachované.
- [config_validator.cpp](../../../client/src/core/config/config_validator.cpp), optionalPositiveInteger, a [config.h](../../../client/src/core/config/config.h) dokládají typ/default. Není přidán nový limit ani implementace extrémních převodů.
- [Povinné testovací scénáře synchronizace času](../../../client.md#povinné-testovací-scénáře-synchronizace-času) odpovídají pořadí kontroly v enginu. Neobsahují povinnost recovery po samotném health failure ani prioritu OS triggeru. Zachování neúspěšného kandidáta je oddělené od provozního lifecycle.
- [protocol.md](../../../protocol.md), §5.2, už vyžaduje ověření health před použitím času a rozlišuje provozní recovery od jednorázové diagnostiky. [tray_UX.md](../../../tray_UX.md) změnu času nevyžaduje. Oba soubory zůstaly beze změny. V [matici](../../../Plans/2026-09-12-02-29-09/priskok-matice.md) byl upraven pouze T04, předchozí T14 zachován.

## Existující pokrytí a skutečné běhy

[test_time_sync.cpp](../../../client/tests/unit/test_time_sync.cpp) obsahuje 9 testů: odhad a offset, obě větve rezervy, monotónní projekci timestampu, chybějící snímek, zpětné hodiny, zachování kandidáta, wall clock skok oběma směry, přesný wire formát a reset. Test failed sample neprovádí neúspěšný periodický HTTP request v enginu. Vyšší RTT ověřuje safetyMarginMs, není to samostatný end-to-end request s vysokým RTT.

[test_runtime_status.cpp](../../../client/tests/unit/test_runtime_status.cpp) obsahuje bootstrap a runtime scénáře s fake transportem/driverem. Konkrétně health_syntax_is_published_before_key_derivation a bootstrap_errors_classify_actual_operation_and_checkpoint ověřují hranici syntaxe/crypto a klasifikaci selhání bootstrapu včetně špatného health tagu. [test_config.cpp](../../../client/tests/unit/test_config.cpp), applies_the_documented_defaults, ověřuje použití defaultu. Registrace je v [CMakeLists.txt](../../../client/tests/CMakeLists.txt).

Windows AMD64, 12. 9. 2026, C:\git\phpvpn, větev tray_UX, HEAD 1c25096111cdf20f901c58da6299ddde7450e229.

| Příkaz | Výsledek |
|---|---|
| `cmd /c work\d11-build.cmd` | PASS, exit 0. Skript načetl VS 2022 BuildTools přes VsDevCmd.bat -no_logo -arch=x64 -host_arch=x64 a spustil cmake --build work/build/debug --target phpvpn-client-tests. Ninja: no work to do. |
| `work/build/debug/tests/phpvpn-client-tests.exe time.` | PASS, 28 testů, 0 selhání. Podřetězcový filtr vybral 9 time a 19 runtime. |
| `work/build/debug/tests/phpvpn-client-tests.exe runtime.health_syntax_is_published_before_key_derivation` | PASS, 1 test, 0 selhání, opakovaný výběr z předchozích 28. |
| `work/build/debug/tests/phpvpn-client-tests.exe runtime.bootstrap_errors_classify_actual_operation_and_checkpoint` | PASS, 1 test, 0 selhání, opakovaný výběr z předchozích 28. |
| `work/build/debug/tests/phpvpn-client-tests.exe config.applies_the_documented_defaults` | PASS, 1 test, 0 selhání. |
| `git diff --check`, diff proti vstupnímu snímku a lokální odkazy | PASS, viz kontrola rozsahu níže. |

Celkem 31 provedení, 29 různých testů, žádné selhání. První dva pokusy o build selhaly na předání uvozovek mezi shelly ještě před spuštěním buildu. Uvedený jednoduchý batch soubor tento problém odstranil. Chybné průzkumné cesty config/* a tests/harness.cpp byly nahrazeny existujícím adresářem config a tests/unit/harness.cpp. Tyto chyby nejsou testové výsledky.

## Kontrola rozsahu, NOT RUN a rizika

- Vstup obsahoval necommitované client.md, T14 v matici, progress.md a task-10-report.md. Baseline obsahu a SHA-256 je v [d11-baseline.xml](../../../work/d11-baseline.xml). Porovnání proti tomuto vstupu zachovává změny D01–D10. progress.md a task-10-report.md byly ponechány koordinátorovi.
- V01/V02: nový rozsah je client.md, pouze T04 v matici a tento report. Zdrojové symboly a cílové části jsou uvedeny výše. SHA-256 všech šestnácti historických zdrojů, protocol.md a tray_UX.md jsou stejné jako na vstupu. Žádný kód ani test není ve výsledném diffu.
- V03/V06: všech 77 lokálních souborových odkazů v client.md, matici a reportu je ověřeno včetně uvedených fragmentů, git diff --check bez chyb. Kontrola SHA-256 potvrdila 20 nezměněných vstupních souborů, šestnáct historických zdrojů, protocol.md, tray_UX.md, progress.md a task-10-report.md. Diff proti vstupnímu client.md omezuje tento blok na konfiguraci času/timeoutu, synchronizaci a její testy.
- V04/V05: ponecháno 300 s, bez domyšlené horní meze. Opačné věty o periodickém failure recovery a OS prioritě byly nahrazeny. Pořadí data → kontrola refresh odpovídá kódu. Univerzální aritmetická bezpečnost se netvrdí.
- NOT RUN: samostatný řízený lifecycle periodického health po 300 s, jeho selhání a následný úspěch ani integrace wall clock změny do connected smyčky. Tyto runtime vlastnosti jsou doloženy čtením kódu, nikoli samostatným testem tohoto běhu. Matematické zachování kandidáta není náhrada takového testu.
- NOT RUN: zvláštní logovací scénář pro všechny výsledky synchronizace, celý CTest, Release/ASAN, skutečné PHP/VPN, služba/SCM, Wintun, GUI, instalace a ProgramData. Unit PASS není důkaz skutečného OS lifecycle nebo síťové interoperability.
- Vyjmuté OS triggery, recovery po samotném health failure a univerzální extrémní garance nemají novou implementační ani testovací povinnost. Výraz timeSyncIntervalSeconds * 1000 a všechny rozdíly hodin nemají univerzálně doloženou ochranu proti extrémům. Kód se v tomto bloku neopravuje.
- Zbývající obecná věta o zachování tunelu v bodu Obnova stavového automatu patří k D13/R03, podle reportu D05 je záměrně ponechána dalšímu bloku. D12 ani D13 nejsou tímto reportem dokončené. R04 zůstává odložené, B01–B05 nedodané.
- Bez commitu, push, resetu, checkoutu, worktree, subagentů nebo mazání.
