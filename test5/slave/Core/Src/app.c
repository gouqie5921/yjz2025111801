/**
  ******************************************************************************
  * @file    app.c
  * @brief   舵机执行端应用层实现
  *
  *  数据流：
  *   HC-05 --(USART1 115200)--> ParseTask --(消息队列)--> ServoTask --> TIM3 PWM --> 两个 SG90
  *
  *  为什么用队列：解析是"突发的"（每 20ms 来 9 个字节），舵机是"周期的"。
  *  用队列把两者解耦，ServoTask 永远只在固定时刻取"最新"的一帧，既不丢节奏、
  *  也不会因为串口来的时序抖动而让舵机抖动。
  ******************************************************************************
  */
#include "app.h"
#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "usart.h"
#include "gpio.h"
#include "protocol.h"
#include "servo.h"
#include "dbg.h"

/* Private variables ---------------------------------------------------------*/
static osMutexId_t    s_lock = NULL;
static osMessageQueueId_t s_cmd_q = NULL;
static app_state_t    s_st;
static uint32_t       s_last_rx_tick = 0u;
static uint8_t        s_ever_rx = 0u;

static osThreadId_t s_th_servo = NULL;
static osThreadId_t s_th_parse = NULL;
static osThreadId_t s_th_link  = NULL;
static osThreadId_t s_th_led   = NULL;
static osThreadId_t s_th_dbg   = NULL;

static const osThreadAttr_t s_attr_servo = {
    .name = "ServoTask",
    .stack_size = APP_STACK_SERVO * 4u,
    .priority = (osPriority_t)osPriorityAboveNormal,
};
static const osThreadAttr_t s_attr_parse = {
    .name = "ParseTask",
    .stack_size = APP_STACK_PARSE * 4u,
    .priority = (osPriority_t)osPriorityNormal,
};
static const osThreadAttr_t s_attr_link = {
    .name = "LinkTask",
    .stack_size = APP_STACK_LINK * 4u,
    .priority = (osPriority_t)osPriorityLow,
};
static const osThreadAttr_t s_attr_led = {
    .name = "LedTask",
    .stack_size = APP_STACK_LED * 4u,
    .priority = (osPriority_t)osPriorityLow,
};
static const osThreadAttr_t s_attr_dbg = {
    .name = "DbgTask",
    .stack_size = APP_STACK_DBG * 4u,
    .priority = (osPriority_t)osPriorityLow,
};

static void servo_task(void *argument);
static void parse_task(void *argument);
static void link_task(void *argument);
static void led_task(void *argument);
static void dbg_task(void *argument);

/* Private functions ---------------------------------------------------------*/
static uint32_t tick_next(uint32_t prev, uint32_t period)
{
    uint32_t next = prev + period;
    uint32_t now  = osKernelGetTickCount();

    if ((int32_t)(now - next) > (int32_t)(5u * period))
    {
        next = now + period;
    }
    return next;
}

static void lock(void)
{
    if (s_lock != NULL)
    {
        (void)osMutexAcquire(s_lock, osWaitForever);
    }
}

static void unlock(void)
{
    if (s_lock != NULL)
    {
        (void)osMutexRelease(s_lock);
    }
}

/* Public functions ----------------------------------------------------------*/
uint8_t app_init(void)
{
    const osMutexAttr_t   mtx_attr = { .name = "appLock" };
    const osMessageQueueAttr_t q_attr = { .name = "cmdQueue" };

    s_st.link_ok  = 0u;
    s_st.mode     = PROTO_MODE_POT;
    s_st.ok_cnt   = 0u;
    s_st.err_cnt  = 0u;
    s_st.age_ms   = 0u;
    s_st.pan_cmd  = SERVO_ANGLE_CENTER;
    s_st.tilt_cmd = SERVO_ANGLE_CENTER;
    s_st.pan_act  = SERVO_ANGLE_CENTER;
    s_st.tilt_act = SERVO_ANGLE_CENTER;

    s_lock  = osMutexNew(&mtx_attr);
    s_cmd_q = osMessageQueueNew(APP_CMD_QUEUE_LEN, sizeof(proto_cmd_t), &q_attr);

    if ((s_lock == NULL) || (s_cmd_q == NULL))
    {
        return 1u;
    }
    return 0u;
}

void app_start_periph(void)
{
    servo_init();
}

void app_create_tasks(void)
{
    s_th_servo = osThreadNew(servo_task, NULL, &s_attr_servo);
    s_th_parse = osThreadNew(parse_task, NULL, &s_attr_parse);
    s_th_link  = osThreadNew(link_task,  NULL, &s_attr_link);
    s_th_led   = osThreadNew(led_task,   NULL, &s_attr_led);
#if (APP_DBG_ENABLE == 1u)
    s_th_dbg   = osThreadNew(dbg_task,   NULL, &s_attr_dbg);
#endif
}

void app_get_state(app_state_t *st)
{
    if (st == NULL)
    {
        return;
    }
    lock();
    *st = s_st;
    unlock();
}

/* ---------------- ParseTask：串口收字节 + 协议解析 ---------------- */
static void parse_task(void *argument)
{
    proto_rx_t  rx;
    proto_cmd_t cmd;
    uint8_t     b;
    (void)argument;

    proto_rx_init(&rx);

    for (;;)
    {
        HAL_StatusTypeDef r = HAL_UART_Receive(&huart1, &b, 1u, APP_RX_POLL_MS);

        if (r == HAL_OK)
        {
            if (proto_rx_byte(&rx, b, &cmd) != 0u)
            {
                /* 收到一个校验正确的完整帧 */
                s_last_rx_tick = osKernelGetTickCount();
                s_ever_rx      = 1u;

                /* 队列满就丢掉这一帧（20ms 一帧，下一帧马上又来） */
                (void)osMessageQueuePut(s_cmd_q, &cmd, 0u, 0u);

                lock();
                s_st.ok_cnt  = rx.ok_cnt;
                s_st.err_cnt = rx.err_cnt;
                s_st.mode    = cmd.mode;
                unlock();
            }
        }
        else if (r == HAL_ERROR)
        {
            /* 串口溢出等错误必须清标志，否则后面永远收不到数据 */
            __HAL_UART_CLEAR_OREFLAG(&huart1);
            lock();
            s_st.err_cnt = rx.err_cnt + 1u;
            unlock();
            (void)osDelay(1u);      /* 让出 CPU，见下方说明 */
        }
        else
        {
            /* HAL_TIMEOUT：这段时间没数据。
               关键：本任务是"纯轮询"、永不阻塞，如果不主动让出 CPU，
               优先级更低的 LinkTask/LedTask/DbgTask 会被永久饿死
               （表现为 LED 一直快闪、串口没输出、link_ok 恒为 0）。
               收到字节的路径不让出，保证一帧 9 字节能连续快速读完。 */
            (void)osDelay(1u);
        }
    }
}

/* ---------------- ServoTask：取最新目标角 + 限速驱动 ----------------
   APP_SELF_TEST=1 时不做协议解析，改为分三段小幅慢摆，用于分离
   "程序/PWM/接线" 与 "机械装配/舵机本体" 的问题。 */
static void servo_task(void *argument)
{
    uint32_t wake  = osKernelGetTickCount();
    uint32_t last  = wake;
#if (APP_SELF_TEST == 1u)
    static const float   seq[4]  = { APP_SELFTEST_LO, SERVO_ANGLE_CENTER,
                                     APP_SELFTEST_HI, SERVO_ANGLE_CENTER };
    static const uint8_t mask[3] = { 0x1u, 0x2u, 0x3u };  /* 只水平 / 只俯仰 / 两路一起 */
    uint32_t dwell = 0u;
    uint8_t  ph    = 0u;
    uint8_t  si    = 0u;
    dbg_str("SELFTEST phase 0 (only PAN PA6)\r\n");
#endif
    (void)argument;

    for (;;)
    {
        uint32_t now = osKernelGetTickCount();
        float    dt  = (float)(now - last) * 0.001f;

        last = now;
        if ((dt <= 0.0f) || (dt > 0.5f))
        {
            dt = (float)APP_SERVO_PERIOD_MS * 0.001f;
        }

#if (APP_SELF_TEST == 1u)
        dwell += APP_SERVO_PERIOD_MS;
        if (dwell >= APP_SELFTEST_STEP_MS)
        {
            dwell = 0u;
            si++;

            if (si >= 4u)
            {
                si = 0u;
                ph = (uint8_t)((ph + 1u) % 3u);

                /* 换阶段：两路先回中（限速，不会猛甩），再只动本阶段该动的那一路 */
                servo_set_target(SERVO_CH_PAN,  SERVO_ANGLE_CENTER);
                servo_set_target(SERVO_CH_TILT, SERVO_ANGLE_CENTER);

                dbg_str("SELFTEST phase ");
                dbg_u32(ph);
                dbg_str((ph == 0u) ? " (only PAN PA6)"
                                   : ((ph == 1u) ? " (only TILT PA7)" : " (both)"));
                dbg_nl();
            }

            if ((mask[ph] & 0x1u) != 0u) { servo_set_target(SERVO_CH_PAN,  seq[si]); }
            if ((mask[ph] & 0x2u) != 0u) { servo_set_target(SERVO_CH_TILT, seq[si]); }
        }
#else
        {
            proto_cmd_t cmd;

            /* 把队列里积压的帧全部取出来，只留最后一帧（最新的角度） */
            while (osMessageQueueGet(s_cmd_q, &cmd, NULL, 0u) == osOK)
            {
                servo_set_target(SERVO_CH_PAN,  (float)cmd.pan_x10  / 10.0f);
                servo_set_target(SERVO_CH_TILT, (float)cmd.tilt_x10 / 10.0f);
            }
        }
#endif

        /* 按转速限制逼近目标，写 CCR */
        servo_update(dt);

        lock();
        s_st.pan_cmd  = servo_get_target(SERVO_CH_PAN);
        s_st.tilt_cmd = servo_get_target(SERVO_CH_TILT);
        s_st.pan_act  = servo_get_angle(SERVO_CH_PAN);
        s_st.tilt_act = servo_get_angle(SERVO_CH_TILT);
        unlock();

        wake = tick_next(wake, APP_SERVO_PERIOD_MS);
        (void)osDelayUntil(wake);
    }
}

/* ---------------- LinkTask：失联监视 ---------------- */
static void link_task(void *argument)
{
    uint32_t wake = osKernelGetTickCount();
    (void)argument;

    for (;;)
    {
        uint32_t now  = osKernelGetTickCount();
        uint32_t age  = now - s_last_rx_tick;
        uint8_t  ok   = 0u;

        /* 开机后一直没收到过帧，或超过 200ms 没有新帧 -> 失联 */
        if ((s_ever_rx != 0u) && (age < APP_LINK_TIMEOUT_MS))
        {
            ok = 1u;
        }

        lock();
        s_st.age_ms  = age;
        s_st.link_ok = ok;
        unlock();

        /* 失联时不做任何动作：舵机保持当前位置（比乱动安全） */

        wake = tick_next(wake, APP_LINK_PERIOD_MS);
        (void)osDelayUntil(wake);
    }
}

/* ---------------- LedTask：通信状态指示 ---------------- */
static void led_task(void *argument)
{
    uint32_t wake = osKernelGetTickCount();
    uint32_t n    = 0u;
    (void)argument;

    for (;;)
    {
        app_state_t st;
        uint8_t     on;

        app_get_state(&st);

        if (st.link_ok != 0u)
        {
            on = (uint8_t)((n / 10u) % 2u);     /* 500ms 慢闪：通信正常 */
        }
        else
        {
            on = (uint8_t)((n / 2u) % 2u);      /* 100ms 快闪：失联 */
        }

        HAL_GPIO_WritePin(APP_LED_GPIO_Port, APP_LED_Pin,
                          (on != 0u) ? GPIO_PIN_RESET : GPIO_PIN_SET);

        n++;
        wake = tick_next(wake, APP_LED_PERIOD_MS);
        (void)osDelayUntil(wake);
    }
}

/* ---------------- DbgTask：串口2 打印 ---------------- */
#if (APP_DBG_ENABLE == 1u)
static void dbg_task(void *argument)
{
    uint32_t wake = osKernelGetTickCount();
    uint32_t cnt  = 0u;
    (void)argument;

    for (;;)
    {
        app_state_t st;

        app_get_state(&st);

        if ((cnt % 16u) == 0u)
        {
            dbg_str("HEAP ");
            dbg_u32((uint32_t)xPortGetFreeHeapSize());
            dbg_str("/");
            dbg_u32((uint32_t)configTOTAL_HEAP_SIZE);
            dbg_nl();
        }
        cnt++;

        dbg_str("LINK=");
        dbg_u32(st.link_ok);
        dbg_str(" AGE=");
        dbg_u32(st.age_ms);
        dbg_str("ms MODE=");
        dbg_u32(st.mode);
        dbg_str(" OK=");
        dbg_u32(st.ok_cnt);
        dbg_str(" ERR=");
        dbg_u32(st.err_cnt);

        dbg_str(" | CMD ");
        dbg_f(st.pan_cmd, 1u);
        dbg_str(" ");
        dbg_f(st.tilt_cmd, 1u);

        dbg_str(" | ACT ");
        dbg_f(st.pan_act, 1u);
        dbg_str(" ");
        dbg_f(st.tilt_act, 1u);
        dbg_nl();

        wake = tick_next(wake, APP_DBG_PERIOD_MS);
        (void)osDelayUntil(wake);
    }
}
#endif
