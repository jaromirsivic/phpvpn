# Uživatelská rozhodnutí po plánu v2

Autorita: rozhodnutí uživatele předaná koordinátorem úkolu 01a092fb-41a1-71d1-b4c8-a31f879cc836 v této konverzaci dne 12. 9. 2026. Rozšíření R02 je formálně zaznamenáno v [intent-v004.md](../../Intents/2026-09-12-02-29-09/intent-v004.md). Tato novější explicitní rozhodnutí mají přednost před závěry o otevřených rozporech ve v1/v2. Historická zjištění v tématech se zachovávají jako důkaz původu změny.

Časový limit: uživatel schválil **unlimited**, koordinátor jej výslovně předal. Původní deadline 05:05 Europe/Berlin se již nepoužívá. Nevytvářejí se další agenti. Při obnovení kontextu je nutné obnovit celý účinný globální blok z reálného CODEX_HOME/AGENTS.override.md nebo AGENTS.md, případně úplného hostem dodaného bloku, a ověřit obě přesné hranice před věcnou prací.

| ID | Rozhodnutí | Dopad do plánu a testů |
|---|---|---|
| R01 | Vyjmout nedoloženou recovery po periodickém health failure, OS resume/network-change triggery a nedoloženou bezpečnou horní mez extrémního intervalu. | Popsat pouze dodanou časovou synchronizaci. Neplánovat implementaci ani testovací povinnost odstraněných corner cases. |
| R02 | Maximum běžného pollingu 5 s. Pre-poll backpressure je schválená budoucí implementace. | Samostatná implementační větev a její testy. Do dodaného popisu se promítne až po implementaci a ověření. |
| R03 | Popsat skutečný teardown tunelu/front a jejich nové vytvoření. | Bez slibu zachování tunelu a obecné recovery pádu role. Testovat skutečný lifecycle, nikoli odstraněnou záruku. |
| R04 | Odloženo, nyní neřešit. | Žádná realizace ani nové testy této odložené změny. Zachovat otevřený záznam T08. |
| R05 | Popsat současný jednodušší installer. | Odstranit slib plně transakčního upgrade/rollback. Testy ověření balíčku a běžného install/uninstall, ne nové transakční crash recovery. |
| R06 | Users mají záměrně povolené čtení datového stromu. | Autorita pro současnou politiku, nepřebírat SYSTEM/Administrators-only. Existující testy této politiky zachovat. |
| R07 | Notes bez odpovídající implementace jsou pro konsolidaci irelevantní. | Nevracet placeholder, destruktivní Reset, vypnutí validace ani starou modalitu. Zachovat zdrojové přiřazení jako vyřazené. |
| R08 | Dynamická high-contrast cache je implementovaný chtěný stav. | Popsat podle kódu, již správné pasáže ponechat. Ověřit změnu palety bez zbytečné obnovy fontů/ikon. |
| R09 | Dirty je výsledkové. Stejný výsledný klíč není změna ani po potvrzeném generování. | Popsat sémantiku modelu včetně disabled klíče, zachovat test návratu na původní hodnotu a doplnit pouze chybějící stejné-hash UI scénáře. |
| R10 | Současných deset hodnot včetně Total size a přepočtených jednotek je chtěný stav. | Aktualizovat client.md a UX07 podle kódu, testovat formátovací hranice a přesný součet před formátováním. Devět službových čítačů zůstává devět, desátá hodnota je odvozená. Samostatný Connected for tato volba výslovně neřeší, proto se nově nedokumentuje. |

Výsledkem této fáze je stále pouze plán. Nejde o autorizaci tohoto plánovacího úkolu měnit kód, testy nebo produktové dokumenty. Nová úplná verze má být plan-v003.md. R04 zůstává jediným explicitně odloženým rozhodnutím z původních deseti bodů.
