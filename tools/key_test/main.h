// 主机模拟测试用的“假 main.h”（只给电脑上编译 key.c 用，不进单片机工程）
#pragma once
#include <stdint.h>

typedef struct { uint32_t dummy; } GPIO_TypeDef;

#define GPIO_PIN_RESET	0u
#define GPIO_PIN_SET	1u
#define GPIO_PIN_0	(1u << 0)
#define GPIO_PIN_1	(1u << 1)
#define GPIO_PIN_2	(1u << 2)
#define GPIO_PIN_3	(1u << 3)

#define KEY_UP_GPIO_Port	((GPIO_TypeDef *)0x1000u)
#define KEY_DOWN_GPIO_Port	((GPIO_TypeDef *)0x2000u)
#define KEY_OK_GPIO_Port	((GPIO_TypeDef *)0x3000u)
#define KEY_BACK_GPIO_Port	((GPIO_TypeDef *)0x4000u)

#define KEY_UP_Pin		GPIO_PIN_0
#define KEY_DOWN_Pin	GPIO_PIN_1
#define KEY_OK_Pin		GPIO_PIN_2
#define KEY_BACK_Pin	GPIO_PIN_3

// 测试电平：1=按下（低电平），0=松开（高电平）。由测试程序修改。
extern uint8_t g_test_level[4];

static inline uint8_t HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{
	uint8_t idx = 3;

	(void)pin;
	if (port == KEY_UP_GPIO_Port)
	{
		idx = 0;
	}
	else if (port == KEY_DOWN_GPIO_Port)
	{
		idx = 1;
	}
	else if (port == KEY_OK_GPIO_Port)
	{
		idx = 2;
	}
	return g_test_level[idx] ? GPIO_PIN_RESET : GPIO_PIN_SET;
}
