/**
  ******************************************************************************
  * @file    can.h
  * @brief   通用 CAN 驱动（ST 标准库风格的模块化封装，业务无关，可移植）
  *
  * 本文件只暴露与"总线通信"本身相关的接口，不掺任何具体业务逻辑。
  * 换到其它工程只需改引脚宏（见 can.c 顶部 CAN_*_PIN），上层调用不变。
  ******************************************************************************
  */
#ifndef __CAN_H
#define __CAN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"
#include <stdint.h>

/* ============================================================================
 *  CAN 工作模式开关（只改这一行）
 *      双机通信（默认）: CAN_MODE_NORMAL
 *      单板回环自测    : CAN_MODE_LOOPBACK   ← 一块板就能自发自收，不需要第二个节点
 * ========================================================================== */
#define CAN_MODE_SEL    CAN_MODE_NORMAL

/* ============================ 报文结构体 ============================ */
typedef struct
{
    uint32_t id;      /* 标识符：标准帧 11 位 / 扩展帧 29 位           */
    uint8_t  ide;     /* 帧类型：0=标准帧(CAN_ID_STD) 1=扩展帧(CAN_ID_EXT) */
    uint8_t  rtr;     /* 帧格式：0=数据帧(CAN_RTR_DATA) 1=远程帧(CAN_RTR_REMOTE) */
    uint8_t  dlc;     /* 数据长度 0~8 */
    uint8_t  data[8]; /* 数据段 */
} can_msg_t;

/* ============================ 通用接口 ============================ */

/* 初始化 CAN（波特率 125k/250k/500k/1M，内部算位时序）。返回 0=成功 */
uint8_t can_init(uint32_t baudrate);

/* 发送标准帧：std_id 为 11 位 ID，data 指向数据，len 为数据长度(≤8)。
 * 内部等待邮箱空并按需等待发送完成。返回 0=成功 1=失败(邮箱忙/总线路) */
uint8_t can_send(uint16_t std_id, const uint8_t *data, uint8_t len);

/* 非阻塞取一帧（从 ISR 填充的环形队列里取）。返回 0=取到 1=队列空 */
uint8_t can_recv(can_msg_t *msg);

/* 配置硬件筛选器（掩码模式，32 位），只放行 (id & mask)==(std_id & mask) 的帧。
 * mask=0 表示全收。返回 0=成功 */
uint8_t can_set_filter(uint16_t std_id, uint16_t mask);

/* 注册"收到帧"回调：在 CAN 接收中断上下文被调用，注意回调里不要做耗时/阻塞操作 */
void can_register_rx_callback(void (*cb)(const can_msg_t *msg));

/* 轮询接收：在主循环里调用即可，把 FIFO0 里的帧全部取空并派发（与中断方式可并用，不会重复派发） */
void can_poll(void);

/* 状态与异常恢复 */
uint8_t can_is_busoff(void);          /* 是否进入总线关闭状态（1=是） */
void    can_recover(void);            /* 退出 Bus-Off：复位错误计数并重新启动 */

/* 供中断服务函数在 stm32f1xx_it.c 中调用 */
extern CAN_HandleTypeDef hcan1;
void can_irq_handler(void);           /* 内部= HAL_CAN_IRQHandler(&hcan1) */

#ifdef __cplusplus
}
#endif

#endif /* __CAN_H */
