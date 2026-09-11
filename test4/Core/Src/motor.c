/**
  ******************************************************************************
  * @file    motor.c
  * @brief   TB6612FNG 驱动实现
  *
  *   真值表：STBY=1 时——
  *     AIN1=1, AIN2=0, PWM=占空比  → 正转
  *     AIN1=0, AIN2=1, PWM=占空比  → 反转
  *     AIN1=0, AIN2=0              → 停止（滑行）
  ******************************************************************************
  */
#include "motor.h"
#include "tim.h"
#include "gpio.h"

/* TB6612 控制脚（对应 CubeMX 里 PB0/PB1/PB5） */
#define AIN1_PORT   GPIOB
#define AIN1_PIN    GPIO_PIN_0
#define AIN2_PORT   GPIOB
#define AIN2_PIN    GPIO_PIN_1
#define STBY_PORT   GPIOB
#define STBY_PIN    GPIO_PIN_5

static uint16_t s_arr = 3599u;      /* TIM4 自动重装值，motor_init 里读取 */

/* 千分比 → CCR（按实际 ARR 换算） */
static void pwm_write_permille(uint16_t permille)
{
    uint32_t ccr;

    if (permille > MOTOR_DUTY_MAX)
    {
        permille = MOTOR_DUTY_MAX;
    }
    ccr = ((uint32_t)permille * (uint32_t)s_arr) / (uint32_t)MOTOR_DUTY_MAX;
    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_1, (uint16_t)ccr);
}

void motor_init(void)
{
    s_arr = (uint16_t)__HAL_TIM_GET_AUTORELOAD(&htim4);
    if (s_arr == 0u)
    {
        s_arr = 3599u;
    }

    /* 先停住，再使能芯片，避免上电乱转 */
    pwm_write_permille(0u);
    HAL_GPIO_WritePin(AIN1_PORT, AIN1_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(AIN2_PORT, AIN2_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(STBY_PORT, STBY_PIN, GPIO_PIN_SET);       /* STBY = 1 使能 */

    (void)HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_1);
}

void motor_set(int16_t duty)
{
    uint16_t mag;

    if (duty > (int16_t)MOTOR_DUTY_MAX)
    {
        duty = (int16_t)MOTOR_DUTY_MAX;
    }
    if (duty < -(int16_t)MOTOR_DUTY_MAX)
    {
        duty = -(int16_t)MOTOR_DUTY_MAX;
    }

    if (duty > 0)
    {
        HAL_GPIO_WritePin(AIN1_PORT, AIN1_PIN, GPIO_PIN_SET);
        HAL_GPIO_WritePin(AIN2_PORT, AIN2_PIN, GPIO_PIN_RESET);
        mag = (uint16_t)duty;
    }
    else if (duty < 0)
    {
        HAL_GPIO_WritePin(AIN1_PORT, AIN1_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(AIN2_PORT, AIN2_PIN, GPIO_PIN_SET);
        mag = (uint16_t)(-duty);
    }
    else
    {
        HAL_GPIO_WritePin(AIN1_PORT, AIN1_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(AIN2_PORT, AIN2_PIN, GPIO_PIN_RESET);
        mag = 0u;
    }

    pwm_write_permille(mag);
}

void motor_stop(void)
{
    pwm_write_permille(0u);
    HAL_GPIO_WritePin(AIN1_PORT, AIN1_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(AIN2_PORT, AIN2_PIN, GPIO_PIN_RESET);
}
