# 016 - Přehlednější Statistics, součty a dynamické jednotky

## Cíl

Zpřehlednit okno **Statistics** tak, aby pořadí metrik odpovídalo jejich významu, technické názvy interních proměnných nerušily uživatelskou prezentaci, velikosti dat se zobrazovaly v automaticky zvolené čitelné jednotce a nadpis zároveň ukazoval, zda je VPN připojená.

Úprava je čistě prezentační nad existujícími globálními čítači a stavem vraceným současným `get-status`. Nezavádí nový IPC příkaz, nový serverový čítač ani změnu významu existujících hodnot.

## Rozsah

Mikro-update zahrnuje pouze okno **Statistics** a přímo související prezentační model a layout:

- změnu pořadí tří řádků v panelu **Data polling**,
- přidání vypočteného řádku `Total size` do panelu **Serialized data**,
- odstranění viditelných technických názvů proměnných ze všech statistických řádků,
- dynamické formátování všech bajtových hodnot na stránce Statistics,
- barevný stav `(Connected)` nebo `(Disconnected)` vedle nadpisu `Statistics`,
- nezbytné přepočty layoutu, oddělovačů, scrollování, tab orderu, resize a DPI chování.

Neměnit službu, strukturu `get-status`, zdrojové čítače, frekvenci pollingu ani síťové chování.

## Data polling

V prvním panelu změnit pořadí řádků na:

1. `Empty polls`
2. `Polls with payload`
3. `Total polls`

`Total polls` se pouze přesune z prvního na poslední řádek. Hodnoty, jejich zdroj a invariant `Total polls = Empty polls + Polls with payload` zůstávají beze změny.

Vizuální pořadí, pořadí fokusu a pořadí při čtení ovládacími technologiemi musí odpovídat tomuto novému pořadí.

## Serialized data

Panel zachová dosavadní první dva řádky a rozšíří se na toto pořadí:

1. `Request body`
2. `Response body`
3. `Total size`

`Total size` je pouze vypočtená prezentační hodnota:

`serializedDataRequestBodyBytes + serializedDataResponseBodyBytes`

- Součet se nesmí přidávat do IPC ani do `TrafficCountersView` jako nový autoritativní čítač.
- Musí vždy vycházet ze stejného přijatého snapshotu jako oba dílčí řádky.
- Nesmí při sčítání dvou 64bitových hodnot tiše přetéct nebo se zobrazit jako menší hodnota. Implementace má zachovat matematický součet alespoň pro celý rozsah dvou nezáporných 64bitových vstupů, případně použít bezpečnou reprezentaci určenou přímo pro formátování.
- Pokud zatím neexistuje žádný platný snapshot, zobrazí se stejně jako u ostatních hodnot `-`.
- Při dočasné nedostupnosti služby se zachová poslední úplný snapshot i vypočtený součet podle současného pravidla okna Statistics.

## Odstranění technických názvů proměnných

Ze všech řádků ve všech třech panelech odstranit viditelné technické klíče, například:

- `dataPollsTotal`,
- `dataPollsEmpty`,
- `serializedDataRequestBodyBytes`,
- `ipv4BytesReceived`.

U každého řádku zůstane pouze uživatelský anglický label a jeho hodnota. Technický klíč nesmí zůstat jako samostatný řádek, read-only edit, skrytý prázdný prostor ani text v závorce.

Odstranit také zbytečné vytváření, fontování, měření, pozicování, focusování a lifecycle správu ovládacích prvků určených výhradně pro tyto technické klíče. Interní názvy polí mohou samozřejmě zůstat v kódu a parseru.

Po odstranění klíčů upravit výšku řádků a karet. Nesmí po nich zůstat nadměrné mezery. Label, hodnota a horizontální oddělovač musí tvořit kompaktní a vyvážený řádek.

## Dynamické formátování velikostí

Pravidlo platí pro všechny bajtové hodnoty zobrazené v okně Statistics:

- `Request body`,
- `Response body`,
- nový `Total size`,
- `Bytes sent`,
- `Bytes received`.

Použít desetinné jednotky se základem 1000, nikoli binární jednotky se základem 1024.

### Jednotky a prahy

- `B` pro hodnoty od 0 do 999 bajtů,
- `kB` od 1 000 bajtů,
- `MB` od 1 000 kB,
- `GB` od 1 000 MB,
- `TB` od 1 000 GB,
- `PB` od 1 000 TB.

Při dosažení hodnoty 1000 v aktuální jednotce přejít na následující jednotku. `PB` je nejvyšší požadovaná jednotka. Případná ještě větší hodnota zůstane vyjádřena v PB a nesmí přetéct, změnit znaménko ani se vrátit do nižší jednotky.

### Přesnost a textový formát

- Hodnoty v `B` zobrazovat jako celé číslo bez desetinných míst, například `999 B`.
- Všechny hodnoty od `kB` do `PB` zobrazovat vždy přesně na dvě desetinná místa, například `1.00 kB`, `12.35 MB` nebo `4.20 GB`.
- Použít desetinnou tečku, protože celé GUI a názvy jednotek jsou v angličtině.
- Hodnotu zaokrouhlit běžným způsobem na dvě desetinná místa.
- Výsledek po zaokrouhlení nesmí zůstat jako `1000.00` nižší jednotky. Pokud zaokrouhlení překročí hranici, zobrazit hodnotu v následující jednotce, kromě nejvyššího `PB`.
- Nepoužívat vědecký zápis.
- Nebajtové čítače nadále zobrazovat jako celá čísla s dosavadním anglickým oddělováním tisíců.

Příklady očekávaného formátu:

| Bajty | Zobrazení |
|---:|---:|
| 0 | `0 B` |
| 999 | `999 B` |
| 1 000 | `1.00 kB` |
| 1 500 | `1.50 kB` |
| 1 000 000 | `1.00 MB` |
| 1 234 567 | `1.23 MB` |
| 1 000 000 000 | `1.00 GB` |

Formátování musí být deterministické a nezávislé na regionálním nastavení Windows.

## Stav připojení v nadpisu

Vedle hlavního nadpisu `Statistics` zobrazovat barevný stav v závorce:

- zelené `(Connected)`, pouze pokud poslední použitý platný `TrayStatus.state` je přesně `connected`,
- červené `(Disconnected)` pro `disconnected`, `connecting`, `recovering`, `error`, počáteční stav bez platného snapshotu a každý jiný stav, který není autoritativně `connected`.

Použít existující barevné role GUI `success` a `error`. Nevkládat barevný stav jako součást jednoho obyčejného textu, pokud by tím nešlo zachovat odlišnou barvu nadpisu a stavu. Nadpis `Statistics` zůstává ve své současné barvě a typografii, stav je umístěný bezprostředně vedle něj s přirozenou mezerou a odpovídající vertikální osou.

Stav se aktualizuje při otevření okna i při každém novém snapshotu stejnou cestou jako čítače. Pokud služba dočasně přestane odpovídat a okno zachovává poslední platný snapshot, zachovat také stav odvozený z tohoto snapshotu. Existující text o nedostupnosti služby nadále upozorňuje, že jsou zobrazené poslední přijaté hodnoty.

Změna stavu nesmí způsobit překrytí, blikání, zbytky starého textu ani posun celého obsahu. Musí fungovat při resize a všech podporovaných DPI.

## Layout, ovládání a přístupnost

- Panel Data polling má nadále tři řádky, pouze v novém pořadí.
- Panel Serialized data má nově tři řádky.
- Panel IPv4 traffic zachová čtyři dosavadní řádky a jejich pořadí.
- Celkem bude zobrazeno deset hodnotových řádků.
- Přepočítat všechny pevné velikosti polí, indexy, pole HWND, metriky layoutu, oddělovače a scroll reveal logiku tak, aby odpovídaly deseti řádkům.
- Na širokém layoutu zůstávají Data polling a Serialized data vedle sebe. Jejich karty mají po přidání třetího řádku přirozeně stejnou výšku.
- Na úzkém layoutu se karty nadále skládají svisle a všechny hodnoty zůstanou dostupné scrollováním.
- Horizontální oddělovače musí být mezi sousedními řádky, nikoli za posledním řádkem karty. Vertikální oddělení sloupců IPv4 traffic musí zůstat správně zarovnané.
- Hodnoty zůstávají read-only, označitelné a kopírovatelné. Tab pořadí musí sledovat nové vizuální pořadí všech deseti hodnot.
- Minimální klientská velikost 640 × 480 DIP zavedená předchozím mikro-updatem zůstává zachovaná.
- Resize, změna DPI, změna systémových barev, získání fokusu a scrollování nesmějí zanechávat grafické artefakty.

## Zachování ostatního chování

- Neměnit obsah, validaci ani bezpečnostní pravidla `get-status`.
- Neměnit devět autoritativních čítačů ve službě a IPC.
- Neměnit pětisekundový tray polling ani zavádět vlastní timer pro Statistics.
- Neměnit pravidlo, že platný snapshot aktualizuje všechny hodnoty společně a neplatný snapshot nepřepíše poslední platná data.
- Neměnit texty `Waiting for service status` a `Service status unavailable. Showing the last received values.`.
- Neměnit tray menu, ostatní okna, VPN lifecycle, službu ani síťový protokol.
- Neupravovat běžnou projektovou dokumentaci.

## Akceptační kritéria

1. Data polling zobrazuje přesné pořadí `Empty polls`, `Polls with payload`, `Total polls`.
2. Serialized data zobrazuje `Request body`, `Response body`, `Total size`.
3. Total size je bezpečný součet request a response body ze stejného snapshotu a není novým IPC čítačem.
4. Žádný statistický řádek nezobrazuje technický název interní proměnné ani po něm nezanechává prázdné místo.
5. Všechny bajtové řádky používají desetinný základ 1000 a jednotky B, kB, MB, GB, TB a PB.
6. B se zobrazuje bez desetinných míst a kB až PB vždy přesně se dvěma desetinnými místy.
7. Přechod na vyšší jednotku nastává při dosažení 1000 a formátování je deterministické bez vědeckého zápisu.
8. Nebajtové čítače zachovávají dosavadní celočíselný formát.
9. Vedle Statistics je zelené `(Connected)` pouze pro stav connected, jinak červené `(Disconnected)`.
10. Barevný stav se aktualizuje při nových snapshotech a respektuje zachování posledního platného snapshotu při nedostupnosti služby.
11. Nadpis, stav, labely, hodnoty a oddělovače se nepřekrývají při širokém ani úzkém layoutu a při podporovaných DPI.
12. Všech deset hodnot zůstává dostupných, read-only, označitelných, kopírovatelných a dosažitelných klávesou Tab i scrollováním.
13. Stávající minimum 640 × 480 DIP, refresh, stale stav a ostatní chování Statistics zůstávají funkční.
14. Nedojde ke změně IPC, služby, zdrojových čítačů, polling intervalu, VPN lifecycle ani ostatních oken.
15. Tray aplikace se po změně úspěšně zkompiluje a aktuální sestava je zkopírována do distribuce pro ruční instalaci.

## Omezení implementace a ověření

- Neprovádět komplexní testy.
- Nevytvářet nové testy.
- Existující test upravit pouze tehdy, pokud je potřebná změna malá, přímočará a rychlá. Rozsáhlejší úpravy testů odložit.
- Neupravovat běžnou projektovou dokumentaci, včetně `protocol.md`, `client.md`, `tray_UX.md` a plánovacích dokumentů.
- Po implementaci provést pouze přiměřené statické a cílené ověření, zkompilovat tray aplikaci a vyřešit chyby způsobené změnou.
- Znovu vytvořit a ověřit distribuci podporovaným projektovým postupem. Finální instalační vstup musí být připravený pro ruční instalaci a vizuální kontrolu uživatelem.
- Neprovádět skutečnou instalaci, odinstalaci, změny služby, ProgramData nebo VPN bez výslovného požadavku uživatele.
- Neprovádět commit ani push, pokud o to uživatel výslovně nepožádá.

## Hranice formalizační fáze

Tento soubor vytvořila hlavní koordinátorka osobně před založením implementačního úkolu. Vytvoření ani zápis formálního zadání nebyly delegovány na jiného agenta.

Uživatel nejprve zvolil model Sol a effort Medium, následně tuto volbu výslovně změnil na model Sol a effort High. Implementace smí být předána samostatnému projektovému úkolu až po dokončení tohoto zadání.
