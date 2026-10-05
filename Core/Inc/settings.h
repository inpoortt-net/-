#pragma once
#include"main.h"

// settings.h —— 密码与连续输错次数的存取（Flash，掉电不丢）

// 密码最长位数（和 ui.c 里的 PWD_MAX_LEN 保持一致）
#define SETTINGS_PWD_MAX	8

// ================== 对外接口 ==================
void settings_init(void);									// 上电初始化：从 Flash 读密码（没有就用默认 1234）
void settings_get_password(uint8_t *digits, uint8_t *len);	// 读出当前密码
void settings_set_password(const uint8_t *digits, uint8_t len);	// 保存新密码（写进 Flash，掉电不丢）
uint8_t settings_get_fail_count(void);						// 读取连续输错次数
void settings_set_fail_count(uint8_t count);				// 保存连续输错次数（写进 Flash，掉电不丢）
