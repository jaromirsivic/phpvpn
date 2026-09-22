---
intent_version: 1
created_at: 2026-09-22-18-52-06
produces: intent-v001.md
---

# Zdrojová konverzace intentu

**User:** V rámci projektu PHP VPN chci vytvořit nový intent.

**AI:** Tak, v tomhle intentu pro PHP VPN už zachytávám požadavky. Co má řešit jako první?

**User:** Chci upravit tray aplikaci, konkrétně klientskou aplikaci následujícím způsobem: když uživatel klikne na configuration wizard, tak hnedka v prvním kroku, kde je server URL, tak bych chtěl pod prvním horizontálním separátorem vložit novou sekci, která bude oddělená dalším horizontálním separátorem od patičky, kde se píše HTTP and HTTPS are supported. The URL cannot a tak dále. A do téhleté nové sekce, oddělené separátory, vložíme tlačítko nazvané HTTP headers. A když uživatel na tohleto tlačítko klikne, tak se mu otevře okno nad aktuálním formulářem, kde bude čistě jenom textbox jako vstupní pole, multiline textbox s HTTP hlavičkama, kde může tyhlety HTTP hlavičky vložit typicky na nový řádek. A je třeba to sladit s již defaultními HTTP hlavičkami, které jsou teďka v sekci Edit config, protože teď tam máme tu Edit configu nějaké výchozí HTTP hlavičky při resetu. Takže tohleto převzít a převzít to z nějakého pravděpodobně hardcodovaného globální proměny. A ještě navíc do té sekce pod to tlačítko nebo nad něj vypsat, o co se jedná a dát tam relativně krátký popis uživateli, že tady může nastavit HTTP hlavičky pro případ, kdy to webový server vyžaduje, ale taky je třeba tam dopsat informaci, že pokud uživatel neví, co s tím dělat, tak ať to nevyplňuje, že je lepší ponechat výchozí stav.

**AI:** Díky, mám to zachycené. Pokračuj dalšími detaily, řekni, že to mám pozastavit, nebo že to mám rovnou zpracovat.

**User:** Ještě u těch HTTP hlaviček napiš před to tlačítko, zde je možné nastavit HTTP hlavičky, samozřejmě všechno v angličtině, přelož to, a potom pod tím tlačítkem na nový řádek tam vypiš, některé webové servery a poskytovatelé požadují vyplnění HTTP hlaviček, obzvláště cookie a user agenta, ale pokud uživatel neví, co nastavit, tak je dobré ponechat výchozí hodnoty. Je třeba to tam říct nějak rozumně v korporátním stylu a nějak krátce. Pochopitelně, když uživatel klikne na to tlačítko, tak se zobrazí formulář, kde bude v podstatě přes celý formulář jenom ten multiline textbox s hlavičkama. A pozor, a dole bude tlačítko OK Cancel, takže nebude to přes celý formulář, dole bude patička, vpravo bude klasické tlačítko OK, vlevo potom od toho tlačítka OK Cancel, udržovat stejný styl jako přímo na tom configuration wizardu, to znamená stejný padding, stejné odsazení, stejný styl patičky a nastavit nějaké rozumné minimální velikosti. Maximální velikost nenastavovat, maximální velikost může být až do velikosti obrazovky, prostě uživatel to může maximalizovat, kdykoliv se mu zachce.

**AI:** I tyto doplňující požadavky jsem zachytila. Můžeš pokračovat, nebo říct, že mám intent zpracovat.

**User:** Dokud do prdele neskončím, tak nebudeš... ne... drž hubu. Dokud neskončím s diktováním, tak mě nepřerušuj. A dokud uživatel nedá doleva... Takhle, dej doleva tlačítko Reset, klasicky červené, v postišení resetu si vezmeš defaultní HTTP hlavičky a nastavíš je do multiline textboxu. A pozor, bude to odděleno, tento krát... tento krát nikoliv... nikoliv lomeno R lomeno N, ale klasicky to bude odentrované. Na lomeno R lomeno N se to převede až teprve poté. Udržuj si ty HTTP hlavičky jenom v paměti a uložíš to do config JSON až teprve poté, co si uživatel projde celým konfiguračním... celým configuration wizardem a dá save.

**AI:** Zachytávání pokračuje, bez dalších hlasových vstupů z mé strany, dokud výslovně neřekneš, že jsi skončil.

**User:** Dobře, tak tohleto je první bod, teďka druhý bod.

**AI:** První bod je zachycen. Pokračuj druhým.

**User:** Tak v rámci Edit config formuláře, když uživatel klikne na tlačítko Reset, tak chci, aby v defaultních komunikačních kontraktech nebylo nic. Komunikační kontrakty zůstanou prázdné.

**AI:** Druhý bod průběžně zachycuji. Pokračuj.

**User:** Pochopitelně poté, třeba upravit veškerou dokumentaci, to znamená klient MD nebo tray MD, podle toho, co se změnilo. To je klasický... klasické workflow.

**AI:** Zachyceno. Pokračuj.

**User:** Zpracuj intent.

**AI:** V projektu je vzorový komunikační kontrakt součástí sdílené výchozí konfigurace, zatímco průvodce má vlastní logiku pro vytváření kontraktů. Má se změna týkat jen výchozí konfigurace používané přes Edit Config a její Reset, s ponecháním chování průvodce beze změny? Výchozí návrh je ano.

**User:** Tomu nerozumím, vysvětli mi to.

**AI:** Jde o to, že vzorový komunikační kontrakt je teď součástí sdílené výchozí konfigurace, ale průvodce si kontrakty vytváří po svém. Potřebuju si potvrdit: má se změna týkat jen výchozí konfigurace, kterou ovlivňuje Edit Config a Reset, a nechat průvodce beze změn? Výchozí návrh je ano.

**User:** Ano, na edit konfigu při stištění tlačítka reset, tak tam se konfigurace skutečně nevyplní a ten průvodce, konfigurační průvodce, ten zůstane, co se týká výchozích konfiguračních kontraktů, nezměněný. Ten jich tam vytváří mnoho.

**AI:** Rozsah druhého bodu je potvrzený: Reset v Edit Config použije prázdný objekt komunikačních kontraktů, zatímco generování kontraktů v Configuration Wizardu zůstane beze změny. Má tlačítko OK v dialogu HTTP headers hlavičky rovnou validovat a při chybě ponechat dialog otevřený s vysvětlením, zatímco Cancel změny zahodí? Výchozí návrh je ano.

**User:** Ano, při validaci hlaviček tohleto je třeba udělat. Důležité je zkontrolovat, jestli uživatel náhodou se nepokouší přepsat nějakou z hlaviček, které jsou v systémem generovány nativně. To znamená, nativní hlavičky není možné umožnit uživateli, aby přetížil.

**AI:** Validace dialogu je potvrzená: OK musí odmítnout neplatné hlavičky i pokus o přepsání nativně generovaných hlaviček, zobrazit srozumitelnou chybu a ponechat dialog otevřený; Cancel změny zahodí. Ověřuji existující pravidla, aby intent navazoval na současnou validaci.
