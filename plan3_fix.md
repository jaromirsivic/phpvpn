# Oprava vytvoření konfigurace přes Edit Config

## 1. Cíl a rozsah

Vyřešit scénář: běží služba a tray, administrátor odstraní `C:\ProgramData\PHPVPN\config.json`, v tray zvolí Edit Config a potvrdí vytvoření. Při bezpečném stavu adresáře musí ihned vzniknout validní výchozí konfigurace a otevřít se její skutečný obsah. Pokud vytvoření blokují oprávnění, aplikace musí vysvětlit konkrétní příčinu a použitelný postup nápravy.

Tento dokument je plán, nikoli provedená implementace nebo oprava instalace. Vychází ze zdrojů v repozitáři, `client.md`, částí 5.5 a 5.6 a kritéria A37 v `plan3_client.md`, přiložené hlášky a níže uvedené kontroly místní instalace dne 6. 9. 2026. Protokol relay ani síťové chování se nemění.

## 2. Zjištěná příčina

### Ověřený stav instalace

Služba `phpvpn` běží jako `LocalSystem`. `config.json` v kontrolovaném okamžiku neexistuje. Adresář PHPVPN má vlastníka `BUILTIN\Administrators` a chráněnou DACL, ale obsahuje tři explicitní povolení FullControl:

| Identita | Stav vzhledem k politice backendu |
|---|---|
| `NT AUTHORITY\SYSTEM` | požadované |
| `BUILTIN\Administrators` | požadované |
| `NB4553\User` | dodatečné, backend je odmítá |

Kontrola `restricted()` v `client/src/platform/windows/config_files_win.cpp` vyžaduje přesně dvě položky ACL, pouze SYSTEM a Administrators. Třetí položka proto sama stačí k odmítnutí zápisu. Nejde o to, že by služba běžela pod běžným uživatelem nebo nutně neměla Windows právo zapisovat. Jde o nesoulad adresáře s bezpečnostní politikou aplikace.

Jak a kdy dodatečné oprávnění vzniklo, nebylo zjištěno. Nelze tvrdit, že samotné smazání souboru mění ACL nebo že je určitě změnil konkrétní dialog Průzkumníka. Rozlišit obyčejné administrátorské smazání souboru od zásahu, který navíc změní oprávnění adresáře.

### Cesta ke zobrazené chybě

1. `TrayApplication::startConfigAction()` a `finishConfigAction()` v `client/apps/tray/main.cpp` požádají o `get-config-document`.
2. `WindowsConfigFiles::readBounded()` otevře cestu k souboru. Při neexistenci vrátí missing, aniž by znovu kontrolovala DACL rodičovského adresáře. Proto tray nabídne vytvoření i za současného změněného ACL.
3. Po Yes odešle tray `create-default-config`. `WizardService::execute()` volá `ConfigStore::createDefault()`, který validuje sdílenou šablonu a volá `publish(..., createIfMissing, ...)`.
4. `WindowsConfigFiles::publish()` nejprve zavolá `recoverLocked()`. Jeho první kontrola `restricted(directories_.back().value, true)` při zjištěném ACL selže. Vrátí obecný `config_write_failed`, ještě před čtením publikačního žurnálu a vytvořením nového souboru.
5. `WizardIpc::safeError()` převede chybu na obecný text „The configuration file could not be written.“ a výchozí radu s tlačítkem Next. Tray ji zobrazí, přestože uživatel právě neprovádí krok Wizardu.

Tato cesta ve zdrojích plně vysvětluje hlášku při zjištěném stavu adresáře. Nebyl zachycen trace původního kliknutí ani ověřena binární shoda instalované služby se zdroji, proto jde o doložený blokující stav a odpovídající cestu v kódu, nikoli o tvrzení o odchyceném původním volání.

V adresáři zůstaly také `config.previous` a `config.publish-journal`. Jejich přítomnost je po potvrzeném uložení v této implementaci očekávaná. Obsah ani ACL těchto souborů nebylo možné současným tokenem přečíst. Nelze je označit za poškozené nebo za příčinu této chyby. Nesmějí se preventivně mazat.

## 3. Zvolené řešení

Zachovat ochranu SYSTEM/Administrators. Neodstraňovat kontrolu počtu položek ACL, nepovolovat zápis do libovolně rozšířeného adresáře a nepřenášet zápis configu do tray.

Oprava má dvě části:

- **Provozní náprava:** obnovit očekávaná oprávnění a znovu provést Edit Config. Již existující start služby volá `restrictDirectoryToAdministrators()` před vytvořením backendu. Pro zjištěnou změnu DACL je proto ručně provedený administrátorský restart služby dostupnou cestou obnovy politiky. Restart přeruší aktivní VPN a sám konfiguraci nevytvoří. Po úspěšném startu zvolit Edit Config a Yes. Úspěch celého postupu ověřit, mohou se následně odhalit další nezávislé chyby obnovy.
- **Oprava aplikace:** místo obecného selhání zápisu vracet konkrétní chybu bezpečnosti adresáře, zobrazit správný postup a pokrýt rozdíl mezi smazáním souboru a změnou ACL regresními testy.

Automatická změna ACL při kliknutí Yes není součástí této opravy. Znamenala by novou správní operaci a změnu současné politiky, podle které Reset neopravuje ACL. Pro tento problém není nutná. Tray také nebude samo restartovat službu.

## 4. Implementační kroky

### F01: Reprodukce před změnou implementace

V izolovaném testovacím adresáři na NTFS se správným ACL vytvořit backend a konfiguraci. Se stejnou instancí backendu provést dvě varianty:

1. Odstranit jen `config.json` administrátorským procesem, bez změny ACL. Ověřit nové vytvoření přes `ConfigStore::createDefault()` a validitu výsledku.
2. Po vytvoření backendu přidat třetí explicitní FullControl ACE pro testovacího uživatele, odstranit config a zopakovat čtení a vytvoření. Před opravou očekávat missing při čtení a `config_write_failed` při zápisu. Tím oddělit příčinu od samotného smazání souboru.

Pokrýt i stav s potvrzeným žurnálem a zálohou po předchozím Save. Pokud první varianta selže i se správným ACL, zaznamenat konkrétní publikační fázi a Win32 chybu, opravit prokázanou další chybu a doplnit regresi. Nepředpokládat předem chybu atomického rename nebo potřebu mazat žurnál.

### F02: Konkrétní chyba adresáře

V `client/src/core/base/error.h` a `error.cpp` přidat stabilní kód `config_directory_security_invalid`. Význam: bezpečnostní descriptor konfiguračního adresáře byl úspěšně načten, ale neodpovídá požadované politice. Popis v UI zůstane anglický.

V `client/src/platform/windows/config_files_win.cpp` rozlišit alespoň:

- nepovolený bezpečnostní descriptor adresáře,
- selhání načtení descriptoru nebo přístupu,
- jinou chybu publikace či obnovy.

Dosavadní boolean kontrolu podle potřeby rozdělit na kontrolu s typovaným výsledkem a pomocný predikát. Pouhé selhání Win32 API nesmí být automaticky označeno jako prokázaná změna oprávnění. Nový kód vracet konzistentně při kontrole adresáře v konstrukci backendu, `recoverLocked()` a `publish()`.

Kontrolu při publikaci ponechat i po dřívějším úspěšném čtení. ACL se mohou změnit během potvrzovacího dialogu. Nezavádět další periodické kontroly, watcher ani automatické opakování zápisu. Missing nadále popisuje neexistenci souboru, není příslibem možnosti jeho vytvoření.

Diagnostika v logu může obsahovat pevný identifikátor fáze a okamžitě zachycený Win32 kód. Log ani IPC nesmějí obsahovat JSON konfigurace, klíče nebo syrový obsah žurnálu. Úprava diagnostiky nesmí převést neznámý výsledek již zahájené publikace na falešné tvrzení, že nic nebylo zapsáno.

### F03: IPC a srozumitelné hlášení

V `client/src/core/ipc/wizard_ipc.cpp` doplnit bezpečný popis a `suggestedAction` nového kódu. Stávající obálka chyby stačí, není nutné přidávat libovolné interní detaily nebo měnit verzi IPC. Prověřit seznamy přijímaných kódů a stávající testy chyb.

Příklad výsledného textu pro tento případ:

> The configuration folder permissions do not match PHPVPN's required security settings. Ask an administrator to restart the PHPVPN service to restore the folder permissions, then choose Edit Config again. Restarting the service disconnects the VPN.

Tray doplní autoritativní cestu ze služby. Pro tuto chybu v `finishConfigAction()` nepřidá obecnou radu otevřít Wizard nebo kopírovat konfiguraci. Wizard používá stejný backend a problém oprávnění tím nezmizí.

Odstranit také obecnou radu „choose Next“ pro souborové chyby mimo síťové kroky Wizardu. U ostatních `config_write_failed` uvést neutrální postup kontroly chyby a nového vyvolání akce, bez nepravdivého určení příčiny a bez automatického retry. Specifické rady skutečných kroků Wizardu zachovat.

### F04: Testy a dokumentace

| Oblast | Požadované ověření |
|---|---|
| `client/tests/unit/test_config_files_win.cpp` | Stejný backend po delete/recreate funguje se správným ACL. Přidání třetí ACE za běhu vrací nový kód bez zápisu. Selhání čtení ACL není zaměněno s nepovoleným descriptorem. |
| Obnova publikace | Potvrzená publikace, následné smazání cíle a nové vytvoření fungují i se zbývajícím žurnálem a zálohou. Neznámý nebo poškozený žurnál si zachová vlastní chybu. |
| Obnova oprávnění | V testovací fixture obnovit DACL pomocí produkčního postupu a ověřit, že následné explicitní vytvoření uspěje. Neopravovat ani nemazat pomocné soubory testovacím fallbackem. |
| `test_config_store.cpp` | Nová chyba se zachová do volající vrstvy. Validace šablony a create-if-missing zůstanou beze změny. |
| `test_wizard_ipc.cpp`, službové testy | Nový kód, bezpečný text a configPath projdou obálkou a parserem. Souborová chyba neobsahuje radu Next ani interní citlivý detail. |
| Tray a skutečná služba | Celé Edit Config → Yes po administrátorském smazání otevře skutečný validní dokument. Cancel soubor zachová. No nic nevytvoří. Změněné ACL zobrazí konkrétní nápravu bez otevření prázdného editoru. |
| Souběh | Cizí config vzniklý během dialogu se nepřepíše defaultem. Změna ACL během dialogu bezpečně zastaví zápis. |

Windows testy chráněných ACL provést s oprávněným tokenem a skutečnými NTFS soubory. Dosavadní `SecureFixture` může bez administrátorského tokenu vypsat NOT RUN a vrátit se, takže samotný zelený souhrn testů neprokazuje tuto regresi. Report musí potvrdit, že konkrétní scénáře skutečně běžely.

Po implementaci přeložit konfigurace debug a release pomocí existujících CMake presetů a spustit odpovídající CTest. Samostatně provést Windows ACL regresi a ruční scénář s produkční tray a izolovanou službou. Síťový relay není pro samotné vytvoření výchozího configu potřeba.

V `client.md` doplnit nový chybový kód, vysvětlení politiky adresáře a postup administrátorské nápravy. V testovacím reportu a matici plan3 doplnit výsledky A03/A37. Historické NOT RUN nepřepisovat na údajný dřívější úspěch. `protocol.md` nevyžaduje změnu.

## 5. Kritéria dokončení

1. Pouhé smazání souboru při zachovaném ACL a běžící službě nebrání potvrzenému vytvoření, bez restartu služby.
2. Dodatečné oprávnění uživatele vyvolá konkrétní chybu adresáře a použitelný návod, nikoli obecný text s Next.
3. Administrátorská obnova očekávaných oprávnění umožní následné explicitní vytvoření, doložené testem.
4. Create-if-missing zachová cizí existující soubor, původní ochrany publikace, limity a validaci. Cancel již vytvořený soubor nemaže.
5. Instalace, start služby ani Connect konfiguraci samy nevytvoří. Aktivní VPN se nezmění při pouhém pokusu o vytvoření.
6. Report obsahuje skutečně provedené testy i zbývající omezení, včetně výsledku celého tray scénáře.

## 6. Co bylo při přípravě provedeno

Prostudování relevantních zdrojů a dokumentace, porovnání hlášky s mapováním chyb, read-only kontrola služby, seznamu souborů a ACL adresáře. Nebyly změněny zdrojové soubory aplikace, oprávnění instalace ani obsah ProgramData. Služba nebyla restartována, config nebyl vytvářen a runtime scénář ani build/testy nebyly spuštěny. Jediným novým výstupem je tento plán.
