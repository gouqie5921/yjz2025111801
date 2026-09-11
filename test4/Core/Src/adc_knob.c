/**
  ******************************************************************************
  * @file    adc_knob.c
  * @brief   旋钮电位器读取（ADC1 连续转换 + 多次平均 + 一阶低通）
  ******************************************************************************
  */
#include "adc_knob.h"
#include "adc.h"

#define KNOB_AVG_N      8u          /* 每次读 8 次求平均 */
#define KNOB_ADC_FULL   4095.0f     /* 12 位满量程 */

void knob_init(void)
{
    /* ADC1 已配置为连续转换模式，启动一次后即可随时取最新值 */
    (void)HAL_ADC_Start(&hadc1);
}

uint16_t knob_read_raw(void)
{
    uint32_t sum = 0u;
    uint8_t  i;

    for (i = 0u; i < KNOB_AVG_N; i++)
    {
        sum += HAL_ADC_GetValue(&hadc1);
    }
    return (uint16_t)(sum / KNOB_AVG_N);
}

float knob_read_percent(void)
{
    static float filt = 0.0f;
    float x;

    x = ((float)knob_read_raw() * 100.0f) / KNOB_ADC_FULL;

    /* 一阶低通：旋钮是慢变量，滤掉 ADC 抖动，PID 目标才平滑 */
    filt += 0.25f * (x - filt);

    return filt;
}
