---
intent_version: 1
created_at: 2026-09-18-11-59-52
source: intent-chat-v001.md
---

# Vytvoření archivu distribuce a jeho přenos do virtuálního počítače

## Shrnutí

V kořeni projektu phpVPN vznikne dávkový soubor `copydistributiontovm.bat`. Jedním spuštěním připraví aktuální archiv `dist.zip` ze složky `dist` a následně s oprávněním správce spustí existující PowerShell skript `copydistributiontovm.ps1`, který archiv přenese do virtuálního počítače.

## Kontext projektu

- Kořen projektu: `C:\git\phpvpn`.
- Existující `copydistributiontovm.ps1` používá `Copy-VMFile` pro přenos `C:\git\phpvpn\dist.zip` do virtuálního počítače `windows 11`.
- Požadovaný dávkový soubor má být v témže kořeni a musí používat skutečný název bez mezer: `copydistributiontovm.bat`.

## Funkční požadavky

1. Dávkový soubor se spouští z kořene projektu a pracuje s artefakty v tomto kořeni, nezávisle na aktuálním pracovním adresáři volajícího.
2. Pokud v kořeni existuje `dist.zip`, před vytvořením nového archivu jej odstraní.
3. Zazipuje celý obsah složky `dist` do souboru `dist.zip` v kořeni projektu.
4. Zdrojovou složku `dist` nijak nemaže ani nepřesouvá.
5. Po úspěšném vytvoření archivu spustí `copydistributiontovm.ps1` se zvýšenými oprávněními, aby skript přenesl nově vytvořený archiv do virtuálního počítače.
6. Pokud příprava archivu selže, přenos do virtuálního počítače se nespustí a uživatel dostane srozumitelnou chybovou informaci.

## Realizační vodítka

- Dávkový soubor může využít vestavěný PowerShell pro odstranění archivu, kompresi a vyžádání zvýšených oprávnění při spuštění existujícího skriptu.
- Před kompresí je vhodné ověřit existenci složky `dist` a při její absenci skončit chybou.
- Existující `copydistributiontovm.ps1` není součástí požadované změny, pokud realizace neodhalí nutnost technické úpravy.

## Akceptační kritéria

1. V kořeni projektu existuje `copydistributiontovm.bat`.
2. Po spuštění s existujícím starším `dist.zip` vznikne nový `dist.zip` odpovídající aktuálnímu obsahu složky `dist`.
3. Složka `dist` zůstane po dokončení beze změny a na původním místě.
4. Po úspěšném vytvoření archivu se existující PowerShell skript otevře se zvýšenými oprávněními a provede přenos na virtuální počítač.
5. Při chybějící složce `dist` nebo neúspěšné kompresi nedojde k pokusu o přenos.
