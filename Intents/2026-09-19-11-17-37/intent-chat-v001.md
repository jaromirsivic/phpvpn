---
intent_version: 1
created_at: 2026-09-19-11-17-37
produces: intent-v001.md
---

# Zdrojová konverzace intentu

**User:** Vytvoř nový intent.

**AI:** V projektu už je aktivní intent ve fázi realizace plánu. Mám nyní založit nový intent a přepnout aktivní záznam na něj, přičemž dosavadní intent i jeho soubory zůstanou nedotčené?

**User:** Zatím ne. Zatím ti budu diktovat a nemusíš zaznamenávat intent. Až ti řeknu zaznamenat intent, teprve potom ho přepneš a uložíš do nového intentu.

**AI:** Pokračuj v diktování. Zatím nic nepřepínám ani neukládám.

**User:** Chtěl bych udělat kompletní bezpečnostní a performance audit. Agent se podívá na PHP VPN, na serverovou i klientskou část. Především mě zajímá, kde je možné systém a celý běh zrychlit. Pokud zjištění nekolidují s bezpečností, nebo by narušení bezpečnosti bylo malé a přijatelné, označí tato zjištění jako důležitá.

Paralelně projde všechny řádky kódu, podívá se na souvislosti a zjistí, zda tam není zásadní bezpečnostní mezera nebo něco logicky navrženého špatně, co by se mělo dělat jinak. Pokud něco takového najde, ověří, zda jsme s tím dříve nesouhlasili v některém dokumentu, typicky v `protocol.md`. Zjištění uvede ve finálním reportu. Buď odkáže na dokumentaci a uvede, že jsme riziko označili za přijatelné, nebo, pokud takovou informaci nenajde, uvede ho jako nové zjištění a přidá závažnost.

Závažnost uvede v každém případě. U všech bezpečnostních zjištění chci identifikátor, přesný popis, návrh řešení, závažnost a informaci, zda jsme s rizikem dříve souhlasili. To bude pravděpodobně druhá tabulka reportu.

První tabulka se bude týkat performance issues. Chci ID, popis, textové vyhodnocení, o kolik a za jakých okolností zrychlení pomůže, a informaci, zda implementace zrychlení nenaruší bezpečnost.

Audit se nemá zabývat corner cases, které nedávají smysl. Na začátku reportu vypíše, kterými corner cases se nezabýval a proč u nich zjišťovat bezpečnost nebo výkon nedává smysl. Nemá se pálit čas na něčem, co téměř nemůže nastat, co nenastává při běžné komunikaci nebo co nelze jednoduše vyvolat.

Prozatím zadání zpracuj jen v paměti, formalizuj ho do chatu a nic nikam nezapisuj.

**AI vytvořila v konverzaci pracovní formalizaci:**

# Pracovní zadání: kompletní bezpečnostní a výkonnostní audit PHP VPN

Toto je zatím pouze pracovní formalizace v konverzaci. Aktivní intent se nepřepíná a nic se nezapisuje do souborů.

## Cíl

Provést kompletní audit PHP VPN na serverové i klientské straně se dvěma souběžnými oblastmi:

1. Identifikovat možnosti zrychlení systému.
2. Odhalit bezpečnostní nedostatky, zásadní logické chyby a nevhodná návrhová rozhodnutí.

Audit musí zohledňovat souvislosti napříč celým systémem, nikoliv pouze izolované úseky kódu.

## Rozsah kontroly

- Projít veškerý relevantní zdrojový kód serverové i klientské části.
- Posuzovat jednotlivé řádky v kontextu celého komunikačního toku a návrhu systému.
- Prověřit také relevantní dokumentaci, zejména `protocol.md`.
- U každého bezpečnostního zjištění ověřit, zda už bylo riziko dříve vědomě přijato.

## Vyloučené okrajové případy

Audit se nemá zabývat teoretickými corner cases, které:

- za běžného provozu prakticky nenastávají,
- nelze jednoduše nebo realisticky vyvolat,
- nemají smysluplný dopad na bezpečnost ani výkon,
- by spotřebovaly nepřiměřené množství času vzhledem k pravděpodobnosti a dopadu.

Na začátku výsledného reportu musí být samostatně uvedeno:

- které skupiny corner cases byly vyloučeny,
- proč byly vyloučeny,
- na základě čeho byly považovány za nereálné nebo nerelevantní.

## Výkonnostní audit

Hledat místa, kde lze zrychlit celý systém nebo jeho konkrétní fáze. Prioritu mají optimalizace, které:

- nesnižují bezpečnost, nebo
- přinášejí pouze malé, výslovně popsatelné a přijatelné bezpečnostní riziko.

První hlavní tabulka reportu bude obsahovat minimálně:

| Pole | Obsah |
|---|---|
| ID | Jedinečný identifikátor zjištění |
| Popis | Přesný popis problému nebo příležitosti ke zrychlení |
| Přínos | Jak výrazné zrychlení lze očekávat a ve kterých situacích |
| Bezpečnostní dopad | Zda by implementace mohla oslabit bezpečnost a jakým způsobem |
| Důležitost | Zvýraznění významných optimalizací s příznivým poměrem výkonu a bezpečnostního rizika |

## Bezpečnostní audit

Hledat zejména:

- zásadní bezpečnostní mezery,
- chybné nebo nedostatečné kontroly,
- rizikové vazby mezi serverovou a klientskou částí,
- logické chyby v komunikačních tocích,
- návrhová rozhodnutí, která by měla být provedena jinak.

U každého nálezu ověřit `protocol.md` a další relevantní dokumentaci. Rozlišit:

- riziko již bylo vědomě přijato,
- riziko nebylo jako přijatelné doloženo.

Druhá hlavní tabulka reportu bude obsahovat minimálně:

| Pole | Obsah |
|---|---|
| ID | Jedinečný identifikátor zjištění |
| Popis | Přesný popis problému a jeho souvislostí |
| Doporučené řešení | Návrh možného odstranění nebo omezení rizika |
| Závažnost | Závažnost každého nálezu bez ohledu na jeho dřívější přijetí |
| Dříve přijato | Ano nebo ne |
| Doklad o přijetí | Přesný odkaz na `protocol.md` nebo jinou dokumentaci, pokud existuje |

Dřívější přijetí rizika nesmí způsobit, že nález z reportu zmizí nebo nebude mít stanovenou závažnost. Musí pouze doplnit jeho kontext.

**User:** Je to skoro dobré. Ještě bych dal jako jeden z důležitých dokumentů `client.md` a řekl, že výsledek se má uložit do souboru `audit.md` přímo v rootu projektu.

**AI:** Zapracováno do pracovního zadání pouze v rámci konverzace. Mezi klíčové dokumenty patří také `client.md` a finální report auditu se má uložit jako `audit.md` přímo v kořenovém adresáři projektu. Zatím jsem nic nezapsala ani nepřepnula aktivní intent.

**User:** A ještě tam doplň, že `audit.md` by se měl aktualizovat vždycky po každém zjištění, aby se průběžně doplňoval. Upravené formální zadání mi vypiš do chatu.

**AI vytvořila v konverzaci upravenou pracovní formalizaci:**

# Pracovní zadání: kompletní bezpečnostní a výkonnostní audit PHP VPN

Toto je zatím pouze formalizace v konverzaci. Aktivní intent se nepřepíná a nic se nyní nezapisuje do souborů.

## Cíl auditu

Provést kompletní bezpečnostní a výkonnostní audit PHP VPN, a to na serverové i klientské straně.

Audit má souběžně:

1. identifikovat možnosti zrychlení systému,
2. odhalit bezpečnostní nedostatky,
3. odhalit zásadní logické chyby,
4. upozornit na nevhodná návrhová rozhodnutí.

Jednotlivé části kódu musí být posuzovány v kontextu celého systému a jeho komunikačních toků.

## Rozsah kontroly

- Projít veškerý relevantní zdrojový kód serverové i klientské části.
- Kontrolovat všechny řádky relevantního kódu a jejich vzájemné souvislosti.
- Posoudit průběh běžné komunikace mezi klientem a serverem.
- Prověřit související dokumentaci.
- U každého bezpečnostního nálezu ověřit, zda už bylo dané riziko dříve vědomě přijato.

Mezi zvlášť důležité dokumenty patří:

- `protocol.md`
- `client.md`
- další dokumentace obsahující návrhová rozhodnutí nebo přijatá rizika

## Vyloučené okrajové případy

Audit se nemá podrobně zabývat teoretickými corner cases, které:

- za běžného provozu prakticky nenastávají,
- nelze jednoduše nebo realisticky vyvolat,
- nesouvisejí s běžnou komunikací systému,
- nemají smysluplný dopad na bezpečnost nebo výkon,
- by vyžadovaly nepřiměřené množství času vzhledem k pravděpodobnosti a dopadu.

Na začátku výsledného reportu musí být uvedeno:

- které skupiny corner cases nebyly analyzovány,
- proč byly vyloučeny,
- na základě čeho byly považovány za nereálné nebo nerelevantní.

## Výkonnostní audit

Hledat místa, kde lze zrychlit systém jako celek nebo jeho konkrétní fáze.

Za důležitá označit zejména zjištění, jejichž implementace:

- nenaruší bezpečnost, nebo
- způsobí pouze malé, výslovně popsané a přijatelné bezpečnostní riziko.

První hlavní tabulka reportu bude obsahovat minimálně:

| Pole | Požadovaný obsah |
|---|---|
| ID | Jedinečný identifikátor výkonnostního zjištění |
| Popis | Přesný popis problému nebo příležitosti ke zrychlení |
| Očekávaný přínos | Jak výrazně a v jakých situacích se systém zrychlí |
| Bezpečnostní dopad | Zda může implementace oslabit bezpečnost a jakým způsobem |
| Důležitost | Označení významných optimalizací s příznivým poměrem výkonu a bezpečnostního rizika |

## Bezpečnostní audit

Hledat zejména:

- zásadní bezpečnostní mezery,
- chybné nebo nedostatečné kontroly,
- rizikové vazby mezi serverovou a klientskou částí,
- logické chyby v komunikačních tocích,
- návrhová rozhodnutí, která by měla být provedena jinak.

U každého nálezu prověřit `protocol.md`, `client.md` a další relevantní dokumentaci. Rozlišit, zda:

- riziko už bylo vědomě přijato,
- nebo jeho přijetí není v dokumentaci doloženo.

Druhá hlavní tabulka reportu bude obsahovat minimálně:

| Pole | Požadovaný obsah |
|---|---|
| ID | Jedinečný identifikátor bezpečnostního zjištění |
| Popis | Přesný popis problému a jeho souvislostí |
| Doporučené řešení | Návrh odstranění nebo omezení rizika |
| Závažnost | Závažnost nálezu bez ohledu na jeho dřívější přijetí |
| Dříve přijato | Ano nebo ne |
| Doklad o přijetí | Přesný odkaz na `protocol.md`, `client.md` nebo jinou dokumentaci, pokud existuje |

Dřívější přijetí rizika nesmí způsobit, že nález z reportu zmizí nebo nebude mít stanovenou závažnost. Přijetí rizika pouze doplňuje kontext nálezu.

## Výstup a průběžné ukládání

- Výsledný report se uloží jako `audit.md` přímo v kořenovém adresáři projektu.
- `audit.md` se nebude vytvářet až na konci auditu.
- Report se musí průběžně doplňovat.
- Po dokončení každého jednotlivého zjištění se `audit.md` okamžitě aktualizuje.
- Každý dostatečně ověřený výkonnostní nebo bezpečnostní nález se zapíše dříve, než audit pokračuje k dalšímu nálezu.
- Na konci auditu se provede celková kontrola úplnosti a konzistence reportu.

**User:** Na základě úplně posledního zadání, které jsi vypsala do chatu, vytvoř nový intent a dej ho tam.

**AI:** Poslední formalizované zadání je úplné a dostupné. Zakládám pro něj nový samostatný intent, zatímco předchozí intent a jeho artefakty zůstanou nedotčené.

**User:** Ano, udělej to do nového intentu.

**User:** Počkej. Jenom mi řekni, proč to tak dlouho trvá.

**AI:** Nový intent musí obsahovat úplný snímek související konverzace a zároveň nesmí přepsat předchozí intent. Při závěrečném zápisu se změnil aktivní záznam intentu, takže bezpečnostní kontrola zápis odmítla. Samotné soubory intentu se nevytvořily, vznikla pouze prázdná nová složka.

**User:** Můžu ti říct, že máš vystornovat zápis chatu? Bude to v pořádku?

**AI:** Zápis konverzace nelze u platného intentu vynechat, protože intent musí vzniknout jako dvojice úplného snímku chatu a výsledné specifikace. Vynechání by vytvořilo neúplný intent. Mám pokračovat v dokončení plného intentu?

**User:** Ano, dokonči plný intent.
