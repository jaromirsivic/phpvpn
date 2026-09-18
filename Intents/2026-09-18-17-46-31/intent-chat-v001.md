---
intent_version: 1
created_at: 2026-09-18-17-55-45
produces: intent-v001.md
---

# Snímek konverzace k intentu

**User:** Dobře, teďka vytvoř nový intent

**AI:** Našel jsem předchozí intent „Research 32bit PHP“, který je vedený jako rozpracovaný, ale byl přerušený. Chceš ho zahodit a založit nový, nebo se k němu nejdřív vrátit?

**User:** Chci ho zahodit a založit nový

**AI:** Nový intent je připravený a zachytávání běží. Můžeš začít diktovat; kdyžtak to kdykoliv zastavím, pozastavím nebo zpracujeme.

**User:** Chci upravit instalační a odinstalační skripty, když uživatel klikne na uninstall služby, tak chci, aby se, protože teďka ten, tam jenom problikne nějaké okno a uživatel vůbec neví, co se stalo. Takže teďka nově chci, aby se na tom okně vykreslil nějaký ASCII box a v něm bylo napsáno Successfully uninstalled. Jakože že došlo k úspěšné odinstalaci služby, plus nějaký krátký text, i když successfully, jako nemusí tam být přesně successfully uninstalled, ale informace o tom, že došlo k úspěšnému odinstalování služby. A to samé bude u instalace, takže když uživatel instaluje službu a klikne na install, tak zase to okno, to teď jenom problikne. Chtěl bych, aby tam zůstalo, aby se vykresl nějaký ASCII box, kde po dobu tří vteřin bude zobrazeno, že se vše podařilo nainstalovat korektně

**AI:** Mám to zaznamenané. Pokračuj v diktování, nebo řekni, až to mám zpracovat.

**User:** Zpracuj intent

**AI:** Našla jsem zdrojové skripty i distribuční kopie. Pro dokončení intentu navrhuji tyto výchozí volby:

- ASCII box se zobrazí pouze po skutečně úspěšné instalaci nebo odinstalaci.
- Zůstane viditelný tři sekundy a potom se okno automaticky zavře.
- Režim pouhého ověření balíčku nebude zdržovaný ani nebude tvrdit, že proběhla instalace.
- Chybové větve zachovají nenulový návratový kód a nikdy nezobrazí úspěch.
- Text zůstane anglicky, například „PHPVPN installed successfully“ a „PHPVPN uninstalled successfully“, s krátkou doplňující informací.
- Změna se promítne do zdrojových instalačních skriptů, aktuální distribuce a automatických testů.

Mám tyto výchozí volby použít?

**User:** Ano, můžeš tyto hlášky použít
