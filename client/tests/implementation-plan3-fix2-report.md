# Plan3 fix 2: implementace a ověření

Stav 6. září 2026, přímý pracovní strom `C:/git/phpvpn`, výchozí HEAD `9b4bd48`. Autoritou je celé [formální zadání](../../notes/2026-09-06-22-21-54_notes.processed.md) a [plan3_fix2.md](../../plan3_fix2.md). Implementace není commitnutá ani pushnutá. Původní poznámky a odstranění `.note-taker-active` zůstaly zachované. Nebyl použit další agent, worktree ani nový task.

## Výsledek

Implementované jsou všechny požadované změny produkčního kódu. Debug i Release se sestavují včetně izolovaných SCM a UI testovacích cílů. Automatické kontroly uvedené níže prošly. To není tvrzení o úplném provozním ověření: chráněné NTFS testy vyžadující administrátorský token, skutečné SCM/instalační scénáře a ruční kontrola všech DPI zůstávají NOT RUN. Přesnou vazbu všech 17 kritérií uvádí samostatná [akceptační matice](implementation-plan3-fix2-matrix.md).

Nová ověřená vývojová distribuce je `C:/git/phpvpn/dist`. Nainstalovaná služba, její procesy, produkční ProgramData ani trust store nebyly změněny.

## Změny a rozhodnutí

| Oblast | Skutečná implementace |
|---|---|
| Startup | `ServiceRuntime::start()` po přípravě chráněného backendu a `recover()` volá jednou `ensureDefaultAtStartup()`. Pouze skutečné missing používá společné `createDefault()` s createIfMissing. Existující obsah, včetně vady či absence metadat, se nemigruje. |
| Degradace | `ConfigStore` odděluje chybu inicializace od dostupnosti obsahu. Recovery unknown blokuje startup publikaci, Save i Connect. Nedostupný backend má pevnou cestu a konkrétní chybu, nikdy falešné missing. Host dokončí řídicí IPC i při konfigurační nebo engine inicializační chybě. Nezávislá chyba pipe, systémové cesty nebo kryptografické infrastruktury stále může start odmítnout. |
| Lifecycle | Engine bez dokončené bezpečné inicializace nevytváří pracovní vlákno ani tunel. Connect vrací původní chybu, status/diagnostika odpovídají, Disconnect a shutdown nečekají na neexistující worker. Neobchází se ownership recovery. Logy obsahují fázi a bezpečný kód, nikoli dokument či secret. |
| Šablona | Úplný editorový default zůstává jedinou konstantou v `config.h`, pouze se třemi true navíc. Používají jej startup, create-default-config a get-default-config/Reset editoru. Reset Wizardu zůstává samostatným konceptem s placeholder URL, bez identity a starých kontraktů, s týmiž true volbami. |
| Metadata | Dokumentový `WizardPreferences` uchovává optional bool, analýza vyhodnotí každý fallback samostatně. `ClientConfig` nemá logiku voleb Wizardu, běžný runtime používá uloženou konkrétní identitu. Všechny neboolean typy, duplicity a neznámé členy zůstávají odmítané. |
| IPC a důkazy | Fields mají přesně čtyři stringy a tři povinné booleany. View přenáší nejvýše 14 seřazených kandidátních peerů bez klíčů. Všech sedm fields je součástí access evidence i fingerprintu commitu. Služba kontroluje default secret/key i shodu automatické IP s vlastním ověřeným návrhem. |
| Kompatibilita | Přesně rozpoznané historické fields/view vracejí wizard_unsupported, ostatní poškozené tvary ipc_malformed. Nová služba přijme starý config bez migrace, staré binárky nejsou protějškem nového schématu. Služba a tray se aktualizují společně, žádný downgrade ani přímý zápis tray. |
| Wizard | Checkboxy používají přímo fields. Automatická IP přepíše i placeholder, bez návrhu lze automatiku vypnout. Zapnutí použije pouze platný cacheovaný návrh, ne HTTP retry. Změna URL/secretu, Connect, resume, Reset a expirace zneplatňují příslušné důkazy. Back/Next zachová rozpracované volby. |
| Kontrakty | Sdílené helpers počítají přesný rozdíl kandidátů a původních peerů. Finish přidává pouze chybějící položky, existující klíče nepřepočítává ani při změně vlastní IP. False zachová obsah i omission v edit/repair. Původní session DOM zůstává základem, pozdější cizí Save se neslučuje. |
| UI | Volba doplnění je vždy viditelná a při novém otevření true. Nepodporovaný CIDR znamená checked/disabled a efektivní false. Zobrazuje existující, chybějící a skutečně plánované nové položky. Metadata kontraktů se neukládají. |
| Dokumentace | Aktualizované `client.md`, `plan3_client.md`, návaznost `plan3_fix.md`, instalační texty a bezpečné rady. `protocol.md` a relay wire protokol beze změny. Historické reporty nebyly přepsány na nové výsledky. |

Technická forma startup výsledku je `Result<StartupConfigState>`: úspěšné existing/created/alreadyExists a konkrétní chyba v neúspěšné větvi. Nahrazuje pracovní název StartupConfigOutcome v plánu bez změny jeho významu. Chyba samotné publikace se automaticky neopakuje, po nápravě zůstává možná nová explicitní akce. Nevyřešená bezpečná inicializace naopak publikaci i runtime blokuje do řízené obnovy.

## Prostředí a skutečné běhy

Windows AMD64, OS verze `10.0.22631.6199`, DisplayVersion `23H2`. MSVC 19.44.35228, toolset 14.44.35207, CMake/Ninja a C++20. Administrátorská role aktuálního tokenu: false. Nepokoušeli jsme se o elevaci. Skutečná Win32 okna testovacího procesu běžela při 96 DPI, tedy 100 %. GUI používá produkční okna a readery, soukromou pipe a deterministickou náhradní obsluhu, ne instalovanou službu.

| Běh | Výsledek a omezení | Evidence |
|---|---|---|
| Debug build | PASS, produkční binárky, core testy, SCM E2E cíle a UI harness | `client/tools/build-x64.ps1 -Preset debug -Configure -ServiceE2E -UiTests` |
| Release build | PASS, stejné cíle | `client/tools/build-x64.ps1 -Preset release -Configure -ServiceE2E -UiTests` |
| Debug CTest | 235 registrovaných případů, 0 selhání. Skutečně provedeno 220, 15 chráněných NTFS případů NOT RUN | [log](../../work/fix2-debug-ctest.log) |
| Release CTest | 235 registrovaných případů, 0 selhání. Skutečně provedeno 220, 15 chráněných NTFS případů NOT RUN | [log](../../work/fix2-release-ctest.log) |
| Cílený filtr fix2 | 13 registrovaných případů, 12 skutečně provedených bez chyby, jeden chráněný startup NTFS případ NOT RUN. Finální rozšíření těchto testů znovu zahrnuje CTest | [cílený log](../../work/fix2-targeted-tests.log), finální CTest logy |
| Debug skutečná Win32 UI | 9 testů, 0 selhání, produkční HWND, private IPC, 96 DPI | [log](../../work/fix2-debug-ui.log) |
| Release skutečná Win32 UI | 9 testů, 0 selhání, stejná izolovaná infrastruktura | [log](../../work/fix2-release-ui.log) |
| Layout výpočty | PASS v rámci CTest, 96/120/144/192 DPI, malé plochy, footer a oddělení oblastí | `WizardLayout.FooterRemainsAccessibleAcrossDpiAndSmallWorkAreas` |
| Produkční NTFS boundary | Běžné neprivilegované kontroly a odmítnutí nezabezpečeného adresáře PASS. Chráněná publikace, závody a fault/recovery s administrátorským tokenem NOT RUN | CTest logy s explicitním NOT RUN |
| Skutečné SCM/Wintun E2E | Cíle sestavené, běh NOT RUN. Žádná testovací ani produkční služba nebyla registrována | `tests/e2e/service_e2e.cpp`, `phpvpn-service-e2e.exe` |
| Distribuce | PASS integrita, přesný strom, čerstvost PE sekcí, SHA-256, pinovaný RSA manifest, originální Wintun, API a architektura. Vývojový podpis s nedůvěryhodným kořenem | [ověření](../../work/fix2-distribution-verify.log) |

Test harness vypisuje u přeskočeného NTFS případu po NOT RUN také obecné OK a CTest hlásí jeden úspěšný testovací executable. Ani jedno zde není vydáváno za provedení všech 235 případů. Běhy Debug a Release testují stejné případy, jejich počty se nesčítají jako unikátní pokrytí.

Nové cílené testy pokrývají všechny bool kombinace, jednotlivé absence a explicitní false v repair, typy a duplicity, přesnou šablonu, jednu klasifikaci existujícího souboru, BOM/encoding/velikost/repair/unreadable, závod createIfMissing, unknown a chybu recovery, žádný retry, aditivní DOM a původní klíče, vlastní IP mimo kandidáty, změnu vlastní IP, chybějící objekt, opakování, typy/limity/pořadí IPC peerů, historická schémata, model automatiky a efektivní checkbox.

Službové fixture provádějí skutečné parsování IPC, produkční WizardService, kryptografii a ConfigStore s řízeným souborovým a HTTP backendem. Zahrnují edit/repair Finish, odmítnutí falešného defaultu a neověřené auto IP před HTTP, změnu každého příznaku při stejném commitId, všech osm kombinací přes ověřený Finish a nové otevření a dostupnost status/diagnostiky/Disconnect při blokovaném Connectu. Nejde o skutečný start přes SCM ani důkaz atomického NTFS rename.

UI doplnění ověřuje edit s 0/1/13 kontrakty, checked stav a počty, default secret, nahrazení `.2` návrhem `.10`, přepočet klíče, ruční přechod na `.11`, odeslané metadata, no-suggestion a nepodporovaný CIDR. Stávající testy navíc ověřují tabulátor, Shift+Tab, Enter, Escape, resize, opožděné odpovědi, resume, reset editoru a nezávislý Disconnect.

SCM controller byl aktualizován pro default bez tray, druhý start bez změny, starý i invalidní soubor beze změny, smazání za běhu a explicitní create, smazání a startup recreate a první edit s jedním původním kontraktem. Tyto přidané scénáře jsou sestavené, nikoli spuštěné. Další instalace/reinstalace, chráněný unreadable/recovery a ruční Edit Config Yes/No/Cancel proti skutečné službě zůstávají neověřenými integračními scénáři.

První vývojové běhy odhalily stará očekávání testů, chybu dočasné reference v testu a typovou chybu nového fake testu. Byly opraveny, uvedené finální běhy nemají selhání. Jedna ruční konfigurace CMake mimo MSVC prostředí selhala, následné sestavení přes projektový build-x64 s příslušnými přepínači obnovilo správné prostředí a všechny cíle sestavilo.

## Reprodukovatelné příkazy

Z `C:/git/phpvpn`:

```powershell
& .\client\tools\build-x64.ps1 -Preset debug -Configure -ServiceE2E -UiTests
ctest --test-dir work/build/debug --output-on-failure -V
& .\work\build\debug\tests\phpvpn-client-tests.exe fix2
& .\work\build\debug\tests\phpvpn-tray-ui-tests.exe
& .\client\tools\build-x64.ps1 -Preset release -Configure -ServiceE2E -UiTests
ctest --test-dir work/build/release --output-on-failure -V
& .\work\build\release\tests\phpvpn-tray-ui-tests.exe
```

Tyto příkazy nespouštějí SCM E2E controller. Ten vyžaduje samostatný výslovný běh administrátorského runneru v izolovaném prostředí. Ruční DPI 125/150/200 %, úplný vizuální audit/fokus všech režimů, skutečná instalace, reinstall, relay kontraktové testy a široká síťová zátěž nebyly součástí provedeného ověření.

## Distribuce podle distribution.md

Strom byl znovu vytvořen v novém pracovním adresáři, ověřen a teprve potom přesunut na `C:/git/phpvpn/dist`. Staré soubory se nepřekrývaly. Původní balíček je obnovitelný v `C:/git/phpvpn/work/dist-before-plan3-fix2-20260906`. Průběžný nový balíček před závěrečným upřesněním startup logů zůstává mimo dist v `work/dist-plan3-fix2-interim-20260906`.

Použitý příkaz pro finální Windows balíček:

```powershell
& .\client\tools\make-dist.ps1 -BuildDirectory C:\git\phpvpn\work\build\release -OutputDirectory C:\git\phpvpn\work\dist-plan3-fix2-new\client\Windows\x64 -SignBinaries
```

Server byl zkopírován přímo z aktuálního `phpvpn.php` jako `dist/server/phpvpn.php`. Starý název `php-vpn.php` v původní distribuci nebyl přenesen. Linux/macOS mají výslovné README o nepodporované platformě. Finální strom má přesně 17 souborů, z toho 14 v samostatném Windows x64 balíčku. Neobsahuje testy, cache, provozní config, soukromé klíče ani pracovní logy.

```powershell
& .\work\verify-fix2-distribution.ps1 -Root C:\git\phpvpn\dist
powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\git\phpvpn\dist\client\Windows\x64\install.ps1 -VerifyOnly
```

Ověřovací skript navíc porovnává SHA-256 serveru, instalačních zdrojů a Wintunu, velikosti manifestu, přesný seznam souborů, reparse body a všechny raw PE sekce každé binárky s aktuálním Release buildem. Podpis mění checksum a certifikátový overlay, proto se tento rozdíl nezaměňuje za zastaralý kód. `dumpbin /dependents` potvrdil systémové Windows závislosti, žádný chybějící VC runtime balíček. Nepřejmenovaný Wintun 0.14.1 má platný podpis WireGuard a povinné API, VerifyOnly nevytváří adaptér ani neinstaluje službu.

Release zůstává `1.0.0`, IPC verze `1`, architektura x64. Manifest i EXE používají existující `CN=phpvpn Development Test Signing`, thumbprint `7430CCA24F5DE8A6614D582AFBFED7C80E0F2CA8`. Pinovaný RSA podpis manifestu a hashe prošly. Authenticode EXE vrací UnknownError s konkrétní příčinou nedůvěryhodného kořenového certifikátu. Není to veřejně důvěryhodný vydavatelský podpis. Trust store nebyl upraven. První rozšířená kontrola vyžadující obecné Valid proto neprošla, následná kontrola výslovně oddělila tento očekávaný vývojový trust limit od integrity ověřené pinovaným manifestem.

Finální SHA-256 manifestu: `106CD728EC630DF4DC0329335163BE4DCDBF668B229CE7D9086D41F1693EF800`.

Finální SHA-256 služby: `F67654A2A54C17F7D6FC95F80A261F568D3B04C12B93D8A7294215FCE03295F6`.

SHA-256 serveru: `B859809F9A29094C8587D2F24C884023B7CA6DC40AEF65437015F30F74044BD9`.

## Hranice předání

Implementace a vývojová distribuce jsou připravené k předání. Zbývající provozní ověření vyžaduje izolované administrátorské prostředí a skutečné SCM/UI scénáře, nelze je nahradit úspěšným unit testem. Neexistuje zde tvrzení, že již nainstalovaná služba používá nové binárky nebo že všech 17 kritérií bylo kompletně ověřeno na živé instalaci.
