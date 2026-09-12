// 生成 autoconnect 批量 spec（netlist.json），并对 page1.json 实测引脚表做三重校验：
//   1) 每个引用到的 "REF:PIN" 必须真实存在
//   2) 任何物理脚不得被接两次
//   3) 未被连接的脚必须显式列在 NC_PINS 里
// 用法: node gen-nets.mjs
import fs from 'node:fs';
import path from 'node:path';

const dir = path.dirname(new URL(import.meta.url).pathname).replace(/^\/([A-Za-z]:)/, '$1');
const read = (p) => JSON.parse(fs.readFileSync(path.join(dir, p), 'utf8').replace(/^\uFEFF/, ''));

const bom = read('bom.json');
const page = read('page1.json').result;

// ---- 实测引脚索引: { REF: { pinNumber: pinName } } ----
const actual = {};
for (const c of page.components) {
  if (c.componentType === 'sheet' || !c.designator) continue;
  actual[c.designator] = {};
  for (const p of c.pins || []) actual[c.designator][p.pinNumber] = p.pinName;
}

// ---- 网表定义（唯一权威来源）----
// kind: gnd | power | netport
const NETS = [
  // ===== 电源 =====
  { net: 'VBUS', kind: 'power', pins: ['J1:A4B9', 'J1:B4A9', 'D1:2'] },            // D1:2 = A(阳极)
  { net: '+5V', kind: 'power', pins: ['D1:1', 'C1:1', 'C2:1', 'U2:3', 'J3:1', 'J6:3'] }, // D1:1 = K(阴极)
  { net: '+3V3', kind: 'power', pins: [
    'U2:2', 'U2:4', 'C3:1', 'C4:1', 'C5:1', 'C6:1', 'C7:1', 'C8:1', 'C9:1', 'C10:1',
    'R4:1', 'R5:1', 'D3:1', 'J2:1', 'J5:1', 'J5:18', 'J6:1',
    'U3:3', 'C14:1', 'U1:1', 'U1:9', 'U1:24', 'U1:36', 'U1:48', 'JP1:2',
  ] },
  { net: 'GND', kind: 'gnd', pins: [
    'J1:A1B12', 'J1:B1A12', 'J1:8', 'J1:9', 'J1:10', 'J1:11',
    'R1:2', 'R2:2', 'C1:2', 'C2:2', 'C3:2', 'C4:2', 'C5:2', 'C6:2', 'C7:2', 'C8:2',
    'C9:2', 'C10:2', 'C11:2', 'C12:2', 'C13:2', 'C14:2',
    'U2:1', 'U3:2', 'U3:8', 'R3:2', 'SW1:2', 'D2:2',
    'U1:8', 'U1:23', 'U1:35', 'U1:47',
    'J2:4', 'J3:2', 'J4:3', 'J5:19', 'J5:20', 'J6:2', 'JP3:2',
  ] },

  // ===== USB-C CC（纯取电，各 5.1k 下拉）=====
  { net: 'USB_CC1', kind: 'netport', pins: ['J1:A5', 'R1:1'] },
  { net: 'USB_CC2', kind: 'netport', pins: ['J1:B5', 'R2:1'] },

  // ===== 时钟 HSE 8MHz =====
  { net: 'OSC_IN', kind: 'netport', pins: ['U1:5', 'Y1:1', 'C11:1'] },
  { net: 'OSC_OUT', kind: 'netport', pins: ['U1:6', 'Y1:2', 'C12:1'] },

  // ===== 复位 =====
  { net: 'NRST', kind: 'netport', pins: ['U1:7', 'R4:2', 'C13:1', 'SW1:1', 'J5:17'] },

  // ===== 启动配置 =====
  { net: 'BOOT0', kind: 'netport', pins: ['U1:44', 'R3:1', 'JP1:1'] },
  { net: 'PB2', kind: 'netport', pins: ['U1:20', 'JP3:1'] },

  // ===== 指示灯 =====
  { net: 'LED_PWR', kind: 'netport', pins: ['R5:2', 'D2:1'] },       // 3V3->R5->D2->GND
  { net: 'LED_USER', kind: 'netport', pins: ['D3:2', 'R6:1'] },      // 3V3->D3->R6->PC13（灌电流，蓝丸同款）

  // ===== 调试 SWD =====
  { net: 'PA13', kind: 'netport', pins: ['U1:34', 'J2:2'] },         // SWDIO
  { net: 'PA14', kind: 'netport', pins: ['U1:37', 'J2:3'] },         // SWCLK

  // ===== 串口 USART1 =====
  { net: 'PA9', kind: 'netport', pins: ['U1:30', 'J3:3', 'J6:15'] },  // USART1_TX
  { net: 'PA10', kind: 'netport', pins: ['U1:31', 'J3:4', 'J6:14'] }, // USART1_RX

  // ===== CAN（CAN1 重映射到 PB8/PB9）=====
  { net: 'PB9', kind: 'netport', pins: ['U1:46', 'U3:1', 'J6:4'] },   // CAN_TX -> 收发器 D(TXD)
  { net: 'PB8', kind: 'netport', pins: ['U1:45', 'U3:4', 'J6:5'] },   // CAN_RX <- 收发器 R(RXD)
  { net: 'CANH', kind: 'netport', pins: ['U3:7', 'JP2:1', 'J4:1'] },
  { net: 'CANL', kind: 'netport', pins: ['U3:6', 'R7:2', 'J4:2'] },
  { net: 'CAN_TERM', kind: 'netport', pins: ['JP2:2', 'R7:1'] },      // 跳线接通才投入 120R

  // ===== 左排针 J5（蓝丸 Header1）=====
  { net: 'PC13', kind: 'netport', pins: ['U1:2', 'J5:2', 'R6:2'] },   // 功能灯（灌电流）
  { net: 'PC14', kind: 'netport', pins: ['U1:3', 'J5:3'] },
  { net: 'PC15', kind: 'netport', pins: ['U1:4', 'J5:4'] },
  { net: 'PA0', kind: 'netport', pins: ['U1:10', 'J5:5'] },
  { net: 'PA1', kind: 'netport', pins: ['U1:11', 'J5:6'] },
  { net: 'PA2', kind: 'netport', pins: ['U1:12', 'J5:7'] },
  { net: 'PA3', kind: 'netport', pins: ['U1:13', 'J5:8'] },
  { net: 'PA4', kind: 'netport', pins: ['U1:14', 'J5:9'] },
  { net: 'PA5', kind: 'netport', pins: ['U1:15', 'J5:10'] },
  { net: 'PA6', kind: 'netport', pins: ['U1:16', 'J5:11'] },
  { net: 'PA7', kind: 'netport', pins: ['U1:17', 'J5:12'] },
  { net: 'PB0', kind: 'netport', pins: ['U1:18', 'J5:13'] },
  { net: 'PB1', kind: 'netport', pins: ['U1:19', 'J5:14'] },
  { net: 'PB10', kind: 'netport', pins: ['U1:21', 'J5:15'] },
  { net: 'PB11', kind: 'netport', pins: ['U1:22', 'J5:16'] },

  // ===== 右排针 J6（蓝丸 Header2）=====
  { net: 'PB7', kind: 'netport', pins: ['U1:43', 'J6:6'] },
  { net: 'PB6', kind: 'netport', pins: ['U1:42', 'J6:7'] },
  { net: 'PB5', kind: 'netport', pins: ['U1:41', 'J6:8'] },
  { net: 'PB4', kind: 'netport', pins: ['U1:40', 'J6:9'] },
  { net: 'PB3', kind: 'netport', pins: ['U1:39', 'J6:10'] },
  { net: 'PA15', kind: 'netport', pins: ['U1:38', 'J6:11'] },
  { net: 'PA12', kind: 'netport', pins: ['U1:33', 'J6:12'] },
  { net: 'PA11', kind: 'netport', pins: ['U1:32', 'J6:13'] },
  { net: 'PA8', kind: 'netport', pins: ['U1:29', 'J6:16'] },
  { net: 'PB15', kind: 'netport', pins: ['U1:28', 'J6:17'] },
  { net: 'PB14', kind: 'netport', pins: ['U1:27', 'J6:18'] },
  { net: 'PB13', kind: 'netport', pins: ['U1:26', 'J6:19'] },
  { net: 'PB12', kind: 'netport', pins: ['U1:25', 'J6:20'] },
];

// ---- 明确 NC（USB-C 数据脚本板只取电；SN65HVD230 Vref 不用时悬空）----
const NC_PINS = [
  'J1:A6', 'J1:B6',   // DP1/DP2
  'J1:A7', 'J1:B7',   // DN1/DN2
  'J1:A8', 'J1:B8',   // SBU1/SBU2
  'U3:5',             // Vref
];

// ---- 校验 ----
const problems = [];
const assigned = {};
const connections = [];

for (const n of NETS) {
  for (const ref of n.pins) {
    const [r, pin] = ref.split(':');
    if (!actual[r]) { problems.push(`unknown ref ${r} (net ${n.net})`); continue; }
    if (!(pin in actual[r])) { problems.push(`ref ${r} has no pin ${pin} (net ${n.net}); pins=${Object.keys(actual[r]).join(',')}`); continue; }
    const key = `${r}:${pin}`;
    if (assigned[key]) { problems.push(`pin ${key} assigned twice: ${assigned[key]} and ${n.net}`); continue; }
    assigned[key] = n.net;
    connections.push({ pin: key, kind: n.kind, net: n.net });
  }
}
for (const ref of NC_PINS) {
  const [r, pin] = ref.split(':');
  if (!actual[r] || !(pin in actual[r])) { problems.push(`NC pin ${ref} does not exist`); continue; }
  assigned[ref] = 'NC';
}

// 未被处理的物理脚
const unhandled = [];
for (const [r, pins] of Object.entries(actual)) {
  if (r === 'U1' && Object.keys(pins).length === 48) { /* ok */ }
  for (const pin of Object.keys(pins)) {
    if (!assigned[`${r}:${pin}`]) unhandled.push(`${r}:${pin}(${pins[pin]})`);
  }
}

console.log(`nets=${NETS.length} connections=${connections.length} nc=${NC_PINS.length}`);
if (problems.length) {
  console.log('--- PROBLEMS ---');
  for (const p of problems) console.log('  ' + p);
}
if (unhandled.length) {
  console.log(`--- UNHANDLED PINS (${unhandled.length}) ---`);
  console.log('  ' + unhandled.join(' '));
}
if (problems.length || unhandled.length) {
  console.log('NOT WRITING netlist.json until the above are resolved');
  process.exit(1);
}

fs.writeFileSync(path.join(dir, 'netlist.json'), JSON.stringify({ connections }, null, 1), 'utf8');
console.log('wrote netlist.json');
