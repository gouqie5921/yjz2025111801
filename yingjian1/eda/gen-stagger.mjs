// 定向修复网标重叠：把重叠网标映射回引脚，按「外法向 + 分级错开桩长」重放。
// 错开规则：同一器件同一方向的多个引脚，按沿边顺序轮流取 20/60/100/140 四档桩长，
// 使相邻脚的网标落在不同 x（或 y）层上，从而不再互相覆盖。
// 输出: stagger.playbook.json（disconnect + connect 显式方向/长度）
import fs from 'node:fs';
import path from 'node:path';

const dir = path.dirname(new URL(import.meta.url).pathname).replace(/^\/([A-Za-z]:)/, '$1');
const readText = (p) => fs.readFileSync(p, 'utf8').replace(/^\uFEFF/, '');
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
  throw new Error('unbalanced json');
}

const page = JSON.parse(readText(path.join(dir, 'page1.json'))).result;
const check = JSON.parse(firstJson(readText(path.join(dir, 'check.json')))).result;
const netlist = JSON.parse(readText(path.join(dir, 'netlist.json')));
const netOf = {};
for (const c of netlist.connections) netOf[c.pin] = { kind: c.kind, net: c.net };

// 引脚 + 器件 bbox
const parts = {};
for (const c of page.components) {
  if (!c.designator) continue;
  parts[c.designator] = { bbox: c.bbox, pins: c.pins || [] };
}
const allPins = [];
for (const [ref, p] of Object.entries(parts)) {
  for (const pin of p.pins) allPins.push({ ref, num: pin.pinNumber, net: pin.net || '', x: pin.x, y: pin.y, bbox: p.bbox });
}

const distToBox = (x, y, b) => Math.hypot(Math.max(b.minX - x, 0, x - b.maxX), Math.max(b.minY - y, 0, y - b.minY));

// 1) 重叠网标 -> 引脚
const targets = new Set();
for (const f of check.findings ?? []) {
  if (f.type !== 'marker-overlap') continue;
  for (const [id, net, box] of [[f.primitiveId, f.markerNet, f.bbox], [f.other?.primitiveId, f.other?.net, f.other?.bbox]]) {
    if (!id || !box) continue;
    const cands = allPins.filter((p) => p.net === net).map((p) => ({ ...p, d: distToBox(p.x, p.y, box) }))
      .filter((p) => p.d <= 160).sort((a, b) => a.d - b.d);
    if (cands[0]) targets.add(`${cands[0].ref}:${cands[0].num}`);
  }
}

// 2) 外法向：引脚贴在器件哪条边上
function outward(pin) {
  const b = pin.bbox;
  const dL = Math.abs(pin.x - b.minX), dR = Math.abs(pin.x - b.maxX);
  const dB = Math.abs(pin.y - b.minY), dT = Math.abs(pin.y - b.maxY);
  const m = Math.min(dL, dR, dB, dT);
  if (m === dL) return 'left';
  if (m === dR) return 'right';
  if (m === dB) return 'down';
  return 'up';
}

// 3) 同一 (器件,方向) 分组，按沿边顺序分四档桩长错开
const groups = {};
for (const key of targets) {
  const [ref, num] = key.split(':');
  const pin = allPins.find((p) => p.ref === ref && p.num === num);
  if (!pin) continue;
  const d = outward(pin);
  const order = d === 'left' || d === 'right' ? pin.y : pin.x;
  (groups[`${ref}|${d}`] = groups[`${ref}|${d}`] ?? []).push({ key, pin, d, order });
}

const offsets = [20, 60, 100, 140];
const steps = [];
for (const [gkey, list] of Object.entries(groups)) {
  list.sort((a, b) => b.order - a.order);
  list.forEach((item, i) => {
    const nl = netOf[item.key];
    if (!nl) { console.log(`WARN no netlist entry for ${item.key}`); return; }
    const off = offsets[i % offsets.length];
    const kind = process.argv.includes('--netlabel') ? 'net_label' : nl.kind;
    steps.push({ id: `disc-${item.key.replace(':', '_')}`, run: 'sch disconnect', flags: { pin: item.key } });
    steps.push({
      id: `conn-${item.key.replace(':', '_')}`,
      run: 'sch connect',
      name: `${item.key} -> ${nl.net} ${kind} ${item.d}/${off}`,
      flags: { pin: item.key, kind, net: nl.net, direction: item.d, offset: off },
    });
  });
}

for (const [gkey, list] of Object.entries(groups)) {
  console.log(`${gkey}: ${list.map((x, i) => `${x.key}@${offsets[i % 4]}`).join(' ')}`);
}
console.log(`\ntargets=${targets.size} steps=${steps.length}`);

const playbook = {
  version: 1,
  meta: {
    name: 'test1-stagger-fix',
    description: '定向消除网标重叠：外法向 + 四档错开桩长',
    project: 'yingjian1',
    doc: 'P1',
  },
  defaults: { timeoutSec: 30, retry: 1 },
  steps,
};
fs.writeFileSync(path.join(dir, 'stagger.playbook.json'), JSON.stringify(playbook, null, 1), 'utf8');
console.log('wrote stagger.playbook.json');
