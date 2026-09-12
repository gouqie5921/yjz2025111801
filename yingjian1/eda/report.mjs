// 汇总 easyeda 的 JSON 报告：自动找出条目数组，统计状态并列出异常项。
// 用法: node report.mjs <json-file> [--all]
import fs from 'node:fs';

function readText(p) {
  const buf = fs.readFileSync(p);
  if (buf.length >= 2 && buf[0] === 0xff && buf[1] === 0xfe) return buf.toString('utf16le').replace(/^\uFEFF/, '');
  return buf.toString('utf8').replace(/^\uFEFF/, '');
}

// CLI 有时在 JSON 之后追加人类可读摘要；用括号配平截出首个 JSON 对象
function firstJson(text) {
  const start = text.indexOf('{');
  if (start < 0) throw new Error('no JSON object found');
  let depth = 0, inStr = false, esc = false;
  for (let i = start; i < text.length; i++) {
    const ch = text[i];
    if (inStr) {
      if (esc) esc = false;
      else if (ch === '\\') esc = true;
      else if (ch === '"') inStr = false;
      continue;
    }
    if (ch === '"') inStr = true;
    else if (ch === '{') depth++;
    else if (ch === '}') {
      depth--;
      if (depth === 0) return text.slice(start, i + 1);
    }
  }
  throw new Error('unbalanced JSON object');
}

const raw = JSON.parse(firstJson(readText(process.argv[2])));
const showAll = process.argv.includes('--all');
const r = raw.result ?? raw;

console.log('top keys:', Object.keys(r).join(', '));

// 找最长的数组字段作为条目列表（优先已知的明细字段名）
let listKey = null;
for (const preferred of ['connections', 'items', 'results', 'steps', 'entries']) {
  if (Array.isArray(r[preferred])) { listKey = preferred; break; }
}
if (!listKey) {
  for (const [k, v] of Object.entries(r)) {
    if (Array.isArray(v) && (!listKey || v.length > r[listKey].length)) listKey = k;
  }
}
if (!listKey) {
  console.log(JSON.stringify(r).slice(0, 1200));
  process.exit(0);
}
const items = r[listKey];
console.log(`items in "${listKey}": ${items.length}`);

const statusOf = (it) => it.status ?? it.state ?? it.result ?? (it.ok === true ? 'ok' : it.ok === false ? 'fail' : '?');
const counts = {};
for (const it of items) {
  const s = String(statusOf(it));
  counts[s] = (counts[s] ?? 0) + 1;
}
console.log('status:', JSON.stringify(counts));

const bad = items.filter((it) => !/^(ok|placed|skipped|already-connected|connected|unchanged|planned)$/i.test(String(statusOf(it))));
console.log(`non-ok items: ${bad.length}`);
const show = showAll ? items : bad;
for (const it of show.slice(0, 60)) {
  const pin = it.pin ?? it.pinRef ?? it.designator ?? '';
  const net = it.net ?? '';
  const dir = it.direction ?? '';
  const off = it.offset ?? '';
  const msg = it.message ?? it.error ?? it.reason ?? it.warning ?? '';
  console.log(`  ${String(statusOf(it)).padEnd(18)} ${String(pin).padEnd(12)} ${String(net).padEnd(12)} dir=${dir} off=${off} ${JSON.stringify(msg).slice(0, 120)}`);
}
if (show.length > 60) console.log(`  ... ${show.length - 60} more`);
