# Report ověření macOS klienta, 2026-10-30

Plán: [plan-macos-v001.md](../../Plans/2026-10-30-18-08-06/plan-macos-v001.md). Specifikace:
[client.md, macOS](../../client.md#macos). Větev `macos`. Veškerý překlad a běh probíhá v GitHub
Actions (`client-macos.yml`, runner `macos-15`, macOS 15.8.2, Xcode 16.4, Apple clang 17, arm64,
Homebrew OpenSSL 3.5.9 staticky, libcurl 8.7.1 z SDK, PHP z Homebrew pro relay fixture). Na Windows
hostu vývoje žádný macOS překladač není, takže tento report je jedinou evidencí.

## Průběh iterací

| Commit | Výsledek | Příčina a oprava |
|---|---|---|
| `90b79ff` | překlad selhal | `memset_s` nedeklarován (makro `__STDC_WANT_LIB_EXT1__` musí předcházet první `<string.h>`); runner `macos-15` nemá PHP, přidán `brew install php`; přidáno `ninja -k 0`. |
| `771a8e0` | překlad selhal | `std::jthread` v `tests/unit/test_logger.cpp` (Apple libc++ ho nemá), nahrazen `std::thread`; linker varování OpenSSL „built for newer macOS“ potlačeno `-Wl,-w`. Vše ostatní (platformní vrstva, služba, CLI, testy) se přeložilo napoprvé. |
| `957b4ad` | 1 unit test selhal | `paths_macos`: `std::filesystem::path::operator==` porovnává po komponentách, dvojité lomítko prošlo validací; opraveno porovnání nativních řetězců. |
| `bf18e9e` | tray přeložen | AppKit tray (6 300 řádků ObjC++) bez jediné chyby překladu, model testy tray prošly. |
| `b667a65` | e2e selhal při Connect | Unit, utun i kontraktní testy zelené; Connect skončil `tunnel_start_failed`, bez detailu v logu. |
| `20c8e1c` | e2e selhal při Connect | Přidán log selhání síťových změn: `add route failed … the routing answer timed out`. |
| `aaa3a0e` | e2e prošel (33/33), balení selhalo | Úspěšný `write` RTM_ADD/RTM_DELETE se bere jako výsledek (kernel vrací odmítnutí jako errno zápisu, ozvěnu `route(8)` u ADD nečte); RTM_GET se čte blokujícím `recv` se `SO_RCVTIMEO`. `productbuild` odmítl `--` uvnitř XML komentáře `distribution.xml`. |
| `a0f0da6` | **zelený běh** | Komentář opraven; `client-macos` i `client-linux` úspěšné, artefakt `phpvpn-macos-arm64` (7,5 MB: `phpvpn-1.0.2-arm64.pkg`, `SHA256SUMS`, `build-provenance.json`, `install.sh`, `uninstall.sh`, `README.md`, logy), běh 41109110153. |

## Stav podle kroků workflow (běh `a0f0da6`, 41109110153)

| Krok | Stav | Poznámka |
|---|---|---|
| Configure (`macos-release`, `PHPVPN_BUILD_CONTRACT_TESTS=ON`) | RUN, OK | OpenSSL 3.5.9 static, CURL 8.7.1 SDK, SystemConfiguration, CoreFoundation. |
| Build (service, CLI, tray, testy) | RUN, OK | `-Wall -Wextra -Wpedantic -Werror` čisté. |
| `phpvpn-core-tests`, `phpvpn-logger-tests` (portable) | RUN, OK | |
| `phpvpn-macos-tests` (81 případů: POSIX config files, IPC kqueue/pipe/getpeereid, hodiny, transport libcurl s vlastním TLS serverem, OpenSSL provider, cesty, kodek PF_ROUTE, identita rozhraní, DNS snapshot) | RUN, OK | |
| `phpvpn-tray-macos-tests` | RUN, OK | |
| `phpvpn-utun-tests` (root: utun, popis, MTU, `IFF_UP`, adresa, routa, send/receive, úklid) | RUN, OK | |
| Kontraktní testy proti `php -S` (6 případů) | RUN, OK | |
| E2E služby (33 kontrol: `--verify-tunnel`, foreground start, socket, status, Wizard IPC s commitem, soubor 0600/0700, set-config/get-config, connect do `connected`, `utun4` s 10.99.99.2, routa 10.99.99/24, `route get`, info, diagnostics, disconnect, úklid adresy, routy i rozhraní, singleton, SIGTERM exit 0, odstranění socketu, souborový log, nezměněná výchozí routa) | RUN, OK | Služba běží jako root z `/private/tmp` fixture, relay `php -S` lokálně. |
| Balení `.pkg` (`cmake --install`, ad hoc `codesign`, `pkgbuild`, `productbuild`, SHA256SUMS, provenance) | RUN, OK | Artefakt `phpvpn-macos-arm64`. |

## Neověřeno

- Ruční instalace `.pkg` na fyzickém Macu, Gatekeeper, LaunchDaemon a LaunchAgent po restartu.
- Tray vizuálně (CI nemá grafickou relaci); přeložen a model testy prošly.
- Provoz peer-to-peer mezi dvěma macOS klienty nebo macOS a Windows/Linux klientem.
- Intel x86_64 (binárka je jen arm64).
- Chování po pádu služby: odstranění `-interface` routy jádrem po zavření `utun` socketu (privilegovaný test to kontroluje, viz `tests/macos/privileged/test_tunnel_utun.cpp`).
