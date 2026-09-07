# Druhá kritická revize plánu tray UX a rozdělení implementace

Revize z 8. září 2026 pro projekt phpvpn. Posuzované soubory jsou `plan3_fix3.md`, `plan3_fix3_work_for_agents.md` a `tray_UX.md`. Skutečný název posledního souboru v repozitáři je `tray_UX.md`.

## Výsledek a rozsah ověření

Našla jsem šest podstatných problémů: dva P1 a čtyři P2. P1 znamená vysoký dopad na ochranu konfigurace nebo komunikace, P2 významnou chybu návrhu či proveditelnosti. Nenašla jsem doložený problém P0. Před předáním plánu implementačním agentům doporučuji vyřešit zejména chybějící vlastnictví životního cyklu Edit Config a bezpečnostní prezentaci slabých klíčů.

Ověřený HEAD: `f2ba369bec913738fc9cd74dee67fc4e5901a6eb`. Při zahájení revize byl `plan3_fix3_work_for_agents.md` nesledovaný soubor. Jeho obsah jsem zachovala. Čísla řádků níže odpovídají souborům při tomto auditu, trvalejší identifikaci poskytují uvedené oddíly a symboly.

Přečetla jsem posuzované dokumenty a ověřila jejich relevantní vazby proti `client.md`, `protocol.md`, předchozí revizi `plan3_fix3_problems.md` a skutečnému kódu. Zvlášť jsem kontrolovala tray koordinaci, EditorWindow/EditorDocument, WizardWindow, CommandReader, IPC dispatcher, zpracování pipe a ConfigStore. Chování systémových barev a limity USER prostředků jsem ověřila také v dokumentaci Microsoftu. Jde o revizi návrhu a statickou kontrolu implementačních předpokladů. Neprováděla jsem build, GUI testy ani benchmark a nezasahovala jsem do služby, konfigurace nebo distribuce.

Schválené náhrady N01 až N09 mají přednost před starším zněním UX. Samotné dosud neaktualizované požadavky na modalitu, virtualizaci, 4092 řádků nebo širší CIDR proto nejsou vykázané znovu jako nové vady. Stejně tak nezpochybňuji již výslovně přijatý rozdíl uloženého a aktivního configu ani politiku poslední úspěšné publikace celého dokumentu.

## 1. P1: Ochrana Edit Config při Exit a nejasném Save nemá implementačního vlastníka

### Uživatelské rozhodnutí z 8. září 2026: náprava odložena

Uživatel tento problém nyní nechce řešit. Náprava se odkládá a může být zvážena někdy v budoucnu. Nález, jeho technické vysvětlení, dopad a doporučení zůstávají zachovány. Toto rozhodnutí neznamená vyřešení problému a nevyplývají z něj žádné další změny plánu nebo kódu.

**Místo ve všech třech dokumentech:**

- `plan3_fix3.md:140–148`, N04a, vztahuje saving/saveUnknown a potvrzování všech neuložených konceptů také na Edit Config. F06 na řádku 326 požaduje společné ukončování všech konfiguračních editorů. N03 na řádku 102 požaduje respektování dirty-state.
- `plan3_fix3_work_for_agents.md:144` zakazuje WP04 a WP05 měnit `editor_window.*`. WP03 na řádku 262 má editor měnit pouze prezentačně a jeho dirty chování „zachovat“. WP05 na řádku 314 nesmí měnit sémantiku editoru ani lifecycle Wizardu, ale na řádku 324 má zajistit společný Exit.
- `tray_UX.md:154–158`, UX06, ponechává zavírání podle dosavadního editorového chování a jen obecně zakazuje falešné tvrzení o neprovedeném zápisu. Tabulka plánovaných náhrad tento oddíl výslovně nepřepisuje na úplný nový životní cyklus.

**Ověřený předpoklad je chybný:** současný `client/apps/tray/editor_window.cpp:249–251,299` při Cancel/Escape/X přímo volá `DestroyWindow`. Dirty potvrzení zde není. `editor_document.h` řeší uchování původních bajtů a NUL, neposkytuje automat dirty/saving/unknown. `editor_window.h:14–24` nemá rozhraní pro dotaz na neuložený koncept, unknown ani koordinované zavření. V `main.cpp:238–255` se při dokončení transportu nejprve zruší saving, prázdná odpověď pak pouze zobrazí chybu. Stav saveUnknown a jeho uchování zde nejsou. Wizard má svůj stav i commit deadline soukromé v `wizard_window.h`, a ani jeho hlavička neposkytuje rozhraní pro koordinovaný Exit.

**Dopad:** agent nemůže požadované chování pouze „zachovat“ nebo bezpečně implementovat jen v koordinátoru. Dodržení přiděleného rozsahu ponechá možnost ztratit rozpracovaný text a po timeoutu nerozlišit neuložený koncept od možné publikace. Implementace N04a naopak vyžaduje změny zakázané vlastnickou tabulkou. Dodatečná oprava ve WP08 by byla chybějící funkcí, nikoli triviální regresní opravou.

**Náprava:** přidělit jednomu balíčku výslovně `editor_window.*`, editorový stav ukládání, nutné rozhraní `wizard_window.*` a koordinaci Exit. Oddělit neproměnnou službovou sémantiku Wizardu od povolené změny jeho lokálního UI lifecycle. Stanovit rozhraní pro dirty/unknown, původní deadline, generaci konceptu, potvrzené zahození a systémové ukončení. Současně aktualizovat UX06. Není nutný nový IPC příkaz.

**Nutný důkaz:** skutečné Edit Config s neuloženým textem, ztraceným ack a probíhajícím Reset, současně s Contracts a Wizardem. Exit musí respektovat No, nezdvojit potvrzení a po timeoutu použít unknown text. Ověřit také X/Cancel a WM_ENDSESSION TRUE/FALSE. Test pouze nad ContractsSaveController tento požadavek nedokládá.

## 2. P1: Nové ovládání umožní nahradit tajný klíč veřejným nebo snadno odhadnutelným bez varování o důsledku

**Rozhodnutí uživatele z 8. září 2026:**

Tento problém se nyní nebude řešit. Slabá i prázdná hesla mají zůstat povolená a odpovědnost za volbu vhodného hesla nebo klíče nese administrátor či uživatel. Rozhraní nemá přidávat varování ani dodatečné potvrzení. Nález a jeho technické důsledky zůstávají zdokumentované, toto rozhodnutí však znamená vědomé přijetí popsaného rizika a nevyplývají z něj žádné změny plánu nebo kódu.

**Místo ve všech třech dokumentech:**

- `plan3_fix3.md:108–118`, N03, předvyplňuje veřejný klíč odvozený z IP a dovoluje potvrdit SHA-256 prázdného hesla. Enabled se při generování nemění.
- `plan3_fix3_work_for_agents.md`, WP01 a WP05, přebírá stejné odvození, prázdný vstup i generátory. Jejich požadavky ani testy nevyžadují bezpečnostní upozornění před použitím těchto hodnot.
- `tray_UX.md:249,269–289`, UX10/11, používá popis „Manage secure communication“, vysvětluje veřejnost defaultu autorovi implementace, ale nepředepisuje viditelný text o ztrátě utajení v Contracts. Password SHA předepisuje jen popis generování a automatické aktualizace hashe. Naopak ve Wizardu UX09 viditelná varování před veřejnými defaulty výslovně existují.

**Konkrétní scénář:** uživatel otevře Password SHA u již zapnutého kontraktu s náhodným tajným klíčem a bez psaní stiskne Enter. Prázdné pole je platné, proto se převezme veřejně známý `e3b0…b855`, kontrakt zůstane enabled a hlavní OK jej publikuje. Jiná snadná cesta je zapnout předvyplněný kontrakt s klíčem odvoditelným pouze z dvojice IP. Slabé neprázdné heslo neposkytne 256 bitů entropie jen tím, že se zobrazí 64 hex znaků.

**Bezpečnostní dopad:** `protocol.md:404–437`, §9.4, používá payloadKey přímo jako AES-GCM klíč a přenáší IV, tag i podklady AAD. Držitel zachyceného payload záznamu tak může kandidáty hesla ověřovat offline přes GCM tag. U prázdného nebo IP defaultu není slovníkový útok vůbec potřeba. Týká se to například relay, který vidí obálky, přestože podle §3.2 nemá znát end-to-end klíče. Bez znalosti ostatních tajemství z toho neplyne obecné prolomení serverové autentizace. Jde o ohrožení utajení payloadu a při rozdílných klíčích obou peerů také o výpadek komunikace. Obecný důvod, proč rychlé SHA-256 neztěžuje hádání slabých hesel, popisuje [OWASP Password Storage Cheat Sheet](https://cheatsheetseries.owasp.org/cheatsheets/Password_Storage_Cheat_Sheet.html). Aplikace tohoto principu na ověřování kandidáta pomocí GCM vychází z místního protokolu.

**Náprava v mezích schváleného algoritmu:** předepsat viditelné varování v Contracts i Password SHA, odlišit syntaktickou platnost od bezpečnosti a doporučit Random pro tajný klíč. Výslovně upozornit, že stejný výsledný klíč je nutné bezpečně nastavit také u druhého peeru a změna se použije až při novém spojení. Pro potvrzení prázdného hesla nebo nahrazení aktivního tajného klíče veřejným defaultem doporučuji samostatné potvrzení s výchozím No. To je návrh doplnění UX ke schválení, nikoli automatické zrušení již schválené podpory prázdného vstupu.

Prostý SHA-256, veřejné defaulty a chybějící KDF jsou v dokumentaci vědomé volby. Nález spočívá v nové snadné cestě k oslabení klíče bez vysvětlení uživateli. Pokud má riziko zůstat bez dodatečného potvrzení, má být tato konkrétní možnost neúmyslného oslabení výslovně přijata. KDF se nesmí do implementace přidat potají.

**Nutný důkaz:** UI scénáře prázdné heslo → Enter u enabled kontraktu, veřejný default → Enabled a náhrada klíče při aktivní VPN. Testovat nejen výsledný hash, ale také upozornění, zachování původní hodnoty po No a žádný zápis před hlavním OK.

## 3. P2: Více Password SHA oken nemá předepsanou viditelnou identitu cílového kontraktu

**Rozhodnutí uživatele z 8. září 2026:**

Identita cílového kontraktu se zobrazí přímo v systémovém titulkovém pruhu okna ve formátu `Password SHA - Contract {minIP_maxIP}`. Hodnota se určí z neměnného cíle při otevření dialogu a nebude se měnit podle aktuálního focusu nebo výběru v rodičovském okně. Titul současně poskytne identitu okna čtečce obrazovky a přepínači aplikací, proto se kvůli této informaci nebude přidávat další prvek do obsahu dialogu. Dvě okna pro tentýž kontrakt mohou mít stejný titul, protože obě mají stejný cíl a nadále platí pravidlo posledního potvrzeného výsledku.

**Místo ve všech třech dokumentech:** `plan3_fix3.md:102,114,312`, N03/F05, dovoluje více oken i pro různé kontrakty a vyžaduje interní remoteIp/generation. WP05 na `plan3_fix3_work_for_agents.md:320` tento požadavek opakuje. `tray_UX.md:283–289`, UX11, předepisuje stejný titulek, nadpis, dvě pole a OK, bez viditelného contractId. UX03 na řádku 92 doplňuje identitu přístupného názvu opakovaných akcí v tabulce, nikoli trvale viditelnou identitu otevřeného heslového dialogu. Náhrady N03 tuto mezeru nedoplňují.

**Dopad:** uživatel otevře dialogy pro peer A a B, přepne okna nebo posune rodičovskou tabulku a nemá v aktivním dialogu údaj, který rozliší jeho cíl. Správná interní generace pouze zajistí, že se chybná uživatelská volba spolehlivě aplikuje na původní kontrakt. Následkem může být nechtěná rotace či sdílení klíče s jiným peerem a výpadek komunikace. Původní modalita tento problém částečně omezovala vazbou na právě zablokovaný řádek.

**Náprava:** ponechat schválený titulek, ale přidat trvale viditelné `Contract: {minIP_maxIP}` a odpovídající přístupný popis dialogu. Identitu získat z neměnného cíle při otevření, nikdy z aktuálního focusu rodiče. Zahrnout tuto bezpečnostní odchylku do vizuální akceptace mockupu.

**Nutný důkaz:** současně dvě okna pro různé kontrakty a dvě pro tentýž kontrakt, změna pořadí aktivace, scroll rodiče a ovládání čtečkou obrazovky. Před OK musí být cíl zjistitelný přímo v dialogu.

## 4. P2: Rozpočty chrání jeden editor, ale neomezený počet heslových oken je obchází

**Rozhodnutí uživatele z 8. září 2026:**

Tento problém se nyní nebude řešit. Limit současně otevřených oken Password SHA se v této etapě zavádět nebude a případná náprava může být zvážena někdy v budoucnu. Nález, jeho technické vysvětlení, dopad a doporučení zůstávají zachovány. Toto rozhodnutí neznamená vyřešení problému a nevyplývají z něj žádné další změny plánu nebo kódu.

**Místo ve všech třech dokumentech:** `plan3_fix3.md:102,116,238–255,312` omezuje délku jednoho hesla, součet Key textů a základní Contracts, zároveň dovoluje více Password SHA i pro stejný cíl bez celkového limitu. WP05 na `plan3_fix3_work_for_agents.md:320–326` vyžaduje více oken a kontrolu objektů, nevymezuje jejich maximální současný počet. `tray_UX.md:309–313`, UX13, omezuje vlastněné buffery, ale jeho předepsaná aktualizace nedoplňuje společný rozpočet živých dialogů.

**Dopad:** z 255 znaků na dialog neplyne omezená celková paměť. Každé další okno přidává HWND, vlastní cache, EDIT/undo a citlivý vstup. Opakované vyvolání téže akce může pokračovat až do selhání vytváření prostředků. Jde o lokální vyčerpání prostředků tray a případně sdílené relace, ne o doložený vzdálený DoS nebo zvýšení oprávnění. Microsoft potvrzuje procesní i relační omezení [USER objektů](https://learn.microsoft.com/en-us/windows/win32/sysinfo/user-objects). Test dvaceti cyklů otevřít/zavřít neověřuje mnoho současně otevřených dialogů.

Přesný účet potřebuje i základní tabulka. Čtyři nativní interaktivní prvky na každý z 252 řádků představují již 1008 HWND. Přidání samostatného STATIC pro identitu každého řádku dává 1260 HWND ještě před okolím okna. To neprokazuje nemožnost dodržet +1200, protože identitu lze řešit jinak, ale předepsaný limit nelze zaměnit za „252 controls“, jak jej zkracuje WP05.

**Náprava:** určit konečný limit současných Password SHA oken na rodiče i na tray relaci, stále větší než jedna a podporující více oken stejného cíle. Po dosažení limitu odmítnout pouze nové otevření, zachovat vstupy a funkční zavření/Disconnect. Oddělit rozpočet základní tabulky od povolené skupiny dětí a doplnit konkrétní účet HWND/GDI/USER i bezpečné selhání částečné konstrukce. Neřešit to zvýšením systémových kvót.

**Nutný důkaz:** maximální počet současných oken, pokus o další, vynucené selhání CreateWindow/resource alokace, následné zavření rodiče a návrat počtů k baseline. Měřit i dostupnost tray akcí během této zátěže.

## 5. P2: Cache obnovovaná pouze při změně DPI odporuje podpoře High Contrast

**Rozhodnutí uživatele z 8. září 2026:**

Tento problém se nyní nebude řešit. Obnova barevných prostředků cache při změně systémových barev nebo režimu High Contrast se v této etapě nebude doplňovat a případná náprava může být zvážena někdy v budoucnu. Nález, jeho technické vysvětlení, dopad a doporučení zůstávají zachovány. Toto rozhodnutí neznamená vyřešení problému a nevyplývají z něj žádné další změny plánu nebo kódu.

**Místo ve všech třech dokumentech:** `plan3_fix3.md:219`, N09a, dovoluje obnovit cache fontů, ikon, štětců a per pouze po skutečné změně DPI. F01 i WP02 v `plan3_fix3_work_for_agents.md:222–234` mají tento přesný model převzít. `tray_UX.md:90`, UX03, přitom vyžaduje systémové barvy a focus v High Contrast. N09b/A20 podporu High Contrast zachovávají, ale nevymezují událost změny barev jako výjimku z pravidla cache.

**Dopad:** přepnutí High Contrast nebo systémových barev během otevřeného editoru při stejném DPI nezmění cache vytvořených barevných prostředků. Vlastní kresba může zůstat ve světlé paletě, zatímco nativní prvky přejdou na nové barvy. Mohou zmizet čitelné hranice, focus nebo validační text. Toto je funkční problém přístupnosti, nikoli požadavek na kosmetickou podobu.

**Ověření platformy:** Microsoft pro [WM_SYSCOLORCHANGE](https://learn.microsoft.com/en-us/windows/win32/gdi/wm-syscolorchange) výslovně popisuje obnovu štětců používajících změněné systémové barvy a předání zprávy relevantním common controls. Stav [High Contrast](https://learn.microsoft.com/en-us/windows/win32/winauto/high-contrast-parameter) se má zjišťovat při inicializaci i při změně systémových barev.

**Náprava:** klíč platnosti cache rozšířit o skutečnou generaci barev/High Contrast a podle použitých prostředků také relevantní změnu systémového nastavení či tématu. Povolit obnovu dotčené části cache a invalidaci při těchto událostech. Nadále nevytvářet prostředky při běžném WM_PAINT. Změna barvy nevyžaduje zbytečně vytvářet nová pole nebo rušit jejich vstup.

**Nutný důkaz:** přepnout High Contrast zapnuto/vypnuto nad otevřenými okny při neměnném DPI, včetně rozpracovaného klíče, chyby a focusu. Ověřit čitelnost, zachování vstupu a návrat GDI/USER po opakování.

## 6. P2: Pracovní balíčky zakazují synchronizaci dokumentů, kterou nadřazený plán vyžaduje během jednotlivých fází

**Rozhodnutí uživatele z 8. září 2026:**

Každý pracovní balíček aktualizuje příslušné části normativních dokumentů současně se svou implementací. Změny dokumentace musí zůstat omezené na rozsah daného balíčku a agent je přesně uvede v předávacím reportu. WP07 nebude odkládat veškerý normativní zápis až na konec, ale provede závěrečný audit úplnosti, terminologie a konzistence napříč dokumenty. Zákazy zápisu do `client.md` a `tray_UX.md` v kartách WP00 až WP06 je nutné upravit tak, aby této strategii neodporovaly.

**Místo ve všech třech dokumentech:** `plan3_fix3.md:284,306,326`, F01/F04/F06, vyžaduje zapracovat normativní změny současně s danou implementací, F04 doslova „v této fázi“. Stejný princip mají úvod a tabulka povinných aktualizací. `tray_UX.md:7` počítá se současným zapracováním náhrad. Naproti tomu `plan3_fix3_work_for_agents.md:75,148` a karty WP00 až WP06 zápis `client.md`/`tray_UX.md` zakazují a odkládají jej na WP07. Organizační dokument sám řadí technický plán výše než vlastní pokyny a při konfliktu převzatého kontraktu předepisuje STOP.

**Dopad:** samostatný agent nemůže současně splnit dokončovací požadavek vyššího plánu a zákaz zápisu své karty. Může se zastavit, nebo svévolně zvolit jiný postup. V mezidobí navíc noví agenti povinně čtou staré normativní dokumenty v témže checkoutu. Nejde o samotnou existenci známých starých vět, ale o rozpor dvou aktivních instrukcí určujících, kdy je odstranit a kdo to smí provést.

**Náprava:** výslovně sjednotit jednu strategii v obou plánovacích dokumentech. Buď povolit příslušné normativní změny každému balíčku a WP07 ponechat závěrečný audit, nebo změnit F01 až F06 tak, že normativní zápis provede WP07 a dřívější balíčky předávají závazné textové delty. Ve druhém případě jasně vymezit dočasnou autoritu N01 až N09 a zakázat označit celek za dokončený před WP07. Pouhá existující tabulka precedence neodstraní explicitní vzájemně nesplnitelné požadavky.

**Nutný důkaz:** kontrola každé karty proti nadřazené fázi, včetně scope, dokončovací brány a předání. Žádný balíček nesmí být současně povinen a zakázán měnit týž dokument.

## Oblasti bez dalšího potvrzeného závažného nálezu

- **Wire protokol a kryptografické primitivy:** nenašla jsem požadavek měnit AAD, IV, HKDF, tagy nebo serverové endpointy. Náhodný klíč má správných 32 bajtů z CryptoProvider/CSPRNG. Nález 2 se týká entropie vstupů a bezpečnostního UX, nikoli chyby SHA-256 nebo AES-GCM.
- **Publikace Contracts:** návrh správně zachovává celý původní DOM mimo upravovanou část, validuje kompletní kandidát a používá existující ConfigStore. Kód ConfigStore serializuje čtení a publikace a dispatcher vrací standardní ack. Nenašla jsem v plánu novou přímou cestu zápisu do filesystemu nebo obcházení ACL. To není náhrada testů skutečné atomické publikace.
- **Unknown výsledek Contracts:** shoda současného dokumentu se správně nevydává za historický receipt a není předepsán automatický retry. Samotná pozdější publikace jiného editoru není porušením výslovně přijatého pravidla posledního zápisu. Nezaměňovat ji však s pořadím doručení ack, zkrácené „poslední potvrzený save“ ve WP05 je třeba implementovat podle přesnějšího N04.
- **IPC a izolace preview:** plán zachovává uzavřená schémata, konečné klientské deadline, samostatný Disconnect a zakazuje produkční fallback fixture. Nenašla jsem nový požadavek na vzdálený přístup, libovolnou cestu nebo povýšení oprávnění. Veřejná lokální pipe a přístup lokálních aplikací ke klíčům jsou již výslovně přijatou vlastností `client.md:713,745–791`, nikoli novou zranitelností této úpravy.
- **Kompatibilita CIDR:** omezení /24 až /29 je záměrně nekompatibilní, plán je transparentně uvádí a zahrnuje službový build i opravu fixture. Nenašla jsem důvod tvrdit, že samotná změna prefixu vyžaduje nový wire formát. Před nasazením zůstává nutné společně dodat kompatibilní tray a službu.
- **Výkon a distribuce:** odlišení Release od RelWithDebInfo, nenulové testovací filtry, omezená místní práce a nulový zápis do dist jsou popsány použitelně. Není podklad tvrdit, že výsledný tray již splňuje paměťové či časové limity nebo že je jejich splnění nemožné. To musí doložit implementace, zvlášť po doplnění celkového rozpočtu oken z nálezu 4.

Schválené omezení rychlé brány na cílené testy není samo o sobě vadou. Neprovedené reálné service, multiuser, instalační, síťové a dlouhodobé testy musí zůstat přiznaným omezením. Tato revize je neprohlašuje za splněné.
