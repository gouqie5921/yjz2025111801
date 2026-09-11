/**
  ******************************************************************************
  * @file    mpu6050.c
  * @brief   MPU6050 驱动实现（I2C1，400kHz）
  *
  *  引脚：I2C1 = PB6(SCL) / PB7(SDA)，开漏 + 上拉，400kHz。
  *        本工程 CubeMX 勾了 "Generate peripheral initialization as pair of
  *        .c/.h files"，所以 HAL_I2C_MspInit（PB6/PB7 复用开漏 + I2C1 时钟）
  *        生成在 Core/Src/i2c.c 里，MX_I2C1_Init() 会自动调用。
  ******************************************************************************
  */
#include "mpu6050.h"
#include "i2c.h"
#include "main.h"

#define REG_SMPLRT_DIV      0x19u
#define REG_CONFIG          0x1Au
#define REG_GYRO_CONFIG     0x1Bu
#define REG_ACCEL_CONFIG    0x1Cu
#define REG_ACCEL_XOUT_H    0x3Bu
#define REG_PWR_MGMT_1      0x6Bu

static float s_bias_gx = 0.0f;
static float s_bias_gy = 0.0f;
static float s_bias_gz = 0.0f;

static uint8_t wr_reg(uint8_t reg, uint8_t val)
{
    return (HAL_I2C_Mem_Write(&hi2c1, MPU6050_ADDR, reg, I2C_MEMADD_SIZE_8BIT,
                              &val, 1u, 100u) == HAL_OK) ? 0u : 1u;
}

static uint8_t rd_regs(uint8_t reg, uint8_t *buf, uint16_t len)
{
    return (HAL_I2C_Mem_Read(&hi2c1, MPU6050_ADDR, reg, I2C_MEMADD_SIZE_8BIT,
                             buf, len, 100u) == HAL_OK) ? 0u : 1u;
}

uint8_t mpu6050_init(void)
{
    uint8_t id = 0u;
    uint8_t who = 0u;

    /* 1) 器件在不在 */
    if (rd_regs(MPU6050_WHO_AM_I, &who, 1u) != 0u)
    {
        return 1u;
    }
    id = who;
    if (id != MPU6050_WHOAMI_VAL)
    {
        return 1u;                      /* 读到的不是 0x68 */
    }

    /* 2) 唤醒（退出睡眠），时钟源选 X 轴陀螺 PLL */
    (void)wr_reg(REG_PWR_MGMT_1, 0x00u);
    HAL_Delay(50u);
    (void)wr_reg(REG_PWR_MGMT_1, 0x01u);
    HAL_Delay(10u);

    /* 3) 采样率 100Hz、DLPF 44Hz、陀螺 ±250°/s、加速度 ±2g */
    (void)wr_reg(REG_SMPLRT_DIV,  0x09u);
    (void)wr_reg(REG_CONFIG,      0x03u);
    (void)wr_reg(REG_GYRO_CONFIG, 0x00u);
    (void)wr_reg(REG_ACCEL_CONFIG,0x00u);
    HAL_Delay(20u);

    return 0u;
}

uint8_t mpu6050_read_raw(mpu6050_raw_t *raw)
{
    uint8_t b[14];

    if (raw == NULL)
    {
        return 1u;
    }
    if (rd_regs(REG_ACCEL_XOUT_H, b, 14u) != 0u)
    {
        return 1u;
    }

    raw->ax = (int16_t)(((uint16_t)b[0]  << 8) | b[1]);
    raw->ay = (int16_t)(((uint16_t)b[2]  << 8) | b[3]);
    raw->az = (int16_t)(((uint16_t)b[4]  << 8) | b[5]);
    /* b[6] b[7] = 温度，本工程不用 */
    raw->gx = (int16_t)(((uint16_t)b[8]  << 8) | b[9]);
    raw->gy = (int16_t)(((uint16_t)b[10] << 8) | b[11]);
    raw->gz = (int16_t)(((uint16_t)b[12] << 8) | b[13]);

    return 0u;
}

void mpu6050_raw_to_data(const mpu6050_raw_t *raw, mpu6050_data_t *d)
{
    if ((raw == NULL) || (d == NULL))
    {
        return;
    }

    d->ax = (float)raw->ax / MPU6050_ACC_LSB_PER_G;
    d->ay = (float)raw->ay / MPU6050_ACC_LSB_PER_G;
    d->az = (float)raw->az / MPU6050_ACC_LSB_PER_G;

    d->gx = ((float)raw->gx / MPU6050_GYRO_LSB_PER_DPS) - s_bias_gx;
    d->gy = ((float)raw->gy / MPU6050_GYRO_LSB_PER_DPS) - s_bias_gy;
    d->gz = ((float)raw->gz / MPU6050_GYRO_LSB_PER_DPS) - s_bias_gz;
}

uint8_t mpu6050_read(mpu6050_data_t *d)
{
    mpu6050_raw_t r;

    if (d == NULL)
    {
        return 1u;
    }
    if (mpu6050_read_raw(&r) != 0u)
    {
        return 1u;
    }

    mpu6050_raw_to_data(&r, d);

    return 0u;
}

void mpu6050_calib_gyro(uint16_t samples)
{
    mpu6050_raw_t r;
    float sx = 0.0f, sy = 0.0f, sz = 0.0f;
    uint16_t i;
    uint16_t n = 0u;

    if (samples == 0u)
    {
        samples = 1u;
    }

    for (i = 0u; i < samples; i++)
    {
        if (mpu6050_read_raw(&r) == 0u)
        {
            sx += (float)r.gx;
            sy += (float)r.gy;
            sz += (float)r.gz;
            n++;
        }
        HAL_Delay(2u);                  /* 约 100Hz 采样 */
    }

    if (n == 0u)
    {
        n = 1u;
    }

    s_bias_gx = (sx / (float)n) / MPU6050_GYRO_LSB_PER_DPS;
    s_bias_gy = (sy / (float)n) / MPU6050_GYRO_LSB_PER_DPS;
    s_bias_gz = (sz / (float)n) / MPU6050_GYRO_LSB_PER_DPS;
}

void mpu6050_get_gyro_bias(float *bx, float *by, float *bz)
{
    if (bx != NULL) { *bx = s_bias_gx; }
    if (by != NULL) { *by = s_bias_gy; }
    if (bz != NULL) { *bz = s_bias_gz; }
}
