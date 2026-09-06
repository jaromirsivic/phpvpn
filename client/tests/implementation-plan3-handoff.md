# Předání implementace plan3 do hlavního pracovního stromu

Dne 6. září 2026 byly na výslovný pokyn uživatele přeneseny všechny dokončené změny z `C:/Users/User/.codex/worktrees/d6c8/phpvpn` do `C:/git/phpvpn`. Hlavní strom byl před přenosem čistý. Oba stromy vycházely ze stejného commitu `fd058873505d0b2140f5751681c60e134430447a`, nebyly nalezeny překrývající se uživatelské změny.

Přeneseno je 121 souborů: zdroje klienta a serveru, testy, nástroje, plány, normativní dokumentace a tři finální reporty. Ze zdrojového worktree nebyly kopírovány build adresáře, pracovní logy, virtuální prostředí, cache, Git metadata ani původní testovací fronty.

118 souborů je byte po bytu shodných se zdrojovým worktree. Ve třech reportech se upravily pouze odkazy a přidala poznámka o předání. Odkazy na distribuční artefakty vedou do nové výstupní složky hlavního projektu, odkazy na původní testovací logy zůstávají v původním worktree. Výsledky testů ani jejich omezení se nepřepisovaly. Tento předávací report je jediným dalším novým zdrojovým souborem.

## Výstupy

- [Klientský ZIP x64](../../dist/plan3-x64-2026-09-06/phpvpn-client-plan3-x64.zip), absolutně `C:/git/phpvpn/dist/plan3-x64-2026-09-06/phpvpn-client-plan3-x64.zip`.
- [Rozbalený balíček](../../dist/plan3-x64-2026-09-06/package), 14 souborů.
- [SHA-256](../../dist/plan3-x64-2026-09-06/phpvpn-client-plan3-x64.zip.sha256): `3a7e7bba2ad30dd9432c90d2bb34cdc2d2302dec4cd0c2ea62ed62513477d5a8`.
- [Manifest přenosu](../../dist/plan3-x64-2026-09-06/verification/transfer-manifest.json), pro každý přenesený soubor hash před přenosem, zdrojový hash, výsledný hash a případná úprava odkazů.
- [Záloha původních souborů hlavního stromu](../../dist/plan3-x64-2026-09-06/main-before-plan3.zip), pořízená před prvním přepsáním.

Výstupní složka je pod již existujícím pravidlem `/dist/` v `.gitignore`. Stávající soubory přímo v `C:/git/phpvpn/dist` i původní `C:/git/phpvpn/dist.zip` zůstaly beze změn, ověřeno jejich hashi. Nový balíček má vývojový podpis, nikoli veřejně důvěryhodný vydavatelský certifikát. Serverový `phpvpn.php` byl přenesen do kořene projektu, není součástí klientského ZIPu.

## Kontroly po přenosu

- Porovnání všech 121 souborů se zdrojem včetně přesně vymezené úpravy tří reportů.
- Ověření SHA-256 ZIPu a všech 14 distribučních souborů proti finálnímu auditu.
- [PHP regrese v hlavním stromu](../../dist/plan3-x64-2026-09-06/verification/php-tests-main.log): 486 assertions PASS.
- [Skutečný HTTP contention v hlavním stromu](../../dist/plan3-x64-2026-09-06/verification/http-contention-main.log): HTTP 503 queue_busy, Retry-After 1, přibližně 225,48 ms, následné ověřené přijetí a doručení. Pouze dva nové izolované HTTP procesy a nová privátní fronta, oba procesy po testu ukončené.
- [Distribuční VerifyOnly](../../dist/plan3-x64-2026-09-06/verification/package-verify-main.log): manifest, 11 hashovaných souborů, originální Wintun x64 a úplné API PASS. Nic nebylo instalováno.
- `git diff --check`, kontrola Git stavu a nezměněného HEAD/indexu, podrobnosti ve [finálním ověření](../../dist/plan3-x64-2026-09-06/verification/transfer-final.json).

Předchozí širší výsledky a otevřené akceptační brány jsou v [hlavním reportu](implementation-plan3-report.md), [matici B/A/D](implementation-plan3-matrix.md), [RSS/DNS/503 reportu](implementation-plan3-resource-report.md) a [přehledu interaktivních kontrol](e2e/manual-gates.md). Přenos nepředstavuje provedení dalších ručních testů.

Nebyl vytvořen commit ani push. Změny jsou ponechány v hlavním pracovním stromu pro prohlédnutí. Původní worktree zůstalo zachované.
