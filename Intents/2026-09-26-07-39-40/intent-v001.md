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
