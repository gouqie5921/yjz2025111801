/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "servo.h"
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* 上报协议选择：
 *   1 = JustFloat 二进制波形（阶段3，最终交付版）
 *   0 = 纯文本 angle=/duty= （阶段2 调试用）
 * 对应教程 §5.2 主循环里的切换操作，用宏代替手工改代码行。 */
#define VOFA_PROTOCOL_JUSTFLOAT 1u

#define CMD_BUF_LEN 8u
static volatile uint8_t  rx_byte   = 0u;              /* 逐字节接收缓冲 */
static volatile uint8_t  cmd_buf[CMD_BUF_LEN];        /* 帧内数字暂存 */
static volatile uint8_t  cmd_len   = 0u;              /* 已收数字个数 */
static volatile uint8_t  cmd_ok    = 0u;              /* 收到一条有效指令 */
static volatile float    cmd_angle = 0.0f;            /* 待执行角度 */
static uint32_t          last_send = 0u;              /* 上次上报时刻 */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
/* 定义在文件后部(USER CODE 4)，主循环先用到，故在此声明原型 */
void vofa_justfloat_send(float a, float b);
void vofa_text_send(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_TIM3_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  servo_attach(&htim3, TIM_CHANNEL_1);                /* PA6: TIM3_CH1, 50Hz PWM */

  /* ===== 上电自检(临时)：舵机匀速扫描 =====
     步长 5°(>SG90死区~1°) + 15ms → 约330°/s 连续转动，无涩感/无顿挫。
     从回中(90°)出发。确认完删掉本段即恢复正常上电回中(90°)。 */
  float pos;
  for (pos = 90.0f;  pos >= 0.0f;   pos -= 5.0f) { servo_set_angle(pos); HAL_Delay(15u); }
  for (pos = 0.0f;   pos <= 180.0f; pos += 5.0f) { servo_set_angle(pos); HAL_Delay(15u); }
  for (pos = 180.0f; pos >= 90.0f;  pos -= 5.0f) { servo_set_angle(pos); HAL_Delay(15u); }
  /* 扫完停在 90° */

  /* 开启 USART1 逐字节中断接收 */
  HAL_UART_Receive_IT(&huart1, (uint8_t *)&rx_byte, 1u);

  /* 上电提示(文本阶段用 VOFA+ RawData 可看到) */
  const char boot[] = "servo ready, send #000~#180\r\n";
  HAL_UART_Transmit(&huart1, (uint8_t *)boot, sizeof(boot) - 1u, 100u);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* ① 收到新的角度指令 → 驱动舵机 */
    if (cmd_ok)
    {
        cmd_ok = 0u;
        servo_set_angle(cmd_angle);
    }

    /* ② 每 20ms 上报一次 → VOFA+ 波形刷新约 50Hz */
    if ((HAL_GetTick() - last_send) >= 20u)
    {
        last_send = HAL_GetTick();
#if VOFA_PROTOCOL_JUSTFLOAT
        vofa_justfloat_send(servo_get_angle(), servo_get_duty_percent());
#else
        vofa_text_send();   /* 阶段2 调试用纯文本 */
#endif
    }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
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

  /** Initializes the CPU, AHB and APB buses clocks
  */
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

/* USER CODE BEGIN 4 */

/* ================= 上位机指令解析(USART 中断上下文，纯整数运算) ============ */

/* 解析当前一帧：#xxx(3位定长) 或 #xx..\n。含非法字符或超出行程即整帧丢弃。 */
static void parse_cmd(void)
{
    uint16_t a = 0u;
    uint8_t  i;

    for (i = 0u; i < cmd_len; i++)
    {
        if ((cmd_buf[i] < '0') || (cmd_buf[i] > '9'))
        {
            return;                       /* 坏帧：不执行 */
        }
        a = (uint16_t)(a * 10u + (uint16_t)(cmd_buf[i] - '0'));
        if (a > 180u)
        {
            return;                       /* 超出行程：不执行 */
        }
    }
    cmd_angle = (float)a;
    cmd_ok    = 1u;
}

/* USART1 每收到 1 字节回调一次 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART1)
    {
        return;
    }

    if (rx_byte == '#')
    {
        cmd_len = 0u;                     /* 帧起始符：开始新帧 */
    }
    else if ((rx_byte >= '0') && (rx_byte <= '9'))
    {
        if (cmd_len < 3u)                 /* 只收 3 位(0~180)，多余数字忽略 */
        {
            cmd_buf[cmd_len++] = rx_byte;
        }
        if (cmd_len == 3u)
        {
            parse_cmd();                  /* #090 定长帧：凑满 3 位立即生效 */
            cmd_len = 0u;
        }
    }
    else if ((rx_byte == '\n') && (cmd_len > 0u))
    {
        parse_cmd();                      /* #90\n 换行帧 */
        cmd_len = 0u;
    }
    /* '\r'、空格及其它字符一律忽略 */

    HAL_UART_Receive_IT(&huart1, (uint8_t *)&rx_byte, 1u);   /* 继续接收 */
}

/* USART 出错(如溢出)后重新挂起接收，避免长时间不收数据后协议卡死 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART1)
    {
        return;
    }
    HAL_UART_Receive_IT(&huart1, (uint8_t *)&rx_byte, 1u);
}

/* ====================== 上报函数(教程 §5.2 回调区) ======================== */

/* 阶段3：JustFloat 帧 = 2个float(小端) + 帧尾 00 00 80 7F */
void vofa_justfloat_send(float a, float b)
{
    uint8_t buf[12];
    const uint8_t tail[4] = {0x00u, 0x00u, 0x80u, 0x7Fu};

    memcpy(buf,      &a, 4u);             /* 通道0: 角度     */
    memcpy(buf + 4u, &b, 4u);             /* 通道1: 占空比%  */
    memcpy(buf + 8u, tail, 4u);
    HAL_UART_Transmit(&huart1, buf, sizeof(buf), 20u);
}

/* 阶段2：纯文本上报(整数格式，避免依赖 float 打印库) */
void vofa_text_send(void)
{
    char msg[40];
    int  n = sprintf(msg, "angle=%d duty=%d%%\r\n",
                     (int)servo_get_angle(), (int)servo_get_duty_percent());
    if (n > 0)
    {
        HAL_UART_Transmit(&huart1, (uint8_t *)msg, (uint16_t)n, 20u);
    }
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
