# WP03: Android platformní vrstva, JNI hosté a spike T-A01b, 2026-10-31

Výsledek kroku 3 podle [plan-android-v001.md](../../../Plans/2026-10-31-09-40-08/plan-android-v001.md)
(body 1 až 5, 10, 11, 17 a 18, testy T-N02, T-A01, T-A01b, T-A13 a T-N04 v aplikaci). Co se nespustilo, je
NOT RUN s důvodem. Žádný výsledek z emulátoru se nevydává za výsledek z reálného zařízení.

Větev `worktree-agent-a7a8dc612ee8ac4f5` nad `android` (64d6d26, po restartu počítače sloučena `android`
3530321 s WP02 follow-up a WP07). Commity: `ce71e8e` (chybový kód), `921bccc` (platformní vrstva a host
testy), `8678fea` (JNI hosté, Kotlin rozhraní a VpnService testy), `a1997f0` (merge `android`), `e9e0ce4`
(oprava testů pro API 34 až 36), `e65ad0b` (R8 pravidla, T-A13 na emulátoru) a commit s tímto reportem.

## Co se změnilo a proč

### A. `AndroidVpnContext` (`client/src/platform/android/vpn_context.{h,cpp}`, bod 17)

- Jediný vlastník sdíleného stavu, čistý C++ bez JNI (testovatelný na hostu i na Windows). Pod jedním mutexem
  drží `VpnSpec`, příznaky `specAddressSet` a `specRouteSet`, syntetickou identitu aktuálního adaptéru,
  deskriptor, kandidáta baseline a zmrazenou baseline. Hosté mají vlastní mutex, takže registrace nebo
  odregistrace z Kotlin vlákna nikdy nečeká na běžící volání hosta.
- `current()` vrací nainstalovaný kontext, `install(shared_ptr)` a `uninstall()` ho nastaví a zruší.
  Továrny `createPlatformNetConfig()` a `createPlatformTunnelDriver()` si `current()` vezmou při vytvoření;
  bez kontextu vrátí objekt, jehož každá operace selže `tunnel_driver_unsupported` (nikdy nulový ukazatel).
- Syntetická identita `(0x414E << 48) | counter`, čítač je procesní (nový kontext po `nativeStop` a
  `nativeStart` nikdy nezopakuje identitu, kterou by mohl jmenovat starší journal téhož procesu).
- Stavový automat:

| Operace | Podmínka | Výsledek |
|---|---|---|
| `createAdapter(name)` | žádný otevřený adaptér | nová identita, prázdná spec, zmrazí poslední snapshot pořízený bez adaptéru jako baseline; jinak `state_conflict` |
| `recordAddress(id, addr, prefix)` | `id` je aktuální identita; prefix 24 až 29; adresa je klientská adresa prefixu (ne síť, ne `.1`, ne broadcast) | zapíše; stejná hodnota znovu = úspěch; jiná hodnota nebo po `establish` = `netconfig_apply_failed` |
| `recordRoute(id, cidr, nextHop)` | adresa už zapsaná; next hop 0.0.0.0; cíl = přesně prefix adresy | zapíše; jinak `netconfig_apply_failed`; metrika nemá na Androidu obdobu (dokumentovaná odchylka jako macOS) |
| `establish()` | otevřený adaptér, obě položky, registrovaný TunnelHost | `TunnelHost::establish(spec)`, deskriptor drží kontext; bez adaptéru `tunnel_adapter_failed`, neúplná spec `tunnel_session_failed`, bez služby `tunnel_adapter_failed` „the VPN service is not running“, opakovaně `state_conflict` |
| `closeAdapter()` | kdykoli | zavře deskriptor, `waitVpnNetworkGone(2000)` (bez služby jen varování v logu), vynuluje stav |
| `holdsInterface(id)` | | pravda jen pro identitu s právě drženým deskriptorem |

- `VpnSpec` se do Kotlinu předává jako UTF-8 JSON (`serializeVpnSpec`), pořadí členů pevné:

```json
{"version":1,"session":"phpvpn","address":"10.99.99.2","prefixLength":24,"route":"10.99.99.0",
 "routePrefixLength":24,"mtu":1500,"disallowedApplications":["<vlastní balíček>"],
 "allowIpv6":true,"metered":false,"dnsServers":[]}
```

  `allowIpv6` = `Builder.allowFamily(AF_INET6)`: bez něj Android blokuje IPv6 všech aplikací, na které se VPN
  vztahuje, protože VPN nemá žádnou IPv6 adresu ani routu (premisa „IPv6 beze změny“). `metered=false` =
  `Builder.setMetered(false)`: VPN převezme meteredness podkladové sítě, jinak by pro aplikace s
  `targetSdk ≥ 29` byla každá síť pod VPN „metered“. `dnsServers` je prázdné (záložní cesta T-A01b nebyla
  potřeba, viz níže).

### B. `AndroidTunnelDriver` (`client/src/platform/android/tunnel_android.{h,cpp}`)

- `loadAndVerify`: souhlas přes `NetworkHost` jako první (chyba, na kterou může reagovat uživatel, obrazovka
  37) → `tunnel_permission_missing`; chybějící `NetworkHost` → `tunnel_driver_unsupported`; chyba dotazu na
  souhlas → kód hosta (`tunnel_adapter_failed`); chybějící `TunnelHost` → `tunnel_adapter_failed` s popisem
  „the VPN service is not running“.
- `createAdapter`: jen syntetická identita, `created = true`, GUID tak, jak je nakonfigurovaný (stejně jako
  Linux, kvůli porovnání journalu). `openAdapter` = `not_supported`. `runningDriverVersion` = 0.
- `startSession`: `establish` → hned nový snapshot a `baseline.sameDefaultRouteAndDns(after)`; neshoda,
  chybějící baseline nebo nečitelný snapshot → `closeAdapter` a `netconfig_verify_failed` (bod 2). Potom
  `O_NONBLOCK` a `FD_CLOEXEC` na deskriptoru, `eventfd` a `FdTunnel` z `platform/posix/fd_tunnel.h`.
  Vytvoření session je vstříknutá továrna (`AndroidTunnelFactory`), takže logiku ovladače testuje host
  i na Windows; produkce používá `makeFdTunnelSession`.
- `endSession` odpojí session (`FdTunnel::detach`) a deskriptor ponechá, `closeAdapter` ho zavře přes kontext.
  Ovladač zavírá jen adaptér, který sám vytvořil.
- Recovery po smrti procesu: journal jmenuje identitu, kterou žádný deskriptor nedrží → `interfaceExists`
  false → `removeOwnedPhantomInterface` se nevolá → snapshot nemá řádky s touto identitou (VPN sítě jsou
  vyloučené, podkladové identity mají horních 32 bitů nula) → journal se smaže. Pokud identitu drží tento
  proces, `removeOwnedPhantomInterface` vrátí `journal_ambiguous` a journal zůstane. Ověřeno na hostu i na
  emulátoru.

### C. `AndroidNetConfig` a JSON snapshotu (`client/src/platform/android/netconfig_android.{h,cpp}`)

Schéma `NetworkHost.snapshotNetworks()`, verze 1, UTF-8, nejvýš 1 MiB, všechny členy povinné, neznámý člen je
chyba (`netconfig_apply_failed`):

```json
{"version":1,
 "activeNetwork":<Network.getNetworkHandle() aktivní sítě aplikace> | null,
 "networks":[
  {"handle":<getNetworkHandle()>,
   "interfaceName":"<LinkProperties.getInterfaceName()>" | null,
   "vpn":<hasTransport(TRANSPORT_VPN)>,
   "transports":["wifi"|"cellular"|"ethernet"|"vpn"|"bluetooth"|"other", ...],
   "addresses":[{"interfaceName":"<link>","ifindex":<index nebo 0>,
                 "address":"<IPv4 nebo IPv6 text>","prefixLength":<0..32|0..128>}, ...],
   "routes":[{"interfaceName":"<RouteInfo.getInterface()>" | null,"ifindex":<index nebo 0>,
              "destination":"<IpPrefix adresa>","prefixLength":<n>,"gateway":"<adresa>" | null,
              "type":"unicast"|"unreachable"|"throw"|"other","isDefault":<isDefaultRoute()>}, ...],
   "dnsServers":["<adresa>", ...]}, ...]}
```

- Limity: 64 sítí, 512 řádků adres a rout na síť, 16 DNS serverů, jména do 64 bajtů. IPv4 adresy musí být
  kanonické (stejný parser jako jádro), IPv6 se validují jen abecedou a zahodí.
- Převod na `NetworkSnapshot`: každá síť s `vpn: true` je vynechaná (vlastní VPN se nehledá podle jména);
  identita řádku je ifindex s horními 32 bity nula; neznámý ifindex (0) dává `0xFFFFFFFF`, nikdy 0, protože
  preflight jádra identitu 0 přeskakuje jako vlastní rozhraní; jen IPv4 `unicast` routy; cíle, které
  `Ipv4Cidr` neumí vyjádřit (mimo /24 až /29), mají výchozí prefix jako na Linuxu; výchozí routa je první
  IPv4 `isDefault` routa aktivní sítě, `defaultRouteMetric` vždy 0; DNS servery aktivní sítě v pořadí
  systému, IPv4 i IPv6 (Linux bere jen IPv4, ale pro detektor změny je úplný seznam správnější). Aktivní VPN
  nebo chybějící aktivní síť = žádná výchozí routa a žádné DNS.
- Baseline: každý `snapshot()` bez otevřeného adaptéru je kandidát, `createAdapter` ho zmrazí. Baseline je tak
  přesně stav, který právě schválil preflight jádra.
- `addUnicastAddress`/`addRoute` zapisují do spec kontextu, `removeUnicastAddress`/`removeRoute` jsou pro
  vlastní (syntetickou) identitu úspěch bez systémové operace, cizí identita je `netconfig_apply_failed`.
  `interfaceExists(0)` je chyba jako na Linuxu. `interfaceLuidForAlias` vrací identitu otevřeného adaptéru se
  stejným jménem. `isHostReachableOutsideInterface`: `getaddrinfo` (AF_INET) a nejdelší shoda mezi IPv4
  unicast routami aktivní podkladové sítě; vlastní provoz aplikace do VPN nikdy nejde (bod 5). Jádro ji
  zatím nevolá.

### D. Hodiny a logcat

- `clock_android.cpp`: `monotonicMs` a `leaseMonotonicMs` z `CLOCK_BOOTTIME`, `systemUtcMs` z `CLOCK_REALTIME`
  (bod 11). `createAndroidClock()` se překládá i na Linuxu kvůli testu.
- `log_sink_android.cpp`: `installLogcatSink()` nastaví sekundární sink loggeru na `__android_log_write`
  s tagem `phpvpn`; z řádku se odstraní ISO čas a úroveň (logcat má vlastní čas, úroveň nese priorita:
  trace → VERBOSE … critical → FATAL). Logcat dostane jen to, co už logger propustil. Očekávané volání ve
  WP04 (`android_host`, `prepareStorage`, po `secureServiceDirectory` adresáře logů):
  `Logger::instance().configureFile(logsDirectory, "phpvpn-service"); installLogcatSink();`

### E. Systémové CA pro libcurl (`client/src/platform/android/tls_trust_android.{h,cpp}`, bod 10)

- Postup sondy z WP01: každý soubor adresáře přes `PEM_read_X509` do jednoho `X509_STORE`, APEX
  `/apex/com.android.conscrypt/cacerts` přednostně, `/system/etc/security/cacerts` jen když APEX nedá žádný
  certifikát. Uživatelské CA se nenačítají. Store se načte jednou za proces (neúspěch se zkusí znovu při
  dalším requestu), `X509_V_FLAG_PARTIAL_CHAIN` jako u storu, který by sestavil curl, chyby jen kódem
  (`transport_tls_failed`), log jen počet a zdroj („APEX module“), žádná cesta.
- Hook v `client/src/platform/linux/transport_curl.cpp`: `configureTlsTrust(CURL*)` je na Linuxu a macOS
  prázdná funkce vracející úspěch; na Androidu nastaví `CURLOPT_SSL_CTX_FUNCTION` se
  `SSL_CTX_set1_cert_store`. Volá se jen při `trustAnyTlsCertificate=false`, takže Linux a macOS chování je
  beze změny (macOS překlad jen inspekcí, viz výsledky). Hlavička nepotřebuje curl include (`void*`).

### F. JNI hosté (`client/apps/android-jni/jni_vpn_bridge.{h,cpp}`, bod 18)

- `JniNetworkHost` a `JniTunnelHost` drží globální referenci na Kotlin objekt. Vlákno se při prvním volání
  připojí (`AttachCurrentThread`, jméno `phpvpn-native`) a odpojí se při svém konci (`thread_local` guard,
  běží před pthread key destruktorem ART); Java vlákna se nikdy neodpojují. Každé volání má vlastní
  `PushLocalFrame`/`PopLocalFrame` (nativní vlákno nemá Java rámec, který by lokální reference uvolnil)
  a po každém volání `ExceptionCheck` + `ExceptionClear`; výjimka se mění na kód, její text do logu nejde.
- Mapování `TunnelHost.lastEstablishError()`: 1 `ESTABLISH_NOT_PREPARED` → `tunnel_permission_missing`,
  2 `ESTABLISH_REJECTED` → `tunnel_adapter_failed` („the system rejected the VPN interface parameters“),
  jinak `tunnel_adapter_failed`. Výjimka v Kotlinu → `tunnel_adapter_failed`; chyba snapshotu →
  `netconfig_apply_failed`.
- `registerVpnHostNatives(env)` se volá jedinou přidanou řádkou z `JNI_OnLoad` v `jni_entry.cpp` a registruje
  `VpnHosts.registerNetworkHost`, `registerTunnelHost`, `unregisterTunnelHost`. Registrace se ihned propíše do
  nainstalovaného kontextu a uloží se pro kontext vytvořený později: WP04 v `nativeStart` volá
  `attachRegisteredVpnHosts(*context)` před `AndroidVpnContext::install(context)`.
- `libphpvpn-android.so` se linkuje s `-Wl,--exclude-libs,ALL`; export jen `JNI_OnLoad`.
- `bridge/consumer-rules.pro` drží `NetworkHost`, `TunnelHost` a `VpnHosts` pro případný R8 (JNI je hledá
  jménem, přejmenování by zabránilo načtení knihovny).

### Rozhraní pro WP04 a WP05

Kotlin (`client/android/bridge/src/main/kotlin/phpvpn/android/bridge/`):

```kotlin
interface NetworkHost {
    fun snapshotNetworks(): ByteArray      // UTF-8 JSON, schéma výše
    fun isConsentGranted(): Boolean        // VpnService.prepare(applicationContext) == null
}
interface TunnelHost {
    fun establish(specJson: ByteArray): Int   // detachFd() nebo -1
    fun lastEstablishError(): Int             // ESTABLISH_OK 0, _NOT_PREPARED 1, _REJECTED 2, _FAILED 3
    fun waitVpnNetworkGone(timeoutMs: Int): Boolean
}
object VpnHosts {
    @JvmStatic external fun registerNetworkHost(host: NetworkHost)
    @JvmStatic external fun registerTunnelHost(host: TunnelHost)
    @JvmStatic external fun unregisterTunnelHost()
}
class AndroidNetworkHost(context: Context) : NetworkHost   // ConnectivityManager, ACCESS_NETWORK_STATE
data class VpnSpec(...) { companion object { fun parse(json: ByteArray): VpnSpec } }
object VpnInterfaces {
    fun establish(service: VpnService, specJson: ByteArray,
                  configure: (VpnService.Builder) -> Unit = {}): Established   // (descriptor, error)
    fun ownVpnNetworks(context: Context): List<Network>
    fun waitVpnNetworkGone(context: Context, timeoutMs: Int): Boolean
}
```

`PhpvpnVpnService` (WP05) má být tenký adaptér jako testovací `TestVpnService`: `establish` =
`VpnInterfaces.establish(this, specJson) { it.setConfigureIntent(...) }`, `waitVpnNetworkGone` =
`VpnInterfaces.waitVpnNetworkGone(this, timeoutMs)`. Kotlin deskriptor nikdy nezavírá.

C++ (WP04, `nativeStart`/`nativeStop`):

```cpp
AndroidVpnContext::Options options;
options.packageName = <packageName z Kotlinu>;            // addDisallowedApplication
auto context = std::make_shared<AndroidVpnContext>(options);
attachRegisteredVpnHosts(*context);                         // jni_vpn_bridge.h
AndroidVpnContext::install(context);                        // před ServiceRuntime::start
...
AndroidVpnContext::uninstall();                             // po ServiceRuntime::stop
```

### G. Testy

- `client/tests/android/fake_vpn_hosts.h` (falešní hosté, JSON builder), `test_vpn_context.cpp`,
  `test_netconfig_android.cpp`, `test_tunnel_android.cpp` (přenosné), `test_clock_android.cpp`,
  `test_tunnel_android_fd.cpp` (Linux a Android), `test_log_sink_android.cpp`, `test_tls_trust_android.cpp`
  (jen Android, TLS jen s WP01 deps). Seznamy v `client/tests/android/platform_tests.cmake`.
- `phpvpn-android-host-tests` (Windows, Linux; na Linuxu label `android;linux`, takže ho CI
  `-L "portable|linux"` spouští) kompiluje Android zdroje přímo proti `phpvpn-core` bez platformní knihovny
  hosta. Na Androidu běží stejné případy jako shell (`phpvpn-android-platform-tests`) i v sandboxu
  (`libphpvpn-android-tests.so`, filtr `Android`).
- Nový test `StatusContract.AndroidMissingVpnConsentKeepsItsOwnCode` v přenosné sadě.
- Instrumentované testy modulu `bridge` (`src/androidTest`): `VpnPlatformTest` (T-A01, T-A01b, T-A13, chybějící
  služba, recovery, registrace nativních metod produkční knihovny), `AndroidPlatformNativeTest` (T-N02 a T-N04
  v sandboxu), `TestVpnService`, `ConsentActivity`, `ShellUser`, `VpnTestLibrary`. JVM test
  `bridge/src/test/.../VpnContractTest.kt` drží stejný text obou JSON kontraktů jako C++ testy.
- `uiautomator 2.4.0` přidán do `libs.versions.toml` (chyběl) jako `androidTestImplementation` modulu `bridge`.

### Nový chybový kód

`tunnel_permission_missing` ve skupině `tunnel_*` (`error.h`, `error.cpp`), neobnovitelný, popis „The
permission to create the VPN interface is missing or was revoked.“. `SafeError::fromConnection` ho ve stavu
připojení vrací pod vlastním veřejným kódem `tunnel_permission_missing` (ne `tunnel_start_failed`), aby UI
mohlo ukázat obrazovku 37. Desktop ho nikdy nevytvoří. Zápis do slovníku IPC v client.md patří WP13.

## Příkazy

```powershell
# Windows host (VS 2026 x64, MSVC /W4 /WX), čerstvý strom
cmake -S client -B work/build/wp03-win-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON `
  -DCMAKE_C_COMPILER=cl.exe -DCMAKE_CXX_COMPILER=cl.exe
cmake --build work/build/wp03-win-debug --target phpvpn-android-host-tests phpvpn-core-tests phpvpn-client-tests
work/build/wp03-win-debug/tests/phpvpn-android-host-tests.exe

# NDK přímo (x86_64, RelWithDebInfo, clang r28c, -Werror)
cmake -S client -B work/build/wp03-android-x86_64 -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo `
  -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake -DANDROID_ABI=x86_64 `
  -DANDROID_PLATFORM=android-34 -DANDROID_STL=c++_static -DPHPVPN_BUILD_TRAY=OFF -DBUILD_TESTING=ON `
  -DPHPVPN_ANDROID_NATIVE_TESTS=ON -DPHPVPN_ANDROID_DEPS=D:/git/phpvpn/work/android-deps/openssl-3.5.9-curl-8.23.0
cmake --build work/build/wp03-android-x86_64 --target phpvpn-android phpvpn-android-tests `
  phpvpn-android-platform-tests phpvpn-core-tests
client/tools/android/run-native-tests.ps1 -Serial emulator-5554 -ApiLevel 34 -SkipBuild `
  -BuildDir work/build/wp03-android-x86_64 -Tests phpvpn-android-platform-tests,phpvpn-core-tests
llvm-nm -D --defined-only libphpvpn-android.so

# Gradle (JAVA_HOME=D:/androidstudio/jbr, local.properties se sdk.dir a phpvpn.androidDeps)
./gradlew assembleDebug assembleRelease testDebugUnitTest
./gradlew :bridge:connectedDebugAndroidTest          # ANDROID_SERIAL omezí běh na jeden emulátor

# předpoklad kontroly HTTPS v T-A01b: sonda WP01 jako uid 2000
cmake -S client/tests/android/tls_probe -B work/build/tls-probe-x86_64 ... ; cmake --build ...
adb -s <serial> push work/build/tls-probe-x86_64/tls_probe /data/local/tmp/phpvpn-tls-probe

# T-A13 na emulátoru (google_apis obraz API 34, root)
adb -s emulator-5554 root
adb -s emulator-5554 shell ip addr add 10.99.99.17/24 dev wlan0
./gradlew :bridge:connectedDebugAndroidTest `
  "-Pandroid.testInstrumentationRunnerArguments.class=phpvpn.android.bridge.VpnPlatformTest#preflightRefusesAnUnderlyingAddressInsideTheVpnPrefix"
adb -s emulator-5554 shell ip addr del 10.99.99.17/24 dev wlan0; adb -s emulator-5554 unroot
```

Linux (WSL Ubuntu 24.04, GCC 13.3.0, CMake 3.28.3; strom `rsync` do `~/phpvpn-wp03`), přesně podle
`client-linux.yml`: `cmake --preset linux-release -DPHPVPN_BUILD_PRIVILEGED_TESTS=OFF`,
`cmake --build --preset linux-release`, `ctest ... -L "portable|linux"`, `ctest ... -R phpvpn-logger-tests`
a stejné binárky ještě jednou jako `nobody` (`setpriv --reuid=65534`).

### Procedura T-A01 a T-A01b

Instrumentovaný test `VpnPlatformTest.virtualInterfaceCarriesOnlyTheVpnPrefix` (bez ručního zásahu kromě
`adb push` sondy):

1. Souhlas: `ConsentActivity` spustí `VpnService.prepare()` intent přes `startActivityForResult`, UiAutomator
   klikne na `android:id/button1` („OK“) systémového `ConfirmDialog`, test čeká, až `prepare()` vrátí null.
2. Navázání `TestVpnService` (vlastní akce bind), kontroly T-A01b jako uid 2000 bez VPN.
3. `VpnTestLibrary.vpnStart`: produkční `AndroidNetworkHost` a JNI most, kontext, továrny platformy a
   `TunnelSession::start` (10.99.99.2/24, routa 10.99.99.0/24) na nativním vlákně.
4. Kontrola VPN sítě přes ConnectivityManager: adresy, routy, DNS, MTU, kernel rozhraní, aplikace sama zůstává
   na podkladové síti.
5. Nativní responder čte `FdTunnel`, počítá pakety podle cíle a odpovídá na ICMP echo do prefixu.
   `ping 10.99.99.3` jako uid 2000 (`UiAutomation.executeShellCommandRwe`, stejný uid a SELinux doména jako
   `adb shell`) – nejdřív sondy po jedné do první odpovědi, potom měřené `ping -c 3`.
6. Kontroly T-A01b znovu s VPN, zastavení responderu (do tunelu nesmí přijít nic mimo prefix a žádný IPv6
   paket na globální adresu), `TunnelSession::stop`, měření zániku VPN sítě, kontroly po uvolnění.

Kontroly jako uid 2000: `dns` (`ping` na www.google.com vyřeší jméno; ICMP do internetu emulátor nepropouští),
`dnsfresh` (pokaždé nové jméno `wp03-<čas>-<fáze>.1-2-3-4.sslip.io` → 1.2.3.4, mimo cache resolveru),
`tcp443` (`echo | nc -w 5 www.google.com 443`), `https` (sonda WP01 `phpvpn-tls-probe` s ověřením systémovými
CA: DNS, TCP a TLS jiného UID), `ipv6` (`ping6 fec0::2`, router emulátoru; IPv6 do internetu emulátor
nemá), `lan` (`ping 10.0.2.2`), `lantcp` (TCP na adb server hostitele za 10.0.2.2:5037). HTTP request z
instrumentovaného testu by šel pod UID aplikace, která je z VPN vyloučená, proto se nepoužívá.

## Výsledky

| Test | Výsledek | Poznámka |
|---|---|---|
| T-N02 host Windows (`phpvpn-android-host-tests`, MSVC Debug /W4 /WX) | PASS | 38/0 |
| Windows `phpvpn-core-tests` a `phpvpn-client-tests` | PASS | 304/0 a 470/0 (nový test stavu, katalog chyb Wizardu přes všechny kódy) |
| Linux build `linux-release` (GCC 13.3, `-Werror`, tray) po merge `android` | PASS | bez varování; `transport_curl.cpp` s hookem |
| `ctest -L "portable\|linux"` (WSL) | PASS | 3/3: `phpvpn-core-tests` 304, `phpvpn-android-host-tests` 43 (38 + 5 POSIX), `phpvpn-linux-tests` 162 |
| `ctest -R phpvpn-logger-tests` | PASS | 1/1 |
| Totéž jako `nobody` | PASS | 43/0, 304/0, 162/0 |
| macOS build (Apple clang) | NOT RUN | bez macOS hosta a bez pushe do CI; inspekcí: macOS překládá z WP03 jen `transport_curl.cpp`, hook je prázdná funkce bez nové hlavičky |
| NDK x86_64 (`phpvpn-android`, `-tests`, `-platform-tests`, `phpvpn-core-tests`, `-Werror`) | PASS | clang 19 (r28c) |
| Gradle `assembleDebug assembleRelease testDebugUnitTest` (arm64-v8a, x86_64, app i bridge) | PASS | JVM `VpnContractTest` 3/0 |
| T-N02 na zařízení jako shell (`phpvpn-android-platform-tests`) | PASS | 50/0 na API 34, 35 i 36; `phpvpn-core-tests` 304/0 (s kryptografickou podmnožinou) |
| T-N02 v sandboxu aplikace (`untrusted_app`) | PASS | 50/0 na API 34, 35, 36 |
| PosixNativeTest (WP02, po merge) v sandboxu | PASS | 91/0 na API 34, 35, 36 |
| **T-A01** API 34 (`phpvpn-api34`) | **PASS** | viz tabulka níže |
| **T-A01** API 35 (`phpvpn-api35`) | **PASS** | |
| **T-A01** API 36 (`phpvpn-api36`) | **PASS** | |
| **T-A01b** API 34, 35, 36 | **PASS bez záložní cesty** | všech 7 kontrol beze změny před, během i po VPN; DNS jiných aplikací funguje bez `addDnsServer` |
| **T-A13** emulátor API 34 (adresa 10.99.99.17/24 na wlan0 jako root) | **PASS** | `error netconfig_preflight_conflict (an address of the VPN prefix already exists on another interface)`, žádné `establish()` |
| T-A13 na úrovni snapshotu (host, emulátor) | PASS | Wi-Fi adresa v prefixu, podkladová routa do prefixu, neaktivní mobilní síť, cizí VPN v prefixu se ignoruje |
| T-A13 na API 36 (plán) | NOT RUN | obraz `google_apis_playstore` nemá `adb root`, adresu do podkladové sítě nelze přidat; proveden na API 34 |
| T-N04 v aplikaci (CA store a HTTPS jako `untrusted_app`) | PASS | API 34: 134 certifikátů, API 35: 145, API 36: 149, vše z APEX; google.com ověřeno, `expired` a `wrong.host` odmítnuté certifikátem (curl 60), `trustAny` projde |
| Chybějící TunnelHost na zařízení | PASS | `tunnel_adapter_failed (the VPN service is not running)`; bez souhlasu `tunnel_permission_missing` |
| Recovery journalu mrtvého procesu na zařízení | PASS | `ok journalRemoved=true` s reálným snapshotem |
| Registrace `VpnHosts` v produkční knihovně | PASS | `JNI_OnLoad` registruje i hosty, volání bez výjimky |
| Export symbolů `libphpvpn-android.so` | PASS | release arm64-v8a 596 576 B a x86_64 593 424 B: definovaný dynamický symbol jen `JNI_OnLoad`; NEEDED `liblog.so libm.so libdl.so libc.so`; 4× LOAD 0x4000 |
| Účinek `--exclude-libs` | PASS | testovací knihovna obsahuje 2 604 symbolů `curl_`/`SSL_`/`EVP_`, v `.dynsym` 0 |
| Reálné zařízení (arm64) | NOT RUN | žádné zařízení; arm64 jen překlad |
| CI `client-android.yml`, `client-linux.yml` | NOT RUN | bez pushe |

### T-A01 podrobně (poslední běh)

| | API 34 | API 35 | API 36 |
|---|---|---|---|
| Souhlas | dialog přijat UiAutomatorem | stejně | stejně |
| `vpnStart` | `ok identity=0x414e…` | stejně | stejně |
| VPN síť (ConnectivityManager) | `tun0`, adresy `[10.99.99.2/24]`, routy `[10.99.99.0/24]`, DNS `[]`, MTU 1500, `NOT_METERED` | stejně | stejně |
| Kernel rozhraní | `tun0`, MTU 1500, up | stejně | stejně |
| Aplikace sama na VPN | ne (aktivní síť bez `TRANSPORT_VPN`) | ne | ne |
| Kontrola výchozí routy a DNS po `establish` | prošla | prošla | prošla |
| Sondy do první odpovědi (`ready`) | 1 | 0 | 1 |
| `ping -c 3 10.99.99.3` jako uid 2000 | 3/3 odpovědí přes tunel | 3/3 | 3/3 |
| Responder | 4 echo požadavky, 4 odpovědi, 0 mimo prefix, 0 IPv6 na globální adresu, 6 IPv6 link-local/multicast | stejně | stejně |
| `TunnelSession::stop` | 180 ms, journal smazán | 142 ms | 178 ms |
| VPN síť zmizela | po 184 ms | po 161 ms | po 183 ms |

### T-A01b podrobně

| Kontrola (uid 2000) | API 34 před / s VPN / po | API 35 | API 36 |
|---|---|---|---|
| `dns` | ok / ok / ok | ok / ok / ok | ok / ok / ok |
| `dnsfresh` (nové jméno) | ok / ok / ok | ok / ok / ok | ok / ok / ok |
| `tcp443` | ok / ok / ok | ok / ok / ok | ok / ok / ok |
| `https` (ověřený certifikát) | ok / ok / ok | ok / ok / ok | ok / ok / ok |
| `ipv6` (`fec0::2`) | ok / ok / ok | ok / ok / ok | ok / ok / ok |
| `lan` (`10.0.2.2`) | ok / ok / ok | ok / ok / ok | ok / ok / ok |
| `lantcp` (`10.0.2.2:5037`) | ok / ok / ok | ok / ok / ok | ok / ok / ok |
| DNS dotazy v tunelu | 0 | 0 | 0 |

Závěr spiku: premisa „virtuální síťovka“ platí. Bez `addDnsServer` resolver Androidu pro aplikace pod VPN
použije DNS podkladové sítě (VPN bez DNS serverů), IPv6 a LAN jdou mimo tunel díky `allowFamily(AF_INET6)`
a jediné routě prefixu. **Záložní cesta (`addDnsServer` s DNS aktivní sítě) není potřeba a není zapnutá**;
pole `dnsServers` ve spec zůstává prázdné a je připravené jen pro případ, že by ji vyžadovalo reálné
zařízení (T-A04, WP11).

## Zjištění

- `ConfirmDialog` zaznamená souhlas jen pro volajícího spuštěného přes `startActivityForResult`; intent
  z `VpnService.prepare()` spuštěný přes `startActivity` z instrumentace dialog hned zavře. WP05 (obrazovka
  03) musí použít Activity Result API.
- `VpnService.prepare()` má vedlejší účinek: pokud aplikace souhlas má a VPN slot drží jiná aplikace, převezme
  ho (zavře cizí VPN). `isConsentGranted()` se proto volá jen v `loadAndVerify` (Connect) a z BootReceiveru,
  ne periodicky.
- Kernel pošle do `tun0` po vzniku 6 IPv6 paketů (MLD reporty na `ff02::16`, router solicitation na
  `ff02::2`), protože rozhraní dostane link-local adresu. Aplikace k tomu nemá oprávnění (`addr_gen_mode` jako
  na Linuxu nastavit nejde). Vstupní pracovník jádra je zahodí jako neplatné pakety, takže statistika ukáže
  po připojení několik zahozených paketů. Neškodné, otevřený bod pro WP08 (zobrazení) nebo jádro.
- Per-uid routovací pravidla netd se uplatní až chvíli po tom, co ConnectivityManager VPN síť ohlásí
  (API 34 a 36: první sonda do 1 s nedorazila). Pro klienta bez dopadu (bootstrap proběhl před `establish`,
  peer provoz začne později, TCP retransmituje); test proto čeká na první odpověď.
- `LinkProperties.getStackedLinks()` (464XLAT) není veřejné API, snapshot obsahuje jen základní link sítě.
  Na IPv6-only síti s clat tak snapshot nemá IPv4 výchozí routu; baseline i kontrola jsou konzistentní,
  preflight clat adresu nevidí.
- `ConnectivityManager.getAllNetworks()` je od API 31 deprecated, ale stále jediný seznam bez callbacku;
  aplikace vyloučená z VPN v něm svou VPN síť vidí (ověřeno, `ownerUid` je vlastníkovi viditelný).
- badssl.com občas resetuje TLS handshake (curl 35, jako ve WP01); test proto reset opakuje a odmítnutí
  certifikátu ověřuje přímo curl kódem 60.
- toybox 0.8.9 (API 34) nemá `nc -z`.

## Odchylky od plánu a zadání

- Kotlin registrace je `VpnHosts.registerNetworkHost/registerTunnelHost/unregisterTunnelHost` podle zadání
  WP03, ne `NativeBridge.registerNetworkHost` z textu plánu.
- `TunnelHost` má třetí metodu `lastEstablishError()` (zadání připouští druhou metodu nebo pár).
- `VpnSpec` navíc nese `allowIpv6` a `metered` (výše zdůvodněné); `dnsServers` je rezervované pro záložní
  cestu.
- `tunnel_permission_missing` dostal i vlastní veřejný kód ve stavu připojení (`status_snapshot.cpp`), jinak by
  ho UI nerozlišilo od `tunnel_start_failed`. Jediná změna `core/runtime` mimo `error.*`.
- DNS ve snapshotu obsahuje i IPv6 servery v pořadí systému (Linux: seřazené IPv4).
- Host testy na Windows nezahrnují hodiny a FdTunnel (`CLOCK_BOOTTIME`, `eventfd`); ty běží na Linuxu
  a Androidu. `vpn_context.cpp` má kvůli Windows testům větev `_close`.
- T-A13 na emulátoru proběhl na API 34 (root), ne na API 36 z plánu.
- Jediný nový soubor v `bridge/src/test` (`VpnContractTest.kt`); WP06 tam pracuje také, sdílený soubor jsem
  neměnil.
- `ConsentActivity` (jen androidTest) navíc oproti zadání, protože systémový dialog jinak souhlas neuloží.
- Incident: poslední celý běh `connectedDebugAndroidTest` bez `ANDROID_SERIAL` proběhl i na emulátoru
  `emulator-5560` (`phpvpn-api36-wp08`), který mezitím spustil jiný agent. Testovací APK se nainstaloval,
  krátce vytvořil VPN a po běhu se odinstaloval; na emulátoru nezůstal balíček ani `tun0`. Všechny
  předchozí běhy byly omezené přes `ANDROID_SERIAL` nebo běžely, když 5560 neexistoval.

## Otevřené body pro WP04 a WP05

- WP04: vytvořit kontext v `nativeStart` podle ukázky výše (balíček aplikace, `attachRegisteredVpnHosts`,
  `install`), `uninstall` v `nativeStop`; registrovat `AndroidNetworkHost` přes `VpnHosts` před `nativeStart`;
  `Logger::configureFile` + `installLogcatSink()`; produkční knihovna zatím netáhne transport ani crypto
  (`--exclude-libs` je připravené).
- WP04: `TunnelSessionSettings` na Androidu padá do linuxové větve (`phpvpn`, GUID
  `{3f6c2a91-…}`); plánovaný GUID `{c2e5a8d1-7b4f-4e2a-8d3c-6f1b9e0a4d27}` patří do `android_host`.
- WP05: `PhpvpnVpnService` jako tenký adaptér nad `VpnInterfaces` (`setConfigureIntent` přes `configure`),
  souhlas přes Activity Result API, `registerTunnelHost` v `onCreate`, `unregisterTunnelHost` v `onDestroy`.
  Zánik služby za běhu tunelu: `closeAdapter` pak zavře deskriptor bez čekání (jen varování v logu).
- WP05/WP11: `isAlwaysOn`/lockdown a chování `prepare()` vůči jiné VPN (T-A09, T-A15).
- Statistika zahozených neplatných paketů po připojení (IPv6 housekeeping tun0), viz Zjištění.
- WP13: `tunnel_permission_missing` do slovníku IPC v client.md, Android sloupec do tabulky rozdílů
  (metrika rout 0, identita `0x414E…`, CA store, logcat, BOOTTIME).
