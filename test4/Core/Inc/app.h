/**
  ******************************************************************************
  * @file    app.h
  * @brief   第四题《模拟风扇》应用层：任务划分、参数、对外接口
  *
  *  任务划分（FreeRTOS / CMSIS-RTOS V2）：
  *    MotorCtrlTask  优先级 High          1ms   编码器→转速/角度，PID→PWM
  *    KnobTask       优先级 AboveNormal   20ms  读电位器 → 目标百分比（队列）
  *    VofaTask       优先级 Normal        20ms  JustFloat 上报 4 通道
  *    KeyTask        优先级 BelowNormal   20ms  按键切换 定速/定位
  *    StartDefaultTask(CubeMX 生成)       心跳灯 + 模式指示
  ******************************************************************************
  */
#ifndef __APP_H
#define __APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ---------------- 任务周期（ms） ---------------- */
#define CTRL_PERIOD_MS    1u
#define KNOB_PERIOD_MS    20u
#define VOFA_PERIOD_MS    20u
#define KEY_PERIOD_MS     20u

/* ---------------- 量程 ---------------- */
#define SPEED_MAX_RPM     180.0f        /* 旋钮拧到顶 = 该目标转速（实测满占空比约 185RPM，原 600 会导致前 30% 行程就饱和） */
#define ANGLE_MAX_DEG     360.0f        /* 定位模式旋钮拧到顶 = 该目标角度 */

/* ---------------- PID 初值（调参起点，配合 VOFA+ 整定） ----------------
 * 量纲（按这个来估，调参不会瞎试）：
 *   PID 输出 = 占空比千分比（-1000 ~ +1000），1000 = 100% 占空比
 *   定速环误差单位 = RPM ；定位环误差单位 = 度(°)
 *   Kp：误差 100RPM 时输出 = Kp × 100
 *   Ki：每秒积分增长 = Ki × 误差
 */
#define PID_SPEED_KP      15.0f     /* R4：Kp 5→10（R3 上升 600ms 偏慢，PWM 未饱和有余量） */
#define PID_SPEED_KI      8.0f      /* R3：Ki 4→8（R2 静差 -28.9，收敛太慢） */
#define PID_SPEED_KD      0.0f      /* 转速量化噪声大，先不加 D */
#define PID_POS_KP        6.0f      /* 误差 45° → 输出 270‰ */
#define PID_POS_KI        1.0f
#define PID_POS_KD        0.1f      /* 位置环的 D 相当于"速度阻尼"；度量是 度/秒，别给大 */

typedef enum
{
    CTRL_MODE_SPEED    = 0,             /* 定速：旋钮 → 目标转速(RPM) */
    CTRL_MODE_POSITION = 1              /* 定位：旋钮 → 目标角度(°) */
} ctrl_mode_t;

/* ---------------- 自动阶跃测试（无人值守调参 / 自动评价 PID 用） ----------------
 * APP_AUTO_STEP = 1 时忽略旋钮，目标值自动做 0 ↔ AUTO_STEP_LEVEL 的方波，
 * 这样就能用脚本连续采集阶跃响应、自动算超调/调节时间/稳态误差。
 * ★ 调参结束、录视频前务必改回 0（恢复旋钮控制）。
 */
#define APP_AUTO_STEP        0
#define AUTO_STEP_LEVEL      150.0f     /* 定速模式=目标RPM；定位模式=目标角度(°) */
#define AUTO_STEP_MS         3500u      /* 每段持续毫秒数（方波半周期） */

/* 上电默认工作模式（自动化测试定速/定位时不用按键切） */
#define APP_START_MODE       CTRL_MODE_SPEED

/* 在 MX_FREERTOS_Init() 的 USER CODE 区调用 */
void app_rtos_objects_init(void);       /* 创建队列/互斥锁 */
void app_rtos_tasks_create(void);       /* 创建 4 个应用任务 */

/* 在 main() 的 USER CODE 2 区调用（调度器启动之前） */
void app_peripherals_init(void);        /* 启动编码器 / PWM / ADC */

/* 任务体 */
void motor_ctrl_task(void *argument);
void knob_task(void *argument);
void vofa_task(void *argument);
void key_task(void *argument);
void led_task(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* __APP_H */
