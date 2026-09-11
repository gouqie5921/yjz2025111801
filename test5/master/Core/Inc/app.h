/**
  ******************************************************************************
  * @file    app.h
  * @brief   主控端（手柄/面包板端）应用层：采集 + 组帧 + 蓝牙发送
  *
  *  任务划分（FreeRTOS，CMSIS-RTOS V2）：
  *   SensorTask  10ms  读 MPU6050，姿态解算（陀螺仪模式的角度来源）
  *   AdcTask     20ms  读两个电位器（电位器模式的角度来源）
  *   SendTask    20ms  按当前模式选角度源 -> 组帧 -> 串口1 发给 HC-05
  *   KeyTask     20ms  按键切换 电位器 / 陀螺仪 两种控制方式
  *   LedTask     50ms  指示灯：慢闪=电位器模式，快闪=陀螺仪模式，超快闪=IMU 异常
  *   DbgTask    250ms  串口2 打印调试信息（标定陀螺轴向用）
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
   PA0  = 模式切换按键（内部上拉，按下为低）
   PC13 = 蓝丸板载 LED（低电平点亮）
   在 CubeMX 里这两个脚没有起名字，所以在这里直接定义，代码不依赖生成的标签。 */
#define APP_KEY_GPIO_Port   GPIOA
#define APP_KEY_Pin         GPIO_PIN_0
#define APP_LED_GPIO_Port   GPIOC
#define APP_LED_Pin         GPIO_PIN_13

/* ---------------- 任务周期（ms） ---------------- */
#define APP_SENSOR_PERIOD_MS    10u
#define APP_ADC_PERIOD_MS       20u
#define APP_SEND_PERIOD_MS      20u
#define APP_KEY_PERIOD_MS       20u
#define APP_LED_PERIOD_MS       50u
#define APP_DBG_PERIOD_MS       250u

/* ---------------- 任务栈（words，1 word = 4 字节） ---------------- */
#define APP_STACK_SENSOR        256u
#define APP_STACK_ADC           128u
#define APP_STACK_SEND          128u
#define APP_STACK_KEY           128u
#define APP_STACK_LED           128u
#define APP_STACK_DBG           192u

/* ---------------- 陀螺仪模式的角度映射 ---------------- */
/* 进入陀螺仪模式时以当时姿态为零位，云台回中，然后按下面增益跟随 */
#define APP_GYRO_CENTER         90.0f   /* 云台中位角度 */
#define APP_GYRO_PAN_GAIN       1.0f    /* 水平跟随比例 1:1 */
#define APP_GYRO_TILT_GAIN      1.0f    /* 俯仰跟随比例 1:1 */
#define APP_GYRO_LIMIT          88.0f   /* 相对中位最大偏转，防止撞限位 */

/* ---------------- 其他 ---------------- */
#define APP_KEY_DEBOUNCE_CNT    3u      /* 20ms * 3 = 60ms 消抖 */
#define APP_IMU_RETRY_MS        1000u   /* IMU 掉线后的重试间隔 */
#define APP_DBG_ENABLE          1u      /* 0 = 关掉调试打印 */

typedef struct
{
    uint8_t mode;           /* 0=电位器 1=陀螺仪 */
    uint8_t imu_ok;         /* MPU6050 是否正常 */
    float   pan;            /* 当前下发的水平角 */
    float   tilt;           /* 当前下发的俯仰角 */
    float   pot_pan;        /* 电位器角度 */
    float   pot_tilt;
    float   gyro_pan;       /* 陀螺仪角度 */
    float   gyro_tilt;
    float   yaw_raw;        /* 姿态解算原始输出 */
    float   pitch_raw;
} app_state_t;

/* RTOS 启动前调用：创建互斥锁、初始化状态。返回 0=成功 */
uint8_t app_init(void);

/* RTOS 启动前调用：启动 ADC 连续转换 */
void app_start_periph(void);

/* 在 MX_FREERTOS_Init() 里调用：创建应用任务 */
void app_create_tasks(void);

/* 复制一份当前状态（加锁），调试和上报用 */
void app_get_state(app_state_t *st);

/* 从任务里调用：按当前模式取角度（内部加锁） */
void app_get_cmd(float *pan, float *tilt, uint8_t *mode);

#ifdef __cplusplus
}
#endif

#endif /* __APP_H */
