## Architektura

- **Technologický stack**
    - Operační systém
    - Databáze
    - Produkty
    - Komponenty třetích stran
- **Logická a aplikační architektura** - komponenty, odpovědnosti, vazby.
- **Nastavení Sítě**

## Uživatelské rozhraní

- **Design system** - korporátní barvy, typografie.
- **Responsivita** - desktop, tablet, mobil, podporované prohlížeče.
- **Přístupnost** - WCAG a jiná opatření.
- **Lokalizace** - jazyky, časové zóny, formáty, správa překladů.

## Bezpečnost

- **Autentizace** - lokální účty, SSO, OIDC, MFA, session.
- **Role a oprávnění** - matice nad funkcemi a daty.
- **Správa identit** - založení, změna, deaktivace, provisioning.
- **Šifrování a klíče** - přenos, uložení, certifikáty, key store.
- **Auditní log** - rozsah, ochrana proti úpravě.

## Nasazení a Provoz

- **Popis Prostředí** - Produkce, UAT, FAT, ...
- **Nasazení a release** - build, migrace schématu, rollback, feature flags.
- **Logování** - úrovně, formát, korelační ID, uchování.
- **Monitoring a alerting** - metriky, healthcheck, prahy, upozornění.
- **Zálohování a obnova** - rozsah, frekvence, test obnovy, havárie.
- **Provozní dokumentace** - runbooky, incidenty, eskalace, kontakty.
- **Údržba a licence** - aktualizace, závislosti, open source komponenty.

## Testování

- **Testovací strategie** - typy testů, automatizace.
- **Testovací data** - zdroj, anonymizace, obnova prostředí.
- **Testovací scénáře** - navázané na funkční a nefunkční požadavky.
- **Akceptační testy** - seznam testů, akceptační protokol.

## Dokumentace a Školení

- **Provozní Dokumentace** - pro klíčové uživatele
- **Uživatelská dokumentace** - pro koncové uživatele (klienty klienta)
- **Technická dokumentace** - pro IT Administrátory projektu
- **Servisní dokumentace** - servisní tým
- **Školení pro klíčové uživatele**
- **Školení pro IT administrátory**
- **Školení pro koncové uživatele**

## Migrace

- **Migrace dat** - zdroje, mapování, transformace, datová pumpa, čištění, kontrola
- **Cutover plán** - časování, souběžný běh, odstávka, plán návratu.
- **Školení a předání** - rozsah, uživatelská dokumentace.
- **Podpora po náběhu** - hypercare, délka, ukončení.

## Regulatorní požadavky

- **OWASP** - OWASP, validace vstupů, rate limiting.
- **GDPR** - kategorie, právní základ, anonymizace, GDPR.
- **NIS2** - kategorie, právní základ, anonymizace, GDPR.
- **AI Act** - watermarking, usage of AI.