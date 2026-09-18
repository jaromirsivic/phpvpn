---
plan_version: 2
created_at: 2026-09-13T23:43:05+02:00
based_on: plan-v001.md
source: ../../Intents/2026-09-13-22-49-34/intent-v007.md
---

# Realizační plán opravy pollingu a klientského UI

## 1. Závazné zadání a hranice plánu

Autoritativním zadáním je úplný [intent-v007.md](../../Intents/2026-09-13-22-49-34/intent-v007.md). Tento dokument je úplná neměnná plánovací verze podle intentu v007. Pro realizaci nahrazuje plan-v001.md, který zůstává zachovaný jako předchozí verze podle v006. Jeho vytvoření neznamená realizaci, úspěšné testování ani hotovou distribuci. Změny kódu, sestavení, instalace a vytvoření distribuce se provedou až v následně autorizované realizaci. Commit ani push nejsou autorizovány.

Pracovat přímo v `C:\git\phpvpn` v aktivní feature branch. Při průzkumu byla aktivní větev `tray_UX`, HEAD `d6c9c8a3ed1eee2698c0b389e03e9b5bf58af014`. Existovaly změny `.intent-builder-active` a nesledovaný adresář zdrojového intentu. Zachovat je, stejně jako případné další uživatelské změny. Nepoužívat worktree, nevytvářet další agenty ani tasky bez výslovného zadání. Plánovací kontext vlastní pouze tento plán, marker a intenty nadále vlastní koordinátor.

Deadline předaného stromu je **2026-09-14 01:19 Europe/Berlin**. Průběžně kontrolovat skutečný čas, automatické vynucení není garantováno. Po dosažení limitu bezpečně zastavit, zachovat výsledek a pokračovací bod. Nová realizační relace musí mít potvrzené časové podmínky od koordinátora, nelze si sama prodloužit tento deadline.

Požadavky intentu jsou závazné. Níže označená **fakta** jsou ověřená čtením repozitáře přes Graft a navazujících přesných úseků. **Rozhodnutí plánu** jsou doporučené konkrétní implementační kroky, nikoli tvrzení o již existující implementaci. Čísla řádků zachycují stav při průzkumu a po změnách se mohou posunout.

## 2. Ověřená dotčená místa

| Oblast | Ověřená místa | Fakt a dopad |
|---|---|---|
| Runtime smyčka | `client/src/core/runtime/vpn_engine.cpp:481–506, 639–705`, deklarace v `.h:115` | `runConnectedIteration()` přičítá počty výsledku do historických čítačů. `runNetworkThread()` na řádcích 694–695 předává do backoffu `packetsReceived_ > 0 || packetsSent_ > 0`. Po prvním paketu zůstává podmínka pravdivá. Jde o statický důkaz mechanismu, regresní reprodukce se teprve provede. |
| Polling a lifecycle | `client/src/core/runtime/network_worker.cpp:33–73, 130–178, 414–481`, `.h:89,129` | Worker má injektovaný `Clock`, aktuální interval a serverové limity. `configure()` volá `reset()`, `performInfo()` i `performExtendedInfo()` upravují meze. Současné `reset()` neobnovuje samotný interval. Pětisekundové rychlé okno chybí. |
| Skutečný provoz | `client/src/core/runtime/network_worker.cpp:295–412` | Transportní handoff je na řádku 360. `DataExchangeOutcome::packetsSent` roste až při serverem potvrzeném přijetí na řádku 398. Odložený paket se vrací do fronty. Diagnostické účetnictví requestu probíhá před handoffem. Tyto významy se nesmějí zaměnit. |
| Spojení a recovery | `client/src/core/runtime/vpn_engine.cpp:338–479, 508–541, 543–741` | Connect provádí bootstrap před tunelem. Teardown resetuje worker. Recovery znovu volá `runConnectSequence(false)`. Health resynchronizace je mezi datovou výměnou a výpočtem zbývajícího čekání. |
| Meze protokolu | `client/src/core/protocol/response_parser.cpp:199–214`, `response_parser.h:43–50` | Výchozí pollingové hodnoty jsou 50/3200 ms. Parser načte pollingová čísla, ale zde nekontroluje jejich pozitivitu a pořadí. Dokumentace `client.md:212–223` popisuje lokální rozsah 20–5000 ms a známou chybu při neslučitelných mezích. |
| Uložení contracts | `client/apps/tray/contracts_window.cpp:289–319,391,407–413`, `.h` | `notifyAfterSave_` se zachytí již při začátku ukládání. `ContractsEvent::saved` nyní otevře informační dialog. `matched` je jiný výsledek. Modalita může změnit lifecycle, proto již existuje porovnání snapshotu. |
| Standardní Disconnect | `client/apps/tray/tray_application.cpp:193–202,214–254,380–387,407–418`, `.h` | Stav contracts dnes dostává širší `connectedOrConnecting()`. `startCommand(IpcCommand::disconnect)` používá vlastní command reader a skutečné progress okno. Toto je cesta, kterou má nová akce znovu použít. |
| Potvrzení | `client/apps/tray/system_dialog.cpp:17–34,45–51`, `.h:15` | `confirmSystemAction()` podporuje Yes/No přes TaskDialog, ale výchozí tlačítko je `IDNO`. Neúspěšný dialog nevrací souhlas. |
| Server Information | `client/apps/tray/server_info_window.cpp:26–95,96–198,199–256,296–352,396–422,463–477`, `.h` | `open()` znovu aktivuje existující okno. `request()` chrání `pending_`, `finish()` ověřuje generaci, zachovává stará data při chybě a raw výběr/posun při změně. Nadpis používá vždy plurál, OK má neutral roli. |
| Layout a paleta | `client/apps/tray/server_info_layout.h:7–35`, `ui_style.h:29–40` | Patička má Refresh leading a OK trailing. Validní řádek používá `successBackground=#F0FAF2`, invalidní `warningBackground`. Role `contractsChanged=#FFF2B3`, `success=#087C2B`, primary modrá již existují. |
| Přepínač | `client/apps/tray/ui_controls.cpp:201–299,612–623`, `.h:67` | `attachToggleSwitch()` používá skutečný BUTTON s `BS_OWNERDRAW`, vlastním checked stavem, nativní aktivací a textovým labelem. Znovu použít toto řešení. |
| Klientský JSON a model | `client/src/core/presentation/server_info_model.cpp:18–78`, `.h`, `client/src/core/protocol/json.cpp:297–443,517–538` | Model zachovává původní plaintext, parser podporuje whitespace. Klientský pretty serializer se pro tuto změnu nepoužije. |
| Existující testy | `client/tests/unit/test_runtime_status.cpp:30–68,154–302,402–422,504–733`, `test_server_info_model.cpp:38–101`, `test_server_info_layout.cpp`, `test_json.cpp:95–106` | Existují fake clock, scripted transport, engine fixture, modelové a geometrické testy. Graft nalezl `nextPollDelayMs` jen v implementaci, deklaraci a runtime volání, nikoli přímý test. |
| Nativní UI testy | `client/tests/ui/tray_ui_tests.cpp:185–321,828–898,1806–1916,1919–1992` | Existuje soukromý IPC backend, testy tabů, refresh/pagination, zavření během požadavku, repaint a moderních přepínačů. Nativní UI běží mimo CTest. |
| Distribuce | `distribution.md`, `client/tools/make-dist.ps1:17–47`, `client/tools/package/install.ps1:29–34,64–208`, `client/CMakePresets.json`, `client/tests/CMakeLists.txt` | Preset release je x64 RelWithDebInfo. Nástroj make-dist vytváří jen plochý Windows balíček a vyžaduje podpisový certifikát. `-OutputDirectory` je nutné zadat výslovně. Installer nabízí `-VerifyOnly`. |

| Serverový info plaintext | `phpvpn.php:2247–2292` | `InfoHandler::buildPlaintext()` serializuje pole jedním `json_encode(..., JSON_UNESCAPED_SLASHES)`. Limit se ověřuje nad výsledným řetězcem, při překročení se odebírá poslední klient přes `array_pop`. |
| Šifrování a klientská kompatibilita | `phpvpn.php:2222–2244`, `client/src/core/protocol/response_parser.cpp:110–170` | Server šifruje přesné bajty plaintextu, vnější obálka je samostatná. Klient ověřuje GCM před parsováním a omezuje base64url ciphertext vůči 262144 bajtům. |
| Serverové JSON testy | `tests.php:736–773`, `tests-info-extensions.php:13–18,54–79`, `tests-integration/phpvpn_client.py:902–914`, `tests-integration/test_info.py:111–116` | PHP testy již dešifrují info. Python helper dnes vrací jen DOM a velikostní test jej opět zkompaktňuje. To nezměří skutečné bytes odsazeného plaintextu. |
| Protokolová dokumentace | `protocol.md:316,332–369` | Bod 332 požaduje kompaktní info plaintext a vyžaduje cílenou aktualizaci. Kompaktní request objekt E na řádku 316 a datové NDJSON jsou odlišné kontrakty a zůstanou beze změny. |

## 3. Pořadí realizace

1. Znovu načíst AGENTS.md, tento plán a zdrojový intent. Ověřit větev, změny a dostupnost nástrojů, build prostředí, certifikátu a izolovaného testovacího prostředí. Nepřepisovat cizí změny.
2. Nejprve doplnit a spustit reprodukční runtime test. Uložit konkrétní FAIL původní implementace s bezpečným záznamem časů a intervalů. Teprve potom opravit polling, jeho lifecycle a hraniční meze.
3. Realizovat potvrzení contracts a propojit je se standardním Disconnectem. Doplnit jednotkové a nativní testy potvrzeného/neurčitého ukládání a lifecycle závodů.
4. Realizovat serverové pretty-formátování info plaintextu a jeho velikostní/kryptografické testy. Klient ponechá přesné přijaté znění. Realizovat barvy, nadpis a společnou logiku refreshu Server Information, potom lokální Auto refresh a jeho testy.
5. Upravit prokazatelně zastaralou dokumentaci. Spustit automatické regresní sady, nativní UI a integrační testy. Doplnit praktické ověření proti serveru a ve Windows.
6. Po úspěšném ověření zdrojů vytvořit kompletní distribuci podle oddílu 8. Ověřit konečné soubory, instalaci a provoz podle metodiky. Změna po sestavení vyžaduje přestavění dotčených binárek, novou kompletaci a podpis.
7. Předat report podle oddílu 10. Při větších změnách obnovit Graft pomocí `graft build`, pouze jako součást realizace. Neprovádět commit/push.

## 4. Polling: technická rozhodnutí

### 4.1 Výsledek aktuální výměny a okamžik aktivity

Zachovat kumulativní diagnostické čítače a jejich dosavadní význam. Rozhodování pollingu oddělit od nich i od `serverHadMore`, počtu HTTP požadavků a počtu záznamů odpovědi.

Rozšířit interní výsledek aktuální datové výměny o explicitní informaci o skutečné aktivitě a její monotónní okamžik. Odchozí aktivitu zaznamenat při předání neprázdného serializovaného requestu transportu, nikoli při pouhém zařazení do fronty, vytvoření plánu nebo kryptografické přípravě. Serverem odložený, ale skutečně odeslaný paket je stále odchozí provoz. `packetsSent` v dnešním outcome nadále zachová význam potvrzeného přijetí serverem. Příchozí aktivitu zaznamenat po ověření a přijetí alespoň jednoho platného VPN paketu, nikoli při přijetí neověřených bajtů, odmítnuté dávky nebo duplicitního/neschváleného obsahu. Čas je lokální čas pozorování klientem, nepřebírá se serverový timestamp.

Pokud současné transportní API neumí rozlišit chybu před skutečným handoffem, nevymýšlet síťový úspěch z pokusu. Chybová výměna jde stávající cestou recovery a nesmí být úspěšným prázdným pollem. Testy musí odlišit zrušení před transportem od úspěšné výměny s odloženými pakety. Pro úspěšné výměny explicitně předat výsledek z `runConnectedIteration()` do `runNetworkThread()`, například malou strukturou s aktivitou, časem dokončení a plánovaným intervalem. Přesný název struktury je implementační detail.

Čas výměny zachytit bezprostředně při relevantní události a rozhodnutí o prázdném pollu provést podle času dokončení **datové** výměny. Následný pomalý health nesmí zpětně změnit poll dokončený před hranicí 5000 ms na poll po této hranici. Oddělit rozhodnutí o intervalu od odečtení času stráveného výměnou a health kontrolou.

### 4.2 Stavový automat a čas

Pollingový stav vlastní `NetworkWorker` na síťovém vlákně. Použít existující `Clock::monotonicMs()`, nikoli UTC, systémové hodiny, počítání iterací či skutečný sleep v deterministických testech. Stav obsahuje aktuální interval a volitelný okamžik poslední aktivity nebo začátku rychlého okna. Hodnota 0 je platný monotónní čas, neslouží jako sentinel.

| Událost | Změna stavu |
|---|---|
| Konfigurace/reset | Zrušit předchozí časové okno, obnovit interval i serverové limity konzistentně. |
| Úspěšný přechod do datového provozu po bootstrapu, včetně recovery | Explicitně nastavit začátek okna na aktuální monotónní čas a interval na platné minimum. Pomalý bootstrap nespotřebuje první rychlé okno. |
| Aktuální odeslaný nebo přijatý VPN paket | Obnovit čas poslední aktivity a nastavit minimum. Poslední z událostí TX/RX určuje konec okna. |
| Úspěšný prázdný poll s uplynulým časem `< 5000 ms` | Ponechat minimum, čas aktivity nezměnit. |
| Úspěšný prázdný poll s uplynulým časem `>= 5000 ms` | Zdvojnásobit předchozí interval se saturací na maximum. |
| Chyba/cancel datové výměny | Neprovádět idle krok. Zachovat stávající zpracování chyb, teardown a recovery. |
| Health, info diagnostika, keepalive, časová synchronizace bez VPN paketu | Neobnovit rychlé okno. |
| Nové platné serverové meze v rámci stejné datové session | Zachovat čas poslední aktivity, přepočítat meze a omezit interval. Během rychlého okna použít nové minimum. Samotná změna mezí není provoz. |
| Odpojení, shutdown, teardown před recovery | Zahodit časový stav, žádný starý čas se nepřenese do nové session. |

Pro 50/3200 ms má výsledek po hranici pořadí `100, 200, 400, 800, 1600, 3200, 3200…`. Nový paket v libovolném bodě vrátí 50 ms a nové celé okno. Použít saturující násobení, například rozhodnutí proti `high / 2` před násobením, aby ani extrémní vstup nezpůsobil přetečení. Stejně ošetřit aritmetiku času. Zpětný pohyb fake monotónního času nesmí vyvolat přetečení nebo falešný idle krok.

### 4.3 Serverové meze a dosavadní čekání

Rozhodnutí plánu: ověřit `min > 0`, `max >= min` a slučitelnost s podporovaným lokálním rozsahem 20–5000 ms. Pro přijaté limity používat průnik se současným lokálním rozsahem. Prázdný průnik odmítnout jako nepoužitelné ověřené limity, nevracet hodnotu mimo serverové minimum/maximum. Validaci sdílet mezi standardním a rozšířeným info. Pro limity uvnitř podporovaného rozsahu dosáhne backoff přesně serverového maxima. Pokud server dovoluje vyšší maximum než lokálních 5000 ms, lokální limit je přísnější a zůstane zachován. Tuto kompatibilitu výslovně popsat v dokumentaci a reportu. Neprovádět dříve plánovaný backpressure ani ostatní části cizího plánu B03/B05.

Zachovat model jednoho běžícího requestu a současné odečítání času výměny od start-to-start intervalu. Interval je plánovací hodnota, nikoli záruka skutečného síťového odstupu při pomalém HTTP. Zachovat přerušitelné čekání přes `takeCommand()`. Příchozí paket je znám až z odpovědi pollu, jeho okamžité snížení intervalu znamená následující plánovací rozhodnutí. Samotný paket čekající v odchozí frontě není důkaz odeslání a nevyžaduje novou architekturu probouzení tunelového pracovníka.

## 5. Communication Contracts: potvrzení a Disconnect

1. Akci vyvolat pouze pro potvrzené `ContractsEvent::saved`, nikoli `matched`, unknown, rejected, not-sent nebo timeout. Zachovat stávající bezpečnost publikace konfigurace, Check a opakování ukládání.
2. Vyhodnocovat připojení při zpracování potvrzeného uložení. Nepoužívat historický `notifyAfterSave_` nastavený před Save ani široké `connectedOrConnecting()` jako synonymum connected. Doplnit přesnou informaci ze stavového toku aplikace a při neaktuálním či neznámém statusu provést korelované paměťové `get-status` před rozhodnutím. Unknown není důkaz připojení a nevyvolá automatické odpojení. Stavový dotaz nesmí spustit nový Connect ani číst konfiguraci.
3. Pro connected zobrazit text: `Your changes have been saved. It is strictly required that you disconnect and connect again to apply them. Do you want to disconnect now?` Tlačítka `Yes` a `No`, default `Yes`. Enter potvrzuje výchozí Yes. Escape/zavření/selhání vytvoření dialogu nejsou souhlas.
4. Rozšířit `confirmSystemAction()` o explicitní default parametr se zachovaným výchozím No pro dnešní volající, případně přidat úzce pojmenovanou variantu. Novou hodnotu použije pouze tento dialog. Otestovat skutečné výchozí tlačítko TaskDialogu, nikoli jen předaný enum.
5. ContractsWindow předá potvrzenou akci callbackem do `TrayApplication::startCommand(IpcCommand::disconnect)`. Neobcházet standardní command reader, progress, chybové chování a dokončení teardown. Neotevírat samostatnou síťovou/SCM cestu a po Yes automaticky znovu nepřipojovat.
6. Udržet ochranu window/draft generace před i po modálním dialogu. Ukončení relace, zničení okna či nová generace během dialogu nesmí odpojit nově vzniklou relaci nebo zavřít nově otevřený editor. Doplnit korelaci na identitu spojení, pokud se během potvrzení může změnit. Již odpojený klient nepotřebuje další akci. Opakované Yes nesmí zdvojit Disconnect.
7. No uzavře potvrzení a ponechá VPN připojenou. Uložený dokument zůstává uložený, aktivní snapshot se nemění a nové contracts se uplatní až při budoucím Disconnect/Connect. Samotný editor po úspěšném Save zachová dnešní ukončovací tok. V nepřipojeném stavu nevytvářet zbytečnou výzvu.

## 6. Server Information: prezentace a Auto refresh

### 6.1 Barvy, OK a nadpis

Přidat samostatnou roli pro validní řádky Server Information s navrženou hodnotou **#DDF2E2**. Je to rozhodnutí plánu, nikoli již zavedená projektová barva. Globální `successBackground` ponechat beze změny. Invalidní žlutý řádek napojit přímo na `contractsChanged=#FFF2B3`. Offline roli zachovat. Zkontrolovat text, focus a vybraný řádek včetně high contrast podle současné správy systémové palety.

OK přepnout na `ButtonRole::primary`, zachovat IDOK, Escape, Enter, focus a accessible name. Refresh zůstane primární. Nezměnit význam ostatních akcí.

Vytvořit jediný zdroj gramaticky správného textu pro 0, 1 a N. Přesně jedna hodnota používá `valid online client`, všechny ostatní `valid online clients`. Zachovat výpočet počtu validních ostatních klientů v modelu. Systémový titulek obsahuje kompletní prostý text. Obsahový nadpis rozdělit do dvou měřených statických částí nebo ekvivalentního přístupného vykreslení. Základ zůstane běžný, **celá závorka včetně obou závorkových znaků** použije `success=#087C2B`. Preferovat dvě nativní statická pole s úplným srozumitelným textem pro čtečku, bez barevných značek v titulku. Při úzké šířce může závorková část přejít na další řádek, geometrie musí použít skutečnou výšku.

### 6.2 Serverové formátování Raw JSON

Rozhodnutí plánu podle intentu v007: v autoritativním `phpvpn.php`, pouze v `InfoHandler::buildPlaintext()`, doplnit k `JSON_UNESCAPED_SLASHES` standardní `JSON_PRETTY_PRINT`. Zachovat stávající PHP serializaci a její escapování, nesestavovat JSON vlastními textovými pravidly. Tím zůstanou pole, jejich pořadí, čísla, řetězce a význam zachovány. Nezavádět klientskou reserializaci ani neměnit společný C++ JSON parser.

Změna se uplatní na standardní i rozšířenou info odpověď přes společný builder. Formátovat pouze vnitřní plaintext. Vnější odpověď `type/requestId/salt/iv/ciphertext/tag`, auth requesty, kanonický objekt rozšíření E, HMAC vstupy a NDJSON nesmějí být touto volbou zasaženy. Zůstává stejné odvození klíče, náhodný salt/IV, AAD i AES-256-GCM, které autentizují a šifrují přesné nové bajty. Ciphertext a GCM tag se přirozeně změní, proto nelze požadovat jejich shodu se starým kompaktním plaintextem.

Limit **MAX_INFO_PLAINTEXT_BYTES = 262144 bajtů** se nezvyšuje. `strlen()` musí zůstat až po pretty serializaci uvnitř stávající smyčky. Nadměrný výsledek vede k deterministickému odebrání poslední celé klientské položky a nové serializaci. Nesmí se měřit stará kompaktní varianta nebo počet Unicode znaků. Selhání json_encode či příliš velký základní objekt vrátí stávající bezpečnou chybu `server_misconfigured`. Neměnit pořadí klientů, filtrované contracts ani publikaci/touch.

Odsazení může při limitním počtu položek způsobit dřívější odebírání nejvyšších IP. Jde o stávající výslovně zachovaný velikostní mechanismus intentu, nikoli o libovolné filtrování dat. Test musí doložit úplnost každé zachované položky, deterministickou hranici a nezměněné hodnoty. Při zmrazeném testovacím čase porovnat sémantiku a pořadí s původní kompaktní serializací stejné sady položek. Pokud maximální reálná fixture limit nedosáhne, testovat ořezávací větev v izolovaném procesu s testovací sníženou konstantou nebo úzkým testovacím seamem, bez úpravy produkčního limitu.

Klient `ServerInfoWindow::finish()` nadále zobrazí přesný ověřený `snapshot.infoPlaintext`. Zachovat read-only, monospace, oba posuvníky, selection/copy, focus a stávající obnovu výběru a viditelného řádku. Žádný lokální přepočet přes DOM. Starší server s kompaktním plaintextem zůstává podporovaný a klient jeho obsah zobrazí kompaktně. Nový server musí projít současným klientským parserem bez změny kryptografických kontrol. UI fixture má dodat skutečně odsazený plaintext a test má porovnat zobrazené znaky s touto hodnotou, nikoli jen přítomnost názvu serveru.

Velikost již omezeného plaintextu, ciphertext/base64url, vnější HTTP obálky i IPC wrapperu ověřit end-to-end. Neměnit limity transportu, IPC nebo EDIT naslepo. V testu ověřit zobrazení celého povoleného plaintextu bez ořezu, LF/CRLF chování Windows EDIT, kopírování a zachování pozice po aktualizaci. Selhání serverového formátování či následného refreshu při dostupném modelu ponechá předchozí snapshot a neblokující notice. Ošetřit i tichou větev selhání modelu v `finish()`.

Při průzkumu nebyla zjištěna technická překážka serverového pretty-printu. Existuje nutná souběžná aktualizace `protocol.md:332`, které nyní výslovně říká „kompaktní“. Pokud následné detailní ověření prokáže bezpečnostní či nepřiměřenou kompatibilitní překážku, zastavit dotčenou práci a vyžádat rozhodnutí přes koordinátora. Bez něj nepřecházet na klientské formátování.

### 6.3 Auto refresh a lifecycle

Použít existující `attachToggleSwitch()` nad `BUTTON | WS_TABSTOP | BS_OWNERDRAW` s viditelným textem `Auto refresh`, vlastním ID a accessible checkbox jménem/stavem. Zapojení shared control nemá vyžadovat změnu jeho globálního chování. Tab order bude po viditelném obsahu/pageru: Refresh, Auto refresh, OK. Space přepíná stav, Shift+Tab funguje opačně.

Rozšířit `ServerInfoLayout` o obdélník přepínače. Refresh a Auto refresh tvoří nerozdělitelnou horizontální dvojici s přepínačem vpravo. Na široké ploše zůstává OK vpravo na stejné úrovni, na úzké ploše se přesune na další řádek. Zachovat společné okraje 16 DIP, mezery 8 DIP, standardní výšku a měřený text. Ověřit 320 DIP a 100/125/150/200 % DPI bez ořezání dvojice a bez překryvu notice/pageru. Nenutit přepínač pod Refresh.

Timer patří pouze právě zobrazenému ServerInfoWindow. Oddělit jej od pětisekundového status timeru tray a od 500ms progress timeru. Stav nesmí opustit paměť okna ani být zapisován přes config, registry či soubory.

| Událost | Chování |
|---|---|
| Každé `open()`, i znovuzvolení Show Info nad existujícím oknem | Zrušit plánování a nastavit unchecked. Pouhá aktivace nepřidá info request. Běžné přepnutí focusu nebo tabu není nové otevření. |
| První zobrazení po počátečním načtení nebo skutečné nové zobrazení skrytého okna | Výchozí stav vypnutý. |
| Zapnutí bez pending requestu | První termín za 5000 ms, nikoli okamžitý refresh. |
| Zapnutí během pending requestu | Nenaplánovat souběžný request. Po dokončení začne celý 5000ms interval. |
| Dosažení termínu | Zrušit aktuální timer a zavolat stejnou `request()` jako ruční Refresh. |
| Začátek libovolného refreshu | Suspendovat automatický termín. `pending_` blokuje ruční i automatický překryv. |
| Dokončení, včetně chyby nebo odmítnutého zahájení | Je-li přepínač stále zapnutý a okno platné, další termín za 5000 ms. Žádné dohánění zmeškaných intervalů. |
| Vypnutí | Okamžitě zastavit budoucí plánování. Již běžící request může bezpečně dokončit obvyklou cestou, nesmí znovu zapnout timer. |
| Zavření, skrytí ukončující zobrazení, zničení, endSession | KillTimer, unchecked, zneplatnění generace a zahození časového stavu. Pozdní WM_TIMER/IPC odpověď nesmí zasáhnout další otevření. |

Použít monotónní deadline a jednorázový způsob obsluhy nativního timeru. Při WM_TIMER znovu ověřit čas, generation, enabled, pending a existenci okna. Windows timer není přesný real-time zdroj, zpožděný tick nezakládá frontu náhradních požadavků. Timer pokračuje na obou tabech a minimalizace již zapnutého okna není nové otevření. Syntetické testy musí odlišit reaktivaci přes Show Info od obyčejného WM_ACTIVATE.

Zachovat existující neblokující notice při selhání refreshe s dostupným modelem. Počáteční selhání bez modelu nadále následuje dnešní tok. Ošetřit i dnes tichou větev selhání `buildServerInfoModel()` tak, aby zůstala stará data a timer se bezpečně znovu naplánoval.

## 7. Testy a validační postup

### 7.1 Automatická matice nové funkčnosti

| ID | Test a důkaz |
|---|---|
| P01 | Skutečná cesta enginu: jeden TX, pak prázdné výměny. Původní podmínka drží minimum i po 5 s, oprava zdvojnásobuje. Uchovat původní FAIL a nový PASS. |
| P02 | Samostatný RX a TX nastaví minimum a nové celé rychlé okno. Odchozí skutečně předaný, ale serverem odložený paket rovněž obnoví okno. Pouhé frontování/cancel před handoffem nikoli. |
| P03 | Fake clock 0, 4999, 5000 a 5001 ms. Libovolný počet prázdných pollů před hranicí nezkrátí okno. Přesná hranice spustí první zdvojnásobení. |
| P04 | 50/3200 posloupnost až k saturaci, min=max, maximum mimo mocniny dvou, např. 50/750, krajní platné meze, nepoužitelné meze a overflow. |
| P05 | Nový paket uprostřed backoffu a těsně před hranicí, střídání TX/RX, dlouhá výměna, dlouhý health po prázdném pollu. Rozhoduje datový čas. |
| P06 | Prázdné HTTP/NDJSON, auth-only poll, health/time sync, staré kumulativní čítače a skok UTC vpřed/zpět neobnoví okno. |
| P07 | Konfigurace, pomalý bootstrap, Connect, Disconnect, shutdown, recovery a změna serverových mezí. Žádný časový stav z předchozí session. Zachované checkpointy a diagnostické čítače. |
| C01 | Potvrzený Save při connected otevře přesný význam dialogu, Yes/No a skutečný default Yes. Yes spustí právě standardní Disconnect a žádný Connect. |
| C02 | No, Escape a zavření dialogu zachovají připojení. Chyba dialogu neznamená Yes. Nepřipojený klient nemá výzvu. |
| C03 | Rejected, not-sent, timeout, unknown a samotný matched nezpůsobí odpojení. Připojení/odpojení během Save, neznámý status, modalita, změna generace a pozdní odpověď. |
| U01 | Validní řádek má samostatnou zelenou, invalidní přesně contractsChanged, offline nezměněn. Výběr, focus a high contrast jsou čitelné. Jiné success plochy se nezměnily. |
| U02 | Serverový pretty plaintext má odsazení/řádky, zachovává pole, pořadí a hodnoty. Klient zobrazuje přesný ověřený text. Serverová chyba/refresh neztratí starý obsah, copy/výběr/scroll fungují. |
| U03 | OK je modré primary a zachová původní klávesovou funkci. Nadpis i titulek přesně pro 0/1/2 a maximum peerů, zelená celá závorka. |
| A01 | Každé nové otevření i Show Info reaktivace začínají unchecked. Stav se nepřenáší a nezpůsobí žádný persistentní zápis. |
| A02 | První refresh až za 5000 ms, oba taby. Pomalý request přes několik period má nejvýše jeden pending, následující až 5000 ms po dokončení. |
| A03 | Souběh ručního Refresh a timeru, vypnutí během requestu, jeho chyba, busy při zahájení, zavření/reopen, pozdní WM_TIMER/response. Žádné hromadění ani timer po zavření. |
| A04 | Mouse, Space, Tab/Shift+Tab, accessible name/role/state a geometrie přepínače vpravo od Refresh na stejné úrovni. |

| S01 | Standardní auth-semi i rozšířené auth-full info: přesné dešifrované bytes obsahují pretty whitespace. DOM a pořadí členů odpovídají stejné kompaktní fixture, contracts zůstávají správně filtrované. |
| S02 | Skutečný byte count pretty plaintextu je nejvýše 262144. Přesná hranice, překročení, deterministický array_pop, selhání serializace a případ, kdy se nevejde základní objekt. Nikdy neměřit reserializovaný kompaktní DOM. |
| S03 | Nezávislé Python AESGCM a C++ CNG ověří nový serverový výsledek. Změněný tag/AAD/ciphertext/requestId zůstane odmítnut. Stejné derivace, salt/IV velikosti, vnější obálka a kanonický request E. |
| S04 | Stávající klientský parser přijme nové pretty info, nový klient přijme staré kompaktní info. End-to-end HTTP, base64url, IPC a EDIT limity bez ořezu a bez změny přesných plaintext bajtů před validací. |

Serverové testy rozšířit v `tests.php`, `tests-info-extensions.php`, `tests-integration/test_info.py` a `test_info_extensions.py`. V Python helperu `phpvpn_client.py` zpřístupnit autentizované dešifrované bytes před json.loads a ponechat dosavadní DOM API jako obal. Velikostní test přepsat na délku těchto bytes. C++ parser/model testy ověří whitespace a zachování původního textu.

Umístit pollingové testy do `test_runtime_status.cpp` s existujícími `TestClock`, `ScriptedTransport`, `VpnEngineTestAccess` a `EngineFixture`, případně vyčlenit nový úzce zaměřený soubor a přidat jej do CMake. Testovat produkční propojení enginu a scheduleru, ne kopii stejného výrazu v testu. Řídit transport a čas bariérami, bez dlouhých sleepů pro dokazování hranice. Na skutečné smyčce alespoň jeden krátký kontrolovaný test ověří i aplikaci vypočtené prodlevy.

Contracts testy doplnit do existujících model/save/lifecycle sad (`test_contracts_model.cpp`, `test_wp03_save.cpp`, `test_wp04_lifecycle.cpp`, příslušné native windows testy) a skutečné tray akce ověřit přes soukromý IPC backend v `tray_ui_tests.cpp`. Model/JSON testy do `test_server_info_model.cpp`, podle potřeby `test_json.cpp`. Geometrii do `test_server_info_layout.cpp`, paletu do `test_tray_palette.cpp`. Nativní server-info testy rozšířit u existujících `serverInformationTabs` a `serverInformationRefresh`. Časovací helper testovat fake časem, alespoň jeden skutečný nativní test doloží propojení timeru s produkčním oknem.

### 7.2 Spouštění a integrační prostředí

V aktivovaném MSVC x64 prostředí, pracovním adresáři `C:\git\phpvpn\client`:

```powershell
cmake --preset debug -DPHPVPN_BUILD_UI_TESTS=ON
cmake --build --preset debug
ctest --preset debug --output-on-failure
cmake --preset release -DPHPVPN_BUILD_UI_TESTS=ON
cmake --build --preset release
ctest --preset release --output-on-failure
```

UI cíle nejsou registrovány v CTest. Explicitně spustit `work/build/debug/tests/phpvpn-wp03-ui-tests.exe` a `work/build/debug/tests/phpvpn-tray-ui-tests.exe` v interaktivní relaci, obdobně release. Nejprve nové cílené filtry, potom relevantní existující contracts, disconnect, server-information, modern-toggle a repaint scénáře. Použít podporovaný runner `client/tools/dev-tray.ps1 -Mode Test -Filter <skutečně registrovaný filtr>` při vývoji, nulový výběr testů je chyba. Nezaměnit jeho vlastní `tray-dev-*` build s distribučním release presetem.

Opt-in C++ relay kontrakty: povolit `PHPVPN_BUILD_CONTRACT_TESTS=ON`, nastavit `PHPVPN_CONTRACT_URL` na izolovanou připravenou fixture a spustit `phpvpn-contract-tests` přes CTest. Existující `client/tests/contract/test_server_contract.cpp:250–311` ověřuje A/B contracts a get-server-info bez Connect. Rozšířit, pokud nové chování vyžaduje další smluvní důkaz. Python serverová sada neprokazuje Windows polling, ale je povinným nezávislým důkazem serverového pretty-printu, limitu a kryptografické kompatibility. Spustit také `php -l phpvpn.php` a `php tests.php` v podporovaném PHP CLI, včetně ověření, že rozšířené info testy jsou zahrnuty. Pokud je projekt spouští samostatně, provést i jejich skutečný runner.

Z kořene repozitáře pro izolovaný server:

```powershell
uv run --python 3.13 --with pytest --with cryptography --with requests -- pytest tests-integration -v --server-config <izolovana-fixture.json>
```

Fixture připravit pomocí `tools/deploy-test.ps1` s explicitním odděleným WebRoot, BufferDirectory, BaseUrl a ConfigurationPath podle `tests-integration/README.md`. Nepřesměrovat destruktivní testy do provozních front. Nasazený `D:\Nginx\html\phpvpn.php` neměnit, pokud diagnostika neprokáže serverovou příčinu. Požadované pretty-formátování vzniká v kořenovém `phpvpn.php` a nasadí se do explicitně izolované testovací fixture a následně do distribuce. Omezení úprav provozního Nginx souboru při diagnostice pollingu tím zůstává zachováno.

Pro skutečný SCM/Wintun lifecycle použít `client/tools/run-service-e2e.ps1` a jeho izolované preflight/cleanup podmínky. Referencí je `client/tests/e2e/README.md`, včetně samostatného relay na portu 18093. Nástroj nesmí převzít již existující testovací službu nebo adaptér. Tento E2E neprokazuje multiuser ani sleep/resume, tyto kontroly zůstávají samostatné.

### 7.3 Praktická reprodukce

Je-li dostupný druhý klient/VM, připojit obě aktuální klientské sestavy ke stejnému serveru, provést ping, ukončit jej a doložit absenci dalšího VPN payloadu na obou stranách. Rozlišit HTTP health/info od data a skutečný paket od prázdného pollu. Uchovat monotónní čas posledního TX/RX, dokončení pollů a plánované intervaly bez packet obsahů, klíčů a raw requestů. Po pěti sekundách doložit backoff a nový ping vracející minimum. U síťových časů uvést toleranci plánování a RTT, přesnou 5000ms hranici dokazuje deterministický test.

Ručně ověřit UI na 100/125/150/200 % DPI, světlém/tmavém prostředí a high contrast, resize ze všech hran, přesun mezi monitory, minimální plochu, čtečku obrazovky, focus a kopírování raw textu. Bez takového prostředí označit scénáře NOT RUN s konkrétním důvodem. Screenshoty a logy ukládat mimo zdrojový strom a mimo dist, nikdy s tajemstvími.

## 8. Dokumentace a úplná distribuce

### 8.1 Dokumentační rozhodnutí

Aktualizace je **prokazatelně nutná** v `client.md:212–223` a `client.md:1005`: doplnit rychlé okno a aktualizovat OK/Auto refresh. U Raw Information přesně uvést, že server vrací odsazený JSON a klient jej nadále zobrazuje beze změny. Stejnou úpravu provést v `tray_UX.md:181–189`, včetně footeru, singuláru a zelené závorky.

**Povinně upravit také `protocol.md:332`**, který dnes předepisuje kompaktní info plaintext. Nový popis určí serverový pretty whitespace uvnitř info plaintextu a zachování limitu nad výslednými bajty. `protocol.md:344,369` sladit podle potřeby s odebíráním položek a šifrováním přesných bytes. Neměnit kanonický kompaktní request E na řádku 316, pravidla NDJSON ani jiné podpisové vstupy. Tato dokumentační změna vyplývá z výslovně schváleného serverového formátování v intentu v007 a nemění kryptografická pravidla.

Doplnit změny contracts v příslušné sekci ukládání, význam Yes/No, přesný stav připojení, lokální Auto refresh, completion-based interval a reset při Show Info. Doplnit testovací scénáře do `client/tests/manual/README.md` a podle nových filtrů do UI README a integrační dokumentace. Rozlišit skutečně provedené kontroly od checklistu.

Posoudit runtime dokumentaci a související text o mezích, aby nezůstal rozpor se stávajícím lokálním limitem a starou poznámkou B03. Neupravovat neměnné starší plány nebo intenty. `distribution.md` a distribuční nástroje měnit jen při prokázané překážce metodiky, nikoli preventivně. V závěrečném reportu uvést změněné dokumenty a posouzené oblasti bez nutnosti změny.

### 8.2 Příprava distribučního stromu

1. Zaznamenat aktuální commit, relevantní necommitnuté změny, verzi binárek, architekturu, použité nástroje a výsledek testů. Zkontrolovat, že po posledním build/test kroku nebyly zdroje znovu změněny. Samotné stejné číslo verze není důkaz čerstvosti.
2. Ověřit MSVC AMD64, CMake >= 3.28, Ninja, originální Wintun a dostupný RSA podpisový certifikát s privátním klíčem. Neexportovat privátní klíč do balíčku. Nástroj má vývojový thumbprint, jeho dostupnost ani důvěra nebyly při plánování ověřovány. Chybějící certifikát je distribuční blocker, nelze jej nahradit nepodepsaným manifestem nebo předstíranou veřejnou důvěrou.
3. Sestavit release preset z aktuálních zdrojů, při pochybnosti použít clean rebuild odpovídajícího ověřeného build adresáře. Build cache, testy a logy ponechat mimo dist.
4. Připravit nový čistý staging strom, například `C:\git\phpvpn\work\dist-stage-<jedinecny-cas>`. Před každým mazáním nebo nahrazením ověřit normalizovanou absolutní cestu, reparse body a příslušnost k projektu. Konečný cíl musí být přesně `C:\git\phpvpn\dist`. Nepřekopírovat nové soubory přes starý obsah.
5. Zkopírovat aktuální kořenový `phpvpn.php` beze změny do `server/phpvpn.php`. Nevzít nasazenou kopii z Nginx. Serverová status stránka je vložená ve skriptu, samostatné `assets/index.html` není třeba.
6. Vytvořit Windows balíček příkazem s explicitními cestami:

```powershell
& C:\git\phpvpn\client\tools\make-dist.ps1 -BuildDirectory C:\git\phpvpn\work\build\release -OutputDirectory <staging>\client\Windows\x64 -CertificateThumbprint <overeny-thumbprint>
```

7. Povinný obsah `client/Windows/x64`: `install.bat`, `install.ps1`, `uninstall.bat`, `uninstall.ps1`, `phpvpn-service.exe`, `phpvpn-tray.exe`, `phpvpn.exe`, originální nepřejmenovaný `wintun.dll`, `LICENSE-wintun.txt`, `LICENSE-material-symbols.txt`, `NOTICE-material-symbols.txt`, `release-manifest.json`, `release-manifest.sig`, `release-signing.cer`. Doplnit každou další skutečně potřebnou runtime závislost. Nepřidávat testovací EXE, provozní config, tajemství, fronty ani samostatné Wintun `.sys/.cat/.inf`.
8. Vytvořit `client/macOS/README.md` a `client/Linux/README.md` se stručným pravdivým popisem, že aktuální distribuovatelný cíl je Windows x64 a pro tyto platformy není v této etapě předávaná ověřená implementace. Neslibovat podporu jen přítomností adresáře.
9. Všechen obsah Windows složky dokončit před podpisem manifestu. `-SignBinaries` je volitelné další Authenticode podepsání tří EXE, podpis manifestu nenahrazuje. Pokud se mění obsah po vytvoření manifestu, balíček a podpis znovu vytvořit. Needitovat manifest ručně.

### 8.3 Kontroly konečného balíčku

Ověřit povinnou strukturu a absenci starých souborů. SHA-256 a bajtovou shodu `server/phpvpn.php` s kořenovým zdrojem. PE machine **0x8664/AMD64** u všech tří EXE i DLL, ne pouze služby, a dostupnost všech závislostí. Ověřit originální Wintun, jeho Authenticode, povinná API a kompatibilitu běžícího ovladače podle `client.md`.

Ověřit kompletní seznam souborů/velikostí/hashů, kanonický jednořádkový UTF-8 manifest bez BOM, `manifestVersion`, architekturu a podpis **RSA-PKCS1-SHA256 přes přesné bajty manifestu**. Identita musí souhlasit v certifikátu, podpisovém záznamu, manifestu a očekávaném pinu. Důvěru certifikátu uvést pravdivě, vývojový podpis není veřejně důvěryhodné vydání.

Spustit `install.ps1 -VerifyOnly -ExpectedThumbprint <overeny-thumbprint>` nad dokončeným Windows balíčkem. Negativní testy provádět pouze na izolovaných kopiích mimo dist: změněný hash, podpis, chybějící soubor, nesprávná architektura a nadbytečná binárka. Prokázat nulové instalační účinky VerifyOnly a fail-closed výsledky. Ověřit relativní cesty BAT/PowerShell skriptů z předatelné složky, ne náhodnou závislost na build stromu.

Následuje skutečná instalace a provoz. `distribution.md` vyžaduje testy `client.md:1068–1106`, pokud je uživatel výslovně nevyloučí. Připravit evidenční matici všech **35 očíslovaných scénářů**, přiřadit každý automatickému testu nebo ručnímu provedení a zaznamenat PASS/FAIL/NOT RUN s důkazem. Neomezit tuto fázi na VerifyOnly. Matice zahrne zejména:

- čistou instalaci, první bootstrap, skutečný Wintun adaptér/session a opakovaný Disconnect/Connect,
- běžný i same-version upgrade, správné ukončení tray a služby, autostart, restart, uninstall se zachováním dat a izolované explicitní RemoveData,
- povinné negativní podpisové, architekturní a API testy před síťovými změnami,
- pády, recovery/reconciliation, částečnou inicializaci/teardown, sleep/hibernate/resume a Windows upgrade/restart,
- souběh s jinou VPN/Wintun aplikací, kolize adres/rout, zachování cizích prostředků, default route a DNS, dosažitelnost relay mimo tunel,
- ring overflow/backpressure a paměťovou bezpečnost, singleton službu, více interaktivních relací, odhlášení a pád tray bez zastavení VPN,
- všechny UI, DPI, checkpoint, čítačové, IPC a progress scénáře 19–35 v jejich skutečném rozsahu,
- ACL instalace i upgrade: Users Read & Execute se dědí na celý datový strom včetně záloh/logů, start služby opraví existující strom bez ztráty konfigurace, uživatel otevře složku a zkopíruje config bez UAC, přímý zápis je odmítnut, Edit Config a Wizard uloží přes podporovanou cestu.

Tyto testy provádět v připraveném izolovaném Windows prostředí. Destruktivní scénáře a RemoveData nesmějí použít osobní konfiguraci nebo produkční instalaci. Historické reporty ani syntetický UI backend nejsou nový runtime důkaz této distribuce. Není-li některá povinná kontrola proveditelná, zaznamenat blocker a předat sestavený kandidát jako neúplně ověřený. **Neoznačit jej za hotovou ověřenou distribuci.** Nedostupnost prostředí není implicitní výjimka uživatele.

Po ověření stagingu nahradit celý obsah přesně ověřeného `dist`, předchozí případnou zálohu držet mimo něj. Znovu ověřit konečné cesty, hashe a VerifyOnly nad `C:\git\phpvpn\dist\client\Windows\x64`. Případný ZIP vytvořit mimo dist a porovnat jeho obsah včetně platformních README s konečným stromem.

## 9. Rizika a rozhodovací body

| Riziko | Ošetření a podmínka pokračování |
|---|---|
| Test pouze helperu by neodhalil původní kumulativní podmínku | Povinný FAIL/PASS přes produkční runtime propojení P01. |
| Záměna potvrzeného TX a skutečného předání | Samostatná aktivita výměny, bez změny diagnostických čítačů a retry/FIFO. |
| Pomalý health posune hranici | Zachytit dokončení data před health, testovat obě strany 5000 ms. |
| Serverové meze odporují lokálním bezpečným mezím | Validovat průnik, odmítnout neslučitelné hodnoty. Neprovést nesouvisející backpressure. |
| Modalita a zastaralé connected flagy | Vyhodnocení po potvrzeném Save, korelovaný status při nejistotě, lifecycle/connection identity guard. |
| Pretty whitespace zvětší plaintext nebo test změnu zamaskuje | Limit nad skutečnými dešifrovanými bytes, deterministické odebírání celých posledních položek a end-to-end limity. Klient nereprezentuje JSON znovu přes DOM. |
| Timer obslouží starou zprávu po reopen nebo ruční refresh souběžně | Jeden pending guard, vlastní generation, monotónní deadline a reset všech ukončovacích cest. |
| Užší footer či dělený nadpis poškodí přístupnost | Měřená geometrie a nativní kontroly focusu, čtečky, 320 DIP a DPI. |
| Distribuce bude obsahovat staré EXE či nesouhlasný podpis | Čistý strom, aktuální release build, podpis až po finalizaci, závěrečné ověření konečné cesty. |
| Chybí certifikát, admin relace, VM nebo runtime prostředí | Zachovat výsledky, uvést přesný blocker a NOT RUN, neprohlásit akceptaci hotovou. |

Pokud reprodukční test nepotvrdí popsaný mechanismus, pokračovat diagnostikou až ke skutečné příčině, neimplementovat jen hypotézu. Pokud je nutná změna samotného rozsahu intentu, vrátit ji koordinátorovi k nové immutable verzi, nepřepisovat intent ani tento plán. Pozdější materiální upřesnění realizace se promítne do další úplné plánovací verze.

## 10. Akceptační brány a předání

| Kritéria intentu | Nutný výsledek |
|---|---|
| 1–4 | Doložená příčina, runtime FAIL/PASS, celé monotónní 5s okno, správná aktivita, exponenciální růst v mezích a reset novým paketem. |
| 5 | Potvrzený Save, default Yes, skutečný standardní Disconnect, No a chybové větve bez odpojení. |
| 6–8 | Požadované barvy/OK/nadpis, serverové pretty JSON s přesným klientským zobrazením a nepersistovaný Auto refresh na obou tabech bez překryvu. |
| 9 | Relevantní automatické sady skutečně spuštěny a prošly. Počet a filtry jsou doloženy, nula testů není PASS. |
| 10 | Praktická dvouklientská a UI kontrola provedena při dostupném prostředí, jinak konkrétní omezení. |
| 11 | Aktualizované doloženě zastaralé dokumenty a zaznamenané posouzení ostatních. |
| 12 | Úplný aktuální dist, podpisové artefakty a všechny povinné distribuční kontroly. Neúplně ověřený kandidát nesplňuje tuto bránu. |
| 13 | Report obsahuje změněné soubory, identitu zdrojů, důkazy, testy, omezení, důvěru podpisu a přesnou cestu výsledku. |

Závěrečný realizační report uvede `C:\git\phpvpn\dist` a samostatně `C:\git\phpvpn\dist\client\Windows\x64`, verzi a architekturu, shodu serveru, souhrn unit/runtime/UI/integration/manual/install testů a každé NOT RUN/FAIL. Připojí evidenci původní regrese a její opravy, potvrzené chování UI a seznam dokumentačních změn. Neuvádět úspěch pouze na základě kompilace nebo existence souborů.

**Stav při vytvoření tohoto plánu:** provedeno pouze čtení pravidel, celého intentu, Graft průzkum a čtení relevantního kódu a metodik. Implementace, testy, síťová reprodukce, distribuce a instalace nebyly v plánovacím tasku spuštěny. Vytvořeny byly pouze plánovací artefakty: zachovaná předchozí verze plan-v001.md a tato úplná verze plan-v002.md podle intentu v007. Zdroje, marker a intenty nebyly upravovány.
