# 005 - Owner-draw switche bez probliknutí nativního checkboxu

## Cíl

Odstranit ze všech moderních switchů tray aplikace možnost krátkého zobrazení původního Windows checkboxu. Switch musí být plně vlastně vykreslovaný už od okamžiku vytvoření, stejně jako moderní tlačítka po dokončení mikroúpravy 004.

## Rozsah

- Všechny checkboxy nahrazené moderním switchem v mikroúpravě 002.
- Všechny čtyři typy switchů v Configuration Wizardu.
- Switch **Enabled** na každém dynamickém řádku Communication Contracts.
- Každá další cesta tray aplikace, která používá sdílený moderní toggle switch.

## Požadované řešení

- Nativní `BUTTON` musí být vytvořen rovnou v owner-draw režimu, ne jako běžný `BS_AUTOCHECKBOX`, na který se vlastní kreslení připojí až dodatečně.
- Windows nesmí při prvním zobrazení, stisku, tažení myší, získání fokusu, změně checked stavu, změně enabled stavu ani synchronním překreslení nakreslit původní checkbox.
- Moderní switch musí být jediným viditelným vzhledem od prvního vykresleného snímku.
- Zachovat současný modrý posuvníkový design, anti-aliasing, rozměry, popisky a výrazně ztlumený disabled stav.
- Zachovat nativní HWND a chování checkboxu: kliknutí, mouse capture, checked stav, Tab, Shift+Tab, Space, fokus, dostupný název, roli a stav pro čtečky obrazovky.
- Owner-draw nesmí vyžadovat ruční neúplnou emulaci přepínání ani zhoršit přístupnost.
- Při stisku a tažení mimo switch nesmí zůstat grafické artefakty ani nesprávný pressed nebo hover stav.
- Dynamicky vytvářené a recyklované switche ve Wizardu a Contracts musí používat stejnou sdílenou implementaci.

## Akceptační kritéria

1. Nikde v tray aplikaci neproblikne klasický Windows checkbox před moderním switchem ani během interakce.
2. Všechny switche ve Wizardu i Contracts mají od prvního snímku pouze moderní vzhled.
3. Kliknutí a Space spolehlivě přepínají stav právě jednou.
4. Tab, Shift+Tab, fokus, enabled/disabled a přístupnost fungují stejně jako před změnou.
5. Stisk, tažení mimo prvek, návrat dovnitř, uvolnění venku a ztráta mouse capture nezanechají žádný artefakt.
6. Změna neovlivní tlačítka, aplikační workflow ani data konfigurace.

## Lehká kontrola

- Několikrát otevřít jednotlivé kroky Wizardu a Communication Contracts a sledovat switch už při prvním zobrazení.
- Krátce ověřit kliknutí, Space, Tab, Shift+Tab, fokus, disabled stav a změnu checked stavu.
- Stisknout switch, táhnout kurzor mimo prvek, vrátit jej dovnitř a uvolnit uvnitř i venku.
- V Contracts ověřit více dynamických řádků a základní scroll.
- Ověřit jedno zvýšené DPI.
- Testy upravit nebo spustit jen rychle a cíleně. Nespouštět těžké ani plošné sady.
- Neupravovat klientskou dokumentaci.
- Po opravě sestavit Release tray EXE, obnovit standardní distribuci a ověřit manifestové hashe a podpis.

## Stav

Uživatel výslovně schválil implementaci opravy pro všechny switche tray aplikace.
