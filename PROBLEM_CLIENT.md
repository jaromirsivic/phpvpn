# Bezpečnostní a implementační audit `client.md`

## Rozsah a způsob hodnocení

Audit se zaměřuje primárně a pouze na specifikaci klienta v `client.md`. `protocol.md` byl použit jen jako normativní kontext kompatibility tam, kde klient popisuje drátové chování. Níže nejsou kosmetické připomínky; jsou uvedeny jen nedostatky s kritickým nebo závažným bezpečnostním, interoperabilním, provozním či Windows-lifecycle dopadem.

Závažnost:

- **Kritická** – snadné překročení bezpečnostní hranice, kompromitace všech tajemství nebo spuštění neověřeného privilegovaného kódu.
- **Závažná** – reálné riziko trvalé ztráty provozu, DoS, nekonzistentního privilegovaného stavu, neslučitelnosti s protokolem nebo nebezpečné implementační divergence.

Souhrn: nalezeny byly **3 kritické** a **14 závažných** problémy.

---

## 1. Veřejná named pipe ruší lokální bezpečnostní hranici a specifikace ani nevynucuje, že je skutečně jen lokální

**Závažnost: Kritická**

**Odkazy:** `client.md`, „Bezpečnostní model a přijatá rizika“, ř. 227–229; „Windows Service, konzole a tray“, ř. 285–303; „Veřejné lokální IPC“, ř. 299–340.

**Problém:** Pipe výslovně dovoluje každému procesu číst celý konfigurační JSON včetně `SERVER_SECRET`, `ACCESS_KEY` a všech `PAYLOAD_KEY`, libovolně jej měnit a globálně volat `connect` a `disconnect`. Whitelist příkazů ani validní JSON nejsou autorizační hranice. V důsledku toho libovolný nízko privilegovaný proces získá kryptografickou identitu klienta a řídí privilegovanou službu. Navíc chybí povinné `PIPE_REJECT_REMOTE_CLIENTS`, `FILE_FLAG_FIRST_PIPE_INSTANCE`, ověření identity serveru na straně tray/konzole a přesná DACL/SACL. Pojem „lokální pipe“ tedy není technicky vynucen: podle konfigurace SMB může být pipe dosažitelná vzdáleně a při startovacím závodu ji může obsadit podvržený lokální server.

**Dopad:** Úplná kompromitace všech klientských tajemství, možnost vydávat se za klienta, dešifrovat zachycený per-peer provoz, měnit cílový server, vypínat VPN a zneužívat službu běžící s vysokými oprávněními. Podvržená pipe může tray aplikaci předkládat falešný stav nebo konfiguraci a zachytit data, která jí uživatel pošle.

**Realistický scénář:** Běžný proces spuštěný z přílohy nebo kompromitovaný proces jednoho z více přihlášených uživatelů zavolá `get-config`, odešle klíče útočníkovi, přes `set-config` nastaví svůj relay a `trustAnyTlsCertificate=true`, poté provede `disconnect`/`connect`. Není potřeba UAC ani administrátorský token. Na počítači s povoleným vzdáleným přístupem k named pipes se totéž může stát přes síť, protože specifikace vzdálené klienty explicitně neodmítá.

**Požadovaná úprava specifikace:**

1. Rozdělit IPC minimálně na veřejnou read-only pipe (`get-status` s necitlivými údaji) a privilegovanou administrační pipe.
2. DACL privilegované pipe omezit na `SYSTEM`, Administrators a případnou explicitní lokální skupinu phpvpn; u každého spojení ověřit token klienta impersonací a autorizovat příkaz.
3. `get-config` nikdy nevracet s tajemstvími; vracet redigovaný dokument. Klíče přijímat pouze zvláštním privilegovaným zápisem a ukládat je chráněně (např. DPAPI machine scope s vhodnou ACL).
4. Povinně vytvořit pipe s `PIPE_REJECT_REMOTE_CLIENTS | FILE_FLAG_FIRST_PIPE_INSTANCE`, bezpečnou security descriptor a nepovolit fallback bez těchto vlastností.
5. Tray/CLI musí ověřit, že server pipe je očekávaná Windows Service (server PID a token/service SID), a při neshodě skončit fail-closed.

Vědomé přijetí tohoto rizika v textu nemění jeho kritickou závažnost; pouze potvrzuje, že současný návrh nemá lokální bezpečnostní hranici.

---

## 2. Nezabezpečený transport je výchozí stav a umožňuje praktický útok na dostupnost

**Závažnost: Kritická**

**Odkazy:** `client.md`, konfigurační příklad a popis `serverUrl`/`trustAnyTlsCertificate`, ř. 43–74; „Bezpečnostní model a přijatá rizika“, ř. 221–228. Kompatibilitní kontext: `protocol.md`, kap. 1.2, 4.2 a 10.3.

**Problém:** Klient podporuje čisté HTTP a u HTTPS má `trustAnyTlsCertificate` výchozí hodnotu `true`. To znamená, že bezpečné ověření TLS není výchozí ani při použití schématu HTTPS. Aplikační autentizace neřeší replay platného `data` pollingu: útočník na trase může do 60 sekund polling zopakovat a atomicky vyprázdnit serverovou frontu oběti.

**Dopad:** Trvalé odebírání a nenávratné ničení příchozích ciphertextů, podvržení dostupnosti serveru, metadata leakage a dlouhodobý DoS VPN. Jde o útok bez znalosti klíče.

**Realistický scénář:** Útočník na veřejné Wi‑Fi zachytí každý prázdný `data` poll klienta a jeho kopii odešle relay dříve než legitimní klient. Útočník payload nepřečte, ale pravidelně vyprázdní frontu a UDP či jednorázová data jsou ztracena definitivně.

**Požadovaná úprava specifikace:** Výchozí hodnota musí být `false`; produkční profil musí vyžadovat HTTPS, validní řetězec i hostname. HTTP a vypnutí validace smějí existovat jen jako explicitní „dangerous compatibility mode“ potvrzený administrátorem, se stálým varováním ve stavu a diagnostice. Vhodné je doplnit volitelný pin veřejného klíče/certifikátu pro privátní PKI. Přechod HTTPS→HTTP, redirect na jiný origin a tiché vypnutí validace musí být zakázány.

---

## 3. Instalátor spouští nedůvěryhodný kód dříve, než může balíček ověřit

**Závažnost: Kritická**

**Odkazy:** `client.md`, „Wintun baseline a integrita distribuce“, ř. 241–249; „Distribuce a instalace ve Windows“, ř. 356–360.

**Problém:** Podporovaným vstupem je `install.bat`, který smí spustit PowerShell helper a vyžádat elevaci; teprve tento kód má ověřit podpis manifestu a balíček. Útočník, který nahradí `install.bat` nebo helper, už získá spuštění kódu (po potvrzení UAC) před kontrolou. Není definován formát a kanonizace manifestu, důvěryhodný podpisový klíč/signer, algoritmus, časové razítko, revokační politika ani pravidlo pro neočekávané soubory. „Platný digitální podpis DLL“ rovněž nestačí bez allowlistu očekávaného vydavatele.

**Dopad:** Libovolný privilegovaný kód z podvrženého ZIPu nebo distribučního adresáře může obejít všechny následující hashové kontroly a nainstalovat trvalou službu.

**Realistický scénář:** Útočník ponechá původní podepsaný manifest i `wintun.dll`, ale nahradí nepodepsaný `install.bat`. Uživatel spustí instalaci a schválí UAC; změněný skript nainstaluje malware a kontrolu manifestu pouze předstírá.

**Požadovaná úprava specifikace:** Distribuci musí spouštět OS-ověřitelný podepsaný bootstrap/MSI/MSIX, jehož podpis uživatel/Windows ověří před prvním instrukčním bajtem. Specifikovat pinovaný phpvpn signing identity/key, formát a kanonizaci manifestu, hash každého povoleného souboru, odmítnutí chybějících i nadbytečných spustitelných souborů a přesný allowlist vydavatele Wintun. Skript smí být pouze datem pro již ověřený bootstrap, ne kořenem důvěry.

---

## 4. IPC nemá framing, velikostní limity, deadline ani omezení souběhu

**Závažnost: Závažná**

**Odkazy:** `client.md`, „Veřejné lokální IPC“, ř. 299–340, zejména ř. 301, 317 a 336–340.

**Problém:** Je uvedeno pouze „UTF‑8 JSON“. Není určeno, zda je pipe message-mode nebo byte-mode, jak končí zpráva, kolik bajtů smí mít, kolik spojení a rozpracovaných požadavků smí existovat, jak dlouho může klient posílat, zda je povolen pipeline/multiplexing a jak se ruší požadavek při disconnect/stop. Přímé odpovědi `get-info` a `get-config` navíc nemají IPC obálku ani `requestId`, takže na persistentním nebo pipelinovaném spojení nejsou jednoznačně korelovatelné.

**Dopad:** Triviální lokální memory/handle/thread exhaustion a slowloris DoS služby. Různé implementace mohou číst hranice zpráv odlišně nebo přidělit paměť podle neověřeného prefixu.

**Realistický scénář:** Neprivilegovaný proces otevře stovky instancí pipe a na každé pošle jeden bajt JSON za minutu, případně oznámí gigabajtový `set-config`. Služba vyčerpá worker vlákna, handles nebo heap a přestane reagovat i na legitimní `disconnect`.

**Požadovaná úprava specifikace:** Určit jeden framing (např. message-mode pipe, nebo 32bit little-endian délku následovanou přesně jedním UTF‑8 JSON), tvrdý malý limit běžných příkazů, zvláštní rozumný limit konfigurace, limit spojení na klienta/globálně, read/write/idle deadline a omezený worker pool. Po překročení limitu zprávu odhodit bez parsování a spojení zavřít. Každá odpověď včetně `get-info`/`get-config` musí mít obálku s `ipcVersion`, `requestId`, `ok/error` a typovaným `result`; alternativou je normativně vynutit právě jeden request a jednu response na jedno spojení.

---

## 5. Aktivní a nově uložená konfigurace nejsou oddělené; souběh `set-config`, `connect` a recovery je nebezpečně nejednoznačný

**Závažnost: Závažná**

**Odkazy:** `client.md`, „Konfigurační soubor“, ř. 37–39; `connect`/`set-config`, ř. 332–340; globální souběh IPC, ř. 289–293.

**Problém:** Na začátku je řečeno, že po připojení zůstává validovaná konfigurace v paměti a datový tok disk nečte. `set-config` ale současně „nahradí jí aktuální konfiguraci“, přestože se má použít až při příštím pokusu. Není definováno, co vrací `get-config`, který snapshot používá recovery, jak jsou atomicky svázány virtuální IP, kontrakty, klíče a ownership journal ani kdo vyhraje při souběžných `set-config` a `connect`.

**Dopad:** Smíchání starých klíčů s novou IP/routou, teardown jiné generace sítě, nekonzistentní diagnostika a závody vedoucí k použití konfigurace, která nikdy nebyla validována jako jeden celek.

**Realistický scénář:** Jeden IPC klient zahájí `connect`, zatímco druhý uloží konfiguraci s jinou `virtualIp` a kontrakty. Bootstrap použije starý secret, Wintun vrstva přečte novou adresu a disconnect odstraní prostředky podle právě uloženého, nikoli aktivního snapshotu.

**Požadovaná úprava specifikace:** Zavést neměnný `ActiveConfigSnapshot` s číslem generace a oddělený `PersistedPendingConfig`. `set-config` atomicky validuje a uloží pouze pending generaci; nikdy nemění aktivní snapshot. `connect` pod jediným stavovým zámkem načte jednu generaci a všem modulům předá stejný immutable snapshot. Recovery a teardown používají výhradně generaci, se kterou byly prostředky vytvořeny. `get-config` musí explicitně říci, zda vrací pending nebo active, ideálně obě metadata bez secretů. Souběžné mutační příkazy musí být linearizované.

---

## 6. Uživatelské limity nemají horní meze a chybí povinná kontrolovaná aritmetika

**Závažnost: Závažná**

**Odkazy:** `client.md`, ř. 76–103, zejména výslovné „neexistuje globální horní strop“ na ř. 97 a 103; výpočet request budgetu, ř. 188–192.

**Problém:** Samostatná reprezentovatelnost hodnoty nechrání násobení, součty, převod sekund na milisekundy ani odečtení. Ohrožené výrazy zahrnují `maxRemotePeers × minPerPeerQueueBytes`, součet obsazeností, `maxRequestBodyBytes - 1024`, odhady base64/NDJSON režie a timeouty. Konfigurace dovoluje prakticky neomezené body/line/record/queue limity i timeouty.

**Dopad:** Integer overflow/underflow, obejití limitu, obrovská alokace, extrémně dlouhé blokování a trvalý DoS. V kombinaci s veřejným `set-config` je útok dostupný každému lokálnímu procesu a je perzistentní přes restart služby.

**Realistický scénář:** Útočník nastaví `minPerPeerQueueBytes` nebo `maxResponseBodyBytes` blízko `INT64_MAX` a `responseTimeoutSeconds` na roky. Násobení přeteče, konfigurace projde, následná rezervace paměti službu ukončí nebo disconnect čeká na HTTP operaci neomezeně dlouho.

**Požadovaná úprava specifikace:** Stanovit normativní horní meze pro každé pole a společný maximální memory budget procesu. Vyžadovat checked arithmetic pro každý součin, součet, převod jednotek a režii; overflow konfiguraci odmítne. Požadovat relační invarianty, např. `maxNdjsonLineBytes <= maxResponseBodyBytes`, odvodit bezpečný strop počtu records z body limitu a zavést tvrdý interní timeout nepřekročitelný konfigurací.

---

## 7. Konfigurace neobsahuje VPN prefix, přesto je na něm založena validace, kvóty i Windows routa

**Závažnost: Závažná**

**Odkazy:** `client.md`, konfigurační schéma ř. 41–68; odvození `maxRemotePeers`, ř. 78–95; validace cílů, ř. 161–175; split tunnel a ownership routy, ř. 233–237 a 259–265.

**Problém:** JSON obsahuje `virtualIp`, ale nikoli `vpnCidr`, masku/prefix ani jiný zdroj „společně nakonfigurovaného adresního prostoru“. Dokument přesto dovoluje jiný prostor než výchozí, odvozuje z něj počet peerů, validuje platné VPN adresy, konfiguruje adresu adaptéru a vytváří jedinou prefixovou routu. Implementace nemá jednoznačný vstup, ze kterého tyto hodnoty získat.

**Dopad:** Rozdílné implementace zvolí jinou masku, počet rezervací a routu; výsledkem je směrování cizího provozu do VPN nebo naopak chybějící směrování, nesprávné limity a rollback nesprávného prefixu.

**Realistický scénář:** `virtualIp=10.10.5.2` a kontrakt `10.10.6.3` může jedna implementace chápat jako `/16`, jiná jako dvě neplatné adresy mimo implicitní `/24`. První přidá širokou routu `10.10.0.0/16`, druhá konfiguraci odmítne.

**Požadovaná úprava specifikace:** Buď v1 normativně omezit na `10.99.99.0/24` a všechny jiné adresy fail-closed odmítnout, nebo přidat povinné kanonické pole `vpnCidr` a přesně definovat síťovou/broadcast/serverovou adresu, povolený rozsah prefixů, max. počet peerů, masku adaptéru, routu a checked výpočet. `virtualIp` i všechny kontrakty musí patřit do stejného prefixu.

---

## 8. Limity z `info` nejsou dostatečně validované ani úplně vynucené

**Závažnost: Závažná**

**Odkazy:** `client.md`, stavový automat ř. 111; polling ř. 177–190. Kompatibilitní kontext: `protocol.md`, kap. 8.2 a 11.1 (`maxDataRecordsPerRequest`, `maxRequestBodyBytes`, min/max polling).

**Problém:** Klient pouze uvádí, že limity uloží a „tvrdé limity“ dodrží. Plánovač ale normativně neomezuje počet odchozích `data` záznamů hodnotou `maxDataRecordsPerRequest`. Výraz `maxRequestBodyBytes - 1024` nemá minimální vstup ani checked subtraction. Po samostatném `clamp()` není ověřeno `effectiveMinPollIntervalMs <= effectiveMaxPollIntervalMs`. Chybí úplná schématická a relační validace dešifrovaného `info` před publikací nového runtime snapshotu.

**Dopad:** Klient může opakovaně posílat více záznamů než server přijme, dostávat `413` a nikdy nevyprázdnit frontu. Malý body limit může způsobit unsigned underflow a sestavení nadlimitního requestu. Nekonzistentní pollingové limity vedou k rozdílnému nebo busy-loop chování.

**Realistický scénář:** Server oznámí normativní výchozí limit 16 records. Kruhový plánovač s 20 peer frontami vloží po jednom záznamu z každé, protože sleduje jen bajty. Server každý request odmítne a klient jej bez normativní změny plánu opakuje.

**Požadovaná úprava specifikace:** Vyjmenovat všechna povinná pole `limits`, jejich typy a relační invarianty. Snapshot `info` publikovat atomicky až po úplné validaci. Plánovač musí současně dodržet přesný body limit i `maxDataRecordsPerRequest`. Pokud se do limitu nevejde ani platný auth řádek a nejmenší povolený data řádek, spojení skončí jako nekompatibilní server. Veškerá režijní aritmetika musí být checked a finální přesně změřené tělo je poslední fail-closed kontrola.

---

## 9. Klient dovoluje částečné předání neplatné binární dávky, což přímo odporuje protokolu

**Závažnost: Závažná**

**Odkazy:** `client.md`, „Úplná validace vnitřních IPv4 paketů“, ř. 169–175, a „Příjem dat“, ř. 200–208, zvláště ř. 205. Kompatibilitní kontext: `protocol.md`, kap. 9.4, ř. 421–431, a test 16.2/10, ř. 726.

**Problém:** `client.md` říká, že neplatný paket uvnitř dešifrované dávky se zahodí samostatně a ostatní pakety dávky zůstanou platné. Normativní `protocol.md` vyžaduje přesnou validaci celé dávky a stanoví, že neplatná dávka se nesmí předat částečně. Jde o přímou interoperabilní kontradikci, nikoli o volbu implementace.

**Dopad:** Dva „kompatibilní“ klienti se mohou lišit v tom, které pakety předají OS. Částečné předání může porušit vyšší vrstvou očekávané pořadí a atomické odmítnutí poškozené dávky a komplikuje bezpečnostní testy.

**Realistický scénář:** Dávka obsahuje první validní paket a druhý paket s chybným `Total Length`. Klient podle `client.md` první paket vpustí do tunelu; klient podle protokolu nevypustí žádný.

**Požadovaná úprava specifikace:** Nahradit ř. 175 a kroky příjmu pravidlem: po GCM ověření nejprve validovat celou tabulku, přesnou délku a všechny IPv4 pakety včetně adres; při jediné chybě zahodit celou dávku bez enqueue jediného paketu. Teprve plně validovanou dávku atomicky rozdělit do příchozí fronty při zachování pořadí; nedostatek lokální kapacity je samostatná post-validační politika a musí být výslovně popsán.

---

## 10. Výchozí limit 65 535 response records může odmítnout platnou protokolovou odpověď a nenávratně ztratit data

**Závažnost: Závažná**

**Odkazy:** `client.md`, ř. 97–101, příjem ř. 198–210. Kompatibilitní kontext: `protocol.md`, kap. 9.5 a 10.1: server počet záznamů v odpovědi samostatně neomezuje, nestránkuje a před odesláním je odstraní z bufferu.

**Problém:** Klient má výchozí `maxDataRecordsPerResponse=65535` a po překročení odmítne celé tělo. Protokol ale dovoluje libovolný počet řádků, omezený pouze byte kapacitou fronty. Do 16MiB serverového bufferu se může vejít více než 65 535 malých validních záznamů. Server je při pollingu atomicky převezme a odstraní, poté klient celou autentickou odpověď odmítne.

**Dopad:** Deterministická nevratná ztráta celé platné příchozí fronty při výchozí konfiguraci, bez útoku a bez porušení protokolu. Menší uživatelská hodnota problém dále zhoršuje.

**Realistický scénář:** Peer pošle desítky tisíc minimálních paketů po jednom v samostatném datovém záznamu. Součet serializovaných řádků nepřekročí serverový queue limit, ale počet je 65 536. Server vrátí platnou HMAC-chráněnou odpověď a frontu vyprázdní; klient ji zahodí jen kvůli vlastnímu count limitu.

**Požadovaná úprava specifikace:** Odstranit samostatný nižší count limit, nebo jej odvodit tak, aby při platném `maxResponseBodyBytes` nemohl odmítnout žádné tělo splňující protokolový byte limit (z minimální možné délky řádku, checked). Bezpečnost paměti řešit streaming parserem, byte limitem a průběžným HMAC stavem, nikoli nekompatibilním počtem. Pokud administrátor nastaví restriktivnější limit, musí být konfigurace před connect odmítnuta jako nekompatibilní s deklarovanou serverovou kapacitou, ne tiše přijata s následnou ztrátou.

---

## 11. Recovery nemá definovanou fail-closed hranici datové roviny a může neomezeně blackholovat provoz

**Závažnost: Závažná**

**Odkazy:** `client.md`, stavový automat ř. 113–114; synchronizace času ř. 143–147; Windows split tunnel ř. 235–237; disconnect ř. 277–279.

**Problém:** Při síťové nebo „autentizační“ chybě zůstává tunel zapnutý a probíhá nový bootstrap, ale není řečeno, zda vstupní/výstupní pracovníci běží, zda se smějí dále přijímat pakety, jak dlouho smí routa zůstat aktivní, co se stane po opakovaném neplatném GCM/HMAC a kdy se musí datová rovina zastavit. Poslední časový snapshot lze po neúspěšné synchronizaci používat bez maximálního stáří. To je v rozporu s deklarací, že v1 nemá kill switch: aktivní VPN routa bez funkčního transportu se chová jako nechtěný blackhole.

**Dopad:** Tiché zahazování aplikací do zaplněných front, pokračování provozu po kryptografické anomálii a neomezený stav, ze kterého se služba sama bezpečně nevrátí. Různé implementace budou mít zásadně odlišné security chování.

**Realistický scénář:** Po změně `SERVER_SECRET` nebo při aktivním MITM selhává každý `health`. Routa `10.99.99.0/24` a Wintun zůstanou aktivní celé hodiny, vstupní worker naplní fronty a potom zahazuje nové pakety. Aplikace nezkusí běžnou síťovou cestu a tray stále ukazuje neurčitý `recovering`.

**Požadovaná úprava specifikace:** Definovat přesný recovery sub-state machine. Integritní/auth chyba musí okamžitě pozastavit oba směry datové roviny a nesmí použít žádná data z chybné odpovědi. Po krátkém normativním grace limitu nebo po N pokusech musí služba odstranit vlastní routu/adresu a ukončit session, aby obnovila ne-VPN cestu; návrat vyžaduje celý bootstrap. Síťová nedostupnost může mít jinou omezenou politiku, ale musí mít maximální dobu, queue/drop pravidla a jasný stav. Časový snapshot musí mít maximální věk a po resume se před dalším auth requestem povinně obnovit.

---

## 12. `get-info` může porušit jediného vlastníka HTTP a závodit s připojeným provozem

**Závažnost: Závažná**

**Odkazy:** `client.md`, běhový model ř. 29–35; periodická synchronizace ř. 143–145; multi-session IPC ř. 289–295; `get-info`, ř. 336.

**Problém:** Síťový pracovník má být jediným vlastníkem všech `health`, `info` a `data` requestů a nemá existovat paralelní request. IPC `get-info` však smí každý klient vyvolat kdykoli a samo spustí `health` + `info`; není řečeno, zda je požadavek zakázán při `connected/recovering`, zařazen do stejného network event loopu, mění časový snapshot nebo jak se ruší při `disconnect`. Více tray/CLI klientů může příkaz vyvolat souběžně.

**Dopad:** Paralelní HTTP requesty, nekonzistentní časový stav, starvation běžného pollingu, deadlock při disconnect a porušení základního ownership modelu.

**Realistický scénář:** Dvě přihlášené relace současně zvolí Show Info, zatímco běží `data` poll a periodický `health`. Naivní IPC handlery spustí vlastní transportní volání, takže běží čtyři requesty a každý publikuje jiný časový snapshot.

**Požadovaná úprava specifikace:** Veškeré síťové operace včetně `get-info` smějí být pouze příkazy ve frontě jediného network workeru. Určit prioritu, deduplikaci souběžných `get-info`, deadline a cancellation generation. Alternativně `get-info` během `connecting`, `connected`, `recovering` a `stopping` odmítnout stabilní chybou a dovolit jej jen v `disconnected`; i tehdy musí použít stejný worker a nesmí trvale publikovat runtime stav připojení.

---

## 13. První „prázdný“ `data` poll vyprázdní relay dříve, než je Windows tunel připraven

**Závažnost: Závažná**

**Odkazy:** `client.md`, bootstrap ř. 112–113; Wintun preflight/start ř. 251–267. Kompatibilitní kontext: `protocol.md`, kap. 9.5 a 10.3 (každý úspěšný poll provede at-most-once drain).

**Problém:** „Prázdný polling“ znamená pouze request bez odchozích dat; odpověď může obsahovat celou příchozí frontu a server ji nenávratně odstraní. Klient ji zpracuje a připraví pro výstupního workera, ale Wintun adaptér, kompatibilita běžícího ovladače, session, adresa a routa se ověřují/vytvářejí až potom. Selhání kteréhokoli Windows kroku nebo pád služby v tomto okně nemá definované zotavení ani perzistenci přijatých paketů.

**Dopad:** Zbytečně zvětšené okno nevratné ztráty dat a možnost opakovaného drainu při každém neúspěšném connect/recovery, aniž kdy existoval funkční tunel.

**Realistický scénář:** Access key je správný a první poll vrátí důležitá data. Následný `WintunStartSession` selže kvůli nekompatibilní běžící verzi nebo kolizi routy. Server už data smazal a služba nemá výstupní worker ani specifikovaný durable spool.

**Požadovaná úprava specifikace:** Před drainujícím requestem provést všechny read-only kontroly a bezpečně připravit datovou rovinu. Preferovaně vytvořit/otevřít ověřený adaptér a session bez adresy/routy, teprve poté provést access-key poll a po jeho úspěchu atomicky dokončit síťovou konfiguraci a spustit workery; při chybě session zavřít bez změny sítě. Pokud je pořadí nemožné změnit, musí specifikace výslovně přijmout ztrátu a zavést crash-safe šifrovaný lokální spool pro plně ověřené dávky před pollingem, jinak nelze tvrdit bezpečné lifecycle chování.

---

## 14. `serverUrl` není bezpečně vymezená; chybí pravidla pro redirect, proxy, userinfo a konstrukci query

**Závažnost: Závažná**

**Odkazy:** `client.md`, modul Transport ř. 20 a 25; konfigurační pole ř. 43–72; validace URL ř. 107; požadavek na zachování cesty relay ř. 235–237.

**Problém:** „Validace URL“ nemá normativní pravidla. Není zakázán userinfo, existující query/fragment, nekanonický host, jiné schéma po redirectu, cross-origin redirect, HTTPS downgrade, automatické proxy z prostředí služby ani opakované DNS rozlišení na jiné rozhraní. Není ani řečeno, jak se bezpečně přidá `?action=` k URL, která už query obsahuje. Služba provádí requesty s vysokými oprávněními a `set-config` je veřejný.

**Dopad:** SSRF z privilegované služby, odeslání autentizačních těl jinému originu při redirectu, obcházení zamýšlené TLS politiky, leakage URL credentials v povoleném logu a zachycení relay provozu nově přidanou VPN routou.

**Realistický scénář:** Lokální proces nastaví `serverUrl` na interní management endpoint nebo na HTTPS server vracející 302 na HTTP/odlišný host. Implementace s povoleným `CURLOPT_FOLLOWLOCATION` odešle následný POST mimo očekávaný origin; jiná implementace redirect odmítne. Obě mohou tvrdit shodu se specifikací.

**Požadovaná úprava specifikace:** Definovat striktní parser: pouze absolutní `http`/`https`, žádný userinfo/fragment, žádná předexistující `action`, kanonický host a explicitní port, pevné přidání query. Redirecty zakázat; případně povolit jen omezený počet na přesně stejný scheme/host/port bez HTTPS downgrade a znovu ověřit certifikát. Zakázat implicitní environment proxy nebo ji explicitně konfigurovat a auditovat. Před routou ověřit skutečnou TCP cestu/proxy endpoint mimo phpvpn pro všechny použité adresy a při změně DNS/network epoch spojení znovu vyhodnotit.

---

## 15. Windows Service, config a ostatní DLL nemají definované minimální hardening požadavky

**Závažnost: Závažná**

**Odkazy:** `client.md`, bezpečné načtení pouze `wintun.dll`, ř. 249–251; Windows Service ř. 285–297; `set-config`, ř. 340; instalace ř. 356–360.

**Problém:** Specifikace bezpečně řeší pouze cestu k `wintun.dll`. Neurčuje účet služby a jeho minimální oprávnění, service SID, SCM DACL, přesně uvozený absolutní `ImagePath`, DACL/owner instalačního a datového adresáře, zákaz reparse points/hardlinků, bezpečné načítání ostatních privátních DLL, crash dump politiku ani crash-safe zápis konfigurace. „Atomicky uloží“ na Windows nestačí: bez stejného adresáře, `FlushFileBuffers`, `ReplaceFileW`/ekvivalentu a opětovného ověření identity cíle může zápis po pádu poškodit konfiguraci nebo následovat podvržený reparse point.

**Dopad:** DLL search-order hijacking, unquoted-service-path escalation, přepsání jiného souboru službou, čtení secretů z nedostatečně chráněného souboru nebo dumpu a neobnovitelná konfigurace po výpadku napájení.

**Realistický scénář:** Implementace načte jinou pomocnou DLL běžným `LoadLibraryW("helper.dll")`, přestože Wintun načítá bezpečně. Zapisovatelný current directory/PATH obsahuje podvrženou DLL a privilegovaná služba ji načte. Jiná implementace zapíše temp config do `%TEMP%` a následným rename následuje útočníkem připravený reparse point.

**Požadovaná úprava specifikace:** Určit služební účet s minimem práv, service SID/restricted token podle proveditelnosti, SCM DACL jen pro SYSTEM/Administrators, uvozený absolutní `ImagePath`, vlastní nezapisovatelný pracovní adresář a `SetDefaultDllDirectories` plus absolutní načítání všech privátních DLL. Předepsat DACL/owner a kontrolu reparse/hardlink identity pro instalační, datový, config a journal soubor. Atomický config update: temp soubor ve stejném chráněném adresáři, exkluzivní create, write-through/flush, validace, atomická výměna, flush adresáře podle dostupné platformní semantiky a zachování poslední známé platné zálohy. Tajemství nesmějí do běžných crash dumpů.

---

## 16. Význam `WintunCreateAdapter`/`OpenAdapter`/`CloseAdapter` není svázán s ownership žurnálem

**Závažnost: Závažná**

**Odkazy:** `client.md`, „Vlastnictví adaptéru a session“, ř. 253–267; disconnect ř. 277–283; uninstall ř. 356–360.

**Problém:** Specifikace dovoluje adaptér „vytvořit nebo otevřít“, ale stejný teardown vždy končí `WintunCloseAdapter` a journal neuvádí, zda byl handle vytvořen nebo jen otevřen. Aktuální Wintun API přitom rozlišuje efekt: `WintunCloseAdapter` odstraní adaptér vytvořený přes `WintunCreateAdapter`, ale u adaptéru získaného přes `WintunOpenAdapter` pouze uvolní prostředky. Dokument dále neříká, jakou přesnou stabilní GUID předat jako `RequestedGUID`, jak před otevřením ověřit GUID/LUID/instalační identitu a jaký persistentní stav má po disconnect, crash, uninstall a reinstall zůstat. Oficiální popis semantiky je v [Wintun API](https://git.zx2c4.com/wintun/tree/README.md).

**Dopad:** Jedna implementace bude adaptér při každém disconnect mazat, jiná ponechá cizí nebo starý adaptér; ownership record se může rozejít se skutečností. Chybný open podle pouhého názvu může převzít cizí adaptér, zatímco chybný close může odstranit právě vytvořený prostředek dříve, než journal potvrdí rollback.

**Realistický scénář:** Po pádu zůstane adaptér jménem `phpvpn`. Služba jej otevře jen podle názvu, žurnál ale patří jiné/reinstalované identitě. Při disconnect ho `CloseAdapter` neodstraní, záznam označí `removed` a další start buď fail-closed uvázne, nebo adaptér mylně převezme.

**Požadovaná úprava specifikace:** Zvolit jeden normativní model: buď vždy ephemerální adaptér vytvořený s deterministickou per-install GUID a odstraněný close, nebo persistentní adaptér s explicitní, bezpečně ověřovanou identitou a přesným cleanup API. Žurnál musí obsahovat GUID, LUID/device instance ID, origin handle (`created`/`opened`), očekávaný efekt close a generation. `OpenAdapter` je dovolen jen po shodě všech identit, nikdy jen podle názvu. Přesně popsat stavy po disconnect, crash, uninstall a reinstall a přidat test každé větve.

---

## 17. Disconnect a upgrade nemají tvrdě omezené zrušení HTTP a crash-safe multi-file transakci

**Závažnost: Závažná**

**Odkazy:** `client.md`, neomezený `responseTimeoutSeconds`, ř. 97–103; disconnect/upgrade/rollback ř. 277–283; IPC `disconnect`, ř. 334; instalace ř. 356–360.

**Problém:** Disconnect smí existující request „zrušit nebo nechat doběhnout“, ale dokončení čeká na join všech workerů. Není požadováno, aby cancellation přerušila DNS, proxy connect, TLS handshake i pomalé čtení těla, ani neexistuje nepřekročitelný teardown deadline. Současně je timeout uživatelsky neomezený. Upgrade označuje náhradu celé sady souborů za atomickou, ale Windows neposkytuje atomickou výměnu více nezávislých DLL/EXE/manifestu; chybí staging generace a instalační journal pro pád uprostřed.

**Dopad:** Službu nelze včas zastavit ani aktualizovat, session/routa zůstane viset a výpadek proudu může vytvořit smíšenou, přesto spustitelnou instalaci. Rollback pak nemusí vědět, které soubory tvoří jednu ověřenou verzi.

**Realistický scénář:** Server pošle hlavičky a pak tělo po bajtu; config má timeout několik dní. Uživatel zavolá disconnect nebo instalátor zastavuje službu pro upgrade. Network worker se nepřipojí, SCM stop timeout vyprší a instalátor začne měnit část souborů; restart počítače zanechá novou službu se starou DLL a manifestem.

**Požadovaná úprava specifikace:** Transport musí mít jednotný cancellation token, který zavře/abortuje všechny síťové handles a probudí DNS/connect/TLS/read bez čekání na konfigurační timeout. Zavést tvrdý interní horní limit teardownu a přesné chování při jeho selhání; žádné násilné ukončení vlákna s drženými prostředky. Upgrade provádět do nového verzovaného, kompletně ověřeného adresáře a atomicky přepnout jediný malý ukazatel/SCM konfiguraci s crash-safe instalačním journalem; služba při startu ověří, že všechny soubory patří jedné podepsané generaci, jinak se nespustí. Rollback vybírá pouze kompletní předem ověřenou generaci.

---

## Doporučené pořadí nápravy

1. Změnit IPC trust boundary, zakázat vzdálené klienty a odstranit veřejné čtení/zápis secretů.
2. Zabezpečit instalační bootstrap a výchozí TLS profil.
3. Opravit dva přímé protokolové rozpory: atomické odmítnutí neplatné dávky a response record limit.
4. Doplnit explicitní VPN prefix, úplnou validaci `info` limitů a checked/capped konfiguraci.
5. Normativně uzavřít stavový automat active/pending config, recovery, `get-info`, bootstrap drain a disconnect cancellation.
6. Dokončit Windows hardening, Wintun ownership model a crash-safe install/upgrade/config transakce.

Dokud nejsou body 1–3 vyřešeny, nelze současný `client.md` považovat za bezpečný implementační podklad, i když jsou některá rizika v dokumentu označena jako vědomě přijatá.
