/**
  ******************************************************************************
  * @file    hwprobe.h
  * @brief   主控端硬件自检（电位器引脚 + I2C 总线）
  *
  *  打开方式：编译时 -DAPP_HW_PROBE=1（不改正式代码，正常固件里 hwprobe_run()
  *  被展开成空语句）。
  *
  *  做两件事：
  *   1) 引脚探针：把 PA1/PA4 临时配成"输入+内部上拉"和"输入+内部下拉"各读一次，
  *      再恢复成模拟输入。由此判断引脚上到底有没有接低阻的东西：
  *        上拉读1 下拉读0 -> 引脚悬空（没接）
  *        上拉读0 下拉读0 -> 接到低阻的"低电平"（电位器滑臂在 GND 端 / 短到 GND）
  *        上拉读1 下拉读1 -> 接到低阻的"高电平"（滑臂在 3V3 端）
  *      同一招测 PB6/PB7：GY-521 板上有 4.7k 上拉，若模块在且供电，
  *      内部下拉也压不过它 -> 下拉读数仍为 1，可判定"模块在位且有电"。
  *   2) I2C 扫描：扫描 0x08~0x77 全部地址，结果位图存在 g_i2c_scan 里；
  *      另外单独确认 0x68 和 0x69（AD0 拉高时地址会变成 0x69）。
  *
  *  读结果：SWD 直接 dump 这些全局变量即可（见 flash/dump_live.cfg）。
  ******************************************************************************
  */
#ifndef __HWPROBE_H
#define __HWPROBE_H

#include <stdint.h>

#ifndef APP_HW_PROBE
#define APP_HW_PROBE    0u
#endif

#if (APP_HW_PROBE == 1u)

/* I2C 扫描结果：0x08~0x77 共 112 个地址，位图 14 字节 */
extern uint8_t  g_i2c_scan[14];
extern uint8_t  g_i2c_found;        /* 找到的器件个数 */
extern uint32_t g_i2c_err;          /* HAL_I2C_GetError() 的返回值 */
extern uint8_t  g_i2c_addr68_ok;    /* 0x68 是否应答 */
extern uint8_t  g_i2c_addr69_ok;    /* 0x69 是否应答（AD0 拉高时） */

/* 引脚探针结果：[0]=PA1(水平电位器) [1]=PA4(俯仰电位器) */
extern uint8_t  g_pot_pullup[2];
extern uint8_t  g_pot_pulldown[2];

/* 引脚探针结果：[0]=PB6(SCL) [1]=PB7(SDA) */
extern uint8_t  g_i2c_line_pullup[2];
extern uint8_t  g_i2c_line_pulldown[2];

/* MPU6050 寄存器级探测：确认"能不能读寄存器"以及"WHO_AM_I 到底是几" */
extern uint8_t  g_whoami_rd_ok;     /* WHO_AM_I 读取是否成功 */
extern uint8_t  g_whoami_val;       /* 读回的 WHO_AM_I 原始值 */
extern uint8_t  g_pwr_mgmt1;        /* 上电时 PWR_MGMT_1（0x40 = 还在睡眠） */
extern uint8_t  g_burst_ok;         /* 14 字节突发读是否成功 */
extern uint16_t g_accel_x;          /* 突发读回来的加速度 X 原始值 */
extern uint16_t g_gyro_x;           /* 突发读回来的角速度 X 原始值 */
extern uint32_t g_i2c_err_rd;       /* 寄存器读失败时 HAL_I2C_GetError() */

void hwprobe_run(void);

#else

#define hwprobe_run()   do { } while (0)

#endif /* APP_HW_PROBE */

#endif /* __HWPROBE_H */
