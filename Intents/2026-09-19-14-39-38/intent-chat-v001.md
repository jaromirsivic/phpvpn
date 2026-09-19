---
intent_version: 1
created_at: 2026-09-19-14-52-07
produces: intent-v001.md
---

# Přepis konverzace k intentu

**User:** Chci vytvořit nový intent.

**AI:** V projektu už je aktivní intent, jeho realizace je pozastavená po bloku B03. Chceš ho ponechat pozastavený a přepnout aktivní záznam na nový intent?

**User:** Ano, vytvoř nový intent, přepni záznam na nový intent a budu ti říkat, co chci upravit.

**AI:** Nový intent je vytvořený a aktivní záznam jsem na něj přepnula. Předchozí intent zůstal beze změny. Teď mi postupně říkej, co chceš upravit. Můžeš kdykoli zachytávání pozastavit, obnovit nebo mě požádat o zpracování intentu.

**User:** V aplikaci klienta bych chtěl, když kliknu na Communication Contracts, jsem připojený k serveru a upravím něco, tak potom mi vyskočí taková ta hláška, jestli se chci odhlásit. V té hlášce je napsáno: „Your changes have been saved. It is strictly required that you disconnect and connect again to apply them. Do you want to disconnect now?“ Poslední větu bych změnil na „Do you want to disconnect and reconnect now?“ A potom, když uživatel řekne Yes, tak dojde k odhlášení, jako už se to děje teď, a když dojde k úspěšnému bezpečnému odhlášení, tak se systém znovu přihlásí, jako kdyby uživatel znovu klikl na Connect. Toto je první úprava.

**AI:** První úpravu mám přesně zaznamenanou. Pokračuj dalšími požadavky.

**User:** Druhá úprava je taková, že když uživatel klikne na tlačítko Save na formuláři Edit Config, aktuálně se zobrazí hláška „The configuration was stored. It takes effect on the next connection attempt.“ Místo toho dáme úplně stejnou hlášku, jakou bude mít Communication Contracts, pokud je uživatel připojený. Když uživatel klikne na Yes, aplikace ho odpojí a znovu připojí. Když klikne na No, nic dalšího se nestane a konfigurace zůstane uložená. Hláška se při Save zobrazí jen v momentě, kdy jsme připojení k serveru. Když k serveru připojení nejsme, konfigurace se prostě uloží a není potřeba uživateli říkat, že se má odhlásit a přihlásit.

**AI:** Druhou úpravu mám zaznamenanou. Můžeš pokračovat.

**User:** Na stránce Server Information se například píše „0 valid online clients“ a zobrazujeme jen počet validních online klientů. Změnil bych to tak, aby se zobrazovalo, kolik klientů je online celkem a kolik online klientů má validní komunikační kontrakt. Když jsou online čtyři klienti a jen s jedním máme platný komunikační kontrakt, mohlo by tam být například „4 online, 1 valid contract“ nebo „1 online valid contract“.

**AI:** Třetí úpravu mám zaznamenanou. Jestli je to vše, řekni „zpracuj intent“ a připravím specifikaci.

**User:** Ještě chci přidat jednu věc. Aktuálně máme instalační a odinstalační skripty v BAT a PowerShellu. Chtěl bych vytvořit úplně samostatné mini EXE aplikace pro instalaci a odinstalaci phpvpn. Aplikace zobrazí formulář s multiline textboxem, do kterého se bude vypisovat všechen výstup z PowerShellu. Musí se spustit s administrátorskými právy, aby mohla s administrátorskými právy spustit instalační nebo odinstalační PowerShell bez dalšího potvrzování oprávnění. Uživatel uvidí, co se děje. Výchozí pozadí formuláře bude žluté nebo žlutohnědé. Při úspěchu zezelená a při neúspěchu zčervená. Kolem textboxu musí být alespoň 20 pixelů prostoru, aby byla stavová barva jasně vidět. V záhlaví instalačního formuláře bude „Installation in progress...“. Pro odinstalaci se má zvolit odpovídající anglický text, například „Uninstallation in progress...“.

Do multiline textboxu se bude průběžně vypisovat výstup PowerShellu. Po skončení uživatel formulář sám zavře, ať už operace uspěla, nebo selhala. Miniaplikace musí zůstat kompatibilní s budoucím rozšiřováním PowerShellových skriptů a musí spolehlivě zjistit, zda proběhla validní instalace nebo odinstalace. Instalační miniaplikace bude spolupracovat s instalačním PowerShellem a odinstalační s odinstalačním PowerShellem. Instalační aplikace použije jako hlavní ikonu zelenou `connected.ico`, odinstalační aplikace červenou `disconnected.ico`.

**AI:** Čtvrtou úpravu s instalačními a odinstalačními miniaplikacemi mám zaznamenanou. Pokračuj.

**User:** Formulář mini EXE aplikace má mít rozumnou minimální a výchozí velikost. Výchozí velikost může být přibližně 800 × 600 nebo menší, podle rozumného návrhu, ale okno nesmí jít zmenšit na nepoužitelné minimum.

**AI:** Doplnění o výchozí, minimální a maximální rozumné velikosti formuláře mám zaznamenané. Pokračuj.

**User:** Oprava: maximální velikost není omezená a okno lze maximalizovat na celou obrazovku. Je jen minimální a výchozí startovací velikost. Formulář se má zobrazit uprostřed aktivní obrazovky, i když má uživatel více monitorů. Konkrétní rozumné rozměry zvol ty.

**AI:** Opravu mám zaznamenanou: bez maximálního omezení, s rozumnou minimální a výchozí velikostí a se zobrazením uprostřed aktivního monitoru. Pokračuj.

**User:** Zpracuj intent.

**AI:** Potřebuji vyjasnit jediný detail: když uživatel uloží Edit Config ve stavu odpojeno, má se editor po úspěšném uložení zavřít úplně bez hlášky? Navržený výchozí stav je ano, zavřít bez hlášky. Pokud to výslovně nezměníš, použiji tuto variantu.

**User:** Ano, ten editor by se měl zavřít bez hlášky, pokud uživatel není připojen.
