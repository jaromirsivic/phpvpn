# Linuxový klient phpvpn, závěrečné ověření (WP13)

Stav k 26. září 2026. Report uzavírá WP13 plánu `Plans/2026-09-26-08-41-17/plan-v001.md` (oddíly 5/WP13 a 6). Pokrývá negrafickou část akceptace na reálné Fedora 44 VM, kontejnerové běhy Fedora 44 a Ubuntu 24.04, sestavení a podpis linuxových balíčků, zkušební celkovou distribuci a Windows regresi. Grafická akceptace tray v uživatelské relaci GNOME proběhne až po tomto reportu. Její výsledky doplní koordinátor do oddílu [Grafická akceptace tray (GNOME, Fedora)](#grafická-akceptace-tray-gnome-fedora).

Všechny linuxové výsledky v tomto reportu pocházejí z jednoho zdrojového stavu, commitu `2331c7681a26f5624b30b0ba33f6607f0f85fa8d` větve `linux` bez necommitnutých změn (`sourceDirty=false`).

Logy jsou uloženy lokálně v `work/linux-plan/` (soubory `wp13-*.log`, podklady z VM v `work/linux-plan/wp13-vm/`). Adresář `work/` je v `.gitignore`, logy nejsou součástí repozitáře.

## Shrnutí

- Balíčky `phpvpn` a `phpvpn-tray` (RPM z `fedora:44`, DEB z `ubuntu:24.04`) jsou sestavené v čistých kontejnerech z čistého commitu, rpmlint i lintian bez chyb, podepsané a ověřené `verify-distribution.ps1`.
- Instalace podepsané složky na Fedora VM: `rpm -V` čisté, služba `active (running)`, `systemd-analyze security` 2.4 OK, SELinux Enforcing bez AVC, `restorecon -nvR` bez rozdílu.
- Skutečné spojení nainstalované služby s reálným Python relay a druhým klientem v netns: ping, TCP 50 MiB se shodnou SHA-256, **30 minut bez ztráty paketu (1800/1800)**, routy, DNS a NetworkManager po odpojení beze změny.
- Zotavení nainstalované služby: restart, `kill -9`, výpadek relay 60 s a stop s aktivním spojením, vše PASS (32/32).
- Automatické sady na stejném commitu: Fedora debug a ASan ctest, netns 12/12, service E2E 50/50, CLI smoke 28/28, kontrakt PHP i Python 5/5, Ubuntu 24.04 a 26.04 build s unit a netns testy, kontejnerová instalace 61/61 na obou distribucích.
- Žádný provedený test neselhal. Neprovedené scénáře jsou v tabulkách označené NOT RUN s důvodem a shrnuté v oddílu [Neověřené scénáře a omezení](#neověřené-scénáře-a-omezení).

## Prostředí

### Fedora VM (akceptace, build, testy)

| Položka | Verze |
|---|---|
| Systém | Fedora Linux 44 (Workstation Edition), 8 vCPU, 11 GiB RAM, IP 172.31.176.66 |
| Kernel | 6.19.10-300.fc44.x86_64 |
| systemd | 259 (259.9-1.fc44) |
| SELinux | Enforcing, selinux-policy-targeted 43.3-1.fc44 |
| Kompilátory | GCC 16.2.1 20260819, Clang 22.1.8, CMake 4.3.0 |
| Knihovny | OpenSSL 3.5.5, curl/libcurl 8.18.0 (libcurl-8.18.0-4.fc44), systemd-libs 259.9, GLib 2.88.3 |
| GUI | GTK 3.24.52, libayatana-appindicator-gtk3 0.6.0, GNOME Shell 50.0 (rozšíření AppIndicator není nainstalované) |
| Ostatní | NetworkManager 1.56.0, podman 5.8.1, Python 3.14.3 (systém), relay venv Python 3.13.15 (`~/phpvpn-wp06-venv`, `requirements-server.txt`), PHP 8.5.10 |

### Kontejnery sestavení balíčků (provenance)

| Distribuce | Image | Kompilátor a nástroje | Knihovny |
|---|---|---|---|
| Fedora Linux 44 | `registry.fedoraproject.org/fedora:44`, id `5231aa688b62…` | GCC 16.2.1, CMake 4.3.0, rpm-build 6.0.2, rpmlint 2.8.0 | openssl-libs 3.5.8, libcurl 8.18.0-10, systemd-libs 259.9, glibc 2.43, gtk3 3.24.52, ayatana 0.6.0 |
| Ubuntu 24.04.5 LTS | `docker.io/library/ubuntu:24.04`, id `6232b3879100…` | GCC 13.3.0, CMake 3.28.3, debhelper 13.14.1ubuntu5 | OpenSSL 3.0.13, libcurl 8.5.0, libsystemd 255.4, glibc 2.39, gtk3 3.24.41, ayatana 0.5.93 |
| Ubuntu 26.04.1 LTS (informativně) | `docker.io/library/ubuntu:26.04` | GCC 15.2.0, CMake 4.2.3 | OpenSSL 3.5.5, libcurl 8.18.0, libsystemd 259.5 |

### Windows hostitel

Windows 11 Pro 10.0.22631, MSVC 19.44.35228 (Build Tools 2022, toolset 14.44.35207), preset `release`.

## Zdroj a balíčky

Zdrojový commit: `2331c7681a26f5624b30b0ba33f6607f0f85fa8d` (Opravy tray a doplnění dokumentace po WP10), strom čistý. Verze klienta 1.0.0, release balíčků 1.

Sestavení na VM příkazem `client/tools/linux/build-packages-in-container.sh fedora:44|ubuntu:24.04 <out> --source-commit 2331c76… --source-dirty false`. Balíčková sestavení spouštějí unit testy (`%ctest`, `dh_auto_test`): 3/3 ctest PASS v obou distribucích. Lint: rpmlint 0 chyb a 3 varování (`no-url-tag` dvakrát, `invalid-url Source0`), lintian 0 chyb a varování `initial-upload-closes-no-bugs`.

Vydávací složka balíčků `work/linux-packages/1.0.0-1-2331c76/`:

| Soubor | Bajty | SHA-256 |
|---|---|---|
| `phpvpn-1.0.0-1.x86_64.rpm` | 474706 | `98a2ee52d32405f1ff1d8c5c4c67017c97a7feedb9634cfcf9e1ea1400770142` |
| `phpvpn-tray-1.0.0-1.x86_64.rpm` | 427299 | `0d7f9170627a9797fe401251534618520263880145c0b012bb514c117e7d0a86` |
| `phpvpn_1.0.0-1_amd64.deb` | 389064 | `dcdf92378e931d234a81fc4a5648b1161585b21e55c15c8d1ed3c90fe9e455af` |
| `phpvpn-tray_1.0.0-1_amd64.deb` | 364134 | `a5c84ac687a709919fa8c74e92f7360c2ee2f2d2e0ba43d445dd003838cdb46b` |

Podepsaná složka `work/linux-dist/1.0.0-1-2331c76/` (`make-linux-dist.ps1`, vývojový certifikát `CN=phpvpn Development Test Signing`, otisk `7430CCA24F5DE8A6614D582AFBFED7C80E0F2CA8`, není veřejně důvěryhodný): `release-manifest.json` SHA-256 `2b329ae6441465fcfedaa052b68cf5ab48321e2fa466bc54a54ae4d0547fe12d`, `release-manifest.sig` SHA-256 `e8db4df6ad45cd7697009b8eb9e0048722232a65a1cf4d6ba90fb51f00658972`, 9 souborů plus manifest, podpis a certifikát, tray zahrnut.

Pro test upgrade vznikla ze stejného commitu složka s release 2 (`work/linux-packages/1.0.0-2-2331c76/`, `work/linux-dist/1.0.0-2-2331c76/`). Slouží jen testům a do distribuce nepatří.

Zkušební celková distribuce: `tools/make-distribution.ps1 -BuildDirectory ./work/build/release -LinuxPackagesDirectory ./work/linux-packages/1.0.0-1-2331c76 -SignBinaries -StagingOnly` po `client/tools/build-x64.ps1 -Preset release -Configure -ResourceTests`. Výsledek je v `work/wp13-distribution-850bb30a3ed7418fa6bd34ef7d2de9dd/` (staging, `phpvpn.zip` 3401198 B, SHA-256 `50737d03bb9d8f1cf6450d96238355f64ae585cd6a43538463ca9e4ed51409c7`, rozbalená kopie). Projektový `dist/` ani `dist.zip` se neměnily.

## Výsledky podle oddílu 6 plánu

Legenda: PASS = spuštěno a prošlo, FAIL = spuštěno a selhalo, NOT RUN = neprovedeno (důvod uveden). Bez uvedení jiného commitu jde o běh na `2331c76`.

### 6.1 Přenositelné unit testy

| Položka | Stav | Výsledek | Log |
|---|---|---|---|
| `phpvpn-core-tests` (35 souborů ze 6.1 včetně `test_utf8`, `logger` je v samostatném cíli, navíc `test_contracts_save_machine` a `test_tray_arguments`), Fedora linux-debug | PASS | 294 případů, 0 failed | `wp13-linux-debug.log` |
| totéž, Fedora linux-asan (ASan + UBSan) | PASS | 294 případů, 0 failed, bez nálezu sanitizeru | `wp13-linux-asan.log` |
| totéž, Fedora linux-release | PASS | ctest 3/3 | `wp13-linux-release.log` |
| totéž, Ubuntu 24.04 kontejner (GCC 13) | PASS | 294 případů, 0 failed | `wp13-ubuntu2404-build.log` |
| `phpvpn-logger-tests` (Fedora debug a asan) | PASS | 2 případy, 0 failed | `wp13-linux-debug.log`, `wp13-linux-asan.log` |
| Unit testy v balíčkových sestaveních (`%ctest`, `dh_auto_test`) | PASS | 3/3 Fedora, 3/3 Ubuntu | `wp13-build-fedora44-r1.log`, `wp13-build-ubuntu2404-r1.log` |
| Windows release (`phpvpn-core-tests`, `phpvpn-client-tests` a ostatní) | PASS | ctest 10/10 (contract test v této cache není zapnutý) | `wp13-windows-release-2331c76.log` |

Žádný soubor ze seznamu 6.1 nezůstal jen ve Windows cíli.

### 6.2 Linux specifické testy

| Položka | Stav | Výsledek | Log |
|---|---|---|---|
| `phpvpn-linux-tests` bez root (crypto_openssl, clock, config_files, transport_curl, ipc_unix, netlink_codec, modely tray), debug | PASS | 127 případů, 0 failed | `wp13-linux-debug.log` |
| totéž, asan | PASS | 127 případů, 0 failed | `wp13-linux-asan.log` |
| totéž, Ubuntu 24.04 | PASS | 127 případů, 0 failed | `wp13-ubuntu2404-build.log` |
| `phpvpn-netns-tests` (root, vlastní netns), debug | PASS | 12/12 | `wp13-netns-debug.log` |
| `phpvpn-netns-tests`, asan | PASS | 12/12 | `wp13-netns-asan.log` |
| `phpvpn-netns-tests`, Ubuntu 24.04 (rootless privilegovaný kontejner) | PASS | 12/12 | `wp13-ubuntu2404-build.log` |
| Service E2E `run-service-e2e.sh`, debug | PASS | 50/50 (TCP 50 MiB 5308 ms, disconnect 234 ms, SIGTERM se spojením 104 ms) | `wp13-e2e-debug.log` |
| Service E2E, asan | PASS | 50/50 | `wp13-e2e-asan.log` |
| CLI smoke `cli-smoke.sh`, debug | PASS | 28/28, viz poznámka | `wp13-cli-smoke-debug.log` |
| CLI smoke, asan | PASS | 28/28 | `wp13-cli-smoke-asan.log` |
| Kontraktní testy C++ proti PHP relay (`run-server-contract.sh php`) | PASS | 5/5 | `wp13-contract-php.log` |
| Kontraktní testy proti Python relay (`run-server-contract.sh python`) | PASS | 5/5 | `wp13-contract-python.log` |
| linux-clang | NOT RUN ve WP13 | poslední běh fix1 (pracovní strom před commitem 2331c76) 3/3 PASS | `fix1-logs/fix1-linux-clang.log` |
| Headless AT-SPI kontroly tray (fallback, SNI stub, Wizard, Editor/Contracts, Info, Statistics) | NOT RUN ve WP13 | poslední běh fix1: debug i asan vše PASS, 5 kontrol fokusu NOT RUN | `fix1-logs/fix1-gui-summary.log` |

Poznámka k CLI smoke: WP06/07 hlásil 29 kontrol. Kontrola „the production socket path gets the same hint“ se spouští jen tehdy, když neexistuje `/run/phpvpn/control-v1.sock`. Na VM teď běží nainstalovaná služba, proto byla kontrola vynechána. Skript ji nevykazuje jako NOT RUN, zde je tedy uvedena výslovně.

AT-SPI kontroly tray nebyly na release commitu znovu spuštěny. WP13 nezasahoval do grafického prostředí VM s přihlášeným uživatelem (viz incident s at-spi socketem relace ve WP09). Grafickou akceptaci provádí koordinátor s uživatelem.

### 6.3 Reálné ověření na Fedora VM

| Bod | Položka | Stav | Výsledek | Log |
|---|---|---|---|---|
| 1 | `uninstall.sh` z nové podepsané složky (bez `--remove-data`) místo testovacího 1.0.0-2 | PASS | balíčky odebrány, `/etc/phpvpn/config.json` zachován (SHA-256 `dc8b1283…` před i po) | `wp13-vm-install.log` |
| 1 | `install.sh --with-tray --no-weak-deps` | PASS | manifest ověřen, 9 souborů, nainstalováno `phpvpn-1.0.0-1`, `phpvpn-tray-1.0.0-1`, služba enabled a active | `wp13-vm-install.log` |
| 1 | `rpm -V phpvpn phpvpn-tray` | PASS | bez odchylek (po instalaci i na konci WP13) | `wp13-vm-postinstall.log`, `wp13-vm-final-state.log` |
| 1 | `systemd-analyze security phpvpn.service` | PASS | 2.4 OK | `wp13-vm-postinstall.log` |
| 1 | SELinux | PASS | Enforcing, 0 AVC/USER_AVC/SELINUX_ERR od instalace do konce WP13, proces `unconfined_service_t` | `wp13-vm-postinstall.log`, `wp13-vm-final-state.log` |
| 1 | `restorecon -nvR` instalovaných cest, `/etc/phpvpn`, `/var/lib/phpvpn`, `/run/phpvpn` | PASS | žádná změna kontextu | `wp13-vm-postinstall.log` |
| 2 | Reálný relay | PASS | Python `phpvpn.py` (memory storage) na 127.0.0.1:18094 v hlavním namespace, venv s `requirements-server.txt` | `wp13-installed-hold30.log` |
| 2 | Konfigurace headless `phpvpn set-config` neprivilegovaným uživatelem `fedora` | PASS | přijato, `get-config` vrací uloženou konfiguraci | `wp13-installed-hold30.log`, `wp13-vm/hold30/probe-get-config.json` |
| 2 | Konfigurace přes tray Wizard v grafické relaci | NOT RUN | patří do grafické akceptace s uživatelem. Wizard IPC nad reálnou službou pokrývá E2E (50/50). | – |
| 3 | `phpvpn connect`, `status`, `diagnostics`, `info` | PASS | stav connected/running/tunnelReady=yes, `info` vrací oba klienty 10.99.99.2 a .3, diagnostika bez chyb | `wp13-vm/hold30/probe-*.json` |
| 3 | Druhý klient v izolovaném netns (`phpvpn-inst-b`, relay přes unix socket bridge) | PASS | peer connected | `wp13-installed-hold30.log` |
| 3 | Ping a TCP přenos se SHA-256 | PASS | ping 5/5, TCP 50 MiB shodná SHA-256 za 5443 ms | `wp13-installed-hold30.log` |
| 3 | Udržení 30 minut | PASS | 1800/1800 odpovědí, 0 % ztrát, stav po výdrži connected | viz [Výdrž 30 minut](#výdrž-30-minut) |
| 3 | Windows klient na hostiteli proti Linux klientovi | NOT RUN | vyžaduje výslovný souhlas uživatele (plán oddíl 10 bod 7) | – |
| 4 | Odpojení z CLI | PASS | disconnected/idle za 233 ms, rozhraní `phpvpn` a routa 10.99.99.0/24 odstraněny | `wp13-installed-hold30.log` |
| 4 | Odpojení z tray | NOT RUN | grafická akceptace | – |
| 4 | Před a po: `ip route`, `ip -6 route`, `resolvectl status`, `nmcli device` | PASS | všechny čtyři výpisy shodné, výchozí routy a DNS beze změny, `phpvpn` během spojení `unmanaged` | `wp13-vm/hold30/probe.txt`, `wp13-vm/hold30/*-before.txt`, `*-after.txt` |
| 5 | `systemctl restart phpvpn` během spojení (autoConnectOnStartup=true) | PASS | restart 82 ms, znovu connected 323 ms po příkazu, ping OK | `wp13-recovery.log` |
| 5 | `kill -9` procesu služby | PASS | jádro odstranilo TUN za 8 ms, systemd spustil nový proces po 5226 ms (RestartSec=5 s), connected 5245 ms po kill, journal: „retiring the journal of a driver-owned interface already removed by the OS“ | `wp13-recovery.log`, `wp13-vm/recovery/journal-kill9.txt` |
| 5 | Výpadek relay 60 s a obnova | PASS | stav `recovering` po celou dobu výpadku (59/59 vzorků), proces služby přežil, po obnově relay connected za 8295 ms, peer také, čítače viz níže | `wp13-recovery.log`, `wp13-vm/recovery/outage-states.txt` |
| 5 | Stop služby s aktivním spojením ≤ 45 s | PASS | `systemctl stop` 24 ms, rozhraní a routa odstraněny, `Result=success` | `wp13-recovery.log` |
| 5 | Suspend a resume VM | NOT RUN | na VM běží přihlášená grafická relace uživatele, suspend nebyl povolen | – |
| 5 | Reboot VM s `autoConnectOnStartup=true` | NOT RUN | na VM běží přihlášená grafická relace uživatele, reboot nebyl povolen. Start služby s autoConnect ověřen restartem a `systemctl start` (R1, R4). | – |
| 6 | Headless přes `systemctl isolate multi-user.target` na VM | NOT RUN | ukončilo by grafickou relaci uživatele. Nahrazeno čistým kontejnerem bez desktopu (řádek níže). | – |
| 6 | Headless v čistém systemd kontejneru, služba a CLI, balíček `phpvpn` bez GUI závislostí | PASS | Fedora 44 61/61, Ubuntu 24.04 61/61 | viz [Headless důkazy](#headless-důkazy) |
| 7 | Tray v GNOME (s rozšířením i bez, autostart, druhá instance, notice, Exit, odhlášení) | NOT RUN ve WP13 | doplní koordinátor po relaci s uživatelem | [Grafická akceptace](#grafická-akceptace-tray-gnome-fedora) |

### 6.4 Ubuntu

| Položka | Stav | Výsledek | Log |
|---|---|---|---|
| Build a unit testy v `ubuntu:24.04` (linux-debug, GCC 13.3, CMake 3.28) | PASS | ctest 3/3, core 294, linux 127 | `wp13-ubuntu2404-build.log` |
| netns testy v `ubuntu:24.04` | PASS | 12/12 | `wp13-ubuntu2404-build.log` |
| DEB balíčky `phpvpn` a `phpvpn-tray`, lintian | PASS | 0 chyb, `dh_auto_test` 3/3 | `wp13-build-ubuntu2404-r1.log`, `work/linux-packages/1.0.0-1-2331c76/logs/lint-ubuntu-24.04.log` |
| Headless instalace se systemd v kontejneru | PASS | 61/61 | `wp13-install-container-ubuntu2404.log` |
| Ubuntu Desktop VM, body 1–7 z 6.3 | NOT RUN | Ubuntu Desktop VM nebyla k dispozici (plán oddíl 10 bod 8) | – |
| Ubuntu 26.04 build (informativně) | PASS | ctest 3/3, core 294, linux 127, netns 12/12 | `wp13-ubuntu2604-build.log` |

### 6.5 Regrese Windows

| Položka | Stav | Výsledek | Log |
|---|---|---|---|
| `build-x64.ps1 -Preset release -Configure -ResourceTests` na 2331c76 | PASS | build OK | `wp13-windows-release-dist-build.log` |
| ctest release na 2331c76 (worktree cache bez contract testu) | PASS | 10/10 | `wp13-windows-release-2331c76.log` |
| ctest release na a2d3cc9 (hlavní cache s contract testem) | PASS s očekávanou výjimkou | 10/11, jediný FAIL `phpvpn-contract-tests` bez relay na 127.0.0.1:18093 (= baseline) | `wp13-windows-release.log` |
| ctest debug na a2d3cc9 | PASS s očekávanou výjimkou | 10/11, jediný FAIL contract bez relay | `wp13-windows-debug.log` |
| Contract test s lokálním PHP relay (deploy-test wizard-default, `php -S` 18093) na a2d3cc9 | PASS | 5/5 | `wp13-windows-contract.log`, `wp13-contract-deploy.log` |
| ctest asan | NOT RUN ve WP13 | – | – |
| `verify-package.ps1` Windows balíčku | PASS | součást `make-distribution.ps1`: parita skriptů, manifest, hashe, VerifyOnly, ikony všech pěti EXE | `wp13-make-distribution-staging.log` |
| UI testy tray (`-UiTests`) | NOT RUN ve WP13 | poslední běh WP09a, výsledky shodné s neupraveným kódem | `wp09a-tray-ui-tests.log` |
| `make-distribution.ps1 -StagingOnly` s Linux artefakty | PASS | staging, ZIP i rozbalená kopie ověřeny | `wp13-make-distribution-staging.log` |
| `verify-distribution.ps1` na stagingu a rozbalené kopii | PASS | úplný allowlist, hashe zdrojů, LF skripty, podepsaná Linux složka (release 1.0.0-1, source 2331c76) i Windows balíček | `wp13-verify-distribution.log` |
| Service E2E `run-service-e2e.ps1` | NOT RUN | vyžaduje UAC a interakci uživatele | – |

Srovnání s baseline (WP00, b45cd15, 9/10 s jediným FAIL contract bez relay): přibyl jeden test a jediné selhání je stále contract test bez relay. Rozdíl a2d3cc9 → 2331c76 mění jen linuxový tray (`client/apps/tray-linux`, `client/tests/linux/tray`) a dokumentaci (`client.md`, `tray_UX.md`), Windows kód se nemění. Windows výsledky z a2d3cc9 proto platí i pro release commit, release build a ctest jsou navíc potvrzené přímo na 2331c76.

## Výdrž 30 minut

Běh `installed-service-check.sh 30` přes obal `wp13-vm/wp13-hold.sh` (snímky sítě před a po, CLI sondy během výdrže jako uživatel `fedora`). Služba A je nainstalovaná služba v hlavním namespace (10.99.99.2). Peer B (10.99.99.3) je izolovaná instance z `work/build/linux-release` v netns.

| Metrika | Hodnota |
|---|---|
| Začátek spojení | 2026-09-26T11:54:28.502Z |
| Pingy (1 s, timeout 3 s) | 1800 odesláno, 1800 přijato, **0 % ztrát** |
| RTT min/avg/max/mdev | 60.315 / 129.181 / 206.872 / 18.746 ms |
| Stav po výdrži | connected / running / tunnelReady=yes, `recoveryCount` 0, `lastError` null |
| Čítače po 1 min výdrže | dataPollsTotal 1418, ipv4PacketsSent 36292, ipv4PacketsReceived 2384 |
| Čítače v polovině výdrže | dataPollsTotal 18132 (empty 17141, s payloadem 991), ipv4PacketsSent 37131, ipv4PacketsReceived 3224 |
| Diagnostika na konci | health/info/data verified yes, uptime služby 2140 s, 0 recovery |
| Odpojení | 233 ms, rozhraní a routa pryč |
| Síť před a po | `ip route`, `ip -6 route`, `resolvectl status`, `nmcli device` shodné |
| Výsledek skriptu | 17/17 PASS |

RTT kolem 130 ms odpovídá HTTP pollingu přes relay, ne přímé síti. Během výdrže běžely na VM souběžně další sady (build a ctest debug a asan s `nice 10`, netns testy, kontraktní testy, kontejnerové instalace a Ubuntu buildy). Tyto sady nemění síť hlavního namespace. I pod touto zátěží nedošlo ke ztrátě paketu.

Konfigurace nainstalované služby se po každém běhu obnovila: `/etc/phpvpn/config.json` SHA-256 `dc8b12839fa133466130fd0c8f5079ce56e946ca1de16e7f0c40923dd9526532` (stejná jako před WP13), výstup `phpvpn get-config` SHA-256 `22dfbcbe17dbc223f2bb4afb4d93bc86e9679f4e65d1bc3fe25f373933339e3d` (stejný jako před WP13), `autoConnectOnStartup=false`. Služba zůstala nainstalovaná, `active` a `disconnected/idle`.

## Zotavení

Skript `wp13-vm/wp13-recovery.sh` používá stejnou topologii jako výdrž a pro test nastaví `autoConnectOnStartup=true`. Výsledek 32/32 PASS (`wp13-recovery.log`).

| Scénář | Měření |
|---|---|
| R1 `systemctl restart` se spojením | restart 82 ms, connected 323 ms po příkazu, nový MainPID, jedna routa |
| R2 `kill -9` | TUN odstraněn jádrem za 8 ms, nový proces po 5226 ms (`NRestarts=1`), connected 5245 ms po kill, čistá obnova journalu |
| R3 výpadek relay 60 s | `recovering` od prvního vzorku (17 ms), `lastError` `endpoint_unavailable_or_invalid`, connected 8295 ms po obnově relay |
| R4 `systemctl stop` se spojením | 24 ms, `Result=success`, rozhraní a routa odstraněny, po `systemctl start` autoConnect znovu připojil |

Čítače při výpadku relay (stejné logické spojení, `connectionEpoch` 1):

| Okamžik | recoveryCount | dataPollsTotal | ipv4PacketsSent/Received | ipv4BytesSent/Received |
|---|---|---|---|---|
| před výpadkem | 0 | 88 | 6 / 6 | 504 / 504 |
| během výpadku | 1 | 89 | 6 / 6 | 504 / 504 |
| po obnově | 1 | 178 | 11 / 11 | 924 / 924 |

Recovery čítače zachovalo a dál je monotónně zvyšovalo, jak vyžaduje `client.md`. Journal služby za celý WP13 obsahuje jen dvě varování: vstup do recovery při výpadku relay a vyřazení journalu rozhraní po `kill -9` (`wp13-vm-journal.log`). Síťové výpisy před a po zotavovacích testech jsou shodné.

## Headless důkazy

Předchozí kontejnerové běhy (`package-install-container.sh`, rootless podman se systemd, bez desktopu):

| Běh | Fedora 44 | Ubuntu 24.04 | Log |
|---|---|---|---|
| WP08 (jen `phpvpn`, commit 31a9c1a dirty) | 34/34 | 34/34 | `wp08-install-fedora44.log`, `wp08-install-ubuntu2404.log` |
| WP11 (`phpvpn` a `phpvpn-tray`, dirty build) | 61/61 | 61/61 | `wp11-install-fedora44.log`, `wp11-install-ubuntu2404.log` |
| **WP13 (release 1.0.0-1 z 2331c76, rozbalená ověřená kopie ze stagingu, upgrade na 1.0.0-2)** | **61/61** | **61/61** | `wp13-install-container-fedora44.log`, `wp13-install-container-ubuntu2404.log` |

Test pokrývá `--verify-only` (i neprivilegovaně), odmítnutí upraveného souboru, balíčku a manifestu, nezapsaného skriptu a chybějícího certifikátu. Dále instalaci `--headless --no-weak-deps`, aktivní a povolenou službu, `phpvpn status` jako root i neprivilegovaně, socket 0666, konfiguraci 0700 root, `--verify-tunnel` a absenci grafických knihoven. Následuje upgrade na release 2 s restartem a zachovanou konfigurací, `uninstall.sh` se zachováním dat a `--remove-data`. Tray fáze ověří, že automatický režim bez grafického přihlášení tray přeskočí, `--with-tray` nainstaluje soubory, ikony, man stránku a licence, `ldd` vyřeší knihovny, závislost na přesném release, `phpvpn-tray` bez displeje skončí srozumitelnou chybou s kódem 1 a `--headless` povýší instalovaný tray.

## Grafická akceptace tray (GNOME, Fedora)

> **Placeholder, doplní koordinátor po relaci s uživatelem.** Nic z tohoto oddílu nebylo ve WP13 spuštěno. Na VM je nainstalován `phpvpn-tray-1.0.0-1` z podepsané složky, rozšíření AppIndicator není nainstalované a služba je `disconnected` s původní konfigurací.

| Bod 6.3/7 | Stav | Výsledek | Poznámka |
|---|---|---|---|
| Bez rozšíření AppIndicator: ovládací okno a notifikace | | | |
| S rozšířením AppIndicator: ikona, menu, všechna okna | | | |
| Stav tray shodný s `phpvpn status` při změnách z CLI | | | |
| Konfigurace přes Wizard proti reálnému relay | | | |
| Odpojení z tray | | | |
| Autostart po přihlášení | | | |
| Druhá instance | | | |
| `--installation-complete` notice | | | |
| Exit neodpojí VPN, odhlášení neodpojí VPN | | | |
| Fokus, Enter a Escape v oknech (headless NOT RUN) | | | |

## Neověřené scénáře a omezení

- **Suspend a resume VM, reboot s `autoConnectOnStartup=true`:** NOT RUN. Na VM běží přihlášená grafická relace uživatele a reboot ani suspend nebyly povoleny. Start služby s autoConnect je ověřen přes `systemctl restart`, `systemctl start` a restart po `kill -9`. Chování po probuzení (změna času, přerušená TCP spojení) a start v bootu před `network-online.target` zůstávají neověřené.
- **`systemctl isolate multi-user.target` na VM:** NOT RUN ze stejného důvodu. Nahrazeno čistým kontejnerem bez desktopu na obou distribucích.
- **Ubuntu Desktop GUI:** NOT RUN, Ubuntu Desktop VM nebyla k dispozici. Ubuntu je ověřené jen headless (build, testy, balíčky, instalace v kontejneru).
- **Interop Windows ↔ Linux:** NOT RUN, vyžaduje výslovný souhlas uživatele, protože mění hostitelský systém. Oba klienti jsou ověřeni jen proti stejnému relay kontraktu (C++ contract testy na obou platformách).
- **Fokus klávesnice v kontrolách tray:** 5 kontrol fokusu ve Wizard AT-SPI a Focus/Enter/Escape jsou headless NOT RUN (WP10a, fix1). Ověří se v grafické akceptaci.
- **Zrušení během DNS fáze** v transportu libcurl (WP03) není deterministicky testováno.
- **Čekání na EAGAIN při zápisu do TUN** (plný send buffer) není deterministicky vyvoláno žádným testem. TCP přenos 50 MiB ani 30min výdrž ho neodhalily.
- **Ověření dosažitelnosti relay** (`client.md:914`) není zapojené v core pro žádnou platformu. Jde o existující mezeru, mimo rozsah podle oddílu 10 bodu 6.
- **HTTPS:** akceptace běžela přes HTTP na loopbacku s `trustAnyTlsCertificate=true`. Reálný relay přes HTTPS s ověřovaným certifikátem ve WP13 použit nebyl.
- **Windows ASan, UI testy a service E2E** nebyly ve WP13 znovu spuštěny (viz 6.5). Service E2E vyžaduje UAC.
- **linux-clang a headless AT-SPI sady tray** nebyly na release commitu znovu spuštěny. Platí poslední běh fix1.
- **Podpis:** manifest je podepsán vývojovým certifikátem, který není veřejně důvěryhodný. Balíčky nejsou podepsané GPG klíčem distribuce (mimo rozsah).
- **Metadata balíčků k potvrzení s uživatelem:** licence MIT s držitelem „phpvpn contributors“ a placeholder maintainera `maintainers@phpvpn.invalid` (WP08). rpmlint hlásí `no-url-tag`, lintian `initial-upload-closes-no-bugs`, v obou případech jen varování.
- **MTU:** Linux TUN používá 1500 (výchozí jádra). Srovnání s Wintun MTU nebylo možné (WP00).
- **Testovací harness `cli-smoke.sh`:** pomocný proces `nobody` s falešným socketem (`as_nobody python3 … &`) po skončení zůstává běžet, protože `kill` zasáhne jen obalující subshell. Na VM byly nalezeny 2 osiřelé procesy z WP06/07 a 2 z WP13, všechny ukončeny. Oprava skriptu je samostatný úkol. Kontrola produkčního socketu se při běžící nainstalované službě tiše vynechá (28 místo 29 kontrol).
- **Zátěž při výdrži:** 30minutová výdrž běžela souběžně s buildy a testy na stejné VM. Výsledek 0 % ztrát je tím spíš konzervativní, ale RTT statistika není měřena na nečinném systému.

## Úklid

Po WP13 na VM neběží žádný relay, bridge ani testovací instance služby. Neexistuje žádný netns ani adresáře `/run/phpvpn-inst`, `/run/phpvpn-e2e`, `/run/phpvpn-cli-smoke`, rozhraní `phpvpn` ani podman kontejner. Síť hlavního namespace (`ip route`, `ip -6 route`, `nmcli device`) odpovídá stavu před testy (`wp13-vm-final-state.log`). Nainstalované balíčky `phpvpn-1.0.0-1` a `phpvpn-tray-1.0.0-1` zůstávají, služba je `active`, `enabled`, `disconnected/idle` s původní konfigurací.
