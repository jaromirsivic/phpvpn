# Report úlohy 10: D10 a ověření TT11

D10 dokončeno. TT11 inventář i cílené existující testy ověřeny bez přidávání testů. Síťová autentizace všech čtrnácti identit nebyla v tomto bloku spuštěna. D11 ani další bloky nebyly zahájeny. Společný časový limit byl předán jako výslovně neomezený.

Při obnovení nebo předání kontextu obnov celý účinný globální blok instrukcí z reálného CODEX_HOME a jeho AGENTS.override.md nebo AGENTS.md, případně z úplného hostem dodaného bloku. Před věcnou prací ověř obě přesné samostatné hranice. Tento report chráněný blok nenahrazuje.

## Rozsah a dokumentační důkazy

Zadání: D00, D10 a TT11 v [plan-v003.md](../../../Plans/2026-09-12-02-29-09/plan-v003.md), [T14.md](../../../Plans/2026-09-12-02-29-09/T14.md), S03 §10/§11/B05/A29/A30 a [rozhodnuti-v004.md](../../../Plans/2026-09-12-02-29-09/rozhodnuti-v004.md). R07 ponechává platné deterministické klíče, historický plaintext placeholder se nevrací.

- V [client.md](../../../client.md#testovací-profily-serveru) je jedna nová krátká podčást Sestavení klienta. Popisuje výchozí integration s vlastním fixture secretem a čtyřmi klíči .2 až .5 a wizard-default s CHANGE_THIS_SERVER_SECRET a čtrnácti klíči .2 až .15. Klíč opakuje dvouznakový hex posledního oktetu 32krát.
- [tools/deploy-test.ps1](../../../tools/deploy-test.ps1), Profile a Replace-ExactlyOnce, dokládá tyto hodnoty. Každá náhrada SERVER_SECRET, ACCESS_KEYS a BUFFER_DIRECTORY vyžaduje právě jeden výskyt celého očekávaného bloku. Všechny tři kontroly předcházejí prvnímu WriteAllText cílového PHP.
- Nový text odkazuje na [tests-integration/README.md](../../../tests-integration/README.md), který obsahuje parametry izolace a --server-config. Běžná specifikace neobsahuje nový deployment příkaz.
- [protocol.md](../../../protocol.md), zejména 16.7 bod 7, již požaduje autentizaci čtrnácti default adres. [tray_UX.md](../../../tray_UX.md), UX09 a UX11, již popisuje deterministické klíče. Změna těchto dokumentů nebyla nutná.
- V [matici](../../../Plans/2026-09-12-02-29-09/priskok-matice.md) se změnil pouze řádek T14. T14.md i plán zůstaly zachované.

## Existující pokrytí TT11

- [test_crypto.cpp](../../../client/tests/unit/test_crypto.cpp) ověřuje délkovou normalizaci prázdného secretu, zachování 32/64 ASCII nul a neprázdných nulových bajtů, kryptografické vektory a vazbu odvození na vstupy. Komentář o 32 ASCII nulách u normalizace je historicky zastaralý, vlastní assertion správně očekává CHANGE_THIS_SERVER_SECRET. Komentář nebyl v dokumentačním bloku měněn.
- [test_wizard_document.cpp](../../../client/tests/unit/test_wizard_document.cpp), wizard_defaults.AccessKeyAndIndependentPayloadVectors, obsahuje .2/.10/.15/.17 a pevný payload vektor pro obráceně předané .10 a .2. ContractsAndIpSuggestion ověřuje také počty 13/14. [wizard_defaults.cpp](../../../client/src/core/wizard/wizard_defaults.cpp) dokládá numerické seřazení páru před SHA-256.
- [test_default_config.cpp](../../../client/tests/unit/test_default_config.cpp) ověřuje validitu aktuální default šablony. [test_plan3_fix2.cpp](../../../client/tests/unit/test_plan3_fix2.cpp) pokrývá sdílený startup default, zachování existujících bajtů a další metadata/kontrakty, používá fake storage.
- [test_crypto_vectors.py](../../../tests-integration/test_crypto_vectors.py) zahrnuje test_server_secret_normalization pro prázdný secret, jednu i 32/64 ASCII nul a 32 nulových bajtů, včetně návazného odvození. Tato suite nepoužívá síť.
- [test_deploy_profiles.ps1](../../../client/tests/test_deploy_profiles.ps1) spouští kopii deployment skriptu v novém work/build sandboxu. Ověřuje počty 4/14, všech 14 default klíčů, vlastní integration secret a změněnou secret/key-map hlavičku odmítnutou bez změny publikovaného PHP. Netvrdíme, že tento test samostatně mutuje BUFFER_DIRECTORY nebo duplicitu bloků. Tyto meze dokládá čtení Replace-ExactlyOnce, nikoli nový negativní runtime scénář.
- Síťová mezera už má test [test_default_profile.py](../../../tests-integration/test_default_profile.py), test_all_fourteen_default_identities_authenticate. Smyčka polluje .2 až .15 a kontroluje nulové acceptedRecords/acceptedBytes. Poll klienta ověřuje skutečnou odpověď a data-mac. S jiným secretem tento test přeskočí scénář.
- Existuje také C++ [test_server_contract.cpp](../../../client/tests/contract/test_server_contract.cpp), server_contract.all_fourteen_default_clients_authenticate_and_are_reported, který přes WinHTTP/CNG ověří každou identitu, právě jeden data POST a všech 14 adres v info. Jde o samostatný opt-in target v [testovém CMake](../../../client/tests/CMakeLists.txt). Další kopie těchto testů nebyla přidána.

## Spuštěné kontroly

Windows AMD64, 12. 9. 2026, C:\git\phpvpn, větev tray_UX, výchozí HEAD 1c25096111cdf20f901c58da6299ddde7450e229. Pro C++ sestavení načteno prostředí VS 2022 BuildTools přes VsDevCmd.bat -no_logo -arch=x64 -host_arch=x64.

| Příkaz | Výsledek |
|---|---|
| `cmake --build work/build/debug --target phpvpn-client-tests` | exit 0, aktuální Debug target |
| `work/build/debug/tests/phpvpn-client-tests.exe crypto.` | 13 testů, 0 selhání |
| `work/build/debug/tests/phpvpn-client-tests.exe wizard_defaults.` | 2 testy, 0 selhání |
| `work/build/debug/tests/phpvpn-client-tests.exe default_config.document_passes_the_current_validator` | 1 test, 0 selhání |
| `work/build/debug/tests/phpvpn-client-tests.exe fix2.` | 8 testů, 0 selhání |
| `uv run --python 3.13 --with pytest --with cryptography --with requests -- pytest tests-integration/test_crypto_vectors.py -v` | exit 0, Python 3.13.7, pytest 9.1.1, 46 passed |
| `powershell -NoProfile -ExecutionPolicy Bypass -File client/tests/test_deploy_profiles.ps1` | exit 0, PASS obou profilů a odmítnutí změněné hlavičky |
| `git diff --check` a kontrola lokálních Markdown cílů | PASS |

C++ substring filtry měly dohromady 24 nenulových testů bez překryvu. První default_config test startující engine nebyl vybrán, ověření šablony a startup přes fake storage pokrývají zvolené testy. PowerShell fixture zůstala v [deploy-profile-407e4060053240b5a4d9f94172e7857d](../../../work/build/deploy-profile-407e4060053240b5a4d9f94172e7857d). Nevznikl nový síťový server.

## NOT RUN a rizika

- NOT RUN: Python test_default_profile.py a C++ server_contract včetně autentizace všech .2 až .15 proti skutečnému PHP. Read-only inventář našel běžící nginx a php-cgi, ale existující tests-integration/server.json má integration secret, čtyři klíče, URL http://127.0.0.1/phpvpn.php, deployedFile D:\nginx\html\phpvpn.php a buffer D:/nginx/phpvpn-buffer. Připravenost izolovaného wizard-default endpointu nebyla doložena. Tuto fixture jsem nepřenasadila a síťové pollování nespouštěla. Statický inventář ani offline PASS nejsou důkaz tohoto síťového výsledku.
- NOT RUN: celá CTest/Python integrační sada, Release/ASAN, skutečná služba, SCM, Wintun/VPN, ProgramData, instalace, GUI a runtime/vizuální brána. Síťová interoperabilita, PHP normalizace za běhu, HTTPS a proxy zůstávají mimo nový důkaz.
- NOT RUN: nové runtime mutace duplicitní nebo buffer hlavičky. Existující PowerShell test má dvě negativní mutace, jeho PASS nesmí být zobecněn na všechny možné podoby poškození.
- Změny tohoto bloku jsou pouze client.md, řádek T14 a tento report. Výchozí nekomitovaný progress.md patří koordinátorovi. D01–D09 zůstaly zachované. SHA-256 protocol.md, tray_UX.md, PHP, deployment skriptu, server.json a všech šestnácti zdrojových plánů/notes se nezměnily.
- Bez commitu, push, resetu, checkoutu, worktree, subagentů nebo mazání. R04 zůstává odložené, B01–B05 plánované. Úloha končí po D10, nikoli dokončením celé D větve.
