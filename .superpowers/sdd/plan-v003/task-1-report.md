# Report úlohy 1: D00, D01 a TT01

Stav: DONE. Rozsah: pouze dokumentační D01 a rozhodnutí TT01. Časový limit delegované práce byl koordinátorem předán jako neomezený.

Při obnovení nebo předání kontextu nejprve obnov celý účinný globální blok instrukcí z reálného CODEX_HOME a jeho AGENTS.override.md nebo AGENTS.md, případně z úplného hostem dodaného bloku. Ověř obě přesné samostatné hranice před další věcnou prací. Tento report chráněný blok nenahrazuje.

## Změněné soubory a sekce

- `client.md`, Architektura: současné Windows adaptéry Wintun, WinHTTP, CNG, IP Helper a členění core/platform/apps. libcurl a jiné platformní varianty jsou pouze budoucí možnost.
- `client.md`, nové Sestavení klienta před Specifika platforem: C++20, MSVC, CMake 3.28+, Ninja, statický CRT, tři targety a skutečné EXE. Presety debug, release=RelWithDebInfo a asan, nedoložené distribuční ARM64 vydání a odkaz na existující izolované GUI Release.
- `Plans/2026-09-12-02-29-09/priskok-matice.md`, jediný řádek T01: dokumentace D01 doplněna, TT01 bez nového testu, binární kontrola NOT RUN a odkaz na tento report. Předchozí změny matice zachovány.
- Tento nový report. Žádný další D krok, kód ani test nebyl změněn.

## D00: Výchozí stav

`git rev-parse HEAD` vrátil `529d1c4e53ec48ea95e96c663a62f99af479d5bc`, shodný s plánem v3. `git status --short` před úpravou evidoval existující změny: `.intent-builder-active`, témata T02, T04–T09, T13–T15, T17–T20, T22–T28 a soubory pokryti-zdroju.md a priskok-matice.md ve složce Plans/2026-09-12-02-29-09. Výchozí diff měl 24 souborů, 218 přidaných a 10 odebraných řádků.

Nesledované již byly `.superpowers/`, intent-chat-v004.md a intent-v004.md ve složce Intents/2026-09-12-02-29-09 a overeni-planu-v003.md, plan-v003.md, revize-pracovni.md a rozhodnuti-v004.md ve složce Plans/2026-09-12-02-29-09. Do těchto existujících souborů jsem nezapisovala, s výjimkou výslovně povoleného jediného řádku T01 v matici. client.md, protocol.md, tray_UX.md a adresář client měly před úpravou nulový diff.

Obsahové SHA256 byly získány přes `Get-FileHash -Algorithm SHA256 -LiteralPath <soubor>` před úpravou a po ní:

| Soubor | Výchozí SHA256 | Výsledek porovnání |
|---|---|---|
| `client.md` | `ACE89E97C3C9256A4B7F4D1F42A254E0831B7D20903B324203FE38A55D003EC3` | změněn pouze D01 |
| `protocol.md` | `F1FE29886616149079D9531E1759B6A6DE20DC78C576C82D00EFA8DFB1B38283` | beze změny |
| `tray_UX.md` | `29193CDAF8EDAC14103C29E689E7904A30D08305011DBC3BE0FB0BDB5D946FB2` | beze změny |
| `plan_client.md` | `9411CBA3AB6F9E9DA15C367241B792A674EDA32BBFFA74169F766C7D4420B999` | beze změny |
| `plan2_client.md` | `65F92C4A647C147A5474BD457487FD9FA7600B4981722958A169647E8B4AE03E` | beze změny |
| `plan3_client.md` | `CFE6807F1496181EF473288C6A8AF9A83CECE7AA8CB8033A84FE5CBCCB830E9D` | beze změny |
| `plan3_fix.md` | `4C4BD840650348780E3991B7A9F12EF9C729918C809CB2247336048F926A7449` | beze změny |
| `plan3_fix2.md` | `652FB6B8D368D5ED41CC96CD2B3FAC521D1FDC537C623C3E661BF5DD322ABA7F` | beze změny |
| `plan3_fix3.md` | `E75A2949ECFD30D5C4EBA91FB75C60CA5A6C21C3D055DAED2E16684D40A10971` | beze změny |
| `plan3_fix3_problems.md` | `CB867184055714EF167144D3A980B6C7868AA3F7DC7E17400B494B8D20BFC54F` | beze změny |
| `plan3_fix3_problems2.md` | `D62AEB47DC8E7EF5A6E9A5C2668FCD252BD170A3E0CB78B77DFA2F07C4B5EE87` | beze změny |
| `plan3_fix3_work_for_agents.md` | `5302B6FC5B94E2FE1DB36FFB439907FC60D0F6A5256AE4B70E81039F1AA8B4C7` | beze změny |
| `notes/2026-09-04-18-59-01_notes.md` | `781340408B86AD101324D6FE8FE9A931AD9F827C249EB0DD20B5B4E3506C1CDD` | beze změny |
| `notes/2026-09-04-22-20-52_notes.md` | `6F401B6E607BE51D0D95527056949CE87981FA8DAEB3F8F75AF709DB27166BC3` | beze změny |
| `notes/2026-09-05-08-25-34_notes.md` | `8055D512F8605FB436267003DFC3AA82EB10F409D6F056551AF2AFE66B43EBE9` | beze změny |
| `notes/2026-09-05-09-53-11_notes.md` | `E988C334A2ADE2A94469189FF9B3F8FBC8BB7CC325FAC883DE3D0CA4974D5490` | beze změny |
| `notes/2026-09-06-22-21-54_notes.md` | `794432D692ABEE14EF856C812CA2014F91CD15A1C223B8542464CB181EDBBD6A` | beze změny |
| `notes/2026-09-07-11-19-48_notes.md` | `65A11F5F9673CE536160831B3381FFF498406A00DE7064D01EF02A07B9B9546A` | beze změny |
| `notes/2026-09-09-20-43-00_notes.md` | `FFBFAF636DED678C78F7D99A23DBE0CDD51AB6A6D94D5F309E13F1FB5419E739` | beze změny |

Nový SHA256 client.md: `CCE4A6BEB3F7F42D122A3B431AA415BCAC53BD1E817FE8C172B413DB5FAD6A0B`.

## Důkazy

- `client/CMakeLists.txt:1`: minimum CMake 3.28. Řádky 8–10 vyžadují C++20 bez extensions, řádky 12–14 odmítají jinou platformu než Windows a řádek 16 nastavuje statický CRT. MSVC přepínače jsou v témže souboru.
- `client/src/CMakeLists.txt:1` a `:57`: statické phpvpn-core a phpvpn-platform. Contracts a Password modely jsou samostatné zdroje core, CNG a WinHTTP jsou Windows platformní zdroje. Linkované systémové knihovny obsahují bcrypt, winhttp a iphlpapi.
- `client/apps/CMakeLists.txt:27`, `:36`, `:43`: oddělené service/CLI/tray. OUTPUT_NAME na řádcích 32, 40 a 68 potvrzuje phpvpn-service.exe, phpvpn.exe a phpvpn-tray.exe.
- `client/CMakePresets.json`: Ninja, externí architektura a cl.exe, debug=Debug, release=RelWithDebInfo, asan=Debug s PHPVPN_ENABLE_ASAN=ON, arm64-release=RelWithDebInfo s BUILD_TESTING=OFF.
- `client/src/platform/windows/transport_winhttp.cpp:81` a `:98`: WinHttpTransport a WinHttpOpen. `client/src/platform/windows/crypto_cng.cpp:94`: CngCryptoProvider, jeho BCrypt operace dokládají CNG.
- `client/tests/CMakeLists.txt:6` a `:7`: stávající modelové testy Contracts a Password Key. Opt-in UI targety začínají na řádku 117.
- Podklady D01/T01, rozhodnuti-v004.md a relevantní zdroje S01, S03, S07, S09 byly porovnány s CMake a platformními symboly. `protocol.md` §7.4 omezuje čisté PHP AES-GCM na PHP implementace. `tray_UX.md` UX02 ponechává modely přenosné a Windows adaptéry nativní, změna těchto dvou dokumentů není pro D01 potřebná.

## TT01: Rozhodnutí o testech

Testová změna není pro TT01 potřebná. Funkcionalita modelů má existující unit testy, ale nový test kopírující CMake by neprokázal binární závislosti. D01 nemění implementaci. Pozdější konkrétní binární ověření by vyžadovalo build core/service/CLI/tray a kontrolu linkerových map. Plán v3 a zadání této úlohy takové sestavení kvůli dokumentaci nepožadují.

NOT RUN: nové i existující unit testy, Debug/release/ASAN build, linkerové mapy, ARM64 build/distribuce, GUI, služba, VPN, instalace a E2E. Důvod: čistě dokumentační změna, mimo rozsah TT01. Žádná existence testu ani statické knihovny zde není vydána za nový testový PASS nebo potvrzení výsledku linkování.

## Provedená ověření

- `git rev-parse HEAD`, `git status --short` a `git diff --stat`: zjištěn a zaznamenán výchozí stav výše.
- `git -c core.safecrlf=false diff --check`: exit 0, prázdný výstup, žádné whitespace chyby.
- `git diff -- client.md`: vizuální kontrola dvou změněných oblastí a tabulek, všechny požadavky D01 přítomné.
- `git diff --numstat -- protocol.md tray_UX.md client`: prázdný výstup, kód, testy, protokol a UX beze změny.
- `Get-FileHash -Algorithm SHA256 -LiteralPath <soubor>` pro tři cíle a šestnáct zdrojů, porovnání před/po: všech 16 historických zdrojů a protocol.md/tray_UX.md shodné, mění se pouze obsah client.md.
- `Test-Path -LiteralPath <odkaz> -PathType Leaf`: nové odkazy na client/CMakeLists.txt, client/apps/CMakeLists.txt, client/CMakePresets.json a T01.md existují. Kotva `#izolovaný-vývoj-a-náhled-gui` odpovídá právě jednomu existujícímu nadpisu. Odkaz matice na report po vytvoření a rozbalení relativní cesty přes `[IO.Path]::GetFullPath` vrací PASS.
- Cílené `rg -n` nad CMake, presety, transport_winhttp.cpp, crypto_cng.cpp a registrací stávajících testů: potvrzeny výše uvedené konkrétní hodnoty a symboly.

## Rizika a hranice

Žádný BLOCKED v rozsahu D01/TT01. Binární vlastnosti, ARM64 distribuce a runtime nejsou tímto dokumentačním ověřením potvrzeny. Ostatní textové rozpory určené D02–D17 zůstávají pro následující sekvenční bloky. B01–B05 nebyly realizovány a R04 zůstává odložené. Úvod matice nadále popisuje předchozí plánovací fázi, aktuální pokrok D01 je záměrně evidován pouze v povoleném řádku T01. Nebyl proveden commit, push, checkout, reset, instalace ani mazání.
