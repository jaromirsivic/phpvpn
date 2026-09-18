---
intent_version: 1
created_at: 2026-09-13-22-56-14
source: intent-chat-v001.md
---

# Oprava neustupujícího minimálního polling intervalu po provozu

## Shrnutí

Zjistit, spolehlivě prokázat a odstranit příčinu chování, při kterém Windows klient po ukončení skutečného síťového provozu nadále volá datový endpoint serveru v minimálním serverem určeném intervalu. Oprava musí obnovit očekávaný idle decay, zabránit regresi automatickými testy, podle potřeby sladit dokumentaci a skončit vytvořením aktuální distribuce.

## Pozorovaný scénář

1. Na lokálním počítači běží aktuální serverový skript `D:\Nginx\html\phpvpn.php`, dostupný jako `http://127.0.0.1/phpvpn.php`.
2. Lokální klient se k serveru připojí a spojení se jeví jako funkční.
3. Nejnovější dostupná klientská distribuce se nainstaluje také na jiný počítač, konkrétně na virtuální stroj, a připojí se ke stejnému serveru.
4. Mezi stroji proběhne úspěšný ping.
5. Po skončení pingu už nejsou odesílány ani přijímány další VPN pakety.
6. Přesto klient pokračuje v dotazování serveru přibližně každých 50 ms, tedy v aktuálním minimálním intervalu. Polling interval se při nečinnosti nezdvojnásobuje.

## Kontext projektu a pracovní hypotéza

- Serverem předávané limity jsou v klientovi reprezentovány hodnotami `minDataPollIntervalMs` a `maxDataPollIntervalMs`. Aktuální výchozí hodnoty v kódu jsou 50 ms a 3200 ms.
- `NetworkWorker::nextPollDelayMs(bool hadTraffic)` v `client/src/core/runtime/network_worker.cpp` resetuje interval na minimum při `hadTraffic == true` a při nečinnosti ho násobí dvěma až do maxima.
- `VpnEngine::runNetworkThread()` v `client/src/core/runtime/vpn_engine.cpp` nyní volá tuto funkci s podmínkou `packetsReceived_ > 0 || packetsSent_ > 0`.
- `packetsReceived_` a `packetsSent_` jsou v `runConnectedIteration()` kumulativní čítače za připojení. Po prvním přeneseném paketu proto podmínka zůstává pravdivá i v dalších prázdných iteracích.
- Toto je silná pracovní hypotéza příčiny, nikoli náhrada požadovaného ověření. Realizace ji musí reprodukcí nebo cíleným testem potvrdit, případně zdokumentovat jinou skutečnou příčinu.
- Přímé testovací pokrytí volání `nextPollDelayMs` z běhu připojeného klienta nebylo při přípravě intentu nalezeno.

## Požadované změny

### Diagnostika a důkaz příčiny

- Reprodukovat popsaný problém na aktuálním zdrojovém stavu nebo vytvořit deterministický test, který přesně zachytí stejný stavový přechod: provoz v jedné či více iteracích, následované prázdnými iteracemi.
- Prokázat, zda je příčinou použití kumulativních čítačů místo aktivity aktuální datové výměny.
- Pokud se hypotéza nepotvrdí, pokračovat v diagnostice až k doložené příčině a opravit skutečnou závadu v témže funkčním rozsahu.
- Odlišit skutečné VPN pakety v aktuální datové výměně od samotného úspěšného HTTP pollu, keepalive komunikace, health kontroly a historického provozu.

### Oprava klienta

- Rozhodnutí, zda je aktuální iterace aktivní, musí vycházet z paketů skutečně odeslaných nebo přijatých v této iteraci, nikoli z kumulativních diagnostických čítačů za celé připojení.
- Iterace s alespoň jedním skutečně odeslaným nebo přijatým VPN paketem má polling interval vrátit na serverem povolené minimum.
- Každá následující iterace bez skutečného provozu má aplikovat existující násobek idle backoffu a respektovat serverem určené minimum a maximum.
- Oprava nesmí porušit zpracování příchozích či odchozích paketů, recovery logiku, časovou synchronizaci, health kontrolu ani diagnostické kumulativní čítače.
- Nasazený `D:\Nginx\html\phpvpn.php` se nemá měnit, pokud diagnostika neprokáže podíl serverové implementace. Případná nutná serverová změna musí být nejprve provedena v autoritativním zdroji repozitáře.

### Testy

- Přidat regresní automatický test na přechod z aktivního provozu do nečinnosti.
- Test musí selhat proti původnímu chybnému chování a projít po opravě.
- Ověřit alespoň tyto případy:
  - skutečný provoz resetuje interval na minimum,
  - první prázdná iterace po provozu zahájí prodlužování intervalu,
  - další prázdné iterace interval postupně zdvojnásobují až k serverem stanovenému maximu,
  - historický paket už po vyprázdnění provozu neudržuje polling na minimu,
  - nový provoz po období nečinnosti znovu vrátí interval na minimum.
- Spustit relevantní jednotkové, integrační a runtime testy odpovídající dotčené vrstvě. Neprovedené testy nesmí být hlášeny jako úspěšné.
- Pokud je dostupné odpovídající prostředí, provést také praktické ověření se dvěma klienty nebo s virtuálním strojem proti aktuálnímu serveru.

### Dokumentace

- Zkontrolovat autoritativní dokumentaci klientského polling chování, zejména `client.md`, a související distribuční pokyny.
- Dokumentaci změnit pouze tehdy, pokud je současný popis neúplný, nesprávný nebo pokud oprava zavádí uživatelsky či provozně významnou změnu. Pokud změna není nutná, uvést tuto skutečnost v závěrečném reportu.

### Distribuce

- Po úspěšné implementaci a ověření vytvořit novou distribuci z aktuálního zdrojového stavu podle závazné metodiky v `distribution.md`.
- Znovu vytvořit projektový strom `dist`, včetně aktuálního `dist/server/phpvpn.php`, kompletního Windows x64 balíčku, požadovaných platformních adresářů, licencí a podepsaného manifestu.
- Distribuční klientské binárky musí obsahovat opravu a odpovídat aktuálním zdrojům.
- Ověřit úplnost, architekturu, integritu, podpisové artefakty a shodu distribuovaného serverového skriptu s aktuálním zdrojem.
- Předat přesnou cestu k výsledné distribuci a pravdivý souhrn provedených kontrol.

## Pravděpodobně dotčené oblasti

- `client/src/core/runtime/vpn_engine.cpp`
- `client/src/core/runtime/vpn_engine.h`, pokud bude potřeba změnit rozhraní nebo uchovávaný stav
- `client/src/core/runtime/network_worker.cpp`
- `client/src/core/runtime/network_worker.h`
- relevantní testy pod `client/tests/`, případně integrační scénáře pod `tests-integration/`
- `client.md`, pouze pokud kontrola prokáže potřebu aktualizace
- `distribution.md` a distribuční nástroje pouze tehdy, pokud jejich současný postup brání vytvoření požadované úplné distribuce

## Akceptační kritéria

1. Příčina je doložena reprodukcí, regresním testem nebo jiným kontrolovatelným důkazem.
2. Po ukončení skutečného VPN provozu už klient nezůstává trvale na minimálním polling intervalu kvůli historickým paketům.
3. Při nečinnosti se interval zvětšuje existujícím faktorem dva až k serverem stanovenému maximu a nikdy neporuší serverem stanovené meze.
4. Nový skutečný provoz interval správně vrátí na minimum.
5. Relevantní automatické testy pokrývají regresi a procházejí.
6. Praktické ověření proti aktuálnímu serveru je provedeno, pokud je potřebné prostředí dostupné, jinak je omezení výslovně uvedeno.
7. Dokumentace je buď aktualizována, nebo je doloženo, proč změnu nepotřebovala.
8. Je vytvořená a ověřená aktuální distribuce obsahující opraveného klienta a aktuální serverový skript.
9. Závěrečný report uvede změněné soubory, důkaz příčiny, provedené testy, případná omezení a přesnou cestu k distribuci.

## Realizační omezení

- Intent sám neautorizuje commit ani push.
- Změny mají být provedeny přímo v aktivní feature branch projektu.
- Neprovádět nesouvisející změny a zachovat existující protokolové a bezpečnostní vlastnosti.
