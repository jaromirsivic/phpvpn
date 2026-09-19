# Aktuální Windows distribuce ve standardní složce

Dokončeno 19. září 2026 přibližně v 10:33 +02:00, před děděným termínem 11:02:55 +02:00.

Výsledek: `C:\git\phpvpn\dist\client\Windows\x64`, releaseVersion `1.0.0`, architektura x64. Distribuce obsahuje všech 14 požadovaných souborů: tři EXE, Wintun DLL, install/uninstall BAT a PowerShell skripty, licence/NOTICE a manifest s odděleným podpisem a veřejným certifikátem. Verze produktu nebyla svévolně zvýšena.

## Postup

Práce proběhla přímo ve větvi `tray_UX` nad aktuální realizací intentu v003. Release build byl znovu vyvolán a oznámil `ninja: no work to do`, tedy již odpovídal aktuálním zdrojům. Existující `make-dist.ps1` vytvořil nový podepsaný balíček v prázdné pracovní složce. Ten prošel `verify-package.ps1` ještě před nahrazením distribuce.

Předchozí celá složka Windows x64 byla přesunuta do `C:\git\phpvpn\work\dist-before-autostart-20260919-1031-x64`. Ověřený pracovní balíček ji nahradil jako celek, bez ponechání starých souborů. Všechny cílové cesty byly před přesunem ověřeny a kontrolované adresáře nebyly reparse points.

Serverová distribuce již přesně odpovídala aktuálnímu `phpvpn.php`. Server a README pro macOS/Linux proto zůstaly zachovány. SHA-256 všech tří souborů mimo Windows x64 byl porovnán před a po operaci.

## Ověření

| Kontrola | Výsledek | Evidence v `work` |
|---|---|---|
| Aktuálnost Release buildu | PASS, bez potřebného překladu | `dist-publish-build.log` |
| Nové sestavení balíčku projektovým nástrojem | PASS | `dist-publish-package-build.log` |
| Ověření pracovní složky před nahrazením | PASS | `dist-publish-stage-verify.log` |
| Standardní dist z jiné pracovní složky | PASS, BAT VerifyOnly, manifest/hash/signature, script parity, přesné PE ikony | `dist-publish-final-verify.log` |
| Shoda tří EXE s aktuálním buildem | PASS, porovnány všechny původní PE bytes mimo checksum a údaje přidaného Authenticode podpisu | `dist-publish-binary-parity.log` |
| Kryptografická kontrola vloženého CMS podpisu EXE | PASS s připnutým vývojovým certifikátem | `dist-publish-binary-parity.log` |
| Wintun DLL | PASS, přesná shoda s buildem a platný podpis WireGuard | packaging a parity logy |
| Shoda s dříve ověřeným balíčkem realizace | PASS, všech 14 finálních souborů byte-for-byte shodných s `work/autostart-dist-final` | závěrečný distribuční běh |
| Server/macOS/Linux | PASS, původní soubory beze změny, server odpovídá zdroji | `dist-publish-preserved-before.json` |
| Git | `git diff --check` PASS, stav zdrojových změn zůstal shodný s počátkem distribučního kroku | `dist-publish-git-before.txt`, `dist-publish-git-after.txt`, `dist-publish-diff-stat.txt` |

SHA-256 výsledného `release-manifest.json`:

```text
16677B94643332CB7A9770EB6D8CD60E4FFDF5C1B3180B8D62F2358C7D939BCC
```

## Instalace a omezení

Vstupní bod je `C:\git\phpvpn\dist\client\Windows\x64\install.bat`, spuštěný jako správce. Projektový VerifyOnly prošel, samotná instalace nebo restart produkční služby se v tomto distribučním kroku neprováděly.

Certifikát je `CN=phpvpn Development Test Signing`, thumbprint `7430CCA24F5DE8A6614D582AFBFED7C80E0F2CA8`. Manifest a matematické podpisy jsou ověřené, ale Windows tomuto kořenovému certifikátu automaticky nedůvěřuje. Get-AuthenticodeSignature a SignTool proto hlásí nedůvěryhodný kořen. Trust store nebyl měněn. Nejde o veřejně důvěryhodný vydavatelský podpis.

Výsledky předchozích unit, UI, E2E a resource testů včetně baseline UI selhání a neprovedených ručních Windows scénářů zůstávají popsány v `realization-v001.md`. Tento distribuční krok je nenahrazuje a nepřeznačuje jako PASS.

Chování programu ani packaging rozhraní se nezměnilo, proto nebyla potřeba další změna `client.md`, `tray_UX.md` ani `distribution.md`. Tento report zaznamenává nové standardní umístění a jeho ověření. Nevznikl worktree, commit, push ani PR. Nesouvisející zdrojové změny byly zachovány.
