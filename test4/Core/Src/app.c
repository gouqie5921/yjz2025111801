/**
  ******************************************************************************
  * @file    app.c
  * @brief   第四题《模拟风扇》应用逻辑（FreeRTOS 任务 + 队列 + 互斥锁 + PID）
  *
  *  数据流：
  *    旋钮(ADC) → KnobTask → 队列 qKnob → MotorCtrlTask(PID) → PWM → TB6612 → 电机
  *    编码器(TIM3) ─────────────────────→ MotorCtrlTask → 转速/角度 → PID
  *    MotorCtrlTask → 遥测结构(互斥锁) → VofaTask → USART1 → VOFA+
  *    KeyTask → 切换模式（定速/定位）
  ******************************************************************************
  */
#include "app.h"
#include "cmsis_os.h"
#include "gpio.h"
#include "motor.h"
#include "encoder.h"
#include "adc_knob.h"
#include "pid.h"
#include "vofa.h"

/* 按键 / LED 引脚（对应 CubeMX 里 PA0 上拉输入、PC13 输出） */
#define KEY_PORT        GPIOA
#define KEY_PIN         GPIO_PIN_0
#define LED_PORT        GPIOC
#define LED_PIN         GPIO_PIN_13

/* ============================ 任务属性 ============================ */
/* 注意：CMSIS-RTOS V2 的 stack_size 单位是【字节】 */
static const osThreadAttr_t s_attr_ctrl = { .name = "MotorCtrl", .stack_size = 192u * 4u, .priority = osPriorityHigh };
static const osThreadAttr_t s_attr_knob = { .name = "Knob",      .stack_size = 128u * 4u, .priority = osPriorityAboveNormal };
static const osThreadAttr_t s_attr_vofa = { .name = "Vofa",      .stack_size = 160u * 4u, .priority = osPriorityNormal };
static const osThreadAttr_t s_attr_key  = { .name = "Key",       .stack_size = 128u * 4u, .priority = osPriorityBelowNormal };

/* ============================ 共享对象 ============================ */
static osThreadId_t        s_task_ctrl = NULL;
static osThreadId_t        s_task_knob = NULL;
static osThreadId_t        s_task_vofa = NULL;
static osThreadId_t        s_task_key  = NULL;
static osMessageQueueId_t  s_q_knob    = NULL;      /* 传旋钮百分比 0~100（深度 1） */
static osMutexId_t         s_mtx_telem = NULL;      /* 保护遥测结构 */

typedef struct
{
    float target;
    float actual;
    float pwm;
    float error;
    float angle;        /* 累计角度(°)，任何模式下都上报，便于标定 CPR */
} telem_t;

static telem_t          s_telem;
static volatile uint8_t s_mode           = (uint8_t)APP_START_MODE;
static volatile uint8_t s_mode_reset_req = 0u;      /* 1 = 需要复位 PID / 角度基准 */

static pid_t s_pid_speed;
static pid_t s_pid_pos;

/* ============================ 初始化 ============================ */

void app_rtos_objects_init(void)
{
    s_q_knob    = osMessageQueueNew(1u, sizeof(float), NULL);
    s_mtx_telem = osMutexNew(NULL);
    vofa_init();                                    /* 串口互斥锁 */
}

void app_rtos_tasks_create(void)
{
    s_task_ctrl = osThreadNew(motor_ctrl_task, NULL, &s_attr_ctrl);
    s_task_knob = osThreadNew(knob_task,       NULL, &s_attr_knob);
    s_task_vofa = osThreadNew(vofa_task,       NULL, &s_attr_vofa);
    s_task_key  = osThreadNew(key_task,        NULL, &s_attr_key);
    /* LED/心跳任务由 CubeMX 生成的 defaultTask(StartDefaultTask) 承担 */
}

void app_peripherals_init(void)
{
    encoder_init();                                 /* 启动 TIM3 编码器接口 */
    motor_init();                                   /* 启动 TIM4 PWM + STBY 使能 */
    knob_init();                                    /* 启动 ADC1 连续转换 */
}

/* ============================ 任务 1：控制环（1ms） ============================ */

void motor_ctrl_task(void *argument)
{
    const float dt = (float)CTRL_PERIOD_MS / 1000.0f;
    uint32_t    tick = osKernelGetTickCount();
    float       speed_f   = 0.0f;                   /* 滤波后的转速 RPM */
    float       angle_deg = 0.0f;                   /* 累计角度 */
    float       pct       = 0.0f;                   /* 旋钮百分比 0~100 */
    float       target    = 0.0f;
    float       err       = 0.0f;
    float       duty      = 0.0f;

    (void)argument;

    pid_init(&s_pid_speed, PID_SPEED_KP, PID_SPEED_KI, PID_SPEED_KD,
             -(float)MOTOR_DUTY_MAX, (float)MOTOR_DUTY_MAX);
    pid_init(&s_pid_pos, PID_POS_KP, PID_POS_KI, PID_POS_KD,
             -(float)MOTOR_DUTY_MAX, (float)MOTOR_DUTY_MAX);

    for (;;)
    {
        int16_t delta;
        float   rpm;
        float   tmp;

        /* 1) 编码器 → 转速 / 角度 */
        delta = encoder_read_delta();
        rpm   = ((float)delta * 60.0f) / ((float)ENCODER_CPR * dt);
        speed_f += 0.30f * (rpm - speed_f);                     /* 一阶低通，抑制量化噪声 */
        angle_deg = encoder_total_to_deg(encoder_get_total());

        /* 2) 取旋钮最新百分比（队列深度 1；取不到就沿用上次） */
        if (osMessageQueueGet(s_q_knob, &tmp, NULL, 0u) == osOK)
        {
            pct = tmp;
        }

        /* 3) 模式切换 / 重新定位：复位 PID 与角度基准（把当前位置当 0°） */
        if (s_mode_reset_req != 0u)
        {
            s_mode_reset_req = 0u;
            pid_reset(&s_pid_speed);
            pid_reset(&s_pid_pos);
            encoder_reset_total();
            angle_deg = 0.0f;
            speed_f   = 0.0f;
        }

        /* 4) 目标值：自动阶跃测试模式 或 旋钮 ADC（采样值越高，目标转速/角度越大） */
#if APP_AUTO_STEP
        target = (((osKernelGetTickCount() / AUTO_STEP_MS) & 1u) != 0u)
                 ? AUTO_STEP_LEVEL : 0.0f;                      /* 0 ↔ LEVEL 方波 */
        (void)pct;                                              /* 自动模式下忽略旋钮 */
#else
        if (s_mode == (uint8_t)CTRL_MODE_SPEED)
        {
            target = pct * (SPEED_MAX_RPM / 100.0f);
        }
        else
        {
            target = pct * (ANGLE_MAX_DEG / 100.0f);
        }
#endif

        /* 5) PID：按模式算输出 */
        if (s_mode == (uint8_t)CTRL_MODE_SPEED)
        {
            err  = target - speed_f;
            duty = pid_compute(&s_pid_speed, target, speed_f, dt);
        }
        else
        {
            err = target - angle_deg;
            while (err > 180.0f)  { err -= 360.0f; }            /* 走最短路径 */
            while (err < -180.0f) { err += 360.0f; }
            duty = pid_compute(&s_pid_pos, angle_deg + err, angle_deg, dt);
        }

        /* 5) 【仅定速模式】目标为 0 → 停机，避免死区补偿在零速附近引起嗡嗡抖动。
              注意：定位模式下 target=0 表示"转到 0°"，必须让 PID 转回去，不能停机！ */
        if ((s_mode == (uint8_t)CTRL_MODE_SPEED) && (target <= 0.5f))
        {
            duty = 0.0f;
            pid_reset(&s_pid_speed);
            pid_reset(&s_pid_pos);
        }
        else if ((s_mode == (uint8_t)CTRL_MODE_POSITION) &&
                 (err < 3.0f) && (err > -3.0f))
        {
            duty = 0.0f;                                        /* 定位到位（±3°），保持不动 */
        }
        else
        {
            /* 死区补偿：小输出抬到最小启动占空比，否则电机不转 → PID 永远调不到 */
            if ((duty > 0.0f) && (duty < (float)MOTOR_DEADZONE))
            {
                duty = (float)MOTOR_DEADZONE;
            }
            else if ((duty < 0.0f) && (duty > -(float)MOTOR_DEADZONE))
            {
                duty = -(float)MOTOR_DEADZONE;
            }
            else
            {
                /* 保持 */
            }
        }

        /* 6) 输出到 TB6612 */
        motor_set((int16_t)duty);

        /* 7) 遥测（互斥锁保护，VofaTask 读取） */
        (void)osMutexAcquire(s_mtx_telem, osWaitForever);
        s_telem.target = target;
        s_telem.actual = (s_mode == (uint8_t)CTRL_MODE_SPEED) ? speed_f : angle_deg;
        s_telem.pwm    = duty;
        s_telem.error  = err;
        s_telem.angle  = angle_deg;
        (void)osMutexRelease(s_mtx_telem);

        /* 8) 严格 1ms 周期（用 osDelayUntil，不能用 osDelay） */
        tick += CTRL_PERIOD_MS;
        (void)osDelayUntil(tick);
    }
}

/* ============================ 任务 2：旋钮采样（20ms） ============================ */

void knob_task(void *argument)
{
    uint32_t tick = osKernelGetTickCount();

    (void)argument;

    for (;;)
    {
        float pct = knob_read_percent();                        /* 0~100 */
        (void)osMessageQueuePut(s_q_knob, &pct, 0u, 0u);

        tick += KNOB_PERIOD_MS;
        (void)osDelayUntil(tick);
    }
}

/* ============================ 任务 3：VOFA+ 上报（20ms） ============================ */

void vofa_task(void *argument)
{
    uint32_t tick = osKernelGetTickCount();

    (void)argument;

    for (;;)
    {
        telem_t t;

        (void)osMutexAcquire(s_mtx_telem, osWaitForever);
        t = s_telem;
        (void)osMutexRelease(s_mtx_telem);

        /* 5 通道：0=目标 1=实际 2=PWM 3=误差 4=累计角度(°) */
        vofa_send_justfloat(t.target, t.actual, t.pwm, t.error, t.angle);

        tick += VOFA_PERIOD_MS;
        (void)osDelayUntil(tick);
    }
}

/* ============================ 任务 4：按键切模式（20ms） ============================ */

void key_task(void *argument)
{
    uint32_t tick      = osKernelGetTickCount();
    uint32_t press_cnt = 0u;

    (void)argument;

    for (;;)
    {
        uint8_t pressed = (HAL_GPIO_ReadPin(KEY_PORT, KEY_PIN) == GPIO_PIN_RESET) ? 1u : 0u;

        if (pressed != 0u)
        {
            if (press_cnt < 100u)
            {
                press_cnt++;
            }
            if (press_cnt == 3u)                                /* 稳定按下 3×20ms = 60ms 消抖 */
            {
                uint8_t m = (s_mode == (uint8_t)CTRL_MODE_SPEED)
                            ? (uint8_t)CTRL_MODE_POSITION
                            : (uint8_t)CTRL_MODE_SPEED;
                s_mode = m;
                s_mode_reset_req = 1u;
                vofa_send_text((m == (uint8_t)CTRL_MODE_SPEED)
                               ? "\r\n[mode] SPEED (target RPM)\r\n"
                               : "\r\n[mode] POSITION (target deg)\r\n");
            }
        }
        else
        {
            press_cnt = 0u;
        }

        tick += KEY_PERIOD_MS;
        (void)osDelayUntil(tick);
    }
}

/* ============================ 任务 5：心跳灯（defaultTask 调用） ============================ */

void led_task(void *argument)
{
    uint32_t tick = osKernelGetTickCount();

    (void)argument;

    for (;;)
    {
        uint8_t  n = (s_mode == (uint8_t)CTRL_MODE_SPEED) ? 1u : 2u;   /* 定速闪1次 / 定位闪2次 */
        uint8_t  i;

        for (i = 0u; i < n; i++)
        {
            HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
            tick += 80u;
            (void)osDelayUntil(tick);
            HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
            tick += 80u;
            (void)osDelayUntil(tick);
        }

        tick += 640u;
        (void)osDelayUntil(tick);
    }
}
