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

    ret = mpu6050_init();
    if (ret != 0u)
    {
        /* 区分"能读但不能通信"和"ID 不对" */
        if (mpu6050_read_raw(&s_raw) != 0u)
        {
            return ATT_ERR_I2C;
        }
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

    return ATT_OK;
}

att_status_t attitude_update(float dt)
{
    float w_yaw, w_pitch;

    if (dt <= 0.0f)
    {
        return ATT_ERR_I2C;
    }

    /* 一次 I2C 读取同时拿到原始值和物理量（400kHz 下 14 字节约 0.4ms） */
    if (mpu6050_read_raw(&s_raw) != 0u)
    {
        return ATT_ERR_I2C;
    }
    mpu6050_raw_to_data(&s_raw, &s_dat);

    w_yaw   = ATT_YAW_SIGN   * pick_gyro(&s_dat, ATT_YAW_AXIS);
    w_pitch = ATT_PITCH_SIGN * pick_gyro(&s_dat, ATT_PITCH_AXIS);

    /* 静止时把积分项掐掉，抑制零偏漂移 */
    if (fabsf(w_yaw) < ATT_STILL_DPS)   { w_yaw = 0.0f; }
    if (fabsf(w_pitch) < ATT_STILL_DPS) { w_pitch = 0.0f; }

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
