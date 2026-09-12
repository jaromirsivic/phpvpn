# Report úlohy 16: D16, TT15 a TT17

Stav: dokumentace D16 a dvě chybějící regrese dokončeny. Cílené dirty/HC ověření prošlo. Širší ContractsDocument má čtyři existující selhání popsaná níže, celá TT17 proto není označena za PASS. Společný časový limit byl předán jako uživatelem schválený neomezený. Větev `tray_UX`, HEAD `1c25096111cdf20f901c58da6299ddde7450e229`. Bez změn produkčního kódu, commitu, pushe či worktree. R04 a B01–B05 nebyly řešeny.

Při obnovení nebo předání kontextu nejprve obnov celý účinný globální blok instrukcí z reálného CODEX_HOME a jeho AGENTS.override.md nebo AGENTS.md, případně z úplného hostem dodaného bloku. Ověř obě přesné samostatné hranice před další věcnou prací. Tento report chráněný blok nenahrazuje. Globální soubor byl načten celý v UTF-8, projektový AGENTS.md nebyl nalezen.

## Změny a dohledatelnost

- [Plán D16/TT15/TT17](../../../Plans/2026-09-12-02-29-09/plan-v003.md), [R08/R09](../../../Plans/2026-09-12-02-29-09/rozhodnuti-v004.md) a [T19](../../../Plans/2026-09-12-02-29-09/T19.md), [T24](../../../Plans/2026-09-12-02-29-09/T24.md), [T25](../../../Plans/2026-09-12-02-29-09/T25.md) určují rozsah. Matice byla nejprve označena jako probíhající a poté doplněna o výsledky pouze těchto tří témat, včetně zachování D15/R07 u T25.
- [client.md](../../../client.md), Sdílená Win32 infrastruktura, a [tray_UX.md](../../../tray_UX.md), UX02: odstraněna výlučnost obnovy cache při DPI. [UiResources::update](../../../client/apps/tray/ui_resources.cpp) porovnává DPI, HINSTANCE a požadavek na ikony. Shodná aktiva a paleta jsou no-op. Změna HC nebo jeho systémových barev nahradí barevné prostředky, zachová fonty/ikony. Mimo HC zůstává světlá paleta. Dokumentace jmenuje skutečné adaptéry s WM_SETTINGCHANGE/WM_SYSCOLORCHANGE, netvrdí nové obecné WM_THEMECHANGED zpracování.
- client.md, Přenosné jádro Communication contracts, a tray_UX.md UX11/UX12: odstraněno dirty z pouhého potvrzení generování. [ContractRow::dirty](../../../client/src/core/presentation/contracts_model.cpp) porovnává enabled a klíč pouze při enabled. Shodný výsledný klíč ani návrat k originálu dirty nevynucují. Klíč původně i nadále vypnutého řádku není publikován a sám dirty nevytváří. Vypnutí původně zapnutého řádku změnou je. saveUnknown, generace a společný Exit zůstávají zachované.

## Testové změny

- [test_contracts_model.cpp](../../../client/tests/unit/test_contracts_model.cpp): nový `ContractsModel.enabled_same_generated_key_is_clean_and_different_result_is_dirty`. Načte původně enabled kontrakt s deterministickým klíčem, ověří skutečné potvrzené Random se shodným výsledkem, shodný vstup applyPasswordKey, odlišný potvrzený klíč, návrat k originálu a enabled toggle. Test nepředstírá nativní kliknutí v Password okně. Existující `key_validation_all_rows_dirty_restore_and_focus` již přímo kontroluje disabled-only klíč bez dirty, nebyl duplikován.
- [test_tray_palette.cpp](../../../client/tests/unit/test_tray_palette.cpp): nový `TrayResources.HighContrastAndSystemPaletteKeepFontsAndIconsAtSameDpi`, registrovaný v [CMakeLists.txt](../../../client/tests/CMakeLists.txt). Překládá přímo současný ui_resources.cpp pod testovým jménem třídy, pouze čtecí SystemParametersInfoW a GetSysColor mají lokální deterministické vstupy. Nemění produkční API ani nastavení uživatelské plochy. Skutečné GDI a načítání ikon zůstávají Windows operacemi. Ověří HC zapnutí, změnu systémové barvy při trvajícím HC, vypnutí HC, změnu barvy/per, zachování fontu/ikony/DPI a no-op při opakování. Systémové zapůjčené brushes nenahrazuje ani jejich simulovanou barvu netestuje. Není to důkaz doručení OS zpráv ani skutečné přístupnosti.

## Přesně spuštěné ověření

Build přes `cmd /c work\d16-build.cmd`, jeho obsah:

```bat
@call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -no_logo -arch=x64 -host_arch=x64
@if errorlevel 1 exit /b %errorlevel%
@cmake --build work/build/debug --target phpvpn-client-tests
```

Debug build prošel, následná kontrola stejným příkazem vrátila exit 0 a `ninja: no work to do.`. Každý filtr níže byl samostatný proces s příkazem `work/build/debug/tests/phpvpn-client-tests.exe FILTR`, výstup uložen přes Tee-Object do `work/task-16-FILTR.log` bez koncové tečky filtru.

| FILTR | Výsledek |
|---|---|
| `ContractsModel.` | 10 PASS, exit 0 |
| `ContractsDocument.` | 6 spuštěno, 2 PASS / 4 FAIL, exit 1 |
| `PasswordKey.` | 4 PASS, exit 0 |
| `TrayResources.` | 5 PASS, exit 0 |
| `TrayGeometry.` | 9 PASS, exit 0 |
| `TrayShell.` | 2 PASS, exit 0 |
| `TrayControls.` | 2 PASS, exit 0 |
| `TrayPlacement.` | 1 PASS, exit 0 |

Celkem 39 C++ testů, 35 PASS a 4 FAIL. `uv run client/tools/check_icons.py` prošel, tři varianty po osmi snímcích, celkem 24 bitmap. Testy Resources zahrnují již existujících 500 paintů a 20 cyklů cache, nejde o plnou UX14 výkonnostní bránu.

## Existující selhání a NOT RUN

ContractsDocument selhává v `absent_present_empty_and_unchecked_removal` (řádek 26), `preserves_all_unrelated_values_types_and_optional_presence` (70), `complete_snapshot_rejects_omissions_duplicates_foreign_self_and_bad_keys` (103) a `result_document_limit_and_actual_encoded_envelope` (141). První, druhý a čtvrtý scénář zapínají prázdný klíč a před očekávanou publikační/velikostní kontrolou dostanou config_invalid_key. Třetí předpokládá, že další řádky přidají bajty nad přesných 524288, ale ostatní klíče jsou prázdné. Model už před D16 nechával chybějící klíče prázdné, viz historie `git log -2 --oneline -- client/src/core/presentation/contracts_model.cpp` a commit `a1facef`. `git diff -- client/tests/unit/test_contracts_document.cpp client/src/core/presentation/contracts_model.cpp client/src/core/config/contracts_document.cpp` je prázdný. Tyto soubory D16 nezměnilo.

Také starší popis automatického defaultu v client.md a UX11 neodpovídá této dřívější mikro úpravě. D16 řeší pouze HC a výsledkové dirty, neslučuje sem změnu default/empty Password UX. Koordinátorovi je předána potřeba samostatně sladit tento popis a čtyři staré fixture s již dodaným chováním. Nové dirty očekávání na chybějícím defaultu nezávisí.

NOT RUN: fyzické přepnutí HC a systémových barev, screen reader, klávesnicová akceptace všech oken, přepínač aplikací a titul Password, multi-monitor/unplug a skutečné DPI 100/125/150/200 %, WP05Windows a aplikační Exit/session broadcast, kompletní vizuální a Release výkonová brána UX14, ASAN, plný CTest, skutečná VPN/služba/SCM/Wintun. Existující lifecycle kód a testy se neměnily. Modelové PASS tyto scénáře nenahrazují.

## Kontrola dokumentace a rozsahu

Závěrečná kontrola PowerShell regexem pro Markdown odkazy a Test-Path ověřila 112 lokálních souborových odkazů v obou cílových dokumentech, matici a tomto reportu, 0 chyb. `git diff --check` prošel s exit 0, pouze běžná upozornění na budoucí LF/CRLF konverzi. `git diff --numstat -- protocol.md` je prázdný. Sdílený checkout při vstupu již obsahoval necommitované změny D10–D15 a progress koordinátora. D16 upravilo pouze výše uvedené dva cílové dokumenty, tři řádky matice, dvě testové implementace, jednu registraci testu a tento report. protocol.md, historické zdroje ani cizí rozpracované soubory nebyly změněny. Plný V01–V06 a D17 zůstávají koordinátorovi.
