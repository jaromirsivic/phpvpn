# 012 - Bezpečnostní upozornění ve Wizardu a zpráva po dokončení

## Cíl

Zpřesnit texty a podmíněná bezpečnostní upozornění v konfiguračním Wizardu tray aplikace tak, aby:

- krok konfigurace `SERVER_SECRET` srozumitelně vysvětloval účel secretu a výrazně upozornil pouze na použití veřejně známé výchozí hodnoty,
- krok konfigurace Access Key odstranil technické upozornění o diagnostickém odebrání čekajících zpráv, které není pro běžného uživatele přínosné,
- tentýž krok místo něj zobrazil srozumitelné bezpečnostní upozornění pouze při vědomém použití výchozího Access Key,
- dialog po úspěšném dokončení Wizardu uživatele přímo navedl na tlačítko `Connect`.

## Rozsah

Úprava zahrnuje pouze:

- informační text a podmíněný žlutý panel v kroku konfigurace `SERVER_SECRET`,
- odstranění dosavadního žlutého diagnostického upozornění v kroku konfigurace Access Key,
- odstranění dosavadní stručné věty o předvídatelném výchozím Access Key z bílého panelu,
- nový podmíněný žlutý panel pro výchozí Access Key,
- text informačního dialogu po úspěšném stisknutí `Finish`,
- nezbytné úpravy layoutu, viditelnosti a překreslování přímo dotčených prvků.

Neměnit ikony informačních ani jiných dialogů. Vzhled nevyhovující ikony zachycené uživatelem je výslovně odložen do dalšího mikro-updatu a není součástí mikro-updatu 012.

## Krok konfigurace SERVER_SECRET

### Informační text v bílém panelu

- Odstranit z bílé oblasti dosavadní stručnou větu:

  `The default secret is publicly known.`

- Dosavadní obecnou větu:

  `The server response will be authenticated before continuing.`

  nahradit přesným zněním:

  `This secret is used to authenticate the server response before continuing.`

- Nová věta zůstane v bílé oblasti pod ovládáním secretu. Má vysvětlovat účel zadávané hodnoty, nikoli fungovat jako varování.

### Žlutý panel pro výchozí server secret

- Pod bílým panelem zobrazovat samostatný žlutý varovný panel.
- Panel zobrazit, pokud platí alespoň jedna z těchto podmínek:
  - checkbox `Use default server secret` je zaškrtnutý,
  - aktuální hodnota server secretu je přesně `CHANGE_THIS_SERVER_SECRET`.
- V ostatních případech panel skrýt a layout plynule stáhnout bez prázdného místa.
- Panel musí obsahovat tento nadpis:

  `Default server secret in use`

- Panel musí obsahovat přesně tento text:

  `The default server secret is publicly known and may allow an attacker to impersonate the server. This configuration is potentially unsafe. It is strongly recommended that the SERVER_SECRET value on the server be changed to a unique, strong secret before using this connection.`

- Název proměnné `SERVER_SECRET` musí být ve výsledném GUI čitelný jako přesný technický název. Způsob typografického zvýraznění přizpůsobit možnostem stávající komponenty a vizuálnímu stylu aplikace.
- Viditelnost panelu aktualizovat okamžitě při změně checkboxu i při změně relevantní hodnoty secretu.

## Krok konfigurace Access Key

### Odstranění dosavadního diagnostického upozornění

- Úplně odstranit dosavadní žluté upozornění začínající textem:

  `Each Verify & continue check may permanently remove waiting messages for this IP...`

- Toto upozornění již ve Wizardu nezobrazovat, a to ani před spuštěním ověření, během něj, po neúspěchu nebo při návratu na krok.
- Neměnit samotné diagnostické chování, síťovou komunikaci, počet pokusů ani zpracování čekajících zpráv. Jde pouze o změnu GUI a textu.
- Současná normativní dokumentace může toto předběžné informování stále vyžadovat. V rámci mikro-updatu 012 ji neupravovat. Sjednocení dokumentace s novým UX rozhodnutím je výslovně odloženo do pozdější souhrnné fáze.

### Bílý panel

- Z bílé oblasti odstranit dosavadní stručnou větu:

  `The default key is publicly predictable.`

- Tuto větu v bílé oblasti ničím nenahrazovat. Ostatní relevantní obsah bílé oblasti zachovat, pokud není v tomto zadání uvedeno jinak.

### Žlutý panel pro výchozí Access Key

- V prostoru po odstraněném diagnostickém upozornění použít žlutý panel pro bezpečnostní informaci o výchozím Access Key.
- Panel zobrazit výhradně tehdy, když je zaškrtnutý checkbox `Use default access key`.
- Viditelnost neodvozovat z textové nebo binární hodnoty samotného Access Key. Rozhodující je pouze stav checkboxu.
- Při odškrtnutí checkboxu panel okamžitě skrýt a layout stáhnout bez prázdného místa.
- Panel musí obsahovat tento nadpis:

  `Default access key in use`

- Panel musí obsahovat přesně tento text:

  `The default access key is publicly predictable and should not be considered private. Its use may be acceptable in a trusted network where all participating peers are known and trusted. Otherwise, it is recommended that a unique access key be requested from the server administrator.`

## Dialog po úspěšném dokončení Wizardu

- Po úspěšném stisknutí `Finish` zachovat stávající informační dialog s titulkem:

  `Configuration saved`

- Dosavadní text:

  `The configuration was saved. This wizard did not connect the VPN.`

  nahradit přesným zněním:

  `The configuration was saved. To connect to the VPN server, click Connect.`

- Zachovat stávající ovládání dialogu, tlačítko `OK`, modalitu a následný aplikační workflow.
- Neměnit ikonu tohoto dialogu ani ikony ostatních dialogů.

## Vizuální a interakční požadavky

- Nové žluté panely mají navázat na existující vizuální styl varovných panelů tray aplikace.
- Text musí být celý čitelný, správně zalomený a bez ořezu při podporovaném DPI a dostupných velikostech Wizardu.
- Nadpis a tělo panelu musí mít dostatečný kontrast vůči žlutému pozadí.
- Zobrazení ani skrytí panelu nesmí zanechat prázdnou mezeru, překryv, zbytkové pixely nebo nesprávnou pozici dalších prvků.
- Přepínání checkboxů musí aktualizovat panel bez blikání a bez nutnosti přejít na jiný krok.
- Zachovat dosavadní tab order a ovládání klávesnicí, pokud odstranění prvku nevyžaduje jeho přirozené zkrácení.

## Zachování ostatního chování

- Neměnit význam, ukládání ani normalizaci `SERVER_SECRET`.
- Neměnit výpočet, ukládání ani použití Access Key.
- Neměnit podmínky úspěchu jednotlivých ověřovacích kroků.
- Neměnit diagnostický protokol, síťové požadavky, timeouty, retry pravidla ani práci serverových front.
- Neměnit ukládání konfigurace ani chování tlačítek `Back`, `Verify & continue`, `Finish`, `Connect` a `OK` nad rámec výslovně uvedených textových a prezentačních změn.
- Neměnit jiné kroky Wizardu, jiné dialogy ani ostatní části tray aplikace.
- Neupravovat běžnou projektovou dokumentaci.

## Akceptační kritéria

1. V kroku `SERVER_SECRET` již bílá oblast neobsahuje větu `The default secret is publicly known.`.
2. Bílá oblast v tomto kroku obsahuje přesný text `This secret is used to authenticate the server response before continuing.`.
3. Žlutý panel server secretu se zobrazí při zaškrtnutém `Use default server secret` nebo při přesné hodnotě `CHANGE_THIS_SERVER_SECRET`.
4. Žlutý panel server secretu je ve všech ostatních případech skrytý.
5. Panel obsahuje schválený nadpis a celý schválený formální text bez ořezu.
6. V kroku Access Key se již nikde nezobrazuje dosavadní upozornění o možném odebrání čekajících zpráv.
7. Z bílé oblasti kroku Access Key zmizela věta `The default key is publicly predictable.`.
8. Nový žlutý panel Access Key se zobrazí právě tehdy, když je zaškrtnutý `Use default access key`.
9. Skutečný obsah pole Access Key sám o sobě viditelnost panelu nemění.
10. Panel Access Key obsahuje schválený nadpis a celý schválený formální text bez ořezu.
11. Po skrytí kteréhokoli panelu nezůstane v layoutu prázdné místo.
12. Úspěšné `Finish` zobrazí přesný text `The configuration was saved. To connect to the VPN server, click Connect.`.
13. Ikona dialogu i všechny ostatní ikony zůstávají beze změny.
14. Funkční chování konfigurace, ověřování a síťové komunikace zůstává beze změny.

## Omezení implementace a ověření

- Neprovádět komplexní testy.
- Nevytvářet nové testy.
- Existující test upravit pouze tehdy, pokud je potřebná změna malá, přímočará a rychlá. Rozsáhlejší úpravy testů odložit.
- Neupravovat běžnou projektovou dokumentaci, včetně `protocol.md`, `client.md`, `tray_UX.md` a plánovacích dokumentů.
- Po implementaci zkompilovat tray aplikaci, vyřešit chyby způsobené změnou a zkopírovat výslednou sestavu do distribuce pro ruční instalaci a ověření uživatelem.
- Neprovádět commit ani push, pokud o to uživatel výslovně nepožádá.

## Hranice formalizační fáze

Tento soubor vytvořila hlavní koordinátorka před založením implementačního úkolu. Implementující agent smí pracovat až podle tohoto zadání. Vzhled dialogových ikon není součástí této práce.
