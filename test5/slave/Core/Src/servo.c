/**
  ******************************************************************************
  * @file    servo.c
  * @brief   SG90 舵机驱动实现
  ******************************************************************************
  */
#include "servo.h"
#include "tim.h"
#include "main.h"

typedef struct
{
    uint32_t channel;
    float    angle;         /* 当前角度 */
    float    target;        /* 目标角度 */
} servo_ch_t;

static servo_ch_t s_ch[SERVO_CH_NUM] = {
    { TIM_CHANNEL_1, SERVO_ANGLE_CENTER, SERVO_ANGLE_CENTER },
    { TIM_CHANNEL_2, SERVO_ANGLE_CENTER, SERVO_ANGLE_CENTER },
};

static float clampf(float v, float lo, float hi)
{
    if (v < lo) { return lo; }
    if (v > hi) { return hi; }
    return v;
}

/* 角度 -> 比较值（= 高电平微秒数） */
static uint32_t angle_to_ccr(float deg)
{
    float span = (float)(SERVO_PULSE_MAX_US - SERVO_PULSE_MIN_US);
    float us   = (float)SERVO_PULSE_MIN_US
               + (deg - SERVO_ANGLE_MIN) * span / (SERVO_ANGLE_MAX - SERVO_ANGLE_MIN);

    return (uint32_t)(us + 0.5f);
}

void servo_init(void)
{
    uint8_t i;

    /* 先给中位，再开 PWM，避免上电瞬间舵机乱甩 */
    for (i = 0u; i < SERVO_CH_NUM; i++)
    {
        __HAL_TIM_SET_COMPARE(&htim3, s_ch[i].channel,
                              angle_to_ccr(SERVO_ANGLE_CENTER));
    }

    (void)HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
    (void)HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);

    for (i = 0u; i < SERVO_CH_NUM; i++)
    {
        s_ch[i].angle  = SERVO_ANGLE_CENTER;
        s_ch[i].target = SERVO_ANGLE_CENTER;
    }
}

void servo_set_target(uint8_t ch, float deg)
{
    if (ch >= SERVO_CH_NUM)
    {
        return;
    }
    s_ch[ch].target = clampf(deg, SERVO_ANGLE_MIN, SERVO_ANGLE_MAX);
}

void servo_jump(uint8_t ch, float deg)
{
    if (ch >= SERVO_CH_NUM)
    {
        return;
    }
    s_ch[ch].target = clampf(deg, SERVO_ANGLE_MIN, SERVO_ANGLE_MAX);
    s_ch[ch].angle  = s_ch[ch].target;
    __HAL_TIM_SET_COMPARE(&htim3, s_ch[ch].channel, angle_to_ccr(s_ch[ch].angle));
}

void servo_update(float dt)
{
    uint8_t i;
    float   step;

    if (dt <= 0.0f)
    {
        return;
    }

    step = SERVO_SLEW_DPS * dt;     /* 本周期最多走多少度 */

    for (i = 0u; i < SERVO_CH_NUM; i++)
    {
        float d = s_ch[i].target - s_ch[i].angle;

        if (d > step)
        {
            s_ch[i].angle += step;
        }
        else if (d < -step)
        {
            s_ch[i].angle -= step;
        }
        else
        {
            s_ch[i].angle = s_ch[i].target;
        }

        __HAL_TIM_SET_COMPARE(&htim3, s_ch[i].channel, angle_to_ccr(s_ch[i].angle));
    }
}

float servo_get_angle(uint8_t ch)
{
    return (ch < SERVO_CH_NUM) ? s_ch[ch].angle : 0.0f;
}

float servo_get_target(uint8_t ch)
{
    return (ch < SERVO_CH_NUM) ? s_ch[ch].target : 0.0f;
}
