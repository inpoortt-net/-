#pragma once
#include"main.h"

// buzzer.h —— 无源蜂鸣器接口（PWM 变调 + 后台音序，见 buzzer.c）

// ================== 对外接口 ==================
void buzzer_init(void);		// 初始化（启动 PWM，默认静音）
void buzzer_tick(void);		// 每 1ms 调一次（放在 SysTick 中断里，内部按 10ms 走音序）
void buzzer_key(void);		// 按键：轻短“嘀”
void buzzer_confirm(void);	// 确认一步：中性短音
void buzzer_success(void);	// 成功：上行“叮-咚”
void buzzer_fail(void);		// 失败：下行“嘟-呜”
void buzzer_notice(void);	// 提示：两短声（至少4位、已清空）
