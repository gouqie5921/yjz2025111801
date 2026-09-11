/**
  ******************************************************************************
  * @file    knob.h
  * @brief   两个电位器采集（ADC1_IN1 = PA1 水平，ADC2_IN4 = PA4 俯仰）
  ******************************************************************************
  */
#ifndef __KNOB_H
#define __KNOB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define KNOB_ANGLE_MIN      0.0f
#define KNOB_ANGLE_MAX      180.0f

/* 上电时自动标定电位器两端（分别转到头记录 min/max），这里存标定结果 */
typedef struct
{
    uint16_t adc_min;
    uint16_t adc_max;
    uint16_t raw;           /* 最近一次滤波后的原始值 */
    float    angle;         /* 映射到 0~180° */
} knob_t;

/* 启动 ADC1/ADC2 的连续转换（在 RTOS 启动前调用一次） */
void knob_start(void);

/* 读一次并滤波（在任务里按周期调用）。返回 0=成功 */
uint8_t knob_update(void);

/* 水平电位器 / 俯仰电位器 */
const knob_t *knob_get_pan(void);
const knob_t *knob_get_tilt(void);

#ifdef __cplusplus
}
#endif

#endif /* __KNOB_H */
