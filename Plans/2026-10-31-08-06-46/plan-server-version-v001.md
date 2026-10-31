---
plan_version: 1
created_at: 2026-10-31-08-06-46
source: zadání uživatele v chatu (kontrola shody major verze serveru a klienta ve všech klientech)
---

# Kontrola shody major verze serveru a klienta

## Zadání

Klient při každém získání verze serveru ověří, že **major verze serveru je stejná jako major verze klienta**.
Minor a patch se neporovnávají: klient 4.8.3 funguje se serverem 4.1.5, ale ne se serverem 3.4.1 ani 5.1.1.
Při neshodě klient se serverem dál nekomunikuje a uživatel to uvidí ve stávajícím designu:

- v **Configuration Wizardu** v jeho chybové kartě (oblast „Could not complete this action“),
- po stisku **Connect** v okně průběhu připojení (červený panel s příčinou a nápravou),
- při **periodické kontrole serveru** během připojení (spojení se ukončí a tray přejde do chybového stavu).

Platí pro všechny klienty: Windows (tray + služba), Linux (GTK tray + služba + CLI), macOS (AppKit tray +
služba + CLI). Servery se nemění.

## Výchozí stav (zjištěno v kódu)

- Server posílá `serverVersion` (SemVer, dnes `1.0.3`) ve dvou odpovědích. V `health` je součástí AAD, je tedy
  autentizovaná až po ověření GMAC. V `info` je uvnitř GCM plaintextu. Parsuje ji
  `client/src/core/protocol/response_parser.cpp:41` (health) a `:183-187` (info). Nikde se dnes neporovnává.
  [protocol.md](../../protocol.md) ř. 729 výslovně říká, že jde o informativní hodnotu, ne o vyjednávání.
- Verze klienta je makro `PHPVPN_CLIENT_VERSION` z `client/CMakeLists.txt:3,30,36`. Je dostupné v jádře, ve všech
  trayích, v CLI i v testech. Servery i klient mají stejnou verzi, `tools/bump-version.ps1` je drží v souladu.
- **Žádný tray ani CLI nekomunikuje se serverem přímo.** Všechna volání `health`/`info` běží ve službě nad
  sdíleným jádrem `phpvpn-core`:
  - **Wizard:** `ProbeClient` (`client/src/core/wizard/probe_client.cpp`). Krok URL volá `checkUrl` (:95), tj.
    neověřený `health(false)`. Krok SERVER_SECRET volá `checkSecret` (:100), tj. ověřený `health(true)` a `info()`.
    Krok identity volá `checkAccess` (:105), tj. `health`, `info` a data. Chyba jde přes
    `WizardIpc::safeError` (`client/src/core/ipc/wizard_ipc.cpp:425-444`) do `WizardError{code, description,
    suggestedAction, field}` a všechny tři wizardy ji vykreslí v chybové kartě jako `description + "\n" +
    suggestedAction`:
    - Windows `apps/tray/wizard_window.cpp:512-532`,
    - Linux `apps/tray-linux/wizard_controller.cpp:663-691`,
    - macOS `apps/tray-macos/wizard_controller.cpp:665-680` (kopie Linuxu).
  - **Connect:** `VpnEngine::runConnectSequence` (`client/src/core/runtime/vpn_engine.cpp:475-508`) volá
    `NetworkWorker::performHealth` (`network_worker.cpp:93`) a `performInfo` (`:140`). Chyba se převede na veřejný
    kód v `SafeError::fromConnection` (`status_snapshot.cpp:13-43`). Tray si ho přečte z `get-status` a okno
    průběhu ho přeloží pevnou tabulkou `progressErrorText` (`apps/tray/progress_model.cpp:6-27`). Tu sdílejí
    všechny tři traye. Neznámý kód spadne na `communication_failed`.
  - **Periodicky během připojení:** `health` pro resynchronizaci času každých `timeSyncIntervalSeconds`
    (výchozí 300 s, `vpn_engine.cpp:844-855`) a jednorázově při 401 v soft recovery (`:789`). Opakovaný
    bootstrap (`health` + `info`) proběhne při recovery (`:875-888`). Dnes výsledek resync `health` jen nastaví
    `lastHealthVerified_`.
  - **Na vyžádání:** `get_info` (CLI `phpvpn info`, `vpn_engine.cpp:202-233`) a `get_server_info` (okno Server
    Info s 5s auto-refresh, `:235-268`). Obě volají `performHealth` a `performInfo`/`performExtendedInfo`
    (`network_worker.cpp:~470-512`).
  - Periodický 5s poll trayů (`get_status`) čte jen stav služby, se serverem nemluví.
- Pozor na `SafeError::fromConnection`: jakoukoli neznámou chybu ve fázi `info` dnes ohlásí jako
  `server_verification_failed` („check SERVER_SECRET“). Neshoda verze by tak uživatele poslala špatným směrem,
  pokud nedostane vlastní větev.

## Návrh řešení

Kontrola patří **do jádra, na místa, kde se odpověď ověřuje**. Všechny cesty (wizard, Connect, periodická
kontrola, Server Info, CLI) pak používají jedno pravidlo. Traye jen dostanou nový text pro nový veřejný kód.

### 1. Pravidlo a pomocný modul (jádro)

Nový soubor `client/src/core/protocol/server_version.{h,cpp}`:

```cpp
// MAJOR část SemVer: neprázdná posloupnost ASCII číslic bez úvodní nuly (kromě samotné "0"),
// za ní konec řetězce, '.', '-' nebo '+'. Max. 9 číslic. Jinak nullopt.
std::optional<std::uint32_t> semverMajor(std::string_view version) noexcept;

// Ok, když se MAJOR serveru rovná MAJOR klienta. Jinak Error(server_version_incompatible,
// detail = bezpečná podoba serverVersion). Neparsovatelná verze serveru je také neshoda (fail closed).
Status requireCompatibleServerVersion(std::string_view serverVersion,
                                      std::string_view clientVersion = PHPVPN_CLIENT_VERSION);
```

- Přidat `PHPVPN_CLIENT_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}` vedle `client/CMakeLists.txt:36`. Statický test
  ověří, že se rovná `semverMajor(PHPVPN_CLIENT_VERSION)`.
- **Bezpečná podoba verze pro zobrazení:** hodnota přichází ze serveru, v kroku URL dokonce neautentizovaná.
  Do textu půjde jen tehdy, když má nejvýše 32 znaků z množiny `[0-9A-Za-z.+-]`. Jinak se zobrazí
  „unrecognised“. Žádné řídicí znaky, žádné vkládání textu do UI.

### 2. Nový chybový kód

- `ErrorCode::server_version_incompatible` přidat do skupiny „Response verification“ v `error.h` (za
  `response_mac_invalid`). Musí ležet mimo rozsahy `transport_*`, `server_*` a `crypto_*`, které
  `fromConnection` porovnává intervalem.
- `error.cpp`: jméno `server_version_incompatible`, popis „The server uses a different major version than
  this client.“, `recoverable = false`, `transient = false`. Engine pak neopakuje pokus s backoffem a soft
  recovery ho nepřijme.
- `WizardIpc::safeError`: suggestedAction „Install a PHPVPN client and server with the same major version.
  Client version X.Y.Z, server version A.B.C.“ (verze klienta z makra, verze serveru z bezpečného `detail`).
  Popis se rozšíří v `httpHeadersErrorDescription` stejně jako u stávajících detailů. IPC schéma wizardu se
  nemění, `parseError` přijímá libovolný `code`.

### 3. Místa kontroly

| Cesta | Místo | Kdy |
|---|---|---|
| Wizard, krok URL | `ProbeClient::health(false)` po `parseHealthSyntax` | neověřená hodnota, viz rozhodnutí R1 |
| Wizard, krok SERVER_SECRET a identita | `ProbeClient::health(true)` po `verifyHealth`, `ProbeClient::info()` po `parseInfo` | autentizovaná |
| Connect, recovery, resync času, 401 refresh, `get_info`, `get_server_info` | `NetworkWorker::performHealth` po `parseHealth` a před `timeSync_.publish`; `performInfo` a `performExtendedInfo` po `parseInfo` | autentizovaná |

- Neshoda verze není chyba HTTP výměny. Ve wizardu se proto vrací mimo `diagnosticFailure` a v enginu až po
  `failedDiagnostic->reset()`, aby se k ní nepřipojila HTTP diagnostika (status ji pro tento kód ani nepřijme,
  `status_model.cpp:126-127`).
- `ResponseParser` se nemění. Jeho testy a zobrazování `info` (Server Info, `server_info_model`) zůstávají
  nezávislé na pravidle verze.

### 4. Connect a periodická kontrola (engine)

- `SafeError::fromConnection`: jako **první** větev přidat
  `server_version_incompatible → {"server_version_incompatible", "The server uses a different major version than this client.", recoverable=false}`,
  a to v každé fázi (health, info, running).
- Checkpointy při Connect: neshoda zjištěná v `health` nebo `info` nastaví `serverUrlCheck = failed`
  (červeně se označí první bod „Server URL“, stejně jako ve wizardu, kde se chyba váže na pole Server URL).
  Ostatní body zůstanou `pending`. Schéma `get-status` se nemění, má pevný počet členů.
- **Během připojení:** pokud resync `health` (`vpn_engine.cpp:851-855`) nebo 401 refresh (`:789`) vrátí
  `server_version_incompatible`, engine nastaví `lastError_`, provede `teardown(keepError=true)` a přejde do
  `ConnectionState::error`. Stejně jako dnes u neobnovitelné chyby dat (`:811-814`). Tunel se zruší, data už
  neodcházejí. Tray uvidí v příštím 5s pollu chybový stav: ikona chyby a tooltip „PHPVPN: error“.
- Recovery (`:875-888`) se díky `recoverable=false` zastaví ve stavu `error` a nebude server dál zkoušet.

### 5. Uživatelské rozhraní

**Wizard (Windows, Linux, macOS).** Chybová karta zobrazí text ze služby, tedy popis a nápravu s oběma verzemi.
Zvýrazní se pole podle `field` (`serverUrl`, `serverSecret` nebo `accessKey` podle kroku) a krok v postranním
seznamu se označí jako Failed. Obvykle nejsou potřeba změny kódu UI. Ověřit to testy, a pokud některý wizard
kódy filtruje (`wizard_window.cpp:670-684`, `wizard_controller.cpp:681-690`), doplnit
`server_version_incompatible` do všech tří stejně.

**Okno průběhu Connect (sdílené `progress_model.cpp`, všechny traye).** Nový řádek tabulky
`progressErrorText` (pole z 8 na 9):

- kód `server_version_incompatible`,
- popis „The server uses a different major version than this client.“,
- nadpis panelu „Incompatible server version“,
- náprava „The server is reachable, but this client cannot communicate with it. Install a PHPVPN client and
  server with the same major version.“

Verzi klienta okno doplní z `status.clientVersion` jako „Client version X.Y.Z.“. Serverovou verzi `get-status`
nenese a kvůli pevnému schématu ji tam nepřidávám. Uživatel ji uvidí ve wizardu.
Vykreslení beze změn: Windows `progress_window.cpp:171,198-203`, Linux `tray-linux/progress_window.cpp:290-325`,
macOS `mac_progress_window.mm:218-245`.

**Server Info (Windows `server_info_window.cpp:167-198`, Linux/macOS `server_info_window` + sdílený
presenter).** Při chybě s kódem `server_version_incompatible` zobrazit místo obecného „…server compatibility.“
konkrétní text: „The server uses a different major version than this client (X.Y.Z). Server information cannot
be loaded.“ Pokud chybová odpověď IPC kód do okna nepropouští, doplnit předání kódu.

**CLI (Linux, macOS).** Beze změny kódu. `phpvpn info` vypíše `server_version_incompatible: <popis>` s exit
kódem 1 (`apps/cli/main.cpp:93-108`). `phpvpn status` ukáže `lastError.code` po neúspěšném `connect`.

### 6. Dokumentace

- [protocol.md](../../protocol.md) ř. 164 a 729 a stejná místa v
  [documentation/protocol.en.md](../../documentation/protocol.en.md): `serverVersion` je SemVer a klient
  odmítne server s jinou MAJOR verzí. Přepočítat `documentation/protocol.en.source.sha256` stejným postupem
  jako dosud.
- [client.md](../../client.md):
  - katalog UX05 (ř. 1063): nový kód,
  - kapitola Wizard: kontrola v krocích,
  - kapitola Obnova: neobnovitelná chyba, ukončení tunelu při resync,
  - seznam veřejných chybových kódů IPC.
- Po změnách `graft build`.

## Testy

Všechny nové testy používají stávající harness (`PHPVPN_TEST`, `PHPVPN_CHECK*`). Zelené musí zůstat
i dosavadní sady.

**Jádro, portable (`phpvpn-core-tests`, běží na Windows i v CI na Linuxu a macOS):**

1. `tests/unit/test_server_version.cpp` (nový), tabulka `semverMajor`:
   - platné hodnoty: `4.8.3`, `4.1.5`, `0.9.1`, `1`, `1.0`, `1.0.0-rc.1+build.5`, `10.0.0`;
   - neplatné hodnoty: `""`, `v1.0.0`, `01.0.0`, ` 1.0.0`, `1x`, `.1`, 10místné major, ne-ASCII, řídicí znaky.
2. Tamtéž `requireCompatibleServerVersion`:
   - klient 4.8.3 a server 4.1.5 → OK;
   - server 3.4.1 a 5.1.1 → `server_version_incompatible`;
   - neparsovatelný server → neshoda;
   - bezpečná podoba detailu (32 znaků, zakázané znaky → „unrecognised“);
   - `PHPVPN_CLIENT_VERSION_MAJOR == semverMajor(PHPVPN_CLIENT_VERSION)`.
3. `test_status_contract.cpp`: `fromConnection` dává `server_version_incompatible` s `recoverable=false` ve
   fázích health, info i running. Žádná z nich ho nesmí přeložit na `server_verification_failed`.
4. `test_wizard_ipc.cpp`:
   - katalog `safeError` (smyčka přes všechny kódy, `:211-219`) zahrnuje nový kód;
   - suggestedAction obsahuje obě verze;
   - škodlivá serverová verze se nedostane do textu.
5. `test_tray_progress.cpp` (`AllSafeFailureCategoriesFreezeUntilManualClose`) a `test_wp03_models.cpp`
   (allowlist `progressErrorText`): nový řádek, panel zamrzne do ručního zavření a zobrazí verzi klienta.
6. `test_tray_status.cpp`: status s `lastError.code = server_version_incompatible` se naparsuje a projde bez
   HTTP diagnostiky.
7. Fixtury s natvrdo zapsaným `"serverVersion":"1.0"` (`test_wire.cpp`, `test_runtime_status.cpp`,
   `test_info_extensions.cpp`) přepnout na helper `compatibleServerVersion()` odvozený z
   `PHPVPN_CLIENT_VERSION`. Testy pak nespadnou při budoucím zvýšení major verze.

**Engine a služba (`phpvpn-client-tests`, Windows, `test_runtime_status.cpp` s `FakeTransport`).**
`FakeTransport` dostane nastavitelnou `serverVersion` zvlášť pro health a info.

1. Connect se serverem 2.x v `health` → stav `error`, kód `server_version_incompatible`, `serverUrlCheck=failed`,
   žádný tunel, žádný `data` request, žádný opakovaný pokus po backoffu.
2. Health kompatibilní, `info` s jinou major → stejný výsledek. Pokrývá i server, který by verze v obou
   odpovědích měl různé.
3. Připojený klient a server „povýšený“ mezi resyncy → po uplynutí `timeSyncIntervalSeconds` teardown, stav
   `error`, `lastError` s novým kódem.
4. Soft recovery 401 s refresh `health` vracející jinou major → eskalace do `error`, ne do `recovering`.
5. `get_info` a `get_server_info` → IPC chyba `server_version_incompatible`.
6. `WizardService` (`WizardServiceFixture`):
   - krok URL → chyba s `field=serverUrl`;
   - krok SECRET (neověřená verze kompatibilní, ověřená ne) → `field=serverSecret`;
   - krok identity → `field=accessKey`;
   - u žádného kroku se nepřipojí HTTP diagnostika.
7. Kontrolní tabulka `bootstrap_errors_classify_actual_operation_and_checkpoint` (`:2266-2280`) doplněná o oba
   případy.

**Windows UI (`phpvpn-tray-ui-tests`, interaktivní, mimo CTest):**

- wizard: fake backend vrátí `safeError(server_version_incompatible)` v kroku URL, test ověří text karty a
  zvýraznění pole (vzor `wizardHttpDiagnostic`, `tray_ui_tests.cpp:542-597`);
- okno průběhu: `TrayStatus.errorCode = "server_version_incompatible"`, test ověří nadpis, nápravu, verzi klienta
  a `stage == failed` (vzor `connectDefaultResultFits`, `:878-921`);
- scéna v `dev_preview.inc` pro screenshoty.

**Linux:**

- `phpvpn-linux-tests`, `test_tray_linux_wizard.cpp`: nová větev `Check::versionMismatch` ve `FakeService`
  (`:204-216`) a průchod wizardem s ověřením `presentation().errorText`, `invalidField` a stavu kroku;
- `test_tray_linux_models.cpp`: indikátor ve stavu `error` s novým kódem;
- `tests/linux/tray/mock_service.cpp`: řádek `"version"` v tabulce selhání (`:54-65`) a čekání na „Incompatible
  server version“ v `run-tray-checks.sh` (ruční AT-SPI běh ve VM);
- `tests/e2e/linux/cli-smoke.sh`: relay se `SERVER_VERSION = '<major+1>.0.0'` (stejný `sed` jako `:119-121`).
  `phpvpn info` skončí s exit 1 a `server_version_incompatible`, `phpvpn connect` + `phpvpn status` vrátí
  `lastError.code`. Varianta s major−1. Kontrolní běh s kompatibilní verzí 1.99.0 musí projít.

**macOS:**

- `phpvpn-tray-macos-tests` (`apps/tray-macos/tests/test_tray_macos.cpp`): status item ve stavu `error` s novým
  kódem a text okna průběhu přes sdílený `progressErrorText`;
- macOS kopie `wizard_controller.cpp` dnes nemá žádné testy. Doplnit minimální test prezentace chybové karty pro
  `server_version_incompatible` (fake host podle Linuxu), aby macOS wizard nebyl kryt jen předpokladem, že je
  kopií;
- `tests/e2e/macos/run-service-e2e.sh` (GitHub Actions): druhý PHP relay s upravenou `SERVER_VERSION`;
  `info` selže s novým kódem, `connect` skončí v `error`; wizard probe (`wizard_ipc_probe`) v kroku URL vrátí
  nový kód.

**Kontrakt se skutečnými servery** (`tests/contract/test_server_contract.cpp`, PHP/Node/Python relay): přidat
kontrolu, že `serverVersion` ze serveru projde `requireCompatibleServerVersion`. Zachytí to vydání, kde by se
klient a servery rozešly.

**Ověření po implementaci:**

- Windows: build `build-x64.ps1` a CTest;
- Linux: `ctest -L "portable|linux"` v kontejneru nebo VM a `cli-smoke.sh` ve Fedora VM;
- macOS: push větve a workflow `client-macos.yml`;
- ruční kontrola wizardu a Connect proti lokálnímu serveru s dočasně změněnou `SERVER_VERSION`.

## Rozhodnutí k potvrzení (uvedena doporučená varianta)

- **R1, krok URL ve wizardu:** kontrolovat už neautentizovanou verzi z `health` v kroku URL. Uživatel se o
  nekompatibilním serveru dozví hned po zadání adresy. Bezpečnostně je to v pořádku: podvržená hodnota může
  vést jen k odmítnutí, nikdy k přijetí, a v dalších krocích se verze ověří znovu autentizovaně. Alternativa:
  kontrolovat až od kroku SERVER_SECRET.
- **R2, neparsovatelná `serverVersion`:** brát ji jako neshodu (fail closed).
- **R3, Server Info a `phpvpn info`:** při neshodě také odmítnout s konkrétním textem. Alternativa: tyto
  diagnostiky nechat fungovat, protože jen zobrazují informace.
- **R4, checkpoint v okně Connect:** neshodu označit na prvním bodu „Server URL“.
- **R5, verze v okně Connect:** zobrazit jen verzi klienta. Serverovou verzi ukazuje wizard, protože rozšíření
  schématu `get-status` by rozbilo kompatibilitu tray–služba.

## Pořadí implementace

1. Jádro: `server_version.{h,cpp}`, `ErrorCode`, `error.cpp`, CMake makro, portable unit testy.
2. Kontroly v `ProbeClient` a `NetworkWorker`, `fromConnection`, chování enginu při resync, testy enginu a služby.
3. `WizardIpc::safeError` a popis, testy wizard IPC.
4. Sdílený `progressErrorText`, Server Info ve třech trayích, testy trayů (Windows UI, Linux, macOS).
5. E2E skripty Linux a macOS, kontraktní test.
6. Dokumentace a `graft build`.
7. Build a testy na Windows, Linuxu (VM) a macOS (CI). Výsledky zapsat do reportu v `client/tests/`.
