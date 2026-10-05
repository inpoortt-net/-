#pragma once
#include"main.h"

// 按键编号（和 PA0~PA3 的对应关系在 key.c 里）
#define KEY_UP		0
#define KEY_DOWN	1
#define KEY_OK		2
#define KEY_BACK	3
#define KEY_COUNT	4

// 事件类型
#define KEY_EVENT_NONE		0
#define KEY_EVENT_SHORT		1	// 短按：按下后松开（没到长按时间）
#define KEY_EVENT_LONG		2	// 长按：按住 0.8 秒，触发一次
#define KEY_EVENT_REPEAT	3	// 连发：长按之后，每 0.2 秒一次

// ================== 对外接口 ==================
void key_init(void);									// 初始化按键状态机（引脚已由 CubeMX 配好）
void key_tick(void);									// 每 1ms 调一次（放在 SysTick 中断里）
uint8_t key_get_event(uint8_t *key, uint8_t *event);	// 主循环取事件：1=有事件，0=没有
