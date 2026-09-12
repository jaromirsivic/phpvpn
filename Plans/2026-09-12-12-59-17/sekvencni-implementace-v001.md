---
plan_version: 1
created_at: 2026-09-12
source: ./plan-v001.md
purpose: Sekvenční realizace po agentních blocích s ruční akceptací
---

# Sekvenční implementace tray změn

## Cíl

Realizovat zbývající části `plan-v001.md` po ucelených sekvenčních blocích. Každý blok musí dodat implementaci, cílené automatické testy, související dokumentaci a rychlý Debug build. Po každém bloku následuje ruční nasazení a akceptace uživatelem. Commit a push provádí výhradně uživatel. Další blok nesmí začít před potvrzením předchozího.

Distribuční balíčky a úplná regresní nebo release validace nejsou součástí tohoto plánu.

Veškerá práce se provádí přímo v hlavním checkoutu `C:\git\phpvpn`. Pro žádný blok se nevytváří vedlejší Git worktree.

## Výchozí stav

- Původní Task 1, sdílená geometrie tlačítkové patičky a migrace Wizardu, už je implementovaný v commitu `refactor(tray): share wizard footer geometry`.
- Před prvním novým blokem se pouze ověří cílené filtry `ButtonFooterLayout` a `WizardLayout`.
- Zbývající realizace začíná původním Task 2.
- Superpowers se nesmí instalovat ani spouštět. Test-driven kroky z původního plánu se provedou přímo bez tohoto skillu.

## Společný pracovní protokol

Každý agent pracuje nad výchozí revizí, kterou po ručním commitu a pushi předchozího bloku určí uživatel, a provede tento cyklus:

1. Načte globální omezení a pouze svou část původního plánu.
2. Pomocí Graftu načte jen relevantní symboly, volající a přesné zdrojové rozsahy.
3. Napíše nebo upraví cílené testy tak, aby nejprve doložily chybějící chování.
4. Provede minimální implementaci bez rozšíření rozsahu.
5. Aktualizuje dokumentaci přímo související s daným blokem.
6. Spustí pouze cílené testovací filtry bloku.
7. Vytvoří rychlý Debug build produkční služby, CLI a tray aplikace.
8. Provede `git diff --check` a zkontroluje rozsah změn.
9. Ponechá všechny své změny necommitnuté a bez výslovného pokynu uživatele je ani nestaguje.
10. Předá přehled změněných souborů, výsledky cílených testů, cestu k build artefaktům, známá omezení a krátký ruční checklist.
11. Zastaví práci a čeká na ruční akceptaci, commit a push provedené uživatelem.

Změny zůstávají přímo v hlavním checkoutu až do ruční akceptace. Pokud uživatel nahlásí problém, opravu provede stejný agent ve stejném checkoutu a kontextu. Po potvrzení provede commit a push uživatel a určí výchozí revizi dalšího bloku.

Agent nesmí bez samostatného výslovného pokynu uživatele spustit `git add`, `git commit`, `git push` ani ekvivalentní operaci, která mění index nebo vzdálený repozitář.

Agent ani koordinátor nesmí pro tuto realizaci spustit `git worktree add`, vytvořit jiný checkout ani přesunout práci mimo `C:\git\phpvpn`.

## Rychlý build pro ruční nasazení

První blok podle potřeby nakonfiguruje Debug sestavení s UI testy. Po každém bloku musí být úspěšně vytvořeny produkční binárky potřebné pro ruční nasazení:

```powershell
Push-Location client
cmake --preset debug -DPHPVPN_BUILD_UI_TESTS=ON
cmake --build --preset debug --target phpvpn-service phpvpn-cli phpvpn-tray --parallel 4
Pop-Location
```

Při nezměněné konfiguraci se v dalších blocích konfigurační příkaz vynechá. Artefakty jsou pod `work/build/debug/apps`. Agent službu ani tray aplikaci automaticky neodinstaluje a nenasazuje. Tyto kroky provádí ručně uživatel.

## Sekvenční bloky

### Blok 1: Connect a Disconnect

**Model:** Sol  
**Effort:** High  
**Zdrojový rozsah:** původní Task 2

**Důvod volby:** Změna je lokalizovaná, ale obsahuje citlivý Win32 paint lifecycle a časování auto-close. Vyšší effort snižuje riziko nenápadné behaviorální regrese bez ceny širokého expertního kontextu.

**Implementace:**

- zjednodušit horní část Connect a Disconnect na jediný statický text,
- odstranit nadbytečné dynamické horní ovládací prvky,
- zachovat dynamiku v existujících kartách a stavových panelech,
- přesunout hranici úspěšného zobrazení na skutečný paint výsledkového panelu,
- zachovat současný interval a lifecycle auto-close,
- aktualizovat příslušné části `tray_UX.md` a `client.md`.

**Cílená automatická brána:**

- `TrayLayout`,
- `WP03Models`,
- `WP03Windows`, včetně statického progress textu a paint řízeného auto-close.

**Ruční akceptace:**

- Connect při čekání, úspěchu a chybě stále ukazuje jediný předepsaný horní text,
- Disconnect při čekání, úspěchu a chybě stále ukazuje jediný předepsaný horní text,
- dynamický výsledek a chyba zůstávají ve stavovém panelu,
- úspěšné okno se zavře až po skutečném zobrazení výsledku,
- resize nevytvoří prázdný prostor, překryv nebo ořez.

### Blok 2: Server Information a EditorWindow

**Model:** Astra  
**Effort:** Medium  
**Zdrojový rozsah:** původní Task 3 a editorová část původního Task 4

**Důvod volby:** Blok spojuje nativní Win32 tab control, focus, klávesnici, clipboard, stabilitu editorového HWND a produkční IPC invariant. Současné zpracování obou zásahů do EditorWindow omezuje opakované načítání stejného kontextu a konflikty v týchž souborech.

**Implementace:**

- přidat dvě nativní karty Server Information,
- otevřít okno na prázdné kartě `Who is online`,
- zobrazit již načtený JSON pouze na kartě `Raw server information`,
- zajistit, že přepínání nevyvolá další IPC nebo čtení dat,
- zachovat selection, scroll, copy, Tab, Shift+Tab, Enter a Escape,
- převést editable i read-only EditorWindow na sdílenou patičku,
- aktualizovat Show Info, EditorWindow a jejich patičku v `tray_UX.md` a `client.md`.

**Cílená automatická brána:**

- editorové scénáře filtru `TrayLayout`,
- `ServerInformationTabs`,
- `ReferenceGeometry`,
- `plan_v001_server_information_tabs`,
- cílený editorový footer a resize scénář.

**Ruční akceptace:**

- Server Information začíná na prázdné kartě,
- druhá karta ukáže nezměněný raw JSON,
- opakované přepínání nezpůsobí nové načtení ani blikání,
- funguje myš, šipky, Tab, Shift+Tab, copy, Enter a Escape,
- Edit Config zůstává funkčně beze změny,
- patička EditorWindow se správně skládá při změně šířky.

### Blok 3: Password SHA a Communication Contracts

**Model:** Sol  
**Effort:** High  
**Zdrojový rozsah:** zbytek původního Task 4 a původní Task 5

**Důvod volby:** Obě migrace používají hotový helper a sdílejí geometrické a nativní testovací prostředí. Contracts přidává dynamické varianty, error region a scrollování, proto je vhodný vyšší effort. Jeden agent může zachovat stejný kontext a zpracovat dvě oddělené implementační části bez mezilehlého commitu.

**Implementace:**

- převést Password SHA na společnou geometrii patičky,
- odvodit body a scrollbar z výsledné výšky patičky,
- převést Contracts na dynamickou patičku podle skutečné viditelnosti Check a Repeat,
- zachovat význam, text, pořadí a enabled stav všech tlačítek,
- přepočítat error region, body, pager, scrollbar a divider podle skutečné patičky,
- doplnit společný dokumentační kontrakt pro Wizard, Editor, Info, Password SHA a Contracts.

**Cílená automatická brána:**

- příslušné scénáře `TrayLayout`,
- `ReferenceGeometry`,
- `plan_v001_editor_password_footers`,
- `plan_v001_contracts_footer_states`,
- `live_resize_repaint`,
- `wp06_password_children_parent_ok`,
- existující Contracts lifecycle filtry přímo dotčené změnou.

**Ruční akceptace:**

- Password SHA zachovává Default, Random a OK a při resize nic neořezává,
- Contracts správně zobrazuje všechny kombinace Check, Repeat, Cancel a Save,
- změna šířky správně přidává nebo odebírá řádek patičky,
- error region ani tabulka se nepřekrývají s patičkou,
- fungují scrollbary, Tab, Enter, Escape a vlastní akce tlačítek.

### Blok 4: Preview, dokumentace a lehká integrace

**Model:** Sol  
**Effort:** Medium  
**Zdrojový rozsah:** zúžená část původního Task 6

**Důvod volby:** Po vyjmutí úplné regrese a release validace jde převážně o ohraničenou práci v preview harnessu, dokumentaci a cílených integračních filtrech. Astra zde nepřináší dostatečný přínos proti vyšší spotřebě.

**Implementace:**

- doplnit preview `Disconnect` se scénami Disconnecting, Disconnected a Failed,
- doplnit preview `Info` se scénami Who a Raw,
- rozšířit pouze související live resize scénáře,
- aktualizovat `client/tests/ui/README.md`,
- sjednotit a zkontrolovat změny v `tray_UX.md` a `client.md`,
- ověřit, že `protocol.md`, služba, síťování, kryptografie a wire formát zůstaly beze změny,
- vytvořit rychlý Debug build pro poslední ruční průchod.

**Cílená automatická brána:**

- `plan_v001_`,
- `live_resize_repaint`,
- `wp06_scene_matrix`,
- pouze přímo související UI filtry, které byly v předchozích blocích změněny.

**Ruční akceptace:**

- projít výsledné Connect, Disconnect, Info, Editor, Password SHA a Contracts,
- ověřit nové preview scény,
- zkontrolovat běžné resize body a dostupná DPI,
- potvrdit, že dokumentace odpovídá skutečnému chování.

## Předávací formát každého agenta

Každý agent vrátí stručný report v tomto pořadí:

1. název bloku a výchozí revizi,
2. dokončené chování,
3. změněná dokumentace,
4. spuštěné cílené testy a jejich výsledky,
5. rychlý build a cesta k `work/build/debug/apps`,
6. ruční checklist pro uživatele,
7. položky `NOT RUN` a známá omezení,
8. potvrzení, že změny zůstávají necommitnuté a agent čeká na ruční akceptaci, commit a push uživatele.

## Práce výslovně mimo tento plán

Následující činnosti se provedou v samostatném validačním nebo release plánu:

- tvorba distribučního balíčku,
- podpisy, manifest a kontrola kompletnosti distribuce,
- úplný Debug CTest,
- úplný Release build a Release CTest,
- instalační, upgrade a uninstall E2E,
- úplná DPI a cross-monitor matice,
- úplný bezpečnostní, distribuční a release audit.

Žádný agent tohoto plánu nesmí tyto práce přidat jako podmínku dokončení svého implementačního bloku.

## Koordinační podmínky

- Agenti běží výhradně sekvenčně.
- Všichni agenti pracují přímo v `C:\git\phpvpn`. Vedlejší worktree se nevytvářejí.
- Před zahájením každého bloku se ověří aktuální větev a stav pracovního stromu. Neočekávané nebo nesouvisející změny se nesmí přepsat.
- Následující agent nezačne bez výslovné ruční akceptace předchozího bloku.
- Při chybě pokračuje původní agent, aby se neplatil nový kontext.
- Commit a push provádí vždy uživatel. Agent je nesmí provést automaticky ani jako součást dokončovací brány.
- Pro skutečné provedení celého řetězce s uživatelskými pauzami je vhodné předem povolit neomezenou dobu. Výchozí absolutní limit 150 minut nemusí pokrýt ruční akceptace.
- Každý agent dostane jen svůj rozsah, globální omezení, uživatelem určenou výchozí revizi a krátký předávací report předchozího bloku.
