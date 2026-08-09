# Formální zadání: akce `stats` bez OpenSSL

## Cíl

Přidat akci `?action=stats`, která vrací šifrovaný obsah
`QUEUE_ROOT/stats.json`. Implementace nesmí používat OpenSSL ani jiná
nepovinná PHP rozšíření.

## Statistiky při `sync`

Po úspěšné autorizaci `sync` server pracuje se souborem
`QUEUE_ROOT/stats.json`.

- Záznam mapuje VPN IP adresu na UTC čas posledního úspěšného `sync`.
- Pro aktuálně autorizovanou IP se čas zapíše jen tehdy, pokud záznam chybí
  nebo je starší než 30 sekund.
- Staré záznamy se nemažou.
- Souběžné zápisy se nezamykají; platí poslední zápis vyhrává.
- Selhání aktualizace statistik nesmí zneplatnit úspěšný `sync`.
- `health` a `stats` soubor nemění.

## Autorizace `stats`

`stats` je akce `POST`. Nevyžaduje VPN IP ani access token klienta; volající
prokazuje znalost `SERVER_SECRET`.

Požadavek obsahuje UTC timestamp, náhodný nonce a HMAC-SHA-256 podpis nad:

```text
POST
stats
timestamp
nonce
```

Klíčem podpisu je `SERVER_SECRET`. Server ověří formát, krátké časové okno
a podpis přes `hash_equals`. Secret se nikdy neposílá ani neloguje.

## Odvození klíče

AES klíč se nesmí rovnat `SERVER_SECRET` ani vzniknout prostým zřetězením
hodnot.

Implementovat `StatsCrypto::deriveKey()` čistě v PHP pomocí HKDF-SHA-256
postaveného na `hash_hmac`:

- vstupní tajemství: `SERVER_SECRET`;
- salt: SHA-256 kanonického autorizačního requestu;
- context: `phpvpn/stats-response/v1`;
- výstup: přesně 32 bajtů.

## Softwarové AES-256-GCM

Implementovat lehkou čistě-PHP komponentu `StatsCrypto`, bez OpenSSL
a bez Composeru:

- `encrypt(string $plaintext, string $key, string $aad): array`;
- AES-256 pro šifrování bloků;
- GCM pro autentizované šifrování;
- GHASH pro ověření integrity;
- náhodný samostatný 12bajtový IV pro každou odpověď přes `random_bytes`;
- 16bajtový autentizační tag;
- AAD tvoří kanonický autorizační request, aby odpověď nešla připojit
  k jinému requestu.

Odpověď akce `stats` má tvar:

```json
{
  "alg": "AES-256-GCM",
  "iv": "base64-iv",
  "ciphertext": "base64-ciphertext",
  "tag": "base64-authentication-tag"
}
```

Klient odvodí stejný klíč, ověří tag a až poté zpracuje dešifrovaný JSON.

## Bezpečnostní požadavky

- Nepoužívat AES-CBC, ECB ani šifrování bez autentizačního tagu.
- Nikdy znovu nepoužít stejnou kombinaci klíče a IV.
- Při neplatném tagu se nesmí vrátit žádný částečně dešifrovaný obsah.
- Nezapisovat secret, odvozený klíč, IV, auth nonce, podpis, plaintext ani
  ciphertext do logů.
- Čistý HTTP zůstává podporovaný; šifrování odpovědi chrání její obsah, ale
  nenahrazuje ochranu proti zdržení, dropu nebo replayi na transportní vrstvě.

## Ověření

- Ověřit AES-256-GCM proti standardním známým testovacím vektorům.
- Ověřit HKDF-SHA-256 proti známým testovacím vektorům.
- Ověřit úspěšné šifrování a dešifrování prázdných i běžných statistik.
- Ověřit selhání při změně ciphertextu, tagu, IV nebo AAD.
- Ověřit odlišný IV pro každé dvě odpovědi.
- Ověřit, že `stats` bez platné autorizace vrací `401` a nikdy nevrací
  statistiky v otevřeném tvaru.
