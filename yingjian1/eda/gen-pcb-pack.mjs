// 生成 PCB 摆放 playbook：用 pcb-dump.json 的 anchor/bbox 偏移，把「目标 bbox 中心」
// 换算成锚点坐标，从而走 --patch（不触发 --center 的每次重载要求）。
// 用法: node gen-pcb-pack.mjs
import fs from 'node:fs';
import path from 'node:path';

const dir = path.dirname(new URL(import.meta.url).pathname).replace(/^\/([A-Za-z]:)/, '$1');
const dumpRaw = JSON.parse(fs.readFileSync(path.join(dir, 'pcb-dump.json'), 'utf8').replace(/^\uFEFF/, ''));
const dump = dumpRaw.result ?? dumpRaw;
const info = {};
for (const c of dump.components) {
  const bx = (c.bbox.minX + c.bbox.maxX) / 2, by = (c.bbox.minY + c.bbox.maxY) / 2;
  info[c.designator] = { pid: c.primitiveId, dx: c.x - bx, dy: c.y - by, w: c.bbox.maxX - c.bbox.minX, h: c.bbox.maxY - c.bbox.minY };
}

// 目标 bbox 中心（mil，板框 0..2098 × 0..902）
const T = {
  J5: [1050, 762], J6: [1050, 140],
  J1: [250, 451], J4: [1790, 480],
  J3: [560, 250], J2: [560, 650],
  U1: [1090, 451], U2: [430, 700], U3: [1650, 640],
  C1: [330, 660], C2: [430, 640], C3: [540, 700], C4: [540, 620],
  D1: [330, 560], R1: [250, 700], R2: [250, 640],
  D2: [700, 770], R5: [640, 770], D3: [700, 600], R6: [640, 600],
  C5: [900, 210], C6: [960, 210], C7: [1020, 210], C8: [1080, 210], C9: [1140, 210], C10: [1200, 210],
  Y1: [850, 300], C11: [790, 300], C12: [910, 300],
  C13: [1000, 330], R3: [1180, 300], R4: [1240, 300],
  C14: [1570, 640], R7: [1730, 640], JP2: [1650, 720],
  SW1: [1090, 700], JP1: [990, 700], JP3: [1190, 700],
};

const missing = Object.keys(T).filter((r) => !info[r]);
if (missing.length) { console.log('missing: ' + missing.join(',')); process.exit(1); }

// 尺寸概览 + 越界提醒
for (const [ref, [x, y]] of Object.entries(T)) {
  const i = info[ref];
  const x0 = x - i.w / 2, x1 = x + i.w / 2, y0 = y - i.h / 2, y1 = y + i.h / 2;
  if (x0 < 5 || y0 < 5 || x1 > 2093 || y1 > 897) console.log(`OUT   ${ref} bbox [${x0.toFixed(0)},${y0.toFixed(0)}]-[${x1.toFixed(0)},${y1.toFixed(0)}] size ${i.w.toFixed(0)}x${i.h.toFixed(0)}`);
}

const steps = Object.entries(T).map(([ref, [x, y]]) => {
  const i = info[ref];
  const ax = Math.round(x - i.dx), ay = Math.round(y - i.dy);
  const patch = { x: ax, y: ay };
  if (ref === 'J4') patch.rotation = 90;
  return { id: 'mv-' + ref, run: 'pcb modify', flags: { id: i.pid, patch: JSON.stringify(patch) } };
});
const playbook = {
  version: 1,
  meta: { name: 'test1-pcb-pack', description: 'PCB 摆放（锚点换算，patch 批量）', project: 'yingjian1', doc: 'PCB1' },
  defaults: { timeoutSec: 30, retry: 1 },
  steps,
};
fs.writeFileSync(path.join(dir, 'pcb-pack.playbook.json'), JSON.stringify(playbook, null, 1), 'utf8');
console.log(`wrote pcb-pack.playbook.json (${steps.length} steps)`);
