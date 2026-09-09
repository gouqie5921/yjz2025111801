/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gpio.h
  * @brief   This file contains all the function prototypes for
  *          the gpio.c file
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
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __GPIO_H__
#define __GPIO_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* USER CODE BEGIN Private defines */

/* 从机板载 LED：PC13，低电平点亮 */
#define LED_PIN          GPIO_PIN_13
#define LED_PORT         GPIOC
#define LED_ACTIVE_LOW   1u

/* 主机按键：PA0，内部上拉，按下(对GND)为低 */
#define KEY_PIN          GPIO_PIN_0
#define KEY_PORT         GPIOA
#define KEY_ACTIVE_LOW   1u

/* USER CODE END Private defines */

void MX_GPIO_Init(void);

/* USER CODE BEGIN Prototypes */

void led_init(void);
void led_toggle(void);
uint8_t key_read(void);            /* 返回 1=按下(低电平) 0=松开(高) */

/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif
#endif /*__ GPIO_H__ */

