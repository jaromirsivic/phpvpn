# Formální zadání - Robustní podepisování manifestu neexportovatelným CNG klíčem

## Cíl

Upravit standardní distribuční skript `client/tools/make-dist.ps1` tak, aby v prostředí Windows PowerShell 5.1 spolehlivě podepsal release manifest existujícím připnutým neexportovatelným RSA klíčem uloženým v CNG úložišti.

Skript má nadále používat standardní přístup k privátnímu klíči všude, kde funguje. Pokud však přesně rozpozná známou interoperabilní chybu mezi Windows PowerShellem 5.1, .NET Frameworkem a CNG, má automaticky a bezpečně použít kompatibilní CNG cestu. Ruční dopodepisování manifestu po selhání skriptu již nemá být potřeba.

## Zjištěná příčina

- Vývojový podpisový certifikát je přítomný v úložišti `Cert:\CurrentUser\My`, má RSA veřejný klíč a `HasPrivateKey` je `True`.
- Privátní klíč je persistentní, neexportovatelný a uložený prostřednictvím `Microsoft Software Key Storage Provider`.
- `make-dist.ps1` dnes získává privátní klíč voláním `RSACertificateExtensions.GetRSAPrivateKey(certificate)`.
- V ověřeném prostředí Windows PowerShell 5.1 Desktop nad .NET Frameworkem toto volání selže chybou, že otevíraný CNG handle byl vyhodnocen jako ephemeral, ale nebyla zadána volba `EphemeralKey`.
- Skutečný klíč není dočasný. Selhává převod vazby certifikátu na použitelný RSA objekt v daném runtime.
- Přímé otevření správného persistentního klíče přes CNG provider a následné podepsání pomocí `RSACng` funguje. Stejný certifikát, stejný klíč, stejná data manifestu a stejné parametry podpisu následně úspěšně projdou `install.ps1 -VerifyOnly`.

## Rozsah

Úprava zahrnuje pouze:

- získání RSA privátního klíče pro oddělený podpis release manifestu v `client/tools/make-dist.ps1`,
- bezpečný CNG fallback pro přesně rozpoznanou chybu standardního přístupu,
- správný úklid kryptografických objektů a nativních handle,
- chybová hlášení související s výběrem a otevřením podpisového klíče,
- ochranu před použitím nesprávného privátního klíče,
- nezbytné cílené ověření vytvořeného distribučního balíčku.

Neměnit formát manifestu, formát odděleného podpisu, instalační workflow, připnutou podpisovou identitu, obsah balíčku ani standardní sestavení aplikace.

## Požadovaný návrh řešení

### Primární cesta

- Certifikát nadále vyhledat podle explicitně zadaného thumbprintu v `Cert:\CurrentUser\My`.
- Zachovat stávající kontroly, že certifikát existuje, má privátní klíč a používá podporovaný RSA algoritmus.
- Nejprve použít současnou standardní cestu `RSACertificateExtensions.GetRSAPrivateKey`.
- Pokud standardní cesta vrátí použitelný RSA objekt, žádný fallback nespouštět.
- Standardní cesta zůstává preferovaná kvůli kompatibilitě s jinými certifikáty, providery a novějšími PowerShell nebo .NET runtime.

### Podmínka CNG fallbacku

- Fallback spustit pouze tehdy, když standardní získání RSA klíče selže známou chybou CNG handle, konkrétně stavem odpovídajícím hlášení o ephemeral CNG handle bez volby `EphemeralKey`.
- Rozpoznání nesmí být založené pouze na libovolném obecném selhání nebo na slepém zachycení všech výjimek.
- Pokud nastane jiná chyba, zachovat fail-closed chování a skončit s přesnou, bezpečnou a užitečnou diagnostikou.
- Fallback nepoužívat pro certifikát bez privátního klíče, ne-RSA certifikát, nepodporovaný legacy CSP nebo jiný případ, jehož bezpečná interpretace není jednoznačná.

### Zjištění CNG klíče

- Název klíčového kontejneru, název CNG provideru ani jiný interní identifikátor nesmí být pevně zapsaný ve skriptu.
- Správnou vazbu certifikátu na privátní klíč načíst z kryptografických metadat certifikátu uložených ve Windows Certificate Store.
- Preferovaný návrh je získat provider info certifikátu, například vlastnost odpovídající `CERT_KEY_PROV_INFO_PROP_ID`, a z ní bezpečně převzít:
  - přesný název klíčového kontejneru,
  - přesný název provideru,
  - informaci, zda jde o uživatelský nebo strojový klíč,
  - další nezbytné flagy pro otevření stejného klíče.
- Je přípustné použít malý vložený C# helper přes `Add-Type`, pokud Windows PowerShell 5.1 neposkytuje potřebná metadata bezpečně přes veřejné PowerShell API.
- Alternativní implementace je přípustná pouze tehdy, pokud stejně bezpečně získá klíč přímo z vazby certifikátu bez hardcodovaného kontejneru, bez parsování lokalizovaného výstupu `certutil` a bez heuristického hledání podle názvu subjektu.
- Nepoužívat `certutil` jako runtime závislost pro zjišťování klíče. Jeho textový výstup je určený pro diagnostiku, může být lokalizovaný a není stabilním strojovým kontraktem.

### Otevření a ověření klíče

- Otevřít přesně identifikovaný CNG klíč pomocí provideru a správného user nebo machine kontextu odvozeného z certifikátu.
- Nevytvářet nový klíč, neimportovat náhradní klíč a neměnit vlastnosti existujícího klíče.
- Klíč zůstane neexportovatelný. Skript nesmí požadovat ani provádět export privátního materiálu.
- Před podpisem ověřit, že otevřený privátní klíč skutečně odpovídá veřejnému klíči z certifikátu. Porovnat bezpečné veřejné RSA parametry nebo použít rovnocenný kryptograficky průkazný postup.
- Při neshodě klíče a certifikátu okamžitě skončit bez vytvoření platně vypadajícího podpisového souboru.

## Vytvoření podpisu

- Podepisovat přesně stejné bajty `release-manifest.json`, které skript právě zapsal a které následně ověřuje instalátor.
- Zachovat algoritmus RSA PKCS#1 v1.5 se SHA-256.
- Zachovat současný formát a význam `release-manifest.sig`, včetně identifikace algoritmu, thumbprintu, subjectu a Base64 podpisu.
- Zachovat export pouze veřejné části certifikátu do `release-signing.cer`.
- Nevkládat privátní klíč, jeho serializaci ani citlivá metadata do distribuce, logu nebo chybového hlášení.
- Podpisový soubor vytvořit až po úspěšném podpisu a všech kontrolách. Použít bezpečný dočasný soubor a atomické nahrazení nebo stejně bezpečný postup, aby při selhání nezůstal částečný podpis.
- Před zahájením podpisové fáze odstranit nebo bezpečně nahradit případné staré `release-manifest.sig` a `release-signing.cer`, aby nové selhání nemohlo ponechat zdánlivě použitelný podpis z předchozího balíčku.

## Správa prostředků

- Každý získaný RSA objekt, `CngKey`, safe handle a další kryptografický prostředek deterministicky uvolnit i při výjimce.
- Respektovat vlastnictví nativních handle. Nesmí dojít k dvojímu uvolnění ani k úniku handle.
- Po dokončení podpisu nesmí zůstat otevřený privátní klíč déle, než je nezbytné.
- Skript nesmí měnit certificate store, trust store, ACL klíče ani provider configuration.

## Chybové chování

- Při úspěšném použití fallbacku může skript stručně oznámit, že pro neexportovatelný CNG klíč použil kompatibilní cestu. Nezobrazovat kontejner, unikátní jméno klíče ani jiné interní identifikátory.
- Pokud standardní cesta i povolený fallback selžou, ukončit skript s nenulovým výsledkem a jasně rozlišit alespoň:
  - certifikát nebyl nalezen,
  - certifikát nemá použitelný privátní klíč,
  - certifikát není RSA,
  - vazbu na CNG provider nebo kontejner nelze bezpečně získat,
  - CNG klíč nelze otevřít,
  - veřejný klíč certifikátu neodpovídá otevřenému privátnímu klíči,
  - samotné podepsání selhalo.
- Chybové hlášení nesmí obsahovat privátní materiál ani zbytečně zveřejňovat interní identifikátory úložiště klíčů.
- Skript nesmí při chybě tvrdit, že je distribuce podepsaná nebo připravená.

## Zachování ostatního chování

- Neměnit výchozí thumbprint ani možnost předat jiný thumbprint parametrem.
- Neměnit kanonickou JSON serializaci manifestu ani pořadí a význam jeho polí.
- Neměnit seznam balených souborů, jejich hashování, kontrolu architektury ani kontrolu podpisu Wintun knihovny.
- Neměnit volitelný Authenticode podpis binárních souborů. Tento mikro-update řeší pouze získání klíče pro oddělený podpis manifestu.
- Neměnit `client/tools/package/install.ps1`, pokud není nutná drobná oprava přímo vyvolaná zachováním existujícího podpisového formátu. Preferováno je ponechat instalátor beze změny.
- Nepřidávat externí knihovnu, nový instalovaný nástroj ani síťovou závislost.
- Neměnit aplikaci, službu, tray GUI, protokol ani kryptografické doménové řetězce.
- Neupravovat běžnou projektovou dokumentaci.

## Cílené ověření

- Ověřit skript v aktuálním prostředí Windows PowerShell 5.1 s existujícím připnutým neexportovatelným CNG klíčem.
- Potvrdit, že skript dokončí celý balíčkovací postup bez ručního dopodepisování.
- Spustit `dist/install.ps1 -VerifyOnly` a ověřit, že:
  - podpis manifestu je platný,
  - thumbprint a signing identity odpovídají připnuté hodnotě,
  - všechny soubory a SHA-256 hashe projdou,
  - kontrola architektury a Wintun API projde,
  - nic se nenainstaluje.
- Ověřit negativní případ s neexistujícím thumbprintem. Skript musí bezpečně selhat a nesmí ponechat starý nebo částečný podpis.
- Pokud je bez instalace dalších nástrojů dostupný runtime, ve kterém standardní `GetRSAPrivateKey` funguje, provést malou kontrolu, že primární cesta zůstala funkční. Nedostupnost takového runtime není blokující.
- Neprovádět komplexní testovací sadu a nevytvářet nové aplikační testy.

## Akceptační kritéria

1. `make-dist.ps1` v aktuálním Windows PowerShellu 5.1 podepíše manifest připnutým neexportovatelným CNG RSA klíčem bez ručního zásahu.
2. Skript vždy nejprve zkusí standardní `GetRSAPrivateKey` a CNG fallback použije pouze pro přesně rozpoznanou známou interoperabilní chybu.
3. Fallback získá provider a klíčový kontejner z vazby certifikátu, nikoli z hardcodované hodnoty, subjectu nebo textového výstupu `certutil`.
4. Otevřený klíč je před podpisem ověřen proti veřejnému klíči certifikátu.
5. Privátní klíč zůstává neexportovatelný a žádný privátní materiál se nezapisuje do souboru ani logu.
6. Manifest se nadále podepisuje pomocí RSA PKCS#1 v1.5 a SHA-256 a formát distribučních souborů zůstává kompatibilní se stávajícím instalátorem.
7. Při selhání nezůstane starý nebo částečný podpis, který by mohl být zaměněn za výsledek aktuálního běhu.
8. Všechny kryptografické objekty a handle jsou korektně uvolněné i při výjimce.
9. `install.ps1 -VerifyOnly` nad nově vytvořenou distribucí projde bez chyby a nic nenainstaluje.
10. Negativní případ s neexistujícím certifikátem skončí bezpečnou a srozumitelnou chybou bez použitelného podpisového artefaktu.
11. Binární Authenticode podepisování, obsah balíčku, aplikace, služba, GUI, protokol a běžná dokumentace zůstávají beze změny.

## Omezení implementace

- Neupravovat běžnou projektovou dokumentaci, včetně `protocol.md`, `client.md`, `tray_UX.md`, `distribution.md` a plánovacích dokumentů.
- Nevytvářet nové aplikační testy ani nespouštět komplexní testovací sady.
- Neinstalovat PowerShell 7, další SDK, kryptografický provider ani jinou závislost jen kvůli ověření.
- Neprovádět commit ani push, pokud o to uživatel výslovně nepožádá.
- Nerozšiřovat změnu na jiné podpisové nebo kryptografické workflow mimo oddělený podpis release manifestu.

## Hranice formalizační fáze

Toto je samostatné technické zadání a není součástí žádného číslovaného mikro-updatu. Soubor vytvořila hlavní koordinátorka osobně. Vytvoření ani zápis formálního zadání nebyly delegovány na jiného agenta.

V této fázi se nesmí upravovat `make-dist.ps1` ani žádný jiný implementační soubor. Implementace smí začít až po uživatelově výslovném schválení zadání a volbě modelu a effortu. Formální zadání je jediná změna provedená v této fázi.
