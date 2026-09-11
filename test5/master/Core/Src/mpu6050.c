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
static uint8_t s_whoami = 0u;

/* ---------------- 软件 I2C（位操作）----------------
   SCL = PB6，SDA = PB7，都是开漏 + 模块板载 4.7k 上拉。
   速度约 100kHz（SI2C_DELAY_LOOPS 决定），对 MPU6050/6500 足够。 */
#define SI2C_PORT           GPIOB
#define SI2C_SCL_PIN        GPIO_PIN_6
#define SI2C_SDA_PIN        GPIO_PIN_7
#define SI2C_DELAY_LOOPS    60u

#define SCL_H()   HAL_GPIO_WritePin(SI2C_PORT, SI2C_SCL_PIN, GPIO_PIN_SET)
#define SCL_L()   HAL_GPIO_WritePin(SI2C_PORT, SI2C_SCL_PIN, GPIO_PIN_RESET)
#define SDA_H()   HAL_GPIO_WritePin(SI2C_PORT, SI2C_SDA_PIN, GPIO_PIN_SET)
#define SDA_L()   HAL_GPIO_WritePin(SI2C_PORT, SI2C_SDA_PIN, GPIO_PIN_RESET)
#define SDA_IN()  (HAL_GPIO_ReadPin(SI2C_PORT, SI2C_SDA_PIN) == GPIO_PIN_SET)

static void i2c_delay(void)
{
    volatile uint32_t i;

    for (i = 0u; i < SI2C_DELAY_LOOPS; i++)
    {
        /* 空转，产生约 1~2us 的半周期 */
    }
}

static void i2c_start(void)
{
    SDA_H();
    SCL_H();
    i2c_delay();
    SDA_L();            /* SCL 高时 SDA 下降沿 = 起始 */
    i2c_delay();
    SCL_L();
    i2c_delay();
}

static void i2c_stop(void)
{
    SDA_L();
    i2c_delay();
    SCL_H();
    i2c_delay();
    SDA_H();            /* SCL 高时 SDA 上升沿 = 停止 */
    i2c_delay();
}

/* 返回 0 = 收到 ACK，1 = 没应答 */
static uint8_t i2c_wr_byte(uint8_t b)
{
    uint8_t i;
    uint8_t ack;

    for (i = 0u; i < 8u; i++)
    {
        if ((b & 0x80u) != 0u) { SDA_H(); } else { SDA_L(); }
        b = (uint8_t)(b << 1);
        i2c_delay();
        SCL_H();
        i2c_delay();
        SCL_L();
        i2c_delay();
    }

    SDA_H();            /* 释放 SDA 由从机应答 */
    i2c_delay();
    SCL_H();
    i2c_delay();
    ack = SDA_IN() ? 1u : 0u;
    SCL_L();
    i2c_delay();

    return ack;
}

static uint8_t i2c_rd_byte(uint8_t ack)
{
    uint8_t i;
    uint8_t b = 0u;

    SDA_H();
    for (i = 0u; i < 8u; i++)
    {
        b = (uint8_t)(b << 1);
        i2c_delay();
        SCL_H();
        i2c_delay();
        if (SDA_IN())
        {
            b |= 1u;
        }
        SCL_L();
        i2c_delay();
    }

    if (ack != 0u) { SDA_L(); } else { SDA_H(); }
    i2c_delay();
    SCL_H();
    i2c_delay();
    SCL_L();
    i2c_delay();
    SDA_H();

    return b;
}

/* ---- 调试统计（SWD 直接 dump，用来定位"到底哪一步失败"）---- */
uint8_t  g_imu_last_hal_status = 0u;   /* 最近一次 Mem_Read 返回值 0=OK 1=ERROR 2=BUSY 3=TIMEOUT */
uint8_t  g_imu_last_err_code   = 0u;   /* 失败时的 HAL_I2C_GetError() */
uint8_t  g_imu_last_who        = 0u;   /* 最近一次读到的 WHO_AM_I */
uint16_t g_imu_rd_ok           = 0u;
uint16_t g_imu_rd_fail         = 0u;

void mpu6050_bus_recover(void)
{
    uint8_t i;

    /* 释放总线 + 打 9 个时钟，把从机可能卡住的移位寄存器清空 */
    SDA_H();
    for (i = 0u; i < 9u; i++)
    {
        SCL_L();
        i2c_delay();
        SCL_H();
        i2c_delay();
    }
    i2c_stop();
}

/* 用软件时序接管 PB6/PB7（关掉硬件 I2C 外设）
   原因：STM32F1 的硬件 I2C 在 FreeRTOS 的中断环境下会稳定地失败
   （实测 RTOS 启动前 117 次传输全成功，启动后 20/20 次 AF），
   软件位操作时序完全可控，不再受外设状态机影响。 */
void mpu6050_bus_config(void)
{
    GPIO_InitTypeDef gi = {0};

    (void)HAL_I2C_DeInit(&hi2c1);       /* 关外设，并把 PB6/PB7 交还 GPIO */

    __HAL_RCC_GPIOB_CLK_ENABLE();
    gi.Pin   = SI2C_SCL_PIN | SI2C_SDA_PIN;
    gi.Mode  = GPIO_MODE_OUTPUT_OD;     /* 开漏：写 1 释放，靠外部上拉拉高 */
    gi.Pull  = GPIO_PULLUP;
    gi.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(SI2C_PORT, &gi);

    SCL_H();
    SDA_H();
    i2c_delay();
}

uint8_t mpu6050_get_whoami(void)
{
    return s_whoami;
}

uint8_t mpu6050_id_known(uint8_t id)
{
    switch (id)
    {
    case 0x68u:                 /* MPU6050 / MPU6000（正品） */
    case 0x70u:                 /* MPU6500 */
    case 0x71u:                 /* MPU6500 变体 */
    case 0x72u:                 /* MPU6500 变体 */
    case 0x73u:                 /* MPU9250 */
    case 0x98u:                 /* 部分兼容芯片 */
    case 0x19u:                 /* 部分兼容芯片 */
        return 1u;
    default:
        return 0u;
    }
}

/* 写寄存器：起始 + 器件地址 + 寄存器号 + 数据 + 停止 */
static uint8_t wr_reg(uint8_t reg, uint8_t val)
{
    uint8_t bad = 0u;

    i2c_start();
    if (i2c_wr_byte(MPU6050_ADDR) != 0u)      { bad = 1u; }
    else if (i2c_wr_byte(reg) != 0u)          { bad = 2u; }
    else if (i2c_wr_byte(val) != 0u)          { bad = 3u; }
    i2c_stop();

    g_imu_last_hal_status = bad;
    if (bad != 0u)
    {
        g_imu_last_err_code = bad;      /* 1=地址没应答 2=寄存器号没应答 3=数据没应答 */
        return 1u;
    }
    return 0u;
}

/* 读寄存器：起始 + 地址W + 寄存器号 + 重复起始 + 地址R + 连续读 */
static uint8_t rd_regs(uint8_t reg, uint8_t *buf, uint16_t len)
{
    uint16_t i;
    uint8_t  bad = 0u;

    i2c_start();
    if (i2c_wr_byte(MPU6050_ADDR) != 0u)             { bad = 1u; }
    else if (i2c_wr_byte(reg) != 0u)                 { bad = 2u; }
    else
    {
        i2c_start();
        if (i2c_wr_byte((uint8_t)(MPU6050_ADDR | 1u)) != 0u) { bad = 3u; }
        else
        {
            for (i = 0u; i < len; i++)
            {
                buf[i] = i2c_rd_byte((uint8_t)((i + 1u < len) ? 1u : 0u));
            }
        }
    }
    i2c_stop();

    g_imu_last_hal_status = bad;
    if (bad != 0u)
    {
        g_imu_rd_fail++;
        g_imu_last_err_code = bad;      /* 1=地址W没应答 2=寄存器号没应答 3=地址R没应答 */
        return 1u;
    }

    g_imu_rd_ok++;
    return 0u;
}

/* 把 14 字节突发数据拆成 6 轴原始值 */
static void unpack(const uint8_t *b, mpu6050_raw_t *raw)
{
    raw->ax = (int16_t)(((uint16_t)b[0]  << 8) | b[1]);
    raw->ay = (int16_t)(((uint16_t)b[2]  << 8) | b[3]);
    raw->az = (int16_t)(((uint16_t)b[4]  << 8) | b[5]);
    /* b[6] b[7] = 温度，本工程不用 */
    raw->gx = (int16_t)(((uint16_t)b[8]  << 8) | b[9]);
    raw->gy = (int16_t)(((uint16_t)b[10] << 8) | b[11]);
    raw->gz = (int16_t)(((uint16_t)b[12] << 8) | b[13]);
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
    s_whoami = who;
    g_imu_last_who = who;
    if (mpu6050_id_known(id) == 0u)
    {
        return 1u;                      /* 读到了但不认识的 ID */
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
    uint8_t try;

    if (raw == NULL)
    {
        return 1u;
    }

    /* 偶发 NACK/超时先重试几次（长线干扰时很常见） */
    for (try = 0u; try < MPU6050_READ_RETRY; try++)
    {
        if (rd_regs(REG_ACCEL_XOUT_H, b, 14u) == 0u)
        {
            unpack(b, raw);
            return 0u;
        }
        HAL_Delay(1u);
    }

    /* 还是不行：复位 I2C 外设（清总线卡死）再试最后一回 */
    mpu6050_bus_recover();
    if (rd_regs(REG_ACCEL_XOUT_H, b, 14u) == 0u)
    {
        unpack(b, raw);
        return 0u;
    }

    return 1u;
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
