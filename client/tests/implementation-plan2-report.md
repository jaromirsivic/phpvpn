# Ověření implementace plan2_client.md

Ověřeno 4. září 2026 na Windows x64 pomocí MSVC 19.44, CMake a Ninja.
Implementace byla provedena přímo v `C:/git/phpvpn`. Původní změny specifikace,
plánů a poznámek zůstaly zachované. Serverový wire protokol nebyl změněn.

## Implementovaný rozsah

- Služba publikuje tři uchované výsledky bootstrapu, devět globálních čítačů
  a bezpečnou anglickou chybu s kódem a obnovitelností. Recovery zachovává
  výsledky původního bootstrapu. Nový logický Connect a dokončený Disconnect
  atomicky resetují příslušný snímek.
- Souběžné příkazy Connect sdílejí přijetí jediného pokusu. Potvrzení čeká pouze
  na zveřejnění stavu, nikoli na HTTP. Callback health publikuje úspěch Server URL
  před odvozováním klíče a kryptografickým ověřením.
- Účtování zahrnuje předané requesty, opakované odeslání paketů a přijaté bajty
  i při selhání. Odpověď se zaúčtuje právě jednou před resetem. WinHTTP navíc
  odmítá předčasný konec těla deklarovaného pomocí Content-Length.
- IPC a tray přenášejí celý rozsah uint64. Parser nevyžaduje pořadí členů JSON,
  kontroluje úplnost snímku a neplatnou odpovědí nepřepisuje poslední platný stav.
- Tray obsahuje progress s vlastním 500ms pollingem, Statistics a opravený
  editor s klientskou plochou 1080 × 560 při 96 DPI. Pětisekundový timer zůstává
  oddělený. IPC příkazy neblokují UI. Save chrání generace okna a po dobu čekání
  nepovoluje další editaci, Cancel zůstává dostupné.
- Tři ikony obsahují osm samostatných rasterizovaných velikostí, bílé neprůhledné
  pozadí a černý rámeček. Distribuce obsahuje licenci i NOTICE Material Symbols.
  Původ a licence byly ověřeny v [upstream repozitáři](https://github.com/google/material-design-icons/blob/master/LICENSE).

## Provedené automatické kontroly

| Kontrola | Výsledek |
|---|---|
| x64 Debug, všechny tři aplikace a CTest | 151 testů, 0 chyb |
| x64 Release, všechny tři aplikace a CTest | 151 testů, 0 chyb |
| x64 AddressSanitizer, všechny tři aplikace a CTest | 151 testů, 0 chyb |
| `uv run client/tools/make_icons.py` | Vygenerovány tři ICO |
| `uv run client/tools/check_icons.py` | Všech 24 rámců prošlo |
| `client/tools/make-dist.ps1` | Vytvořen x64 balíček `dist`, verze 1.0.0 |
| Kontrola balíčku | 11 souborů, délky a SHA-256 odpovídají manifestu, obě oznámení přítomna |
| Podpis manifestu | RSA-PKCS1-SHA256 ověřen přibaleným veřejným certifikátem |
| `git diff --check` | Bez chyb whitespace |

Nové testy používají produkční engine, síťového pracovníka a CNG s řízenými
odpověďmi. Pokrývají recovery, chyby jednotlivých kroků, souběžné Connect,
čtení stavu při 200 ověřovaných paketových pollingách, saturaci, retransmit
100bajtového paketu a cancellation před resetem. Samostatné integrační testy
posílají skutečný výstup služby do parseru tray včetně hranic INT64_MAX a UINT64_MAX.

Šest testů skutečného WinHTTP používá pouze privátní loopback HTTP server
na dočasném portu. Ověřují HTTP 200 a 503, přečtený blok přes limit, částečné
tělo při timeoutu, zrušení, předčasném zavření spojení a TCP resetu.
Nepřistupují k instalované službě, tunelu ani externímu VPN serveru.

## Omezení a neprovedené kontroly

ARM64 preset byl vyzkoušen, ale konfigurace nemohla dokončit test kompilátoru.
V instalaci MSVC chybí `Hostx64/arm64/cl.exe` a knihovny `lib/arm64`, konkrétně
`MSVCRTD.lib`. ARM64 sestavení ani běh proto nebyly ověřeny.

Ruční DPI, vykreslování, oba posuvníky ve skutečném okně, více interaktivních
relací a lifecycle instalované služby nebyly provedeny. Zůstávají označené
**NOT RUN** v [ručním seznamu](manual/README.md). Testy čistých modelů nejsou
náhradou těchto kontrol.

Distribuce používá existující vývojový certifikát pro podpis manifestu.
Nebyla provedena instalace, restart živé služby, commit ani push.
