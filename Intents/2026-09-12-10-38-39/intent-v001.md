---
intent_version: 1
created_at: 2026-09-12-10-40-18
source: intent-chat-v001.md
---

# Pre-poll admission backpressure pro Windows službu

## Shrnutí

Implementovat konzervativní pre-poll admission backpressure pro běžný VPN `data` polling. Před zahájením dalšího provozního requestu služba ověří, zda příchozí fronty bezpečně přijmou alespoň jeden další minimální platný IPv4 paket. Pokud ne, request dočasně nepustí a počká běžným přerušitelným mechanismem do dalšího intervalu. Cílem je nevyžádat od serveru data, která klient v dané chvíli pravděpodobně nemá kam zařadit.

## Potvrzený současný stav

- Pravidelný VPN polling provádí Windows služba v `VpnEngine::runNetworkThread` přes `NetworkWorker::exchangeData`.
- `DirectionQueues::canAdmitAnotherPacket` existuje ve sdíleném core, ale runtime ji před `exchangeData` nevolá.
- Tray aplikace nemá vlastní VPN `data` polling, pouze přes IPC čte paměťový stav služby. Do tray aplikace ani `tray_UX.md` se proto nyní neplánuje funkční změna.
- Backpressure nemění HTTP wire formát, serverový kontrakt ani hodnoty vracené serverem. `protocol.md` se má zkontrolovat, ale očekávaným výsledkem je nulová změna.
- `client.md` dnes správně označuje pre-poll admission backpressure jako nedodanou budoucí práci. Po úspěšné realizaci musí popisovat skutečné dodané chování.

## Požadované chování

1. Před běžným provozním `data` pollingem služba pod zámkem posoudí kapacitu inbound `DirectionQueues` pro minimální platný IPv4 paket o 20 bajtech.
2. Kapacitní sonda musí konzervativně posoudit globální limit, rezervace pro existující fronty a případného nového peera. Nesmí vytvořit frontu, alokovat buffer, odebrat paket ani změnit účetnictví.
3. Je-li kapacita nedostatečná, služba nevolá `exchangeData`. Nesmí vytvořit request, odebrat outbound paket, změnit pollingové, requestové, response, IPv4 nebo drop čítače, změnit ověřený stav pollingu ani spustit recovery.
4. Při blokaci zůstává VPN ve stavu connected. Použije stávající přerušitelné čekání příkazové smyčky bez busy loopu, nového vlákna, timeru nebo IPC. Disconnect a stop musí čekání přerušit bez zbytečné prodlevy.
5. Po uvolnění kapacity pokračuje běžný polling bez nového bootstrapu. Runtime admission jednotlivých přijatých paketů zůstává zachovaná, protože sonda nezná skutečnou velikost celé odpovědi.
6. Bootstrap, recovery access check a Wizard diagnostika se nesmějí za úspěšný polling považovat pouze proto, že běžný provozní poll byl přeskočen.
7. Běžný pollingový interval musí být při krajních hodnotách serverových doporučení omezen na 20 až 5000 ms. Limit 5000 ms není povinnost request odeslat, pokud admission gate dál blokuje polling.

## Dotčené oblasti

### Produkční kód

- `client/src/core/queues/packet_queues.h` a `.cpp`, oprava kapacitní sondy pro nejhorší existující i nový peer.
- `client/src/core/runtime/vpn_engine.cpp`, gate před běžným provozním `exchangeData` a zachování přerušitelného čekání.
- `client/src/core/runtime/network_worker.cpp`, oboustranné omezení serverových intervalů na 20 až 5000 ms.
- Další interní runtime typ jen tehdy, pokud je nezbytný pro rozlišení skutečně provedeného a přeskočeného cyklu.

### Dokumentace

- `client.md` se aktualizuje až spolu s ověřenou implementací: pollingový interval, příjem dat a povinné testovací scénáře.
- `protocol.md` se zkontroluje. Změna se neplánuje, protože nevzniká nový wire požadavek ani serverové pravidlo.
- `tray_UX.md` se zkontroluje. Změna se neplánuje, protože tray polling neřídí.

### Testy

- Rozšířit existující queue a runtime testy, bez nového frameworku nebo skutečné VPN závislosti.
- Ověřit volnou, přesně dostatečnou a nedostatečnou kapacitu pro 20 bajtů, globální limit, rezervace, nový peer i nejvíce obsazeného existujícího peera. Sonda nesmí změnit FIFO, počty front, bajty ani dropy.
- S fake transportem a zadrženým tunnel writerem ověřit, že během plné inbound fronty nevzniká `data` request ani při outbound backlogu, čítače a checkpoint zůstávají beze změny a po uvolnění se polling obnoví bez bootstrapu a se zachováním FIFO.
- S fake clock ověřit absenci busy loopu, rychlý Disconnect/stop a intervalové hrany pod 20 ms, uvnitř rozsahu a nad 5000 ms.
- Zachovat existující deferred/error návrat outbound paketů na čelo po skutečně provedeném requestu. Skipped větev nesmí maskovat access check nebo Wizard diagnostiku jako úspěch.

## Omezení

- Nezavádět nové vlákno, timer, IPC příkaz, tray funkci ani změnu protokolu.
- Neřešit odložené R04 kolem Wintun vlastnictví a integrity.
- Neprovádět změny historických plánů, notes ani již hotové dokumentační konsolidace mimo nutný zápis výsledného chování.
- Nepovažovat konzervativní admission sondu za záruku bezeztrátovosti celé serverové odpovědi.

## Akceptační kritéria

1. Při nedostatečné inbound kapacitě nevznikne nový běžný `data` request a nevzniknou jeho vedlejší účinky.
2. Po uvolnění kapacity se běžný polling obnoví bez bootstrapu a bez porušení FIFO či účetnictví.
3. Všechny běžné intervaly jsou omezené do 20 až 5000 ms, zatímco trvající kapacitní blokace smí přeskočit více intervalů.
4. Cílené unit a runtime testy pro uvedené scénáře projdou a jejich přesné výsledky budou uvedeny v realizační evidenci.
5. `client.md` popisuje dodané chování. `protocol.md` a `tray_UX.md` zůstávají beze změny, pokud kontrola během realizace nenajde konkrétní faktický dopad.
