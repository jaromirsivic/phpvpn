# Ověření plánu v3

Ověřeno 2026-09-12 v časové zóně Europe/Berlin. Výstup: [plan-v003.md](plan-v003.md), úplná neměnná verze navazující na intent-v004 a rozhodnutí R01–R10.

- Všech 29 tematických záznamů existuje a obsahuje dopad na konstrukci testů. Jednadvacet dotčených záznamů má aktuální rozhodnutí nad zachovanou historickou analýzou.
- Plán obsahuje 18 kroků D00–D17, 5 kroků B01–B05 a 20 položek TT01–TT20. R04 zůstává odložené, ostatní rozhodnutí jsou zapracovaná.
- Kontrola Markdown odkazů ve složce Plans nenalezla neexistující lokální cíl.
- git diff --check skončil s návratovým kódem 0. Git pouze upozornil na budoucí převod LF na CRLF.
- Diff proti HEAD pro client, client.md, protocol.md, tray_UX.md, šestnáct původních zdrojů a plan-v001/v002 je prázdný. Změny tohoto úkolu jsou pouze v jeho složce Plans. Marker a nové intent soubory jsou změny koordinátora.
- B větev výslovně plánuje budoucí kapacitní kontrolu před data requestem, opravu helperu pro existující i nový peer, interval 20–5000 ms a povinné testy odkladu/obnovení a účetnictví. Žádná z těchto změn není tímto reportem prohlášena za implementovanou.
- Buildy, unit/integration testy, GUI, služba a installer: **NOT RUN**, tento úkol pouze plánuje. Statické přečtení testu není výsledkem jeho běhu.

Dokumentační realizace má vlastní akceptaci V01–V06. Dodání backpressure se potvrzuje zvlášť až po B05. Tento report neautorizuje odstranění zdrojů ani realizaci odloženého R04.
