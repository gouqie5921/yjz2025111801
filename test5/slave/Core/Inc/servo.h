/**
  ******************************************************************************
  * @file    servo.h
  * @brief   SG90 舵机驱动（TIM3 CH1=PA6 水平, CH2=PA7 俯仰）
  *
  *  SG90 是 50Hz PWM：0.5ms -> 0°、1.5ms -> 90°、2.5ms -> 180°。
  *  TIM3: PSC=71（72MHz/72 = 1MHz，1 个计数 = 1us），ARR=19999 -> 20ms 周期。
  *  所以 CCR 直接就是"高电平微秒数"，写 500~2500 即可。
  ******************************************************************************
  */
#ifndef __SERVO_H
#define __SERVO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define SERVO_CH_PAN        0u      /* 水平：TIM3_CH1 (PA6) */
#define SERVO_CH_TILT       1u      /* 俯仰：TIM3_CH2 (PA7) */
#define SERVO_CH_NUM        2u

#define SERVO_ANGLE_MIN     0.0f
#define SERVO_ANGLE_MAX     180.0f
#define SERVO_ANGLE_CENTER  90.0f

#define SERVO_PULSE_MIN_US  500u    /* 0°   */
#define SERVO_PULSE_MAX_US  2500u   /* 180° */

/* 转速限制（°/s）。SG90 空载约 0.1s/60°，即约 600°/s，
   这里限到 300°/s：既能跟上手上的陀螺仪动作，又不会让齿轮猛冲。 */
#define SERVO_SLEW_DPS      300.0f

/* 启动 PWM，两路先回到中位 */
void  servo_init(void);

/* 设定目标角度（会自动限幅到 0~180°） */
void  servo_set_target(uint8_t ch, float deg);

/* 周期调用（dt 单位秒）：按转速限制逼近目标，并写入 CCR */
void  servo_update(float dt);

/* 立即跳到目标（不做限速），上电初始化用 */
void  servo_jump(uint8_t ch, float deg);

float servo_get_angle(uint8_t ch);      /* 当前实际角度 */
float servo_get_target(uint8_t ch);     /* 当前目标角度 */

#ifdef __cplusplus
}
#endif

#endif /* __SERVO_H */
