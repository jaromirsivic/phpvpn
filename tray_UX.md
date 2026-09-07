# Normativní UX tray aplikace PHPVPN

## UX00: Platnost a podklady

Tento dokument určuje cílové chování následné implementace, nikoli stav hotového GUI. Vznikl 7. září 2026 podle [zadání](notes/2026-09-07-11-19-48_notes.processed.md). Implementační pořadí, přesné změny nynějšího `client.md`, IPC a akceptační matice jsou v [plan3_fix3.md](plan3_fix3.md). V této dokumentační etapě se `client.md`, kód, testy ani distribuce nemění.

Slova MUSÍ, NESMÍ a SMÍ jsou závazná. Identifikátory UX01 až UX14 jsou cíle testů. [protocol.md](protocol.md) zůstává autoritou wire protokolu, [client.md](client.md) autoritou ostatních klientských pravidel a [distribution.md](distribution.md) autoritou plného vydání. Rozpory cílového UX s dnešním `client.md` řeší normativní náhrady N01 až N09 v plánu, které se mají zapracovat současně s implementací. Historické plány nejsou důkazem aktuálního kódu.

Závazné vizuální reference:

| Okno | Reference | Určující kompozice |
|---|---|---|
| Communication contracts | [communication_contracts1.png](assets/communication_contracts1.png) | Nadpis a popis vlevo, vlastní IP vpravo, tabulka, pevná patička s OK. |
| Password SHA-256 | [communication_contracts2.png](assets/communication_contracts2.png) | Menší dialog nad zakázaným rodičem, dvě pole a OK. |
| Connect failure / success | [connect1.png](assets/connect1.png), [connect2.png](assets/connect2.png) | Connection status, velký nadpis, karta detailů a kontrol, samostatný výsledkový panel. |
| Edit Config | [editconfig.png](assets/editconfig.png) | Nadpis a cesta nad editorem, Reset vlevo, Cancel a Save vpravo. |
| Statistics | [statistics.png](assets/statistics.png) | Dvě horní karty, spodní karta přes šířku, devět hodnot. |
| Wizard | [wizzard0.png](assets/wizzard0.png), [wizzard1.png](assets/wizzard1.png), [wizzard2.png](assets/wizzard2.png), [wizzard3.png](assets/wizzard3.png), [wizzard4.png](assets/wizzard4.png) | Levý sidebar, pravý obsah s kartou, spodní oddělené akce. |

Názvy `wizzard*.png` se nemění. Uživatelské texty používají výhradně **Wizard**. Ukázkové IP, klíče a čítače na obrázcích nejsou zdrojem skutečných dat. Pomlčka mezi IP v obrázku kontraktů se výslovně nahrazuje požadovaným podtržítkem. Hodnoty editorového JSON na obrázku neurčují novou defaultní šablonu. Nativní titulkový pruh nemusí reprodukovat kreslený hamburger ani grafické nepřesnosti mockupů.

## UX01: Typografie, barvy a geometrie

Použít Segoe UI, při nedostupnosti systémové UI písmo. Konfigurační JSON, celé klíče, URL a technické názvy čítačů používají Consolas nebo dostupné neproporcionální písmo. Následující velikosti jsou logické výšky písma v DIP při 96 DPI, nikoli typografické body. Nativní titulkový pruh používá systémovou metriku.

| Role | Výška / váha | Použití |
|---|---|---|
| Hlavní nadpis | 28 / 600 | Communication contracts, Statistics, Edit configuration, Wizard krok. |
| Výsledek Connect | 34 / 600 | Connecting, Connection failed, Successfully connected. |
| Nadpis karty / dialogu | 20 / 600 | Connection details, Data polling, Generate key from password. |
| Podnadpis | 17 / 400 | Connection status, úvodní stručná věta. |
| Popis a label | 15 / 400 | Texty nad poli, popisy akcí. |
| Hodnota | 16 / 400, důležitá 600 | IP, běžné hodnoty. |
| Čítač | 24 / 600 | Zarovnaná číselná hodnota. |
| Pomocný a technický text | 13 / 400 | Hint, jednotka, název JSON pole. |
| Chybový text | 15 / 400, nadpis 600 | Vždy současně text i ikona. |
| JSON a Key | 14 / 400 mono | Bez automatického zalamování klíče nebo JSON řádku. |

Normativní paleta je konkrétní implementační aproximace světlých mockupů, nikoli tvrzení o přesném odečtu jejich pixelů:

| Token | Hodnota |
|---|---|
| background / surface | `#F7F8FA` / `#FFFFFF` |
| text / secondaryText | `#0D1533` / `#4B5C82` |
| border / separator | `#D6DCE7` / `#E1E5ED` |
| primary / primaryHover / primaryPressed | `#0062E8` / `#0056CE` / `#0049AF` |
| primaryText | `#FFFFFF` |
| selectedStep | `#E5EDF8` |
| success / successBackground / successBorder | `#087C2B` / `#F0FAF2` / `#A8D8B1` |
| warning / warningBackground | `#995400` / `#FFF6E5` |
| error / errorBackground / errorBorder | `#C82020` / `#FFF2F2` / `#E9A4A4` |
| disabledText / disabledBackground | `#747C8B` / `#F0F2F5` |
| focus | `#005FCC` |

Základ rozestupů je 4 DIP. Mezi labelem a polem 8, mezi souvisejícími prvky 12, mezi sekcemi 24, vnitřní okraj karty 24 a okraj hlavního obsahu 32 DIP. Při malé ploše lze okraj snížit na 16 DIP. Řádky tabulky mají minimálně 48 DIP, hlavička 40 DIP. Jednopixelové linky se škálují na nejméně jeden fyzický pixel. Karty mají roh 8 DIP, textové pole a tlačítko 4 DIP. Jemný stín lze vykreslit jednou linkou navíc, není nutná knihovna efektů.

Tlačítko má minimálně 36 DIP na výšku a 88 DIP na šířku, vodorovný padding 16 DIP. Delší labely určují větší šířku měřením textu. Pole má minimálně 36 DIP na výšku. Klikací plocha checkboxu/switche zahrnuje label a má nejméně 32 DIP na výšku. Na malé ploše se mění rozložení, nezmenšuje se písmo ani tlačítko pod čitelnou velikost.

## UX02: Sdílené komponenty a rendering

Primární tlačítko je modré s bílým textem, sekundární bílé s rámečkem, textové má průhledné pozadí a modrý text. Reset je textová akce, která má vlastní potvrzení. Destruktivní potvrzovací akce používá červený text nebo červené vyplnění a slovní popis důsledku. Zakázaná akce nepřijímá myš, klávesnici ani programové vyvolání příkazu. Výchozí potvrzení destruktivní operace je vždy No nebo Cancel.

Editovatelné pole je bílé s rámečkem. Read-only pole má lehce tónované pozadí, ale dovoluje focus, označení a explicitní kopírování. Zakázané pole není totéž co read-only. Validní pole používá běžný rámeček, nevalidní červený rámeček, textovou chybu a přístupný stav invalid. Validace nemaže ani neopravuje vstup bez pokynu uživatele.

Checkbox v tabulce má standardní zaškrtávátko. Wizard používá vizuální switch se stejnou dvoustavovou boolean sémantikou. Implementace smí upravit kreslení nativního BUTTON, musí zachovat roli, checked/disabled stav a ovládání Space. Stav nesmí být pouze namalovaný obrázek bez přístupného ovládacího prvku.

Stavové ikony mají tvar i textový význam: zelené kolečko s fajfkou a Success/Completed, červené s křížkem a Failed, pending jantarový bod a Pending, budoucí Wizard krok šedé číslo a Upcoming. Aktivní Wizard krok má modré číslo a tónovaný podklad. Zdvojená čísla z některých obrázků se nereprodukují.

Tabulka má pevnou hlavičku, vodorovné oddělovače, bílé řádky a viditelný focus právě ovládaného prvku. Svislý scroll posouvá jen řádky. Horizontální scroll tabulky synchronně posouvá hlavičku i obsah. Editory dlouhých klíčů mají vlastní horizontální posun, takže na úzké ploše neztrácejí data. UI nesmí řešit overflow ořezáním přístupu k Actions nebo OK.

Informační a chybový panel mají nadpis, krátký popis a případnou nápravu. Nemají raw stack trace, HTML ani celé konfigurační dokumenty. Překreslení pokryje všechny nově odkryté plochy bez blikání a pruhů. Win32 implementace využije existující `window_utils.h`, nativní EDIT/BUTTON/scrollbar a omezené GDI kreslení karet. Fonty, štětce a pera mají vlastníka a uvolnění, nejsou nově vytvářeny pro každý řádek při každém paintu. Při chybě dávkového přesunu HWND zůstává funkční individuální přesun a úplná invalidace odkrytého rodiče.

Přenosné jsou barvy a DIP konstanty, textové katalogy, geometrie s předanými metrikami textu, modely stavů, validace, hashování přes CryptoProvider a schémata IPC. HWND, kreslení, font handles, message loop, systémový focus, accessibility adaptéry, modality a tray integrace se na macOS a Linuxu implementují nativně. Tato etapa jim neslibuje podporovaný build. Nezavádí Qt, Electron, WebView ani jiný rozsáhlý framework nebo novou knihovnu třetí strany.

## UX03: Focus, modalita, DPI a malá plocha

Všechna okna se otevřou na pracovní ploše aktivního monitoru, včetně záporných souřadnic dalšího monitoru, a jejich vnější rozměr se omezí na tuto plochu. Rozměry níže jsou výchozí klientské plochy při 96 DPI, po přičtení nativního rámu. Nejde o fyzické rozměry PNG.

| Okno | Výchozí DIP | Přednost při zmenšení |
|---|---|---|
| Connect | 940 × 680 | Svislý scroll celého obsahu, bez změny dat a výsledku. |
| Edit Config | 1080 × 560 | Nadpis/cesta a akce zachované, zmenšuje se editor. |
| Statistics | 1000 × 650 | Horní karty pod sebe, potom svislý scroll. |
| Wizard | 1000 × 680 | Sidebar 230 DIP, obsah scrolluje, patička zůstává. |
| Contracts | 1180 × 680 | Scroll tabulky, patička a vlastní IP dostupné. |
| Password SHA-256 | 540 × 360 | Pole po šířce, delší hash horizontálně posuvný. |

Na šířce pod 760 DIP přejdou Statistics na jeden sloupec. Wizard sidebar se při nedostatku šířky změní na kompaktní vodorovný seznam čtyř čísel s názvem aktuálního kroku. Není interaktivní navigací. Reset lze přesunout na vlastní řádek patičky, stejně tak delší primární akci. Výška patičky se odvodí od skutečných řádků tlačítek. Při 480 × 320 DIP musí být všechny akce dostupné, obsah může scrollovat. Je-li fyzická pracovní plocha ještě menší, clamp má přednost před minimem okna, použije se další zalomení a scroll, ne off-screen tlačítko. Nelze slíbit současné zobrazení všech sekcí na libovolně malé ploše.

Změna DPI znovu spočítá fonty, metriky, layout a scrollbar, zachová vstup, výběr textu, logický focus a viditelný řádek. Ověřit 100/125/150/200 %, přesun mezi monitory a opakovaný resize všech hran i rohů. Světlý klientský obsah odpovídá mockupům i při tmavém systémovém režimu, titulkový pruh smí být systémový. Samostatná tmavá paleta není požadavkem této etapy. High Contrast používá systémové barvy a viditelný focus místo vynucené palety. Běžný text má kontrast nejméně 4,5:1 a velký text i podstatné hranice ovládání nejméně 3:1.

Tab a Shift+Tab postupují vizuálním pořadím. Enter spouští povolenou primární akci, pokud focus nemá jiné tlačítko. V multiline editoru Enter vloží nový řádek. Escape odpovídá zrušení příslušného okna, nikoli Disconnectu. Viditelný focus má nejméně 2 DIP obrys. Přístupný název pole vychází z labelu, opakované Key/Random/Password SHA navíc z identity řádku. Čtečka obrazovky musí dostat názvy, role, hodnoty, stav a chybu také u kreslených prvků. Informační text nesmí být dostupný jen při hoveru.

Contracts je modální konfigurační formulář v rámci dané tray relace. Zablokuje místní konkurenční editaci/Wizard, opakované otevření aktivuje existující formulář bez čtení disku. Základní status polling, překreslení a nezávislý globální Disconnect z tray menu zůstávají funkční. Modalita nesmí spočívat v blokujícím čekání UI vlákna nebo globálním zákazu všech zpráv tray. Jiné uživatelské relace nejsou blokované.

Vnořený Password SHA či potvrzení Random zakáže vstup rodičovskému formuláři. Po zavření obnoví původní povolený stav a focus na vyvolávající akci stejného remoteIp, nikoli náhodný recyklovaný HWND. Tab neopustí vnořený dialog. Selhání vytvoření dítěte rodiče znovu zpřístupní. Ukončení tray nebo rodiče bezpečně uklidí dítě a citlivý koncept bez změny globální VPN.

## UX04: Menu a datové hranice

Pořadí menu zůstává: Connect/Disconnect, oddělovač, Configuration Wizard, Communication contracts, Edit Config, oddělovač, Show Info, Statistics, oddělovač, Exit. Mění se pouze velikost písmena v `contracts`, v souladu s titulkem mockupu. Start, otevření menu ani základní pětisekundový timer nečtou konfiguraci. Položky se nezakazují na základě starého výsledku dostupnosti souboru.

Connect a Show Info validují soubor ve službové obsluze akce. Statistics provede jedno `get-config-state`, potom používá pouze `get-status`. Contracts získá kontrolu i dokument jediným `get-config-document`, bez předřazeného `get-config-state`. Jen availability valid dovolí otevření modelu. Po Wizard Finish se použije dokument konkrétní potvrzené publikace z paměti služby, nikoli další čtení aktuálního disku. Podrobnosti určuje N02/N06 plánu.

| Zjištěný stav | Contracts | Edit Config | Wizard |
|---|---|---|---|
| valid | Otevřít editor. | Skutečný dokument. | Edit. |
| repairable | Bez editoru, nabídnout Wizard pro doplnění identity. | Skutečný text. | Repair. |
| invalid | Bez editoru, návod k opravě. | Skutečný platný UTF-8 text i při chybě JSON. | Nabídka Reset nebo opravy. |
| unreadable | Chyba přístupu bez náhrady. | Bez dokumentu. | Reset neobchází přístup. |
| encoding_error | Chyba UTF-8 bez náhrady. | Bez dokumentu. | Nabídka Reset. |
| too_large | Chyba limitu 512 KiB. | Bez dokumentu. | Nabídka Reset. |
| missing | Varování bez vytvoření, návod na Wizard/Edit Config. | Yes/No k vytvoření. | Create. |
| služba / IPC nedostupné | Chyba služby, nikoli missing. | Totéž. | Bez kontrol a Finish. |
| config_directory_security_invalid | Administrátorský postup níže, žádné opravné editování ACL. | Totéž. | Žádný Reset ACL ani Wizard jako náprava ACL. |

Bezpečnostní chyba adresáře má přednost před obecnou radou vytvořit konfiguraci. Uvede pouze autoritativní `configPath` ze služby a návod požádat administrátora o restart PHPVPN service. Zřetelně uvede, že restart přeruší VPN a může vytvořit chybějící default, potom doporučí ověřit skutečný soubor v Edit Config. Selhání načtení descriptoru je unreadable, nikoli důkaz neplatného ACL. Tray neopravuje ACL, nemaže žurnál a neprovádí restart.

URL/IP v Connect patří pouze do určených dvou polí. Nový oddělený paměťový IPC snímek je získá ze stejného logického spojení jako jeho status. `get-status`, Statistics, tooltip, obecné chyby a log nadále nesmějí obsahovat URL, IP konfiguraci, tajemství nebo cesty. Connect kvůli detailům nestahuje celý JSON. Edit Config a konfigurační pole Wizard/Contracts smějí zobrazit své určené hodnoty včetně klíčů. Výjimka neplatí pro chybové popisy nebo logování.

## UX05: Connect

Titul je `PHPVPN connection`. Svislá kompozice je `Connection status`, velký nadpis, stručný popis, bílá karta a výsledkový panel. Karta obsahuje `Connection details`, dva řádky `Server URL` a `Virtual IP`, oddělovač a `Connection checks`. Kontroly jsou přesně `Server URL`, `Server secret`, `Access key`, s ikonou a `Pending`, `Success` nebo `Failed`. Jsou to nové labely existujících `serverUrlCheck`, `serverSecretCheck`, `accessKeyCheck`, nikoli nová kontrola nebo zveřejnění hodnot secretů.

URL se zobrazí jako doslovná validovaná konfigurační hodnota bez automatického otevření browseru, IP jako kanonický text. Dlouhá URL musí být celá dostupná označením a horizontálním posunem read-only pole. Chybějící kontext se zobrazí `Unavailable`, nikdy ukázkovou URL/IP nebo konfigurací jiného spojení. Nesmějí se zobrazit SERVER_SECRET, ACCESS_KEY, payloadKey, tag, ciphertext, paket, interní cesta nebo raw síťová/systémová chyba.

Při zahájení je nadpis `Connecting`, popis `Checking the server and preparing the VPN connection.` a oranžové pending kontroly. Nativní ikona okna/tray zachová existující štíty a licence. Velký štít uvnitř obsahu se nahradí typografickou kompozicí mockupu. Trojice kontrol se přebírá jako celek, bez kumulace předchozích zelených bodů. Nová epocha znamená nový pokus, recovery historii bootstrapu nepřepisuje. Progress používá nezávislý 500ms paměťový polling podle N05 plánu. Neprovádí HTTP ani čtení konfigurace a nemění termíny základního pollingu.

Úspěch je přípustný až při `connected`, `running` a připraveném tunelu, což služba publikuje až po spuštění pracovních rolí. Nadpis zní `Successfully connected`, popis `The VPN connection is ready.`. Všechny kontroly jsou zelené. Spodní zelený panel má `Connected successfully`, pod ním `This window will close automatically.` a napravo číselný odpočet `Closing in 3 seconds`, potom `Closing in 2 seconds` a `Closing in 1 second`. Na úzké ploše se odpočet přesune pod text panelu.

Čas začíná prvním skutečným zobrazením úspěchu. Model používá monotónní deadline +3000 ms, zbývající sekundy jsou zaokrouhlené nahoru. Polling ani paint deadline neobnovují. Po vypršení zavřít při nejbližší obsluze UI, nejpozději o jeden 500ms tick později při nezablokovaném UI. Po sleep/resume již prošlý deadline nevytváří nový odpočet. Čtečka obrazovky ohlásí úspěch, nesmí být každým tickem zahlcena opakováním celé obrazovky.

Selhání má nadpis `Connection failed`, skutečný bezpečný popis ze služby a červený panel. Nadpis panelu odpovídá příčině podle tabulky. Text nápovědy je pevný katalog podle bezpečného kódu, ne interpretace jednotlivých kontrol a ne raw serverová odpověď.

| Kód příčiny | Popis | Panel / náprava |
|---|---|---|
| endpoint_unavailable_or_invalid | The server endpoint is not reachable or not valid. | Cannot connect to the server / Check the Server URL and network connection, then try again. |
| server_verification_failed | The server could not be verified with the configured SERVER_SECRET. | Cannot verify the server / Check the configured SERVER_SECRET and server settings. |
| access_key_rejected | The server rejected the configured ACCESS_KEY. | Cannot connect to the server / Check the configured ACCESS_KEY and try again. |
| tunnel_start_failed | The local tunnel could not be started. | Cannot start the VPN tunnel / Ask your administrator to check the PHPVPN service and tunnel setup. |
| communication_failed | Communication with the server could not be completed. | Cannot complete the connection / Check the network and server availability, then try again. |
| communication_timeout | The server did not respond in time. Verification could not be completed. | Connection timed out / Check the network and server availability, then try again. |
| chyba doručení / nepodporované progress IPC | The connection status is unavailable. | Cannot read connection status / Check that the PHPVPN service is running and supports this application. |

Endpoint selže v prvním bodě, ověření serveru ve druhém, access ve třetím. Při timeoutu selže právě prováděný bod. Pozdější neprovedené body zůstanou pending, při chybě tunelu mohou být všechny zelené. Chyba IPC nesmí vymyslet chybu žádného serverového bodu. Běžný provozní výpadek není automaticky tunnel_start_failed. Skutečné kódy i obnovitelnost nadále určuje služba. Timeout, 503 a neplatný MAC se nesmějí prezentovat jako odmítnutý klíč.

Konečný výsledek okna se uchová i během následné recovery, chybové okno se nikdy nezavře automaticky. Nový místní Connect resetuje prezentaci. X/Escape zavře pouze progress. Enter sám nic nepřipojí, neopakuje a neodpojí. Read-only detailní pole jsou v pořadí URL, IP a podporují kopírování. Cizí dokončený Disconnect zavře ještě neukončenou prezentaci podle dosavadního lifecycle. Žádný timer přeživší zánik okna nesmí měnit nové okno.

## UX06: Edit Config

Titul `PHPVPN configuration`, nadpis `Edit configuration`, pod ním skutečný `configPath` vrácený službou. Pevně zapsaná ukázka `C:\ProgramData\phpvpn\config.json` se nepoužije jako náhrada autoritativní cesty. Pod hlavičkou je jediný velký nativní multiline editor s vertikálním i horizontálním scrollbarem a bez word wrap. Patička má Reset vlevo, Cancel a modré Save vpravo.

Zůstává `EditorDocument`: nezměněný vstup pochází z původních bajtů, nikoli z CRLF prezentačního převodu Win32. Syntakticky vadný JSON v platném UTF-8 je opravitelný text. Jediný počáteční BOM se normalizuje podle `client.md`. Raw NUL použije výslovně označený režim `JSON string literal`, striktní dekódování a žádné zkrácení na prvním NUL. Otevření a Cancel nemění disk.

Save pošle celý text službě, která validuje UTF-8, JSON, všechny konfigurační invarianty, velikost a bezpečnou atomickou publikaci. Úspěch zavře editor a oznámí `The configuration was stored. It takes effect on the next connection attempt.`. Chyba zachová text, vrátí focus k editoru nebo známé chybě a neoznačí neurčitý zápis za neprovedený. Během ukládání není možný druhý Save nebo Reset. Ztracené potvrzení nesmí vést k automatickému přeposlání.

Reset použije Yes/No: `Restore the default configuration in this editor? The file will change only after you choose Save.`. Yes asynchronně načte `get-default-config`, teprve úspěch nahradí koncept a označí dirty. No, chyba nebo odpověď staré generace nemění text. Missing při otevření používá existující potvrzené `create-default-config`, které zapisuje ještě před otevřením editoru. Cancel již vytvořený soubor neodstraňuje.

Focus začíná v editoru. Tab přejde na Reset, Cancel, Save, Shift+Tab opačně. Enter v textu vloží nový řádek, Ctrl+A/C/V a běžné editační zkratky zůstávají. Escape/Cancel/X zavírají koncept podle dosavadního editorového chování. Resize zachová oba scrollbary, aktuální caret, hlavičku i tlačítka a nepřidá původní svislý artefakt vedle Cancel. Show Info, který dnes sdílí EditorWindow, zůstane read-only bez konfigurační cesty, Save nebo Reset.

## UX07: Statistics

Titul `PHPVPN Statistics`, nadpis `Statistics`, popis `Read-only global service counters for the current VPN connection.`. Slovo session z mockupu znamená globální logické spojení, nikoli Windows uživatelskou relaci. Použít tuto jednoznačnou adaptaci.

| Karta | Label | Pole |
|---|---|---|
| Data polling | Total polls | dataPollsTotal |
| Data polling | Empty polls | dataPollsEmpty |
| Data polling | Polls with payload | dataPollsWithPayload |
| Serialized data | Request body | serializedDataRequestBodyBytes |
| Serialized data | Response body | serializedDataResponseBodyBytes |
| IPv4 traffic, vlevo | Packets sent | ipv4PacketsSent |
| IPv4 traffic, vlevo | Bytes sent | ipv4BytesSent |
| IPv4 traffic, vpravo | Packets received | ipv4PacketsReceived |
| IPv4 traffic, vpravo | Bytes received | ipv4BytesReceived |

Technický název se zobrazuje menším mono písmem pod labelem, hodnota vpravo. Bajty se vypíší přesně v B, například `3,458 B`. Použít celočíselné anglické seskupení tisíců bez float převodu nebo zaokrouhlení na KiB. Zachovat přesnost až UINT64_MAX.

Každé překreslení vychází z jednoho úplného validního `get-status` snímku. Platí invariant pollingů, UI nic nesčítá ani nenuluje samo. Čekání bez snímku zobrazuje `Waiting for service status` a pomlčku místo hodnot. Chyba čtení zachová poslední celý snímek s viditelným `Service status unavailable. Showing the last received values.`. Další úspěšný snímek aktualizuje všechny hodnoty společně. Otevření dovoluje jednorázový paměťový odečet, pravidelně se používá existující pětisekundový timer bez posunu jeho termínů.

Neexistuje Reset counters, nové get-statistics, HTTP, diskový refresh, ukládání nebo tajné údaje. Tab dovolí označit/kopírovat hodnoty v pořadí tabulky, Escape/X zavře jen okno, Enter nemá mutující akci. Široká karta IPv4 traffic má dva sloupce s oddělovačem, na úzké ploše se rozloží svisle. Při resize nesmí label překrýt ani nejdelší čítač.

## UX08: Wizard, společná struktura a lifecycle

Titul `Configuration Wizard`, sidebar `Configure this computer` a čtyři kroky `Server URL`, `Server secret`, `IP and access key`, `Contracts`. Sidebar je informativní. Completed a zelená značka znamenají doručené platné ověření současných vstupů. Chyba je Failed, zbývající kroky Upcoming. Aktivní krok má modrou značku a tónované pozadí.

Patička: Reset vlevo, Back, Cancel a primární akce vpravo. V prvním URL kroku je Back viditelný, ale zakázaný, čímž se zachová nevratnost a vzhled mockupu. V odpojovací bráně Back není. Tato výslovná změna nahrazuje starý požadavek na úplnou nepřítomnost Back. Na nultý krok se nikdy nenaviguje zpět.

Kroky URL/secret/identity mají primární label `Verify & continue`, čtvrtý `Finish`. Výraz Next v interních modelech a algoritmech označuje právě jednu tuto akci. Reset, Cancel a Escape nezmění disk před Finish. Při kontrole jsou ověřovaná pole a opakovaná primární akce zakázané, Cancel/Reset bezpečně ruší vlastní práci. Enter spouští jen povolenou akci. Focus je na prvním editovatelném poli/switchi, po validaci na první chybě, po návratu zachová rozpracovaný text a výběr.

Zachovávají se create/edit/repair/reset, loading, checking, waitingDisconnect, interruptedByConnect, sessionExpired, committing, commitUnknown, completed a closed. Vypršení 15/60s pronájmu, 500ms čtení operace a epochy, konečný rozpočet pokusu, attemptSequence, clientAttemptId, windowGeneration a bezpečné rušení mají stejný význam jako nyní. Neexistuje automatický retry po 401, timeoutu, chybě IPC ani po ztraceném start acknowledgement. Pozdní úspěch neobnovuje uzavřený pokus. Základní tray timer zůstane nezávislý.

Katalog stručných provozních textů:

| Stav | Text / akce |
|---|---|
| loading | Loading configuration… |
| checking URL / secret / identity | Checking the server… / Verifying the server… / Checking access… |
| waitingDisconnect | Waiting for the VPN to stop… |
| saving | Saving configuration… |
| timeout | The check timed out. Your entries have been kept. Choose Verify & continue to start a new check. |
| sessionExpired | Contact with the service was lost and this setup session expired. Open the wizard again. / Open again |
| commitUnknown | The save result is unknown. The file may already have changed. Verify the result before saving again. / Verify save |
| interruptedByConnect | A VPN connection was started while setup was open. Your entries are kept, but the checks must be repeated. |

Při cizím Connect se porovnává epocha, i když mezitím proběhl Disconnect. Zachovat koncept, zneplatnit důkazy a vrátit odpojovací bránu. Je-li již disconnected/idle, nabídnout `Continue` bez dalšího zbytečného Disconnectu a začít kontroly od URL. Connect přijatý před commitem brání Finish, Connect po commitu načte novou konfiguraci. Wizard nesmí uzamknout Connect na celou dobu vyplňování.

## UX09: Jednotlivé kroky Wizardu

### Krok 0, wizzard0.png

Eyebrow `Before setup`, nadpis `Disconnect the VPN before continuing`, popis `Configuration requires the global VPN connection on this computer to be stopped.`. Karta má přesnou významovou adaptaci `The VPN will be disconnected for everyone.` a `This affects every user of this computer. The wizard will not reconnect it automatically.`. Množné číslo All VPN connections z obrázku neznamená odpojování cizích VPN produktů, phpvpn vlastní jedinou globální VPN.

Primární `Disconnect & continue` je výslovný souhlas s jedním globálním Disconnectem, bez dalšího duplicitního Yes/No. Reset a Cancel zůstávají dostupné, přestože obrázek Cancel neukazuje. Čekat na skutečné disconnected/idle, ne jen IPC acknowledgement. Při chybě zůstat v bráně s příčinou. Focus začíná na Cancel, aby náhodný Enter při otevření nepotvrdil globální odpojení.

### Krok 1, wizzard1.png

Nadpis `Server URL configuration`, popis `Set up this computer for your PHPVPN server. Changes are saved with Finish.`. V kartě label `Server URL`, pole, hint `Enter the complete address of the PHPVPN server script.`, tónovaný příklad `Example: https://vpn.example.com/phpvpn.php`, oddělovač a `HTTP and HTTPS are supported. The URL cannot contain a username, query, or fragment.` a `The endpoint will be checked before server authentication.`.

Nový/resetovaný koncept stále obsahuje `http://MODIFY-THIS-URL/phpvpn.php`, nikoli loopback opsaný z obrázku. Placeholder se odmítne před DNS s `Replace MODIFY-THIS-URL with your server address.`. Existující `parseServerUrl` nadále dovoluje jiný název skriptu. Kontrola je pouze syntaxe/dostupnost health, nezveřejní ověřený čas ani kryptografickou identitu. TLS politika zachovaného kontextu se nemění za účelem úspěchu kontroly.

### Krok 2, wizzard2.png

Nadpis `Server secret configuration`, popis `Use the secret configured on your PHPVPN server.`. V kartě label `Server secret (exact text, including any spaces)`, pole, `Enter the exact SERVER_SECRET value configured in phpvpn.php.`, switch `Use default server secret`, oddělovač a `The server response will be authenticated before continuing.`. Při defaultu navíc jediná stručná věta `The default secret is publicly known.`.

Switch je pod polem podle mockupu. True zobrazuje default v zakázaném poli, false doslovnou hodnotu. Přepnutí zapnuto nahrazuje hodnotu, vypnutí ji ponechá. `useDefaultServerSecretInWizard` má přednost před fallbackem. Žádný trim, změna velikosti písmen, normalizace Unicode nebo ztráta NUL. Při řídicích znacích zůstává zvláštní label `Server secret (JSON string literal)` a krátký hint `Keep the quotes. JSON escapes represent control characters.`. Kontrola provede nový ověřený health a info. Chyba zůstane na tomto kroku a zachová přesný vstup.

### Krok 3, wizzard3.png

Nadpis `Virtual IP and access key`. Popis: `Choose this computer's virtual IP inside {vpnCidr}. A suggested IP is not a reservation. Duplicate client IPs are allowed, but collisions are the operator's responsibility. A local contract cannot point to this computer itself.`.

Karta: `Virtual IP address`, pole, switch `Automatically detect IP address`, oddělovač, `Access key (64 lowercase hexadecimal characters)`, pole a switch `Use default access key`. Přemístění switchů pod pole mění layout, nikoli význam metadat. Automatická IP používá pouze platný ověřený návrh. Bez návrhu: `No verified IP suggestion is available. Turn off automatic detection to enter an IP, or go Back and verify the server again.`. Žádná rezervace ani aktualizace vzdálené mapy ACCESS_KEYS.

Při true se default accessKey okamžitě přepočítá při každé změně IP jako hex posledního oktetu opakovaný 32krát. Neplatná IP jej vyprázdní. Zapnutí přepíše vlastní hodnotu, vypnutí ponechá poslední vygenerovanou. Explicitní uložené příznaky mají přednost, fallback false pro existující platný klíč platí pouze při absenci příznaku. Validace self-kontraktu proběhne ještě před kontrolou access.

Pod kartou MUSÍ být vždy před akcí viditelný krátký panel, který v mockupu chybí: `Each Verify & continue check may permanently remove waiting messages for this IP, even if no result arrives. Messages are discarded, not delivered to the VPN. Checks are never retried automatically.`. Panel není schovaný v tooltipu nebo sbalené nápovědě. Primární akce je souhlasem s jediným pokusem. Zachovat úplnou diagnostickou validaci obálky a zahození payloadů bez GCM výhradně podle protocol.md 9.5.

Krátký hint u klíče: `Use the matching ACCESS_KEYS entry on the server. Ask your administrator if needed.`. Při defaultu doplnit `The default key is publicly predictable.`. Při 401 náprava odkazuje na nastavení této IP ve správě hostingu, ne na zdrojový PHP soubor údajně otevřitelný přes webovou URL. Timeout ani chybný MAC nejsou automaticky špatný klíč.

### Krok 4, wizzard4.png

Nadpis `Communication contracts`, popis `Review the default peer contracts that will be included in this configuration.`. Karta obsahuje switch `Add missing default communication contracts`, oddělovač a tři sloupce `Existing contracts`, `Missing defaults`, `To add on Finish`. Na malé ploše sloupce přejdou pod sebe.

Výpočty používají původní DOM a `.2` až `.15` pro podporované `10.A.B.0/24`, vyloučí self a existující peery. Kandidátů je 13/14, což se NESMÍ zaměnit s 252 řádky následného plného editoru. Nové otevření má volbu zapnutou, odškrtnutí nastaví doplnění na nulu bez odstranění původních kontraktů. Mimo podporovaný CIDR zůstane switch zaškrtnutý, zakázaný a efektivní request false.

Pod kartou stručně `Existing contracts and keys are kept. Default keys are publicly predictable.` nebo konkrétní vysvětlení nepodporovaného doplnění. Dále `Finish saves the configuration and opens Communication contracts. It does not connect the VPN.`. Finish provede úplnou validaci a jedinou atomickou publikaci bez dalšího data pollingu. Až confirmed receipt dovolí předat přesný publikovaný dokument plnému editoru. Chyba otevření editoru nemění úspěšný commit. Nejasný Finish používá Verify save a receipt, nikdy slepý opakovaný Finish.

## UX10: Contracts, model a tabulka

Titul i nadpis `Communication contracts`, popis `Manage secure communication between this computer and other virtual IP addresses.`. Vpravo nahoře label `Your virtual IP` a IP z načteného dokumentu. Při aktivní VPN jde o uloženou konfiguraci pro příští spojení, její identita nesmí být zaměněna za identitu současného tunelu.

Tabulka má sloupce `Contract`, `Enabled`, `Key`, `Actions`. Pro výchozí `/24` má právě 252 řádků, vzdálené `.2` až `.254` po vyloučení lokální IP. Řazení je vzestupně podle numerické hodnoty remoteIp. Self řádek nevzniká. Identita řádku je remoteIp, nikdy jeho pořadové číslo nebo HWND.

Contract zobrazuje `minNumericIp_maxNumericIp`. Například vlastní `.3` a vzdálená `.2` dávají `10.99.99.2_10.99.99.3`. Do JSON patří klíč `communicationContracts[remoteIp]`, nikoli tento popisný identifikátor. `.2` je numericky před `.10`, ne lexikograficky za ní.

Enabled je true právě pro existující validní objekt s payloadKey. Chybějící kontrakt je unchecked, ale jeho Key je editovatelný a předvyplněný. Nevalidní existující konfigurace se nesmí tiše převést na unchecked řádek, vůbec se neotevře jako validní model.

Počáteční šířky sloupců jsou 250 / 80 / pružná nejméně 260 / 220 DIP, padding buněk 12 DIP. Na výchozí ploše má Key většinu zbývající šířky. Header i spodní chyba a OK zůstávají mimo svisle posouvané řádky. Při nedostatku šířky se použije synchronní horizontální scroll tabulky. Vlastní IP a popis mohou přejít pod sebe.

Každý řádek má logický stav enabled, text klíče, validitu a změnu vůči originálu. Vytvářejí se pouze lehké hodnotové modely. Nativní ovládací prvky se vytvářejí pro viditelné řádky a nejvýše jeden rezervní řádek na každé straně. Recyklace nejprve uloží rozpracovaný text do správného remoteIp. Focus je dvojice remoteIp + sloupec/akce, zachovává se po scrollu, DPI a aktualizaci validace.

Tab order je po řádcích Enabled → Key → Random → Password SHA, potom další řádek a nakonec OK. Shift+Tab opačně. Při přechodu na neviditelný řádek se automaticky odkryje. Ctrl+End v navigaci tabulky přejde na poslední řádek, v textboxu zůstává textovou zkratkou. Wheel a PageUp/PageDown obsluhují tabulku, pokud není aktivní textová operace. Hromadná editace ani automatické přepnutí Enabled akcí Random/Password SHA nejsou součástí.

Jiné CIDR se odvozují přes `Ipv4Cidr::firstClientAddress`, `lastClientAddress`, `isValidClientAddress` a `clientAddressCount`, nikoli prefixem `10.99.99`. Runtime nyní přijímá kanonické `/16` až `/29` a vyžaduje rozpočty front. Cílový editor má samostatnou mez nejvýše 4092 řádků, tedy počet peerů `/20`. Je to konečná implementační mez UI, nikoli změna runtime nebo protokolu. `/19` má 8188 peerů a už pouhých 64 znaků každého klíče spotřebuje 524032 bajtů bez IP a JSON syntaxe, takže úplná zapnutá sada přesáhne limit 512 KiB. `/20` dovoluje plnou sadu s rozumnou rezervou pro běžný dokument.

Počet ověřit před alokací a generováním hashů. Větší rozsah odmítnout fail-closed: `This address range is too large for the contracts editor. Use Edit Config to manage this configuration.`. Nezobrazit jen první část kontraktů a nezkrátit seznam při publikaci. Také u menšího rozsahu se zvlášť kontroluje skutečná velikost kompletního výsledného dokumentu. Všechny runtime validační limity zůstávají povinné.

## UX11: Key, Random a Password SHA

Existující payloadKey se načte přesně. Chybějící se zobrazí pomocí jediné sdílené `defaultPayloadKey`:

```text
lowercase_hex(SHA-256(ASCII(minNumericIp + "_" + maxNumericIp)))
```

Jde o kanonické IPv4 texty, jeden ASCII znak `_`, žádný LF, NUL, whitespace ani další prefix. Stejná funkce je použita ve Wizardu i defaultu. Tento veřejně odvoditelný klíč se nesmí popsat jako náhodné tajemství. Disk ani druhý peer se otevřením/odvozením nemění.

Key musí být přesně `[0-9a-f]{64}`. Žádný automatický lowercase, trim nebo tiché zkrácení vložené hodnoty na 64 znaků. Pole musí umožnit ukázat chybu nadbytečného znaku. Kontrola má konečný vstupní limit odvozený od 512KiB rozpočtu konceptu, nadlimitní vložení se odmítne celé s vysvětlením, ne částečně.

Nevalidní aktivní řádek má červený textbox a patičku `Key must contain exactly 64 lowercase hexadecimal characters.`. OK je disabled. První blokující remoteIp je uvedena bez klíče a lze ji zaměřit. Oprava okamžitě obnoví platnost bez modálního dialogu. Nevalidní unchecked řádek smí mít stejnou lokální značku chyby, ale OK NEBLOKUJE, protože se nepublikuje. Jeho zaškrtnutí začne OK blokovat. Klíče unchecked řádků nejsou kopírovány do výsledného JSON.

Random vyvolá Yes/No s výchozím No: `Replace the key for {contractId} with a new random key?`. Yes zavolá `CryptoProvider::randomBytes` pro přesně 32 bajtů a převede je do 64 lowercase hex znaků. Použití běžného PRNG nebo časového seedu je zakázané. No/Escape/X nic nezmění. Chyba CSPRNG zachová starý klíč a zobrazí `A random key could not be generated. The current key has been kept.`. Změna zůstává jen v příslušném konceptu.

Password SHA otevře vnořený modal podle reference. Titul `Password SHA-256`, nadpis `Generate key from password`, popis `Enter a password to create a SHA-256 communication key.`. Následuje label `Password`, prázdné editovatelné pole s cue bannerem `CHANGE-THIS-PASSWORD`, hint `The key updates automatically as you type.`, label `SHA-256 key`, read-only hash a OK napravo v patičce. Běžný text hesla je viditelný jako v mockupu, nevytváří se nový povinný maskovací režim.

Hash je prostý SHA-256 přes přesné UTF-8 bajty aktuálního hesla. Není to heslový KDF, salt ani iterovaný hash. Neprovádí se trim, změna velikosti písmen nebo Unicode normalizace. Prázdné pole je platný vstup a zobrazí `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`. Placeholder není hodnota a nehashuje se. Při neplatném UTF-16 vstupu nebo chybě crypto zneplatnit výstup a zakázat OK, nikdy nepřenést předchozí hash jako aktuální.

Vstup je omezen na nejvýše 524288 UTF-8 bajtů a nesmí při převodu přetéct. Převod se dělá s explicitní délkou. Vložení NUL nebo znaku, který nativní jednorádkový textbox neumí bezeztrátově reprezentovat, se odmítne s viditelnou chybou, ne useknutím. Hash běžného Unicode včetně emoji je nutné ověřit proti nezávislým vektorům. Úplná kompozice a rozestupy zůstanou jako na mockupu, nikoli jen MessageBox s hashem.

Focus je v Password, Tab přejde na read-only hash a OK. Enter potvrdí jen aktuální platný hash. OK přenese hash do vybraného řádku, označí koncept dirty a zavře dialog, nezmění Enabled. Escape/X zavře bez změny řádku. Rodič se znovu zpřístupní a focus vrátí na Password SHA stejného kontraktu.

## UX12: Uložení, souběh a dirty-state kontraktů

Stavy modelu jsou loading, editing, invalid, saving, saveUnknown a closed. Dirty zachytí změnu Enabled, editaci klíče, Random i potvrzený Password SHA. U běžné editace se může vrátit do clean při obnovení všech původních hodnot, potvrzená generační akce však vyžádá potvrzení při zavření i u shodného výsledku. Otevření a prostý scroll dirty nenastaví.

OK během editing provede lokální kontrolu relevantních řádků, patch původního DOM, kontrolu velikosti a odešle právě jeden `set-config`. Pro enabled řádky vloží `{ "payloadKey": "…" }` pod remoteIp, unchecked odstraní. Zachová všechny nesouvisející členy, hodnoty, typy a jejich přítomnost, včetně vynechaného runtime, CIDR, TLS a metadat Wizardu. Výsledný dokument se nerekonstruuje z ClientConfig. Původně chybějící communicationContracts zůstane chybějící, pokud se nic nezapíná, existující objekt po vypnutí všeho zůstane `{}`. Whitespace lze při DOM editaci změnit.

Přísné současné schéma nepovoluje libovolné neznámé členy. Zachování DOM není povolením neznámých nebo nevalidních polí. Úplnou validaci opakuje služba před temp souborem, všechny publikace používají jediný ConfigStore a serializační bránu. Úspěch zavře okno. Neprovede Connect, Disconnect ani reload aktivního snímku. Změna se použije až při novém logickém připojení, nikoli při recovery současného.

Při aktivní VPN zobrazit `Changes take effect on the next connection.`. Souběžné relace zapisují celé dokumenty, poslední úspěšná publikace vyhraje. Starší revisionToken není konflikt a nevyvolává CAS, automatické merge, refresh ani nové potvrzení přepsání. Změna nebo delete/recreate cíle během otevření nezmění koncept. Pokud je publikace bezpečná, OK může obnovit smazaný soubor nebo nahradit mezitím změněný obsah, stejně jako Save editoru. Nepřebírá části novější konfigurace.

Při potvrzeném odmítnutí před publikací se vrátí editing/invalid s celým konceptem a bezpečnou příčinou. Chyba write/flush/publish nebo ztracená, chybná či nekorelovaná odpověď po možném odeslání je saveUnknown, pokud není prokázán konečný nepublikovaný výsledek. V saving se pole, OK i zavření uzamknou pouze po konečný rozpočet IPC. Po jeho uplynutí se zobrazí `The save result is unknown. The file may already have changed. Check the current configuration before saving again.`.

Stávající set-config nemá dohledatelný commit receipt. Editor jej proto nesmí vymýšlet z requestId nebo aktuální revize. V saveUnknown nabídne `Check current configuration`, což je výslovná jednorázová akce `get-config-document`. Porovná celý DOM aktuálního validního dokumentu s odeslaným kandidátem, včetně typů a přítomnosti členů. Shoda dovolí oznámit `The current configuration matches your changes.` a zavřít, není historickým potvrzením původního zápisu. Neshoda nebo missing zachová koncept, zobrazí `The current configuration differs. Your draft has been kept.` a nabídne buď zavření, nebo výslovné `Save draft again` s potvrzením nahrazení současného celého dokumentu. Tato výjimka platí jen pro řešení unknown, nikoli běžné souběžné ukládání. Nejde o automatický retry. Nečitelnost/ACL/encoding_error/too_large se nejprve řeší podle UX04, nepovolují slepou novou publikaci.

X/Escape v clean zavře přímo. V dirty zobrazí `Discard unsaved changes?`, Yes je zahodí, No/Escape/X potvrzení se vrátí. V saveUnknown je text `The file may already have changed. Close without checking the save result?`, výchozí No. Zavření nevrací již publikovaný soubor. Pozdní odpověď po zavření nebo nové generaci neotevře dialog a neprovede další zápis.

## UX13: Bezpečnost a limity

Tray nikdy neotevře, nezapíše ani netestuje existenci `config.json` přes filesystem. Dočasný JSON, heslo, klíče a DOM jsou pouze v paměti do schválené službové publikace. Všechny logy, chyby, testové reporty a screenshoty skutečných konfigurací musí být bez tajemství. Testy používají výhradně syntetické hodnoty. UI samo nic nekopíruje do clipboardu.

Citlivé nativní buffery se mažou přes `secureWipe`/SecureBuffer před uvolněním, také při chybě crypto a zrušení. Pracovní UTF-8/UTF-16 řetězce, přijatá IPC těla, původní DOM i kandidát mají omezený životní cyklus a co nejméně kopií. Před zánikem pole vyprázdnit jeho obsah i undo buffer. Best-effort mazání vlastněné paměti není záruka odstranění kopií v OS, alokátoru nebo uživatelem vytvořeném clipboardu. Heslo nikdy nejde přes IPC ani do konfigurace, pouze výsledný klíč v potvrzeném JSON.

Limit uložené konfigurace je 524288 bajtů a zakódované IPC zprávy 4194304 bajtů včetně escapingu a obálky. Kontrola proběhne před alokací podle nedůvěryhodné délky i před odesláním/publikací. Existující validátor, UTF-8 a JSON hloubkové/tokenové meze se neoslabují. Editor má nejvýše 4092 lehkých řádků a součet rozpracovaných Key textů nejvýše 524288 bajtů. Překročení limitu odmítne novou editaci s vysvětlením a zachová dosavadní koncept. Neexistuje neomezená historie klíčů nebo bufferů.

Veřejná lokální pipe, zákaz vzdálených klientů, uzavřené schéma, korelace, konečné deadline a oddělená kapacita Disconnect/get-status zůstávají. Nepřidává se obecný IPC příkaz pro crypto, souborovou cestu nebo HTTP. SHA-256 a CSPRNG jsou lokální volání stávající kryptografické abstrakce. Konfigurační volby nemění wire formát, AAD, HKDF, IV ani end-to-end kontrolu.

## UX14: Ověření a velikost

Vizuální akceptace porovná všech jedenáct mockupů s cílovými okny pomocí syntetických hodnot při stejném měřítku klientského obsahu. Hodnotí hierarchii, pořadí sekcí, barvy, typografii, rozestupy, tabulku, umístění akcí a čitelnost. Odchylky povolené UX00, UX03, UX08 a UX09 se uvedou v reportu. Samotný screenshot ani unit test geometrie nedokládají funkční klávesnici, modalitu či bezpečnou publikaci.

Měřit počet a velikost změněných zdrojů a prostředků i výslednou `phpvpn-tray.exe` vůči výchozí binárce. Plán a následná implementační zpráva musí uvést velikost před změnou, po změně, absolutní rozdíl v bajtech a procentní rozdíl `100 × (po − před) / před`. Porovnat také stejnou build konfiguraci, architekturu a stav podepsání. PDB se nepočítá do EXE. Nedefinuje se nepodložený číselný limit růstu, násobné zvětšení kvůli samotné vizuální vrstvě není přijatelné a musí vést k úpravě implementace.

Budoucí rychlé ověření a kopie pouze tray EXE se řídí závěrem plánu. Neznamenají plnou release kvalitu. V této dokumentační fázi se klient nebuildí, testy GUI se nespouštějí a binárky se nikam nekopírují.
