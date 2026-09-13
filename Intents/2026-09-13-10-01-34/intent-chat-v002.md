---
intent_version: 2
created_at: 2026-09-13-10-29-25
based_on: intent-v001.md
produces: intent-v002.md
---

# ZĂˇznam konverzace k intentu

AI:

NovĂ˝ intent jsem zaloĹľila a zachytĂˇvĂˇnĂ­ je aktivnĂ­. MĹŻĹľeĹˇ zaÄŤĂ­t diktovat. Kdykoli mĹŻĹľeĹˇ Ĺ™Ă­ct â€žpozastavâ€ś, â€žpokraÄŤujâ€ś nebo â€žzpracuj intentâ€ś.

User:

Chci ti teÄŹ Ĺ™Ă­ct nÄ›jakĂ˝ novĂ˝ intent, zaloĹľ novĂ˝ intent.

User:

Chci upravit klientskou aplikaci, pravdÄ›podobnÄ› sluĹľbu i tray aplikaci. MoĹľnĂˇ tray aplikaci vlastnÄ› ani ne, ale hlavnÄ› sluĹľbu a to v mĂ­stÄ›, kde se edituje konfigurace. KonfiguraÄŤnĂ­ JSON je potĹ™eba rozĹˇĂ­Ĺ™it o novou poloĹľku. NejdĹ™Ă­v jsem uvaĹľoval, Ĺľe bude volitelnĂˇ, ale mĂˇ bĂ˝t povinnĂˇ a pĹ™i vytvĂˇĹ™enĂ­ konfiguraÄŤnĂ­ho JSONu se do nÄ›j pĹ™idĂˇ automaticky. PoloĹľka se bude jmenovat httpHeaders, malĂ© http, velkĂ© H a zbytek malĂ˝mi pĂ­smeny.

PoloĹľka bude obsahovat HTTP hlaviÄŤky, kterĂ© pĹ™ijdou do vĹˇech poĹľadavkĹŻ na PHPVPN, tedy do health, info a data. DĹŻvodem je moĹľnost simulovat typickĂ˝ webovĂ˝ prohlĂ­ĹľeÄŤ nebo pĹ™idat napĹ™Ă­klad cookies.

AI:

RozumĂ­m, pokraÄŤuj prosĂ­m.

User:

VĂ˝chozĂ­ hodnota hlaviÄŤek:

~~~http
Cache-Control: no-cache
Pragma: no-cache
User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:155.0) Gecko/20100101 Firefox/155.0
~~~

AI:

Zaznamenala jsem to. PokraÄŤuj.

User:

HlaviÄŤek mĹŻĹľe bĂ˝t vĂ­ce a hodnota je vĂ­ceĹ™ĂˇdkovĂˇ, takĹľe v JSONu musĂ­ bĂ˝t sprĂˇvnÄ› escapovanĂˇ.

AI:

Zaznamenala jsem to. PokraÄŤuj.

User:

Tohle je vĹˇechno, zpracuj intent.

AI:

ZachycenĂ­ je uzavĹ™enĂ©. MĂˇm celĂ˝ viditelnĂ˝ prĹŻbÄ›h vÄŤetnÄ› vĂ˝chozĂ­ch HTTP hlaviÄŤek a ovÄ›Ĺ™uji dotÄŤenĂˇ mĂ­sta konfigurace i sĂ­ĹĄovĂ˝ch poĹľadavkĹŻ, abych mohla intent pĹ™esnÄ› formalizovat.

User:

OÄŤekĂˇval bych, Ĺľe ty hlaviÄŤky dĂˇme do masterSetup hned pod serverUrl.

AI:

UmĂ­stÄ›nĂ­ je teÄŹ jednoznaÄŤnĂ©. PovinnĂ© httpHeaders bude v masterSetup hned za serverUrl.

User:

MoĹľnĂˇ bych v masterSetup pĹ™esklĂˇdal celĂ© vĂ˝chozĂ­ poĹ™adĂ­. PrvnĂ­ poloĹľka bude serverSecret, pod nĂ­ serverUrl, pod nĂ­ httpHeaders, potom trustAnyTlsCertificate a pak uĹľ poĹ™adĂ­, jakĂ© mĂˇme.

User:

Tohle je pouze vĂ˝chozĂ­ poĹ™adĂ­ pĹ™i prvnĂ­ instalaci sluĹľby nebo pĹ™i resetu konfiguraÄŤnĂ­ho JSONu. KdyĹľ uĹľivatel poloĹľky pĹ™euspoĹ™ĂˇdĂˇ, aplikace to musĂ­ umÄ›t zpracovat a na poĹ™adĂ­ nesmĂ­ trvat.

AI:

PotĹ™ebuji potvrdit jednu vÄ›c. Pod poĹľadavkem na ÄŤas myslĂ­Ĺˇ stĂˇvajĂ­cĂ­ health poĹľadavek, pĹ™es kterĂ˝ se synchronizuje ÄŤas? Pokud neodpovĂ­Ĺˇ, vezmu jako vĂ˝chozĂ­ odpovÄ›ÄŹ ano.

User:

Ano, myslĂ­m tĂ­m health poĹľadavek, pĹ™es kterĂ˝ se synchronizuje ÄŤas. OvÄ›Ĺ™ ve zdrojovĂ©m kĂłdu, z ÄŤeho se ÄŤerpĂˇ ÄŤas, podle kterĂ©ho se potom synchronizuje delta.

AI:

ZdrojovĂ˝ kĂłd potvrzuje, Ĺľe ovÄ›Ĺ™enĂˇ odpovÄ›ÄŹ health obsahuje autentizovanĂ˝ serverovĂ˝ timestamp. Z nÄ›j se vytvoĹ™Ă­ ÄŤasovĂ˝ snapshot s dobou odeslĂˇnĂ­ a pĹ™ijetĂ­. NĂˇslednĂ© requesty odvozujĂ­ aktuĂˇlnĂ­ serverovĂ˝ ÄŤas pomocĂ­ monotĂłnnĂ­ch hodin a bezpeÄŤnostnĂ­ rezervy. HlaviÄŤky proto musĂ­ bĂ˝t i na health poĹľadavku, kterĂ˝ tento stav zaklĂˇdĂˇ a obnovuje.

User:

SamozĹ™ejmÄ› platĂ­, Ĺľe na uvedenĂ©m poĹ™adĂ­ nesmĂ­me trvat. Je to pouze nĂˇmi navrĹľenĂ© vĂ˝chozĂ­ poĹ™adĂ­ pĹ™i resetu konfigurace nebo pĹ™i prvnĂ­ instalaci sluĹľby, kdy se konfigurace vytvĂˇĹ™Ă­. KdyĹľ uĹľivatel poloĹľky pĹ™euspoĹ™ĂˇdĂˇ, musĂ­me se s tĂ­m umÄ›t vypoĹ™Ăˇdat.

AI:

Byl vytvořen intent v001. Jeho plný obsah jako AI vytvořený artefakt:

    ---
    intent_version: 1
    created_at: 2026-09-13-10-16-36
    source: intent-chat-v001.md
    ---
    
    # PovinnĂ© vlastnĂ­ HTTP hlaviÄŤky v konfiguraci klienta
    
    ## ShrnutĂ­
    
    RozĹˇĂ­Ĺ™it klientskou konfiguraci o povinnou poloĹľku masterSetup.httpHeaders. Hodnota bude Ĺ™etÄ›zec obsahujĂ­cĂ­ jeden nebo vĂ­ce Ĺ™ĂˇdkĹŻ HTTP hlaviÄŤek. DekĂłdovanĂ© hlaviÄŤky se pĹ™idajĂ­ ke kaĹľdĂ©mu HTTP poĹľadavku klienta na PHPVPN, tedy k akcĂ­m health, info a data. Health zahrnuje takĂ© zĂ­skĂˇnĂ­ ovÄ›Ĺ™enĂ©ho serverovĂ©ho ÄŤasu pro synchronizaci.
    
    VĂ˝chozĂ­ konfigurace vytvoĹ™enĂˇ pĹ™i prvnĂ­ instalaci sluĹľby nebo pĹ™i resetu musĂ­ obsahovat pĹ™edepsanĂ© tĹ™i hlaviÄŤky a novĂ© vĂ˝chozĂ­ poĹ™adĂ­ poloĹľek v masterSetup. PoĹ™adĂ­ je pouze prezentaÄŤnĂ­ vlastnost vĂ˝chozĂ­ho dokumentu. Parser a bÄ›h aplikace musĂ­ zĹŻstat na poĹ™adĂ­ JSON ÄŤlenĹŻ nezĂˇvislĂ©.
    
    ## OvÄ›Ĺ™enĂ˝ kontext projektu
    
    - SdĂ­lenou kompletnĂ­ vĂ˝chozĂ­ konfiguraci pro start sluĹľby a explicitnĂ­ vytvoĹ™enĂ­ nebo reset editoru definuje kDefaultConfigDocument v client/src/core/config/config.h:35-54.
    - Validace konfigurace kontroluje povolenĂ© ÄŤleny masterSetup a vytvĂˇĹ™Ă­ ConfigValues v client/src/core/config/config_validator.cpp:119-318.
    - PĹ™enĂˇĹˇenĂ© hodnoty konfigurace jsou v ConfigValues v client/src/core/config/config_document.h:36-47.
    - Runtime pĹ™edĂˇvĂˇ sĂ­ĹĄovĂ© nastavenĂ­ transportu v NetworkWorker::configure v client/src/core/runtime/network_worker.cpp:33-63.
    - Wizard pouĹľĂ­vĂˇ vlastnĂ­ konfiguraci transportu v ProbeClient::prepare v client/src/core/wizard/probe_client.cpp:11-26.
    - WinHTTP transport pĹ™ijĂ­mĂˇ TransportSettings z client/src/platform/iface/transport.h:26-32, otevĂ­rĂˇ request v client/src/platform/windows/transport_winhttp.cpp:164-194 a nynĂ­ odesĂ­lĂˇ pouze aplikaÄŤnĂ­ Content-Type v client/src/platform/windows/transport_winhttp.cpp:243-264.
    - Diagnostika rekonstruuje aplikaÄŤnĂ­ a systĂ©movĂ© request hlaviÄŤky v client/src/core/wizard/http_diagnostic.cpp:23-48.
    - Tray editor pracuje s celĂ˝m JSON dokumentem a pĹ™i uloĹľenĂ­ jej odesĂ­lĂˇ sluĹľbÄ› v client/apps/tray/tray_application.cpp:321-326. FormĂˇtovaÄŤ zachovĂˇvĂˇ poĹ™adĂ­ ÄŤlenĹŻ a escapovĂˇnĂ­ Ĺ™etÄ›zcĹŻ v client/apps/tray/editor_window.cpp:20-58.
    - NetworkWorker::performHealth v client/src/core/runtime/network_worker.cpp:82-127 ovÄ›Ĺ™Ă­ health odpovÄ›ÄŹ, vezme jejĂ­ timestampMs, vytvoĹ™Ă­ TimeSnapshot a publikuje jej do TimeSync.
    - TimeSync::requestTimestampMs v client/src/core/runtime/time_sync.cpp:56-76 projektuje ovÄ›Ĺ™enĂ˝ serverovĂ˝ ÄŤas podle monotĂłnnÄ› uplynulĂ© doby a odeÄŤĂ­tĂˇ bezpeÄŤnostnĂ­ rezervu.
    
    ## PoĹľadovanĂ© zmÄ›ny
    
    ### SchĂ©ma a vĂ˝chozĂ­ dokument
    
    1. PĹ™idat do objektu masterSetup povinnĂ˝ ÄŤlen pĹ™esnÄ› pojmenovanĂ˝ httpHeaders.
    2. Hodnota httpHeaders je JSON string. Po dekĂłdovĂˇnĂ­ pĹ™edstavuje vĂ­ceĹ™ĂˇdkovĂ˝ blok HTTP hlaviÄŤek.
    3. VĂ˝chozĂ­ dokument musĂ­ pouĹľĂ­t toto poĹ™adĂ­ prvnĂ­ch ÄŤlenĹŻ masterSetup:
       1. serverSecret
       2. serverUrl
       3. httpHeaders
       4. trustAnyTlsCertificate
       5. zbĂ˝vajĂ­cĂ­ dosavadnĂ­ ÄŤleny v jejich dosavadnĂ­m relativnĂ­m poĹ™adĂ­
    4. VĂ˝chozĂ­ hodnota musĂ­ po JSON dekĂłdovĂˇnĂ­ odpovĂ­dat tomuto bloku:
    
    ~~~http
    Cache-Control: no-cache
    Pragma: no-cache
    User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:155.0) Gecko/20100101 Firefox/155.0
    ~~~
    
    5. Ve fyzickĂ©m JSON textu musĂ­ bĂ˝t oddÄ›lovaÄŤe Ĺ™ĂˇdkĹŻ escapovanĂ©. VĂ˝chozĂ­ serializace mĂˇ pouĹľĂ­t \r\n, takĹľe napĹ™Ă­klad hodnota bude mĂ­t tvar:
    
    ~~~json
    "httpHeaders": "Cache-Control: no-cache\r\nPragma: no-cache\r\nUser-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:155.0) Gecko/20100101 Firefox/155.0"
    ~~~
    
    6. PovinnĂ© je jmĂ©no a pĹ™Ă­tomnost ÄŤlenu, nikoli jeho pozice. PlatnĂ˝ dokument s libovolnÄ› pĹ™euspoĹ™ĂˇdanĂ˝mi ÄŤleny musĂ­ bĂ˝t pĹ™ijat a musĂ­ mĂ­t stejnĂ˝ vĂ˝znam.
    7. VĂ˝chozĂ­ dokument se pouĹľije pĹ™i prvnĂ­m vytvoĹ™enĂ­ konfigurace sluĹľbou i pĹ™i resetu nebo explicitnĂ­m vytvoĹ™enĂ­ vĂ˝chozĂ­ konfigurace.
    
    ### DatovĂ˝ tok konfigurace
    
    1. RozĹˇĂ­Ĺ™it validaci znĂˇmĂ˝ch ÄŤlenĹŻ masterSetup a naÄŤĂ­st httpHeaders jako povinnĂ˝ string.
    2. Uchovat dekĂłdovanou hodnotu v konfiguraÄŤnĂ­m modelu a pĹ™edat ji do vĹˇech transportnĂ­ch instancĂ­.
    3. Zahrnout ji jak do bÄ›ĹľnĂ©ho runtime transportu sluĹľby, tak do transportu pouĹľĂ­vanĂ©ho wizardem a jeho ovÄ›Ĺ™ovacĂ­mi poĹľadavky.
    4. Operace wizardu, kterĂ© upravujĂ­ jinĂ© konfiguraÄŤnĂ­ poloĹľky, musĂ­ httpHeaders zachovat.
    5. NenĂ­ poĹľadovĂˇno novĂ© samostatnĂ© formulĂˇĹ™ovĂ© pole v tray aplikaci. Tray a editor se majĂ­ zmÄ›nit pouze tam, kde je to nutnĂ© pro sprĂˇvnĂ© vytvoĹ™enĂ­, zobrazenĂ­, Ăşpravu, zachovĂˇnĂ­ a uloĹľenĂ­ celĂ©ho JSON dokumentu.
    
    ### OdesĂ­lĂˇnĂ­ poĹľadavkĹŻ
    
    1. PĹ™idat dekĂłdovanĂ˝ blok httpHeaders ke kaĹľdĂ©mu requestu pro HttpAction::health, HttpAction::info a HttpAction::data.
    2. To zahrnuje poÄŤĂˇteÄŤnĂ­ health, pravidelnĂ© nebo obnovovacĂ­ health poĹľadavky, info, bÄ›ĹľnĂ˝ datovĂ˝ polling i diagnostickĂ© a wizard probe varianty tÄ›chto poĹľadavkĹŻ.
    3. Zachovat transportem Ĺ™Ă­zenĂ˝ Content-Type pro jednotlivĂ© akce a sprĂˇvnĂ© HTTP rĂˇmovĂˇnĂ­ requestu.
    4. HlaviÄŤky nakonfigurovanĂ© uĹľivatelem musĂ­ umoĹľnit bÄ›ĹľnĂ© pĹ™Ă­pady jako Cookie, User-Agent, Cache-Control a Pragma.
    5. DiagnostickĂ˝ vĂ˝pis requestu musĂ­ obsahovat skuteÄŤnÄ› pouĹľitĂ˝ nebo korektnÄ› rekonstruovanĂ˝ blok aplikaÄŤnĂ­ch hlaviÄŤek vÄŤetnÄ› httpHeaders.
    
    ## OmezenĂ­ a realizaÄŤnĂ­ vodĂ­tka
    
    - ZmÄ›na se tĂ˝kĂˇ klienta. ServerovĂ˝ PHP endpoint se kvĹŻli nĂ­ nemÄ›nĂ­.
    - HlaviÄŤky jsou konfiguraÄŤnĂ­ data. NesmÄ›jĂ­ mÄ›nit HTTP metodu, cĂ­lovou URL, tÄ›lo ani oddÄ›lenĂ­ hlaviÄŤek od tÄ›la.
    - Validace musĂ­ odmĂ­tnout nesprĂˇvnĂ˝ JSON typ a syntakticky nebezpeÄŤnĂ˝ nebo neplatnĂ˝ hlaviÄŤkovĂ˝ blok tak, aby nebylo moĹľnĂ© vloĹľit dalĹˇĂ­ request line nebo tÄ›lo. KonkrĂ©tnĂ­ bezpeÄŤnĂ© slouÄŤenĂ­ s hlaviÄŤkami vlastnÄ›nĂ˝mi WinHTTP mĂˇ respektovat stĂˇvajĂ­cĂ­ transportnĂ­ kontrakt.
    - Implementace nesmĂ­ odvozovat vĂ˝znam ÄŤlenĹŻ JSON objektu z jejich poĹ™adĂ­.
    - ZmÄ›na mĂˇ zachovat stĂˇvajĂ­cĂ­ diagnostiku, ÄŤasovĂ© limity, zruĹˇenĂ­ requestu, TLS chovĂˇnĂ­ a ĂşÄŤtovĂˇnĂ­ pĹ™enesenĂ˝ch dat.
    
    ## PravdÄ›podobnÄ› dotÄŤenĂ© oblasti
    
    - client/src/core/config/config.h
    - client/src/core/config/config_document.h
    - client/src/core/config/config_validator.cpp
    - client/src/core/config/config_document.cpp
    - client/src/core/runtime/network_worker.cpp
    - client/src/core/wizard/probe_client.cpp
    - client/src/platform/iface/transport.h
    - client/src/platform/windows/transport_winhttp.cpp
    - souvisejĂ­cĂ­ unit, contract, wizard, runtime a WinHTTP testy
    - pĹ™Ă­padnÄ› tray editor a diagnostickĂ© zobrazenĂ­, pokud testy odhalĂ­ nutnou adaptaci
    
    ## AkceptaÄŤnĂ­ kritĂ©ria
    
    1. NovÄ› vytvoĹ™enĂˇ nebo resetovanĂˇ konfigurace obsahuje povinnĂ© masterSetup.httpHeaders s pĹ™esnĂ˝mi tĹ™emi vĂ˝chozĂ­mi hlaviÄŤkami.
    2. VĂ˝chozĂ­ poĹ™adĂ­ v masterSetup je serverSecret, serverUrl, httpHeaders, trustAnyTlsCertificate a potĂ© dosavadnĂ­ zbĂ˝vajĂ­cĂ­ ÄŤleny.
    3. StejnĂˇ konfigurace s jinĂ˝m poĹ™adĂ­m ÄŤlenĹŻ se validuje a funguje identicky.
    4. ChybÄ›jĂ­cĂ­ httpHeaders zpĹŻsobĂ­ jednoznaÄŤnou validaÄŤnĂ­ chybu povinnĂ© poloĹľky.
    5. JinĂ˝ JSON typ neĹľ string nebo neplatnĂ˝ hlaviÄŤkovĂ˝ blok je odmĂ­tnut jednoznaÄŤnou validaÄŤnĂ­ chybou.
    6. Na server dorazĂ­ nakonfigurovanĂ© hlaviÄŤky pĹ™i health, info i data requestu.
    7. Health request s vlastnĂ­mi hlaviÄŤkami nadĂˇle ovÄ›Ĺ™Ă­ serverovĂ˝ timestamp a publikuje ÄŤasovĂ˝ snapshot. NĂˇslednĂˇ tvorba ÄŤasovĂ˝ch ĂşdajĹŻ pro autentizovanĂ© requesty zĹŻstane funkÄŤnĂ­.
    8. Wizard a diagnostickĂ© poĹľadavky pouĹľĂ­vajĂ­ stejnĂ© vlastnĂ­ hlaviÄŤky jako runtime a diagnostika je umĂ­ zobrazit.
    9. Ăšprava a uloĹľenĂ­ konfigurace pĹ™es existujĂ­cĂ­ editor zachovĂˇ httpHeaders i uĹľivatelskĂ© poĹ™adĂ­ ÄŤlenĹŻ.
    10. AutomatickĂ© testy pokryjĂ­ vĂ˝chozĂ­ dokument, povinnost a typ poloĹľky, nezĂˇvislost na poĹ™adĂ­, vĂ­ce hlaviÄŤek a jejich escapovĂˇnĂ­, pĹ™enos ve vĹˇech tĹ™ech akcĂ­ch, wizard probe a diagnostickou rekonstrukci.

User:

Ještě přímo intent změň, protože jsme zatím nevytvářeli plán. Současné requesty mají Content-Type, User-Agent, Connection a Content-Length. Implicitní User-Agent z nynějšího requestu odstraň, protože User-Agent si uživatel nastaví přes httpHeaders. Ve výchozí konfiguraci mu User-Agent stále předvyplníme a uživatel ho může změnit.

Je pořadí Content-Type, User-Agent, Connection a Content-Length nějak povinné?

AI:

Ne. Podle RFC 9110 není pořadí hlaviček s různými názvy významové ani povinné. Význam může mít pořadí opakovaných řádků se stejným názvem. Ve zdrojovém kódu aplikace explicitně přidává Content-Type, identifikace relace WinHTTP vytváří User-Agent PHPVPN/1.0 a Connection s Content-Length doplňuje síťová vrstva. V nové verzi se odstraní pouze implicitní PHPVPN User-Agent. Uživatelsky měnitelný User-Agent z httpHeaders zůstane.

User:

Ponecháme Content-Type, Connection a Content-Length a připojíme k nim to, co si uživatel nadefinoval v httpHeaders. Když se nějaká hlavička opakuje, kolize neřešíme. Je to chyba uživatele. Prostě logicky sloučíme to, co se generuje nyní, s novými hlavičkami.

Chvíli jsem uvažoval o přejmenování položky, ale rozmyslel jsem si to. Název se nemění.

AI:

Název zůstává httpHeaders. Do nové verze zapisuji logické připojení uživatelského bloku k existujícím hlavičkám bez řešení kolizí, odstranění pouze implicitního PHPVPN/1.0 User-Agentu a žádný požadavek na konkrétní pořadí na drátu, protože WinHTTP jej může serializovat po svém.

User:

Přesně tak a pořád se to bude jmenovat httpHeaders, takže nemusíš nic přejmenovávat.
