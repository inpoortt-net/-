#pragma once
#include"main.h"

// clock.h —— 软件时钟接口：走时 + 每分钟自动存 Flash + 上电恢复

// 时间结构：weekday 0=周日、1=周一、……、6=周六
typedef struct
{
	uint16_t year;		// 年，比如 2026
	uint8_t month;		// 月，1~12
	uint8_t day;		// 日，1~31
	uint8_t hour;		// 时，0~23
	uint8_t minute;		// 分，0~59
	uint8_t second;		// 秒，0~59
	uint8_t weekday;	// 星期，0=周日 ~ 6=周六
} clock_time_t;

// ================== 对外接口 ==================
void clock_init(void);		// 上电初始化：先找 Flash 里存的旧时间，找不到就用编译时间
void clock_add_seconds(uint32_t seconds);	// 走时 N 秒（自动进位；过整分钟会自动存一次 Flash）
void clock_set(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second);	// 校时（以后做“设置时间”功能用）
const clock_time_t *clock_get(void);		// 读取当前时间（只读）
