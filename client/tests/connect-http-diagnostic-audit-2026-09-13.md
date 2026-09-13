# Connect HTTP diagnostika: akceptační audit

Stav: dokončeno podle intentu a definitivního doplnění uživatele. Přijatá, ale odmítnutá health/info odpověď ukončí úvodní Connect terminální chybou bez retry. Přechodné komunikační chyby bez použitelné odpovědi zachovávají recovery a nemají detail. Projektový dist je obnoven a ověřen.

Zadání: Intents/2026-09-13-08-58-25/intent-v001.md a definitivní rozhodnutí předané koordinátorem ve stejné realizaci. Práce proběhla přímo na tray_UX, vstupní HEAD 6ac51fd. Při vstupní kontrole byl index prázdný, v pracovním stromu byl marker a nová složka intentu. Index, commit, pracovní stromy ani další úlohy nebyly vytvářeny či měněny. Doba pokračování byla výslovně neomezená.

## Implementace

- Jednorázový opt-in Transport::postDiagnostic pro úvodní runtime health/info, s využitím původního WinHTTP bounded capture. Ostatní runtime výměny používají původní post bez capture. Wizard má dosavadní nastavení beze změny.
- NetworkWorker vrací kandidáta pouze pro odpovídající neúspěšnou výměnu. Úspěšný health kandidáta odstraní před info. Data, tunel a recovery nemají výstupní diagnostický kanál.
- Služba publikuje terminální status a jeho diagnostiku atomicky po teardown. Kontroluje epochu, control generation, running/connectRequested a zrušení. Nový Connect, Disconnect a shutdown raw kopii invalidují.
- Každý nově přijatý Connect má UUIDv4 connectAttemptId a číselnou connectionEpoch. Idempotentní Connect identitu nemění. UUID zabraňuje záměně i při opakování epochy po restartu služby.
- get-status má 25 povinných členů vnitřního status objektu a nejvýše jeden volitelný httpDiagnostic. Sdílený codec Wizardu zajišťuje stejné octety, délky, řez, separator a striktní validaci. Celá zpráva má limit 4 MiB. Parser odmítá nesoulad akce, stavu, kontrolních bodů, chyby nebo identity.
- Nový přesný IPC příkaz dismiss-connect-diagnostic přijímá epochu a UUID pokusu. Nesprávná starší identita nic nemaže. Neprovádí síťovou akci ani změnu konfigurace.
- Connect panel má červený Error detail vpravo. Na úzké ploše se tlačítko řadí pod text a zůstává vpravo. Používá stávající moderní tlačítko a paletu. Dialog je stejný HttpErrorDetailWindow jako ve Wizardu, vlastněný Connect formulářem.
- Model invaliduje raw data a owned okno při výpadku IPC, změně pokusu a zavření. Pozdní data invalidovaného pokusu neobnoví. Tray ikona a Statistics cache diagnostiku neuchovávají.
- Přijatá, ale odmítnutá health/info odpověď má samostatný příznak výsledku výměny. Terminální klasifikace nezávisí na existenci raw diagnostiky. Test bez dostupného capture objektu také prokazuje zákaz retry. Úspěšná fáze příznak i kandidáta odstraní, takže neovlivní pozdější chybu.
- client.md a protocol.md popisují sdílený formát, identity, cleanup, výjimku raw dat v lokálním statusu a definitivní klasifikaci retry.

## Provedené kontroly

- Debug, Release (RelWithDebInfo) a ASAN build: PASS, včetně posledních změn identity a testů.
- StatusContract, TrayLayout, http_diagnostic, wizard_ipc, WizardModel a pozdní zrušená runtime odpověď: PASS v Debug, Release i ASAN.
- IPC testuje 65535, 65536, 65537 a 200000 původních bajtů, maximálně escapované bounded prefixy/suffixy pod 4 MiB, chybná schémata, nulovou/zápornou epochu, neplatnou korelaci a zákaz staršího cleanup. Stávající testy Wizard codec pokrývají plný uint64 rozsah.
- Celá nativní WP03Windows sada: 14 testů PASS v Debug. Zahrnuje Show Server Info, taby, resize, focus a GDI/USER cykly bez přírůstku.
- Nový nativní ConnectHttpDiagnosticOwnedDialogAndCleanup: PASS v Debug, Release a ASAN. Skutečné DPI 144 (150 %), resize široké i úzké plochy, Tab order, aktivace Space, oba taby, read-only obsah, žádné OK/Cancel, zavření/znovuotevření a cleanup vlastníka.
- Matematické layout kontroly: 96, 120, 144 a 192 DPI, šířky 320 až 1200 DIP. Ruční přepnutí systémového high contrast a fyzické přemístění mezi monitory: NOT RUN. Styl znovu používá původní komponenty.
- Screenshot syntetického Connect panelu: work/logs/connect-http-diagnostic/connect-http-diagnostic. Vizuálně zkontrolováno, tlačítko nepřekrývá text.
- Wizard nativní http_diagnostic_wizard_owned_window_cleanup: PASS.
- Wizard lokální Nginx/PHP regrese: všech devět scénářů PASS, včetně health/info a přesných 65535/65536/65537 bajtů.
- Connect lokální info: PASS přes skutečný Nginx/PHP a WinHTTP, runtime, izolovanou named pipe a nativní Connect dialog. Ověřena shoda obou zobrazených zpráv se službovým objektem a odstranění službové kopie při zavření formuláře. TunnelDriver/NetConfig byly falešné, žádný skutečný adaptér ani instalovaná služba nebyly použity.
- Úplné Connect health/info/noresponse scénáře: PASS v Debug, Release i ASAN přes stejný reálný HTTP a nativní UI řetězec. Unit test navíc posune čas za backoff, prokáže stabilní terminální chybu bez dalšího health/info a naopak skutečný retry DNS/timeoutu bez diagnostického capture.
- Externí best-effort test https://phpvpn.freehosting.dev/phpvpn.php: PASS až po lokálních testech. Skutečná chybná odpověď zobrazena v Connect detailu, TLS ověřování zapnuté. Integrační transport blokuje data request i v případě nečekaného úspěchu health/info.
- Oba lokální runnery odstranily jen vlastní dočasný endpoint a control soubor. Původní D:/nginx/conf/nginx.conf, D:/nginx/html/phpvpn.php a repozitářový phpvpn.php zůstaly hashově beze změny.
- Graft refresh a git diff --check: PASS.

## Celkové regrese a omezení

Produktová volba retry je uzavřená. Test connect_http_diagnostic_phase_scope_and_cleanup nyní prochází ve všech osmi režimech, včetně dříve neuzavřeného mode 0. Ověřuje také health HMAC, info, DNS, timeout, ACCESS_KEY, tunel, nedostupnost capture a korelovaný cleanup. Pozdní zrušená odpověď a nové připojení jsou ověřeny samostatně.

Závěrečný Debug i Release CTest spustil 350 registrovaných případů. Zůstala pouze čtyři dříve doložená selhání ContractsDocument, žádné nové selhání. Logger test prošel v obou konfiguracích. Testový zdroj ContractsDocument je po normalizaci CRLF shodný s uchovaným předchozím baseline. Podrobnosti původních chyb jsou v http-diagnostic-audit-2026-09-13.md. Tyto nesouvisející chyby nebyly opravovány ani vydávány za nové regrese. ASAN runtime filtr: 27 registrovaných případů, žádné selhání. Opt-in integrace bez nastaveného endpointu hlásí NOT RUN, jejich skutečné běhy jsou vykázány samostatně výše.

Instalace, upgrade, odinstalace a systémový restart: NOT RUN podle výslovného zadání. Automatizované kopírování do skutečného uživatelského clipboardu: NOT RUN, sdílený dialog používá stejné nativní read-only editory jako Wizard. Ruční systémové high-contrast přepnutí a fyzické změny monitorů nejsou vykázány jako provedené. Při ztrátě IPC je lokální cleanup okamžitý, vzdálené doručení dismiss není garantované. Vzdálenou kopii dále ruší nový Connect, Disconnect nebo shutdown.

## Distribuce

Výsledek: C:\git\phpvpn\dist\client\Windows\x64. Celý projektový dist má 17 souborů, včetně aktuální serverové kopie a nepodporovaných platforem s README. Všech 17 původních souborů bylo jednotlivě přesunuto do C:\git\phpvpn\work\distribution-connect-20260913\previous-dist. Po ověření prázdného obsahu byl adresářový strom naplněn výhradně z nového ověřeného stagingu. Záloha zůstává mimo distribuci, nedošlo k míchání starých souborů s novými.

make-dist.ps1 vytvořil manifest a RSA-PKCS1-SHA256 podpis stávající vývojovou identitou CN=phpvpn Development Test Signing, pin 7430CCA24F5DE8A6614D582AFBFED7C80E0F2CA8. Certifikát je platný do 2031-08-19. Nejde o veřejně důvěryhodný vydavatelský podpis. Soukromý klíč nebyl exportován.

Ověřeno: všech 11 manifestových délek a SHA-256, přesný soupis 14 Windows souborů, shoda všech tří EXE a Wintun s aktuálním Release, architektura x64, systémové runtime importy, aktuální instalační/odinstalační skripty, byte-for-byte serverová kopie, původní platný Wintun Authenticode a povinné driver API. install.bat -VerifyOnly prošel ve stagingu i v konečném projektovém dist. Změněný hash, vadný podpis, chybějící EXE a nadbytečný EXE byly v izolovaných kopiích správně odmítnuty. test_package_upgrade.ps1 prošel. Skutečná instalovaná služba nebyla pro tyto kontroly zastavována ani měněna.

Balíček lze předat pro upgrade přes jeho install.bat bez předchozí ruční odinstalace. Dosavadní instalační skript žádá běžný tray Exit, respektuje neuloženou práci a zastaví službu před přepisem stejné generace 1.0.0. Plně transakční rollback není implementován ani slibován.

| Soubor | SHA-256 |
| --- | --- |
| phpvpn-service.exe | 67C6A73FBEC4BDC7B94DD6BB353743EF6EF6A19E79E609E7143FAFF1FCF66DE0 |
| phpvpn-tray.exe | 4EF7600B1895F170D019A7F5863A37B96434005123023EF7666D636B8F3EA2B7 |
| phpvpn.exe | CED0E3C909ECB3C6060A08BB1AF6924451619A86CEB240C35436C1AE465D3DBB |
| release-manifest.json | E1DBB7C26DC4A2836C6C801DEAD5F4A749A9EA93287D534E5A8C1AFF3F198AEC |

Pracovní logy jsou v work/connect-diagnostic-*.log a work/connect-*.log. Staging, záloha, negativní kopie a výpis importů jsou mimo dist v work/distribution-connect-20260913. Závěrečné změny zdrojů, dokumentace i audit zůstávají bez commitu.
