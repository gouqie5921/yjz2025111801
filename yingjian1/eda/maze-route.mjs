// 迷宫布线器（自研，headless）：读回焊盘/已布铜，给每个网络做 A* 走线（双层 + 过孔），
// 输出 easyeda pcb track / pcb via 的 playbook。所有几何按 live 规则留距（clearance 6mil）。
// 用法: node maze-route.mjs            -> 规划并打印报告
//       node maze-route.mjs --emit     -> 写出 pcb-maze.playbook.json
import fs from 'node:fs';
import path from 'node:path';

const dir = path.dirname(new URL(import.meta.url).pathname).replace(/^\/([A-Za-z]:)/, '$1');
const read = (f) => {
  const t = fs.readFileSync(path.join(dir, f), 'utf8').replace(/^\uFEFF/, '');
  const i = t.indexOf('{');
  return JSON.parse(t.slice(i, t.lastIndexOf('}') + 1));
};
const dump = (() => { const d = read('pcb-state.json'); return d.result ?? d; })();
const tracksJson = read(process.env.TRACKS_FILE ?? 'tracks.json');
const viasJson = read(process.env.VIAS_FILE ?? 'vias.json');
const tracks = (tracksJson.result ?? tracksJson).lines ?? [];
const vias = (viasJson.result ?? viasJson).vias ?? [];
// --nets A,B,C：只布这些网络（其余网络视为已布好，只当障碍）
const ONLY = (() => {
  const i = process.argv.indexOf('--nets');
  return i >= 0 && process.argv[i + 1] ? new Set(process.argv[i + 1].split(',')) : null;
})();

// ---- 参数 ----
const CELL = 2;
const W = Math.floor(2098 / CELL) + 1;   // 420
const H = Math.floor(902 / CELL) + 1;    // 181
const TRACK_W = (() => {
  const i = process.argv.indexOf('--width');
  return i >= 0 && process.argv[i + 1] ? Number(process.argv[i + 1]) : 10;
})();                                     // 线宽 = live 规则 trackWidth 10mil（难走的段可用 6mil，规则下限 5mil）
const CLEAR = 6;                          // live clearance 5.98 → 6
const HALO = Math.ceil((CLEAR + TRACK_W / 2) / CELL);   // 障碍外扩格数 = 3 (15mil)
const VIA_HALO = Math.ceil((CLEAR + 12) / CELL);        // 过孔外扩 = 4 (20mil)
const EDGE = Math.ceil((CLEAR + 10 + TRACK_W / 2) / CELL); // 板边禁布 = 4 格
const VIA_COST = 120;                     // 过孔代价（mil 等效）
const SKIP_NETS = /^(GND|\+3V3|\+5V|VBUS|\+3\.3V)$/i;   // 走铺铜，不细走线

const idx = (x, y) => y * W + x;
const cx = (x) => x * CELL, cy = (y) => y * CELL;

// ---- 栅格：owner[0]=顶层(1lay) owner[1]=底层(2lay) ----
const owner = [new Int16Array(W * H).fill(0), new Int16Array(W * H).fill(0)];
const netIds = new Map();
const netId = (n) => { if (!netIds.has(n)) netIds.set(n, netIds.size + 1); return netIds.get(n); };
const LAY = { 1: 0, 2: 1, 12: null };   // 12 = 通孔：两层都算

const markRect = (lay, x0, y0, x1, y1, v, halo, onlyFree) => {
  const ix0 = Math.max(0, Math.floor((x0 - halo) / CELL)), ix1 = Math.min(W - 1, Math.ceil((x1 + halo) / CELL));
  const iy0 = Math.max(0, Math.floor((y0 - halo) / CELL)), iy1 = Math.min(H - 1, Math.ceil((y1 + halo) / CELL));
  const layers = lay === 12 ? [0, 1] : [LAY[lay] ?? 0];
  for (const L of layers) {
    for (let y = iy0; y <= iy1; y++) {
      for (let x = ix0; x <= ix1; x++) {
        const i = idx(x, y);
        if (onlyFree && owner[L][i] !== 0) continue;
        owner[L][i] = v;
      }
    }
  }
};
const markSeg = (lay, x1, y1, x2, y2, width, v, halo, onlyFree) => {
  const len = Math.hypot(x2 - x1, y2 - y1);
  const steps = Math.max(1, Math.ceil(len / (CELL / 2)));
  const half = width / 2;
  for (let s = 0; s <= steps; s++) {
    const t = s / steps, px = x1 + (x2 - x1) * t, py = y1 + (y2 - y1) * t;
    markRect(lay, px - half, py - half, px + half, py + half, v, halo, onlyFree);
  }
};

// 第一遍：真实铜（后写覆盖先写，同网络重叠无妨）
const isVia = new Set();  // "x,y" 网格坐标：过孔所在格（跨层桥）
for (const c of dump.components) {
  for (const p of c.pads ?? []) {
    if (!p.net) continue;
    markRect(p.layer, p.x - p.width / 2, p.y - p.height / 2, p.x + p.width / 2, p.y + p.height / 2, netId(p.net), 0, false);
    if (p.layer === 12) {   // 通孔焊盘：两层铜天然连通，作为跨层桥
      const x0 = Math.floor((p.x - p.width / 2) / CELL), x1 = Math.ceil((p.x + p.width / 2) / CELL);
      const y0 = Math.floor((p.y - p.height / 2) / CELL), y1 = Math.ceil((p.y + p.height / 2) / CELL);
      for (let y = y0; y <= y1; y++) for (let x = x0; x <= x1; x++) isVia.add(`${x},${y}`);
    }
  }
}
for (const t of tracks) markSeg(t.layer, t.startX, t.startY, t.endX, t.endY, t.lineWidth ?? TRACK_W, netId(t.net), 0, false);
for (const v of vias) {
  const r = (v.diameter ?? 24) / 2;
  markRect(12, v.x - r, v.y - r, v.x + r, v.y + r, netId(v.net), 0, false);
  isVia.add(`${Math.round(v.x / CELL)},${Math.round(v.y / CELL)}`);
}
// 第二遍：只在外扩区填“别人不许进”，不覆盖真实铜
for (const c of dump.components) {
  for (const p of c.pads ?? []) {
    if (!p.net) continue;
    markRect(p.layer, p.x - p.width / 2, p.y - p.height / 2, p.x + p.width / 2, p.y + p.height / 2, netId(p.net), CLEAR + TRACK_W / 2, true);
  }
}
for (const t of tracks) markSeg(t.layer, t.startX, t.startY, t.endX, t.endY, t.lineWidth ?? TRACK_W, netId(t.net), CLEAR + TRACK_W / 2, true);
for (const v of vias) {
  const r = (v.diameter ?? 24) / 2;
  markRect(12, v.x - r, v.y - r, v.x + r, v.y + r, netId(v.net), CLEAR + TRACK_W / 2, true);
}
// ---- 板边禁布 ----
for (let L = 0; L < 2; L++) {
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
    if (x < EDGE || y < EDGE || x > W - 1 - EDGE || y > H - 1 - EDGE) if (owner[L][idx(x, y)] === 0) owner[L][idx(x, y)] = -1;
  }
}
// ---- 器件本体禁布（渲染框收缩 25% 近似包体；贴片只挡本层，通孔两层面都挡）----
for (const c of dump.components) {
  const b = c.bbox;
  const sx = (b.maxX - b.minX) * 0.125, sy = (b.maxY - b.minY) * 0.125;
  const x0 = b.minX + sx, x1 = b.maxX - sx, y0 = b.minY + sy, y1 = b.maxY - sy;
  const tht = (c.pads ?? []).some((p) => p.layer === 12);
  // 通孔件的塑料本体只在顶面；底面对应位置只有焊盘/焊脚，走线仍可贴着焊盘进线
  const layers = tht ? [1] : [c.layer === 2 ? 2 : 1];
  for (const lay of layers) markRect(lay, x0, y0, x1, y1, -1, CLEAR + TRACK_W / 2, true);
}

// ---- 每个信号网络的连通分量 ----
const padsByNet = new Map();
for (const c of dump.components) for (const p of c.pads ?? []) {
  if (!p.net || SKIP_NETS.test(p.net)) continue;
  if (!padsByNet.has(p.net)) padsByNet.set(p.net, []);
  padsByNet.get(p.net).push(p);
}
const walkable = (L, i, v) => owner[L][i] === 0 || owner[L][i] === v;

const componentsOf = (net) => {
  const v = netId(net);
  const seen = new Uint8Array(W * H * 2);
  const comps = [];
  for (let L = 0; L < 2; L++) for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
    const i = idx(x, y);
    if (owner[L][i] !== v) continue;
    if (seen[L * W * H + i]) continue;
    const cellList = [];
    const q = [[L, x, y]];
    seen[L * W * H + i] = 1;
    while (q.length) {
      const [l, qx, qy] = q.pop();
      const qi = idx(qx, qy);
      cellList.push([l, qx, qy]);
      const push = (nl, nx, ny) => {
        if (nx < 0 || ny < 0 || nx >= W || ny >= H) return;
        const ni = idx(nx, ny);
        if (owner[nl][ni] !== v) return;
        if (seen[nl * W * H + ni]) return;
        seen[nl * W * H + ni] = 1; q.push([nl, nx, ny]);
      };
      push(l, qx + 1, qy); push(l, qx - 1, qy); push(l, qx, qy + 1); push(l, qx, qy - 1);
      if (isVia.has(`${qx},${qy}`)) push(1 - l, qx, qy);
    }
    comps.push(cellList);
  }
  return comps;
};

// ---- A* ----
const viaOK = (x, y, v) => {
  for (let dy = -VIA_HALO; dy <= VIA_HALO; dy++) for (let dx = -VIA_HALO; dx <= VIA_HALO; dx++) {
    const nx = x + dx, ny = y + dy;
    if (nx < EDGE || ny < EDGE || nx > W - 1 - EDGE || ny > H - 1 - EDGE) return false;
    for (let L = 0; L < 2; L++) if (!walkable(L, idx(nx, ny), v)) return false;
  }
  return true;
};

const astar = (net, srcCells, dstCells) => {
  const v = netId(net);
  const src = new Set(), dst = new Set();
  for (const [L, x, y] of srcCells) src.add(`${L},${x},${y}`);
  for (const [L, x, y] of dstCells) dst.add(`${L},${x},${y}`);
  const dstXY = dstCells.map(([, x, y]) => [x, y]);
  const h = (x, y) => { let m = Infinity; for (const [dx, dy] of dstXY) m = Math.min(m, (Math.abs(dx - x) + Math.abs(dy - y)) * CELL); return m; };
  const gScore = new Map(); const came = new Map();
  // 简易二叉堆
  const heap = [];
  const push = (f, key) => { heap.push([f, key]); let i = heap.length - 1; while (i > 0) { const p = (i - 1) >> 1; if (heap[p][0] <= heap[i][0]) break; [heap[p], heap[i]] = [heap[i], heap[p]]; i = p; } };
  const pop = () => { const top = heap[0], last = heap.pop(); if (heap.length) { heap[0] = last; let i = 0; for (;;) { const l = 2 * i + 1, r = l + 1; let m = i; if (l < heap.length && heap[l][0] < heap[m][0]) m = l; if (r < heap.length && heap[r][0] < heap[m][0]) m = r; if (m === i) break; [heap[m], heap[i]] = [heap[i], heap[m]]; i = m; } } return top; };
  for (const k of src) { gScore.set(k, 0); const [, x, y] = k.split(',').map(Number); push(h(x, y), k); }
  let goal = null;
  while (heap.length) {
    const [, key] = pop();
    if (dst.has(key)) { goal = key; break; }
    const [L, x, y] = key.split(',').map(Number);
    const g = gScore.get(key);
    const nb = [[L, x + 1, y], [L, x - 1, y], [L, x, y + 1], [L, x, y - 1]];
    for (const [nl, nx, ny] of nb) {
      if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
      if (!walkable(nl, idx(nx, ny), v)) continue;
      const nk = `${nl},${nx},${ny}`; const ng = g + CELL;
      if (gScore.has(nk) && gScore.get(nk) <= ng) continue;
      gScore.set(nk, ng); came.set(nk, key); push(ng + h(nx, ny), nk);
    }
    if (viaOK(x, y, v)) {
      const nk = `${1 - L},${x},${y}`; const ng = g + VIA_COST;
      if (!(gScore.has(nk) && gScore.get(nk) <= ng)) { gScore.set(nk, ng); came.set(nk, key); push(ng + h(x, y), nk); }
    }
  }
  if (!goal) return null;
  const cells = [];
  for (let k = goal; k !== undefined; k = came.get(k)) cells.push(k.split(',').map(Number));
  cells.reverse();
  return cells;
};

// ---- 逐网络：分量 MST → 每段 A* ----
const playbookSteps = [];
const report = [];
let routed = 0, failed = 0;
const dist2 = (a, b) => { let m = Infinity; for (const [, ax, ay] of a) for (const [, bx, by] of b) { const d = Math.abs(ax - bx) + Math.abs(ay - by); if (d < m) m = d; } return m; };

for (const [net] of padsByNet) {
  if (ONLY && !ONLY.has(net)) continue;
  let comps = componentsOf(net);
  if (comps.length <= 1) continue;
  const v = netId(net);
  let guard = 0;
  while (comps.length > 1 && guard++ < 40) {
    // 贪心 MST：每次连最近的一对分量；失败则退到次近的一对
    const pairs = [];
    for (let a = 0; a < comps.length; a++) for (let b = a + 1; b < comps.length; b++) pairs.push({ a, b, d: dist2(comps[a], comps[b]) });
    pairs.sort((p, q) => p.d - q.d);
    let done = false;
    for (const pr of pairs) {
      const path = astar(net, comps[pr.a], comps[pr.b]);
      if (!path) continue;
      // 路径 → 线段 + 过孔
      const segs = []; let runStart = 0;
      for (let i = 1; i <= path.length; i++) {
        const changed = i === path.length || path[i][0] !== path[i - 1][0];
        if (!changed) continue;
        const run = path.slice(runStart, i);
        let s = 0;
        for (let j = 1; j <= run.length; j++) {
          const dirChanged = j === run.length ||
            Math.sign(run[j][1] - run[j - 1][1]) !== Math.sign(run[j - 1][1] - run[j - 2]?.[1] ?? 0) ||
            Math.sign(run[j][2] - run[j - 1][2]) !== Math.sign(run[j - 1][2] - run[j - 2]?.[2] ?? 0);
          if (!dirChanged) continue;
          if (j - 1 > s) segs.push({ layer: run[s][0] + 1, x1: cx(run[s][1]), y1: cy(run[s][2]), x2: cx(run[j - 1][1]), y2: cy(run[j - 1][2]) });
          s = j - 1;
        }
        if (i < path.length) segs.push({ via: true, layer: path[i - 1][0] + 1, x: cx(path[i - 1][1]), y: cy(path[i - 1][2]) });
        runStart = i;
      }
      for (const sg of segs) {
        if (sg.via) {
          playbookSteps.push({ id: `via-${net}-${playbookSteps.length}`, run: 'pcb via', flags: { net, x: sg.x, y: sg.y, diameter: 24, hole: 12 } });
          const gx = Math.round(sg.x / CELL), gy = Math.round(sg.y / CELL);
          isVia.add(`${gx},${gy}`);
          markRect(12, sg.x - 12, sg.y - 12, sg.x + 12, sg.y + 12, v, 0, false);
          markRect(12, sg.x - 12, sg.y - 12, sg.x + 12, sg.y + 12, v, CLEAR + TRACK_W / 2, true);
        } else if (sg.x1 !== sg.x2 || sg.y1 !== sg.y2) {
          playbookSteps.push({ id: `trk-${net}-${playbookSteps.length}`, run: 'pcb track', flags: { net, layer: sg.layer, x1: sg.x1, y1: sg.y1, x2: sg.x2, y2: sg.y2, width: TRACK_W } });
          markSeg(sg.layer, sg.x1, sg.y1, sg.x2, sg.y2, TRACK_W, v, 0, false);
          markSeg(sg.layer, sg.x1, sg.y1, sg.x2, sg.y2, TRACK_W, v, CLEAR + TRACK_W / 2, true);
        }
      }
      routed++;
      report.push(`  ok   ${net}: ${segs.filter((s) => !s.via).length} 段 + ${segs.filter((s) => s.via).length} 过孔`);
      comps = componentsOf(net);   // 逐跳重算真实连通分量
      done = true;
      break;
    }
    if (!done) {
      failed++;
      const sizes = comps.map((c) => c.length).sort((a, b) => b - a);
      const s0 = comps[0][0];
      report.push(`  FAIL ${net}: 剩余 ${comps.length} 段无法连通（分量规模 ${sizes.slice(0, 6).join('/')}，起点格 [${s0[1] * CELL},${s0[2] * CELL}] L${s0[0] + 1}）`);
      break;
    }
  }
}

const FIXED = new Set(['J5', 'J6']);
if (process.argv.includes('--stats')) {
  for (let L = 0; L < 2; L++) {
    let free = 0, blocked = 0, copper = 0;
    for (let i = 0; i < W * H; i++) { const o = owner[L][i]; if (o === 0) free++; else if (o < 0) blocked++; else copper++; }
    console.log(`层 ${L + 1}: 空闲 ${free} 格 (${(free / (W * H) * 100).toFixed(1)}%)，禁布 ${blocked}，铜 ${copper}`);
  }
}
if (process.argv.includes('--reach')) {
  const net = process.argv[process.argv.indexOf('--reach') + 1];
  const comps = componentsOf(net);
  const v = netId(net);
  console.log(`网络 ${net}: ${comps.length} 个分量，规模 ${comps.map((c) => c.length).sort((a, b) => b - a).slice(0, 8).join('/')}`);
  for (const [i, c] of comps.entries()) {
    const xs = c.map(([, x]) => x * CELL), ys = c.map(([, , y]) => y * CELL);
    const bbox = `[${Math.min(...xs)},${Math.min(...ys)}]-[${Math.max(...xs)},${Math.max(...ys)}]`;
    console.log(`  分量#${i + 1} 规模 ${c.length} 层 ${[...new Set(c.map(([L]) => L + 1))].join('/')} 范围 ${bbox}`);
  }
  const seen = new Uint8Array(W * H * 2);
  const q = [...comps[0]];
  for (const [L, x, y] of q) seen[L * W * H + idx(x, y)] = 1;
  let n = 0;
  while (q.length) {
    const [L, x, y] = q.pop(); n++;
    const nb = [[L, x + 1, y], [L, x - 1, y], [L, x, y + 1], [L, x, y - 1]];
    for (const [nl, nx, ny] of nb) {
      if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
      if (!walkable(nl, idx(nx, ny), v)) continue;
      if (seen[nl * W * H + idx(nx, ny)]) continue;
      seen[nl * W * H + idx(nx, ny)] = 1; q.push([nl, nx, ny]);
    }
    if (viaOK(x, y, v)) {
      const nl = 1 - L;
      if (!seen[nl * W * H + idx(x, y)]) { seen[nl * W * H + idx(x, y)] = 1; q.push([nl, x, y]); }
    }
  }
  const reachComp = comps.slice(1).map((c, i) => ({ i, hit: c.some(([L, x, y]) => seen[L * W * H + idx(x, y)]) }));
  console.log(`  从最大分量洪水可达 ${n} 格；其它分量是否落在可达域: ${reachComp.map((r) => `#${r.i + 1}:${r.hit}`).join(' ')}`);
}
if (process.argv.includes('--map')) {
  const spec = process.argv[process.argv.indexOf('--map') + 1];   // x0,y0,x1,y1 或 加 ,L
  const [x0, y0, x1, y1, lay] = spec.split(',').map(Number);
  const L = (lay ?? 1) === 2 ? 1 : 0;
  const names = new Map([...netIds.entries()].map(([k, v]) => [v, k]));
  console.log(`== 栅格图 层${L + 1} x ${x0}..${x1} y ${y0}..${y1}（. 空 # 禁布 字母=铜）==`);
  for (let y = y1; y >= y0; y -= CELL * 2) {
    let row = String(y).padStart(4) + ' ';
    for (let x = x0; x <= x1; x += CELL * 2) {
      const o = owner[L][idx(Math.round(x / CELL), Math.round(y / CELL))];
      row += o === 0 ? '.' : o < 0 ? '#' : String.fromCharCode(97 + ((o - 1) % 26));
    }
    console.log(row);
  }
  console.log('网络字母: ' + [...netIds.entries()].map(([k, v]) => String.fromCharCode(97 + ((v - 1) % 26)) + '=' + k).join(' '));
}
for (const line of report) console.log(line);
console.log(`连通段 ${routed}，失败 ${failed}；生成 track ${playbookSteps.filter((s) => s.run === 'pcb track').length} 条，via ${playbookSteps.filter((s) => s.run === 'pcb via').length} 个`);

if (process.argv.includes('--emit')) {
  const playbook = {
    version: 1,
    meta: { name: 'test1-pcb-maze', description: '迷宫级补线：剩余长跳线（双层 + 过孔，按 live 规则留距）', project: 'yingjian1', doc: 'PCB1' },
    defaults: { timeoutSec: 30, retry: 1 },
    steps: playbookSteps,
  };
  fs.writeFileSync(path.join(dir, 'pcb-maze.playbook.json'), JSON.stringify(playbook, null, 1), 'utf8');
  console.log(`wrote pcb-maze.playbook.json (${playbookSteps.length} steps)`);
}
