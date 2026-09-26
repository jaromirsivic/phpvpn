---
intent_version: 2
created_at: 2026-09-26-07-53-58
based_on: intent-v001.md
produces: intent-v002.md
---

# Záznam původní konverzace

Tento záznam zachycuje viditelnou konverzaci od založení intentu po upřesnění před jeho zpracováním. Krátké nedokončené úseky jsou ponechány jako přepis řeči.

**User:** Vytvoř nový intent.

**AI:** Založím nový intent a ověřím postup, jak ho správně zachytit; pak mi můžeš nadiktovat zadání.

**AI:** Hotovo, je to založené pro projekt phpvpn. Můžeš začít diktovat.

**User:** Tak, uprav instalátor a taky tray aplikaci takovým způsobem, a pravděpodobně to asi uděláš přes nějaký parametr v command line.

**AI:** Mhm.

**User:** Uprav to takovým způsobem, že když se poprvé nainstaluje ta tray aplikace, to znamená ten klient PHPVPN do systému, a nemusí to být poprvé, protože ta aplikace tam mohla být nainstalovaná a odinstalovaná mnohokrát, ale zkrátka poté, co doběhne instalace, tak se zobrazí taková ta bublina, která se zobrazuje, když tray aplikace má nějakou informaci pro uživatele. A v té bublině bude napsáno pěkně, neříkám PR nebo marketingově, ale pěkně tam bude napsáno, že PHPVPN klient byl nainstalován a uživatel může pokračovat dál konfigurací nebo editací configu a následním připojením pomocí Connect. Tohleto dej do nějaké hezké věty, angličtině, tak maximálně 40-50 slov. A ta bublina se zobrazí na 30 vteřin nebo samozřejmě kratší dobu, pokud to uživatel zavře křížkem, ale výchozí hodnota bude 30 vteř…

**AI:** Jasně, rozumím, jdu na to.

**AI:** Můžeš pokračovat v diktování, beru to do nového intentu.

**User:** Čistě teoreticky a nevím, jestli je to možné, můžeš tu bublinu taky zobrazit přímo z instalátoru, aby prostě jenom ta bublina svítila nad tou ikonou toho PHPVPN v tray anebo pokud je ten PHPVPN v tray schované pod takovou tou šipkou vpravo, tak prostě, tak zase zmiz…

**AI:** Můžeš navázat, kdykoli budeš chtít.

**User:** Zpracuj intent.

**AI:** Instalátor už po úspěšné instalaci tray aplikaci spouští, takže bublinu může vyvolat právě její spuštění po instalaci. Když je ikona schovaná pod šipkou, má se oznámení přesto zobrazit a pak samo zmizet po třiceti sekundách, pokud ho uživatel nezavře dřív? Beru to tak jako výchozí výklad.

**User:** Ano.

V přepisu řeči se objevily také samostatné nedokončené úseky „in“, „í“ a „. Což“. Neobsahovaly další určitelný požadavek.


**AI-created artifact: intent-v001.md**

---
intent_version: 1
created_at: 2026-09-26-07-39-40
source: intent-chat-v001.md
---

# Oznámení po instalaci klienta PHPVPN

## Shrnutí

Po každé úspěšně dokončené instalaci klienta PHPVPN zobrazit uživateli informační oznámení spojené s ikonou tray aplikace. Platí to i při opětovné instalaci po odinstalování nebo při další instalaci, nikoli pouze při prvním použití systému. Text má potvrdit instalaci a krátce navést ke konfiguraci a k následnému připojení.

## Požadované chování

- Oznámení se zobrazí až po úspěšném dokončení instalace. Při neúspěšné instalaci se nezobrazí.
- Použít podobu informační bubliny tray aplikace PHPVPN, viditelnou i tehdy, když je její ikona skrytá pod šipkou v oznamovací oblasti Windows.
- Výchozí doba zobrazení je 30 sekund. Uživatel může oznámení zavřít křížkem dříve.
- Samotný běžný start tray aplikace mimo dokončení instalace toto oznámení nevyvolá.
- Text oznámení bude anglický, přirozený, věcný a nemarketingový, nejvýše 50 slov. Musí říci, že klient PHPVPN byl nainstalován, že lze použít Configuration Wizard nebo Edit Config, a poté Connect.

## Navržený text

Nadpis: **PHPVPN installed**

Tělo: **PHPVPN client is installed. To get started, open Configuration Wizard or Edit Config to set up your connection, then choose Connect when you're ready.**

## Kontext projektu

- Instalační skript již po instalaci spouští `phpvpn-tray.exe` (`client/tools/package/install.ps1`, řádky 374 až 378).
- Tray aplikace vytváří oznamovací ikonu při inicializaci (`client/apps/tray/tray_application.cpp`, řádky 78 až 108) a obnovuje ji přes `ShellIcon` (`client/apps/tray/tray_application.cpp`, řádky 241 až 260).
- Spuštění tray aplikace zatím nepřijímá účelový parametr pro instalační oznámení (`client/apps/tray/main.cpp`, řádky 5 až 17).

## Pokyny pro realizaci

- Propojit úspěšné dokončení instalátoru s tray aplikací tak, aby oznámení vycházelo z její ikony. Parametr příkazové řádky je uživatelem navržená možnost, ne závazný mechanismus.
- Zohlednit stav, kdy tray aplikace již běží, aby se oznámení zobrazilo bez druhé aktivní instance a nezdvojilo se.
- Ověřit skutečné chování oznámení ve Windows včetně skryté ikony, třicetisekundové doby a možnosti dřívějšího zavření. Pokud systémová bublina nerespektuje dobu nebo umístění, popsat omezení a zvolit nejbližší řešení odpovídající požadovanému chování.

## Přijetí

1. Po úspěšné instalaci i opětovné instalaci se oznámení zobrazí bez další akce uživatele.
2. Po neúspěšné instalaci a při běžném spuštění tray aplikace se instalační oznámení nezobrazí.
3. Oznámení obsahuje anglický text uvedený výše nebo věcně rovnocennou variantu do 50 slov.
4. Oznámení je viditelné i se skrytou ikonou a standardně zmizí po 30 sekundách nebo po dřívějším zavření uživatelem, pokud toto chování platforma umožňuje.

**AI:** Hotovo, tady je hotový intent k otevření.

**AI:** A tady máš ještě jednou odkaz, chceš rovnou realizovat změnu, nebo nejdřív připravit plán?

**User:** Co navrhuješ? Má se to rovnou implementovat nebo naplánovat?

**AI:** V intentu je výsledek jasný a instalační skript už tray aplikaci spouští, takže bych to rovnou implementovala. Při realizaci se jen ověří, jak přesně Windows zachází s délkou a umístěním oznámení. Chceš zvolit přímou realizaci?

**User:** Ano, ať to udělá a Astralium.

**AI:** Jen si ověřím výraz „Astralium“, není mi jasné, jaký model a úsilí mám pro nový realizační chat zvolit. Který model a jakou úroveň úsilí chceš?

**User:** Astra Medium.

**AI:** Jaký přesně model a úsilí tedy chceš? Myslíš Astra se středním úsilím?

**User:** Ano, Astra z effortem medium. Už jsem to psal líše.

**AI:** Jasně, beru Astra s úsilím medium.

**User:** Až bude implementace vytvořená, tak je, tak nech udělat distribuci.

**AI:** Implementace už běží; doplním do stejného chatu i požadavek na vytvoření distribuce po dokončení změny. Projektový chat už ten navazující úkol má.

**AI:** Ještě upřesním jednu věc: systémová bublina ve Windows nerespektuje pevnou dobu zobrazení, takže se teď připravuje oznámení v tray aplikaci s vlastním třicetisekundovým časovačem a křížkem pro zavření.

**User:** Ne, nemusí to tam být takhle jako custom, a nebo jinak, nemusí to tam být custom, ale udělejme to, aby to fungovalo potom i v Linuxu a v macOS, takže to nemusí mít čistě teoreticky žádné časové omezení. Prostě ta bublina tam může být zobrazená jakkoliv dlouho. Nejde mi o to, aby tam byla třicet vteřin. Takže klidně ji zobraz do okamžiku, než jí odklikne uživatel. Ale pokud, pokud na macOS a Linuxu nic podobného není a museli bychom to tam custom implementovat, tak zvaž, jestli neudělat obecnou custom implementaci, která se potom použije, přepoužije na Linuxu i na macOS. A řekni mi, jak to teda budeš dělat.

**AI:** Ověřila jsem, že klient se zatím sestavuje jen pro Windows, ale Linux i macOS mají vlastní systémová oznámení. Doporučuji společné rozhraní v tray aplikaci a nativní oznámení pro každý systém. Délku pak řídí prostředí uživatele; společné vlastní okno zatím nedává smysl.



