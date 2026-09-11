/**
  ******************************************************************************
  * @file    motor.h
  * @brief   TB6612FNG 电机驱动（TIM4_CH1 PWM + PB0/PB1 方向 + PB5 使能）
  *
  *  占空比用"千分比"表示（0 ~ 1000），内部按 TIM4 实际 ARR 换算成 CCR，
  *  所以即使以后改了 PWM 频率（ARR），PID 参数也不用重调。
  ******************************************************************************
  */
#ifndef __MOTOR_H
#define __MOTOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define MOTOR_DUTY_MAX  1000        /* 千分比满量程：1000 = 100% 占空比 */
#define MOTOR_DEADZONE  150         /* 电机启动死区（千分比，实测标定后修改） */

/* 初始化：STBY 使能、方向脚清零、启动 PWM，并记录 TIM4 的 ARR */
void motor_init(void);

/* 设置输出：duty 取 -1000 ~ +1000，正=正转、负=反转、0=停止(滑行) */
void motor_set(int16_t duty);

/* 停止（滑行） */
void motor_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* __MOTOR_H */
