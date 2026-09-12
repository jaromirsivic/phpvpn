# Ověření plánovacího výstupu

Ověřovaná úplná verze: [plan-v002.md](plan-v002.md). Verze v1 zůstala zachovaná, v2 opravila přesnou cestu scheduler.cpp na client/src/core/queues/scheduler.cpp.

Kontrola 12. 9. 2026 po uložení v2:

| Kontrola | Zjištění |
|---|---|
| Frontmatter | plan_version je číselně 2, created_at je místní čas, source odkazuje na intent-v003, závislosti v1/v2 jsou vysvětlené v úvodu. |
| Tematické záznamy | 29 souborů T01–T29, žádnému nechybí Dopad na konstrukci testů. |
| Relativní odkazy plánu | 0 chybějících lokálních cílů. |
| Explicitní existující kódové cesty v plánu | 0 chybějících cest. Zápisy s globem nebo příponovým shorthandem nebyly tímto automatickým checkem hodnoceny. Navržený nový test loggeru není tvrzen jako existující. |
| Názvy existujících testů v tématech | Porovnány s inventářem client/tests, žádný chybějící název test_*.cpp nebo test_*.ps1. |
| Pokrytí zdrojů | Samostatný index všech 16 zdrojů a jejich částí, včetně R01–R10, neimplementovaných bodů a nerelevantního S12. |
| git diff --check | Exit 0, bez hlášení. Tato kontrola se vztahuje na sledované změny, ne na dosud untracked Plans. |
| git diff --name-only | Prázdný, sledované soubory včetně kódu, testů, cílů a historických zdrojů nezměněné. |
| git status --short | Pouze untracked .intent-builder-active, Intents/ a Plans/. První dvě položky existovaly již před touto plánovací prací. |
| Build, unit, UI, HTTP, SCM, Wintun, install | NOT RUN, v této fázi nebyly autorizovanou realizační prací. |

Kontrola neuzavírá produktové rozpory. R01–R10 zůstávají k rozhodnutí před změnou dotčených tvrzení. Nesporné dokumentační kroky lze vybrat k realizaci odděleně od testovacích změn. Koordinátor vlastní intent marker a rozhodnutí o další realizaci, plánovací úkol jej nepřepsal.
