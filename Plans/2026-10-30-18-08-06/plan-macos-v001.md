---
plan_version: 1
created_at: 2026-10-30-18-08-06
source: zadání uživatele v chatu (klientská část phpvpn pro macOS, později iOS; vývoj z Windows s ověřením přes GitHub Actions)
---

# Klient phpvpn pro macOS (třetí platforma nad `phpvpn-core`), výhled iOS

## Rozhodnutí uživatele a výchozí stav

- Primární cíl je **macOS**, iOS následuje jako další etapa. Tento plán proto volí jen taková řešení, která iOS
  nezablokují, ale neplatí za ně dnes (viz část [Výhled iOS](#výhled-ios)).
- Vývoj probíhá na Windows bez Macu. Jediný dostupný překladač a běhové prostředí macOS jsou **GitHub Actions
  macOS runnery** (`macos-15`, Apple Silicon, Xcode, `sudo` bez hesla, předinstalované `php`, `node`, `python`,
  `cmake`, Homebrew). Repozitář `jaromirsivic/phpvpn` je veřejný, minuty macOS runnerů jsou zdarma. Větev `macos`
  existuje jen lokálně a odvíjí se od `main` (commit `9035401`).
- Modely: plán a integrace Fable 5.1 (high), samostatné pracovní balíky Opus 5.5 (high/xhigh) v paralelních
  agentech. Žádný agent neumí macOS kód přeložit; překlad a testy dělá CI, iterace jde přes CI logy.
- Zásady z [client.md](../../client.md) platí beze změny: jádro `phpvpn-core` se nemění, tři VPN pracovní role,
  split tunnel v1 (jen `virtualIp` a jediná routa `vpnCidr`), IPC v1 smlouva, Wizard, konfigurační dokument,
  bezpečnostní model. macOS je třetí platforma nad stejným jádrem, stejně jako Linux podle části
  [Linux](../../client.md#linux).

## Zjištěná omezení a volby

1. **Tunel.** macOS nemá `/dev/net/tun`. Nativní, veřejně podporovaný mechanismus pro root daemon je kernel
   control `utun` (`PF_SYSTEM`/`SYSPROTO_CONTROL`, `com.apple.net.utun_control`), který používají WireGuard,
   OpenVPN i Tailscale mimo App Store. Network Extension (`NEPacketTunnelProvider`) vyžaduje Apple Developer
   Program, entitlementy, podepsání a schválení uživatelem; pro iOS je jedinou cestou. **Rozhodnutí: macOS v1
   používá `utun` v root daemonu pod launchd**, stejný model služba + CLI + tray jako Linux. NE zůstává pro iOS
   a pro případnou App Store distribuci macOS.
   - Jméno `utunN` přiděluje jádro, přesné jméno `phpvpn` nelze vynutit. Konfigurované `adapterName` na macOS je
     proto logické jméno `phpvpn`, skutečné rozhraní se identifikuje přes ifindex a značku vlastnictví.
   - macOS nemá `ifalias`, má však **popis rozhraní** (`SIOCSIFDESC`/`SIOCGIFDESC`, `ifconfig … description`).
     Značka `phpvpn-owner:<GUID>` se ukládá do něj a identita `(ownerTag << 32) | ifindex` zůstává jako na Linuxu.
   - `utun` je datagramové rozhraní s 4bajtovou hlavičkou rodiny protokolu (`htonl(AF_INET)`) před každým
     paketem. Čte se `recvmsg` s detekcí `MSG_TRUNC` (přetečení bufferu se započítá do `oversizedReceiveCount`),
     zapisuje `writev` s hlavičkou. Rozhraní zanikne s uzavřením deskriptoru, takže recovery jádrem platí stejně
     jako na Linuxu; persistentní `utun` neexistuje, `removeOwnedPhantomInterface` je úspěch jen při nepřítomnosti.
2. **Síťová konfigurace.** Místo rtnetlink se používá `PF_ROUTE` socket (`RTM_ADD`, `RTM_DELETE`, `RTM_GET`),
   `sysctl(NET_RT_DUMP)` pro snímek rout, `getifaddrs` pro adresy, `SIOCAIFADDR` pro adresu na point-to-point
   rozhraní (adresa i cíl `virtualIp`, maska z `vpnCidr`), routa `vpnCidr` typu `-interface` (`RTF_UP|RTF_STATIC`,
   gateway `sockaddr_dl` rozhraní). macOS routy nemají metriku; snímek i porovnání existující routy používají
   metriku 0 a shoda se posuzuje podle cíle, rozhraní a next hopu. DNS servery se čtou ze SystemConfiguration
   (`State:/Network/Global/DNS`, klíč `ServerAddresses`), fail closed při chybě čtení store. Výchozí routa je
   `RTM_GET` na `0.0.0.0` s `RTF_GATEWAY`.
3. **Kryptografie.** CommonCrypto nemá veřejné AES-GCM, CryptoKit je jen ve Swiftu. Swift shim v CMake přes
   C++ linker je pro slepý první pokus příliš riskantní. **Rozhodnutí: v1 znovu použije `crypto_openssl.cpp`
   s OpenSSL 3 linkovaným staticky** (Homebrew `openssl@3`, `OPENSSL_USE_STATIC_LIBS=ON`). Žádná runtime
   závislost, žádný nový kryptografický kód. Důsledek: v1 je **arm64 (Apple Silicon)**, univerzální binárka
   vyžaduje druhé sestavení OpenSSL pro x86_64 (volitelný krok 12). CryptoKit shim je plán iOS etapy.
4. **HTTP transport.** macOS SDK obsahuje hlavičky i `libcurl.4.dylib` (macOS 14: 8.4, macOS 15: 8.7).
   `transport_curl.cpp` se použije beze změny; CMake na Apple sníží minimální verzi na 8.4 a linkuje systémovou
   knihovnu. NSURLSession adaptér je iOS etapa (iOS libcurl nemá).
5. **IPC.** Unix socket zůstává. `epoll`/`timerfd`/`eventfd`/`accept4`/`SO_PEERCRED` nejsou na macOS. Sdílená
   POSIX implementace dostane malou abstrakci čekání (`epoll` na Linuxu, `kqueue` na macOS), stop a completed
   signalizaci přes `pipe` tam, kde není `eventfd`, `accept` + `fcntl` místo `accept4`, ověření protistrany
   `LOCAL_PEERCRED` (`xucred`) místo `SO_PEERCRED`. Chování, limity a chybové kódy z client.md se nemění.
6. **Konfigurační soubor.** `config_files_linux.cpp` je POSIX až na `renameat2(RENAME_NOREPLACE)` (macOS:
   `renameatx_np(RENAME_EXCL)`) a `statfs.f_type` magic (macOS: `f_fstypename`, odmítnout `nfs`, `smbfs`,
   `afpfs`, `webdav`, `osxfuse`, `macfuse`, `fuse`, `ftp`). Soubor se přesune do `platform/posix/` se dvěma
   `#if` větvemi. Totéž pro `durable_file_linux.cpp`, `paths_linux.cpp` (jen konstanty cest se liší), `fd.h`.
7. **Hodiny.** Na Darwinu `CLOCK_MONOTONIC` běží i během spánku (obdoba `CLOCK_BOOTTIME`), `CLOCK_UPTIME_RAW`
   se během spánku zastaví (obdoba linuxového `CLOCK_MONOTONIC`). Mapování: `monotonicMs` → `CLOCK_UPTIME_RAW`,
   `leaseMonotonicMs` → `CLOCK_MONOTONIC`, `systemUtcMs` → `CLOCK_REALTIME`.
8. **Cesty a důvěryhodné předky.** `openTrustedDirectory` otevírá komponenty s `O_NOFOLLOW`; `/var` a `/etc`
   jsou na macOS symbolické odkazy do `/private`. Produkční cesty proto používají kanonické `/private/var/…`.
   `/Library` má sticky bit a je zapisovatelný skupinou `admin`, což sdílené pravidlo připouští;
   `/Library/Application Support` je `root:admin 0755`.
9. **Správa služby.** launchd nemá `Type=notify`, `RuntimeDirectory=` ani restartovací řadu 5/17/60 s.
   Daemon si runtime adresář vytvoří sám, `KeepAlive` s `SuccessfulExit=false` a `ThrottleInterval=5`
   dává restart po pádu (launchd vynucuje nejméně 10 s). Log jde do souboru s rotací 8 MiB × 5 jako na Windows
   (`Logger::configureFile`), ne do journalu; `--foreground` loguje na stdout.
10. **Podepisování a Gatekeeper.** Bez Developer ID se binárky podepíší ad hoc (`codesign -s -`), `.pkg` je
    nepodepsaný. Instalace přes `sudo installer -pkg` z terminálu Gatekeeper neblokuje; dvojklik na stažený
    `.pkg` ano. Stejně jako Windows vývojový certifikát jde o výslovně dokumentované omezení, manifest složky se
    podepisuje stejným RSA mechanismem jako Linux a ověřuje `openssl` (LibreSSL CLI je součástí macOS).
11. **Tray.** Nativní AppKit (`NSStatusItem`, `NSWindow`, `NSTableView`) v Objective-C++, aby mohl přímo
    používat přenosné C++ modely (`status_model`, `progress_model`, `wizard_model`, `contracts_model`,
    `password_key_model`, `server_info_model`) a vlákenný `ipc_async` z `tray-linux` s vlastním dispatcherem
    (`dispatch_async(dispatch_get_main_queue())`). Bez Qt, Electronu, WebView, bez Swift UI v této etapě.
12. **Co se nemění.** Wire hodnota `"winhttp"` v diagnostice (pevná smlouva IPC v1), schéma IPC, konfigurační
    dokument, Wizard, limity front, chybové kódy. Linuxový a Windows klient musí zůstat beze změny chování;
    POSIX refaktor hlídá nový linuxový CI job.

## Struktura

```
.github/workflows/
  client-macos.yml                 build + testy + balení na macos-15, artefakt phpvpn-macos-arm64
  client-linux.yml                 build + neprivilegované testy na ubuntu-24.04 (hlídá POSIX refaktor)
client/
  CMakeLists.txt                   + APPLE: PHPVPN_PLATFORM "macos", clang flagy bez GNU ld voleb
  CMakePresets.json                + macos-debug, macos-release, macos-asan, macos-headless
  src/CMakeLists.txt               + platform/posix/*, platform/macos/*, OpenSSL static, systémový curl,
                                     frameworky SystemConfiguration, CoreFoundation
  src/platform/posix/              sdílené POSIX soubory (přesun z linux/, Linux includy zůstávají přes
    fd.h, durable_file_posix.cpp,    tenké přesměrovací hlavičky)
    paths_posix.{h,cpp}, config_files_posix.{h,cpp}, event_wait.{h,cpp}, ipc_unix.{h,cpp}
  src/platform/macos/
    clock_macos.cpp                CLOCK_UPTIME_RAW / CLOCK_MONOTONIC / CLOCK_REALTIME
    paths_macos.h                  kanonické cesty a režimy (viz tabulka)
    interface_identity.{h,cpp}     značka vlastnictví v popisu rozhraní, (ownerTag << 32) | ifindex
    tunnel_utun.cpp                TunnelDriver/Tunnel přes utun
    route_socket.{h,cpp}           PF_ROUTE klient: sekvence, timeout 2 s, kódování sockaddr
    route_codec.{h,cpp}            čisté kódování/dekódování rt_msghdr + sockaddr pole (testovatelné bez root)
    netconfig_route.cpp            NetConfig: snapshot, adresa, routa, identita, phantom, reachability
    dns_snapshot.{h,cpp}           SystemConfiguration State:/Network/Global/DNS
  apps/CMakeLists.txt              + APPLE větev: phpvpn-service, phpvpn-cli, tray-macos
  apps/service/service_main_macos.cpp   --launchd | --foreground | --verify-tunnel, kqueue signály, flock
  apps/cli/main.cpp                + __APPLE__ includy, hint řádky, cesta konfigurace v textech
  apps/tray-macos/                 AppKit Objective-C++ menu bar aplikace (viz krok 9)
  packaging/macos/
    launchd/com.pilandrome.phpvpn.plist, com.pilandrome.phpvpn-tray.plist
    pkg/scripts/preinstall, postinstall; pkg/distribution.xml; pkg/Info.plist (tray bundle)
    dist/README.md, install.sh, uninstall.sh
  tools/macos/
    build.sh                       configure + build + ctest s macos-* presetem
    package-pkg.sh                 pkgbuild/productbuild, ad hoc codesign, iconutil, SHA256SUMS, provenance
    run-server-contract.sh         kontraktní testy proti lokálnímu php -S
    make-macos-dist.ps1            stage dist/client/macOS z artefaktu CI, podepsaný manifest
  tests/macos/unit/                test_clock_macos, test_route_codec, test_interface_identity_macos,
                                   test_paths_macos, test_config_files_posix, test_ipc_unix (sdílené)
  tests/macos/privileged/          test_tunnel_utun.cpp (root: utun + adresa + routa + snapshot + úklid)
  tests/e2e/macos/                 run-service-e2e.sh (služba foreground jako root, lokální relay, CLI)
tools/make-distribution.ps1        client/macOS už není placeholder, volá make-macos-dist.ps1
```

Produkční cesty macOS (obdoba tabulky Linux):

| Cesta | Vlastník a režim | Obsah |
|---|---|---|
| `/Library/Application Support/phpvpn/` | `root:wheel`, `0700` | Konfigurační adresář, `config.json` `0600`, publikační soubory. |
| `/private/var/db/phpvpn/` | `root:wheel`, `0700` | `ownership.json` (záznam vlastnictví rozhraní). |
| `/private/var/run/phpvpn/` | `root:wheel`, `0755` | `control-v1.sock` `0666`, `service.lock` `0600`. Vytváří daemon při startu. |
| `/private/var/log/phpvpn/` | `root:wheel`, `0755` | `phpvpn-service.log` s rotací 8 MiB × 5; launchd stdout/stderr `launchd.log`. |
| `/Library/PrivilegedHelperTools/phpvpn-service` | `root:wheel`, `0755` | Daemon. |
| `/usr/local/bin/phpvpn` | `root:wheel`, `0755` | CLI, čistý IPC klient. |
| `/Applications/phpvpn Tray.app` | `root:wheel` | Menu bar aplikace, `LSUIElement`. |
| `/Library/LaunchDaemons/com.pilandrome.phpvpn.plist` | `root:wheel`, `0644` | Registrace daemonu. |
| `/Library/LaunchAgents/com.pilandrome.phpvpn-tray.plist` | `root:wheel`, `0644` | Autostart tray po přihlášení. |

`kIpcSocketPath` v [ipc_protocol.h](../../client/src/core/ipc/ipc_protocol.h) dostane větev `__APPLE__` s
`/private/var/run/phpvpn/control-v1.sock`. `TunnelSessionSettings` dostane větev `__APPLE__` s `adapterName`
`phpvpn`, `tunnelType` `utun` a vlastním GUID `{a1c4e7b2-6d3f-4a8e-9b0c-5e2d7f1a3c64}`.

## Kroky

### 0. Sestavení, presety a CI kostra (WP00, Fable)

- [client/CMakeLists.txt](../../client/CMakeLists.txt): `elseif(APPLE) set(PHPVPN_PLATFORM "macos")`,
  `CMAKE_OSX_DEPLOYMENT_TARGET` 14.0, `-fstack-protector-strong -fPIE`, `_FORTIFY_SOURCE=2` (Apple clang),
  žádné `-Wl,-z,…`, `-Wl,-pie` není třeba (výchozí). ASan/UBSan volba zůstává.
- Presety `macos-debug`, `macos-release` (`RelWithDebInfo`), `macos-asan`, `macos-headless`
  (`PHPVPN_BUILD_TRAY=OFF`), generátor Ninja, `CMAKE_CXX_COMPILER` `clang++`, `binaryDir` `work/build/<preset>`.
- `src/CMakeLists.txt`: větev `APPLE` s `find_package(OpenSSL 3 REQUIRED)` (`OPENSSL_USE_STATIC_LIBS ON`,
  `OPENSSL_ROOT_DIR` z `brew --prefix openssl@3` v presetu/CI), `find_package(CURL 8.4 REQUIRED)` ze SDK,
  frameworky `SystemConfiguration` a `CoreFoundation`, seznam souborů podle struktury výše.
- `apps/CMakeLists.txt`: větev `APPLE` před Windows blokem (service, cli, `add_subdirectory(tray-macos)` při
  `PHPVPN_BUILD_TRAY`), `install()` do layoutu `.pkg` (`DESTDIR` staging: `Library/PrivilegedHelperTools`,
  `usr/local/bin`, `Applications`, `Library/LaunchDaemons`, `Library/LaunchAgents`).
- `tests/CMakeLists.txt`: větev `APPLE` s `phpvpn-macos-tests` (label `macos`), `phpvpn-utun-tests`
  (label `privileged`, volba `PHPVPN_BUILD_PRIVILEGED_TESTS`), kontraktní testy beze změny.
- `.github/workflows/client-macos.yml`: `push` na `macos` a `main`, `pull_request`, `workflow_dispatch`;
  `macos-15`; `brew install ninja openssl@3`; `cmake --preset macos-release`; build; `ctest -L portable`,
  `ctest -L macos`; `sudo ctest -L privileged`; kontraktní testy s `php -S` přes
  `tools/macos/run-server-contract.sh`; `sudo tests/e2e/macos/run-service-e2e.sh`; `tools/macos/package-pkg.sh`;
  `actions/upload-artifact` `phpvpn-macos-arm64` (pkg, SHA256SUMS, build-provenance.json, logy testů).
  Souhrn výsledků do `$GITHUB_STEP_SUMMARY`.
- `.github/workflows/client-linux.yml`: `ubuntu-24.04`, `apt-get install ninja-build libcurl4-openssl-dev
  libssl-dev libsystemd-dev libgtk-3-dev libayatana-appindicator3-dev`, preset `linux-release`, build, `ctest
  -L portable`, `ctest -L linux`. Chrání Linux před POSIX refaktorem.

### 1. POSIX refaktor sdílených souborů (WP01, Opus high)

- Přesun `fd.h`, `durable_file_linux.cpp`, `paths_linux.{h,cpp}`, `config_files_linux.{h,cpp}` do
  `platform/posix/` pod názvy `*_posix.*`; typy `LinuxDirectories`, `LinuxConfigFilePolicy` atd. dostanou
  POSIX názvy a původní linuxové hlavičky zůstanou jako tenké `#include` + `using` aliasy, aby se existující
  linuxový kód a testy nemusely měnit.
- `paths_posix.h`: konstanty cest se vybírají podle platformy (`kPosixConfigDirectory` …) z `paths_macos.h`
  nebo z linuxových hodnot; `resolve*Directories`, `openTrustedDirectory`, `secureServiceDirectory` beze změny.
- `config_files_posix.cpp`: `renameat2(RENAME_NOREPLACE)` vs. `renameatx_np(RENAME_EXCL)`; kontrola
  souborového systému přes `f_type` magic (Linux) vs. `f_fstypename` (macOS); `fdatasync` → `fsync`
  (na macOS navíc `fcntl(F_FULLFSYNC)` pro journal a publikaci). `O_DIRECTORY|O_NOFOLLOW`, `*at` volání,
  `linkat`, `openat(O_CREAT|O_EXCL)` jsou na macOS dostupné.
- `durable_file_posix.cpp`: beze změny logiky, `F_FULLFSYNC` na macOS.
- Linux build musí projít beze změny chování (CI job z kroku 0).

### 2. IPC přes Unix socket pro obě platformy (WP02, Opus high)

- `event_wait.{h,cpp}`: minimální `Poller` (přidat/odebrat fd se zájmem o čtení/zápis, čekat s timeoutem,
  periodický tick 250 ms) nad `epoll` + `timerfd` (Linux) a `kqueue` + `EVFILT_TIMER` (macOS); `Notifier`
  nad `eventfd` (Linux) a `pipe` (macOS) se stejným `signal()`/`drain()`.
- `ipc_unix.cpp` → `platform/posix/ipc_unix.cpp`: server používá `Poller`/`Notifier`, `accept` + `O_NONBLOCK`
  + `FD_CLOEXEC` kde chybí `accept4`, `MSG_NOSIGNAL` → `SO_NOSIGPIPE` na macOS; klient ověřuje uid 0 přes
  `LOCAL_PEERCRED` (`struct xucred`, `cr_uid`). Deadline 5000 ms, 32 slotů, rámec `u32 BE` + JSON, `busy`,
  `SHUT_WR` a čekání na uzavření zůstávají.
- `tests/linux/unit/test_ipc_unix.cpp` se přesune do `tests/posix/unit/` a přidá se do obou platformních
  testovacích binárek.

### 3. Hodiny, cesty, identita a kryptografie (WP03, Opus high)

- `clock_macos.cpp` podle bodu 7 omezení; test `test_clock_macos.cpp` (monotónnost, lease ≥ monotonic).
- `paths_macos.h` s tabulkou cest, `secureServiceDirectories` v pořadí config, state, runtime, logs.
- `interface_identity.{h,cpp}` pro macOS: stejné `canonicalInstallationGuid`, `ownershipMarker`,
  `ownerTagForGuid`; čtení a zápis popisu rozhraní přes `ioctl(SIOCGIFDESC/SIOCSIFDESC, struct if_descreq)`
  (`ifdr_name`, `ifdr_len`, `ifdr_desc`, nejvýše `IF_DESCSIZE` 128 bajtů).
- `SecureBuffer`: `explicit_bzero` je na macOS 10.12+ dostupné; ověřit, jinak `memset_s`.
- Kryptografie: `crypto_openssl.cpp` se přidá do macOS platformní knihovny beze změny.

### 4. Tunel utun (WP04, Opus xhigh)

- `loadAndVerify`: otevře `PF_SYSTEM` socket, `CTLIOCGINFO` pro `com.apple.net.utun_control`, nic nepřipojí,
  zavře. Bez root selže `EPERM` a vrátí `tunnel_driver_unavailable`-ekvivalent z existujících kódů.
- `createAdapter(name, tunnelType, guid)`: `socket(PF_SYSTEM, SOCK_DGRAM, SYSPROTO_CONTROL)`, `connect` se
  `sc_unit = 0` (jádro přidělí první volný `utunN`), `getsockopt(UTUN_OPT_IFNAME)` → skutečné jméno,
  `fcntl(O_NONBLOCK|FD_CLOEXEC)`, `SIOCSIFDESC` značka vlastnictví, `SIOCSIFMTU` 1500, nejlépe
  `SIOCPROTODETACH_IN6` (best effort, chyba se loguje a neukončuje), `SIOCSIFFLAGS` `IFF_UP`; ověření jména,
  popisu, MTU a `IFF_UP`; vrátí identitu `(ownerTag << 32) | if_nametoindex`.
- `openAdapter`: `not_supported`. `startSession`: `recvmsg` s `iovec[2]` (4bajtová hlavička + buffer volajícího),
  `MSG_TRUNC` → oversized; `writev` s hlavičkou `htonl(AF_INET)`; čekání `poll` nad socketem a stop pipe,
  plná fronta 200 × 5 ms a potom `busy`. `endSession` zastaví a odpojí session, deskriptor drží;
  `closeAdapter` zavře a čeká nejvýše 2 s na zmizení ifindexu (`if_indextoname`). `runningDriverVersion` 0.
- Název logického rozhraní `phpvpn` z nastavení se používá v journalu a v logu; skutečné `utunN` se loguje
  při vytvoření.

### 5. Síťová konfigurace přes PF_ROUTE a SystemConfiguration (WP05, Opus xhigh)

- `route_codec`: čisté sestavení `rt_msghdr` + zarovnaných `sockaddr` (`RTA_DST`, `RTA_GATEWAY`, `RTA_NETMASK`,
  `RTA_IFP`), dekódování `RTM_GET` odpovědí a `NET_RT_DUMP`/`NET_RT_IFLIST2` výpisů. Unit testy bez root.
- `route_socket`: `socket(PF_ROUTE, SOCK_RAW, AF_INET)`, `rtm_seq`/`rtm_pid` korelace, timeout 2 s, mapování
  `errno` do textu jen číslem.
- `netconfig_route.cpp`:
  - `snapshot()`: adresy z `getifaddrs` (AF_INET, identita z ifindex + popis), routy z `NET_RT_DUMP`
    (jen `AF_INET`, bez `RTF_WASCLONED`/`RTF_LLINFO`, next hop z gateway nebo `0.0.0.0` u `RTF_GATEWAY` nenastaveno),
    výchozí routa z `RTM_GET` na `0.0.0.0/0`, DNS z `dns_snapshot`.
  - `interfaceExists(identity)`: `if_indextoname` + shoda popisu s tagem.
  - `addUnicastAddress`: ověření značky, `SIOCAIFADDR` (`ifra_addr`, `ifra_dstaddr` = táž adresa, `ifra_mask`);
    `EEXIST`/shoda = úspěch jen při přesné shodě. `removeUnicastAddress`: `SIOCDIFADDR`, nepřítomné rozhraní
    = úspěch.
  - `addRoute`: `RTM_ADD` s `RTF_UP|RTF_STATIC` (+ `RTF_GATEWAY` při next hopu ≠ 0.0.0.0, jinak `sockaddr_dl`
    rozhraní), `EEXIST` = úspěch při přesné shodě (ověřit `RTM_GET`). `removeRoute`: `RTM_DELETE`, `ESRCH` nebo
    zaniklé rozhraní = úspěch.
  - `removeOwnedPhantomInterface`: rozhraní nepřítomno → ok; přítomno → `journal_ambiguous`.
  - `interfaceLuidForAlias`: `if_nametoindex` + tag z popisu. `isHostReachableOutsideInterface`:
    `getaddrinfo` + `RTM_GET` a porovnání `rtm_index`.
- `dns_snapshot`: `SCDynamicStoreCreate`, `SCDynamicStoreCopyValue("State:/Network/Global/DNS")`,
  `ServerAddresses` → IPv4, seřazené, bez duplicit; chybějící klíč = žádný server, chyba store = fail closed.

### 6. Služba pod launchd a CLI (WP06, Opus high)

- `service_main_macos.cpp`: režimy `--launchd` (výchozí, když je nastaveno `XPC_SERVICE_NAME` nebo rodič je
  launchd, tj. `getppid() == 1`), `--foreground`/`--console`, `--verify-tunnel`, `--help`; skryté `--config-dir`,
  `--state-dir`, `--runtime-dir`, `--log-dir`, `--adapter-name` pro izolované testy. Signály SIGTERM/SIGINT přes
  `kqueue` `EVFILT_SIGNAL` (signály blokované před vznikem vláken), SIGHUP a SIGPIPE ignorované. Kontrola
  `geteuid() == 0`, `secureServiceDirectory` runtime i logs, `flock` singleton, `Logger::configureFile`
  (`/private/var/log/phpvpn`, 8 MiB × 5) v režimu launchd, stdout sink ve foreground. Status řádek se loguje při
  změně (launchd nemá `STATUS=`). Shodný `ServiceRuntime` a pořadí teardownu.
- `apps/cli/main.cpp`: `#elif defined(__APPLE__)` pro `platform/posix/ipc_unix.h`, `hint:` řádky jako Linux
  (služba neběží: `sudo launchctl bootstrap system /Library/LaunchDaemons/com.pilandrome.phpvpn.plist`),
  text cesty konfigurace `/Library/Application Support/phpvpn/config.json`.
- `com.pilandrome.phpvpn.plist`: `Label`, `ProgramArguments` `[…/phpvpn-service, --launchd]`, `RunAtLoad`,
  `KeepAlive` `{SuccessfulExit: false}`, `ThrottleInterval` 5, `ExitTimeOut` 45, `StandardOutPath`/`StandardErrorPath`
  `/private/var/log/phpvpn/launchd.log`, `ProcessType` `Interactive` ne, `Nice` 0.

### 7. Testy na macOS (WP07, Opus high)

- `phpvpn-macos-tests` (bez root): `test_clock_macos`, `test_paths_macos` (překlad `/private/var`, odmítnutí
  symlinku), `test_config_files_posix` (sdílený s Linuxem včetně hooků stage/crash/recover), `test_ipc_unix`
  (sdílený), `test_route_codec`, `test_interface_identity_macos`, `test_transport_curl` (sdílený, lokální
  `php -S` není nutný: používá vlastní loopback server testu jako na Linuxu), `test_dns_snapshot` (parsování
  `CFDictionary` fixture bez živého store).
- `phpvpn-utun-tests` (root, label `privileged`): vytvoření utun, značka, MTU, `IFF_UP`, `SIOCAIFADDR`,
  routa `vpnCidr`, snímek obsahuje adresu i routu, odeslání paketu do rozhraní a jeho přečtení druhým směrem
  (ICMP echo na vlastní VPN adresu jde přes loopback, proto se ověřuje `send` syntetického paketu a `receive`
  paketu, který jádro odešle po `connect` UDP socketu s `IP_BOUND_IF` na utun), `closeAdapter` a zmizení.
- Kontraktní testy: `tools/macos/run-server-contract.sh` spustí `php -S 127.0.0.1:18080` s `server/php`,
  nastaví `PHPVPN_CONTRACT_URL` a spustí `phpvpn-contract-tests`.
- `tests/e2e/macos/run-service-e2e.sh` (root): dočasné adresáře, `phpvpn-service --foreground` s override
  přepínači, `phpvpn --socket … status`, `set-config` s lokálním relay (`php -S`), `connect` do stavu
  `connected` do 30 s, kontrola `ifconfig`/`netstat -rn` (adresa i routa přítomná), `disconnect`, kontrola
  úklidu, SIGTERM, `wizard` IPC sonda z `tests/e2e/linux/wizard_ipc_probe.cpp` (sdílená).

### 8. Balení, instalace a distribuce (WP08, Opus high)

- `package-pkg.sh`: `cmake --install` do stagingu, `iconutil` z PNG v `assets/` na `phpvpn.icns`, ad hoc
  `codesign --force -s - --options runtime` pro service, CLI a tray bundle, `pkgbuild --root staging
  --scripts packaging/macos/pkg/scripts --identifier com.pilandrome.phpvpn --version <ver> --install-location /`,
  `productbuild --distribution distribution.xml` → `phpvpn-<ver>-arm64.pkg`, `SHA256SUMS`,
  `build-provenance.json` (`sourceCommit`, `sourceDirty`, runner, Xcode, OpenSSL verze).
- `postinstall`: `launchctl bootout system/com.pilandrome.phpvpn` (best effort), `bootstrap system`,
  `kickstart -k`, čekání na socket 30 s, `phpvpn status`; tray LaunchAgent `bootstrap gui/<uid>` pro
  přihlášeného uživatele (`stat -f %u /dev/console`). `preinstall`: `phpvpn disconnect` best effort,
  `bootout` daemonu i agenta.
- `dist/install.sh` (`sudo sh install.sh [--headless] [--verify-only]`): root, macOS ≥ 14 a arm64 z `sw_vers`
  a `uname -m`, pin SPKI SHA-256 a ověření `release-manifest.sig` přes `openssl dgst -sha256 -verify`, SHA-256
  každého souboru na soukromé kopii, `installer -pkg … -target /`, socket do 30 s, `phpvpn status`;
  `--headless` nastaví `PHPVPN_INSTALL_TRAY=0` pro `postinstall` (tray se neregistruje). `uninstall.sh
  [--remove-data]`: `disconnect`, `bootout`, odstranění souborů podle `pkgutil --files`, `pkgutil --forget`,
  data jen s `--remove-data`.
- `dist/README.md` anglicky podle vzoru Linuxu (obsah složky, požadavky, instalace, Gatekeeper poznámka,
  konfigurace, CLI, tray, odinstalace, omezení: arm64, ad hoc podpis, utun bez Network Extension).
- `make-macos-dist.ps1`: vstup `work/macos-packages/<id>` (stažený artefakt CI), výstup
  `dist/client/macOS/arm64/` s pkg, skripty, README a manifestem podepsaným stejným certifikátem;
  `tools/make-distribution.ps1` a `distribution-common.ps1` nahradí placeholder voláním a `verify-distribution.ps1`
  získá kontrolu složky macOS. `make-download-archives.ps1` beze změny (`phpvpn-macos.zip` = `client/macOS`).

### 9. Tray pro macOS (WP09, Opus xhigh)

- `apps/tray-macos/` v Objective-C++ (`.mm`), bundle `phpvpn Tray.app` (`Info.plist`: `LSUIElement` 1,
  `CFBundleIdentifier` `com.pilandrome.phpvpn-tray`, minimální systém 14.0), ikony stavů z `assets/*.png`
  jako template `NSImage` pro `NSStatusItem`.
- Jedna instance na relaci (`NSRunningApplication` se stejným bundle ID → `--show-window` přes
  `NSDistributedNotificationCenter`), `--installation-complete` zobrazí `UNUserNotification`.
- Menu a okna podle [tray_UX.md](../../tray_UX.md) se stejnými texty, barvami a geometriemi z přenosných modelů:
  Connect/Disconnect progress, Configuration Wizard (kroky 0–4, HTTP headers, HTTP error detail), Edit Config
  (`NSTextView`), Communication contracts (`NSTableView`, nejvýše 252 řádků), Password SHA, Server Information,
  Statistics, Exit. Dirty-state a souběh uložení přes `contracts_save_machine`. IPC přes sdílený `ipc_async`
  s dispatcherem na hlavní frontu a `pipe` jako cancel deskriptor.
- Oddíl `macOS (AppKit)` v `tray_UX.md` s odchylkami (žádný tooltip delší než 1 řádek, pozice oken řídí
  AppKit, menu bar místo tray, notifikace přes UserNotifications).
- Testy modelů bez AppKit v `phpvpn-macos-tests`; UI se ověřuje v CI pouze spuštěním `--help` a validací
  bundle (`codesign --verify`, `plutil -lint`).

### 10. Dokumentace (WP10, Fable)

- `client.md`: část `### macOS` přepsat na popis dodaného portu podle vzoru Linuxu (sestavení, součásti,
  balení, cesty, služba a launchd, IPC, konfigurační soubor, utun, PF_ROUTE, transport/krypto/hodiny, log, tray,
  tabulka rozdílů); úvodní odstavce a část [Sestavení klienta](../../client.md#sestavení-klienta) doplnit
  o macOS; `Doporučené pořadí další práce` aktualizovat (iOS).
- `distribution.md` (oddíl macOS, složka `client/macOS/arm64`), `readme.txt` (postup instalace macOS),
  `tray_UX.md` (oddíl macOS), `client/packaging/macos/dist/README.md`.
- Datovaný report `client/tests/macos-client-report-<datum>.md` s výsledky CI běhů (co prošlo, co se neověřilo:
  Intel, Gatekeeper dvojklik, reálná síť peer-to-peer).

### 11. Ověření přes GitHub Actions a iterace (WP11, Fable)

- Push větve `macos`, sledování běhu `client-macos.yml` a `client-linux.yml` přes GitHub API (veřejný repozitář)
  a přes přihlášený Chrome pro logy. Oprava chyb překladu a testů po iteracích, dokud není zelený build, testy a
  artefakt `phpvpn-macos-arm64`.
- Výstup pro uživatele: odkaz na artefakt, postup `sudo installer -pkg`, co bylo a nebylo ověřeno.

### 12. Volitelné: univerzální binárka (x86_64)

- CI krok, který sestaví OpenSSL 3 ze zdrojů pro `darwin64-x86_64-cc no-shared` (cache podle verze), druhý
  CMake build s `CMAKE_OSX_ARCHITECTURES=x86_64` a `lipo -create` obou sad binárek; `.pkg` `universal`.
  Teprve potom může `README.md` uvádět Intel.

## Výhled iOS

Po dokončení macOS v1 lze pro iOS znovu použít beze změny `phpvpn-core` (včetně front, protokolu, Wizard
modelů), `platform/posix/clock` a `durable_file`, logiku `config_files_posix` (v kontejneru app group),
`crypto_openssl.cpp` (OpenSSL se pro iOS sestavuje) nebo nový CryptoKit shim, a testy jádra. Nelze použít
`utun`, `PF_ROUTE`, launchd, Unix socket IPC ani AppKit tray:

| Vrstva | iOS řešení |
|---|---|
| Tunel | `NEPacketTunnelProvider` v app extension; `Tunnel::receive` z `packetFlow.readPackets`, `send` přes `writePackets`, přes malý C bridge a frontu. Adresy, routy a DNS nastavuje `NEPacketTunnelNetworkSettings`, `NetConfig` je no-op s věrným snímkem. |
| Transport | NSURLSession adaptér (ObjC++), stejné chování jako WinHTTP/libcurl: jeden deadline, limit těla, bez redirectů, bez proxy, `trustAnyTlsCertificate` přes delegát. Tento adaptér lze později nasadit i na macOS. |
| Kryptografie | CryptoKit shim ve Swiftu (`@_cdecl`) nebo statický OpenSSL pro iOS. |
| IPC | `sendProviderMessage` mezi aplikací a extension místo socketu; stejná JSON schémata IPC v1. |
| UI | SwiftUI aplikace s obdobou Wizardu, kontraktů a statistik; konfigurace v app group kontejneru, tajemství v Keychain. |
| Distribuce | Apple Developer Program, entitlement `packet-tunnel-provider`, TestFlight/App Store, žádný `sudo`. |

Volby v tomto plánu (abstrakce `Tunnel`/`NetConfig`/`Transport` zůstávají jediné místo platformní závislosti,
ObjC++ místo Swiftu v jádře, PF_ROUTE jen v `platform/macos`) nic z toho nezablokují.

## Pracovní balíky a paralelizace

| WP | Obsah | Model | Závislost |
|---|---|---|---|
| WP00 | CMake, presety, CI kostra, konstanty jádra | Fable | – |
| WP01 | POSIX refaktor (fd, durable, paths, config files) | Opus high | WP00 (názvy souborů) |
| WP02 | IPC `Poller`/`Notifier`, `LOCAL_PEERCRED` | Opus high | WP01 (fd.h umístění) |
| WP03 | Hodiny, cesty macOS, identita rozhraní | Opus high | WP00 |
| WP04 | Tunel utun | Opus xhigh | WP03 (identita) |
| WP05 | PF_ROUTE netconfig, DNS snapshot | Opus xhigh | WP03 |
| WP06 | Služba launchd, CLI | Opus high | WP01, WP02 |
| WP07 | Testy macOS | Opus high | WP01–WP06 |
| WP08 | Balení a distribuce | Opus high | WP06 |
| WP09 | Tray AppKit | Opus xhigh | WP02 (ipc) |
| WP10 | Dokumentace | Fable | vše |
| WP11 | CI iterace | Fable | vše |

Sdílené CMake soubory edituje pouze WP00/Fable; agenti vytvářejí jen soubory uvedené ve struktuře. Každý agent
pracuje v jiné sadě souborů. Překlad probíhá výhradně v CI, proto každý balík končí sebekontrolou proti
hlavičkám rozhraní (`platform/iface/*.h`) a Linux vzoru.
