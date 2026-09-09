/**
  ******************************************************************************
  * @file    can.c
  * @brief   通用 CAN 驱动（HAL 封装，业务无关）
  *
  * 提供的接口见 can.h。默认配置：CAN1，标准帧，自动重传，波特率由调用方给出（125k/250k/500k/1M）。
  * 引脚：PA11 = CAN1_RX，PA12 = CAN1_TX（默认映射，无需重映射）。
  * 接收：FIFO0 + 中断 → 环形队列；上层可轮询 can_recv() 或注册回调。
  ******************************************************************************
  */
#include "can.h"

/* ============================ 硬件相关宏 ============================ */
#define CAN_GPIO_PORT   GPIOA
#define CAN_TX_PIN      GPIO_PIN_12          /* CAN1_TX */
#define CAN_RX_PIN      GPIO_PIN_11          /* CAN1_RX */
#define CAN_GPIO_CLK    __HAL_RCC_GPIOA_CLK_ENABLE()
#define CAN_PERIPH_CLK  __HAL_RCC_CAN1_CLK_ENABLE()

/* ============================ 位时序表 ============================ */
/* APB1 = 36MHz；波特率 = 36MHz / (presc × (1+bs1+bs2)) */
typedef struct { uint8_t presc; uint8_t bs1; uint8_t bs2; uint8_t sjw; } can_timing_t;

static uint8_t can_timing_lookup(uint32_t baudrate, can_timing_t *t)
{
    switch (baudrate)
    {
    case 1000000u: t->presc = 4;  t->bs1 = 5; t->bs2 = 3; t->sjw = 1; return 0;
    case  500000u: t->presc = 8;  t->bs1 = 5; t->bs2 = 3; t->sjw = 1; return 0;
    case  250000u: t->presc = 16; t->bs1 = 5; t->bs2 = 3; t->sjw = 1; return 0;
    case  125000u: t->presc = 32; t->bs1 = 5; t->bs2 = 3; t->sjw = 1; return 0;
    default:      return 1;                             /* 不支持的波特率 */
    }
}

/* ============================ 私有变量 ============================ */
CAN_HandleTypeDef hcan1;

#define CAN_RX_Q_DEPTH 8u
static can_msg_t      rx_q[CAN_RX_Q_DEPTH];
static volatile uint8_t rx_q_head = 0u;   /* 写入位置（ISR） */
static volatile uint8_t rx_q_tail = 0u;   /* 读取位置（主循环） */

static void (*rx_cb)(const can_msg_t *msg) = 0;

/* ============================ 内部实现 ============================ */

/* 收到 FIFO0 帧（在 CAN 接收中断上下文被 HAL 调用） */
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    CAN_RxHeaderTypeDef rx;
    can_msg_t           m;
    uint8_t             next;

    if (hcan->Instance != CAN1)
    {
        return;
    }

    if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &rx, m.data) != HAL_OK)
    {
        return;
    }

    m.id  = (rx.IDE == CAN_ID_EXT) ? rx.ExtId : rx.StdId;
    m.ide = (uint8_t)(rx.IDE == CAN_ID_EXT);
    m.rtr = (uint8_t)(rx.RTR == CAN_RTR_REMOTE);
    m.dlc = rx.DLC;

    /* 入队（覆盖最旧丢弃，保证队列不溢出） */
    next = (uint8_t)((rx_q_head + 1u) % CAN_RX_Q_DEPTH);
    if (next != rx_q_tail)
    {
        rx_q[rx_q_head] = m;
        rx_q_head       = next;
    }

    if (rx_cb)
    {
        rx_cb(&m);
    }
}

/* ============================ 对外接口 ============================ */

uint8_t can_init(uint32_t baudrate)
{
    CAN_FilterTypeDef filter;
    can_timing_t      t;

    if (can_timing_lookup(baudrate, &t) != 0)
    {
        return 1;
    }

    hcan1.Instance = CAN1;
    hcan1.Init.Mode                  = CAN_MODE_SEL;
    hcan1.Init.SyncJumpWidth         = (t.sjw == 2) ? CAN_SJW_2TQ : CAN_SJW_1TQ;
    hcan1.Init.TimeSeg1              = (t.bs1 == 5) ? CAN_BS1_5TQ : (t.bs1 == 4) ? CAN_BS1_4TQ : CAN_BS1_8TQ;
    hcan1.Init.TimeSeg2              = (t.bs2 == 3) ? CAN_BS2_3TQ : (t.bs2 == 2) ? CAN_BS2_2TQ : CAN_BS2_8TQ;
    hcan1.Init.Prescaler             = t.presc;
    hcan1.Init.TimeTriggeredMode     = DISABLE;
    hcan1.Init.AutoBusOff            = ENABLE;      /* 自动退出总线关闭 */
    hcan1.Init.AutoWakeUp            = DISABLE;
    hcan1.Init.AutoRetransmission    = ENABLE;      /* 出错自动重传 */
    hcan1.Init.ReceiveFifoLocked     = DISABLE;
    hcan1.Init.TransmitFifoPriority  = DISABLE;

    if (HAL_CAN_Init(&hcan1) != HAL_OK)
    {
        return 1;
    }

    /* 筛选器：默认全收（掩码 0 = 不关心任何位），业务层再按 ID 软件过滤 */
    filter.FilterBank            = 0;
    filter.FilterMode            = CAN_FILTERMODE_IDMASK;
    filter.FilterScale           = CAN_FILTERSCALE_32BIT;
    filter.FilterIdHigh          = 0x0000;
    filter.FilterIdLow           = 0x0000;
    filter.FilterMaskIdHigh      = 0x0000;
    filter.FilterMaskIdLow       = 0x0000;
    filter.FilterFIFOAssignment  = CAN_RX_FIFO0;
    filter.FilterActivation      = ENABLE;
    filter.SlaveStartFilterBank  = 14;
    if (HAL_CAN_ConfigFilter(&hcan1, &filter) != HAL_OK)
    {
        return 1;
    }

    if (HAL_CAN_Start(&hcan1) != HAL_OK)
    {
        return 1;
    }

    if (HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK)
    {
        return 1;
    }
    return 0;
}

uint8_t can_send(uint16_t std_id, const uint8_t *data, uint8_t len)
{
    CAN_TxHeaderTypeDef tx;
    uint32_t            mail;
    uint32_t            t0;

    if (len > 8u)
    {
        len = 8u;
    }

    tx.StdId              = std_id;
    tx.ExtId              = 0;
    tx.RTR                = CAN_RTR_DATA;
    tx.IDE                = CAN_ID_STD;
    tx.DLC                = len;
    tx.TransmitGlobalTime = DISABLE;

    /* 等一个空闲邮箱（最多 10ms），满了则认为总线忙/异常 */
    t0 = HAL_GetTick();
    while (HAL_CAN_GetTxMailboxesFreeLevel(&hcan1) == 0u)
    {
        if ((HAL_GetTick() - t0) > 10u)
        {
            return 1;
        }
    }

    if (HAL_CAN_AddTxMessage(&hcan1, &tx, (uint8_t *)data, &mail) != HAL_OK)
    {
        return 1;
    }

    /* 等该邮箱发完（最多 10ms） */
    t0 = HAL_GetTick();
    while (HAL_CAN_GetTxMailboxesFreeLevel(&hcan1) < 3u)
    {
        if ((HAL_GetTick() - t0) > 10u)
        {
            break;
        }
    }
    return 0;
}

uint8_t can_recv(can_msg_t *msg)
{
    uint8_t ok = 0;

    __disable_irq();
    if (rx_q_tail != rx_q_head)
    {
        *msg      = rx_q[rx_q_tail];
        rx_q_tail = (uint8_t)((rx_q_tail + 1u) % CAN_RX_Q_DEPTH);
        ok        = 1;
    }
    __enable_irq();
    return ok ? 0u : 1u;
}

uint8_t can_set_filter(uint16_t std_id, uint16_t mask)
{
    CAN_FilterTypeDef filter;

    /* F1 内部 32 位筛选字：标准 ID 位于 bit[31:21]，bit2=IDE，bit1=RTR，bit0=0 */
    uint32_t idw = ((uint32_t)(std_id & 0x7FFu)) << 21;
    uint32_t msk = ((uint32_t)(mask  & 0x7FFu)) << 21;

    filter.FilterBank           = 0;
    filter.FilterMode           = CAN_FILTERMODE_IDMASK;
    filter.FilterScale          = CAN_FILTERSCALE_32BIT;
    filter.FilterIdHigh         = (uint16_t)(idw >> 16);
    filter.FilterIdLow          = (uint16_t)(idw & 0xFFFFu);
    filter.FilterMaskIdHigh     = (uint16_t)(msk >> 16);
    filter.FilterMaskIdLow      = (uint16_t)(msk & 0xFFFFu);
    filter.FilterFIFOAssignment = CAN_RX_FIFO0;
    filter.FilterActivation     = ENABLE;
    filter.SlaveStartFilterBank = 14;

    return (HAL_CAN_ConfigFilter(&hcan1, &filter) == HAL_OK) ? 0u : 1u;
}

void can_register_rx_callback(void (*cb)(const can_msg_t *msg))
{
    rx_cb = cb;
}

/* 轮询接收：主循环调用，把 FIFO0 取空。与中断并用时不会重复派发（谁先取走算谁的） */
void can_poll(void)
{
    CAN_RxHeaderTypeDef rx;
    can_msg_t           m;
    uint8_t             next;

    while (HAL_CAN_GetRxFifoFillLevel(&hcan1, CAN_RX_FIFO0) > 0u)
    {
        if (HAL_CAN_GetRxMessage(&hcan1, CAN_RX_FIFO0, &rx, m.data) != HAL_OK)
        {
            break;
        }

        m.id  = (rx.IDE == CAN_ID_EXT) ? rx.ExtId : rx.StdId;
        m.ide = (uint8_t)(rx.IDE == CAN_ID_EXT);
        m.rtr = (uint8_t)(rx.RTR == CAN_RTR_REMOTE);
        m.dlc = rx.DLC;

        next = (uint8_t)((rx_q_head + 1u) % CAN_RX_Q_DEPTH);
        if (next != rx_q_tail)
        {
            rx_q[rx_q_head] = m;
            rx_q_head       = next;
        }

        if (rx_cb)
        {
            rx_cb(&m);
        }
    }
}

uint8_t can_is_busoff(void)
{
    return (hcan1.Instance->ESR & CAN_ESR_BOFF) ? 1u : 0u;
}

void can_recover(void)
{
    HAL_CAN_Stop(&hcan1);
    HAL_CAN_ResetError(&hcan1);
    HAL_CAN_Start(&hcan1);
}

/* 供 stm32f1xx_it.c 调用 */
void can_irq_handler(void)
{
    HAL_CAN_IRQHandler(&hcan1);
}
