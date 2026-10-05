// buzzer.c —— 蜂鸣器驱动（低电平触发的无源蜂鸣器模块，接 PB0 / TIM3_CH3）
// 发声原理：PWM 的频率 = 音调；不想响的时候把占空比设成 0
//（CubeMX 里 CH Polarity = Low：占空比 0 时引脚保持高电平，模块不通电、不发热）
// 播放是后台进行的（SysTick 里每 10ms 走一步），不会卡住按键和屏幕
#include"buzzer.h"

// main.c 里定义的 TIM3 句柄
extern TIM_HandleTypeDef htim3;

// ================== 参数（想改音色就改这里） ==================
// 定时器计数频率：72MHz / (71+1) = 1MHz（和 CubeMX 里的 Prescaler=71 对应）

// 一个音符：频率（Hz，0=静音间隔）、时长（毫秒）
typedef struct
{
	uint16_t freq;
	uint16_t ms;
} buzzer_note_t;

// 各场景的音符表
static const buzzer_note_t s_key_notes[]     = { { 2000, 25 } };							// 按键：短嘀
static const buzzer_note_t s_confirm_notes[] = { { 1600, 80 } };							// 确认：中音短声
static const buzzer_note_t s_success_notes[] = { { 2000, 120 }, { 2700, 180 } };			// 成功：上行叮咚
static const buzzer_note_t s_fail_notes[]    = { { 2700, 180 }, { 1400, 320 } };			// 失败：下行嘟呜
static const buzzer_note_t s_notice_notes[]  = { { 1800, 40 }, { 0, 60 }, { 1800, 40 } };	// 提示：两短声

// ================== 内部变量 ==================
static const buzzer_note_t *s_seq = 0;	// 正在播放的序列（0 = 没在播）
static uint8_t s_seq_len = 0;			// 序列里有几个音符
static uint8_t s_seq_pos = 0;			// 正在播第几个
static uint16_t s_note_left = 0;		// 当前音符还剩多少毫秒
static uint8_t s_tick_div = 0;			// 把 1ms 分频成 10ms 用

// 让蜂鸣器安静：占空比 0（配合 CH Polarity=Low，引脚是高电平，模块不通电）
static void buzzer_silent(void)
{
	__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0);
}

// 开始播放一个音符
static void buzzer_play_note(const buzzer_note_t *note)
{
	uint16_t arr;

	if (note->freq == 0)
	{
		buzzer_silent();	// 静音音符（拿来做间隔）
	}
	else
	{
		// 频率 -> 自动重装值：1MHz / 频率 - 1
		arr = (uint16_t)(1000000UL / note->freq - 1);
		__HAL_TIM_SET_AUTORELOAD(&htim3, arr);
		__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, (uint16_t)((arr + 1) / 2));	// 50% 占空比
	}
	s_note_left = note->ms;
}

// 从头播一段序列（会打断上一段）
static void buzzer_play(const buzzer_note_t *seq, uint8_t len)
{
	// 先置 0 让中断暂时“停播”，避免主循环和中断同时改状态出乱子
	s_seq = 0;
	s_seq_len = len;
	s_seq_pos = 0;
	buzzer_play_note(&seq[0]);
	s_seq = seq;	// 最后一步再“开播”
}

// 初始化：启动 PWM 输出（占空比 0 = 安静）
void buzzer_init(void)
{
	buzzer_silent();
	HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);
	s_seq = 0;
	s_seq_pos = 0;
	s_note_left = 0;
	s_tick_div = 0;
}

// 每 1ms 调一次（放在 SysTick 中断里），每 10ms 走一步音序
void buzzer_tick(void)
{
	s_tick_div++;
	if (s_tick_div < 10)
	{
		return;
	}
	s_tick_div = 0;

	if (s_seq == 0)
	{
		return;		// 没在播
	}
	if (s_note_left > 10)
	{
		s_note_left = (uint16_t)(s_note_left - 10);
		return;		// 当前音符还没播完
	}

	// 当前音符播完，换下一个
	s_seq_pos++;
	if (s_seq_pos >= s_seq_len)
	{
		s_seq = 0;
		buzzer_silent();
		return;
	}
	buzzer_play_note(&s_seq[s_seq_pos]);
}

// ================== 各场景音效 ==================
void buzzer_key(void)
{
	buzzer_play(s_key_notes, (uint8_t)(sizeof(s_key_notes) / sizeof(s_key_notes[0])));
}

void buzzer_confirm(void)
{
	buzzer_play(s_confirm_notes, (uint8_t)(sizeof(s_confirm_notes) / sizeof(s_confirm_notes[0])));
}

void buzzer_success(void)
{
	buzzer_play(s_success_notes, (uint8_t)(sizeof(s_success_notes) / sizeof(s_success_notes[0])));
}

void buzzer_fail(void)
{
	buzzer_play(s_fail_notes, (uint8_t)(sizeof(s_fail_notes) / sizeof(s_fail_notes[0])));
}

void buzzer_notice(void)
{
	buzzer_play(s_notice_notes, (uint8_t)(sizeof(s_notice_notes) / sizeof(s_notice_notes[0])));
}
