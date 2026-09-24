---
intent_version: 1
created_at: 2026-09-24-08-58-27
source: intent-chat-v001.md
---

# Zalamování výstupu v instalačním a odinstalačním EXE

## Shrnutí

Upravit zobrazení textového výstupu PowerShellu v instalačním a odinstalačním EXE tak, aby se dlouhé řádky ve víceřádkovém textovém poli zalamovaly podle dostupné šířky okna.

## Výslovný požadavek uživatele

- Instalace a odinstalace služby a tray aplikace přes PowerShell nyní fungují. Změna se týká zobrazení jejich textového výstupu.
- Ve víceřádkových polích instalačního i odinstalačního EXE se má text zalamovat.

## Zjištěný kontext projektu

- Oba režimy používají společné okno `PackageWindow` v `client/apps/package_gui/package_window.cpp`. Výstupní ovládací prvek vzniká v `PackageWindow::open` na řádcích 26 až 67.
- Jde o víceřádkový Win32 `EDIT` prvek s příznaky `WS_HSCROLL` a `ES_AUTOHSCROLL`, které souvisejí s aktuálním vodorovným zobrazením řádků.
- Výstup PowerShellu se zachytává v `client/apps/package_gui/script_runner.cpp` funkcí `runScript` na řádcích 73 až 159 a do okna se průběžně doplňuje přes `PackageWindow::poll` v `client/apps/package_gui/package_window.cpp` na řádcích 98 až 122.

## Požadované chování

- Dlouhý výstupní řádek je v obou EXE čitelný na více vizuálních řádcích v rámci šířky textového pole.
- Změna šířky okna odpovídajícím způsobem změní vizuální zalomení.
- Zobrazení zachová původní text, pořadí výstupu a skutečné konce řádků. Vizuální zalomení nemění obsah výstupu ani běh skriptu.
- Průběžné doplňování výstupu a svislé posouvání zůstanou funkční.

## Dotčená oblast a vodítko k realizaci

Primární místo změny je nastavení stylu výstupního prvku v `PackageWindow::open`. Ověřit chování Win32 `EDIT` při odstranění vodorovných stylů, zejména `ES_AUTOHSCROLL` a `WS_HSCROLL`, a případně upravit navazující rozvržení. Není důvod měnit samotné instalační a odinstalační skripty, pokud se při realizaci neobjeví konkrétní souvislost.

## Akceptační kritéria

1. Instalační EXE zalamuje dlouhé řádky výstupu PowerShellu podle aktuální šířky pole.
2. Odinstalační EXE se chová stejně.
3. Vizuální zalomení nezmění obsah zachyceného výstupu ani existující funkčnost instalace a odinstalace.
4. Výstup zůstává průběžně čitelný a lze jím svisle procházet.
