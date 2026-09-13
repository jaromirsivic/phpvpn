---
intent_version: 1
created_at: 2026-09-13-08-58-25
source: intent-chat-v001.md
---

# HTTP Error detail v Connect formuláři

## Shrnutí

Rozšířit již implementovanou HTTP diagnostiku z prvních dvou kroků Configuration Wizardu také do běžného Connect formuláře. Pokud pokus o připojení selže během ověření serverové URL nebo SERVER_SECRET a služba skutečně přijala HTTP odpověď, zobrazí Connect formulář v červeném panelu „Cannot connect to the server“ tlačítko „Error detail“. Tlačítko otevře stejný dialog Request/Response se stejným vzhledem, daty a chováním jako ve Configuration Wizardu.

## Kontext projektu

- Configuration Wizard již obsahuje bounded HTTP diagnostiku, přenos diagnostiky přes lokální IPC a nativní okno `HttpErrorDetailWindow` se záložkami Request a Response.
- Běžné připojení probíhá přes `VpnEngine::runConnectSequence`. První dvě relevantní síťové fáze jsou `health` a `info`.
- Tray aplikace získává průběh a bezpečný veřejný popis chyby z runtime status snapshotu přes běžné IPC.
- Connect formulář již zobrazuje červený chybový panel s titulkem „Cannot connect to the server“.

## Požadované chování

### Dostupnost diagnostiky

1. Diagnostiku zachytávat pouze pro HTTP komunikaci ve fázích `health` a `info` běžného pokusu vyvolaného tlačítkem Connect.
2. Tlačítko „Error detail“ zobrazit pouze tehdy, když:
   - aktuální pokus o Connect terminálně selhal ve fázi ověření serverové URL nebo SERVER_SECRET,
   - HTTP komunikace skutečně proběhla,
   - služba přijala HTTP odpověď,
   - odpověď nebylo možné korektně zpracovat nebo ověřit,
   - diagnostika patří přesně k tomuto aktuálnímu pokusu a této chybě.
3. Tlačítko nezobrazovat při lokální validační chybě, DNS chybě, selhání TLS, timeoutu bez odpovědi, zrušení, IPC chybě ani při selhání v pozdějších fázích, například ACCESS_KEY, spuštění tunelu, datový provoz nebo recovery.
4. Úspěšná odpověď z dřívější fáze se nesmí nabídnout jako detail pozdější chyby. Pokud `health` uspěje a `info` selže, zobrazit pouze request a response z `info`.
5. Diagnostiku odstranit při novém pokusu o Connect, úspěšném připojení, odpojení, zrušení, nahrazení výsledku novějším pokusem, zavření příslušného chybového stavu a při ukončení služby nebo tray aplikace. Pozdní výsledek staršího pokusu se nesmí znovu zobrazit.

### Connect formulář

6. Tlačítko umístit na pravou stranu existujícího červeného panelu „Cannot connect to the server“ s běžnou mezerou od textového obsahu a okrajů.
7. Použít stejný červený vizuální styl a přesný popisek „Error detail“ jako ve Configuration Wizardu.
8. Tlačítko musí být dostupné klávesnicí, respektovat DPI, lokalizované rozložení, focus order a high-contrast chování stávajícího Connect formuláře.
9. Panel musí zůstat čitelný a správně zalomený i po přidání tlačítka. Tlačítko nesmí překrývat titulek ani popis chyby.

### Dialog Request/Response

10. Znovu použít stejné `HttpErrorDetailWindow`, nikoli vytvářet druhou vizuálně podobnou implementaci.
11. Dialog otevřít jako okno vlastněné Connect formulářem. Musí obsahovat stejné dvě záložky Request a Response, stejné barvy a ovládání jako dialog otevřený z Configuration Wizardu.
12. Každá záložka obsahuje read-only víceřádkové textové pole přes dostupnou plochu. Dialog nemá patičku s OK nebo Cancel a zavírá se křížkem.
13. Zobrazit aplikační podobu HTTP requestu a response před parsováním. Nezobrazovat TLS bajty ani interní HTTP/2 rámce.
14. Použít beze změny již zavedené pravidlo velikosti a projekce:
    - celý obsah při velikosti nejvýše 64 KiB,
    - při větším obsahu prvních 63 KiB, přesně čtyři samostatné řádky `...` a poslední 1 KiB bez překryvu,
    - původní velikost requestu i response v KiB na dvě desetinná místa,
    - stejné dekódování a bezpečné zobrazení binárních nebo neplatných textových bajtů jako ve Configuration Wizardu.
15. Obsah musí jít označit a kopírovat stejným způsobem jako ve stávajícím diagnostickém dialogu.

## Přenos a životní cyklus dat

16. Služba musí zachytit bounded request a response před jejich parsováním a zachovat je jen pro terminální chybu aktuálního Connect pokusu.
17. Runtime diagnostika musí být jednoznačně korelovaná s identitou nebo generací Connect pokusu. Tray nesmí spojit status chyby s diagnostikou jiného nebo staršího pokusu.
18. Rozšířit běžné service-to-tray IPC tak, aby tray získal volitelnou HTTP diagnostiku společně s odpovídajícím terminálním stavem nebo jiným atomicky korelovaným způsobem. Zachovat striktní validaci schématu a limit IPC zprávy 4 MiB.
19. Při úspěchu nebo při chybě bez přijaté HTTP odpovědi se diagnostický objekt přes IPC neposílá.
20. Raw diagnostický obsah se nesmí zapisovat do logů. Funkce se týká pouze lokální komunikace služby a tray aplikace.
21. Zachytávání se nerozšiřuje na datové requesty ani na další runtime fáze.

## Dotčené oblasti

- Runtime connect sekvence a stavový snapshot služby.
- HTTP transport a síťová vrstva pouze v rozsahu nutném pro bounded diagnostiku fází health/info.
- Běžný IPC protokol služby a tray aplikace, včetně striktního parseru a dokumentace schématu.
- Status model, Connect formulář a jeho chybový panel.
- Sdílené `HttpErrorDetailWindow` a jeho vlastnictví/lifecycle.
- Testy modelu, IPC, runtime, layoutu a nativního UI.
- `client.md` a `protocol.md`.

## Testování

1. Unit testy pro zachycení a ořez přes hranice 65 535, 65 536 a 65 537 bajtů, včetně jediného vynechaného bajtu.
2. Runtime testy pro selhání `health` a `info`, úspěšný `health` následovaný chybným `info`, chyby bez odpovědi a pozdní výsledek staršího pokusu.
3. IPC testy pro platnou diagnostiku, její absenci, chybné schéma, překročení limitu, korelaci pokusu a maximální escapovaný obsah pod 4 MiB.
4. UI testy pro viditelnost a skrytí tlačítka, pravou pozici v panelu, focus order, DPI, otevření, zavření a opětovné otevření dialogu, oba taby a read-only obsah.
5. Regrese stávající diagnostiky Configuration Wizardu a Show Server Info.
6. Lokální integrační test přes Nginx/PHP pro nevalidní odpověď ve fázích health a info, bez trvalé změny produkčního `phpvpn.php`.
7. Best-effort ověření proti známému free hostingu až po úspěšných lokálních testech.

## Akceptační kritéria

1. Při chybné přijaté odpovědi ve fázi `health` nebo `info` zobrazí Connect formulář červené tlačítko „Error detail“ na pravé straně panelu „Cannot connect to the server“.
2. Kliknutí otevře stejný dialog Request/Response jako Configuration Wizard se správnými daty daného Connect pokusu.
3. Při selhání bez HTTP odpovědi a při chybě po fázi `info` se tlačítko nezobrazí.
4. Nikdy se nezobrazí diagnostika předchozího nebo jiného pokusu.
5. Nový Connect, úspěch, odpojení a zánik vlastníka diagnostiku i otevřený dialog korektně invalidují.
6. Ořez, velikosti, textová projekce a limit IPC jsou shodné s již implementovaným chováním Configuration Wizardu.
7. Raw request ani response nejsou v logu.
8. Relevantní buildy a testy projdou. Existující nesouvisející selhání musí být oddělená a doložená proti výchozímu stavu.
9. Dokumentace klienta a protokolu přesně popisuje dostupnost, schéma, korelaci, limity a cleanup diagnostiky.

## Realizační směr

- Bez samostatného plánu.
- Po revizi tohoto intentu předat přímou realizaci stejné existující úloze, která implementovala HTTP diagnostiku Configuration Wizardu.
- Pracovat přímo na aktuální větvi `tray_UX`, bez worktree a bez commitu, pokud uživatel neurčí jinak.
- Maximálně znovu použít existující datové typy, bounded capture, parser, layout utility a `HttpErrorDetailWindow`.
- Nový kód nesmí oslabit striktní IPC validaci ani rozšířit diagnostiku na běžný datový provoz.
