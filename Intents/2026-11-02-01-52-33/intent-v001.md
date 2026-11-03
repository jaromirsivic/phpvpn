---
intent_version: 1
created_at: 2026-11-02-01-52-33
timezone: Europe/Berlin
source: intent-chat-v001.md
status: specification_only
---

# Diagnostika selhání startu tunelu PHPVPN a doplnění logování

## Cíl a rozsah

Upravit diagnostiku klientské služby tak, aby při selhání připojení bylo z lokálního logu možné určit konkrétní neúspěšnou operaci, původní příčinu a výsledek následného úklidu. Vyšetřit také, proč v popsaném incidentu zůstal soubor služby prázdný. Samotné přidání jedné obecné chybové zprávy není dostatečné.

Toto je zadání budoucí implementace. V rámci jeho sepsání se nemění klient, server, konfigurace ani instalace. Přesná příčina incidentu zatím nebyla zjištěna. Případná oprava funkční chyby musí vycházet z důkazu, nikoli z předpokladu o ovladači, virtualizaci, serveru nebo pingu.

Hlavní rozsah je klientská služba pro Windows a společné části runtime a loggeru. Zachovat chování ostatních platforem. Nezavádět plošné logování paketů ani nový vzdálený sběr dat.

## Popis incidentu

### Prostředí a průběh podle uživatele

- PHPVPN běží ve virtuálních Windows 11 v Hyper-V. Název připojení je `windows 11 on NB4553`.
- Instalace zobrazená v Průzkumníku je v `C:\Program Files\phpvpn\1.0.4`. Stejnou cestu ke službě obsahují záznamy SetupAPI.
- Uživatel za běhu změnil konfiguraci komunikačního kontraktu s protistranou. Následně provedl Disconnect a Connect.
- Z protistrany současně probíhal opakovaný ping na jeho virtuální IP `10.99.99.5`. Přesná frekvence pingu a změněná pole kontraktu nebyly doloženy.
- Připojení skončilo chybou popsanou níže. Stejný výsledek nastal i při dalších pokusech.
- Uživatel následně oznámil restart služby označené jako PHPVPN client. Ani potom se připojení neopravilo. Nový proces a změna jeho PID nebyly samostatně ověřeny. Restart nesmí být v návrhu považován za prokázané řešení.

### Ověřeno na obrazovce

1. Okno připojení ukázalo `Success` u všech čtyř kontrol: `Server URL`, `Server secret`, `Access key`, `Communication contracts`.
2. Následoval panel:

   ```text
   Cannot start the VPN tunnel
   Ask your administrator to check the PHPVPN service and tunnel setup.
   ```

   Zelené kontrolní body nepotvrzují úspěšné vytvoření tunelu ani funkční přenos.
3. Ve složce `C:\ProgramData\PHPVPN\logs` byl jediný zobrazený soubor `phpvpn-service.log` o velikosti 0 bajtů. Po opakovaném pokusu a obnovení Průzkumníka zůstal prázdný.
4. V instalační složce byly přítomné `phpvpn-service.exe` a `wintun.dll`. Přítomnost knihovny nepotvrzuje úspěšné načtení DLL, vytvoření adaptéru ani start session.
5. V rozbalené skupině Network adapters ve Správci zařízení byl adaptér Hyper-V a WAN Miniporty. V aktuálně zobrazeném seznamu nebyl adaptér PHPVPN. Skrytá zařízení nebyla zkoumána. Absence adaptéru po chybě neprokazuje odinstalaci ovladače.
6. Konec `C:\Windows\INF\setupapi.dev.log` obsahoval opakované operace `Delete Device` pro stejnou identitu `SWD\WINTUN`, vyvolané procesem služby PHPVPN. Některé obsahovaly:

   ```text
   Query-and-Remove failed: 0x05: CR_INVALID_DEVNODE
   Exit status: SUCCESS
   ```

   Ve starším úseku bylo také `Query-and-Remove succeeded`. Zachycené bloky dokládají operace odstraňování zařízení. Neprokazují původní chybu vytvoření tunelu ani odstranění balíčku ovladače.
7. Nalezený výskyt `Enable Device Install` patřil ke starší systémové operaci Windows. Nebyl potvrzen jako součást incidentu PHPVPN.

### Omezení důkazů a hypotézy

- Úplný obsah konfigurační změny, úplný log SetupAPI a diagnostika serveru nebyly získány.
- Čas ve virtuálním stroji ukazoval 1. listopadu 2026. Zadání vzniká 2. listopadu 2026 v pásmu Europe/Berlin. Při korelaci záznamů ověřit hodiny a pásmo jednotlivých zařízení.
- Není potvrzeno, že nainstalovaná verze přesně odpovídá aktuálním zdrojům v repozitáři.
- Možné selhání při ukončení a novém vytvoření tunelu po změně konfigurace je pracovní hypotéza.
- Běžící ping je součást reprodukčního scénáře, nikoli potvrzená příčina.
- Uživatel navrhl i možnost zbylé relace na serveru. Serverová příčina nebyla ověřena. V aktuálním kódu je po úspěšných kontrolách vlastní start tunelu lokální operace. Případný vliv dříve přijatých dat nebo souběžného provozu je třeba doložit.

## Výchozí stav aktuálního kódu

Před realizací ověřit aktuální stav zdrojů a souvislost s nasazeným buildem. Uvedené rozsahy odpovídají zdrojům při sepsání zadání.

| Oblast | Výchozí zjištění |
| --- | --- |
| `client/src/core/runtime/vpn_engine.cpp:565–591` | Po bootstrap kontrolách přechází engine do `starting_tunnel`, volá `TunnelSession::start` a spouští pracovní role. |
| `client/src/core/runtime/vpn_engine.cpp:738–764` | Neúspěšný počáteční pokus se převádí na `SafeError` a provádí teardown. Původní chyba se v této větvi přímo neloguje. |
| `client/src/core/runtime/status_snapshot.cpp:13–50` | Různé chyby ve fázi startu tunelu se slučují do veřejného `tunnel_start_failed`. Veřejná klasifikace má zůstat bezpečná. |
| `client/src/core/runtime/tunnel_session.cpp:237–341` | Start zahrnuje načtení driveru, síťový snapshot a preflight, adaptér, provozní journal, IP, trasu, ověření sítě a session. |
| `client/src/core/runtime/tunnel_session.cpp:343–399` | Úklid odebírá trasu a adresu, ukončuje session, zavírá adaptér a pracuje s journalem. Některé dílčí cleanup výsledky při startu se zahazují. |
| `client/src/platform/windows/tunnel_wintun.cpp:171–300` | Důležitá selhání načtení DLL, adaptéru a session vracejí obecné interní chyby bez zachování původního systémového kódu v těchto větvích. |
| `client/src/platform/windows/tunnel_wintun.cpp:332–347` | Symbol `WintunSetLogger` se načítá, ale v prozkoumaném souboru není callback následně registrován. |
| `client/src/core/base/logger.cpp:81–102,145–193` | Logger otevírá stream a po záznamu provádí flush. Přímo zde není explicitně zveřejněn výsledek otevření a zápisu. |
| `client/apps/service/service_host_win.cpp:53–95` | Windows host konfiguruje souborový logger až po přípravě úložiště. |
| `client.md:318–326` | Platná pravidla pro lokální log, rotaci, flush a zakázaný obsah. |

Chybějící logování původní chyby je ověřená mezera, samo však nevysvětluje celý nulový log. Existují i jiné informační záznamy. Nutno prověřit aktivní cestu zápisu, stav streamu, inicializaci, úroveň logování a skutečně spuštěný build.

## Požadavky na výsledné chování

### 1. Funkční log od startu služby

- Při běžném úspěšném spuštění souborového logování vytvořit stručný bezpečný záznam o startu služby a inicializaci diagnostiky. Uvést verzi/build, platformu, architekturu a identifikaci běhu procesu vhodnou pro odlišení restartů.
- Ověřit a zpracovat selhání otevření, zápisu, flush a rotace. Nesmí vznikat falešný dojem, že logování funguje, když stream odmítá zápis.
- Pro chybu samotného loggeru využít vhodnou existující lokální diagnostickou cestu, například diagnostický snapshot či dostupný výstup hostitele. Nevytvářet rekurzivní logování do nefunkčního streamu ani nový síťový reporting.
- Rozlišit stav neprovedené inicializace, nepoužitelného úložiště a selhání již otevřeného logu. Stanovit a doložit chování služby v těchto stavech bez svévolné změny dostupnosti VPN.
- Produkční datový adresář nadále odvozovat systémovým API, nikoli natvrdo z cesty použité při ruční kontrole incidentu.

### 2. Původní příčina před převodem na veřejnou chybu

- Každý neúspěšný pokus musí mít čitelný výsledný záznam s fází, konkrétní operací, interním kódem chyby a bezpečným vysvětlením. Zachovat vazbu na výsledný veřejný kód.
- Původní chybu zachytit před teardownem a před operacemi, které by mohly přepsat systémový error stav. Následný cleanup ji nesmí nahradit.
- Zaznamenat významné začátky a výsledky Connect, Disconnect, aplikace konfigurace, vytvoření a ukončení tunelu. U konfigurace stačí informace o revizi a o tom, která revize je uložená a která byla použita pro spojení, bez obsahu konfigurace.
- Využít existující identity běhu, epochy a pokusu o připojení, kde jsou dostupné. Záznamy různých pokusů a restartů musí jít rozlišit. Nevytvářet rozsáhlý nový korelační systém, pokud stačí současné mechanismy.

### 3. Rozlišení kroků lokálního tunelu

Diagnostika musí umožnit určit poslední úspěšný a první neúspěšný krok alespoň v těchto oblastech:

1. Načtení knihovny a vyřešení API ovladače.
2. Získání síťového snapshotu a vyhodnocení preflight konfliktu.
3. Otevření nebo vytvoření adaptéru a získání identity rozhraní.
4. Uložení a aktualizace ownership journalu.
5. Přidání IP adresy a trasy.
6. Kontrola výchozí trasy a DNS.
7. Spuštění Wintun session a obou pracovních rolí.
8. Ukončení pracovníků a session, odebrání adresy a trasy, uzavření adaptéru a dokončení práce s journalem.

Základní příčina musí být dostupná při výchozí produkční úrovni logování. Detailní úspěšné mezikroky mohou být DEBUG. Nelogovat každý ping nebo paket.

### 4. Systémové chyby Windows a Wintun

- U neúspěšných Windows API a Wintun volání zachytit bezprostředně příslušný návratový nebo systémový kód. Nepoužívat později náhodně přetrvávající `GetLastError`.
- Zachovat původ kódu, například Win32, HRESULT nebo jinou skutečně vrácenou doménu, aby nedošlo k chybné interpretaci čísla.
- Bezpečně zapojit Wintun logger, pokud to je pro používanou verzi API dostupné. Vyřešit životnost callbacku, souběh a ukončení knihovny.
- Raw text z callbacku nebo systémové chybové zprávy nepřeposílat bez kontroly. Preferovat pevné názvy operací, kódy a povolené parametry. Zejména neuniknou interní cesty ani tajemství.

### 5. Diagnostika úklidu a konfigurace za provozu

- Log musí rozlišit primární selhání startu od sekundárního selhání úklidu. Pokud nastanou obě, zachovat obě události ve správném pořadí a se společnou vazbou na pokus.
- Rozlišit očekávaně nepřítomné zařízení od skutečného neúspěchu odstranění. Záznam `Delete Device` ani varování z SetupAPI samy neoznačovat jako kořenovou příčinu.
- Při změně kontraktu a následném Disconnect/Connect musí být dohledatelné, kdy byla konfigurace přijata, kdy skončilo staré spojení a kdy začal nový tunel s novou revizí.
- Zachovat současný model aplikace konfigurace a zpracování provozu. Nepřidávat svévolné restarty služby, serveru, automatické reinstalace ovladače ani nové retry chování jako náhradu diagnostiky.

### 6. Obsah a provozní omezení

- Zachovat pravidla `client.md`, zejména rotaci 8 MiB, pět historických souborů a flush po řádku na Windows.
- Nelogovat klíče, hesla, celé konfigurace, HMAC/GCM tagy, ciphertext, obsah paketů, těla requestů či odpovědí, interní cesty ani další obsah zakázaný současnou specifikací. Neobcházet to logováním celého `Error::message()`.
- Diagnostika má být stručná a použitelná při běžné zátěži i průběžném pingu. Nezavádět per-packet diskové I/O, neomezené fronty, výrazné blokování nebo nové čekání měnící časování lifecycle.
- Zachovat veřejné bezpečné chyby v tray a IPC. Technické detaily patří do povolené lokální diagnostiky, nikoli do uživatelského progress panelu.
- Změny ve společném runtime a loggeru nesmějí porušit současné platformní sinky a politiku logů na Linuxu, macOS a Androidu.

## Ověření a akceptační kritéria

1. **Čerstvý proces:** po startu je v dostupném logu identifikovatelný nový běh služby. Pokud zápis nefunguje, existuje zjistitelný bezpečný diagnostický stav se skutečným důvodem.
2. **Chyba startu:** řízeně vyvolaná chyba načtení DLL, vytvoření adaptéru nebo startu session je při výchozí úrovni dohledatelná jako konkrétní operace s původním interním a systémovým kódem. UI si zachová bezpečnou kategorii chyby.
3. **Síť a journal:** řízené selhání preflightu, nastavení sítě nebo journalu lze odlišit od chyby ovladače.
4. **Úklid:** při současném primárním selhání a selhání cleanupu zůstane primární příčina zachovaná a doplňkový problém je zvlášť dohledatelný.
5. **Původní scénář:** ve Windows ověřit fungující spojení, průběžný ping z druhého klienta, změnu komunikačního kontraktu, Disconnect a Connect. Porovnat se stejným postupem bez pingu a s opětovným připojením bez změny konfigurace. Neměnit více proměnných naráz.
6. **Restart:** zopakovat pokus po prokazatelném restartu místní služby, doloženém novou identitou procesu. Server při tomto srovnání ponechat beze změny. Samotný úspěch či neúspěch restartu nevydávat za důkaz kořenové příčiny.
7. **Ztráta logu:** smysluplnými testy ověřit neúspěšné otevření nebo zápis a rotaci. Selhání diagnostiky nesmí vytvářet nekonečnou rekurzi nebo deadlock.
8. **Zakázaný obsah:** test s rozpoznatelnými testovacími hodnotami prokáže, že se tajemství a zakázaná data nedostanou do nových záznamů ani prostřednictvím Wintun callbacku.
9. **Regrese:** stávající relevantní testy klasifikace chyb, tunelového lifecycle, loggeru a rotace projdou. Ověřit build dotčených částí a regresi společných rozhraní. Nové testy chrání důležité chování, nikoli přesné znění každého informačního řádku.
10. **Výsledek diagnostiky:** při selhání reprodukce lze z logu určit první neúspěšný krok a jeho konkrétní příčinu, nebo přesně doložit, která diagnostická informace ještě chybí. Pokud se původní problém neprojeví, uvést to výslovně, netvrdit jeho opravu.

## Očekávaný výstup realizace

- Cílená úprava logování, zpracování chyb a odpovídající dokumentace.
- Smysluplné automatizované testy a protokol skutečně provedených manuálních scénářů. Neprovedené kontroly výslovně označit.
- Bezpečný příklad logu neúspěšného připojení, včetně rozlišení primární chyby a cleanupu.
- Závěr vyšetření nulového logu s důkazem. Pokud příčina závisí na rozdílu nasazeného a aktuálního buildu, popsat tento rozdíl.
- Samostatné vyhodnocení původní chyby tunelu. Nezaměnit dokončení diagnostiky za vyřešení dosud nepotvrzené funkční závady.

## Podklady

- Původní screenshot připojení dodaný uživatelem a následné vizuální kontroly v tomto chatu.
- Uživatelské hlášení o změně kontraktu za provozu, pingu z protistrany a neúčinném restartu služby.
- [Pravidla logování klienta](../../client.md#logování-a-diagnostika).
- [Microsoft: SetupAPI Device Installation Log Entries](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/setupapi-device-installation-log-entries). Dokumentace potvrzuje účel systémového logu, nikoli příčinu tohoto incidentu.
