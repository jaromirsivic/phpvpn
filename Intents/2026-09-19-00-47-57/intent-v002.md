---
intent_version: 2
created_at: 2026-09-19-08-20-26
based_on: intent-v001.md
source: intent-chat-v002.md
---

intent_version: 1
created_at: 2026-09-19-08-14-34
source: intent-chat-v001.md
---

# AutomatickĂ˝ start, automatickĂ© pĹ™ipojenĂ­ a ikony Windows aplikace

## ShrnutĂ­

RozĹˇĂ­Ĺ™it Windows klienta PHPVPN o tĹ™i souvisejĂ­cĂ­ oblasti:

1. Tray aplikaci bezpeÄŤnÄ› registrovat pro automatickĂ© spuĹˇtÄ›nĂ­ pĹ™i pĹ™ihlĂˇĹˇenĂ­ uĹľivatele do Windows a registraci odstranit pĹ™i odinstalaci.
2. PĹ™idat do `masterSetup` konfiguraÄŤnĂ­ volbu `autoConnectOnStartup`, kterĂˇ pĹ™i hodnotÄ› `true` po startu sluĹľby zahĂˇjĂ­ bÄ›ĹľnĂ© pĹ™ipojenĂ­.
3. PouĹľĂ­t existujĂ­cĂ­ ICO soubory z koĹ™enovĂ© sloĹľky `assets` jako aplikaÄŤnĂ­ a stavovĂ© ikony.

## Kontext projektu

NĂˇsledujĂ­cĂ­ body jsou zjiĹˇtÄ›nĂ© skuteÄŤnosti o aktuĂˇlnĂ­m projektu, nikoli novĂ© poĹľadavky uĹľivatele:

- ProdukÄŤnĂ­mi Windows procesy jsou sluĹľba a tray aplikace.
- Instalaci a odinstalaci sluĹľby obsluhujĂ­ pĹ™Ă­kazy `phpvpn-service.exe --install` a `phpvpn-service.exe --uninstall` v `client/apps/service/main.cpp`; vlastnĂ­ SCM operace jsou v `client/apps/service/service_host.cpp`.
- VĂ˝chozĂ­ konfiguraci vytvĂˇĹ™Ă­ `client/src/core/config/config_defaults.cpp`. PovolenĂ© ÄŤleny a jejich typy ovÄ›Ĺ™uje `client/src/core/config/config_validator.cpp`. Wizard pracuje s dokumentem v `client/src/core/config/config_document.cpp` a reset pouĹľĂ­vĂˇ novĂ˝ vĂ˝chozĂ­ dokument.
- Start sluĹľby, vytvoĹ™enĂ­ `ConfigStore` a start `VpnEngine` probĂ­hajĂ­ v `client/apps/service/service_host.cpp`.
- Tray jiĹľ rozliĹˇuje stavy `disconnected`, `connecting`, `connected`, `recovering` a `error`. AktuĂˇlnĂ­ mapovĂˇnĂ­ stavovĂ˝ch ikon je v `client/apps/tray/tray_application.cpp` a naÄŤĂ­tĂˇnĂ­ prostĹ™edkĹŻ v `client/apps/tray/ui_resources.cpp`.
- PoĹľadovanĂ© zdrojovĂ© soubory existujĂ­ jako `assets/default.ico`, `assets/connected.ico`, `assets/connecting.ico` a `assets/disconnected.ico`.

## 1. AutomatickĂ˝ start tray aplikace

### FunkÄŤnĂ­ poĹľadavky

- ĂšspÄ›ĹˇnĂˇ instalace aplikace mĂˇ vytvoĹ™it strojovou registraci, kterĂˇ spustĂ­ tray aplikaci pro kaĹľdĂ©ho interaktivnÄ› pĹ™ihlĂˇĹˇenĂ©ho uĹľivatele pĹ™i jeho pĹ™ihlĂˇĹˇenĂ­ do Windows.
- Tray aplikace se nesmĂ­ spouĹˇtÄ›t v servisnĂ­ relaci ani pod ĂşÄŤtem sluĹľby.
- Odinstalace musĂ­ odstranit pouze vlastnĂ­ registraci automatickĂ©ho startu PHPVPN.
- OpakovanĂˇ instalace i odinstalace musĂ­ bĂ˝t idempotentnĂ­.
- RegistrovanĂ˝ pĹ™Ă­kaz musĂ­ pouĹľĂ­vat absolutnĂ­, sprĂˇvnÄ› citovanou cestu k nainstalovanĂ©mu `phpvpn-tray.exe` a nesmĂ­ umoĹľnit zĂˇmÄ›nu spustitelnĂ©ho souboru nebo argumentĹŻ.

### UmĂ­stÄ›nĂ­ a ĹľivotnĂ­ cyklus

- Registraci vlastnit ve stejnĂ©m autoritativnĂ­m instalaÄŤnĂ­m a odinstalaÄŤnĂ­m toku jako sluĹľbu, aby ji pouĹľĂ­vala balĂ­ÄŤkovĂˇ instalace i pĹ™Ă­mĂ© pĹ™Ă­kazy `--install` a `--uninstall`.
- Pro strojovĂ˝ start pĹ™i pĹ™ihlĂˇĹˇenĂ­ pouĹľĂ­t standardnĂ­ 64bitovou hodnotu v `HKLM\Software\Microsoft\Windows\CurrentVersion\Run` s jednoznaÄŤnĂ˝m nĂˇzvem PHPVPN.
- PĹ™i instalaci nejprve zachovat souÄŤasnou kritickou instalaci sluĹľby. Registrace tray autostartu je doplĹkovĂ˝ krok.

### BezpeÄŤnĂ© selhĂˇnĂ­

- SelhĂˇnĂ­ zĂˇpisu registrace automatickĂ©ho startu nesmĂ­ zmÄ›nit ĂşspÄ›Ĺˇnou instalaci sluĹľby na neĂşspÄ›Ĺˇnou instalaci celĂ©ho produktu.
- SelhĂˇnĂ­ odstranÄ›nĂ­ registrace nesmĂ­ zablokovat odstranÄ›nĂ­ sluĹľby ani dokonÄŤenĂ­ odinstalace.
- DoplĹkovĂ© selhĂˇnĂ­ musĂ­ bĂ˝t viditelnÄ› zaznamenĂˇno jako varovĂˇnĂ­ bez citlivĂ˝ch ĂşdajĹŻ. PĹ™Ă­kaz mĹŻĹľe skonÄŤit ĂşspÄ›ĹˇnÄ›, pokud kritickĂˇ ÄŤĂˇst instalace nebo odinstalace uspÄ›la.
- Chyba registrace nesmĂ­ ponechat neplatnou nebo ÄŤĂˇsteÄŤnÄ› sestavenou hodnotu.

## 2. `autoConnectOnStartup`

### KonfiguraÄŤnĂ­ kontrakt

- PĹ™idat boolean `masterSetup.autoConnectOnStartup`.
- V serializovanĂ©m vĂ˝chozĂ­m dokumentu bude umĂ­stÄ›n jako poslednĂ­ ÄŤlen `masterSetup`, bezprostĹ™ednÄ› za `useDefaultAccessKeyInWizard`.
- VĂ˝chozĂ­ hodnota je `false`.
- VĂ˝chozĂ­ konfigurace vytvoĹ™enĂˇ pĹ™i prvnĂ­m startu nebo instalaci musĂ­ pole obsahovat.
- Konfigurace vytvoĹ™enĂˇ resetem ve wizardu musĂ­ pole obsahovat s hodnotou `false`.
- UloĹľenĂ­ nebo dokonÄŤenĂ­ wizardu musĂ­ existujĂ­cĂ­ hodnotu zachovat. Pokud starĹˇĂ­ dokument pole neobsahuje, vĂ˝stup wizardu jej doplnĂ­ s hodnotou `false`.
- StarĹˇĂ­ konfigurace bez pole zĹŻstĂˇvĂˇ platnĂˇ a chovĂˇ se jako `false`.
- PĹ™Ă­tomnĂˇ neboolean hodnota je validaÄŤnĂ­ chyba stejnĂ˝m zpĹŻsobem jako u ostatnĂ­ch boolean ÄŤlenĹŻ `masterSetup`.
- Tato zmÄ›na sama o sobÄ› nepĹ™idĂˇvĂˇ novĂ© zaĹˇkrtĂˇvacĂ­ polĂ­ÄŤko do wizardu. Volba je souÄŤĂˇstĂ­ konfiguraÄŤnĂ­ho dokumentu.

### ChovĂˇnĂ­ sluĹľby

- PĹ™i `false` zĹŻstane souÄŤasnĂ© chovĂˇnĂ­ po startu beze zmÄ›ny a sluĹľba sama pĹ™ipojenĂ­ nezahĂˇjĂ­.
- PĹ™i `true` sluĹľba po inicializaci ĂşloĹľiĹˇtÄ› konfigurace a VPN enginu bez zbyteÄŤnĂ©ho odkladu spustĂ­ stejnou pĹ™ipojovacĂ­ operaci, jakou pouĹľĂ­vĂˇ bÄ›ĹľnĂ˝ pĹ™Ă­kaz Connect.
- ChovĂˇnĂ­ platĂ­ pĹ™i startu sluĹľby, tedy po restartu operaÄŤnĂ­ho systĂ©mu i po ruÄŤnĂ­m restartu sluĹľby.
- AutomatickĂ© pĹ™ipojenĂ­ nesmĂ­ obchĂˇzet validaci konfigurace, bezpeÄŤnostnĂ­ kontroly, stavovĂ˝ model, retry/recovery logiku ani existujĂ­cĂ­ ochranu proti soubÄ›ĹľnĂ˝m connect operacĂ­m.
- NeĂşspÄ›ĹˇnĂ˝ pokus nesmĂ­ shodit ani ukonÄŤit sluĹľbu. MusĂ­ se projevit standardnĂ­m stavem `error` nebo `recovering`, bĂ˝t dostupnĂ˝ pĹ™es souÄŤasnĂ© status rozhranĂ­ a bĂ˝t bezpeÄŤnÄ› zalogovĂˇn.
- NeplatnĂˇ nebo nedostupnĂˇ konfigurace nesmĂ­ blokovat dokonÄŤenĂ­ startu sluĹľby; automatickĂ© pĹ™ipojenĂ­ selĹľe standardnĂ­ chybovou cestou a sluĹľba zĹŻstane ovladatelnĂˇ.
- ZmÄ›na hodnoty se projevĂ­ pĹ™i pĹ™Ă­ĹˇtĂ­m startu sluĹľby. NenĂ­ poĹľadovĂˇn novĂ˝ mechanismus ĹľivĂ©ho sledovĂˇnĂ­ konfigurace.

## 3. AplikaÄŤnĂ­ a stavovĂ© ikony

### ZdrojovĂ© soubory

- PouĹľĂ­t pĹ™esnÄ› tyto soubory:
  - `assets/default.ico`
  - `assets/connected.ico`
  - `assets/connecting.ico`
  - `assets/disconnected.ico`
- Ikony vloĹľit do Windows resources pĹ™i sestavenĂ­. NainstalovanĂ˝ program nesmĂ­ bĂ˝t zĂˇvislĂ˝ na samostatnĂ˝ch ICO souborech vedle EXE.

### VĂ˝chozĂ­ aplikaÄŤnĂ­ ikona

- `default.ico` bude ikonou vĹˇech dodĂˇvanĂ˝ch produkÄŤnĂ­ch Windows EXE souborĹŻ, zejmĂ©na `phpvpn-service.exe` a `phpvpn-tray.exe`, aby byla viditelnĂˇ v PrĹŻzkumnĂ­ku Windows a systĂ©movĂ˝ch vlastnostech souboru.
- `default.ico` bude nastavena jako velkĂˇ i malĂˇ ikona vĹˇech top-level oken a dialogĹŻ tray aplikace, vÄŤetnÄ› levĂ©ho hornĂ­ho rohu a pĹ™epĂ­naÄŤe oken.
- StavovĂˇ ikona nesmĂ­ nahrazovat aplikaÄŤnĂ­ ikonu formulĂˇĹ™e. StavovĂ© ikony patĹ™Ă­ do notification area.

### MapovĂˇnĂ­ tray ikony

- `connected.ico`: pouze zdravĂ˝ stav `connected`.
- `connecting.ico`: pĹ™ipojovĂˇnĂ­, odpojovĂˇnĂ­ a stav `recovering`.
- `disconnected.ico`: stav `disconnected`, stav `error` a stav nedostupnĂ© nebo neplatnĂ© status odpovÄ›di.
- PĹ™i lokĂˇlnÄ› probĂ­hajĂ­cĂ­m odpojovĂˇnĂ­ musĂ­ tray ukĂˇzat `connecting.ico`, i kdyĹľ poslednĂ­ autoritativnĂ­ snapshot jeĹˇtÄ› hlĂˇsĂ­ `connected`.
- Po dokonÄŤenĂ­ nebo selhĂˇnĂ­ operace se ikona pĹ™epne podle nejnovÄ›jĹˇĂ­ho autoritativnĂ­ho stavu.
- Zachovat korektnĂ­ vĂ˝bÄ›r velikosti ikony pro DPI a notification area.

## DotÄŤenĂ© oblasti

Realizace pravdÄ›podobnÄ› zasĂˇhne zejmĂ©na:

- `client/apps/service/main.cpp`
- `client/apps/service/service_host.cpp` a jeho rozhranĂ­
- balĂ­ÄŤkovĂ© instalaÄŤnĂ­ a odinstalaÄŤnĂ­ skripty v `client/tools/package/` a souvisejĂ­cĂ­ ovÄ›Ĺ™ovacĂ­ skripty
- `client/src/core/config/config.h` nebo pĹ™Ă­sluĹˇnĂ˝ datovĂ˝ model konfigurace
- `client/src/core/config/config_validator.cpp`
- `client/src/core/config/config_defaults.cpp`
- `client/src/core/config/config_document.cpp`
- startovacĂ­ tok sluĹľby v `client/apps/service/service_host.cpp`
- `client/apps/tray/tray_application.cpp`
- `client/apps/tray/ui_resources.*`, `.rc` resources a CMake definice produkÄŤnĂ­ch EXE
- testy konfigurace, wizardu, sluĹľby, tray UI, resources a balĂ­ÄŤkovĂ© instalace.

PĹ™esnĂ˝ seznam souborĹŻ mĂˇ realizĂˇtor potvrdit podle aktuĂˇlnĂ­ho grafu zĂˇvislostĂ­ a build systĂ©mu.

## NefunkÄŤnĂ­ poĹľadavky

- Zachovat bezpeÄŤnostnĂ­ hranici mezi privilegovanou sluĹľbou a uĹľivatelskou tray aplikacĂ­.
- NevytvĂˇĹ™et proces tray aplikace ze sluĹľby.
- Zachovat zpÄ›tnou kompatibilitu existujĂ­cĂ­ch konfiguracĂ­.
- NevnĂˇĹˇet blokujĂ­cĂ­ prĂˇci do startu sluĹľby ani do hlavnĂ­ho UI vlĂˇkna tray aplikace.
- VĹˇechny zĂˇpisy do registru, prĂˇce s cestami a resource identifikĂˇtory musĂ­ mĂ­t deterministickĂ©, testovatelnĂ© chovĂˇnĂ­.
- Instalace a odinstalace musĂ­ bĂ˝t bezpeÄŤnĂ© pĹ™i opakovĂˇnĂ­ a po ÄŤĂˇsteÄŤnĂ©m selhĂˇnĂ­ doplĹkovĂ©ho kroku.

## AkceptaÄŤnĂ­ kritĂ©ria

1. Po instalaci existuje jedinĂˇ sprĂˇvnÄ› citovanĂˇ strojovĂˇ autostart registrace a pĹ™i pĹ™ihlĂˇĹˇenĂ­ bÄ›ĹľnĂ©ho uĹľivatele se spustĂ­ `phpvpn-tray.exe` v jeho interaktivnĂ­ relaci.
2. OpakovanĂˇ instalace nevytvĂˇĹ™Ă­ duplicitnĂ­ poloĹľky. Odinstalace vlastnĂ­ poloĹľku odstranĂ­ a opakovanĂˇ odinstalace nevytvĂˇĹ™Ă­ vedlejĹˇĂ­ Ĺˇkody.
3. SimulovanĂ© selhĂˇnĂ­ zĂˇpisu nebo odstranÄ›nĂ­ autostartu nezabrĂˇnĂ­ ĂşspÄ›chu kritickĂ© instalace nebo odinstalace sluĹľby a vytvoĹ™Ă­ ovÄ›Ĺ™itelnĂ© varovĂˇnĂ­.
4. NovĂˇ i wizardem resetovanĂˇ konfigurace obsahuje na konci `masterSetup` poloĹľku `"autoConnectOnStartup": false` hned za `useDefaultAccessKeyInWizard`.
5. StarĹˇĂ­ konfigurace bez poloĹľky se naÄŤte jako `false`; explicitnĂ­ `true` a `false` se naÄŤtou sprĂˇvnÄ›; jinĂ˝ typ je odmĂ­tnut.
6. Wizard zachovĂˇ explicitnĂ­ hodnotu a pĹ™i uloĹľenĂ­ starĹˇĂ­ konfigurace doplnĂ­ chybÄ›jĂ­cĂ­ hodnotu `false`.
7. PĹ™i `false` sluĹľba po startu sama Connect nevyvolĂˇ.
8. PĹ™i `true` a platnĂ© konfiguraci sluĹľba po startu pĹ™ejde standardnĂ­ cestou pĹ™es `connecting` do `connected`.
9. PĹ™i `true` a neĂşspÄ›ĹˇnĂ©m pĹ™ipojenĂ­ sluĹľba zĹŻstane spuĹˇtÄ›nĂˇ a dostupnĂˇ, zatĂ­mco status a log odpovĂ­dajĂ­ standardnĂ­mu `error` nebo `recovering` chovĂˇnĂ­.
10. ProdukÄŤnĂ­ EXE soubory i vĹˇechna top-level okna pouĹľĂ­vajĂ­ `default.ico`.
11. Tray ikona pouĹľĂ­vĂˇ pĹ™esnĂ© mapovĂˇnĂ­ uvedenĂ© vĂ˝Ĺˇe, vÄŤetnÄ› `connecting.ico` bÄ›hem odpojovĂˇnĂ­ a obnovovĂˇnĂ­.
12. SestavenĂ­ a balĂ­ÄŤek nevyĹľadujĂ­ pĹ™Ă­tomnost samostatnĂ˝ch ICO souborĹŻ za bÄ›hu.
13. AutomatizovanĂ© testy pokrĂ˝vajĂ­ novĂ© konfiguraÄŤnĂ­ chovĂˇnĂ­, start sluĹľby, bezpeÄŤnĂ© selhĂˇnĂ­ registrace, mapovĂˇnĂ­ ikon a resource integraci.
14. StĂˇvajĂ­cĂ­ unit, UI, E2E, resource a packaging testy zĹŻstĂˇvajĂ­ ĂşspÄ›ĹˇnĂ©.

## RealizaÄŤnĂ­ vodĂ­tka

- Autostart drĹľet jako malou, samostatnÄ› testovatelnou Windows platformnĂ­ operaci. KritickĂ˝ vĂ˝sledek instalace sluĹľby a doplĹkovĂ˝ vĂ˝sledek autostartu vyhodnocovat oddÄ›lenÄ›.
- Pro automatickĂ© pĹ™ipojenĂ­ znovu pouĹľĂ­t veĹ™ejnou domĂ©novou operaci enginu, nevytvĂˇĹ™et druhou implementaci connect sekvence.
- KonfiguraÄŤnĂ­ hodnotu pĹ™enĂ©st pĹ™es validaÄŤnĂ­ a dokumentovĂ˝ model tak, aby serializace zachovala poĹ™adĂ­ a starĹˇĂ­ dokumenty zĂ­skaly bezpeÄŤnĂ˝ default.
- ICO soubory vloĹľit pĹ™es `.rc` resources a existujĂ­cĂ­ CMake resource tok. StavovĂ© a aplikaÄŤnĂ­ ikony drĹľet oddÄ›lenÄ›.
