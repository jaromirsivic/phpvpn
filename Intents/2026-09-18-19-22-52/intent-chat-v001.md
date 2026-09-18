---
intent_version: 1
created_at: 2026-09-18-19-52-40
produces: intent-v001.md
---

# Zdrojová konverzace

Poznámka: Hlasový přepis obsahoval průběžně opakované a překrývající se úseky. Níže jsou zachyceny všechny věcné uživatelské a AI příspěvky v chronologickém pořadí, přičemž pouze technické duplikáty vzniklé překryvem přepisu jsou sloučeny.

**User:** Ahoj, chci vytvořit nový intent. To znamená záměr na úpravu systému.

**AI:** Nový intent jsem založila a zachytávání je aktivní. Pokračuj v diktování. Kdykoli můžeš říct „pozastavit“, „pokračovat“ nebo „zpracovat intent“.

**User:** Aktuálně v systému na straně klienta dostáváme IP pakety do jednotlivých bucketů a už teď je tam buffrování podle klientské IP adresy. Děje se to před tím, než spočítáme hash z přijatých bufferů, zakódujeme je a předáme VPN serveru. Když se klient připojí k druhému klientovi prostřednictvím zabezpečeného spojení a jedna aplikace, například FTP, zcela vytíží paketovou komunikaci, ostatní aplikace se stanou nepoužitelné nebo se k nim pakety vůbec nedostanou. Potřebuji jednotlivé buckety dále rozčlenit tak, že každý bude mít šestnáct sub-bucketů.

**AI:** Detail o šestnácti dynamicky přidělovaných sub-bucketech mám zachycený v aktivním intentu.

**User:** V rámci každého bucketu bude dalších šestnáct sub-bucketů, tedy podbufferů, které se budou dynamicky přidělovat jednotlivým službám, pokud budou všechny využity. Asi je rozdělíme klasickou pěticí: zdrojová IP adresa, cílová IP adresa a protokol. Co tam je dál?

**AI:** Během zachytávání obsah nerozebírám. Chceš-li teď doplnit chybějící položky té pětice, řekni „pozastavit intent“ a odpovím ti.

**User:** Teď zastav zaznamenávání a diskutuj se mnou.

**AI:** Zachytávání jsem pozastavila. Dosavadní obsah zůstává součástí intentu a teď můžeme návrh normálně prodiskutovat. Klasická síťová pětice je zdrojová IP adresa, cílová IP adresa, zdrojový port, cílový port a transportní protokol. U protokolů bez portů bude potřeba určit náhradní klíč nebo je zařadit do společného sub-bucketu.

**User:** Přesně tak. Chci dál brainstormovat. Když běží hodně aplikací, chci uvnitř komunikace z jedné IP adresy na druhou sub-bucketový systém, aby jedna aplikace na nějakém portu neutlačovala jinou. Je to dobrý způsob, nebo máš lepší?

**AI:** Lepší než pouhé hashování do šestnácti front je fair queuing nad jednotlivými toky. Pětice je správný identifikátor toku, ale plánovač musí aktivně střídat neprázdné fronty a omezit, kolik dat z jedné odešle v jednom kole. Doporučila bych dvouúrovňové férové řazení: současný bucket pro cílového klienta ponechat a uvnitř dynamicky vytvářet fronty podle pětice. Ty obsluhovat algoritmem Deficit Round Robin podle počtu bajtů. Pevných šestnáct hashovaných front poskytuje pouze přibližnou férovost, protože kolidující toky se mohou znovu blokovat. Pětice zajišťuje férovost mezi spojeními, ne spolehlivě mezi aplikacemi.

**User:** Popiš mi fair queuing.

**AI:** Každý komunikační tok má vlastní frontu a plánovač jim pravidelně střídá prostor k odesílání. Deficit Round Robin přidává každé frontě v každém kole kredit v bajtech. Fronta může odesílat, dokud má dost kreditu, a nevyužitý kredit si převádí. Objemný přenos tak využije kapacitu, když je sám, ale po příchodu malého interaktivního toku jej nemůže vyhladovět. Uvnitř jednoho toku se zachová pořadí paketů.

**User:** To se mi líbí. Kdyby bylo více než šestnáct spojení, udělal bych patnáct samostatných front a šestnáctou vyhrazenou pro všechny ostatní. Je to v pořádku?

**AI:** Je to použitelné, pokud je více než patnáct toků výjimečné, ale všechny přetokové toky by společně dostaly podíl jediné fronty a znovu se blokovaly. Lepší je mít šestnáct skutečných flow front a další toky stabilně rozdělit mezi všechny. Pokud by zůstalo řešení patnáct plus jedna, sloty potřebují uvolňování, aby staré toky nezůstaly zvýhodněné.

**User:** Co kdybychom zkombinovali oba přístupy? Budeme mít šestnáct zpočátku volných front. První unikátní pětice obsadí první frontu, druhá pětice druhou a tak dále. Když jsou všechny obsazené a dorazí sedmnáctá pětice, vypočte se hash pětice a jeho poslední čtyři bity určí frontu. Je to lepší?

**AI:** Ano. Jde o rozumný hybrid mezi přesným fair queuingem a omezenou pamětí. Nová pětice získá volnou frontu, existující vazba zůstává stabilní a po vyčerpání front se další pětice stabilně přiřazují hashem. Mezi šestnácti frontami poběží Deficit Round Robin podle bajtů. Férovost bude přesná mezi frontami a přibližná uvnitř sdílené fronty. Hash musí kvalitně promíchat všechna pole a kvůli úmyslným kolizím by měl být náhodně klíčovaný při startu klienta.

**User:** Drobná úprava: když bude všech šestnáct front zablokovaných, sedmnáctý a každý další paket se uloží do náhodné fronty. Pokud je fronta prázdná, její alokace pětici se smaže. Pokud jsou ve frontě jen pakety jiných pětic než pětice v hlavičce fronty, fronta se alokuje pětici prvního paketu, který v ní je. Co si o tom myslíš?

**AI:** Automatické přebírání uvolněné fronty je dobré, ale náhodné přiřazení každého paketu by mohlo změnit pořadí paketů jednoho toku, zejména u TCP. Doporučené chování je: vlastník používá svou frontu, nová pětice získá volnou frontu, po obsazení všech front se použije stabilní hash celé pětice, prázdná fronta se uvolní a fronta bez paketů původního vlastníka může atomicky povýšit pětici svého prvního zbývajícího paketu. Před povýšením je potřeba ověřit, že daná pětice nevlastní jinou frontu.

**User:** Náhodné rozmisťování beru zpět, vracíme se k hashi. Jinak se mi návrh velice líbí.

**AI:** Přetokové pětice se tedy budou stabilně přiřazovat pomocí hashe, nikoli náhodně po jednotlivých paketech. Dynamické vlastnictví sub-bucketů a jejich přebírání zůstává podle posledního návrhu.

**User:** Vytvoř intent. Chtěl jsem říct: zpracuj intent.

**AI:** Pro jednoznačné zpracování zbývá určit směr provozu, zacházení s fragmenty a protokoly bez portů a DRR kvantum. Výchozí návrh je pouze odchozí směr, společný fallback podle adres a protokolu a kvantum odvozené od maximální velikosti tunelového paketu.

**User:** Co je DRR kvantum?

**AI:** Je to příděl v bajtech pro každou frontu v jednom kole. Fronta si jej nasčítá, odešle pakety, dokud má kredit, a nespotřebovaný zbytek si nechá na příště, aby dostaly šanci i větší pakety.

**User:** Souhlasím s výchozím návrhem.
