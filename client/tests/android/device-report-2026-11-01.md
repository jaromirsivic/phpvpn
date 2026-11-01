# Reálné zařízení: T-D01, T-A20 a první běh arm64, report 2026-11-01

Výsledek testů na reálném telefonu podle [plan-android-v001.md](../../../Plans/2026-10-31-09-40-08/plan-android-v001.md)
(T-D01, T-A20, část T-A04, bod 11 „Doze, spánek CPU a hodiny“, R6 a R9). Co se nespustilo, je NOT RUN s důvodem.
Všechny výsledky jsou z jednoho telefonu uživatele připojeného přes USB; žádný není z emulátoru.

Větev `worktree-agent-a100ec3cfa46e3fa6` nad `android` 95f8478 (všechny balíky kromě WP11). Jediná změna kódu
aplikace je debug receiver (viz „Co vzniklo“); produkční kód ani release build se nemění.

## Zařízení a prostředí

| Údaj | Hodnota |
|---|---|
| Telefon | Google Pixel 10 (`frankel`), sériové číslo `64090DLCR000R5` |
| Systém | Android 17 (API 37), build `CP3A.260905.009`, `user/release-keys`; novější než vše dosud testované (emulátory API 34 až 36) |
| ABI, stránka | `arm64-v8a`, `getconf PAGE_SIZE` = 4096 (kontrola 16 KB zůstává teoretická, viz WP12) |
| Displej | 1080 × 2424 px, 420 dpi |
| Síť | Wi-Fi „D-Link“ 192.168.68.55/22 (po výpadku AP 192.168.68.54), bez globální IPv6, **bez SIM** (`gsm.sim.state=ABSENT,NOT_READY`) |
| Zámek | zabezpečená zamykací obrazovka (otisk prstu, PIN); viz „Co se nespustilo“ |
| Relay | nginx + PHP na Windows hostiteli `D:\nginx`, `phpvpn.php` verze 1.0.2, port 80; hostitel 192.168.68.53 (po výpadku AP 192.168.68.55) |
| Windows peer | služba phpvpn 1.0.2 (`C:\Program Files\phpvpn\1.0.2`), `serverUrl` `http://127.0.0.1/phpvpn.php`, virtuální IP 10.99.99.2 |
| APK | `app-arm64-v8a-debug.apk` z `:app:assembleDebug -Pphpvpn.abiSplits=true` (JDK `D:/androidstudio/jbr`), 45 431 626 B, versionName 1.0.3, versionCode 10003, `targetSdk` 36 |

Telefon zůstal celou dobu na USB, `adb reverse tcp:18080 tcp:80` ze zadání jsem na začátku odstranil
(`adb reverse --remove-all`), relay se volal po Wi-Fi.

## Co vzniklo

| Část | Soubory | Obsah |
|---|---|---|
| Debug receiver | `client/android/app/src/debug/.../DebugVpnCommandReceiver.kt` | Dvě nové akce jen v debug buildu: `DIAGNOSTICS` (veřejné čítače `get-diagnostics`: stav, fáze, `recoveryCount`, `lastRecoveryAt`, `softRecoveryCount`, `lastSoftRecoveryAt`, čítače pollů a paketů, `lastError`; žádná adresa ani klíč) a `CONFIGURE_FILE` (celý dokument z `cacheDir/device-test-config.json` přes `set-config`, soubor se přepíše nulami a smaže, obsah se nikdy neloguje). Zadání počítalo s `get-diagnostics` přes receiver, ten ho ale neměl. |
| Skripty | `client/tests/android/device/` | Spouštějí se z kořene repozitáře, výstupy jdou do `work/device/` (gitignored). `logcat.sh` (záznam logu aplikace podle uid a systémových řádků o VPN a Doze), `push_config.py` (konfigurace telefonu z relay a Windows konfigurace bez výpisu tajemství, přes `adb exec-in run-as` do `cacheDir`), `win_add_contract.py` (kontrakt pro telefon do Windows konfigurace s novým náhodným klíčem přes `phpvpn.exe set-config` a dočasný soubor, který se smaže), `http_get.sh` a `other_traffic.sh` (na telefonu, `toybox nc`), `listen.sh` + `tcp_probe.py` (TCP hostitel → telefon), `pinger.py` + `pingstats.py` (ping každých 10 s s logem), `diag.sh` (čítače obou stran), `wifi_cycle.sh`, `ta20_sleep.sh`, `doze_watch.sh`, `ta20_collect.sh`, `force_idle.sh`, `snapshot.sh` (nastavení a Doze), `scan_secrets.py` (kontrola, že žádný access key ani payload key není v uložených souborech). |

## Výsledky

| Test | Výsledek | Klíčová čísla |
|---|---|---|
| Sestavení `:app:assembleDebug` (splity ABI) | PASS | bez varování; tři debug APK (arm64, x86_64, univerzální) |
| Instalace arm64 APK (`adb install -r`) | PASS | čistá instalace, `primaryCpuAbi=arm64-v8a`, uid 10151 |
| První spuštění | PASS | `am start -W`: COLD, TotalTime 1413 ms; `libphpvpn-android.so` z `base.apk!/lib/arm64-v8a` načtená, „the native runtime is running“, „config startup: default created“; žádný nativní pád, crash buffer bez řádku aplikace, `exit-info` jen `PACKAGE UPDATED` |
| T-D01 průvodce 00 → 12 → 14 → 15 → 16 → 17 → 03 → 04 → 01 v UI | **NOT RUN** | zamčený telefon, viz „Co se nespustilo“ |
| T-D01 kontrakt přes 19/20 nebo 23, 25, Reconnect v UI | **NOT RUN** | dtto; stejná konfigurace uložena produkčním `set-config` přes `CONFIGURE_FILE` |
| T-D01 Connect produkční cestou (`ConnectActions` → služba) | PASS | od přijetí Connect do `connected` 387 ms (health, info, info, `establish`, tunel); znovu po změně URL 336 ms; Disconnect potvrzen za 74 ms |
| Služba a notifikace (Android 17) | PASS | foreground `types=0x400` (`systemExempted`), kanál `vpn-connection`, `ONGOING_EVENT\|ONLY_ALERT_ONCE\|FOREGROUND_SERVICE`, jedna akce (Disconnect), viditelnost `PRIVATE`; `tun0` 10.99.99.5/24, routa 10.99.99.0/24 |
| Peer: telefon → Windows HTTP přes tunel | PASS | 5/5 `GET /phpvpn.php` → `HTTP/1.1 200 OK`, 30 789 B, 1029 až 1635 ms (stejný GET přímo po LAN 179 ms) |
| Peer: telefon → Windows ICMP | PASS | 5/5, RTT 133 až 402 ms, průměr 211 ms (Windows na adaptéru PHPVPN ICMP odpovídá) |
| Peer: Windows → telefon ICMP | PASS | 10/10, první 2332 ms (klidový polling 3,2 s), další 129 až 215 ms |
| Peer: Windows → telefon TCP 5555 (echo) | PASS | 6/6, connect první 2519 ms, další 109 až 171 ms, echo 221 až 266 ms |
| Čítače obou stran | PASS | telefon `pktSent` 229 = Windows `ipv4PacketsReceived` 229, telefon `pktRecv` 205 = Windows `ipv4PacketsSent` 205, `recoveryCount` 0 na obou |
| T-A04 na zařízení (provoz shell uid 2000 při VPN) | PASS (IPv6 NOT RUN) | internet přes `wlan0`, jen 10.99.99.0/24 přes `tun0`; DNS (www.google.com, example.com, cloudflare.com), TCP 443 na 1.1.1.1 a 8.8.8.8, HTTP example.com 200 za 216 ms, LAN relay 225 ms; Wi-Fi nemá globální IPv6 |
| Wi-Fi vypnuto 61 s a zapnuto (`svc wifi`) | PASS | výpadek zachycen za 0,5 s, soft recovery (stav `connected`, fáze `reconnecting`, `tun0` zůstal), po zapnutí obnoveno za 7,8 s (většinou asociace Wi-Fi); `recoveryCount` 0, `softRecoveryCount` 1; ping poté 175 až 207 ms |
| Wi-Fi ↔ mobilní data | **NOT RUN** | v telefonu není SIM; relay navíc není z internetu dosažitelný (privátní 192.168.68.x) |
| T-A20 vypnutá obrazovka 30 min, Doze | PASS (s omezením) | 182/182 pingů, žádná mezera, RTT medián 1725 ms, max 3314 ms (před spánkem medián 1748, max 3214); deep `IDLE` 27 min 42 s (91 %); žádné 401, žádná recovery ani soft recovery, `health` každých 5 min ověřen; první odpověď po probuzení 437 ms. Omezení: CPU na USB neusnulo (zjištění 2) |
| T-A20 `deviceidle force-idle` 5 min | PASS | 30/30 pingů, RTT medián 1970 ms, max 3457 ms; po `unforce` a probuzení 8/8 (231 až 2733 ms); žádná recovery |
| T-A20 akceptace plánu (po 10 min spánku žádné 401, nejvýš jedna recovery) | PASS | 0× 401, 0 recovery během spánku; hodiny `CLOCK_BOOTTIME` přes suspend neověřeny (zjištění 2) |
| Neplánovaný výpadek AP 02:00:27 (oba klienti i hostitel odpojeni, DHCP přidělilo nové adresy) | PASS (chování podle bodu 11) | Wi-Fi telefonu zpět po 82 s (v deep Doze); soft recovery 02:00:28.092, rozpočet vyčerpán 02:02:00.945 (92,9 s), plná recovery (`recoveryCount` 1), VPN síť uvolněna; po opravě URL Connect za 336 ms |
| ANR, pády | PASS | žádné ANR (`exit-info`), crash buffer bez aplikace |
| Kontrola tajemství v uložených souborech | PASS | `scan_secrets.py` nad `work/device`, `client/tests/android` a debug zdroji: jediný nález je veřejný výchozí `DUMMY_ACCESS_KEY` v již existujícím `ConfigReplies.kt` |

## Postup a podrobnosti

### Konfigurace a souhlas bez UI

Telefon byl po celou dobu zamčený zabezpečeným zámkem, UI tedy nešlo ovládat (`input` a `uiautomator` za
zamykací obrazovkou nevidí aplikaci a odemčení PINem nesmím provést). Hlavní cestu jsem proto provedl produkčními
příkazy bez UI:

1. `push_config.py` přečte výchozí dokument aplikace (`run-as … cat no_backup/phpvpn/config/config.json`), doplní
   `serverUrl` `http://192.168.68.53/phpvpn.php`, `serverSecret` z relay, `virtualIp` 10.99.99.5, `accessKey`
   relay pro 10.99.99.5 a kontrakt 10.99.99.2 s klíčem z Windows konfigurace, pošle ho přes
   `adb exec-in run-as … sh -c 'umask 077; cat > cache/device-test-config.json'` a broadcast `CONFIGURE_FILE`.
   Log: `engine: a new configuration was stored`, `configure-file ok fileDeleted=true`. Tajemství nebyla nikde
   vypsána ani uložena mimo aplikaci a Windows konfiguraci.
2. Souhlas s VPN (systémový dialog 03) jsem nahradil `appops set io.github.jaromirsivic.phpvpn ACTIVATE_VPN allow`
   (stejný app-op, který dialog uděluje; před tím `No operations`, výchozí `ignore`).
3. Connect a Disconnect přes `DebugVpnCommandReceiver` (`CONNECT`, `DISCONNECT`), tedy produkční `ConnectActions`
   a služba, jako tlačítko na 02.

**Virtuální IP 10.99.99.5 místo 10.99.99.3.** Relay hlásil 10.99.99.3 jako aktivního klienta (`phpvpn.exe info`:
`lastSeen` 01:10:48, 01:11:18, 01:11:48, tedy každých 30 s), zjevně emulátor WP11 proti stejnému relay. Dva
klienti se stejnou adresou by si navzájem brali fronty, proto telefon dostal volnou 10.99.99.5 (relay pro ni má
access key, `lastSeen` nikdy) a Windows konfigurace kontrakt 10.99.99.5 s novým náhodným payload klíčem
(`win_add_contract.py`, `set-config` „ok“, dočasný soubor smazán). Windows klient se kvůli tomu jednou
odpojil a připojil (Disconnect 2,9 s, Connect 1,9 s).

**Access key.** `ACCESS_KEYS` v `phpvpn.php` je mapa podle virtuální IP (14 klíčů pro 10.99.99.2 až .15, všechny
různé). Access key z Windows konfigurace patří adrese 10.99.99.2 (shoduje se s relay pro .2, s .3 ani .4 ne),
pro telefon tedy použít nejde: telefon potřebuje klíč relay pro svou vlastní adresu. `SERVER_SECRET` relay je
shodný s Windows konfigurací (výchozí hodnota, relay na ni sám upozorňuje).

### Peer provoz

Windows klient byl `connected` (`running`, kontroly `success`). Měření v tabulce výše; HTTP přes `toybox nc`
(skript s CRLF, `toybox nc` končí při EOF na stdin, proto stdin drží `sleep`), TCP přes echo listener
`toybox nc -L -p 5555 cat` na telefonu a Python socket z hostitele. RTT ICMP i TCP jsou dané pollingem obou
stran (50 ms po paketu, v klidu až 3,2 s), proto první paket po klidu trvá 2 až 2,5 s.

### Wi-Fi vypnuto a zapnuto

`wifi_cycle.sh`: `svc wifi disable` 01:18:38.848, `engine: the data exchange failed, retrying while the tunnel
stays up` 01:18:39.371, po dobu výpadku stav `connected` / fáze `reconnecting` / `tunnel=yes`; `svc wifi enable`
01:19:41.862, `the data exchange recovered, the tunnel was kept` 01:19:49.684. Log v `work/device/wifi-cycle.txt`.

### T-A20

- Před testem (`work/device/ta20-before.txt`): `stay_on_while_plugged_in=0` (už výchozí stav uživatele, nic jsem
  neměnil), `screen_off_timeout=30000`, deep i light `ACTIVE`, `mCharging=true`.
- 01:21:06 start pingu z hostitele na 10.99.99.5 každých 10 s (`ping -n 1 -w 20000`, log `work/device/ta20-ping.txt`).
- 01:22:34.934 `dumpsys battery unplug` (bez něj Doze na USB nenastane), 01:22:35.246 `dumpsys batterystats --reset`,
  01:22:35.535 `input keyevent KEYCODE_SLEEP`.
- Doze každých 5 min (`work/device/ta20-doze-watch.txt`): 01:22:35 deep `INACTIVE` / light `INACTIVE`, od 01:27:36 až
  do 01:52:42 deep `IDLE` / light `OVERRIDE`; podle `deviceidle` deep idle od 01:25:13 (2 min 38 s po vypnutí).
  Obrazovka `mWakefulness=Dozing` (always-on display).
- 01:52:52 `KEYCODE_WAKEUP`, sběr (`ta20_collect.sh`): diagnostika, `deviceidle`, `batterystats` (jen globální řádky
  a blok uid u0a151), log aplikace.

| Ping | Počet | Odpovědi | RTT min / medián / p90 / max (ms) |
|---|---|---|---|
| před spánkem (01:21:06 až 01:22:35) | 9 | 9 | 281 / 1748 / 2783 / 3214 |
| spánek celkem (01:22:35 až 01:52:52) | 182 | 182 | 148 / 1725 / 2996 / 3314 |
| z toho deep `IDLE` (od 01:27:36) | 152 | 152 | 148 / 1729 / 2996 / 3314 |
| po probuzení (01:52:52 až 02:00:07) | 44 | 44 | 254 / 1842 / 3063 / 3345 |
| forced idle (02:07:17 až 02:12:19) | 30 | 30 | 372 / 1970 / 3166 / 3457 |

Po probuzení: první odpověď 01:52:57 za 437 ms. Tři timeouty 02:00:17 až 02:00:56 patří výpadku AP (níže), ne Doze.

Diagnostika telefonu po 30 min: `state=connected phase=running recoveryCount=0 softRecoveryCount=1` (jediná soft
recovery je z testu Wi-Fi v 01:18:39), `lastError=none`, `dropped=10`, `outExpired=0 inExpired=0`. Log aplikace
v okně spánku: jen `network: health response verified and server time accepted` v 01:23:27, 01:28:27, 01:33:27,
01:38:27, 01:43:27, 01:48:29 a 01:53:29; žádné 401, chyba času, recovery ani soft recovery.

`dumpsys batterystats` (od resetu, 30 min 22 s):

| Údaj | Hodnota |
|---|---|
| Time on battery / screen off | 30 min 22 s realtime = 30 min 22 s **uptime** (CPU ani jednou nespalo) |
| Device full idling | 27 min 42 s (91,2 %), 1× |
| Discharge | 0 mAh (fyzicky na USB napájení, `unplug` mění jen hlášený stav) |
| Kernel wakelock `c450000.usb3` | 31 min 26 s (USB řadič) |
| Kernel wakelock `wlan_txfl_wake` / `wlan_rx_wake` | 18 min 6 s (880×) / 17 min 41 s (971×) |
| Partial wakelocky aplikace (u0a151) | žádné |
| Foreground služba aplikace | 30 min 22 s |
| CPU aplikace | 59,7 s user + 18,0 s kernel (4,3 % jednoho jádra) |
| Wi-Fi aplikace | 23,74 MB přijato, 20,81 MB odesláno (39 596 / 57 789 paketů), Wi-Fi Rx 7,9 s, Tx 17,9 s |
| Polly telefonu | 20 100 za ~32 min (≈ 10,5/s), 99 % prázdných; tunelem jen 195 paketů ven a 195 dovnitř |

Forced Doze (`force_idle.sh`, `work/device/force-idle.txt`): 02:07:17.290 `KEYCODE_SLEEP`, 02:07:17.630
`deviceidle force-idle` („Now forced in to deep idle mode“), po 5 min stále deep `IDLE`, 02:12:19.135 `unforce`,
02:12:19.492 `KEYCODE_WAKEUP`. Stav po celou dobu `connected/running`, žádná recovery, 30/30 pingů.

### Neplánovaný výpadek AP

V 02:00:27 se AP „D-Link“ odpojil od všech klientů (`CTRL-EVENT-DISCONNECTED reason=0` na telefonu, Wi-Fi
hostitele také odpojená). Telefon se v deep Doze znovu připojil v 02:01:49 (82 s), dostal 192.168.68.54;
hostitel se sám nepřipojil (profil „D-Link“ má „Connect manually“), na 192.168.68.53 odpovídalo jiné zařízení
(`Connection refused`). Klient: soft recovery 02:00:28.092, `the soft recovery budget is exhausted` 02:02:00.945,
`entering recovery`, `the VPN network was removed` 02:02:01.237, stav `recovering` s `lastError=communication_failed`.
To je chování bodu 11 (epizoda delší než 90 s končí plnou recovery). Hostitele jsem připojil zpět jeho uloženým
profilem (`netsh wlan connect name="D-Link"`, dostal 192.168.68.55), konfiguraci telefonu změnil na
`http://192.168.68.55/phpvpn.php` (`CONFIGURE_FILE`), Disconnect potvrzen za 74 ms, Connect za 336 ms. V 02:06:59
ještě jedna krátká soft recovery (0,3 s, timeout pingu 02:06:34, jeden ping 5951 ms), bez události Wi-Fi na
telefonu, zřejmě dozvuk výpadku na straně hostitele.

## Zjištění

1. **Řídký provoz drží polling v rychlém režimu: ping každých 10 s = ≈ 10,5 requestu za sekundu na každé straně.**
   `NetworkWorker::nextPollDelayMs` (`client/src/core/runtime/network_worker.cpp`) po každém paketu (příchozím i
   odchozím) nastaví interval na minimum serveru (50 ms) a drží ho 5 s, teprve pak zdvojnásobuje do 3,2 s. Ping
   každých 10 s tak udrží polovinu času rychlý polling: za 30 min 20 100 pollů, 44,5 MB přes Wi-Fi, 77,7 s CPU
   a ~18 min wakelocků WLAN ovladače na 195 užitečných paketů v každém směru. Bez provozu je polling správně
   3,2 s (10 pollů za 32 s, změřeno 01:17). Není to pád ani porušení specifikace (jde o sdílené jádro, Windows klient polloval
   ve stejném čase podobně, měl ale i provoz jiných peerů), ale na telefonu je to pro baterii zásadní a pro R6 a T-A20 na baterii
   rozhodující: jakákoli periodická aplikace v tunelu (keepalive, ping, synchronizace) stojí desítky HTTP requestů
   za každý paket. Návrh do plánu (neopravoval jsem): kratší rychlé okno nebo rychlejší útlum na Androidu, případně
   při vypnuté obrazovce.
2. **Na USB telefon nikdy neusne, takže T-A20 hodiny `CLOCK_BOOTTIME` přes suspend neověřil.** Kernel wakelock
   `c450000.usb3` po celou dobu, `uptime` = `realtime`. Doze (deep `IDLE`) přitom nastal a omezoval ostatní
   aplikace, takže zjištění o síti platí, ale rozdíl `CLOCK_MONOTONIC` / `CLOCK_BOOTTIME` se neprojevil. Akceptace
   (žádné 401, nejvýš jedna recovery) prošla, ale za podmínek, kdy by prošla i s `CLOCK_MONOTONIC`. Pro skutečné
   ověření je potřeba běh bez USB (ruční postup 2). Pozor: i bez USB bude CPU kvůli zjištění 1 při provozu
   v tunelu často vzhůru (`wlan_txfl_wake`/`wlan_rx_wake` 18 min z 30).
3. **Doze síť aplikace neomezuje.** Foreground služba `systemExempted` s VPN je na Androidu 17 vyjmutá ze síťových
   omezení Doze: v deep `IDLE` (přirozeném i `force-idle`) odpověděly všechny pingy s RTT do 3,5 s, tedy do
   maximálního intervalu pollingu, stejně jako s rozsvíceným telefonem. Text v `documentation/android-client.md`
   („Android may delay network access (Doze), so packets from other peers can arrive late“) je na tomto zařízení
   příliš opatrný: zpoždění dává polling (až 3,2 s), ne Doze. Ponechávám rozhodnutí WP13/WP11, dokumentaci jsem
   neměnil. Výjimka z optimalizace baterie nebyla potřeba (aplikace není v `deviceidle whitelist`).
4. **Access key je na relay pro každou virtuální IP jiný.** Zadání předpokládalo access key z Windows konfigurace
   („one per server“); `ACCESS_KEYS` v `phpvpn.php` je ale mapa IP → klíč a Windows klíč patří 10.99.99.2. Wizard
   (15) proto musí dostat klíč relay pro adresu telefonu. Testovací instrukce a ruční postup níže to respektují.
5. **10.99.99.3 současně používal jiný klient (emulátor WP11)**, telefon proto běžel jako 10.99.99.5, viz výše.
   Pro další ruční testy na stejném relay je potřeba adresy rozdělit předem.
6. **Android 17, rozdíly proti emulátorům:**
   - ART při instalaci překládá aplikaci filtrem `speed`, protože je „a VPN service host“ (`MaybeOverrideCompilerFilter`);
     studený start debug buildu (`am start -W`) 1,4 s.
   - Po `adb install -r` systém sám znovu otevře úlohu aplikace (`MainActivity` vytvořená znovu 01:10:41 bez
     `am start`, přes `com.android.systemui/...packageupdate`).
   - Systémový logger hlásí „Foreground service start for UID: 10151 does not have any types“ a „FGS stop call for:
     10151 has no types!“, přestože `dumpsys activity services` ukazuje `types=0x00000400` (`systemExempted`);
     bez vlivu na funkci (služba běží, notifikace je, Doze ji nepostihuje).
   - Při zřízení VPN systém zruší živá TCP spojení všech ostatních uid kromě vlastního (`Destroyed live tcp sockets
     for uids={[0, 10150], [10152, 20150], [20152, 99999]} exemptUids={1016, 10151}`); ostatní aplikace se tedy
     při Connect jednou přepojí. Standardní chování platformy, stojí za větu v dokumentaci.
   - `getconf PAGE_SIZE` 4096; žádný pád, ANR ani varování linkeru u `libphpvpn-android.so`.
7. **Výpadek sítě delší než 90 s končí plnou recovery** (bod 11, viz „Neplánovaný výpadek AP“); kratší výpadek
   (61 s, `svc wifi`) přežije tunel soft recovery. Obojí odpovídá plánu.

Vady aplikace (pád, ANR, únik tajemství, špatný stav) jsem nenašel. UI vady na displeji 1080 × 2424 a v Androidu 17
nešlo posoudit (UI NOT RUN).

## Co se nespustilo

| Test | Důvod |
|---|---|
| T-D01 průchod průvodcem v UI (00 → 12 → 14 → 15 → 16 → 17 → 03 → 04 → 01), kontrakt přes 19/20/23, 25, Reconnect, obrazovka 26, snímky obrazovek a vady UI | Telefon má zabezpečený zámek (otisk/PIN) a byl celou dobu zamčený; odemknout ho nesmím a `wm dismiss-keyguard` zabezpečený zámek neodemkne. Snímky z `work/device` jsou jen ze zamykací obrazovky, do `device-screenshots/` jsem nic nekopíroval. Ruční postup 1. |
| Systémový dialog souhlasu 03 (text na Androidu 17) | Dtto; souhlas udělen `appops`. |
| Wi-Fi ↔ mobilní data | Bez SIM. Relay navíc není z internetu dosažitelný, očekávaný výsledek by byl `recovering`. |
| T-A20 se skutečným suspendem CPU (hodiny `CLOCK_BOOTTIME`), spotřeba v mAh | Telefon musel zůstat na USB; USB drží CPU vzhůru a nabíjí. Ruční postup 2. |
| T-A04 IPv6 | Wi-Fi nemá globální IPv6 adresu. |
| T-A16 se skutečným LMK, T-A21 (paměť při plných frontách) | Mimo zadání tohoto běhu. |
| Peer s Fedora klientem (T-D01) | Mimo zadání (jen Windows). |

## Obnovená nastavení a stav na konci

| Položka | Před | Během | Na konci |
|---|---|---|---|
| `global stay_on_while_plugged_in` | 0 | 0 (neměněno, T-A20 vyžaduje 0) | 0 |
| `system screen_off_timeout` | 30000 | neměněno | 30000 |
| `global wifi_on` | 1 | 0 na 61 s (`svc wifi disable`) | 1 |
| `dumpsys battery` | skutečný stav | `unplug` 01:22:34 až 02:14 | `dumpsys battery reset` (USB powered, nabíjí) |
| `deviceidle` | ACTIVE | `force-idle` 02:07:17 až 02:12:19 | `unforce`, ACTIVE |
| `batterystats` | – | `--reset` 01:22:35 (statistiky před tím ztraceny, podle zadání) | – |
| `adb reverse` | `tcp:18080 tcp:80` | odstraněno podle zadání | žádný |
| appop `ACTIVATE_VPN` aplikace | výchozí `ignore` | `allow` | `allow` (odpovídá přijatému souhlasu; zrušit lze v Nastavení → VPN → PHPVPN → Forget VPN) |
| Aplikace | neinstalovaná | debug 1.0.3 | nainstalovaná s konfigurací (`http://192.168.68.55/phpvpn.php`, 10.99.99.5, kontrakt 10.99.99.2), VPN odpojená (`disconnected`, žádný `tun0`) |
| Soubory na telefonu | – | skripty v `/data/local/tmp/phpvpn-*` | smazané; `cacheDir` aplikace prázdný |
| Windows konfigurace | kontrakty .3 a .4 | + kontrakt 10.99.99.5 | ponechán (potřebuje ho konfigurace telefonu); Windows klient `connected` |
| Wi-Fi hostitele | připojeno (D-Link, .53) | odpojeno výpadkem AP | znovu připojeno uloženým profilem (D-Link, .55) |

Telefon nebyl restartován, data jiných aplikací se nečetla ani nemazala (`batterystats` se ukládal jen v globálních
řádcích a bloku uid aplikace, systémový log jen v řádcích o VPN, Doze a Wi-Fi).

## Ruční postupy

1. **T-D01 v UI (uživatel, odemčený telefon).** `adb shell settings put global stay_on_while_plugged_in 7` (obnovit
   na 0), odemknout. Pro průvodce od 00 nejdřív smazat konfiguraci aplikace (`adb shell pm clear
   io.github.jaromirsivic.phpvpn`, smaže i souhlas) nebo odinstalovat. Průvodce: 12 URL relay (aktuální adresa
   hostitele, dnes `http://192.168.68.55/phpvpn.php`), 14 `SERVER_SECRET` relay, 15 volná virtuální IP a **access key
   relay pro tuto IP** (zjištění 4), 16, 17, 03 přijmout, 04, 01; pak kontrakt 10.99.99.2 s payload klíčem, který má
   Windows konfigurace pro tuto IP (`win_add_contract.py`, pokud ještě neexistuje), 25 Reconnect, 26. Snímky
   `adb exec-out screencap -p`; obrazovky 14, 15, 20, 21, 23 mají `FLAG_SECURE`, jejich snímek je černý.
2. **T-A20 bez USB.** `adb tcpip 5555`, `adb connect <ip telefonu>:5555`, odpojit kabel, spustit ping z hostitele
   (`pinger.py`), `ta20_sleep.sh 30`, po 30 min `ta20_collect.sh`. Vyhodnotit `uptime` vs `realtime`
   v `batterystats` (skutečný suspend), `Discharge` v mAh, podíl aplikace, a v logu 401, `time` a recovery. Pozor:
   adb přes Wi-Fi sám budí Wi-Fi; Doze dotazy jen jednou za 5 min.

## Soubory

- `work/device/` (gitignored): `build.log`, `logcat-app.txt`, `logcat-system.txt`, `ta04-connected.txt`,
  `wifi-cycle.txt`, `ta20-before.txt`, `ta20-ping.txt`, `ta20-doze-watch.txt`, `ta20-diag.txt`, `ta20-deviceidle.txt`,
  `ta20-batterystats.txt`, `ta20-batterystats-app.txt`, `ta20-logcat-recovery.txt`, `force-idle.txt`, `force-ping.txt`,
  `restore-after.txt`, `lock-check.png` (zamykací obrazovka).
- `client/tests/android/device/`: skripty výše.
