// 把 sch check 的 marker-overlap 明细映射到归属引脚：同网 + 离网标 bbox 最近的引脚。
// 用法: node map-markers.mjs <page1.json> <check.json> [maxDist]
import fs from 'node:fs';

const readText = (p) => {
  const buf = fs.readFileSync(p);
  if (buf.length >= 2 && buf[0] === 0xff && buf[1] === 0xfe) return buf.toString('utf16le').replace(/^\uFEFF/, '');
  return buf.toString('utf8').replace(/^\uFEFF/, '');
};
function firstJson(text) {
  const start = text.indexOf('{');
  let depth = 0, inStr = false, esc = false;
  for (let i = start; i < text.length; i++) {
    const ch = text[i];
    if (inStr) { if (esc) esc = false; else if (ch === '\\') esc = true; else if (ch === '"') inStr = false; continue; }
    if (ch === '"') inStr = true;
    else if (ch === '{') depth++;
    else if (ch === '}') { depth--; if (depth === 0) return text.slice(start, i + 1); }
  }
}
const page = JSON.parse(readText(process.argv[2])).result;
const check = JSON.parse(firstJson(readText(process.argv[3]))).result ?? JSON.parse(firstJson(readText(process.argv[3])));
const maxDist = Number(process.argv[4] ?? 160);

const pins = [];
for (const c of page.components) {
  if (!c.designator) continue;
  for (const p of c.pins || []) pins.push({ ref: c.designator, pin: p.pinNumber, name: p.pinName, net: p.net || '', x: p.x, y: p.y });
}

const distToBox = (x, y, b) => {
  const dx = Math.max(b.minX - x, 0, x - b.maxX);
  const dy = Math.max(b.minY - y, 0, y - b.maxY);
  return Math.hypot(dx, dy);
};

const out = [];
for (const f of check.findings ?? []) {
  if (f.type !== 'marker-overlap') continue;
  for (const [id, net, box] of [[f.primitiveId, f.markerNet, f.bbox], [f.other?.primitiveId, f.other?.net, f.other?.bbox]]) {
    if (!id || !box) continue;
    const cands = pins
      .filter((p) => p.net === net)
      .map((p) => ({ ...p, d: distToBox(p.x, p.y, box) }))
      .filter((p) => p.d <= maxDist)
      .sort((a, b) => a.d - b.d);
    const best = cands[0];
    out.push({
      markerId: id,
      net,
      kind: f.componentType === 'netflag' && net === 'GND' ? 'gnd' : undefined,
      guess: best ? `${best.ref}:${best.pin}` : 'UNMAPPED',
      dist: best ? Math.round(best.d * 10) / 10 : null,
      runnersUp: cands.slice(1, 3).map((c) => `${c.ref}:${c.pin}@${Math.round(c.d)}`),
    });
  }
}

const seen = new Set();
for (const r of out) {
  const key = `${r.markerId}`;
  if (seen.has(key)) continue;
  seen.add(key);
  console.log(`${r.markerId} net=${r.net.padEnd(8)} -> ${r.guess.padEnd(12)} dist=${String(r.dist).padEnd(6)} others: ${r.runnersUp.join(' ') || '-'}`);
}
console.log(`\nmarkers=${seen.size}`);
// 去重后的引脚清单，供逐脚重放
const pinList = [...new Set(out.map((r) => r.guess).filter((g) => g !== 'UNMAPPED'))];
console.log('pins: ' + pinList.join(' '));

// 位置参数: [2]=page1.json [3]=check.json [4]=maxDist(可选,默认160) [5]=netlist.json(可选) [6]=输出(可选)
if (process.argv[5] && process.argv[5].endsWith('.json')) {
  const nl = JSON.parse(readText(process.argv[5]));
  const byPin = {};
  for (const c of nl.connections) byPin[c.pin] = c;
  const lines = [];
  for (const p of pinList) {
    const c = byPin[p];
    if (!c) { console.log(`WARN: ${p} not in netlist`); continue; }
    lines.push(`${p}\t${c.kind}\t${c.net}`);
  }
  const outPath = process.argv[6] ?? 'repair.tsv';
  fs.writeFileSync(outPath, lines.join('\n') + '\n', 'utf8');
  console.log(`wrote ${outPath} with ${lines.length} pins`);
}
