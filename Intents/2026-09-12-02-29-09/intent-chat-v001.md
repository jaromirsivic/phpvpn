---
intent_version: 1
created_at: 2026-09-12-02-29-09
produces: intent-v001.md
---

# Přepis záměru

Tento přepis zachycuje viditelnou hlasovou konverzaci, včetně oprav a upřesnění. Pozdější formulace uživatele mají přednost před dřívějšími.

**User:** Mám nový záměr na úpravu.

**AI:** Jasně, poslouchám. Co přesně potřebuješ upravit?

**User:** Ve složce `notes` je uvedeno mnoho úprav, které byly realizovány, ale u většiny nebylo to, co se nakonec implementovalo do klientské tray aplikace ani do služby, zapracováno do `client.md`. Cílem je dostat `client.md` do stavu, který postihne tyto poznámky. Ještě před nimi je potřeba postupně projít `plan_client.md`, `plan2_client.md`, `plan3_client.md`, `plan3_fix.md`, `plan3_fix2.md`, `plan3_fix3_problems.md`, `plan3_fix3_work_for_agents.md` a `plan3_fix3.md`. Pozdější plány a fixy opravují předchozí, proto je důležité číst je postupně a zapracovat je do výsledného `client.md`.

**User:** A potom se teprve…

**AI:** Navazující část záměru zůstává otevřená. Poslouchám dál a zatím nic nevyhodnocuji ani neupravuji.

**User:** A potom se teprve vrhneme na poznámky. Ještě se na nic nedívej. Teď si zapisuješ poznámky a potom tě nechám záměr zpracovat.

**AI:** Beru to jako záměr a zatím si jej pouze poznamenávám.

**User:** Zapomeň předchozí popis. První fáze má začít zjištěním aktuálního stavu `client.md` a potom se mají v pořadí projít změny z `plan_client.md`, `plan2_client.md`, `plan3_client.md`, `plan3_fix.md`, `plan3_fix2.md`, `plan3_fix3.md`, `plan3_fix3_problems.md`, `plan3_fix3_problems2.md` a `plan3_fix3_work_for_agents.md`. Vypiš toto pořadí jako seznam.

**AI:**

1. `client.md`
2. `plan_client.md`
3. `plan2_client.md`
4. `plan3_client.md`
5. `plan3_fix.md`
6. `plan3_fix2.md`
7. `plan3_fix3.md`
8. `plan3_fix3_problems.md`
9. `plan3_fix3_problems2.md`
10. `plan3_fix3_work_for_agents.md`

**User:** Seznam sedí, pouze `client.md` budeme opravovat podle zbývajících položek.

**AI:** `client.md` je cílový dokument a všechny následující soubory jsou postupně aplikované zdroje oprav v uvedeném pořadí.

**User:** Potom lze pokračovat do složky `notes`. Všechny soubory končící `_notes.md` zařaď na konec seznamu a seřaď vzestupně podle času. Vypiš celý seznam znovu do chatu.

**AI:**

1. `client.md` – cílový dokument
2. `plan_client.md`
3. `plan2_client.md`
4. `plan3_client.md`
5. `plan3_fix.md`
6. `plan3_fix2.md`
7. `plan3_fix3.md`
8. `plan3_fix3_problems.md`
9. `plan3_fix3_problems2.md`
10. `plan3_fix3_work_for_agents.md`
11. `notes/2026-09-04-18-59-01_notes.md`
12. `notes/2026-09-04-22-20-52_notes.md`
13. `notes/2026-09-05-08-25-34_notes.md`
14. `notes/2026-09-05-09-53-11_notes.md`
15. `notes/2026-09-06-22-21-54_notes.md`
16. `notes/2026-09-07-11-19-48_notes.md`
17. `notes/2026-09-09-20-43-00_notes.md`

**User:** Cílovými dokumenty mohou být `client.md`, `protocol.md` a `tray_UX.md`, primárně však `client.md`. Tyto tři dokumenty je potřeba upravit tak, aby všechny ostatní plány a poznámky byly nadbytečné a mohly být později vymazány. Zdrojové soubory sloužily k implementaci, proto se musí nejdřív ověřit skutečný stav implementace. Je potřeba zjistit, zda je implementované chování podchyceno v některém ze tří cílových dokumentů. Pokud implementace v cílových dokumentech chybí a existuje k ní podklad v některém plánu nebo souboru `_notes.md`, musí se zakomponovat do odpovídajícího cílového dokumentu. Pokud existuje implementace, která nebyla podchycena v žádném zdrojovém dokumentu, zatím pro ni nic nevytvářej. Postupujeme pouze podle obsahu vyjmenovaných plánů a poznámek: zjistit, zda byl implementován, a pokud ano, promítnout ho zpětně do tří cílových dokumentů. Potom bude možné se celého zdrojového seznamu zbavit. Vytvoř z toho formální zadání.

**AI:** Pro uložení formálního zadání potřebuje Intent Builder v projektu vytvořit složku `Intents`, která zatím neexistuje. Mohu ji vytvořit a zadání tam verzovaně uložit?

**User:** Ano.

