/**
  ******************************************************************************
  * @file    pid.h
  * @brief   通用增量式/位置式 PID（浮点，带积分限幅与微分低通）
  ******************************************************************************
  */
#ifndef __PID_H
#define __PID_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    float kp;          /* 比例增益 */
    float ki;          /* 积分增益 */
    float kd;          /* 微分增益 */

    float integral;    /* 积分累加项（已含 ki） */
    float prev_meas;   /* 上一次测量值（微分作用在测量值上，避免设定值突变的微分冲击） */
    float d_filt;      /* 微分低通后的值 */

    float out_min;     /* 输出下限（同时作为积分限幅下限，抗饱和） */
    float out_max;     /* 输出上限 */
    float d_alpha;     /* 微分低通系数 0~1，越小越平滑 */
} pid_t;

/* 初始化：kp/ki/kd 与输出限幅 */
void  pid_init(pid_t *p, float kp, float ki, float kd, float out_min, float out_max);

/* 清空积分与微分状态（模式切换、重新定位时调用） */
void  pid_reset(pid_t *p);

/* 在线改参数（调参用） */
void  pid_set_gains(pid_t *p, float kp, float ki, float kd);

/* 单次运算：setpoint=目标值，measure=实测值，dt=周期(秒)，返回限幅后的输出 */
float pid_compute(pid_t *p, float setpoint, float measure, float dt);

#ifdef __cplusplus
}
#endif

#endif /* __PID_H */
