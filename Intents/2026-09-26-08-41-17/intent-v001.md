---
intent_version: 1
created_at: 2026-09-26-08-57-13
---

# Linuxový klient phpvpn

## Shrnutí

Implementovat nativního linuxového klienta phpvpn se stejnými hlavními schopnostmi, které má současný klient pro Windows: systémovou službou, nástrojem příkazové řádky a aplikací pro systémovou lištu. Klient musí fungovat na systému bez grafického prostředí i na desktopových instalacích Fedory a Ubuntu. Chování vůči uživateli a serveru má být co nejbližší Windows verzi. Tam, kde se platformy liší, se má použít obvyklé linuxové řešení.

Nejvyšší prioritu má plně funkční služba. Musí správně implementovat celý potřebný protokol a umožnit skutečné připojení linuxového klienta k serveru phpvpn.

## Projektový kontext

- V repozitáři je existující klient ve složce `client/`. Má sdílené jádro, rozhraní platformních služeb a implementace pro Windows.
- Windows klient zahrnuje službu, CLI a tray aplikaci. CLI podporuje stav, připojení, odpojení, informace o serveru, čtení a nastavení konfigurace a diagnostiku.
- Platformně závislé části zahrnují mimo jiné síťový tunel, trasy, konfiguraci sítě, transport, kryptografii, bezpečné ukládání konfigurace, běh služby, IPC a integraci s desktopem.
- Tyto body jsou zjištění ze stávajícího kódu, nikoli dodatečné požadavky uživatele.

## Požadované chování

1. Linuxová služba běží bez grafického prostředí, spravuje konfiguraci a životní cyklus VPN spojení a komunikuje se stávajícím serverem phpvpn pomocí plného protokolu potřebného pro připojení a provoz.
2. Linuxová implementace síťového tunelu a konfigurace sítě používá nativní linuxové mechanismy. Připojení, odpojení, zotavení po chybě a úklid vlastních síťových změn zachovávají bezpečné chování odpovídající Windows klientovi.
3. Linuxové CLI zpřístupňuje schopnosti současného Windows CLI. Funguje i na headless systému a ovládá běžící službu.
4. Linuxová tray aplikace zpřístupňuje odpovídající ovládání a stav ve grafické relaci. Má být použitelná na desktopových instalacích Fedory a Ubuntu a respektovat způsob integrace obvyklý pro dané linuxové prostředí.
5. Služba, CLI a tray aplikace sdílejí stejnou konfiguraci a konzistentní stav. Práva k citlivým datům, oprávnění služby a přístup uživatelských procesů ke službě se řeší podle linuxových zvyklostí.
6. Instalace, spouštění a případné automatické spouštění služby či tray aplikace odpovídají linuxovým standardům cílových distribucí. Řešení musí být sestavitelné a provozovatelné na Fedoře a Ubuntu.
7. Rozdíly oproti Windows chování, které plynou z linuxových standardů nebo omezení desktopového prostředí, se zdokumentují. Funkční rozdíl nesmí znemožnit headless provoz ani běžné ovládání na podporovaném desktopu.

## Priority a rozsah

1. Funkční služba, správnost protokolu a reálné spojení se serverem.
2. Headless ovládání přes CLI.
3. Grafické ovládání přes tray aplikaci.

Dokončené řešení zahrnuje všechny tři části. Pořadí určuje postup práce, nikoli možnost poslední část vynechat.

## Ověření

- Ověřit sestavení a provoz na Linuxu, zejména na Fedoře a Ubuntu.
- Na skutečném serveru phpvpn ověřit navázání spojení, přenos provozu, odpojení a stav/diagnostiku služby přes CLI.
- Na headless instalaci ověřit provoz služby a CLI bez grafických závislostí.
- V grafické relaci ověřit spuštění a ovládání tray aplikace, její komunikaci se službou a zobrazení skutečného stavu spojení.
- Ověřit, že linuxové úpravy nenarušily existující Windows klient.

### Dostupný testovací stroj Fedora

Uživatel poskytl dočasný virtuální stroj s běžícím SSH serverem. Po implementaci je možné na něm ověřit službu, CLI i tray aplikaci. Grafická plocha stroje je dostupná na druhém monitoru uživatele a lze ji při testování proklikat.

- IP adresa: `192.168.162.108`
- SSH uživatel: `fedora`
- Heslo: `phpvpnlinux`
- Podle uživatele platí stejné heslo také pro účet `root`.

Uživatel výslovně povolil uvést tyto údaje v intentu a předat je následnému realizačnímu kontextu, protože jde o dočasný virtuální stroj, který bude odstraněn. Přístup ke stroji je určen k pozdějšímu ověření realizace, nikoli k současnému zpracování intentu.

## Akceptační kritéria

- Linuxová služba na Fedoře nebo Ubuntu úspěšně naváže a udrží spojení se serverem phpvpn při použití podporované konfigurace a protokolu.
- CLI na systému bez grafického prostředí umožní službu ovládat a získat stav i diagnostiku v rozsahu odpovídajícím Windows klientovi.
- Tray aplikace v grafické relaci umožní běžné ovládání spojení a ukazuje stav shodný se službou.
- Instalace, oprávnění, správa služby a síťová integrace odpovídají běžným linuxovým postupům.
- Reálné chování je ověřeno na poskytnuté Fedoře a vhodně také na Ubuntu. Omezení testovacího prostředí a případné neověřené scénáře jsou uvedeny ve výsledku realizace.

## Vodítko pro realizaci

Znovu použít sdílené části existujícího klienta, kde jsou přenositelné. Nahradit závislosti na Windows linuxovými implementacemi za existujícími rozhraními nebo rozhraní cíleně upravit, pokud Windows předpoklady brání správnému linuxovému chování. Volbu konkrétních knihoven, balení a desktopové integrace určit podle požadované funkce a ověřit ji na cílových distribucích. Zachovat interoperabilitu se stávajícím serverem a regresně ověřit Windows klienta.

## Původ zadání

Tento dokument vychází z dostupné části původního rozhovoru. Starší konverzace, ve které vznikl aktivní záznam intentu, nebyla při zpracování dostupná. Uživatel výslovně zvolil pokračování s dostupným zadáním a požádal ukládat jen výsledný intent, nikoli snímek chatu.
