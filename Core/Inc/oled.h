#pragma once
#include"main.h"

// oled.h —— SSD1306 OLED 驱动接口（显存画点/矩形/ASCII/汉字/整屏刷新）

// ================== SSD1306 OLED 屏幕参数 ==================
#define OLED_WIDTH		128							// 屏幕宽度（像素）
#define OLED_HEIGHT		64							// 屏幕高度（像素）
#define OLED_BUF_SIZE	(OLED_WIDTH * OLED_HEIGHT / 8)	// 显存大小（字节）

// I2C 从机地址（7 位）：常见模块是 0x3C，少数是 0x3D，初始化时自动探测
#define OLED_ADDR_0		0x3C
#define OLED_ADDR_1		0x3D
#define OLED_TIMEOUT	100							// I2C 发送超时时间（毫秒）

// ================== 对外接口 ==================
void oled_init(void);								// 初始化屏幕（上电后调用一次）
void oled_clear(void);								// 清空显存（要配合 oled_refresh 才会真正清屏）
void oled_refresh(void);							// 把显存整屏刷到屏幕上
void oled_draw_pixel(uint8_t x, uint8_t y, uint8_t color);						// 画一个点（color：1 亮 / 0 灭）
void oled_fill_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t color);	// 填充一个矩形（拿来做标题栏）
void oled_show_char(uint8_t x, uint8_t y, char ch, uint8_t color);				// 显示一个字符（8x16 点阵）
void oled_show_string(uint8_t x, uint8_t y, const char *str, uint8_t color);	// 显示一个字符串（8x16 点阵）
void oled_show_cn(uint8_t x, uint8_t y, uint16_t code, uint8_t color);			// 显示一个 16x16 汉字（code 是 Unicode 码点）
void oled_show_text(uint8_t x, uint8_t y, const char *str, uint8_t color);		// 显示一段文字（自动混排 ASCII 和汉字）
uint16_t oled_text_width(const char *str);										// 算一段文字的像素宽度（居中排版用）
