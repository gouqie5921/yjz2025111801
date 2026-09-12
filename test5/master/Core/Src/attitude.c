/**
  ******************************************************************************
  * @file    attitude.c
  * @brief   姿态解算实现
  *
  *  水平旋转（yaw）：陀螺仪直接积分。没有磁力计，长期会漂，所以提供了
  *                  attitude_rebase() 在上电/切换模式时重新定零位。
  *  俯仰（pitch）  ：陀螺积分 + 加速度计重力方向做互补滤波，不会长期漂。
  ******************************************************************************
  */
#include "attitude.h"
#include "main.h"
#include <math.h>

#define RAD_TO_DEG      57.2957795f

static mpu6050_raw_t  s_raw;
static mpu6050_data_t s_dat;

static float s_yaw        = 0.0f;   /* 累积角度 */
static float s_pitch      = 0.0f;
static float s_yaw_zero   = 0.0f;   /* 零位偏置 */
static float s_pitch_zero = 0.0f;

static uint8_t s_calibrated = 0u;
static float   s_bias_avg   = 0.0f;

/* ---- 调试统计（SWD 直接 dump）---- */
uint8_t  g_att_init_ret = 0xFFu;    /* 最近一次 attitude_init() 的返回值 */
uint16_t g_att_init_cnt = 0u;
uint16_t g_att_init_ok  = 0u;
uint16_t g_att_upd_ok   = 0u;
uint16_t g_att_upd_fail = 0u;

/* 按轴号取分量：0=gx 1=gy 2=gz（同一套取法，保证宏可换） */
static float pick_gyro(const mpu6050_data_t *d, uint8_t axis)
{
    if (axis == 0u) { return d->gx; }
    if (axis == 1u) { return d->gy; }
    return d->gz;
}

static float pick_acc(const mpu6050_data_t *d, uint8_t axis)
{
    if (axis == 0u) { return d->ax; }
    if (axis == 1u) { return d->ay; }
    return d->az;
}

att_status_t attitude_init(void)
{
    uint8_t ret;
    float bx, by, bz;

    /* 把 PB6/PB7 从硬件 I2C 外设接管过来，改用软件位操作时序 */
    mpu6050_bus_config();

    g_att_init_cnt++;

    ret = mpu6050_init();
    if (ret != 0u)
    {
        /* 区分"能读但不能通信"和"ID 不对" */
        if (mpu6050_read_raw(&s_raw) != 0u)
        {
            g_att_init_ret = (uint8_t)ATT_ERR_I2C;
            return ATT_ERR_I2C;
        }
        g_att_init_ret = (uint8_t)ATT_ERR_WHOAMI;
        return ATT_ERR_WHOAMI;
    }

    /* 静止零偏校准：此时面包板必须放平不动 */
    mpu6050_calib_gyro(200u);
    mpu6050_get_gyro_bias(&bx, &by, &bz);
    s_bias_avg = (fabsf(bx) + fabsf(by) + fabsf(bz)) / 3.0f;

    if (s_bias_avg > 20.0f)
    {
        /* 零偏大得离谱，说明校准过程有晃动，等调用方重新校准 */
        s_calibrated = 0u;
    }
    else
    {
        s_calibrated = 1u;
    }

    s_yaw = 0.0f;
    s_pitch = 0.0f;
    s_yaw_zero = 0.0f;
    s_pitch_zero = 0.0f;

    g_att_init_ret = (uint8_t)ATT_OK;
    g_att_init_ok++;

    return ATT_OK;
}

att_status_t attitude_update(float dt)
{
    float w_yaw, w_pitch;

    if (dt <= 0.0f)
    {
        g_att_upd_fail++;
        return ATT_ERR_I2C;
    }

    /* 一次 I2C 读取同时拿到原始值和物理量（100kHz 下 14 字节约 1.6ms） */
    if (mpu6050_read_raw(&s_raw) != 0u)
    {
        g_att_upd_fail++;
        return ATT_ERR_I2C;
    }
    mpu6050_raw_to_data(&s_raw, &s_dat);
    g_att_upd_ok++;

    w_yaw   = ATT_YAW_SIGN   * pick_gyro(&s_dat, ATT_YAW_AXIS);
    w_pitch = ATT_PITCH_SIGN * pick_gyro(&s_dat, ATT_PITCH_AXIS);

    /* 注意：这里**不做**静止死区。
       之前用 "|w| < 1.5°/s 就当 0" 来抑制漂移，副作用是缓慢转动（<1.5°/s）
       被整个丢掉 —— 表现为"慢慢俯仰时陀螺仪不灵敏、云台半拍才跟上"。
       漂移现在由 mpu6050 里的零偏自适应跟踪负责（实测 0.0034°/s），
       所以这里必须原样积分，才能对慢速动作也立即响应。 */

    /* --- yaw：陀螺积分 --- */
    s_yaw += w_yaw * dt;

    /* --- pitch：陀螺积分 + 加速度计互补 --- */
    s_pitch += w_pitch * dt;

    if (ATT_COMP_ALPHA < 0.999f)
    {
        float up  = pick_acc(&s_dat, ATT_ACC_UP_AXIS);
        float fwd = ATT_ACC_SIGN * pick_acc(&s_dat, ATT_ACC_FWD_AXIS);
        float mag = sqrtf(up * up + fwd * fwd);

        if (mag > 0.3f)                 /* 自由落体/剧烈晃动时不修正 */
        {
            float pitch_acc = atan2f(-fwd, up) * RAD_TO_DEG;
            s_pitch = ATT_COMP_ALPHA * s_pitch
                    + (1.0f - ATT_COMP_ALPHA) * pitch_acc;
        }
    }

    return ATT_OK;
}

void attitude_rebase(void)
{
    s_yaw_zero   = s_yaw;
    s_pitch_zero = s_pitch;
}

void attitude_limit_yaw(float limit)
{
    float rel = s_yaw - s_yaw_zero;

    if (rel > limit)
    {
        s_yaw_zero = s_yaw - limit;
    }
    else if (rel < -limit)
    {
        s_yaw_zero = s_yaw + limit;
    }
}

void attitude_limit_pitch(float limit)
{
    float rel = s_pitch - s_pitch_zero;

    if (rel > limit)
    {
        s_pitch_zero = s_pitch - limit;
    }
    else if (rel < -limit)
    {
        s_pitch_zero = s_pitch + limit;
    }
}

float attitude_get_yaw(void)
{
    return s_yaw - s_yaw_zero;
}

float attitude_get_pitch(void)
{
    return s_pitch - s_pitch_zero;
}

const mpu6050_raw_t *attitude_get_raw(void)
{
    return &s_raw;
}

const mpu6050_data_t *attitude_get_data(void)
{
    return &s_dat;
}

float attitude_get_gyro_bias_avg(void)
{
    return s_bias_avg;
}

uint8_t attitude_is_calibrated(void)
{
    return s_calibrated;
}
