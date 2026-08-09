# phpvpn

Relay pro end-to-end šifrovanou IPv4 VPN v **jediném souboru PHP**. Server šifrovaný paket nikdy nedešifruje, pouze ověří oprávnění volajícího a přesune neprůhledná data mezi diskovými frontami jednotlivých klientů.

- Jeden soubor `phpvpn.php`, PHP 8.2 a novější.
- Bez databáze, bez Composeru, bez cronu, bez shellu.
- Bez jediného nepovinného rozšíření PHP. Stačí `hash`, `json`, `pcre`, `random`, `date`, `SPL` a `standard`.
- Žádná dlouhá spojení. Klient opakuje krátký `POST ?action=sync` ve vlastním adaptivním intervalu.

Tento repozitář obsahuje **jen serverovou část**. Klientskou virtuální síťovou kartu lze implementovat nezávisle podle [PROTOCOL.md](PROTOCOL.md).

| Dokument | Obsah |
|---|---|
| [design2.md](design2.md) | Cílová architektura |
| [problems.md](problems.md) | Analýza nasaditelnosti na sdílený hosting |
| [plan.md](plan.md) | Implementační plán a odůvodnění rozhodnutí |
| [PROTOCOL.md](PROTOCOL.md) | Závazná specifikace drátového protokolu |

---

## Požadavky na hosting

Server je navržený pro **typický hosting**, nikoli pro konkrétní vyladěné prostředí.

| Vlastnost | Požadavek |
|---|---|
| PHP | 8.2 nebo novější, libovolné SAPI. 64bitový build je potřeba jen pro `?action=stats`. |
| Transport | HTTP i HTTPS, server mezi nimi nerozlišuje. Funguje i za proxy. |
| Zapisovatelný adresář | Jeden, ideálně mimo document root |
| `post_max_size` | Nad `MAX_REQUEST_BYTES`, tedy nad 1 MiB při výchozím nastavení |
| `memory_limit` | Doporučeno alespoň 64 MiB |
| `max_execution_time` | Doporučeno alespoň 30 s |
| Souborový systém | Lokální disk se spolehlivým `flock`. Síťové FS nejsou podporovaný provozní model. |

Nevyžaduje se OpenSSL, cURL, mbstring, sockets, FFI, APCu, OPcache ani konkrétní webserver. `fastcgi_finish_request()` se využije, pokud existuje, ale jeho absence nic nerozbije.

### Transport

Server transport vědomě ignoruje. Schéma, port, adresa protistrany a hlavičky typu `X-Forwarded-Proto` se nečtou, nevyhodnocují ani nelogují a neovlivňují autentizaci, HMAC, ochranu proti replay, fronty, odpověď ani HTTP status. Prostý HTTP i HTTPS, přímé spojení i provoz za proxy vedou ke stejnému chování, takže server nemá žádný přepínač, kterým by šlo provoz podle transportu omezit.

Čisté HTTP je tedy vědomě podporovaný režim. Nezajišťuje důvěrnost, ověření serveru ani ochranu odpovědi na síti. Pokud tyto vlastnosti potřebujete, zajistěte je infrastrukturou před PHP VPN, typicky TLS na hostingu nebo na reverzní proxy. Užitečná zátěž paketů je end to end šifrovaná mezi klienty nezávisle na tom, server ji nikdy nedešifruje.

---

## Instalace

### 1. Vygenerujte přihlašovací údaje

```bash
php deploy/gen-credentials.php 8
```

Skript vypíše `SERVER_SECRET` a tabulku `ACCESS_TOKENS` připravenou k vložení. Každý klient dostane pouze svou adresu a svůj token. **Token se po síti nikdy nepřenáší**, slouží jen k odvození podpisu.

### 2. Vyplňte konfigurační blok

Otevřete `phpvpn.php` a upravte pouze první blok. Nic dalšího se needituje.

```php
const SERVER_SECRET = 'k7Qv2ZpL...';                 // alespoň 32 znaků
const QUEUE_ROOT    = '/home/ucet/private/phpvpn';   // ideálně MIMO document root
const ACCESS_TOKENS = [
    '10.99.99.2' => '4f1c...',
    '10.99.99.3' => '9ab7...',
];
```

`QUEUE_ROOT` je nejdůležitější volba celé instalace. Prázdná hodnota znamená adresář `phpvpn` vedle skriptu, tedy uvnitř document rootu, a spoléhá se pak jen na pravidlo webserveru. Absolutní cesta mimo document root je bezpečnější, protože ji nelze omylem zpřístupnit chybnou konfigurací.

### 3. Nahrajte soubor a zabezpečte webserver

Nahrajte `phpvpn.php` do webového adresáře.

- **Apache:** zkopírujte `deploy/apache.htaccess` vedle skriptu jako `.htaccess`.
- **nginx:** převezměte pravidla z `deploy/nginx.conf`. nginx `.htaccess` ignoruje, takže bez toho by byl `QUEUE_ROOT` uvnitř webrootu veřejný.

### 4. Ověřte, že server běží

Otevřete `https://vas-server/phpvpn.php` v prohlížeči. Musí se zobrazit **Server is running**. Pokud se objeví červené varování, `SERVER_SECRET` není správně nastavený a `sync` bude vracet `503`.

```bash
curl https://vas-server/phpvpn.php?action=health
# {"version":"1.0.0","status":"OK","serverTime":"2026-08-08T14:20:00Z"}
```

### 5. Spusťte preflight

Před nasazením provozu ověřte prostředí přímo na cílovém hostingu. Preflight je ve výchozím stavu vypnutý, aby nebyl trvale dostupný.

1. Nastavte `const SELFTEST_ENABLED = true;`.
2. Odešlete podepsaný `POST` na `?action=selftest`. Podpis se sestavuje stejně jako u `sync`, jen s akcí `selftest`, viz [PROTOCOL.md](PROTOCOL.md) kapitola 4.
3. Zkontrolujte, že žádná kontrola nemá výsledek `fail`.
4. **Nastavte `SELFTEST_ENABLED` zpět na `false`.**

Preflight ověří verzi PHP, dostupnost funkcí, šířku celých čísel, známé testovací vektory AES-256-GCM a HKDF-SHA-256, atomické publikování dávky, `flock`, volné místo, `post_max_size`, `memory_limit`, `disable_functions` a umístění `QUEUE_ROOT` vůči document rootu.

---

## Provoz

### Co vzniká na disku

```text
QUEUE_ROOT/
    .htaccess                              obranné pravidlo navíc
    index.html                             prázdný, blokuje výpis adresáře
    stats.json                             čas posledního sync každého klienta
    10_99_99_0/004/                        fronta klienta 10.99.99.4
        queue.lock                         zámek fronty
        queue.stat                         počet souborů, bajtů a čas poslední údržby
        <batchId>.data                     publikovaná, dosud nevyzvednutá dávka
        <batchId>.sending                  vydaná dávka čekající na ack
    _nonce/004/<bucket>.bin                jen při PERSIST_NONCES = true
    _state/global.stat                     agregovaná kvóta celé instalace
    _logs/YYYY-MM-DD.log                   strukturovaný log, rotovaný podle stáří
```

### Údržba

Bez cronu se údržba dělá výhradně při běžném provozu. Každý požadavek uklidí frontu, se kterou pracuje, a po odeslání odpovědi navíc provede malý, časově omezený díl globálního úklidu. Nic se nespouští ručně.

Nevyzvednutá dávka se po `QUEUE_TTL_SECONDS` zahodí. Jde o vědomou a omezenou vlastnost relay služby, nikoli o chybu.

### Statistiky

Server si u každého úspěšného `sync` poznamenává do `QUEUE_ROOT/stats.json` UTC čas, kdy klienta naposledy viděl. Záznam se pro jednu adresu obnovuje nejvýše jednou za `STATS_MIN_UPDATE_SECONDS`, výchozí 30 sekund, staré záznamy se nemažou a selhání zápisu nikdy nezneplatní už odbavený `sync`. Zapisuje se až po odeslání odpovědi, takže to klienta nezdržuje.

Přehled si vyzvednete podepsaným `POST` na `?action=stats`. Podpis se sestavuje ze `SERVER_SECRET`, nikoli z tokenu klienta, takže tuto akci může volat jen správce. Odpověď je vždy zašifrovaná pomocí AES-256-GCM klíčem odvozeným z konkrétního požadavku, i na čistém HTTP. Tvar požadavku, odvození klíče a povinné ověření autentizačního tagu popisuje [PROTOCOL.md](PROTOCOL.md) kapitola 13, referenční klient je v [tests/lib/StatsClient.php](tests/lib/StatsClient.php).

Šifrování je napsané přímo v PHP, aby nezáviselo na OpenSSL. Vyžaduje proto 64bitové PHP. Na 32bitovém buildu vrací `?action=stats` `503 server_misconfigured` a nic dalšího to neovlivní.

### Logy

`LOG_LEVEL` je ve výchozím stavu `error`. Hodnota `info` zaznamená každý `sync` a hodí se při ladění, ale na produkci zbytečně zatěžuje disk. Log nikdy neobsahuje token, podpis, nonce, secret ani obsah paketů.

### Ladění výkonu

Nejdřív změřte, pak měňte. Užitečná vodítka:

| Situace | Nastavení |
|---|---|
| Klienti hlásí často `429 queue_full` | Zvyšte `MAX_BUFFERS`, nebo zkraťte interval klientů, aby si data brali dřív. |
| Hosting hlásí vyčerpanou inode kvótu | Snižte `MAX_BUFFERS` a `MAX_TOTAL_QUEUE_FILES`. |
| Časté `503 queue_busy` | Zvyšte `LOCK_TIMEOUT_MS`, ale nikdy ne nad zlomek `max_execution_time`. |
| Hosting zvládne větší dávky | Zvyšte `MAX_REQUEST_BYTES` a `MAX_ACCEPTED_REQUEST_BYTES`, vždy s rezervou pod `post_max_size`. |
| Vyžadujete tvrdou ochranu proti replay | `PERSIST_NONCES = true`. Počítejte s několika soubory navíc na klienta. |
---

## Testy

Testy nevyžadují Composer, PHPUnit ani žádné rozšíření. Spouští vlastní instance PHP s přepínačem `-n`, takže je lokální `php.ini` neovlivní a testy vidí výchozí hodnoty typického hostingu.

```bash
php tests/run.php                      # vše
php tests/run.php unit integration     # jen vybrané skupiny
php tests/run.php --filter=nonce       # jen testy s daným řetězcem v názvu
```

| Skupina | Co ověřuje |
|---|---|
| `compat` | Že zdroj zůstává v jazykovém základu PHP 8.2 a nesahá na nepovinné rozšíření |
| `unit` | Validace protokolu, identifikátory dávek, fronta a garbage collection |
| `integration` | Router, autorizace, obousměrný cyklus, kapacita a preflight, přes skutečné HTTP |
| `concurrency` | Souběh nad několika nezávislými PHP procesy sdílejícími jednu frontu |
| `fault` | Zotavení z pádu, poškozeného stavu a nedostupného úložiště |

Skupina `concurrency` startuje čtyři samostatné servery nad jedním `QUEUE_ROOT`. Jediný proces by souběh prokázat nemohl.

---

## Řešení potíží

| Příznak | Příčina a náprava |
|---|---|
| Stránka ukazuje červené varování | `SERVER_SECRET` je kratší než 32 znaků. Vygenerujte jej přes `deploy/gen-credentials.php`. |
| `sync` vrací `503 server_misconfigured` | Chybný secret, nebo si limity v konfiguraci odporují, například `MAX_ACCEPTED_REQUEST_PACKETS` nad `MAX_REQUEST_PACKETS`. |
| `sync` vrací `503 queue_unavailable` | `QUEUE_ROOT` nelze vytvořit nebo do něj zapsat. Zkontrolujte práva, `open_basedir` a volné místo. |
| `sync` vrací `401` i se správným tokenem | Rozjeté hodiny klienta. Klient si má čas srovnat podle `?action=health`. |
| Klient dostává pakety dvakrát | Očekávané chování. Doručení je at-least-once, klient musí deduplikovat podle `uuid`. |
| Obsah fronty je dostupný z webu | `QUEUE_ROOT` je uvnitř document rootu a webserver nemá pravidlo. Přesuňte jej ven, nebo nasaďte `deploy/`. |
