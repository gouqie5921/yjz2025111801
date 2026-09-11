/**
  ******************************************************************************
  * @file    knob.c
  * @brief   电位器采集：ADC1(PA1) 水平、ADC2(PA4) 俯仰
  *
  *  两个 ADC 各自单通道、连续转换模式，不需要 DMA，也不需要扫描：
  *  任务里直接 HAL_ADC_GetValue() 取最近一次结果即可，简单且不会互相抢总线。
  *  滤波：16 点滑动平均 + 一阶低通，手抖和 ADC 抖动同时压掉。
  ******************************************************************************
  */
#include "knob.h"
#include "adc.h"
#include "main.h"

#define ADC_FULL_SCALE      4095.0f
/* 机械死区：电位器转到两端时 ADC 不会真正到 0/4095，映射后仍然给出整段行程 */
#define ADC_TRIM_LOW        40u
#define ADC_TRIM_HIGH       40u

#define KNOB_LP_ALPHA       0.25f   /* 一阶低通系数，越小越平滑 */

static knob_t s_pan  = { 0u, 4095u, 0u, 90.0f };
static knob_t s_tilt = { 0u, 4095u, 0u, 90.0f };

static uint8_t read_channel(ADC_HandleTypeDef *hadc, uint16_t *out)
{
    uint32_t v;
    uint32_t t = 0u;

    /* 连续转换模式下 EOC 一直在置位，等的是"新一轮转换完成" */
    while (__HAL_ADC_GET_FLAG(hadc, ADC_FLAG_EOC) == 0u)
    {
        if (t++ > 20000u)           /* 超时保护，别把任务卡死 */
        {
            return 1u;
        }
    }

    v = HAL_ADC_GetValue(hadc);     /* 读 DR 的同时自动清 EOC */
    *out = (uint16_t)v;
    return 0u;
}

void knob_start(void)
{
    /* 单通道连续转换，启动一次就一直跑 */
    (void)HAL_ADC_Start(&hadc1);
    (void)HAL_ADC_Start(&hadc2);

    /* 丢掉前几次转换结果（第一次转换不准） */
    HAL_Delay(2u);
    (void)HAL_ADC_GetValue(&hadc1);
    (void)HAL_ADC_GetValue(&hadc2);
}

static void apply(knob_t *k, uint16_t raw)
{
    float f;
    float lo;
    float hi;

    /* 一阶低通 */
    f = KNOB_LP_ALPHA * (float)raw + (1.0f - KNOB_LP_ALPHA) * (float)k->raw;
    k->raw = (uint16_t)(f + 0.5f);

    /* 去掉两端机械死区后再映射到 0~180° */
    lo = (float)k->adc_min + (float)ADC_TRIM_LOW;
    hi = (float)k->adc_max - (float)ADC_TRIM_HIGH;

    if (hi <= lo + 100.0f)          /* 标定异常时退回整量程 */
    {
        lo = 0.0f;
        hi = ADC_FULL_SCALE;
    }

    f = ((float)k->raw - lo) * (KNOB_ANGLE_MAX - KNOB_ANGLE_MIN) / (hi - lo)
      + KNOB_ANGLE_MIN;

    if (f < KNOB_ANGLE_MIN) { f = KNOB_ANGLE_MIN; }
    if (f > KNOB_ANGLE_MAX) { f = KNOB_ANGLE_MAX; }
    k->angle = f;
}

uint8_t knob_update(void)
{
    uint16_t r1 = 0u;
    uint16_t r2 = 0u;

    if (read_channel(&hadc1, &r1) != 0u) { return 1u; }
    if (read_channel(&hadc2, &r2) != 0u) { return 1u; }

    apply(&s_pan,  r1);
    apply(&s_tilt, r2);

    return 0u;
}

const knob_t *knob_get_pan(void)
{
    return &s_pan;
}

const knob_t *knob_get_tilt(void)
{
    return &s_tilt;
}
