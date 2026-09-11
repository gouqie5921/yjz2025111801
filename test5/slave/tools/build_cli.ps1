# ============================================================================
#  build_cli.ps1 - build test5/slave WITHOUT ninja/cmake.
#
#  Why: in this sandbox ninja hangs when it spawns the compiler with piped
#  stdio, so we invoke arm-none-eabi-gcc directly (that works).
#  Flags/source list mirror the CMake "Release" preset, so the output is
#  equivalent. Result: build\cli\slave.elf
#
#  Usage:
#    powershell -ExecutionPolicy Bypass -File .\tools\build_cli.ps1
#
#  NOTE: keep this file pure ASCII (PS 5.1 reads .ps1 as ANSI/GBK without BOM).
# ============================================================================
param()

$ErrorActionPreference = "Stop"
$root  = Split-Path $PSScriptRoot -Parent
$gcc   = "C:\Users\31589\AppData\Local\stm32cube\bundles\gnu-tools-for-stm32\14.3.1+st.2\bin\arm-none-eabi-gcc.exe"
$objc  = "C:\Users\31589\AppData\Local\stm32cube\bundles\gnu-tools-for-stm32\14.3.1+st.2\bin\arm-none-eabi-objcopy.exe"
$outdir = Join-Path $root "build\cli"
$out    = Join-Path $outdir "slave.elf"
if (-not (Test-Path $outdir)) { New-Item -ItemType Directory -Path $outdir | Out-Null }

$inc = @(
  "-I", (Join-Path $root "Core\Inc"),
  "-I", (Join-Path $root "Middlewares\Third_Party\FreeRTOS\Source\include"),
  "-I", (Join-Path $root "Middlewares\Third_Party\FreeRTOS\Source\CMSIS_RTOS_V2"),
  "-I", (Join-Path $root "Middlewares\Third_Party\FreeRTOS\Source\portable\GCC\ARM_CM3"),
  "-I", (Join-Path $root "Drivers\STM32F1xx_HAL_Driver\Inc"),
  "-I", (Join-Path $root "Drivers\STM32F1xx_HAL_Driver\Inc\Legacy"),
  "-I", (Join-Path $root "Drivers\CMSIS\Device\ST\STM32F1xx\Include"),
  "-I", (Join-Path $root "Drivers\CMSIS\Include")
)

$appNames = @("main","gpio","freertos","tim","usart",
              "stm32f1xx_it","stm32f1xx_hal_msp","stm32f1xx_hal_timebase_tim",
              "sysmem","syscalls")
$halNames = @("stm32f1xx_hal_gpio_ex","stm32f1xx_hal_tim","stm32f1xx_hal_tim_ex",
              "stm32f1xx_hal","stm32f1xx_hal_rcc","stm32f1xx_hal_rcc_ex",
              "stm32f1xx_hal_gpio","stm32f1xx_hal_dma","stm32f1xx_hal_cortex",
              "stm32f1xx_hal_pwr","stm32f1xx_hal_flash","stm32f1xx_hal_flash_ex",
              "stm32f1xx_hal_exti","stm32f1xx_hal_uart")
$osNames  = @("croutine","event_groups","list","queue","stream_buffer","tasks","timers")
$myNames  = @("app","servo","protocol","dbg")

$srcs = @()
$srcs += ($appNames  | ForEach-Object { Join-Path $root ("Core\Src\" + $_ + ".c") })
$srcs += ($halNames  | ForEach-Object { Join-Path $root ("Drivers\STM32F1xx_HAL_Driver\Src\" + $_ + ".c") })
$srcs += ($osNames   | ForEach-Object { Join-Path $root ("Middlewares\Third_Party\FreeRTOS\Source\" + $_ + ".c") })
$srcs += ($myNames   | ForEach-Object { Join-Path $root ("Core\Src\" + $_ + ".c") })
$srcs += @(
  (Join-Path $root "Core\Src\system_stm32f1xx.c"),
  (Join-Path $root "Middlewares\Third_Party\FreeRTOS\Source\CMSIS_RTOS_V2\cmsis_os2.c"),
  (Join-Path $root "Middlewares\Third_Party\FreeRTOS\Source\portable\MemMang\heap_4.c"),
  (Join-Path $root "Middlewares\Third_Party\FreeRTOS\Source\portable\GCC\ARM_CM3\port.c"),
  (Join-Path $root "startup_stm32f103xb.s")
)

$flags = @(
  "-mcpu=cortex-m3","-std=gnu11","-Os","-g0",
  "-Wall","-fdata-sections","-ffunction-sections",
  "-DUSE_HAL_DRIVER","-DSTM32F103xB","-DNDEBUG",
  "-T", (Join-Path $root "STM32F103xx_FLASH.ld"),
  "--specs=nano.specs",
  "-Wl,--gc-sections",
  ("-Wl,-Map=" + (Join-Path $outdir "slave.map")),
  "-Wl,--print-memory-usage"
)

Write-Host ("Compiling+linking " + $srcs.Count + " files ...") -ForegroundColor Cyan
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$log = & $gcc @flags @inc @srcs -o $out -lm 2>&1
$rc = $LASTEXITCODE
$sw.Stop()
$log | Where-Object { $_ -match "error|warning:|RAM:|FLASH:" } | Select-Object -First 30
Write-Host ("build exit=" + $rc + "   elapsed=" + [math]::Round($sw.Elapsed.TotalSeconds,1) + " s") -ForegroundColor Cyan
if ($rc -ne 0) { exit $rc }

# also emit bin/hex for convenience
& $objc -O binary $out (Join-Path $outdir "slave.bin")
Write-Host ("OK -> " + $out) -ForegroundColor Green
