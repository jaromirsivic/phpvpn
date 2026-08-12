# Poznámky k návrhu klienta phpvpn

## Platformy a technologie

- Klient bude primárně určený pro Windows; sekundárně musí být přenositelný na macOS a Linux.
- Návrh má proto oddělit platformně závislou vrstvu od znovupoužitelného jádra.
- Primární implementační jazyk bude C++ nebo čisté C; volba se rozhodne podle vhodnosti pro implementaci.
- Ve Windows bude klient používat TUN/TAP adaptér.

## Konfigurace

Klient načte konfigurační JSON. Povinná část může nést pracovní název `masterSetup` a bez ní se klient nesmí spustit.

Musí obsahovat alespoň:

- úplnou URL adresu serverového souboru `phpvpn.php`; podporované jsou HTTP i HTTPS;
- přepínač pro důvěru HTTPS certifikátům. Výchozí hodnota je `true` a znamená přijetí libovolného certifikátu pro danou URL bez ověření důvěryhodné certifikační autority. Toto bezpečnostní omezení musí být výslovně zdokumentováno;
- `serverSecret`;
- virtuální IP adresu tohoto klienta;
- přístupový klíč/token klienta (`accessKey`).

Pod povinnou částí budou `communicationContracts`. Každý kontrakt určí IP adresu protistrany a společný komunikační klíč/heslo pro oba směry. Tento klíč se používá k šifrování i dešifrování paketů mezi danou dvojicí klientů.

## Navázání spojení

Po načtení konfigurace klient postupuje po krocích:

1. Zavolá endpoint `health`, aby ověřil dostupnost serveru.
2. Z úspěšné odpovědi ověří kryptografickou autenticitu a tím správnost `serverSecret`.
3. Z aktuálního času serveru vypočítá lokální `timeDelta`; lokální systémový čas nemění. Následné autentizované požadavky používají takto určený čas serveru s bezpečnostní rezervou tak, aby klient nebyl více než přibližně jednu sekundu za serverem.
4. Čas serveru má být pro tento účel poskytován s milisekundovou přesností. Současná specifikace vrací sekundy, takže tato změna bude vyžadovat sladění se serverovým protokolem.
5. Po ověření `health` zavolá `info`, načte doporučené limity a provozní parametry, uloží je v paměti a pokud je spuštěn z příkazové řádky, zobrazí je v logu. Zejména maximální velikost těla požadavku je nutné dodržet; ostatní doporučení, například intervaly požadavků, nejsou nutně závazná.
6. Teprve po úspěšném získání a ověření stavu serveru a odpovědi `info` začne číst a bufferovat IP pakety z virtuálního rozhraní.

## Provozní výstup

Klient spuštěný z příkazové řádky má srozumitelně hlásit jednotlivé fáze: cílovou adresu, dostupnost serveru, úspěšné ověření serverového tajemství a přijaté provozní informace a limity.

## Otevřené body k rozhodnutí

- Finální volba C++ versus čisté C.
- Přesná velikost časové rezervy: jedna sekunda nebo jedna milisekunda.
- Změna formátu serverového času na milisekundy a její dopad na normativní specifikaci.
- Konečný název a přesná struktura kořenové konfigurační sekce.
