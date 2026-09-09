/**
  ******************************************************************************
  * @file    servo.c
  * @brief   SG90/MG90S 舵机驱动（PWM 50Hz，0.5ms~2.5ms ↔ 0°~180°）
  *          教程：02_上位机调试_SG90_VOFA+_教程.md §5.1
  ******************************************************************************
  */
#include "servo.h"

static TIM_HandleTypeDef *servo_htim = NULL;
static uint32_t          servo_ch   = 0;

static float    cmd_angle = 90.0f;   /* 当前(指令)角度 */
static uint32_t pulse_us  = 1500u;   /* 当前脉宽(µs) */

void servo_set_angle(float angle)
{
    if (angle < SERVO_ANGLE_MIN) angle = SERVO_ANGLE_MIN;
    if (angle > SERVO_ANGLE_MAX) angle = SERVO_ANGLE_MAX;

    cmd_angle = angle;
    /* 0.5ms~2.5ms 线性映射到 0°~180° */
    pulse_us = SERVO_PULSE_MIN_US +
               (uint32_t)((angle / SERVO_ANGLE_MAX) *
                          (SERVO_PULSE_MAX_US - SERVO_PULSE_MIN_US) + 0.5f);

    /* 计数频率 1MHz：比较值(µs)直接等于脉宽 */
    __HAL_TIM_SET_COMPARE(servo_htim, servo_ch, pulse_us);
}

float servo_get_angle(void)
{
    return cmd_angle;
}

float servo_get_duty_percent(void)
{
    return (float)pulse_us / (float)SERVO_PERIOD_US * 100.0f;
}

void servo_attach(TIM_HandleTypeDef *htim, uint32_t channel)
{
    servo_htim = htim;
    servo_ch   = channel;

    /* 本工程 CubeMX 只生成了 TIM3 的基础计数配置(72MHz/71/19999)，
       并没有生成通道级 PWM 配置。这里补上与 CubeMX “Channel1 =
       PWM Generation CH1”完全一致的通道初始化，否则 PA6 不会真正
       输出 PWM（只定时器计数不使能输出比较通道，引脚无波形）。
       即使之后在 CubeMX 里补配 Channel1 并重新生成代码，该调用也
       只是重复写入相同寄存器，无副作用。 */
    TIM_OC_InitTypeDef sConfigOC = {0};
    sConfigOC.OCMode     = TIM_OCMODE_PWM1;
    sConfigOC.Pulse      = 0u;      /* 实际脉宽稍后由 servo_set_angle 写入 */
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(htim, &sConfigOC, channel) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_TIM_PWM_Start(htim, channel) != HAL_OK)
    {
        Error_Handler();
    }

    servo_set_angle(90.0f);   /* 上电回中，方便确认接线 */
}
