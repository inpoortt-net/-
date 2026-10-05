#include"oled.h"
#include"oledfont.h"
#include"oledfont_cn.h"
#include<string.h>

// main.c 里定义的 I2C1 句柄
extern I2C_HandleTypeDef hi2c1;

// 显存：1024 字节。先在内存里画好，再一次性推给屏幕
static uint8_t oled_buffer[OLED_BUF_SIZE];

// 屏幕实际使用的 I2C 地址（HAL 要求 7 位地址左移 1 位）
static uint16_t oled_addr = (OLED_ADDR_0 << 1);

// SSD1306 初始化命令表（第一个字节 0x00 是控制字节：表示后面全是命令）
static const uint8_t oled_init_cmds[] =
{
	0x00,
	0xAE,			// 关闭显示（先关掉，配置完再打开，避免花屏）
	0xD5, 0x80,		// 显示时钟分频
	0xA8, 0x3F,		// 多路复用比例：64 行
	0xD3, 0x00,		// 显示偏移：0
	0x40,			// 显示起始行：0
	0x8D, 0x14,		// 电荷泵开启（模块靠它内部升压，不开屏幕不亮）
	0x20, 0x00,		// 水平寻址模式
	0xA1,			// 左右方向：列 127 映射到 SEG0（常用接法）
	0xC8,			// 上下方向：COM 反扫（常用接法）
	0xDA, 0x12,		// COM 引脚配置：128x64
	0x81, 0xCF,		// 对比度
	0xD9, 0xF1,		// 预充电周期
	0xDB, 0x40,		// VCOMH 电压
	0xA4,			// 显示内容跟随显存
	0xA6,			// 正常显示（不反色）
	0xAF,			// 打开显示
};

// 把一段字节发给屏幕（阻塞式，发完才返回）
static void oled_write(uint8_t *data, uint16_t len)
{
	HAL_I2C_Master_Transmit(&hi2c1, oled_addr, data, len, OLED_TIMEOUT);
}

// 探测总线上有没有某个地址的设备（用来自动识别 0x3C / 0x3D）
static uint8_t oled_probe(uint8_t addr7)
{
	if (HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(addr7 << 1), 3, 50) == HAL_OK)
	{
		return 1;
	}
	return 0;
}

// 初始化屏幕：识别地址 -> 发配置命令 -> 清屏
void oled_init(void)
{
	uint8_t cmds[sizeof(oled_init_cmds)];

	// 自动识别屏幕地址：一般是 0x3C，少数模块是 0x3D
	if (oled_probe(OLED_ADDR_0))
	{
		oled_addr = (OLED_ADDR_0 << 1);
	}
	else if (oled_probe(OLED_ADDR_1))
	{
		oled_addr = (OLED_ADDR_1 << 1);
	}

	// 拷贝一份再发（HAL 发送接口的参数不是 const，拷一下避免编译告警）
	memcpy(cmds, oled_init_cmds, sizeof(oled_init_cmds));
	oled_write(cmds, sizeof(cmds));

	oled_clear();
	oled_refresh();
}

// 清空显存（只是把内存里的画面擦掉，要调 oled_refresh 屏幕才会变）
void oled_clear(void)
{
	memset(oled_buffer, 0x00, sizeof(oled_buffer));
}

// 把整块显存刷到屏幕上
void oled_refresh(void)
{
	uint8_t tx[OLED_WIDTH + 1];		// 1 字节控制字 + 一页 128 字节数据
	uint8_t area_cmds[7] = { 0x00, 0x21, 0x00, 0x7F, 0x22, 0x00, 0x07 };	// 显示区域设为整屏
	const uint8_t *p = oled_buffer;
	uint8_t page;

	// 先设置显示区域：列 0~127、页 0~7（水平寻址模式下，数据会自动跨页写）
	oled_write(area_cmds, sizeof(area_cmds));

	tx[0] = 0x40;					// 控制字：后面都是数据
	for (page = 0; page < (OLED_HEIGHT / 8); page++)
	{
		memcpy(tx + 1, p, OLED_WIDTH);
		oled_write(tx, sizeof(tx));
		p += OLED_WIDTH;
	}
}

// 画一个点：color=1 点亮，color=0 熄掉。坐标超出屏幕就忽略，防止越界
void oled_draw_pixel(uint8_t x, uint8_t y, uint8_t color)
{
	uint8_t *p;

	if (x >= OLED_WIDTH || y >= OLED_HEIGHT)
	{
		return;
	}
	p = &oled_buffer[(y / 8) * OLED_WIDTH + x];
	if (color)
	{
		*p |= (uint8_t)(1 << (y % 8));
	}
	else
	{
		*p &= (uint8_t)~(1 << (y % 8));
	}
}

// 填充矩形：从 (x, y) 开始，宽 w、高 h
void oled_fill_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t color)
{
	uint8_t i, j;

	for (j = 0; j < h; j++)
	{
		for (i = 0; i < w; i++)
		{
			oled_draw_pixel((uint8_t)(x + i), (uint8_t)(y + j), color);
		}
	}
}

// 显示一个 8x16 字符：color=1 在黑底上写白字；color=0 在白底上写黑字（反白）
void oled_show_char(uint8_t x, uint8_t y, char ch, uint8_t color)
{
	const uint8_t *font;
	uint8_t col, row, byte;

	if (ch < 0x20 || ch > 0x7E)
	{
		ch = '?';	// 字库外的字符统一显示成 '?'
	}
	font = oled_font8x16[(uint8_t)ch - 0x20];
	for (col = 0; col < 8; col++)
	{
		for (row = 0; row < 16; row++)
		{
			byte = font[(row < 8) ? col : (col + 8)];
			if ((byte >> (row % 8)) & 0x01)
			{
				// 字库里有笔画的点
				oled_draw_pixel((uint8_t)(x + col), (uint8_t)(y + row), color);
			}
			else
			{
				// 字库里没有笔画的点（反白显示时这里要点亮）
				oled_draw_pixel((uint8_t)(x + col), (uint8_t)(y + row), (uint8_t)(1 - color));
			}
		}
	}
}

// 显示一个字符串：从 (x, y) 开始，每个字符宽 8 像素
void oled_show_string(uint8_t x, uint8_t y, const char *str, uint8_t color)
{
	while (*str != '\0')
	{
		oled_show_char(x, y, *str, color);
		x = (uint8_t)(x + 8);
		str++;
	}
}

// ================== 汉字显示 ==================

// UTF-8 解码：读出当前字符的 Unicode 码点，并把指针移到下一个字符
// （返回值 <0x80 表示 ASCII 字符；汉字是 3 字节编码，返回对应码点）
static uint16_t oled_utf8_next(const char **p)
{
	const uint8_t *s = (const uint8_t *)(*p);
	uint16_t code;

	if (s[0] < 0x80)
	{
		code = s[0];
		*p += 1;
	}
	else if ((s[0] & 0xE0) == 0xC0 && s[1] != 0)
	{
		code = (uint16_t)(((uint16_t)(s[0] & 0x1F) << 6) | (s[1] & 0x3F));
		*p += 2;
	}
	else if ((s[0] & 0xF0) == 0xE0 && s[1] != 0 && s[2] != 0)
	{
		code = (uint16_t)(((uint16_t)(s[0] & 0x0F) << 12) | ((uint16_t)(s[1] & 0x3F) << 6) | (s[2] & 0x3F));
		*p += 3;
	}
	else
	{
		// 不认识的编码：跳过一个字节，显示成 ?
		code = '?';
		*p += 1;
	}
	return code;
}

// 显示一个 16x16 汉字（code 是 Unicode 码点）；字库里没有就显示 ?
void oled_show_cn(uint8_t x, uint8_t y, uint16_t code, uint8_t color)
{
	const uint8_t *font = 0;
	uint16_t i;
	uint8_t col, row, byte;

	for (i = 0; i < (uint16_t)(sizeof(oled_cn_font) / sizeof(oled_cn_font[0])); i++)
	{
		if (oled_cn_font[i].code == code)
		{
			font = oled_cn_font[i].data;
			break;
		}
	}
	if (font == 0)
	{
		oled_show_char(x, y, '?', color);
		return;
	}
	for (col = 0; col < 16; col++)
	{
		for (row = 0; row < 16; row++)
		{
			byte = font[(row < 8) ? col : (col + 16)];
			if ((byte >> (row % 8)) & 0x01)
			{
				oled_draw_pixel((uint8_t)(x + col), (uint8_t)(y + row), color);
			}
			else
			{
				oled_draw_pixel((uint8_t)(x + col), (uint8_t)(y + row), (uint8_t)(1 - color));
			}
		}
	}
}

// 算一段文字的像素宽度（ASCII 每个 8 像素，汉字每个 16 像素）
uint16_t oled_text_width(const char *str)
{
	uint16_t w = 0;
	uint16_t code;

	while (*str != '\0')
	{
		code = oled_utf8_next(&str);
		w += (code < 0x80) ? 8 : 16;
	}
	return w;
}

// 显示一段文字（自动混排：ASCII 用 8x16，汉字用 16x16）
void oled_show_text(uint8_t x, uint8_t y, const char *str, uint8_t color)
{
	uint16_t code;

	while (*str != '\0')
	{
		code = oled_utf8_next(&str);
		if (code < 0x80)
		{
			oled_show_char(x, y, (char)code, color);
			x = (uint8_t)(x + 8);
		}
		else
		{
			oled_show_cn(x, y, code, color);
			x = (uint8_t)(x + 16);
		}
	}
}
