// settings.c —— 系统设置存储（密码 + 连续输错次数）
// 存在 Flash 的 0x0800F800 这一页（倒数第二页），方法和 clock.c 的存储一样：
// 一页 64 个格子，一个个往后写，写满整页擦掉重来
#include"settings.h"

// ================== 存储相关参数 ==================
#define SETTINGS_FLASH_ADDR	0x0800F800UL	// Flash 倒数第二页（1KB，本模块专用）
#define SETTINGS_SLOT_SIZE	16				// 一条记录占 16 字节
#define SETTINGS_SLOT_COUNT	64				// 一页存 64 条（存满了整页擦掉重来）
#define SETTINGS_MAGIC		0x50574431UL	// 魔数（'PWD1'），判断格子里有没有数据

// 默认密码：第一次上电、或者 Flash 里没有记录时用（想改就改这一行）
static const uint8_t s_default_pwd[] = { 1, 2, 3, 4 };

// 一条密码记录（16 字节）
typedef struct
{
	uint32_t magic;						// 等于 SETTINGS_MAGIC 表示有数据；全 0xFFFFFFFF 表示空格子
	uint8_t len;						// 密码位数（4~8）
	uint8_t digits[SETTINGS_PWD_MAX];	// 各位数字
	uint8_t checksum;					// 前面 13 个字节相加的低 8 位（防写坏）
	uint8_t reserved[2];				// 补满 16 字节
} settings_slot_t;

// 编译期检查：结构体必须正好 16 字节（如果以后改字段改坏了，这里会直接编译报错）
typedef char settings_slot_size_check[(sizeof(settings_slot_t) == SETTINGS_SLOT_SIZE) ? 1 : -1];

#define SETTINGS_FAIL_MAGIC	0x4641494CUL	// 魔数（'FAIL'），给“输错次数”用

// 连续输错次数的记录（16 字节）
typedef struct
{
	uint32_t magic;			// 等于 SETTINGS_FAIL_MAGIC 表示有数据
	uint8_t count;			// 连续错误次数
	uint8_t checksum;		// 前 5 个字节相加的低 8 位
	uint8_t reserved[10];	// 补满 16 字节
} settings_fail_slot_t;

// 编译期检查同上
typedef char settings_fail_slot_size_check[(sizeof(settings_fail_slot_t) == SETTINGS_SLOT_SIZE) ? 1 : -1];

static uint8_t s_password[SETTINGS_PWD_MAX];	// 当前密码
static uint8_t s_password_len;					// 当前密码位数
static uint8_t s_fail_count;					// 连续输错次数（掉电保存）

// 一条记录的校验和：前 13 个字节相加
static uint8_t settings_slot_checksum(const settings_slot_t *slot)
{
	const uint8_t *p = (const uint8_t *)slot;
	uint8_t sum = 0;
	uint8_t i;

	for (i = 0; i < 13; i++)
	{
		sum = (uint8_t)(sum + p[i]);
	}
	return sum;
}

// 从 Flash 里读密码；找到合法记录返回 1，没有返回 0
static uint8_t settings_flash_load(void)
{
	int8_t i;
	uint8_t j;

	for (i = (int8_t)(SETTINGS_SLOT_COUNT - 1); i >= 0; i--)
	{
		const settings_slot_t *s = (const settings_slot_t *)(SETTINGS_FLASH_ADDR + (uint32_t)i * SETTINGS_SLOT_SIZE);

		if (s->magic == SETTINGS_MAGIC &&
			s->checksum == settings_slot_checksum(s) &&
			s->len >= 4 && s->len <= SETTINGS_PWD_MAX)
		{
			for (j = 0; j < s->len; j++)
			{
				if (s->digits[j] > 9)
				{
					break;	// 数据不合法
				}
			}
			if (j == s->len)
			{
				for (j = 0; j < SETTINGS_PWD_MAX; j++)
				{
					s_password[j] = (j < s->len) ? s->digits[j] : 0;
				}
				s_password_len = s->len;
				return 1;
			}
		}
	}
	return 0;
}

// 通用：往 Flash 写一条 16 字节记录（从前往后找空格子；写满整页就擦掉重来）
static void settings_flash_write(const uint8_t *rec)
{
	FLASH_EraseInitTypeDef erase;
	uint32_t page_error = 0;
	uint32_t addr = SETTINGS_FLASH_ADDR;
	const uint16_t *p;
	uint8_t i;
	uint8_t need_erase = 1;

	// 1. 找第一个还没写过的格子
	for (i = 0; i < SETTINGS_SLOT_COUNT; i++)
	{
		const uint32_t *magic = (const uint32_t *)(SETTINGS_FLASH_ADDR + (uint32_t)i * SETTINGS_SLOT_SIZE);

		if (*magic == 0xFFFFFFFFUL)
		{
			addr = SETTINGS_FLASH_ADDR + (uint32_t)i * SETTINGS_SLOT_SIZE;
			need_erase = 0;
			break;
		}
	}

	// 2. 开写
	HAL_FLASH_Unlock();

	if (need_erase)
	{
		erase.TypeErase = FLASH_TYPEERASE_PAGES;
		erase.PageAddress = SETTINGS_FLASH_ADDR;
		erase.NbPages = 1;
		if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK)
		{
			HAL_FLASH_Lock();
			return;
		}
		addr = SETTINGS_FLASH_ADDR;
	}

	p = (const uint16_t *)rec;
	for (i = 0; i < (uint8_t)(SETTINGS_SLOT_SIZE / 2); i++)
	{
		if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr + (uint32_t)i * 2, p[i]) != HAL_OK)
		{
			break;	// 写失败就停（下次上电校验不过会跳过这条）
		}
	}

	HAL_FLASH_Lock();
}

// 把密码写进 Flash
static void settings_flash_save(const uint8_t *digits, uint8_t len)
{
	settings_slot_t slot;
	uint8_t i;

	slot.magic = SETTINGS_MAGIC;
	slot.len = len;
	for (i = 0; i < SETTINGS_PWD_MAX; i++)
	{
		slot.digits[i] = (i < len) ? digits[i] : 0;
	}
	slot.checksum = 0;
	slot.reserved[0] = 0;
	slot.reserved[1] = 0;
	slot.checksum = settings_slot_checksum(&slot);

	settings_flash_write((const uint8_t *)&slot);
}

// “输错次数”记录的校验和：前 5 个字节相加
static uint8_t settings_fail_checksum(const settings_fail_slot_t *slot)
{
	const uint8_t *p = (const uint8_t *)slot;
	uint8_t sum = 0;
	uint8_t i;

	for (i = 0; i < 5; i++)
	{
		sum = (uint8_t)(sum + p[i]);
	}
	return sum;
}

// 从 Flash 读连续输错次数；没有记录就是 0
static uint8_t settings_fail_load(void)
{
	int8_t i;

	for (i = (int8_t)(SETTINGS_SLOT_COUNT - 1); i >= 0; i--)
	{
		const settings_fail_slot_t *s = (const settings_fail_slot_t *)(SETTINGS_FLASH_ADDR + (uint32_t)i * SETTINGS_SLOT_SIZE);

		if (s->magic == SETTINGS_FAIL_MAGIC &&
			s->checksum == settings_fail_checksum(s) &&
			s->count <= 100)
		{
			return s->count;
		}
	}
	return 0;
}

// 把连续输错次数写进 Flash
static void settings_fail_save(uint8_t count)
{
	settings_fail_slot_t slot;
	uint8_t i;

	slot.magic = SETTINGS_FAIL_MAGIC;
	slot.count = count;
	slot.checksum = 0;
	for (i = 0; i < 10; i++)
	{
		slot.reserved[i] = 0;
	}
	slot.checksum = settings_fail_checksum(&slot);

	settings_flash_write((const uint8_t *)&slot);
}

// 上电初始化
void settings_init(void)
{
	uint8_t i;

	// 读密码：没有就用默认密码并存一份
	if (!settings_flash_load())
	{
		for (i = 0; i < SETTINGS_PWD_MAX; i++)
		{
			s_password[i] = (i < (uint8_t)sizeof(s_default_pwd)) ? s_default_pwd[i] : 0;
		}
		s_password_len = (uint8_t)sizeof(s_default_pwd);
		settings_flash_save(s_password, s_password_len);
	}

	// 读连续输错次数（没有记录就是 0）
	s_fail_count = settings_fail_load();
}

// 读出当前密码
void settings_get_password(uint8_t *digits, uint8_t *len)
{
	uint8_t i;

	for (i = 0; i < SETTINGS_PWD_MAX; i++)
	{
		digits[i] = s_password[i];
	}
	*len = s_password_len;
}

// 保存新密码（先存内存，再写 Flash）
void settings_set_password(const uint8_t *digits, uint8_t len)
{
	uint8_t i;

	if (len < 4 || len > SETTINGS_PWD_MAX)
	{
		return;		// 位数不合法，忽略（调用方应该先检查）
	}
	for (i = 0; i < SETTINGS_PWD_MAX; i++)
	{
		s_password[i] = (i < len) ? digits[i] : 0;
	}
	s_password_len = len;
	settings_flash_save(digits, len);
}

// 读取连续输错次数
uint8_t settings_get_fail_count(void)
{
	return s_fail_count;
}

// 保存连续输错次数（写进 Flash，掉电不丢）
void settings_set_fail_count(uint8_t count)
{
	s_fail_count = count;
	settings_fail_save(count);
}
