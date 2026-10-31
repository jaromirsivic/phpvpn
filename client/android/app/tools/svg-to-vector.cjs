// Converts assets/phpvpn*.svg (five axis-aligned rects, viewBox 8 8 72 72)
// into VectorDrawables with @color references (WP07). Run from the repo root:
//   node client/android/app/tools/svg-to-vector.cjs assets client/android/app/src/main/res
const fs = require('node:fs');
const path = require('node:path');
const [assets, out] = process.argv.slice(2);
const states = { '': 'brand', '-connected': 'connected', '-connecting': 'connecting', '-disconnected': 'disconnected' };
// Paint order of the five rects in every SVG.
const roles = ['frame', 'fill', 'frame', 'accent', 'frame'];

function rects(svg) {
  return [...svg.matchAll(/<rect x="([\d.]+)" y="([\d.]+)" width="([\d.]+)" height="([\d.]+)" fill="(#[0-9A-Fa-f]+)"\/>/g)]
    .map(m => ({ x: +m[1] - 8, y: +m[2] - 8, w: +m[3], h: +m[4] }));
}
const n = v => +v.toFixed(2);
const pathOf = r => `M${n(r.x)},${n(r.y)}h${n(r.w)}v${n(r.h)}h${n(-r.w)}z`;

function vector({ comment, size, scale, offset, paths }) {
  const body = paths.map(p => `        <path\n${p.evenOdd ? '            android:fillType="evenOdd"\n' : ''}            android:fillColor="${p.color}"\n            android:pathData="${p.data}" />`).join('\n');
  return `<?xml version="1.0" encoding="utf-8"?>\n<!-- ${comment} -->\n<vector xmlns:android="http://schemas.android.com/apk/res/android"\n    android:width="${size}dp"\n    android:height="${size}dp"\n    android:viewportWidth="${size}"\n    android:viewportHeight="${size}">\n    <group\n        android:scaleX="${scale}"\n        android:scaleY="${scale}"\n        android:translateX="${offset}"\n        android:translateY="${offset}">\n${body}\n    </group>\n</vector>\n`;
}

for (const [suffix, name] of Object.entries(states)) {
  const source = `assets/phpvpn${suffix}.svg`;
  const rs = rects(fs.readFileSync(path.join(assets, `phpvpn${suffix}.svg`), 'utf8'));
  if (rs.length !== roles.length) throw Error(`unexpected shape count in ${source}`);
  const colored = rs.map((r, i) => ({ color: `@color/icon_${name}_${roles[i]}`, data: pathOf(r) }));
  fs.writeFileSync(path.join(out, 'drawable', `ic_state_${name}.xml`), vector({
    comment: `State icon "${name}", converted by client/android/app/tools/svg-to-vector.cjs from ${source}.`,
    size: 72, scale: 1, offset: 0, paths: colored,
  }));
  if (name !== 'brand') continue;
  // Adaptive icon: 108 dp canvas, the 72-unit logo scaled to 48 dp and centred,
  // so its corners (34 dp from the centre) stay inside the 66 dp safe zone.
  const scale = (48 / 72).toFixed(4);
  fs.writeFileSync(path.join(out, 'drawable', 'ic_launcher_foreground.xml'), vector({
    comment: `Launcher foreground, converted by client/android/app/tools/svg-to-vector.cjs from ${source}.`,
    size: 108, scale, offset: 30, paths: colored,
  }));
  // Themed icon layer: the outline frame and the cross in one colour; the
  // launcher tints it.
  fs.writeFileSync(path.join(out, 'drawable', 'ic_launcher_monochrome.xml'), vector({
    comment: `Themed launcher layer, converted by client/android/app/tools/svg-to-vector.cjs from ${source}.`,
    size: 108, scale, offset: 30, paths: [
      { color: '@color/glyph_default', evenOdd: true, data: pathOf(rs[0]) + pathOf(rs[1]) },
      { color: '@color/glyph_default', data: pathOf(rs[2]) + pathOf(rs[3]) },
    ],
  }));
}
console.log('ok');
