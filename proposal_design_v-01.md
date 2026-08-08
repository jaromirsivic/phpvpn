# Návrh úpravy V-01: transport není součástí aplikačního rozhodování

## Rozhodnutí

`sync` musí fungovat shodně přes přímé HTTP, přímé HTTPS i přes libovolnou reverzní proxy. Aplikace nesmí podle protokolu, `REMOTE_ADDR`, `HTTPS`, portu ani žádné proxy hlavičky měnit přijetí požadavku, autentizaci, oprávnění, HMAC, replay kontrolu, fronty, odpověď nebo návratový HTTP kód.

Toto řešení nezavádí žádný nový konfigurační parametr. Uživatel nebude volit HTTP, HTTPS ani proxy a nebude nikde povolovat proxy. `X-Forwarded-Proto` se nebude číst ani ukládat. Proxy je z pohledu PHP VPN transparentní síťová vrstva.

## Změny implementace

1. Odstranit `REQUIRE_HTTPS` a `TRUST_PROXY_HEADERS` z konfiguračního bloku i z validace konfigurace.
2. Odstranit `Http::isHttps()` a veškeré čtení `HTTPS`, `SERVER_PORT` a `HTTP_X_FORWARDED_PROTO`, pokud je jejich jediným účelem určení transportu nebo povolení requestu.
3. Z `App::assertPreconditions()` odstranit větev vracející `403 https_required`. Preconditions nadále kontrolují pouze metodu, Content-Type, velikost těla a ostatní aplikační limity.
4. Z health, diagnose, selftestu, logů a dokumentace odstranit transportní stav, proxy konfiguraci a výsledek HTTPS kontroly. Tyto výstupy nesmí pro `sync` vytvářet `pass`, `warn` ani `fail` podle schématu.
5. Neměnit canonical HMAC: obsahuje metodu, akci, klienta, timestamp, nonce a hash datové části těla; neobsahuje schéma, host, port, vzdálenou adresu ani proxy hlavičky.

## Chování

| Cesta požadavku | Chování PHP VPN |
|---|---|
| Přímé HTTP | Platný podepsaný `sync` se zpracuje běžně. |
| Přímé HTTPS | Stejné aplikační chování jako u HTTP. |
| HTTP nebo HTTPS přes proxy | Stejné aplikační chování; proxy hlavičky se ignorují. |
| Podvržené `X-Forwarded-Proto` | Nemá žádný účinek, protože jej aplikace nečte. |

## Bezpečnostní důsledky

HMAC nadále chrání autenticitu a integritu requestu. Access token ani `SERVER_SECRET` se po síti neposílají. Volitelná persistence nonce dál určuje, zda server odmítne replay požadavku.

Při HTTP však aplikace neposkytuje důvěrnost přenosu, autentizaci serveru ani ochranu odpovědi proti aktivnímu útočníkovi na trase. Toto je vědomé rozhodnutí uživatele. Neřeší se aplikací ani přepínačem; kdo tyto vlastnosti potřebuje, zajistí je mimo PHP VPN pomocí HTTPS nebo soukromé sítě.

## Testy

1. Stejný platný `sync` přes HTTP, HTTPS a proxy musí vracet stejný aplikační výsledek.
2. Přidání, odebrání nebo změna `X-Forwarded-Proto` nesmí změnit status, autentizaci, frontu, `sync-result` ani logický obsah odpovědi.
3. `403 https_required` se nikde nevrací.
4. Chybný HMAC, změněné tělo, neplatný timestamp, replay podle režimu nonce, chybný Content-Type a nadlimitní tělo mají stejné chování ve všech transportních cestách.
5. Selftest ani health neobsahují požadavek, varování nebo konfiguraci vztahující se k HTTPS či proxy.

## Akceptační kritérium

V-01 je vyřešeno, když lze použít čisté HTTP bez konfigurace, HTTPS bez konfigurace i proxy bez konfigurace; aplikace ve všech případech ignoruje transportní a proxy metadata a řídí se výhradně aplikačním protokolem a HMAC.
