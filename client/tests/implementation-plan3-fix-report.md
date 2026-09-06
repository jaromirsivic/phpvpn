# Oprava Edit Config podle plan3_fix.md

Provedeno 6. září 2026 v `C:/git/phpvpn`. Výchozí commit je `f0daf6915801d43f9f4a21110ed40d112429017c`, distribuce obsahuje aktuální necommitnuté změny této opravy. Verze klienta zůstává 1.0.0, cílem je Windows x64.

## Výsledek

Backend zachovává přesnou politiku SYSTEM/Administrators. Úspěšně přečtený, ale nepovolený descriptor adresáře vrací `config_directory_security_invalid` při konstrukci, obnově i publikaci. Chyba načtení descriptoru zůstává `config_unreadable`. Selhání publikace, poškozený žurnál a neznámý výsledek nejsou převáděny na chybu oprávnění.

IPC přenáší stabilní kód, bezpečný anglický popis, návod na administrátorský restart služby a autoritativní configPath. Text upozorňuje na odpojení VPN a nutnost znovu explicitně zvolit Edit Config. Tray pro tento kód nepřidává radu otevřít Wizard nebo kopírovat konfiguraci. Souborové chyby mimo síťové kroky nepoužívají Next, nápověda síťových kontrol zůstává zachována. Žádné automatické změny ACL, restart, retry ani změny relay protokolu nebyly přidány.

## Reprodukce před opravou

Dvě nové regrese běžely ještě nad původní produkční implementací, s administrátorským tokenem a skutečnými NTFS soubory v náhodném dočasném adresáři:

- Samotné smazání configu po Save, při existujícím potvrzeném žurnálu a záloze, umožnilo okamžité vytvoření a následné čtení validního defaultu stejnou instancí ConfigStore/backendu.
- Třetí explicitní FullControl ACE aktuálního testovacího uživatele po načtení missing vyvolala původní `config_write_failed`. Žurnál a záloha zůstaly stejné. Produkční `restrictDirectoryToAdministrators()` následně umožnila explicitní vytvoření bez testovacího čištění pomocných souborů.

[Baseline log](../../work/plan3-fix-baseline-tests.log) obsahuje 2 úspěšné testy. První vývojový běh nové regrese měl chybu životnosti dočasné hodnoty v testovacím makru. Byla opravena v testu, další chyba publikace se neprokázala. [Původní vývojový log](../../work/plan3-fix-baseline-test-lifetime-error.log) je zachován.

## Cílené ověření konečné opravy

Debug i Release byly sestaveny existujícím `client/tools/build-x64.ps1`, bez přepínače Test. Podle výslovného zadání uživatele nebyl spuštěn kompletní CTest ani komplexní síťové, zátěžové nebo instalační testy. Spuštěny byly pouze tyto filtry `phpvpn-client-tests.exe`:

| Filtr | Debug | Release | Účel |
|---|---:|---:|---|
| `config_files.` | 15/15 | 15/15 | Skutečné chráněné NTFS soubory, delete/recreate, třetí ACE, obnova ACL, chyba descriptor API, zachování žurnálu a pravidel publikace |
| `config_store.` | 7/7 | 7/7 | Propagace nové chyby, validace a create-if-missing, zachování cizího obsahu |
| `wizard_ipc.` | 11/11 | 11/11 | Přenos kódu, bezpečný text, configPath, parser a zachování nápovědy síťových kontrol |
| `wizard_service.create_default_preserves` | 1/1 | 1/1 | Produkční WizardService nad testovacím backendem, propagace chyby bez konfigurace a HTTP |
| **Celkem** | **34/34** | **34/34** | **68 provedených cílených testů, 0 selhání, 0 NOT RUN** |

Windows regrese byly spuštěny přes standardní UAC s administrátorským tokenem. Logy obsahují konkrétní EVIDENCE řádky pro oba hlavní scénáře i chybu descriptor API. Kontrola runneru odmítá výstup NOT RUN nebo nulový počet testů. Chyba načtení descriptoru je deterministicky vyvolána předáním nulového handle skutečnému `GetSecurityInfo`, při konstrukci, v recoverLocked a při druhé kontrole publish. Tento test neprokazuje všechny možné příčiny OS access-denied.

- [Debug log](../../work/plan3-fix-debug-tests.log), [Release log](../../work/plan3-fix-release-tests.log), [návratový kód 0](../../work/plan3-fix-tests-exit.txt).
- [Cílený runner](../../work/run-plan3-fix-tests.ps1), [Debug build](../../work/plan3-fix-debug-build.log), [Release build](../../work/plan3-fix-release-build.log).

Závod s cizím configem ověřuje existující `create_race_and_pinned_directory`. Změnu ACL mezi čtením missing a vytvořením simuluje nová regrese s třetí ACE. Při odmítnutí nepřibude config a přesné načtené obsahy žurnálu i zálohy zůstávají stejné. Poškozený žurnál po smazání cíle stále vrací `journal_corrupt`, nejasná starší publikace stále zůstává unknown.

Celý interaktivní průchod produkčního tray menu proti skutečné SCM službě, včetně Yes, No a následného Cancel, nebyl v tomto cíleném běhu proveden. Úprava `finishConfigAction()` byla ověřena kontrolou zdroje a překladem. Historické NOT RUN pro celé A03/A37 tedy zůstávají v platnosti. Běžící instalovaná služba, její konfigurace a oprávnění nebyly měněny. Testy používají vlastní dočasné adresáře, které fixture uklízí.

## Distribuce

Nová distribuce je v [dist](../../dist), samostatný Windows balíček v [dist/client/Windows/x64](../../dist/client/Windows/x64). [ZIP celé distribuce](../../work/phpvpn-plan3-fix-2026-09-06.zip) zachovává strukturu server/client a má 17 souborů. macOS a Linux obsahují pouze README s informací, že klient není implementován. Provozní config ani soukromý podpisový klíč v balíčku nejsou.

`dist/server/php-vpn.php` má stejný SHA-256 jako aktuální `phpvpn.php`. Všechny tři EXE i originální Wintun DLL byly ověřeny jako AMD64. Výpis importů přes dumpbin ukázal systémové závislosti bez potřeby dodávat samostatnou MSVC runtime DLL. BAT vstupy odkazují na přítomné sousední PowerShell skripty. `install.ps1 -VerifyOnly` ověřil podpis manifestu, všech 11 hashovaných klientských souborů, podpis a povinné API Wintunu bez instalace. Všech 17 položek ZIPu bylo znovu porovnáno délkou a SHA-256 s distribučním stromem.

Manifest a EXE používají existující vývojový certifikát `CN=phpvpn Development Test Signing`, thumbprint `7430CCA24F5DE8A6614D582AFBFED7C80E0F2CA8`. Pinovaný podpis manifestu prošel. Authenticode EXE hlásí UnknownError kvůli nedůvěryhodnému kořenu certifikátu, nejde o veřejně důvěryhodný vydavatelský podpis. Authenticode Wintun 0.14.1 je Valid. Trust store nebyl změněn.

[Audit balíčku](../../work/plan3-fix-package-audit.json), [VerifyOnly log](../../work/plan3-fix-verify-package.log), [SHA-256 ZIPu](../../work/phpvpn-plan3-fix-2026-09-06.zip.sha256): `9781927fc3cc3f7a1eb7fd9031a4ffac7dcbc4a0055a068f217e74f8536478ac`.

`git diff --check` prošla. Nebyl vytvořen commit ani push.
