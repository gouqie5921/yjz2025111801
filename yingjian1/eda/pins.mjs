// 读取 easyeda sch list / read 的 JSON 输出，打印器件与引脚表
// 用法: node pins.mjs <json-file> [designator-filter]
import fs from 'node:fs';

const file = process.argv[2];
const filter = process.argv[3];

// PowerShell 的 > 重定向会写出 UTF-16LE/BOM，这里统一归一化成字符串
function readText(p) {
  const buf = fs.readFileSync(p);
  if (buf.length >= 2 && buf[0] === 0xff && buf[1] === 0xfe) return buf.toString('utf16le').replace(/^\uFEFF/, '');
  if (buf.length >= 2 && buf[0] === 0xfe && buf[1] === 0xff) return buf.swap16().toString('utf16le').replace(/^\uFEFF/, '');
  return buf.toString('utf8').replace(/^\uFEFF/, '');
}

const raw = JSON.parse(readText(file));
const r = raw.result ?? raw;

const comps = r.components ?? r.items ?? [];
if (process.argv.includes('--keys')) {
  console.log('top keys:', Object.keys(r).join(', '));
  if (comps[0]) console.log('component keys:', Object.keys(comps[0]).join(', '));
}

// --compact: 一行一个器件，便于核对全部引脚名/编号
if (process.argv.includes('--compact')) {
  const real = comps.filter((c) => c.componentType !== 'sheet');
  console.log('parts:', real.length);
  for (const c of real) {
    const pins = (c.pins || []).map((p) => `${p.pinNumber ?? p.number}=${p.pinName ?? p.name}`).join(' ');
    const bb = c.bbox ? `[${c.bbox.minX},${c.bbox.minY},${c.bbox.maxX},${c.bbox.maxY}]` : '';
    const nm = String((c.device && c.device.name) || '').slice(0, 24);
    console.log(`${String(c.designator || '?').padEnd(4)} ${nm.padEnd(24)} ${bb.padEnd(26)} ${pins}`);
  }
  process.exit(0);
}

// --pos: 只打印位号与坐标，便于确认布局
if (process.argv.includes('--pos')) {
  const real = comps.filter((c) => c.designator);
  console.log(real.map((c) => `${c.designator}@(${c.x},${c.y})`).join('  '));
  process.exit(0);
}

// --nets: 汇总每只脚的网络，用于 pin→net 对账
if (process.argv.includes('--nets')) {
  const real = comps.filter((c) => c.designator);
  const byNet = {};
  let connected = 0, nc = 0, floating = 0;
  const floatList = [];
  for (const c of real) {
    for (const p of c.pins || []) {
      const key = `${c.designator}:${p.pinNumber}`;
      if (p.noConnected) { nc++; byNet['<NC>'] = (byNet['<NC>'] ?? []).concat(key); continue; }
      if (p.net) { connected++; (byNet[p.net] = byNet[p.net] ?? []).push(key); }
      else { floating++; floatList.push(key); }
    }
  }
  console.log(`connected=${connected} nc=${nc} floating=${floating} nets=${Object.keys(byNet).length}`);
  if (floatList.length) console.log('FLOATING: ' + floatList.join(' '));
  const names = Object.keys(byNet).sort();
  for (const n of names) console.log(`  ${n.padEnd(12)} n=${String(byNet[n].length).padEnd(3)} ${byNet[n].join(' ')}`);
  process.exit(0);
}

console.log('components:', comps.length);

for (const c of comps) {
  const ref = c.designator || c.ref || c.name || '?';
  if (filter && ref !== filter) continue;
  const pid = c.primitiveId || c.id || '';
  const bb = c.bbox ? ` bbox=[${c.bbox.minX},${c.bbox.minY},${c.bbox.maxX},${c.bbox.maxY}]` : '';
  console.log(`--- ${ref} @(${c.x},${c.y}) rot=${c.rotation ?? ''} mirror=${c.mirror ?? ''} pid=${pid}${bb}`);
  const pins = c.pins || [];
  if (!pins.length) { console.log('    (no pins)'); continue; }
  const rows = pins.map((p) => {
    const num = String(p.number ?? p.pinNumber ?? '');
    const nm = String(p.name ?? p.pinName ?? '');
    const net = String(p.net ?? '');
    const nc = p.noConnected ? ' NC' : '';
    const st = p.connectionState ? ` [${p.connectionState}]` : '';
    return `${num.padEnd(6)} ${nm.padEnd(14)} ${net}${nc}${st} (${p.x},${p.y})`;
  });
  console.log('    ' + rows.join('\n    '));
}
