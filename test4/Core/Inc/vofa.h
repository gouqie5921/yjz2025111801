/**
  ******************************************************************************
  * @file    vofa.h
  * @brief   串口输出（VOFA+ JustFloat 波形 + 文本），带互斥锁保护
  ******************************************************************************
  */
#ifndef __VOFA_H
#define __VOFA_H

#ifdef __cplusplus
extern "C" {
#endif

/* 创建串口互斥锁（在 RTOS 对象初始化阶段调用） */
void vofa_init(void);

/* 发一帧 JustFloat：5 个 float(小端) + 帧尾 00 00 80 7F，VOFA+ 协议选 JustFloat
 * 通道：0=目标  1=实际  2=PWM  3=误差  4=累计角度(°)（任何模式下都有效，便于标定 CPR） */
void vofa_send_justfloat(float c0, float c1, float c2, float c3, float c4);

/* 发一段文本（固定字符串，避免多任务 printf） */
void vofa_send_text(const char *s);

#ifdef __cplusplus
}
#endif

#endif /* __VOFA_H */
