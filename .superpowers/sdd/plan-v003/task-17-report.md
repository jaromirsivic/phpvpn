# Report úlohy 17: D17, T22 a R10

D17 dokončeno. Debug build a 26 cílených testů prošly, produkční kód a IPC se nezměnily. Práce proběhla přímo na větvi `tray_UX`, HEAD `1c25096111cdf20f901c58da6299ddde7450e229`, se zděděným výslovně schváleným neomezeným časem. Bez commitu, pushe, worktree, mazání nebo dalších agentů. R04 a budoucí pre-poll backpressure zůstávají mimo rozsah.

Při obnovení nebo předání kontextu nejprve obnov celý účinný globální blok instrukcí z reálného CODEX_HOME a jeho AGENTS.override.md nebo AGENTS.md, případně z úplného hostem dodaného bloku. Ověř obě přesné samostatné hranice před další věcnou prací. Tento report chráněný blok nenahrazuje. Globální soubor byl načten celý v UTF-8, projektový AGENTS.md nebyl nalezen.

## Změny

- [client.md](../../../client.md), Statistics v menu a test 34: deset zobrazovaných hodnot z devíti uint64 čítačů, karty 3/3/4, odvozený Total size, přesný součet s přenosovým bitem i nad UINT64_MAX před formátováním. Počty zůstávají přesná celá čísla s anglickými čárkami. Bajty pod 1000 jsou v B, vyšší v desetinných kB/MB/GB/TB/PB se dvěma desetinnými místy, round-half-up a povýšením jednotky po zaokrouhlení. Zaokrouhlení není přesným surovým počtem bajtů.
- [tray_UX.md](../../../tray_UX.md), UX00/UX07/UX14: správné pořadí všech deseti polí a schválená odchylka od původního mockupu. Odstraněn zákaz prezentačního součtu. Pole v tabulce určují zdroj dat, současné nativní okno pod labelem technický název nevytváří. Zachován jediný snapshot, stale, tab pořadí a pětisekundový status polling. Zákaz vlastního timeru je zpřesněn na status polling, aby nepopíral existující lokální prezentační timer. Connected for se nově nedokumentuje.
- [Matice](../../../Plans/2026-09-12-02-29-09/priskok-matice.md): pouze řádek T22 průběžně označen jako probíhající a po ověření dokončený. Historická analýza [T22](../../../Plans/2026-09-12-02-29-09/T22.md) ponechána, nad ní již platí R10. Autorita: [D17](../../../Plans/2026-09-12-02-29-09/plan-v003.md) a [rozhodnutí R10](../../../Plans/2026-09-12-02-29-09/rozhodnuti-v004.md).

## Testové pokrytí

Prověřeny skutečné [statistics_model.h](../../../client/apps/tray/statistics_model.h), [statistics_window.cpp](../../../client/apps/tray/statistics_window.cpp) a existující [test_wp03_models.cpp](../../../client/tests/unit/test_wp03_models.cpp), StatusContract, TrayStatus, TrayPolling, TrayLayout a EnglishUint64FormattingDoesNotLosePrecision. Existující test již pokrývá deset hodnot, maximum obou sčítanců, čekání/stale, 999/1000 B a přechod kB → MB. Toto pokrytí nebylo duplikováno.

Do test_wp03_models.cpp přidány pouze dva testy:

- `StatisticsRoundsHalfUpAcrossDecimalUnits`: dosud chybějící poloviny a hodnoty těsně pod nimi v kB až PB, povýšení MB → GB → TB → PB a zachování PB jako nejvyšší jednotky. Literal očekávání odhalí truncation, binární dělitel nebo chybějící povýšení.
- `StatisticsSumsExactBytesBeforeRoundingIncludingCarry`: přímé ověření součtu 0, nad 2^53, přes INT64_MAX, přes UINT64_MAX v obou pořadích a dvojnásobného UINT64_MAX. Zachytí ztrátu nízkých bitů, přetečení či saturaci. Modelový případ 1004 + 1004 dává 2.01 kB, přestože oba jednotlivé texty jsou 1.00 kB, a tento součet zachová při stale. Očekávání nejsou vypočtena testovanými helpery. Jde o doplnění pokrytí existujícího chování, nikoli opravu produkční chyby nebo deklarovaný RED/GREEN cyklus.

## Přesně spuštěné ověření

`cmd /c work\d16-build.cmd`, existující přečtený skript obsahuje:

```bat
@call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -no_logo -arch=x64 -host_arch=x64
@if errorlevel 1 exit /b %errorlevel%
@cmake --build work/build/debug --target phpvpn-client-tests
```

Build přeložil změněný test a slinkoval testovací EXE, exit 0. Každý následující příkaz byl samostatný proces, exit 0:

| Příkaz | Výsledek |
|---|---|
| `work/build/debug/tests/phpvpn-client-tests.exe WP03Models.` | 8 PASS |
| `work/build/debug/tests/phpvpn-client-tests.exe TrayControls.EnglishUint64FormattingDoesNotLosePrecision` | 1 PASS |
| `work/build/debug/tests/phpvpn-client-tests.exe StatusContract.` | 2 PASS |
| `work/build/debug/tests/phpvpn-client-tests.exe TrayStatus.` | 5 PASS |
| `work/build/debug/tests/phpvpn-client-tests.exe TrayPolling.` | 2 PASS |
| `work/build/debug/tests/phpvpn-client-tests.exe TrayLayout.` | 8 PASS |

Celkem 26 testů, 0 selhání. Výstupy jsou v `work/task-17-FILTR.log`, koncová tečka filtru je v názvu souboru odstraněna. StatusContract ověřuje službový JSON → tray parser a přeskupení členů, modelové testy zvlášť formátování. Nejde o nové ověření celé cesty do nativních polí.

## Dokumentační kontrola a NOT RUN

`git diff --check` prošel, pouze upozornění na budoucí konverzi LF/CRLF. `git diff --numstat -- protocol.md` je prázdný. PowerShell kontrola Markdown odkazů přes regex a Test-Path ověřila souborové cíle obou dokumentů, matice a tohoto reportu. Nové odkazy na UX07 mají existující nadpis `## UX07: Statistics`. Cílená kontrola výskytů devět/devíti a Statistics potvrdila rozlišení službových čítačů od UI hodnot. Zbývající devět u službového účtování je správně.

NOT RUN: WP03Windows a nativní kopírování/tab/scroll nejdelších hodnot, skutečné DPI/multi-monitor/HC/screen reader, vizuální akceptace všech jedenácti mockupů, Release výkonová brána UX14, plný CTest, ASAN, reálná VPN/služba/SCM/Wintun. Čistý layout test při 96/120/144/192 DPI tyto scénáře nenahrazuje. Celková kontrola V01–V06 všech D bloků zůstává koordinátorovi, zde byla ověřena část D17.

Navazující drobnost mimo schválené cíle: [UI README](../../../client/tests/ui/README.md) stále uvádí „Nine exact uint64 counters“. Soubor nebyl změněn. Starší rozpor ContractsDocument z reportu D16 nebyl řešen. Při vstupu checkout již obsahoval změny D10–D16 a progress koordinátora. D17 mění pouze client.md, tray_UX.md, řádek T22 matice, test_wp03_models.cpp a tento report. Historické zdroje, protocol.md a ostatní rozpracované oblasti zůstaly nedotčené.
