---
intent_version: 1
created_at: 2026-09-29-22-10-40
produces: intent-v001.md
---

# Zdrojový rozhovor k intentu

Níže je úplný dostupný přepis původního hlasového rozhovoru od aktivace tohoto intentu po požadavek na jeho zpracování. Výplňová odpověď před aktivací není součástí zachyceného intentu.

**User:** Vytvoř nový intent

**AI:** Hotovo, mám připravený nový intent pro projekt PHPVPN. Klidně mi nadiktuj poznámky, a já je budu zachytávat.

**User:** Tak, je potřeba vytvořit serverovou část, tak jako máme teďka phvpvpn.php

**AI:** Mm-hmm.

**User:** Tak bych chtěl vyrobit Node.js verzi. To znamená úplně nejlépe nějaký jeden javascriptový file, který se dá potom nasadit na Node.js a bude fungovat úplně stejně jako phpvpn.php. Zatím bez cacheování do paměti, zatím všechno jenom odkládat na disk, nebude tam teda prozatím, říkám prozatím podpora pro memcached ani Redis ani pro cacheování do paměti, to přijde až později. Zatím to udělejme podobně jako phpvpn.php, přičemž ta Node.js verze nemusí být jakoby kompatibilní s phpvpn.php, myšleno, že kdyby někdo vzal adresář, který zrovna vytvářel phpvpn.php a nasadil tam Node.js, tak to nemusí převzít jeho soubory. Jde o to, aby to plně implementovalo protokol, ta Node.js verze ta se nasadí, tu si nasadí kdo bude potřebovat Node.js, zatím celý projekt phpvpn.php ještě není nikde nasazený, takže tady v tomhle tom je to fajn, protože nemusíme řešit žádnou zpětnou kompatibilitu. No je v podstatě potřeba dodržet celý protokol a otestovat, že klient korektně komunikuje s tou Node.js verzi.

**AI:** Pokračuj, zachycuji další požadavky.

**User:** Zpracuj intent
