# OpenOCD 烧录/调试使用说明（test3 · 第三题 CAN 通信 · STM32F103C8T6）

## 背景
你的 **ST-Link 是克隆的、芯片也是克隆片**（SWD DPIDR `0x2ba01477`），ST 官方工具
（STM32CubeProgrammer / ST-LINK_gdbserver / CubeIDE 调试）会做正版校验而拒绝连接。
**OpenOCD 不做校验，烧录/调试都不受影响。**

- OpenOCD 位置: `D:\123\xpack-openocd-0.12.0-7\bin\openocd.exe`
- GDB(现成): `C:\Users\31589\AppData\Local\stm32cube\bundles\gnu-tools-for-stm32\14.3.1+st.2\bin\arm-none-eabi-gdb.exe`

## 硬件接线（每次调试前确认）
| ST-Link | 板子 |
|---|---|
| SWDIO | PA13 |
| SWCLK | PA14 |
| GND | GND（共地） |
| 3.3V | 3V3 |

> CAN 调试时注意：两块板、两个 CAN 模块都要**共地**；主机 PA0 短接 GND 当按键；
> 从机观察 PC13 板载 LED。板上 5V 正常供电即可，别靠 ST-Link 的 3.3V 带模块。

## 编译（两种角色）
角色在源码里一行切换，**用 VS Code 的编译按钮就行**（和编 test2 一样）：

- 打开 `Core/Src/main.c`，找到顶部：
  ```c
  #define NODE_ROLE  NODE_ROLE_MASTER    /* 主机 */
  ```
- 从机把它改成：
  ```c
  #define NODE_ROLE  NODE_ROLE_SLAVE     /* 从机 */
  ```
- 然后编译（`Ctrl+Shift+B` 的 Build，或 STM32Cube 的编译按钮），产出 `build/Debug/test3.elf`。
- 流程：保持 `MASTER` 编译 → 烧板A；改成 `SLAVE` 编译 → 烧板B。

> 编译是纯本地的，**与正版校验无关，可以正常用**。

## 烧录（三种方式任选）
- VSCode 里：`Ctrl+Shift+P` → `Tasks: Run Task` → **Flash (OpenOCD)**
- 或终端一条命令：
  ```powershell
  powershell -ExecutionPolicy Bypass -File D:\Projecttest\test3\flash\flash.ps1 -OpenOcd "D:\123\xpack-openocd-0.12.0-7\bin\openocd.exe"
  ```
- 看到 `** Programming Finished **` + `** Verified OK **` 即成功，板子已自动复位运行。
- 先烧主机，再把角色改成 `SLAVE` 重编，烧从机。

## 验证（串口助手 115200-8-N-1）
烧完把每块板的 PA9/PA10 接 USB-TTL（或板载 CH340），开串口助手：
- 主机复位应打印：`[role] MASTER`、`[can ] 500kbps, ID 0x111->0x222`、`[key ] touch PA0 to GND to send 0x11`
- 从机复位应打印：`[role] SLAVE`、`[slv ] waits for 0x11, reply 0x22 + toggle LED`
- 主机按 PA0(对GND短接) → 主机 `[M] CAN TX: 0x11` → 从机 `[S] CAN RX: 0x11`+`[S] CAN TX: 0x22`+LED 翻转 → 主机 `[M] CAN RX: 0x22`。

## 需要单步/断点调试时
```powershell
powershell -ExecutionPolicy Bypass -File D:\Projecttest\test3\flash\debug.ps1 -OpenOcd "D:\123\xpack-openocd-0.12.0-7\bin\openocd.exe"
```
脚本会自动：起 OpenOCD gdb server → 连 gdb → 复位 halt + load 程序 → 停在 gdb 提示符。
常用命令：`b main`、`b can_send`、`c`、`p g_rx.data[0]`、`n`/`s`、`quit`。

## 常见问题
- **连不上**：先跑 `probe.ps1`；检查 SWD 四根线、板子供电、ST-Link 是否被其它软件占用。
- **收不到数据**：查 CANH/CANL 是否接反、两个 120Ω 是否都在、两板波特率是否一致、模块 VCC 是否 5V（见工程教程 §6 排查表）。
- **OpenOCD 说 port 3333/6666 被占**：关掉之前残留的 openocd 窗口再试。

## 验收录像（必须用 ST 官方工具时）
ST 工具认不了克隆件，**唯一办法是换正版 ST-Link**（借官方 Nucleo 板的板载 ST-Link 最省钱，
或买正版 ST-Link V2）。换正版仿真器后：
STM32CubeProgrammer → ST-LINK → Mode 选 **Under reset** → 烧 `build/Debug/test3.elf` → 录屏。
开发期间请继续用本目录的 OpenOCD 流程，别动 ST 的调试按钮。
