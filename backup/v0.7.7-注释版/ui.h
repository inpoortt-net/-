#pragma once
#include"main.h"

// ================== 对外接口 ==================
void ui_init(void);		// 初始化界面（屏幕 + 时钟 + 画第一屏）
void ui_task(void);		// 主循环里反复调用：处理按键、走时、刷新屏幕
