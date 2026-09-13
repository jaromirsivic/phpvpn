---
intent_version: 2
created_at: 2026-09-13-10-29-25
based_on: intent-v001.md
source: intent-chat-v002.md
---

# Povinné vlastní HTTP hlavičky v konfiguraci klienta

## Shrnutí

Rozšířit klientskou konfiguraci o povinnou položku masterSetup.httpHeaders. Hodnota bude řetězec obsahující jeden nebo více řádků HTTP hlaviček. Dekódovaný uživatelský blok se logicky připojí k dosavadním HTTP hlavičkám každého požadavku klienta na PHPVPN, tedy pro akce health, info a data.

Dosavadní transportem řízené Content-Type, Connection a Content-Length zůstanou. Odstraní se pouze implicitní User-Agent PHPVPN/1.0 vytvářený identifikací WinHTTP relace. User-Agent bude nadále přítomen ve výchozí hodnotě httpHeaders a uživatel jej bude moci změnit nebo odebrat úpravou konfigurace.

Výchozí konfigurace vytvořená při první instalaci služby nebo při resetu musí obsahovat předepsané tři uživatelské hlavičky a nové výchozí pořadí položek v masterSetup. Pořadí členů JSON ani pořadí hlaviček s různými názvy není kontrakt.

## Ověřený kontext projektu

- Sdílenou kompletní výchozí konfiguraci pro start služby a explicitní vytvoření nebo reset editoru definuje kDefaultConfigDocument v client/src/core/config/config.h:35-54.
- Validace konfigurace kontroluje povolené členy masterSetup a vytváří ConfigValues v client/src/core/config/config_validator.cpp:119-318.
- Přenášené hodnoty konfigurace jsou v ConfigValues v client/src/core/config/config_document.h:36-47.
- Runtime předává síťové nastavení transportu v NetworkWorker::configure v client/src/core/runtime/network_worker.cpp:33-63.
- Wizard používá vlastní konfiguraci transportu v ProbeClient::prepare v client/src/core/wizard/probe_client.cpp:11-26.
- TransportSettings jsou v client/src/platform/iface/transport.h:26-32.
- WinHTTP relace se nyní otevírá s identifikací PHPVPN/1.0 v client/src/platform/windows/transport_winhttp.cpp:120-148. Tato identifikace je zdrojem implicitního User-Agentu.
- Request se otevírá v client/src/platform/windows/transport_winhttp.cpp:164-194. Aplikace explicitně předává Content-Type přes WinHttpSendRequest v client/src/platform/windows/transport_winhttp.cpp:243-264. Connection a Content-Length jsou hlavičky sestavené síťovou vrstvou.
- Diagnostika rekonstruuje aplikační a systémové request hlavičky v client/src/core/wizard/http_diagnostic.cpp:23-48.
- Tray editor pracuje s celým JSON dokumentem a při uložení jej odesílá službě v client/apps/tray/tray_application.cpp:321-326. Formátovač zachovává pořadí členů a escapování řetězců v client/apps/tray/editor_window.cpp:20-58.
- NetworkWorker::performHealth v client/src/core/runtime/network_worker.cpp:82-127 ověří health odpověď, vezme její timestampMs, vytvoří TimeSnapshot a publikuje jej do TimeSync.
- TimeSync::requestTimestampMs v client/src/core/runtime/time_sync.cpp:56-76 projektuje ověřený serverový čas podle monotónně uplynulé doby a odečítá bezpečnostní rezervu.
- RFC 9110, oddíl 5.3, stanoví, že pořadí řádků s rozdílnými názvy hlaviček není významové. Význam může mít pořadí více řádků se stejným názvem.

## Požadované změny

### Schéma a výchozí dokument

1. Přidat do objektu masterSetup povinný člen přesně pojmenovaný httpHeaders. Název se v této verzi nemění.
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

5. Ve fyzickém JSON textu musí být oddělovače řádků escapované. Výchozí serializace má použít \r\n:

~~~json
"httpHeaders": "Cache-Control: no-cache\r\nPragma: no-cache\r\nUser-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:155.0) Gecko/20100101 Firefox/155.0"
~~~

6. Povinné je jméno a přítomnost členu, nikoli jeho pozice. Platný dokument s libovolně přeuspořádanými členy musí být přijat a musí mít stejný význam.
7. Výchozí dokument se použije při prvním vytvoření konfigurace službou i při resetu nebo explicitním vytvoření výchozí konfigurace.

### Datový tok konfigurace

1. Rozšířit validaci známých členů masterSetup a načíst httpHeaders jako povinný string.
2. Uchovat dekódovanou hodnotu v konfiguračním modelu a předat ji do všech transportních instancí.
3. Zahrnout ji do běžného runtime transportu služby i do transportu používaného wizardem a jeho ověřovacími požadavky.
4. Operace wizardu, které upravují jiné konfigurační položky, musí httpHeaders zachovat.
5. Není požadováno nové samostatné formulářové pole v tray aplikaci. Tray a editor se mají změnit pouze tam, kde je to nutné pro správné vytvoření, zobrazení, úpravu, zachování a uložení celého JSON dokumentu.

### Sestavení a odesílání request hlaviček

1. Zachovat dosavadní hlavičky Content-Type, Connection a Content-Length a jejich stávající odpovědnost:
   - Content-Type zůstává nastavený aplikací podle HttpAction.
   - Connection a Content-Length zůstávají sestavené WinHTTP podle requestu.
2. Odstranit implicitní User-Agent PHPVPN/1.0 vznikající ze současného nastavení WinHTTP relace.
3. Logicky připojit dekódovaný blok httpHeaders k dosavadním hlavičkám každého requestu pro HttpAction::health, HttpAction::info a HttpAction::data.
4. To zahrnuje počáteční health, pravidelné a obnovovací health požadavky, info, datový polling i diagnostické a wizard probe varianty.
5. User-Agent se po změně bere pouze z httpHeaders. Výchozí konfigurace jej obsahuje, uživatel jej však může změnit nebo odebrat.
6. Kolize názvů mezi transportem vytvořenými hlavičkami a uživatelským blokem se na úrovni aplikace neřeší. Uživatelský blok se připojí i tehdy, když opakuje existující název. Za takovou konfiguraci odpovídá uživatel.
7. Není požadováno konkrétní fyzické pořadí hlaviček na drátu. Logické připojení uživatelského bloku nesmí být testováno jako pořadí za Content-Length, protože WinHTTP může hlavičky serializovat nebo přeuspořádat.
8. U více uživatelských řádků se stejným názvem se zachová jejich vzájemné pořadí tak, jak je uživatel zapsal v httpHeaders.
9. Diagnostický výpis requestu musí obsahovat skutečně použitý nebo korektně rekonstruovaný blok aplikačních hlaviček včetně httpHeaders a bez odstraněného implicitního PHPVPN/1.0 User-Agentu.

## Omezení a realizační vodítka

- Změna se týká klienta. Serverový PHP endpoint se kvůli ní nemění.
- Hlavičky jsou konfigurační data. Nesmějí měnit HTTP metodu, cílovou URL, tělo ani oddělení hlaviček od těla.
- Validace musí odmítnout nesprávný JSON typ a syntakticky nebezpečný nebo neplatný hlavičkový blok tak, aby nebylo možné vložit další request line nebo tělo.
- Požadavek neřešit kolize názvů neruší syntaktickou a bezpečnostní validaci jednotlivých řádků.
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

1. Nově vytvořená nebo resetovaná konfigurace obsahuje povinné masterSetup.httpHeaders s přesnými třemi výchozími uživatelskými hlavičkami.
2. Výchozí pořadí v masterSetup je serverSecret, serverUrl, httpHeaders, trustAnyTlsCertificate a poté dosavadní zbývající členy.
3. Stejná konfigurace s jiným pořadím členů se validuje a funguje identicky.
4. Chybějící httpHeaders způsobí jednoznačnou validační chybu povinné položky.
5. Jiný JSON typ než string nebo syntakticky neplatný hlavičkový blok je odmítnut jednoznačnou validační chybou.
6. Na server dorazí uživatelské hlavičky z httpHeaders při health, info i data requestu.
7. Výchozí request obsahuje firefoxový User-Agent z httpHeaders a neobsahuje implicitní User-Agent PHPVPN/1.0.
8. Po změně nebo odebrání User-Agentu v httpHeaders se na drátu použije uživatelova hodnota nebo žádná taková uživatelská hlavička. PHPVPN/1.0 se samovolně nevrátí.
9. Content-Type, Connection a Content-Length zůstanou funkční jako dosud.
10. Duplicitní název v uživatelském bloku není aplikací deduplikován ani přepsán. Test nesmí předepisovat globální pořadí různě pojmenovaných hlaviček na drátu.
11. Více uživatelských řádků se stejným názvem zachová vzájemné pořadí.
12. Health request s vlastními hlavičkami nadále ověří serverový timestamp a publikuje časový snapshot.
13. Wizard a diagnostické požadavky používají stejné vlastní hlavičky jako runtime a diagnostika je umí zobrazit.
14. Úprava a uložení konfigurace přes existující editor zachová httpHeaders i uživatelské pořadí členů.
15. Automatické testy pokryjí výchozí dokument, povinnost a typ položky, nezávislost na pořadí, escapování, přenos ve všech třech akcích, odstranění implicitního PHPVPN User-Agentu, kolize bez deduplikace, wizard probe a diagnostickou rekonstrukci.

## Referenční standard

- RFC 9110, oddíl 5.3 Field Order: https://www.rfc-editor.org/rfc/rfc9110.html#section-5.3
- Microsoft WinHttpSendRequest: https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpsendrequest
