---
intent_version: 5
created_at: 2026-09-13-15-07-22
based_on: intent-v004.md
source: intent-chat-v005.md
---

# Povinné vlastní HTTP hlavičky v konfiguraci klienta

## Shrnutí

Rozšířit klientskou konfiguraci o povinný string `masterSetup.httpHeaders`, který po JSON dekódování obsahuje jeden nebo více řádků HTTP hlaviček. Validovaný uživatelský blok se připojí ke každému requestu klienta pro akce health, info a data, včetně runtime, wizard probe a diagnostických variant.

Odesílací cesta zůstane jednoduchá. Aplikace sestaví svůj `Content-Type` a připojí celý předem validovaný uživatelský blok. Konfliktní řádky se při odesílání nefiltrují ani nepřepisují. Místo toho validace odmítne celý konfigurační dokument, pokud uživatel zadá hlavičku vlastněnou aplikací nebo transportem. Stejně odmítne druhý a každý další výskyt stejného uživatelského názvu bez ohledu na velikost písmen.

Implicitní `User-Agent: PHPVPN/1.0` vytvořený WinHTTP relací se odstraní. User-Agent bude pocházet pouze z `httpHeaders`. Výchozí konfigurace jej obsahuje, uživatel jej může změnit nebo odebrat.

## Výchozí dokument a schéma

1. `masterSetup.httpHeaders` je povinný JSON string.
2. Ve výchozím dokumentu jsou první členy `masterSetup` v pořadí `serverSecret`, `serverUrl`, `httpHeaders`, `trustAnyTlsCertificate`, potom zbývající dosavadní členy v jejich relativním pořadí.
3. Pořadí členů JSON není validační ani runtime kontrakt. Přeskládaný dokument musí fungovat stejně.
4. Výchozí dekódovaná hodnota je:

~~~http
Cache-Control: no-cache
Pragma: no-cache
User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:155.0) Gecko/20100101 Firefox/155.0
~~~

5. Fyzický JSON použije mezi řádky escape sekvence `\r\n`.
6. Stejný výchozí dokument se použije při prvním vytvoření konfigurace službou i při resetu nebo explicitním vytvoření v editoru.

## Validace hlaviček

1. Blok se validuje jedním lineárním průchodem řádek po řádku. Tato validace současně kontroluje syntaxi, bezpečnost a zakázané názvy.
2. Přijmout CRLF a LF jako oddělovače. Samostatné CR, vnitřní prázdný řádek, request line, status line, obs-fold, NUL, nepovolené řídicí znaky a možnost vložit tělo odmítnout.
3. Název musí být neprázdný ASCII HTTP token před první dvojtečkou. Hodnota může být prázdná, pokud je její syntaxe bezpečná.
4. Názvy se pro kontrolu kolizí porovnávají ASCII case-insensitive.
5. Zakázané názvy jsou:
   - `Content-Type`, vlastněný aplikací podle `HttpAction`
   - `Content-Length`, vlastněný WinHTTP podle těla
   - `Connection`, vlastněný WinHTTP
   - `Host`, vlastněný WinHTTP podle cílové URL
   - `Transfer-Encoding`, vlastněný transportem a rámováním těla
6. Při výskytu zakázaného názvu se odmítne celý konfigurační dokument. Žádný řádek se potichu nezahodí, žádný částečný blok se neaktivuje a předchozí platná konfigurace zůstane aktivní.
7. Chyba uvede cestu `masterSetup.httpHeaders`, číslo řádku, název hlavičky a skutečnost, že ji spravuje aplikace nebo transport. Do chyby nekopírovat hodnotu hlavičky.
8. `User-Agent`, `Cache-Control`, `Pragma`, `Cookie` a jiné nekolidující vlastní hlavičky zůstávají povolené, každá však nejvýše jednou.
9. Druhý a každý další výskyt stejného názvu hlavičky se odmítne case-insensitive validační chybou. Nic se potichu nededuplicuje, neslučuje ani neřadí. Chyba uvede číslo řádku a již použitý název, ale ne hodnotu.
10. Normalizovat lze pouze oddělovače řádků na CRLF a odstranit jeden volitelný koncový oddělovač.
11. Transport při `configure` znovu ověří vstup, aby bylo bezpečné i přímé použití mimo konfigurační parser.

## Datový tok

1. Validátor načte povinný string a uloží dekódovanou hodnotu do `ConfigValues`.
2. Hodnota se přenese přes `ClientConfig`, `TransportSettings`, runtime `NetworkWorker`, `WizardContext`, IPC roundtrip a `ProbeClient`.
3. Wizard operace měnící jiné položky musí `httpHeaders` zachovat včetně původního JSON dokumentu a pořadí členů.
4. Nepřidávat nové formulářové pole. Stávající JSON editor zajišťuje zobrazení a úpravu.
5. Nezavádět automatickou migraci starých dokumentů bez povinné položky.

## Sestavení requestu a diagnostika

1. Zachovat aplikační `Content-Type` a transportní `Connection` a `Content-Length`.
2. WinHTTP relaci otevřít bez implicitního aplikačního User-Agentu.
3. Pro health, info a data připojit celý normalizovaný uživatelský blok k aplikačním hlavičkám.
4. Hlavičky nesmějí změnit metodu, URL, tělo ani rámování requestu.
5. Diagnostika zobrazí skutečně použitý nebo korektně rekonstruovaný aplikační blok všech jedinečně pojmenovaných uživatelských hlaviček a bez odstraněného `PHPVPN/1.0`.
6. Health s vlastními hlavičkami musí nadále ověřit serverový timestamp a publikovat časový snapshot.
7. NeASCII hodnoty musí mít definovaný a wire testem ověřený převod. Nepoužívat systémovou code page ani převod, který zdvojí UTF-8 kódování.

## Dotčené oblasti

- výchozí konfigurace a konfigurační validátor
- `ConfigValues`, `ClientConfig`, `TransportSettings` a jejich převody
- runtime network worker
- wizard dokument, `WizardContext`, IPC a probe
- WinHTTP transport
- diagnostická rekonstrukce
- unit, contract, runtime, wizard, IPC a raw wire testy

## Akceptační kritéria

1. Nová nebo resetovaná konfigurace obsahuje povinné `httpHeaders` se třemi přesnými výchozími hlavičkami a předepsaným výchozím pořadím členů.
2. Přeskládání JSON členů nemění validitu ani význam.
3. Chybějící položka, jiný JSON typ nebo syntakticky nebezpečný blok vyvolá jednoznačnou validační chybu.
4. Každý ze zakázaných názvů v libovolné velikosti písmen odmítne celý dokument s názvem a číslem řádku.
5. Po odmítnutí se konfigurace neuloží ani neaktivuje a předchozí platná konfigurace zůstane zachována.
6. Druhý výskyt libovolného názvu, včetně jiné velikosti písmen, odmítne celý dokument s jednoznačnou chybou a číslem řádku.
7. Každá povolená vlastní hlavička dorazí při health, info i data requestu přesně jednou a se svou hodnotou.
8. Výchozí request obsahuje User-Agent z konfigurace a nikdy implicitní `PHPVPN/1.0`.
9. Odebrání User-Agentu z uživatelského bloku nezpůsobí jeho návrat.
10. Aplikační a transportní hlavičky zůstanou funkční jako dosud.
11. Wizard, IPC, probe a diagnostika používají stejný blok a editor jej zachová.
12. Health nadále zajišťuje ověřenou synchronizaci času.
13. Automatické testy pokryjí výchozí dokument, validaci, všech pět zakázaných názvů, case-insensitive porovnání zakázaných i duplicitních názvů, všechny tři akce, wizard, IPC, diagnostiku a skutečné wire chování.
14. Celá testovací sada projde v podporovaných konfiguracích bez regresí.



