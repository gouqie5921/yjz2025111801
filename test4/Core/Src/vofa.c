/**
  ******************************************************************************
  * @file    vofa.c
  * @brief   VOFA+ 输出实现（互斥锁保护，避免多任务串口数据交叉）
  *
  *  注意：本工程未开启 USE_NEWLIB_REENTRANT，所以这里只用 HAL_UART_Transmit
  *        + 静态缓冲区 + memcpy，不使用 printf（不依赖 newlib 全局状态）。
  ******************************************************************************
  */
#include "vofa.h"
#include "usart.h"
#include "cmsis_os.h"
#include <string.h>

static osMutexId_t s_mtx_uart = NULL;

void vofa_init(void)
{
    if (s_mtx_uart == NULL)
    {
        s_mtx_uart = osMutexNew(NULL);
    }
}

void vofa_send_justfloat(float c0, float c1, float c2, float c3, float c4)
{
    uint8_t       buf[24];
    const uint8_t tail[4] = {0x00u, 0x00u, 0x80u, 0x7Fu};

    memcpy(&buf[0],  &c0, 4u);
    memcpy(&buf[4],  &c1, 4u);
    memcpy(&buf[8],  &c2, 4u);
    memcpy(&buf[12], &c3, 4u);
    memcpy(&buf[16], &c4, 4u);
    memcpy(&buf[20], tail, 4u);

    if (s_mtx_uart != NULL)
    {
        (void)osMutexAcquire(s_mtx_uart, osWaitForever);
    }
    (void)HAL_UART_Transmit(&huart1, buf, (uint16_t)sizeof(buf), 50u);
    if (s_mtx_uart != NULL)
    {
        (void)osMutexRelease(s_mtx_uart);
    }
}

void vofa_send_text(const char *s)
{
    if (s == NULL)
    {
        return;
    }

    if (s_mtx_uart != NULL)
    {
        (void)osMutexAcquire(s_mtx_uart, osWaitForever);
    }
    (void)HAL_UART_Transmit(&huart1, (uint8_t *)s, (uint16_t)strlen(s), 100u);
    if (s_mtx_uart != NULL)
    {
        (void)osMutexRelease(s_mtx_uart);
    }
}
