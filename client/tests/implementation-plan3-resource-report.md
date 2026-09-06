# Plan3: doplněné RSS, DNS a HTTP contention

Zdroje byly 6. září 2026 přeneseny do `C:/git/phpvpn`. Tento report zachovává výsledky běhů v původním worktree. Odkazy na pracovní logy vedou do tohoto původního umístění, odkazy na distribuci vedou na novou kopii v hlavním projektu. Podrobnosti přenosu uvádí [předávací report](implementation-plan3-handoff.md).

Skutečně provedeno 6. září 2026 na Windows 11 AMD64. Tento dodatek doplňuje původní report a uzavírá tři dříve nedoložené automatizovatelné scénáře. Neuzavírá zbývající interaktivní brány.

## Maximální paměť IPC

[Finální log](C:/Users/User/.codex/worktrees/d6c8/phpvpn/work/build/resource-plan3.log), [exit 0](C:/Users/User/.codex/worktrees/d6c8/phpvpn/work/build/resource-plan3.exit). Samostatný Release proces používá produkční `IpcPipeServer`, skutečné Windows named pipes a klienty, kteří nečtou odpovědi. V každém ze čtyř cyklů je současně obsazeno všech 32 slotů, každý drží kompletní 4MiB request a kompletní 4MiB response. Test před měřením ověřuje dokončení všech 32 handlerů a nulový přírůstek timeoutů, tedy souběžné držení bufferů. Třiatřicáté spojení se nepřijme. Po 5s I/O deadline se všech 32 slotů obnoví a nový status projde.

| Měření finálního společného DNS/RSS běhu | Bajty | MiB |
|---|---:|---:|
| RSS před zátěží | 8421376 | 8,03 |
| Nejvyšší RSS, shodné s OS PeakWorkingSetSize | 283848704 | 270,70 |
| Nejvyšší měřený private commit | 278110208 | 265,23 |
| OS peak commit | 278134784 | 265,25 |
| RSS po reclaimu v každém cyklu | 15159296 | 14,46 |
| Private commit po reclaimu v každém cyklu | 17293312 | 16,49 |
| Private commit po stopu | 6516736 | 6,21 |

Ve druhém až čtvrtém cyklu jsou naměřené hodnoty shodné. Status po reclaimu a stop mají 0 ms v rozlišení GetTickCount64. To neznamená nulovou fyzikální latenci. První samostatný RSS běh je zachován v `resource-rss-first.log`, jeho nižší baseline nezahrnoval dřívější DNS/WinHTTP test ve stejném procesu.

Test kontroluje RSS i private commit proti baseline + 320 MiB. Produkční limit bufferů je 256 MiB + 2 MiB pevných bloků. Zbytek je výslovná rezerva testovacího procesu pro jeden klientský payload, haldu, zásobníky a runtime, není to nový protokolový limit. Sleduje se také OS peak working set a commit. Test vyžaduje alespoň 1 GiB dostupné fyzické paměti a má v runneru vnější limit 90 s.

Jde o skutečně naplněnou maximální kapacitu IPC bufferů, nikoli odhad podle konstant. Nejde o RSS celé SCM služby se všemi současně plnými tunelovými frontami a všemi třídami práce. Dostupnost při podporovaném mixu a meze pracovních front dokládají předchozí čtyři `ipc_availability.*` testy. Stop SCM a Wintun jsou samostatný E-E2E důkaz.

## Skutečně zadržené DNS

[Finální log](C:/Users/User/.codex/worktrees/d6c8/phpvpn/work/build/resource-plan3.log) obsahuje kontrolní `getaddrinfo=0`, doručený DNS dotaz při WinHTTP a celkem šest přijatých dotazů. Samostatný testovací proces nastaví výhradní vlastní resolver přes `DnsSetApplicationSettings`. Listener používá pouze `127.0.0.2:53` s `SO_EXCLUSIVEADDRUSE`, kontroluje, že tato adresa není DNS serverem žádného adaptéru, a odmítne kolidující bind. Systémové DNS, adaptéry, routy, proxy, hosts ani bezpečnostní nastavení se nemění.

1. Kontrolní DNS odpověď přeloží unikátní jméno pod `.phpvpn.test` na vlastní loopback HTTP server. Skutečný produkční WinHTTP vrátí 200 a `ok`.
2. Resolver další skutečně přijaté dotazy úmyslně zahodí. HTTP request s rozpočtem jedné sekundy vrátí `transport_timeout` za **1016 ms**, bez bajtů těla.
3. Nový request se zruší až po doručení dalšího DNS dotazu. Vrátí `cancelled` za **0 ms v rozlišení hodin**.
4. Resolver znovu odpovídá, stejná instance transportu s novým jménem vrátí 200 a `ok`. Reset procesového nastavení DNS vrátí ERROR_SUCCESS.

Předchozí neúspěchy testovacího prostředí se zachovaly. `resource-dns-first.log`: API odmítlo nenulový port chybou 87, procesová sonda ověřila, že port musí být nula, tedy implicitní UDP 53. IPv6 varianta na `::1` přijala nastavení, ale žádný dotaz nepřišel, kontrolní getaddrinfo i WinHTTP skončily DNS chybou. Její příčina nebyla určena, tyto běhy nejsou důkazem timeoutu. Finální IPv4 varianta má pozitivní kontrolu i skutečně přijaté a zadržené dotazy.

[Host před](C:/Users/User/.codex/worktrees/d6c8/phpvpn/work/build/resource-host-before.json) a [po](C:/Users/User/.codex/worktrees/d6c8/phpvpn/work/build/resource-host-after.json) jsou shodné pro DNS, výchozí routy a identitu/PID/stav existující produkční služby. [Cleanup](C:/Users/User/.codex/worktrees/d6c8/phpvpn/work/build/resource-cleanup.log) potvrzuje nepřítomnost privátního DNS listeneru. Tento blok nevyžadoval UAC.

API podklady: [procesová DNS konfigurace](https://learn.microsoft.com/en-us/windows/win32/api/windns/nf-windns-dnssetapplicationsettings), [výhradní servery](https://learn.microsoft.com/en-us/windows/win32/api/windns/ns-windns-dns_application_settings), [DNS_CUSTOM_SERVER](https://learn.microsoft.com/en-us/windows/win32/api/windnsdef/ns-windnsdef-dns_custom_server). API vyžaduje Windows build 22000 nebo novější. Finální HANDLE_CLOSING stále závisí na fungování Windows, test nedokazuje absolutní mez při zastavení OS.

## HTTP 503 při skutečném contention

[Reprodukce před opravou](C:/Users/User/.codex/worktrees/d6c8/phpvpn/work/build/http-contention-red.log) prokázala odchylku od tabulky chyb v kapitole 13 protokolu: vyčerpaný storage lock vracel HTTP 200 s `deferredMessageIds`. Nešlo pouze o nedostatečný souběh původního Python testu.

Nový [samostatný test](e2e/test_http_contention.py) spustí dva skutečné PHP HTTP workery na náhodných loopback portech, oba s čerstvou identickou kopií `phpvpn.php` a společným izolovaným bufferem. Druhý worker pomocí standardního PHP `flock` drží přesný `.lock` cílové fronty. První worker zpracuje normálně autentizovaný požadavek s datovým záznamem. Test ověřuje, že držitel zámku zůstává aktivní po celou dobu odpovědi.

Oprava přidává výjimku `QueueBusy` pouze pro vyčerpaný čas čekání, DataHandler ji před zahájením odpovědi mapuje na 503. Nedostatek kapacity fronty dál vrací 200 a odložené položky. Interval posledního čekání je omezen zbývajícím časem zámku.

[Samostatný zelený běh](C:/Users/User/.codex/worktrees/d6c8/phpvpn/work/build/http-contention-green.log): HTTP 503, přesně `{"type":"error","code":"queue_busy"}`, povinné protokolové hlavičky a `Retry-After: 1`, **225,94 ms** včetně HTTP a validace. Cílová fronta před uvolněním neobsahuje publikovaná data. Po uvolnění nový explicitní pokus vrátí 200, nezávislý Python klient ověří data-mac, přijetí i doručení stejné zprávy. Následující běh s úplnou Python regresí naměřil 219,60 ms. [PHP regrese](C:/Users/User/.codex/worktrees/d6c8/phpvpn/work/build/php-contention-regression.log): 486 assertions PASS.

Úplná Python regrese po opravě: **386 passed, 1 skipped, 233,85 s**, [log](C:/Users/User/.codex/worktrees/d6c8/phpvpn/work/build/http-contention-cncjqcbq/pytest.log), [JUnit](C:/Users/User/.codex/worktrees/d6c8/phpvpn/work/build/http-contention-cncjqcbq/pytest.xml), [výsledek fixture](C:/Users/User/.codex/worktrees/d6c8/phpvpn/work/build/http-contention-cncjqcbq/result.json). Skip náleží starému pravděpodobnostnímu hammer testu, který nezaručuje držení zámku po dobu 200 ms. Nový řízený test skutečných souběžných HTTP workerů je PASS, takže tento skip už neznamená chybějící důkaz odpovědi 503. Oba vlastní PHP procesy byly ukončeny, `children_stopped=true`.

První chybně připravený fixture v `http-contention-before.log` zamkl adresář s tečkami místo normativních podtržítek, server zprávu správně přijal. Test byl opraven a skutečný červený běh je výše uvedený `http-contention-red.log`. Všechny výsledky i soukromé adresáře jsou zachované, žádné produkční fronty se nepoužily.

## Reprodukce a artefakty

Z kořene pracovního stromu:

```powershell
client/tools/run-resource-tests.ps1
uv run --python 3.13 --with pytest --with requests --with cryptography -- python client/tests/e2e/test_http_contention.py --php D:/nginx/php8.5.9/php.exe --full-suite
```

Resource target je explicitně opt-in a není součástí normálního CTest ani distribuce. Klientské produkční C++ zdroje se tímto blokem nezměnily. Opravený serverový `phpvpn.php` není součástí klientského ZIPu, proto se klientský balíček nepřebaloval a zachoval původní hash. Zdroj serveru a jeho SHA256 jsou součástí výsledku každého HTTP fixture. Žádný server nebyl nasazen do uživatelské instalace.
