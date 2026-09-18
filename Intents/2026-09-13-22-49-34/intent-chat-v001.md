---
intent_version: 1
created_at: 2026-09-13-22-56-14
produces: intent-v001.md
---

AI: Máme zapnuté zachytávání nového intentu pro projekt phpvpn. Nadiktuj mi první požadavky nebo nápady a když budeš chtít, můžeš kdykoli říct pozastavit, pokračovat nebo zpracovat intent.

User: Narazil jsem na problém, který je potřeba dohledat, zjistit, co ho způsobuje, a opravit. Stane se to, děje se to tehdy, když například na lokálním počítači se přihlásím k http://127.0.0.1/phpvpn.php, což je server, který běží v D:\Nginx\html. Tam je možné najít ten skript. Je to nejaktuálnější phpvpn.php. A když se z lokálního systému na tohleto PHP přihlásím, všechno se zdá OK. Potom nainstaluju na jiný počítač ten samý klient, to znamená nejnovější verzi, nejnovější distribuci, kterou máme, tak jsem nainstaloval na jiný počítač, na virtuální stroj. Přihlásil jsem se taky k phpvpn.php, které u nás běží. Pingnul jsem si z jiného počítače na tento počítač, což je super, ale jakmile ping skončil, tak už se neposílají žádné další pakety. A přesto se neustále tenhle ten stroj dotazuje serveru ve frekvenci, která byla stanovena serverem, což je tuším teď padesát milisekund, a vůbec tam neprobíhá žádný decay, to znamená ten interval se nezdvojnásobuje. A ani když na tenhle ten stroj nepřicházejí vůbec žádné pakety.

AI: Mám to poznamenané, pokračuj prosím.

User: No, to je v podstatě vše, je třeba zjistit, čím to je, odladit to a zabezpečit, aby se to nedělo.

AI: Beru i ten požadavek na diagnostiku, opravu a prevenci opakování. Až budeš chtít, řekni zpracovat intent.

User: Zpracuj intent.

AI: Našla jsem pravděpodobnou příčinu: klient bere kumulativní čítače paketů, takže po prvním pingu už interval zůstane pořád na minimu. Navrhuju opravit to v klientovi, přidat regresní testy a ověřit proti současnému serveru, bez úprav nasazeného skriptu, pokud se neprokáže jeho podíl. Sedí ti tenhle rozsah? Když neodpovíš, vezmu to jako výchozí.

User: Myslím, že ano, že jsi to řekla dobře, to znamená najít příčinu, ověřit, že to je skutečně ona, přidat testy, pokud je to nutné, tak opravit dokumentaci, ale tím si nejsem úplně jistý, a potom nezapomenout vytvořit distribuci.
