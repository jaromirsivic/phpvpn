# B2: placeholder adresy serveru v linuxovém tray, ověření na Fedora VM (2026-11-01)

Ověření linuxové části změny B2 (výchozí `serverUrl` je placeholder
`https://change-this-server-url.invalid`, `connect` ho odmítá kódem
`config_server_url_unset`, tray místo chyby položí otázku a Ano otevře průvodce)
na větvi `android`, stav 759471f plus opravy skriptů z tohoto reportu.

## Prostředí

| Položka | Hodnota |
|---|---|
| VM | Fedora 44 Workstation (Hyper-V), `172.27.16.228`, uživatel `fedora`, jádro 6.19.10 |
| Kompilátor | GCC 16.2.1, preset `linux-release` jako v CI (`-DPHPVPN_BUILD_PRIVILEGED_TESTS=OFF`) |
| Strom | `~/phpvpn-b2` (`git archive` z `android` 759471f) |
| Relay pro `wizard` mód | `PHPVPN_SERVER_PYTHON=~/phpvpn-wp06-venv/bin/python` |

Build: všechny cíle kromě `phpvpn-android-host-tests` (GCC 16 hlásí falešné
`-Wfree-nonheap-object` v `tests/android/test_netconfig_android.cpp:167`; CI na
Ubuntu s GCC 14 prochází, s touto změnou to nesouvisí).

## Výsledky

| Sada | Výsledek |
|---|---|
| `ctest -L "portable\|linux" -E android-host` | 2/2 PASS (31,8 s) |
| `run-tray-checks.sh … fallback` | 9 PASS, 0 FAIL: nová kontrola „placeholder server URL: question instead of an error, No does nothing, Yes opens the Wizard“ PASS (mock `config placeholder`), Connect → otázka `The server URL has not been configured yet.\nDo you want to set it up now?`, No zavře bez okna průběhu, Yes otevře Configuration Wizard; dál původní tok (`config configured`) |
| `run-tray-checks.sh … sni` | 3 PASS, 0 FAIL |
| `run-wizard-checks.sh … wizard` (skutečná služba v netns + Python relay) | 15 PASS, 0 FAIL, 6 kontrol fokusu NOT RUN (okno v headless mutteru není aktivní, jako dřív). Nový krok 0: Connect na výchozí konfiguraci vytvořenou službou → otázka, No nechá `disconnected idle`, Yes otevře krok Server URL; krok 1 ukazuje `https://change-this-server-url.invalid`; `http://CHANGE-THIS-SERVER-URL.invalid/phpvpn.php` odmítnut lokálně s „Replace change-this-server-url.invalid with your server address.“; Reset vrací placeholder |
| `run-config-checks.sh` (editor, deadline, contracts) | všech 31 PASS; mock s `--config-dir` odmítá Connect na placeholder stejně jako služba |

## Zastaralé kontroly opravené při tomto běhu

Oba skripty byly naposledy spuštěny před změnami z 30. a 31. 10. a dvě kontroly
proto padaly nezávisle na B2 (ověřeno: se stromem před těmito změnami procházejí):

- `run-wizard-checks.sh`: od commitu 6e969be („default secret toggle off“) má
  výchozí šablona `useDefaultServerSecretInWizard: false`, skript čekal zapnutý
  přepínač. Teď čeká vypnutý, zapne ho a ověří varování „Default server secret in use“.
- `run-tray-checks.sh`: od commitu 12e3ac1 (odmítnutí jiné hlavní verze serveru)
  je vysvětlení v okně průběhu jeden popisek i s verzí klienta; `tray_atspi.py`
  dostal `wait-prefix` a skript ho pro tento text používá.

## Neprovedeno

- `run-info-checks.sh` (Server information, Statistics): změna se jich netýká.
- macOS tray: jen kompilace a jednotkové testy v CI (`client-macos`, krok
  instalace `.pkg` padá nezávisle na této změně).
