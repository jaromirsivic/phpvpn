---
plan_version: 1
created_at: 2026-09-12-13-34-04
source: ../../Intents/2026-09-12-12-59-17/intent-v001.md
---

# Zjednodušení tray dialogů a sjednocení patiček Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Zjednodušit horní část oken Connect a Disconnect, doplnit dvě karty do Server Information a sjednotit všechny vlastní tlačítkové patičky Windows tray aplikace s geometrií prvních dvou kroků Configuration Wizardu.

**Architecture:** Změna zůstane v prezentační vrstvě Win32 tray aplikace. Čistý DPI-aware helper v `layout.*` bude jediným zdrojem 16DIP vnějšího okraje, 8DIP mezery, 36DIP výšky tlačítka a víceřádkového skládání patiček. `ProgressWindow` odstraní dvě nadbytečné horní prezentace a zachová dynamický stav v existujících kartách a panelech. Read-only režim `EditorWindow` dostane nativní dvoukartový ovladač, který pouze přepíná viditelnost již načteného JSON pole.

**Tech Stack:** C++20, Win32 API, Windows Common Controls, CMake 3.28+, Ninja, MSVC AMD64, stávající vlastní test harness a opt-in nativní tray UI harness.

**Spec:** `Intents/2026-09-12-12-59-17/intent-v001.md`

**Test-driven postup:** Každá behaviorální změna začíná konkrétním selhávajícím čistým nebo nativním testem, pokračuje minimální implementací a končí cílenou i regresní zelenou bránou před commitem.

## Global Constraints

- Intent `intent-v001.md` je autoritativní. Pokud implementace odhalí rozhodnutí, které mění jeho význam, práce se zastaví a rozhodnutí se vrátí koordinátorovi intentu.
- Změna se týká pouze Windows tray klienta. `protocol.md`, `phpvpn.php`, služba, kryptografie, síťové chování, IPC příkazy a wire formát se nemění.
- Connect musí po celou dobu života okna zobrazovat jediný horní text `Connecting client to the server.`.
- Disconnect musí po celou dobu života okna zobrazovat jediný horní text `Disconnecting client from the server.`.
- Dynamický výsledek, chyba, náprava, checkpointy a odpočet zůstávají v existujících kartách a stavových panelech.
- Server Information se otevře na prázdné kartě `Who is online`. Karta `Raw server information` zobrazí beze změny již načtený read-only JSON.
- Přepnutí karty nesmí vyvolat `get-info`, jiné IPC, HTTP, diskové čtení ani změnu uložených dat.
- Normativní geometrie patičky při 96 DPI je 16 px vnější okraj, 8 px mezera a 36 px výška tlačítka. Všechny hodnoty se škálují přes `scaleForDpi` a vnější okraj se na velmi úzké šířce bezpečně omezí na `width / 8` stejně jako ve wizardu.
- Význam, text, pořadí, role, enabled stav, Enter, Escape, Tab a Shift+Tab existujících tlačítek se nemění.
- Screenshoty a runtime evidence patří pouze pod `work/logs`, ne jako nové sledované binární soubory.
- Skutečné vizuální, DPI, monitorové nebo přístupnostní scénáře, které realizační prostředí nepodporuje, musí být ve výsledném reportu označené `NOT RUN`, nikdy odvozené z jiného testu.

---

## Výchozí stav a potvrzený rozsah

Graft ukazuje tyto rozhodující symboly a současné odpovědnosti:

- `ProgressWindow::open`, `ProgressWindow::refresh`, `ProgressWindow::layout` a `ProgressWindow::headingProcedure` v `client/apps/tray/progress_window.cpp:32-204,217-293,318-334` vytvářejí a mění `eyebrow_`, `heading_` a `description_`. Úspěšný auto-close se dnes aktivuje prvním skutečným paintem velkého nadpisu Connect nebo nadpisu panelu Disconnect.
- `ProgressLayout`, `progressHorizontalMargin`, `progressContentRight` a `computeProgressLayout` v `client/apps/tray/progress_layout.h:7-84` obsahují nesymetrických 48/64 DIP na běžné šířce a rezervují prostor pro velký nadpis i popis.
- `EditorWindow::open`, `EditorWindow::layout`, `EditorWindow::procedure` a `EditorWindow::translate` v `client/apps/tray/editor_window.cpp:256-500` a `editor_window.h:21-76` sdílejí jediný read-only editor mezi Show Info a Edit Config.
- `computeEditorLayout` v `client/apps/tray/layout.cpp:108-137` používá vlastní 16/32DIP okraje a jednořádkovou patičku.
- `computeWizardLayout` v `client/apps/tray/wizard_layout.cpp:4-36` je současný vizuální vzor 16/8/36 DIP a dvouřádkového skládání.
- `ContractsWindow::layout` v `client/apps/tray/contracts_window.cpp:425-515` a `PasswordShaWindow::layout` v `client/apps/tray/password_sha_window.cpp:117-135` mají vlastní výpočty patiček.
- Exhaustivní `graft grep "BS_OWNERDRAW"` potvrdil, že samostatné vlastní patičky mají Wizard, EditorWindow, ContractsWindow a PasswordShaWindow. Ostatní výskyty jsou tlačítka uvnitř obsahu, pager, přepínače nebo infrastruktura, ne další patička.
- Čisté geometrické testy jsou v `client/tests/unit/test_wizard_layout.cpp:7-21`, `client/tests/unit/test_tray_layout.cpp:15-177` a `client/tests/unit/test_wp03_models.cpp:200-210`.
- Nativní resize, paint, auto-close a EditorWindow scénáře jsou v `client/tests/unit/test_wp03_windows.cpp:190-512`. Opt-in GUI harness a jeho registry testů jsou v `client/tests/ui/tray_ui_tests.cpp:372-423,1106-1142,1360-1422` a `client/tests/ui/dev_preview.inc:1-320`.

## Cílová souborová mapa

| Soubor | Změna a odpovědnost |
|---|---|
| `client/apps/tray/layout.h` | Přidat veřejné čisté typy a signatury sdílené patičky. Rozšířit `EditorLayout` o obdélník tab controlu. |
| `client/apps/tray/layout.cpp` | Implementovat jediný algoritmus patičky a použít jej v `computeEditorLayout`. |
| `client/apps/tray/wizard_layout.cpp` | Přesměrovat stávající geometrii tlačítek na sdílený helper bez změny wizardu. |
| `client/apps/tray/progress_layout.h` | Odstranit obdélníky velkého nadpisu a popisu, použít symetrický wizardový okraj a přímo modelovat Connect i Disconnect. |
| `client/apps/tray/progress_window.h` | Odstranit HWND členy nadbytečného nadpisu a popisu. Zachovat horní kontextový label a stavový panel. |
| `client/apps/tray/progress_window.cpp` | Vytvářet jediný statický horní label, přesunout dynamiku výhradně do panelů, zachovat auto-close přes paint viditelného výsledkového panelu. |
| `client/apps/tray/resource.h` | Přidat stabilní ID horního progress labelu a tab controlu Server Information pro nativní testy a přístupnost. |
| `client/apps/tray/editor_window.h` | Přidat `tabs_`, stav vybrané info karty a privátní `selectInfoTab`. |
| `client/apps/tray/editor_window.cpp` | V read-only režimu vytvořit nativní dvě karty, přepínat viditelnost editoru bez nového načtení a zapojit sdílenou patičku. Edit Config zůstane funkčně stejný. |
| `client/apps/tray/contracts_window.cpp` | Nahradit lokální výpočet akčních řádků sdíleným helperem a zachovat dynamickou chybu, Check, Repeat, Cancel a Save. |
| `client/apps/tray/password_sha_window.cpp` | Nahradit 24DIP patičku sdílenou 16/8/36DIP geometrií a odvodit výšku body z výsledku helperu. |
| `client/tests/unit/test_wizard_layout.cpp` | Uzamknout ekvivalenci wizardu a sdíleného helperu na všech podporovaných DPI a na šířkách před i po zalomení. |
| `client/tests/unit/test_tray_layout.cpp` | Pokrýt všechny sady patiček, nový Editor/Info layout, symetrické progress okraje a odstranění dvou horních řádků. |
| `client/tests/unit/test_wp03_models.cpp` | Přizpůsobit jediného dalšího volajícího nové signatuře `computeProgressLayout` a zachovat kontrolu panelů a odpočtu. |
| `client/tests/unit/test_wp03_windows.cpp` | Ověřit skutečné HWND, texty, paint hranici auto-close, tabs, JSON editor, scroll/copy, fokus a resize. |
| `client/tests/ui/tray_ui_tests.cpp` | Přidat produkční Win32 regresní scénáře pro patičky, karty a statické progress texty. Rozšířit live resize o nové pohledy. |
| `client/tests/ui/dev_preview.inc` | Přidat samostatné preview `Disconnect` a `Info` se scénami potřebnými pro vizuální kontrolu, stále nad privátní in-memory pipe. |
| `client/tests/ui/README.md` | Popsat nové preview scény a přesné spouštěcí příkazy. |
| `tray_UX.md` | Aktualizovat UX05, read-only Server Information a společný kontrakt patiček. Odstranit normativní texty, které by znovu vyžadovaly velký progress nadpis. |
| `client.md` | Aktualizovat Show Info, Connect, Disconnect a akceptační scénáře. Zachovat všechny síťové, pollingové a bezpečnostní kontrakty. |

Nevznikne nový produkční `.cpp` soubor, takže `client/tests/CMakeLists.txt` ani jiné build manifesty není nutné měnit. Pokud implementátor přesto rozdělí helper do nového souboru, je to změna architektury tohoto plánu a musí ji nejprve zdůvodnit v review.

## Přesná cílová rozhraní

V `layout.h` vznikne toto rozhraní. Výsledek má stejný počet položek jako vstup a neviditelná položka má prázdný `Rect`.

```cpp
enum class FooterSide { leading, trailing };

struct FooterButtonSpec {
    int preferredWidth = 0;
    FooterSide side = FooterSide::trailing;
    bool visible = true;
};

struct ButtonFooterLayout {
    Rect bounds;
    std::vector<Rect> buttons;
    int margin = 0;
    int gap = 0;
    int buttonHeight = 0;
    int rows = 0;
};

int buttonFooterMargin(int width, int dpi);
ButtonFooterLayout computeButtonFooterLayout(
    Size client, int dpi, std::span<const FooterButtonSpec> buttons);
```

Algoritmus `computeButtonFooterLayout` musí zachovat pořadí v každé skupině. Na jednom řádku umístí `leading` zleva a `trailing` zprava. Když preferované šířky nevejdou, trailing skupina zůstane v posledním řádku a leading skupina se přesune do předchozího řádku. Pokud se nevejde ani samostatná skupina, její tlačítka se bezpečně zmenší do dostupné šířky stejným principem jako současný wizard, nejméně na jeden fyzický pixel, bez změny pořadí, překryvu nebo ořezu. `bounds` zahrne horní i dolní 16DIP okraj, všechny 36DIP řádky a 8DIP mezery mezi řádky.

`EditorLayout` se rozšíří takto:

```cpp
struct EditorLayout {
    Rect textArea;
    Rect tabs;
    Rect saveButton;
    Rect resetButton;
    Rect cancelButton;
    Rect heading;
    Rect path;
    Rect footer;
};
```

`computeProgressLayout` bude mít rozhraní bez výšek odstraněných textů:

```cpp
inline ProgressLayout computeProgressLayout(
    int width, int dpi, int actionHeight, bool success,
    bool disconnect, int panelHeadingHeight = 28);
```

`ProgressLayout` ponechá `eyebrow`, kartu, panel, checkpointy a odpočet. Členy `heading` a `description` odstraní. V režimu Disconnect vrátí prázdnou kartu a panel umístěný přímo pod horní text, takže `ProgressWindow::layout` již nebude provádět dodatečný posun obdélníků.

`EditorWindow` doplní:

```cpp
void selectInfoTab(int index);
HWND tabs_ = nullptr;
int infoTab_ = 0;
```

Index `0` je `Who is online`, index `1` je `Raw server information`. `selectInfoTab` přijme pouze 0 nebo 1, aktualizuje `TabCtrl_SetCurSel`, zavolá `ShowWindow(field_, index == 1 ? SW_SHOW : SW_HIDE)`, nepřepisuje text a neposílá žádný požadavek.

---

### Task 1: Zavést sdílenou geometrii tlačítkové patičky

**Files:**

- Modify: `client/apps/tray/layout.h:1-40`
- Modify: `client/apps/tray/layout.cpp:93-137`
- Modify: `client/apps/tray/wizard_layout.cpp:4-36`
- Test: `client/tests/unit/test_wizard_layout.cpp:7-21`
- Test: `client/tests/unit/test_tray_layout.cpp:1-177`

**Interfaces:**

- Consumes: `Size`, `Rect`, `scaleForDpi` a pořadí tlačítek dodané volajícím.
- Produces: `FooterSide`, `FooterButtonSpec`, `ButtonFooterLayout`, `buttonFooterMargin` a `computeButtonFooterLayout` podle přesných signatur výše.

- [ ] **Step 1: Napsat selhávající test normativních metrik**

Do `test_wizard_layout.cpp` přidat `ButtonFooterLayout.MatchesWizardReferenceAtSupportedDpi`. Pro DPI 96, 120, 144 a 192 a logické šířky 320, 480, 600 a 1000 ověřit:

```cpp
const std::array specs{
    FooterButtonSpec{p(88), FooterSide::leading, true},
    FooterButtonSpec{p(88), FooterSide::trailing, true},
    FooterButtonSpec{p(200), FooterSide::trailing, true},
};
const auto footer = computeButtonFooterLayout(client, dpi, specs);
PHPVPN_CHECK_EQ(footer.margin, std::min(client.width / 8, p(16)));
PHPVPN_CHECK_EQ(footer.gap, p(8));
PHPVPN_CHECK_EQ(footer.buttonHeight, p(36));
PHPVPN_CHECK_EQ(footer.buttons.size(), specs.size());
```

Každý viditelný `Rect` musí být uvnitř klientské plochy, mít výšku `p(36)`, nepřekrývat jiný a zachovat pořadí ve své skupině.

- [ ] **Step 2: Spustit test a potvrdit očekávané selhání**

Run:

```powershell
Push-Location client
cmake --preset debug
cmake --build --preset debug --target phpvpn-client-tests --parallel 4
../work/build/debug/tests/phpvpn-client-tests.exe ButtonFooterLayout
Pop-Location
```

Expected: kompilace selže na neexistujících typech `FooterButtonSpec` a `ButtonFooterLayout`.

- [ ] **Step 3: Implementovat čistý helper v `layout.*`**

Implementovat výše uvedené typy a algoritmus. Výpočet musí používat pouze celočíselnou geometrii a `scaleForDpi`, nesmí číst HWND, HDC ani globální stav. `buttons.size()` musí vždy odpovídat počtu vstupních specifikací.

- [ ] **Step 4: Přesměrovat `computeWizardLayout` na helper**

Vytvořit tři specifikace v pořadí Reset, Back, primary. Reset je `leading`, Back a primary jsou `trailing`. Použít šířky 88 DIP, 88 DIP a `max(200 DIP, primaryWidth)`. `WizardLayout::content.bottom` odvodit z `footer.bounds.top`. Ostatní sidebar a compact výpočty ponechat beze změny.

- [ ] **Step 5: Ověřit bezezměnnou wizard geometrii**

Rozšířit existující `WizardLayout.FooterRemainsAccessibleAcrossDpiAndSmallWorkAreas`, aby porovnal obdélníky `computeWizardLayout(...).buttons` s výstupem helperu a ověřil jednořádkový i dvouřádkový režim.

Run:

```powershell
Push-Location client
cmake --build --preset debug --target phpvpn-client-tests --parallel 4
../work/build/debug/tests/phpvpn-client-tests.exe WizardLayout
../work/build/debug/tests/phpvpn-client-tests.exe ButtonFooterLayout
Pop-Location
```

Expected: oba filtry PASS.

- [ ] **Step 6: Commit**

```powershell
git add client/apps/tray/layout.h client/apps/tray/layout.cpp client/apps/tray/wizard_layout.cpp client/tests/unit/test_wizard_layout.cpp client/tests/unit/test_tray_layout.cpp
git commit -m "refactor(tray): share wizard footer geometry"
```

---

### Task 2: Zjednodušit Connect a Disconnect bez změny lifecycle

**Files:**

- Modify: `client/apps/tray/resource.h:13-23`
- Modify: `client/apps/tray/progress_layout.h:7-84`
- Modify: `client/apps/tray/progress_window.h:27-53`
- Modify: `client/apps/tray/progress_window.cpp:32-204,217-293,318-334`
- Test: `client/tests/unit/test_tray_layout.cpp:22-99`
- Test: `client/tests/unit/test_wp03_models.cpp:200-210`
- Test: `client/tests/unit/test_wp03_windows.cpp:190-415`

**Interfaces:**

- Consumes: `buttonFooterMargin`, stávající `ProgressModel::view`, `ProgressModel::onSuccessPresented` a všechny existující panelové texty.
- Produces: novou signaturu `computeProgressLayout`, `IDC_PROGRESS_CONTEXT` a jediný statický horní HWND `eyebrow_`.

- [ ] **Step 1: Upravit čisté layout testy tak, aby nejprve selhaly**

Nahradit stará očekávání 48/64 DIP a `heading`/`description` těmito invarianty pro Connect i Disconnect:

```cpp
const auto l = computeProgressLayout(p(width), dpi, p(72), success, disconnect, p(56));
PHPVPN_CHECK_EQ(l.eyebrow.left, buttonFooterMargin(p(width), dpi));
PHPVPN_CHECK_EQ(p(width) - l.eyebrow.right, buttonFooterMargin(p(width), dpi));
PHPVPN_CHECK(l.card.top >= l.eyebrow.bottom + p(8) || disconnect);
PHPVPN_CHECK(disconnect ? l.card == Rect{} : l.card.bottom < l.panel.top);
PHPVPN_CHECK(l.panel.top >= l.eyebrow.bottom + p(8));
```

V `test_wp03_models.cpp` odstranit kontroly odstraněných obdélníků a ponechat test oddělení karty, panelu a odpočtu.

- [ ] **Step 2: Napsat nativní test statičnosti horního textu**

Do `test_wp03_windows.cpp` přidat `WP03Windows.StaticProgressContextSurvivesEveryState`. Test otevře Connect, uloží HWND `IDC_PROGRESS_CONTEXT`, pošle connecting, succeeded, failed a status-unavailable snímek a po každém ověří přesný text `Connecting client to the server.`. Nově otevřený Disconnect ověří `Disconnecting client from the server.` při waiting, success a failure. Enumerace potom potvrdí nepřítomnost starých horních textů `Connection status`, `Connecting`, `Successfully connected`, `Connection failed`, `VPN connection status` a `The VPN connection is ready.`.

- [ ] **Step 3: Spustit nové testy a potvrdit červený stav**

Run:

```powershell
Push-Location client
cmake --preset debug -DPHPVPN_BUILD_UI_TESTS=ON
cmake --build --preset debug --target phpvpn-client-tests phpvpn-wp03-ui-tests --parallel 4
../work/build/debug/tests/phpvpn-client-tests.exe TrayLayout
../work/build/debug/tests/phpvpn-client-tests.exe WP03Models
../work/build/debug/tests/phpvpn-wp03-ui-tests.exe StaticProgressContext
Pop-Location
```

Expected: čisté testy selžou na staré signatuře a okrajích, nativní test selže na chybějícím `IDC_PROGRESS_CONTEXT` nebo starém textu.

- [ ] **Step 4: Odstranit nadbytečné ovládací prvky a přepočítat layout**

V `ProgressWindow::open` vytvořit pouze `eyebrow_` s ID `IDC_PROGRESS_CONTEXT`. Při každém `open` mu nastavit přesný text podle `disconnect_`. `heading_` a `description_` nevytvářet, neukládat a neumisťovat. V `updateResources` ponechat pro horní text `ui::Font::subtitle`. V `refresh` neměnit horní text a veškerou dynamiku ponechat v `panelHeading_`, `panelAction_`, `countdown_`, detailech a checkpointech.

- [ ] **Step 5: Přesunout hranici úspěšného zobrazení na výsledkový panel**

`headingProcedure` připojit pouze k `panelHeading_`. Podmínku úspěšného paintu změnit na:

```cpp
if (h == self->panelHeading_ && self->model_.view().stage == ProgressStage::succeeded)
    self->model_.onSuccessPresented(nowMs());
```

Zachovat požadavek na skutečný `WM_PAINT`, viditelné protínající se HWND a neminimalizovaný rodič. `WM_PRINT` ani přijetí snímku deadline nespustí. `ProgressModel`, délka 3000 ms, 500ms timer a uzavření 3000 až 3500 ms se nemění.

- [ ] **Step 6: Ověřit nativní auto-close a resize**

Aktualizovat `WP03Windows.ActualPaintStartsCountdownAfterHiddenSuccessAndClosesInBounds`, aby paintoval viditelný `panelHeading_` namísto odstraněného velkého nadpisu. Zachovat scénáře hidden, minimized, first visible paint, sleep/resume a nulový Disconnect.

Run:

```powershell
Push-Location client
cmake --build --preset debug --target phpvpn-client-tests phpvpn-wp03-ui-tests --parallel 4
../work/build/debug/tests/phpvpn-client-tests.exe TrayLayout
../work/build/debug/tests/phpvpn-client-tests.exe WP03Models
../work/build/debug/tests/phpvpn-wp03-ui-tests.exe WP03Windows
Pop-Location
```

Expected: PASS. Log musí stále dokládat auto-close v intervalu 3000 až 3500 ms.

- [ ] **Step 7: Commit**

```powershell
git add client/apps/tray/resource.h client/apps/tray/progress_layout.h client/apps/tray/progress_window.h client/apps/tray/progress_window.cpp client/tests/unit/test_tray_layout.cpp client/tests/unit/test_wp03_models.cpp client/tests/unit/test_wp03_windows.cpp
git commit -m "feat(tray): simplify connection progress headers"
```

---

### Task 3: Přidat dvě karty Server Information

**Files:**

- Modify: `client/apps/tray/resource.h:13-23`
- Modify: `client/apps/tray/layout.h:33-40`
- Modify: `client/apps/tray/layout.cpp:108-137`
- Modify: `client/apps/tray/editor_window.h:21-76`
- Modify: `client/apps/tray/editor_window.cpp:256-500`
- Test: `client/tests/unit/test_tray_layout.cpp:100-157`
- Test: `client/tests/unit/test_wp03_windows.cpp:455-512`
- Test: `client/tests/ui/tray_ui_tests.cpp:1360-1422`

**Interfaces:**

- Consumes: již načtený a zformátovaný řetězec předaný `EditorWindow::open`, `WC_TABCONTROLW`, `TCN_SELCHANGE`, `TabCtrl_AdjustRect` a existující `EditorWindow::translate`.
- Produces: `IDC_INFO_TABS`, `EditorLayout::tabs`, `EditorWindow::tabs_`, `EditorWindow::infoTab_` a `EditorWindow::selectInfoTab(int)`.

- [ ] **Step 1: Napsat selhávající čistý layout test pro read-only režim**

Rozšířit `TrayLayout.EditorReferenceTopPreservesHeaderAndFooterAtSupportedDpi` a resize testy tak, aby read-only layout obsahoval neprázdný `tabs`, nadpis byl nad ním, tab control byl nad patičkou a editable režim měl `tabs == Rect{}`.

```cpp
const auto info = computeEditorLayout(size, dpi, false);
PHPVPN_CHECK(info.heading.bottom <= info.tabs.top);
PHPVPN_CHECK(info.tabs.bottom <= info.footer.top);
PHPVPN_CHECK(inside(info.tabs, size));
PHPVPN_CHECK_EQ(computeEditorLayout(size, dpi, true).tabs, Rect{});
```

- [ ] **Step 2: Napsat nativní test pořadí, výchozí karty a JSONu**

Rozšířit `WP03Windows.EditorPreservesNulSelectionDirtyAndSeparatesShowInfo` nebo přidat samostatný `WP03Windows.ServerInformationTabsAreNativeAndDataStable`. Test musí:

1. otevřít read-only `EditorWindow` s textem `{"server":"unchanged"}`,
2. získat `IDC_INFO_TABS` a ověřit `TabCtrl_GetItemCount(...) == 2`,
3. přečíst přes `TCM_GETITEMW` texty v pořadí `Who is online`, `Raw server information`,
4. ověřit výchozí index 0 a skrytý `IDC_TEXT_AREA`,
5. přepnout šipkou doprava a myší na druhý tab a ověřit index 1, viditelný read-only multiline editor, oba scrollbary a nezměněný text,
6. uložit původní clipboard, označit text a ověřit, že běžná copy operace vloží přesně vybraný text do testovacího clipboardu, potom původní clipboard obnovit i při selhání testu,
7. přepnout zpět a ověřit, že JSON není viditelný, ale po návratu na druhou kartu má stejný obsah, selection a scroll pozici,
8. ověřit Tab, Shift+Tab, Enter na OK a Escape bez změny dat.

- [ ] **Step 3: Napsat produkční IPC regresi bez opakovaného načtení**

V `tray_ui_tests.cpp` přidat filtr `plan_v001_server_information_tabs`. Použít privátní backend a skutečnou `TrayApplication` akci `IDM_SHOW_INFO`. Po otevření uložit počet `get_info`, přepnout kartu nejméně desetkrát myší i klávesnicí a ověřit, že počet zůstane 1 a žádný jiný neočekávaný příkaz nevznikne.

- [ ] **Step 4: Spustit testy a potvrdit očekávané selhání**

Run:

```powershell
Push-Location client
cmake --build --preset debug --target phpvpn-client-tests phpvpn-wp03-ui-tests phpvpn-tray-ui-tests --parallel 4
../work/build/debug/tests/phpvpn-client-tests.exe TrayLayout
../work/build/debug/tests/phpvpn-wp03-ui-tests.exe ServerInformationTabs
../work/build/debug/tests/phpvpn-tray-ui-tests.exe plan_v001_server_information_tabs
Pop-Location
```

Expected: selhání na chybějícím `IDC_INFO_TABS`, nulovém počtu karet nebo viditelném JSONu na výchozí kartě.

- [ ] **Step 5: Implementovat nativní tab control pouze v read-only režimu**

V `EditorWindow::open` vytvořit `WC_TABCONTROLW` s `WS_CHILD | WS_VISIBLE | WS_TABSTOP | TCS_FOCUSONBUTTONDOWN` pouze když `save_` není nastaven. Vložit dvě `TCITEMW` v určeném pořadí, nastavit index 0 a skrýt `field_`. Editable režim nesmí tab control vytvořit.

V `EditorWindow::procedure` obsloužit `WM_NOTIFY` pouze pro `IDC_INFO_TABS` a `TCN_SELCHANGE`, přečíst `TabCtrl_GetCurSel` a zavolat `selectInfoTab`. Žádný handler nesmí volat `setContent`, čtečku ani callback Save.

- [ ] **Step 6: Zapojit layout, paint, fokus a lifecycle**

V read-only režimu umístit tab control do `EditorLayout::tabs`. Pro umístění editoru použít kopii `tabs` převedenou přes `TabCtrl_AdjustRect(tabs_, FALSE, &page)` a stávající bezpečný inset. V `WM_PAINT` nekreslit editable rounded editor frame přes nativní tab page. Kliknutí do prázdné stránky nesmí přesunout fokus na skrytý editor. Otevření Server Information nastaví fokus na tab control. Při `WM_DESTROY` nulovat `tabs_` a `infoTab_`.

- [ ] **Step 7: Ověřit testy**

Spustit stejné tři příkazy ze Step 4. Expected: PASS, jeden `get-info`, výchozí prázdná stránka, stabilní JSON a funkční OK.

- [ ] **Step 8: Commit**

```powershell
git add client/apps/tray/resource.h client/apps/tray/layout.h client/apps/tray/layout.cpp client/apps/tray/editor_window.h client/apps/tray/editor_window.cpp client/tests/unit/test_tray_layout.cpp client/tests/unit/test_wp03_windows.cpp client/tests/ui/tray_ui_tests.cpp
git commit -m "feat(tray): add server information tabs"
```

---

### Task 4: Převést EditorWindow a Password SHA na sdílenou patičku

**Files:**

- Modify: `client/apps/tray/layout.cpp:108-137`
- Modify: `client/apps/tray/editor_window.cpp:329-369,447-459`
- Modify: `client/apps/tray/password_sha_window.cpp:110-135`
- Test: `client/tests/unit/test_tray_layout.cpp:100-177`
- Test: `client/tests/unit/test_wp03_windows.cpp:236-308,455-512`
- Test: `client/tests/ui/tray_ui_tests.cpp:372-389,1106-1142,1360-1422`

**Interfaces:**

- Consumes: `computeButtonFooterLayout` a současné HWND pořadí Editoru a Password SHA.
- Produces: skutečné obdélníky obou patiček se stejnými metrikami jako wizard.

- [ ] **Step 1: Napsat selhávající geometrické scénáře**

Do `test_tray_layout.cpp` přidat sady specifikací:

```cpp
const std::array editor{
    FooterButtonSpec{p(104), FooterSide::leading, true},
    FooterButtonSpec{p(104), FooterSide::trailing, true},
    FooterButtonSpec{p(104), FooterSide::trailing, true},
};
const std::array info{
    FooterButtonSpec{p(104), FooterSide::trailing, true},
};
const std::array password{
    FooterButtonSpec{p(104), FooterSide::leading, true},
    FooterButtonSpec{p(104), FooterSide::leading, true},
    FooterButtonSpec{p(104), FooterSide::trailing, true},
};
```

Pro 96, 120, 144 a 192 DPI a logické šířky 320, 480, 640, 1080 ověřit vnější okraje, 8DIP mezery, 36DIP výšku, nepřekryv, bezpečné zalomení a `textArea` nebo body končící nejvýše na `footer.bounds.top`.

- [ ] **Step 2: Zpřísnit nativní helper `footer`**

V `tray_ui_tests.cpp:372-389` rozšířit kontrolu tak, aby vedle existence a nepřekryvu ověřila 16DIP krajní okraje, 36DIP výšku a 8DIP mezeru mezi sousedními tlačítky stejného řádku s tolerancí jednoho fyzického pixelu. Přidat `plan_v001_editor_password_footers`, který provede resize před a po zalomení a ověří Tab pořadí i aktivaci tlačítek.

- [ ] **Step 3: Spustit testy a potvrdit staré 32DIP a 24DIP okraje**

Run:

```powershell
Push-Location client
cmake --build --preset debug --target phpvpn-client-tests phpvpn-wp03-ui-tests phpvpn-tray-ui-tests --parallel 4
../work/build/debug/tests/phpvpn-client-tests.exe TrayLayout
../work/build/debug/tests/phpvpn-wp03-ui-tests.exe ReferenceGeometry
../work/build/debug/tests/phpvpn-tray-ui-tests.exe plan_v001_editor_password_footers
Pop-Location
```

Expected: test odhalí starý 32DIP editorový okraj nebo 24DIP Password SHA okraj.

- [ ] **Step 4: Zapojit EditorWindow**

`computeEditorLayout` sestaví Reset jako `leading`, Cancel a Save jako `trailing`, nebo pouze OK jako `trailing`. `footer`, tlačítkové obdélníky a spodní hranu obsahu převezme z helperu. Při úzké ploše se Reset přesune do horního řádku stejně jako ve wizardu. Paint oddělovače používá `oldLayout_.footer.top`.

- [ ] **Step 5: Zapojit PasswordShaWindow**

V `PasswordShaWindow::layout` sestavit Default a Random jako `leading`, OK jako `trailing`. Body ukončit na `footer.bounds.top`, scrollbar `nPage` odvodit z této hodnoty a všechny tři HWND umístit podle vrácených obdélníků. Obsahové souřadnice a význam tlačítek se nemění.

- [ ] **Step 6: Ověřit resize, fokus a obsahové scrollbary**

Spustit stejné testy ze Step 3 a navíc:

```powershell
Push-Location client
../work/build/debug/tests/phpvpn-tray-ui-tests.exe live_resize_repaint
../work/build/debug/tests/phpvpn-tray-ui-tests.exe wp06_password_children_parent_ok
Pop-Location
```

Expected: PASS bez překryvu, ořezu, změny focusu nebo nevybarveného pruhu.

- [ ] **Step 7: Commit**

```powershell
git add client/apps/tray/layout.cpp client/apps/tray/editor_window.cpp client/apps/tray/password_sha_window.cpp client/tests/unit/test_tray_layout.cpp client/tests/unit/test_wp03_windows.cpp client/tests/ui/tray_ui_tests.cpp
git commit -m "refactor(tray): standardize editor and password footers"
```

---

### Task 5: Převést Communication Contracts na sdílenou dynamickou patičku

**Files:**

- Modify: `client/apps/tray/contracts_window.cpp:322-387,425-515,576-589`
- Test: `client/tests/unit/test_tray_layout.cpp`
- Test: `client/tests/ui/tray_ui_tests.cpp:1106-1142,1360-1422`
- Test: `client/tests/ui/dev_preview.inc:265-320`

**Interfaces:**

- Consumes: `computeButtonFooterLayout`, `measuredButtonWidth`, `IsWindowVisible(check_)`, `IsWindowVisible(repeat_)`, dynamický `saveText_` a stávající error text.
- Produces: patičku Check, Repeat, Cancel a Save se zachovanými skupinami a výškou odvozenou od skutečného počtu řádků.

- [ ] **Step 1: Napsat selhávající čistou sadu pro všechny stavy Contracts**

V `test_tray_layout.cpp` pokrýt čtyři viditelnostní varianty: běžný Cancel+Save, unknown s Check, repeat s Repeat a současný Check+Repeat. Použít preferované šířky 236 DIP, 164 DIP, 104 DIP a `max(180 DIP, measured Save width)`. Pro šířky 320, 480, 700, 900 a 1180 DIP ověřit, že skryté položky mají prázdný obdélník, trailing Cancel+Save drží pořadí a obsah nikdy neklesne do `footer.bounds`.

- [ ] **Step 2: Přidat nativní test dynamického přelomu**

Přidat `plan_v001_contracts_footer_states`. Otevřít reálný Contracts preview v Default, Saving a SaveUnknown, změnit šířku kolem bodu zalomení a ověřit skutečné HWND. Kontrola musí potvrdit, že zobrazení Check nebo Repeat změní počet řádků a výšku patičky, ale nezmění text, enabled stav, pořadí, výsledek Save ani Tab order.

- [ ] **Step 3: Spustit test a potvrdit odchylku starého layoutu**

Run:

```powershell
Push-Location client
cmake --build --preset debug --target phpvpn-client-tests phpvpn-tray-ui-tests --parallel 4
../work/build/debug/tests/phpvpn-client-tests.exe ButtonFooterLayout
../work/build/debug/tests/phpvpn-tray-ui-tests.exe plan_v001_contracts_footer_states
Pop-Location
```

Expected: starý Contracts layout selže na 32DIP okraji nebo na vlastní 60/148DIP výšce.

- [ ] **Step 4: Nahradit lokální výpočet patičky**

V `ContractsWindow::layout` změřit Save přes stávající `measuredButtonWidth`, sestavit specifikace Check a Repeat jako `leading`, Cancel a Save jako `trailing` a předat jejich aktuální viditelnost helperu. Tlačítka umístit přímo z výsledku.

Error region umístit bezprostředně nad `footer.bounds.top`. `body_` ukončit nad error regionem, nebo přímo na `footer.bounds.top`, když je error prázdný. `SCROLLINFO::nPage`, `bodyExtent`, `tableBottom` a pager počítat z nové spodní hrany body. Divider v `paint` ponechat na skutečné hraně body, aby při změně počtu řádků nezůstal starý pruh.

- [ ] **Step 5: Ověřit dynamic state a regresní suite**

Run:

```powershell
Push-Location client
../work/build/debug/tests/phpvpn-tray-ui-tests.exe plan_v001_contracts_footer_states
../work/build/debug/tests/phpvpn-tray-ui-tests.exe wp06_unknown_check_confirmed_repeat
../work/build/debug/tests/phpvpn-tray-ui-tests.exe wp06_saving_original_deadline
../work/build/debug/tests/phpvpn-tray-ui-tests.exe live_resize_repaint
Pop-Location
```

Expected: PASS. Žádný test nesmí změnit 30sekundový Save deadline nebo lifecycle.

- [ ] **Step 6: Commit**

```powershell
git add client/apps/tray/contracts_window.cpp client/tests/unit/test_tray_layout.cpp client/tests/ui/tray_ui_tests.cpp client/tests/ui/dev_preview.inc
git commit -m "refactor(tray): standardize contracts footer"
```

---

### Task 6: Doplnit preview, normativní dokumentaci a úplné ověření

**Files:**

- Modify: `client/tests/ui/dev_preview.inc:4-320`
- Modify: `client/tests/ui/tray_ui_tests.cpp:1106-1142,1360-1422`
- Modify: `client/tests/ui/README.md:1-114`
- Modify: `tray_UX.md:1-20,82-105,131-175,200-208`
- Modify: `client.md:942-966,1028-1031`

**Interfaces:**

- Consumes: produkční okna, privátní syntetickou pipe, stávající `devScenes`, `DevPreview::advance`, `liveResizeRepaint` a dokumentované UX14 meze důkazu.
- Produces: reprodukovatelné `Disconnect` a `Info` preview, finální test filtr `plan_v001_` a aktualizovanou normativní dokumentaci.

- [ ] **Step 1: Přidat preview scény pro chybějící vizuální stavy**

Rozšířit `devScenes` o:

```cpp
{"Disconnect", {"Disconnecting", "Disconnected", "Failed"}},
{"Info", {"Who", "Raw"}},
```

`Disconnect` musí otevřít produkční `ProgressWindow` v disconnect režimu nad syntetickým stavem a nesmí používat skutečnou službu. `Info` musí spustit skutečnou `IDM_SHOW_INFO`, počkat na právě jeden syntetický `get-info` a ve scéně Raw přepnout na druhou kartu až po vytvoření okna. `DevPreview::window` musí obě okna vracet jednoznačně. `liveResizeRepaint` rozšířit o `Disconnect` a `Info`.

- [ ] **Step 2: Aktualizovat README pro nativní GUI**

Do tabulky pohledů přidat oba nové pohledy a tyto příklady:

```powershell
./client/tools/dev-tray.ps1 -View Disconnect -Scene Disconnecting
./client/tools/dev-tray.ps1 -View Info -Scene Who
./client/tools/dev-tray.ps1 -View Info -Scene Raw
./client/tools/dev-tray.ps1 -Mode Test -Filter plan_v001_
```

Zachovat tvrzení, že harness nepoužívá ProgramData, HTTP, SCM, Wintun ani routy a že screenshoty nepatří do repozitáře.

- [ ] **Step 3: Aktualizovat `tray_UX.md`**

V UX05 nahradit kompozici `Connection status` + velký nadpis + popis jediným statickým horním textem Connect. Popsat stejný kontrakt Disconnect, nové symetrické wizardové okraje a skutečnost, že hranici auto-close tvoří první skutečný paint viditelného úspěšného panelu. Katalog chyb a panelové texty ponechat beze změny.

V UX06 doplnit Server Information s kartami, výchozí prázdnou stránkou, raw JSON stránkou, myší, klávesnicí, fokusovým pořadím a nulovým novým načtením. Doplnit společný kontrakt patiček pro Wizard, Edit Config, Server Information, Contracts a Password SHA.

- [ ] **Step 4: Aktualizovat `client.md`**

U položky Show Info popsat dvě karty a jediný existující `get-info`. V Connect pasážích odstranit normativní požadavek na velké dynamické nadpisy a popisy, ale zachovat přesné panelové texty, checkpointy, bezpečné chyby, polling, config read a auto-close. Přidat Disconnect statický text a panelovou dynamiku. V akceptačních bodech rozšířit testy o patičky, karty a DPI. `protocol.md` neupravovat.

- [ ] **Step 5: Spustit cílenou Debug bránu**

Run:

```powershell
Push-Location client
cmake --preset debug -DPHPVPN_BUILD_UI_TESTS=ON
cmake --build --preset debug --target phpvpn-client-tests phpvpn-wp03-ui-tests phpvpn-tray-ui-tests --parallel 4
../work/build/debug/tests/phpvpn-client-tests.exe TrayLayout
../work/build/debug/tests/phpvpn-client-tests.exe WizardLayout
../work/build/debug/tests/phpvpn-client-tests.exe WP03Models
../work/build/debug/tests/phpvpn-wp03-ui-tests.exe WP03Windows
../work/build/debug/tests/phpvpn-tray-ui-tests.exe plan_v001_
../work/build/debug/tests/phpvpn-tray-ui-tests.exe live_resize_repaint
../work/build/debug/tests/phpvpn-tray-ui-tests.exe wp06_scene_matrix
Pop-Location
```

Expected: všechny filtry PASS, scéna matrix zahrnuje nové pohledy a nulový počet vybraných testů zůstává chybou harnessu.

- [ ] **Step 6: Spustit úplný automatizovaný Debug a Release rozsah**

Run:

```powershell
Push-Location client
ctest --preset debug --output-on-failure
cmake --preset release -DPHPVPN_BUILD_UI_TESTS=ON
cmake --build --preset release --target phpvpn-client-tests phpvpn-wp03-ui-tests phpvpn-tray-ui-tests --parallel 4
ctest --preset release --output-on-failure
../work/build/release/tests/phpvpn-wp03-ui-tests.exe WP03Windows
../work/build/release/tests/phpvpn-tray-ui-tests.exe plan_v001_
../work/build/release/tests/phpvpn-tray-ui-tests.exe live_resize_repaint
../work/build/release/tests/phpvpn-tray-ui-tests.exe wp06_scene_matrix
Pop-Location
```

Expected: všechny automatizované testy PASS. Chráněné nebo interaktivní scénáře mohou být pouze explicitně vypsané `NOT RUN` s důvodem.

- [ ] **Step 7: Provést vizuální a DPI matici na interaktivním Windows desktopu**

Pro 100, 125, 150 a 200 % DPI otevřít `Connect/Connecting`, `Connect/Connected`, `Connect/Failed`, všechny tři Disconnect scény, `Info/Who`, `Info/Raw`, `Editor/Default`, `Wizard/Url`, `Wizard/Secret`, `Contracts/Default`, `Contracts/SaveUnknown` a `Password/Default`. Na každém dostupném DPI:

1. zkontrolovat přesné statické horní texty a absenci prázdného prostoru,
2. porovnat levý a pravý okraj progress karet s krajními wizard tlačítky,
3. přepnout Info myší, šipkami, Tab a Shift+Tab a zkopírovat raw JSON,
4. změnit šířku přes body zalomení 320, 480, 640, 760, 900 a výchozí šířku daného okna,
5. táhnout každou hranu i roh, maximalizovat, obnovit a přesunout mezi monitory s odlišným DPI,
6. ověřit scrollbary, focus ring, Enter, Escape, enabled stav a nepřekryv obsahu s patičkou,
7. uložit případné screenshoty do jedinečného `work/logs/tray-plan-v001/<timestamp>/`.

Výsledný report musí pro každou kombinaci uvést OS build, skutečné DPI z `GetDpiForWindow`, scénu, rozměr, RUN/PASS/FAIL nebo NOT RUN a důvod. Automatizovaný čistý výpočet pro DPI nenahrazuje fyzické cross-monitor ověření.

- [ ] **Step 8: Provést bezpečnostní a rozsahový audit diffu**

Run:

```powershell
git diff --check
git diff -- client/apps/tray client/tests client.md tray_UX.md
rg -n "get-info|get_info|IpcCommand|SERVER_SECRET|ACCESS_KEY|PAYLOAD_KEY" client/apps/tray/editor_window.* client/apps/tray/progress_window.*
```

Ověřit, že přepínání karet nevolá IPC, progress nezobrazuje tajemství, žádný log neobsahuje JSON a změna nezasáhla server, službu, kryptografii nebo wire soubory.

- [ ] **Step 9: Commit**

```powershell
git add client/tests/ui/dev_preview.inc client/tests/ui/tray_ui_tests.cpp client/tests/ui/README.md tray_UX.md client.md
git commit -m "docs(test): verify unified tray dialogs"
```

---

## Rizika a pojistky

| Riziko | Pojistka v plánu |
|---|---|
| Odstranění velkého nadpisu zruší paint událost, která spouští auto-close. | Task 2 přesouvá jedinou hranici na viditelný `panelHeading_` a znovu spouští hidden/minimized/WM_PRINT/native timing test. |
| Native tab control změní Tab pořadí nebo Enter zavře okno před přepnutím. | Server Information začíná fokusem na tab controlu, používá `IsDialogMessageW` a nativní test ověřuje šipky, Tab, Shift+Tab, Enter a Escape. |
| Skrytý raw editor zůstane viditelný přes paint nebo focus. | Výchozí `ShowWindow(field_, SW_HIDE)`, click guard kontroluje viditelnost a test ověřuje skutečné HWND i screenshot obou karet. |
| Patička s dynamickým Check/Repeat překryje error nebo tabulku. | Contracts odvozuje body a error region z `footer.bounds.top`, testuje všechny viditelnostní kombinace a body scrollbar. |
| Sdílený helper nechtěně změní wizard. | Wizard je první referenční volající a jeho staré obdélníky jsou uzamčené ekvivalenčním testem před migrací ostatních oken. |
| Úzká plocha zmenší tlačítka pod použitelnou šířku. | Preferované šířky se nejprve skládají do řádků. Zmenšení je až poslední bezpečný krok a nativní test kontroluje text, ořez a aktivaci při 320 a 480 DIP. |
| Resize nebo DPI zničí selection, undo nebo scroll pozici JSONu. | Produkční HWND se při layoutu znovu nevytvářejí. Nativní test přepíná karty po resize a kontroluje obsah, selection, scroll a fokus. |
| Dokumentace bude dál vyžadovat odstraněné dynamické texty. | Task 6 aktualizuje současně `tray_UX.md` i `client.md` a závěrečný audit hledá staré normativní formulace. |
| Vizuální kontrola bude zaměněna za plné runtime ověření. | Evidence odděluje čisté DPI testy, syntetický Win32 harness a fyzickou monitorovou matici. Neprovedené položky zůstávají `NOT RUN`. |

## Pokrytí akceptačních kritérií

| Kritérium intentu | Realizační a ověřovací bod |
|---|---|
| 1 až 4, statický Connect/Disconnect a zachovaná dynamika | Task 2, čisté layout testy, `StaticProgressContextSurvivesEveryState`, stávající katalogové a auto-close testy. |
| 5, shodné symetrické okraje progress panelů | Task 1 helper a Task 2 testy na 96/120/144/192 DPI a šířky před i po breakpointu. |
| 6 a 7, výchozí prázdná karta a raw JSON | Task 3 nativní tab test a Info preview Who/Raw. |
| 8, myš, klávesnice a bez nového requestu | Task 3 `plan_v001_server_information_tabs` s přesně jedním `get-info`. |
| 9, všechny vlastní patičky | Tasks 1, 4 a 5, exhaustivně ověřený seznam Wizard, Editor/Info, Contracts a Password SHA. |
| 10, DPI, resize, scrollbary a artefakty | Tasks 4 až 6, pure geometry, live resize a fyzická DPI matice. |
| 11, klávesy, fokus, enabled stav a význam tlačítek | Nativní footer a Server Information testy v Tasks 3 až 5. |
| 12, požadované automatizované pokrytí | Debug a Release filtry `TrayLayout`, `WizardLayout`, `WP03Models`, `WP03Windows`, `plan_v001_`, `live_resize_repaint` a `wp06_scene_matrix`. |
| 13, existující relevantní testy a poctivé NOT RUN | Task 6 úplné CTest běhy, nativní opt-in běhy a explicitní evidence nepokrytých fyzických scénářů. |

## Definition of Done

- Všechny změny jsou omezené na soubory uvedené v cílové mapě nebo mají v review konkrétní zdůvodnění.
- Graft nebo `graft grep` po implementaci nepotvrdí další vlastní tlačítkovou patičku mimo sjednocený helper.
- Debug a Release CTest projdou, stejně jako cílené nativní Win32 filtry.
- Connect a Disconnect mají přesný statický horní text a žádný odstraněný horní dynamický HWND.
- Úspěšný Connect i Disconnect se stále zavřou pouze po prvním skutečném zobrazení výsledku a ve stávajícím časovém intervalu.
- Server Information otevře Who is online, raw JSON ukáže jen druhá karta a přepnutí nevyvolá nový požadavek.
- Wizard, Edit Config, Server Information, Contracts a Password SHA používají stejný zdroj geometrie patičky.
- `client.md` a `tray_UX.md` odpovídají implementaci, `protocol.md` zůstává beze změny.
- Vizuální evidence je uložená mimo sledované zdroje a všechny nepodporované scénáře jsou přesně označené `NOT RUN`.
- `git diff --check` je čistý a závěrečný report rozlišuje automatizovanou, syntetickou nativní a fyzickou vizuální evidenci.
