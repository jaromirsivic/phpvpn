# Implementační plán 3 fix 2: startovní konfigurace a trvalé volby Wizardu

Stav navazující implementace ze 6. září 2026 je doložen v [reportu](client/tests/implementation-plan3-fix2-report.md) a [akceptační matici](client/tests/implementation-plan3-fix2-matrix.md). Text níže zachovává plán a stav v okamžiku jeho přípravy, nikoli tvrzení o výsledku pozdějších testů.

## 1. Autorita, rozsah a výchozí stav

Tento dokument je plán budoucí implementace. Vznikl 6. září 2026 podle celého [formálního zadání](notes/2026-09-06-22-21-54_notes.processed.md). Software v rámci přípravy plánu nebyl změněn ani spuštěn. Formální zadání má v dotčených otázkách přednost před staršími pravidly v `client.md`, `plan3_client.md` a `plan3_fix.md`.

Označení **P** níže znamená potvrzený požadavek zadání. **F** označuje fakt ověřený ve zdrojích aktuálního stromu. **T** označuje technické rozhodnutí tohoto plánu, které konkretizuje implementaci, ale není vydáváno za doslovné uživatelské zadání. Názvy navržených nových symbolů jsou výslovně označené.

Výchozí HEAD při analýze je `9b4bd48` s názvem `Uprava klientske aplikace polozky edit config`. Původní Plan 3 je commit `fcbc31d`. V aktuálním kódu už existuje `checkDirectorySecurity()` a chyba `config_directory_security_invalid`. Oprava diagnostiky ACL podle předchozího plánu tedy již ve zdrojích je. Její historický report `client/tests/implementation-plan3-fix-report.md` uvádí vlastní provedené a neprovedené kontroly. Tento plán nepřisuzuje jejich výsledky nové funkčnosti a neověřoval instalované binární soubory.

Při zahájení existovaly cizí rozpracované změny: odstraněný `.note-taker-active` a dva nové soubory poznámek z této relace. Cílový `plan3_fix2.md` neexistoval. Tyto změny i původní dokumenty zůstávají zachované.

### Potvrzené požadavky

| ID | Požadavek |
|---|---|
| P01 | Při každém startu služby po bezpečné přípravě a recovery jednorázově ověřit autoritativní config. Pouze skutečně chybějící soubor atomicky vytvořit. |
| P02 | Existující soubor včetně nevalidního, nečitelného, příliš velkého nebo chybně kódovaného ponechat bajtově beze změny. Nedoplňovat do něj při startu metadata. Souběžně vzniklý cíl nepřepsat. |
| P03 | Startup, Edit Config Reset a explicitní službové vytvoření používají jednu úplnou šablonu. Její dosavadní obsah včetně jediného kontraktu zůstane stejný, přibudou pouze tři bool členy s hodnotou true. |
| P04 | Tři členy `masterSetup` s příponou `InWizard` jsou volitelné, striktně booleovské a určují pouze stav Wizardu. Staré dokumenty zůstávají platné s přesně určeným odvozením chybějících voleb. |
| P05 | Krok 2 načítá a ukládá volbu výchozího secretu společně s výsledným konkrétním secretem. |
| P06 | Krok 3 načítá a ukládá automatickou IP a výchozí Access Key. Automatický návrh nahrazuje i uložený placeholder. Chybějící návrh dovolí vypnout automatiku a zadat IP ručně. |
| P07 | Poslední krok vždy zobrazuje volbu doplnění chybějících výchozích kontraktů, včetně edit/repair a úplné sady. Při novém otevření je zaškrtnutá. Mimo podporovaný CIDR je viditelná, zakázaná a vysvětlená. |
| P08 | Doplnění je aditivní, zachová existující kontrakty i přesné payload klíče, vynechá vlastní IP a vytváří pouze chybějící kandidáty `.2` až `.15`. Odškrtnutí ani úplná sada existující kontrakty nemění. |
| P09 | UI ukazuje počet existujících a počet chybějících výchozích kontraktů. Volba kontraktů se do konfigurace neukládá. |
| P10 | Finish v edit/repair patchuje čtyři konkrétní hodnoty a tři nové příznaky v původním DOM a případně doplní kontrakty. Zachová ostatní neprezentované členy včetně jejich nepřítomnosti. |
| P11 | Chyba automatického vytvoření ponechá službu dostupnou přes IPC a diagnostiku. Bez platné konfigurace zůstane VPN fail-closed. Startup nepoužívá tray IPC a instalátor neudržuje vlastní JSON šablonu. |

### Ověřené vazby na aktuální implementaci

| ID | Soubor a symbol | Dnešní stav a dopad |
|---|---|---|
| F01 | `client/apps/service/service_host.cpp`, `ServiceRuntime::start()` | Připraví adresář, omezí ACL, vytvoří backend a `ConfigStore`, zavolá `recover()`, poté spustí engine a IPC. Default nevytváří. Chybu recovery nyní vrací ještě před startem IPC. |
| F02 | `client/src/core/config/config.h`, `kDefaultConfigDocument`, `config_defaults.cpp`, `makeDefaultEditorDocument()` | Existuje jedna úplná šablona s lokální URL, IP `.2` a kontraktem `.3`. Nové příznaky chybějí. |
| F03 | `client/src/core/config/config_store.cpp`, `read()`, `createDefault()`, `save()` | Čtení rozlišuje dostupnost. `createDefault()` validuje společnou šablonu a publikuje `createIfMissing`. `save()` publikuje `replace`. Obě cesty jsou serializované. |
| F04 | `client/src/platform/windows/config_files_win.cpp`, `readBounded()`, `publish()`, `recoverLocked()` | Backend používá připnuté adresáře, NTFS, chráněné staging soubory, žurnál, flush a atomický rename. Recovery nedokončuje nepublikovaný commit a neopravuje obsah cíle. |
| F05 | `client/src/core/config/config_validator.cpp`, `validateConfigValues()` | Uzavřený `kMasterMembers` obsahuje šest dosavadních členů. `ConfigValues` je neúplný dokumentový model, `parseAndValidateConfig()` z něj vytváří runtime `ClientConfig`. |
| F06 | `client/src/core/config/config_document.h/.cpp`, `WizardFields`, `analyzeWizardDocument()`, `buildWizardDocument()` | Fields mají čtyři stringy. Edit/repair patchuje jen tyto členy. Generátor kontraktů se provádí pouze v create/reset a tam sestavuje nový objekt. |
| F07 | `client/src/core/wizard/wizard_model.cpp`, `load()`, `applyOperation()`, `contractsAllowed()` | Access Key režim se odvozuje z prázdného klíče, secret z doslovné shody. Návrh IP se přijímá pouze při prázdné IP. Kontrakty jsou povolené jen v create/reset. |
| F08 | `client/src/core/wizard/wizard_service.cpp`, `open()`, `startProbe()`, `probeTask()`, `finish()` | Služba drží původní DOM a důkazy kroků. Finish odmítá `createDefaultContracts=true` v edit/repair. Otisk commitu zahrnuje serializované fields a volbu kontraktů. |
| F09 | `client/src/core/ipc/wizard_ipc.cpp`, `fieldsValue()`, `parseFields()`, `viewValue()`, `parseView()` | Fields jsou uzavřený čtyřpoložkový objekt, view je také uzavřené. Pouhé přidání členů na jedné straně IPC selže. |
| F10 | `client/apps/tray/wizard_window.cpp`, `request()`, `render()`, `command()` | Chybí checkbox automatické IP. Volba kontraktů je podmíněna `contractsAllowed()`, v edit/repair se zobrazuje jen informace o zachování. |
| F11 | `client/src/core/wizard/wizard_defaults.cpp` | `defaultAccessKey()` opakuje hex posledního oktetu. `defaultPayloadKey()` řadí IP číselně. `suggestWizardIp()` používá stávající rozsah `10.99.99.2` až `.15`, filtrovaný CIDR a existujícími kontrakty. |
| F12 | `client/src/core/config/config_defaults.cpp`, `makeNewWizardDocument()` | Reset/create Wizardu má odlišný rozpracovaný koncept s URL `http://MODIFY-THIS-URL/phpvpn.php`, bez identity a původních kontraktů. Není totožný s úplnou šablonou editoru. |

## 2. Schéma, metadata a jediná úplná šablona

### 2.1 Ukládané členy a kompatibilní odvození

| Člen `masterSetup` | Typ | Nová úplná šablona a Reset Wizardu | Odvození pouze při absenci členu ve starém dokumentu |
|---|---|---|---|
| `useDefaultServerSecretInWizard` | volitelný JSON boolean | `true` | `serverSecret == "CHANGE_THIS_SERVER_SECRET"`, přesná shoda bajtů. Prázdný secret dává false. |
| `autoDetectIpAddressInWizard` | volitelný JSON boolean | `true` | false pro přítomnou platnou `virtualIp`, true pro repair bez IP. |
| `useDefaultAccessKeyInWizard` | volitelný JSON boolean | `true` | false pro přítomný platný `accessKey`, true pro repair bez klíče. Ani shoda s vypočteným defaultem neznamená true. |

Přítomná hodnota false má vždy přednost před odvozením. V repair s explicitním false a chybějící konkrétní hodnotou zůstane příslušné pole prázdné a editovatelné, uživatel je musí doplnit. `null`, číslo, string, objekt a pole jsou chyby, nikoli absence. Chybný typ nového členu není repairable. Duplicitní ani neznámé členy se nezačnou tolerovat.

**T01:** Do dokumentového `ConfigValues` přidat tři `std::optional<bool>` pod jednoznačně pojmenovaným dokumentovým blokem, například novým `WizardPreferences`. Přítomnost zachovat do analýzy. `analyzeWizardDocument()` vyhodnotí fallback právě jednou a předá tři konkrétní booleany v rozšířených `WizardFields`. Platný původní DOM se tím nemění. `WizardFields` tedy bude obsahovat čtyři dosavadní stringy a tři výše pojmenované booleany.

`validateConfigValues()` rozšíří známé členy a striktně zkontroluje typy. `parseAndValidateConfig()` metadata do běhových polí `ClientConfig` nepřenese. `rawJson` je může dál obsahovat jako původní dokument. Kód síťového workeru, odvozování autentizačních klíčů, tunelu a směrování nesmí číst žádný z těchto příznaků. Dosavadní protokolová normalizace prázdného secretu zůstává beze změny a není řízená metadaty.

Validátor běžné konfigurace nesmí odmítnout jinak platný soubor jen proto, že obsahuje true spolu s vlastní uloženou hodnotou. Metadata určují chování příštího Wizardu. Do té doby VPN používá uložené hodnoty. Konzistenci výsledných hodnot se zvolenými režimy kontroluje zvlášť službový tok Wizardu před jeho uložením.

### 2.2 Přesný úplný default

V `kDefaultConfigDocument` zachovat všechny dosavadní hodnoty, jejich strukturu, nepřítomnost `runtime` a jeden existující výchozí contract. Vložit pouze tři členy z následujícího cílového dokumentu:

```json
{
  "masterSetup": {
    "serverUrl": "http://127.0.0.1/phpvpn.php",
    "trustAnyTlsCertificate": true,
    "serverSecret": "CHANGE_THIS_SERVER_SECRET",
    "useDefaultServerSecretInWizard": true,
    "vpnCidr": "10.99.99.0/24",
    "virtualIp": "10.99.99.2",
    "autoDetectIpAddressInWizard": true,
    "accessKey": "0202020202020202020202020202020202020202020202020202020202020202",
    "useDefaultAccessKeyInWizard": true
  },
  "communicationContracts": {
    "10.99.99.3": {
      "payloadKey": "918c488528c2bc41a90f000b0a63bab0ba418790058ea7b448a3760aec16349d"
    }
  }
}
```

Tento příklad v plánu popisuje schéma. Produkční implementace má jedinou JSON konstantu, nadále poskytovanou přes `makeDefaultEditorDocument()`. Použijí ji `get-default-config`, Edit Config Reset, `create-default-config` a startup. Aktualizovat i zastaralý komentář u konstanty o zápisu pouze po explicitním potvrzení.

**T02:** `makeNewWizardDocument()` zachová účel samostatného rozpracovaného konceptu, placeholder URL, chybějící IP/klíč a prázdnou výchozí množinu kontraktů. Doplní tři true do fields i DOM. Společné výchozí preference definovat jednou a použít i zde. Zadání sjednocuje úplnou šablonu editoru a službového vytvoření a hodnoty příznaků obou Resetů, nepožaduje nahradit Reset Wizardu úplným dokumentem editoru. Nový/resetovaný Wizard nadále vytvoří soubor až po Finish.

## 3. Bezpečná inicializace při každém startu

### 3.1 Pořadí a rozdělení odpovědností

**T03:** Vyčlenit testovatelnou interní startup operaci nad `ConfigStore`, pracovně `ensureDefaultAtStartup()`. Zavolá ji `ServiceRuntime::start()` právě jednou po přípravě bezpečného backendu a vyhodnocení recovery. Službová i konzolová hostitelská cesta používají stejnou inicializaci. `VpnEngine::start()` ani `Connect` default samy nevytvářejí.

Pořadí pro běžný úspěšný start:

1. Autoritativně vyhodnotit adresář a `configFilePath(data)` stejně jako nyní. Nepřijímat cestu od tray ani z aktuálního pracovního adresáře procesu.
2. Připravit chráněný adresář a obnovit očekávanou politiku SYSTEM/Administrators. Zachovat ochrany proti reparse bodům a stávající ověření připnutých předků.
3. Vytvořit jediný `ConfigStore` a provést `recover()`. Výsledek `unknown` není úspěšná obnova, i když je vrácen jako hodnota úspěšného `Result`.
4. Po jednoznačné obnově provést jednu počáteční klasifikaci přes `ConfigStore::read()`. Automatické vytvoření připustit výhradně při dostupnosti `missing` s důvodem skutečné neexistence. Chyba čtení či obecné `!result` nejsou souhlasem k vytvoření.
5. Pro missing zavolat `ConfigStore::createDefault()`. Použít jeho validaci sdílené šablony, zámek a `ConfigPublishMode::createIfMissing`. Nepřidávat `ofstream`, truncation, kopírování od instalátoru nebo druhou publish cestu.
6. Vyhodnotit vytvořený nebo souběžně vzniklý skutečný snímek. Vnitřní kontrolní čtení po publikaci je ověření výsledku, nikoli další startovní cyklus či opakování vytvoření.
7. Dokončit dostupnost řídicího IPC a diagnostiky. Úspěšné vytvoření samo nevolá Connect a nespouští HTTP, Wintun ani přidávání tras.

### 3.2 Rozhodovací tabulka

| Stav | Akce při startupu | Výsledek |
|---|---|---|
| Bezpečná obnova, soubor skutečně chybí | Jediný pokus `createDefault()` | Celý validní default nebo konkrétní chyba, nikdy částečný cíl. |
| Soubor existuje a je validní | Žádná publikace | Přesné původní bajty včetně formátování a BOM zůstanou zachované. |
| Soubor existuje a je invalid/repairable/encoding_error/too_large/unreadable | Žádná publikace | Diagnostika skutečného stavu, žádná automatická oprava obsahu. |
| Soubor vznikne před atomickým rename | Backend vrátí alreadyExists nebo konkrétní chybu bezpečného přístupu | Cizí obsah zůstane zachovaný, platnost se neposuzuje jako oprávnění přepsat jej. |
| Recovery je unknown, poškozená nebo selže | Automatické vytvoření se nepovolí | Zachovat původní důvod a pomocné soubory, zpřístupnit diagnostiku. |
| Publikace selže nebo její výsledek není potvrzený | Bez automatického retry | Nehlásit úspěch ani jistotu, že nic nevzniklo. Ověření aktuálního stavu pouze čtením. |

`ConfigStore::createDefault()` dnes převádí neznámý výsledek publikace na `config_write_failed` s informací o nepotvrzeném výsledku. Startup tuto chybu nesmí změnit na falešné „soubor nebyl vytvořen“ a nesmí ji řešit druhým pokusem. Explicitní IPC operace si zachová dnešní návrat skutečného dokumentu, včetně cizího souboru vzniklého během dialogu.

### 3.3 Dostupnost IPC při chybě

**T04:** Nový pokus o vytvoření nesmí být obalený makrem nebo větví, která při chybě ukončí `ServiceRuntime::start()` před spuštěním pipe. Přidat strukturovaný výsledek startup inicializace, pracovně `StartupConfigOutcome`, s rozlišením existujícího souboru, vytvoření, souběžného vzniku a degradace. Logovat bezpečný kód a fázi, nikoli dokument, secret nebo žurnál.

Současnou větev okamžitého návratu při chybě `configStore_->recover()` změnit na explicitní degradovaný stav úložiště. `ConfigStore` musí mít oddělenou chybu bezpečné inicializace od dostupnosti obsahu. Nevyřešená recovery blokuje publikaci i `readValid()` pro zahájení VPN. Obyčejné bezpečné čtení pro editor může dál vrátit skutečný text, pokud jej backend dokáže bezpečně přečíst. Nesmí vzniknout náhradní validní konfigurace v paměti. Výsledek `unknown` se nesmí zahodit pouhým pokračováním s read.

Konkrétně zavést samostatnou bránu připravenosti úložiště, pracovně `ConfigStore::operationalStatus()`. `VpnEngine::connect()` dnes používá přímo `read()` a `snapshot->config`, nikoli `readValid()`, proto musí bránu ověřit také před tímto přímým čtením. Totéž platí pro další zahájení provozu a diagnostiky závislé na validní runtime konfiguraci. Neměnit validní editorový dokument na falešné invalid jen kvůli samostatné chybě recovery. Dostupnost obsahu a chyba připravenosti jsou dvě odlišné informace. Konkrétní chybu dostupnou přes Connect a odpovídající paměťový diagnostický stav engine neztratit pod obecným `service_not_running`.

Chyba počáteční bezpečné přípravy nebo nevyřešené recovery drží tuto bránu zavřenou do další úspěšné řízené inicializace při restartu. Obyčejná potvrzená chyba vytvoření, například nedostatek místa před publikací, sama nezavádí trvalý zákaz pozdějšího explicitního Edit Config/Save. Po bezpečném odstranění příčiny může tato akce použít běžnou publikaci a recovery. Žádná běžná operace však nesmí sama provést administrátorskou obnovu ACL. Nejasný výsledek publikace zachová svou nejistotu a povinné souborové recovery.

Pokud chyba přípravy úložiště zabrání vytvoření backendu, zavést explicitní konstrukci nedostupného `ConfigStore` s pevnou již vyhodnocenou cestou a bezpečným důvodem. Nové rozhraní musí zajistit, že `path()` nepotřebuje platný souborový backend a každá operace nad nedostupným backendem vrací konkrétní chybu. Nevytvářet falešný backend, který hlásí missing nebo úspěšný zápis. Obnovu ACL nebo nové inicializování backendu neprovádět skrytě při běžném IPC požadavku. Další administrátorský restart je nová řízená inicializace.

Při změně pořadí hostu zachovat samostatnou obnovu vlastnictví tunelu. Pokud ji nelze bezpečně dokončit, musí být Connect blokovaný příslušnou chybou a IPC musí pravdivě prezentovat degradaci. Kontrolní objekt engine a dispatcher mohou být dostupné i bez spuštěné síťové části. V `VpnEngine::start()`, `connect()`, `shutdown()` a obsluze hostu explicitně ošetřit stav „síťová inicializace nedokončena“, aby nečekaly na neexistující worker a netvrdily připravenost k VPN. Toto je technická návaznost dostupnosti IPC, nikoli povolení obejít ownership recovery. Nezávislé selhání samotné pipe, kryptografického provideru nebo nedostupná autoritativní systémová cesta se nadále pravdivě hlásí jako selhání potřebné infrastruktury.

### 3.4 Zachování recovery a řešení původní chyby

`recoverLocked()` nesmí použít zálohu jako náhradní default, dokončit nepublikovaný commit ani přepsat existující cíl. Zachovat rozlišení clean/confirmed/notPublished/unknown, identitu stagingu, potvrzený historický commit a bezpečný úklid pouze známých pomocných souborů. Žurnál ani `config.previous` preventivně nemazat. Záloha může být hardlink, její bezpečné odstranění nesmí změnit bajty cíle.

Ponechat `checkDirectorySecurity()`, `config_directory_security_invalid` a rozlišení od chyby načtení descriptoru. Kliknutí Edit Config → Yes neopravuje ACL a nerestartuje službu. Po ručním administrátorském restartu se nyní při skutečné neexistenci default vytvoří automaticky, proto aktualizovat původní nápovědu. Restart přeruší VPN. Soubor smazaný za běhu lze nadále vytvořit přes `create-default-config`, pokud úložiště splňuje bezpečnostní pravidla.

## 4. Datový tok a uzavřené IPC schéma

### 4.1 Tok hodnot

```text
config.json → validateConfigValues → dokumentová metadata s přítomností
            → analyzeWizardDocument → sedm konkrétních WizardFields
            → WizardView → WizardIpc → WizardModel → uživatelské změny
            → wizard-check-access → službový důkaz přesných sedmi fields
            → wizard-finish → patch původního DOM + aditivní kontrakty
            → plná validace → ConfigStore::save → atomická publikace
```

Běžný runtime odbočuje již v `parseAndValidateConfig()` do konkrétních hodnot `ClientConfig`. Režimy Wizardu nerozhodují o jeho efektivní identitě.

### 4.2 Přesné změny schématu

**T05:** Zachovat jména příkazů a IPC v1, ale aktualizovat uzavřené schéma spolu v tray a službě. `fields` bude mít přesně následujících sedm povinných členů. Volitelnost v uloženém JSON neznamená volitelnost v tomto již vyhodnoceném přenosu.

| Člen fields | IPC typ |
|---|---|
| `serverUrl` | string |
| `serverSecret` | string |
| `virtualIp` | string |
| `accessKey` | string |
| `useDefaultServerSecretInWizard` | boolean |
| `autoDetectIpAddressInWizard` | boolean |
| `useDefaultAccessKeyInWizard` | boolean |

Změnit `fieldsValue()` i `parseFields()`. Odstranit dnešní obecnou podmínku, že každý člen fields musí být string, a nahradit ji přesnou validací čtyř stringů a tří booleanů. Neotevírat schéma libovolným dalším členům. Rozšířená fields se použijí ve view, `wizard-check-access` a `wizard-finish` a ve všech uložených důkazech a pracovních kopiích.

`wizard-check-url` a `wizard-check-secret` nadále přijímají dosavadní konkrétní vstupy na nejvyšší úrovni. Samotný síťový test secretu nezávisí na zdroji jeho hodnoty. Celou finální sedmici služba přijme a ověří při `wizard-check-access`. Nastavení příznaku bez změny konkrétní hodnoty nesmí umožnit obejít povinný access důkaz pro aktuální finální fields.

### 4.3 Data potřebná pro přesný náhled kontraktů

**T06:** Do `WizardView` a uzavřených odpovědí `wizard-open`, `wizard-reset`, `wizard-resume` přidat povinné `existingDefaultContractPeers`, pole nejvýše 14 kanonických IPv4 stringů. Obsahuje průnik původních peerů s kandidáty `.2` až `.15` podporovaného CIDR. `contractCount` nadále znamená počet všech původních kontraktů, včetně těch mimo výchozí sadu. Žádné payload klíče se nepřenášejí v tomto novém členu.

`viewValue()` a `parseView()` rozšířit současně. Vyžadovat unikátní, číselně seřazené adresy příslušné kandidátní sady, počet nejvýše 14 a nejvýše `contractCount`. Pro nepodporovaný CIDR je pole prázdné. Při fields/context null musí být pole prázdné a contractCount nula, stejně u nového create/reset konceptu bez kontraktů. View s nevalidním kontextem nadále neumožní pokračování bez Resetu. Nevyhodnocovat shodu vlastní IP a tohoto pole už při syntaktickém parsování rozpracovaných fields, aby služba mohla vrátit konkrétní chybu identity v příslušném kroku.

Služba odvozuje tento seznam ze svého `Session::original`, tray ho pouze přijímá. Finish nepřijímá od tray tento seznam ani klientem tvrzené počty. Oba výpočty náhledu a skutečného doplnění použijí sdílený čistý výběr kandidátů popsaný v kapitole 6. Přenos celé sady kontraktů není kvůli náhledu potřeba.

### 4.4 Důkazy, validace a idempotence

**T07:** `WizardFields::operator==` musí zahrnout všechny tři booleany. `accessEvidence` váže celou sedmici a Finish ji porovnává přesně. `fieldsValue()` musí serializovat příznaky i do otisku vstupu commitu. Stejný `commitId` s jiným příznakem nebo jinou volbou kontraktů se odmítne. Opakování přesně stejného vstupu používá existující receipt a neopakuje zápis.

Před síťovým `check-access` služba ověří:

- Konkrétní URL a secret odpovídají úspěšným předchozím krokům.
- True pro výchozí secret vyžaduje přesný výchozí string.
- True pro výchozí klíč vyžaduje shodu se sdíleným `defaultAccessKey()` pro právě validovanou IP.
- True pro automatickou IP vyžaduje konkrétní návrh z posledního platného úspěšného kroku secret dané relace a jeho shodu s předanou IP.
- Platí dosavadní validace IP, klíče a zákaz vlastní IP v již existujícím kontraktu.

Pro poslední bod automatického režimu přidat do `Session` uložený ověřený návrh IP, pracovně `suggestedIpEvidence`. Nesmí se odvozovat z klientem zaslaných fields. Uložit jej v `probeTask()` současně s platným secret důkazem a stejnou generací/epochou. Zneplatnit při zneplatnění URL nebo secret důkazu, Resetu, přerušení Connectem a expiraci. Nový check-access s vlastním čerstvým info nemá zpětně přepočítat uživateli IP, nadále ověřuje identitu zvolenou po předchozím úspěšném kroku secret.

Příznaky jsou metadata Wizardu, ale tento službový krok musí vynutit jejich vlastní prezentační význam proti vadnému či zastaralému tray. Tato kontrola nepatří do běžného runtime parseru. Finish navíc před sestavením dokumentu znovu ověří potřebné invarianty a plnou validitu výsledku.

`createDefaultContracts` zůstane povinným booleovským členem requestu `wizard-finish` mimo fields. Je to jednorázový pokyn, nikoli člen `masterSetup`, DOM, runtime ani uložených preferencí. Ze `WizardService::finish()` odstranit zákaz pro edit/repair. Zachovat odmítnutí true pro nepodporovaný CIDR. Přepnutí této jednorázové volby samo nevyžaduje další síťový access test.

Zachovat kontrolu session, serviceInstanceId, generace, connectionEpoch, attemptSequence, lhůt, bezpečné rušení a oddělené file/diagnostic executory. Změny nesmějí přidat automatické síťové retry, druhý data request v jediném pokusu ani síťové operace při startup vytvoření.

### 4.5 Kompatibilita verzí

Staré konfigurační dokumenty bez příznaků přijímá nová služba bez migrace. Starší binární služba nové členy kvůli uzavřenému validátoru odmítne, proto zpětná kompatibilita dokumentů neznamená podporu nového dokumentu starými binárními soubory.

Rozšířené uzavřené fields/view nejsou kompatibilní se starým tray protějškem. Distribuovat a aktualizovat službu i tray společně. Nesoulad musí skončit konkrétní informací o nekompatibilním schématu a nutnosti sjednotit instalaci, bez vynechání příznaků, přímého zápisu na disk nebo opakování požadavku ve starším tvaru. Parser nesmí libovolný poškozený obsah automaticky označit za starou verzi. Rozpoznaný přesný historický čtyřpoložkový tvar lze diagnostikovat zvlášť, ostatní chyby zůstávají `ipc_malformed`. Sedm původních příkazů IPC a jejich obálky se touto změnou nerozšiřují.

## 5. WizardModel a kroky UI

### 5.1 Inicializace a zachování rozpracovaného stavu

**T08:** V `WizardModel` používat příznaky z fields jako jediný zdroj uloženého významu. Odstranit nebo odvodit dnešní duplicitní `defaultSecret_` a `defaultKey_`, aby se nemohly rozejít s odesílanými hodnotami. Přidat accessor a setter automatické IP, pracovně `autoDetectIpAddress()` a `setAutoDetectIpAddress(bool)`.

Při `load(view, false)` načíst vyhodnocené preference služby. Při true výchozím secretu okamžitě nastavit rozpracovaný secret na default. Výchozí klíč odvozovat z platné aktuální IP, při neplatné nebo chybějící IP jej vyprázdnit. Automatická IP má do doručení ověřeného návrhu stav čekání, uložený placeholder není úspěšný návrh. Otevření ani tato úprava konceptu nic nezapisují.

`load(view, true)` při resume zachová všechny rozpracované konkrétní hodnoty, tři preference i jednorázovou volbu kontraktů. Síťové důkazy a ověřený návrh jsou po přerušení zneplatněné. Back/Next zachovává aktuální nastavení. Reset nastaví nové tři true a volbu kontraktů znovu do výchozího stavu. Nové otevření Wizardu vždy inicializuje volbu kontraktů jako zaškrtnutou bez ohledu na počet stávajících kontraktů.

### 5.2 Krok 2

`Use default SERVER_SECRET` při true zobrazí `CHANGE_THIS_SERVER_SECRET` v zakázaném poli. Při false zobrazí uložený nebo právě rozpracovaný secret v editovatelném poli. Zachovat přesné UTF-8 bajty, prázdné hodnoty a stávající literal režim řídicích znaků. Žádný trim ani normalizace textu.

Zaškrtnutí nahradí koncept výchozím secretem. Odškrtnutí ponechá právě zobrazenou hodnotu editovatelnou. Každé přepnutí uloží příznak v fields a zneplatní secret a access kontrolu v souladu s dosavadním modelem. Finish uloží konkrétní ověřený secret i příznak.

### 5.3 Krok 3

Před IP pole přidat checkbox `Automatically detect IP address`. Při true je IP pole zakázané, checkbox zůstává použitelný pro vypnutí automatiky, pokud model není busy/committing. Po úspěšném aktuálním secret výsledku `applyOperation()` přijme `suggestedIp` při zapnutém automatickém režimu i pro neprázdnou uloženou IP. Při false ji ignoruje a ponechá vlastní hodnotu.

Zachovat algoritmus `suggestWizardIp()`: kandidáti `10.99.99.2` až `.15`, omezení zachovaným CIDR a existujícími kontrakty, nejnižší chybějící adresa, jinak nejstarší lastSeen s číselným tie-break. Podpora generování kontraktů pro jiné `10.A.B.0/24` tento rozsah automatické IP nerozšiřuje. Nejde o rezervaci adresy ani o změnu serverové konfigurace.

**T09:** Model bude mít oddělený dočasný stav platného návrhu pro aktuální secret kontrolu. Úspěšný secret s `suggestedIp=null` může přejít do identity, kde zobrazí konkrétní chybu návrhu a zablokuje Next při zapnuté automatice. Neoznačovat tím zpětně platný secret za špatný. Nechat uživatele vypnout automatiku a zadat IP. Po vypnutí lze ponechat poslední zobrazenou IP k editaci, ale stále musí projít validací a access testem. Opětovné zapnutí použije pouze dosud platný návrh aktuálního secret kroku, jinak znovu vyžaduje jeho získání. Žádný automatický HTTP požadavek na pouhé zaškrtnutí.

Oddělit ruční `setIp()` od interního nastavení ověřeného návrhu. Ruční setter nesmí přepsat IP při zapnuté automatice. Každá skutečná změna IP zneplatní access kontrolu a při true pro výchozí klíč okamžitě přepočítá klíč. Při nevalidní IP neponechat starý klíč jako platný default. Platnost zahrnuje CIDR, nikoli jen parsovatelný IPv4 string.

`Use default access key` při true zobrazuje zakázané pole s `hex2(lastOctet)` opakovaným 32krát. Při false pole zůstane editovatelné. Zapnutí přepíše dosavadní klíč, vypnutí ponechá aktuální text a neobnovuje dřívější vlastní klíč. Změna příznaku zneplatní access důkaz i při textově stejném výsledném klíči. Finish ukládá oba příznaky a obě konkrétní hodnoty identity.

V `wizard_window.cpp` doplnit jedinečné control ID, obsluhu `command()`, stav enabled/checked v `render()` a synchronizaci textu bez reentrantních změn při interním renderu. `WizardLayout` a testy musí pokrýt vyšší krok, scroll, fokus, tabulátor a chování Back/Next. Zachovat upozornění na jednorázové spotřebování zpráv při access kontrole.

## 6. Kontrakty: jednotný náhled a aditivní Finish

### 6.1 Kandidáti a počty

**T10:** V `wizard_defaults.h/.cpp` zavést sdílené čisté funkce pro kandidátní sadu a výběr chybějících peerů, pracovně `defaultContractCandidates(cidr)` a `missingDefaultContractPeers(cidr, own, existingPeers)`. Kandidáti jsou číselně seřazené platné adresy `network | 2` až `network | 15` pro `supportsDefaultContracts(cidr)`. Vynechat vlastní IP a všechny existující klíče. Funkce pro náhled nepočítá SHA-256 a nic nemění.

Tray použije z view celkový `contractCount` a omezenou množinu `existingDefaultContractPeers`. Služba použije plné `Session::original->contractPeers`. Pro kandidátní rozdíl dávají oba vstupy stejný výsledek. Formálně:

```text
C = kandidáti podporovaného CIDR bez vlastní IP
E = všechny původní kontrakty
M = C bez peerů obsažených v E
existující počet = |E|
chybějící výchozí počet = |M|
plánované doplnění = checkbox && podporovaný CIDR ? |M| : 0
výsledný počet = |E| + plánované doplnění
```

Poslední krok zobrazí vždy alespoň první dva počty. Při odškrtnutí zůstává počet chybějících viditelný, ale text výslovně ukazuje plánované doplnění nula. Při neplatné identitě neprezentovat odhad jako potvrzený náhled pro Finish.

Na defaultní konfiguraci s vlastní `.2` a jediným kontraktem `.3` vychází existující 1, chybějící 12, výsledné 13. Mimo kandidátní rozsah, ale v podporovaném CIDR, může být kandidátů 14. Smlouvy mimo kandidátní sadu se počítají do existujících a zachovají se.

### 6.2 Trvale viditelný checkbox

V `WizardModel::contractsAllowed()` odstranit test režimu create/reset, ponechat pouze použitelnost kontextu a podporovaný CIDR. V `wizard_window.cpp` vykreslovat checkbox ve všech čtyřech režimech i při úplné sadě. Doporučený text je `Add missing default communication contracts`, protože již existující položky se nenahrazují.

**T11:** Pro nepodporovaný platný CIDR zachovat podle zadání výchozí zaškrtnutý vizuální stav, ale prvek zakázat a jasně uvést, že se nic nedoplní. Oddělit `createContracts()` jako uživatelskou volbu od efektivního pokynu pro request, například novým `shouldAddDefaultContracts() = createContracts() && contractsAllowed()`. Finish odesílá efektivní false. Služba přesto odmítá ručně podstrčené true pro nepodporovaný CIDR. Nezavádět skrytý přechod na podporovanou síť.

### 6.3 Aditivní sestavení dokumentu

`buildWizardDocument()` vždy začíná kopií DOM aktuálního konceptu. V edit/repair jde o DOM zachycený při otevření, v create/reset o nový koncept, který již původní kontrakty nemá.

1. Ověřit identitu a zákaz vlastní IP mezi existujícími peery před vytvořením jakéhokoli nového kontraktu.
2. Patchovat čtyři konkrétní členy `masterSetup` a tři příznaky typu bool. Ostatní členy včetně explicitních TLS/runtime hodnot a nepřítomných defaultů zachovat.
3. Je-li jednorázová volba false, ponechat existující `communicationContracts` obsahově i přítomností beze změny. U nového create/reset lze zachovat dosavadní výstup prázdného objektu, protože tam žádný původní objekt není.
4. Při true ověřit podporovaný CIDR, získat M a doplnit pouze jeho položky do stávajícího objektu. Pokud objekt v edit/repair chyběl, založit jej až při skutečném doplnění. Nepřestavovat stávající objekt z `ClientConfig`, nepoužívat při vkládání přepis existujícího klíče.
5. Pro každý nový peer použít dosavadní `defaultPayloadKey(crypto, own, peer)`: SHA-256 přes ASCII číselně seřazených kanonických IP oddělených jediným podtržítkem, bez LF, výstup lowercase hex.
6. Serializovat UTF-8 bez BOM, ověřit 512KiB limit, plnou konfiguraci a velikost skutečné IPC obálky. Teprve potom publikovat přes `ConfigStore::save()`.

Při prázdné množině M nepočítat nové klíče a neměnit obsah kontraktů. Finish může uložit změněné masterSetup, nesmí však odmítnout zaškrtnutou volbu jen kvůli nulovému doplnění. Chyba generování kteréhokoli nového klíče nebo finální validace zastaví celý commit před publikací.

Změna vlastní IP nepřegeneruje žádné existující payload klíče, ani když byly původně odvozené pro jinou vlastní IP. To je přímý důsledek P08. Zákaz vlastního kontraktu zůstává platný, konflikt se řeší jinou vlastní IP nebo explicitní opravou kontraktu přes editor. Automatické doplnění nesmí konflikt „opravit“ smazáním kontraktu.

### 6.4 Souběh a význam zachování

Zachování kontraktů platí vůči výchozímu DOM dané Wizard relace. Nevzniká automatické slučování s mezitím cizím uloženým dokumentem. Dosavadní pravidlo poslední úspěšné publikace zůstává platné mezi Save/Finish, dvěma editory a dvěma Wizardy. Náhled proto nepotřebuje watcher ani opakované čtení disku. Pokud mezitím jiný editor změní config, Finish stále ukládá celý vlastní validovaný koncept.

Opakovaný Finish stejného commitu zachovává receipt idempotenci. Nové otevření nad již úplnou sadou opět ukáže zaškrtnutý checkbox a nulu chybějících. Obsah kontraktů se nezmění, i když formátování JSON po samostatném novém Finish může být jiné.

## 7. Implementační bloky a závislosti

| Blok | Konkrétní práce a soubory | Závislosti a výstupní brána |
|---|---|---|
| B00 | Aktualizovat dotčená normativní pravidla `client.md`, `plan3_client.md` a návaznosti `plan3_fix.md` podle kapitoly 8. | Schválené formální zadání. Žádné souběžně závazné opačné pravidlo pro startup, příznaky, edit/repair nebo fields. |
| B01 | `config.h`, `config_document.h`, `config_validator.cpp`, `config_defaults.cpp`: volitelné dokumentové preference, rozšířená fields, jediná úplná šablona, nové defaults konceptu. | B00. Validátor přijme staré i nové dokumenty a zachová konkrétní runtime hodnoty. |
| B02 | `config_store.h/.cpp`, `service_host.cpp`, podle potřeby `paths_win.h/.cpp` a lifecycle `vpn_engine.h/.cpp`: startup operace, vyhodnocení recovery a dostupnost IPC při degradaci. | B01. Pouze missing může vést k vytvoření, chyba neodstaví konfigurační IPC. Zachované bezpečné ownership recovery a stop. |
| B03 | `wizard_defaults.h/.cpp`, `config_document.cpp`: společný výběr chybějících peerů a aditivní patch sedmi členů a kontraktů. | B01. Výsledný DOM zachová nepředstavené hodnoty a každý existující klíč. |
| B04 | `wizard_types.h`, `wizard_ipc.h/.cpp`: sedm fields, omezené preview peers, uzavřená obousměrná schémata a diagnostika verzí. | B01, B03. Roundtrip všech nových členů, odmítnutí špatného typu a nadbytečných polí. |
| B05 | `wizard_service.h/.cpp`: inicializace view, návrhový důkaz IP, sedmipoložkové access evidence, Finish a fingerprint, odstranění mode zákazu kontraktů. | B03, B04. Upravený request nemůže uložit neověřené hodnoty, opakovaný commit nepíše podruhé. |
| B06 | `wizard_model.h/.cpp`: preference, čekání/návrh automatické IP, invalidace, zachování přes Back/resume, náhled a oddělení efektivní volby kontraktů. | B03, B04, kontrakt B05. Přechody neobnovují automaticky síťové pokusy. |
| B07 | `wizard_window.h/.cpp`, `wizard_layout.h/.cpp`, podle potřeby `wizard_reader.cpp` a tray `main.cpp`: nové UI, kontrolní ID, dispatch fields, vždy viditelné kontrakty a nové nápovědy. | B05, B06. Všechny čtyři režimy použitelné na malém okně a při DPI. |
| B08 | Instalační a balíčkovací texty, cílené testy a nový report/matice. | B02 až B07. Doložená kritéria níže, skutečně provedené NTFS a SCM scénáře oddělené od NOT RUN. |

B02 a B03 mají společný základ, jejich integrace nemusí zavádět další runtime worker. Změny schématu a všech jeho konzumentů musejí tvořit sestavitelný celek. Případné nové testovatelné startup komponenty registrovat v aktuálních CMake souborech, nevytvářet druhý produkční způsob startu pouze pro test.

## 8. Dokumentace a odstranění rozporů

| Dokument nebo oblast | Požadovaná úprava při budoucí implementaci |
|---|---|
| `client.md`, Konfigurační soubor | Nahradit zákaz automatického vytvoření při startu pravidlem P01. Connect soubor nadále nevytváří, smazání za běhu nezavádí watcher ani změnu aktivního snímku. |
| `client.md`, schéma, Opravitelný dokument, Zachování hodnot a Reset | Popsat tři volitelné booleany, přesné fallbacky a oddělení runtime. Čtyřpoložkový patch nahradit sedmipoložkovým s podmíněným aditivním doplněním. |
| `client.md`, SERVER_SECRET, IP a Access Key, Kontrakty | Přítomné příznaky mají přednost před heuristikou. Přidat automatickou IP, odstranit tvrzení, že původ volby není uložený, a zákaz doplnění při edit/repair. Popsat náhled a nepersistovaný checkbox. |
| `client.md`, Rozšíření IPC v1, UI a Windows distribuce | Aktualizovat uzavřená schémata fields/view, důkazy, fingerprint, kompatibilitu a nový start služby. |
| `plan3_client.md` | Aktualizovat zejména D04, D07, D11/D12, D27 a části 5.3, 5.6, 6, 7.2 až 7.4, 8, 9 a testovací matici. A01, A07, A11, A13, A18, A19, A27, A37, A39 upravit na nové chování bez zrušení zbývajících záruk. Počet 13/14 je velikost kandidátní sady, nikoli vždy počet nově vložených kontraktů. |
| `plan3_fix.md` | Označit jako překonané tvrzení, že restart default nevytvoří, a kritérium 5 v části zákazu startup vytvoření. Zachovat ACL ochranu, konkrétní chyby a žádné automatické opravy oprávnění přes tray. |
| `client/src/core/ipc/wizard_ipc.cpp`, `safeError()` a tray nápověda | Po administrátorském restartu už při chybějícím cíli může default existovat. Doporučit kontrolu Edit Config, nikoli vždy další povinné Yes. Upozornění na odpojení VPN zachovat. |
| `client/tools/package/install.ps1`, `client/tools/dev-install.ps1` | Nahradit závěrečné texty o nevytváření při startu. Vysvětlit, že config vytváří první úspěšný start služby a existující obsah se zachová. Nevkládat JSON konstantu do instalátoru. |
| Generátor distribuce a doprovodné texty | Dohledat aktuální texty pomocí `rg` v `client/tools` a zdrojích dokumentace, aktualizovat jejich zdroj. Generované kopie mají později vzniknout standardním balíčkováním. |
| `protocol.md` | Wire formát a kryptografická pravidla se nemění. Metadata jsou lokální stav Wizardu, do relay protokolu je nepřidávat. Zachovat dosavadní algoritmy, info clients a diagnostickou výjimku. |
| Historické reporty | Nepřepisovat minulý úspěch nebo NOT RUN na nový výsledek. Přidat nový report implementace fix 2 a odkazy v aktuální matici. |

## 9. Cílená ověření při implementaci

Tato kapitola předepisuje budoucí ověření. V rámci přípravy plánu nebyla spuštěna.

### 9.1 Unit a službové testy

| Sada | Konkrétní scénáře |
|---|---|
| `test_config.cpp`, `test_wizard_document.cpp` | Absence každého příznaku samostatně i všech, všech osm bool kombinací, všechny neboolean typy a duplicity. Přesná fallback pravidla v edit a repair bez IP, bez klíče a bez obou. Explicitní false s chybějící konkrétní hodnotou neaktivuje default. |
| `test_default_config.cpp`, `test_config_store.cpp` | Rovnost úplné šablony editoru, explicitního vytvoření a startup souboru včetně tří true a jediného původního kontraktu. Test samotného `VpnEngine::start()` dnes není testem `ServiceRuntime::start()`, proto zachovat engine/Connect bez tvorby a doplnit test skutečné nové hostitelské startup operace. |
| `test_config_store.cpp`, `fake_config_files.h` | Počítat čtení a publikace. Existující validní, invalidní, repairable, nečitelný, chybně kódovaný a nadlimitní soubor vyvolá nula startup publikací. Žádné nové členy ve starém souboru. Závod s vytvořením cizího cíle, chyba před publikací i unknown bez retry. |
| `test_config_files_win.cpp` | Skutečné NTFS a oprávněný token: atomický missing create, restart s byte-identickým cílem, závod před rename, nesprávné ACL, reparse/hardlink ochrany, chyby write/flush/rename, potvrzený i nepotvrzený žurnál, missing po předchozím Save. Nikdy preventivně nemazat pomocné soubory v testu místo produkčního recovery. |
| `test_wizard_ipc.cpp` | Builder/parser všech změněných requestů a view, přesně sedm fields, booleany false beze ztráty, chybějící/nadbytečné/duplicitní členy, nevalidní preview peers a limity. Historická schémata pravdivě odmítnutá, sedm původních IPC příkazů funkční. |
| `test_wizard_model.cpp` | Otevření s true i vlastními uloženými hodnotami, explicitní false i hodnoty shodné s defaultem, přepnutí secret/key, ruční vs systémová IP, nevalidní IP vyprázdní default key. Návrh nahradí neprázdný placeholder pouze při auto=true. Chybějící a opožděný návrh, vypnutí/zapnutí, Back/Next, resume a Reset. |
| `test_wizard_document.cpp` | Edit/repair s nulovou, částečnou a úplnou sadou, custom klíč pro kandidáta, peer mimo `.2` až `.15`, chybějící objekt contracts, odškrtnutí, nepodporovaný CIDR a vlastní IP uvnitř i mimo kandidáty. Zachovat všechny původní hodnoty a omission neprezentovaných členů. |
| `test_wizard_document.cpp`, `wizard_defaults` | Sdílený náhled odpovídá skutečně přidaným peerům. Zachovat nezávislé SHA-256 vektory, číselné pořadí `.2` a `.10`, `.2` až `.15` bez self. Druhé doplnění má nula změn v obsahu kontraktů. |
| `test_runtime_status.cpp`, `WizardServiceFixture` | Celý open/check/finish v edit/repair s true kontrakty. Podstrčená metadata, nesoulad defaultu a hodnoty, auto IP mimo službový návrh, změněná sedmice po check-access. Stejný commitId a jiné příznaky se odmítnou, shodný receipt nepíše znovu. |
| Runtime parser a síťové fixture | Dva JSON dokumenty s totožnými konkrétními hodnotami a různými validními příznaky dávají totožnou runtime identitu, klíče a konfiguraci směrování. Samotný Connect příznaky neupravuje ani podle nich nedoplňuje config. |
| `test_wizard_layout.cpp` a UI | Nové pole nezpůsobí překryv tlačítek, checkbox kontraktů je vždy vidět. Ručně ověřit focus, tabulátor, resize a DPI 100/125/150/200 %. |

Dosavadní test `PatchPreservesUnshownValuesAndOmission` volá build v repair s true a očekává nezměněné peers. Rozdělit jej na zachování při false a aditivní doplnění při true. Neopravovat jej pouze změnou počtu peerů, porovnat přesné původní payload klíče a neprezentované členy.

### 9.2 Integrace služby a UI

Použít existující infrastrukturu `client/tests/e2e/service_main.cpp`, `service_e2e.cpp` a `client/tools/run-service-e2e.ps1` s vlastní testovací službou a izolovaným adresářem. Přidat alespoň tyto scénáře:

1. Čistý start bez configu a bez tray vytvoří přesný default. IPC odpovídá, VPN není automaticky připojená. Druhý start nic v configu nezmění.
2. Před startem vložený starý nebo vadný soubor zůstane bajtově stejný. Zvlášť zachytit nečitelný soubor a selhání recovery. Diagnostika funguje a nedovolí nebezpečný Connect.
3. Smazání pouze configu za běhu, Edit Config → Yes, skutečný default, Cancel soubor ponechá. No nic nevytvoří. Smazání následované restartem vytvoří default ještě před otevřením tray.
4. Změněná ACL za běhu vrátí konkrétní chybu bez zápisu. Administrátorský restart obnoví politiku a při chybějícím configu se pokusí o startup vytvoření. Žádná automatická změna ACL na Yes.
5. První Wizard po startup defaultu je režim edit s jedním kontraktem a třemi true. Automatická IP reaguje na ověřené clients i přes uloženou `.2`. Finish zachová původní kontrakt a doplní pouze zbývající kandidáty.
6. Opakované otevření načte tři uložené volby, checkbox kontraktů je znovu zaškrtnutý a při úplné sadě ukazuje nulu chybějících. Odškrtnutí nic neodstraní.
7. Ztracené potvrzení Finish se ověří receipt cestou. Cizí Save během Wizardu nadále respektuje poslední publikaci, nevznikne revizní konflikt ani dodatečné slučování.
8. Ověřit instalaci s prázdným adresářem, opakovanou instalaci s existujícím configem a smazání mezi dvěma starty. Reinstalace není důkaz čistého startu, pokud zůstalo ProgramData.

Pro samotné startup vytvoření není potřebný relay. Skutečné Wizard kontroly použijí izolovaný relay podle současných fixture. ACL test bez potřebného tokenu se vykáže NOT RUN, nesmí se vydávat za ověřený bezpečný startup. Nepoužívat produkční ProgramData jako testovací fixture.

Po implementaci sestavit dotčené produkční komponenty a testy v existujících debug/release presetech. Spustit cílené výše uvedené sady a přiměřenou regresi IPC, lifecycle a konfigurace. Nový report uvede příkazy, počty skutečně provedených testů, OS, token potřebný pro NTFS, UI prostředí a konkrétní neprovedené scénáře. Široká síťová zátěž není náhradou za cílené ověření startup a DOM invariantu.

## 10. Dohledatelnost akceptace

Značky AK01 až AK17 přesně odpovídají číslovaným akceptačním kritériím formálního zadání.

| Kritérium zadání | Implementační vazba | Rozhodující důkaz |
|---|---|---|
| AK01 | P01, P11, B02 | Čistý skutečný start služby bez tray vytvoří validní config. |
| AK02 | P03, B01 | Celý výstup odpovídá jediné šabloně s třemi true a původním kontraktem. |
| AK03 | P02, B02 | Druhý a další start, přesné porovnání bajtů včetně původního formátu. |
| AK04 | P02, B02 | Invalid/unreadable a ostatní chyby obsahu vyvolají nula publikací. |
| AK05 | P02, B02 | Cizí soubor vzniklý mezi klasifikací a rename není přepsán. |
| AK06 | P04, B01, B06 | Staré dokumenty jsou validní, samostatné fallbacky přesně odpovídají tabulce 2.1. |
| AK07 | P04, B01 | Každý neboolean typ pro každý nový člen odmítnut. |
| AK08 | P05, B05 až B07 | Secret a jeho volba projdou open, změnou, důkazem, Finish a novým open. |
| AK09 | P06, B05 až B07 | IP/klíč a obě volby projdou stejným úplným tokem. |
| AK10 | P06, B06 | Ověřený návrh nahradí placeholder při true, při false uloženou IP nezmění. |
| AK11 | P09, B01, B03 | Uložený DOM a uzavřené konfigurační schéma neobsahují žádný příznak kontraktů. |
| AK12 | P01, P11, B02 | NTFS crash/fault scénáře, žádná truncation, chráněný staging, IPC při chybě. |
| AK13 | P03, B01, B02, B04 | Porovnání Reset odpovědi, explicitního create a startup bytes s jedinou produkční šablonou. |
| AK14 | P07, B06, B07 | Edit s nulovou/částečnou/úplnou sadou vždy ukazuje zaškrtnutou volbu. Totéž create/reset/repair. |
| AK15 | P08, B03, B05 | Původní kontrakty a klíče zůstávají, vložené klíče mapy jsou přesně M. |
| AK16 | P08, B03, B05, B06 | Druhé doplnění a nové otevření úplné sady nemění obsah kontraktů. |
| AK17 | P08, B03, B05 | False nemění obsah ani přítomnost contracts v edit/repair. |

Doplňkové povinnosti z funkčních částí zadání, které nejsou samostatným číslovaným AK:

| Požadavek | Kontrola |
|---|---|
| Funkční 1.7 a Konzistence ukládání | Instalátor nemá šablonu, startup nepoužívá IPC a existuje jediný ConfigStore. |
| Funkční 2.5 | Metadata neovlivňují běžný runtime ani jeho kryptografii. |
| Funkční 4.7 | Při chybě návrhu jde vypnout automatiku a ručně dokončit identitu. |
| Funkční 5.4 | Náhled zobrazuje existující a chybějící počet, odpovídá skutečnému M. |
| Funkční 5.7 | Zachovaný algoritmus payload klíče a nezávislé SHA-256 vektory. |
| Funkční 5.10 | První edit nad startovní šablonou má jeden původní contract a zapnuté doplnění. |
| Funkční 5.11 | Nepodporovaný CIDR má viditelný zakázaný checkbox, vysvětlení a žádné nové kontrakty. |
| Konzistence ukládání | Oba Resety používají tři true, editor umí nové členy otevřít a uložit a edit/repair zachová neprezentované hodnoty. |

## 11. Kontrola úplnosti plánu a stav dokončení

Při přípravě byly posouzeny všechny funkční části, konzistence ukládání, implementační vodítka a všech 17 kritérií celého formálního zadání proti aktuálním zdrojům. Byly rozlišeny dokumentové preference od runtime, úplná šablona editoru od konceptu Reset Wizardu a metadata JSON od jednorázového IPC pokynu kontraktů.

Byly vyřešeny vazby, které by jinak umožnily neúplnou implementaci: uzavřené čtyřpoložkové fields, uzavřené view, službový zákaz edit/repair doplnění, vazba nových polí na evidence a fingerprint, omezený přenos dat pro náhled, neprázdný IP placeholder, návrh bez kandidáta, původní kontrakt při změně vlastní IP a chyba recovery před dostupností IPC.

Plán stanoví konkrétní technická rozhodnutí T01 až T11 a nezanechává alternativní produktové chování k výběru při implementaci. Ověření bezpečné Windows publikace, degradovaného startu, IPC, UI a instalace zůstává budoucí prací podle kapitol 9 a 10. Tento dokument jejich úspěch netvrdí.

Jediným výstupem přípravy je `C:\git\phpvpn\plan3_fix2.md`. Nebyla provedena implementace, build, runtime test, změna instalované služby, zápis do ProgramData, commit ani push.
