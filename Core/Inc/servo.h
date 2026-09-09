#ifndef __SERVO_H
#define __SERVO_H

#include "main.h"

#define SERVO_ANGLE_MIN     0.0f
#define SERVO_ANGLE_MAX     180.0f
#define SERVO_PULSE_MIN_US  500u     /* 0°   → 0.5ms */
#define SERVO_PULSE_MAX_US  2500u    /* 180° → 2.5ms */
#define SERVO_PERIOD_US     20000u   /* 20ms = 50Hz */

void servo_attach(TIM_HandleTypeDef *htim, uint32_t channel);
void servo_set_angle(float angle);        /* 0~180 */
float servo_get_angle(void);              /* 指令角度（回显用） */
float servo_get_duty_percent(void);       /* 当前 PWM 占空比 % */

#endif /* __SERVO_H */
