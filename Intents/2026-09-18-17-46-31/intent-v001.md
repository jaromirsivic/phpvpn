---
intent_version: 1
created_at: 2026-09-18-17-55-45
source: intent-chat-v001.md
---

# Viditelné potvrzení úspěšné instalace a odinstalace

## Shrnutí

Upravit podporované instalační a odinstalační vstupní body Windows balíčku tak, aby po úspěšném dokončení nezmizelo konzolové okno bez srozumitelné zpětné vazby. Po skutečně úspěšné instalaci nebo odinstalaci se má zobrazit zřetelný ASCII box s anglickou zprávou o výsledku. Box zůstane viditelný tři sekundy a poté se okno automaticky zavře.

## Kontext projektu

- Podporované uživatelské vstupní body jsou `client/tools/package/install.bat` a `client/tools/package/uninstall.bat`, které spouštějí odpovídající PowerShell skripty.
- Instalační logika je v `client/tools/package/install.ps1` a odinstalační logika v `client/tools/package/uninstall.ps1`.
- Instalace se při běžném spuštění bez administrátorských oprávnění znovu spouští zvýšeně pomocí `Start-Process -Verb RunAs`.
- Aktuální úspěšný běh vypíše běžné řádky, ale po spuštění kliknutím se konzole ihned zavře.
- Aktuálně distribuované kopie leží v `dist/client/Windows/x64/` a mají zůstat synchronizované se zdrojovými skripty.

## Výslovné požadavky

1. Po úspěšné instalaci služby a klienta zobrazit v konzoli výrazný ASCII box.
2. Box pro instalaci má obsahovat anglickou zprávu `PHPVPN installed successfully` a krátkou doplňující informaci potvrzující korektní dokončení instalace.
3. Po úspěšné odinstalaci zobrazit obdobný ASCII box.
4. Box pro odinstalaci má obsahovat anglickou zprávu `PHPVPN uninstalled successfully` a krátkou doplňující informaci potvrzující korektní dokončení odinstalace.
5. Každý úspěšný box musí zůstat viditelný po dobu tří sekund. Poté se konzolové okno může automaticky zavřít.
6. Výsledek musí být dobře čitelný při běžném uživatelském spuštění instalačního nebo odinstalačního `.bat` souboru kliknutím.

## Potvrzené chování

- Úspěšný box se zobrazí pouze po skutečně dokončené instalaci nebo odinstalaci.
- Režim `-VerifyOnly` nebude zobrazovat zprávu o instalaci a nebude přidávat třísekundové čekání.
- Neúspěšné větve nesmějí zobrazit úspěšný box.
- Chybové větve zachovají nenulový návratový kód a stávající diagnostiku.
- Angličtina odpovídá současnému jazyku instalačních skriptů.
- Změna zahrnuje zdrojové skripty, aktuální distribuční kopie a automatické testy.

## Dotčené oblasti

- `client/tools/package/install.bat`
- `client/tools/package/install.ps1`
- `client/tools/package/uninstall.bat`
- `client/tools/package/uninstall.ps1`
- `dist/client/Windows/x64/install.bat`
- `dist/client/Windows/x64/install.ps1`
- `dist/client/Windows/x64/uninstall.bat`
- `dist/client/Windows/x64/uninstall.ps1`
- testy instalačního balíčku a kontroly distribučních artefaktů
- případné zdrojové mechanismy, které distribuční balíček generují nebo ověřují

## Akceptační kritéria

1. Úspěšná běžná instalace zobrazí ASCII box s textem `PHPVPN installed successfully` a krátkým potvrzením výsledku.
2. Box zůstane čitelně zobrazený tři sekundy a teprve poté běh skončí.
3. Úspěšná běžná odinstalace zobrazí obdobný box s textem `PHPVPN uninstalled successfully` a krátkým potvrzením výsledku.
4. Odinstalační box zůstane čitelně zobrazený tři sekundy a teprve poté běh skončí.
5. `-VerifyOnly` zachová svůj význam, nevypíše tvrzení o instalaci a nebude uměle čekat tři sekundy.
6. Jakékoli selhání skončí nenulovým návratovým kódem, zachová užitečnou chybovou zprávu a nezobrazí úspěšný box.
7. Výsledek funguje i při zvýšeném spuštění přes UAC, takže uživatel skutečně uvidí potvrzení v konzoli, která prováděla operaci.
8. Zdrojové a distribuované kopie skriptů jsou obsahově synchronizované podle pravidel balíčku.
9. Automatické testy ověřují text obou boxů, jejich zobrazení pouze na úspěšné cestě, absenci instalačního boxu v režimu ověření a zachování chybových návratových kódů.

## Realizační vodítka

- Sdílet formátovací logiku nebo alespoň jednotný vizuální styl, aby instalační a odinstalační box nepůsobily odlišně.
- ASCII rámeček musí používat znaky spolehlivě zobrazitelné ve standardní konzoli Windows.
- Čekání umístit až za poslední úspěšný krok. Nesmí zakrývat nedokončenou operaci ani měnit návratový kód.
- Zachovat bezpečnostní pořadí kontrol, zvýšení oprávnění, instalace, odinstalace a práce s konfigurací. Tento intent mění pouze uživatelskou zpětnou vazbu po výsledku.
- Testy nemají být plošně zpomalené o tři sekundy. Realizace může nabídnout interní testovací cestu nebo testovat pomocnou prezentační logiku odděleně, pokud tím nezmění běžné uživatelské chování.
