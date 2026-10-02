---
intent_version: 1
created_at: 2026-09-29-22-10-40
source: intent-chat-v001.md
---

# Server PHPVPN pro Node.js s diskovým úložištěm

## Shrnutí

Vytvořit samostatně nasaditelnou serverovou implementaci PHPVPN pro Node.js. Má plně implementovat stávající drátový protokol a spolupracovat se stávajícím klientem. Přednost má jeden JavaScriptový soubor obdobný `phpvpn.php`. V této fázi používat pouze diskové úložiště. Perzistentní fronty a související stav nesmějí být vedené jen v paměti, v Memcached ani v Redis.

## Výslovné požadavky uživatele

- Serverová část má být určena pro provoz na Node.js a funkčně odpovídat stávajícímu `phpvpn.php` z pohledu protokolu a klienta.
- Upřednostnit jediný nasaditelný JavaScriptový soubor. Jde o preferenci, nikoli výslovný zákaz podpůrných souborů, pokud by byly nezbytné.
- Implementovat celý protokol, nikoli jen základní nebo šťastnou cestu komunikace.
- V první verzi ukládat data na disk. Nezavádět paměťový storage driver ani podporu Memcached či Redis. Přechodné pracovní objekty a buffery potřebné ke zpracování jednoho požadavku nejsou zamýšleným zákazem běžné operační paměti.
- Nevyžadovat kompatibilitu diskového formátu a adresářů PHP implementace. Výměna PHP za Node.js nad existujícím adresářem nemusí převzít uložená data.
- Ověřit testy, že stávající klient s Node.js serverem správně komunikuje.

## Kontext ověřený v projektu

- Normativním zdrojem drátového chování je `protocol.md`. Popisuje čtyři endpointy včetně provozní stránky, `health`, `info` a `data`, autentizaci, kryptografii, limity, chyby, diskové fronty, částečné přijetí a at-most-once vydání.
- Současná implementace `phpvpn.php` obsahuje mimo jiné směrování požadavků, validaci, kryptografii, diskové úložiště, správu front a handlery tří protokolových akcí. Slouží jako implementační reference, zatímco shodu určuje `protocol.md`.
- `tests-integration` již sdílí protokolové scénáře pro PHP a Python server a obsahuje možnost testovat kontrakt skutečného C++ klienta proti serverové fixture. Nový Node.js profil lze do této struktury přidat.

## Funkční rozsah

1. Poskytovat protokolem předepsané HTTP metody, URL, typy zpráv, kódování, hlavičky, úspěšné a chybové odpovědi. Zachovat wire kompatibilitu potřebnou pro nezměněného klienta, včetně cesty `/phpvpn.php` nebo jejího rovnocenného zpřístupnění na klientem používané URL.
2. Dodržet přesnou autentizaci, odvozování klíčů, kanonizaci, časové kontroly a integritu odpovědí pro `health`, `info` a `data` podle `protocol.md`. Omezení na softwarovou AES-GCM implementaci v čistém PHP se vztahuje na PHP, nikoli automaticky na Node.js.
3. Implementovat diskový backend se správnou kapacitou, expirací, atomickou publikací a převzetím dávek, zamykáním nebo ekvivalentní bezpečnou koordinací při souběhu, částečným přijetím, údržbou a předepsanými statistikami klientů. Funkčnost má přetrvat restart procesu v rozsahu protokolové sémantiky.
4. Zajistit bezpečné nasazení diskového úložiště, aby jeho soubory nebyly veřejně dostupné přes HTTP a aby odpovědi ani logy neprozrazovaly tajemství nebo interní cesty.
5. Poskytnout srozumitelnou konfiguraci a postup spuštění Node.js serveru. Zdokumentovat podporovanou verzi Node.js, potřebné nastavení HTTP nebo reverzní proxy, umístění diskových dat a způsob zadání serverového tajemství a přístupových klíčů. Konfigurační formát se nemusí shodovat s PHP, pokud se zachová protokolové chování.

## Mimo aktuální rozsah

- Paměťový storage driver, Memcached a Redis.
- Migrace existujících souborů, front či adresářů z `phpvpn.php` a zpětná kompatibilita jejich interního formátu.
- Změny drátového protokolu nebo nutnost upravovat klienta pouze kvůli volbě Node.js serveru.

## Ověření a kritéria přijetí

- Společné integrační scénáře specifikace běží také proti izolované Node.js diskové fixture, včetně chybových větví, kryptografie, limitů, front, souběhu, expirace a restartu tam, kde jsou pro disk relevantní.
- Skutečný klientský kontrakt a praktická výměna `health`, `info` a `data` se stávajícím klientem projdou proti Node.js serveru. Ověření zahrne odeslání a příjem dat mezi klienty, nikoli jen odpověď provozní stránky.
- Server vrací stejný protokolový význam jako PHP implementace i tehdy, když jeho vlastní diskový formát a nasazení jsou odlišné.
- Instalace a spuštění jsou popsány tak, aby šlo Node.js variantu samostatně nasadit. Výsledný počet nasaditelných souborů a případné závislosti jsou výslovně uvedeny.

## Vodítka pro realizaci

- Při rozporu mezi chováním PHP implementace a `protocol.md` dát přednost normativní specifikaci a rozpor vyřešit testem.
- Jediný JavaScriptový soubor a vestavěné moduly Node.js jsou vhodný výchozí návrh, pokud jimi lze zachovat úplnou protokolovou shodu a bezpečné diskové operace.
- Rozšířit stávající testovací infrastrukturu o Node.js profil, aby se co nejvíc zachovaly stejné požadavky napříč implementacemi.
