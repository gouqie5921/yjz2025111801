// 手工分区布局：按功能模块上架摆放，留足网标扇出通道（U1 两侧各留 ~95 raw）。
// A3 = 1655x1170 (y-up)，图签禁放区 x>=953 且 y<=198 —— 全部内容放在 y>=260。
// 输出: pack.playbook.json（一串 sch modify 绝对坐标写回）
import fs from 'node:fs';
import path from 'node:path';

const dir = path.dirname(new URL(import.meta.url).pathname).replace(/^\/([A-Za-z]:)/, '$1');
const page = JSON.parse(fs.readFileSync(path.join(dir, 'page1.json'), 'utf8').replace(/^\uFEFF/, '')).result;
const byRef = {};
for (const c of page.components) if (c.designator) byRef[c.designator] = c;

// 目标坐标（raw，y 向上）
const target = {
  // ---- PWR: 3x3 网格，间距 120 ----
  J1: [100, 1080], D1: [220, 1080], C1: [340, 1080],
  U2: [100, 960], R1: [220, 960], C2: [340, 960],
  C3: [100, 840], R2: [220, 840], C4: [340, 840],

  // ---- MCU: U1 居中于 (700,850)，外围左右两列 + 上下 ----
  U1: [700, 850],
  C5: [520, 1010], C6: [520, 960], C7: [520, 910], C8: [520, 860], C9: [520, 810],
  C10: [430, 1010], C11: [430, 960], C12: [430, 910], C13: [430, 860], Y1: [430, 810],
  R3: [880, 1010], R4: [880, 960], JP1: [880, 910], JP3: [880, 860], SW1: [880, 810],

  // ---- CAN: 3x2 ----
  U3: [1150, 1050], J4: [1280, 1050], C14: [1410, 1050],
  R7: [1150, 900], JP2: [1280, 900],

  // ---- LED: 2x2 ----
  D2: [150, 500], D3: [280, 500], R5: [150, 370], R6: [280, 370],

  // ---- DBG ----
  J2: [1150, 450], J3: [1260, 450],

  // ---- HDR: 两根 1x20 并排（高 211）----
  J5: [1420, 450], J6: [1560, 450],
};

const missing = Object.keys(target).filter((r) => !byRef[r]);
const extra = Object.keys(byRef).filter((r) => !target[r]);
if (missing.length || extra.length) {
  console.log('missing in page:', missing.join(','));
  console.log('not in layout:', extra.join(','));
  process.exit(1);
}

// 碰撞自检：用实测 bbox 半宽/半高做 AABB 检查
const boxes = [];
for (const [ref, [x, y]] of Object.entries(target)) {
  const b = byRef[ref].bbox;
  const hw = (b.maxX - b.minX) / 2, hh = (b.maxY - b.minY) / 2;
  boxes.push({ ref, minX: x - hw, maxX: x + hw, minY: y - hh, maxY: y + hh });
}
let collisions = 0;
for (let i = 0; i < boxes.length; i++) {
  for (let j = i + 1; j < boxes.length; j++) {
    const a = boxes[i], b = boxes[j];
    const ox = Math.min(a.maxX, b.maxX) - Math.max(a.minX, b.minX);
    const oy = Math.min(a.maxY, b.maxY) - Math.max(a.minY, b.minY);
    if (ox > 0 && oy > 0) { collisions++; console.log(`COLLIDE ${a.ref}/${b.ref} overlap ${ox.toFixed(1)}x${oy.toFixed(1)}`); }
  }
}
// 图签与出图检查
for (const b of boxes) {
  if (b.minX < 10 || b.minY < 10 || b.maxX > 1645 || b.maxY > 1160) console.log(`OUT-OF-SHEET ${b.ref}`);
  if (b.maxX > 950 && b.minY < 210) console.log(`TITLEBLOCK-HIT ${b.ref}`);
}
console.log(`parts=${boxes.length} collisions=${collisions}`);

const steps = Object.entries(target).map(([ref, [x, y]]) => ({
  id: `move-${ref}`,
  run: 'sch modify',
  flags: { id: byRef[ref].primitiveId, patch: JSON.stringify({ x, y }) },
}));

const playbook = {
  version: 1,
  meta: {
    name: 'test1-minsys-pack',
    description: '第一题最小系统板：按功能分区的确定性摆放（sch modify 绝对坐标）',
    project: 'yingjian1',
    doc: 'P1',
  },
  defaults: { timeoutSec: 30, retry: 1 },
  steps,
};
fs.writeFileSync(path.join(dir, 'pack.playbook.json'), JSON.stringify(playbook, null, 1), 'utf8');
console.log(`wrote pack.playbook.json: ${steps.length} steps`);
