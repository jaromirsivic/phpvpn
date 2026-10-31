# WP01: nativní závislosti OpenSSL a curl, 2026-10-31

Výsledek kroku 1 podle [plan-android-v001.md](../../../Plans/2026-10-31-09-40-08/plan-android-v001.md)
a spike T-N04 na úrovni shellu. Co se nespustilo, je NOT RUN s důvodem. Žádný výsledek z emulátoru se nevydává
za výsledek z reálného zařízení.

## Verze a hashe

| Součást | Verze | Zdroj a ověření |
|---|---|---|
| OpenSSL | 3.5.9 (3.5 LTS, vydáno 2026-09-29, poslední 3.5.x k 2026-10-31) | `openssl-3.5.9.tar.gz`, SHA-256 `603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a`; shodně `.sha256` na openssl.org, `.sha256` a digest assetu v GitHub release. Podpis `.asc`: „Good signature from OpenSSL <openssl@openssl.org>“, primární klíč `B146 647E 45A7 B339 47AB 226B 2A2C 87D1 6169 2D40` (uveden v `doc/fingerprints.txt` tarballu). |
| curl | 8.23.0 (vydáno 2026-10-14, poslední stabilní k 2026-10-31) | `curl-8.23.0.tar.xz`, SHA-256 `8dadd7d2536a314f982ad05a651b713f426bf7d02cc2c76cffb9796f2218e6c5`; shodně digest assetu GitHub release `curl-8_23_0` a hash staženého souboru z curl.se. Podpis `.asc`: „Good signature from Daniel Stenberg“, klíč `27ED EAF2 2F3A BCEB 50DB 9A12 5CC9 08FD B71E 12C2`. |
| NDK (Linux) | 28.2.13676358 (r28c) | `android-ndk-r28c-linux.zip`, 722 261 334 B, SHA-1 `a7b54a5de87fecd125a17d54f73c446199e72a64` podle `repository2-3.xml` (balíček `ndk;28.2.13676358`); `sha1sum -c` OK. Rozbaleno do `~/android-ndk/28.2.13676358` ve WSL. |
| Build host | WSL Ubuntu 24.04, 32 jader, CMake 3.28.3, Ninja 1.11.1 | |

Verze a hashe jsou pevně ve skriptu, `manifest.json` je opakuje. `recipe_sha256` skriptu
v manifestu výstupu: `cb82c080f6a4c220f1c9333081df6a1035e9f528a2ddbf63a0007c040ca89b0d`.

## Skript

[`client/tools/android/build-deps.sh`](../../tools/android/build-deps.sh), použití shodné s CI:
`build-deps.sh --ndk <ndk> --out <dir> [--jobs N]`, navíc volitelně `--work`, `--downloads`, `--force`.
Popis v [`client/tools/android/README.md`](../../tools/android/README.md).

- **OpenSSL:** `Configure android-arm64|android-x86_64 -D__ANDROID_API__=34 --libdir=lib no-shared no-module
  no-engine no-legacy no-tests no-apps no-docs no-autoload-config -fPIC`, build v adresáři zdrojů (relativní
  `__FILE__`), `make build_libs`, `make install_dev DESTDIR=...`. Oproti zadání navíc `no-module`, `no-engine`,
  `no-docs` a `no-autoload-config`: za běhu se nic nenačítá z disku. `--prefix` a `--openssldir` míří na
  `/nonexistent/phpvpn-openssl`, takže zakompilované `OPENSSLDIR`, `MODULESDIR` a `ENGINESDIR` na zařízení nikdy
  neexistují a do `libcrypto.a` se nedostane cesta buildu. `crypto_openssl.cpp` potřebuje jen výchozí provider
  (AES-GCM, SHA-256, HMAC přes `EVP_MAC`), `legacy` není potřeba.
- **curl:** CMake, `BUILD_SHARED_LIBS=OFF`, `BUILD_CURL_EXE=OFF`, `BUILD_TESTING=OFF`, bez dokumentace,
  `CURL_USE_OPENSSL=ON`, `CURL_DISABLE_OPENSSL_AUTO_LOAD_CONFIG=ON`, `CURL_CA_BUNDLE=none`, `CURL_CA_PATH=none`,
  `CURL_CA_FALLBACK=OFF`, `ENABLE_THREADED_RESOLVER=ON`, `ENABLE_IPV6=ON`, `CURL_USE_PKGCONFIG=OFF` (při
  cross buildu nesmí najít knihovny hosta), vypnuté zlib, brotli, zstd, libpsl, libidn2, nghttp2, ngtcp2, quiche,
  libssh, libssh2, GSSAPI, gsasl, libuv, c-ares, ECH, HTTPS RR a všechny protokoly kromě HTTP a HTTPS
  (curl 8.23 už nemá volbu `HTTP_ONLY`, proto výčet `CURL_DISABLE_<protokol>`). Výsledek podle CMake:
  `Protocols: http https`, `Features: alt-svc AsynchDNS HSTS HTTPS-proxy IPv6 Largefile SSL threadsafe
  UnixSockets`, `Enabled SSL backends: OpenSSL`.
- **Úpravy výstupu:** odstraněny `bin/curl-config` a `lib/pkgconfig` (absolutní cesty) a případné `share/`.
  Informativní `CURL_LIBRARIES_PRIVATE` v `CURLConfig.cmake` přepsáno na `${PACKAGE_PREFIX_DIR}`. NDK toolchain
  vždy přidává `-g`, proto `llvm-strip --strip-debug` na všech třech archivech (symboly zůstávají). Bez toho měl
  `libcurl.a` 10 MB a obsahoval cesty k NDK.
- **Kontroly ve skriptu před publikací:** verze v `opensslv.h` a `curlver.h`, žádná cesta buildu v CMake
  souborech, žádná cesta buildu ani NDK v archivech, a smoke link všech tří archivů (`--whole-archive`) do jedné
  sdílené knihovny s `--no-undefined -z text`: dokazuje PIC, vzájemné rozlišení symbolů curl ↔ OpenSSL, povolené
  NEEDED (`libc.so libdl.so`) a LOAD zarovnání 0x4000.
- **Reprodukovatelnost:** `SOURCE_DATE_EPOCH` pevně 2026-10-31, `-ffile-prefix-map` pro curl. Dva buildy
  v různých pracovních adresářích (`--work` a výchozí `mktemp`) daly bitově shodné archivy.
- **Idempotence:** druhé spuštění se stejným skriptem a úplným výstupem skončí „is up to date“ s kódem 0,
  `--force` přestaví. Manifest se maže na začátku buildu a zapisuje jako poslední, takže nedokončený výstup
  nevypadá jako hotový.

## Příkazy

```sh
# WSL, NDK a zdroje uvnitř souborového systému WSL
client/tools/android/build-deps.sh --ndk ~/android-ndk/28.2.13676358 \
    --out ~/phpvpn-wp01-build/final --downloads ~/phpvpn-wp01-dl
cp -r ~/phpvpn-wp01-build/final /mnt/d/git/phpvpn/work/android-deps/openssl-3.5.9-curl-8.23.0
```

```powershell
# Windows, CMake 4.4.2 + NDK r28c (Windows host), pro x86_64 i arm64-v8a
cmake -S client -B work/build/wp01-android-<abi> -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo `
  -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake -DANDROID_ABI=<abi> `
  -DANDROID_PLATFORM=android-34 -DANDROID_STL=c++_static -DPHPVPN_BUILD_TRAY=OFF -DBUILD_TESTING=OFF `
  -DPHPVPN_ANDROID_DEPS=D:/git/phpvpn/work/android-deps/openssl-3.5.9-curl-8.23.0
cmake --build work/build/wp01-android-<abi> --target phpvpn-android

# Gradle se SDK CMake 3.31.6
$env:PHPVPN_ANDROID_DEPS='D:/git/phpvpn/work/android-deps/openssl-3.5.9-curl-8.23.0'
./gradlew --no-daemon assembleDebug

# T-N04
cmake -S client/tests/android/tls_probe -B work/build/tls-probe-x86_64 -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake -DANDROID_ABI=x86_64 `
  -DANDROID_PLATFORM=android-34 -DANDROID_STL=c++_static -DPHPVPN_ANDROID_DEPS=<deps>
cmake --build work/build/tls-probe-x86_64
adb -s emulator-5554 push work/build/tls-probe-x86_64/tls_probe /data/local/tmp/
adb -s emulator-5554 shell /data/local/tmp/tls_probe
```

## Výstup

Windows: `D:\git\phpvpn\work\android-deps\openssl-3.5.9-curl-8.23.0` (kopie ověřena SHA-256 proti stromu ve
WSL). Pro Android Studio: `phpvpn.androidDeps=D\:/git/phpvpn/work/android-deps/openssl-3.5.9-curl-8.23.0`
v `client/android/local.properties`.

| ABI | `libcrypto.a` | `libssl.a` | `libcurl.a` |
|---|---|---|---|
| arm64-v8a | 11 182 064 B | 2 064 004 B | 1 769 966 B |
| x86_64 | 11 285 006 B | 2 084 428 B | 1 739 902 B |

SHA-256 archivů:

```
67e39f5f2caafec235b3c15958fbafd3b0c061c1b23d32d9c486acc6412b7c02  arm64-v8a/lib/libcrypto.a
7f4a2b74b09bffdf185f85fb5f566cc6c9f93c4b2cff37b39d5314134a1ea152  arm64-v8a/lib/libssl.a
98d16386d42674ae02ac1d07db66c6264efd4fae9e93d47e16ed15214ccb8617  arm64-v8a/lib/libcurl.a
51b36cba2823cccb457df9cdf82a5d39b9149e5d75c83acf582b4f0ae0833f6b  x86_64/lib/libcrypto.a
c5824b52c65b46d08c4935749a9c71b9df46311a65ce9d05409717d30e1050d3  x86_64/lib/libssl.a
ef714d32f20e25e75cc966f6f31173f734ec5822c650e960bbb580582d747b04  x86_64/lib/libcurl.a
```

Doba buildu obou ABI s 32 úlohami: 57 s (stažené tarbally v mezipaměti).

## Změna v `client/src/CMakeLists.txt`

Jen blok `if(PHPVPN_ANDROID_DEPS)`:

- `list(APPEND CMAKE_FIND_ROOT_PATH "${PHPVPN_ANDROID_DEPS_ABI}")`. Bez toho konfigurace selhala „Could NOT find
  OpenSSL (missing: OPENSSL_CRYPTO_LIBRARY OPENSSL_INCLUDE_DIR)“: NDK toolchain nastavuje
  `CMAKE_FIND_ROOT_PATH_MODE_LIBRARY/INCLUDE/PACKAGE` na `ONLY` a každou cestu mimo kořeny přesměruje pod NDK,
  včetně `OPENSSL_ROOT_DIR` a `CURL_ROOT`. Předsestavený strom musí být vlastním kořenem.
- `find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto SSL)` místo jen `Crypto`, protože blok linkuje
  i `OpenSSL::SSL` a chybějící `libssl.a` má selhat při konfiguraci, ne při linkování.

`find_package(CURL 8.5)` najde config `lib/cmake/CURL/CURLConfig.cmake` (FindCURL zkouší config režim první),
který sám přes `find_dependency(OpenSSL 3)` použije už nalezené statické OpenSSL. `CURL_STATICLIB` v bloku je
nadbytečné (`CURLTargets.cmake` ho dává cíli `CURL::libcurl_static` jako `INTERFACE_COMPILE_DEFINITIONS`), ale
neškodí, ponecháno.

## Výsledky

| Test | Výsledek | Poznámka |
|---|---|---|
| NDK r28c pro Linux stažen a ověřen | PASS | velikost a SHA-1 podle `repository2-3.xml` |
| Tarbally: SHA-256 a GPG podpisy | PASS | dva nezávislé zdroje hashe, oba podpisy „Good signature“ |
| `build-deps.sh` obě ABI | PASS | 57 s, smoke link obou ABI OK |
| Archivy PIC | PASS | smoke link s `-z text` bez chyby; 0 relokací `R_X86_64_32/32S` a `R_AARCH64_ABS32` v archivech |
| Archivy bez cest buildu a NDK | PASS | `strings` 0 výskytů, `OPENSSLDIR: "/nonexistent/phpvpn-openssl/ssl"`, `built on: Sat Oct 31 00:00:00 2026 UTC` |
| Reprodukovatelnost | PASS | tři buildy (pevný `--work` a dvakrát `mktemp`) daly bitově shodné archivy |
| Idempotence a fail fast | PASS | druhý běh „up to date“; chybějící `--ndk`, neexistující NDK, adresář bez `source.properties`, `--jobs 0` končí chybou |
| CMake konfigurace `phpvpn-android` s `PHPVPN_ANDROID_DEPS`, x86_64 a arm64-v8a (CMake 4.4.2) | PASS | po úpravě bloku; „Found OpenSSL ... 3.5.9 ... components: Crypto SSL“, „Found CURL ... CURLConfig.cmake ... 8.23.0“ |
| Build `phpvpn-android` obě ABI, `-Werror` | PASS | 0 varování; `crypto_openssl.cpp` a `transport_curl.cpp` přeloženy proti hlavičkám OpenSSL 3.5.9 a curl 8.23.0 |
| `libphpvpn-android.so`: LOAD 0x4000, NEEDED | PASS | x86_64 56 176 B, arm64-v8a 57 144 B; 3× LOAD 0x4000; NEEDED `liblog.so libm.so libdl.so libc.so`; export jen `JNI_OnLoad` |
| `libphpvpn-android.so` obsahuje symboly curl a OpenSSL | NOT RUN (zatím nelze) | `jni_entry.cpp` z WP00 transport ani crypto provider nevolá, linker objekty z `libphpvpn-platform.a` vynechá (0 symbolů curl/OpenSSL). Stane se ve WP04. Náhradou řádek níže. |
| Celý `libphpvpn-platform.a` (`--whole-archive`) + core + deps do `.so`, `--no-undefined -z text` | PASS | x86_64 17 510 328 B, arm64-v8a 17 965 568 B (s debug info phpvpn); definováno `curl_` 90, `EVP_` 895, `SSL_` 539; řetězce `OpenSSL 3.5.9 29 Sep 2026` a `8.23.0`; NEEDED `liblog.so libm.so libdl.so libc.so`; 4× LOAD 0x4000 |
| `gradlew assembleDebug` s `PHPVPN_ANDROID_DEPS` (SDK CMake 3.31.6) | PASS | `CURL_DIR` a `OPENSSL_CRYPTO_LIBRARY` v `.cxx` cache obou ABI míří do deps; `zipalign -c -P 16 -v 4` „Verification successful“ |
| `curl_version_info` hlásí OpenSSL 3.5 | PASS | na emulátoru: `libcurl 8.23.0 ssl=OpenSSL/3.5.9` |
| T-N04 shell, API 34 (`emulator-5554`, x86_64) | PASS | viz níže |
| T-N04 shell, API 36 (`emulator-5556`, `phpvpn-api36`, x86_64) | PASS | navíc, stejný binární soubor |
| T-N04 v aplikaci (`untrusted_app`) | NOT RUN | patří do WP03; shell (`u:r:shell:s0`) není kontext aplikace |
| T-N04 na arm64 zařízení | NOT RUN | žádné zařízení; arm64 archivy ověřeny jen linkováním |
| Build na Fedora VM (R3) | NOT RUN | použita WSL Ubuntu 24.04 podle zadání WP01; skript je na distribuci nezávislý |
| CI job `deps` | NOT RUN | větev se nepushuje |
| `shellcheck` | NOT RUN | ve WSL není nainstalován; `bash -n` OK |

## T-N04: výstup sondy

[`client/tests/android/tls_probe`](tls_probe/) je samostatný CMake projekt (není součástí buildu klienta),
statický spustitelný soubor 9 609 440 B, NEEDED `libm.so libdl.so libc.so`. Sonda čte každý soubor obou
adresářů přes `PEM_read_X509` do `X509_STORE` (soubory jsou pojmenované `subject_hash_old`, `CApath` by
nefungoval), předává store přes `CURLOPT_SSL_CTX_FUNCTION` (`SSL_CTX_set1_cert_store`) a volá
`GET` s HTTP/1.1, `CURLOPT_SSLVERSION` TLS 1.2+ a `CURLOPT_PROTOCOLS_STR "http,https"` jako `transport_curl.cpp`.
Kontrolní běh s prázdným storem musí selhat, což dokazuje, že ověření prošlo díky systémovým CA.

Shell (`uid=2000(shell)`, `u:r:shell:s0`) oba adresáře přečetl. Oba mají SELinux typ
`system_security_cacerts_file`, APEX adresář `root:shell 0755`, `/system/etc/security/cacerts` `root:root 0755`.

| | API 34 | API 36 |
|---|---|---|
| `curl_version_info` | `libcurl 8.23.0 ssl=OpenSSL/3.5.9` | stejné |
| Funkce | ipv6, ssl, asynchdns, threadsafe; bez libz, http2, idn, psl; protokoly `http https` | stejné |
| `/apex/com.android.conscrypt/cacerts` | 134 souborů, 134 certifikátů, 0 nečitelných, 0 ne-PEM | 149 / 149 / 0 / 0 |
| `/system/etc/security/cacerts` | 134 / 134 / 0 / 0 | 149 / 149 / 0 / 0 |
| www.google.com, ověření se systémovým storem (APEX) | curl 0, HTTP 200, TLSv1.3, `TLS_AES_256_GCM_SHA384`, verify 0 | stejné |
| www.google.com, trustAny | curl 0, HTTP 200, TLSv1.3, verify 20 (řetězec se neověřuje, výchozí store je prázdný) | stejné |
| www.google.com, prázdný store | curl 60, „unable to get local issuer certificate (20)“ | stejné |
| curl.se, ověření | curl 0, HTTP 200, TLSv1.3, `TLS_AES_128_GCM_SHA256`, verify 0 | stejné |
| curl.se, trustAny | curl 0, HTTP 200, TLSv1.3 | stejné |
| curl.se, prázdný store | curl 60 | stejné |
| www.google.com, ověření se záložním storem `/system` | curl 0, HTTP 200 | stejné |
| Výsledek | PASS (0 neočekávaných) | PASS |

Doplňkově na API 34 proti badssl.com (očekávaný výsledek je chyba ověření, sonda je proto hlásí jako
„unexpected“): `wrong.host.badssl.com` s ověřením curl 60 „no alternative certificate subject name matches
target hostname“, `expired.badssl.com` curl 60 „certificate has expired (10)“, oba s trustAny curl 0, HTTP 200,
TLSv1.2 `ECDHE-RSA-AES128-GCM-SHA256`. Kontrola jména (`VERIFYHOST 2`), expirace a TLS 1.2 tedy fungují.
Jeden opakovaný pokus na `wrong.host.badssl.com` skončil curl 35 „Connection reset by peer“ na straně serveru.

Závěr pro WP03: oba adresáře jsou čitelné pro shell na API 34 i 36 a obsahují stejnou sadu. Zbývá ověřit
čitelnost z kontextu `untrusted_app` (T-N04 v aplikaci). Výchozí store OpenSSL je prázdný (trustAny vrací
verify 20), takže bez `CURLOPT_SSL_CTX_FUNCTION` žádné spojení s ověřením neprojde, což je zamýšlené.

## Doporučení pro další balíky

- **Export symbolů (WP04):** až `jni_entry` začne volat transport, objekty curl a OpenSSL se do
  `libphpvpn-android.so` dostanou s výchozí viditelností a exportují se (v testu `--whole-archive` 1 605
  dynamických symbolů `curl_`/`EVP_`/`SSL_`/`OPENSSL_`). `CXX_VISIBILITY_PRESET hidden` platí jen pro vlastní
  zdroje. Doporučuji `-Wl,--exclude-libs,libcrypto.a:libssl.a:libcurl.a` (nebo version script) na cíli
  `phpvpn-android`: menší `.dynsym`, přímá volání bez PLT a žádný konflikt s jinou knihovnou v procesu.
  Neměnil jsem, protože to není potřeba pro správné linkování a cíl je mimo rozsah WP01.
- **CA store (WP03):** stejný postup jako sonda; `X509_STORE_add_cert` duplicity tiše přeskočí,
  `SSL_CTX_set1_cert_store` sdílí jeden store mezi handly.
