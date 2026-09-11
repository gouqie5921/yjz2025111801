# OpenOCD 烧录/调试使用说明（test4 · 第四题 模拟风扇 · STM32F103C8T6 + FreeRTOS）

## 背景
你的 **ST-Link 是克隆的、芯片也是克隆片**（SWD DPIDR `0x2ba01477`），ST 官方工具
（STM32CubeProgrammer / ST-LINK_gdbserver / **CubeIDE 的下载与调试**）会做正版校验而拒绝连接。
**OpenOCD 不做校验，烧录/调试都不受影响。** 所以：用 VS Code 编译，用本目录脚本烧录。

- OpenOCD 位置: `D:\123\xpack-openocd-0.12.0-7\bin\openocd.exe`
- GDB(现成): `C:\Users\31589\AppData\Local\stm32cube\bundles\gnu-tools-for-stm32\14.3.1+st.2\bin\arm-none-eabi-gdb.exe`

## 硬件接线（每次调试前确认）
| ST-Link | 板子 |
|---|---|
| SWDIO | PA13 |
| SWCLK | PA14 |
| GND | GND（共地） |
| 3.3V | 3V3 |

> 电机调试注意：`VM` 走 7.4V（降压模块），**不要**接到蓝丸 5V/3V3 上；
> TB6612 的 `GND` 要用粗线直接回降压模块；所有 GND（板、TB6612、编码器、电位器、USB-TTL、电源）连通。

## 编译（VS Code）
**必须用 Release**（或至少不要用纯 -O0 的 Debug）：
   
   本项目 `-O0` 编译时 HAL+FreeRTOS+应用 ≈ **99KB > 64KB Flash**，会链接失败。
   已在 `CMakeLists.txt` 里给 Debug 配置追加了 `-Os` 兜底，所以 Debug 现在也能编过，
   但仍建议直接用 **Release**。

- VS Code 里：`Ctrl+Shift+P` → `Tasks: Run Task` → **Configure+Build (Release)**
  （或直接 `Ctrl+Shift+B` 走 Build & Flash）
- 命令行等价写法：
  ```powershell
  $env:PATH = "C:\Users\31589\AppData\Local\stm32cube\bundles\gnu-tools-for-stm32\14.3.1+st.2\bin;" +
              "C:\Users\31589\AppData\Local\stm32cube\bundles\cmake\4.3.1+st.1\bin;" +
              "C:\Users\31589\AppData\Local\stm32cube\bundles\ninja\1.13.2+st.1\bin;" + $env:PATH
  cmake --preset Release
  cmake --build --preset Release        # 产出 build/Release/test4.elf
  ```
- 实测占用：**Flash 51492 B / 64KB（78.6%）**，**RAM 13640 B / 20KB（66.6%）**

## 烧录
- VSCode：`Ctrl+Shift+P` → `Tasks: Run Task` → **Flash (OpenOCD)**
- 或终端：
  ```powershell
  powershell -ExecutionPolicy Bypass -File D:\Projecttest\test4\test4\flash\flash.ps1 -OpenOcd "D:\123\xpack-openocd-0.12.0-7\bin\openocd.exe"
  ```
  脚本会自动找 `build/Release/test4.elf`（找不到就用 `build/Debug/test4.elf`）。
- 看到 `** Programming Finished **` + `** Verified OK **` 即成功，板子自动复位运行。

## 验证（串口 115200-8-N-1 + VOFA+）
- 串口助手（文本）应看到按键切换模式时的 `[mode] SPEED (target RPM)` / `[mode] POSITION (target deg)` 提示。
- **VOFA+**：数据格式选 **JustFloat**，波特率 115200，即可看到 4 条曲线：
  `通道0=目标  通道1=实际  通道2=PWM  通道3=误差`
- 板载 LED：**闪 1 次 = 定速模式**，**闪 2 次 = 定位模式**（心跳 + 状态指示）。

## 需要单步/断点调试时
```powershell
powershell -ExecutionPolicy Bypass -File D:\Projecttest\test4\test4\flash\debug.ps1 -OpenOcd "D:\123\xpack-openocd-0.12.0-7\bin\openocd.exe"
```
常用 gdb 命令：`b motor_ctrl_task`、`c`、`p s_mode`、`n`/`s`、`quit`。

## 常见问题
- **连不上**：先跑 `probe.ps1`；检查 SWD 四根线、板子供电、ST-Link 是否被别的软件占用。
- **链接报 region FLASH overflowed**：你在编 Debug（-O0）→ 用 Release，或确认 `CMakeLists.txt` 里
  那行 `add_compile_options($<$<CONFIG:Debug>:-Os>)` 还在。
- **电机不转 / 只嗡嗡响**：`STBY` 没拉高、`AIN1=AIn2=1`（刹车）、`VM` 没电、死区未补偿。
- **转速读数乱跳**：编码器 `VCC` 就近加 0.1µF、编码器线与电机线分开走、`VM` 旁加 100µF。
- **一按就跑飞/复位**：电机电流污染地线 → TB6612 `GND` 必须粗线直回降压模块。
- **OpenOCD 说 port 3333/6666 被占**：关掉残留的 openocd 窗口再试。
