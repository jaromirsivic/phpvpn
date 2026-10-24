# Ověření selhání velkých FTP přenosů přes PHPVPN

Datum: 2026-10-24. Cíl: ověřit hypotézy z analýzy kódu, proč přenos
několika set MB přes čisté FTP (bez TLS) uvnitř PHPVPN tunelu náhle selže.
Hlavní hypotéza: jediná neúspěšná HTTP výměna `data` (timeout, 503, zavřené
spojení, 401) vede v klientovi k `teardown()`, který odebere VPN adresu
a routu, a tím zruší všechna TCP spojení v tunelu včetně FTP.

## Prostředí

| Role | Stroj | Adresa | Software |
|---|---|---|---|
| PHPVPN server | tento Windows host | `http://172.18.96.1/phpvpn.php` (z VM), `http://127.0.0.1/phpvpn.php` (lokálně) | nginx 0.0.0.0:80 + php-cgi 127.0.0.1:9000 z `D:\nginx`, `D:\nginx\html\phpvpn.php` = aktuální `phpvpn.php` z repozitáře (SHA-256 `3ad49721…`) |
| Klient A (Windows) | tento host | VPN `10.99.99.2`, access key `0202…` | phpvpn 1.0.0 z `dist/client/Windows/x64` (již nainstalovaný, shodný hash `phpvpn-service.exe`) |
| Klient B (Fedora 44) | VM `172.18.97.203` (Hyper-V Default Switch) | VPN `10.99.99.3`, access key `0303…` | phpvpn 1.0.0-1 z `dist/client/Linux/x64` (RPM), nainstalováno `install.sh --headless` |
| FTP server | Fedora VM | `10.99.99.3:21`, pasivní porty 40000–40100 | **vsftpd 3.0.5-15.fc44** |
| FTP klient | Windows host | – | `curl.exe` 8.13.0 (součást Windows, pasivní režim, čisté FTP bez TLS) |

Kontrakt obou klientů: `10.99.99.2 <-> 10.99.99.3`, payload key `918c4885…`,
`SERVER_SECRET` výchozí (`CHANGE_THIS_SERVER_SECRET`), stejně jako výchozí
access keys serveru.

## Postup

### 1. Server

1. Záloha původního souboru: `D:\nginx\html\phpvpn.php` ->
   `D:\nginx\html\phpvpn.php.before-testing-20261024.bak`.
2. Kopie aktuálního `phpvpn.php` z repozitáře do `D:\nginx\html\`.
3. Start stacku `D:\nginx\start.bat` (nginx + php-cgi supervisor). Kontrolní
   skript `stack-control.ps1` hlásil chybějící marker stavové stránky, protože
   běžel ještě před výměnou souboru; procesy ale běžely a po výměně souboru
   stavová stránka i `health` odpovídají.
4. Ověření: `GET /phpvpn.php` obsahuje „PHPVPN server is running“,
   `POST ?action=health` vrací podepsaný `health`; z Fedory
   `curl http://172.18.96.1/phpvpn.php` -> 200.

### 2. Linux klient na Fedoře

Přístup: `ssh -i work/linux-plan/ssh/id_ed25519 fedora@172.18.97.203`
(klíč z repozitáře je na VM autorizovaný, `sudo` bez hesla).

1. Na VM byla starší instalace phpvpn 1.0.0-2 (build 26. 9. 2026, release 2
   z upgrade testu). Odstraněna: `sudo sh uninstall.sh --remove-data`.
2. `scp -r dist/client/Linux/x64 fedora@172.18.97.203:~/phpvpn-linux-dist-20261024`
3. `sh install.sh --verify-only` (manifest a podpis OK), potom
   `sudo sh install.sh --headless` -> `phpvpn-1.0.0-1.x86_64`, služba aktivní.
4. Konfigurace `~/phpvpn-test-config.json` uložena přes
   `phpvpn set-config` (serverUrl `http://172.18.96.1/phpvpn.php`,
   virtualIp `10.99.99.3`, kontrakt na `10.99.99.2`).

### 3. FTP server na Fedoře

1. `sudo dnf install -y vsftpd` -> vsftpd 3.0.5-15.fc44.
2. Původní `/etc/vsftpd/vsftpd.conf` zálohován jako
   `/etc/vsftpd/vsftpd.conf.before-phpvpn-test`; nová konfigurace:
   lokální uživatelé, zápis povolen, `pasv_min_port=40000`,
   `pasv_max_port=40100`, `local_root=/home/fedora/ftp-test`,
   `idle_session_timeout=600`, `data_connection_timeout=300`,
   log `/var/log/vsftpd.log`.
3. SELinux: `setsebool -P ftpd_full_access 1`.
4. firewalld: `--add-service=ftp`, `--add-port=40000-40100/tcp`, reload.
5. `systemctl enable --now vsftpd`; ověřeno `curl ftp://127.0.0.1/` s účtem
   `fedora`.

### 4. Windows klient

1. Záloha `C:\ProgramData\phpvpn\config.json` do
   `work\windows-config-backup-20261024.json` (původní konfigurace mířila na
   `https://phpvpn.freehosting.dev/phpvpn.php`).
2. Testovací konfigurace uložena přes `phpvpn.exe set-config`
   (serverUrl `http://127.0.0.1/phpvpn.php`, virtualIp `10.99.99.2`,
   kontrakt na `10.99.99.3`).

### 5. Spojení a kontrola tunelu

- `phpvpn connect` na obou stranách -> oba `state: connected`,
  `tunnelReady: yes`, `recoveryCount: 0`; Windows má adaptér `PHPVPN`
  s `10.99.99.2`, Fedora rozhraní `phpvpn` s `10.99.99.3`.
- `ping 10.99.99.3`: 0 % ztrát, RTT 172–2387 ms (polling relay).
- FTP výpis přes tunel a 20 MiB tam a zpět: upload 1,95 MB/s, download
  1,20 MB/s, SHA-256 shodné.

### 6. Monitorování během přenosu

- Windows: skript ve scratchpadu každých 5 s zapisuje `phpvpn status`
  (stav, `lastError`, `recoveryCount`, čítače) a počet/velikost souborů ve
  frontách `D:\nginx\html\phpvpn_data\10_99_99_{2,3}`, každých 30 s
  `phpvpn diagnostics`. Log služby `C:\ProgramData\phpvpn\logs\phpvpn-service.log`.
- Fedora: `journalctl -u phpvpn -u vsftpd -f` ->
  `~/phpvpn-test-journal.log`, stejný status poller ->
  `~/phpvpn-test-status.log`.

### 7. Přenosy

Testovací data: 1 GiB náhodných bajtů na každé straně
(`win-upload-1g.bin`, SHA-256 `bf36ba8f…`; `fedora-download-1g.bin`,
SHA-256 `b7a74f2d…`).

1. Upload Windows -> Fedora: `curl.exe -v --user fedora:… -T win-upload-1g.bin ftp://10.99.99.3/`
2. Download Fedora -> Windows: `curl.exe -v --user fedora:… -o … ftp://10.99.99.3/fedora-download-1g.bin`

## Výsledky

### Fáze 1: LAN (Hyper-V Default Switch, bez omezení)

| Přenos | Výsledek | Čas | Rychlost | Hash | recoveryCount | expirované pakety |
|---|---|---|---|---|---|---|
| Upload 1 GiB Windows -> Fedora | OK (226 Transfer complete) | 160 s | 6,7 MB/s | shodný | 0 / 0 | 0 / 0 |
| Download 1 GiB Fedora -> Windows | OK | 238 s | 4,5 MB/s | shodný | 0 / 0 | 0 / 0 |

Diagnostika po obou přenosech: Windows 3806 pollů, `packetsDropped` 147
(drobné pakety bez kontraktu nebo s cizím zdrojem, ne expirace),
`responsesRejected` 0, špička odpovědi 4,3 MiB; Fedora 6649 pollů,
`packetsDropped` 21, `responsesRejected` 0, špička odpovědi 1,5 MiB.
Odchozí fronta Windows během uploadu držela cca 8,6 MB (5 700 paketů),
tj. jen 1,3 s při 6,7 MB/s, takže 5s expirace nenastala.

Na rychlé lokální lince se selhání nereprodukovalo.

### Fáze 2: emulace WAN

Na Fedoře `tc netem` v obou směrech (egress na `eth0`, ingress přes `ifb0`):
zpoždění 40 ms, 10 Mbit/s, fronta 20 000 paketů. RTT ping přes tunel
343–1095 ms. Efektivní propustnost tunelu je potom cca 0,75–0,9 MB/s
(10 Mbit/s mínus režie base64 a NDJSON).

#### Běh 1: upload 1 GiB Windows -> Fedora: SELHAL po 121 MB

- curl po 197 s: `curl: (55) Send failure: Connection was reset`,
  přeneseno 121 110 528 B (`work/ftp-test-20261024/upload-1g-wan.log`).
- Windows klient 13:03:49.305Z: `[WARN] engine: the data exchange failed,
  entering recovery`, `the virtual interface was released`; `recoveryCount` 0 -> 1,
  `responsesRejected` 0 -> 1; znovu připojen v 13:03:53.
- Fedora klient 13:03:50: totéž, `the TUN interface was removed`,
  `recoveryCount` 1, znovu připojen 13:03:54.
- nginx `error.log`: v 15:03:49 a 15:03:50 (lokální čas) pro oba klienty
  `connect() failed (10060 ...) while connecting to upstream fastcgi://127.0.0.1:9000`
  u `POST /phpvpn.php?action=data`, tedy odpověď **502 Bad Gateway**.
- nginx `access.log`: do 15:03:27 stabilně 17 požadavků/s, v 15:03:28 tři,
  pak **21 s žádný dokončený požadavek** (15:03:28 až 15:03:49), potom dva
  502 a od 15:03:53 opět plný provoz. 21 s odpovídá limitu SYN retransmisí
  Windows (3 + 6 + 12 s): jednovláknový `php-cgi.exe` po tuto dobu
  nepřijímal spojení, nginx nedokázal navázat spojení s upstreamem a vrátil
  502. Příčina samotného zaseknutí php-cgi nebyla v tomto běhu zjištěna
  (běžel bez měření časů; viz běh 2).
- Důsledek: oba klienti vyhodnotily jedinou odpověď 502 jako recoverable
  chybu, provedly `teardown()` (odebrání adresy a routy), systém zrušil TCP
  spojení FTP a server `vsftpd` nezapsal žádný záznam o dokončení přenosu.

Tím je hlavní hypotéza potvrzena: **jediná neúspěšná HTTP výměna `data`
během přenosu shodí tunel a tím i FTP spojení.** Tunel se během 4 s sám
obnoví, ale TCP spojení FTP už neexistuje.

#### Běh 2: upload 1 GiB Windows -> Fedora s měřením časů: prošel

Pro další běh byl do `D:
ginx\conf
ginx.conf` dočasně přidán access log
`logs/phpvpn-timing.log` s `$request_time`, `$upstream_connect_time` a
`$upstream_response_time` (záloha `nginx.conf.before-testing-20261024.bak`,
`nginx -s reload`) a na Windows sonda, která každé 2 s měří odpověď serveru
na `health`.

- curl: 1 073 741 824 B za 1 446 s (743 kB/s), exit 0, SHA-256 na Fedoře
  shodné. Žádná recovery na žádné straně.
- Server: 24 585 požadavků, všechny 200 (sonda 400, protože posílá
  zjednodušené tělo), max `request_time` 3,96 s, žádná mezera v logu
  delší než 3 s. Zaseknutí z běhu 1 se neopakovalo.
- Odpovědi serveru pro Fedoru (příjemce) rostly až na **13,9 MB v jedné
  odpovědi** (233 odpovědí nad 2 MB), protože Fedora na 10 Mbit lince
  stahuje jednu odpověď 5 až 11 s a mezitím nepolluje. Fronta
  `10_99_99_3` na serveru se tak držela těsně pod limitem 16 MiB.
- Fedora klient: `outboundQueueExpiredPackets` **4 964** (z 0). Po dobu
  stahování velké odpovědi je transport serializovaný, Fedora neodesílá nic,
  a její odchozí pakety (TCP ACK k FTP datům, řídicí spojení) ve frontě
  zestárly přes 5 s a byly zahozeny. Windows klient: 0 expirací, ale
  `packetsDropped` rostl (237 -> 484).
- Přenos přežil jen proto, že TCP toleruje ztrátu ACKů; propustnost ale
  kolísala a RTT v tunelu dosahovalo sekund.

Tím jsou potvrzeny i hypotézy 2 (serializovaný transport blokuje opačný
směr po dobu jedné dlouhé odpovědi) a 3 (5s expirace ve frontě klienta
zahazuje pakety, jakmile se opačný směr zdrží).

#### Běh 3: download 1 GiB Fedora -> Windows přes WAN: prošel

- curl: 1 073 741 824 B za 1 509 s (712 kB/s), exit 0, SHA-256 shodné,
  žádná recovery. Server: 24 987 požadavků, vše 200, max `request_time`
  1,76 s, žádná mezera.
- Tentokrát je úzkým hrdlem uplink Fedory (její `data` requesty s těly až
  960 kB). Fedora klient: `outboundQueueExpiredPackets` 4 964 -> **8 724**,
  tedy dalších 3 760 zahozených datových paketů FTP, které ve frontě
  klienta čekaly déle než 5 s (TCP je muselo poslat znovu). Windows: 0
  expirací, špička odpovědi 4,3 MiB.

### Shrnutí

| Fáze | Směr | Výsledek | Teardown | Expirace ve frontě |
|---|---|---|---|---|
| LAN | upload | OK, 160 s | ne | 0 |
| LAN | download | OK, 238 s | ne | 0 |
| WAN 10 Mbit | upload, běh 1 | **selhal po 121 MB, connection reset** | **ano, oba klienti (502 ze serveru)** | 0 |
| WAN 10 Mbit | upload, běh 2 | OK, 1 446 s | ne | Fedora 4 964 (ACKy) |
| WAN 10 Mbit | download | OK, 1 509 s | ne | Fedora +3 760 (data) |

Reprodukovaný mechanismus selhání: přechodná chyba serveru (zde 502 po
zaseknutí php-cgi na 21 s, u hostingu typicky 5xx, timeout nebo zavřené
spojení) -> klient `teardown()` -> odebrání VPN adresy -> reset TCP spojení
FTP. Na pomalé lince navíc serializovaný transport a 5s expirace fronty
zvyšují ztrátovost, což zpomaluje přenos a prodlužuje dobu, po kterou je
přenos vystaven riziku takové chyby.

Logy jsou uložené v `work/ftp-test-20261024/` (curl, status/diagnostics
obou klientů, journal Fedory, log služby Windows, nginx access/error
a timing log, vsftpd log).

## Návrh řešení

### A. Hlavní: nerušit tunel při přechodné chybě výměny `data`

Dnes v [vpn_engine.cpp](client/src/core/runtime/vpn_engine.cpp) větev
`if (!healthy)` v `runNetworkThread` volá `teardown()` pro každou
recoverable chybu a `teardown()` zastaví workery, zruší tunelovou session
(adresa, routa, adaptér), vyresetuje `NetworkWorker` a vyprázdní fronty.
Potom běží celý connect (health, info, data, info, tunel) s backoffem
1 až 60 s. Navrhovaná změna:

1. **Nový stav `reconnecting` (soft recovery).** Při recoverable chybě
   výměny `data` zůstane `TunnelSession` aktivní, oba tunelové workery
   běží dál, fronty se nemažou, odchozí lease se vrátí do fronty (to už
   `exchangeData` dělá). Engine jen opakuje `exchangeData` s krátkým
   backoffem (250 ms, 500 ms, 1 s, 2 s, max 5 s, s jitterem; u 503 navíc
   `Retry-After` jako dolní mez). Z pohledu TCP uvnitř tunelu je to pár
   sekund ztrátové linky, kterou retransmise překlenou; FTP spojení
   přežije.
2. **Rozdělit recoverable chyby na dvě třídy** (tabulka v
   [error.cpp](client/src/core/base/error.cpp)): „přechodné“
   (`transport_timeout`, `transport_receive_failed`,
   `transport_send_failed`, `transport_connect_failed`,
   `transport_dns_failed`, `server_internal_error`, 502/503/504 a jiná
   5xx, `response_invalid`, `response_mac_invalid`, `wire_*` chyby
   odpovědi) -> soft recovery; „vyžadující nový bootstrap“ (`401`
   `unauthorized`, `time_not_synchronised`, `contract_missing`,
   `config_*`, `tunnel_io_failed`) -> dosavadní plný teardown.
   U `401` nejdřív jednou obnovit čas přes `performHealth()` bez
   teardownu a zopakovat; teprve opakované 401 vede k plnému restartu.
3. **Časový rozpočet soft recovery.** Když se během
   `softRecoveryGraceSeconds` (návrh 90 s, konfigurovatelné v `runtime`)
   nepodaří ani jedna ověřená výměna, přejít na dnešní plný teardown
   a recovery. Tím zůstane zachována dnešní sémantika pro skutečné
   výpadky serveru.
4. **Stav pro UI a IPC.** `ConnectionState::reconnecting` (nebo
   `connected` s `phase=reconnecting`) do status snapshotu, tray a CLI;
   `recoveryCount` počítat jen plné recovery, přidat
   `softRecoveryCount` a `lastSoftRecoveryAt`.
5. **Health refresh.** Chyba periodického `performHealth` (každých 300 s)
   dnes stav nemění, jen `lastHealthVerified`; ponechat, ale přidat retry
   po 10 s, aby časový offset nezastaral.
6. **Dokumentace a testy.** Popsat soft recovery v `client.md`
   (Stavový automat připojení, Obnova), doplnit jednotkové testy enginu
   s falešným transportem: jedna 502 uprostřed provozu nesmí zastavit
   tunelové workery ani vyprázdnit frontu; 401 vede k health a retry;
   vypršení rozpočtu vede k plnému teardownu.

Dopad na protokol: žádný. Dopad na server: žádný.

**Stav implementace (2026-10-24):** návrh A je implementován v klientovi
(`errorCodeIsTransient` v `error.cpp`, fáze `reconnecting`, soft recovery
ve `VpnEngine::runNetworkThread`, pole `softRecoveryCount` a
`lastSoftRecoveryAt` v `get-diagnostics`, ikona connecting v tray,
dokumentace v `client.md`, šest nových scénářů v
`client/tests/unit/test_runtime_status.cpp`). Odchylky od návrhu:
rozpočet je pevná konstanta `kSoftRecoveryGraceMs = 90 s` místo položky
`runtime.softRecoveryGraceSeconds`, protože každý člen `runtime` je
součástí uzavřeného Wizard IPC schématu a jeho přidání by změnilo tento
kontrakt; `Retry-After` se nerespektuje, transport hlavičky odpovědi
nevydává; bod 5 (retry periodického `health`) nebyl potřeba, connected
smyčka už dnes po neúspěšném `health` zkouší znovu v další iteraci.
Ověření: Windows Debug build (`work/build/debug-soft`) `phpvpn-client-tests`
460 testů bez chyby (včetně šesti nových), `phpvpn-core-tests` 294 bez
chyby; na Fedoře `sh tools/linux/build.sh linux-debug` nad kopií
upravených zdrojů: 100 % testů prošlo. Změny nejsou commitnuté.

### B. Doplňující: omezit dobu, po kterou serializovaný transport blokuje opačný směr

1. **Horní mez velikosti jedné odpovědi.** Protokol dnes zakazuje
   stránkování; navrhnout rozšíření v1.1: volitelné pole
   `maxResponseBytes` v záznamu `auth-full` (klient uvede např. 2 MiB),
   server vydá jen celé dávky do tohoto limitu a zbytek nechá na další
   poll (`drainAll` -> `drainUpTo`). Starší server pole odmítne (400),
   proto klient pole pošle jen po `info`, které ohlásí podporu
   (`limits.maxResponseBytesSupported`). Příjemce na pomalé lince tak
   nikdy nečeká 10 s na jednu odpověď a jeho ACKy odejdou včas.
2. **Alternativa bez změny protokolu:** druhý souběžný HTTP kanál v
   klientovi. Pokud probíhá stahování odpovědi déle než např. 1 s a
   odchozí fronta není prázdná, poslat další `data` request paralelně
   (protokol souběžné requesty téhož klienta povoluje). Vyžaduje
   odstranit zámek `serial_` v transportu a ošetřit souběžné `drainAll`
   (obě odpovědi mohou nést data, to je dnes dovoleno).
3. **Timeout odpovědi podle velikosti.** Absolutní deadline 30 s pro
   odpověď až 16 MiB je na 10 Mbit lince nedosažitelný; po přijetí
   hlaviček prodlužovat deadline podle `Content-Length` (minimálně
   30 s, plus 1 s na každých 100 kB), nebo spolu s B.1 stačí limit
   velikosti.

### C. Doplňující: 5s expirace fronty

`maxQueuedPacketAgeMs` 5 000 ms je pro TCP přes relay s RTT v sekundách
příliš nízké; po soft recovery (A) navíc fronta přežívá výpadek serveru a
musí udržet pakety déle. Návrh: výchozí hodnotu zvýšit na 15 000 ms a
nechat ji konfigurovatelnou (už je), případně expiraci odchozí fronty po
dobu soft recovery pozastavit. Zahazování podle věku ponechat jako ochranu
proti bufferbloatu.

### D. Server (mimo klienta)

Zaseknutí jednovláknového `php-cgi.exe` na 21 s je samostatný problém
testovacího serveru (na hostingu se analogicky projeví jako 5xx nebo
timeout). Pro provoz na Windows použít více FastCGI procesů nebo php-fpm
ekvivalent; pro sdílený hosting nic měnit nelze, proto je A nezbytné.

## Úklid

- Windows: zastaveny monitorovací skripty a sonda; klient odpojen
  (`phpvpn disconnect`) a vrácena původní konfigurace
  z `work\windows-config-backup-20261024.json`
  (`serverUrl https://phpvpn.freehosting.dev/phpvpn.php`); smazány
  testovací soubory ve scratchpadu (`win-upload-1g.bin`, `small-20m.bin`,
  oba stažené 1 GiB soubory); `D:\nginx\conf\nginx.conf` vrácen do
  původní podoby a nginx reloadován, dočasný `logs/phpvpn-timing.log`
  přesunut do `work/ftp-test-20261024/`. Ponecháno: aktualizovaný
  `D:\nginx\html\phpvpn.php`, záloha
  `D:\nginx\html\phpvpn.php.before-testing-20261024.bak`, běžící stack
  nginx + php-cgi, fronty `phpvpn_data` prázdné.
- Fedora: klient odpojen, `tc`/`ifb` shaping odstraněn, smazán adresář
  `/home/fedora/ftp-test` (nahraný `win-upload-1g.bin`,
  `win-upload-1g-wan.bin`, `win-upload-1g-wan2.bin`, `small-20m.bin`,
  vygenerovaný `fedora-download-1g.bin`), monitorovací logy, testovací
  konfigurace a `/var/log/vsftpd.log`. Ponecháno: nainstalovaný phpvpn
  1.0.0-1 (služba aktivní, konfigurace v `/etc/phpvpn/config.json` míří na
  `http://172.18.96.1/phpvpn.php`), nainstalovaný a běžící vsftpd
  s testovací konfigurací (`local_root=/home/fedora/ftp-test`, adresář
  už neexistuje; původní konfigurace v
  `/etc/vsftpd/vsftpd.conf.before-phpvpn-test`), otevřený firewall pro
  ftp a porty 40000–40100, `ftpd_full_access=1`, instalační složka
  `~/phpvpn-linux-dist-20261024`.
