// PCB 布局规划器（离线）：把目标锚点/旋转/层换算成 pcb modify 补丁，并在写入前做几何自检。
// 用法: node plan-pcb.mjs            -> 自检报告
//       node plan-pcb.mjs --pads     -> 附带焊盘/网络核对
//       node plan-pcb.mjs --emit     -> 写出 pcb-place.playbook.json
//
// 坐标：mil，y 向上，板框 0..2098 x 0..902。
// 板两侧 2.54 排针(J5 上 / J6 下)铜箔带：y 733.5..796.5 / 111.5..174.5 —— 其余器件铜箔须留在 182..726。
import fs from 'node:fs';
import path from 'node:path';

const dir = path.dirname(new URL(import.meta.url).pathname).replace(/^\/([A-Za-z]:)/, '$1');
const raw = JSON.parse(fs.readFileSync(path.join(dir, 'pcb-state.json'), 'utf8').replace(/^\uFEFF/, ''));
const dump = raw.result ?? raw;
const comps = new Map(dump.components.map((c) => [c.designator, c]));

// ---- 目标布局：[anchorX, anchorY, rotation, layer] ----
const T = {
  // USB-C 取电口：旋转 270 -> 插口朝左板边（使用方目视确认后由 90 修正为 270）
  J1: [140.8, 451, 270],
  R1: [290, 550, 90],            // CC1 下拉（J2 左移让位）
  R2: [310, 620, 90],            // CC2 下拉
  D1: [370, 430, 90, 2],         // SS34 只进不出（放底面：顶面左侧要给 U1 留出线通道）
  U2: [494, 470, 180],           // AMS1117：VIN 左下 / VOUT tab 右
  C1: [470, 620, 0],             // 10uF +5V 储能
  C2: [630, 285, 0],             // 100nF +5V
  C3: [620, 620, 0],             // 22uF +3V3
  C4: [660, 320, 0],             // 100nF +3V3
  // MCU 及卫星
  U1: [900, 490, 0],
  C5: [780, 275, 0],
  C6: [1130, 570, 0],
  C7: [792, 712, 0],
  C8: [960, 275, 0],
  C9: [1030, 275, 0],
  C10: [1580, 245, 0],
  Y1: [800, 250, 0, 2],          // 8MHz 晶振：放底面，正对 OSC 脚
  C11: [620, 235, 0, 2],
  C12: [960, 235, 90, 2],
  C13: [1010, 211, 0],
  R4: [1150, 285, 0],
  R6: [770, 211, 0],
  D3: [900, 211, 180],
  SW1: [1790, 245, 0],
  // 启动跳线
  JP1: [380, 245, 0],
  R3: [540, 275, 0],
  JP3: [1400, 245, 0],           // PB2<->GND 跳线
  // 调试口 / 串口
  J2: [470, 690, 0],             // SWD（左移 30mil，给 U1 左侧通道让出上出口）
  J3: [1230, 690, 0],            // 串口 5V/GND/TX/RX
  // CAN
  U3: [1250, 450, 0],
  C14: [1250, 290, 0],
  R7: [1620, 510, 0],
  JP2: [1700, 560, 90],
  J4: [2010, 456, 90],           // KF128L-3.5-3P：进线朝右板边，本体 445mil 落在排针带之间
  // 电源指示灯
  R5: [1500, 690, 90],
  D2: [1590, 690, 180],
  // 排针（贴边，保持）
  J5: [1050, 765, 0],
  J6: [1050, 143, 0],
};

const BOARD = { x0: 0, y0: 0, x1: 2098, y1: 902 };
const EDGE_KEEP = 10;      // 铜到板边
const CLEAR = 6;           // 铜-铜最小间距（live 规则 5.98mil）
const HEADER_TOP = 733.5;  // J5 铜箔下沿
const HEADER_BOT = 174.5;  // J6 铜箔上沿

const rot = (deg, [x, y]) => {
  const r = (deg * Math.PI) / 180, c = Math.cos(r), s = Math.sin(r);
  return [x * c - y * s, x * s + y * c];
};

// 由当前 dump 反推每个器件的“局部”几何（相对锚点、未旋转）
const local = new Map();
for (const c of comps.values()) {
  const deg = c.rotation ?? 0;
  const pads = (c.pads ?? []).map((p) => {
    const [lx, ly] = rot(-deg, [p.x - c.x, p.y - c.y]);
    const swap = Math.abs(((deg % 180) + 180) % 180) === 90;
    return { n: p.padNumber, net: p.net || '', x: lx, y: ly, w: swap ? p.height : p.width, h: swap ? p.width : p.height, layer: p.layer };
  });
  const corners = [
    [c.bbox.minX - c.x, c.bbox.minY - c.y],
    [c.bbox.maxX - c.x, c.bbox.maxY - c.y],
    [c.bbox.minX - c.x, c.bbox.maxY - c.y],
    [c.bbox.maxX - c.x, c.bbox.minY - c.y],
  ].map((p) => rot(-deg, p));
  local.set(c.designator, { pads, corners, rotOrig: deg });
}

const results = [];
for (const [ref, [ax, ay, rotation, layer]] of Object.entries(T)) {
  const c = comps.get(ref);
  if (!c) { console.log('MISSING designator', ref); continue; }
  const L = local.get(ref);
  const swap = Math.abs(((rotation % 180) + 180) % 180) === 90;
  const pads = L.pads.map((p) => {
    const [dx, dy] = rot(rotation, [p.x, p.y]);
    const w = swap ? p.h : p.w, h = swap ? p.w : p.h;
    return { n: p.n, net: p.net, x: ax + dx, y: ay + dy, w, h, layer: p.layer };
  });
  const xs = pads.flatMap((p) => [p.x - p.w / 2, p.x + p.w / 2]);
  const ys = pads.flatMap((p) => [p.y - p.h / 2, p.y + p.h / 2]);
  const cu = { x0: Math.min(...xs), x1: Math.max(...xs), y0: Math.min(...ys), y1: Math.max(...ys) };
  const bb = L.corners.map(([x, y]) => rot(rotation, [x, y]));
  const rendered = {
    x0: ax + Math.min(...bb.map((p) => p[0])), x1: ax + Math.max(...bb.map((p) => p[0])),
    y0: ay + Math.min(...bb.map((p) => p[1])), y1: ay + Math.max(...bb.map((p) => p[1])),
  };
  results.push({ ref, pid: c.primitiveId, ax, ay, rotation, layer: layer ?? 1, cu, rendered, pads, origLayer: c.layer, origRot: c.rotation });
}

const rectGap = (a, b) => {
  const dx = Math.max(a.x0 - b.x1, b.x0 - a.x1);
  const dy = Math.max(a.y0 - b.y1, b.y0 - a.y1);
  return Math.max(dx, dy); // >0 = 分离
};
const padRect = (p) => ({ x0: p.x - p.w / 2, x1: p.x + p.w / 2, y0: p.y - p.h / 2, y1: p.y + p.h / 2 });
const padLayer = (p) => p.layer ?? 1;
const padGap = (A, B) => {
  let best = Infinity;
  for (const pa of A.pads) {
    for (const pb of B.pads) {
      const la = padLayer(pa), lb = padLayer(pb);
      if (la !== lb && la !== 12 && lb !== 12) continue;
      const g = rectGap(padRect(pa), padRect(pb));
      if (g < best) best = g;
    }
  }
  return best;
};

const FIXED = new Set(['J5', 'J6']);
console.log('== 自检 ==');
let bad = 0;
for (const r of results) {
  if (FIXED.has(r.ref)) continue;
  const { cu } = r;
  const msgs = [];
  if (cu.x0 < EDGE_KEEP) msgs.push(`左边距 ${cu.x0.toFixed(1)}`);
  if (cu.x1 > BOARD.x1 - EDGE_KEEP) msgs.push(`右边距 ${(BOARD.x1 - cu.x1).toFixed(1)}`);
  if (cu.y0 < EDGE_KEEP) msgs.push(`下边距 ${cu.y0.toFixed(1)}`);
  if (cu.y1 > BOARD.y1 - EDGE_KEEP) msgs.push(`上边距 ${(BOARD.y1 - cu.y1).toFixed(1)}`);
  if (cu.y0 < HEADER_BOT) msgs.push(`压 J6 排针带 (y0=${cu.y0.toFixed(1)})`);
  if (cu.y1 > HEADER_TOP) msgs.push(`压 J5 排针带 (y1=${cu.y1.toFixed(1)})`);
  if (msgs.length) { bad++; console.log(`  BOUND ${r.ref.padEnd(5)} ${msgs.join('; ')}`); }
}
for (let i = 0; i < results.length; i++) {
  for (let j = i + 1; j < results.length; j++) {
    const A = results[i], B = results[j];
    if (A.layer !== B.layer && !A.pads.some((p) => padLayer(p) === 12) && !B.pads.some((p) => padLayer(p) === 12)) continue;
    const g = padGap(A, B);
    if (g < CLEAR) {
      bad++;
      console.log(`  CLASH ${A.ref}<->${B.ref} 焊盘间距 ${g.toFixed(1)}mil   A[${A.cu.x0.toFixed(0)},${A.cu.y0.toFixed(0)}]-[${A.cu.x1.toFixed(0)},${A.cu.y1.toFixed(0)}] B[${B.cu.x0.toFixed(0)},${B.cu.y0.toFixed(0)}]-[${B.cu.x1.toFixed(0)},${B.cu.y1.toFixed(0)}]`);
    }
  }
}
for (const r of results) {
  if (FIXED.has(r.ref) || r.ref === 'U1') continue;
  // LQFP-48 本体 7x7mm：其它器件不得落在芯片包体上
  const body = { x0: 900 - 138, x1: 900 + 138, y0: 490 - 138, y1: 490 + 138 };
  const g = rectGap(r.cu, body);
  if (g < CLEAR) { bad++; console.log(`  BODY  ${r.ref} 落在 U1 包体上/过近 (间隙 ${g.toFixed(1)}mil)`); }
}
for (const r of results) {
  if (FIXED.has(r.ref)) continue;
  if (r.rendered.y1 > 711 && r.rendered.y0 < 824) console.log(`  note ${r.ref} 渲染框进入上排针带 (y ${r.rendered.y0.toFixed(0)}..${r.rendered.y1.toFixed(0)})`);
  if (r.rendered.y0 < 202 && r.rendered.y1 > 89) console.log(`  note ${r.ref} 渲染框进入下排针带 (y ${r.rendered.y0.toFixed(0)}..${r.rendered.y1.toFixed(0)})`);
}
console.log(bad === 0 ? 'OK: 无阻塞几何问题' : `WARN: ${bad} 处需处理`);

if (process.argv.includes('--pads')) {
  console.log('\n== 焊盘/网络核对 ==');
  for (const r of results) {
    const s = r.pads.map((p) => `${p.n}:${p.net || '-'}`).join(' ');
    console.log(r.ref.padEnd(5) + `L${r.layer} r${r.rotation}`.padEnd(9) + s);
  }
}

if (process.argv.includes('--emit')) {
  const steps = results.map((r) => {
    const patch = { x: Math.round(r.ax * 100) / 100, y: Math.round(r.ay * 100) / 100 };
    if (r.rotation !== r.origRot) patch.rotation = r.rotation;
    if (r.layer !== r.origLayer) patch.layer = r.layer;
    return { id: 'place-' + r.ref, run: 'pcb modify', flags: { id: r.pid, patch: JSON.stringify(patch) } };
  });
  const playbook = {
    version: 1,
    meta: { name: 'test1-pcb-place', description: 'PCB 分档布局：USB 左 / 电源 / MCU 中 / CAN 右，避开上下排针带', project: 'yingjian1', doc: 'PCB1' },
    defaults: { timeoutSec: 30, retry: 1 },
    steps,
  };
  fs.writeFileSync(path.join(dir, 'pcb-place.playbook.json'), JSON.stringify(playbook, null, 1), 'utf8');
  console.log(`wrote pcb-place.playbook.json (${steps.length} steps)`);
}




