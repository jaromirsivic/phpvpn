# WP11: testovací matice na emulátorech API 34, 35 a 36, end-to-end scénáře a měření, report 2026-11-01

Výsledek kroku 11 podle [plan-android-v001.md](../../../Plans/2026-10-31-09-40-08/plan-android-v001.md)
(tabulka „Testy“, „Rizika“). Všechny výsledky jsou z emulátorů x86_64 na Windows hostu (WHPX), z JVM a z Windows
klienta na hostiteli jako druhého peeru. **Žádný výsledek není z reálného zařízení**; položky, které reálné zařízení
vyžadují, jsou níže jako NOT RUN s přesným postupem pro uživatele.

Větev `worktree-agent-a9f759c716dec10e0` nad `android` 867de95 (všechny WP kromě WP11 a WP12). Commity: `fc6ce6c`
(instrumentovaný test `RealRelayTest` a debug háčky), `b388832` (hostitelské skripty a README), `cfa2e7a` a `7ef043a`
(oprava flaky Wizard testů), `5528863` (oprava výchozího příkazu zastavení relay) a commit s tímto reportem. Kód aplikace ani nativní kód se neměnil; změny jsou jen v testech, v debug-only přijímači příkazů
a ve skriptech.

## Prostředí

| Součást | Stav |
|---|---|
| Emulátory | `phpvpn-api34` (`emulator-5554`, google_apis, Android 14), `phpvpn-api35` (`emulator-5558`, google_apis, Android 15), `phpvpn-api36` (`emulator-5556`, google_apis_playstore, Android 16); všechny x86_64, 4 GB RAM, ABI `x86_64,arm64-v8a` (ARM přes překlad emulátoru) |
| Relay | nginx + PHP 8.5.9 na hostiteli (`D:\nginx\start.bat`), emulátor ho vidí jako `10.0.2.2`. Pro každý emulátor vlastní relay v profilu `wizard-default` (`/wp11/api34/`, `/wp11/api35/`, `/wp11/api36/phpvpn.php`, server 1.0.3, buffer mimo web root, [deploy-wp11-relays.ps1](deploy-wp11-relays.ps1)), kopie se `SERVER_VERSION 2.0.0` (`/wp11/v2/phpvpn.php`) pro T-A14 a stávající `/phpvpn.php` (server 1.0.2) pro peer testy. Relay `php -S` s proxy z WP04 nebyl potřeba. |
| Druhý peer | Windows klient 1.0.2 (`C:\Program Files\phpvpn\1.0.2`), služba běží, `phpvpn.exe connect` funguje bez elevace; `10.99.99.2` přes `http://127.0.0.1/phpvpn.php`, kontrakty pro `10.99.99.3` a `.4`. Emulátor se připojuje jako `.3` (API 34, 36) nebo `.4` (API 35) se stejným klíčem kontraktu ([make_peer_config.py](make_peer_config.py)). Linuxový klient ve WSL nebyl potřeba. |
| Nepřístupnost relay | T-A08 a T-A16 b až d přes [relay_block_proxy.py](relay_block_proxy.py) (blokovací soubor, relay se jednomu emulátoru jeví jako nedostupný, nginx běží dál); T-A08 jednou i se skutečným zastavením nginx (`stop.bat`). |
| Build | Gradle 9.8.2, JBR 25, `:bridge:testDebugUnitTest :app:testDebugUnitTest :app:assembleDebug :app:assembleDebugAndroidTest :bridge:assembleDebugAndroidTest`; NDK r28c pro nativní testy. |

Spouštění: instrumentované moduly přes [run-instrumented.sh](run-instrumented.sh) / [run-matrix.sh](run-matrix.sh)
(stejné kroky jako `connectedDebugAndroidTest`: odinstalace, `install -t -g`, `am instrument -w -r`, odinstalace,
jen bez Gradle, aby tři emulátory běžely souběžně každý proti svému relay). Kontrola shody s Gradle:
`ANDROID_SERIAL=emulator-5558 ./gradlew :bridge:connectedDebugAndroidTest` dal stejný výsledek (50 testů, 1 přeskočený).
Postup a skripty: [README.md](README.md).

## Matice

PASS = prošlo, FAIL = selhalo, NOT RUN = nespuštěno (důvod). Čísla v buňkách jsou počty testů nebo kontrol.

### Automatické sady

| Sada | Obsah | API 34 | API 35 | API 36 |
|---|---|---|---|---|
| T-J01 `:bridge:testDebugUnitTest`, `:app:testDebugUnitTest` | JVM | PASS 109/109 a 183/183 (hostitel, nezávislé na API) | | |
| T-N01 `run-native-tests.ps1` `phpvpn-core-tests` (s OpenSSL a curl z WP01) | přenosná sada vč. kryptografické podmnožiny | PASS 304/0 | PASS 304/0 | PASS 304/0 |
| T-N01 `phpvpn-logger-tests` | | PASS 2/0 | PASS 2/0 | PASS 2/0 |
| T-N02 `phpvpn-android-platform-tests` na emulátoru | `VpnSpec`, `AndroidVpnContext`, snapshot, tunel, TLS trust, hodiny | PASS 50/0 | PASS 50/0 | PASS 50/0 |
| T-N02 hostitelská část (`phpvpn-android-host-tests`) | | NOT RUN ve WP11: nativní kód se od WP05 nezměnil, WP05 PASS (Windows MSVC 38/0, WSL) | | |
| arm64-v8a: tytéž tři nativní sady pod ARM překladem emulátoru | | | PASS 304/0, 2/0, 50/0 | |
| `:bridge:connectedDebugAndroidTest` celkem | | PASS 49, SKIP 1 | PASS 49, SKIP 1 | PASS 49, SKIP 1 |
| – T-N03 `PosixNativeTest` | publikace konfigurace v app sandboxu | PASS | PASS | PASS |
| – T-N04 `AndroidPlatformNativeTest` | systémové CA, TLS, `trustAny` | PASS 2 | PASS 2 | PASS 2 |
| – T-A01 a T-A01b `VpnPlatformTest` | `establish`, paket, DNS/HTTPS/IPv6/LAN jiného UID | PASS | PASS | PASS |
| – T-A13 preflight konflikt | adresa `10.99.99.17/24` na `wlan0` (root) | PASS (samostatný běh s `adb root`) | PASS (samostatný běh s `adb root`) | NOT RUN: obraz `google_apis_playstore` nemá `adb root`, adresu nelze přidat (v celkovém běhu SKIP na všech API) |
| – WP04 IPC a JNI (`IpcDispatchTest` 9, `NativeRuntimeTest` 6, JNI modely 27) | | PASS 42 | PASS 42 | PASS 42 |
| `:app:connectedDebugAndroidTest` celkem (vč. nového `RealRelayTest`), závěrečný běh | | PASS 79, SKIP 1 | PASS 79, SKIP 1 | PASS 79, SKIP 1 |
| – T-U01 `MainFlowsTest` 16, `WizardFlowTest` 15 | navigace, Back, 32, FLAG_SECURE | PASS 31 | PASS 31 | PASS 31 |
| – T-U02 `Wp08ScreenshotTest` 24, `WizardScreenshots` 1, snímky WP10 | 412, 360, 320 dp, písmo 100 % a 200 % | PASS (263 PNG) | PASS (263 PNG) | PASS (263 PNG), viz pozn. 2 |
| – T-U03 `ContractsUiTest` | | PASS 5 | PASS 5 | PASS 5 |
| – T-U04 `EditorUiTest` 5, `WizardSaveOutcomeUiTest` 2 | | PASS 7 | PASS 7 | PASS 7 |
| – WP05 `VpnServiceTest` (T-A02, T-A05, T-A09, T-A11 ×2, Exit 35) | skutečná služba, systémový dialog | PASS 6, SKIP 1 | PASS 6, SKIP 1 | PASS 6, SKIP 1 |
| – WP11 `RealRelayTest` (T-A06 ×3, T-A14, T-A07) | skutečná aplikace přes UiAutomator proti relay | PASS 5 | PASS 5 | PASS 5 |
| WP05 `service-lifecycle-tests.sh` (T-A10, T-A16, T-A12 broadcast, Always-on) | hostitel + adb | PASS 40/0 | PASS 40/0 | PASS 32/0 (bez T-A12 broadcast, viz níže) |

Poznámka 1: průběh běhů `:app` viz „Závěrečné běhy“ níže.
Poznámka 2: snímky T-U02 se generují a stahují (`run-instrumented.sh` → `<log dir>/screenshots`); ruční porovnání
všech 49 obrazovek s `assets/android-en/previews/full` je NOT RUN (ruční krok, viz „Postupy pro uživatele“).
Namátkově porovnána 01 Connected při 412 dp na API 34: rozložení, texty a barvy odpovídají návrhu.
SKIP v `VpnServiceTest` je `a_notificationPermissionDenied…`: APK je instalované s `-g` (stejně jako Gradle), takže
odmítnuté `POST_NOTIFICATIONS` nejde bez zabití procesu nasimulovat (WP05 ho ověřil na API 34).

### End-to-end scénáře

| ID | Obsah | API 34 | API 35 | API 36 |
|---|---|---|---|---|
| T-A02 | Connect přes systémový souhlas, čtyři kontroly, `tun0` | PASS | PASS | PASS |
| T-A03 | peer emulátor ↔ Windows klient: ping a TCP echo 1 KiB, 64 KiB, 1 MiB oběma směry | PASS 10/10 | PASS 10/10 (jako `.4`) | PASS 10/10 |
| T-A04 | DNS (i čerstvé jméno), TCP 443, HTTPS s ověřeným certifikátem, IPv6, LAN ping a TCP jiného UID při skutečném spojení; routy (internet, LAN, IPv6 mimo `tun0`, `vpnCidr` do `tun0`); VPN síť bez DNS | PASS 12/12 | PASS 12/12 | PASS 12/12 |
| T-A04 | Wi-Fi vypnuté (mobilní data `eth0`) a znovu zapnuté při spojení | PASS | PASS | PASS |
| T-A05 | Disconnect, notifikace a síť zmizí | PASS | PASS | PASS |
| T-A05 | timeout Disconnect → 38 se skutečnou službou | NOT RUN: runtime timeout Disconnect nejde bez testovacího háčku vyvolat (pokryto `MainFlowsTest` a JVM, jako ve WP05) | NOT RUN | NOT RUN |
| T-A06 | Wizard 00 → 12 → 14 → 15 → 16 → 17 → 03 → 04 → 01 proti relay | PASS | PASS | PASS |
| T-A06 | špatný secret → 18 `crypto_tag_mismatch`, secret se neopakuje, „Correct the secret“ → 14 s chybou | PASS | PASS | PASS |
| T-A06 | špatná URL (HTTP 404) → chyba 12 s „HTTP status: 404“ → 30 (Response) → 31 (Request) | PASS | PASS | PASS |
| T-A07 | uložení kontraktu při aktivním spojení → 25 „Changes saved“ → Reconnect → `connected` s novou epochou, dokument obsahuje změnu | PASS | PASS | PASS |
| T-A08 | výpadek relay 20 s: `connected/reconnecting`, 05 v UI, návrat do 01 bez recovery | PASS (proxy) | PASS (proxy) | PASS s výhradou (nginx `stop.bat`): bez recovery a návrat do 01, ale výpadek 20 s je kratší než detekce odmítaných spojení (~26 s), engine během něj zůstal `running` |
| T-A08 | výpadek relay 120 s: plná recovery (`recovering`, `recoveryCount` +1), 05, návrat do 01 | PASS (proxy) | PASS (proxy) | PASS (nginx `stop.bat`) |
| T-A09 | `onRevoke` jinou VPN aplikací | PASS | PASS | PASS |
| T-A10 | `am kill` v `disconnected` a návrat | PASS 9/9 | PASS 9/9 | PASS 9/9 |
| T-A11 | zamítnutí souhlasu (37), odebraný souhlas → `tunnel_permission_missing` | PASS | PASS | PASS |
| T-A12 | `BOOT_COMPLETED` broadcastem (true, true bez souhlasu, false) | PASS 8/8 | PASS 8/8 | NOT RUN: broadcast smí poslat jen root, obraz API 36 nemá `adb root` |
| T-A12 | skutečný restart emulátoru (`adb reboot`), `autoConnectOnStartup: true` | NOT RUN: emulátory 5554 a 5558 nespustil tento agent a sdílí je jiní agenti (WP12), restart by jim přerušil práci; postup níže | NOT RUN (dtto) | PASS: `boot: autoConnectOnStartup is on, connecting`, `connected`, notifikace |
| Always-on | zapnutí v systémovém nastavení spustí službu a připojí | PASS 4/4 | PASS 4/4 | PASS 4/4 |
| T-A13 | preflight konflikt | PASS | PASS | NOT RUN (bez rootu) |
| T-A14 | server 2.0.0: Wizard 12 → „Incompatible server version“, „This app is version 1.0.3, the server is version 2.0.0.“; Connect → `error` `server_version_incompatible`, kontrola URL `failed`, žádný tunel | PASS | PASS | PASS |
| T-A15 | Always-on s lockdownem (manuální, automatizováno UiAutomatorem) | NOT RUN (plán: API 36 a zařízení) | **FAIL** (chyba B1) | **FAIL** (chyba B1) |
| T-A16 a | SIGKILL při `connected` | PASS (záložní větev, sticky restart nenastal) | PASS (dtto) | PASS (dtto) |
| T-A16 b, c, d | bez souhlasu, bez příznaku, limit tří restartů | PASS | PASS | PASS |
| T-A20 | Doze na emulátoru (`deviceidle force-idle`, 600 s, obrazovka vypnutá, `battery unplug`) | PASS (emulátor) | NOT RUN (stačí API 34 a 36) | PASS (emulátor) |
| T-A20 | Doze a `CLOCK_BOOTTIME` přes skutečný spánek CPU | NOT RUN: emulátor | NOT RUN | NOT RUN |
| T-A21 | paměť při plných frontách | NOT RUN | NOT RUN | měření na emulátoru (níže) |
| T-D01 | reálné zařízení | NOT RUN | NOT RUN | NOT RUN |

Počty kontrol T-A04: 7 kontrol jiného UID, 4 routy a peer po kontrolách (+ informační řádek DNS); přepnutí sítě má 2 kontroly.

### Závěrečné běhy `:app:connectedDebugAndroidTest`

| Běh | API 34 | API 35 | API 36 |
|---|---|---|---|
| 1. sada WP05 až WP10 (bez `RealRelayTest`), tři emulátory souběžně | INCOMPLETE 61/1 z 75 (restart adb serveru jiným procesem) | PASS 74, SKIP 1 | PASS 74, SKIP 1 |
| `RealRelayTest` samostatně | (v běhu 2) | PASS 5 | PASS 5 (po opravě testu, viz flaky) |
| 2. celá sada | PASS 79, SKIP 1 | PASS 77, **FAIL 2** (`WizardSaveOutcomeUiTest`, flaky) | PASS 79, SKIP 1 |
| 3. celá sada | PASS 79, SKIP 1 | PASS 78, **FAIL 1** (`WizardFlowTest.resetLoadsTheDefaultPlaceholder`, flaky) | – |
| 4. závěrečná celá sada po opravě flaky testů (`cfa2e7a`, `7ef043a`), tři emulátory souběžně | **PASS 79, SKIP 1** | **PASS 79, SKIP 1** | **PASS 79, SKIP 1** |

Smyčky po opravě na API 35: `EditorUiTest` + `WizardSaveOutcomeUiTest` 10× PASS, `EditorUiTest` + `WizardFlowTest`
+ `WizardSaveOutcomeUiTest` 5× PASS. Shoda s Gradle: `ANDROID_SERIAL=emulator-5558 ./gradlew
:bridge:connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.phpvpnServerUrl=…` PASS 49, SKIP 1
(10 tříd, stejné jako skript).

## Měření

Všechna čísla jsou z emulátorů na jednom hostiteli se single `php-cgi` relay; jde o funkční orientaci, ne o výkon
na zařízení.

### Připojení a přepojení

| Měření | API 34 | API 35 | API 36 |
|---|---|---|---|
| T-A06: od potvrzení dialogu 03 do `connected` (4× `success`) | 1564 ms | 1525 ms | 1235 ms |
| T-A07: Reconnect z 25 do `connected` s novou epochou | 1800 ms | 1997 ms | 1544 ms |
| Connect přes debug přijímač do `connected` (skript, poll 1 s) | 2 až 4 s | 2 až 4 s | 2 až 5 s |
| T-A12: `adb reboot` → `sys.boot_completed` → `connected` | | | 68 s → 101 s |
| T-A16 b, c, d: restart procesu po SIGKILL bez tunelu (b, c, d1 až d4; backoff Androidu) | 2, 2, 1, 5, 17, 65 s | 7, 3, 3, 5, 17, 65 s | 2, 2, 4, 5, 16, 65 s |

### T-A03 peer provoz (emulátor ↔ Windows klient 1.0.2 přes `/phpvpn.php`)

Druhý běh každého API (čas na emulátoru z `/proc/uptime`; první běh měřil přes `date +%s%N`, které na API 35 dalo
záporný čas, na API 34 a 36 hodnoty v podobném rozsahu kromě 1,3 s pro 1 MiB na API 36).

| Měření | API 34 (`.3`) | API 35 (`.4`) | API 36 (`.3`) |
|---|---|---|---|
| ping emulátor → peer, 10 paketů: ztráta, RTT min/avg/max | 0 %, 169/529/2325 ms | 0 %, 133/192/309 ms | 0 %, 98/243/976 ms |
| ping peer → emulátor, 10 paketů: ztráta, RTT min/avg/max | 0 %, 154/191/220 ms | 0 %, 139/175/204 ms | 0 %, 156/195/236 ms |
| TCP echo emulátor → peer 1 KiB / 64 KiB / 1 MiB (round trip) | 0,36 s / 0,84 s / 4,6 s (222 KiB/s) | 0,40 s / 0,93 s / 5,1 s (202 KiB/s) | 0,40 s / 0,89 s / 4,5 s (228 KiB/s) |
| TCP echo peer → emulátor 1 KiB / 64 KiB / 1 MiB | 0,40 s / 1,12 s / 5,7 s (181 KiB/s) | 0,37 s / 0,84 s / 4,7 s (218 KiB/s) | 0,39 s / 0,90 s / 4,4 s (233 KiB/s) |
| první běh, TCP echo peer → emulátor 1 MiB | 5,5 s | 17,3 s | 6,0 s |

Data jdou dvakrát přes relay (tam i zpět). Rozptyl odpovídá pollingu: Windows klient 1.0.2 a relay 1.0.2 mají
`MAX_DATA_POLL_INTERVAL_MS` 3200, první paket po nečinnosti čeká až na další poll (maximum RTT 1 až 2,6 s). Pomalejší první běh na API 35 (17 s pro
1 MiB) proběhl souběžně s instrumentovanými testy API 34 na stejném jediném `php-cgi`.

### T-A04 přepnutí sítě

Wi-Fi vypnuto: výchozí síť `eth0` (mobilní data emulátoru), peer znovu dostupný za 7 až 8 s, stav poté
`connected/running` (v logu 0 až 1 řádek se slovem „recover“, `recoveryCount` se nesledoval). Wi-Fi zapnuto: peer dostupný za 9 až 10 s. VPN síť nemá
DNS servery (`DnsAddresses: [ ]`), DNS jiných aplikací jde přes podkladovou síť.

### T-A08 výpadek relay (proxy s blokovacím souborem, na API 36 zastavený nginx)

| Měření | API 34 (proxy) | API 35 (proxy) | API 36 (nginx `stop.bat`/`start.bat`) |
|---|---|---|---|
| 20 s: první `reconnecting` po výpadku | 1 s | 1 s | nenastalo během 20 s |
| 20 s: návrat do `connected/running` po obnovení | 7 s | 6 s | 20 s |
| 20 s: `recoveryCount` | 0 | 0 | 0 |
| 120 s: `reconnecting` po výpadku | 3 s | 3 s | 26 s (`lastError` zatím null) |
| 120 s: přechod do `recovering` (plná recovery, tunel uvolněn) | 97 s (`endpoint_unavailable_or_invalid`) | 95 s (dtto) | 120 s (`communication_timeout`) |
| 120 s: návrat do `connected/running` po obnovení | 6 s | 7 s | 21 s |
| 120 s: `recoveryCount` | 0 → 1 | 0 → 1 | 0 → 1 |

Během soft recovery zůstává tunel (`tunnelReady: yes`) a UI ukazuje 05 Reconnecting; během plné recovery je
`state=recovering`, `phase=idle`, `tunnelReady=no`. Proxy relay spojení přijme a hned zavře, klient výpadek pozná za
1 až 3 s. Zastavený nginx (spojení přes síť emulátoru odmítnutá nebo bez odpovědi) klient pozná až vypršením
komunikace (~26 s, `communication_timeout`) a po `start.bat` se vrací ~20 s (čas `start.bat` a dobíhající timeout),
takže 20s výpadek nginx engine překlene bez soft recovery. Obě chování odpovídají návrhu (timeout požadavku, ne
chyba); pro plán: „20 s“ v T-A08 ověřuje 05 jen s relay, který spojení přijímá. Windows klient na stejném nginx
prošel výpadkem také plnou recovery (`recoveryCount` 1) a připojil se sám.

### T-A20 Doze na emulátoru (600 s `deviceidle force-idle`, obrazovka vypnutá, napájení odpojené)

| Měření | API 34 | API 36 |
|---|---|---|
| stav během celé doby | `connected/running` | `connected/running` |
| data polly za 600 s | 2700 (4,5/s) | 2906 (4,8/s) |
| ping na peer každých 20 s | 0 ztrát, 180 až 1100 ms | 0 ztrát, 176 až 3000 ms |
| `connected` po `unforce` | 2 s | 1 s |
| první ping po `unforce` | 696 ms | 2993 ms |
| 401 v logu, recovery | 0, 0 | 0, 0 |
| wakelocky aplikace (`batterystats --charged`) | žádný vlastní wakelock aplikace | dtto |

Výklad: foreground služba VPN (`systemExempted`) není na emulátoru Doze omezena v síti a polling běží dál.
`force-idle` ale neuspí CPU hostitele, takže `CLOCK_BOOTTIME` a skutečný spánek se tím neověří (T-A20 na zařízení
NOT RUN). Pro R6 (wakelock) to znamená jen to, že síťová omezení Doze VPN službu neblokují; spotřebu a chování po
spánku musí změřit zařízení.

### T-A21 paměť při zátěži fronty (API 36)

Zátěž: čtyři `dd if=/dev/zero bs=1400 | nc -u` z `adb shell` (uid 2000) na peer `10.99.99.2` (UDP sink na
Windows), relay `/phpvpn.php`, dokument emulátoru s `runtime.maxQueuedPacketAgeMs: 600000` (jinak výchozích
5 s stáří paketu frontu vyprázdní dávno před 128 MiB), limity front výchozí (128 MiB na směr).

| Měření | 180 s zátěž | 600 s zátěž |
|---|---|---|
| PSS / RSS na začátku | 84,1 MiB / 211,7 MiB | 83,9 MiB / 211,3 MiB |
| maximum PSS / RSS | 130,7 MiB (+46,6) / 259,1 MiB (+47,5) | 170,4 MiB (+86,6) / 300,0 MiB (+88,8) |
| přečteno z `tun0` aplikací | 48 MiB (generátory skončily po ~80 s) | 291 MiB (~0,5 MiB/s) |
| odesláno na relay (serializovaná těla) | 64 MiB | 74 MiB |
| zahozeno jádrem na `tun0` (TX drop) | nesledováno | 61,4 mil. paketů |
| PSS 60 s po konci zátěže | 86,7 MiB | 150,2 MiB (fronta se dál vyprazdňuje) |
| proces, LMK, spojení | stejný PID, žádné `am_kill`, `connected` | stejný PID, žádné `am_kill`, `connected` |

Předběžný běh s výchozím `maxQueuedPacketAgeMs` (5 s) a 180 s zátěže: maximum PSS +13,4 MiB, po zátěži návrat
na výchozí hodnotu, proces i spojení beze změny.

Výklad: generátor vyrobí řádově 100 tisíc datagramů za sekundu, aplikace čte z `tun0` jen ~0,5 MiB/s (zbytek zahodí
jádro na frontě tun zařízení) a relay odebírá ~0,1 až 0,3 MiB/s, takže fronta roste o rozdíl. PSS se od ~330 s ustálil
na ~170 MiB (+87 MiB) a dál nerostl, přestože do aplikace vstoupilo 291 MiB a na relay odešlo 74 MiB. Emulátor má 4 GB
RAM a `MemAvailable` zůstal ~2,0 GB, LMK proces nezabil. Hodnota „plných front“ 128 MiB na směr se v paměti
procesu neprojevila jako +128 MiB; přesný obsah fronty klient veřejně nehlásí (status nemá velikost fronty), takže
nejde rozhodnout, zda se fronta zastavila na limitu, nebo jen na rovnováze přívodu a odvodu. Na zařízení s menší
pamětí (2 až 3 GB) může +90 MiB u procesu s foreground službou ještě projít, LMK pod tlakem jiných aplikací ale
emulátor neověří.

## Nalezené chyby

### B1: varování lockdownu chybí, když se lockdown zapne při běžící službě (T-A15)

- **Reprodukce (API 35 i 36):** aplikace s platnou konfigurací a souhlasem; Nastavení Androidu → Síť → VPN → ozubené
  kolo PHPVPN → zapnout „Always-on VPN“ (systém spustí službu, `Always-on start: connecting`, `connected`) → zapnout
  „Block connections without VPN“ → potvrdit „Turn on“. `settings get secure always_on_vpn_lockdown` = 1. V aplikaci
  Settings (10): řádek Always-on ukazuje „On, set in Android VPN settings“, ale varování „Block connections without VPN
  is on“ chybí (snímek `work/wp11/ta15-app-settings-emulator-5558.png`). V logu chybí
  `W/phpvpn: Always-on lockdown is enabled; phpvpn does not support it`. `dumpsys activity services` ukazuje
  `lastStartId=1`: systém po zapnutí lockdownu službě žádný nový start nepošle.
- **Po Disconnect a Connect** (nový `onStartCommand`) se v logu objeví
  `W/phpvpn: Always-on lockdown is enabled; phpvpn does not support it` a Settings 10 varování ukáže (PASS).
- **Diagnóza:** `PhpvpnVpnService.publishAlwaysOn()` čte `isAlwaysOn`/`isLockdownEnabled` jen v `onStartCommand`
  a při změně stavu notifikace (`updateNotification`). Android při přepnutí lockdownu pro už běžící Always-on balíček
  službu nerestartuje ani jí nepošle příkaz, takže `ServiceAlwaysOnStatus` drží `lockdown=false` až do příštího
  startu. Typické pořadí v nastavení (nejdřív Always-on, pak blokování) tak varování nikdy neukáže, dokud se uživatel
  nepřepojí. Oprava patří do WP05 (například číst `isLockdownEnabled` při každém pollingu stavu nebo při otevření
  Settings/About), ve WP11 se kód aplikace neměnil.
- Ostatní pozorování T-A15 (očekávané): v lockdownu vlastní provoz aplikace na relay prochází (Connect i přepojení
  fungují), ostatní aplikace (uid 2000) nemají internet ani přístup na `10.0.2.2:80`, protože phpvpn nesměruje
  výchozí trasu; text varování to popisuje.

### B2 (nález k rozhodnutí): Welcome 00 se po čisté instalaci nikdy neukáže

- **Reprodukce (API 36):** čistá instalace (`pm uninstall`, `install`), spuštění. Log:
  `I/phpvpn: service: config startup: default created`. První obrazovka je 02 „Ready to connect“ s „Configuration
  ready“, Server `http://127.0.0.1/phpvpn.php`, Virtual IP `10.99.99.2`, tlačítko Connect VPN.
- **Diagnóza:** nativní služba při startu volá `ConfigStore::ensureDefaultAtStartup()` (společné chování desktopu) a
  vytvoří výchozí dokument s identitou. `StartDestinationViewModel` otevírá 00 jen pro `availability == missing`, takže
  hlavní cesta plánu `00 → 12 …` začíná na Androidu jen tehdy, když soubor zmizí za běhu runtime (tak ji testuje
  `RealRelayTest`). Na telefonu je `127.0.0.1` vlastní zařízení, Connect z 02 po instalaci proto skončí chybou relay.
  Rozhodnutí, zda Android má vynechat vytváření výchozí konfigurace při startu (nebo 00 ukazovat i pro nezměněný
  výchozí dokument), patří do plánu; WP11 kód neměnil.

### Další pozorování (ne chyby)

- T-A16 a: sticky restart služby po SIGKILL s aktivním rozhraním nenastal ani jednou (API 34, 35, 36, po jednom
  pokusu), vždy proběhla záložní větev `ProcessEndNotice` (příznak smazán, `process_restarted`, text na 02). Shodné
  se zjištěním 1 WP05.
- Výchozí dokument má `communicationContracts: {}`; T-A07 proto kontrakt zapíná s klíčem, ne vypíná.
- `phpvpn.exe` (Windows klient 1.0.2) se připojí a odpojí bez elevace a s relay 1.0.2 si s Android klientem 1.0.3
  bez problému rozumí (stejná major verze).

## Flaky testy a infrastruktura

| Případ | Příčina | Řešení |
|---|---|---|
| `:app` běh na API 34 skončil po 62 ze 75 testů bez chyby testu | adb server na hostiteli restartoval jiný proces (`* daemon not running; starting now`), přerušil proud `am instrument` | opakovaný běh PASS 79/1; `run-instrumented.sh` nově hlásí `INCOMPLETE` místo falešného úspěchu |
| `RealRelayTest` první verze 0/5 | `UiObject2.setText` na pole Compose funguje bez klávesnice, následné `pressBack` proto navigovalo zpět; skryté pole na 15 při přechodu obrazovek | opraveno v testu (bez Back, čekání na pole s IP) |
| T-A21 první verze visela po zátěži | `pkill -f "nc -u …"` přes `adb shell` ztratil uvozovky | `pkill -x nc` |
| T-A03 čas 1 MiB echa na API 35 záporný | toybox `date +%s%N` není spolehlivé | `/proc/uptime` |
| `run-native-tests.ps1 -Tests a,b,c` po `-File` | čárkový seznam dorazí jako jeden řetězec | volat přes `-Command "& …"` (README) |
| **`WizardSaveOutcomeUiTest` (WP10) na API 35: 2 z 12 běhů FAIL** (`ComposeTimeoutException` na 14 po Verify na 12) | výpis sémantického stromu při selhání: obrazovka zůstala na 12, `wizard-check-url` odeslán 0×, okno zmenšené klávesnicí (kořen obsahu 1580 px z 2400). `performTextReplacement` otevře klávesnici a injektovaný dotyk na Verify dopadl během animace insetů vedle posouvajícího se tlačítka v patičce. Častější po `EditorUiTest` a pod zátěží tří emulátorů; samostatně prochází. | test čeká na povolené tlačítko a klikne sémantickou akcí `OnClick` (`WizardUiTestBase.clickWhenEnabled`), commit `cfa2e7a`; potom 10 z 10 běhů PASS. Chování aplikace se nemění (uživatel by klepnutí během animace prostě zopakoval). |
| výchozí `RELAY_STOP`/`RELAY_START` ve `wp11-scenarios.sh` | `eval` odstranil zpětná lomítka cesty `D:
ginx\stop.bat`, první běh T-A08 na API 36 proto nginx vůbec nezastavil (5 FAIL) | zdvojená lomítka; opakovaný běh je v tabulce |
| `D:
ginx\start.bat` hlásí „The Nginx/PHP stack could not be started.“ | `stack-control.ps1` hledá na `/phpvpn.php` značku `<h1>Server is running</h1>`, nasazený server 1.0.2 má jiný text stránky; nginx i `php-cgi` přesto běží (HTTP 200) | jen poznámka, mimo repozitář; skript T-A08 ověřuje relay sám přes HTTP |

Testy aplikace `VpnServiceTest`, `MainFlowsTest`, `WizardFlowTest`, `ContractsUiTest`, `EditorUiTest` a nativní testy
v WP11 neselhaly ani jednou.

## NOT RUN a postupy pro uživatele (reálné zařízení)

Žádné zařízení nebylo k dispozici. Postupy předpokládají debug APK z `client/android/app/build/outputs/apk/debug/`
(obsahuje `x86_64` i `arm64-v8a`), relay dostupný ze zařízení (nginx na hostiteli v LAN, např.
`http://<IP hostitele>/phpvpn.php`) a Windows klienta jako peer.

1. **T-D01 hlavní cesta a peer provoz na Android 14+ (arm64).** `adb install -r -g app-debug.apk`; spustit; protože
   výchozí konfigurace vznikne sama (B2), otevřít Settings → Setup wizard; 12 URL relay, 14 výchozí secret, 15 návrh IP
   (zapsat), 16 Finish, 17 Connect VPN, potvrdit 03, ověřit 01. Na Windows klientovi doplnit kontrakt pro IP telefonu
   se stejným klíčem jako v 20 (Settings → Communication contracts → peer), nebo spustit
   `client/tests/android/peer-traffic-tests.sh <serial> <url> C:/ProgramData/phpvpn/config.json <ip> <apk>
   "connect ta03 ta04 netswitch disconnect"` přes USB adb (skript sám vytvoří dokument telefonu). Totéž s Fedora
   klientem (`/etc/phpvpn/config.json` jako peer config). Přepnout Wi-Fi ↔ mobilní data při spojení a zapsat dobu do
   obnovení provozu.
2. **T-A20 na zařízení (R6, Doze, `CLOCK_BOOTTIME`).** Připojit, odpojit USB napájení (nebo `adb shell dumpsys battery
   unplug` přes Wi-Fi adb), vypnout obrazovku na 30 min. Z peeru pingovat telefon každých 30 s (zapsat zpoždění).
   Pak `adb shell dumpsys batterystats --charged io.github.jaromirsivic.phpvpn > ta20-batterystats.txt`,
   `adb logcat -d -s phpvpn > ta20.log`; ověřit: v logu žádné 401 (`grep -c 401`), nejvýš jedna recovery
   (`recoveryCount` z `adb shell am broadcast -n io.github.jaromirsivic.phpvpn/phpvpn.android.debug.DebugVpnCommandReceiver
   -a phpvpn.android.debug.STATUS_JSON` v logu `phpvpn-debug`), zpoždění prvního paketu po probuzení. Zopakovat
   s 10 min spánku bez USB a s výjimkou z optimalizace baterie (Settings → Battery optimisation). Alternativně
   `client/tests/android/wp11-scenarios.sh <serial> <url> <apk> ta20` s `TA20_IDLE_S=1800` a `CONFIG_FILE` z kroku 1.
3. **T-A16 pod skutečným tlakem paměti (LMK).** Připojit, přepnout do jiných náročných aplikací (hry, kamera, prohlížeč
   s mnoha kartami), dokud `adb shell dumpsys activity processes | grep phpvpn` neukáže konec procesu, nebo
   `adb logcat -b events | grep am_kill`. Zapsat, zda Android službu restartoval (`dumpsys activity services
   io.github.jaromirsivic.phpvpn`: `app=ProcessRecord`), zda R9 připojil (`reconnecting (restart 1)` v logu), nebo zda
   02 ukázala text o ukončení systémem (záložní větev).
4. **T-A21 na zařízení.** Stejně jako výše `wp11-scenarios.sh <serial> <url> <apk> ta21` s `CONFIG_FILE` z
   `make_peer_config.py … maxQueuedPacketAgeMs=600000` a `TA21_SECONDS=600`; zapsat maximum PSS/RSS, `MemAvailable`
   a `am_kill`.
5. **arm64-v8a aplikace.** Body 1 až 4 na arm64 zařízení; na emulátoru prošly jen nativní testy pod ARM překladem.
6. **T-A15 na zařízení:** jako B1 (Always-on, pak Block connections) a ověřit, zda se varování objeví; zapsat, zda
   výrobce systému chování mění.
7. **T-A12 se skutečným restartem na API 34 a 35:** `wp11-scenarios.sh emulator-5554 http://10.0.2.2/wp11/api34/phpvpn.php
   <apk> ta12reboot` (restartuje emulátor, proto jen když ho nikdo jiný nepoužívá).
8. **T-U02 ruční porovnání:** snímky v `work/wp11/logs-final2/api36/screenshots/{wp08,wp09,wp10}` (263 PNG na každé API) proti
   `assets/android-en/previews/full/NN-*.png` (412 dp), zkontrolovat i 320 dp a písmo 200 % (oříznutí, překryv
   patičky).

Další NOT RUN bez zařízení: T-A05 timeout → 38 se skutečnou službou (vyžaduje testovací háček v jádře), T-A12
broadcast a T-A13 na API 36 (bez rootu), T-N02 hostitelská část (beze změny od WP05), CI `client-android.yml`
(bez pushe).

## Stav prostředí po WP11

nginx na hostiteli běží (před WP11 neběžel; `D:\nginx\stop.bat` ho zastaví) a obsahuje relay `D:\nginx\html\wp11\`
s buffery `D:\nginx\wp11-buffer\` (`deploy-wp11-relays.ps1 -Remove` je smaže); `D:\nginx\html\phpvpn.php` se neměnil.
Windows klient je odpojený jako před WP11. `emulator-5556` (API 36) spuštěný WP11 dál běží, 5554 a 5558 nespustil a
nezastavil. Aplikace je ze všech tří emulátorů odinstalovaná (konfigurace peer testů obsahovala klíč kontraktu
Windows klienta); vygenerované dokumenty v `work/wp11` jsou smazané.

## Soubory

- `client/android/app/src/androidTest/kotlin/phpvpn/android/wp11/RealRelayTest.kt` (T-A06, T-A07, T-A14)
- `client/android/app/src/debug/kotlin/phpvpn/android/debug/DebugVpnCommandReceiver.kt` (`CONFIGURE_FILE`, `STATUS_JSON`)
- `client/tests/android/`: `README.md`, `deploy-wp11-relays.ps1`, `run-instrumented.sh`, `run-matrix.sh`,
  `peer-traffic-tests.sh`, `wp11-scenarios.sh`, `make_peer_config.py`, `tcp_echo.py`, `relay_block_proxy.py`
- Logy a stopy (mimo git): `work/wp11/logs*`, `work/wp11/*.trace`, `work/wp11/*.png`
