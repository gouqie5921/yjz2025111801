/**
  ******************************************************************************
  * @file    hwprobe.c
  * @brief   主控端硬件自检实现（只在 APP_HW_PROBE=1 时编译进固件）
  ******************************************************************************
  */
#include "hwprobe.h"

#if (APP_HW_PROBE == 1u)

#include "main.h"
#include "i2c.h"
#include <string.h>

uint8_t  g_i2c_scan[14];
uint8_t  g_i2c_found;
uint32_t g_i2c_err;
uint8_t  g_i2c_addr68_ok;
uint8_t  g_i2c_addr69_ok;
uint8_t  g_pot_pullup[2];
uint8_t  g_pot_pulldown[2];
uint8_t  g_i2c_line_pullup[2];
uint8_t  g_i2c_line_pulldown[2];

uint8_t  g_whoami_rd_ok;
uint8_t  g_whoami_val;
uint8_t  g_pwr_mgmt1;
uint8_t  g_burst_ok;
uint16_t g_accel_x;
uint16_t g_gyro_x;
uint32_t g_i2c_err_rd;

#define MPU_ADDR_W      0xD0u       /* 0x68 << 1 */

/* ---------------- 引脚探针 ---------------- */
static void probe_pins(void)
{
    GPIO_InitTypeDef gi = {0};

    /* --- PA1 / PA4：电位器 --- */
    gi.Pin   = GPIO_PIN_1 | GPIO_PIN_4;
    gi.Mode  = GPIO_MODE_INPUT;
    gi.Speed = GPIO_SPEED_FREQ_LOW;

    gi.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &gi);
    HAL_Delay(3);
    g_pot_pullup[0] = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_1) == GPIO_PIN_SET) ? 1u : 0u;
    g_pot_pullup[1] = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_4) == GPIO_PIN_SET) ? 1u : 0u;

    gi.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(GPIOA, &gi);
    HAL_Delay(3);
    g_pot_pulldown[0] = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_1) == GPIO_PIN_SET) ? 1u : 0u;
    g_pot_pulldown[1] = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_4) == GPIO_PIN_SET) ? 1u : 0u;

    /* 恢复成模拟输入，交给 ADC */
    gi.Mode = GPIO_MODE_ANALOG;
    gi.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gi);

    /* --- PB6 / PB7：I2C --- */
    gi.Pin  = GPIO_PIN_6 | GPIO_PIN_7;
    gi.Mode = GPIO_MODE_INPUT;

    gi.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOB, &gi);
    HAL_Delay(3);
    g_i2c_line_pullup[0] = (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_6) == GPIO_PIN_SET) ? 1u : 0u;
    g_i2c_line_pullup[1] = (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_7) == GPIO_PIN_SET) ? 1u : 0u;

    gi.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(GPIOB, &gi);
    HAL_Delay(3);
    g_i2c_line_pulldown[0] = (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_6) == GPIO_PIN_SET) ? 1u : 0u;
    g_i2c_line_pulldown[1] = (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_7) == GPIO_PIN_SET) ? 1u : 0u;

    /* 恢复成复用开漏，交回 I2C */
    gi.Mode  = GPIO_MODE_AF_OD;
    gi.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gi);
}

/* ---------------- I2C 扫描 ---------------- */
static void probe_i2c(void)
{
    uint8_t  a;
    uint8_t  i;

    for (i = 0u; i < 14u; i++)
    {
        g_i2c_scan[i] = 0u;
    }
    g_i2c_found = 0u;

    for (a = 0x08u; a <= 0x77u; a++)
    {
        if (HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)((uint16_t)a << 1), 2u, 5u) == HAL_OK)
        {
            g_i2c_scan[(uint8_t)((a - 0x08u) >> 3)] |= (uint8_t)(1u << ((a - 0x08u) & 7u));
            g_i2c_found++;
        }
    }

    g_i2c_err = HAL_I2C_GetError(&hi2c1);

    g_i2c_addr68_ok = (HAL_I2C_IsDeviceReady(&hi2c1, 0x68u << 1, 2u, 5u) == HAL_OK) ? 1u : 0u;
    g_i2c_addr69_ok = (HAL_I2C_IsDeviceReady(&hi2c1, 0x69u << 1, 2u, 5u) == HAL_OK) ? 1u : 0u;
}

/* ---------------- MPU6050 寄存器级探测 ----------------
   回答两个问题：① 寄存器到底能不能读（Mem_Read 是否成功）；
   ② WHO_AM_I 究竟是几（便宜的 GY-521 常用 MPU6500/9250 兼容芯片，
      其 WHO_AM_I 不是 0x68）。 */
static void probe_whoami(void)
{
    uint8_t v   = 0u;
    uint8_t p   = 0u;
    uint8_t buf[14];
    uint8_t i;

    g_whoami_rd_ok = (HAL_I2C_Mem_Read(&hi2c1, MPU_ADDR_W, 0x75u, I2C_MEMADD_SIZE_8BIT,
                                       &v, 1u, 100u) == HAL_OK) ? 1u : 0u;
    g_whoami_val   = v;
    g_i2c_err_rd   = HAL_I2C_GetError(&hi2c1);

    (void)HAL_I2C_Mem_Read(&hi2c1, MPU_ADDR_W, 0x6Bu, I2C_MEMADD_SIZE_8BIT, &p, 1u, 100u);
    g_pwr_mgmt1 = p;

    for (i = 0u; i < 14u; i++)
    {
        buf[i] = 0u;
    }
    g_burst_ok = (HAL_I2C_Mem_Read(&hi2c1, MPU_ADDR_W, 0x3Bu, I2C_MEMADD_SIZE_8BIT,
                                   buf, 14u, 100u) == HAL_OK) ? 1u : 0u;
    g_accel_x = (uint16_t)(((uint16_t)buf[0] << 8) | buf[1]);
    g_gyro_x  = (uint16_t)(((uint16_t)buf[8] << 8) | buf[9]);
}

void hwprobe_run(void)
{
    probe_pins();
    probe_i2c();
    probe_whoami();
}

#endif /* APP_HW_PROBE */
