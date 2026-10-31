// Generate the per-screen design documentation from local English data.
const fs=require('node:fs');
const path=require('node:path');
const root=__dirname;
const pages=JSON.parse(fs.readFileSync(path.join(root,'manifest.json'),'utf8')).sort((a,b)=>a.id.localeCompare(b.id));
const rules=JSON.parse(fs.readFileSync(path.join(root,'screen-rules.json'),'utf8'));
fs.mkdirSync(path.join(root,'screens'),{recursive:true});
for(const page of pages){
 const item=rules[page.id.slice(0,2)];
 if(!item||item.length!==3)throw Error('Missing rules for '+page.id);
 const sources=page.source.split('; ').map(s=>s.startsWith('client/')?`- \`${s}\``:`- ${s}`).join('\n');
 const androidSource=page.source.includes('Android Developers')?'\n- [Android VPN documentation](https://developer.android.com/develop/connectivity/vpn)\n':'';
 const related=page.flow.map(id=>{const p=pages.find(x=>x.id===id);return `[${p.title}](${id}.md)`;}).join(' · ');
 fs.writeFileSync(path.join(root,'screens',page.id+'.md'),`# ${page.id.slice(0,2)} · ${page.title}\n\n[Open mockup](../${page.id}.html) · [All screen rules](../RULES.md) · [Full PNG](../previews/full/${page.id}.png)\n\n## Existing client\n\n${item[0]}\n\n## Proposed Android behavior\n\n${item[1]}\n\n## Mockup limitations and pitfalls\n\n${item[2]}\n\n## Sources\n\n${sources}\n${androidSource}\n## Related scenarios\n\n${related}\n`);
}
const rows=pages.map(p=>`| ${p.id.slice(0,2)} | [${p.title}](screens/${p.id}.md) | [Open screen](${p.id}.html) |`).join('\n');
fs.writeFileSync(path.join(root,'RULES.md'),`# Screen rules and pitfalls\n\n[Visual catalog](index.html) · [Design overview and source map](README.md)\n\nEach of the 49 screen descriptions separates three kinds of information:\n\n- **Existing client:** behavior supported by the referenced client code. Android-only screens explicitly identify that distinction.\n- **Proposed Android behavior:** the intended mobile presentation and interaction. This is a design proposal, not a claim that an Android client already implements it.\n- **Mockup limitations and pitfalls:** what the static example omits, which transitions are illustrative, and what must not be mistaken for real operation.\n\nShared navigation, sample-data conventions, platform boundaries and regeneration instructions are in [README.md](README.md). They are not repeated in full on every screen. The source reference on each screen provides the basis for its existing-client rules. These documents do not introduce additional protocol, security or service guarantees.\n\n| Screen | Rules and pitfalls | Mockup |\n| --- | --- | --- |\n${rows}\n`);
console.log(`Generated ${pages.length} screen descriptions and RULES.md.`);
