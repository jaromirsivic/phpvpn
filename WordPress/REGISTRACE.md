# Podklady k podání VPN Server

Stav: připraveno k vlastní kontrole, dosud nepodáno a nepublikováno. Podání provede vlastník účtu až po samostatném rozhodnutí. Výsledek lidské revize nelze zaručit.

## Potvrzené údaje

| Položka | Hodnota |
| --- | --- |
| Název pluginu | VPN Server |
| Požadovaný slug a text domain | `vpn-server` |
| První podávaná verze | `1.0.4`, zachována shoda s aktuálním PHPVPN serverem |
| Autor | Jaromír Šivic, samostatný vývojář |
| Účet WordPress.org | `pilandrome` |
| Web autora | https://www.pilandrome.com |
| Licence | MIT, kompatibilní s GPL |
| Práva ke kódu a tutorial.png | Vlastník výslovně potvrdil oprávnění k publikaci pod MIT |
| Kontaktní e-mail účtu | ______________________________ |
| Dvoufaktorové ověřování účtu | Před podáním ověřit, stav účtu nebyl čten |
| E-mail pro bezpečnostní hlášení, pokud jej chceš zveřejnit | ______________________________ |
| URL pluginu po schválení | Doplní WordPress.org podle přiděleného slugu |

Doména není prezentována jako samostatná společnost. Uživatelské jméno není e-mail ani doména. Heslo, kódy 2FA, záložní kódy ani SVN heslo do tohoto dokumentu nepatří. Zadávají se pouze přímo do přihlašovacího rozhraní WordPress.org nebo bezpečného správce přihlašovacích údajů.

## Před odesláním

1. Projdi `OVERENI.md`, skutečné výsledky v `checks/` a otevřené body. Pokud se změní obsah pluginu, znovu sestav ZIP a ověř jej.
2. Přihlas se jako `pilandrome` a ověř aktuální e-mail účtu. Musíš na něm přijímat a osobně zodpovídat zprávy týmu pluginů. Nenastavuj automatické odpovědi ani přesměrování do ticketového systému.
   Pro vlastníka pluginu a nové podání je vyžadované dvoufaktorové ověřování. Pokud není zapnuté, nastav jej přímo v účtu podle [oficiálního návodu](https://make.wordpress.org/meta/handbook/tutorials-guides/configuring-two-factor-authentication/). Záložní kódy ulož soukromě. [Oznámení povinného 2FA a Plugin Check](https://make.wordpress.org/plugins/2024/10/01/plugin-check-and-2fa-now-mandatory-for-new-plugin-submissions/).
3. Ověř, že název a slug chceš zachovat. `VPN Server` je obecný název a recenzent může požadovat odlišující název. Před schválením lze požádat o úpravu slugu, po schválení jej nelze běžně přejmenovat. Kontrola API 2. 11. 2026 vrátila pro slug 404, což není rezervace ani záruka přidělení.
4. Zkontroluj aktuální dostupnost klientských ZIPů a zdrojového kódu z `readme.txt`. Nepotřebuješ nahrávat desktopové klienty do WordPress pluginu.
5. Ověř popis a limity produktu. Jde o autentizovaný relay mezi vlastními klienty v soukromé IPv4 síti, nikoli anonymizér nebo internetovou výstupní VPN.
6. Projdi [pravidla adresáře](https://developer.wordpress.org/plugins/wordpress-org/detailed-plugin-guidelines/) a [běžné chyby](https://developer.wordpress.org/plugins/wordpress-org/common-issues/). MIT je záměrně zachovaná, není nutná změna na GPL.

## Vlastní podání, až je výslovně schválíš

1. Otevři [Add your plugin](https://wordpress.org/plugins/developers/add/) a přihlas se na WordPress.org.
2. Nahraj pouze `vpn-server-1.0.4.zip` z této složky. Jeho kořenem je jediná složka `vpn-server/`. Nenahrávej celý projekt, tento registrační dokument ani složku `work`.
3. Potvrď prohlášení formuláře jen podle skutečnosti. Obsah formuláře po přihlášení se může měnit. Automatické kontroly při podání mohou být přísnější nebo novější než místní Plugin Check.
4. Je-li k dispozici pole pro poznámku, použij anglický text `REVIEWER-NOTES.txt`. Jinak ho uchovej pro odpověď na e-mail recenzenta. Tento dokument není náhradou uploadu ZIPu.
5. Uchovej potvrzení podání a přidělený slug. Případné nálezy oprav v udržovaném zdroji, přegeneruj core, sestav ZIP a znovu proveď kontroly. Nevytvářej druhé podání téhož pluginu kvůli čekání.

## Po případném schválení

Schválení samo o sobě není publikace balíčku. Řiď se konkrétním e-mailem týmu pluginů a [návodem pro SVN](https://developer.wordpress.org/plugins/wordpress-org/how-to-use-subversion/).

Pro SVN si v nastavení účtu vytvoř samostatné SVN heslo. Neukládej jej do projektu, skriptů ani tohoto dokumentu. Účet musí mít zapnuté 2FA, samotný SVN protokol ho nenahrazuje. Podrobnosti jsou v [oficiálních pravidlech přístupu do SVN](https://make.wordpress.org/meta/handbook/tutorials-guides/svn-access/).

- Obsah adresáře `vpn-server/` z ověřeného ZIPu patří přímo do SVN `trunk/`, bez další vnořené složky.
- Stejný obsah patří do `tags/1.0.4/`. `Stable tag: 1.0.4` musí souhlasit s touto značkou i hlavičkou pluginu.
- Při jiném přiděleném slugu nejprve uprav text domain, názvy balíčku a související metadata. Nepublikuj soubor s neodpovídajícím slugem.
- Marketingové obrázky patří do kořenového SVN `assets/`, nikoli do `trunk/assets/`. Runtime `assets/` uvnitř pluginu musí naopak zůstat součástí pluginu.
- Bannery a vlastní ikona nejsou nutné pro prvotní podání. Pokud je doplníš, použij vlastní či řádně licencované podklady. Velikosti: banner 772 × 250 a volitelně 1544 × 500, ikona 128 × 128 a 256 × 256. Screenshoty `screenshot-1.png` atd. musejí odpovídat sekci Screenshots v readme. Do veřejných obrázků nepatří skutečné klíče ani údaje uživatelů.
- Před zápisem do SVN proveď `svn status` a `svn diff`. Publikační commit a případné nastavení vydání v účtu udělej až na další výslovný pokyn.

Po publikaci sleduj bezpečnostní hlášení a podporu, udržuj aktuální kontaktní údaje a zvyšuj verzi při každém novém vydání.

## Oficiální zdroje ověřené 2. listopadu 2026

- [Podání a správa pluginů](https://developer.wordpress.org/plugins/wordpress-org/planning-submitting-and-maintaining-plugins/)
- [Hlavička pluginu](https://developer.wordpress.org/plugins/plugin-basics/header-requirements/)
- [Formát readme](https://developer.wordpress.org/plugins/wordpress-org/how-your-readme-txt-works/)
- [Plugin Check](https://wordpress.org/plugins/plugin-check/)
- [Grafické podklady](https://developer.wordpress.org/plugins/wordpress-org/plugin-assets/)
- [Archiv verzí WordPress](https://wordpress.org/download/releases/)

Před odesláním znovu ověř aktuální znění, protože pravidla a kontroly se mohou změnit.
