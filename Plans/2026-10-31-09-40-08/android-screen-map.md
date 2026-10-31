# Mapa obrazovek Android klienta

Příloha k [plan-android-v001.md](plan-android-v001.md). Vychází z návrhu
[assets/android-en](../../assets/android-en/README.md) (`manifest.json`, `screen-rules.json`, `screens/*.md`).
Pravidla v posledním sloupci shrnují závazné body z `RULES.md`, plné znění je v souboru obrazovky.

Sloupec **Data** uvádí příkaz IPC v1, který obrazovku plní (volaný přes `NativeBridge.dispatch`), JNI model
nebo lokální stav Androidu. Sloupec **Cesta** je route v Navigation Compose.

## Hlavní destinace (bottom navigation)

| Destinace | Cesta | Obrazovky |
|---|---|---|
| Connection | `connection` | 01, 02, 04, 05, 06, 07, 08 podle stavu z `get-status` |
| Traffic | `traffic` | 26, 27, 43 |
| Server | `server` | 28, 29 (záložky), 44, 45 |
| Settings | `settings` | 10 |

Stavy 01 až 08 nejsou samostatné cesty. Je to jedna obrazovka `connection`, která vykresluje stav ze
`StatusSnapshot` (`state`, `phase`, čtyři checkpointy, `lastError`, `connectPending`). Stejně Traffic
a Server vykreslují varianty podle dostupnosti dat.

## Všechny obrazovky

| ID | Obrazovka | Cesta | Data | WP | Pravidla implementace |
|---|---|---|---|---|---|
| 00 | Welcome | `welcome` | `get-config-state` (chybí identita) | WP08 | Vstup do Wizardu nebo editoru. Vstup do editoru při chybějícím souboru vede přes 36a. Žádná registrace účtu ani adresář serverů. |
| 01 | Connection: connected | `connection` | `get-status` | WP08 | Connected jen pro potvrzený stav. Doba trvání z `connectionStartedAtUtc`, čítače ze statusu. Otevření detailu nikdy nespustí Connect. Text mluví o přístupu do virtuální sítě, ne o ochraně veškerého provozu. |
| 02 | Disconnected | `connection` | `get-status`, příznak `onRevoke` | WP08 | Jedna aktuální konfigurace. Při uděleném souhlasu tlačítko vede rovnou na 04. Po převzetí slotu jinou VPN to říct textem. |
| 03 | VPN permission | systémový dialog | `VpnService.prepare` | WP05 | Dialog vlastní systém. Zrušení není chyba serveru ani úspěch, vede na 37. |
| 04 | Connecting | `connection` | `get-status` à 500 ms | WP08 | Čtyři kontroly jako desktop (URL, secret, access key, kontrakty). Úspěch přejde na 01 sám. Cancel = `disconnect`. Poznámka, že obrazovku lze opustit a průběh je v notifikaci. |
| 05 | Reconnecting | `connection` | `get-status` (`reconnecting`, `recovering`) | WP08 | Odlišit od Connected. Neslibovat dostupnost sítě ani blokování provozu mimo VPN. |
| 06 | Connection error | `connection` | `get-status.lastError`, diagnostika | WP08 | HTTP detail jen když ho klient opravdu zachytil. Chyby vstupu a transportu oddělené. Texty jen z resources podle kódu. |
| 07 | Disconnecting | `connection` | `get-status` à 500 ms | WP08 | Čeká na potvrzení. Timeout nikdy není úspěch, vede na 38. |
| 08 | Status unavailable | `connection` | timeout `get-status` (400 ms) | WP08 | Mobilní obdoba nedostupné služby. Další čtení může vrátit libovolný potvrzený stav. |
| 09 | Background notification | notifikace | stav ze služby | WP05 | Otevře Connection, akce Disconnect. Zmizí ve stavu `disconnected`. Zavření aktivity ≠ zastavení VPN. |
| 10 | Settings | `settings` | lokální | WP08 | Rozcestník: Wizard, kontrakty, editor, About, Exit, baterie, stav Always-on. Žádné profily, země ani účty. Wizard při aktivním spojení přes 11. |
| 11 | Before setup | `wizard/gate` | `get-status`, `disconnect`, pak `wizard-resume` | WP09 | Existující disconnectGate. Jeden `disconnect`, čekat na potvrzený `disconnected` a `idle`, teprve pak jeden `wizard-resume` na krok URL. Odpojený klient obrazovku přeskočí (jen resume). |
| 12 | Server address | `wizard/url` | `wizard-open`, `wizard-check-url` | WP09 | První ze čtyř kroků. Verify mění obrazovku až po výsledku. |
| 13 | HTTP headers | `wizard/headers` | stav Wizardu | WP09 | Úprava, Reset a Cancel hlaviček jako `http_headers_window`. |
| 14 | Server secret | `wizard/secret` | `wizard-check-secret` | WP09 | Vlastní i výchozí secret. Skryté pole s dočasným zobrazením. FLAG_SECURE. |
| 15 | IP and access key | `wizard/identity` | `wizard-check-access` | WP09 | Automatický návrh IP a výchozí access key vypnou pole. Bez návrhu ruční zadání. FLAG_SECURE. |
| 16 | Complete setup | `wizard/finish` | `wizard-finish` | WP09 | Výchozí kontrakty se přidají až při Finish. Volba vypnutá pro nepodporované konfigurace. |
| 17 | Configuration saved | `wizard/saved` | výsledek `wizard-finish` | WP09 | Potvrzené uložení je samostatný stav. Connect je explicitní volba, uložení VPN nespouští. |
| 18 | Verification error | `wizard/error` | chyba kroku Wizardu | WP09 | Varianta `crypto_tag_mismatch`. Timeout vstupy nezneplatní. Hláška nikdy neopakuje secret. |
| 19 | Communication contracts | `contracts` | `get-config-document` + `ContractsModel` (JNI) | WP10 | Seznam s detailem místo tabulky. Všechny kandidátní adresy stránkované, lokální IP vyloučená. |
| 20 | Contract details | `contracts/{ip}` | `ContractsModel` | WP10 | Klíč skrytý. Klíč vypnutého kontraktu jen pro čtení. FLAG_SECURE. |
| 21 | Key from password | `contracts/{ip}/password` | `PasswordKeyModel` (JNI) | WP10 | Password, SHA-256, Default, Random. Vstup UTF-16 bez konverze, mazání po použití. FLAG_SECURE. |
| 22 | Invalid key | `contracts/{ip}` | `ContractsModel.keyErrorVisible` | WP10 | Chyba patří konkrétnímu kontraktu a poli. Ostatní neuložené změny zůstávají. |
| 23 | Configuration editor | `editor` | `get-config-document`, `set-config` | WP10 | Skutečný obsah dokumentu. Soukromé úložiště aplikace, ne cesta z Windows. Při `availability == "missing"` se neotevře prázdný editor, ale dialog 36a. FLAG_SECURE. |
| 24 | Reset configuration | `editor/reset` | `get-default-config` | WP10 | Reset mění jen draft, nezapisuje. |
| 25 | Changes saved | `saved` | `get-status` | WP10 | Při aktivní VPN nabídnout Reconnect, Disconnect dokončit před Connect. Při neaktivní VPN nabídnout Connect. |
| 26 | Traffic statistics | `traffic` | `get-status` (deset čítačů) | WP08 | Všech deset polí. Žádná rychlost, ping, historie ani graf. |
| 27 | Last known statistics | `traffic` | poslední snapshot + chyba čtení | WP08 | Ponechat poslední hodnoty a jasně je označit jako zastaralé. |
| 28 | Who is online | `server` | `get-server-info` + `ServerInfoModel` (JNI) | WP08 | Online s platným kontraktem, online s neplatným, offline. Souhrn, lokální klient vynechán, stránkování, auto refresh 5 s. |
| 29 | Server data | `server?tab=raw` | `get-server-info` (`infoPlaintext`) | WP08 | Čitelný JSON bez přejmenování polí, stejný snapshot jako 28. |
| 30 | HTTP response | `diagnostic/response` | diagnostika Connect nebo Wizardu | WP09 | Záložky Request a Response, upozornění na neúplná nebo rekonstruovaná data. |
| 31 | HTTP request | `diagnostic/request` | dtto | WP09 | Označit zdroj dat (`requestHeadersSource`). |
| 32 | Unsaved changes | dialog | lokální | WP07 | Společný vzor pro Back a zavření Wizardu, editoru a kontraktů. Návrat obnoví původní formulář. |
| 33 | Save result unknown | `save/unknown` | `wizard-get-commit` nebo `get-config-document` | WP09, WP10 | Wizard ověřuje receipt, kontrakty čtou aktuální konfiguraci. Opakovat zápis až po vyhodnocení. |
| 34 | About | `about` | `get-diagnostics`, verze z buildu | WP08 | Verze z `BuildConfig`. Licence Inter (OFL). Nevymýšlet nepodložené informace. |
| 35 | Closing the app | dialog | lokální, `disconnect` | WP08 | „Odpojit a zavřít“ vs. „Zavřít okno“. Nepředstírat vynucené ukončení procesu. |
| 36 | Configuration needs attention | `config/attention` | `get-config-state` | WP08 | Chybějící, neplatná a nečitelná konfigurace. Úprava jen když je dokument čitelný. Chybějící identita → Wizard. Varianta `missing` nabízí Wizard jako primární akci a „Create default configuration and edit“ přes 36a. |
| 36a | Create default configuration (dialog) | dialog nad 00, 10, 36 | `get-config-document` (`missing`), `create-default-config` | WP10 | Nová obrazovka nad rámec návrhu, stejné chování jako Edit Config na desktopu. Ukázat skutečnou cestu ze služby. Ano zapíše hned a otevře skutečný výsledek (`created:false` otevře mezitím vzniklý soubor, nepřepíše). Ne nic nevytvoří. Cancel editoru po vytvoření soubor nemaže. |
| 37 | VPN permission denied | `connection/permission` | výsledek souhlasu, `tunnel_permission_missing` | WP05 | Odmítnutí souhlasu není chyba serveru. Nabídnout zopakování. |
| 38 | Disconnection unconfirmed | `connection` | timeout `disconnect` | WP08 | Žádné falešné potvrzení. |
| 39 | JSON error | `editor` | chyba validátoru ze `set-config` | WP10 | Text z validátoru. Číslo řádku jen když ho validátor dodá. |
| 40 | Reset wizard | `wizard/url` | `wizard-reset` | WP09 | Reset používá výchozí placeholder. |
| 41 | More contracts | `contracts?page=n` | `ContractsModel` | WP10 | Draft podle cílové IP, ne jen viditelných řádků. |
| 42 | Default editor content | `editor/reset` | `get-default-config` | WP10 | Náhled resetu bez přepsání konfigurace. |
| 43 | Loading statistics | `traffic` | před prvním snapshotem | WP08 | Chybějící data nejsou nula. |
| 44 | No server records | `server` | prázdný seznam z `get-server-info` | WP08 | Platná prázdná odpověď ≠ selhání načtení. |
| 45 | Server refresh failed | `server` | chyba `get-server-info` | WP08 | Ponechat poslední úspěšnou odpověď. Bez předchozích dat jen vysvětlení a Retry. |
| 46 | Save checked | `save/checked` | `get-config-document` | WP10 | Potvrzený zápis = úspěch, ne Retry. Konflikt nikdy nepřepíše tiše. |
| 47 | Wizard session expired | `wizard/expired` | `wizard_session_expired` | WP09 | Zastaralý výsledek ověření se nesmí použít k uložení. |
| 48 | Connection checks | `connection/checks` | `get-status` | WP08 | Samostatný pohled na kontroly. Otevření nikdy nespustí Connect. |

## Navigační toky (z `manifest.json`)

- Hlavní cesta: `00 → 12 → 14 → 15 → 16 → 17 → 03 → 04 → 01`. Při uděleném souhlasu se 03 přeskočí.
- Rekonfigurace aktivního spojení začíná 11.
- Odpojení: `01 → 07 → 02`, timeout `07 → 38`.
- Chyba připojení: `06 → 30/31` (diagnostika), `06 → 12` (oprava konfigurace).
- Uložení: `19/23 → 25` (Reconnect nebo Connect), nejistý výsledek `→ 33 → 46`.
- Chybějící konfigurace: `00/10/36 → 36a → 23` (Ano) nebo zpět (Ne). Nad rámec `manifest.json`.
- Neuložené změny: systémové Back na 12–16, 19–22, 23 → 32.
