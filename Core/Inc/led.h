#pragma once
#include"main.h"

// 状态指示灯（开漏输出、低电平点亮；接线：3.3V→LED长脚(+)→短脚(−)→电阻→引脚）：
//   红灯 = PB1（LED_RED）——密码错误 / 锁定
//   绿灯 = PA4（LED_GREEN）——密码正确 / 修改成功
// ================== 对外接口 ==================
void led_init(void);		// 初始化：两个灯都熄灭
void led_off(void);			// 两个灯都熄灭（同时停止慢闪）
void led_green(void);		// 绿灯亮（红灯灭）
void led_red(void);			// 红灯亮（绿灯灭）
void led_blink(uint8_t on);	// on=1：红灯慢闪（锁定用）；on=0：停止并熄灭
void led_task(void);		// 每轮主循环调用一次：负责慢闪计时
