---
intent_version: 4
created_at: 2026-09-13-23-15-27
based_on: intent-v003.md
produces: intent-v004.md
---

AI: MĂˇme zapnutĂ© zachytĂˇvĂˇnĂ­ novĂ©ho intentu pro projekt phpvpn. Nadiktuj mi prvnĂ­ poĹľadavky nebo nĂˇpady a kdyĹľ budeĹˇ chtĂ­t, mĹŻĹľeĹˇ kdykoli Ĺ™Ă­ct pozastavit, pokraÄŤovat nebo zpracovat intent.

User: Narazil jsem na problĂ©m, kterĂ˝ je potĹ™eba dohledat, zjistit, co ho zpĹŻsobuje, a opravit. Stane se to, dÄ›je se to tehdy, kdyĹľ napĹ™Ă­klad na lokĂˇlnĂ­m poÄŤĂ­taÄŤi se pĹ™ihlĂˇsĂ­m k http://127.0.0.1/phpvpn.php, coĹľ je server, kterĂ˝ bÄ›ĹľĂ­ v D:\Nginx\html. Tam je moĹľnĂ© najĂ­t ten skript. Je to nejaktuĂˇlnÄ›jĹˇĂ­ phpvpn.php. A kdyĹľ se z lokĂˇlnĂ­ho systĂ©mu na tohleto PHP pĹ™ihlĂˇsĂ­m, vĹˇechno se zdĂˇ OK. Potom nainstaluju na jinĂ˝ poÄŤĂ­taÄŤ ten samĂ˝ klient, to znamenĂˇ nejnovÄ›jĹˇĂ­ verzi, nejnovÄ›jĹˇĂ­ distribuci, kterou mĂˇme, tak jsem nainstaloval na jinĂ˝ poÄŤĂ­taÄŤ, na virtuĂˇlnĂ­ stroj. PĹ™ihlĂˇsil jsem se taky k phpvpn.php, kterĂ© u nĂˇs bÄ›ĹľĂ­. Pingnul jsem si z jinĂ©ho poÄŤĂ­taÄŤe na tento poÄŤĂ­taÄŤ, coĹľ je super, ale jakmile ping skonÄŤil, tak uĹľ se neposĂ­lajĂ­ ĹľĂˇdnĂ© dalĹˇĂ­ pakety. A pĹ™esto se neustĂˇle tenhle ten stroj dotazuje serveru ve frekvenci, kterĂˇ byla stanovena serverem, coĹľ je tuĹˇĂ­m teÄŹ padesĂˇt milisekund, a vĹŻbec tam neprobĂ­hĂˇ ĹľĂˇdnĂ˝ decay, to znamenĂˇ ten interval se nezdvojnĂˇsobuje. A ani kdyĹľ na tenhle ten stroj nepĹ™ichĂˇzejĂ­ vĹŻbec ĹľĂˇdnĂ© pakety.

AI: MĂˇm to poznamenanĂ©, pokraÄŤuj prosĂ­m.

User: No, to je v podstatÄ› vĹˇe, je tĹ™eba zjistit, ÄŤĂ­m to je, odladit to a zabezpeÄŤit, aby se to nedÄ›lo.

AI: Beru i ten poĹľadavek na diagnostiku, opravu a prevenci opakovĂˇnĂ­. AĹľ budeĹˇ chtĂ­t, Ĺ™ekni zpracovat intent.

User: Zpracuj intent.

AI: NaĹˇla jsem pravdÄ›podobnou pĹ™Ă­ÄŤinu: klient bere kumulativnĂ­ ÄŤĂ­taÄŤe paketĹŻ, takĹľe po prvnĂ­m pingu uĹľ interval zĹŻstane poĹ™Ăˇd na minimu. Navrhuju opravit to v klientovi, pĹ™idat regresnĂ­ testy a ovÄ›Ĺ™it proti souÄŤasnĂ©mu serveru, bez Ăşprav nasazenĂ©ho skriptu, pokud se neprokĂˇĹľe jeho podĂ­l. SedĂ­ ti tenhle rozsah? KdyĹľ neodpovĂ­Ĺˇ, vezmu to jako vĂ˝chozĂ­.

User: MyslĂ­m, Ĺľe ano, Ĺľe jsi to Ĺ™ekla dobĹ™e, to znamenĂˇ najĂ­t pĹ™Ă­ÄŤinu, ovÄ›Ĺ™it, Ĺľe to je skuteÄŤnÄ› ona, pĹ™idat testy, pokud je to nutnĂ©, tak opravit dokumentaci, ale tĂ­m si nejsem ĂşplnÄ› jistĂ˝, a potom nezapomenout vytvoĹ™it distribuci.

AI vytvoĹ™ila tento artefakt, kterĂ˝ byl v konverzaci pĹ™edstaven odkazem:

~~~markdown
---
intent_version: 1
created_at: 2026-09-13-22-56-14
source: intent-chat-v001.md
---

# Oprava neustupujĂ­cĂ­ho minimĂˇlnĂ­ho polling intervalu po provozu

## ShrnutĂ­

Zjistit, spolehlivÄ› prokĂˇzat a odstranit pĹ™Ă­ÄŤinu chovĂˇnĂ­, pĹ™i kterĂ©m Windows klient po ukonÄŤenĂ­ skuteÄŤnĂ©ho sĂ­ĹĄovĂ©ho provozu nadĂˇle volĂˇ datovĂ˝ endpoint serveru v minimĂˇlnĂ­m serverem urÄŤenĂ©m intervalu. Oprava musĂ­ obnovit oÄŤekĂˇvanĂ˝ idle decay, zabrĂˇnit regresi automatickĂ˝mi testy, podle potĹ™eby sladit dokumentaci a skonÄŤit vytvoĹ™enĂ­m aktuĂˇlnĂ­ distribuce.

## PozorovanĂ˝ scĂ©nĂˇĹ™

1. Na lokĂˇlnĂ­m poÄŤĂ­taÄŤi bÄ›ĹľĂ­ aktuĂˇlnĂ­ serverovĂ˝ skript `D:\Nginx\html\phpvpn.php`, dostupnĂ˝ jako `http://127.0.0.1/phpvpn.php`.
2. LokĂˇlnĂ­ klient se k serveru pĹ™ipojĂ­ a spojenĂ­ se jevĂ­ jako funkÄŤnĂ­.
3. NejnovÄ›jĹˇĂ­ dostupnĂˇ klientskĂˇ distribuce se nainstaluje takĂ© na jinĂ˝ poÄŤĂ­taÄŤ, konkrĂ©tnÄ› na virtuĂˇlnĂ­ stroj, a pĹ™ipojĂ­ se ke stejnĂ©mu serveru.
4. Mezi stroji probÄ›hne ĂşspÄ›ĹˇnĂ˝ ping.
5. Po skonÄŤenĂ­ pingu uĹľ nejsou odesĂ­lĂˇny ani pĹ™ijĂ­mĂˇny dalĹˇĂ­ VPN pakety.
6. PĹ™esto klient pokraÄŤuje v dotazovĂˇnĂ­ serveru pĹ™ibliĹľnÄ› kaĹľdĂ˝ch 50 ms, tedy v aktuĂˇlnĂ­m minimĂˇlnĂ­m intervalu. Polling interval se pĹ™i neÄŤinnosti nezdvojnĂˇsobuje.

## Kontext projektu a pracovnĂ­ hypotĂ©za

- Serverem pĹ™edĂˇvanĂ© limity jsou v klientovi reprezentovĂˇny hodnotami `minDataPollIntervalMs` a `maxDataPollIntervalMs`. AktuĂˇlnĂ­ vĂ˝chozĂ­ hodnoty v kĂłdu jsou 50 ms a 3200 ms.
- `NetworkWorker::nextPollDelayMs(bool hadTraffic)` v `client/src/core/runtime/network_worker.cpp` resetuje interval na minimum pĹ™i `hadTraffic == true` a pĹ™i neÄŤinnosti ho nĂˇsobĂ­ dvÄ›ma aĹľ do maxima.
- `VpnEngine::runNetworkThread()` v `client/src/core/runtime/vpn_engine.cpp` nynĂ­ volĂˇ tuto funkci s podmĂ­nkou `packetsReceived_ > 0 || packetsSent_ > 0`.
- `packetsReceived_` a `packetsSent_` jsou v `runConnectedIteration()` kumulativnĂ­ ÄŤĂ­taÄŤe za pĹ™ipojenĂ­. Po prvnĂ­m pĹ™enesenĂ©m paketu proto podmĂ­nka zĹŻstĂˇvĂˇ pravdivĂˇ i v dalĹˇĂ­ch prĂˇzdnĂ˝ch iteracĂ­ch.
- Toto je silnĂˇ pracovnĂ­ hypotĂ©za pĹ™Ă­ÄŤiny, nikoli nĂˇhrada poĹľadovanĂ©ho ovÄ›Ĺ™enĂ­. Realizace ji musĂ­ reprodukcĂ­ nebo cĂ­lenĂ˝m testem potvrdit, pĹ™Ă­padnÄ› zdokumentovat jinou skuteÄŤnou pĹ™Ă­ÄŤinu.
- PĹ™Ă­mĂ© testovacĂ­ pokrytĂ­ volĂˇnĂ­ `nextPollDelayMs` z bÄ›hu pĹ™ipojenĂ©ho klienta nebylo pĹ™i pĹ™Ă­pravÄ› intentu nalezeno.

## PoĹľadovanĂ© zmÄ›ny

### Diagnostika a dĹŻkaz pĹ™Ă­ÄŤiny

- Reprodukovat popsanĂ˝ problĂ©m na aktuĂˇlnĂ­m zdrojovĂ©m stavu nebo vytvoĹ™it deterministickĂ˝ test, kterĂ˝ pĹ™esnÄ› zachytĂ­ stejnĂ˝ stavovĂ˝ pĹ™echod: provoz v jednĂ© ÄŤi vĂ­ce iteracĂ­ch, nĂˇsledovanĂ© prĂˇzdnĂ˝mi iteracemi.
- ProkĂˇzat, zda je pĹ™Ă­ÄŤinou pouĹľitĂ­ kumulativnĂ­ch ÄŤĂ­taÄŤĹŻ mĂ­sto aktivity aktuĂˇlnĂ­ datovĂ© vĂ˝mÄ›ny.
- Pokud se hypotĂ©za nepotvrdĂ­, pokraÄŤovat v diagnostice aĹľ k doloĹľenĂ© pĹ™Ă­ÄŤinÄ› a opravit skuteÄŤnou zĂˇvadu v tĂ©mĹľe funkÄŤnĂ­m rozsahu.
- OdliĹˇit skuteÄŤnĂ© VPN pakety v aktuĂˇlnĂ­ datovĂ© vĂ˝mÄ›nÄ› od samotnĂ©ho ĂşspÄ›ĹˇnĂ©ho HTTP pollu, keepalive komunikace, health kontroly a historickĂ©ho provozu.

### Oprava klienta

- RozhodnutĂ­, zda je aktuĂˇlnĂ­ iterace aktivnĂ­, musĂ­ vychĂˇzet z paketĹŻ skuteÄŤnÄ› odeslanĂ˝ch nebo pĹ™ijatĂ˝ch v tĂ©to iteraci, nikoli z kumulativnĂ­ch diagnostickĂ˝ch ÄŤĂ­taÄŤĹŻ za celĂ© pĹ™ipojenĂ­.
- Iterace s alespoĹ jednĂ­m skuteÄŤnÄ› odeslanĂ˝m nebo pĹ™ijatĂ˝m VPN paketem mĂˇ polling interval vrĂˇtit na serverem povolenĂ© minimum.
- KaĹľdĂˇ nĂˇsledujĂ­cĂ­ iterace bez skuteÄŤnĂ©ho provozu mĂˇ aplikovat existujĂ­cĂ­ nĂˇsobek idle backoffu a respektovat serverem urÄŤenĂ© minimum a maximum.
- Oprava nesmĂ­ poruĹˇit zpracovĂˇnĂ­ pĹ™Ă­chozĂ­ch ÄŤi odchozĂ­ch paketĹŻ, recovery logiku, ÄŤasovou synchronizaci, health kontrolu ani diagnostickĂ© kumulativnĂ­ ÄŤĂ­taÄŤe.
- NasazenĂ˝ `D:\Nginx\html\phpvpn.php` se nemĂˇ mÄ›nit, pokud diagnostika neprokĂˇĹľe podĂ­l serverovĂ© implementace. PĹ™Ă­padnĂˇ nutnĂˇ serverovĂˇ zmÄ›na musĂ­ bĂ˝t nejprve provedena v autoritativnĂ­m zdroji repozitĂˇĹ™e.

### Testy

- PĹ™idat regresnĂ­ automatickĂ˝ test na pĹ™echod z aktivnĂ­ho provozu do neÄŤinnosti.
- Test musĂ­ selhat proti pĹŻvodnĂ­mu chybnĂ©mu chovĂˇnĂ­ a projĂ­t po opravÄ›.
- OvÄ›Ĺ™it alespoĹ tyto pĹ™Ă­pady:
  - skuteÄŤnĂ˝ provoz resetuje interval na minimum,
  - prvnĂ­ prĂˇzdnĂˇ iterace po provozu zahĂˇjĂ­ prodluĹľovĂˇnĂ­ intervalu,
  - dalĹˇĂ­ prĂˇzdnĂ© iterace interval postupnÄ› zdvojnĂˇsobujĂ­ aĹľ k serverem stanovenĂ©mu maximu,
  - historickĂ˝ paket uĹľ po vyprĂˇzdnÄ›nĂ­ provozu neudrĹľuje polling na minimu,
  - novĂ˝ provoz po obdobĂ­ neÄŤinnosti znovu vrĂˇtĂ­ interval na minimum.
- Spustit relevantnĂ­ jednotkovĂ©, integraÄŤnĂ­ a runtime testy odpovĂ­dajĂ­cĂ­ dotÄŤenĂ© vrstvÄ›. NeprovedenĂ© testy nesmĂ­ bĂ˝t hlĂˇĹˇeny jako ĂşspÄ›ĹˇnĂ©.
- Pokud je dostupnĂ© odpovĂ­dajĂ­cĂ­ prostĹ™edĂ­, provĂ©st takĂ© praktickĂ© ovÄ›Ĺ™enĂ­ se dvÄ›ma klienty nebo s virtuĂˇlnĂ­m strojem proti aktuĂˇlnĂ­mu serveru.

### Dokumentace

- Zkontrolovat autoritativnĂ­ dokumentaci klientskĂ©ho polling chovĂˇnĂ­, zejmĂ©na `client.md`, a souvisejĂ­cĂ­ distribuÄŤnĂ­ pokyny.
- Dokumentaci zmÄ›nit pouze tehdy, pokud je souÄŤasnĂ˝ popis neĂşplnĂ˝, nesprĂˇvnĂ˝ nebo pokud oprava zavĂˇdĂ­ uĹľivatelsky ÄŤi provoznÄ› vĂ˝znamnou zmÄ›nu. Pokud zmÄ›na nenĂ­ nutnĂˇ, uvĂ©st tuto skuteÄŤnost v zĂˇvÄ›reÄŤnĂ©m reportu.

### Distribuce

- Po ĂşspÄ›ĹˇnĂ© implementaci a ovÄ›Ĺ™enĂ­ vytvoĹ™it novou distribuci z aktuĂˇlnĂ­ho zdrojovĂ©ho stavu podle zĂˇvaznĂ© metodiky v `distribution.md`.
- Znovu vytvoĹ™it projektovĂ˝ strom `dist`, vÄŤetnÄ› aktuĂˇlnĂ­ho `dist/server/phpvpn.php`, kompletnĂ­ho Windows x64 balĂ­ÄŤku, poĹľadovanĂ˝ch platformnĂ­ch adresĂˇĹ™ĹŻ, licencĂ­ a podepsanĂ©ho manifestu.
- DistribuÄŤnĂ­ klientskĂ© binĂˇrky musĂ­ obsahovat opravu a odpovĂ­dat aktuĂˇlnĂ­m zdrojĹŻm.
- OvÄ›Ĺ™it Ăşplnost, architekturu, integritu, podpisovĂ© artefakty a shodu distribuovanĂ©ho serverovĂ©ho skriptu s aktuĂˇlnĂ­m zdrojem.
- PĹ™edat pĹ™esnou cestu k vĂ˝slednĂ© distribuci a pravdivĂ˝ souhrn provedenĂ˝ch kontrol.

## PravdÄ›podobnÄ› dotÄŤenĂ© oblasti

- `client/src/core/runtime/vpn_engine.cpp`
- `client/src/core/runtime/vpn_engine.h`, pokud bude potĹ™eba zmÄ›nit rozhranĂ­ nebo uchovĂˇvanĂ˝ stav
- `client/src/core/runtime/network_worker.cpp`
- `client/src/core/runtime/network_worker.h`
- relevantnĂ­ testy pod `client/tests/`, pĹ™Ă­padnÄ› integraÄŤnĂ­ scĂ©nĂˇĹ™e pod `tests-integration/`
- `client.md`, pouze pokud kontrola prokĂˇĹľe potĹ™ebu aktualizace
- `distribution.md` a distribuÄŤnĂ­ nĂˇstroje pouze tehdy, pokud jejich souÄŤasnĂ˝ postup brĂˇnĂ­ vytvoĹ™enĂ­ poĹľadovanĂ© ĂşplnĂ© distribuce

## AkceptaÄŤnĂ­ kritĂ©ria

1. PĹ™Ă­ÄŤina je doloĹľena reprodukcĂ­, regresnĂ­m testem nebo jinĂ˝m kontrolovatelnĂ˝m dĹŻkazem.
2. Po ukonÄŤenĂ­ skuteÄŤnĂ©ho VPN provozu uĹľ klient nezĹŻstĂˇvĂˇ trvale na minimĂˇlnĂ­m polling intervalu kvĹŻli historickĂ˝m paketĹŻm.
3. PĹ™i neÄŤinnosti se interval zvÄ›tĹˇuje existujĂ­cĂ­m faktorem dva aĹľ k serverem stanovenĂ©mu maximu a nikdy neporuĹˇĂ­ serverem stanovenĂ© meze.
4. NovĂ˝ skuteÄŤnĂ˝ provoz interval sprĂˇvnÄ› vrĂˇtĂ­ na minimum.
5. RelevantnĂ­ automatickĂ© testy pokrĂ˝vajĂ­ regresi a prochĂˇzejĂ­.
6. PraktickĂ© ovÄ›Ĺ™enĂ­ proti aktuĂˇlnĂ­mu serveru je provedeno, pokud je potĹ™ebnĂ© prostĹ™edĂ­ dostupnĂ©, jinak je omezenĂ­ vĂ˝slovnÄ› uvedeno.
7. Dokumentace je buÄŹ aktualizovĂˇna, nebo je doloĹľeno, proÄŤ zmÄ›nu nepotĹ™ebovala.
8. Je vytvoĹ™enĂˇ a ovÄ›Ĺ™enĂˇ aktuĂˇlnĂ­ distribuce obsahujĂ­cĂ­ opravenĂ©ho klienta a aktuĂˇlnĂ­ serverovĂ˝ skript.
9. ZĂˇvÄ›reÄŤnĂ˝ report uvede zmÄ›nÄ›nĂ© soubory, dĹŻkaz pĹ™Ă­ÄŤiny, provedenĂ© testy, pĹ™Ă­padnĂˇ omezenĂ­ a pĹ™esnou cestu k distribuci.

## RealizaÄŤnĂ­ omezenĂ­

- Intent sĂˇm neautorizuje commit ani push.
- ZmÄ›ny majĂ­ bĂ˝t provedeny pĹ™Ă­mo v aktivnĂ­ feature branch projektu.
- NeprovĂˇdÄ›t nesouvisejĂ­cĂ­ zmÄ›ny a zachovat existujĂ­cĂ­ protokolovĂ© a bezpeÄŤnostnĂ­ vlastnosti.
~~~

AI: HotovĂ˝ intent verze jedna je pĹ™ipravenĂ˝. ChceĹˇ nejdĹ™Ă­v udÄ›lat realizaÄŤnĂ­ plĂˇn, nebo jdeme rovnou na realizaci?

User: Tak jeĹˇtÄ› bych tam chtÄ›l doplnit pĂˇr dalĹˇĂ­ch vÄ›cĂ­, kterĂ© se toho pĹ™Ă­mo netĂ˝kajĂ­, ale jsou to drobnosti, ale je tĹ™eba je udÄ›lat. PrvnĂ­ z nich je na Communication Contracts. KdyĹľ jsme pĹ™ipojenĂ­ a uĹľivatel zmÄ›nĂ­ Communication Contract a dĂˇ Save, zobrazĂ­ se hlĂˇĹˇka â€žYour changes have been saved. Disconnect and connect again to apply them.â€ś Je striktnÄ› vyĹľadovĂˇno, aby se odpojil a znovu pĹ™ipojil, proto se mĂˇ dialog zeptat â€žChcete se nynĂ­ odpojit?â€ś, s volbami Ano a Ne. Ano mĂˇ bĂ˝t vĂ˝chozĂ­ a mĂˇ uĹľivatele rovnou odpojit.

AI: HotovĂ˝ intent verze jedna je pĹ™ipravenĂ˝, zahrnula jsem i ty doplĹky k dialogu a distribuÄŤnĂ­m krokĹŻm. ChceĹˇ nejdĹ™Ă­v udÄ›lat realizaÄŤnĂ­ plĂˇn, nebo jdeme rovnou na realizaci?

User: Zapracovala jsi to poslednĂ­, co jsem Ĺ™Ă­kal, s tĂ­m odpojenĂ­m?

AI: Kontrola odhalila nesoulad. Intent verze jedna poĹľadavek na potvrzovacĂ­ dialog a okamĹľitĂ© odpojenĂ­ po volbÄ› â€žAnoâ€ś neobsahuje, pĹ™estoĹľe nĂˇslednĂˇ hlasovĂˇ odpovÄ›ÄŹ tvrdila opak. Tento poĹľadavek je v pĹ™episu vĂ˝slovnÄ› potvrzenĂ˝, takĹľe pĹ™ipravĂ­m opravenou Ăşplnou verzi dvÄ›.

User: Pokud ne, tak jeĹˇtÄ› poÄŤkej, pĹ™idĂˇm ti dalĹˇĂ­ vÄ›ci.

AI: PoÄŤkĂˇm. Verzi dvÄ› zatĂ­m nevytvĂˇĹ™Ă­m a zachytĂˇvĂˇm dalĹˇĂ­ doplnÄ›nĂ­. PokraÄŤuj.

User: Na Show Server Info, kdyĹľ zobrazujeme zelenĂ˝ Ĺ™Ăˇdek, tak bych ho chtÄ›l o troĹˇiÄŤku vĂ­c zelenĂ˝. Ta svÄ›tle zelenĂˇ je moc svÄ›tlĂˇ, chtÄ›l bych ho o troĹˇiÄŤku vĂ­c zelenĂ˝, jako mĂ©nÄ› svÄ›tlĂ˝. Ĺ˝lutou bych drĹľel ĂşplnÄ› stejnÄ› Ĺľlutou, jakĂˇ se teÄŹ pouĹľĂ­vĂˇ na podbarvenĂ­ Ĺ™Ăˇdku v Communication Contracts, kdyĹľ se nÄ›jakĂ˝ Ĺ™Ăˇdek upravĂ­. DalĹˇĂ­ vÄ›c, kterou bych chtÄ›l upravit, je Raw Server Information. Po pĹ™epnutĂ­ na druhĂ˝ tab JSON, kterĂ˝ se mi zobrazuje, nenĂ­ nijak formĂˇtovanĂ˝, chybĂ­ tam indent. MyslĂ­m si, Ĺľe dĹ™Ă­v tam byl. NevĂ­m, jestli je to chyba serveru nebo klienta, ale Ĺ™eknÄ›me, Ĺľe je to spĂ­Ĺˇ problĂ©m na klientovi. Pokud moĹľno, naformĂˇtoval bych to, pokud to nenĂ­ velkĂ˝ problĂ©m. DĂˇle bych v Server Information dal tlaÄŤĂ­tko OK modĹ™e. Vedle tlaÄŤĂ­tka Refresh bych dal na pravou stranu na stejnou ĂşroveĹ pĹ™epĂ­naÄŤ â€žAuto refreshâ€ś. KdyĹľ bude zapnutĂ˝, bude se pravidelnÄ› provĂˇdÄ›t totĂ©Ĺľ, jako kdyby uĹľivatel stiskl tlaÄŤĂ­tko Refresh.

AI: Zaznamenala jsem to. PokraÄŤuj s dalĹˇĂ­mi doplnÄ›nĂ­mi.

User: Ne, beru zpÄ›t, co pÄ›t vteĹ™in probÄ›hne ten Auto refresh.

AI: Zaznamenala jsem opravu intervalu Auto refresh na pÄ›t sekund. PokraÄŤuj.

User: NahoĹ™e za Server Information je teÄŹ hlĂˇĹˇka â€žone valid online clientâ€ś, coĹľ u jedniÄŤky a mnoĹľnĂ©ho ÄŤĂ­sla nedĂˇvĂˇ smysl. Beru zpÄ›t pĹ™edchozĂ­ nĂˇvrhy, udÄ›lĂˇme to jinak. Nech tam to, co tam je teÄŹ, v zĂˇvorce bude zelenĂ˝ text. KdyĹľ bude jeden, bude tam jednotnĂ© ÄŤĂ­slo. KdyĹľ tam bude cokoliv jinĂ©ho neĹľ jeden, jako dva, tĹ™i, ÄŤtyĹ™i, pÄ›t, bude mnoĹľnĂ© ÄŤĂ­slo. A nula tam bude takĂ© sprĂˇvnÄ› podle angliÄŤtiny.

AI: Zaznamenala jsem koneÄŤnou variantu textu v zĂˇvorce, vÄŤetnÄ› sprĂˇvnĂ©ho jednotnĂ©ho a mnoĹľnĂ©ho ÄŤĂ­sla podle angliÄŤtiny. PokraÄŤuj.

User: To je vĹˇechno, zapracuj to do intentu.

AI: ZbĂ˝vĂˇ jedna volba: mĂˇ bĂ˝t Auto refresh pĹ™i kaĹľdĂ©m otevĹ™enĂ­ okna Server Information vypnutĂ˝ a uĹľivatel ho zapne ruÄŤnÄ›? VĂ˝chozĂ­ nĂˇvrh je ano, s intervalem pÄ›t sekund, na obou zĂˇloĹľkĂˇch a bez pĹ™ekrĂ˝vĂˇnĂ­ poĹľadavkĹŻ. NeodpovÄ›zenĂ­ pĹ™ijĂ­mĂˇ tento nĂˇvrh.

User: VĂ˝chozĂ­ varianta je skuteÄŤnÄ›, Ĺľe to nenĂ­ zapnuto a uĹľivatel to musĂ­ vĹľdycky zapnout, kdyĹľ na ten formulĂˇĹ™ pĹ™ijde, to znamenĂˇ vĹľdycky, kdyĹľ se ten formulĂˇĹ™ zobrazĂ­, tak to bude vypnutĂ©.

AI: PoslednĂ­ rozhodnutĂ­ je jasnĂ©: Auto refresh bude pĹ™i kaĹľdĂ©m novĂ©m zobrazenĂ­ okna vypnutĂ˝ a uĹľivatel ho musĂ­ ruÄŤnÄ› zapnout. VĹˇechny materiĂˇlnĂ­ volby jsou tĂ­m uzavĹ™enĂ© a pĹ™ipravuji Ăşplnou verzi dvÄ›.

User: PochopitelnÄ› tuhle volbu nikde neperzistujeme, ta je jenom in memory, kdyĹľ je ten formulĂˇĹ™ zobrazenĂ˝. A jeĹˇtÄ› jednu vÄ›c. VrĂˇtĂ­m se k ĂşplnÄ› prvnĂ­mu Ăşkolu s neustĂˇlĂ˝m kontaktovĂˇnĂ­m serveru. Po kaĹľdĂ©m pollu bez dat nebudeme interval okamĹľitÄ› zdvojnĂˇsobovat. Po poslednĂ­ch pĹ™ijatĂ˝ch datech zĹŻstanou prvnĂ­ dva prĂˇzdnĂ© pokusy na nejrychlejĹˇĂ­m intervalu. Poll s daty okamĹľitÄ› skoÄŤĂ­ na nejrychlejĹˇĂ­ interval, napĹ™Ă­klad 50 ms. DalĹˇĂ­ polly s daty zĹŻstĂˇvajĂ­ na 50 ms. PrvnĂ­ poll bez dat zvĂ˝ĹˇĂ­ ÄŤĂ­taÄŤ, ale zĹŻstane na 50 ms. DruhĂ˝ poll bez dat znovu zvĂ˝ĹˇĂ­ ÄŤĂ­taÄŤ, ale stĂˇle zĹŻstane na 50 ms. TĹ™etĂ­ poll bez dat, kdy uĹľ ÄŤĂ­taÄŤ dosĂˇhl dvojky, zaÄŤne zdvojnĂˇsobovat na 100 ms a dalĹˇĂ­ prĂˇzdnĂ© polly pokraÄŤujĂ­ na 200 ms, 400 ms, 800 ms a tak dĂˇle aĹľ k maximu.

AI: NovĂ© upĹ™esnÄ›nĂ­ pĹ™iĹˇlo po zapsĂˇnĂ­ verze dvÄ›, takĹľe ji nepĹ™epĂ­Ĺˇu. FinĂˇlnĂ­ dokument vytvoĹ™Ă­m jako Ăşplnou verzi tĹ™i. DoplnĂ­m nepersistovanĂ˝ stav Auto refresh a dvÄ› prĂˇzdnĂˇ pollovĂˇnĂ­ na minimĂˇlnĂ­m intervalu pĹ™ed zaÄŤĂˇtkem zdvojnĂˇsobovĂˇnĂ­.

User: AĹľ to budeĹˇ mĂ­t vĹˇechno hotovo, tak mi pĹ™iprav finĂˇlnĂ­ intent dokument, ve kterĂ©m je vĹˇechno, o ÄŤem jsme se tady teÄŹ bavili.

AI: PotĹ™ebuji potvrdit jedinĂ˝ detail: majĂ­ se dvÄ› prĂˇzdnĂˇ pollovĂˇnĂ­ poÄŤĂ­tat aĹľ po jakĂ©mkoli skuteÄŤnĂ©m VPN provozu, tedy po odeslanĂ©m nebo pĹ™ijatĂ©m paketu? VĂ˝chozĂ­ nĂˇvrh je ano. JakĂ˝koli odeslanĂ˝ ÄŤi pĹ™ijatĂ˝ paket v aktuĂˇlnĂ­ vĂ˝mÄ›nÄ› vynuluje ÄŤĂ­taÄŤ prĂˇzdnĂ˝ch pollĹŻ a vrĂˇtĂ­ interval na minimum.

User: Ano.

AI vytvořila tento artefakt, který byl v konverzaci představen odkazem:

~~~markdown
---
intent_version: 3
created_at: 2026-09-13-23-10-32
based_on: intent-v002.md
source: intent-chat-v003.md
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
~~~

AI: Finální intent verze tři je hotový a obsahuje všechno z tohoto rozhovoru. Chceš nejdřív vytvořit realizační plán, nebo přejít rovnou k realizaci? Pro nový kontext zároveň potřebuji zvolený model a úroveň úsilí.

User: Ještě bych ten intent chtěl upravit. Jak jsme se bavili o čítači, kde první dva pokusy po přijetí dat zůstávají na minimu a potom se začne zdvojnásobovat, chtěl bych to upravit jednodušeji. Poté, co přestaneme přijímat nebo odesílat jakékoli pakety, nech polling na nejvyšší rychlosti po dobu pěti sekund a teprve potom začni interval zdvojnásobovat.

AI: Nové pravidlo nahrazuje dvoupollový čítač časovým oknem. Po posledním skutečně odeslaném nebo přijatém VPN paketu zůstane klient pět sekund na minimálním intervalu a teprve potom začne při pokračující nečinnosti zdvojnásobovat. Připravím úplnou verzi čtyři.

