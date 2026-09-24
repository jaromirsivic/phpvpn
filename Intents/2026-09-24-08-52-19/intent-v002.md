---
intent_version: 2
created_at: 2026-09-24-09-17-58
based_on: intent-v001.md
source: intent-chat-v002.md
---

# Řádky výstupu PowerShellu v instalačním a odinstalačním EXE

## Shrnutí

Zobrazovat výstup PowerShellu v instalačním a odinstalačním EXE na samostatných řádcích podle skutečných konců řádků ve výstupu. Dlouhý fyzický řádek se nesmí automaticky zalamovat podle šířky pole.

## Výslovné požadavky uživatele

- Instalace a odinstalace služby a tray aplikace přes PowerShell nyní fungují. Změna se týká zobrazení jejich textového výstupu.
- Ve víceřádkovém poli obou EXE se mají respektovat konce řádků, které PowerShell skutečně vydá.
- Word wrap není požadován. Dlouhý řádek zůstane jedním řádkem, i když je širší než textové pole.
- Není nutné spouštět celou testovací sadu. Proveď cílené ověření.
- Nakonec aktualizuj projektovou distribuci tak, aby obsahovala nově sestavené soubory.

## Zjištěný kontext projektu

- Oba režimy používají společné okno `PackageWindow` v `client/apps/package_gui/package_window.cpp`. Výstupní ovládací prvek vzniká v `PackageWindow::open` na řádcích 26 až 67.
- Výstup je víceřádkový Win32 `EDIT` prvek s vodorovným a svislým posuvem. `PackageWindow::append` na řádcích 90 až 97 předává dekódovaný text přímo do prvku přes `EM_REPLACESEL`.
- PowerShell se spouští a jeho výstup se zachytává v `client/apps/package_gui/script_runner.cpp` funkcí `runScript` na řádcích 73 až 159. `PackageWindow::poll` v `client/apps/package_gui/package_window.cpp` na řádcích 98 až 122 předává přijaté dávky k zobrazení.
- Jiné víceřádkové pole v projektu (`client/apps/tray/editor_window.cpp`, `EditorWindow::setContent`, řádky 151 až 171) při zobrazení doplňuje `CR` před samostatné `LF`. Jde o vodítko k prověření chování klasického Win32 `EDIT`, nikoli o předem určené řešení.

## Požadované chování

- Každý skutečný konec řádku ve výstupu PowerShellu vytvoří v textovém poli hranici řádku v obou EXE.
- Vizuální šířka okna počet řádků nemění. Delší řádky zůstávají vodorovně posuvné.
- Zobrazení zachová text a pořadí výstupu. Pokud je kvůli ovládacímu prvku nutné upravit reprezentaci konců řádků, jde pouze o úpravu pro zobrazení, nikoli o změnu významu výstupu.
- Průběžné doplňování výstupu správně zachází s koncem řádku i tehdy, když je rozdělen mezi dvě přijaté dávky.
- Svislé i vodorovné posouvání zůstane funkční. Běh instalačního a odinstalačního skriptu se nezmění.

## Vodítko k realizaci

Prověř skutečné konce řádků ve výstupu PowerShellu a chování `EDIT` prvku při jejich vložení přes `EM_REPLACESEL`. Oprav zobrazení ve společné cestě obou režimů. Zachovej vodorovný posuv a vypni jakoukoli změnu, která by zapínala word wrap. Ověř výsledek cíleným GUI testem s různými konci řádků, dlouhým řádkem a průběžným výstupem. Poté znovu sestav distribuci a ověř, že obsahuje opravená EXE.

## Akceptační kritéria

1. Instalační i odinstalační EXE zobrazí skutečné konce řádků výstupu PowerShellu jako oddělené řádky.
2. Dlouhý řádek se při zúžení okna vizuálně nezalomí a zůstane dostupný vodorovným posuvem.
3. Řádky, jejichž koncové znaky dorazí v různých dávkách, se zobrazí správně.
4. Obsah výstupu, běh obou skriptů a průběžné zobrazení zůstanou zachované.
5. Aktualizovaná distribuce obsahuje nově sestavená instalační a odinstalační EXE a její kontrola projde.
