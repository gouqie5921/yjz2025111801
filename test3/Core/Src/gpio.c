/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gpio.c
  * @brief   This file provides code for the configuration of all used GPIO pins.
  *
  * 本项目引脚安排：
  *   - PC13 : 从机板载 LED（低电平点亮）
  *   - PA0  : 主机按键（内部上拉，外接轻触键到 GND，按下为低）
  *   - PA11 : CAN1_RX（在 stm32f1xx_hal_msp.c 的 HAL_CAN_MspInit 配置）
  *   - PA12 : CAN1_TX（同上）
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "gpio.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/*----------------------------------------------------------------------------*/
/* Configure GPIO                                                             */
/*----------------------------------------------------------------------------*/
/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/** Configure pins as
        * Analog
        * Input
        * Output
        * EVENT_OUT
        * EXTI
*/
void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /* PC13 板载 LED：推挽输出 */
  HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);   /* 低电平点亮，先熄灭 */
  GPIO_InitStruct.Pin  = LED_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_PORT, &GPIO_InitStruct);

  /* PA0 按键：内部上拉输入，按下对 GND 为低 */
  GPIO_InitStruct.Pin  = KEY_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(KEY_PORT, &GPIO_InitStruct);
}

/* USER CODE BEGIN 2 */

void led_init(void)
{
  HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);   /* 熄灭（低电平点亮） */
}

void led_toggle(void)
{
  HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
}

uint8_t key_read(void)
{
#if KEY_ACTIVE_LOW
  return (HAL_GPIO_ReadPin(KEY_PORT, KEY_PIN) == GPIO_PIN_RESET) ? 1u : 0u;
#else
  return (HAL_GPIO_ReadPin(KEY_PORT, KEY_PIN) == GPIO_PIN_SET)   ? 1u : 0u;
#endif
}

/* USER CODE END 2 */
