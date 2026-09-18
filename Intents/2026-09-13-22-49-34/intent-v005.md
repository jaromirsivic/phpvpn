---
intent_version: 5
created_at: 2026-09-13-23-19-36
based_on: intent-v002.md
source: intent-chat-v005.md
---

# Oprava polling backoffu a Ăşpravy klientskĂ©ho UI

## ShrnutĂ­

Zjistit, prokĂˇzat a odstranit pĹ™Ă­ÄŤinu chovĂˇnĂ­, pĹ™i kterĂ©m Windows klient po ukonÄŤenĂ­ skuteÄŤnĂ©ho sĂ­ĹĄovĂ©ho provozu nadĂˇle volĂˇ datovĂ˝ endpoint serveru v minimĂˇlnĂ­m serverem urÄŤenĂ©m intervalu. OpravenĂ˝ backoff musĂ­ po skuteÄŤnĂ©m provozu ponechat dva prĂˇzdnĂ© polly na minimĂˇlnĂ­m intervalu a zdvojnĂˇsobovĂˇnĂ­ zahĂˇjit aĹľ tĹ™etĂ­m prĂˇzdnĂ˝m pollem. SouÄŤasnÄ› upravit nĂˇslednou akci po uloĹľenĂ­ Communication Contracts, vzhled a chovĂˇnĂ­ okna Server Information, zobrazenĂ­ Raw Server Information a gramatiku poÄŤtu validnĂ­ch online klientĹŻ. ZmÄ›ny musĂ­ bĂ˝t pokrytĂ© pĹ™imÄ›Ĺ™enĂ˝mi testy, podle potĹ™eby promĂ­tnutĂ© do dokumentace a zakonÄŤenĂ© vytvoĹ™enĂ­m aktuĂˇlnĂ­ distribuce.

## 1. Polling interval po ukonÄŤenĂ­ provozu

### PozorovanĂ˝ scĂ©nĂˇĹ™

1. Na lokĂˇlnĂ­m poÄŤĂ­taÄŤi bÄ›ĹľĂ­ aktuĂˇlnĂ­ serverovĂ˝ skript `D:\Nginx\html\phpvpn.php`, dostupnĂ˝ jako `http://127.0.0.1/phpvpn.php`.
2. LokĂˇlnĂ­ klient se k serveru pĹ™ipojĂ­ a spojenĂ­ se jevĂ­ jako funkÄŤnĂ­.
3. NejnovÄ›jĹˇĂ­ dostupnĂˇ klientskĂˇ distribuce se nainstaluje takĂ© na jinĂ˝ poÄŤĂ­taÄŤ, konkrĂ©tnÄ› na virtuĂˇlnĂ­ stroj, a pĹ™ipojĂ­ se ke stejnĂ©mu serveru.
4. Mezi stroji probÄ›hne ĂşspÄ›ĹˇnĂ˝ ping.
5. Po skonÄŤenĂ­ pingu uĹľ nejsou odesĂ­lĂˇny ani pĹ™ijĂ­mĂˇny dalĹˇĂ­ VPN pakety.
6. PĹ™esto klient pokraÄŤuje v dotazovĂˇnĂ­ serveru pĹ™ibliĹľnÄ› kaĹľdĂ˝ch 50 ms. Polling interval se pĹ™i neÄŤinnosti nezaÄŤne zvÄ›tĹˇovat.

### ProjektovĂˇ fakta a pracovnĂ­ hypotĂ©za

- Serverem pĹ™edĂˇvanĂ© limity jsou v klientovi reprezentovĂˇny hodnotami `minDataPollIntervalMs` a `maxDataPollIntervalMs`. AktuĂˇlnĂ­ vĂ˝chozĂ­ hodnoty v kĂłdu jsou 50 ms a 3200 ms.
- `NetworkWorker::nextPollDelayMs(bool hadTraffic)` v `client/src/core/runtime/network_worker.cpp` resetuje interval na minimum pĹ™i `hadTraffic == true` a pĹ™i neÄŤinnosti ho nĂˇsobĂ­ dvÄ›ma aĹľ do maxima.
- `VpnEngine::runNetworkThread()` v `client/src/core/runtime/vpn_engine.cpp` nynĂ­ volĂˇ tuto funkci s podmĂ­nkou `packetsReceived_ > 0 || packetsSent_ > 0`.
- `packetsReceived_` a `packetsSent_` jsou v `runConnectedIteration()` kumulativnĂ­ ÄŤĂ­taÄŤe za pĹ™ipojenĂ­. Po prvnĂ­m pĹ™enesenĂ©m paketu proto podmĂ­nka zĹŻstĂˇvĂˇ pravdivĂˇ i v dalĹˇĂ­ch prĂˇzdnĂ˝ch iteracĂ­ch.
- Toto je silnĂˇ pracovnĂ­ hypotĂ©za, nikoli nĂˇhrada poĹľadovanĂ©ho ovÄ›Ĺ™enĂ­. Realizace ji musĂ­ reprodukcĂ­ nebo cĂ­lenĂ˝m testem potvrdit, pĹ™Ă­padnÄ› zdokumentovat jinou skuteÄŤnou pĹ™Ă­ÄŤinu.
- PĹ™Ă­mĂ© testovacĂ­ pokrytĂ­ volĂˇnĂ­ `nextPollDelayMs` z bÄ›hu pĹ™ipojenĂ©ho klienta nebylo pĹ™i pĹ™Ă­pravÄ› intentu nalezeno.

### Definice provozu a prĂˇzdnĂ©ho pollu

- Za skuteÄŤnĂ˝ provoz v aktuĂˇlnĂ­ datovĂ© vĂ˝mÄ›nÄ› se povaĹľuje alespoĹ jeden VPN paket skuteÄŤnÄ› odeslanĂ˝ nebo pĹ™ijatĂ˝ klientem.
- JakĂ˝koli takovĂ˝ paket vynuluje ÄŤĂ­taÄŤ po sobÄ› jdoucĂ­ch prĂˇzdnĂ˝ch pollĹŻ a vrĂˇtĂ­ polling interval na serverem povolenĂ© minimum.
- Za prĂˇzdnĂ˝ poll se povaĹľuje dokonÄŤenĂˇ datovĂˇ vĂ˝mÄ›na, pĹ™i kterĂ© nebyl odeslĂˇn ani pĹ™ijat ĹľĂˇdnĂ˝ VPN paket.
- SamotnĂ˝ ĂşspÄ›ĹˇnĂ˝ HTTP poĹľadavek, prĂˇzdnĂˇ serverovĂˇ odpovÄ›ÄŹ, keepalive, health kontrola, ÄŤasovĂˇ synchronizace ani historickĂ˝ provoz se nepovaĹľujĂ­ za skuteÄŤnĂ˝ provoz aktuĂˇlnĂ­ datovĂ© vĂ˝mÄ›ny.

### PoĹľadovanĂ˝ backoff po provozu

- Po kaĹľdĂ© aktivnĂ­ datovĂ© vĂ˝mÄ›nÄ› je interval na serverem povolenĂ©m minimu a ÄŤĂ­taÄŤ prĂˇzdnĂ˝ch pollĹŻ je nula.
- PrvnĂ­ nĂˇsledujĂ­cĂ­ prĂˇzdnĂ˝ poll zvĂ˝ĹˇĂ­ ÄŤĂ­taÄŤ na jedna, ale dalĹˇĂ­ poll se stĂˇle naplĂˇnuje v minimĂˇlnĂ­m intervalu.
- DruhĂ˝ po sobÄ› jdoucĂ­ prĂˇzdnĂ˝ poll zvĂ˝ĹˇĂ­ ÄŤĂ­taÄŤ na dva, ale dalĹˇĂ­ poll se stĂˇle naplĂˇnuje v minimĂˇlnĂ­m intervalu.
- TĹ™etĂ­ po sobÄ› jdoucĂ­ prĂˇzdnĂ˝ poll zahĂˇjĂ­ backoff. NĂˇsledujĂ­cĂ­ interval se zvÄ›tĹˇĂ­ na dvojnĂˇsobek minima.
- KaĹľdĂ˝ dalĹˇĂ­ po sobÄ› jdoucĂ­ prĂˇzdnĂ˝ poll interval znovu zdvojnĂˇsobĂ­ aĹľ k serverem urÄŤenĂ©mu maximu.
- PĹ™i minimu 50 ms je oÄŤekĂˇvanĂˇ posloupnost po poslednĂ­ aktivnĂ­ vĂ˝mÄ›nÄ›: 50 ms, 50 ms pro prvnĂ­ dva prĂˇzdnĂ© polly, potĂ© 100 ms, 200 ms, 400 ms, 800 ms a dĂˇle aĹľ k maximu.
- NovĂ˝ odeslanĂ˝ nebo pĹ™ijatĂ˝ VPN paket v libovolnĂ© fĂˇzi backoffu okamĹľitÄ› vrĂˇtĂ­ interval na minimum a vynuluje ÄŤĂ­taÄŤ prĂˇzdnĂ˝ch pollĹŻ.
- Serverem urÄŤenĂ© minimum a maximum jsou zĂˇvaznĂ©. Klient je nesmĂ­ podkroÄŤit ani pĹ™ekroÄŤit.

### Diagnostika a oprava

- Reprodukovat problĂ©m na aktuĂˇlnĂ­m zdrojovĂ©m stavu nebo vytvoĹ™it deterministickĂ˝ test zachycujĂ­cĂ­ provoz a nĂˇslednĂ© prĂˇzdnĂ© iterace.
- ProkĂˇzat, zda je pĹ™Ă­ÄŤinou pouĹľitĂ­ kumulativnĂ­ch ÄŤĂ­taÄŤĹŻ mĂ­sto aktivity aktuĂˇlnĂ­ datovĂ© vĂ˝mÄ›ny. Pokud se hypotĂ©za nepotvrdĂ­, pokraÄŤovat aĹľ k doloĹľenĂ© skuteÄŤnĂ© pĹ™Ă­ÄŤinÄ›.
- Implementovat explicitnĂ­, bezpeÄŤnÄ› resetovanĂ˝ stav pro poÄŤet po sobÄ› jdoucĂ­ch prĂˇzdnĂ˝ch pollĹŻ nebo ekvivalentnĂ­ Ĺ™eĹˇenĂ­ se stejnĂ˝m pozorovatelnĂ˝m chovĂˇnĂ­m.
- RozhodnutĂ­ o backoffu musĂ­ vychĂˇzet z vĂ˝sledku aktuĂˇlnĂ­ vĂ˝mÄ›ny, nikoli z kumulativnĂ­ch diagnostickĂ˝ch ÄŤĂ­taÄŤĹŻ.
- Oprava nesmĂ­ poruĹˇit zpracovĂˇnĂ­ paketĹŻ, recovery logiku, ÄŤasovou synchronizaci, health kontrolu ani diagnostickĂ© kumulativnĂ­ ÄŤĂ­taÄŤe.
- Stav prĂˇzdnĂ˝ch pollĹŻ musĂ­ bĂ˝t sprĂˇvnÄ› inicializovĂˇn a resetovĂˇn pĹ™i konfiguraci, novĂ©m pĹ™ipojenĂ­, odpojenĂ­, recovery a zmÄ›nÄ› serverovĂ˝ch limitĹŻ podle ĹľivotnĂ­ho cyklu souÄŤasnĂ©ho polling stavu.
- NasazenĂ˝ `D:\Nginx\html\phpvpn.php` se nemĂˇ mÄ›nit, pokud diagnostika neprokĂˇĹľe podĂ­l serveru. PĹ™Ă­padnĂˇ nutnĂˇ serverovĂˇ zmÄ›na musĂ­ vzniknout nejprve v autoritativnĂ­m zdroji repozitĂˇĹ™e.

### PovinnĂ© regresnĂ­ scĂ©nĂˇĹ™e

- AktivnĂ­ vĂ˝mÄ›na s odeslanĂ˝m paketem resetuje interval a prĂˇzdnĂ˝ ÄŤĂ­taÄŤ.
- AktivnĂ­ vĂ˝mÄ›na s pĹ™ijatĂ˝m paketem resetuje interval a prĂˇzdnĂ˝ ÄŤĂ­taÄŤ.
- PrvnĂ­ a druhĂ˝ prĂˇzdnĂ˝ poll po provozu ponechajĂ­ interval na minimu.
- TĹ™etĂ­ prĂˇzdnĂ˝ poll zvĂ˝ĹˇĂ­ nĂˇsledujĂ­cĂ­ interval na dvojnĂˇsobek minima.
- DalĹˇĂ­ prĂˇzdnĂ© polly pokraÄŤujĂ­ v posloupnosti nĂˇsobku dva aĹľ k maximu.
- HistorickĂ˝ paket uĹľ po vyprĂˇzdnÄ›nĂ­ provozu neudrĹľuje polling trvale na minimu.
- NovĂ˝ provoz uprostĹ™ed backoffu okamĹľitÄ› vrĂˇtĂ­ interval na minimum a znovu poskytne dva prĂˇzdnĂ© polly na minimu.
- MeznĂ­ hodnoty serverovĂ©ho minima a maxima zĹŻstĂˇvajĂ­ dodrĹľenĂ©.
- RegresnĂ­ test musĂ­ selhat proti pĹŻvodnĂ­mu chybnĂ©mu chovĂˇnĂ­ a projĂ­t po opravÄ›.

## 2. UloĹľenĂ­ Communication Contracts pĹ™i aktivnĂ­m pĹ™ipojenĂ­

### SouÄŤasnĂ˝ stav

Po uloĹľenĂ­ zmÄ›nÄ›nĂ©ho Communication Contract v pĹ™ipojenĂ©m stavu se zobrazuje pouze informace:

`Your changes have been saved. Disconnect and connect again to apply them.`

### PoĹľadovanĂ© chovĂˇnĂ­

- Pokud je klient v okamĹľiku ĂşspÄ›ĹˇnĂ©ho uloĹľenĂ­ pĹ™ipojenĂ˝, zobrazit potvrzovacĂ­ dialog, kterĂ˝ sdÄ›lĂ­, Ĺľe zmÄ›ny byly uloĹľeny, Ĺľe pro jejich pouĹľitĂ­ je striktnÄ› vyĹľadovĂˇno odpojenĂ­ a novĂ© pĹ™ipojenĂ­, a zeptĂˇ se na okamĹľitĂ© odpojenĂ­.
- VĂ˝chozĂ­ anglickĂ˝ text dialogu mĂˇ vĂ˝znamovÄ› odpovĂ­dat:

  `Your changes have been saved. It is strictly required that you disconnect and connect again to apply them. Do you want to disconnect now?`

- Dialog musĂ­ mĂ­t volby `Yes` a `No`.
- `Yes` je vĂ˝chozĂ­ odpovÄ›ÄŹ.
- Volba `Yes` okamĹľitÄ› spustĂ­ standardnĂ­ odpojenĂ­ klienta. NeprovĂˇdĂ­ automatickĂ© opÄ›tovnĂ© pĹ™ipojenĂ­.
- Volba `No` dialog zavĹ™e, ponechĂˇ klienta pĹ™ipojenĂ©ho a zachovĂˇ fakt, Ĺľe novĂ© contracts se uplatnĂ­ aĹľ po budoucĂ­m odpojenĂ­ a pĹ™ipojenĂ­.
- Dialog se zobrazĂ­ aĹľ po potvrzenĂ©m ĂşspÄ›ĹˇnĂ©m uloĹľenĂ­. SelhĂˇnĂ­ nebo neurÄŤitĂ˝ vĂ˝sledek uloĹľenĂ­ nesmĂ­ vyvolat odpojenĂ­.
- Pokud klient v okamĹľiku uloĹľenĂ­ pĹ™ipojenĂ˝ nenĂ­, zachovat odpovĂ­dajĂ­cĂ­ souÄŤasnĂ˝ tok bez zbyteÄŤnĂ© vĂ˝zvy k odpojenĂ­.

## 3. Okno Server Information

### 3.1 Barvy Ĺ™ĂˇdkĹŻ

- U validnĂ­ho online klienta zmÄ›nit zelenĂ© pozadĂ­ Ĺ™Ăˇdku pouze v oknÄ› Server Information na o nÄ›co sytÄ›jĹˇĂ­ a mĂ©nÄ› svÄ›tlou zelenou neĹľ souÄŤasnĂˇ `successBackground` barva `#F0FAF2`.
- NovĂ˝ odstĂ­n mĂˇ zĹŻstat vizuĂˇlnÄ› umĂ­rnÄ›nĂ˝, zapadat do souÄŤasnĂ© palety a zachovat dobrou ÄŤitelnost textu a stavĹŻ vĂ˝bÄ›ru.
- Ĺ˝lutĂ© pozadĂ­ pĹ™Ă­sluĹˇnĂ©ho Ĺ™Ăˇdku v Server Information musĂ­ pĹ™esnÄ› pouĹľĂ­vat stejnou paletovou barvu jako zmÄ›nÄ›nĂ˝ Ĺ™Ăˇdek v Communication Contracts, tedy existujĂ­cĂ­ roli `contractsChanged` s hodnotou `#FFF2B3`, nikoli souÄŤasnou svÄ›tlejĹˇĂ­ `warningBackground`.
- ZmÄ›na zelenĂ© je omezena na Ĺ™Ăˇdky Server Information. NemĂˇ bez ovÄ›Ĺ™enĂ­ globĂˇlnÄ› ztmavit ĂşspÄ›ĹˇnĂ© stavy v ostatnĂ­ch oknech.

### 3.2 FormĂˇtovĂˇnĂ­ Raw Server Information

- Na zĂˇloĹľce `Raw server information` zobrazit serverovĂ˝ JSON ÄŤitelnÄ› s odsazenĂ­m a Ĺ™ĂˇdkovĂˇnĂ­m.
- Preferovat klientskĂ© formĂˇtovĂˇnĂ­ z jiĹľ ovÄ›Ĺ™enĂ˝ch JSON dat, aby kvĹŻli ÄŤistÄ› prezentaÄŤnĂ­ zmÄ›nÄ› nebylo nutnĂ© mÄ›nit wire formĂˇt ani server.
- PouĹľĂ­t existujĂ­cĂ­ projektovĂ˝ JSON parser a serializaci, pokud zachovĂˇ vĹˇechny hodnoty a jejich vĂ˝znam. NezavĂˇdÄ›t ad hoc textovĂ© formĂˇtovĂˇnĂ­.
- Zachovat read-only charakter pole, monospace font, moĹľnost vĂ˝bÄ›ru a kopĂ­rovĂˇnĂ­, scrollovĂˇnĂ­ a stĂˇvajĂ­cĂ­ zachovĂˇnĂ­ vĂ˝bÄ›ru a viditelnĂ© pozice pĹ™i refreshi, pokud je to moĹľnĂ©.
- Pokud by bezpeÄŤnĂ© formĂˇtovĂˇnĂ­ konkrĂ©tnĂ­ho ovÄ›Ĺ™enĂ©ho payloadu selhalo, nesmĂ­ se informace ztratit. Zobrazit pĹŻvodnĂ­ ovÄ›Ĺ™enĂ˝ obsah nebo jasnĂ˝ neblokujĂ­cĂ­ stav mĂ­sto prĂˇzdnĂ©ho pole ÄŤi pĂˇdu.
- ServerovĂ˝ skript kvĹŻli samotnĂ©mu odsazenĂ­ JSON nemÄ›nit.

### 3.3 TlaÄŤĂ­tko OK

- TlaÄŤĂ­tko `OK` ve spodnĂ­ ÄŤĂˇsti okna Server Information zobrazit modĹ™e jako primĂˇrnĂ­ akci.
- Zachovat jeho souÄŤasnou funkci zavĹ™enĂ­ okna, klĂˇvesovou obsluhu, focus a pĹ™Ă­stupnost.
- OstatnĂ­ tlaÄŤĂ­tka nemÄ›nit nad rĂˇmec toho, co vyĹľaduje konzistentnĂ­ a ovÄ›Ĺ™enĂ© rozloĹľenĂ­ novĂ©ho Auto refresh prvku.

### 3.4 Auto refresh

- Vedle tlaÄŤĂ­tka `Refresh`, napravo od nÄ›j a na stejnĂ© vodorovnĂ© Ăşrovni, pĹ™idat pĹ™epĂ­naÄŤ ve stylu posuvnĂ©ho checkboxu s viditelnĂ˝m nĂˇzvem `Auto refresh`.
- PĹ™i kaĹľdĂ©m novĂ©m otevĹ™enĂ­ nebo zobrazenĂ­ okna Server Information musĂ­ bĂ˝t Auto refresh vypnutĂ˝.
- UĹľivatel musĂ­ Auto refresh pĹ™i kaĹľdĂ©m otevĹ™enĂ­ ruÄŤnÄ› zapnout.
- Stav existuje pouze v pamÄ›ti po dobu Ĺľivota prĂˇvÄ› zobrazenĂ©ho okna. NesmĂ­ se zapisovat do konfigurace, registru, souboru ani jinĂ© perzistentnĂ­ vrstvy a nesmĂ­ se pĹ™enĂ©st do dalĹˇĂ­ho otevĹ™enĂ­ okna.
- Po zapnutĂ­ spustit refresh kaĹľdĂ˝ch pÄ›t sekund.
- AutomatickĂ˝ refresh musĂ­ pouĹľĂ­vat stejnou ovÄ›Ĺ™enou cestu a mĂ­t stejnĂ˝ uĹľivatelskĂ˝ vĂ˝sledek jako ruÄŤnĂ­ stisk tlaÄŤĂ­tka `Refresh`.
- Auto refresh funguje bez ohledu na zvolenou zĂˇloĹľku `Who is online` nebo `Raw server information`.
- NesmĂ­ spustit novĂ˝ poĹľadavek, dokud pĹ™edchozĂ­ refresh stĂˇle probĂ­hĂˇ. Po dokonÄŤenĂ­ se pokraÄŤuje dalĹˇĂ­m Ĺ™ĂˇdnĂ˝m pÄ›tisekundovĂ˝m intervalem bez hromadÄ›nĂ­ zmeĹˇkanĂ˝ch poĹľadavkĹŻ.
- RuÄŤnĂ­ Refresh musĂ­ zĹŻstat dostupnĂ˝ podle stĂˇvajĂ­cĂ­ch pravidel a nesmĂ­ vytvoĹ™it soubÄ›ĹľnĂ˝ poĹľadavek.
- VypnutĂ­ pĹ™epĂ­naÄŤe okamĹľitÄ› zastavĂ­ plĂˇnovĂˇnĂ­ dalĹˇĂ­ch automatickĂ˝ch refreshĹŻ. ZavĹ™enĂ­ nebo zniÄŤenĂ­ okna musĂ­ timer bezpeÄŤnÄ› ukonÄŤit a zahodit jeho in-memory stav.
- SelhĂˇnĂ­ automatickĂ©ho refreshu mĂˇ pouĹľĂ­t stĂˇvajĂ­cĂ­ chovĂˇnĂ­ okna, tedy ponechat pĹ™edchozĂ­ informace a zobrazit neblokujĂ­cĂ­ informaci o selhĂˇnĂ­, pokud jiĹľ model existuje.
- PĹ™epĂ­naÄŤ musĂ­ bĂ˝t ovladatelnĂ˝ myĹˇĂ­ i klĂˇvesnicĂ­, sprĂˇvnÄ› zaĹ™azenĂ˝ do tab orderu a pĹ™Ă­stupnÄ› pojmenovanĂ˝.

### 3.5 PoÄŤet validnĂ­ch online klientĹŻ

- Zachovat souÄŤasnĂ˝ vĂ˝znam a strukturu nadpisu `Server information`, vÄŤetnÄ› poÄŤtu validnĂ­ch online klientĹŻ v zĂˇvorce.
- Opravit anglickĂ© jednotnĂ© a mnoĹľnĂ© ÄŤĂ­slo:
  - `Server information (1 valid online client)` pro pĹ™esnÄ› jednoho klienta,
  - `Server information (0 valid online clients)` pro nulu,
  - `Server information (N valid online clients)` pro vĹˇechny hodnoty jinĂ© neĹľ jedna.
- V obsahovĂ©m nadpisu okna zobrazit celĂ˝ text v zĂˇvorce zelenÄ›. ZĂˇkladnĂ­ text `Server information` zĹŻstane ve stĂˇvajĂ­cĂ­ barvÄ›.
- SystĂ©movĂ˝ titulek okna, kde nelze obarvit pouze ÄŤĂˇst textu, musĂ­ alespoĹ pouĹľĂ­vat stejnou gramaticky sprĂˇvnou textovou hodnotu bez poĹľadavku na ÄŤĂˇsteÄŤnĂ© obarvenĂ­.
- ZelenĂˇ musĂ­ zĹŻstat ÄŤitelnĂˇ a odpovĂ­dat souÄŤasnĂ©mu stylu ĂşspÄ›ĹˇnĂ˝ch stavĹŻ.

## 4. Testy a ovÄ›Ĺ™enĂ­

- Doplnit automatickĂ© testy polling regrese vÄŤetnÄ› pĹ™esnĂ© dvoupollovĂ© prodlevy pĹ™ed zahĂˇjenĂ­m backoffu.
- RozĹˇĂ­Ĺ™it testy Communication Contracts o ĂşspÄ›ĹˇnĂ© uloĹľenĂ­ v pĹ™ipojenĂ©m stavu, vĂ˝znam dialogu, vĂ˝chozĂ­ volbu `Yes`, odpojenĂ­ po `Yes`, setrvĂˇnĂ­ po `No` a absenci odpojenĂ­ pĹ™i neĂşspÄ›ĹˇnĂ©m uloĹľenĂ­.
- RozĹˇĂ­Ĺ™it testy Server Information alespoĹ o:
  - sprĂˇvnĂ© barvy validnĂ­ho a ĹľlutÄ› zvĂ˝raznÄ›nĂ©ho Ĺ™Ăˇdku,
  - ÄŤitelnÄ› odsazenĂ˝ Raw Server Information JSON,
  - zachovĂˇnĂ­ obsahu a stability pĹ™i selhĂˇnĂ­ formĂˇtovĂˇnĂ­ nebo refreshu,
  - modrou primĂˇrnĂ­ roli tlaÄŤĂ­tka `OK`,
  - pĹ™Ă­tomnost, vĂ˝chozĂ­ vypnutĂ˝ stav, neperzistovĂˇnĂ­, pĹ™Ă­stupnost a tab order pĹ™epĂ­naÄŤe Auto refresh,
  - pÄ›tisekundovĂ˝ interval, oba taby, zĂˇkaz pĹ™ekryvu poĹľadavkĹŻ a zastavenĂ­ timeru,
  - jednotnĂ© ÄŤĂ­slo pro hodnotu jedna a mnoĹľnĂ© ÄŤĂ­slo pro nulu i vĂ­ce klientĹŻ,
  - zelenou barvu zĂˇvorkovĂ© ÄŤĂˇsti obsahovĂ©ho nadpisu.
- Zachovat a spustit relevantnĂ­ jednotkovĂ©, integraÄŤnĂ­, runtime a UI testy. NeprovedenĂ© testy nesmĂ­ bĂ˝t vykĂˇzĂˇny jako ĂşspÄ›ĹˇnĂ©.
- Pokud je dostupnĂ© odpovĂ­dajĂ­cĂ­ prostĹ™edĂ­, prakticky ovÄ›Ĺ™it polling se dvÄ›ma klienty nebo virtuĂˇlnĂ­m strojem proti aktuĂˇlnĂ­mu serveru a ruÄŤnÄ› zkontrolovat vĂ˝slednĂ˝ vzhled v podporovanĂ˝ch DPI a motivech.

## 5. Dokumentace

- Zkontrolovat `client.md`, souvisejĂ­cĂ­ UI a runtime dokumentaci a distribuÄŤnĂ­ pokyny.
- Dokumentaci zmÄ›nit pouze tehdy, pokud je souÄŤasnĂ˝ popis po realizaci neĂşplnĂ˝, nesprĂˇvnĂ˝ nebo pokud zmÄ›na zavĂˇdĂ­ uĹľivatelsky ÄŤi provoznÄ› vĂ˝znamnĂ© chovĂˇnĂ­, kterĂ© projekt dokumentuje.
- Pokud dokumentaÄŤnĂ­ zmÄ›na nenĂ­ nutnĂˇ, uvĂ©st tuto skuteÄŤnost v zĂˇvÄ›reÄŤnĂ©m reportu.

## 6. Distribuce

- Po ĂşspÄ›ĹˇnĂ© implementaci a ovÄ›Ĺ™enĂ­ vytvoĹ™it novou distribuci z aktuĂˇlnĂ­ho zdrojovĂ©ho stavu podle zĂˇvaznĂ© metodiky v `distribution.md`.
- Znovu vytvoĹ™it projektovĂ˝ strom `dist`, vÄŤetnÄ› aktuĂˇlnĂ­ho `dist/server/phpvpn.php`, kompletnĂ­ho Windows x64 balĂ­ÄŤku, poĹľadovanĂ˝ch platformnĂ­ch adresĂˇĹ™ĹŻ, licencĂ­ a podepsanĂ©ho manifestu.
- DistribuÄŤnĂ­ klientskĂ© binĂˇrky musĂ­ obsahovat vĹˇechny zmÄ›ny z tohoto intentu a odpovĂ­dat aktuĂˇlnĂ­m zdrojĹŻm.
- OvÄ›Ĺ™it Ăşplnost, architekturu, integritu, podpisovĂ© artefakty, instalaÄŤnĂ­ a provoznĂ­ chovĂˇnĂ­ pĹ™edepsanĂ© metodikou a shodu distribuovanĂ©ho serverovĂ©ho skriptu s aktuĂˇlnĂ­m zdrojem.
- PĹ™edat pĹ™esnou cestu k vĂ˝slednĂ© distribuci a pravdivĂ˝ souhrn provedenĂ˝ch kontrol.

## PravdÄ›podobnÄ› dotÄŤenĂ© oblasti

- `client/src/core/runtime/vpn_engine.cpp` a `.h`
- `client/src/core/runtime/network_worker.cpp` a `.h`
- `client/apps/tray/contracts_window.cpp`
- `client/apps/tray/system_dialog.cpp` a `.h`, pokud souÄŤasnĂ© API nepodporuje poĹľadovanĂ© Yes/No potvrzenĂ­
- `client/apps/tray/server_info_window.cpp` a `.h`
- `client/apps/tray/server_info_layout.h`
- `client/apps/tray/ui_style.h` nebo lokĂˇlnĂ­ prezentaÄŤnĂ­ role, pokud je potĹ™ebnĂ˝ novĂ˝ zelenĂ˝ odstĂ­n
- relevantnĂ­ testy pod `client/tests/`, pĹ™Ă­padnÄ› scĂ©nĂˇĹ™e pod `tests-integration/`
- `client.md` pouze pĹ™i prokĂˇzanĂ© potĹ™ebÄ› dokumentaÄŤnĂ­ zmÄ›ny
- `distribution.md` a distribuÄŤnĂ­ nĂˇstroje pouze tehdy, pokud souÄŤasnĂ˝ postup brĂˇnĂ­ vytvoĹ™enĂ­ ĂşplnĂ© distribuce

## SouhrnnĂˇ akceptaÄŤnĂ­ kritĂ©ria

1. PĹ™Ă­ÄŤina polling zĂˇvady je doloĹľena kontrolovatelnĂ˝m dĹŻkazem.
2. HistorickĂ© pakety po skonÄŤenĂ­ provozu jiĹľ neudrĹľujĂ­ polling trvale na minimĂˇlnĂ­m intervalu.
3. Po jakĂ©mkoli odeslanĂ©m nebo pĹ™ijatĂ©m VPN paketu zĹŻstanou prvnĂ­ dva prĂˇzdnĂ© polly na minimĂˇlnĂ­m intervalu a tĹ™etĂ­ prĂˇzdnĂ˝ poll zahĂˇjĂ­ zdvojnĂˇsobovĂˇnĂ­.
4. Idle interval dĂˇle roste nĂˇsobkem dva aĹľ k serverovĂ©mu maximu a novĂ˝ provoz ho vracĂ­ na minimum s vynulovanĂ˝m prĂˇzdnĂ˝m ÄŤĂ­taÄŤem.
5. Po uloĹľenĂ­ contracts v pĹ™ipojenĂ©m stavu nabĂ­zĂ­ dialog `Yes` jako vĂ˝chozĂ­ volbu a `Yes` klienta skuteÄŤnÄ› odpojĂ­.
6. Server Information pouĹľĂ­vĂˇ poĹľadovanou Ĺľlutou, sytÄ›jĹˇĂ­ zelenou, modrĂ© tlaÄŤĂ­tko OK a gramaticky sprĂˇvnĂ˝ poÄŤet klientĹŻ se zelenĂ˝m textem v zĂˇvorce.
7. Raw Server Information je ÄŤitelnÄ› naformĂˇtovanĂ˝ bez zmÄ›ny vĂ˝znamu nebo ztrĂˇty dat.
8. Auto refresh je pĹ™i kaĹľdĂ©m otevĹ™enĂ­ vypnutĂ˝, existuje pouze v pamÄ›ti otevĹ™enĂ©ho okna a po ruÄŤnĂ­m zapnutĂ­ obnovuje kaĹľdĂ˝ch pÄ›t sekund na obou tabech bez pĹ™ekrĂ˝vajĂ­cĂ­ch se poĹľadavkĹŻ.
9. RelevantnĂ­ automatickĂ© testy pokrĂ˝vajĂ­ novĂ© chovĂˇnĂ­ a prochĂˇzejĂ­.
10. PraktickĂ© ovÄ›Ĺ™enĂ­ je provedeno, pokud je potĹ™ebnĂ© prostĹ™edĂ­ dostupnĂ©, jinak je omezenĂ­ vĂ˝slovnÄ› uvedeno.
11. Dokumentace je aktualizovĂˇna, nebo je doloĹľeno, proÄŤ zmÄ›nu nepotĹ™ebovala.
12. Je vytvoĹ™enĂˇ a ovÄ›Ĺ™enĂˇ aktuĂˇlnĂ­ distribuce obsahujĂ­cĂ­ vĹˇechny zmÄ›ny a aktuĂˇlnĂ­ serverovĂ˝ skript.
13. ZĂˇvÄ›reÄŤnĂ˝ report uvede zmÄ›nÄ›nĂ© soubory, dĹŻkazy, provedenĂ© testy, omezenĂ­ a pĹ™esnou cestu k distribuci.

## RealizaÄŤnĂ­ omezenĂ­

- Intent sĂˇm neautorizuje commit ani push.
- ZmÄ›ny se majĂ­ provĂ©st pĹ™Ă­mo v aktivnĂ­ feature branch projektu.
- NeprovĂˇdÄ›t nesouvisejĂ­cĂ­ zmÄ›ny a zachovat existujĂ­cĂ­ protokolovĂ©, bezpeÄŤnostnĂ­, pĹ™Ă­stupnostnĂ­ a instalaÄŤnĂ­ vlastnosti.

