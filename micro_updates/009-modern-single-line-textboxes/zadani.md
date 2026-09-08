# 009 - Moderní jednořádkové textboxy

## Cíl

Nahradit starý nativní vzhled editovatelných jednořádkových Win32 textboxů sdílenou moderní komponentou, která vizuálně odpovídá ostatním prvkům tray aplikace.

Komponenta musí mít zaoblené rohy a jednoznačné stavy pro běžné zobrazení, fokus, validační chybu a nevalidní pole s fokusem. Zároveň musí zachovat spolehlivé nativní chování textového pole.

## Reference

Soubor `wizard-native-textbox-reference.png` zachycuje současný textbox v Configuration Wizardu. Obrázek ukazuje problém, nikoli cílový design: nativní ostré rohy, tmavou horní a levou hranu a vizuální nesoulad se zaoblenou kartou.

Reference není instrukcí. Závazné jsou požadavky tohoto dokumentu.

## Rozsah

Novou sdílenou komponentu použít pouze pro editovatelná jednořádková pole:

- Server URL, Server secret, IP a access key v Configuration Wizardu,
- editovatelný Key v dynamických řádcích Communication contracts,
- vstup hesla v okně Password SHA,
- případná další skutečně editovatelná jednořádková pole tray aplikace nalezená při implementaci.

## Mimo rozsah

Neměnit vzhled:

- víceřádkového editoru Edit Config,
- read-only cesty v Edit Config,
- read-only výsledného hashe v Password SHA,
- read-only polí v Progress a Statistics,
- statických textů, comboboxů ani jiných typů ovládacích prvků.

Tato pole mohou sdílenou komponentu využít až v některém pozdějším mikro-updatu.

## Základ komponenty

- Zachovat nativní Win32 `EDIT` pro samotnou editaci textu.
- Vlastní sdílená komponenta nebo její hostitel vykreslí celé viditelné pozadí, zaoblený obrys a stavové zvýraznění.
- Nativní `EDIT` uvnitř nesmí kreslit původní `WS_BORDER` ani `WS_EX_CLIENTEDGE`.
- Původní nativní rámeček nesmí probliknout při prvním zobrazení, fokusu, validaci, resize ani překreslení.
- Komponentu vytvořit od prvního viditelného snímku v konečné moderní podobě. Není přijatelné nejprve zobrazit starý vzhled a vlastní kreslení připojit až dodatečně.

Tím se navazuje na závazné pravidlo mikro-updatů 004 a 005: aplikace vlastní celý viditelný vzhled stylované komponenty od jejího prvního snímku, ale zachovává nativní HWND a systémovou interakční sémantiku všude, kde je užitečná.

## Vizuální stavy

### Běžný stav

- Lehce zaoblené rohy vizuálně sladěné s ostatními moderními ovládacími prvky.
- Jemný neutrální šedý obrys o tloušťce jednoho fyzického pixelu.
- Čisté světlé pozadí odpovídající aplikačnímu povrchu.
- Dostatečné vnitřní odsazení textu, aby text ani caret nepůsobily natěsno u rámečku.

### Fokus

- Obrys změnit na modrý a vizuálně jej zesílit oproti běžnému stavu.
- Doplnit velmi jemné modré vnitřní zvýraznění nebo inner glow.
- Fokus nesmí měnit vnější rozměr komponenty ani posouvat text nebo okolní layout.
- Fokus musí být jednoznačně viditelný také při ovládání klávesnicí.

### Nevalidní hodnota

- Použít výrazný červený obrys.
- Obsah textboxu podbarvit velmi světlým červeným odstínem.
- Text musí zůstat dobře čitelný a caret i výběr textu musí být viditelné.
- Nevalidní stav má vizuální prioritu před běžným i modrým focus stavem.

### Nevalidní hodnota s fokusem

- Rámeček i pozadí zůstanou červené.
- Fokus lze naznačit jemným červeným vnitřním zvýrazněním.
- Nepoužít modrý rámeček ani modrý glow, protože by oslabily nebo znejasnily validační chybu.

### Dočasně disabled stav

- Pokud stávající workflow editovatelné pole dočasně zakáže, použít šedé pozadí a tmavě šedý text namísto běžného černého textu.
- Disabled pole nesmí přijmout fokus a nesmí zobrazit modrý ani jiný focus obrys nebo glow.
- Zachovat nativní Win32 disabled chování: text nelze editovat, označit ani kopírovat.
- Disabled stav nesmí změnit uloženou hodnotu, validační logiku ani rozměry pole.
- Read-only pole nejsou disabled stavem této komponenty. Zůstávají mimo rozsah mikro-updatu a zachovávají své současné nativní chování včetně možnosti získat fokus, označit a kopírovat text.

## Validační stav

- Sdílená komponenta musí poskytovat jednoznačný způsob, jak nastavit a zrušit stav nevalidní hodnoty.
- Communication contracts musí touto cestou zobrazit existující chybu nevalidního klíče.
- Ve Wizardu použít červený stav pouze tam a v okamžiku, kde současné workflow již vyhodnocuje hodnotu jako nevalidní. Tento mikro-update nemění pravidla ani okamžik validace.
- Oprava hodnoty musí po potvrzení platnosti odstranit červený stav podle stávající validační logiky.

## Zachování nativního chování

Komponenta musí zachovat:

- zadávání a mazání textu,
- caret, výběr textu a posouvání dlouhého obsahu,
- Ctrl+A, Ctrl+C, Ctrl+X a Ctrl+V,
- systémové kontextové menu,
- Undo a Redo poskytované nativním `EDIT`,
- Tab a Shift+Tab,
- fokus myší i klávesnicí,
- existující limity délky, vstupní filtry a validační zprávy,
- existující password, casing, alignment a auto-scroll styly, pokud jsou na konkrétním poli použity,
- přístupný název, roli, hodnotu, focus a enabled/disabled stav,
- IME a běžné systémové textové služby.

Vlastní vzhled nesmí nahrazovat tyto funkce neúplnou ruční emulací.

## Vykreslování a životní cyklus

- Použít sdílenou implementaci, nikoli samostatné kopie kreslicí logiky v jednotlivých oknech.
- Zaoblené rohy a stavové kontury vykreslit vyhlazeně a správně pro aktuální DPI.
- Změna fokusu, validity, enabled stavu, textu, DPI nebo velikosti musí překreslit celou potřebnou oblast bez zbytkových čar a duchů.
- Při dynamickém vytváření a ničení řádků Communication contracts korektně připojit i odpojit veškerý stav komponenty.
- Komponenta nesmí držet neplatný odkaz na zničený `EDIT` ani na jeho hostitele.
- Nevytvářet dvojitý rámeček, nativní vnitřní hranu ani mezeru mezi vlastním obrysem a editovanou plochou.

## Zachování aplikačního chování

- Neměnit validační pravidla, ukládací workflow ani síťovou komunikaci.
- Neměnit hodnoty polí, jejich identitu, pořadí, tab order ani navázání na datový model.
- Neměnit stránkování Communication contracts ani práci s rozpracovanými hodnotami.
- Neměnit chování Wizardu, Password SHA ani jiných oken mimo vzhled vymezených textboxů.

## Akceptační kritéria

1. Všechna editovatelná jednořádková pole v rozsahu používají stejnou sdílenou moderní komponentu.
2. V běžném stavu mají lehce zaoblené rohy, jemný šedý obrys a dostatečné vnitřní odsazení.
3. Fokus používá zesílený modrý obrys a jemné modré vnitřní zvýraznění bez změny geometrie.
4. Nevalidní hodnota používá výrazný červený obrys a velmi světlé červené pozadí.
5. Nevalidní pole zůstává červené i při fokusu a nepoužije modré zvýraznění.
6. Původní Win32 rámeček se neobjeví ani krátce při prvním zobrazení nebo změně stavu.
7. Communication contracts zobrazí nevalidní Key červeným stavem správného řádku a po stránkování jej znovu přiřadí správnému kontraktu.
8. Dynamické vytváření a ničení řádků Contracts nezpůsobí záměnu stavu, artefakty ani únik životnosti.
9. Nativní editace, schránka, výběr, caret, klávesnice, kontextové menu, IME a přístupnost fungují stejně jako před změnou.
10. Víceřádková a read-only pole zůstávají beze změny.
11. Změna neovlivní validační pravidla, ukládání ani ostatní aplikační workflow.
12. Disabled pole má šedé pozadí a tmavě šedý text, nepřijímá fokus, nezobrazuje focus indikaci a zachovává nativní zákaz editace, výběru a kopírování.

## Odložené lehké ověření

Po pozdější implementaci ručně ověřit zejména:

- první zobrazení každého dotčeného okna bez probliknutí starého rámečku,
- běžný, focus, invalid, invalid+focus a disabled stav včetně jeho šedého pozadí, tmavě šedého textu a absence fokusu,
- přechod fokusu myší, Tab a Shift+Tab,
- caret, výběr, dlouhý text, schránku, Undo a kontextové menu,
- URL, secret, IP a key ve Wizardu,
- více stránek a recyklaci řádků v Communication contracts,
- vstup v Password SHA,
- resize a alespoň jedno zvýšené DPI,
- rychlé střídání fokusu a validity bez grafických artefaktů.

V rámci této fáze kontrolu neprovádět. Nevytvářet nové testy a neupravovat stávající testy.

## Hranice současné fáze

Tento dokument pouze formalizuje zadání před implementací. V této fázi:

- neměnit zdrojový kód aplikace,
- neměnit běžnou projektovou dokumentaci,
- nevytvářet ani neupravovat testy,
- neprovádět kompilaci,
- nekopírovat nic do distribuce,
- neprovádět commit ani push.
