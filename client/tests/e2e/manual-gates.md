# Zbývající kontroly vyžadující interaktivní zásah

Tyto kontroly nejsou nahrazeny jednotkovými testy ani úspěšným izolovaným SCM/Wintun testem. Hlavní provedené důkazy jsou v implementation-plan3-report.md a matici.

RSS při maximální obsazenosti IPC, skutečně zadržené DNS a HTTP 503 při contention byly následně automaticky provedeny a prošly. Podrobnosti, nalezenou a opravenou serverovou chybu i meze důkazu uvádí [doplněný report](../implementation-plan3-resource-report.md). Tyto tři scénáře už nevyžadují zásah uživatele.

## Skutečná škálování 125 % a 200 %

Připravený testovací UI target se spouští `client/tools/run-tray-ui-tests.ps1 -Interactive`. Používá produkční okna a privátní testovací backend, nemění službu ani konfiguraci. Nástroj computer-use v této relaci nefungoval. Screen-master může po změně nastavení zachytit skutečný výsledek.

Potřebný zásah uživatele: dočasně změnit škálování jednoho monitoru v nastavení Windows na 125 %, poté 200 % a nakonec obnovit původní hodnotu. V každém nastavení projít URL, secret, identitu a Contracts, zmenšit okno a ověřit všechna čtyři tlačítka, posouvání, Tab/Shift+Tab/Enter/Escape a přechod mezi monitory. Změna nastavení celé uživatelovy obrazovky nebyla automaticky provedena.

## Sleep/resume a dvě přihlášené relace

Izolované E2E používá skutečnou službu `phpvpn-plan3-e2e-d6c8`, privátní pipe a vlastní adapter GUID. Jeho dosavadní automatický controller není test sleep/resume ani druhého uživatele.

Pro sleep/resume je potřeba interaktivní testovací okno před uspáním a alespoň 60 sekund bez platného heartbeat. Po probuzení má expirovaná relace zůstat uzavřená, bez automatického opakování Next nebo Finish. Aktivní VPN má provést novou sériovou časovou synchronizaci. Před uspáním je nutné uložit ostatní rozpracovanou práci. Uspání přeruší spojení této pracovní relace s agentkou, proto vyžaduje zvolený okamžik a ruční probuzení uživatelem.

Pro dvě relace je potřeba uživatelův druhý existující Windows účet a souběžné přihlášení. Každá relace má mít vlastní Wizard session. Zavření/expirace jedné nesmí ukončit druhou, globální Connect/Disconnect musí změnit epochu obou. Přihlášení a zadávání hesel provádí uživatel. Nový účet ani heslo nebyly kvůli testu vytvořeny.

Konkrétní průchod: v první relaci otevřít testovací Wizard, přepnout uživatele bez odhlášení, v druhé relaci otevřít druhý Wizard a ověřit oddělené session. Pak zavřít jednu relaci, ověřit druhou a vyvolat globální Connect/Disconnect pouze izolované testovací služby. Samostatně uložit práci ve druhé relaci a odhlásit ji. První relace má zůstat použitelná, opuštěná Wizard session má po lease expirovat. Odhlášení zavře aplikace daného uživatele. Přepnutí do jiné relace může dočasně přerušit ovládání desktopu agentkou.

Tyto dvě kontroly potřebují před spuštěním doplnit controller s explicitním režimem čekajícím na ruční krok. Dosavadní automatický runner se po svém testu uklidí a nesmí být vydáván za připravený sleep/multiuser test.

## Čistá instalace, upgrade a odinstalace balíčku

Balíček má ověřený manifest, soubory, x64 architekturu a Wintun API přes `install.ps1 -VerifyOnly`. Skutečný izolovaný SCM test používá samostatné jméno služby. Produkční instalační skript pracuje s pevným jménem `phpvpn` a produkčním umístěním.

Na tomto hostu již běží skutečná služba phpvpn. Spuštění jejího install/upgrade/uninstall by zasáhlo existující instalaci, nebylo provedeno. Potřebné rozhodnutí: vyhradit jiný Windows stroj, nebo výslovně povolit kontrolované přerušení a změnu nynější instalace se zálohou a návratem. Dostupnost VM není předpokladem izolovaného SCM/Wintun testu, je pouze jedna z možností pro čistý instalační scénář.

## Skutečný výpadek napájení

Pád vlastního procesu už prošel, skutečný výpadek napájení nikoli. Potřebný zásah je vybrat postradatelný testovací Windows stroj se zálohou nebo izolovaný virtuální disk s řízeným tvrdým vypnutím. Po přípravě kontrolních bodů během zápisu/flush/publikace configu a během aktivního tunelu uživatel vyvolá tvrdé vypnutí a následný start. Pak se ověří validní původní nebo nový dokument, journal recovery, vlastnictví adaptéru/rout a zákaz automatického Connectu. Restart Windows ani TerminateProcess tento test nenahrazuje. Tvrdé vypnutí může ztratit ostatní neuloženou práci a poškodit souborový systém. Tvrdé vypnutí VM je důkaz jen pro virtuální disk a příslušné cache, nikoli fyzický výpadek napájení hosta. Na nynější pracovní stanici nebyl tento zásah proveden.

## Přehled rozhodnutí pro interaktivní pokračování

| Brána | Krok uživatele | Dopad |
|---|---|---|
| DPI 125/200 % | Dočasně změnit měřítko jednoho monitoru, projít testovací okna a obnovit původní hodnotu. | Přeskupení oken a změna velikosti celé plochy. Bez změny služby. |
| Dva uživatelé a logoff | Zvolit druhý účet, sám zadat přihlašovací údaje, přepínat relace a jednu po uložení práce odhlásit. | Odhlášení zavře aplikace této relace, ovládání desktopu může být přerušeno. |
| Sleep/resume | Uložit práci, zvolit okamžik uspání, ponechat stroj spát alespoň 60 s a ručně jej probudit. | Dočasně se přeruší síť i spojení s agentkou. |
| Install/upgrade/uninstall | Zvolit samostatný stroj, nebo výslovně povolit zálohu, přerušení a změnu existující instalace. | Produkční instalátor používá pevné jméno phpvpn. Na nynějším hostu by zasáhl používanou službu. |
| Power loss | Zvolit postradatelný stroj nebo izolovaný virtuální disk a řízené tvrdé vypnutí. | Riziko ztráty neuložených dat a poškození souborového systému, poté nutný ruční start. |

Pro sleep/multiuser/power-loss je před samotným ručním krokem potřeba doplnit a lokálně ověřit režim controlleru, který uloží důkazy a čeká v konkrétním kontrolním bodě. Existující automatický SCM runner takový režim nemá. Žádné odhlášení, uspání, odinstalace ani vypnutí existující služby nebylo v navazujícím bloku zahájeno.
