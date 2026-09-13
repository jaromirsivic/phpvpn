---
intent_version: 1
created_at: 2026-09-13-10-16-36
produces: intent-v001.md
---

# Záznam konverzace k intentu

AI:

Nový intent jsem založila a zachytávání je aktivní. Můžeš začít diktovat. Kdykoli můžeš říct „pozastav“, „pokračuj“ nebo „zpracuj intent“.

User:

Chci ti teď říct nějaký nový intent, založ nový intent.

User:

Chci upravit klientskou aplikaci, pravděpodobně službu i tray aplikaci. Možná tray aplikaci vlastně ani ne, ale hlavně službu a to v místě, kde se edituje konfigurace. Konfigurační JSON je potřeba rozšířit o novou položku. Nejdřív jsem uvažoval, že bude volitelná, ale má být povinná a při vytváření konfiguračního JSONu se do něj přidá automaticky. Položka se bude jmenovat httpHeaders, malé http, velké H a zbytek malými písmeny.

Položka bude obsahovat HTTP hlavičky, které přijdou do všech požadavků na PHPVPN, tedy do health, info a data. Důvodem je možnost simulovat typický webový prohlížeč nebo přidat například cookies.

AI:

Rozumím, pokračuj prosím.

User:

Výchozí hodnota hlaviček:

~~~http
Cache-Control: no-cache
Pragma: no-cache
User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:155.0) Gecko/20100101 Firefox/155.0
~~~

AI:

Zaznamenala jsem to. Pokračuj.

User:

Hlaviček může být více a hodnota je víceřádková, takže v JSONu musí být správně escapovaná.

AI:

Zaznamenala jsem to. Pokračuj.

User:

Tohle je všechno, zpracuj intent.

AI:

Zachycení je uzavřené. Mám celý viditelný průběh včetně výchozích HTTP hlaviček a ověřuji dotčená místa konfigurace i síťových požadavků, abych mohla intent přesně formalizovat.

User:

Očekával bych, že ty hlavičky dáme do masterSetup hned pod serverUrl.

AI:

Umístění je teď jednoznačné. Povinné httpHeaders bude v masterSetup hned za serverUrl.

User:

Možná bych v masterSetup přeskládal celé výchozí pořadí. První položka bude serverSecret, pod ní serverUrl, pod ní httpHeaders, potom trustAnyTlsCertificate a pak už pořadí, jaké máme.

User:

Tohle je pouze výchozí pořadí při první instalaci služby nebo při resetu konfiguračního JSONu. Když uživatel položky přeuspořádá, aplikace to musí umět zpracovat a na pořadí nesmí trvat.

AI:

Potřebuji potvrdit jednu věc. Pod požadavkem na čas myslíš stávající health požadavek, přes který se synchronizuje čas? Pokud neodpovíš, vezmu jako výchozí odpověď ano.

User:

Ano, myslím tím health požadavek, přes který se synchronizuje čas. Ověř ve zdrojovém kódu, z čeho se čerpá čas, podle kterého se potom synchronizuje delta.

AI:

Zdrojový kód potvrzuje, že ověřená odpověď health obsahuje autentizovaný serverový timestamp. Z něj se vytvoří časový snapshot s dobou odeslání a přijetí. Následné requesty odvozují aktuální serverový čas pomocí monotónních hodin a bezpečnostní rezervy. Hlavičky proto musí být i na health požadavku, který tento stav zakládá a obnovuje.

User:

Samozřejmě platí, že na uvedeném pořadí nesmíme trvat. Je to pouze námi navržené výchozí pořadí při resetu konfigurace nebo při první instalaci služby, kdy se konfigurace vytváří. Když uživatel položky přeuspořádá, musíme se s tím umět vypořádat.
