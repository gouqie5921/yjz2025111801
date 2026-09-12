/**
  ******************************************************************************
  * @file    app.c
  * @brief   主控端应用层实现
  *
  *  数据流：
  *   电位器(PA1/PA4) --AdcTask--> s_st.pot_pan/tilt  \
  *                                                    >--SendTask--> 协议帧 --> HC-05
  *   MPU6050(I2C1)   --SensorTask--> s_st.gyro_pan/tilt/
  *
  *  两个来源都放在共享结构体里，用互斥锁保护；SendTask 按 s_st.mode 选一个下发。
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
#include "knob.h"
#include "attitude.h"
#include "dbg.h"

/* Private variables ---------------------------------------------------------*/
static osMutexId_t s_lock = NULL;
static app_state_t s_st;

static osThreadId_t s_th_sensor = NULL;
static osThreadId_t s_th_adc    = NULL;
static osThreadId_t s_th_send   = NULL;
static osThreadId_t s_th_key    = NULL;
static osThreadId_t s_th_led    = NULL;
static osThreadId_t s_th_dbg    = NULL;

static const osThreadAttr_t s_attr_sensor = {
    .name = "SensorTask",
    .stack_size = APP_STACK_SENSOR * 4u,
    .priority = (osPriority_t)osPriorityAboveNormal,
};
static const osThreadAttr_t s_attr_adc = {
    .name = "AdcTask",
    .stack_size = APP_STACK_ADC * 4u,
    .priority = (osPriority_t)osPriorityNormal,
};
static const osThreadAttr_t s_attr_send = {
    .name = "SendTask",
    .stack_size = APP_STACK_SEND * 4u,
    .priority = (osPriority_t)osPriorityNormal,
};
static const osThreadAttr_t s_attr_key = {
    .name = "KeyTask",
    .stack_size = APP_STACK_KEY * 4u,
    .priority = (osPriority_t)osPriorityNormal,
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

static void sensor_task(void *argument);
static void adc_task(void *argument);
static void send_task(void *argument);
static void key_task(void *argument);
static void led_task(void *argument);
static void dbg_task(void *argument);

/* Private functions ---------------------------------------------------------*/
static float clampf(float v, float lo, float hi)
{
    if (v < lo) { return lo; }
    if (v > hi) { return hi; }
    return v;
}

/* 周期对齐：返回下一次唤醒的绝对 tick。落后太多就重新对齐，避免"追赶"式连发 */
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
    const osMutexAttr_t mtx_attr = { .name = "appLock" };

    s_st.mode      = PROTO_MODE_POT;    /* 上电默认用电位器，方便直接看现象 */
    s_st.imu_ok    = 0u;
    s_st.pan       = APP_GYRO_CENTER;
    s_st.tilt      = APP_GYRO_CENTER;
    s_st.pot_pan   = APP_GYRO_CENTER;
    s_st.pot_tilt  = APP_GYRO_CENTER;
    s_st.gyro_pan  = APP_GYRO_CENTER;
    s_st.gyro_tilt = APP_GYRO_CENTER;
    s_st.yaw_raw   = 0.0f;
    s_st.pitch_raw = 0.0f;

    s_lock = osMutexNew(&mtx_attr);
    return (s_lock == NULL) ? 1u : 0u;
}

void app_start_periph(void)
{
    knob_start();
}

void app_create_tasks(void)
{
    s_th_sensor = osThreadNew(sensor_task, NULL, &s_attr_sensor);
    s_th_adc    = osThreadNew(adc_task,    NULL, &s_attr_adc);
    s_th_send   = osThreadNew(send_task,   NULL, &s_attr_send);
    s_th_key    = osThreadNew(key_task,    NULL, &s_attr_key);
    s_th_led    = osThreadNew(led_task,    NULL, &s_attr_led);
#if (APP_DBG_ENABLE == 1u)
    s_th_dbg    = osThreadNew(dbg_task,    NULL, &s_attr_dbg);
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

void app_get_cmd(float *pan, float *tilt, uint8_t *mode)
{
    lock();

    if (s_st.mode == PROTO_MODE_GYRO)
    {
        *pan  = s_st.gyro_pan;
        *tilt = s_st.gyro_tilt;
    }
    else
    {
        *pan  = s_st.pot_pan;
        *tilt = s_st.pot_tilt;
    }
    *mode = s_st.mode;

    unlock();
}

/* ---------------- SensorTask：MPU6050 + 姿态解算 ---------------- */
static void sensor_task(void *argument)
{
    uint32_t wake   = osKernelGetTickCount();
    uint32_t last   = wake;
    uint32_t retry    = 0u;
    uint8_t  imu_ok   = 0u;         /* 本任务的私有副本，发布时才加锁写 s_st */
    uint8_t  imu_fail = 0u;         /* 连续读失败计数 */
    (void)argument;

    for (;;)
    {
        uint32_t now = osKernelGetTickCount();
        float    dt  = (float)(now - last) * 0.001f;

        last = now;
        if ((dt <= 0.0f) || (dt > 0.5f))
        {
            dt = (float)APP_SENSOR_PERIOD_MS * 0.001f;
        }

        if (imu_ok == 0u)
        {
            /* 没插好 / 接触不良时定期重试，插好了自动恢复，不用复位 */
            if (retry == 0u)
            {
                if (attitude_init() == ATT_OK)
                {
                    attitude_rebase();
                    imu_ok = 1u;
                    lock();
                    s_st.imu_ok = 1u;
                    unlock();
                }
                retry = APP_IMU_RETRY_MS;
            }
            else if (retry >= APP_SENSOR_PERIOD_MS)
            {
                retry -= APP_SENSOR_PERIOD_MS;
            }
            else
            {
                retry = 0u;
            }
        }
        else if (attitude_update(dt) != ATT_OK)
        {
            /* 偶发一次读失败不算掉线（长线/干扰常见）：
               连续失败 APP_IMU_FAIL_LOST 次（约 50ms）才认定为 IMU 异常 */
            if (imu_fail < APP_IMU_FAIL_LOST)
            {
                imu_fail++;
            }
            if (imu_fail >= APP_IMU_FAIL_LOST)
            {
                imu_ok = 0u;
                retry  = APP_IMU_RETRY_MS;
                lock();
                s_st.imu_ok = 0u;
                unlock();
            }
        }
        else
        {
            float yaw;
            float pitch;
            float pan_cmd;
            float tlt_cmd;

            imu_fail = 0u;

            /* 软饱和：超出可用行程时把零位一起挪，保证反向立刻有响应 */
            attitude_limit_yaw(APP_GYRO_RANGE / APP_GYRO_PAN_GAIN);
            attitude_limit_pitch(APP_GYRO_RANGE / APP_GYRO_TILT_GAIN);

            yaw     = attitude_get_yaw();
            pitch   = attitude_get_pitch();
            pan_cmd = APP_GYRO_CENTER + (APP_GYRO_PAN_GAIN * yaw);
            tlt_cmd = APP_GYRO_CENTER + (APP_GYRO_TILT_GAIN * pitch);

            pan_cmd = clampf(pan_cmd, APP_GYRO_CENTER - APP_GYRO_LIMIT,
                                      APP_GYRO_CENTER + APP_GYRO_LIMIT);
            tlt_cmd = clampf(tlt_cmd, APP_GYRO_CENTER - APP_GYRO_LIMIT,
                                      APP_GYRO_CENTER + APP_GYRO_LIMIT);

            lock();
            s_st.gyro_pan  = pan_cmd;
            s_st.gyro_tilt = tlt_cmd;
            s_st.yaw_raw   = yaw;
            s_st.pitch_raw = pitch;
            unlock();
        }

        wake = tick_next(wake, APP_SENSOR_PERIOD_MS);
        (void)osDelayUntil(wake);
    }
}

/* ---------------- AdcTask：两个电位器 ---------------- */
static void adc_task(void *argument)
{
    uint32_t wake = osKernelGetTickCount();
    (void)argument;

    for (;;)
    {
        if (knob_update() == 0u)
        {
            float pan  = knob_get_pan()->angle;
            float tilt = knob_get_tilt()->angle;

            lock();
            s_st.pot_pan  = pan;
            s_st.pot_tilt = tilt;
            unlock();
        }

        wake = tick_next(wake, APP_ADC_PERIOD_MS);
        (void)osDelayUntil(wake);
    }
}

/* ---------------- SendTask：组帧 + 蓝牙发送 ---------------- */
static void send_task(void *argument)
{
    uint8_t     buf[PROTO_FRAME_SIZE];
    proto_cmd_t cmd;
    uint32_t    wake = osKernelGetTickCount();
    (void)argument;

    for (;;)
    {
        float   pan  = APP_GYRO_CENTER;
        float   tilt = APP_GYRO_CENTER;
        uint8_t mode = PROTO_MODE_POT;
        uint8_t n;

        app_get_cmd(&pan, &tilt, &mode);

        cmd.pan_x10  = proto_deg_to_x10(pan);
        cmd.tilt_x10 = proto_deg_to_x10(tilt);
        cmd.mode     = mode;

        n = proto_build(&cmd, buf);
        if (n > 0u)
        {
            /* 20ms 一帧，9 字节 @115200 约 0.8ms，阻塞发完即可 */
            (void)HAL_UART_Transmit(&huart1, buf, (uint16_t)n, 50u);

            lock();
            s_st.pan  = pan;
            s_st.tilt = tilt;
            unlock();
        }

        wake = tick_next(wake, APP_SEND_PERIOD_MS);
        (void)osDelayUntil(wake);
    }
}

/* ---------------- KeyTask：切换控制方式 ---------------- */
static void key_task(void *argument)
{
    uint32_t wake = osKernelGetTickCount();
    uint8_t  cnt  = 0u;
    uint8_t  prev = 0u;             /* 0 = 松开, 1 = 按下 */
    (void)argument;

    for (;;)
    {
        /* PA0 上拉，按下为低电平 */
        uint8_t cur = (HAL_GPIO_ReadPin(APP_KEY_GPIO_Port, APP_KEY_Pin) == GPIO_PIN_RESET)
                      ? 1u : 0u;

        if (cur != prev)
        {
            prev = cur;
            cnt  = 0u;
        }
        else if (cnt < APP_KEY_DEBOUNCE_CNT)
        {
            cnt++;
            if ((cnt == APP_KEY_DEBOUNCE_CNT) && (cur == 1u))
            {
                /* 确认按下一次：切换控制方式 */
                uint8_t new_mode;

                lock();
                new_mode = (s_st.mode == PROTO_MODE_POT) ? PROTO_MODE_GYRO : PROTO_MODE_POT;

                /* IMU 异常时不允许切到陀螺仪模式，免得云台没反应还以为程序死了 */
                if ((new_mode == PROTO_MODE_GYRO) && (s_st.imu_ok == 0u))
                {
                    new_mode = PROTO_MODE_POT;
                }
                s_st.mode = new_mode;
                unlock();

                if (new_mode == PROTO_MODE_GYRO)
                {
                    /* 以"拿起面包板时的姿态"为零位，云台先回中再跟随 */
                    attitude_rebase();
                }
            }
        }

        wake = tick_next(wake, APP_KEY_PERIOD_MS);
        (void)osDelayUntil(wake);
    }
}

/* ---------------- LedTask：状态指示 ---------------- */
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

        if (st.imu_ok == 0u)
        {
            /* IMU 异常用"双闪两下 + 长停"（周期 400ms），
               和陀螺仪模式的单次快闪（100ms）明确区分开 */
            uint8_t p = (uint8_t)(n % 8u);
            on = (uint8_t)((p == 0u) || (p == 2u));
        }
        else if (st.mode == PROTO_MODE_POT)
        {
            on = (uint8_t)((n % 20u) < 10u);            /* 亮 500ms 灭 500ms：电位器模式 */
        }
        else
        {
            on = (uint8_t)((n % 4u) < 2u);              /* 亮 100ms 灭 100ms：陀螺仪模式 */
        }

        /* 蓝丸板载 LED 接 PC13，低电平点亮 */
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
        const mpu6050_raw_t *raw;

        app_get_state(&st);
        raw = attitude_get_raw();

        /* 每约 4 秒报一次剩余堆，用来验证任务栈够不够 */
        if ((cnt % 16u) == 0u)
        {
            dbg_str("HEAP ");
            dbg_u32((uint32_t)xPortGetFreeHeapSize());
            dbg_str("/");
            dbg_u32((uint32_t)configTOTAL_HEAP_SIZE);
            dbg_nl();
        }
        cnt++;

        dbg_str("MODE=");
        dbg_u32(st.mode);
        dbg_str(" IMU=");
        dbg_u32(st.imu_ok);
        dbg_str(" ID=");
        dbg_hex8(mpu6050_get_whoami());

        dbg_str(" PAN=");
        dbg_f(st.pan, 1u);
        dbg_str(" TILT=");
        dbg_f(st.tilt, 1u);

        dbg_str(" | POT ");
        dbg_f(st.pot_pan, 1u);
        dbg_str(" ");
        dbg_f(st.pot_tilt, 1u);

        dbg_str(" | GYRO ");
        dbg_f(st.gyro_pan, 1u);
        dbg_str(" ");
        dbg_f(st.gyro_tilt, 1u);

        dbg_str(" | YAW ");
        dbg_f(st.yaw_raw, 1u);
        dbg_str(" PIT ");
        dbg_f(st.pitch_raw, 1u);

        /* 陀螺轴向标定用：转动面包板时看哪两个轴变化最大 */
        dbg_str(" | RAW A ");
        dbg_i32(raw->ax);
        dbg_str(" ");
        dbg_i32(raw->ay);
        dbg_str(" ");
        dbg_i32(raw->az);
        dbg_str(" G ");
        dbg_i32(raw->gx);
        dbg_str(" ");
        dbg_i32(raw->gy);
        dbg_str(" ");
        dbg_i32(raw->gz);

        dbg_nl();

        wake = tick_next(wake, APP_DBG_PERIOD_MS);
        (void)osDelayUntil(wake);
    }
}
#endif
