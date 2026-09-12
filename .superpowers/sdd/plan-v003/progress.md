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

Stav: připraveno k realizaci.
