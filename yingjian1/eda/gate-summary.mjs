// 打印 sch gate 报告的结构化摘要（阶段 / 阻断项 / 告警），消息截断。
// 用法: node gate-summary.mjs <gate.json>
import fs from 'node:fs';

function readText(p) {
  const buf = fs.readFileSync(p);
  if (buf.length >= 2 && buf[0] === 0xff && buf[1] === 0xfe) return buf.toString('utf16le').replace(/^\uFEFF/, '');
  return buf.toString('utf8').replace(/^\uFEFF/, '');
}
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
  throw new Error('unbalanced');
}

const raw = JSON.parse(firstJson(readText(process.argv[2])));
const r = raw.result ?? raw;
const clip = (s, n = 110) => String(s ?? '').replace(/\s+/g, ' ').slice(0, n);

console.log('verdict:', r.verdict, ' ok:', r.ok);
for (const st of r.stages ?? []) {
  const name = st.name ?? st.stage ?? st.id ?? '?';
  const status = st.status ?? st.verdict ?? '?';
  const findings = st.findings ?? st.items ?? [];
  console.log(`\n[${name}] ${status}  findings=${Array.isArray(findings) ? findings.length : 'n/a'}`);
  if (st.summary) console.log('  summary:', clip(JSON.stringify(st.summary), 300));
  if (Array.isArray(findings)) {
    const byCat = {};
    for (const f of findings) {
      const k = `${f.severity ?? f.level ?? '?'}|${f.category ?? f.rule ?? f.code ?? '?'}`;
      byCat[k] = (byCat[k] ?? 0) + 1;
    }
    for (const [k, v] of Object.entries(byCat)) console.log(`    ${k} x${v}`);
    for (const f of findings.slice(0, 8)) {
      console.log(`      - ${clip(f.message ?? f.detail ?? JSON.stringify(f), 150)}`);
    }
    if (findings.length > 8) console.log(`      ... ${findings.length - 8} more`);
  } else if (typeof findings !== 'object') {
    console.log('  raw:', clip(JSON.stringify(st), 200));
  }
}
for (const key of ['blockers', 'warnings']) {
  const list = r[key];
  if (!Array.isArray(list) || !list.length) continue;
  console.log(`\n${key} (${list.length}):`);
  for (const b of list.slice(0, 12)) console.log('  -', clip(typeof b === 'string' ? b : (b.message ?? JSON.stringify(b)), 160));
  if (list.length > 12) console.log(`  ... ${list.length - 12} more`);
}
