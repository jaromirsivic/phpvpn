# Nefunkční požadavky - struktura detailního designu

Bodový seznam kapitol, které má obsahovat nefunkční část specifikace. U každé kapitoly je stručně uvedeno, co se v ní má popsat.

## 1. Rámec a metodika

- **Rozsah a kontext.** Vymezení hranic systému, jeho okolí a integračních partnerů. Explicitně se uvede, co do nefunkční části nepatří.
- **Předpoklady a omezení.** Předpoklady, na kterých požadavky stojí, a pevná omezení, která nelze změnit, například existující infrastruktura nebo legislativa.
- **Katalog nefunkčních požadavků.** Každý požadavek má identifikátor, kategorii, metriku, cílovou a mezní hodnotu, prioritu, vlastníka a způsob ověření. Bez měřitelné hodnoty se požadavek nepovažuje za dokončený.
- **Referenční rámec kvality.** Uvede se použitý model, typicky ISO/IEC 25010 nebo FURPS+, aby bylo prokazatelné, že žádná kategorie kvality nebyla vynechána.

## 2. Kvalita za běhu

- **Výkon.** Doba odezvy podle typu operace včetně percentilů P50, P95 a P99, propustnost a chování při špičkovém zatížení. Uvedou se i dávková okna a limity dlouhotrvajících operací.
- **Kapacita a škálovatelnost.** Počet uživatelů, souběžných session, objem dat, roční přírůstek a horizont plánování. Popíše se vertikální i horizontální škálování a případný autoscaling.
- **Dostupnost.** Cílová úroveň dostupnosti, provozní doba, RTO a RPO a způsob výpočtu dostupnosti za dané období. Rozliší se plánované a neplánované odstávky.
- **Spolehlivost a odolnost.** Chování při selhání závislostí, degradovaný režim, timeouty, opakování požadavků, idempotence a circuit breaker. Uvedou se cíle MTBF a MTTR a požadavek na konzistenci dat po havárii.
- **Zálohování a obnova.** Předmět zálohování, frekvence, doba uchování, umístění záloh a postup obnovy. Součástí je požadavek na pravidelné testování obnovy a plán obnovy po havárii.
- **Bezpečnost.** Model hrozeb, autentizace a autorizace, kryptografie při přenosu i v uložení, správa klíčů a tajemství, hardening a správa zranitelností. Popíše se i ochrana proti typickým zranitelnostem podle OWASP a bezpečnost integračních rozhraní.
- **Auditní stopa.** Seznam auditovaných událostí, obsah záznamu, neměnnost a ochrana proti manipulaci. Definuje se doba uchování a kdo má k auditu přístup.
- **Ochrana osobních údajů a data governance.** Klasifikace dat, minimalizace, pseudonymizace, retenční a skartační lhůty a geografické umístění dat. Doplní se podpora práva na výmaz a export dat.
- **Compliance a normy.** Legislativní a normativní požadavky, například GDPR, NIS2, ISO 27001, případně DORA nebo PCI DSS. Uvedou se i interní bezpečnostní politiky zákazníka.

## 3. Kvalita z pohledu uživatele

- **Použitelnost a UX standardy.** Design system, konzistence obrazovek, chování chybových stavů a čitelnost chybových zpráv. Stanoví se limity typu maximální počet kroků u klíčových scénářů.
- **Přístupnost.** Cílová úroveň WCAG, ovládání klávesnicí a podpora čteček obrazovky.
- **Lokalizace a internacionalizace.** Podporované jazyky, formáty data, čísel a měn, časové zóny a kódování textu. Popíše se způsob správy překladů.
- **Kompatibilita klientů.** Podporované prohlížeče a jejich minimální verze, operační systémy, rozlišení obrazovek a chování na mobilních zařízeních.

## 4. Provoz a observabilita

- **Provozní model.** Kdo systém provozuje, jaké má provozní režimy, kdy jsou údržbová okna a jaké plánované úlohy v jakých intervalech běží. Součástí jsou runbooky pro typické provozní situace.
- **Observabilita.** Strukturované logování s úrovněmi a korelačními identifikátory, metriky, distribuované trasování a health endpointy. Doplní se dashboardy, alerting s prahovými hodnotami a definice SLI a SLO.
- **Podpora a servisní úrovně.** SLA a OLA, doby reakce a vyřešení podle priority incidentu a kontaktní kanály. Popíše se incident, problem a change management včetně eskalace.
- **Prostředí a infrastruktura.** Seznam prostředí od vývojového po produkční, sizing hardwaru, síťová topologie, DNS, certifikáty a externí závislosti.
- **Konfigurace a správa tajemství.** Externalizovaná konfigurace oddělená od kódu, feature flags a bezpečné ukládání a rotace tajemství.
- **Nasazení a release management.** CI/CD pipeline, verzování, strategie vydání, migrace databázového schématu a rollback. Uvede se, zda je vyžadováno nasazení bez odstávky.

## 5. Kvalita vývoje a životní cyklus

- **Udržovatelnost a modifikovatelnost.** Kódové standardy, statická analýza, modularita, minimální pokrytí testy a pravidla code review. Stanoví se přípustná míra technického dluhu.
- **Testovatelnost a akceptační kritéria nefunkčních požadavků.** U každého požadavku se určí metoda ověření, prostředí, testovací data a kdo výsledek akceptuje. Detailní testovací scénáře patří do samostatného testovacího plánu.
- **Přenositelnost.** Nezávislost na operačním systému, možnost běhu on-premise i v cloudu, kontejnerizace a omezení vendor lock-inu.
- **Interoperabilita a rozhraní.** Standardy API, verzování, zpětná kompatibilita, formát chybových odpovědí a limity včetně rate limitingu.
- **Dokumentace.** Rozsah architektonické, provozní, uživatelské a API dokumentace a požadavek na její aktuálnost při každé změně.
- **Licencování a náklady.** Licence komponent třetích stran, compliance open source licencí a odhad provozních nákladů a TCO.
- **Přechod do provozu.** Migrace dat a její parametry, tedy objem, délka odstávky, ověření integrity a rollback, dále cutover plán, případný souběžný běh se starým systémem, školení a kritéria převzetí do provozu.
- **Ukončení životního cyklu a exit strategie.** Archivace a bezpečná likvidace dat, export dat do standardního formátu a podmínky předání jinému dodavateli.

## 6. Průřezové kapitoly

- **Rizika nefunkčních požadavků.** Rizika ohrožující splnění cílových hodnot, jejich dopad, pravděpodobnost a opatření k jejich zmírnění.
- **Matice trasovatelnosti.** Vazba mezi nefunkčními požadavky, architektonickými rozhodnutími, implementací a testy, které je ověřují.
- **Energetická a environmentální efektivita.** Volitelná kapitola pro požadavky na spotřebu zdrojů a udržitelnost, pokud je zákazník vyžaduje.

## 7. Šablona jednoho požadavku

- **Povinné atributy.** Identifikátor, kategorie, popis, metrika, cílová hodnota, mezní nepřijatelná hodnota, podmínky měření, metoda ověření, priorita podle MoSCoW, vlastník a zdroj požadavku.
- **Nejčastější chyby.** Neměřitelné formulace typu rychlý a bezpečný systém, chybějící podmínky měření, převzatý katalog bez doplněných cílových hodnot a požadavky bez vlastníka, které nikdo neakceptuje.
