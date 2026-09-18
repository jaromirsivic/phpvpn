---
intent_version: 2
created_at: 2026-09-18-20-08-20
based_on: intent-v001.md
produces: intent-v002.md
---

# ZdrojovĂˇ konverzace

PoznĂˇmka: HlasovĂ˝ pĹ™epis obsahoval prĹŻbÄ›ĹľnÄ› opakovanĂ© a pĹ™ekrĂ˝vajĂ­cĂ­ se Ăşseky. NĂ­Ĺľe jsou zachyceny vĹˇechny vÄ›cnĂ© uĹľivatelskĂ© a AI pĹ™Ă­spÄ›vky v chronologickĂ©m poĹ™adĂ­, pĹ™iÄŤemĹľ pouze technickĂ© duplikĂˇty vzniklĂ© pĹ™ekryvem pĹ™episu jsou slouÄŤeny.

**User:** Ahoj, chci vytvoĹ™it novĂ˝ intent. To znamenĂˇ zĂˇmÄ›r na Ăşpravu systĂ©mu.

**AI:** NovĂ˝ intent jsem zaloĹľila a zachytĂˇvĂˇnĂ­ je aktivnĂ­. PokraÄŤuj v diktovĂˇnĂ­. Kdykoli mĹŻĹľeĹˇ Ĺ™Ă­ct â€žpozastavitâ€ś, â€žpokraÄŤovatâ€ś nebo â€žzpracovat intentâ€ś.

**User:** AktuĂˇlnÄ› v systĂ©mu na stranÄ› klienta dostĂˇvĂˇme IP pakety do jednotlivĂ˝ch bucketĹŻ a uĹľ teÄŹ je tam buffrovĂˇnĂ­ podle klientskĂ© IP adresy. DÄ›je se to pĹ™ed tĂ­m, neĹľ spoÄŤĂ­tĂˇme hash z pĹ™ijatĂ˝ch bufferĹŻ, zakĂłdujeme je a pĹ™edĂˇme VPN serveru. KdyĹľ se klient pĹ™ipojĂ­ k druhĂ©mu klientovi prostĹ™ednictvĂ­m zabezpeÄŤenĂ©ho spojenĂ­ a jedna aplikace, napĹ™Ă­klad FTP, zcela vytĂ­ĹľĂ­ paketovou komunikaci, ostatnĂ­ aplikace se stanou nepouĹľitelnĂ© nebo se k nim pakety vĹŻbec nedostanou. PotĹ™ebuji jednotlivĂ© buckety dĂˇle rozÄŤlenit tak, Ĺľe kaĹľdĂ˝ bude mĂ­t ĹˇestnĂˇct sub-bucketĹŻ.

**AI:** Detail o ĹˇestnĂˇcti dynamicky pĹ™idÄ›lovanĂ˝ch sub-bucketech mĂˇm zachycenĂ˝ v aktivnĂ­m intentu.

**User:** V rĂˇmci kaĹľdĂ©ho bucketu bude dalĹˇĂ­ch ĹˇestnĂˇct sub-bucketĹŻ, tedy podbufferĹŻ, kterĂ© se budou dynamicky pĹ™idÄ›lovat jednotlivĂ˝m sluĹľbĂˇm, pokud budou vĹˇechny vyuĹľity. Asi je rozdÄ›lĂ­me klasickou pÄ›ticĂ­: zdrojovĂˇ IP adresa, cĂ­lovĂˇ IP adresa a protokol. Co tam je dĂˇl?

**AI:** BÄ›hem zachytĂˇvĂˇnĂ­ obsah nerozebĂ­rĂˇm. ChceĹˇ-li teÄŹ doplnit chybÄ›jĂ­cĂ­ poloĹľky tĂ© pÄ›tice, Ĺ™ekni â€žpozastavit intentâ€ś a odpovĂ­m ti.

**User:** TeÄŹ zastav zaznamenĂˇvĂˇnĂ­ a diskutuj se mnou.

**AI:** ZachytĂˇvĂˇnĂ­ jsem pozastavila. DosavadnĂ­ obsah zĹŻstĂˇvĂˇ souÄŤĂˇstĂ­ intentu a teÄŹ mĹŻĹľeme nĂˇvrh normĂˇlnÄ› prodiskutovat. KlasickĂˇ sĂ­ĹĄovĂˇ pÄ›tice je zdrojovĂˇ IP adresa, cĂ­lovĂˇ IP adresa, zdrojovĂ˝ port, cĂ­lovĂ˝ port a transportnĂ­ protokol. U protokolĹŻ bez portĹŻ bude potĹ™eba urÄŤit nĂˇhradnĂ­ klĂ­ÄŤ nebo je zaĹ™adit do spoleÄŤnĂ©ho sub-bucketu.

**User:** PĹ™esnÄ› tak. Chci dĂˇl brainstormovat. KdyĹľ bÄ›ĹľĂ­ hodnÄ› aplikacĂ­, chci uvnitĹ™ komunikace z jednĂ© IP adresy na druhou sub-bucketovĂ˝ systĂ©m, aby jedna aplikace na nÄ›jakĂ©m portu neutlaÄŤovala jinou. Je to dobrĂ˝ zpĹŻsob, nebo mĂˇĹˇ lepĹˇĂ­?

**AI:** LepĹˇĂ­ neĹľ pouhĂ© hashovĂˇnĂ­ do ĹˇestnĂˇcti front je fair queuing nad jednotlivĂ˝mi toky. PÄ›tice je sprĂˇvnĂ˝ identifikĂˇtor toku, ale plĂˇnovaÄŤ musĂ­ aktivnÄ› stĹ™Ă­dat neprĂˇzdnĂ© fronty a omezit, kolik dat z jednĂ© odeĹˇle v jednom kole. DoporuÄŤila bych dvouĂşrovĹovĂ© fĂ©rovĂ© Ĺ™azenĂ­: souÄŤasnĂ˝ bucket pro cĂ­lovĂ©ho klienta ponechat a uvnitĹ™ dynamicky vytvĂˇĹ™et fronty podle pÄ›tice. Ty obsluhovat algoritmem Deficit Round Robin podle poÄŤtu bajtĹŻ. PevnĂ˝ch ĹˇestnĂˇct hashovanĂ˝ch front poskytuje pouze pĹ™ibliĹľnou fĂ©rovost, protoĹľe kolidujĂ­cĂ­ toky se mohou znovu blokovat. PÄ›tice zajiĹˇĹĄuje fĂ©rovost mezi spojenĂ­mi, ne spolehlivÄ› mezi aplikacemi.

**User:** PopiĹˇ mi fair queuing.

**AI:** KaĹľdĂ˝ komunikaÄŤnĂ­ tok mĂˇ vlastnĂ­ frontu a plĂˇnovaÄŤ jim pravidelnÄ› stĹ™Ă­dĂˇ prostor k odesĂ­lĂˇnĂ­. Deficit Round Robin pĹ™idĂˇvĂˇ kaĹľdĂ© frontÄ› v kaĹľdĂ©m kole kredit v bajtech. Fronta mĹŻĹľe odesĂ­lat, dokud mĂˇ dost kreditu, a nevyuĹľitĂ˝ kredit si pĹ™evĂˇdĂ­. ObjemnĂ˝ pĹ™enos tak vyuĹľije kapacitu, kdyĹľ je sĂˇm, ale po pĹ™Ă­chodu malĂ©ho interaktivnĂ­ho toku jej nemĹŻĹľe vyhladovÄ›t. UvnitĹ™ jednoho toku se zachovĂˇ poĹ™adĂ­ paketĹŻ.

**User:** To se mi lĂ­bĂ­. Kdyby bylo vĂ­ce neĹľ ĹˇestnĂˇct spojenĂ­, udÄ›lal bych patnĂˇct samostatnĂ˝ch front a ĹˇestnĂˇctou vyhrazenou pro vĹˇechny ostatnĂ­. Je to v poĹ™Ăˇdku?

**AI:** Je to pouĹľitelnĂ©, pokud je vĂ­ce neĹľ patnĂˇct tokĹŻ vĂ˝jimeÄŤnĂ©, ale vĹˇechny pĹ™etokovĂ© toky by spoleÄŤnÄ› dostaly podĂ­l jedinĂ© fronty a znovu se blokovaly. LepĹˇĂ­ je mĂ­t ĹˇestnĂˇct skuteÄŤnĂ˝ch flow front a dalĹˇĂ­ toky stabilnÄ› rozdÄ›lit mezi vĹˇechny. Pokud by zĹŻstalo Ĺ™eĹˇenĂ­ patnĂˇct plus jedna, sloty potĹ™ebujĂ­ uvolĹovĂˇnĂ­, aby starĂ© toky nezĹŻstaly zvĂ˝hodnÄ›nĂ©.

**User:** Co kdybychom zkombinovali oba pĹ™Ă­stupy? Budeme mĂ­t ĹˇestnĂˇct zpoÄŤĂˇtku volnĂ˝ch front. PrvnĂ­ unikĂˇtnĂ­ pÄ›tice obsadĂ­ prvnĂ­ frontu, druhĂˇ pÄ›tice druhou a tak dĂˇle. KdyĹľ jsou vĹˇechny obsazenĂ© a dorazĂ­ sedmnĂˇctĂˇ pÄ›tice, vypoÄŤte se hash pÄ›tice a jeho poslednĂ­ ÄŤtyĹ™i bity urÄŤĂ­ frontu. Je to lepĹˇĂ­?

**AI:** Ano. Jde o rozumnĂ˝ hybrid mezi pĹ™esnĂ˝m fair queuingem a omezenou pamÄ›tĂ­. NovĂˇ pÄ›tice zĂ­skĂˇ volnou frontu, existujĂ­cĂ­ vazba zĹŻstĂˇvĂˇ stabilnĂ­ a po vyÄŤerpĂˇnĂ­ front se dalĹˇĂ­ pÄ›tice stabilnÄ› pĹ™iĹ™azujĂ­ hashem. Mezi ĹˇestnĂˇcti frontami pobÄ›ĹľĂ­ Deficit Round Robin podle bajtĹŻ. FĂ©rovost bude pĹ™esnĂˇ mezi frontami a pĹ™ibliĹľnĂˇ uvnitĹ™ sdĂ­lenĂ© fronty. Hash musĂ­ kvalitnÄ› promĂ­chat vĹˇechna pole a kvĹŻli ĂşmyslnĂ˝m kolizĂ­m by mÄ›l bĂ˝t nĂˇhodnÄ› klĂ­ÄŤovanĂ˝ pĹ™i startu klienta.

**User:** DrobnĂˇ Ăşprava: kdyĹľ bude vĹˇech ĹˇestnĂˇct front zablokovanĂ˝ch, sedmnĂˇctĂ˝ a kaĹľdĂ˝ dalĹˇĂ­ paket se uloĹľĂ­ do nĂˇhodnĂ© fronty. Pokud je fronta prĂˇzdnĂˇ, jejĂ­ alokace pÄ›tici se smaĹľe. Pokud jsou ve frontÄ› jen pakety jinĂ˝ch pÄ›tic neĹľ pÄ›tice v hlaviÄŤce fronty, fronta se alokuje pÄ›tici prvnĂ­ho paketu, kterĂ˝ v nĂ­ je. Co si o tom myslĂ­Ĺˇ?

**AI:** AutomatickĂ© pĹ™ebĂ­rĂˇnĂ­ uvolnÄ›nĂ© fronty je dobrĂ©, ale nĂˇhodnĂ© pĹ™iĹ™azenĂ­ kaĹľdĂ©ho paketu by mohlo zmÄ›nit poĹ™adĂ­ paketĹŻ jednoho toku, zejmĂ©na u TCP. DoporuÄŤenĂ© chovĂˇnĂ­ je: vlastnĂ­k pouĹľĂ­vĂˇ svou frontu, novĂˇ pÄ›tice zĂ­skĂˇ volnou frontu, po obsazenĂ­ vĹˇech front se pouĹľije stabilnĂ­ hash celĂ© pÄ›tice, prĂˇzdnĂˇ fronta se uvolnĂ­ a fronta bez paketĹŻ pĹŻvodnĂ­ho vlastnĂ­ka mĹŻĹľe atomicky povĂ˝Ĺˇit pÄ›tici svĂ©ho prvnĂ­ho zbĂ˝vajĂ­cĂ­ho paketu. PĹ™ed povĂ˝ĹˇenĂ­m je potĹ™eba ovÄ›Ĺ™it, Ĺľe danĂˇ pÄ›tice nevlastnĂ­ jinou frontu.

**User:** NĂˇhodnĂ© rozmisĹĄovĂˇnĂ­ beru zpÄ›t, vracĂ­me se k hashi. Jinak se mi nĂˇvrh velice lĂ­bĂ­.

**AI:** PĹ™etokovĂ© pÄ›tice se tedy budou stabilnÄ› pĹ™iĹ™azovat pomocĂ­ hashe, nikoli nĂˇhodnÄ› po jednotlivĂ˝ch paketech. DynamickĂ© vlastnictvĂ­ sub-bucketĹŻ a jejich pĹ™ebĂ­rĂˇnĂ­ zĹŻstĂˇvĂˇ podle poslednĂ­ho nĂˇvrhu.

**User:** VytvoĹ™ intent. ChtÄ›l jsem Ĺ™Ă­ct: zpracuj intent.

**AI:** Pro jednoznaÄŤnĂ© zpracovĂˇnĂ­ zbĂ˝vĂˇ urÄŤit smÄ›r provozu, zachĂˇzenĂ­ s fragmenty a protokoly bez portĹŻ a DRR kvantum. VĂ˝chozĂ­ nĂˇvrh je pouze odchozĂ­ smÄ›r, spoleÄŤnĂ˝ fallback podle adres a protokolu a kvantum odvozenĂ© od maximĂˇlnĂ­ velikosti tunelovĂ©ho paketu.

**User:** Co je DRR kvantum?

**AI:** Je to pĹ™Ă­dÄ›l v bajtech pro kaĹľdou frontu v jednom kole. Fronta si jej nasÄŤĂ­tĂˇ, odeĹˇle pakety, dokud mĂˇ kredit, a nespotĹ™ebovanĂ˝ zbytek si nechĂˇ na pĹ™Ă­ĹˇtÄ›, aby dostaly Ĺˇanci i vÄ›tĹˇĂ­ pakety.

**User:** SouhlasĂ­m s vĂ˝chozĂ­m nĂˇvrhem.

## Pokračování konverzace po vytvoření v001

**AI-created artifact:** Po dokončení první verze byl uživateli zobrazen odkaz na následující úplný intent:

---
intent_version: 1
created_at: 2026-09-18-19-52-40
source: intent-chat-v001.md
---

# Flow fair queuing uvnitĹ™ odchozĂ­ch peer front

## ShrnutĂ­

RozĹˇĂ­Ĺ™it klientskĂ© odchozĂ­ paketovĂ© fronty o druhou, vnitĹ™nĂ­ ĂşroveĹ plĂˇnovĂˇnĂ­. KaĹľdĂˇ souÄŤasnĂˇ fronta protistrany bude obsahovat pĹ™esnÄ› 16 sub-bucketĹŻ. AktivnĂ­ sĂ­ĹĄovĂ© toky budou podle moĹľnostĂ­ dostĂˇvat vĂ˝hradnĂ­ sub-bucket a vĹˇechny neprĂˇzdnĂ© sub-buckety budou obsluhovĂˇny pomocĂ­ Deficit Round Robin podle bajtĹŻ. CĂ­lem je zabrĂˇnit tomu, aby objemnĂ˝ tok, napĹ™Ă­klad FTP pĹ™enos, vyhladovÄ›l interaktivnĂ­ nebo jinak mĂ©nÄ› objemnĂ© toky stejnĂ© dvojice klientĹŻ.

## Kontext projektu

- OdchozĂ­ pakety jsou nynĂ­ uloĹľeny v `DirectionQueues` podle IP adresy protistrany. PĹ™ijetĂ­ a ĂşÄŤetnictvĂ­ front je soustĹ™edÄ›no v `client/src/core/queues/packet_queues.h` a `client/src/core/queues/packet_queues.cpp`.
- `RequestScheduler::planNextRequest` v `client/src/core/queues/scheduler.cpp` jiĹľ rotuje poĹ™adĂ­ peer front, aby dlouhĂˇ fronta jednĂ© protistrany nevyhladovÄ›la ostatnĂ­.
- `NetworkWorker::exchangeData` v `client/src/core/runtime/network_worker.cpp` vracĂ­ po neĂşspÄ›ĹˇnĂ©m poĹľadavku naplĂˇnovanĂ© pakety zpÄ›t do odchozĂ­ch front. NovĂ© plĂˇnovĂˇnĂ­ musĂ­ umÄ›t obnovit i pĹŻvodnĂ­ sub-bucket a poĹ™adĂ­.
- SouÄŤasnĂ˝ `Ipv4PacketView` v `client/src/core/protocol/ipv4.h` obsahuje jen adresy a dĂ©lky. Pro klasifikaci toku bude nutnĂ© bezpeÄŤnÄ› doplnit ÄŤtenĂ­ protokolu, fragmentace a TCP/UDP portĹŻ.

## Rozsah

- ZmÄ›na se tĂ˝kĂˇ pouze odchozĂ­ho smÄ›ru pĹ™ed sestavenĂ­m, zaĹˇifrovĂˇnĂ­m a odeslĂˇnĂ­m datovĂ©ho poĹľadavku VPN serveru.
- PĹ™Ă­chozĂ­ fronty pĹ™ed zĂˇpisem do tunelu se v tomto intentu nemÄ›nĂ­.
- Zachovat stĂˇvajĂ­cĂ­ fĂ©rovost mezi protistranami, limity poĹľadavku, ĹˇifrovĂˇnĂ­, formĂˇt pĹ™enosu a validaci paketĹŻ.
- FĂ©rovost je definovĂˇna mezi sĂ­ĹĄovĂ˝mi toky, nikoli mezi procesy nebo skuteÄŤnĂ˝mi aplikacemi. Jedna aplikace pouĹľĂ­vajĂ­cĂ­ vĂ­ce pÄ›tic mĹŻĹľe zĂ­skat vĂ­ce dĂ­lĹŻ neĹľ aplikace s jedinou pÄ›ticĂ­.

## Identita toku

### BÄ›ĹľnĂ˝ TCP a UDP

Tok je urÄŤen smÄ›rovou pÄ›ticĂ­:

1. zdrojovĂˇ IPv4 adresa,
2. cĂ­lovĂˇ IPv4 adresa,
3. zdrojovĂ˝ port,
4. cĂ­lovĂ˝ port,
5. ÄŤĂ­slo IP protokolu.

PÄ›tice se nekanonizuje mezi smÄ›ry. V tomto intentu se pouĹľĂ­vĂˇ jen odchozĂ­ smÄ›r.

### Fragmenty a protokoly bez portĹŻ

- Pro fragmentovanĂ© IPv4 pakety a protokoly, u kterĂ˝ch nelze bezpeÄŤnÄ› pĹ™eÄŤĂ­st TCP/UDP porty, pouĹľĂ­t fallback klĂ­ÄŤ tvoĹ™enĂ˝ zdrojovou adresou, cĂ­lovou adresou a protokolem, s obÄ›ma porty nastavenĂ˝mi na nulu.
- VĹˇechny fragmenty odpovĂ­dajĂ­cĂ­ tomuto fallbacku musĂ­ prochĂˇzet stejnou klasifikacĂ­. Intent nevyĹľaduje fragmentovou mapovacĂ­ cache.
- NeznĂˇmĂ˝ podporovanĂ˝ IP protokol se nesmĂ­ zahodit pouze proto, Ĺľe nemĂˇ porty.
- ÄŚtenĂ­ hlaviÄŤek musĂ­ bĂ˝t dĂ©lkovÄ› bezpeÄŤnĂ© a nesmĂ­ ÄŤĂ­st porty z neĂşplnĂ©ho nebo nenultĂ©ho fragmentu.

## Struktura ĹˇestnĂˇcti sub-bucketĹŻ

KaĹľdĂˇ odchozĂ­ peer fronta obsahuje prĂˇvÄ› 16 sub-bucketĹŻ. KaĹľdĂ˝ sub-bucket mĂˇ:

- FIFO posloupnost paketĹŻ doplnÄ›nĂ˝ch o jejich klasifikaÄŤnĂ­ klĂ­ÄŤ,
- volitelnĂ©ho vĂ˝hradnĂ­ho vlastnĂ­ka, kterĂ˝m je jeden flow klĂ­ÄŤ,
- poÄŤet uloĹľenĂ˝ch bajtĹŻ,
- DRR deficit v bajtech,
- metadata nutnĂˇ pro bezpeÄŤnĂ© vrĂˇcenĂ­ paketu a zmÄ›nu vlastnictvĂ­.

Peer fronta udrĹľuje omezenĂ˝ adresĂˇĹ™ vlastnickĂ˝ch vazeb `flow key -> sub-bucket`. V adresĂˇĹ™i jsou pouze aktuĂˇlnĂ­ vĂ˝hradnĂ­ vlastnĂ­ci, nejvĂ˝Ĺˇe 16 poloĹľek. PĹ™etokovĂ© flow klĂ­ÄŤe nepotĹ™ebujĂ­ neomezenou stavovou mapu.

## ZaĹ™azenĂ­ odchozĂ­ho paketu

ZaĹ™azenĂ­ paketu probÄ›hne atomicky pod stejnou synchronizacĂ­, kterĂˇ chrĂˇnĂ­ peer frontu a jejĂ­ ĂşÄŤetnictvĂ­:

1. Paket se bezpeÄŤnÄ› klasifikuje na flow klĂ­ÄŤ.
2. Pokud klĂ­ÄŤ vlastnĂ­ sub-bucket, paket se vloĹľĂ­ na konec tohoto sub-bucketu.
3. Pokud klĂ­ÄŤ sub-bucket nevlastnĂ­ a existuje volnĂ˝ sub-bucket, klĂ­ÄŤ jej zĂ­skĂˇ jako vĂ˝hradnĂ­ vlastnĂ­k a paket se vloĹľĂ­ do nÄ›j.
4. Pokud nenĂ­ ĹľĂˇdnĂ˝ volnĂ˝ sub-bucket, z celĂ©ho flow klĂ­ÄŤe se vypoÄŤĂ­tĂˇ kvalitnĂ­, procesnÄ› stabilnĂ­ a nĂˇhodnÄ› klĂ­ÄŤovanĂ˝ hash. PoslednĂ­ ÄŤtyĹ™i rovnomÄ›rnÄ› rozloĹľenĂ© bity zvolĂ­ jeden ze 16 sub-bucketĹŻ.
5. VĹˇechny pakety stejnĂ©ho pĹ™etokovĂ©ho toku musĂ­ pĹ™i nezmÄ›nÄ›nĂ©m procesnĂ­m klĂ­ÄŤi zvolit stejnĂ˝ sub-bucket. JednotlivĂ© pakety se nikdy nerozmisĹĄujĂ­ nĂˇhodnÄ›.

HashovacĂ­ klĂ­ÄŤ vznikne pĹ™i startu klienta, nemusĂ­ pĹ™eĹľĂ­t restart a nesmĂ­ bĂ˝t ovlivnitelnĂ˝ vzdĂˇlenĂ˝m protÄ›jĹˇkem. Implementace nesmĂ­ pĹ™edpoklĂˇdat, Ĺľe nĂ­zkĂ© ÄŤtyĹ™i bity nekvalitnĂ­ho obecnĂ©ho hashe jsou rovnomÄ›rnĂ©.

## Ĺ˝ivotnĂ­ cyklus vlastnictvĂ­

- Jakmile se sub-bucket vyprĂˇzdnĂ­, jeho vlastnickĂˇ vazba se odstranĂ­, deficit se resetuje a sub-bucket se stane volnĂ˝m.
- Pokud v neprĂˇzdnĂ©m sub-bucketu nezĹŻstĂˇvĂˇ ĹľĂˇdnĂ˝ paket souÄŤasnĂ©ho vlastnĂ­ka, jeho vazba se odstranĂ­.
- PrvnĂ­ zbĂ˝vajĂ­cĂ­ paket mĹŻĹľe urÄŤit novĂ©ho vĂ˝hradnĂ­ho vlastnĂ­ka sub-bucketu, pokud jeho flow klĂ­ÄŤ jiĹľ nevlastnĂ­ jinĂ˝ sub-bucket. AdresĂˇĹ™ a hlaviÄŤka sub-bucketu se aktualizujĂ­ atomicky.
- Pokud flow klĂ­ÄŤ prvnĂ­ho paketu jiĹľ vlastnĂ­ jinĂ˝ sub-bucket, nesmĂ­ vzniknout duplicitnĂ­ vlastnictvĂ­. Implementace musĂ­ bezpeÄŤnÄ› pokraÄŤovat bez povĂ˝ĹˇenĂ­, pĹ™Ă­padnÄ› zvolit prvnĂ­ zpĹŻsobilĂ˝ klĂ­ÄŤ podle jednoznaÄŤnÄ› zdokumentovanĂ©ho pravidla.
- Intent vyĹľaduje okamĹľitĂ© uvolnÄ›nĂ­ po vyprĂˇzdnÄ›nĂ­, nikoli ÄŤasovĂ˝ idle timeout.
- SamotnĂˇ pĹ™Ă­tomnost pĹ™etokovĂ˝ch paketĹŻ ve frontÄ› neznamenĂˇ, Ĺľe je sub-bucket volnĂ˝.

## Deficit Round Robin

- UvnitĹ™ kaĹľdĂ© peer fronty plĂˇnovat neprĂˇzdnĂ© sub-buckety pomocĂ­ Deficit Round Robin.
- ĂšÄŤetnĂ­ jednotkou jsou bajty paketu, nikoli poÄŤet paketĹŻ.
- KaĹľdĂ˝ aktivnĂ­ sub-bucket dostĂˇvĂˇ stejnĂ© kvantum za kolo.
- VĂ˝chozĂ­ kvantum odvodit od aktuĂˇlnĂ­ maximĂˇlnĂ­ velikosti tunelovĂ©ho paketu tak, aby sub-bucket s bÄ›ĹľnĂ˝m plnĂ˝m paketem mohl pĹ™i dostateÄŤnĂ©m rozpoÄŤtu odeslat pĹ™ibliĹľnÄ› jeden takovĂ˝ paket za kolo.
- NevyuĹľitĂ˝ deficit se pĹ™enĂˇĹˇĂ­ do dalĹˇĂ­ch kol, aby nebyly vyhladovÄ›ny vÄ›tĹˇĂ­ pakety.
- Po vyprĂˇzdnÄ›nĂ­ sub-bucketu deficit resetovat.
- Zachovat FIFO poĹ™adĂ­ uvnitĹ™ kaĹľdĂ©ho sub-bucketu.
- Peer-level rotace zĹŻstĂˇvĂˇ nad touto vrstvou. Ĺ˝ĂˇdnĂˇ peer fronta ani aktivnĂ­ sub-bucket nesmĂ­ bĂ˝t trvale vyhladovÄ›n.

## SestavenĂ­ poĹľadavku a nĂˇvrat paketĹŻ

- VĂ˝bÄ›r paketĹŻ musĂ­ stĂˇle respektovat pĹ™esnĂ˝ limit tÄ›la poĹľadavku, maximĂˇlnĂ­ poÄŤet datovĂ˝ch zĂˇznamĹŻ a chovĂˇnĂ­ pro paket, kterĂ˝ se nevejde ani do jinak prĂˇzdnĂ©ho platnĂ©ho poĹľadavku.
- Jeden datovĂ˝ zĂˇznam mĹŻĹľe nadĂˇle obsahovat vĂ­ce paketĹŻ stejnĂ© protistrany, i kdyĹľ pochĂˇzejĂ­ z rĹŻznĂ˝ch sub-bucketĹŻ.
- PlĂˇn poĹľadavku musĂ­ uchovat dostatek ĂşdajĹŻ k vrĂˇcenĂ­ kaĹľdĂ©ho paketu do pĹŻvodnĂ­ho peeru a pĹŻvodnĂ­ho sub-bucketu.
- PĹ™i selhĂˇnĂ­ nebo neovÄ›Ĺ™enĂ©m pĹ™ijetĂ­ poĹľadavku se vĹˇechny pakety vrĂˇtĂ­ tak, aby se zachovalo jejich pĹŻvodnĂ­ poĹ™adĂ­ a DRR stav nebyl poĹˇkozen dvojĂ­m ĂşÄŤtovĂˇnĂ­m.
- ĂšspÄ›ĹˇnÄ› pĹ™ijatĂ© pakety se nevracejĂ­.

## Limity a ĂşÄŤetnictvĂ­

- Zachovat souÄŤasnĂ˝ globĂˇlnĂ­ limit odchozĂ­ fronty, minimĂˇlnĂ­ rezervaci na peer a maximĂˇlnĂ­ poÄŤet peerĹŻ.
- Ĺ estnĂˇct sub-bucketĹŻ nevytvĂˇĹ™Ă­ ĹˇestnĂˇct novĂ˝ch minimĂˇlnĂ­ch bajtovĂ˝ch rezervacĂ­. SdĂ­lejĂ­ bajtovĂ˝ rozpoÄŤet svĂ© peer fronty.
- SouÄŤet bajtĹŻ a poloĹľek na peer i globĂˇlnĂ­ Ăşrovni musĂ­ zĹŻstat pĹ™esnĂ˝ pĹ™i vloĹľenĂ­, odebrĂˇnĂ­, vrĂˇcenĂ­, zahozenĂ­ i zmÄ›nÄ› vlastnĂ­ka.
- PĹ™i pĹ™ekroÄŤenĂ­ existujĂ­cĂ­ch limitĹŻ zachovat souÄŤasnĂ© dĹŻvody zahozenĂ­, pokud zmÄ›na nevyĹľaduje novĂ˝ pĹ™esnÄ› zdokumentovanĂ˝ dĹŻvod.

## Pozorovatelnost

Doplnit pouze takovou diagnostiku, kterĂˇ je potĹ™ebnĂˇ pro ovÄ›Ĺ™enĂ­ chovĂˇnĂ­, minimĂˇlnÄ› moĹľnost v testech zjistit:

- poÄŤet aktivnĂ­ch sub-bucketĹŻ,
- vlastnickĂ© a sdĂ­lenĂ© pĹ™iĹ™azenĂ­,
- prĹŻbÄ›h DRR vĂ˝bÄ›ru,
- zachovĂˇnĂ­ bajtovĂ©ho ĂşÄŤetnictvĂ­.

ProdukÄŤnĂ­ logovĂˇnĂ­ nesmĂ­ vypisovat kaĹľdĂ˝ paket ani nekontrolovanÄ› zvyĹˇovat objem logĹŻ.

## TestovacĂ­ a akceptaÄŤnĂ­ kritĂ©ria

1. PrvnĂ­ch nejvĂ˝Ĺˇe 16 souÄŤasnÄ› aktivnĂ­ch odchozĂ­ch pÄ›tic jednĂ© protistrany zĂ­skĂˇ pĹ™i dostupnĂ˝ch volnĂ˝ch sub-bucketech samostatnĂ© vlastnictvĂ­.
2. SedmnĂˇctĂˇ a dalĹˇĂ­ pÄ›tice se stabilnÄ› mapujĂ­ hashem do jednoho ze 16 sub-bucketĹŻ. OpakovanĂ© pakety stejnĂ© pÄ›tice se nerozptĂ˝lĂ­ mezi vĂ­ce sub-bucketĹŻ.
3. HashovanĂ© pĹ™etokovĂ© toky se rozklĂˇdajĂ­ mezi vĹˇech 16 sub-bucketĹŻ a nejsou soustĹ™edÄ›ny v jedinĂ© pĹ™etokovĂ© frontÄ›.
4. PrĂˇzdnĂ˝ sub-bucket okamĹľitÄ› ztratĂ­ vlastnĂ­ka a mĹŻĹľe jej zĂ­skat novĂˇ pÄ›tice.
5. Pokud zmizĂ­ vĹˇechny pakety vlastnĂ­ka, ale zĹŻstanou cizĂ­ pakety, vlastnictvĂ­ lze bezpeÄŤnÄ› pĹ™evĂ©st na zpĹŻsobilou pÄ›tici prvnĂ­ho zbĂ˝vajĂ­cĂ­ho paketu bez duplicitnĂ­ho vlastnictvĂ­.
6. DlouhĂ˝ objemnĂ˝ tok nedokĂˇĹľe trvale vyhladovÄ›t sporadickĂ˝ tok stejnĂ© protistrany.
7. Toky s rĹŻznĂ˝mi velikostmi paketĹŻ zĂ­skĂˇvajĂ­ fĂ©rovĂ˝ podĂ­l podle bajtĹŻ, nikoli podle poÄŤtu paketĹŻ.
8. FIFO poĹ™adĂ­ paketĹŻ v jednom sub-bucketu zĹŻstane zachovĂˇno, vÄŤetnÄ› nĂˇvratu po neĂşspÄ›ĹˇnĂ©m sĂ­ĹĄovĂ©m poĹľadavku.
9. Protokoly bez portĹŻ a fragmentovanĂ© IPv4 pakety jsou pĹ™ijaty a pouĹľijĂ­ definovanĂ˝ fallback klĂ­ÄŤ bez nebezpeÄŤnĂ©ho ÄŤtenĂ­ hlaviÄŤky.
10. StĂˇvajĂ­cĂ­ peer-level fĂ©rovost, limity tÄ›la poĹľadavku, maximĂˇlnĂ­ poÄŤet zĂˇznamĹŻ, drop ĂşÄŤetnictvĂ­ a globĂˇlnĂ­ i per-peer bajtovĂ© limity zĹŻstanou funkÄŤnĂ­.
11. NovĂ© jednotkovĂ© testy pokryjĂ­ alokaci vĹˇech slotĹŻ, hashovou kolizi, uvolnÄ›nĂ­, povĂ˝ĹˇenĂ­ vlastnĂ­ka, DRR deficit, rĹŻznÄ› velkĂ© pakety, oversized paket, nĂˇvrat po chybÄ› a soubÄ›ĹľnĂ© zaĹ™azovĂˇnĂ­.
12. StĂˇvajĂ­cĂ­ testy paketovĂ˝ch front, scheduleru, sĂ­ĹĄovĂ©ho workeru a integraÄŤnĂ­ pĹ™enosovĂ© testy projdou beze zmÄ›ny oÄŤekĂˇvanĂ©ho drĂˇtovĂ©ho formĂˇtu.

## RealizaÄŤnĂ­ vodĂ­tka

- Preferovat rozĹˇĂ­Ĺ™enĂ­ stĂˇvajĂ­cĂ­ch abstrakcĂ­ `DirectionQueues`, `PeerQueue`, `RequestScheduler` a `RequestPlan` pĹ™ed vytvoĹ™enĂ­m paralelnĂ­ho systĂ©mu front.
- OddÄ›lit parsovĂˇnĂ­ flow klĂ­ÄŤe od plĂˇnovĂˇnĂ­, aby Ĺˇlo klasifikaci samostatnÄ› testovat.
- HashovacĂ­ funkci a procesnĂ­ klĂ­ÄŤ zapouzdĹ™it za testovatelnĂ˝m rozhranĂ­m s moĹľnostĂ­ deterministickĂ©ho klĂ­ÄŤe v testech.
- Zachovat jednoho konzumenta odchozĂ­ fronty, ale nepĹ™edpoklĂˇdat, Ĺľe producenti jsou jedinĂ­ nebo nesoubÄ›ĹľnĂ­.
- NedÄ›lat zmÄ›ny serverovĂ©ho drĂˇtovĂ©ho protokolu, pokud testy neodhalĂ­ nevyhnutelnou potĹ™ebu. TakovĂˇ potĹ™eba by znamenala novou materiĂˇlnĂ­ zmÄ›nu intentu.

**AI:** První úplná verze intentu je vytvořená a ověřená společně se zdrojovým přepisem. Chceš pokračovat vytvořením realizačního plánu, nebo přímou realizací?

**User:** Ještě se chci na pár věcí zeptat. Zajímá mě, jestli se zvýšily nároky na paměť klienta.

**AI:** Ano, nároky se zvýší, ale ne šestnáctkrát. Paketová data zůstanou pod stejným konfigurovaným limitem. Přibudou struktury šestnácti front, flow klíče, deficity a vlastnická mapa. Samotná metadata sub-bucketů představují jednotky KiB na aktivní protistranu. Ukládání celé přibližně 16B pětice ke každému paketu by ale přidalo asi jedno procento u 1500B paketů, 25 procent u 64B paketů a výrazně více u minimálních IPv4 paketů. Doporučením je pětici podle potřeby obnovovat z existující IP hlavičky nebo použít kompaktní metadata, aby se paketová data nekopírovala. Tuto optimalizaci v001 výslovně nevyžadoval.

**User:** Když máme šestnáct sub-bucketů, maximální limit například 16 MB, jedna aplikace pošle v jednom slotu 2 MB a maximální odchozí request je 8 MB, jak to dopadne?

**AI:** Limit se sdílí mezi všemi sub-buckety a není automaticky rozdělen rovným dílem. Dva megabajty se přijmou, pokud je ve společné frontě místo. Je-li aktivní jen tento tok, work-conserving DRR mu dovolí využít dostupný request a obsah se pravděpodobně vejde do jednoho requestu, přičemž rozhoduje zakódovaná velikost těla. Při dalších aktivních tocích se data skládají po bajtových kolech. DRR řeší férovost odesílání, nikoli obsazování paměti; bez další rezervace by jeden tok mohl zaplnit celý limit.

**User:** Takže 64 MB je na frontu jako celek a při šestnácti sub-bucketech to znamená 4 MB na jednu sub-frontu?

**AI:** Ne. Výchozích 64 MiB je společný limit celé odchozí struktury přes všechny protistrany, ne jedné peer fronty. Šestnáct sub-bucketů není pevné rovnoměrné rozdělení. Čtyři MiB na sub-bucket by platily pouze při tvrdých kvótách. Bez nich může jeden sub-bucket využít více, pokud ostatní prostor nepotřebují a zůstanou zachovány peer rezervace.

**User:** Co znamenají měkké kvóty? Rozveď to konkrétně.

**AI:** Měkká kvóta dává sub-bucketu garantované minimum, ale dovoluje mu půjčit si volnou společnou kapacitu. V příkladu s 64MiB rozpočtem jedné peer fronty a 1MiB rezervací na každý sub-bucket se logicky rezervuje 16 MiB a zbývajících 48 MiB je společných. Jediný tok může využít vlastní minimum i společný fond, ale další tok má stále okamžitě dostupnou svou rezervaci. Nejde o předem alokovanou RAM. Kontrola nejvýše šestnácti položek je konstantní a levná.

**User:** Jakou konkrétní garantovanou velikost doporučuješ?

**AI:** Doporučená výchozí hodnota je 8 KiB na sub-bucket, odvozená jako současných 128 KiB minimální peer rezervace dělených šestnácti. To odpovídá přibližně pěti běžným 1500B paketům. Není to maximální velikost, pouze logická rezerva pro přijetí krátkého burstu. Hodnota by neměla být nezávislým parametrem, ale měla by se odvozovat z peer rezervace.

**User:** Byl by problém použít 16 KiB?

**AI:** Technicky je to možné, ale šestnáct krát 16 KiB zvyšuje logickou peer rezervaci ze 128 na 256 KiB. Při globálním limitu 64 MiB to podporuje přibližně 256 vzdálených protistran; pro 512 protistran by byl nutný nejméně 128MiB globální limit. Nejde o okamžitou fyzickou alokaci, ale o menší půjčitelnou kapacitu.

**User:** Připomeň mi rezervaci jednoho klasického bucketu bez sub-bucketů.

**AI:** Současná výchozí minimální logická rezervace jednoho peer bucketu je 128 KiB. Není to maximální velikost ani předem alokovaná paměť.

**User:** Souhlasím s 8 KiB na rezervaci, kterou jsi navrhla.

**AI:** Osm KiB na každý ze šestnácti sub-bucketů je potvrzené. Součet zůstává 128 KiB na klasický peer bucket, takže se nemění současná globální rezervační matematika. Jde o materiální doplnění hotového intentu a bude vytvořena nová úplná verze.

**User:** Zpracuj intent.

