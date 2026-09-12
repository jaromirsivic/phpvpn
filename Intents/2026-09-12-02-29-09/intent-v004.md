---
intent_version: 4
created_at: 2026-09-12-07-11-38
based_on: intent-v003.md
source: intent-chat-v004.md
---

# Rozšíření konsolidace o pre-poll backpressure

Tento záměr doplňuje `intent-v001.md` až `intent-v003.md`. Ostatní hranice konsolidace zůstávají zachované.

## Nový schválený rozsah

Vedle dokumentační konsolidace a návrhu testů je schváleno navrhnout realizaci **pre-poll backpressure** pro příchozí data.

- Maximální pollingový interval klienta zůstává 5 sekund.
- Bezprostředně před dalším `data` pollingem se má ověřit, zda příchozí fronty mají kapacitu pro bezpečné přijetí případných datových záznamů.
- Pokud kapacita chybí, nový polling se odloží. Cílem je nestahovat pakety, které by klient následně musel kvůli plným frontám zahodit.
- Řešení musí zachovat limity front, FIFO, účetnictví, zpracování deferred/error návratu a stávající rozdělení odpovědností vláken.
- Součástí realizačního plánu musí být konkrétní implementační kroky a přiměřené regresní či integrační testy tohoto chování.
- Dokud se změna nerealizuje, nesmí cílová dokumentace tvrdit, že je pre-poll backpressure již dodaná.

## Vztah k R02

Rozhodnutí R02 je tímto uzavřeno: odchylka 2 s versus 5 s se řeší zachováním 5 s. Dosud nevolaná pomocná admission funkce se má napojit do skutečné pollovací cesty podle výše uvedených pravidel.

