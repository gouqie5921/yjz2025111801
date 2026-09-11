/**
  ******************************************************************************
  * @file    app.h
  * @brief   舵机执行端（从机）应用层：蓝牙接收 + 解析 + 驱动两个 SG90
  *
  *  任务划分（FreeRTOS，CMSIS-RTOS V2）：
  *   ParseTask  轮询  从 HC-05 读字节 -> 协议状态机解析 -> 完整帧投递到队列
  *   ServoTask  20ms  取队列里最新的目标角 -> 限速逼近 -> 写 TIM3 两路 CCR
  *   LinkTask  100ms  监视"多久没收到帧"，>200ms 判为失联（保持当前位置不动）
  *   LedTask    50ms  指示灯：慢闪=通信正常，快闪=失联
  *   DbgTask   250ms  串口2 打印状态（收到的帧数/校验错/角度）
  ******************************************************************************
  */
#ifndef __APP_H
#define __APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "main.h"

/* ---------------- 引脚定义 ----------------
   PC13 = 蓝丸板载 LED（低电平点亮） */
#define APP_LED_GPIO_Port   GPIOC
#define APP_LED_Pin         GPIO_PIN_13

/* ---------------- 任务参数 ---------------- */
#define APP_SERVO_PERIOD_MS     20u
#define APP_LED_PERIOD_MS       50u
#define APP_LINK_PERIOD_MS      100u
#define APP_DBG_PERIOD_MS       250u
#define APP_RX_POLL_MS          5u      /* 串口单字节轮询超时（ms） */
#define APP_LINK_TIMEOUT_MS     200u    /* 超过这么久没收到帧 = 失联 */
#define APP_DBG_ENABLE          1u      /* 0 = 关掉调试打印 */

/* ---------------- 任务栈（words） ---------------- */
#define APP_STACK_SERVO         128u
#define APP_STACK_PARSE         192u
#define APP_STACK_LINK          128u
#define APP_STACK_LED           128u
#define APP_STACK_DBG           192u

/* ---------------- 帧队列深度 ---------------- */
#define APP_CMD_QUEUE_LEN       4u

typedef struct
{
    uint8_t  link_ok;       /* 1 = 通信正常 */
    uint8_t  mode;          /* 主机当前的控制方式（0 电位器 / 1 陀螺仪） */
    uint32_t ok_cnt;        /* 收到的有效帧数 */
    uint32_t err_cnt;       /* 校验错/串口溢出次数 */
    uint32_t age_ms;        /* 距离最后一帧的毫秒数 */
    float    pan_cmd;       /* 收到的水平目标角 */
    float    tilt_cmd;      /* 收到的俯仰目标角 */
    float    pan_act;       /* 舵机实际水平角 */
    float    tilt_act;      /* 舵机实际俯仰角 */
} app_state_t;

/* RTOS 启动前调用：创建互斥锁/队列 */
uint8_t app_init(void);

/* RTOS 启动前调用：启动舵机 PWM */
void app_start_periph(void);

/* 在 MX_FREERTOS_Init() 里调用：创建应用任务 */
void app_create_tasks(void);

/* 复制一份当前状态（加锁） */
void app_get_state(app_state_t *st);

#ifdef __cplusplus
}
#endif

#endif /* __APP_H */
