// 生成"清页 + 放置全部器件"的 playbook（供 easyeda apply 执行）
// 用法: node gen-place.mjs [起始位号] [输出文件名]
//   不给参数 = 清页 + 全部器件；给起始位号 = 只放置该位号及其后的器件（不回清页）
import fs from 'node:fs';
import path from 'node:path';

const dir = path.dirname(new URL(import.meta.url).pathname).replace(/^\/([A-Za-z]:)/, '$1');
const bom = JSON.parse(fs.readFileSync(path.join(dir, 'bom.json'), 'utf8'));

const fromRef = process.argv[2];
const outName = process.argv[3] || (fromRef ? 'place-remain.playbook.json' : 'place.playbook.json');

const LIB = bom.library;
const COLS = 7;
const X0 = 70, Y0 = 70, DX = 175, DY = 145;

const startIndex = fromRef
  ? bom.parts.findIndex((p) => p.ref === fromRef)
  : 0;
if (fromRef && startIndex < 0) {
  console.error(`unknown ref: ${fromRef}`);
  process.exit(1);
}

const steps = [];
if (!fromRef) {
  steps.push({
    id: 'clear-page',
    run: 'sch clear',
    name: '清空当前页（保留图签）——确认门控，--yes 放行',
    confirm: true,
  });
}

bom.parts.forEach((p, i) => {
  if (i < startIndex) return;
  const col = i % COLS;
  const row = Math.floor(i / COLS);
  steps.push({
    id: `place-${p.ref}`,
    run: 'sch place',
    name: `放置 ${p.ref} (${p.value} ${p.lcsc})`,
    flags: {
      lib: LIB,
      uuid: p.uuid,
      x: X0 + col * DX,
      y: Y0 + row * DY,
      designator: p.ref,
    },
  });
});

const playbook = {
  version: 1,
  meta: {
    name: fromRef ? 'test1-minsys-place-remain' : 'test1-minsys-place',
    description: fromRef
      ? `第一题最小系统板：续放 ${fromRef} 及其后器件`
      : '第一题最小系统板：清页 + 放置 38 个器件（官方库身份，原子位号）',
    project: 'yingjian1',
    doc: 'P1',
  },
  defaults: { timeoutSec: 30, retry: 1 },
  steps,
};

const out = path.join(dir, outName);
fs.writeFileSync(out, JSON.stringify(playbook, null, 1), 'utf8');
console.log(`wrote ${out}: ${steps.length} steps (from ${bom.parts[startIndex].ref})`);
