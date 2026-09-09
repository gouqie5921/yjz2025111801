# OpenOCD 烧录/调试使用说明（test2 · STM32F103C8T6 + SG90 舵机）

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

> 舵机调试时：板子正常供电(USB/5V)，舵机独立 5V 并共地，别靠 ST-Link 的 3.3V 带舵机。

## 日常流程（三步）

### ① 改代码 + 编译
照你原来的做法：改 `Core/Src` 下代码 → 用 STM32Cube 扩展的**编译按钮**生成最新
`build/Debug/test2.elf`。编译是纯本地的，**与正版校验无关，可以正常用**。

### ② 烧录（三种方式任选）
- VSCode 里：`Ctrl+Shift+P` → `Tasks: Run Task` → **Flash (OpenOCD)**
- 或终端一条命令：
  ```powershell
  powershell -ExecutionPolicy Bypass -File D:\Projecttest\test2\flash\flash.ps1 -OpenOcd "D:\123\xpack-openocd-0.12.0-7\bin\openocd.exe"
  ```
- 看到 `** Programming Finished **` + `** Verified OK **` 即成功，板子已自动复位运行。

### ③ 验证/调舵机（串口）
烧完后把 USB 数据线接板载 CH340，开 **VOFA+**（COM / 115200）：
- 复位应看到一行 `servo ready, send #000~#180`（其后是 JustFloat 二进制乱码，正常）
- 发 `#000` `#090` `#180` 看舵机转，Right 协议选 JustFloat 画角度/占空比波形。

## 需要单步/断点调试时
```powershell
powershell -ExecutionPolicy Bypass -File D:\Projecttest\test2\flash\debug.ps1 -OpenOcd "D:\123\xpack-openocd-0.12.0-7\bin\openocd.exe"
```
脚本会自动：起 OpenOCD gdb server → 连 gdb → 复位 halt + load 程序 → 停在 gdb 提示符。
常用命令：`b servo_set_angle`、`c`、`p cmd_angle`、`n`/`s`、`quit`。

## 常见问题
- **连不上**：先跑 `probe.ps1`；检查 SWD 四根线、板子供电、ST-Link 是否被其它软件占用。
- **OpenOCD 说 port 3333/6666 被占**：关掉之前残留的 openocd 窗口再试。
- **烧录后舵机仍不动**：问题在接线/供电/舵机本体，与烧录无关（回到工程教程 §7 排查表）。

## 验收录像（必须用 ST 官方工具时）
ST 工具认不了克隆件，**唯一办法是换正版 ST-Link**（借官方 Nucleo 板的板载 ST-Link 最省钱，
或买正版 ST-Link V2）。换正版仿真器后：
STM32CubeProgrammer → ST-LINK → Mode 选 **Under reset** → 烧 `build/Debug/test2.elf` → 录屏。
开发期间请继续用本目录的 OpenOCD 流程，别动 ST 的调试按钮。
