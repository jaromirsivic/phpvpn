# B2: placeholder adresy serveru a kód `config_server_url_unset` v Android klientu (2026-11-01)

Android část změny jádra z commitu `905a04d` (výchozí konfigurace nese
`https://change-this-server-url.invalid`, `connect` ji odmítá kódem
`config_server_url_unset`). Větev `android`, pracovní strom
`worktree-agent-a4e6bbc1a6ae1ac46`. Jádro (`client/src/core`) se neměnilo.

## Co se změnilo (vše pod `client/android`)

### Zrcadlo chybového kódu
- `bridge/.../errors/ErrorCode.kt`: `CONFIG_SERVER_URL_UNSET("config_server_url_unset", false)`
  hned za `CONFIG_INVALID_URL` (stejné pořadí jako `error.h`, hlídá
  `ErrorCompletenessTest`).
- `bridge/.../errors/ErrorTextKey.kt`: `ERROR_CONFIG_SERVER_URL_UNSET`.
- `app/.../errors/ErrorStrings.kt` + `values/errors.xml`
  („The server URL has not been configured yet.“) + `values-cs/errors.xml`
  („Adresa serveru zatím není nastavená.“).

### Zrcadlo placeholderu
- `WizardRules.PLACEHOLDER_HOST = "change-this-server-url.invalid"` (zrcadlo
  `kPlaceholderServerHost`) a nová konstanta `WizardRules.PLACEHOLDER_URL`
  (`https://` + host, zrcadlo `kPlaceholderServerUrl`).
- `strings_wp09.xml`: EN „Replace change-this-server-url.invalid with your
  server address.“, CS „Nahraď change-this-server-url.invalid adresou svého
  serveru.“
- Debug fake služby: `FakeWizardService.PLACEHOLDER_URL` → `WizardRules.PLACEHOLDER_URL`,
  `ConfigReplies.document()` má v `serverUrl` placeholder.
- `bridge/src/androidTest/.../RuntimeHarness.document()` nahrazuje v šabloně
  nový řetězec placeholderu.
- JSON fixtury v `bridge/src/test/resources/ipc` i
  `bridge/src/androidTest/resources/ipc` (shodné sady): `default-config.json`,
  `default-config-document.json`, `wizard-fields.json`, `wizard-view-create.json`
  (dřív `http://modify-this-url/phpvpn.php`), `wizard-view-edit.json`,
  `wizard-view-edit-connected.json`, `wizard-view-repair-peers.json`,
  `wizard-view-reset.json`, `config-document-valid.json`,
  `config-document-repairable.json`, `create-default-config-created.json`.
  Ponecháno: `wizard-error-unauthorized.json` a argumenty v `IpcClientTest`
  / `WizardIpcTest` (obyčejná testovací URL, ne výchozí dokument).
- Testy `WizardRulesTest` a `WizardDraftTest` používají nový placeholder
  (včetně varianty s velkými písmeny a portem).

### Dialog po odmítnutém Connectu (obrazovka 02)
- `ConnectionViewModel.handleRefusal`: kód `config_server_url_unset` má
  přednost před obecným směrováním `config_*` → 36 a vysílá nový efekt
  `ConnectionEffect.ServerUrlUnset`; `refusedConnect` zůstává null.
- `ConnectionScreen`: efekt zapne dialog (stav v `rememberSaveable`, přežije
  otočení). Dialog je ve stylu aplikace (`Dialog` + `DialogCard` jako 36a a
  dialog vypnutých oznámení, barvy jen z `PhpvpnTokens`): titulek „Server
  address not set“, text „The server URL has not been configured yet. Do you
  want to set it up now?“, tlačítka „Set up“ (primární → `openWizardUrl`,
  tedy 12 `wizard/url`) a „Not now“ (jen zavře). CS: „Adresa serveru není
  nastavená“ / „Adresa serveru zatím není nastavená. Chcete ji nastavit teď?“
  / „Nastavit“ / „Teď ne“. Test tagy `ConnectionTags.SERVER_URL_UNSET_DIALOG*`.

### Upozornění na 02
- Při `availability == valid` a hostiteli `serverUrl` rovném
  `WizardRules.PLACEHOLDER_HOST` (bez ohledu na velikost písmen; pomocná
  funkce `ConfigSummary.hasPlaceholderServerUrl()`) se místo „Configuration
  ready“ zobrazí varovné upozornění „Server address not set“ / „Enter the
  address of your PHPVPN server before connecting.“ s tlačítkem „Set up“
  (→ 12). CS: „Adresa serveru není nastavená“ / „Před připojením zadejte
  adresu svého serveru PHPVPN.“ / „Nastavit“. Connect zůstává povolený.
- Komponenta `Notice` dostala volitelný slot `action` (tlačítko pod textem).
- Welcome 00 beze změny (jen pro chybějící soubor).
- Nové texty jsou ve `strings_wp08.xml` (EN i CS, stejné klíče).

### Testy
- `ConnectionViewModelTest`: `refusedConnectWithThePlaceholderUrlAsksToSetItUpInsteadOf36`
  (efekt `ServerUrlUnset`, žádné 36, žádné upozornění o odmítnutí, stav
  zůstává Disconnected) a `placeholderServerUrlIsRecognisedByHostOnly`.
- `MainFlowsTest` (instrumentovaný, WP08 harness):
  `placeholderServerUrl_02_noticeAndRefusedConnectDialogLeadTo12` (upozornění
  místo „Configuration ready“, Connect → dialog, Not now zavře bez dalšího
  textu, Set up → route `wizard/url`) a
  `placeholderServerUrl_02_setUpInTheNoticeOpens12`.

## Co bylo ověřeno

Build gate (`JAVA_HOME=D:/androidstudio/jbr`,
`./gradlew :bridge:testDebugUnitTest :app:testDebugUnitTest :app:assembleDebug`):
BUILD SUCCESSFUL, `:bridge` 109 JVM testů / 0 selhání, `:app` 186 JVM testů /
0 selhání (včetně `ErrorCompletenessTest` proti `error.h`/`error.cpp`
a `Wp08StringResourcesTest`).

Emulátor `phpvpn-api36` (API 36, headless, port 5556), čistá instalace
debug APK, `appops ACTIVATE_VPN allow`, skutečná služba (výchozí konfiguraci
vytvořila služba při startu):

| Krok | Výsledek | Snímek (`work/b2/`) |
|---|---|---|
| Start aplikace → 02 s kartou serveru `https://change-this-server-url.invalid` a varováním „Server address not set“ | PASS | `02-server-url-unset-notice.png`, `02-server-url-unset-notice-scrolled.png` (s tlačítkem Set up) |
| Connect VPN → služba odmítla, dialog „The server URL has not been configured yet. Do you want to set it up now?“ se Set up / Not now, žádné 36 | PASS | `02-connect-dialog-server-url-unset.png` |
| Set up v dialogu → obrazovka 12 Server address s předvyplněným placeholderem | PASS | `12-wizard-url-after-set-up.png` |
| Debug broadcast `phpvpn.android.debug.CONNECT` | logcat `phpvpn-debug: connect failed config_server_url_unset`, `phpvpn: Connect refused: config_server_url_unset`, stav zůstal disconnected | `02-after-debug-connect-broadcast.png` |

Instrumentované sady (`client/tests/android/run-instrumented.sh emulator-5556 …`,
bez relay, logy ve `work/b2/logs/`):
- `:app` `phpvpn.android.ui.MainFlowsTest`: 18 PASS / 0 FAIL (včetně dvou nových).
- `:app` `phpvpn.android.wizard.WizardFlowTest`: 15 PASS / 0 FAIL
  (`resetLoadsTheDefaultPlaceholder` s novým placeholderem).
- `:bridge` celá sada: 49 PASS / 0 FAIL / 1 SKIP (T-A13, potřebuje adb root;
  stejně jako v matici WP11). `IpcDispatchTest` porovnává
  `default-config.json` se skutečnou odpovědí `get-default-config` nativní
  služby, takže fixtury odpovídají jádru.

Emulátor byl po ověření ukončen (`emu kill`). Telefon se nepoužil.

## NOT RUN
- Ostatní instrumentované třídy `:app` (`Wp08ScreenshotTest`, WP10 editor
  a kontrakty, `VpnServiceTest`, `RealRelayTest`) a matice API 34/35: netýkají
  se změny, případně vyžadují relay.
- Ruční kontrola české lokalizace na emulátoru (texty ověřeny jen přes
  `Wp08StringResourcesTest` a shodné klíče).
- Otočení displeje s otevřeným dialogem (stav je v `rememberSaveable`, ale
  neověřeno na zařízení).

## Odchylky od popisu
- Dialog je postaven na `DialogCard` aplikace uvnitř Compose `Dialog`
  (styl 36a a dialogu vypnutých oznámení), ne na composable
  `androidx.compose.material3.AlertDialog`; `DialogCard` potřebuje titulek,
  použit je „Server address not set“ a text otázky je odstavec pod ním.
- Přidána konstanta `WizardRules.PLACEHOLDER_URL` (popis zmiňuje jen host).
- `Notice` má nový volitelný slot `action`; jinde se zatím nepoužívá.
