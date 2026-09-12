---
intent_version: 1
created_at: 2026-09-12-02-29-09
source: intent-chat-v001.md
---

# Konsolidace implementovaných změn do hlavních specifikací klienta

## Shrnutí

Provést řízenou konsolidaci historických klientských plánů a časově řazených poznámek do tří dlouhodobě udržovaných cílových dokumentů. Každé tvrzení ze zdrojového dokumentu se nejprve porovná se skutečnou implementací. Do cílových dokumentů se doplní pouze chování, které je skutečně implementované a současně má oporu v některém výslovně uvedeném zdrojovém dokumentu.

Výsledkem mají být úplné a aktuální cílové specifikace, díky kterým budou uvedené plány a poznámky obsahově nadbytečné. Samotné odstranění zdrojových souborů není součástí tohoto záměru.

## Projektový kontext

- Projekt: `phpvpn`
- Primární cílový dokument: `client.md`
- Další cílové dokumenty: `protocol.md`, `tray_UX.md`
- Skutečný název UX souboru v projektu používá velká písmena `UX`: `tray_UX.md`.
- Zdroj pravdy pro skutečně dodané chování: aktuální implementace a její relevantní testy.
- Zdroj pravdy pro rozsah této konsolidace: pouze níže vyjmenované plány a soubory `_notes.md`.

## Cíl

1. Zjistit aktuální obsah a rozdělení odpovědností mezi `client.md`, `protocol.md` a `tray_UX.md`.
2. Postupně projít každý zdrojový dokument v závazném pořadí.
3. U každého věcného požadavku nebo popisu změny dohledat, zda a v jaké podobě byl skutečně implementován.
4. Porovnat ověřenou implementaci s cílovými dokumenty.
5. Chybějící nebo zastaralý popis upravit v odpovídajícím cílovém dokumentu.
6. Zachovat významové rozdělení dokumentace: obecný klient a služba v `client.md`, wire protokol a interoperabilní pravidla v `protocol.md`, specificky tray UI a UX v `tray_UX.md`.
7. Dosáhnout stavu, kdy všechny ověřené a implementované skutečnosti z uvedených zdrojů jsou beze ztráty obsažené v cílových dokumentech.

## Závazné pořadí zdrojů

Před zpracováním zdrojů se načtou tři cílové dokumenty. Zdroje se potom vyhodnocují přesně v tomto pořadí:

1. `plan_client.md`
2. `plan2_client.md`
3. `plan3_client.md`
4. `plan3_fix.md`
5. `plan3_fix2.md`
6. `plan3_fix3.md`
7. `plan3_fix3_problems.md`
8. `plan3_fix3_problems2.md`
9. `plan3_fix3_work_for_agents.md`
10. `notes/2026-09-04-18-59-01_notes.md`
11. `notes/2026-09-04-22-20-52_notes.md`
12. `notes/2026-09-05-08-25-34_notes.md`
13. `notes/2026-09-05-09-53-11_notes.md`
14. `notes/2026-09-06-22-21-54_notes.md`
15. `notes/2026-09-07-11-19-48_notes.md`
16. `notes/2026-09-09-20-43-00_notes.md`

## Pravidlo přednosti

- Pozdější zdroj v uvedeném pořadí může zpřesnit, opravit nebo nahradit dřívější zdroj.
- Při rozporu se nesmí mechanicky spojit obě varianty. Rozhodující je skutečná implementace, přičemž pozdější zdroj vysvětluje zamýšlenou opravu předchozího návrhu.
- Aktuální implementace potvrzuje, co bylo skutečně dodáno. Sama však nerozšiřuje rozsah tohoto záměru mimo témata uvedená ve zdrojových dokumentech.
- Pokud se implementace, cílová dokumentace a zdrojové dokumenty rozcházejí nejednoznačným způsobem, musí být rozpor zaznamenán a před změnou dokumentace vyjasněn. Nesmí se hádat zamýšlené chování.

## Postup vyhodnocení každého tvrzení

Pro každý samostatný požadavek, fix, rozhodnutí nebo popis chování ze zdrojů:

1. Identifikovat dotčenou součást, například přenosné jádro, službu, IPC, konfiguraci, tray aplikaci, wizard, editor, protokol nebo testovací bránu.
2. Dohledat odpovídající implementaci a relevantní testy.
3. Zařadit zjištění do jedné z kategorií:
   - implementováno a správně dokumentováno,
   - implementováno, ale v cílových dokumentech chybí nebo je popsáno nesprávně,
   - neimplementováno,
   - nelze jednoznačně rozhodnout.
4. Pouze ve druhém případě upravit odpovídající cílový dokument.
5. V prvním případě neprovádět zbytečnou změnu.
6. Ve třetím případě nepřenášet plánovaný stav do normativní dokumentace jako hotovou skutečnost.
7. Ve čtvrtém případě zachovat důkaz a vyžádat rozhodnutí před zápisem sporného tvrzení.

## Rozsah úprav cílových dokumentů

### `client.md`

Primární konsolidační dokument. Má obsahovat implementovanou architekturu klienta, služby a tray aplikace, jejich odpovědnosti, konfiguraci, IPC, provozní stavy, bezpečnostní vlastnosti, obnovu, testovací požadavky a další klientské chování, které není výhradně wire protokolem nebo čistě UX specifikací.

### `protocol.md`

Upravit pouze tam, kde ověřená změna ze zdrojů mění nebo zpřesňuje síťový protokol, formáty zpráv, kryptografická pravidla, kanonizaci, limity, validaci, interoperabilitu nebo jiné normativní chování mezi klientem a serverem.

### `tray_UX.md`

Upravit tam, kde ověřená změna ze zdrojů popisuje uživatelské rozhraní tray aplikace, okna, akce, texty, stavy, focus, layout, přístupnost, chování wizardu a editorů nebo jiné čistě prezentační a interakční požadavky.

Stejná skutečnost se nemá bezdůvodně duplikovat. Pokud potřebuje přesah mezi dokumenty, jeden dokument ji popíše normativně a ostatní na ni vhodně odkážou.

## Výslovně mimo rozsah

- Nevytvářet dokumentaci pro implementaci, která nemá oporu v žádném z vyjmenovaných plánů ani souborů `_notes.md`.
- Neimplementovat nové funkce a neopravovat kód pouze proto, že zdrojový dokument popisuje neimplementovaný záměr.
- Neprohlašovat plánované chování za skutečně implementované bez ověření v kódu a relevantních testech.
- V této fázi nemaž žádný plán ani soubor ve složce `notes`.
- Neměnit `plan_server.md`, protože nebyl zařazen do schváleného zdrojového seznamu.
- Nerozšiřovat úkol na úplný audit veškeré implementace mimo témata doložená schválenými zdroji.

## Požadované důkazy a dohledatelnost

Realizace musí udržovat pracovní matici alespoň s těmito poli:

- zdrojový soubor a konkrétní část,
- shrnutí tvrzení,
- výsledek ověření implementace,
- relevantní implementační soubory a testy,
- cílový dokument a upravená část,
- stav: beze změny, doplněno, neimplementováno nebo nejasné.

Matice může být pracovní artefakt realizace. Nemusí se stát součástí dlouhodobé uživatelské dokumentace, musí však umožnit ověřit, že žádný zdrojový bod nebyl vynechán.

## Akceptační kritéria

- Všechny tři cílové dokumenty byly načteny a jejich role byly respektovány.
- Všech šestnáct zdrojových dokumentů bylo zpracováno v uvedeném pořadí.
- Každé věcné tvrzení ze zdrojů má doloženou klasifikaci vůči aktuální implementaci.
- Každé implementované tvrzení ze zdrojů je buď již správně popsáno, nebo bylo doplněno do odpovídajícího cílového dokumentu.
- Neimplementovaná tvrzení nebyla zapsána jako aktuální stav.
- Implementace bez opory ve schválených zdrojích nebyla v rámci tohoto záměru nově dokumentována.
- Pozdější opravy a poznámky mají přednost před překonanými částmi starších plánů.
- Mezi `client.md`, `protocol.md` a `tray_UX.md` nevznikly zjevné rozpory ani zbytečné úplné duplicity.
- Změny dokumentace prošly kontrolou odkazů, názvů souborů, terminologie a vnitřní konzistence.
- Zdrojové plány a poznámky zůstaly zachované. Po dokončení je možné samostatně rozhodnout o jejich odstranění, protože jejich relevantní implementovaný obsah je dohledatelně pokryt cílovými dokumenty.

## Realizační vodítko

Úkol je vhodné rozdělit na sekvenční průchod zdroji, nikoli na nezávislé paralelní přepisy. Pozdější dokumenty opravují dřívější a sdílené cílové soubory by při souběžných úpravách snadno získaly rozpory. Před každou dokumentační změnou je nutné nejprve uzavřít ověření odpovídající implementace.

