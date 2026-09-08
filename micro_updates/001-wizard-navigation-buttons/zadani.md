# 001 - Odstranění tlačítka Cancel z Configuration Wizardu

## Cíl

Zjednodušit navigaci v Configuration Wizardu. Samostatné tlačítko **Cancel** odstranit a tlačítko **Back** umístit bezprostředně vedle primárního tlačítka pro pokračování.

## Požadované chování

- Úprava platí pro všechny kroky a stavy Configuration Wizardu.
- Tlačítko **Cancel** nebude v patičce wizardu nikde zobrazené.
- Pokud je na daném kroku dostupné tlačítko **Back**, bude umístěné bezprostředně vlevo od primárního tlačítka.
- Mezi tlačítky **Back** a primárním tlačítkem zůstane běžná malá mezera používaná současným GUI mezi sousedními tlačítky.
- Mezi tlačítky nebude žádné další tlačítko ani nadbytečná mezera určená pro odstraněné tlačítko Cancel.
- Primárním tlačítkem se rozumí tlačítko pro pokračování v aktuálním kroku, bez ohledu na jeho konkrétní popisek, například **Continue**, **Verify & continue**, **Disconnect & continue** nebo **Finish**.
- Pokud na daném kroku není návrat dostupný, tlačítko **Back** se nezobrazí a napravo zůstane pouze primární tlačítko.
- Zavření wizardu křížkem okna zůstane zachované.
- Zavření wizardu klávesou **Escape** zůstane zachované.
- Zavření křížkem i klávesou Escape zachová současnou logiku potvrzení, rušení probíhající operace a bezpečného úklidu.

## Beze změny

- Funkce tlačítek **Back** a primárního tlačítka.
- Pořadí a obsah kroků wizardu.
- Tlačítko **Reset** a jeho umístění.
- Tlačítka **Cancel** v ostatních oknech aplikace.
- Síťová komunikace, práce s konfigurací a ostatní aplikační logika.

## Akceptační kritéria

1. V žádném kroku ani stavu Configuration Wizardu není viditelné tlačítko **Cancel**.
2. Kde je dostupné **Back**, je bezprostředně vlevo od primárního tlačítka se stávající běžnou malou mezerou mezi tlačítky.
3. Kde **Back** dostupné není, napravo zůstává pouze primární tlačítko.
4. Po odstraněném tlačítku Cancel nezůstává prázdné místo.
5. Wizard lze nadále zavřít křížkem okna i klávesou **Escape** se současným chováním.
6. Rozložení funguje ve všech běžně dostupných krocích wizardu bez překrytí nebo oříznutí tlačítek.
7. Ostatní okna a jejich tlačítka **Cancel** nejsou změnou dotčená.

## Lehká kontrola po implementaci

- Ručně projít běžný tok wizardu a vizuálně ověřit všechny dostupné kroky.
- Na kroku s návratem ověřit pořadí **Back** a primárního tlačítka i funkci Back.
- Na kroku bez návratu ověřit, že je napravo pouze primární tlačítko.
- Jednou ověřit zavření křížkem a jednou klávesou **Escape**.
- Ověřit, že po Cancel nezůstala mezera a tlačítka nejsou oříznutá.
- Poté sestavit tray EXE a nasadit ho standardním projektovým postupem.

## Mimo aktuální krok

Tento dokument pouze formalizuje požadavek. Implementace, sestavení a nasazení začnou až na výslovný pokyn uživatele.
