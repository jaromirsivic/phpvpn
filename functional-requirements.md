# Struktura dokumentu detailní analýzy (detailního designu)

Nefunkční požadavky jsou v samostatném dokumentu, zde se na ně pouze odkazuje.

## Úvod

- **Název projektu** - oficiální název a zkratka.
- **Identifikace dokumentu** - verze, datum, autoři, schvalovatelé, stav.
- **Historie změn** - verze a popis změn.
- **Pojmy a zkratky** - slovník doménových pojmů.
- **Související dokumenty** - zadání, katalog požadavků, nefunkční požadavky, smlouva, Dokumentace formátu zpráv platná pro vývoj.

## Požadavky (mimo tento dokument)

- **Katalog funkčních požadavků** - číslovaný, odkazovatelný.
- **Matice pokrytí** - požadavek, kapitola designu, test.
- **Akceptační kritéria** - definice hotovo.

## Data

- **Entitní datový model** - entity, atributy, vazby, kardinality.
- **Dokumentový datový model** - kolekce, struktura dokumentů, denormalizace.
- **Model časových řad** - dimenzní prostory, časové rady, vzorce.
- **Číselníky a master data** - statická data, výchozí konfigurace.

## **Business procesy** - kroky, aktéři, rozhodovací body. (nahradit workflow)

## **Workflow**
- High level úroveň
- Rozpis až na úrovni jednotlivých aktivit.

## **Stavové diagramy** - stavy, přechody, podmínky.

## **Scheduler a naplánované úlohy** - plán, vstupy a výstupy, souběh, sledování.

## **Vizuální use case**
- **Mockupy**
- **Popis visuálního use case**
- **Navigace** - menu.

## **Nevizuální use cases** - dávky, integrační a systémové scénáře. (raději použít micro workflows)

## **Notifikace** - alarmy (email, sms, GUI), šablony, jazyky, omezení frekvence.

## **Chování při chybách** - katalog chyb, fail fast, nebo kompenzace.


## Integrace a rozhraní

- **Datové toky** - vstup, transformace, výstup, popis datového formátu (XSD).
- **Integrace** - connection sheet, protokol, frekvence odesílání, vlastník, chování při chybě.

## Bezpečnost

- **Autentizace** - lokální účty, SSO, OIDC, MFA, session.
- **Role a oprávnění** - matice nad funkcemi a daty.
- **Správa identit** - založení, změna, deaktivace, provisioning.



