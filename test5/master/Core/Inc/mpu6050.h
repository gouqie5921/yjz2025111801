/**
  ******************************************************************************
  * @file    mpu6050.h
  * @brief   MPU6050 (GY-521) 六轴传感器驱动 —— I2C1 / PB6(SCL) PB7(SDA)
  ******************************************************************************
  */
#ifndef __MPU6050_H
#define __MPU6050_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define MPU6050_ADDR        0xD0u       /* 0x68 << 1（HAL 用 8 位地址） */
#define MPU6050_WHO_AM_I    0x75u
#define MPU6050_WHOAMI_VAL  0x68u

/* 灵敏度（当前量程下） */
#define MPU6050_GYRO_LSB_PER_DPS   131.0f   /* ±250 °/s  → 131  LSB/(°/s) */
#define MPU6050_ACC_LSB_PER_G      16384.0f /* ±2 g      → 16384 LSB/g    */

typedef struct
{
    int16_t ax, ay, az;     /* 原始加速度 */
    int16_t gx, gy, gz;     /* 原始角速度 */
} mpu6050_raw_t;

typedef struct
{
    float ax, ay, az;       /* 单位 g   */
    float gx, gy, gz;       /* 单位 °/s */
} mpu6050_data_t;

/* 初始化：检查 WHO_AM_I、唤醒、配置 DLPF/量程。返回 0=成功 1=失败（读不到器件） */
uint8_t mpu6050_init(void);

/* 读一次原始 6 轴数据（I2C 突发读 14 字节），返回 0=成功 */
uint8_t mpu6050_read_raw(mpu6050_raw_t *raw);

/* 原始值 -> 物理量（g / °·s⁻¹），并扣掉陀螺零偏 */
void    mpu6050_raw_to_data(const mpu6050_raw_t *raw, mpu6050_data_t *d);

/* 读一次并换算成物理量，返回 0=成功 */
uint8_t mpu6050_read(mpu6050_data_t *d);

/* 静止零偏校准：连续采样 samples 次取平均，作为陀螺仪零偏（消除积分漂移） */
void    mpu6050_calib_gyro(uint16_t samples);

/* 读回最近一次校准得到的零偏（°/s） */
void    mpu6050_get_gyro_bias(float *bx, float *by, float *bz);

#ifdef __cplusplus
}
#endif

#endif /* __MPU6050_H */
