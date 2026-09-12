---
intent_version: 2
created_at: 2026-09-12-02-44-37
based_on: intent-v001.md
produces: intent-v002.md
---

# Kumulativní přepis záměru

## Dosavadní záměr

V první verzi záměru uživatel určil, že `client.md` je primárním cílovým dokumentem a `protocol.md` a `tray_UX.md` jsou dalšími možnými cílovými dokumenty. Zdrojový seznam tvoří v závazném pořadí devět klientských plánů a fixů, následovaných sedmi soubory `_notes.md` řazenými vzestupně podle času. Úkolem je porovnat tvrzení z těchto zdrojů se skutečnou implementací a testy, promítnout pouze potvrzené implementované chování do odpovídajících cílových dokumentů a nevytvářet dokumentaci pro implementaci bez opory ve zdrojovém seznamu. Zdrojové plány a notes se v této fázi nemažou.

## Nové upřesnění uživatele

**User:** Když se celé zadání nevejde do kontextu, je třeba postupovat metodou přískoku. Agent má projít dokumenty po jednotlivých funkcích, u každé zjistit konečný výsledek a někam jej zapsat, potom pokračovat další funkcí.

**AI:** Doporučuji trvalou pracovní matici mimo kontext agenta. Nejprve vznikne registr témat a zdrojů. Agent pak zpracovává malé chronologické dávky a po každé zapisuje stabilní syntézu: co se zavedlo, co pozdější dokument změnil nebo zrušil a které zdroje to dokazují. Nad touto syntézou ověřuje kód a testy a nakonec z ní složí plán aktualizace `client.md`, `protocol.md` a `tray_UX.md`.

**User:** Výborně, to se mi líbí. Udělej z toho formalizované zadání a vypiš ho do chatu.

