---
intent_version: 1
created_at: 2026-09-19-08-14-34
produces: intent-v001.md
---

AI: Nový intent je aktivní v projektu phpvpn. Můžeš začít diktovat. Kdykoli můžeš zachycení pozastavit, obnovit nebo požádat o zpracování intentu.

User: Vytvoř nový intent.

User: V rámci nového zadání bych chtěl řešit automatický start tray aplikace po startu Windows, to znamená kdykoliv je aplikace nastartována, teda nainstalována, tak se někde zaregistruje a teď nevím kde, udělej to na tom správném místě, tak se zaregistruje, že tray aplikace se startuje vždycky po startu Windows. Když uživatel aplikaci odinstaluje, tak se ta tray aplikace samozřejmě odinstaluje taky. A musí to být bezpečné, při instalaci služby a tray aplikace, kdyby náhodou spadlo to spuštění tray aplikace po startu Windows, tak to nesmí být blokry v celé instalaci.

User: Už jsem začal, načti si ten text, který jsem ti před chvílí poslal do chatu.

AI: Mám ten předchozí text zachycený jako součást nového intentu. Pokračuj v diktování.

User: Tak tady, to je jedna věc. Druhá věc, kterou bych chtěl, a to dejme do konfigurace a dokonce to bude i ve výchozí konfiguraci, ať už je ta konfigurace vytvořená při instalaci nebo potom při resetu nebo při, když uživatel bude procházet konfiguračním wizardem a vyresetuje to v konfiguračním wizardu, tak bych chtěl úplně na konec do... elementů... do elementů Master Setup... pod use default access key in wizard. Tak úplně pod tohle, pod tenhle ten element bych přidal autoconnect, něco autoconnect on startup, které... což bude boolean, výchozí hodnota je false, ale když ji někdo změní na true a restartuje operační systém, tak to znamená, že po startu se hnedka služba připojí k serveru nebo se pokusí připojit k serveru. Když to nepůjde, no tak spadne, ale když to půjde, tak se připojí.

AI: Mám zachycenou i druhou část. Pokračuj dál.

User: A další věc, chtěl bych dát do aplikace ikony, aplikační ikona bude ikona default tečka ico ze složky assets, kterou teďka máme v projektu. Pak tam jsou ikony connected, connecting a disconnected. Myslím si, že jsou z dvojkou. Vydrž chvilku, kouknu se. Ne, nejsou. Jsou opravdu pojmenované connected, connecting, disconnected a default, všechno ICO. Takže ta default ICO to by měla být výchozí ikona těch exe aplikací ve Windows. V podstatě bude se používat na všech formulářích v takovém tom levém horním rožku a taky jako ikona exe souborů, aby to uživatel viděl, když si bude prohlížet tu složku. Potom ikony connected, connecting a disconnected se budou používat v tray aplikaci, aby indikovaly v jakém jsme stavu, jestli jsme ve stavu connected nebo jsme ve stavu disconnected a connecting je vlastně takový mezistav, který se používá, když ještě nejsme připojeni, ale připojujeme se, anebo naopak když jsme připojení a odpojujeme se. Takový ten středový stav.

AI: Mám zachycenou i tuto část. Pokračuj dál.

User: Zpracuj intent.

AI: Zachycení je uzavřené a intent přechází do zpracování. Ověřím instalační tok, konfiguraci a stavový model tray aplikace, potom položím jen otázky, které mají dopad na realizaci.

AI: Před sepsáním intentu potřebuji jediné potvrzení: mohu použít všechny tyto výchozí volby?

1. Tray aplikace se bude registrovat strojově pro všechny uživatele a spustí se při jejich přihlášení do Windows. Selhání registrace ani jejího odstranění nezablokuje instalaci či odinstalaci, pouze se zaznamená varování.
2. `autoConnectOnStartup: false` bude zpětně kompatibilní. Při hodnotě `true` služba po startu provede běžné připojení. Neúspěch nezpůsobí pád služby, ale projeví se standardním chybovým nebo obnovovacím stavem.
3. Ikona `connecting.ico` se použije při připojování, odpojování i obnovování spojení. `disconnected.ico` pokryje odpojený, chybový a nedostupný stav. `connected.ico` se použije pouze pro zdravé připojení.

AI: Pokud něco neupravíš, budu tyto volby považovat za přijaté výchozí hodnoty.

User: Je už ten intent hotový?

AI: Ještě není uložený. Čekala jsem na potvrzení tří okrajových rozhodnutí. Protože jsi je neupravil, přijímám oznámené výchozí volby a teď vytvářím finální verzi intentu.
