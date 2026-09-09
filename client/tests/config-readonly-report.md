# Read-only datový strom: implementace a ověření

Dokončená implementace 9. září 2026, výchozí HEAD `56a5287`, přímý pracovní strom `C:/git/phpvpn`. Bez commitu nebo push. Debug i Release build a integrita distribučního balíčku jsou ověřené. Plná testovací sada není zelená, známá selhání a neprovedené administrátorské testy jsou uvedeny níže.

## Závazný rozsah

Konečné rozhodnutí uživatele dovoluje BUILTIN\Users Read & Execute na všechny současné a budoucí potomky datového adresáře, včetně aktuálních, historických a rozpracovaných klíčů, publikačního žurnálu, ownership metadat a logů. SYSTEM/Administrators mají Full Control. Přímé neprivilegované mutace zůstávají zakázané. Hardlinková záloha se nemění na kopii. Kontrola a normalizace oprávnění patří do každého startu služby, nejde o jednorázovou migraci ani o tvrzení, že existují produkčně nasazené starší verze.

Oprava připne předky i všechny nalezené objekty bez sdílení mazání. Adresáře nejprve zabezpečí bez dědičných ACE, aniž by měnila neprověřené potomky. Po kontrole identity a počtu interních názvů hardlinků zabezpečí soubory a povolí bezpečnou dědičnost adresářů. Low-level `SetFileSecurityW` zde používá cestu jištěnou připnutým objektem i všemi předky. Na rozdíl od `SetSecurityInfo` neprovádí automatickou propagaci do neprověřených potomků. Toto chování potvrzuje vlastní neprivilegovaný NTFS test a popis rozdílu low-level setterů v [Microsoft dokumentaci](https://devblogs.microsoft.com/oldnewthing/20200102-00/?p=103287).

Kanonické descriptory se nepřepisují. Počet hardlinků se ověřuje před i po skutečné opravě a na konci normalizace. Uživatel výslovně potvrdil úzkou výjimku pro nový externí hardlink přidaný souběžně během změny ACL téhož připnutého inode, s vědomím, že kontrola/oprava může nastat při každém startu. Následně detekovaná změna zastaví inicializaci, ACL tohoto souběžně přidaného názvu ale již může být změněná. Existující vnější linky se odmítnou před opravou souboru. Samostatný lokální probe prokázal, že ani nulový share mode nezakazuje vznik nového hardlinku. Výjimka neumožňuje záměnu připnutého souboru za jiný.

## Testovací matice

| Oblast | Automatický důkaz | Stav |
|---|---|---|
| Sémantická ACL, bezpečné extra ACE, generická a složená práva | `config_acl.canonical_and_safe_extra_reads_without_elevation` | Debug/Release PASS |
| Zápis, mazání, WRITE_DAC, WRITE_OWNER, nebezpečná dědičnost, omezující deny | `config_acl.rejects_mutation_inheritance_and_privileged_restriction` | Debug/Release PASS |
| Skutečný Windows AccessCheck s tokenem bez Administrators | `config_acl.standard_token_effective_access` | Debug/Release PASS |
| ACL setter nezasahuje neprověřené potomky | `config_acl.pinned_low_level_setter_does_not_propagate_to_uninspected_child` | Debug/Release PASS, skutečný NTFS |
| Čtení/kopie celého stromu na NTFS, zákaz mutací, následná publikace | `config_files.readonly_tree_actual_ntfs_access_and_publication` | Elevated NOT RUN |
| Rozlišení chyby descriptor API od prokázané ACL chyby | Stávající `config_files.directory_descriptor_api_failure_is_not_a_policy_mismatch` | Elevated NOT RUN |
| Atomic rename, create-only, uzamčený cíl, identity a nenásledování nahrazeného odkazu | `config_files_probe.*` | Debug/Release PASS, skutečný NTFS |
| Recovery, crash boundaries, UTF-8/velikosti, připnutí a race ochrany | Stávající `config_files.*`, `ConfigStore`, Wizard testy | Neprivilegované PASS, privileged NOT RUN |
| Wizard/Edit Config bezpečný text a autoritativní cesta | `wizard_ipc.configuration_errors_have_safe_action_and_authoritative_path` | Debug/Release PASS |
| Normalizace staré/Explorer ACL bez změny bajtů a identity hardlinku | `config_files.startup_migrates_legacy_and_explorer_acl_without_byte_or_identity_changes` | Elevated NOT RUN |
| Vnější hardlink/reparse bez změny ACL externího souboru | `config_files.startup_rejects_external_hardlinks_without_external_acl_change`, `startup_rejects_reparse_without_external_acl_change` | Elevated NOT RUN |
| Extra read ACE dovolí Save, inherit-only write ACE jej zablokuje | `config_files.safe_extra_read_ace_allows_save_unsafe_inherited_write_blocks_it` | Elevated NOT RUN |
| Nativní Wizard Finish a lost outcome | Podporovaný `dev-tray.ps1 -Mode Test -Filter wp04_production` | 2/2 PASS |
| Explorer/Notepad, skutečný instalátor, SCM a UAC | Ruční ověření uživatelem | NOT RUN |

AccessCheck nad security descriptorem není vydáván za privilegovaný test skutečného NTFS souboru. Aktuální proces používá medium integrity token a Administrators má pouze deny-only. Harness označuje i přeskočené privileged případy jako OK, rozhodující je proto jejich výslovný výstup `[ NOT RUN ]`.

Finální Debug i Release CTest: 310 registrovaných případů, 4 selhání uvedená níže. Samostatné ACL případy 4/4 PASS. Souborová sada obsahuje 23 případů, z toho 20 vyžaduje zvýšený token a nebylo provedeno. Logy jsou v `work/config-readonly-ctest-debug.log`, `work/config-readonly-ctest-release.log`, `work/config-readonly-acl-*.log` a `work/config-readonly-files-*.log`.

## Nesouvisející výchozí selhání

Celá Debug sada odhalila čtyři chyby v nezměněném `test_contracts_document.cpp`. Nezměněný `ContractsModel::create()` ponechává chybějící klíče prázdné a `setEnabled()` je nedoplňuje. Testy `absent_present_empty_and_unchecked_removal`, `preserves_all_unrelated_values_types_and_optional_presence` a `result_document_limit_and_actual_encoded_envelope` stále předpokládají platný klíč po pouhém zapnutí řádku, proto dostanou `config_invalid_key`. Test `complete_snapshot_rejects_omissions_duplicates_foreign_self_and_bad_keys` očekává překročení součtového limitu po vložení textu přesně o velikosti limitu, ostatní řádky ale nyní mají nulovou délku. Tento předpoklad také pochází z doby automaticky doplňovaných klíčů. Tyto soubory ani chování v tomto úkolu neměním.

Nativní `phpvpn-wp03-ui-tests` vykázal 11 selhání z 25 i při spuštění bez konzole a bez skrytí prvního GUI okna. Jde o `ReferenceGeometrySurvivesNativeResizeAndEditorUndo` a deset WP05 případů s neplatnými kontraktovými předpoklady, geometrií nebo vstupem klíče. Samostatný podporovaný `editor_reset_failure_generation` selhal na očekávání načtení společné šablony. Tyto nezměněné fake-backend/GUI scénáře filesystem ACL nevolají. Nejsou vykazovány jako úspěšné a jejich oprava není součástí tohoto úkolu. Přesné výstupy: `work/config-readonly-native-ui-correct-launch.log`, `work/config-readonly-editor-supported.log`. První pokus přes `WindowStyle Hidden` navíc skryl první testované okno, proto byl zopakován správným způsobem. Úspěšné produkční Wizard Finish scénáře jsou v `work/config-readonly-wizard-supported.log`.

## Distribuce

Finální Release programy, originální Wintun, instalační skripty a aktuální server tvoří nový strom `C:/git/phpvpn/dist`. Původní dist je zachován mimo distribuci v `work/dist-before-config-readonly-20260909`. Strom obsahuje předepsaných 17 souborů, Linux/macOS mají README o nepodporované distribuci. Neobsahuje provozní konfiguraci, soukromé klíče ani testovací binárky.

Sestavení proběhlo přes `client/tools/build-x64.ps1 -Preset debug` a `-Preset release`. Balíček vytvořil `make-dist.ps1 -SignBinaries`, rozšířený `work/verify-fix2-distribution.ps1` ověřil přesný strom, PE sekce proti Release, hashe zdrojů a podpisy. Podporované `install.ps1 -VerifyOnly` prošlo, včetně pinovaného RSA podpisu manifestu, hashů, x64 a Wintun API. Logy: `work/config-readonly-package-build.log`, `work/config-readonly-package-verify.log`, `work/config-readonly-package-supported.log`.

Podpis manifestu i EXE používá existující vývojový certifikát `CN=phpvpn Development Test Signing`, thumbprint `7430CCA24F5DE8A6614D582AFBFED7C80E0F2CA8`. Integrita je ověřená pinovaným podpisem, nejde o veřejně důvěryhodný vydavatelský certifikát. Trust store nebyl měněn. Skutečná instalace, SCM, restart provozní služby a Explorer/Notepad pod jiným uživatelem nebyly provedeny. Instalátor pro ruční ověření je `dist/client/Windows/x64/install.bat`.

## Ruční dokončení akceptace

1. Ve zvýšeném terminálu spustit `work/build/release/tests/phpvpn-client-tests.exe config_files`. Používá náhodné dočasné NTFS fixture, nikoli provozní ProgramData. Ověřit, že privileged případy skutečně neběží jako NOT RUN.
2. Spustit připravený instalátor, ověřit zachování existujících bajtů konfigurace a restart aktualizované služby.
3. Bez zvýšených práv otevřít datovou složku v Exploreru a zkopírovat konfiguraci, zálohu a log. Nesmí se objevit administrátorské pokračování ani změna ACL. Přímý zápis, tvorba, mazání, přejmenování a změny zabezpečení musí selhat.
4. Provést Edit Config/Save a Wizard/Finish. Znovu ověřit čitelnost po publikaci a po dalším restartu. Přijatá hardlinková souběžná výjimka zůstává výslovně popsaná v `client.md`.
