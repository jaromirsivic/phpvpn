---
plan_version: 1
created_at: 2026-09-26-09-24-37
timezone: Europe/Berlin
source: ../../Intents/2026-09-26-08-41-17/intent-v001.md
branch: linux
base_commit: b45cd15
---

# Realizační plán linuxového klienta phpvpn

## 1. Zadání a hranice

Autoritativním zadáním je [intent v001](../../Intents/2026-09-26-08-41-17/intent-v001.md). Tento plán je úplná neměnná verze 1. Pozdější věcné upřesnění vytvoří celý `plan-v002.md`, tato verze se nepřepisuje. Samotné plánování neopravňuje k implementaci. Intent se nemění. Soubor `.intent-builder-active` patří koordinačnímu workflow a realizace ho nemění.

Výsledkem realizace bude nativní linuxový klient se třemi součástmi, stejně jako na Windows:

1. `phpvpn-service` je systemd služba. Vlastní konfiguraci, TUN rozhraní, adresu, routu, tři pracovní role a stav VPN.
2. `phpvpn` je CLI. Je to čistý IPC klient se stejnými příkazy a výstupy jako Windows CLI a funguje bez grafiky.
3. `phpvpn-tray` je tray aplikace v GTK 3 přes StatusNotifierItem (AppIndicator) pro GNOME na Fedoře a Ubuntu.

Priority určují pořadí práce, ne rozsah. Hotové řešení obsahuje všechny tři části:

1. služba, protokol a skutečné spojení,
2. CLI,
3. tray.

**Pracovní větev:** `linux`, výchozí HEAD `b45cd15`, čistý strom. Pracovat přímo v této větvi, bez worktree pro hlavní linii. Commit vzniká po každém dokončeném a ověřeném pracovním balíku (WP). Push, merge do `main` a PR jen na výslovný pokyn uživatele.

**Testovací Fedora VM:** přístupové údaje jsou v oddílu „Dostupný testovací stroj Fedora“ v intentu. Plán je záměrně neopakuje. Nesmí se dostat do repozitáře, commitů, reportů, logů ani skriptů. Předávají se jen za běhu, například přes proměnnou prostředí.

## 2. Ověřený výchozí stav

Kontext pochází z přímého čtení zdrojů a ze čtyř průzkumných agentů, protože `graft` v tomto prostředí není na PATH. Realizátor má na začátku WP00 zkusit `graft map`. Když funguje, použije ho podle `AGENTS.md`. Rozsahy řádků platí pro commit `b45cd15`.

| Zdroj a rozsah | Význam pro realizaci |
|---|---|
| `client/CMakeLists.txt:12–14` | `FATAL_ERROR` mimo Windows. |
| `client/CMakeLists.txt:16–60` | Volby a definice jen pro MSVC, platí i pro core. |
| `client/CMakePresets.json` | Presety pevně s `cl.exe` a strategií architektury `external`. |
| `client/src/CMakeLists.txt:1–83` | `phpvpn-core` je přenositelný seznam. `phpvpn-platform` jsou jen zdroje z `platform/windows`. |
| `client/apps/CMakeLists.txt` | Win32 resource, manifesty, package GUI, kopie `wintun.dll`. |
| `client/src/platform/iface/*.h` | Rozhraní `Clock`, `ConfigFiles`, `NetConfig`, `Transport`, `Tunnel/TunnelDriver`. `CryptoProvider` je v `core/crypto/crypto_provider.h:29–64`. |
| `platform/iface/netconfig.h:17–80` | LUID (`uint64`), aliasy `std::wstring`, `removeOwnedPhantomInterface` (PnP). |
| `platform/iface/tunnel.h:60–80` | GUID, tunnel type, `ringCapacity`, `loadAndVerify(installDirectory)`. |
| `platform/windows/netconfig_iphlpapi.cpp:464–473` | Definice sdílené `NetworkSnapshot::sameDefaultRouteAndDns`, pro Linux ji přesunout. |
| `core/runtime/tunnel_session.cpp:168–233` | Recovery po pádu přes journal a `interfaceExists` s oknem 5 s. |
| `core/runtime/tunnel_session.cpp:235–261` | Fail-closed preflight: kolize adresy nebo routy `vpnCidr`. |
| `core/runtime/tunnel_session.cpp:267–366` | Start: journal, adresa, jediná on-link routa s metrikou 5, kontrola výchozí routy a DNS, session. |
| `core/runtime/tunnel_session.cpp:397–424` | Stop: routa, adresa, session, adaptér. |
| `core/runtime/tunnel_session.cpp:41–66` | Journal zapisovaný bez `fsync`. |
| `core/runtime/tunnel_session.h:44–51` | `L"PHPVPN"`, pevný GUID, 4 MiB ring, `installDirectory`. |
| `core/runtime/vpn_engine.cpp:375–519` | Connect: health, info, kontroly, `tunnelSession_.start`, workery. |
| `core/runtime/vpn_engine.cpp:364–373, 690–766` | Backoff 1–60 s s jitterem a recovery. |
| `core/ipc/ipc_protocol.h:24–33` | IPC v1, jméno pipe, limit 4 MiB. |
| `core/ipc/ipc_protocol.cpp:19–29` | Sedm základních příkazů. |
| `core/ipc/wizard_ipc.cpp:100–130` | 17 Wizard příkazů. |
| `core/ipc/wizard_ipc.cpp:26–50` | `absoluteConfigPath` přijímá jen Windows cesty. Na Linuxu by Wizard odmítl `/etc/...`. |
| `core/ipc/wizard_ipc.cpp:65–66, 89–90` | Wire hodnota `"winhttp"` ve zdroji hlaviček diagnostiky. |
| `core/wizard/http_diagnostic.cpp:157–178` | Ruční UTF-16 surrogates, chybné pro 32bitový `wchar_t`. |
| `core/base/secure_buffer.cpp:3–20` | `SecureZeroMemory`, jinak fallback. |
| `core/base/logger.cpp:134` | CRLF. |
| `apps/service/service_host.cpp:57–152` | Kompozice runtime: cesty, ACL, config, crypto, transport, clock, tunel, netconfig, engine, IPC. |
| `apps/service/service_host.cpp:121–144` | Klasifikátor executorů. |
| `apps/service/service_host.cpp:298–352` | SCM registrace: LocalSystem, auto start, recovery 5/15/60 s. |
| `apps/service/startup_connect.cpp:5–31` | Auto-connect po startu. Přenositelná logika. |
| `platform/windows/ipc_pipe.{h,cpp}` | 32 slotů, deadline I/O 5 s, tick 250 ms, executory `shortControl` 16, `file` 4, `diagnostic` 1, odpověď busy, drain při shutdownu. |
| `apps/cli/main.cpp:120–175` | `wmain`, příkazy `status`, `connect`, `disconnect`, `info`, `get-config`, `set-config F`, `diagnostics`. Exit kódy 0/1/2. |
| `platform/windows/config_files_win.cpp:152–421` | Journal publikace `PHPVPN-CONFIG-1`, stage, hard link `config.previous`, rename, `CONFIRMED`, recovery. |
| `platform/windows/transport_winhttp.cpp:121–383` | No-proxy, bez redirectu, TLS 1.2+, trust-any, deadline, cancel, účtování bajtů, zachytávání hlaviček, limit těla. |
| `apps/tray/*` | Přenositelné modely a layouty jsou v `menu_model.h`, `status_model`, `progress_model`, `reconnect_controller.h`, `statistics_model.h`, `ui_style.h`, `*_layout`, `wizard_text.h`, `editor_document.h`. Okna, readery a `contracts_save_controller` jsou Win32 (`HWND`, `PostMessage`). |
| `client.md:300–309` | Záměrně veřejné IPC bez autentizace, přijaté riziko. |
| `client.md:880–936` | Split tunnel v1: bez výchozí routy, DNS a kill switch. Relay musí zůstat dosažitelný. |
| `client.md:1152–1158` | **Linux:** `/dev/net/tun`, netlink, `CAP_NET_ADMIN`, systemd s minimálními capability, ochrana konfigurace na disku. Veřejný přístup přes IPC zůstává. Nepředpokládat konkrétní firewall ani síťový manažer. Jeden TUN fd. |
| `client.md:1180–1193` | Auto-connect a registrace tray při přihlášení. |
| `tray_UX.md:75` | Přenosné jsou modely, texty a IPC. Okna a tray integraci implementovat nativně. Windows etapa nezaváděla Qt, Electron ani WebView. |
| `distribution.md:88–92, 94–120` | `dist/client/Linux` zatím s README. Nástroje `tools/make-distribution.ps1` a `tools/verify-distribution.ps1`. |
| `assets/*.ico` | Rámce 16, 32 a 128 px, žádné SVG stavových ikon. |

### Zjištěné nesoulady a mezery (pro kontext, ne automaticky k opravě)

- `client.md:904` požaduje název adaptéru `phpvpn`, kód používá `PHPVPN`. Linux použije přesně `phpvpn`. Windows hodnota se v této realizaci nemění.
- `client.md:914` požaduje ověření dosažitelnosti relay mimo tunel. `isHostReachableOutsideInterface` nemá produkčního volajícího. Linux metodu implementuje. Zapojení do core by měnilo Windows chování, proto je až v oddílu 10 jako samostatné rozhodnutí.
- Journal vlastnictví nemá stavy `planned/created/removed` podle `client.md:916`. Linux přebírá chování core beze změny.
- `transport.h:4–6` slibuje streaming sink, WinHTTP přesto tělo bufferuje. Linux zachová skutečné chování: bufferuje s limitem.

## 3. Cílové platformy a toolchain

| Oblast | Rozhodnutí |
|---|---|
| Distribuce | Fedora Workstation aktuální podporovaná verze, podle verze VM zjištěné ve WP00, a starší ještě podporovaná. Ubuntu 24.04 LTS a 26.04 LTS. Architektura x86_64. |
| Kompilátor | GCC ≥ 13 (Ubuntu 24.04). Druhý průchod Clang ≥ 17 kvůli přenositelnosti. C++20 bez rozšíření. |
| Build | CMake ≥ 3.28 a Ninja. Ubuntu 24.04 má CMake 3.28. |
| Knihovny služby a CLI | OpenSSL 3 (`libcrypto`), libcurl ≥ 8.5 s OpenSSL, libsystemd ≥ 255 (sd-bus, sd-daemon). Nic grafického. |
| Knihovny tray | GTK 3.24 a libayatana-appindicator3. Obojí jsou distribuční balíčky obou cílů. |
| Balení | Nativní RPM (`.spec`) a DEB (`debian/`), dva balíčky: `phpvpn` a `phpvpn-tray`. |
| Build prostředí | Fedora nativně na VM. Ubuntu v kontejneru `ubuntu:24.04` přes podman na VM, případně WSL2 na hostiteli, pokud je dostupné. |

## 4. Architektonická rozhodnutí

### 4.1 Struktura zdrojů a sestavení

- Nový adresář `client/src/platform/linux/`: `clock_linux.cpp`, `crypto_openssl.cpp`, `transport_curl.cpp`, `tunnel_tun.cpp`, `netconfig_netlink.cpp`, `netlink_codec.{h,cpp}`, `dns_snapshot.{h,cpp}`, `config_files_linux.{h,cpp}`, `paths_linux.{h,cpp}`, `ipc_unix.{h,cpp}`, `fd.h` (RAII deskriptor).
- Sdílené platformní soubory v `client/src/platform/iface/`:
  - `netconfig.cpp` s přesunutou `sameDefaultRouteAndDns`,
  - `ipc_executors.{h,cpp}` s executory, frontami, odpovědí busy a drainem vytaženými z `ipc_pipe.cpp`,
  - `ipc_server.h` se sdíleným typem voleb a klasifikací.
- `client/CMakeLists.txt`:
  - Odstranit `FATAL_ERROR`, podporovat `WIN32` a `CMAKE_SYSTEM_NAME STREQUAL "Linux"`, jinak chyba.
  - MSVC volby beze změny pod `if(MSVC)`. Windows definice (`UNICODE`, `NOMINMAX`, `_WIN32_WINNT`…) pod `if(WIN32)`.
  - GCC a Clang: `-Wall -Wextra -Wpedantic -Werror` (Werror jen v presetech s testy), `-fstack-protector-strong`, `-D_FORTIFY_SOURCE=3` pro optimalizované buildy, `-fPIE -pie`, `-fcf-protection`, `-Wl,-z,relro,-z,now,-z,noexecstack`.
  - Volitelně `PHPVPN_ENABLE_ASAN` přes `-fsanitize=address,undefined`.
- `client/src/CMakeLists.txt`: výběr zdrojů `phpvpn-platform` podle platformy. Na Linuxu `find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto)`, `find_package(CURL 8.5 REQUIRED)` a `pkg_check_modules(SYSTEMD REQUIRED libsystemd)`.
- `client/apps/CMakeLists.txt`:
  - Windows resource, `package_gui` a kopie Wintun pod `if(WIN32)`.
  - Na Linuxu: `phpvpn-service`, `phpvpn` (CLI) a `phpvpn-tray` z `apps/tray-linux`.
  - Tray lze vypnout přes `PHPVPN_BUILD_TRAY=OFF` pro headless build bez GTK hlaviček.
  - `install()` pravidla přes `GNUInstallDirs`: služba do `${CMAKE_INSTALL_LIBEXECDIR}/phpvpn/`, CLI a tray do `bin`.
- `client/CMakePresets.json`:
  - Stávající `base` dostane `condition` pro Windows. Windows presety zůstávají funkčně stejné.
  - Nové skryté `linux-base` (podmínka Linux, GCC, `binaryDir` `${sourceDir}/../work/build/${presetName}`).
  - Presety `linux-debug`, `linux-release`, `linux-asan`, `linux-clang` a `linux-headless` (bez tray).

### 4.2 Úpravy sdíleného jádra (musí zachovat Windows chování)

1. Nový `core/base/utf8.{h,cpp}`: `toUtf8(std::wstring_view)` a `fromUtf8(std::string_view)`, správně pro 16bitový i 32bitový `wchar_t`, se striktním odmítnutím neplatných sekvencí. Použije se v `tunnel_session.cpp:17–26` místo lokálního `narrow` a na Linuxu v CLI a tray. Stávající Windows převody jinde zůstávají.
2. `http_diagnostic.cpp:157–178`: generování `DiagnosticDisplay` přes `if constexpr (sizeof(wchar_t) == 2)`. Na Linuxu přímé kódové body. Testy pro obě šířky, kde to jde, přes `char32_t` variantu.
3. `secure_buffer.cpp`: na Linuxu `explicit_bzero`.
4. `logger.cpp:134`: konec řádku `\r\n` na Windows a `\n` jinde. Přidat sink na stderr s prefixem priority sd-daemon (`<3>` až `<7>`) pro journald. Windows soubory se nemění.
5. `wizard_ipc.cpp:26–50`: `absoluteConfigPath` podle platformy. Linux přijme jen absolutní normalizovanou POSIX cestu bez NUL, `.`, `..` a zdvojených lomítek. Windows větev se nemění.
6. `ipc_protocol.h:28–29`: jméno pipe pod `#if defined(_WIN32)`. Linux dostane `kIpcSocketPath = "/run/phpvpn/control-v1.sock"`. Verze smlouvy zůstává 1.
7. Wire hodnota `"winhttp"` v Wizard IPC zůstává beze změny, protože IPC v1 je pevná smlouva. Význam: hlavičky zachytil platformní transport. Linux curl transport hlásí stejný zdroj. Dokumentovat v `client.md`.
8. Chybějící include (`<functional>`, `<optional>`, `<chrono>`) ve `vpn_engine.h/.cpp` a `transport.h`, podle výsledku kompilace GCC a Clang.
9. `NetworkSnapshot::sameDefaultRouteAndDns` přesunout do `platform/iface/netconfig.cpp`. Windows ji linkuje odtud.
10. Trvanlivost journalu vlastnictví: v `tunnel_session.cpp` nahradit prostý zápis a rename platformním helperem `durableReplaceFile(path, bytes)` v `platform/iface/durable_file.h`. Linux: `O_CREAT|O_EXCL|O_NOFOLLOW` 0600, `fsync` souboru, `rename`, `fsync` adresáře. Windows implementace zachová dnešní sémantiku (`std::filesystem` rename) a volitelně přidá `FlushFileBuffers`. O tom rozhodne regresní test.
11. `TunnelSessionSettings` (`tunnel_session.h:44–51`): výchozí hodnoty podle platformy.
    - Linux: `adapterName = L"phpvpn"`, `tunnelType = L"phpvpn"`, vlastní pevný GUID instalace pro Linux, `ringCapacity` se ignoruje, `installDirectory` prázdné.
    - Windows hodnoty beze změny.

Rozhraní `NetConfig` a `TunnelDriver` se **nepřejmenovávají**, aby Windows kód zůstal netknutý. Komentáře v `netconfig.h` a `tunnel.h` doplní linuxový význam polí: „LUID“ je platformní identita rozhraní a „alias/GUID“ je jméno a značka vlastnictví.

### 4.3 TUN (`tunnel_tun.cpp`)

**`loadAndVerify`**
- Ověří, že `/dev/net/tun` je znakové zařízení 10:200 a lze ho otevřít.
- `TUNGETFEATURES` musí nabízet `IFF_TUN|IFF_NO_PI`.
- Nic nevytváří.

**`createAdapter(name, type, guid)`**
- `open("/dev/net/tun", O_RDWR|O_CLOEXEC|O_NONBLOCK)`, `TUNSETIFF` s přesným jménem `phpvpn` a `IFF_TUN|IFF_NO_PI`, bez `IFF_MULTI_QUEUE` a bez `TUNSETPERSIST`.
- Rozhraní je nepersistentní a jádro ho při pádu procesu samo odstraní i s adresami a routami. To je hlavní bezpečnostní vlastnost linuxové recovery.
- Existuje-li rozhraní stejného jména (`EBUSY`/`EEXIST`), je to kolize a skončí chybou bez zásahu.
- Hned potom přes netlink `RTM_NEWLINK`/`RTM_SETLINK` (`NLM_F_ACK`):
  - `IFLA_IFALIAS = "phpvpn-owner:<GUID>"` jako značka vlastnictví,
  - MTU podle měření ve WP00 (stejná hodnota jako Wintun adaptér na Windows, jinak 1500 s odůvodněním v reportu),
  - `IFLA_INET6_ADDR_GEN_MODE = NONE`, tedy žádné IPv6 adresy na tunelu, bez zápisu do `/proc/sys`,
  - `IFF_UP`. Routa na neaktivní link by skončila `ENETDOWN`.
- Vrací `TunnelAdapterInfo{interfaceLuid = identita (4.4), adapterGuid = guid, created = true}`.

**`openAdapter`**
- Vrací `not_supported`. Core ho nevolá.

**`startSession(ring)`**
- Vytvoří `Tunnel` nad fd a `eventfd` pro stop. Kapacita ringu nemá na Linuxu obdobu a zaloguje se jednou na debug úrovni.

**`Tunnel::receive(buffer, timeoutMs)`**
- `poll` na fd a eventfd s timeoutem.
- `readv` do bufferu volajícího plus jednobajtový přetokový iovec. Paket delší než buffer se započítá do `oversizedReceiveCount` a vrátí se `empty`, stejně jako na Windows.
- `EAGAIN` znamená `empty`, stop znamená `stopped`.

**`Tunnel::send`**
- `write`. Při `EAGAIN` omezené přerušitelné čekání `poll(POLLOUT|eventfd)` stejně jako na Windows (200 × 5 ms), pak `busy`. Žádný busy loop.

**`signalStop`**
- Atomický flag a zápis do eventfd.

**`endSession` a `closeAdapter`**
- Uzavření fd. Tím jádro rozhraní odstraní.
- `closeAdapter` ověří zmizení (netlink) s krátkým omezeným čekáním.

**`runningDriverVersion`**
- Vrací 0. `--verify-tunnel` na Linuxu vypíše nabízené TUN features a verzi jádra.

### 4.4 Síťová konfigurace (`netconfig_netlink.cpp`)

**Transport**
- Vlastní malý rtnetlink klient bez libmnl. Socket `NETLINK_ROUTE` s `SOCK_CLOEXEC`.
- Sekvenční čísla, `NLM_F_ACK`, kontrola `nlmsgerr`, omezený timeout přes `SO_RCVTIMEO`, dump s `NLM_F_DUMP_INTR` znamená opakování, nejvýše tři pokusy.
- Kódování a dekódování zpráv je v čistém `netlink_codec`, unit testovatelném bez privilegií.

**Identita rozhraní** (pole `interfaceLuid`, `uint64`)
- `(uint64(ownerTag) << 32) | ifindex`, kde `ownerTag` jsou první 4 bajty SHA-256 z GUID instalace.
- Snapshot přiřadí tag jen rozhraní, jehož `IFLA_IFALIAS` přesně odpovídá značce `phpvpn-owner:<GUID>`. Cizí rozhraní mají tag 0.
- Po recyklaci ifindexu cizím rozhraním tedy stará identita neexistuje a nemůže vzniknout falešná shoda.
- Adresy a routy se nastavují podle dolních 32 bitů. Před každou změnou se ověří, že rozhraní s daným ifindexem stále nese značku.

**`snapshot()`**
- IPv4 adresy (`RTM_GETADDR`) ze všech rozhraní.
- IPv4 unicast routy ze všech tabulek kromě `local` a `broadcast`, s identitou rozhraní, metrikou (`RTA_PRIORITY`) a next hopem.
- Výchozí routa: `0.0.0.0/0` s nejnižší metrikou v tabulce `main`.
- DNS: `dns_snapshot`, viz dále.

**`dns_snapshot`**
- Je-li na systémové sběrnici `org.freedesktop.resolve1`, přečte přes sd-bus vlastnost `Manager.DNS` (`a(iiay)`), IPv4 servery a link.
- Vždy navíc `nameserver` řádky z `/etc/resolv.conf` (omezené čtení, bez následování neočekávaných cest).
- Seřazený deduplikovaný seznam textových IPv4. Nedostupnost resolved není chyba, protože `client.md:1156` nepředpokládá konkrétní síťový manažer. Nečitelný `resolv.conf` je chyba snapshotu, fail closed.

**`interfaceExists(id)`**
- Ifindex existuje a nese značku odpovídající tagu. Chyba dotazu zůstane chybou, ne `false`.

**`interfaceLuidForAlias(alias)`**
- `RTM_GETLINK` podle jména, identita podle značky.

**`add/removeUnicastAddress`**
- `RTM_NEWADDR` s `IFA_LOCAL`, `IFA_ADDRESS`, prefixem a `IFA_F_NOPREFIXROUTE`. Bez tohoto flagu by jádro přidalo vlastní prefixovou routu s metrikou 0 a explicitní routa by skončila `EEXIST`.
- `NLM_F_CREATE|NLM_F_EXCL`. „Už existuje“ a „nenalezeno“ se chovají stejně jako na Windows (`netconfig_iphlpapi.cpp:314–343`): shoda znamená úspěch, ale jen při přesné shodě ifindexu, adresy a prefixu.

**`add/removeRoute`**
- `RTM_NEWROUTE` v tabulce `main`, `RTPROT_STATIC`, `RT_SCOPE_LINK` pro next hop 0.0.0.0, jinak `RT_SCOPE_UNIVERSE` s `RTA_GATEWAY`.
- `RTA_OIF` a `RTA_PRIORITY = metrika`, `NLM_F_CREATE|NLM_F_EXCL`. Mazání přesně podle stejných atributů.

**`removeOwnedPhantomInterface(alias, guid, id)`**
- Odstraní (`RTM_DELLINK`) jen rozhraní, které splňuje všechny podmínky:
  - jméno se rovná `alias`,
  - značka se rovná `guid`,
  - typ je tun,
  - je persistentní (`tun_flags` obsahuje `IFF_PERSIST`, z `/sys/class/net/<if>/tun_flags`),
  - nemá připojený fd (`IFF_LOWER_UP` chybí),
  - vlastník je uid 0.
- V jiném případě fail closed. Běžně k této cestě nedojde, protože klient persistentní rozhraní nevytváří.

**`isHostReachableOutsideInterface(host, exclude)`**
- `getaddrinfo` pro IPv4 a `RTM_GETROUTE` s `RTA_DST` pro každou adresu. Pravda, když výstupní rozhraní není `exclude`. Sémantika jako na Windows. Core ji zatím nevolá (oddíl 2).

**Volitelné integrace** (`client.md:1156`)
- Balíček dodá pasivní `/usr/lib/NetworkManager/conf.d/90-phpvpn.conf` s `unmanaged-devices=interface-name:phpvpn`. Bez NetworkManageru je bez účinku.
- Firewall se nekonfiguruje. firewalld zařadí rozhraní do výchozí zóny, dokumentovat.
- systemd-networkd rozhraní nespravuje, pokud ho nezachytí obecný `.network` soubor administrátora, dokumentovat.

### 4.5 Transport (`transport_curl.cpp`)

**Model**
- Jeden `CURL*` easy handle na instanci transportu kvůli znovupoužití spojení, stejně jako WinHTTP session. Každý request běží v privátním `CURLM` s `curl_multi_poll`.
- Zrušení: atomická epocha a `curl_multi_wakeup`. `cancelCurrentRequest` přeruší DNS, connect, TLS i pomalé čtení bez čekání na sekundový progress callback.
- Po zrušení nebo timeoutu se easy handle resetuje (`curl_easy_reset` a znovunastavení voleb), transport zůstává použitelný.
- `curl_global_init` jednou v procesu (`std::call_once`).

**Volby**
- `CURLOPT_PROTOCOLS_STR "http,https"`, `CURLOPT_FOLLOWLOCATION 0`, `CURLOPT_PROXY ""` a `CURLOPT_NOPROXY "*"` (ignoruje i `http_proxy` z prostředí), `CURLOPT_NETRC` off, bez cookie enginu, `CURLOPT_HTTPAUTH` none, `CURLOPT_NOSIGNAL 1`, `CURLOPT_SSLVERSION TLSv1_2` jako minimum.
- `trustAnyTlsCertificate`: `CURLOPT_SSL_VERIFYPEER 0` a `CURLOPT_SSL_VERIFYHOST 0`. Jinak systémové CA.

**Hlavičky**
- Uživatelský blok z `httpHeaders` přes existující core parser, `Content-Type` podle akce a `Content-Length` od curl.
- Potlačit implicitní `Accept:` a `Expect:` (bez 100-continue). User-Agent jen z bloku.
- Přesná sada hlaviček se porovná s WinHTTP zachycením v testu (WP03).

**Deadline**
- Jeden absolutní monotónní deadline. `CURLOPT_TIMEOUT_MS` se nastaví na zbytek a smyčka `multi_poll` hlídá vlastní deadline. Pozdní úspěch po deadline se nevrací.

**Tělo**
- Write callback kopíruje do `response.body` a sčítá `responseBodyBytesRead`, i přes chybu.
- Překročení `maxResponseBodyBytes` vrátí 0 a nastaví flag, výsledkem je `transport_response_too_large`. Nesoulad `Content-Length` (`CURLE_PARTIAL_FILE`) je chyba. `Content-Type` musí být ASCII.

**Diagnostika**
- Jen při `captureWizardHttpDiagnostics`, a to pro `postDiagnostic` a akce mimo `data`.
- Hlavičky požadavku přes `CURLOPT_DEBUGFUNCTION` (`CURLINFO_HEADER_OUT`). Status řádek, hlavičky odpovědi a trailery přes `CURLOPT_HEADERFUNCTION`.
- Stejné limity 64512 + 1024 B, `HTTP/?` při chybějící verzi, zdroj `winhttp` (4.2 bod 7).

**Mapování chyb**
- Resolve na DNS, connect na connect, SSL na TLS, timeout na timeout, abort na cancelled. Kódy chyb shodné s `transport_winhttp.cpp:53–64`.

### 4.6 Kryptografie (`crypto_openssl.cpp`)

- OpenSSL 3 EVP: `EVP_sha256`, HMAC přes `EVP_MAC` "HMAC" s digestem SHA256, `EVP_aes_256_gcm` (klíč 32 B, IV 12 B, tag 16 B, prázdný plaintext jako GMAC), `RAND_bytes`.
- Chyba ověření tagu dá `crypto_tag_mismatch`, ostatní dají obecnou chybu kryptografie.
- Klíčový materiál v `SecureBuffer`, dočasné kontexty se uvolní v RAII. `OPENSSL_cleanse` pro mezibuffery.
- Fedora crypto-policies AES-GCM ani HMAC-SHA256 neomezují. Ověřit test vektory i v režimu `FIPS` jen jako informativní běh.

### 4.7 Hodiny (`clock_linux.cpp`)

- `monotonicMs` je `CLOCK_MONOTONIC`, `leaseMonotonicMs` je `CLOCK_BOOTTIME` (zahrnuje suspend jako `GetTickCount64`) a `systemUtcMs` je `CLOCK_REALTIME`.

### 4.8 Konfigurace, cesty a oprávnění

| Položka | Linux | Windows analogie |
|---|---|---|
| Konfigurace | `/etc/phpvpn/config.json`, adresář `0700 root:root`, soubor `0600 root:root` | `%ProgramData%\PHPVPN\config.json` |
| Publikační soubory | `config.publish-journal`, `config.previous`, `config.stage.<uuid>.tmp` ve stejném adresáři, `0600` | stejné názvy |
| Journal vlastnictví | `/var/lib/phpvpn/ownership.json`, adresář `0700` | `ownership.json` |
| Runtime | `/run/phpvpn/` `0755 root:root`: `control-v1.sock` `0666`, `service.lock` `0600` | named pipe |
| Logy | journald (`journalctl -u phpvpn`) | `logs\phpvpn-service.log` |
| Binárky | `/usr/libexec/phpvpn/phpvpn-service`, `/usr/bin/phpvpn`, `/usr/bin/phpvpn-tray` | `%ProgramFiles%\phpvpn\<ver>` |

**Oprávnění k datům**
- `client.md:1154` výslovně žádá ochranu konfiguračního souboru před přímým přístupem přes souborový systém. Linuxovou zvyklostí pro soubor s tajemstvími je `0600 root`, podobně jako u NetworkManageru.
- Windows oproti tomu dává Users čtení. Rozdíl se dokumentuje. Uživatel získá konfiguraci přes `phpvpn get-config` a Edit Config, protože IPC zůstává veřejné.

**`config_files_linux.cpp`**
- Stejná sémantika rozhraní `ConfigFiles` jako `config_files_win.cpp`, stejný formát journalu `PHPVPN-CONFIG-1`.
- Adresář drží otevřený `O_DIRECTORY|O_NOFOLLOW` a všechny operace dělá přes `*at`.
- Stage soubor: `openat(O_CREAT|O_EXCL|O_NOFOLLOW, 0600)`, `fchmod` a ověření vlastníka proběhnou před zápisem tajemství. Pak zápis, `fsync`, znovuotevření read-only a ověření identity a SHA-256.
- Záloha přes `linkat` na `config.previous`.
- `renameat` pro replace, `renameat2(RENAME_NOREPLACE)` pro `createIfMissing`. `fsync` adresáře, ověření publikované identity (`st_dev`, `st_ino`), `CONFIRMED` a `fsync`.
- `recover()`: stejné stavy `clean/confirmed/notPublished/unknown`.

**Kontroly při otevření**
- Regulární soubor, vlastník uid 0, bez zápisu pro skupinu a ostatní, `nlink == 1`.
- Předci (`/`, `/etc`) patří rootovi a nejsou zapisovatelní ostatními.
- Odmítnout síťové a FUSE souborové systémy (`statfs`: NFS, CIFS/SMB2, FUSE). Obdoba požadavku na lokální NTFS.
- Testy dostanou injektovatelnou politiku očekávaného vlastníka (`geteuid()`), produkční kód vždy uid 0.

**`paths_linux.cpp`**
- Pevné produkční cesty. Při startu služba ověří a opraví režimy vlastních adresářů patřících rootovi (obdoba opravy ACL).
- Cizí vlastník nebo symlink znamená fail closed s diagnostikou.
- Testovací přepínače služby (`--config-dir`, `--state-dir`, `--runtime-dir`, `--adapter-name`) slouží jen izolovaným E2E instancím. Produkční unit je nepoužívá.

### 4.9 IPC (`ipc_unix.cpp`)

**Model přístupu**
- Beze změny oproti `client.md:306, 978`: každý lokální proces smí socket použít. Socket je `0666` v adresáři `0755 root`. Server nerozhoduje podle identity klienta.
- Ochrana proti squattingu jako náhrada `FILE_FLAG_FIRST_PIPE_INSTANCE`:
  - adresář `/run/phpvpn` patří rootovi a nikdo jiný do něj nezapisuje,
  - služba drží `flock` na `service.lock` a odstraní starý socket jen tehdy, je-li to socket patřící rootovi v tomto adresáři.
- Ochrana klienta proti podvržené službě: klient přes `SO_PEERCRED` ověří, že protistrana má uid 0. Teprve potom odešle požadavek, například `set-config` s tajemstvími. To je obdoba `PIPE_REJECT_REMOTE_CLIENTS`.
- Alternativa „skupina `phpvpn` a socket `0660`“ je linuxově obvyklejší, ale odporuje normativnímu `client.md`. Je uvedena v oddílu 10 k rozhodnutí. Výchozí realizace drží `client.md`.

**Rámování**
- `SOCK_STREAM` s rámcem `u32 big-endian délka` a UTF-8 JSON. Jedna zpráva v každém směru a jedno spojení na požadavek.
- Explicitní délka odliší úplnou odpověď od uříznuté, což je důležité pro klasifikaci uložení jako confirmed nebo unknown.
- `SOCK_SEQPACKET` se nepoužije: zprávy do 4 MiB přesahují výchozí socket buffery.

**Server**
- `epoll`, `timerfd` s tickem 250 ms, nejvýše 32 slotů, deadline čtení i zápisu 5 s, limit 4 MiB, `SOCK_CLOEXEC|SOCK_NONBLOCK`, `accept4`.
- Po zápisu odpovědi `shutdown(SHUT_WR)` a čekání na zavření klientem s deadline, jako na Windows.

**Executory**
- Sdílené `platform/iface/ipc_executors` vytažené z `ipc_pipe.cpp`: `shortControl` 16, `file` 4, `diagnostic` 1, korelovaná odpověď busy a drain při shutdownu.
- Windows pipe server se přepne na sdílenou třídu. Regrese se ověří existujícími testy IPC (WP05).

**Klient**
- `platform/linux/ipc_unix.h: IpcClient::exchange(request, timeout, cancel)` s `poll`, deadline a rozlišením chyb: služba neběží (`ENOENT`, `ECONNREFUSED`), přístup odepřen (`EACCES`), timeout, uříznutá odpověď, neplatná protistrana.

### 4.10 Služba a systemd

**Refaktor `apps/service`**
- `service_runtime.{h,cpp}` je přenositelná kompozice z `service_host.cpp:57–180`: engine, `ConfigStore`, `StartupConnect`, `WizardService`, dispatcher, klasifikátor a IPC server. Platformní části přicházejí přes malou strukturu továren (`PlatformHost`: cesty, příprava úložiště, `ConfigFiles`, IPC server, log sink).
- `service_host_win.cpp` obsahuje SCM, konzoli, install a uninstall. Chování beze změny, jen přesun.
- `service_main_linux.cpp` je linuxový vstup.

**Linuxový vstup (`service_main_linux.cpp`)**
- `--systemd` (výchozí, pokud je nastaveno `NOTIFY_SOCKET`): `sd_notify("READY=1")` po startu IPC, `STOPPING=1` při ukončení a `STATUS=` se stavem VPN.
- `--foreground` a alias `--console`: log na stdout, ukončení přes Ctrl+C.
- `--verify-tunnel`.
- Signály přes `signalfd` (SIGTERM, SIGINT; SIGHUP ignorovat). Při stopu korektní teardown ve stejném pořadí jako Windows `stop()`.
- Singleton přes `flock` na `/run/phpvpn/service.lock`.
- `--install` a `--uninstall` na Linuxu neexistují. Registraci dělá balíček, jako u Windows „instalace není operací IPC“.

**Auto-connect**
- Stejná logika `StartupConnect`, spuštěná po `READY=1` (obdoba SCM RUNNING).

**Unit `phpvpn.service`** (`client/packaging/linux/systemd/`)

```ini
[Unit]
Description=phpvpn client service
Documentation=man:phpvpn-service(8)
Wants=network-online.target
After=network-online.target

[Service]
Type=notify
ExecStart=/usr/libexec/phpvpn/phpvpn-service --systemd
Restart=on-failure
RestartSec=5s
RestartSteps=2
RestartMaxDelaySec=60s
TimeoutStopSec=45s
User=root
CapabilityBoundingSet=CAP_NET_ADMIN
NoNewPrivileges=yes
UMask=0077
ConfigurationDirectory=phpvpn
ConfigurationDirectoryMode=0700
StateDirectory=phpvpn
StateDirectoryMode=0700
RuntimeDirectory=phpvpn
RuntimeDirectoryMode=0755
ProtectSystem=strict
ReadWritePaths=/etc/phpvpn
ProtectHome=yes
PrivateTmp=yes
DevicePolicy=closed
DeviceAllow=/dev/net/tun rw
ProtectKernelTunables=yes
ProtectKernelModules=yes
ProtectKernelLogs=yes
ProtectControlGroups=yes
ProtectClock=yes
ProtectHostname=yes
RestrictNamespaces=yes
RestrictRealtime=yes
RestrictSUIDSGID=yes
LockPersonality=yes
MemoryDenyWriteExecute=yes
SystemCallArchitectures=native
SystemCallFilter=@system-service
RestrictAddressFamilies=AF_UNIX AF_INET AF_INET6 AF_NETLINK

[Install]
WantedBy=multi-user.target
```

- `RestartSteps` a `RestartMaxDelaySec` vyžadují systemd ≥ 254 a oba cíle je splňují. Posloupnost 5 s, přibližně 17 s a 60 s odpovídá Windows 5/15/60 s.
- `IFLA_INET6_ADDR_GEN_MODE` přes netlink umožňuje `ProtectKernelTunables=yes`.
- Root bez `CAP_DAC_OVERRIDE` pracuje jen s vlastními soubory. Cizí vlastník znamená fail closed.
- Ověřit `systemd-analyze security phpvpn.service`, cílové skóre ≤ 3.0. Ověřit chod v SELinux enforcing na Fedoře (`ausearch -m avc` bez záznamů pro phpvpn) a bez vlastní SELinux politiky, služba běží jako `unconfined_service_t`. Na Ubuntu bez AppArmor profilu.
- Man stránky: `phpvpn(1)`, `phpvpn-tray(1)`, `phpvpn-service(8)`, krátké, v `client/packaging/linux/man/`.

### 4.11 CLI

- `apps/cli/main.cpp` bude přenositelný:
  - Windows `wmain` s dnešním převodem argv.
  - Linux `main` s UTF-8 argv.
  - Čtení souboru `set-config` přes `std::ifstream` v binárním režimu, s limitem 512 KiB + 1. Opraví i Windows chybu s ne-ASCII cestou přes `fopen_s` (drobná, testem pokrytá oprava). Rozhodnutí je v oddílu 10.
  - IPC klient přes malé rozhraní `IpcClientTransport` (Windows pipe nebo Linux socket).
- Příkazy, výstupy (pretty JSON a raw text), stderr formát `code: description`, exit kódy 0/1/2 a timeout 10 s zůstávají **beze změny**.
- Linux přidá jen srozumitelné diagnostické texty pro stavy služba neběží nebo socket nedostupný. Například `service_not_running: phpvpn service is not running (systemctl status phpvpn)`. Kódy chyb zůstávají stejné.
- Žádné root oprávnění a žádná GUI závislost.

### 4.12 Tray (`client/apps/tray-linux/`)

**Toolkit**
- GTK 3 a libayatana-appindicator3. Oba jsou nativní systémové komponenty cílových desktopů (GNOME), ne přibalený framework.
- Qt, Electron ani WebView se nepoužijí, v souladu s `tray_UX.md:75`.
- GTK 4 nemá tray API a ayatana vyžaduje GtkMenu z GTK 3.
- Závislost se schvaluje v oddílu 10. Náhradní varianta bez ayatana je vlastní StatusNotifierItem a dbusmenu přes GDBus.

**Sdílení s Windows** (Windows tray se funkčně nemění)
- Znovu použít přenositelné modely a texty: `menu_model.h`, `status_model`, `progress_model`, `progress_details`, `reconnect_controller.h`, `poll_schedule.h`, `statistics_model.h`, `ui_style.h` (barvy a texty), `wizard_text.h`, `editor_document.h` a core `presentation/*`, `wizard/wizard_model`.
- Čisté funkce vytáhnout z Win32 hlaviček do přenositelných:
  - `classifySaveResponse` a `SaveOperation` ze `status_reader.h` do `apps/tray/save_response.h`,
  - stavový automat ukládání kontraktů z `contracts_save_controller` do `apps/tray/contracts_save_machine.h` bez `HWND`,
  - timeouty IPC (status 400 ms, příkazy 30 s, Wizard 5 s, close 1 s, reset editoru 5 s, polling 5000/500 ms) do `apps/tray/ipc_timeouts.h`,
  - `tray_arguments.h` dostane přetížení pro UTF-8 argv.
- Windows soubory potom jen includují nové hlavičky. Ověří se stávajícími unit a UI testy.

**Linuxové soubory**

| Soubor | Obsah |
|---|---|
| `main.cpp` | Vstup. |
| `tray_app.{h,cpp}` | `GtkApplication`, jedna instance na relaci s `G_APPLICATION_HANDLES_COMMAND_LINE`. Druhé spuštění s `--installation-complete` předá požadavek primární instanci. |
| `indicator.{h,cpp}` | AppIndicator: tři stavové ikony, menu podle `menu_model.h`, detekce `org.kde.StatusNotifierWatcher`. |
| `ipc_async.{h,cpp}` | Pracovní vlákno pro IPC s deadliny a zrušením. Výsledky do UI přes `g_main_context_invoke`. Brána „zápis odeslán“ pro klasifikaci uložení. |
| `progress_window`, `wizard_window`, `http_headers_window`, `http_error_detail_window`, `editor_window`, `contracts_window`, `password_sha_window`, `server_info_window`, `statistics_window` | Okna. |
| `dialogs.{h,cpp}` | Yes/No a chybové dialogy se stejnými texty a výchozí volbou No. |
| `control_window.{h,cpp}` | Záložní ovládací okno. |
| `notifications.{h,cpp}` | `org.freedesktop.Notifications` přes GDBus pro installation notice. Implementuje `InformationNoticePresenter`. |
| `style.{h,cpp}` | `GtkCssProvider` z barev `ui_style.h` a kreslení štítů přes cairo. |

**Chování shodné s Windows**
- Menu: pořadí, texty a přepínání Connect/Disconnect.
- Ikony: disconnected, connecting, connected podle `status_model` a pravidel `tray_UX.md:426+`.
- Polling 5 s a 500 ms v progress okně.
- Progress okno se čtyřmi kontrolami a odpočtem 3 s.
- Wizard: kroky, keepalive a close.
- Edit Config, Contracts, Server Info s automatickým obnovením 5 s, Statistics, Password SHA, HTTP headers a HTTP error detail.
- Exit s kontrolou neuložených změn.
- Konec relace: stejně jako Windows `WM_QUERYENDSESSION → TRUE` a `WM_ENDSESSION → endSession` (`tray_application.cpp:202–203`). Tray odhlášení neblokuje. Signál `GApplication` shutdown nebo `query-end` spustí stejný úklid.
- Tray nevlastní VPN. Ukončení tray VPN neodpojí.

**Linuxové rozdíly** (dokumentovat v `tray_UX.md`, nový oddíl „Linux“)
- AppIndicator nemá tooltip. Stavový text z `ui_style.h:11–16` se zobrazí jako neaktivní první položka menu a oddělovač nad pořadím z UX04, plus jako `app_indicator_set_title`. Je to jediná odchylka od menu UX04.
- Geometrie: GTK layout kontejnery s minimálními velikostmi z DIP konstant `ui_style.h`. Škálování řeší GTK. Pixelová shoda s Win32 layoutem se nepožaduje, stejný je obsah, pořadí, texty, barvy a chování.
- Wayland neumožňuje aplikaci pozicovat okna, umístění určí kompozitor.
- Fedora Workstation (vanilla GNOME) zobrazuje SNI jen s rozšířením `gnome-shell-extension-appindicator`. Ubuntu ho má zapnuté (`ubuntu-appindicators`).
  - RPM `phpvpn-tray` ho dá do `Recommends`, ale nezapíná ho. Změna uživatelských desktopových nastavení patří uživateli.
  - Když watcher chybí, tray otevře záložní ovládací okno `control_window` se stavem, štítem a tlačítky ve stejném pořadí jako menu, a jednou zobrazí notifikaci s návodem (`gnome-extensions enable appindicatorsupport@rgcjonas.gmail.com`, nové přihlášení).
  - Když se watcher objeví později, indikátor se zaregistruje sám.
  - Tím podmínka „funkční rozdíl nesmí znemožnit běžné ovládání“ platí i bez rozšíření.
- Spouštěč v menu aplikací `phpvpn-tray.desktop` spouští `phpvpn-tray --show-window`: aktivuje běžící instanci a ukáže ovládací okno. Autostart je bez argumentů jako na Windows.

**Autostart**
- `/etc/xdg/autostart/phpvpn-tray.desktop` z balíčku `phpvpn-tray` je obdoba HKLM Run: pro všechny uživatele a vypnutelný v nastavení desktopu (`Hidden=true` v `~/.config/autostart`).

**Ikony**
- Nový reprodukovatelný generátor `client/tools/make_linux_icons.py` (Pillow, jen vývojový nástroj) vytvoří z `assets/{default,disconnected,connecting,connected}.ico` PNG v 16, 22, 24, 32, 48, 64 a 128 px.
- Výstup se commitne do `client/assets/linux/hicolor/...` a kontrolní režim `--check` ověří shodu.
- Instalace do `/usr/share/icons/hicolor/<n>x<n>/apps/`: `phpvpn.png`, `phpvpn-disconnected.png`, `phpvpn-connecting.png`, `phpvpn-connected.png`.
- Licence Material Symbols do `/usr/share/licenses/phpvpn-tray/`.

**ID aplikace**
- Jediná konstanta, výchozí `org.phpvpn.Tray`, k potvrzení v oddílu 10.

### 4.13 Balení, instalace a distribuce

**Balíčky** (`client/packaging/linux/`)

| Balíček | Obsah | Závislosti |
|---|---|---|
| `phpvpn` | služba, CLI, `phpvpn.service`, NetworkManager snippet, man stránky, README | automatické sonames (libcrypto, libcurl, libsystemd), **žádné GUI** |
| `phpvpn-tray` | tray, `.desktop`, autostart, ikony, licence ikon | `phpvpn = %{version}`, GTK 3, ayatana. Fedora `Recommends: gnome-shell-extension-appindicator`. Ubuntu `Recommends: gnome-shell-extension-appindicator` |

- RPM: `rpm/phpvpn.spec` s makry `%systemd_post/preun/postun_with_restart`, `%check` spouští ctest bez privilegií.
- DEB: `debian/` (debhelper-compat 13, `dh_installsystemd --restart-after-upgrade`, `dh_shlibdeps`).
- Build na nejstarší podporované verzi každé distribuce (Ubuntu 24.04, starší podporovaná Fedora). Názvy `phpvpn-<ver>-1.x86_64.rpm` a `phpvpn_<ver>-1_amd64.deb`.
- Odinstalace ponechá `/etc/phpvpn` a `/var/lib/phpvpn`. `apt purge` je odstraní. RPM je nevlastní, `uninstall.sh --remove-data` je odstraní explicitně. Obdoba Windows `-RemoveData`.

**Povolení služby**
- Samotné balíčky se chovají podle distribuční zvyklosti. Debian služby po instalaci povoluje a spouští. Fedora je nechává vypnuté přes presety.
- Pro paritu s Windows instalátorem, kde služba po instalaci běží, udělá `install.sh` explicitně `systemctl enable --now phpvpn.service` a počká na socket.

**Instalační vstupy v distribuci** (`dist/client/Linux/x64/`)
- `install.sh` (POSIX sh, LF), pořadí fail closed:
  1. root (nebo sudo),
  2. detekce distribuce z `/etc/os-release` (Fedora, Ubuntu), jinak chyba,
  3. ověření podpisu manifestu (`openssl dgst -sha256 -verify` s veřejným klíčem z `release-signing.cer` a pinovaným SHA-256 SPKI v `install.sh`, stejná úroveň důvěry jako na Windows),
  4. SHA-256 balíčků,
  5. `dnf install ./phpvpn-*.rpm` nebo `apt-get install ./phpvpn_*.deb` (závislosti z distribučních repozitářů),
  6. volba `--headless` vynechá `phpvpn-tray`, na systému bez grafického cíle se to zvolí automaticky,
  7. `systemctl enable --now`,
  8. čekání na `control-v1.sock` (nejvýše 30 s),
  9. `phpvpn status`,
  10. `--verify-only` jako na Windows.
- Běžící tray instalace neukončuje. Nová verze se použije po dalším přihlášení nebo ručním restartu tray, IPC v1 zůstává kompatibilní.
- `uninstall.sh [--remove-data]`: bezpečný `phpvpn disconnect`, stop a disable, odebrání balíčků, volitelně odstranění dat.
- `README.md`: instalace, headless a desktop, povolení AppIndicator na Fedoře, přidání konfigurace přes tray Wizard nebo `phpvpn set-config`, diagnostika (`phpvpn diagnostics`, `journalctl -u phpvpn`) a oddíl „Rozdíly oproti Windows“.
- Dále balíčky, `release-manifest.json`, `.sig`, `release-signing.cer`, `LICENSE-material-symbols.txt` a `NOTICE-material-symbols.txt`.

**Sestavovací nástroje** (`client/tools/linux/`)
- `build.sh <preset>` spustí configure, build a ctest.
- `package-rpm.sh` a `package-deb.sh`.
- `build-packages-in-container.sh fedora:<N>|ubuntu:24.04` (podman nebo docker): zdrojový snapshot jen pro čtení. Výstup `work/linux-packages/<id>/` obsahuje balíčky, `SHA256SUMS` a `build-provenance.json` (distribuce, kompilátor, verze knihoven, commit a dirty stav).
- `client/tools/linux/sync-to-vm.py` je vývojový helper, **ne součást distribuce**. Přenese zdrojový snapshot (tracked a záměrně modifikované soubory) na VM přes SSH (paramiko, heslo jen z proměnné prostředí) a stáhne artefakty.
- Při prvním použití nainstaluje dočasný klíč pro přihlášení, uložený v `work/` mimo git, a pak pracuje už jen s klíčem.

**Celková distribuce** (Windows host, `distribution.md`)
- `tools/make-distribution.ps1` dostane povinný `-LinuxPackagesDirectory <work/linux-packages/<id>>`.
  - Ověří `SHA256SUMS` a provenance: commit se musí shodovat s aktuálním checkoutem, jinak odmítne s výjimkou explicitního `-AllowLinuxProvenanceMismatch` pro vývoj.
  - Zkopíruje balíčky a skripty do stagingu `client/Linux/x64`, vytvoří manifest a podepíše ho stejným mechanismem jako Windows.
- Sdílenou manifest a podpis logiku vytáhnout do `tools/distribution-common.ps1`, pokud ji dnes drží jen `client/tools/make-dist.ps1`.
- `tools/verify-distribution.ps1`: přesný seznam souborů `client/Linux/x64` a kontrola manifestu. README placeholder pro Linux se ruší, pro macOS zůstává.
- Zachovat executable bit u `install.sh` a `uninstall.sh` v ZIP. Návod uvede i `sh install.sh`.

### 4.14 Logování a diagnostika

- Služba loguje přes stderr sink s prefixy priority do journald. Obsahová pravidla `client.md:310–318` (žádná tajemství, tagy, těla ani interní cesty) platí beze změny.
- Rotaci řeší journald. Souborový log a rotace 8 MiB/5 se na Linuxu nepoužijí, což je dokumentovaný rozdíl.
- `phpvpn diagnostics` a `status` jsou shodné s Windows, protože jsou z IPC.

## 5. Pracovní balíky

Každý WP má vlastní commit (nebo několik malých) ve větvi `linux`. Commit vzniká až po splnění kritéria dokončení. Zprávy commitů jsou věcné. Neprovedený test se v reportu značí `NOT RUN` s důvodem.

### WP00: Příprava, baseline a přístup k prostředím

1. Ověřit větev `linux`, čistý strom a `graft map`, případně `graft build`.
2. Windows baseline: `client/tools/build-x64.ps1 -Preset release -Configure -ResourceTests`, ctest (release) a `verify-package`. Výsledky uložit do `work/linux-plan/baseline-windows.md` jako referenci pro regresi.
3. Změřit MTU Wintun adaptéru na Windows (`netsh interface ipv4 show subinterfaces` při připojeném klientovi nebo z existujícího E2E reportu) pro rozhodnutí 4.3.
4. Fedora VM:
   - SSH přístup přes `sync-to-vm.py` (heslo jen z env), `cat /etc/os-release`, `uname -r`, `systemctl --version`, `getenforce`, `gnome-shell --version`, přítomnost appindicator rozšíření, `podman --version`, síťová dostupnost hostitele.
   - Instalace build závislostí: `gcc-c++ clang cmake ninja-build openssl-devel libcurl-devel systemd-devel gtk3-devel libayatana-appindicator-gtk3-devel rpm-build python3 php-cli uv`.
   - Pro Ubuntu kontejner: `build-essential cmake ninja-build libssl-dev libcurl4-openssl-dev libsystemd-dev libgtk-3-dev libayatana-appindicator3-dev debhelper`.
5. Relay server pro testy: Python server z `dist/server/Python` (init/start skripty) na VM v samostatném network namespace, nebo na hostiteli dosažitelném z VM. Profil `wizard-default` a integration klíče podle `tools/deploy-test.ps1`. Rozhodnutí zapsat.

**Hotovo, když:** baseline je uložená, VM je dostupná klíčem a build závislosti jsou nainstalované.

### WP01: Přenositelný build a úpravy jádra (4.1, 4.2)

1. CMake a presety podle 4.1. Linux preset zatím s prázdnou platformou, jen `phpvpn-core` a přenositelné testy.
2. Úpravy 4.2 body 1–9, bod 10 připravit s rozhraním `durable_file.h`.
3. Rozdělení testů v `client/tests/CMakeLists.txt`:
   - proměnná `PHPVPN_PORTABLE_TEST_SOURCES` (seznam z oddílu 6.1) a cíl `phpvpn-core-tests` na obou platformách,
   - Windows `phpvpn-client-tests` zachová dnešní obsah,
   - ověřit přenositelnost harnessu `tests/unit/harness.{h,cpp}`.
4. Nové unit testy: `test_utf8`, rozšíření `test_http_diagnostic` o 32bitový display, `test_wizard_ipc` pro POSIX cesty (jen na Linuxu) a Windows cesty (jen na Windows).

**Hotovo, když:**
- `linux-debug`, `linux-clang` a `linux-asan` sestaví core a `phpvpn-core-tests` projdou na Fedoře i v Ubuntu kontejneru,
- Windows release build a ctest odpovídají baseline.

### WP02: Linux platforma, základ (4.6, 4.7, 4.8)

`clock_linux`, `crypto_openssl`, `paths_linux`, `config_files_linux`, `durable_file` pro Linux a Windows a stub továrny pro zbytek, aby šla linkovat služba.

Testy:
- `test_crypto` nad OpenSSL: stávající vektory a protokolové vektory z `tests-integration/test_crypto_vectors.py`, převzaté jako data.
- `test_clock_linux`.
- `test_config_files_linux`: všechny stavy publish a recovery včetně simulovaného pádu mezi kroky přes injektovaný fault point, symlink, hard link, špatný mód, špatný vlastník a `createIfMissing` se souběhem.
- Přenositelné `test_config_store` nad Linux backendem.

**Hotovo, když:** testy projdou bez root oprávnění a Windows regrese je zelená.

### WP03: Transport libcurl (4.5)

`transport_curl.cpp` a `test_transport_curl` s lokálním in-process HTTP(S) serverem na 127.0.0.1 (OpenSSL self-signed certifikát generovaný testem). Pokrytí:
- health, info a data tělo, streaming do limitu, překročení limitu, nesoulad Content-Length,
- redirect 302 se nenásleduje, proxy proměnné se ignorují, přesná sada hlaviček (porovnat s Windows zachycením z `tests/fixtures/http-diagnostic.php`),
- deadline v connect (blackhole adresa v netns testu, případně neodpovídající listener), pomalé tělo a cancel během všech fází,
- znovupoužití po cancel, účtování bajtů při chybě,
- zachycení hlaviček a trailerů, trust-any vs. ověření.

**Hotovo, když:** testy projdou a contract test (WP06) má transport k dispozici.

### WP04: TUN a netlink (4.3, 4.4)

1. `netlink_codec` s čistými unit testy kódování a parsování, včetně `NLMSG_ERROR`, `DUMP_INTR` a vícedílných zpráv.
2. `tunnel_tun.cpp`, `netconfig_netlink.cpp` a `dns_snapshot.cpp`.
3. Privilegovaný integrační test `phpvpn-netns-tests` (ctest label `privileged`). Běží v `sudo unshare --net --mount` nebo v kontejneru `--privileged`. Na Ubuntu 24.04 neprivilegované user namespaces omezuje AppArmor, proto sudo. Pokrytí:
   - vytvoření `phpvpn`, značka, MTU, bez IPv6 adres, UP,
   - adresa s `NOPREFIXROUTE`, routa metrika 5,
   - raw socket ping na peer IP v `vpnCidr` se objeví v `receive`, zapsaný reply paket přijme socket,
   - oversized detekce, stop probudí receive i send,
   - preflight kolize (cizí adresa nebo routa `vpnCidr`) a kolize jména s cizím `phpvpn`,
   - `sameDefaultRouteAndDns` při změně výchozí routy a při změně `resolv.conf` (bind mount v mount ns),
   - recovery: `kill -9` potomka držícího tun, rozhraní zmizí, `recoverPreviousRun` je clean,
   - recyklace ifindexu cizím rozhraním nevede k falešné identitě,
   - persistentní cizí `phpvpn` znamená fail closed,
   - `removeOwnedPhantomInterface` jen na přesnou shodu.
4. Celý `TunnelSession` start a stop nad reálnou platformou v netns.

**Hotovo, když:** unit testy a netns testy projdou na Fedoře a v Ubuntu kontejneru.

### WP05: IPC unix socket a sdílené executory (4.9)

1. Vytáhnout `ipc_executors` z `ipc_pipe.cpp`. Windows server na ně přepnout bez změny chování. Spustit stávající `test_ipc`, `test_ipc_availability_win` a `test_runtime_status` na Windows.
2. `ipc_unix.cpp`: server a klient.
3. `test_ipc_unix`:
   - rámování, 4 MiB ± 1, deadline 5 s při pomalém klientovi,
   - 32 slotů a 33. spojení, busy odpovědi všech tří executorů, drain při stopu,
   - starý socket, cizí vlastník adresáře znamená odmítnutí,
   - klient odmítne protistranu s uid ≠ 0 (test s injektovanou politikou),
   - uříznutá odpověď.

**Hotovo, když:** testy projdou na Linuxu a Windows IPC regrese je zelená.

### WP06: Linuxová služba a skutečné spojení (4.10), milník M1

1. Refaktor `service_runtime` a `service_host_win`. Windows chování beze změny, ověří ho stávající testy a `test_service_registration`.
2. `service_main_linux.cpp`, unit soubor a man stránka.
3. Contract test `PHPVPN_BUILD_CONTRACT_TESTS` na Linuxu proti Python serveru, případně PHP built-in serveru, na `127.0.0.1:18093` s profilem `wizard-default`. Skript `client/tools/linux/run-server-contract.sh`.
4. Linux service E2E `client/tests/e2e/linux/run-service-e2e.sh` (root, izolované network namespaces):
   - ns `relay` s Python serverem, dvě ns `client-a` a `client-b` spojené přes veth nebo bridge,
   - v každé ns izolovaná instance služby (vlastní config, state a runtime dir) s konfigurací z testovacích klíčů `10.99.99.2` a `10.99.99.3` přes `phpvpn set-config`,
   - `connect` na obou, `status` s `connected/running/tunnelReady`,
   - `ping 10.99.99.3` z A, TCP přenos 50 MiB s kontrolou SHA-256, čítače `diagnostics` rostou,
   - `disconnect` pod 30 s, rozhraní a routa zmizí,
   - `kill -9` služby během spojení, restart, recovery clean a znovu connect,
   - `autoConnectOnStartup=true` a restart,
   - `SIGTERM` s aktivním spojením skončí úplným úklidem do 45 s,
   - výchozí routa a DNS hlavní ns se nezmění,
   - Wizard IPC tok (open, check url, secret, access, finish) přes malý testovací klient používající core `wizard_ipc` a stejný rámec. Ověří, že POSIX cesta a commit fungují.
5. Instalace z build stromu na Fedora VM (`cmake --install` do `/usr`, dočasně, před balením) a spojení s reálným serverem v hlavní síti VM.

**Hotovo (M1), když:** contract test a E2E projdou a služba na Fedora VM naváže a udrží spojení alespoň 30 minut s periodickým pingem na druhého klienta bez ztráty spojení.

### WP07: CLI (4.11), milník M2

Přenositelný `apps/cli/main.cpp`. Testy:
- skript `client/tests/e2e/linux/cli-smoke.sh`: všechny příkazy, exit kódy, služba zastavená, neplatný soubor, soubor nad limit,
- Windows CLI regrese: spuštění stávajících testů a ruční `phpvpn status` proti službě, je-li k dispozici.

**Hotovo, když:** CLI ovládá službu na VM ve `multi-user.target` bez grafiky a výstupy odpovídají Windows formátu.

### WP08: Headless balení a instalace (4.13), milník M2b

- `phpvpn.spec`, `debian/`, NetworkManager snippet, `install.sh`, `uninstall.sh`, README a build skripty.
- Ověření:
  - `rpmlint` a `lintian` bez chyb,
  - `rpm -qpR` a `dpkg-deb -I` bez GTK závislostí,
  - instalace do čistého kontejneru bez grafiky: `fedora:<N>` s `--setopt=install_weak_deps=False` a `ubuntu:24.04` se systemd v podman `--systemd=always`,
  - `systemctl status`, `phpvpn status`, upgrade na vyšší release číslo s restartem služby a zachovanou konfigurací, uninstall ponechá data, `--remove-data` data odstraní.

**Hotovo, když:** instalace a odinstalace projdou na obou distribucích v headless režimu.

### WP09: Tray, přenositelný refaktor a infrastruktura (4.12)

1. Extrakce `save_response.h`, `contracts_save_machine.h` a `ipc_timeouts.h`, přetížení `tray_arguments`. Windows build a tray testy (`test_wp03_*`, `test_tray_*`) musí zůstat zelené.
2. `apps/tray-linux`: `tray_app`, `indicator`, `ipc_async`, `style`, `notifications`, `dialogs`, `control_window`, generované ikony a `make_linux_icons.py`.
3. Menu, stavy ikon, polling, Connect/Disconnect s progress oknem a reconnect controller.
4. Unit testy modelu indikátoru bez GTK: mapování status na ikonu a text, menu položky a detekce watcheru přes injektované rozhraní.

**Hotovo, když:** na Fedora VM (GNOME s rozšířením) tray ukazuje skutečný stav a Connect a Disconnect fungují. Bez rozšíření se otevře ovládací okno.

### WP10: Tray, zbývající okna, milník M3

Wizard včetně HTTP headers a error detailu, Edit Config, Communication Contracts s Password SHA, Server Info, Statistics, installation notice, Exit a konec relace.

Testy:
- přenositelné modely už jsou pokryté,
- Linux UI smoke přes `GTK_A11Y` a AT-SPI (`dogtail`, případně `python3-pyatspi` skript) v `client/tests/ui/linux/`: otevření každého okna, texty tlačítek, uložení v editoru, Wizard nad testovacím relay,
- ruční proklikání na VM podle checklistu v reportu.

**Hotovo, když:** všechny položky menu jsou funkční a stav tray odpovídá `phpvpn status`.

### WP11: Balíček `phpvpn-tray`

`.desktop`, autostart, ikony, licence, Recommends. Ověřit:
- `desktop-file-validate`,
- autostart po přihlášení na VM,
- instalaci na Ubuntu kontejneru bez chyb (`ldd` a spuštění `phpvpn-tray --help` bez displeje vrátí srozumitelnou chybu, ne pád).

### WP12: Dokumentace

- `client.md`:
  - oddíl `### Linux` rozšířit na normativní popis: cesty, oprávnění, socket, rámování, peer check, systemd, identita rozhraní a značka, NOPREFIXROUTE, DNS snapshot, MTU, journald, balíčky a instalační vstupy,
  - `client.md:1164–1166` aktualizovat. Linux přestane být „budoucí port“.
- `tray_UX.md`: oddíl „Linux“ s rozdíly z 4.12.
- `distribution.md`: strom `client/Linux/x64`, zdroje, postup (Linux artefakty z WP08 a WP11 a parametr `-LinuxPackagesDirectory`), ověření a zrušení Linux README placeholderu.
- `dist/client/Linux/x64/README.md` (zdroj v `client/packaging/linux/README.md`) s oddílem „Rozdíly oproti Windows“ (oddíl 7).
- Man stránky a po změnách `graft build`.

### WP13: Závěrečné ověření a report

Podle oddílu 6. Report `client/tests/linux-client-report-<datum>.md` s tabulkou RUN/PASS/FAIL/NOT RUN, verzemi prostředí a omezeními.

### WP14: Celková distribuce

Podle `distribution.md` (sestavení Windows klienta, Linux balíčky z WP08 a WP11, `make-distribution.ps1`, `verify-distribution.ps1`, rozbalení a ověření). Pouze na pokyn uživatele nebo jako součást předání vydání.

## 6. Testovací strategie a ověření

### 6.1 Přenositelné unit testy (Linux i Windows)

base64url, config, config_store, contracts_document, contracts_model, crypto (nad platformním providerem), flow_key, http_diagnostic, http_headers, info_extensions, ipc, iso8601, json, logger, password_key, payload_batch, plan3_fix2, queue_drr, queue_expiry, queues, server_info_model, server_info_layout, status_contract, time_sync, traffic_counters, tunnel_queues, tray_geometry, tray_layout, tray_progress, tray_status, wizard_document, wizard_ipc, wizard_layout, wizard_model, wp03_models a nové `test_utf8`.

Každý soubor se před zařazením přeloží na Linuxu. Soubor se skrytou Windows závislostí zůstane ve Windows cíli a zapíše se do reportu.

### 6.2 Linux specifické testy

Nevyžadují root: `test_crypto_openssl`, `test_clock_linux`, `test_config_files_linux`, `test_transport_curl`, `test_ipc_unix`, `test_netlink_codec`, modely tray.

Vyžadují root (label `privileged`): `phpvpn-netns-tests`, service E2E a CLI smoke.

Sanitizery: `linux-asan` (ASan a UBSan) pro všechny neprivilegované testy a E2E jednou před M1 a M3.

### 6.3 Reálné ověření na Fedora VM (akceptace)

1. Instalace přes `dist`-like adresář (`install.sh`) z balíčků WP08 a WP11. Ověřit podpis a hashe, službu `active (running)`, `systemd-analyze security` a SELinux enforcing bez AVC.
2. Relay: reálný phpvpn server (Python distribuce, případně PHP), rozhodnutí z WP00. Konfigurace přes tray Wizard (grafická relace) a zvlášť přes `phpvpn set-config` (headless).
3. Spojení: `phpvpn connect`, `status`, `diagnostics`, `info`. Přenos provozu na druhého klienta:
   - druhá izolovaná instance v netns na VM,
   - volitelně Windows klient na hostiteli, jen po výslovném souhlasu uživatele, protože mění hostitelský systém,
   - ping, TCP přenos se SHA-256, udržení 30 minut.
4. Odpojení z CLI i z tray. Úklid rozhraní a routy, výchozí routa a DNS beze změny (`ip route`, `resolvectl status` před a po).
5. Zotavení: restart služby během spojení, `kill -9`, výpadek relay (zastavení serveru a obnova), suspend a resume VM, reboot s `autoConnectOnStartup=true`.
6. Headless: `systemctl isolate multi-user.target`, případně čistý kontejner. Služba a CLI fungují, balíček `phpvpn` nemá GUI závislosti.
7. Tray v GNOME, grafická plocha je na druhém monitoru uživatele:
   - s rozšířením AppIndicator: ikona, menu, všechny okna, stav shodný se `phpvpn status` při změnách vyvolaných z CLI,
   - bez rozšíření: ovládací okno a notifikace,
   - autostart po přihlášení, druhá instance, `--installation-complete` notice, Exit neodpojí VPN, odhlášení neodpojí VPN.
   - Uživatel může být požádán o proklikání. Postup a výsledky zapsat.

### 6.4 Ubuntu

- Build, unit testy, netns testy, balíčky a headless instalace se systemd v kontejneru `ubuntu:24.04`.
- Pokud uživatel poskytne Ubuntu Desktop VM, zopakovat 6.3 body 1–7. Jinak grafické ověření na Ubuntu jako `NOT RUN` s odůvodněním.
- Ubuntu 26.04 build jako informativní průchod, pokud je image dostupný.

### 6.5 Regrese Windows

- `build-x64.ps1 -Preset release -Configure -ResourceTests`, ctest release a asan (jako v baseline), `verify-package.ps1`, UI testy stejně jako v baseline.
- `tools/make-distribution.ps1` a `verify-distribution.ps1` (s Linux artefakty).
- Service E2E (`run-service-e2e.ps1`) vyžaduje UAC. Spustit jen se souhlasem a interakcí uživatele, jinak `NOT RUN`.
- Porovnat s `work/linux-plan/baseline-windows.md`. Každý rozdíl vysvětlit.

## 7. Dokumentované rozdíly Linux vs. Windows (obsah pro README a client.md)

| Oblast | Windows | Linux | Důvod |
|---|---|---|---|
| Správa služby | SCM, `phpvpn-service --install` | systemd unit z balíčku | linuxová zvyklost |
| Tunel | Wintun, adaptér `PHPVPN`, GUID | `/dev/net/tun`, nepersistentní `phpvpn`, značka v `ifalias` | nativní mechanismus, recovery jádrem |
| Čtení konfigurace z disku | Users čtení | jen root (`0600`) | `client.md:1154`, zvyklost pro tajemství |
| IPC | named pipe, Everyone | unix socket `0666` a kontrola, že server je root | zachování `client.md:306` |
| Logy | soubor s rotací 8 MiB/5 | journald | linuxová zvyklost |
| Tooltip tray | ano | stavová položka v menu a title | AppIndicator nemá tooltip |
| Tray na Fedora GNOME | vždy | vyžaduje rozšíření AppIndicator, jinak ovládací okno | omezení GNOME |
| Pozice oken | určuje aplikace | určuje kompozitor (Wayland) | omezení Wayland |
| Aktivace služby po instalaci | instalátor | `install.sh` enable --now. Samotné RPM podle presetů. | zvyklost distribucí |
| Instalace | podepsaný ZIP s PowerShell | podepsaný manifest, RPM/DEB, `install.sh` | zvyklost distribucí |

## 8. Rizika a mitigace

| Riziko | Mitigace |
|---|---|
| Refaktor sdíleného kódu rozbije Windows | Minimální změny rozhraní, přesuny bez změny chování, baseline z WP00, regrese po každém WP, který se dotkne sdílených souborů (WP01, 02, 05, 06, 07, 09). |
| Kolize kernelové prefixové routy s explicitní routou | `IFA_F_NOPREFIXROUTE`, test ve WP04. |
| Recyklace ifindexu a falešná shoda identity | Značka vlastnictví v `ifalias` a tag v identitě, test ve WP04. |
| DNS a výchozí routa se mění nezávisle (DHCP) a vyvolají falešný rollback | Stejné chování jako Windows. Diagnostika v logu. Zaznamenat v reportu, pokud se projeví. |
| GNOME bez SNI | Recommends rozšíření, ovládací okno a notifikace (4.12). |
| Omezení user namespaces na Ubuntu 24.04 | Privilegované testy přes sudo nebo privilegovaný kontejner. |
| SELinux nebo hardening blokuje službu | Test v enforcing. Při problému nejdřív upravit unit, vlastní SELinux politika až jako samostatné rozhodnutí. |
| Dočasná VM zmizí před koncem | Pořadí priorit (M1 co nejdřív). Kontejnerové testy jako náhrada headless části. Neprovedené body jako NOT RUN. |
| Build na Windows hostiteli není možný | Linux build výhradně na VM nebo v kontejnerech. Provenance vázaná na commit. |
| Závislost ayatana a GTK proti duchu `tray_UX.md:75` | Explicitní schválení v oddílu 10. Náhradní vlastní SNI přes GDBus. |

## 9. Rozdělení práce mezi agenty

Práce je rozdělená na ucelené úkoly pro samostatné agenty, které běží sekvenčně nebo paralelně. Každý agent pracuje na jednom tématu od začátku do konce. Menší a soustředěnější kontext je přirozeným důsledkem tohoto rozdělení. Plán proto nestanoví žádná pravidla, která by čtení, výstupy nebo kontext agentů aktivně omezovala. Každý agent si vezme tolik podkladů, kolik jeho úkol potřebuje.

### 9.1 Role

- **Koordinátor (hlavní kontext):** drží tento plán, pořadí, stav WP a rozhodnutí z oddílu 10.
  - Velké části neimplementuje sám.
  - Pro každý WP spustí agenta s předávacím balíčkem (9.3), vyhodnotí jeho výsledek a commituje po ověření.
  - Vede `work/linux-plan/status.md`: stav WP, commity, otevřené problémy a rozhodnutí. Soubor slouží jako společný stav pro navazující agenty.
- **Implementační agent (jeden na WP nebo dílčí úkol):** dostane zadání WP a odkazy na podklady. Implementuje, spustí určené testy a vrátí souhrn: změněné soubory, výsledky testů, odchylky od plánu, otevřené otázky. Necommituje.
- **Ověřovací agent (nezávislý, read-only vůči zdrojům):** po WP s rizikem (WP01, 04, 05, 06, 09) zkontroluje diff proti plánu, `client.md` a Windows regresi a hledá chyby (code review). Nepíše implementaci, vrací nálezy.
- **VM agent (jediný vlastník VM):** všechny operace na Fedora VM (sync, build, testy, instalace) jdou přes jednoho agenta v daném okamžiku. VM je sdílený stavový zdroj a paralelní přístup by kolidoval. Logy ukládá do `work/linux-plan/vm-logs/`, aby byly dostupné i dalším agentům a do reportu.

### 9.2 Graf závislostí a paralelizace

```text
WP00 ─► WP01 ─┬─► WP02 ─┬─► WP06 ─► WP07 ─► WP08 ─┐
              ├─► WP03 ─┤                          ├─► WP12 ─► WP13 ─► WP14
              ├─► WP04 ─┤                          │
              ├─► WP05 ─┘                          │
              └─► WP09 (refaktor, krok 1) ─► WP09 (GTK) ─► WP10 ─► WP11 ─┘
```

| Vlna | Paralelně běžící agenti | Podmínka |
|---|---|---|
| A | WP00: 1 agent Windows baseline a MTU, 1 VM agent pro přípravu VM | žádná |
| B | WP01: 1 agent, sekvenčně | WP00 |
| C | **4 paralelní implementační agenti:** WP02, WP03, WP04 (bez privilegovaných testů), WP05. Plus 1 agent pro WP09 krok 1 (přenositelný refaktor tray, jen Windows build) | WP01 commitnut. Každý agent píše jen do svých souborů (9.4). |
| C′ | VM agent sekvenčně spouští Linux build a testy výstupů vlny C, jak přicházejí. Nejdřív neprivilegované, pak `privileged` pro WP04. | výstupy C |
| D | WP06 (1 agent, sekvenčně). Souběžně 1 agent WP09 krok 2–4 (GTK infrastruktura proti mock IPC nebo `phpvpn-service --foreground` na VM) | WP02–WP05 |
| E | WP07 a WP08 sekvenčně (1 agent každý). Souběžně WP10 rozdělený na 2–3 agenty podle oken: (a) Wizard, headers a error detail, (b) Editor, Contracts a Password SHA, (c) Server Info, Statistics a notice | WP06, WP09 |
| F | WP11 (1 agent), WP12 (1 agent dokumentace, může začít po M1 a doplňovat) | WP08, WP10 |
| G | WP13: VM agent pro akceptaci a ověřovací agent pro celkovou revizi a Windows regresi (sekvenčně kvůli sdílenému Windows buildu), pak koordinátor sepíše report | vše |
| H | WP14 na pokyn | WP13 |

Paralelní agenti ve vlně C a E pracují ve **stejné větvi bez commitů**, s přísně oddělenými soubory. Commituje koordinátor po jednom WP. Kolidují-li změny ve sdílených souborech (`client/src/CMakeLists.txt`, `client/tests/CMakeLists.txt`), provede je jen koordinátor nebo jeden určený agent. Ostatní agenti je dodají jako požadavek v souhrnu.

Alternativa pro větší izolaci: každý paralelní agent pracuje v dočasném git worktree z `linux` (`isolation: worktree`) a koordinátor výsledky postupně aplikuje. Doporučeno pro vlnu C, protože se tam sbíhají CMake soubory.

### 9.3 Předávací balíček pro agenta (šablona)

Každý agent dostane samostatný prompt:

1. **Cíl:** jeden WP nebo dílčí úkol.
2. **Závazné zdroje:** příslušné pododdíly tohoto plánu (např. „4.3 a 5/WP04“) a relevantní části `client.md`, `tray_UX.md` a `protocol.md` s orientačními rozsahy z oddílu 2.
3. **Výchozí body v kódu:** `file:line` z oddílu 2. Agent postupuje podle `AGENTS.md` (graft), když je dostupný.
4. **Vlastněné soubory:** kam smí zapisovat. Vše ostatní jen číst.
5. **Rozhraní, která se nesmí měnit:** například `platform/iface/*.h` ve vlně C.
6. **Testy a příkazy k ověření:** včetně toho, co spouští VM agent. Implementační agent bez přístupu k Linuxu dodá kód a testy, VM agent je spustí.
7. **Obsah návratu:** změněné soubory, výsledky testů (PASS/FAIL/NOT RUN), odchylky od plánu, otevřené otázky.
8. **Zákazy:** necommitovat, nepushovat, neměnit intent ani plán, neukládat přihlašovací údaje VM, neprovádět změny na hostitelském Windows systému mimo build a testy bez souhlasu.

### 9.4 Vlastnictví souborů v paralelních vlnách

| Agent | Vlastní soubory |
|---|---|
| WP02 | `platform/linux/{clock_linux,crypto_openssl,paths_linux,config_files_linux}.*`, `platform/iface/durable_file.h`, `platform/linux/durable_file_linux.cpp`, `platform/windows/durable_file_win.cpp`, testy `test_*_linux`, `test_crypto_openssl` |
| WP03 | `platform/linux/transport_curl.cpp`, `tests/unit/test_transport_curl.cpp` a pomocný HTTP server v testech |
| WP04 | `platform/linux/{tunnel_tun,netconfig_netlink,netlink_codec,dns_snapshot,fd}.*`, `tests/unit/test_netlink_codec.cpp`, `tests/linux/netns/*` |
| WP05 | `platform/iface/ipc_executors.*`, `platform/iface/ipc_server.h`, `platform/windows/ipc_pipe.*`, `platform/linux/ipc_unix.*`, `tests/unit/test_ipc_unix.cpp` |
| WP09 krok 1 | `apps/tray/{save_response,contracts_save_machine,ipc_timeouts}.h`, `apps/tray/status_reader.h`, `apps/tray/contracts_save_controller.*`, `apps/tray/tray_arguments.h` |
| WP10 a/b/c | jen vlastní `apps/tray-linux/<okno>.*`. Registraci v CMake a menu dělá koordinátor. |

### 9.5 Návaznost mezi agenty

- Po každém dokončeném WP koordinátor aktualizuje `status.md` a spustí `graft build`, aby navazující agenti pracovali nad aktuálním stavem.
- Každý agent řeší jeden WP nebo jedno okno. Pokračování stejného úkolu po opravě nálezů může převzít týž agent (zachovaný kontext), nový úkol dostane nový agent.
- Nálezy ověřovacího agenta jdou zpět implementačnímu agentovi daného WP. Koordinátor rozhoduje jen o odchylkách od plánu.

## 10. Rozhodnutí k potvrzení uživatelem

Realizace může začít s uvedenými výchozími volbami. Změna některé z nich se promítne do `plan-v002.md`.

1. **Tray toolkit:** GTK 3 a libayatana-appindicator3 (výchozí), nebo GTK 3 s vlastním SNI a dbusmenu přes GDBus bez ayatana.
2. **Přístup k IPC:** zachovat `client.md` (socket `0666`, výchozí), nebo linuxově obvyklou skupinu `phpvpn` se socketem `0660`. To vyžaduje úpravu `client.md` a rozdíl proti Windows.
3. **Čtení konfigurace z disku:** jen root `0600` (výchozí, podle `client.md:1154`), nebo čtení pro všechny jako na Windows.
4. **ID aplikace tray:** výchozí `org.phpvpn.Tray`, lze nahradit doménou, kterou projekt vlastní.
5. **Oprava Windows CLI ne-ASCII cesty** v `set-config` jako vedlejší efekt přenositelného čtení souboru: ano (výchozí), nebo zachovat přesné dnešní Windows chování.
6. **Ověření dosažitelnosti relay** (`client.md:914`) zapojit do core pro obě platformy: mimo rozsah (výchozí, samostatný úkol), nebo součást této realizace s Windows regresí.
7. **Windows interop test** (Windows klient na hostiteli a Linux klient na VM): jen se souhlasem (výchozí nespouštět).
8. **Ubuntu Desktop VM** pro grafické ověření: bez ní bude Ubuntu GUI `NOT RUN`.

## 11. Mimo rozsah

- Full tunnel, změny DNS, kill switch a IPv6 v tunelu (`client.md:886`).
- Multiqueue TUN, vlastní SELinux politika a AppArmor profil.
- Jiné architektury než x86_64, jiné distribuce (Debian, openSUSE, Arch) a Flatpak nebo Snap.
- Změny serveru a protokolu.
- Přepis Windows tray nad sdílenou UI vrstvou.
- Publikace do veřejných repozitářů a podpis balíčků GPG klíčem distribuce. Manifest je podepsán stejnou úrovní důvěry jako Windows balíček a pravdivě označen.

## 12. Mapování na akceptační kritéria intentu

| Akceptační kritérium | Kde se plní | Důkaz |
|---|---|---|
| Služba naváže a udrží spojení | WP02–WP06, 6.3 bod 3 | E2E, 30 min na VM, report |
| CLI headless ovládá službu, stav a diagnostika | WP07, WP08, 6.3 bod 6 | CLI smoke, headless kontejner a VM |
| Tray ovládá spojení a ukazuje stav shodný se službou | WP09–WP11, 6.3 bod 7 | UI smoke a ruční checklist |
| Instalace, oprávnění, služba a síť podle linuxových postupů | 4.8–4.10, 4.13, WP08, WP11 | rpmlint, lintian, systemd-analyze, SELinux, report |
| Ověřeno na Fedoře, vhodně na Ubuntu, omezení uvedena | WP13, 6.3, 6.4 | report RUN/NOT RUN |
| Windows klient nenarušen | 6.5 | srovnání s baseline |
