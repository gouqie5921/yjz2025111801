// 从 bom.json 生成 sch autolayout 的分区布局 spec
// 用法: node gen-layout.mjs
import fs from 'node:fs';
import path from 'node:path';

const dir = path.dirname(new URL(import.meta.url).pathname).replace(/^\/([A-Za-z]:)/, '$1');
const bom = JSON.parse(fs.readFileSync(path.join(dir, 'bom.json'), 'utf8').replace(/^\uFEFF/, ''));

const modules = bom.modules.map((m) => ({
  name: m.name,
  zone: m.zone,
  core: m.core,
  parts: bom.parts.filter((p) => p.module === m.name).map((p) => p.ref),
}));

const spec = {
  page: 'P1',
  sheet: 'A3',
  modules,
  rules: {
    avoidTitleBlock: true,
    preservePinFanout: true,
    moduleGap: 120,
    routeChannelGap: 60,
    preferVerticalPeripheralPlacement: true,
  },
};

fs.writeFileSync(path.join(dir, 'layout.json'), JSON.stringify(spec, null, 1), 'utf8');
for (const m of modules) console.log(`${m.name.padEnd(5)} zone=${m.zone.padEnd(12)} core=${m.core.padEnd(4)} parts=${m.parts.length}: ${m.parts.join(',')}`);
console.log('wrote layout.json');
