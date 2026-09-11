/**
  ******************************************************************************
  * @file    attitude.h
  * @brief   姿态解算：陀螺仪积分 + 加速度计互补滤波
  *
  *  GY-521 竖直插在面包板上，模块自身坐标系的"竖直方向"和面包板的竖直方向
  *  并不一致，所以这里把轴的选择全部做成宏。第一次上电用串口打印原始 6 轴数据，
  *  按现象改下面 4 个轴号/符号即可，不用改硬件。
  ******************************************************************************
  */
#ifndef __ATTITUDE_H
#define __ATTITUDE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "mpu6050.h"

/* ---------------- 轴向映射（按实测现象调整） ---------------- */
/* 陀螺仪三个轴的编号：0=gx  1=gy  2=gz */
#define ATT_YAW_AXIS        2       /* 面包板"水平旋转"对应的陀螺轴 */
#define ATT_YAW_SIGN        (+1.0f) /* 反了就改成 -1.0f */
#define ATT_PITCH_AXIS      1       /* 面包板"上下俯仰"对应的陀螺轴 */
#define ATT_PITCH_SIGN      (+1.0f)

/* 加速度计用来做俯仰角修正的轴：上方向 / 前后方向 */
#define ATT_ACC_UP_AXIS     2       /* 竖直方向的加速度轴 0=ax 1=ay 2=az */
#define ATT_ACC_FWD_AXIS    1       /* 前后方向的加速度轴 */
#define ATT_ACC_SIGN        (+1.0f)

/* 互补滤波系数。1.0 = 纯陀螺积分（不修正，短时间演示够用） */
#define ATT_COMP_ALPHA      0.98f

/* 角速度小于该值视为静止（°/s），用于自动抑制零偏漂移 */
#define ATT_STILL_DPS       0.8f

/* ---------------- 接口 ---------------- */
typedef enum
{
    ATT_OK = 0,
    ATT_ERR_I2C = 1,        /* 读 MPU6050 失败 */
    ATT_ERR_WHOAMI = 2      /* 器件 ID 不对（没插好 / 地址不对） */
} att_status_t;

/* 初始化：初始化 MPU6050 + 陀螺零偏校准（约 1s，期间面包板要放平不动）。 */
att_status_t attitude_init(void);

/* 周期调用，dt 单位秒。内部做一次 I2C 读取 + 滤波 */
att_status_t attitude_update(float dt);

/* 把当前姿态设成"零位"（切换控制方式时调用，避免云台跳变） */
void attitude_rebase(void);

/* 相对零位的角度（°）：yaw 水平旋转，pitch 俯仰 */
float attitude_get_yaw(void);
float attitude_get_pitch(void);

/* 原始值与物理量，供串口调试和轴向标定使用 */
const mpu6050_raw_t  *attitude_get_raw(void);
const mpu6050_data_t *attitude_get_data(void);
float attitude_get_gyro_bias_avg(void);     /* 零偏大小，判断校准是否可信 */

/* 静态零偏是否已校准成功 */
uint8_t attitude_is_calibrated(void);

#ifdef __cplusplus
}
#endif

#endif /* __ATTITUDE_H */
