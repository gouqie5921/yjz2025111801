/**
  ******************************************************************************
  * @file    pid.c
  * @brief   PID 实现：位置式 + 积分限幅(抗饱和) + 微分低通
  ******************************************************************************
  */
#include "pid.h"

void pid_init(pid_t *p, float kp, float ki, float kd, float out_min, float out_max)
{
    p->kp = kp;
    p->ki = ki;
    p->kd = kd;
    p->out_min = out_min;
    p->out_max = out_max;
    p->d_alpha = 0.3f;      /* 微分低通，抑制编码器量化噪声 */
    pid_reset(p);
}

void pid_reset(pid_t *p)
{
    p->integral  = 0.0f;
    p->prev_meas = 0.0f;
    p->d_filt    = 0.0f;
}

void pid_set_gains(pid_t *p, float kp, float ki, float kd)
{
    p->kp = kp;
    p->ki = ki;
    p->kd = kd;
}

float pid_compute(pid_t *p, float setpoint, float measure, float dt)
{
    float err;
    float deriv;
    float out;

    err = setpoint - measure;

    /* 微分作用在"测量值"上（避免设定值阶跃时的微分冲击），再低通 */
    if (dt > 0.0f)
    {
        deriv = -(measure - p->prev_meas) / dt;
    }
    else
    {
        deriv = 0.0f;
    }
    p->prev_meas = measure;
    p->d_filt += p->d_alpha * (deriv - p->d_filt);

    out = (p->kp * err) + p->integral + (p->kd * p->d_filt);

    /* 条件积分抗饱和：只有"输出未顶到限幅"或"误差方向能让它退回来"时才累加积分。
       否则（如刚启动误差巨大、输出已饱和在 ±1000）就不让积分继续涨，
       避免到目标后要靠反向误差慢慢泄放积分 → 造成大超调。 */
    if (!(((out >= p->out_max) && (err > 0.0f)) ||
          ((out <= p->out_min) && (err < 0.0f))))
    {
        p->integral += (p->ki * err) * dt;
        if (p->integral > p->out_max)
        {
            p->integral = p->out_max;
        }
        if (p->integral < p->out_min)
        {
            p->integral = p->out_min;
        }
        out = (p->kp * err) + p->integral + (p->kd * p->d_filt);
    }

    if (out > p->out_max)
    {
        out = p->out_max;
    }
    if (out < p->out_min)
    {
        out = p->out_min;
    }
    return out;
}
