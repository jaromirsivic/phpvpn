---
intent_version: 1
created_at: 2026-09-19-11-17-37
source: intent-chat-v001.md
---

# Kompletní bezpečnostní a výkonnostní audit PHP VPN

## Shrnutí

Provést kompletní audit serverové a klientské části PHP VPN. Audit má současně hledat možnosti zrychlení a bezpečnostní či zásadní logické nedostatky. Každé ověřené zjištění se musí ihned zapsat do průběžně aktualizovaného souboru `audit.md` v kořenovém adresáři projektu.

## Kontext projektu

- Serverová část je soustředěna především v PHP implementaci `phpvpn.php`.
- Klientská část je nativní klient pod adresářem `client/`.
- Audit musí pokrýt obě strany a jejich společné komunikační toky.
- Za klíčové zdroje návrhových rozhodnutí a přijatých rizik se považují zejména `protocol.md` a `client.md`.
- Je nutné prověřit i další dokumentaci, pokud může obsahovat relevantní návrhové rozhodnutí nebo dříve přijaté riziko.

## Požadovaný výsledek

V kořenovém adresáři projektu vytvořit a průběžně udržovat `audit.md`. Dokument musí obsahovat:

1. přehled vědomě vyloučených nereálných okrajových případů,
2. tabulku výkonnostních zjištění,
3. tabulku bezpečnostních zjištění,
4. konkrétní odkazy na kód a dokumentaci, aby byla každá položka ověřitelná,
5. závěrečnou kontrolu úplnosti a konzistence.

## Rozsah auditu

- Projít veškerý relevantní zdrojový kód serverové i klientské části.
- Kontrolovat relevantní řádky kódu v jejich lokálních i systémových souvislostech.
- Posuzovat celý běžný komunikační tok mezi klientem a serverem.
- Hledat výkonnostní úzká místa, zbytečnou práci, nadbytečné přenosy, čekání, alokace, kopírování, parsování, kryptografické operace a další zdroje měřitelné režie.
- Hledat bezpečnostní mezery, nedostatečné kontroly, nebezpečné předpoklady, chybné vazby stavů, zásadní logické chyby a nevhodná návrhová rozhodnutí.
- Posuzovat vzájemný dopad výkonových změn a bezpečnosti.

## Vyloučení nereálných corner cases

Audit nemá spotřebovávat nepřiměřený čas na teoretické případy, které:

- za běžného provozu prakticky nenastávají,
- nelze jednoduše nebo realisticky vyvolat,
- nesouvisejí s běžnou komunikací systému,
- nemají smysluplný dopad na bezpečnost nebo výkon,
- mají zanedbatelnou pravděpodobnost i dopad.

Na začátku `audit.md` musí být uvedeny konkrétní skupiny vyloučených případů, důvod jejich vyloučení a předpoklad, na jehož základě jsou považovány za nereálné nebo nerelevantní. Toto vyloučení nesmí sloužit k ignorování realisticky zneužitelného bezpečnostního problému ani běžně dosažitelného výkonnostního problému.

## Výkonnostní zjištění

Vyhledat místa, kde lze zrychlit systém jako celek nebo jeho konkrétní fáze. Za důležitá označit zejména zjištění, jejichž realizace:

- bezpečnost nesnižuje, nebo
- přináší pouze malé, konkrétně popsané a přijatelné bezpečnostní riziko.

První hlavní tabulka v `audit.md` musí u každého zjištění obsahovat minimálně:

| Pole | Požadovaný obsah |
|---|---|
| ID | Jedinečný a stabilní identifikátor výkonnostního zjištění |
| Popis | Přesný popis problému nebo příležitosti ke zrychlení včetně dotčených částí systému |
| Očekávaný přínos | Textové nebo měřitelné vyhodnocení, jak výrazně a v jakých situacích se systém zrychlí |
| Bezpečnostní dopad | Zda může implementace oslabit bezpečnost, za jakých podmínek a jak závažně |
| Důležitost | Jasné označení významných optimalizací s příznivým poměrem výkonu a bezpečnostního rizika |

Pokud nelze přínos spolehlivě kvantifikovat bez implementace nebo benchmarku, musí být odhad označen jako odhad a musí být popsán očekávaný mechanismus zrychlení.

## Bezpečnostní zjištění

U každého bezpečnostního nebo zásadního logického zjištění prověřit `protocol.md`, `client.md` a další relevantní dokumentaci. Určit, zda bylo riziko dříve vědomě přijato. Absence dokladu znamená, že přijetí rizika nebylo doloženo.

Druhá hlavní tabulka v `audit.md` musí u každého zjištění obsahovat minimálně:

| Pole | Požadovaný obsah |
|---|---|
| ID | Jedinečný a stabilní identifikátor bezpečnostního zjištění |
| Popis | Přesný popis problému, podmínek vzniku, dopadu a souvislostí |
| Doporučené řešení | Praktický návrh odstranění nebo omezení rizika |
| Závažnost | Závažnost nálezu bez ohledu na to, zda bylo riziko dříve přijato |
| Dříve přijato | Jednoznačně ano nebo ne |
| Doklad o přijetí | Přesný odkaz na `protocol.md`, `client.md` nebo jinou dokumentaci, pokud existuje |

Dřívější přijetí rizika nikdy nesmí způsobit vypuštění nálezu ani snížení transparentnosti reportu. Přijetí pouze doplňuje kontext. Závažnost musí být uvedena vždy.

## Průběžné ukládání

- `audit.md` vytvořit na začátku realizace auditu, nikoliv až po jeho dokončení.
- Po dokončení a dostatečném ověření každého jednotlivého výkonnostního nebo bezpečnostního zjištění ihned aktualizovat `audit.md`.
- Teprve po zápisu zjištění pokračovat k dalšímu nálezu.
- Průběžný dokument musí zůstat čitelný a konzistentní i během nedokončeného auditu.
- Na konci auditu provést kontrolu, že všechna ověřená zjištění byla zapsána právě jednou a že odkazy, závažnosti, přijetí rizik a bezpečnostní dopady jsou konzistentní.

## Omezení a pracovní pravidla

- Audit zatím pouze popisuje zjištění a navrhuje řešení. Samotné opravy kódu nejsou součástí tohoto intentu, pokud nebudou samostatně autorizovány.
- Dřívější dokumentované přijetí rizika se musí respektovat jako projektový fakt, ale nesmí zabránit jeho opětovnému uvedení v reportu.
- Závěry musí být podloženy konkrétními vazbami na zdrojový kód, komunikační tok, testy nebo dokumentaci.
- Spekulativní nálezy musí být jasně odlišeny od ověřených zjištění a nesmí být prezentovány jako potvrzené.

## Akceptační kritéria

- `audit.md` existuje přímo v kořenu projektu.
- Dokument na začátku uvádí vyloučené corner cases a důvody jejich vyloučení.
- Audit pokrývá serverovou i klientskou část a jejich vzájemnou komunikaci.
- Všechny relevantní části kódu byly posouzeny v kontextu systému.
- První hlavní tabulka obsahuje všechna ověřená výkonnostní zjištění a požadovaná pole.
- Druhá hlavní tabulka obsahuje všechna ověřená bezpečnostní zjištění a požadovaná pole.
- U každého bezpečnostního zjištění je uvedena závažnost a stav dřívějšího přijetí.
- Pokud je riziko přijato, report obsahuje přesný dokumentační odkaz.
- Každé ověřené zjištění bylo do `audit.md` zapsáno průběžně bez čekání na dokončení celého auditu.
- Závěrečná kontrola potvrdí úplnost a konzistenci reportu.
