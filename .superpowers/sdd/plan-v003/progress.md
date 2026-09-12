# SDD ledger — plan: C:\git\phpvpn\Plans\2026-09-12-02-29-09\plan-v003.md

## Pre-flight

| Kontrola | Výsledek |
|---|---|
| Pracovní větev | `tray_UX`, není `main` ani `master` |
| Izolace | Uživatel neschválil novou worktree. Realizace poběží sekvenčně v existujícím checkoutu. |
| Rozsah první úlohy | D00, D01 a TT01. Žádná změna produkčního kódu. |
| Sdílené soubory s pozdějšími kroky | `client.md` a `priskok-matice.md`; proto se úlohy nesmějí překrývat ani běžet souběžně. |
| Rozpor v plánu | Dokumentační větev původně neplánuje testové změny. Uživatel následně schválil testy po každém tematickém bloku. Pro TT01 plán výslovně uvádí, že nový unit test není odůvodněný, proto se ověří a zdokumentuje nulová změna testů. |

## Task 1: D00 + D01 + TT01

Stav: dokončeno. D01 je doplněno, TT01 nevyžadovalo nový test. Viz `task-1-report.md`.

## Task 2: D02 + TT02

Stav: dokončeno. D02 je doplněno, TT02 přidalo cílený hraniční test, 24 cílených testů bez selhání. Viz `task-2-report.md`.

## Task 3: D03 + TT03

Stav: dokončeno. D03 je doplněno, TT03 přidalo dva integrační testy, 57 cílených testů bez selhání. Viz `task-3-report.md`.

## Task 4: D04 + TT05

Stav: dokončeno. D04 je doplněno, současné pokrytí TT05 ověřeno 14 cílenými testy bez selhání. Budoucí B04 zůstává nedodané. Viz `task-4-report.md`.

## Task 5: D05 + TT06

Stav: dokončeno. D05 je doplněno, TT06 přidalo dva lifecycle testy, 38 cílených testů bez selhání. Starší rozporná věta mimo D05 je předána D13. Viz `task-5-report.md`.

## Task 6: D06 + TT09

Stav: dokončeno. D06 je doplněno, TT09 ověřeno 28 cílenými existujícími testy bez selhání. Viz `task-6-report.md`.

## Task 7: D07 + TT13

Stav: dokončeno. D07 je doplněno, TT13 přidalo integrační pokrytí ztraceného startu; 38 cílených testů bez selhání. Finish UI akceptace je NOT RUN kvůli existujícímu harnessu. Viz `task-7-report.md`.

## Task 8: D08 + TT20

Stav: dokončeno. D08 je doplněno, TT20 přidalo izolovaný test loggeru, 1 cílený test bez selhání. Viz `task-8-report.md`.

## Task 9: D09

Stav: předáno do samostatného chatu. Rozsah: redakční odpoutání klientských dokumentů od historických pracovních značek, bez kódu a nových testů.
