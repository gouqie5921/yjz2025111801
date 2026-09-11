/**
  ******************************************************************************
  * @file    adc_knob.h
  * @brief   电位器旋钮（ADC1_IN1 / PA1）读取
  ******************************************************************************
  */
#ifndef __ADC_KNOB_H
#define __ADC_KNOB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* 启动 ADC1 连续转换（main 里调用一次） */
void     knob_init(void);

/* 读一次，内部做多次平均。返回 0~4095 */
uint16_t knob_read_raw(void);

/* 读成百分比 0~100（带一阶低通滤波，抑制抖动） */
float    knob_read_percent(void);

#ifdef __cplusplus
}
#endif

#endif /* __ADC_KNOB_H */
