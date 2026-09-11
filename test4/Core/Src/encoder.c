/**
  ******************************************************************************
  * @file    encoder.c
  * @brief   编码器读取（TIM3 编码器模式 TI1+TI2 四倍频）
  ******************************************************************************
  */
#include "encoder.h"
#include "tim.h"

static int32_t s_total = 0;

void encoder_init(void)
{
    __HAL_TIM_SET_COUNTER(&htim3, 0);
    HAL_TIM_Encoder_Start(&htim3, TIM_CHANNEL_ALL);
    s_total = 0;
}

int16_t encoder_read_delta(void)
{
    int16_t cnt;

    cnt = (int16_t)__HAL_TIM_GET_COUNTER(&htim3);
    __HAL_TIM_SET_COUNTER(&htim3, 0);       /* 读完清零，下次读到的就是这一周期的增量 */

#if (ENCODER_SIGN < 0)
    cnt = (int16_t)(-cnt);                  /* 方向修正：等价于对调编码器 A/B */
#endif

    s_total += (int32_t)cnt;
    return cnt;
}

int32_t encoder_get_total(void)
{
    return s_total;
}

void encoder_reset_total(void)
{
    s_total = 0;
    __HAL_TIM_SET_COUNTER(&htim3, 0);
}

float encoder_total_to_deg(int32_t counts)
{
    return ((float)counts * 360.0f) / (float)ENCODER_CPR;
}
