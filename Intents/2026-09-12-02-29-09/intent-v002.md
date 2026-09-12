---
intent_version: 2
created_at: 2026-09-12-02-44-37
based_on: intent-v001.md
source: intent-chat-v002.md
---

# Metoda přískoku pro plán konsolidace klientské dokumentace

## Účel

Toto zadání doplňuje [intent-v001.md](intent-v001.md). Nemění jeho cílové dokumenty, zdrojový seznam ani pravidla, podle nichž se do dokumentace smí promítnout pouze skutečně implementované a zdroji doložené chování. Mění pouze způsob průzkumu a tvorby plánu tak, aby výsledek nebyl závislý na tom, zda se všechny plány, poznámky, zdrojový kód a testy vejdou do jednoho kontextového okna.

## Princip

Průzkum probíhá po samostatných tématech nebo funkcích. Po uzavření každého tématu se jeho výsledek uloží do trvalého pracovní matrice. Další krok vychází z uložené matice, ne z neověřené paměti předchozího kontextu.

## Trvalá pracovní matice

Pro každé téma musí existovat právě jeden dohledatelný záznam s těmito poli:

- stabilní identifikátor a stručný název tématu
- všechny související části zdrojových plánů a notes v chronologickém pořadí
- původní zamýšlené chování
- každá pozdější oprava, změna nebo odstranění, včetně zdroje, který jí dal přednost
- výsledný zamýšlený stav po posledním relevantním zdroji
- důkaz ze skutečné implementace a relevantních testů
- výsledek: implementováno, neimplementováno nebo nejednoznačné
- dopad na `client.md`, `protocol.md` a `tray_UX.md`: doplnit, opravit, ponechat nebo nezapisovat
- otevřené rozhodnutí, pokud důkazy nestačí

Matice je pracovní artefakt plánování. Musí se po každém uzavřeném tématu bezpečně uložit a nesmí se při dalším tématu přepisovat ani zkracovat tak, aby ztratila zdrojové odkazy nebo rozhodnutí o přednosti.

## Postup

1. Založit registr témat odvozených z devíti plánů a fixů a ze sedmi časově řazených notes souborů z `intent-v001.md`.
2. Vybrat jedno téma a načíst pouze relevantní úseky zdrojů, vždy chronologicky.
3. Rekonstruovat jeho vývoj: co bylo původně navrženo, co bylo později změněno, opraveno nebo zrušeno a jaký měl být konečný stav.
4. Zapsat tuto chronologickou syntézu do matice ještě před přechodem k jinému tématu.
5. Ověřit konečný stav v aktuálním kódu a relevantních testech.
6. Doplnit do matice důkazy a klasifikaci implementace.
7. Teprve nyní určit konkrétní plánovanou změnu jednoho či více cílových dokumentů.
8. Opakovat pro další téma, dokud matice nepokrývá celý zdrojový seznam.
9. Z celé uzavřené matice sestavit realizační plán změn `client.md`, `protocol.md` a `tray_UX.md`.

## Pravidla proti ztrátě kontextu

- Do jednoho průzkumného kroku nenačítat nesouvisející témata jen proto, aby byl přečten celý soubor.
- Při rozporu vždy uložit obě varianty, jejich zdroje a výsledek pravidla přednosti. Nikdy nenahradit rozpor nezdokumentovanou zkratkou.
- Před přechodem na další téma ověřit, že záznam matice obsahuje konečný zamýšlený stav i skutečný implementační stav.
- Při obnově nebo novém kontextu nejprve načíst existující matici a pokračovat od prvního neuzavřeného tématu.
- Implementace bez opory v závazném zdrojovém seznamu zůstává mimo rozsah, i kdyby byla při průzkumu objevena.

## Výstupní plán

Konečný plán nesmí být seznamem neurčitých dokumentačních úprav. Pro každou plánovanou změnu musí určit cílový dokument, přesnou oblast, doložené konečné chování, zdrojové odkazy, důkaz implementace a důvod, proč daný obsah patří právě do `client.md`, `protocol.md` nebo `tray_UX.md`.

## Akceptace

- Každý bod závazného zdrojového seznamu z `intent-v001.md` je přiřazen k alespoň jednomu záznamu matice nebo výslovně označen jako nerelevantní bez ztráty důkazu.
- Každé téma obsahuje chronologickou syntézu i ověření v implementaci.
- Konečný plán lze vytvořit i po přerušení práce, aniž by bylo nutné znovu načítat celý zdrojový soubor.
- Žádný neověřený nebo pouze plánovaný požadavek se nevydává za aktuální implementovaný stav.
- Plán rozlišuje změny v klientské specifikaci, protokolu a tray UX a nevytváří zbytečné duplicity.

