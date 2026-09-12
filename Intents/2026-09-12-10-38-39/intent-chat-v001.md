---
intent_version: 1
created_at: 2026-09-12-10-40-18
produces: intent-v001.md
---

# Záznam rozhovoru pro intent v001

**User:** Dobře, vytvoř na to úplně nový intent, kde teďka dáš to, že chceme naimplementovat pre-poll backpressure v klientovi. Primárně ve službě. Hádám, že v tray aplikaci žádná implementace není, ale oznam mi hned, kdyby byla. `client.md` upravíme. Prověř, zda je nutné měnit `protocol.md` nebo `tray_UX.md`. Na závěr budou potřeba testy.

**AI:** Upozornila jsem, že nový intent nelze bezpečně založit, dokud se neuzavře zastaralý marker předchozího, již realizovaného intentu.

**User:** Ano, přesně tak. Chci nový intent pro tenhle úkol.

**AI:** Uzavřela jsem zastaralý stav markeru a založila nový sběr požadavků. Potvrdila jsem, že v této fázi neprobíhá implementace ani vyhodnocení zdrojového kódu.

**User:** Ano, zpracuj intent.

**AI:** Při read-only průzkumu jsem ověřila, že pravidelný VPN `data` polling řídí `VpnEngine` a `NetworkWorker` ve službě. Tray pouze čte a zobrazuje stav přes existující IPC. Wire formát ani serverová pravidla se návrhem nemění. Pomocná kapacitní kontrola inbound front již existuje ve sdíleném core, ale před pollingem dosud zapojená není.
