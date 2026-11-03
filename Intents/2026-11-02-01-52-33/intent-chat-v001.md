---
intent_version: 1
created_at: 2026-11-02-01-52-33
timezone: Europe/Berlin
produces: intent-v001.md
---

# Zdroj zadání pro diagnostiku tunelu a logování

## Konverzace od aktivace formalizace

Formální zpracování tohoto intentu bylo aktivováno následujícím požadavkem. Níže je zachovaný uživatelský požadavek a následné upřesnění dostupné při sepsání. Předchozí diagnostika je shrnuta v samostatné části, není vydávána za doslovný přepis všech předchozích hlasových replik.

### User

Vytvoř mi formální zadání pro to, jak doladit logování s popisem té chyby, kterou řešíme, a ulož ho do Markdownu někam na vhodné místo a dej mi sem absolutní cestu k tomu Markdownu.

### AI

Sepíšu to do Markdownu a pak ti řeknu, kde přesně to leží.

### User

Restartoval jsem službu PHP VPN client a ani potom se to nespravilo.

## Kontext z předchozí diagnostiky

- Uživatel požádal zjistit příčinu chyby PHPVPN ve virtuálním stroji a možnost, jak jí zabránit. Dodal screenshot.
- Screenshot zobrazoval čtyři úspěšné kontroly a následnou chybu `Cannot start the VPN tunnel`.
- Uživatel dovolil prohlédnout běžící virtuální počítač a získat jeho logy. Okno Hyper-V bylo dostupné ke čtení obrazovky, ovládání bylo omezené vyšší úrovní oprávnění. Další navigaci prováděl uživatel podle pokynů.
- Průzkumník ukázal jediný soubor služby o velikosti 0 bajtů v datové složce logů. Opakované připojení a obnovení složky nepřineslo záznam.
- Uživatel výslovně upozornil, že v tomto místě bude nutné zlepšit logování.
- Bylo ověřeno, že nainstalovaná verze má vedle služby soubor `wintun.dll`.
- V aktuálně zobrazeném seznamu síťových adaptérů nebyl adaptér PHPVPN.
- Log SetupAPI obsahoval opakované odstraňování stejného zařízení Wintun službou PHPVPN. U některých operací se objevovalo `CR_INVALID_DEVNODE`, ale končily `SUCCESS`. Jeden starší záznam uváděl úspěšné Query-and-Remove.
- Uživatel doplnil, že za provozu změnil komunikační kontrakt, provedl Disconnect a Connect a současně protistrana pingovala jeho virtuální IP.
- Uživatel se ptal, zda se ovladač samovolně neodinstaloval. Bylo vyjasněno, že absence adaptéru tuto hypotézu nepotvrzuje a že ovladač a konkrétní adaptér jsou odlišné věci.
- Uživatel se ptal na případný zbylý stav na serveru. Tato možnost nebyla potvrzena. V aktuálních zdrojích vlastní start tunelu následuje po kontrolách serveru a zahrnuje lokální operace.
- V aktuálním runtime byla nalezena větev, která při neúspěšném připojení převádí původní chybu na veřejný bezpečný kód bez přímého zapsání původní příčiny v této větvi. Není tím vysvětlen celý nulový soubor.
- Přesná příčina selhání tunelu, vliv pingu, serveru ani rozdíl mezi nasazeným a aktuálním buildem nebyly prokázány.

## Rozsah nynější autorizace

Uživatel požádal o vytvoření a uložení formálního zadání v Markdownu a o absolutní cestu k výsledku. Nezadává tím implementaci změn, změnu konfigurace ani restart serveru. Poslední zpráva doplňuje důležitý výsledek ručního testu, který musí být v zadání zahrnut.
