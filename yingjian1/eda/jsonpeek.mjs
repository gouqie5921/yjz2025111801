// 打印 JSON 结构概览：顶层键、数组长度、每类条目的字段名与前 2 个样例（截断）。
// 用法: node jsonpeek.mjs <file> [--clip N]
import fs from 'node:fs';

function readText(p) {
  const buf = fs.readFileSync(p);
  if (buf.length >= 2 && buf[0] === 0xff && buf[1] === 0xfe) return buf.toString('utf16le').replace(/^\uFEFF/, '');
  return buf.toString('utf8').replace(/^\uFEFF/, '');
}
function firstJson(text) {
  const start = text.indexOf('{');
  if (start < 0) throw new Error('no JSON');
  let depth = 0, inStr = false, esc = false;
  for (let i = start; i < text.length; i++) {
    const ch = text[i];
    if (inStr) { if (esc) esc = false; else if (ch === '\\') esc = true; else if (ch === '"') inStr = false; continue; }
    if (ch === '"') inStr = true;
    else if (ch === '{') depth++;
    else if (ch === '}') { depth--; if (depth === 0) return text.slice(start, i + 1); }
  }
  throw new Error('unbalanced');
}

const clipArgIdx = process.argv.indexOf('--clip');
const clipN = clipArgIdx > 0 ? Number(process.argv[clipArgIdx + 1]) : 300;
const clip = (s, n = clipN) => String(s).replace(/\s+/g, ' ').slice(0, n);

const raw = JSON.parse(firstJson(readText(process.argv[2])));
const r = raw.result ?? raw;

for (const [k, v] of Object.entries(r)) {
  if (Array.isArray(v)) {
    console.log(`${k}: array(${v.length})`);
    if (v.length && typeof v[0] === 'object' && v[0] !== null) {
      console.log(`   item keys: ${Object.keys(v[0]).join(',')}`);
      for (const it of v.slice(0, 2)) console.log('   ' + clip(JSON.stringify(it)));
    } else {
      console.log('   values: ' + clip(v.join(',')));
    }
  } else if (v && typeof v === 'object') {
    console.log(`${k}: object keys=${Object.keys(v).join(',')}`);
    console.log('   ' + clip(JSON.stringify(v)));
  } else {
    console.log(`${k}: ${clip(String(v), 200)}`);
  }
}
