/**
  ******************************************************************************
  * @file    protocol.h
  * @brief   主机 <-> 从机 无线（HC-05 蓝牙）通信协议
  *
  *  固定长度帧，共 9 字节：
  *  +------+------+------+-------+-------+--------+--------+------+------+
  *  | 0xAA | 0x55 | LEN  | PAN_H | PAN_L | TILT_H | TILT_L | MODE | CHK  |
  *  +------+------+------+-------+-------+--------+--------+------+------+
  *   LEN  = 6（从 LEN 到 MODE 的字节数）
  *   PAN  = 水平角 * 10，0~1800（精度 0.1°）
  *   TILT = 俯仰角 * 10，0~1800
  *   MODE = 0 电位器控制 / 1 陀螺仪姿态控制
  *   CHK  = LEN..MODE 逐字节异或
  ******************************************************************************
  */
#ifndef __PROTOCOL_H
#define __PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define PROTO_HEAD1         0xAAu
#define PROTO_HEAD2         0x55u
#define PROTO_LEN_FIELD     6u
#define PROTO_FRAME_SIZE    9u

#define PROTO_MODE_POT      0u      /* 电位器控制 */
#define PROTO_MODE_GYRO     1u      /* 陀螺仪姿态控制 */

typedef struct
{
    uint16_t pan_x10;       /* 水平角 *10 */
    uint16_t tilt_x10;      /* 俯仰角 *10 */
    uint8_t  mode;          /* PROTO_MODE_xxx */
} proto_cmd_t;

/* ---------- 发送端（主机） ---------- */
/* 组帧，buf 至少 11 字节，返回帧长度 */
uint8_t proto_build(const proto_cmd_t *cmd, uint8_t *buf);

/* 角度(float, °) -> 线协议整数（自动限幅），方便直接填 proto_cmd_t */
uint16_t proto_deg_to_x10(float deg);

/* ---------- 接收端（从机） ---------- */
typedef enum
{
    PROTO_RX_WAIT_H1 = 0,
    PROTO_RX_WAIT_H2,
    PROTO_RX_BODY
} proto_rx_state_t;

typedef struct
{
    proto_rx_state_t state;
    uint8_t  buf[PROTO_FRAME_SIZE];
    uint8_t  idx;
    uint32_t ok_cnt;            /* 收到的好帧数 */
    uint32_t err_cnt;           /* 校验错/超时丢弃的帧数 */
} proto_rx_t;

void proto_rx_init(proto_rx_t *rx);

/* 逐字节喂入。返回 1 表示解析出一个完整且校验正确的帧（已写入 out） */
uint8_t proto_rx_byte(proto_rx_t *rx, uint8_t b, proto_cmd_t *out);

#ifdef __cplusplus
}
#endif

#endif /* __PROTOCOL_H */
