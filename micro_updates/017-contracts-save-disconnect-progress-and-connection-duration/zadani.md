# 017 - Uložení Communication Contracts, průběh odpojení a délka připojení

## Cíl

Zlepšit zpětnou vazbu tray aplikace ve třech souvisejících situacích:

1. po uložení změn v **Communication contracts** upozornit, že je pro jejich použití nutné nové připojení,
2. při ručně vyvolaném odpojení zobrazit jednoduchý moderní průběhový formulář,
3. ve **Statistics** zobrazovat živou délku aktuální VPN relace.

## 1. Upozornění po uložení Communication contracts

### Ověřený současný stav

- Save odešle nový konfigurační JSON službě prostřednictvím `set-config`.
- Úspěšný Save sám nevyvolá Disconnect, Connect ani Reconnect.
- Aktivní VPN pokračuje s konfiguračním snímkem načteným při dosavadním připojení.
- Při aktivním spojení už okno zobrazuje nenápadnou informaci `Changes take effect on the next connection.`.
- Po potvrzeném uložení se okno v současnosti zavře bez výrazného upozornění.

### Požadované chování

Pokud je VPN v době ukládání aktivní, uživatel provedl alespoň jednu změnu a služba uložení jednoznačně potvrdila, zobrazit před dokončením akce výrazné upozornění v moderním vizuálním stylu aplikace.

Použít anglický text:

**Configuration saved**

`Your changes have been saved. Disconnect and connect again to apply them.`

Po potvrzení upozornění okno Communication contracts zavřít.

### Pravidla

- Neprovádět automatický Disconnect, Connect ani Reconnect.
- Upozornění nezobrazovat při odmítnutém nebo neznámém výsledku Save.
- Upozornění nezobrazovat, pokud VPN není aktivní.
- Zachovat současné bezpečné chování pro chybu, timeout a neznámý výsledek publikace.

## 2. Stavový formulář pro ruční Disconnect

### Otevření a průběh

Při ručním vyvolání Disconnect z tray aplikace otevřít jednoduchý stavový formulář ve stejném moderním vizuálním stylu jako současný formulář pro Connect.

Během skutečného odpojování zobrazit žlutý stavový panel s textem:

**Disconnecting...**

`Please wait while the VPN disconnects.`

Formulář musí zůstat zobrazený po celou dobu odpojování a reagovat na autoritativní stav služby, nikoli pouze na uplynutí lokálního časovače.

### Úspěšné dokončení

Teprve jakmile služba potvrdí stav Disconnected:

- změnit žlutý panel na zelený,
- zobrazit text:

  **Disconnected**

  `You have been disconnected successfully.`

- zobrazit odpočet `3`, `2`, `1`,
- po dokončení odpočtu formulář automaticky zavřít.

### Chyby a souběhy

- Nikdy nezobrazit Disconnected pouze na základě odeslání příkazu.
- Při chybě nebo nedostupnosti služby nezobrazit falešný úspěch.
- Zachovat bezpečný a srozumitelný způsob prezentace existující chyby.
- Formulář nesmí sám opakovat Disconnect ani automaticky spouštět Connect.
- Respektovat existující ochrany proti souběžným příkazům, zavření aplikace a ukončení Windows relace.

## 3. Délka aktuálního připojení ve Statistics

### Autoritativní čas relace

Služba si bude pamatovat datum a čas začátku aktuální úspěšně navázané VPN relace. Jde o začátek logického připojení, nikoli o dobu běhu služby a nikoli o čas poslední libovolné změny stavu.

Tento čas:

- nastavit při úspěšném navázání nového spojení,
- zachovat během automatického Recovering a následného návratu do Connected,
- vynulovat po dokončeném Disconnectu,
- nahradit novou hodnotou při následujícím úspěšném Connectu,
- bezpečně zpřístupnit tray aplikaci ve stavových datech.

Existující `stateChangedAt` nelze bez dalšího vydávat za délku celé relace, protože se mění také při dalších stavových přechodech.

### Zobrazení

Pokud je VPN ve stavu Connected, nahradit současný text `(Connected)` živě aktualizovaným údajem v přesném kompaktním formátu:

`(Connected for 2d 22h 33m 55s)`

Pravidla formátování:

- údaj aktualizovat každou sekundu,
- zobrazovat celé dny, hodiny, minuty a sekundy,
- používat označení `d`, `h`, `m` a `s`,
- zobrazovat i nulové složky kvůli stabilnímu formátu,
- při Disconnected zobrazovat pouze současný text `(Disconnected)`,
- během Recovering zachovat původní začátek relace, ale délku prezentovat jako Connected pouze tehdy, když je autoritativní stav skutečně Connected,
- při dočasné nedostupnosti služby zachovat bezpečné existující pravidlo pro poslední platný snapshot a nezobrazovat vymyšlený nový začátek relace.

Lokální časovač pro překreslení textu nesmí měnit frekvenci síťového nebo IPC pollingu. Délku mezi platnými snapshoty dopočítávat lokálně z autoritativního začátku relace.

## Akceptační kritéria

1. Potvrzený Save alespoň jedné změny Communication contracts při aktivní VPN zobrazí výrazné upozornění na nutnost Disconnect a následného Connect.
2. Save sám neobnovuje spojení.
3. Upozornění se nezobrazí pro neaktivní VPN ani pro nepotvrzené uložení.
4. Ruční Disconnect otevře nový moderní stavový formulář.
5. Během odpojování je zobrazen žlutý panel s požadovaným textem.
6. Zelený úspěšný panel se zobrazí až po autoritativně potvrzeném Disconnected.
7. Po úspěšném odpojení proběhne viditelný odpočet 3, 2, 1 a formulář se automaticky zavře.
8. Chyba ani nedostupnost služby nesmějí být prezentovány jako úspěšné odpojení.
9. Statistics při Connected zobrazuje délku relace ve formátu `(Connected for 2d 22h 33m 55s)` a aktualizuje ji každou sekundu.
10. Začátek relace se nezmění během automatického Recovering.
11. Nový úspěšný Connect zahájí nové měření a dokončený Disconnect údaj zruší.
12. Časovač zobrazení Statistics nezvyšuje frekvenci IPC pollingu.
13. Layout zůstane čitelný při resize a na podporovaných DPI bez blikání, překrytí a zbytků starého textu.
14. Tray aplikace se po změně úspěšně zkompiluje a aktuální sestava bude zkopírována do distribuce pro ruční instalaci.

## Omezení implementace a ověření

- Neupravovat běžnou projektovou dokumentaci, zejména `protocol.md`, `client.md`, `tray_UX.md` a plánovací dokumenty.
- Nevytvářet nové testy.
- Obecně existující test upravit pouze tehdy, pokud je změna malá, přímočará a rychlá.
- Výjimka výslovně schválená uživatelem: opravit konkrétní starší GUI test Communication contracts, který selhává kvůli neaktuálním předpokladům o počtu řádků, aby odpovídal současné verzi aplikace. Tato oprava je součástí mikro-updatu 017 i v případě, že je rozsáhlejší než běžně povolená drobná úprava testu.
- Neprovádět komplexní testovací sady.
- Provést jen přiměřené statické a cílené ověření nezbytné pro bezpečnou implementaci.
- Po implementaci zkompilovat tray aplikaci a opravit kompilační chyby způsobené změnou.
- Znovu vytvořit a ověřit distribuci podporovaným projektovým postupem, aby byla připravena pro ruční instalaci uživatelem.
- Neprovádět skutečnou instalaci, odinstalaci, změny služby, ProgramData ani reálného VPN připojení bez dalšího výslovného požadavku.
- Neprovádět commit ani push bez výslovného požadavku uživatele.
- Neměnit tento soubor se zadáním. Implementace má vycházet z něj.

## Komunikace s uživatelem

Pokud bude kterákoli část požadovaného chování, textů, vizuálního návrhu, lifecycle nebo distribuce podstatně nejasná, implementující agent nemá hádat. Má se prostřednictvím původního chatu obrátit na uživatele s jednou konkrétní otázkou a počkat na odpověď. Uživatel výslovně uvedl, že je k dispozici pro průběžné upřesnění.

## Hranice formalizační fáze

Tento soubor vytvořila hlavní koordinátorka osobně před založením implementačního úkolu. Vytvoření ani zápis formálního zadání nebyly delegovány na jiného agenta.

Uživatel následně výslovně povolil vytvoření jednoho samostatného projektového úkolu pro implementaci a zvolil model Astra s effortem High.

Během implementace uživatel navíc výslovně schválil opravu staršího GUI testu Communication contracts, který používá neaktuální předpoklady o počtu řádků. Hlavní koordinátorka tuto výjimku osobně doplnila do zadání a předala ji běžícímu implementačnímu úkolu.
