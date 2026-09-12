---
intent_version: 3
created_at: 2026-09-12-02-47-55
based_on: intent-v002.md
source: intent-chat-v003.md
---

# Doplnění metody přískoku o plán testovacího pokrytí

Toto zadání doplňuje [intent-v002.md](intent-v002.md). Všechny jeho cíle, omezení a pravidla zůstávají beze změny.

## Nové povinné pole pracovní matice

Každý tematický záznam musí vedle pole „dopad na cílovou dokumentaci“ obsahovat také pole **dopad na konstrukci testů**.

Toto pole musí rámcově uvést:

- zda je pro dané implementované chování nutné doplnit, opravit nebo rozšířit testy
- které konkrétní funkce, hranice, stavové přechody, chyby nebo integrační vazby mají být ověřeny
- přiměřený druh a rozsah testů, například jednotkové, integrační, IPC, UI, regresní nebo ruční akceptační ověření
- zda současné testy tvrzení pokrývají, částečně pokrývají nebo nepokrývají
- důvod, proč je navržený rozsah testů nutný vzhledem k riziku a povaze změny

## Pravidla

- Neexistence testu není důkazem, že funkce není implementovaná.
- Plán musí oddělit důkaz současného chování v kódu od návrhu budoucího testovacího pokrytí.
- Pokud testy k tématu nejsou, plán nesmí předstírat, že funkce není dodaná. Musí ji ověřit dostupnými důkazy a zároveň zapsat chybějící testy jako samostatnou budoucí práci.
- Testovací návrhy jsou rámcové a nesmí neodůvodněně rozšířit implementační rozsah mimo chování doložené závaznými zdroji.
- Konečný realizační plán musí mít samostatnou sekci testovacích změn a dohledatelně propojit každou testovací položku s tematickým záznamem matice.

## Akceptace

- Každé téma v pracovní matici má vyplněný dopad na konstrukci testů.
- Konečný plán zřetelně rozlišuje existující testy, chybějící testy a navržené testovací doplnění.
- Je možné samostatně rozhodnout o realizaci dokumentačních změn a o realizaci testovacích změn, aniž by se ztratila jejich věcná vazba.

