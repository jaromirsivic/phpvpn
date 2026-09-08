# 002 - Nahrazení všech checkboxů moderními přepínači

## Cíl

Nahradit ve všech oknech tray aplikace vizuálně zastaralé nativní checkboxy jednotným moderním dvoustavovým switchem podle referenčního návrhu. Změna je vizuální a nesmí měnit význam ani chování jednotlivých voleb.

## Rozsah

Úprava platí pro všechny checkboxy v celé tray aplikaci, včetně:

- **Use default server secret** v Configuration Wizardu.
- **Automatically detect IP address** v Configuration Wizardu.
- **Use default access key** v Configuration Wizardu.
- **Add missing default communication contracts** v Configuration Wizardu.
- všech přepínačů ve sloupci **Enabled** v tabulce Communication Contracts, včetně dynamicky vytvořených řádků.
- dalších checkboxů, pokud se při implementaci v tray aplikaci objeví a mají stejnou dvoustavovou boolean sémantiku.

## Požadovaný vzhled

- Přepínač bude mít podobu zaobleného vodorovného pouzdra s kruhovým jezdcem podle cílového referenčního mockupu.
- Zapnutý stav bude mít modré pozadí a jezdec na pravé straně.
- Vypnutý stav bude mít neutrální šedé pozadí a jezdec na levé straně.
- Zakázaný stav musí být na první pohled rozpoznatelný, ale stále musí být zřejmé, zda je hodnota zapnutá nebo vypnutá.
- Ve wizardu zůstane současný textový label vedle switche. Text se nesmí vložit dovnitř přepínače.
- V tabulce Communication Contracts se label neopakuje v každém řádku. Vizuální význam určuje existující záhlaví sloupce **Enabled** a přístupný název switche musí navíc jednoznačně určit příslušný řádek nebo remote IP.
- Kliknutí na switch i jeho label přepne hodnotu, pokud je ovládací prvek povolený.
- Rozměry, odsazení, barvy a proporce budou jednotné ve wizardu i v Communication Contracts.
- Vzhled musí korektně škálovat podle DPI a nesmí se rozpadnout při změně velikosti okna.

## Funkční a přístupnostní požadavky

- Každý switch zachová současnou dvoustavovou boolean hodnotu a napojení na existující model.
- Ovládání myší zůstane zachované.
- Ovládání klávesou **Space** zůstane zachované.
- Tab order a viditelný keyboard focus zůstanou zachované.
- Přístupnost musí nadále poskytovat název, roli, checked stav, enabled/disabled stav a možnost změny hodnoty.
- Switch nesmí být pouze namalovaný obrázek bez funkčního a přístupného ovládacího prvku.
- Zakázaný switch nesmí reagovat na myš ani klávesnici.
- Změna nesmí přidat zápis na disk, síťovou operaci ani jiný vedlejší efekt nad rámec současného chování příslušné volby.

## Communication Contracts

- Každý řádek tabulky zachová vlastní nezávislý stav **Enabled**.
- Přepnutí konkrétního switche nesmí změnit jiný řádek.
- Dynamické vytvoření, scrollování, změna velikosti, změna DPI a práce s maximálním podporovaným počtem řádků musí zachovat správný stav i zarovnání switchů.
- Nahrazení checkboxů nesmí změnit identitu řádku, vazbu na remote IP, tab order ani chování ukládání.

## Beze změny

- Texty jednotlivých voleb.
- Výchozí hodnoty a pravidla, podle kterých se volby zapínají, vypínají nebo zakazují.
- Datový model, ukládání konfigurace, validace a síťová komunikace.
- Ostatní prvky GUI, které nejsou dvoustavovým checkboxem.

## Akceptační kritéria

1. V tray aplikaci nezůstane žádný uživatelsky viditelný klasický čtvercový checkbox.
2. Všechny uvedené volby používají jednotný switch odpovídající referenčnímu vzhledu.
3. Zapnutý, vypnutý, zakázaný a zaměřený stav jsou vizuálně jednoznačné.
4. Každý switch zachovává původní hodnotu a funkční chování.
5. Myš, Tab a Space fungují stejně jako u původního přístupného ovládacího prvku.
6. Kliknutí na label přepíná povolený switch.
7. Communication Contracts správně zobrazí a ovládá switche ve všech dynamických řádcích bez změny jiných řádků.
8. Switche nejsou oříznuté, překryté ani chybně zarovnané při podporovaném resize a DPI.
9. Nedojde ke změně dokumentace, datové logiky ani jiných prvků aplikace mimo tento rozsah.

## Lehká kontrola po implementaci

- Ve wizardu ručně zobrazit všechny čtyři typy voleb a ověřit zapnutý i vypnutý stav.
- U alespoň jednoho prvku ověřit zakázaný stav.
- Ověřit přepnutí myší, kliknutím na label a klávesou Space.
- Ověřit viditelný focus při průchodu klávesou Tab.
- V Communication Contracts přepnout dva různé řádky a ověřit, že se jejich stavy nemíchají.
- Krátce ověřit scroll, resize a jedno zvýšené DPI bez ořezu nebo posunu stavu.
- Nespouštět těžké, plošné ani dlouhé testovací sady.
- Po úspěšné lehké kontrole sestavit tray EXE a vložit ho do standardní distribuce projektu.

## Referenční obrázky

- `current-native-checkbox.png` zachycuje současný nežádoucí Win32 checkbox.
- `target-toggle-switch.png` zachycuje požadovaný modrý switch v referenčním návrhu.

## Mimo aktuální krok

Tento dokument pouze formalizuje požadavek. Implementace, sestavení a nasazení začnou až na výslovný pokyn uživatele.
