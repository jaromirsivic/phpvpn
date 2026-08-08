# Revize návrhu V-01

Tato revize má přednost před dřívější formulací návrhu V-01, která uvažovala konfiguraci důvěryhodných proxy.

## Závazné rozhodnutí

PHP VPN **nesmí zavést žádný nový konfigurační parametr** pro volbu HTTP, HTTPS, proxy, důvěryhodné proxy, `X-Forwarded-Proto` ani transportní režim.

Protokol se má pro účely aplikace ignorovat:

- `sync` musí stejným způsobem přijmout HTTP i HTTPS;
- `sync` musí stejným způsobem fungovat při přímém spojení i za proxy;
- `X-Forwarded-Proto` ani jiné proxy hlavičky se nesmí číst, vyhodnocovat, logovat ani použít pro rozhodnutí;
- HTTP, HTTPS, port, `REMOTE_ADDR` a stav proxy nesmí ovlivnit autentizaci, HMAC, replay ochranu, fronty, odpověď ani HTTP status;
- `403 https_required` musí zaniknout a nesmí být nahrazen jiným blokováním podle transportu.

HMAC, timestamp, nonce a ostatní aplikační kontroly zůstávají beze změny a jsou nezávislé na transportu.

Čisté HTTP je vědomě podporovaný režim. Nezajišťuje důvěrnost, ověření serveru ani ochranu odpovědi na síti; tyto vlastnosti případně zajišťuje infrastruktura mimo PHP VPN. Aplikace kvůli nim nesmí zavádět přepínač ani omezovat provoz.
