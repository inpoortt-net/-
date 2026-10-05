// key.c —— 4 个按键的驱动
// 引脚：PA0=UP、PA1=DOWN、PA2=OK、PA3=BACK（CubeMX 已配好“输入 + 内部上拉”，按下是低电平）
// 用法：key_tick() 放进 SysTick 中断（1ms 一次）；主循环用 key_get_event() 取事件
//
// 消抖（去抖）说明：
//   按钮刚按下/松开的一瞬间，内部金属片会弹跳，电平抖来抖去。这里给每个键做了一套
//   状态机：只有连续 KEY_PRESS_MS 毫秒都读到“按下”才确认按下；只有连续 KEY_RELEASE_MS
//   毫秒都读到“松开”才确认松开。中途任何一次读数不对就重新计时——所以
//     · 抖动、毛刺不会造成误触发（按一下只会出 1 个事件）
//     · 按住过程中接触抖了一下，也不会误判成“松开再按下”
//   参数都在下面，想改手感直接改数字（单位都是毫秒，和扫描周期无关）。
// 三种事件：短按 / 长按 / 连发（连发用于“按住不放快速滚动”）
#include"key.h"

// ================== 参数（想改手感就改这里） ==================
#define KEY_SCAN_MS			5		// 每 5ms 扫描一次按键
#define KEY_PRESS_MS		20		// 按下消抖：连续稳定 20ms 才算真的按下
#define KEY_RELEASE_MS		20		// 松开消抖：连续稳定 20ms 才算真的松开
#define KEY_LONG_MS			800		// 按住 800ms：触发长按
#define KEY_REPEAT_MS		200		// 长按之后，每 200ms 触发一次连发

#define KEY_QUEUE_SIZE		8		// 事件队列大小（必须是 2 的幂）

// ================== 内部变量 ==================
static GPIO_TypeDef *const s_key_port[KEY_COUNT] =
{
	KEY_UP_GPIO_Port, KEY_DOWN_GPIO_Port, KEY_OK_GPIO_Port, KEY_BACK_GPIO_Port
};
static const uint16_t s_key_pin[KEY_COUNT] =
{
	KEY_UP_Pin, KEY_DOWN_Pin, KEY_OK_Pin, KEY_BACK_Pin
};

// 按键的四个状态：
//   IDLE --(读到按下)--> PRESS_DEB --(稳定 20ms)--> PRESSED
//   PRESSED --(读到松开)--> RELEASE_DEB --(稳定 20ms)--> IDLE + 发短按事件
//   在 PRESSED 期间计时：满 800ms 发长按；之后每 200ms 发连发；
//   发过长按的，松手时就不再补发短按
typedef enum
{
	KEY_ST_IDLE = 0,	// 松开（稳定）
	KEY_ST_PRESS_DEB,	// 按下消抖中
	KEY_ST_PRESSED,		// 按住（稳定）
	KEY_ST_RELEASE_DEB	// 松开消抖中
} key_state_e;

typedef struct
{
	uint8_t state;			// 当前状态（上面的枚举）
	uint16_t deb_ms;		// 消抖计时（毫秒）
	uint16_t hold_ms;		// 按下总共持续了多少毫秒
	uint16_t repeat_ms;		// 连发计时（毫秒）
	uint8_t long_sent;		// 长按事件发过了吗
} key_state_t;

static key_state_t s_keys[KEY_COUNT];
static uint8_t s_scan_div = 0;		// 把 1ms 分频成每 KEY_SCAN_MS 毫秒扫一轮

// 事件队列：中断里往里放，主循环往外取（一放一取，不用加锁）
// 每个事件打包成一个 16 位数：高 8 位=事件类型，低 8 位=按键编号
static volatile uint16_t s_queue[KEY_QUEUE_SIZE];
static volatile uint8_t s_q_head = 0;
static volatile uint8_t s_q_tail = 0;

// 往队列里放一个事件（队列满了就丢弃，正常用不会满）
static void key_push(uint8_t key, uint8_t event)
{
	uint8_t next = (uint8_t)((s_q_head + 1) & (KEY_QUEUE_SIZE - 1));

	if (next != s_q_tail)
	{
		s_queue[s_q_head] = (uint16_t)(((uint16_t)event << 8) | key);
		s_q_head = next;
	}
}

// 处理一个按键的一次采样（raw：1=当前读到按下，0=当前读到松开）
static void key_process(uint8_t idx, uint8_t raw)
{
	key_state_t *k = &s_keys[idx];

	switch (k->state)
	{
	case KEY_ST_IDLE:
		if (raw)
		{
			// 疑似按下，开始消抖
			k->state = KEY_ST_PRESS_DEB;
			k->deb_ms = KEY_SCAN_MS;
		}
		break;

	case KEY_ST_PRESS_DEB:
		if (raw)
		{
			k->deb_ms = (uint16_t)(k->deb_ms + KEY_SCAN_MS);
			if (k->deb_ms >= KEY_PRESS_MS)
			{
				// 连续稳定：确认按下
				k->state = KEY_ST_PRESSED;
				k->hold_ms = 0;
				k->repeat_ms = 0;
				k->long_sent = 0;
			}
		}
		else
		{
			k->state = KEY_ST_IDLE;		// 只是一点抖动，不算按下
		}
		break;

	case KEY_ST_PRESSED:
		if (raw)
		{
			k->hold_ms = (uint16_t)(k->hold_ms + KEY_SCAN_MS);
			if (!k->long_sent)
			{
				if (k->hold_ms >= KEY_LONG_MS)
				{
					key_push(idx, KEY_EVENT_LONG);
					k->long_sent = 1;
					k->repeat_ms = 0;
				}
			}
			else
			{
				k->repeat_ms = (uint16_t)(k->repeat_ms + KEY_SCAN_MS);
				if (k->repeat_ms >= KEY_REPEAT_MS)
				{
					key_push(idx, KEY_EVENT_REPEAT);
					k->repeat_ms = 0;
				}
			}
		}
		else
		{
			// 疑似松开，开始消抖
			k->state = KEY_ST_RELEASE_DEB;
			k->deb_ms = KEY_SCAN_MS;
		}
		break;

	case KEY_ST_RELEASE_DEB:
		if (raw)
		{
			// 原来只是接触抖了一下，又按住了：回到按住状态，不产生事件
			k->state = KEY_ST_PRESSED;
		}
		else
		{
			k->deb_ms = (uint16_t)(k->deb_ms + KEY_SCAN_MS);
			if (k->deb_ms >= KEY_RELEASE_MS)
			{
				// 连续稳定：确认松开
				k->state = KEY_ST_IDLE;
				if (!k->long_sent)
				{
					key_push(idx, KEY_EVENT_SHORT);		// 短按
				}
			}
		}
		break;

	default:
		// 不应该发生，兜底
		k->state = KEY_ST_IDLE;
		break;
	}
}

// 初始化：清空状态和队列（引脚的初始化在 CubeMX 生成的 MX_GPIO_Init 里）
void key_init(void)
{
	uint8_t i;

	for (i = 0; i < KEY_COUNT; i++)
	{
		s_keys[i].state = KEY_ST_IDLE;
		s_keys[i].deb_ms = 0;
		s_keys[i].hold_ms = 0;
		s_keys[i].repeat_ms = 0;
		s_keys[i].long_sent = 0;
	}
	s_scan_div = 0;
	s_q_head = 0;
	s_q_tail = 0;
}

// 每 1ms 调一次（放在 SysTick 中断里），内部自己分频成每 KEY_SCAN_MS 毫秒扫一轮
void key_tick(void)
{
	uint8_t i;
	uint8_t raw;

	s_scan_div++;
	if (s_scan_div < KEY_SCAN_MS)
	{
		return;
	}
	s_scan_div = 0;

	for (i = 0; i < KEY_COUNT; i++)
	{
		// 读引脚：低电平 = 按下
		raw = (HAL_GPIO_ReadPin(s_key_port[i], s_key_pin[i]) == GPIO_PIN_RESET) ? 1 : 0;
		key_process(i, raw);
	}
}

// 主循环取事件：取到返回 1，并把按键号和事件类型填进 *key / *event；没有事件返回 0
uint8_t key_get_event(uint8_t *key, uint8_t *event)
{
	uint16_t v;

	if (s_q_head == s_q_tail)
	{
		return 0;
	}
	v = s_queue[s_q_tail];
	s_q_tail = (uint8_t)((s_q_tail + 1) & (KEY_QUEUE_SIZE - 1));
	*key = (uint8_t)(v & 0xFF);
	*event = (uint8_t)(v >> 8);
	return 1;
}
