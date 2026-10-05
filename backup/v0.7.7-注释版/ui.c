// ui.c —— 界面层：页面状态机（中文界面）
// 页面关系：主页 --OK--> 菜单 --OK--> [密码锁 / 设置密码]
// 密码存在 settings 模块里（掉电不丢）；设置密码成功后自动回到密码锁页，可以直接试新密码
#include"ui.h"
#include"oled.h"
#include"key.h"
#include"clock.h"
#include"settings.h"
#include"buzzer.h"
#include"led.h"
#include<stdio.h>

// ================== 页面 ==================
typedef enum
{
	PAGE_HOME = 0,		// 主页：时间 + 日期
	PAGE_MENU,			// 主菜单列表
	PAGE_PASSWORD,		// 密码锁：输入密码
	PAGE_SETPWD			// 设置密码：修改密码
} ui_page_t;

#define MENU_IDLE_HOME_MS	30000	// 菜单页 30 秒没按键：自动回主页

// ================== 主菜单列表 ==================
// 以后加功能：1) 这里加名字  2) ui_menu_key() 里加分支
#define MENU_ITEM_COUNT		2
#define MENU_VISIBLE		3		// 屏幕中间最多显示几条（每条 16 像素高）

static const char *const s_menu_names[MENU_ITEM_COUNT] =
{
	"密码锁",		// 第 0 项：输入密码
	"修改密码"		// 第 1 项：设置新密码
};

static uint8_t s_menu_sel = 0;		// 当前选中的是第几项
static uint8_t s_menu_top = 0;		// 列表从第几项开始显示

// ================== 公共状态 ==================
#define PWD_MIN_LEN		4		// 密码最少几位
#define PWD_MAX_LEN		8		// 密码最多几位
#define MSG_SHOW_MS		2000	// “密码正确/错误”这类结果展示多久（毫秒）
#define HINT_SHOW_MS	1500	// 临时提示显示多久（毫秒）
#define PWD_FAIL_MAX	3		// 连续输错几次就锁定
#define PWD_LOCK_MS		30000	// 锁定时长（毫秒）

static ui_page_t s_page = PAGE_HOME;
static uint8_t s_dirty = 1;				// 1=需要重画屏幕
static uint32_t s_last_tick = 0;		// 上次走时的滴答
static uint32_t s_last_key_tick = 0;	// 最近一次按键的滴答（自动回主页用）

// 临时提示（比如“至少4位”“已清空”），显示在底部提示行
static const char *s_hint = 0;
static uint32_t s_hint_timer = 0;

// 提前声明（下面的函数互相调用）
static void ui_show_hint(const char *text);
static void ui_pwd_page_enter(void);
static void ui_set_page_enter(void);

// ================== 数字输入编辑器（两个密码页面共用） ==================
typedef struct
{
	uint8_t count;					// 已确认几位（0 = 还没开始）
	uint8_t digits[PWD_MAX_LEN];	// 已确认的数字
	uint8_t current;				// 正在调的数字 0~9
} digit_edit_t;

static digit_edit_t s_pwd_edit;		// 密码锁页的输入
static digit_edit_t s_set_edit;		// 设置密码页的输入

// 清空编辑器
static void digit_edit_reset(digit_edit_t *e)
{
	uint8_t i;

	for (i = 0; i < PWD_MAX_LEN; i++)
	{
		e->digits[i] = 0;
	}
	e->count = 0;
	e->current = 0;
}

// 编辑器的按键处理
// 返回：0=已处理；1=长按OK“提交”；2=一位都没输入时按了BACK
static uint8_t digit_edit_key(digit_edit_t *e, uint8_t key, uint8_t event)
{
	switch (key)
	{
	case KEY_UP:
		// 短按一格一格调；按住连发快速滚
		e->current = (uint8_t)((e->current + 1) % 10);
		s_dirty = 1;
		break;
	case KEY_DOWN:
		e->current = (uint8_t)((e->current + 9) % 10);
		s_dirty = 1;
		break;
	case KEY_OK:
		if (event == KEY_EVENT_SHORT)
		{
			// 确认当前数字，跳到下一位
			if (e->count < PWD_MAX_LEN)
			{
				e->digits[e->count] = e->current;
				e->count++;
				e->current = 0;
				s_dirty = 1;
			}
		}
		else if (event == KEY_EVENT_LONG)
		{
			return 1;	// 长按提交
		}
		break;
	case KEY_BACK:
		if (event == KEY_EVENT_SHORT)
		{
			if (e->count > 0)
			{
				// 删掉最后一位，并把它捡回来继续调
				e->count--;
				e->current = e->digits[e->count];
				s_dirty = 1;
			}
			else
			{
				return 2;	// 一位都没输入
			}
		}
		else if (event == KEY_EVENT_LONG)
		{
			// 一键清空
			digit_edit_reset(e);
			ui_show_hint("已清空");
			buzzer_notice();	// 提示：两短声
			s_dirty = 1;
		}
		break;
	default:
		break;
	}
	return 0;
}

// ================== 小工具 ==================

// 时间到点了吗？deadline 用 HAL_GetTick() + 时长 先算好
static uint8_t ui_time_up(uint32_t deadline)
{
	return ((int32_t)(HAL_GetTick() - deadline) >= 0) ? 1 : 0;
}

// 把 v 按固定宽度 w 写成十进制数字（例如 v=7、w=2 写成 "07"）
static void ui_num_to_str(char *buf, uint16_t v, uint8_t w)
{
	int8_t i;

	for (i = (int8_t)(w - 1); i >= 0; i--)
	{
		buf[i] = (char)('0' + v % 10);
		v = (uint16_t)(v / 10);
	}
}

// 显示一段文字并水平居中（自动混排 ASCII 和汉字）
static void ui_show_center(uint8_t y, const char *str, uint8_t color)
{
	uint16_t w = oled_text_width(str);
	uint8_t x = (w >= OLED_WIDTH) ? 0 : (uint8_t)((OLED_WIDTH - w) / 2);

	oled_show_text(x, y, str, color);
}

// 显示一条临时提示（显示 1.5 秒后自动消失）
static void ui_show_hint(const char *text)
{
	s_hint = text;
	s_hint_timer = HAL_GetTick() + HINT_SHOW_MS;
}

// 画标题栏（16 像素高的反白条 + 居中文字）
static void ui_draw_title(const char *text)
{
	oled_fill_rect(0, 0, OLED_WIDTH, 16, 1);
	ui_show_center(0, text, 0);
}

// 画数字行：已确认的显示 *，当前位显示正在调的数字，后面的显示 _
static void ui_draw_digit_line(const digit_edit_t *e, uint8_t y)
{
	char line[17];
	uint8_t i;

	for (i = 0; i < PWD_MAX_LEN; i++)
	{
		if (i < e->count)
		{
			line[i * 2] = '*';
		}
		else if (i == e->count)
		{
			line[i * 2] = (char)('0' + e->current);
		}
		else
		{
			line[i * 2] = '_';
		}
		if (i < (PWD_MAX_LEN - 1))
		{
			line[i * 2 + 1] = ' ';
		}
	}
	line[15] = '\0';
	oled_show_string(4, y, line, 1);
}

// ================== 主页 ==================
static const char *const s_week_names[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };

static void ui_home_key(uint8_t key, uint8_t event)
{
	if (key == KEY_OK && event == KEY_EVENT_SHORT)
	{
		// 进主菜单
		s_page = PAGE_MENU;
		s_menu_sel = 0;
		s_menu_top = 0;
		s_dirty = 1;
	}
}

static void ui_home_draw(void)
{
	const clock_time_t *t = clock_get();
	char buf[17];
	const char *week;

	oled_clear();

	ui_draw_title("主菜单");

	// 时间行 "hh:mm:ss"
	ui_num_to_str(buf, t->hour, 2);
	buf[2] = ':';
	ui_num_to_str(buf + 3, t->minute, 2);
	buf[5] = ':';
	ui_num_to_str(buf + 6, t->second, 2);
	buf[8] = '\0';
	ui_show_center(16, buf, 1);

	// 日期行 "yyyy-mm-dd Www"
	ui_num_to_str(buf, t->year, 4);
	buf[4] = '-';
	ui_num_to_str(buf + 5, t->month, 2);
	buf[7] = '-';
	ui_num_to_str(buf + 8, t->day, 2);
	buf[10] = ' ';
	week = s_week_names[t->weekday % 7];
	buf[11] = week[0];
	buf[12] = week[1];
	buf[13] = week[2];
	buf[14] = '\0';
	ui_show_center(32, buf, 1);

	// 底部提示
	ui_show_center(48, "按OK进菜单", 1);
}

// ================== 主菜单列表页 ==================

// 上下移动后，保证选中的项留在可见范围里（以后列表长了会自动滚动）
static void ui_menu_keep_visible(void)
{
	if (s_menu_sel < s_menu_top)
	{
		s_menu_top = s_menu_sel;
	}
	else if (s_menu_sel >= (uint8_t)(s_menu_top + MENU_VISIBLE))
	{
		s_menu_top = (uint8_t)(s_menu_sel - MENU_VISIBLE + 1);
	}
}

static void ui_menu_key(uint8_t key, uint8_t event)
{
	switch (key)
	{
	case KEY_UP:
		s_menu_sel = (uint8_t)((s_menu_sel + MENU_ITEM_COUNT - 1) % MENU_ITEM_COUNT);
		ui_menu_keep_visible();
		s_dirty = 1;
		break;
	case KEY_DOWN:
		s_menu_sel = (uint8_t)((s_menu_sel + 1) % MENU_ITEM_COUNT);
		ui_menu_keep_visible();
		s_dirty = 1;
		break;
	case KEY_OK:
		if (event == KEY_EVENT_SHORT)
		{
			if (s_menu_sel == 0)
			{
				ui_pwd_page_enter();		// 进入“密码锁”
				s_page = PAGE_PASSWORD;
			}
			else if (s_menu_sel == 1)
			{
				ui_set_page_enter();		// 进入“修改密码”
				s_page = PAGE_SETPWD;
			}
			s_dirty = 1;
		}
		break;
	case KEY_BACK:
		s_page = PAGE_HOME;
		s_dirty = 1;
		break;
	default:
		break;
	}
}

static void ui_menu_draw(void)
{
	uint8_t row;
	uint8_t idx;
	uint8_t y;

	oled_clear();

	ui_draw_title("菜单");

	// 列表：选中的一项反白（白底黑字）
	for (row = 0; row < MENU_VISIBLE; row++)
	{
		idx = (uint8_t)(s_menu_top + row);
		y = (uint8_t)(16 + row * 16);
		if (idx >= MENU_ITEM_COUNT)
		{
			break;
		}
		if (idx == s_menu_sel)
		{
			oled_fill_rect(0, y, OLED_WIDTH, 16, 1);
			oled_show_text(8, y, s_menu_names[idx], 0);
		}
		else
		{
			oled_show_text(8, y, s_menu_names[idx], 1);
		}
	}

	// 底部提示
	ui_show_center(48, "OK进入 BACK返回", 1);
}

// ================== 密码锁页 ==================
#define PWD_RESULT_NONE		0		// 正常（等待输入 / 输入中）
#define PWD_RESULT_OK		1		// 密码正确
#define PWD_RESULT_FAIL		2		// 密码错误
#define PWD_RESULT_LOCKED	3		// 已锁定（连续输错太多）

static uint8_t s_pwd_result;			// 上面的 PWD_RESULT_xxx
static uint32_t s_pwd_result_timer;		// 结果展示的截止时刻
static uint8_t s_pwd_fail_count;		// 连续输错次数（存在 Flash 里，掉电不丢）
static uint32_t s_pwd_lock_until;		// 锁定到什么时候（滴答时刻）

static void ui_pwd_page_enter(void)
{
	digit_edit_reset(&s_pwd_edit);
	s_hint = 0;
	s_hint_timer = 0;
	if (s_pwd_fail_count >= PWD_FAIL_MAX)
	{
		s_pwd_result = PWD_RESULT_LOCKED;	// 还在锁定中（可能是断电前锁的）
		led_blink(1);						// 锁定期间红灯慢闪
	}
	else
	{
		s_pwd_result = PWD_RESULT_NONE;
		led_off();
	}
}

// 提交校验
static void ui_pwd_page_submit(void)
{
	uint8_t i;
	uint8_t pwd[PWD_MAX_LEN];
	uint8_t pwd_len;
	uint8_t ok = 1;

	// 不足 4 位不给提交，只提示
	if (s_pwd_edit.count < PWD_MIN_LEN)
	{
		ui_show_hint("至少4位");
		buzzer_notice();	// 提示：两短声
		s_dirty = 1;
		return;
	}

	settings_get_password(pwd, &pwd_len);
	if (s_pwd_edit.count != pwd_len)
	{
		ok = 0;
	}
	else
	{
		for (i = 0; i < pwd_len; i++)
		{
			if (s_pwd_edit.digits[i] != pwd[i])
			{
				ok = 0;
				break;
			}
		}
	}

	s_hint = 0;
	s_hint_timer = 0;

	if (ok)
	{
		// 密码正确：清掉错误次数（顺带写 Flash）
		if (s_pwd_fail_count > 0)
		{
			s_pwd_fail_count = 0;
			settings_set_fail_count(0);
		}
		s_pwd_result = PWD_RESULT_OK;
		s_pwd_result_timer = HAL_GetTick() + MSG_SHOW_MS;
		led_green();		// 绿灯亮（跟屏幕“输入成功”一起亮 2 秒）
		buzzer_success();	// 正确：上行“叮-咚”
	}
	else
	{
		s_pwd_fail_count++;
		settings_set_fail_count(s_pwd_fail_count);	// 错误次数存 Flash，断电也记得

		if (s_pwd_fail_count >= PWD_FAIL_MAX)
		{
			// 错够次数：进入锁定，开始倒计时
			s_pwd_result = PWD_RESULT_LOCKED;
			s_pwd_lock_until = HAL_GetTick() + PWD_LOCK_MS;
			led_blink(1);	// 锁定期间红灯慢闪
		}
		else
		{
			s_pwd_result = PWD_RESULT_FAIL;
			s_pwd_result_timer = HAL_GetTick() + MSG_SHOW_MS;
			led_red();		// 红灯亮（跟屏幕“密码错误”一起亮 2 秒）
		}
		buzzer_fail();	// 错误：下行“嘟-呜”
	}
	s_dirty = 1;
}

static void ui_pwd_page_key(uint8_t key, uint8_t event)
{
	uint8_t r;

	// 锁定中：只允许 BACK 退出页面，其他键不理
	if (s_pwd_result == PWD_RESULT_LOCKED)
	{
		if (key == KEY_BACK)
		{
			s_page = PAGE_MENU;
			led_off();		// 离开页面，停止慢闪
			s_dirty = 1;
		}
		return;
	}

	// 结果展示的 2 秒里，按键全部忽略（防止手快误触）
	if (s_pwd_result != PWD_RESULT_NONE)
	{
		return;
	}

	r = digit_edit_key(&s_pwd_edit, key, event);
	if (r == 1)
	{
		ui_pwd_page_submit();
	}
	else if (r == 2)
	{
		// 一位都没输入时按 BACK：回菜单
		s_page = PAGE_MENU;
		s_dirty = 1;
	}
}

static void ui_pwd_page_draw(void)
{
	char msg[24];
	int32_t left_ms;

	oled_clear();

	ui_draw_title("密码锁");

	// 锁定画面：显示倒计时
	if (s_pwd_result == PWD_RESULT_LOCKED)
	{
		ui_show_center(16, "已锁定", 1);

		left_ms = (int32_t)(s_pwd_lock_until - HAL_GetTick());
		if (left_ms < 0)
		{
			left_ms = 0;
		}
		sprintf(msg, "请等待 %d 秒", (int)((left_ms + 999) / 1000));
		ui_show_center(32, msg, 1);

		sprintf(msg, "错误 %d 次", PWD_FAIL_MAX);
		ui_show_center(48, msg, 1);
		return;
	}

	// 状态行
	if (s_pwd_result == PWD_RESULT_OK)
	{
		ui_show_center(16, "密码正确", 1);
	}
	else if (s_pwd_result == PWD_RESULT_FAIL)
	{
		ui_show_center(16, "密码错误", 1);
	}
	else if (s_pwd_edit.count == 0)
	{
		ui_show_center(16, "等待输入", 1);
	}
	else
	{
		sprintf(msg, "已输入 %d 位", s_pwd_edit.count);
		ui_show_center(16, msg, 1);
	}

	// 数字行
	ui_draw_digit_line(&s_pwd_edit, 32);

	// 底部提示行
	if (s_pwd_result == PWD_RESULT_OK)
	{
		ui_show_center(48, "输入成功", 1);
	}
	else if (s_pwd_result == PWD_RESULT_FAIL)
	{
		sprintf(msg, "还剩 %d 次", PWD_FAIL_MAX - s_pwd_fail_count);
		ui_show_center(48, msg, 1);
	}
	else if (s_hint != 0)
	{
		ui_show_center(48, s_hint, 1);
	}
	else if (s_pwd_fail_count > 0)
	{
		sprintf(msg, "还剩 %d 次", PWD_FAIL_MAX - s_pwd_fail_count);
		ui_show_center(48, msg, 1);
	}
	else if (s_pwd_edit.count == 0)
	{
		ui_show_center(48, "请输入密码", 1);
	}
	else
	{
		ui_show_center(48, "长按OK=提交", 1);
	}
}

// ================== 设置密码页 ==================
// 步骤：1=输入新密码  2=再输一次  3=修改成功（展示 2 秒后回密码锁页）  4=两次不一致（展示后重来）
static uint8_t s_set_step;
static uint32_t s_set_timer;				// 结果展示的截止时刻
static uint8_t s_set_first[PWD_MAX_LEN];	// 第一次输入的新密码（用来比对第二次）
static uint8_t s_set_first_len;

static void ui_set_page_enter(void)
{
	digit_edit_reset(&s_set_edit);
	s_set_step = 1;
	s_set_first_len = 0;
	s_hint = 0;
	s_hint_timer = 0;
}

// 提交（长按 OK 时调用）
static void ui_set_page_submit(void)
{
	uint8_t i;
	uint8_t same;

	// 不足 4 位不给提交，只提示
	if (s_set_edit.count < PWD_MIN_LEN)
	{
		ui_show_hint("至少4位");
		buzzer_notice();	// 提示：两短声
		s_dirty = 1;
		return;
	}

	if (s_set_step == 1)
	{
		// 第一次输入完成：先把新密码记下来，清空后要求再输一次
		for (i = 0; i < PWD_MAX_LEN; i++)
		{
			s_set_first[i] = s_set_edit.digits[i];
		}
		s_set_first_len = s_set_edit.count;
		digit_edit_reset(&s_set_edit);
		s_set_step = 2;
		buzzer_confirm();	// 确认：进入“再输一次”
		s_dirty = 1;
		return;
	}

	// 第二次输入：两次一样才保存
	same = (s_set_edit.count == s_set_first_len) ? 1 : 0;
	for (i = 0; i < s_set_first_len && same; i++)
	{
		if (s_set_edit.digits[i] != s_set_first[i])
		{
			same = 0;
		}
	}

	if (same)
	{
		settings_set_password(s_set_first, s_set_first_len);	// 存进 Flash，掉电不丢
		s_pwd_fail_count = 0;	// 换了新密码，错误次数清零
		settings_set_fail_count(0);
		s_set_step = 3;
		led_green();		// 绿灯亮（跟“修改成功”一起）
		buzzer_success();	// 成功：上行“叮-咚”
	}
	else
	{
		s_set_step = 4;
		led_red();			// 红灯亮（跟“两次不一致”一起）
		buzzer_fail();		// 失败：下行“嘟-呜”
	}
	s_set_timer = HAL_GetTick() + MSG_SHOW_MS;
	s_hint = 0;
	s_hint_timer = 0;
	s_dirty = 1;
}

static void ui_set_page_key(uint8_t key, uint8_t event)
{
	uint8_t r;

	// 成功/失败展示的 2 秒里，按键全部忽略
	if (s_set_step >= 3)
	{
		return;
	}

	r = digit_edit_key(&s_set_edit, key, event);
	if (r == 1)
	{
		ui_set_page_submit();
	}
	else if (r == 2)
	{
		// 一位都没输入时按 BACK
		if (s_set_step == 2)
		{
			// 第二步退回第一步，重新输新密码
			digit_edit_reset(&s_set_edit);
			s_set_step = 1;
			s_dirty = 1;
		}
		else
		{
			s_page = PAGE_MENU;
			s_dirty = 1;
		}
	}
}

static void ui_set_page_draw(void)
{
	oled_clear();

	ui_draw_title("设置密码");

	// 状态行
	if (s_set_step == 3)
	{
		ui_show_center(16, "修改成功", 1);
	}
	else if (s_set_step == 4)
	{
		ui_show_center(16, "两次不一致", 1);
	}
	else if (s_set_step == 2)
	{
		ui_show_center(16, "再输一次", 1);
	}
	else
	{
		ui_show_center(16, "输入新密码", 1);
	}

	// 数字行
	ui_draw_digit_line(&s_set_edit, 32);

	// 底部提示行
	if (s_set_step == 3)
	{
		ui_show_center(48, "已保存", 1);
	}
	else if (s_set_step == 4)
	{
		ui_show_center(48, "请重新输入", 1);
	}
	else if (s_hint != 0)
	{
		ui_show_center(48, s_hint, 1);
	}
	else
	{
		ui_show_center(48, "长按OK=提交", 1);
	}
}

// ================== 页面调度 ==================

static void ui_handle_key(uint8_t key, uint8_t event)
{
	s_last_key_tick = HAL_GetTick();	// 记录按键时刻（自动回主页用）

	// 按键提示音：短按/长按“嘀”一声（连发不响，免得连续滚动时一直叫）
	if (event == KEY_EVENT_SHORT || event == KEY_EVENT_LONG)
	{
		buzzer_key();
	}

	switch (s_page)
	{
	case PAGE_HOME:
		ui_home_key(key, event);
		break;
	case PAGE_MENU:
		ui_menu_key(key, event);
		break;
	case PAGE_PASSWORD:
		ui_pwd_page_key(key, event);
		break;
	case PAGE_SETPWD:
		ui_set_page_key(key, event);
		break;
	default:
		break;
	}
}

static void ui_draw_page(void)
{
	switch (s_page)
	{
	case PAGE_HOME:
		ui_home_draw();
		break;
	case PAGE_MENU:
		ui_menu_draw();
		break;
	case PAGE_PASSWORD:
		ui_pwd_page_draw();
		break;
	case PAGE_SETPWD:
		ui_set_page_draw();
		break;
	default:
		break;
	}
	oled_refresh();		// 统一在这里把画面推到屏幕
}

// ================== 对外接口 ==================

// 初始化：屏幕 -> 时钟 -> 密码存储 -> 画第一屏
void ui_init(void)
{
	led_init();		// 先处理状态灯（改为开漏、熄灭），尽量早上电保持灭
	oled_init();
	clock_init();
	settings_init();
	buzzer_init();

	// 读连续输错次数；如果上次断电前是锁定状态，重启后继续锁 30 秒
	s_pwd_fail_count = settings_get_fail_count();
	if (s_pwd_fail_count >= PWD_FAIL_MAX)
	{
		s_pwd_lock_until = HAL_GetTick() + PWD_LOCK_MS;
	}

	s_page = PAGE_HOME;
	s_dirty = 1;
	s_last_tick = HAL_GetTick();
	s_last_key_tick = HAL_GetTick();
	ui_draw_page();
	buzzer_confirm();	// 开机一声短“嘀”：听到了说明蜂鸣器接线正常
}

// 主循环任务：处理按键、走时、定时任务、刷新屏幕
void ui_task(void)
{
	uint32_t now;
	uint32_t elapsed;
	uint32_t whole_secs;
	uint8_t key;
	uint8_t event;

	// 1. 处理所有排队的按键事件
	while (key_get_event(&key, &event))
	{
		ui_handle_key(key, event);
	}

	// 2. 走时：攒够 1 秒推 1 秒；主页的时间要跟着刷新
	now = HAL_GetTick();
	elapsed = now - s_last_tick;
	whole_secs = elapsed / 1000;
	if (whole_secs > 0)
	{
		s_last_tick += whole_secs * 1000;
		clock_add_seconds(whole_secs);
		if (s_page == PAGE_HOME ||
			(s_page == PAGE_PASSWORD && s_pwd_result == PWD_RESULT_LOCKED))
		{
			s_dirty = 1;	// 主页走时 + 锁定倒计时都要每秒刷新
		}
	}

	// 3. 密码锁页：正确/错误展示结束 -> 自动清空；锁定到期 -> 清零错误次数、解除锁定
	if (s_pwd_result == PWD_RESULT_OK || s_pwd_result == PWD_RESULT_FAIL)
	{
		if (s_page == PAGE_PASSWORD && ui_time_up(s_pwd_result_timer))
		{
			digit_edit_reset(&s_pwd_edit);
			s_pwd_result = PWD_RESULT_NONE;
			led_off();			// 结果展示结束，灯灭
			s_dirty = 1;
		}
	}
	else if (s_pwd_result == PWD_RESULT_LOCKED && ui_time_up(s_pwd_lock_until))
	{
		// 锁定到期：解除锁定，但【不清零】错误次数——只有输对密码才清零。
		// 到期后只再给 1 次机会（次数压回“上限-1”），再错一次立刻重新锁
		if (s_pwd_fail_count >= PWD_FAIL_MAX)
		{
			s_pwd_fail_count = (uint8_t)(PWD_FAIL_MAX - 1);
			settings_set_fail_count(s_pwd_fail_count);
		}
		s_pwd_result = PWD_RESULT_NONE;
		led_off();				// 停止慢闪
		if (s_page == PAGE_PASSWORD)
		{
			buzzer_confirm();	// “可以重新输入了”
		}
		s_dirty = 1;
	}

	// 4. 设置密码页：结果展示结束
	if (s_page == PAGE_SETPWD)
	{
		if (s_set_step == 3 && ui_time_up(s_set_timer))
		{
			// 修改成功 -> 回到密码锁页，可以直接输新密码
			ui_pwd_page_enter();
			s_page = PAGE_PASSWORD;
			s_dirty = 1;
		}
		else if (s_set_step == 4 && ui_time_up(s_set_timer))
		{
			// 两次不一致 -> 回到第一步重新输
			digit_edit_reset(&s_set_edit);
			s_set_step = 1;
			led_off();			// 红灯灭
			s_dirty = 1;
		}
	}

	// 5. 临时提示到点 -> 清掉，恢复普通提示
	if (s_hint != 0 && ui_time_up(s_hint_timer))
	{
		s_hint = 0;
		s_dirty = 1;
	}

	// 6. 菜单页长时间没操作 -> 自动回主页
	if (s_page == PAGE_MENU && (uint32_t)(now - s_last_key_tick) > MENU_IDLE_HOME_MS)
	{
		s_page = PAGE_HOME;
		s_dirty = 1;
	}

	// 7. 状态灯慢闪计时（锁定时的红灯闪烁）
	led_task();

	// 8. 需要就重画
	if (s_dirty)
	{
		s_dirty = 0;
		ui_draw_page();
	}
}
