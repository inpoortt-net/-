// clock.c —— 软件时钟 + 断电保存
// 走时原理：数 HAL_GetTick 的毫秒滴答，1 秒 1 秒地往前加（含闰年、星期计算）
// 断电保存：每过一分钟，把当前时间写进 Flash 的最后一页；上电时先把它找回来
// 注意：没有电池供电时，断电期间时钟不走，找回的是“断电前最后保存的时间”（误差最多 1 分钟）
//       想要停电也完全不停，需要给板子 VBAT 接纽扣电池并改用 STM32 自带 RTC（以后需要再说）
#include"clock.h"
#include<stdlib.h>

// ================== 断电保存相关参数 ==================
#define CLOCK_FLASH_ADDR	0x0800FC00UL	// Flash 最后一页（1KB，本模块专用，程序用不到）
#define CLOCK_SLOT_SIZE		16				// 一条时间记录占 16 字节
#define CLOCK_SLOT_COUNT	64				// 一页存 64 条（存满了整页擦掉重来）
#define CLOCK_MAGIC			0x54494D45UL	// 魔数（'TIME'），判断格子里有没有数据

// 一条时间记录（16 字节）
typedef struct
{
	uint32_t magic;			// 等于 CLOCK_MAGIC 表示有数据；全 0xFFFFFFFF 表示空格子
	uint16_t year;
	uint8_t month;
	uint8_t day;
	uint8_t hour;
	uint8_t minute;
	uint8_t second;
	uint8_t checksum;		// 前面 11 个字节相加的低 8 位（防写坏数据）
	uint8_t reserved[4];	// 补满 16 字节
} clock_slot_t;

// 编译期检查：结构体必须正好 16 字节（如果以后改字段改坏了，这里会直接编译报错）
typedef char clock_slot_size_check[(sizeof(clock_slot_t) == CLOCK_SLOT_SIZE) ? 1 : -1];

static clock_time_t s_time;		// 当前时间

// ================== 时间计算 ==================

// 判断是不是闰年
static uint8_t clock_is_leap_year(uint16_t year)
{
	if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))
	{
		return 1;
	}
	return 0;
}

// 某年某月有几天
static uint8_t clock_month_days(uint16_t year, uint8_t month)
{
	static const uint8_t days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	uint8_t d = days[month - 1];

	if (month == 2 && clock_is_leap_year(year))
	{
		d = 29;
	}
	return d;
}

// 由日期算星期（蔡勒公式），返回 0=周日 ~ 6=周六
static uint8_t clock_weekday_from_date(uint16_t year, uint8_t month, uint8_t day)
{
	uint16_t y = year;
	uint8_t m = month;
	uint16_t sum;

	if (m < 3)
	{
		// 1、2 月当作上一年的 13、14 月
		m = (uint8_t)(m + 12);
		y = (uint16_t)(y - 1);
	}
	// 蔡勒公式算出来 0=周六、1=周日……这里换算成 0=周日
	sum = (uint16_t)(day + (13 * (m + 1)) / 5 + (y % 100) + (y % 100) / 4 + (y / 100) / 4 + 5 * (y / 100));
	return (uint8_t)(((sum % 7) + 6) % 7);
}

// 用编译时间给时钟上电：__DATE__ 形如 "Oct  4 2026"，__TIME__ 形如 "12:34:56"
static void clock_init_from_build(void)
{
	const char *date_str = __DATE__;
	const char *time_str = __TIME__;
	static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
	uint8_t i;

	for (i = 0; i < 12; i++)
	{
		if (date_str[0] == months[i * 3] &&
			date_str[1] == months[i * 3 + 1] &&
			date_str[2] == months[i * 3 + 2])
		{
			s_time.month = (uint8_t)(i + 1);
			break;
		}
	}
	if (i >= 12)
	{
		s_time.month = 1;	// 理论上不会发生，兜底
	}
	s_time.day = (uint8_t)atoi(date_str + 4);
	s_time.year = (uint16_t)atoi(date_str + 7);
	s_time.hour = (uint8_t)atoi(time_str);
	s_time.minute = (uint8_t)atoi(time_str + 3);
	s_time.second = (uint8_t)atoi(time_str + 6);
	s_time.weekday = clock_weekday_from_date(s_time.year, s_time.month, s_time.day);
}

// ================== Flash 读写 ==================

// 一条记录的校验和：前 11 个字节相加
static uint8_t clock_slot_checksum(const clock_slot_t *slot)
{
	const uint8_t *p = (const uint8_t *)slot;
	uint8_t sum = 0;
	uint8_t i;

	for (i = 0; i < 11; i++)
	{
		sum = (uint8_t)(sum + p[i]);
	}
	return sum;
}

// 把当前时间存进 Flash：从前往后找空格子，填进去；没有空格子了就擦掉整页重来
static void clock_flash_save(void)
{
	clock_slot_t slot;
	FLASH_EraseInitTypeDef erase;
	uint32_t page_error = 0;
	uint32_t addr = CLOCK_FLASH_ADDR;
	const uint16_t *p;
	uint8_t i;
	uint8_t need_erase = 1;

	// 1. 找第一个还没写过的格子
	for (i = 0; i < CLOCK_SLOT_COUNT; i++)
	{
		const clock_slot_t *s = (const clock_slot_t *)(CLOCK_FLASH_ADDR + (uint32_t)i * CLOCK_SLOT_SIZE);

		if (s->magic == 0xFFFFFFFFUL)
		{
			addr = CLOCK_FLASH_ADDR + (uint32_t)i * CLOCK_SLOT_SIZE;
			need_erase = 0;
			break;
		}
	}

	// 2. 把当前时间装进记录
	slot.magic = CLOCK_MAGIC;
	slot.year = s_time.year;
	slot.month = s_time.month;
	slot.day = s_time.day;
	slot.hour = s_time.hour;
	slot.minute = s_time.minute;
	slot.second = s_time.second;
	slot.checksum = 0;
	for (i = 0; i < 4; i++)
	{
		slot.reserved[i] = 0;
	}
	slot.checksum = clock_slot_checksum(&slot);

	// 3. 开写
	HAL_FLASH_Unlock();

	if (need_erase)
	{
		// 整页擦掉（擦完后所有格子都变成 0xFFFFFFFF）
		erase.TypeErase = FLASH_TYPEERASE_PAGES;
		erase.PageAddress = CLOCK_FLASH_ADDR;
		erase.NbPages = 1;
		if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK)
		{
			HAL_FLASH_Lock();
			return;
		}
		addr = CLOCK_FLASH_ADDR;
	}

	// 按半字（2 字节）一个个写进去
	p = (const uint16_t *)&slot;
	for (i = 0; i < (uint8_t)(CLOCK_SLOT_SIZE / 2); i++)
	{
		if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr + (uint32_t)i * 2, p[i]) != HAL_OK)
		{
			break;	// 写失败就停（下次上电校验不过会跳过这条）
		}
	}

	HAL_FLASH_Lock();
}

// 从 Flash 里找回最近一次存的时间；找到返回 1，没有返回 0
static uint8_t clock_flash_load(clock_time_t *t)
{
	int8_t i;

	for (i = (int8_t)(CLOCK_SLOT_COUNT - 1); i >= 0; i--)
	{
		const clock_slot_t *s = (const clock_slot_t *)(CLOCK_FLASH_ADDR + (uint32_t)i * CLOCK_SLOT_SIZE);

		if (s->magic == CLOCK_MAGIC &&
			s->checksum == clock_slot_checksum(s) &&
			s->month >= 1 && s->month <= 12 &&
			s->day >= 1 && s->day <= 31 &&
			s->hour < 24 && s->minute < 60 && s->second < 60)
		{
			t->year = s->year;
			t->month = s->month;
			t->day = s->day;
			t->hour = s->hour;
			t->minute = s->minute;
			t->second = s->second;
			t->weekday = clock_weekday_from_date(t->year, t->month, t->day);
			return 1;
		}
	}
	return 0;
}

// ================== 对外接口 ==================

// 上电初始化
void clock_init(void)
{
	clock_time_t t;

	if (clock_flash_load(&t))
	{
		s_time = t;		// 找到断电前存的时间，用它
	}
	else
	{
		clock_init_from_build();	// 第一次上电：用编译时间做起点
		clock_flash_save();			// 马上存一份，防止没满一分钟就断电
	}
}

// 走时：往前加 seconds 秒（会自动处理进位）
void clock_add_seconds(uint32_t seconds)
{
	uint8_t last_minute = s_time.minute;

	while (seconds > 0)
	{
		seconds--;
		s_time.second++;
		if (s_time.second >= 60)
		{
			s_time.second = 0;
			s_time.minute++;
			if (s_time.minute >= 60)
			{
				s_time.minute = 0;
				s_time.hour++;
				if (s_time.hour >= 24)
				{
					s_time.hour = 0;
					s_time.weekday = (uint8_t)((s_time.weekday + 1) % 7);
					s_time.day++;
					if (s_time.day > clock_month_days(s_time.year, s_time.month))
					{
						s_time.day = 1;
						s_time.month++;
						if (s_time.month > 12)
						{
							s_time.month = 1;
							s_time.year++;
						}
					}
				}
			}
		}
	}

	// 分钟变了就存一次（这样断电最多丢 1 分钟的时间）
	if (s_time.minute != last_minute)
	{
		clock_flash_save();
	}
}

// 校时（以后做“设置时间”菜单时调用；存完立即写 Flash）
void clock_set(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second)
{
	s_time.year = year;
	s_time.month = month;
	s_time.day = day;
	s_time.hour = hour;
	s_time.minute = minute;
	s_time.second = second;
	s_time.weekday = clock_weekday_from_date(year, month, day);
	clock_flash_save();
}

// 读取当前时间
const clock_time_t *clock_get(void)
{
	return &s_time;
}
