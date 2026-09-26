---
intent_version: 2
created_at: 2026-09-26-07-53-58
based_on: intent-v001.md
source: intent-chat-v002.md
---

# Oznámení po instalaci klienta PHPVPN

## Shrnutí

Po každé úspěšné instalaci klienta PHPVPN zobrazit informační systémové oznámení s přirozeným anglickým textem o dalších krocích. Způsob doručení navrhnout tak, aby se dal později použít také v klientských aplikacích pro Linux a macOS. Po realizaci vytvořit distribuci podle stávajícího postupu projektu.

## Výslovné požadavky

- Oznámení se objeví po úspěšném dokončení instalace, včetně opakované instalace. Neobjeví se při selhání instalace ani při běžném spuštění aplikace.
- Na Windows bude oznámení spojené s tray aplikací a bude viditelné i při ikoně skryté pod šipkou oznamovací oblasti.
- Zobrazení nemá pevně stanovenou délku. Třicetisekundový limit z předchozí verze intentu je zrušen. O délce může rozhodovat systém nebo uživatel zavřením oznámení.
- Text bude anglický, přirozený, věcný a nemarketingový, nejvýše 50 slov. Musí sdělit úspěšnou instalaci a odkázat na Configuration Wizard nebo Edit Config a potom na Connect.
- Podoba a struktura implementace má umožnit pozdější využití oznámení v Linuxu a macOS. Pokud nativní systémová oznámení pro tyto platformy vyhovují, není potřeba společné vlastní okno. Pokud by budoucí platformy vyžadovaly vlastní UI, znovu zvážit obecnou přenositelnou implementaci.
- Po dokončení implementace vytvořit distribuci projektu a sdělit její umístění a stav ověření.

## Text oznámení

Nadpis: **PHPVPN installed**

Tělo: **PHPVPN client is installed. To get started, open Configuration Wizard or Edit Config to set up your connection, then choose Connect when you're ready.**

## Kontext projektu

- Instalační skript spouští `phpvpn-tray.exe` po instalaci (`client/tools/package/install.ps1`, řádky 374 až 378).
- Tray aplikace vytváří ikonu při inicializaci (`client/apps/tray/tray_application.cpp`, původně řádky 78 až 108).
- Současný klient je výhradně pro Windows: `client/CMakeLists.txt`, řádky 12 až 13, odmítá ostatní platformy. Tento intent nepožaduje kompletní port klienta na Linux nebo macOS.
- Windows Shell_NotifyIcon poskytuje systémová oznámení a délku jejich zobrazení v aktuálních Windows řídí systém. Freedesktop Desktop Notifications jsou standardní mechanismus na Linuxu; macOS má framework UserNotifications. Jsou to podklady pro volbu adaptérů, nikoli požadavek tyto budoucí adaptéry realizovat v nynější Windows distribuci.

## Pokyny pro realizaci

- Zachovat odlišení instalačního spuštění od běžného startu a doručení požadavku již běžící instanci tray aplikace bez duplicity.
- Použít systémové oznámení Windows a přiměřenou abstrakci pro případné platformní adaptéry. Nevytvářet vlastní Windows UI pouze kvůli kontrole doby zobrazení.
- Ověřit instalační scénáře, text a chování při skryté ikoně v rozsahu dostupného prostředí. Skutečné zobrazení a životnost závisí na nastavení oznámení uživatele a systému; neprezentovat je jako přesně zaručené.
- Po úspěšném sestavení vytvořit distribuci zavedeným postupem projektu.

## Kritéria přijetí

1. Po úspěšné instalaci nebo opětovné instalaci se zobrazí jedno oznámení bez další akce uživatele.
2. Po neúspěšné instalaci a při běžném spuštění tray aplikace se instalační oznámení nezobrazí.
3. Anglický text splňuje požadavky výše.
4. Běžná Windows distribuce obsahuje funkční řešení s rozhraním připraveným pro budoucí Linux a macOS implementace, bez pevného třicetisekundového časovače.
5. Distribuční balík je vytvořen a jeho ověření je zdokumentováno.
