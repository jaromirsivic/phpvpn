# Potvrzení výsledku Windows instalace a odinstalace

Realizace intentu `Intents/2026-09-18-17-46-31/intent-v001.md`, 18. září 2026,
větev `tray_UX`, bez commitu.

Oba PowerShell skripty na konci skutečně úspěšné operace vypisují jednotný
62znakový ASCII box s požadovaným anglickým textem a čekají tři sekundy.
`-VerifyOnly` končí před instalací, boxem i čekáním. BAT vstupy zachovávají
předávání argumentů a návratového kódu, jejich obsah nebylo nutné měnit.

Po UAC rodič čeká na konkrétní zvýšený PowerShell proces pomocí `WaitForExit()`
a vrací jeho exit code. Nepoužívá `Start-Process -Wait`, které by čekalo také
na dlouho běžící tray aplikaci. Box vypisuje pouze proces provádějící operaci.
Zachovává se `RemoveData` a do zvýšeného instalátoru se předává podpisový pin.

Nové kontroly odmítají neúspěšný disconnect, odregistraci služby, `sc.exe`
a spuštění tray aplikace. Selhání zastavení služby v náhradní odinstalační
cestě ani ponechaný instalační reparse point již nevedou k úspěšnému výsledku.
Pořadí kroků a pravidla zachování konfigurace na úspěšných cestách zůstávají
beze změny.

## Ověření

- `test_package_feedback.ps1`: PASS, 33 scénářů. Kontroluje obsah a zarovnání
  boxů, jediné čekání tří sekund až za boxem, chybové cesty, VerifyOnly,
  předávání parametrů a návratových kódů přes UAC i oba skutečné BAT soubory,
  chybějící PowerShell helper a zachování či výslovné odstranění konfigurace.
  Test izoluje systémové operace a nahrazuje spánek, neinstaluje skutečnou VPN.
- `test_package_upgrade.ps1`: PASS, stávající bezpečné pořadí upgradu a
  zachování rozpracované práce v tray aplikaci.
- Oba testy jsou registrované v CTest pod labelem `package`.
- Release build: PASS. Celá dostupná CTest sada: 4/5 PASS. Prošly klientské
  unit testy, logger a oba balíčkové testy. `phpvpn-contract-tests` selhal
  ve všech pěti serverových scénářích na `transport_connect_failed`, protože
  skutečný testovací PHP relay nebyl dostupný.
- `verify-package.ps1` proti výslednému `dist/client/Windows/x64`: PASS.
  Ověřuje shodu zdrojových skriptů, soupis souborů, délky, SHA-256 a spouští
  skutečný `install.bat -VerifyOnly`. Ten ověřil pinovaný RSA manifest,
  originální Wintun, architekturu a driver API bez instalace a bez boxu.
- Čtyři izolované poškozené kopie balíčku: nesprávný hash, vadný podpis,
  chybějící EXE a nadbytečný EXE byly odmítnuty s diagnostikou a nenulovým
  návratovým kódem BAT, bez úspěšného boxu.
- Stávající `work/verify-fix2-distribution.ps1`: PASS pro celý nový strom,
  17 souborů, aktuální server, zdrojové skripty, x64 a shodu všech PE sekcí
  s aktuálním Release buildem.
- Skutečná instalace/odinstalace přes dvojklik a interaktivní UAC nebyly
  provedeny. UAC řízení a systémové chyby jsou pokryty izolovanými testy.

## Distribuce

Celý `dist` byl znovu vytvořen přes ověřený staging podle `distribution.md`.
Windows balíček 1.0.0 vytvořil `make-dist.ps1 -SignBinaries` z aktuálního
Release buildu. Manifest i podpis odpovídají novým skriptům. Použit je
existující vývojový certifikát `7430CCA24F5DE8A6614D582AFBFED7C80E0F2CA8`,
nikoli veřejně důvěryhodný vydavatelský certifikát. Podpis originálního
Wintunu je platný, podpisy EXE mají nedůvěryhodný vývojový kořen.

Předchozí distribuce zůstává v `work/feedback-dist-before-20260918`.
Negativní testovací kopie a jejich reprodukční skript jsou pod `work/feedback-negative*`.
Adresář `dist` je v tomto repozitáři ignorovaný Gitem, aktualizované distribuční
kopie proto nejsou součástí textového git diffu.
