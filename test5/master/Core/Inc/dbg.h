/**
  ******************************************************************************
  * @file    dbg.h
  * @brief   极简串口调试输出（不依赖 printf / newlib，避免重入问题）
  *
  *  FreeRTOSConfig.h 里关掉了 configUSE_NEWLIB_REENTRANT，printf 在多任务下
  *  不安全，所以这里自己写整数/小数转字符串，用 USART2 输出。
  *  所有输出都在同一个任务里调用，天然不会互相打断。
  ******************************************************************************
  */
#ifndef __DBG_H
#define __DBG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void dbg_str(const char *s);
void dbg_i32(int32_t v);
void dbg_u32(uint32_t v);
void dbg_f(float v, uint8_t decimals);      /* 定点小数，如 decimals=1 -> 12.3 */
void dbg_hex8(uint8_t v);                   /* 0x5A */
void dbg_nl(void);                          /* 换行 */

#ifdef __cplusplus
}
#endif

#endif /* __DBG_H */
