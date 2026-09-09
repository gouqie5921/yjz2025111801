/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : 第三题《CAN 通信》主程序（主机/从机 通过宏切换）
  ******************************************************************************
  * 主机：按 PA0(对GND瞬时短接) → CAN 发 0x11(ID=0x111)；收到 0x22(ID=0x222) → 串口打印。
  * 从机：收到 0x11 → CAN 回 0x22(ID=0x222) + 翻转 PC13 LED。
  * 角色：编译时 -DNODE_ROLE=0(主机,默认) 或 -DNODE_ROLE=1(从机)。
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "can.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* ============================================================================
 *  角色选择：只改下面这一行！
 *      主机（默认）: #define NODE_ROLE  NODE_ROLE_MASTER
 *      从机        : #define NODE_ROLE  NODE_ROLE_SLAVE
 *  改完直接重新编译 + 烧录即可，不需要改任何 CMake 参数。
 * ========================================================================== */
#define NODE_ROLE_MASTER 0u
#define NODE_ROLE_SLAVE  1u
#define NODE_ROLE  NODE_ROLE_SLAVE      /* ← 从机改成 NODE_ROLE_SLAVE */

/* CAN 工作模式开关在 Core/Inc/can.h 里（单板回环自测时改成 CAN_MODE_LOOPBACK） */

/* CAN 报文 ID（标准帧）与数据、波特率 */
#define CAN_ID_MASTER2SLAVE 0x111u
#define CAN_ID_SLAVE2MASTER 0x222u
#define CAN_BAUDRATE        500000u

/* 排查用：打印 CAN 关键寄存器（跑通后可改成 0u 关掉） */
#define CAN_DEBUG_DUMP      1u

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

static volatile can_msg_t g_rx;        /* 回调暂存：最近一帧 */
static volatile uint8_t   g_rx_flag;   /* 1=有新帧待处理 */
static uint8_t            s_busoff;    /* Bus-Off 状态（用于只打印一次） */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* 串口工具：直接发送字符串 */
static void uart_print(const char *s)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)s, (uint16_t)strlen(s), 100u);
}

#if CAN_DEBUG_DUMP
/* 打印 CAN 寄存器，用于排查：MCR.INRQ=0 表示已退出初始化；MCR.LBKM=1 表示回环；
 * IER=0x02 表示 FIFO0 中断已使能；RF0R.FMP0 是 FIFO0 里待取帧数；TSR.TXOK0 表示发成功 */
static void can_dump_regs(const char *tag)
{
    char b[140];
    int  n = sprintf(b,
        "[reg]%s MCR=%08lX MSR=%08lX TSR=%08lX RF0R=%08lX IER=%08lX ESR=%08lX BTR=%08lX\r\n",
        tag,
        (unsigned long)CAN1->MCR, (unsigned long)CAN1->MSR, (unsigned long)CAN1->TSR,
        (unsigned long)CAN1->RF0R, (unsigned long)CAN1->IER, (unsigned long)CAN1->ESR,
        (unsigned long)CAN1->BTR);
    HAL_UART_Transmit(&huart1, (uint8_t *)b, (uint16_t)n, 100u);
}
#endif

/* CAN 接收回调（在 CAN 中断上下文调用）：只拷贝并置标志，具体处理放到主循环 */
static void app_can_rx(const can_msg_t *m)
{
    g_rx      = *m;
    g_rx_flag = 1u;
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* MCU Configuration--------------------------------------------------------*/
  HAL_Init();
  SystemClock_Config();

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */

  led_init();
  can_register_rx_callback(app_can_rx);

  if (can_init(CAN_BAUDRATE) != 0u)
  {
      uart_print("CAN init FAIL\r\n");
      while (1) { }
  }

  uart_print("\r\n=== 3rd: CAN comm ===\r\n");
  uart_print(NODE_ROLE == NODE_ROLE_MASTER ? "[role] MASTER\r\n" : "[role] SLAVE\r\n");
  uart_print("[can ] 500kbps, ID 0x111->0x222\r\n");
#if CAN_DEBUG_DUMP
  can_dump_regs("init");
#endif
  if (NODE_ROLE == NODE_ROLE_MASTER)
  {
      uart_print("[key ] touch PA0 to GND to send 0x11\r\n");
  }
  else
  {
      uart_print("[slv ] waits for 0x11, reply 0x22 + toggle LED\r\n");
  }

  while (1)
  {
    /* ⓪ 轮询接收（不依赖中断，保证不漏帧） */
    can_poll();

    /* ① 处理收到的 CAN 帧 */
    if (g_rx_flag)
    {
        g_rx_flag = 0u;
#if CAN_DEBUG_DUMP
        can_dump_regs("rx");
#endif
        if (NODE_ROLE == NODE_ROLE_MASTER)
        {
            /* 打印收到的任何 CAN 帧（带 ID，便于排查）；其中 0x222 就是从机的应答 */
            char buf[56];
            int  n = sprintf(buf, "[M] CAN RX: id=0x%03X dlc=%u d0=0x%02X\r\n",
                             (unsigned)g_rx.id, (unsigned)g_rx.dlc, (unsigned)g_rx.data[0]);
            HAL_UART_Transmit(&huart1, (uint8_t *)buf, (uint16_t)n, 100u);
        }
        else /* SLAVE */
        {
            /* 主机发来的数据：0x11 → 回 0x22 + 翻转 LED（题目要求第 4 条） */
            if ((g_rx.id == CAN_ID_MASTER2SLAVE) && (g_rx.dlc == 1u) && (g_rx.data[0] == 0x11u))
            {
                uint8_t d = 0x22u;
                uart_print("[S] CAN RX: 0x11\r\n");
                if (can_send(CAN_ID_SLAVE2MASTER, &d, 1u) == 0u)
                {
                    uart_print("[S] CAN TX: 0x22\r\n");
                }
                led_toggle();
            }
        }
    }

    /* ② 主机：按键(PA0 对 GND)触发发送 */
    if ((NODE_ROLE == NODE_ROLE_MASTER) && key_read())
    {
        HAL_Delay(20u);                       /* 消抖 */
        if (key_read())
        {
            uint8_t d = 0x11u;
            uart_print("[M] key\r\n");
            if (can_send(CAN_ID_MASTER2SLAVE, &d, 1u) == 0u)
            {
                uart_print("[M] CAN TX: 0x11\r\n");
            }
#if CAN_DEBUG_DUMP
            HAL_Delay(5u);
            can_dump_regs("tx");
#endif
            while (key_read()) { HAL_Delay(5u); }   /* 等按键松开 */
        }
    }

    /* ③ 总线异常（Bus-Off）：只在状态变化时打印一次，避免刷屏 */
    if (can_is_busoff())
    {
        if (!s_busoff)
        {
            s_busoff = 1u;
            uart_print("[can ] BUS-OFF: no ACK -> check 2nd node running, "
                       "CANH<->CANH, CANL<->CANL, common GND, 120ohm, module 5V\r\n");
        }
        can_recover();
        HAL_Delay(200u);
    }
    else if (s_busoff)
    {
        s_busoff = 0u;
        uart_print("[can ] recovered\r\n");
    }
  }
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  (void)file; (void)line;
}
#endif /* USE_FULL_ASSERT */
