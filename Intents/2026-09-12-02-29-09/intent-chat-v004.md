---
intent_version: 4
created_at: 2026-09-12-07-11-38
based_on: intent-v003.md
produces: intent-v004.md
---

# Kumulativní upřesnění záměru

## Dosavadní rozsah

`intent-v001.md` až `intent-v003.md` určují konsolidaci implementovaného chování do `client.md`, `protocol.md` a `tray_UX.md`, plánování metodou přískoku a samostatné posouzení testovacího pokrytí. Původní konsolidace neměla implementovat novou funkčnost.

## Nové schválené rozhodnutí

**User:** Pro R02 má maximální polling interval zůstat pět sekund. Pre-poll backpressure je chtěná ochrana. Před odesláním dalšího data pollingu má klient ověřit kapacitu příchozích front a polling odložit, pokud pro možné příchozí pakety není dost místa.

**AI:** Tento bod rozšiřuje původní čistě dokumentační konsolidaci o budoucí implementaci a testy pre-poll backpressure.

**User:** Ano, termín se prodlužuje na neurčito.

