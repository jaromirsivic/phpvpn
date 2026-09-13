---
intent_version: 1
created_at: 2026-09-13-10-16-36
source: intent-chat-v001.md
---

# Povinné vlastní HTTP hlavičky v konfiguraci klienta

## Shrnutí

Rozšířit klientskou konfiguraci o povinnou položku masterSetup.httpHeaders. Hodnota bude řetězec obsahující jeden nebo více řádků HTTP hlaviček. Dekódované hlavičky se přidají ke každému HTTP požadavku klienta na PHPVPN, tedy k akcím health, info a data. Health zahrnuje také získání ověřeného serverového času pro synchronizaci.

Výchozí konfigurace vytvořená při první instalaci služby nebo při resetu musí obsahovat předepsané tři hlavičky a nové výchozí pořadí položek v masterSetup. Pořadí je pouze prezentační vlastnost výchozího dokumentu. Parser a běh aplikace musí zůstat na pořadí JSON členů nezávislé.

## Ověřený kontext projektu

- Sdílenou kompletní výchozí konfiguraci pro start služby a explicitní vytvoření nebo reset editoru definuje kDefaultConfigDocument v client/src/core/config/config.h:35-54.
- Validace konfigurace kontroluje povolené členy masterSetup a vytváří ConfigValues v client/src/core/config/config_validator.cpp:119-318.
- Přenášené hodnoty konfigurace jsou v ConfigValues v client/src/core/config/config_document.h:36-47.
- Runtime předává síťové nastavení transportu v NetworkWorker::configure v client/src/core/runtime/network_worker.cpp:33-63.
- Wizard používá vlastní konfiguraci transportu v ProbeClient::prepare v client/src/core/wizard/probe_client.cpp:11-26.
- WinHTTP transport přijímá TransportSettings z client/src/platform/iface/transport.h:26-32, otevírá request v client/src/platform/windows/transport_winhttp.cpp:164-194 a nyní odesílá pouze aplikační Content-Type v client/src/platform/windows/transport_winhttp.cpp:243-264.
- Diagnostika rekonstruuje aplikační a systémové request hlavičky v client/src/core/wizard/http_diagnostic.cpp:23-48.
- Tray editor pracuje s celým JSON dokumentem a při uložení jej odesílá službě v client/apps/tray/tray_application.cpp:321-326. Formátovač zachovává pořadí členů a escapování řetězců v client/apps/tray/editor_window.cpp:20-58.
- NetworkWorker::performHealth v client/src/core/runtime/network_worker.cpp:82-127 ověří health odpověď, vezme její timestampMs, vytvoří TimeSnapshot a publikuje jej do TimeSync.
- TimeSync::requestTimestampMs v client/src/core/runtime/time_sync.cpp:56-76 projektuje ověřený serverový čas podle monotónně uplynulé doby a odečítá bezpečnostní rezervu.

## Požadované změny

### Schéma a výchozí dokument

1. Přidat do objektu masterSetup povinný člen přesně pojmenovaný httpHeaders.
2. Hodnota httpHeaders je JSON string. Po dekódování představuje víceřádkový blok HTTP hlaviček.
3. Výchozí dokument musí použít toto pořadí prvních členů masterSetup:
   1. serverSecret
   2. serverUrl
   3. httpHeaders
   4. trustAnyTlsCertificate
   5. zbývající dosavadní členy v jejich dosavadním relativním pořadí
4. Výchozí hodnota musí po JSON dekódování odpovídat tomuto bloku:

~~~http
Cache-Control: no-cache
Pragma: no-cache
User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:155.0) Gecko/20100101 Firefox/155.0
~~~

5. Ve fyzickém JSON textu musí být oddělovače řádků escapované. Výchozí serializace má použít \r\n, takže například hodnota bude mít tvar:

~~~json
"httpHeaders": "Cache-Control: no-cache\r\nPragma: no-cache\r\nUser-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:155.0) Gecko/20100101 Firefox/155.0"
~~~

6. Povinné je jméno a přítomnost členu, nikoli jeho pozice. Platný dokument s libovolně přeuspořádanými členy musí být přijat a musí mít stejný význam.
7. Výchozí dokument se použije při prvním vytvoření konfigurace službou i při resetu nebo explicitním vytvoření výchozí konfigurace.

### Datový tok konfigurace

1. Rozšířit validaci známých členů masterSetup a načíst httpHeaders jako povinný string.
2. Uchovat dekódovanou hodnotu v konfiguračním modelu a předat ji do všech transportních instancí.
3. Zahrnout ji jak do běžného runtime transportu služby, tak do transportu používaného wizardem a jeho ověřovacími požadavky.
4. Operace wizardu, které upravují jiné konfigurační položky, musí httpHeaders zachovat.
5. Není požadováno nové samostatné formulářové pole v tray aplikaci. Tray a editor se mají změnit pouze tam, kde je to nutné pro správné vytvoření, zobrazení, úpravu, zachování a uložení celého JSON dokumentu.

### Odesílání požadavků

1. Přidat dekódovaný blok httpHeaders ke každému requestu pro HttpAction::health, HttpAction::info a HttpAction::data.
2. To zahrnuje počáteční health, pravidelné nebo obnovovací health požadavky, info, běžný datový polling i diagnostické a wizard probe varianty těchto požadavků.
3. Zachovat transportem řízený Content-Type pro jednotlivé akce a správné HTTP rámování requestu.
4. Hlavičky nakonfigurované uživatelem musí umožnit běžné případy jako Cookie, User-Agent, Cache-Control a Pragma.
5. Diagnostický výpis requestu musí obsahovat skutečně použitý nebo korektně rekonstruovaný blok aplikačních hlaviček včetně httpHeaders.

## Omezení a realizační vodítka

- Změna se týká klienta. Serverový PHP endpoint se kvůli ní nemění.
- Hlavičky jsou konfigurační data. Nesmějí měnit HTTP metodu, cílovou URL, tělo ani oddělení hlaviček od těla.
- Validace musí odmítnout nesprávný JSON typ a syntakticky nebezpečný nebo neplatný hlavičkový blok tak, aby nebylo možné vložit další request line nebo tělo. Konkrétní bezpečné sloučení s hlavičkami vlastněnými WinHTTP má respektovat stávající transportní kontrakt.
- Implementace nesmí odvozovat význam členů JSON objektu z jejich pořadí.
- Změna má zachovat stávající diagnostiku, časové limity, zrušení requestu, TLS chování a účtování přenesených dat.

## Pravděpodobně dotčené oblasti

- client/src/core/config/config.h
- client/src/core/config/config_document.h
- client/src/core/config/config_validator.cpp
- client/src/core/config/config_document.cpp
- client/src/core/runtime/network_worker.cpp
- client/src/core/wizard/probe_client.cpp
- client/src/platform/iface/transport.h
- client/src/platform/windows/transport_winhttp.cpp
- související unit, contract, wizard, runtime a WinHTTP testy
- případně tray editor a diagnostické zobrazení, pokud testy odhalí nutnou adaptaci

## Akceptační kritéria

1. Nově vytvořená nebo resetovaná konfigurace obsahuje povinné masterSetup.httpHeaders s přesnými třemi výchozími hlavičkami.
2. Výchozí pořadí v masterSetup je serverSecret, serverUrl, httpHeaders, trustAnyTlsCertificate a poté dosavadní zbývající členy.
3. Stejná konfigurace s jiným pořadím členů se validuje a funguje identicky.
4. Chybějící httpHeaders způsobí jednoznačnou validační chybu povinné položky.
5. Jiný JSON typ než string nebo neplatný hlavičkový blok je odmítnut jednoznačnou validační chybou.
6. Na server dorazí nakonfigurované hlavičky při health, info i data requestu.
7. Health request s vlastními hlavičkami nadále ověří serverový timestamp a publikuje časový snapshot. Následná tvorba časových údajů pro autentizované requesty zůstane funkční.
8. Wizard a diagnostické požadavky používají stejné vlastní hlavičky jako runtime a diagnostika je umí zobrazit.
9. Úprava a uložení konfigurace přes existující editor zachová httpHeaders i uživatelské pořadí členů.
10. Automatické testy pokryjí výchozí dokument, povinnost a typ položky, nezávislost na pořadí, více hlaviček a jejich escapování, přenos ve všech třech akcích, wizard probe a diagnostickou rekonstrukci.
