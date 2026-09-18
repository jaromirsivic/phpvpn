---
intent_version: 1
created_at: 2026-09-18-11-59-52
produces: intent-v001.md
---

# Záznam konverzace k intentu

**Uživatel:** Chci vytvořit nový intent v projektu phpVPN.

**AI:** Intent pro phpVPN je otevřený a zachytávám požadavky.

**Uživatel:** Chci vytvořit nový dávkový soubor `copy distribution to VM.bat`. Existuje soubor `copy distribution to VM.ps1` se stejným názvem kromě přípony. Dávkový soubor v kořenové složce zjistí, zda existuje `dist.zip`, a pokud existuje, smaže jej. Poté zazipuje celou složku `dist` do `dist.zip`, přičemž složku `dist` ponechá. Nakonec se zvýšenými oprávněními spustí PowerShell soubor pro zkopírování nově vytvořeného `dist.zip` do virtuálního počítače.

**Uživatel:** Je to jasné.

**AI:** Požadavek je zachycen jako nový intent.

**Uživatel:** Už nemám další požadavky. Zpracuj to a ukaž mi formální zadání, kde ho vytvořila. To znamená, dej mi link na to formální zadání.

**AI:** Při ověření projektu bylo zjištěno, že existující PowerShell skript se ve skutečnosti jmenuje `copydistributiontovm.ps1`, bez mezer.

**AI:** Má nový dávkový soubor použít stejný skutečný název bez mezer, tedy `copydistributiontovm.bat`?

**Uživatel:** Ano, má.
