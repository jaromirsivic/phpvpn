---
intent_version: 2
created_at: 2026-09-13-02-23-01
based_on: intent-v001.md
source: intent-chat-v002.md
---

# Diagnostika HTTP requestu a response v konfiguraÄŤnĂ­m wizardu

## ShrnutĂ­

RozĹˇĂ­Ĺ™it prvnĂ­ dva kroky konfiguraÄŤnĂ­ho wizardu klienta o diagnostiku konkrĂ©tnĂ­ HTTP vĂ˝mÄ›ny, jejĂ­Ĺľ pĹ™ijatĂˇ odpovÄ›ÄŹ zpĹŻsobila chybu. PĹ™i takovĂ©m selhĂˇnĂ­ se v patiÄŤce wizardu objevĂ­ ÄŤervenĂ© tlaÄŤĂ­tko `Error detail`. TlaÄŤĂ­tko otevĹ™e samostatnĂ© okno s taby `Request` a `Response`, kterĂ© zobrazĂ­ Ăşplnou aplikaÄŤnĂ­ podobu poĹľadavku a odpovÄ›di pĹ™ed protokolovĂ˝m parsovĂˇnĂ­m.

CĂ­lem je umoĹľnit rychlĂ© odhalenĂ­ odpovÄ›dĂ­ pozmÄ›nÄ›nĂ˝ch webhostingem, napĹ™Ă­klad pĹ™idanĂ˝m HTML nebo JavaScriptem, bez nutnosti externĂ­ho sĂ­ĹĄovĂ©ho ladÄ›nĂ­.

## Kontext projektu

- Realizace probÄ›hne pĹ™Ă­mo v aktuĂˇlnĂ­ feature vÄ›tvi `tray_UX`.
- Pro tuto realizaci se nesmĂ­ vytvĂˇĹ™et ani pouĹľĂ­vat worktree.
- SĂ­ĹĄovou komunikaci wizardu vlastnĂ­ sluĹľba. Tray aplikace ovlĂˇdĂˇ wizard pĹ™es lokĂˇlnĂ­ Wizard IPC, takĹľe zmÄ›na pravdÄ›podobnÄ› zasĂˇhne transportnĂ­ diagnostiku, sluĹľbu, Wizard IPC, model a Win32 UI tray aplikace.
- PrvnĂ­ krok `Server URL` provĂˇdĂ­ URL probe pĹ™es `health`.
- DruhĂ˝ krok `Server secret` provĂˇdĂ­ ovÄ›Ĺ™enĂ˝ `health` a nĂˇslednÄ› `info`.
- ExistujĂ­cĂ­ Wizard operation pĹ™edĂˇvĂˇ do UI bezpeÄŤnou chybu a HTTP status, nikoli ĂşplnĂ˝ request a response.
- ReferenÄŤnĂ­ vzhled tabĹŻ je souÄŤasnĂ© okno `Show Server Info`, realizovanĂ© pĹ™es tab control a sdĂ­lenĂ© UI barvy.
- RelevantnĂ­ dokumentace je v koĹ™enovĂ˝ch souborech `client.md` a `protocol.md`.

## PoĹľadovanĂ© chovĂˇnĂ­

### 1. Rozsah

Funkce je dostupnĂˇ pouze pĹ™i selhĂˇnĂ­ ovÄ›Ĺ™enĂ­ v tÄ›chto krocĂ­ch:

1. `Server URL`
2. `Server secret`

Nevztahuje se na dalĹˇĂ­ kroky wizardu, bÄ›ĹľnĂ© VPN poĹľadavky, `Show Server Info` ani jinĂ© diagnostickĂ© akce.

### 2. PodmĂ­nka zobrazenĂ­ tlaÄŤĂ­tka

TlaÄŤĂ­tko `Error detail` se zobrazĂ­ jen tehdy, kdyĹľ:

- sluĹľba skuteÄŤnÄ› provedla HTTP vĂ˝mÄ›nu,
- pro konkrĂ©tnĂ­ chybujĂ­cĂ­ HTTP volĂˇnĂ­ pĹ™ijala odpovÄ›ÄŹ,
- zpracovĂˇnĂ­ tĂ©to pĹ™ijatĂ© odpovÄ›di skonÄŤilo chybou, napĹ™Ă­klad kvĹŻli chybnĂ©mu statusu, neplatnĂ©mu formĂˇtu, neoÄŤekĂˇvanĂ©mu HTML ÄŤi JavaScriptu, chybnĂ© korelaci nebo kryptografickĂ©mu ovÄ›Ĺ™enĂ­.

TlaÄŤĂ­tko se nezobrazĂ­ pĹ™i:

- lokĂˇlnĂ­ validaÄŤnĂ­ chybÄ› pĹ™ed odeslĂˇnĂ­m,
- selhĂˇnĂ­ DNS, TCP nebo TLS pĹ™ed pĹ™ijetĂ­m HTTP odpovÄ›di,
- timeoutu nebo pĹ™eruĹˇenĂ­ bez pĹ™ijatĂ© odpovÄ›di,
- chybÄ› IPC nebo nedostupnosti sluĹľby,
- zruĹˇenĂ­ operace bez pĹ™ijatĂ© odpovÄ›di.

Diagnostika musĂ­ patĹ™it k aktuĂˇlnĂ­mu neĂşspÄ›ĹˇnĂ©mu pokusu. PĹ™i zahĂˇjenĂ­ novĂ©ho ovÄ›Ĺ™enĂ­, zmÄ›nÄ› kroku, resetu nebo ukonÄŤenĂ­ relace se starĂ˝ detail odstranĂ­ a nesmĂ­ se vydĂˇvat za vĂ˝sledek novĂ©ho pokusu.

### 3. Volba konkrĂ©tnĂ­ vĂ˝mÄ›ny

Pokud krok provĂˇdĂ­ vĂ­ce HTTP volĂˇnĂ­, uloĹľĂ­ a zobrazĂ­ se pouze vĂ˝mÄ›na, jejĂ­Ĺľ pĹ™ijatĂˇ odpovÄ›ÄŹ zpĹŻsobila vĂ˝slednou chybu.

- Pokud v kroku `Server secret` selĹľe zpracovĂˇnĂ­ odpovÄ›di `health`, zobrazĂ­ se `health` request a response.
- Pokud `health` uspÄ›je a selĹľe zpracovĂˇnĂ­ pĹ™ijatĂ© odpovÄ›di `info`, zobrazĂ­ se `info` request a response.
- ĂšspÄ›ĹˇnĂˇ pĹ™edchozĂ­ vĂ˝mÄ›na se nesmĂ­ zobrazit jako detail pozdÄ›jĹˇĂ­ chyby bez odpovÄ›di.

### 4. TlaÄŤĂ­tko v patiÄŤce wizardu

- Text tlaÄŤĂ­tka je pĹ™esnÄ› `Error detail`.
- TlaÄŤĂ­tko pouĹľĂ­vĂˇ ÄŤervenĂ˝ chybovĂ˝ vizuĂˇlnĂ­ styl konzistentnĂ­ se souÄŤasnĂ˝m design systĂ©mem tray aplikace.
- Je viditelnĂ© pouze pĹ™i splnÄ›nĂ­ podmĂ­nky vĂ˝Ĺˇe.
- V prvnĂ­m kroku je bezprostĹ™ednÄ› vlevo od `Verify and continue` v pravĂ© ÄŤĂˇsti patiÄŤky.
- Ve druhĂ©m kroku je bezprostĹ™ednÄ› vpravo od `Back`.
- Mezi sousednĂ­mi tlaÄŤĂ­tky zĹŻstane standardnĂ­ drobnĂˇ mezera. TlaÄŤĂ­tka se nesmĂ­ vizuĂˇlnÄ› dotĂ˝kat.
- Layout musĂ­ zachovat stĂˇvajĂ­cĂ­ responzivnĂ­ chovĂˇnĂ­ a tab order wizardu pĹ™i bÄ›ĹľnĂ˝ch i kompaktnĂ­ch rozmÄ›rech.

### 5. Okno `Error detail`

KliknutĂ­ otevĹ™e samostatnĂ© okno vlastnÄ›nĂ© wizardem.

- NahoĹ™e jsou dva taby s pĹ™esnĂ˝mi nĂˇzvy `Request` a `Response`.
- Vzhled tabĹŻ a jejich barvy odpovĂ­dajĂ­ tabĹŻm v `Show Server Info`.
- KaĹľdĂ˝ tab obsahuje vĂ­ceĹ™ĂˇdkovĂ˝ read-only textbox.
- AktivnĂ­ textbox vyuĹľije prakticky celĂ˝ dostupnĂ˝ obsah okna.
- Okno nemĂˇ patiÄŤku ani tlaÄŤĂ­tka `OK`, `Cancel` nebo obdobnĂ© akce.
- UĹľivatel okno zavĹ™e standardnĂ­m kĹ™Ă­Ĺľkem v titulku.
- PĹ™epĂ­nĂˇnĂ­ tabĹŻ nesmĂ­ mÄ›nit zachycenĂˇ data.
- Obsah musĂ­ bĂ˝t ÄŤitelnĂ˝, vybĂ­ratelnĂ˝ a kopĂ­rovatelnĂ˝. Read-only reĹľim nesmĂ­ brĂˇnit vĂ˝bÄ›ru textu ani standardnĂ­mu kopĂ­rovĂˇnĂ­.

### 6. Obsah tabu `Request`

ZobrazĂ­ vÄ›rnou aplikaÄŤnĂ­ rekonstrukci konkrĂ©tnĂ­ho poĹľadavku pĹ™edanĂ©ho transportu:

- HTTP metoda,
- cĂ­lovĂˇ URL vÄŤetnÄ› akce,
- vĹˇechny hlaviÄŤky, kterĂ© aplikace pĹ™edala systĂ©mu,
- dostupnĂ© hlaviÄŤky doplnÄ›nĂ© nebo potvrzenĂ© HTTP vrstvou systĂ©mu, pokud je lze spolehlivÄ› zĂ­skat,
- prĂˇzdnĂ˝ Ĺ™Ăˇdek oddÄ›lujĂ­cĂ­ hlaviÄŤky a tÄ›lo,
- nezmÄ›nÄ›nĂ© tÄ›lo poĹľadavku.

Obsah se nesmĂ­ pĹ™eparsovat, zkrĂˇĹˇlovat, normalizovat ani nahrazovat jiĹľ deĹˇifrovanou ÄŤi jinak odvozenou reprezentacĂ­. Nejde o zachycenĂ­ TLS ciphertextu, paketĹŻ nebo internĂ­ch HTTP/2 rĂˇmcĹŻ.

### 7. Obsah tabu `Response`

ZobrazĂ­ vÄ›rnou aplikaÄŤnĂ­ podobu odpovÄ›di vrĂˇcenĂ© HTTP vrstvou pĹ™ed protokolovĂ˝m parsovĂˇnĂ­m:

- HTTP verzi a status line, pokud je transport poskytuje, jinak jednoznaÄŤnÄ› rekonstruovanĂ˝ status,
- vĹˇechny dostupnĂ© pĹ™ijatĂ© response headers,
- prĂˇzdnĂ˝ Ĺ™Ăˇdek oddÄ›lujĂ­cĂ­ hlaviÄŤky a tÄ›lo,
- nezmÄ›nÄ›nĂ© pĹ™ijatĂ© tÄ›lo odpovÄ›di.

PrĂˇvÄ› tato vrstva musĂ­ zachytit a ukĂˇzat i HTML, JavaScript nebo jinĂ˝ text pĹ™idanĂ˝ hostingem. Obsah se nesmĂ­ nahradit pouze bezpeÄŤnou chybovou zprĂˇvou nebo parsovanĂ˝m JSON objektem.

### 8. PĹ™enos mezi sluĹľbou a tray aplikacĂ­

- SluĹľba zachytĂ­ diagnostiku pouze pro aktuĂˇlnĂ­ Wizard probe v prvnĂ­ch dvou krocĂ­ch.
- Diagnostika se pĹ™ipojĂ­ pouze k terminĂˇlnĂ­mu neĂşspÄ›ĹˇnĂ©mu vĂ˝sledku, kterĂ˝ splĹuje podmĂ­nku pĹ™ijatĂ© odpovÄ›di.
- Wizard IPC pĹ™enese request a response jako volitelnĂˇ data svĂˇzanĂˇ s identitou operace a pokusu.
- Tray model musĂ­ zachovat existujĂ­cĂ­ ochrany proti zastaralĂ˝m odpovÄ›dĂ­m, pĹ™eruĹˇenĂ˝m relacĂ­m a nesprĂˇvnĂ© identitÄ› pokusu.
- DiagnostickĂˇ data se nesmĂ­ zapisovat do logu, konfiguraÄŤnĂ­ho souboru ani jinĂ©ho perzistentnĂ­ho ĂşloĹľiĹˇtÄ›.
- Data majĂ­ Ĺľivotnost pouze po dobu relevantnĂ­ho neĂşspÄ›ĹˇnĂ©ho pokusu a otevĹ™enĂ©ho wizardu.
- ExistujĂ­cĂ­ limit IPC zprĂˇvy musĂ­ bĂ˝t respektovĂˇn. NĂˇvrh musĂ­ pĹ™ed implementacĂ­ ovÄ›Ĺ™it, Ĺľe maximĂˇlnĂ­ transportnĂ­ odpovÄ›ÄŹ a jejĂ­ JSON escaping nemohou pĹ™ekroÄŤit IPC limit. Pokud je nutnĂˇ zmÄ›na limitu nebo samostatnĂ© naÄŤtenĂ­ detailu, musĂ­ bĂ˝t navrĹľena explicitnÄ› a pokryta testy, bez tichĂ©ho oĹ™ezu diagnostiky.

## BezpeÄŤnostnĂ­ a technickĂ© hranice

- PoĹľadavek je zobrazit kompletnĂ­ aplikaÄŤnĂ­ request a response. UĹľivatelskĂ© rozhodnutĂ­ vĂ˝slovnÄ› nepĹ™edpoklĂˇdĂˇ redakci tÄ›chto dat v tomto diagnostickĂ©m oknÄ›.
- Diagnostika je dostupnĂˇ jen po explicitnĂ­m kliknutĂ­ v lokĂˇlnĂ­m tray UI.
- Obsah requestu a response se nesmĂ­ automaticky logovat, telemetricky odesĂ­lat ani uklĂˇdat.
- ZachycenĂ­ nesmĂ­ mÄ›nit odesĂ­lanĂ© bajty, poĹ™adĂ­ sĂ­ĹĄovĂ˝ch operacĂ­, autentizaci, timeouty, retry pravidla ani kryptografickĂ© ovÄ›Ĺ™ovĂˇnĂ­.
- ChybnĂˇ nebo nedĹŻvÄ›ryhodnĂˇ response je zobrazovanĂ˝ text, nikoli instrukce k provedenĂ­. UI ji nesmĂ­ interpretovat jako HTML ani spouĹˇtÄ›t JavaScript.
- Textbox musĂ­ zobrazovat data jako prostĂ˝ text.

## Dokumentace

Aktualizovat nejmĂ©nÄ›:

- `client.md`, zejmĂ©na popis Wizard probe, Wizard IPC, Ĺľivotnosti diagnostickĂ˝ch dat, UI chovĂˇnĂ­ a limitĹŻ,
- `protocol.md`, zejmĂ©na vztah diagnostiky k HTTP requestu a response pĹ™ed protokolovĂ˝m parsovĂˇnĂ­m a skuteÄŤnost, Ĺľe se nemÄ›nĂ­ wire protokol mezi klientem a `phpvpn.php`.

Dokumentace musĂ­ jasnÄ› rozliĹˇit:

- serverovĂ˝ HTTP protokol, kterĂ˝ se tĂ­mto zĂˇmÄ›rem nemÄ›nĂ­,
- lokĂˇlnĂ­ Wizard IPC mezi sluĹľbou a tray aplikacĂ­, kterĂ˝ se rozĹˇiĹ™uje,
- aplikaÄŤnĂ­ rekonstrukci HTTP zprĂˇvy oproti paketovĂ©mu nebo TLS zĂˇznamu.

## Testy

### AutomatickĂ© testy

PĹ™idat nebo rozĹˇĂ­Ĺ™it testy alespoĹ pro:

- zachycenĂ­ requestu a response pĹ™ed parsovĂˇnĂ­m,
- pĹ™enos volitelnĂ© diagnostiky pĹ™es Wizard IPC a jeho striktnĂ­ schema,
- vazbu diagnostiky na sprĂˇvnou operaci, attempt ID a sequence,
- krok `Server URL` s pĹ™ijatou syntakticky nebo kryptograficky chybnou odpovÄ›dĂ­,
- krok `Server secret`, kde selĹľe `health`,
- krok `Server secret`, kde `health` uspÄ›je a selĹľe `info`,
- chybnĂ˝ HTTP status s pĹ™ijatou odpovÄ›dĂ­,
- nezobrazenĂ­ tlaÄŤĂ­tka pĹ™i lokĂˇlnĂ­ validaci, DNS/TLS chybÄ›, timeoutu bez response, IPC chybÄ› a zruĹˇenĂ­,
- odstranÄ›nĂ­ starĂ©ho detailu pĹ™i novĂ©m pokusu a odmĂ­tnutĂ­ zastaralĂ©ho vĂ˝sledku,
- sprĂˇvnou viditelnost, text, ÄŤervenĂ˝ styl, poĹ™adĂ­ a mezery tlaÄŤĂ­tek v obou krocĂ­ch,
- responzivnĂ­ layout a tab order,
- dialog se dvÄ›ma taby, read-only editory, pĹ™epĂ­nĂˇnĂ­m obsahu a zavĹ™enĂ­m kĹ™Ă­Ĺľkem,
- zobrazenĂ­ vloĹľenĂ©ho HTML a JavaScriptu jako inertnĂ­ho prostĂ©ho textu,
- hraniÄŤnĂ­ velikosti diagnostiky a IPC zprĂˇvy bez tichĂ©ho oĹ™ezu.

VyuĹľĂ­t odpovĂ­dajĂ­cĂ­ existujĂ­cĂ­ vrstvy testĹŻ, zejmĂ©na unit testy transportu, ProbeClientu, WizardService, Wizard IPC, WizardModelu a layoutu, plus tray UI testy.

### LokĂˇlnĂ­ integraÄŤnĂ­ ovÄ›Ĺ™enĂ­

Po automatickĂ˝ch testech provĂ©st lokĂˇlnĂ­ ovÄ›Ĺ™enĂ­ pĹ™es HTTP s Nginx instalacĂ­ v `D:\nginx` a testovacĂ­m `phpvpn.php`, pokud je v uvedenĂ©m prostĹ™edĂ­ skuteÄŤnÄ› dostupnĂ˝. TestovacĂ­ server doÄŤasnÄ› upravit nebo nakonfigurovat tak, aby do response pĹ™idal text, HTML nebo JavaScript a vyvolal chybu klientskĂ©ho zpracovĂˇnĂ­.

OvÄ›Ĺ™it, Ĺľe:

- wizard zobrazĂ­ bÄ›Ĺľnou chybu i tlaÄŤĂ­tko `Error detail`,
- `Request` obsahuje sprĂˇvnĂ˝ konkrĂ©tnĂ­ poĹľadavek,
- `Response` obsahuje pĹ™esnÄ› diagnosticky relevantnĂ­ pozmÄ›nÄ›nou odpovÄ›ÄŹ,
- vloĹľenĂ˝ obsah je pouze text a nic se nespustĂ­,
- novĂ© ovÄ›Ĺ™enĂ­ starĂ˝ detail odstranĂ­.

DoÄŤasnĂ© zmÄ›ny mimo repozitĂˇĹ™ po testu bezpeÄŤnÄ› vrĂˇtit do pĹŻvodnĂ­ho stavu. PĹ™ed Ăşpravou ovÄ›Ĺ™it pĹ™esnĂ˝ cĂ­l a zachovat pĹŻvodnĂ­ obsah tak, aby byl obnovitelnĂ˝.

### ExternĂ­ best-effort ovÄ›Ĺ™enĂ­

AĹľ po ĂşspÄ›ĹˇnĂ˝ch lokĂˇlnĂ­ch testech provĂ©st best-effort ovÄ›Ĺ™enĂ­ proti:

[https://phpvpn.freehosting.dev/phpvpn.php](https://phpvpn.freehosting.dev/phpvpn.php)

Toto ovÄ›Ĺ™enĂ­ nenĂ­ povinnou podmĂ­nkou dokonÄŤenĂ­, protoĹľe cizĂ­ hosting mĹŻĹľe bĂ˝t nedostupnĂ˝, ukonÄŤovat TLS spojenĂ­ nebo zmÄ›nit svĂ© chovĂˇnĂ­. VĂ˝sledek zaznamenat jako `PASS`, `FAIL` nebo `NOT RUN` s konkrĂ©tnĂ­m dĹŻvodem. Z tohoto prostĹ™edĂ­ server bÄ›hem pĹ™Ă­pravy intentu ukonÄŤil TLS spojenĂ­ pĹ™ed HTTP odpovÄ›dĂ­, takĹľe vloĹľenĂ˝ obsah nebyl potvrzen.

## AkceptaÄŤnĂ­ kritĂ©ria

1. PĹ™i chybÄ› zpracovĂˇnĂ­ skuteÄŤnÄ› pĹ™ijatĂ© HTTP odpovÄ›di v kroku `Server URL` nebo `Server secret` je viditelnĂ© ÄŤervenĂ© tlaÄŤĂ­tko `Error detail` na potvrzenĂ©m mĂ­stÄ› s bÄ›Ĺľnou mezerou.
2. Bez pĹ™ijatĂ© odpovÄ›di se tlaÄŤĂ­tko nezobrazĂ­.
3. Dialog obsahuje pouze taby `Request` a `Response` s read-only vĂ­ceĹ™ĂˇdkovĂ˝mi textovĂ˝mi poli pĹ™es dostupnĂ˝ obsah a zavĂ­rĂˇ se kĹ™Ă­Ĺľkem.
4. Request a response odpovĂ­dajĂ­ konkrĂ©tnĂ­mu chybujĂ­cĂ­mu HTTP volĂˇnĂ­ a zachovĂˇvajĂ­ aplikaÄŤnĂ­ obsah pĹ™ed parsovĂˇnĂ­m.
5. U vĂ­cekrokovĂ©ho ovÄ›Ĺ™enĂ­ se nezobrazĂ­ pĹ™edchozĂ­ ĂşspÄ›ĹˇnĂˇ vĂ˝mÄ›na jako detail jinĂ© chyby.
6. NovĂ˝ pokus, zmÄ›na kroku, reset nebo ukonÄŤenĂ­ relace odstranĂ­ zastaralĂ˝ detail.
7. DiagnostickĂˇ data nejsou interpretovĂˇna, logovĂˇna ani perzistentnÄ› uklĂˇdĂˇna.
8. StĂˇvajĂ­cĂ­ sĂ­ĹĄovĂ˝ protokol, kryptografie, timeouty, retry pravidla a bezpeÄŤnost ĹľivotnĂ­ho cyklu operacĂ­ zĹŻstanou funkÄŤnÄ› beze zmÄ›ny.
9. AutomatickĂ© testy a lokĂˇlnĂ­ integraÄŤnĂ­ test s pozmÄ›nÄ›nou odpovÄ›dĂ­ projdou.
10. `client.md` a `protocol.md` odpovĂ­dajĂ­ implementovanĂ©mu chovĂˇnĂ­.
11. Realizace probÄ›hne pĹ™Ă­mo na vÄ›tvi `tray_UX` bez worktree.
12. ExternĂ­ free-hosting test probÄ›hne aĹľ jako poslednĂ­ best-effort kontrola a jeho nedostupnost sama neblokuje dokonÄŤenĂ­.

## RealizaÄŤnĂ­ vodĂ­tka

- ZachycenĂ­ Ĺ™eĹˇit v transportnĂ­ nebo ProbeClient vrstvÄ› tak, aby byl k dispozici request i raw aplikaÄŤnĂ­ response jeĹˇtÄ› pĹ™ed tĂ­m, neĹľ parser vrĂˇtĂ­ chybu.
- RozĹˇĂ­Ĺ™it vĂ˝sledek Wizard operation a jeho IPC serializaci pouze o volitelnĂ˝ diagnostickĂ˝ objekt. NepĹ™idĂˇvat diagnostiku do bÄ›ĹľnĂ© bezpeÄŤnĂ© chyby pouĹľĂ­vanĂ© jinde.
- Zachovat striktnĂ­ IPC validaci a existujĂ­cĂ­ korelaci `operationId`, `clientAttemptId`, `attemptSequence` a `connectionEpoch`.
- V tray UI znovu pouĹľĂ­t existujĂ­cĂ­ tab control, barvy, modernĂ­ textbox a layout helpery namĂ­sto paralelnĂ­ho vizuĂˇlnĂ­ho systĂ©mu.
- PĹ™ed realizacĂ­ rozhodnout transportnĂ­ reprezentaci response headers a velikostnĂ­ strategii tak, aby splnila Ăşplnost bez tichĂ©ho oĹ™ezu a souÄŤasnÄ› neporuĹˇila IPC limit.
- NeprovĂˇdÄ›t zmÄ›nu serverovĂ©ho `phpvpn.php` jako souÄŤĂˇst produktovĂ© funkce. Jeho lokĂˇlnĂ­ Ăşprava je pouze doÄŤasnĂ˝ testovacĂ­ prostĹ™edek.
