---
intent_version: 1
created_at: 2026-09-13-01-54-56
produces: intent-v001.md
---

# Zdrojová konverzace k intentu

**User:** Chci vytvořit nový intent

**AI:** Nový intent je ve fázi zachycení a teď potřebuji jeho základní námět.

**AI:** Zachytávání nového intentu je aktivní. Můžeš kdykoli říct „pozastavit“, „pokračovat“ nebo „zpracovat intent“. Co chceš do intentu zachytit?

**User:** Chtěl bych vylepšit klientskou aplikaci jak tray aplikaci, tak pravděpodobně službu. Stalo se mi, že jsem nasazoval `phpvpn.php` na free hostingovou službu. Ta si vkládala do odpovědí od PHP nějaké vlastní texty, a proto padalo připojení klienta k serveru. Nebyl jsem schopen to jednoduše zdebugovat.

Chtěl bych, aby v rámci konfiguračního wizardu, když se uživatel v prvním kroku připojuje na server a ve druhém ověřuje server secret, tedy jen v těchto dvou krocích, kdy se ověřuje URL a následně server secret, při selhání vyskočilo v panelu v patičce tlačítko vedle `Verify and continue`. V kroku dva je to vedle tlačítka `Back`. Původně jsem uvažoval žluté tlačítko `Error message`, ale dejme tam červené tlačítko `Error detail`.

Když uživatel klikne na `Error detail`, zobrazí se okno se dvěma taby nahoře. Použij stejný design tabů a stejné barvy jako v `Show Server Info`. První tab bude `Request` a druhý `Response`. V requestu bude vypsané, co jsme poslali, včetně hlaviček, tedy celý HTTP request, který šel na server. Všechno je buď zašifrované, nebo tam nejdou žádné secrety, takže se toho nemusíme bát. V response bude to, co nám server odpověděl. Potřebuji jednoduše vidět, co podobný webhosting posílá v odpovědi. Tohle bude v prvních dvou krocích konfiguračního wizardu.

**AI:** Mám to zachycené. Pokračuj v diktování, případně řekni „pozastavit“ nebo „zpracovat intent“.

**User:** Myslím si, že bude potřeba upravit tray aplikaci a velice pravděpodobně také službu, aby si spolu povídaly a abychom si v prvních dvou krocích, které se týkají ověření správného fungování serveru a server secretu, dokázali vyměnit celý request a response. Bude se to muset naimplementovat, zdokumentovat v `client.md` a `protocol.md` a budou se na to muset napsat testy. Tentokrát se to nesmí dělat v žádném worktree. Bude se to dělat přímo v aktuální feature větvi, tuším `tray_UX`.

**AI:** Zachyceno. Pokračuj, nebo řekni „zpracovat intent“, až bude zadání kompletní.

**User:** Ještě doplnění k tabům. V tabu `Request` bude read-only víceřádkový textbox, ve kterém bude celý HTTP request přesně tak, jak odešel. V `Response` bude totéž, zase read-only víceřádkový textbox, a přesně tak, jak jsme odpověď dostali, ji zobrazíme.

**AI:** Doplnění je zachycené. Pokračuj, nebo řekni „zpracovat intent“.

**User:** Nemusí tam být žádná patička, kde by bylo nějaké OK, Cancel a tak dál. Taby a obsah budou přes celý formulář a uživatel formulář zavře křížkem.

**AI:** I tohle mám zachycené. Pokračuj, nebo řekni „zpracovat intent“.

**User:** Zpracuj intent.

**AI:** Zachycení je uzavřené a zadání obsahuje konkrétní chování, dokumentaci, testy i omezení na aktuální feature větev. Teď ověřuji projektové souvislosti a případné nejasnosti před sepsáním intentu.

**AI:** Q1: Má se `Error detail` zobrazit při každém selhání v prvních dvou krocích, i když požadavek kvůli lokální validaci, DNS nebo TLS vůbec neodešel? Navržený výchozí stav je ano, přičemž nedostupná část zobrazí stručnou informaci, že žádný HTTP request nebo response nevznikl. Bez odpovědi přijmu tento výchozí stav. Aktuální větev je ověřená jako `tray_UX`.

**User:** Ne, to není nutné. `Error detail` ať se zobrazí jenom v případě, že se k nám odpověď vůbec dostala. Jenom když proběhne komunikace a dojde k chybě, potom se zobrazí `Error detail`.

**AI:** Podmínka je upřesněná: detail chyby se nabídne pouze tehdy, když HTTP komunikace skutečně proběhla, dorazila odpověď a její zpracování skončilo chybou.

**AI:** Q2: Potvrzuješ toto přesné umístění červeného tlačítka `Error detail`? V prvním kroku bude bezprostředně vlevo od `Verify and continue` v pravé skupině, ve druhém kroku bezprostředně vpravo od `Back` v levé skupině. To je navržený výchozí stav a bez odpovědi ho přijmu.

**User:** Ano, přesně tak. Samozřejmě tam bude ještě drobná mezera, aby tlačítka nebyla nalepená na sebe. Jestli si chceš zkusit `phpvpn.php`, kde to kolabuje, pošlu do chatu free hosting, který při volání REST API vkládá nějaké HTML a JavaScript.

**AI:** Umístění i malou mezeru mezi tlačítky mám potvrzené. Pošleš mi teď adresu toho free hostingu, abych ji zahrnula jako reprodukční kontext a mohla ověřit podobu chybné odpovědi?

**User:** [https://phpvpn.freehosting.dev/phpvpn.php](https://phpvpn.freehosting.dev/phpvpn.php)

**User:** Máš to v chatu.

**AI:** Odkaz mám. Ověřím bezpečně pouze veřejnou odpověď serveru bez platných secretů a použiji ji jako reprodukční kontext intentu.

**AI:** Přímé načtení přes webovou čtečku tento server odmítlo jako nebezpečný cíl. Neautentizovaný diagnostický požadavek bez secretů skončil zavřením TLS spojení ještě před přijetím HTTP odpovědi, takže vložené HTML nebylo možné z tohoto prostředí ověřit.

**AI:** Q3: Potvrzuješ, že ve druhém kroku zobrazíme pouze request a response konkrétního HTTP volání, jehož odpověď způsobila chybu, a tento detail se smaže při zahájení nového ověření? Navržený výchozí stav je ano. Bez odpovědi ho přijmu.

**User:** Test vůči tomuto free hostingu zařaď až úplně na konec. Nejdřív by měly proběhnout testy u nás, klidně přes HTTP. Běží nám Nginx ve složce `D:\nginx`. Myslím, že v kořenovém adresáři je i `phpvpn.php`, se kterým se dá pro tyto testy hrát a lehce jej modifikovat, aby vracel chybu. Až se to odladí lokálně, dá se otestovat chování s tímto serverem na free hostingu.

**AI:** Lokální testování přes Nginx a upravitelný `phpvpn.php` bude předcházet ověření na free hostingu. Výchozí pravidlo z Q3 přijímám.

**AI:** Q4: Má být závěrečný test proti free hostingu povinnou podmínkou dokončení, nebo jen best-effort ověřením po úspěšných lokálních testech? Navržený výchozí stav je best-effort, protože cizí hosting může být nedostupný nebo změnit chování. Bez odpovědi přijmu tento výchozí stav.

**User:** Je to jenom best effort.

**AI:** Ještě jedna technicky podstatná volba: WinHTTP neposkytuje doslovné bajty přenesené uvnitř TLS ani vždy přesnou podobu HTTP/2 na drátu. Q5: Stačí v detailech věrná aplikační rekonstrukce HTTP zprávy, tedy metoda, URL, dostupné odeslané a přijaté hlavičky, status a nezměněné tělo před protokolovým parsováním? Navržený výchozí stav je ano, bez zobrazování TLS bajtů nebo interních HTTP/2 rámců. Bez odpovědi ho přijmu.

**User:** Zkus mi to trochu víc popsat, nerozumím tomu, co se po mně chce tady v téhleté odpovědi.

**AI:** Jde o význam slova „přesně“. Aplikace předá Windows metodu, URL, hlavičky a tělo. Windows pak může některé technické hlavičky doplnit nebo upravit, zvolit HTTP/2 a celý přenos zašifrovat pomocí TLS. Navrhuji zobrazit všechno, co aplikace předala Windows, a všechno, co od Windows dostala zpět před parsováním. Uvidíš tedy i nečekané HTML nebo JavaScript vložený hostingem. Nešlo by ale o síťový záznam šifrovaných paketů. Je pro tebe tato varianta v pořádku? Výchozí odpověď je ano.

**User:** Ano.

