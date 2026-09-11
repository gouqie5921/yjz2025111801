/**
  ******************************************************************************
  * @file    protocol.c
  * @brief   通信协议组帧 / 状态机解析实现（主机、从机共用同一份代码）
  *
  *  帧格式（固定 9 字节）：
  *   [0]=0xAA [1]=0x55 [2]=LEN(6) [3]=PAN_H [4]=PAN_L
  *   [5]=TILT_H [6]=TILT_L [7]=MODE [8]=CHK
  *  CHK = buf[2..7] 六个字节逐字节异或
  ******************************************************************************
  */
#include "protocol.h"

static uint8_t checksum(const uint8_t *p, uint8_t len)
{
    uint8_t x = 0u;
    uint8_t i;

    for (i = 0u; i < len; i++)
    {
        x ^= p[i];
    }
    return x;
}

uint16_t proto_deg_to_x10(float deg)
{
    float v = deg * 10.0f;

    if (v < 0.0f)      { v = 0.0f; }
    if (v > 1800.0f)   { v = 1800.0f; }

    return (uint16_t)(v + 0.5f);
}

uint8_t proto_build(const proto_cmd_t *cmd, uint8_t *buf)
{
    uint16_t pan_v;
    uint16_t tilt_v;

    if ((cmd == 0) || (buf == 0))
    {
        return 0u;
    }

    pan_v  = (cmd->pan_x10  > 1800u) ? 1800u : cmd->pan_x10;
    tilt_v = (cmd->tilt_x10 > 1800u) ? 1800u : cmd->tilt_x10;

    buf[0] = PROTO_HEAD1;
    buf[1] = PROTO_HEAD2;
    buf[2] = PROTO_LEN_FIELD;
    buf[3] = (uint8_t)(pan_v >> 8);
    buf[4] = (uint8_t)(pan_v & 0xFFu);
    buf[5] = (uint8_t)(tilt_v >> 8);
    buf[6] = (uint8_t)(tilt_v & 0xFFu);
    buf[7] = (cmd->mode == PROTO_MODE_GYRO) ? PROTO_MODE_GYRO : PROTO_MODE_POT;

    /* 校验范围：LEN..MODE，即 buf[2..7]，共 6 字节 */
    buf[8] = checksum(&buf[2], 6u);

    return PROTO_FRAME_SIZE;
}

void proto_rx_init(proto_rx_t *rx)
{
    uint8_t i;

    if (rx == 0)
    {
        return;
    }

    rx->state   = PROTO_RX_WAIT_H1;
    rx->idx     = 0u;
    rx->ok_cnt  = 0u;
    rx->err_cnt = 0u;

    for (i = 0u; i < PROTO_FRAME_SIZE; i++)
    {
        rx->buf[i] = 0u;
    }
}

uint8_t proto_rx_byte(proto_rx_t *rx, uint8_t b, proto_cmd_t *out)
{
    if ((rx == 0) || (out == 0))
    {
        return 0u;
    }

    switch (rx->state)
    {
    case PROTO_RX_WAIT_H1:
        if (b == PROTO_HEAD1)
        {
            rx->buf[0] = b;
            rx->state  = PROTO_RX_WAIT_H2;
        }
        break;

    case PROTO_RX_WAIT_H2:
        if (b == PROTO_HEAD2)
        {
            rx->buf[1] = b;
            rx->idx    = 2u;
            rx->state  = PROTO_RX_BODY;
        }
        else if (b != PROTO_HEAD1)
        {
            rx->state = PROTO_RX_WAIT_H1;   /* 第二个字节不对，重新找帧头 */
        }
        /* b == 0xAA 时留在本状态，等下一个字节（处理 0xAA 0xAA 0x55 的情况） */
        break;

    case PROTO_RX_BODY:
    default:
        rx->buf[rx->idx] = b;
        rx->idx++;

        if (rx->idx >= PROTO_FRAME_SIZE)
        {
            uint8_t good = 0u;

            rx->state = PROTO_RX_WAIT_H1;
            rx->idx   = 0u;

            if (rx->buf[2] == PROTO_LEN_FIELD)
            {
                if (checksum(&rx->buf[2], 6u) == rx->buf[PROTO_FRAME_SIZE - 1u])
                {
                    out->pan_x10  = (uint16_t)(((uint16_t)rx->buf[3] << 8) | rx->buf[4]);
                    out->tilt_x10 = (uint16_t)(((uint16_t)rx->buf[5] << 8) | rx->buf[6]);
                    out->mode     = (rx->buf[7] == PROTO_MODE_GYRO)
                                    ? PROTO_MODE_GYRO : PROTO_MODE_POT;

                    rx->ok_cnt++;
                    good = 1u;
                }
            }

            if (good == 0u)
            {
                rx->err_cnt++;
            }

            return good;
        }
        break;
    }

    return 0u;
}
