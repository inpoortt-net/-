// led.c —— 红/绿状态指示灯（红灯=PB1、绿灯=PA4，开漏输出）
//
// 【接线】3.3V → LED 长脚(+) → LED 短脚(−) → 限流电阻(220Ω~1kΩ) → 引脚。
// 【亮灭】开漏 + 低电平点亮：引脚拉低 = 亮；引脚放开（高）= 灭。
//         两个亮灭动作都集中在 led_red_write()/led_green_write()，改极性只改这两处。
// 【引脚配置】led_pins_config() 上电把 PA4/PB1 统一配成开漏输出，
//         不依赖 CubeMX 里的配置（重新生成代码也不会丢）。
// 【用途】密码正确亮绿 / 错误亮红 / 锁定期间红灯慢闪（每 500ms 翻转）。
// 【体检开关】LED_TEST_ALWAYS_ON = 1：两个灯上电常亮，用来查接线/极性/电阻；
//         正常使用必须改回 0。
// 【非阻塞】慢闪靠“看时刻”翻转，不用延时等待（见 led_task）。

#include"led.h"

// ================== 硬件测试开关 ==================
// 1 = 两个灯上电常亮（硬件体检用，查接线/极性/电阻）；
// 0 = 正常灯光逻辑（正确绿、错误红、锁定慢闪）。
// 需要体检时把这里改成 1 重新编译烧录即可。
#define LED_TEST_ALWAYS_ON	0

#if !LED_TEST_ALWAYS_ON
#define LED_BLINK_MS	500		// 慢闪周期：每 500ms 翻转一次

static uint8_t s_blink_on = 0;		// 慢闪模式开着吗
static uint8_t s_blink_level = 0;	// 当前闪烁电平（1=亮）
static uint32_t s_blink_timer = 0;	// 上次翻转的时刻
#endif

// 把两个 LED 引脚配置成“开漏输出”
//（开漏：拉低 = 导通点亮；放开 = 高阻熄灭。配合“LED 接 3.3V 一侧”的接法）
// 说明：CubeMX 里配的是推挽也没关系，这里上电统一改成开漏，行为一致
static void led_pins_config(void)
{
	GPIO_InitTypeDef gpio = {0};

	gpio.Mode = GPIO_MODE_OUTPUT_OD;
	gpio.Pull = GPIO_NOPULL;
	gpio.Speed = GPIO_SPEED_FREQ_LOW;
	gpio.Pin = LED_GREEN_Pin;
	HAL_GPIO_Init(LED_GREEN_GPIO_Port, &gpio);
	gpio.Pin = LED_RED_Pin;
	HAL_GPIO_Init(LED_RED_GPIO_Port, &gpio);
}

// 红灯 开/关（开漏接法：拉低 = 亮，放开 = 灭）
static void led_red_write(uint8_t on)
{
	HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

// 绿灯 开/关（开漏接法：拉低 = 亮，放开 = 灭）
static void led_green_write(uint8_t on)
{
	HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

// 初始化：先把引脚配成开漏，再全部熄灭（测试模式下两个灯常亮）
void led_init(void)
{
	led_pins_config();
#if LED_TEST_ALWAYS_ON
	led_green_write(1);		// 测试：绿灯常亮
	led_red_write(1);		// 测试：红灯常亮
#else
	led_off();
#endif
}

// 全部熄灭（同时停止慢闪）
void led_off(void)
{
#if !LED_TEST_ALWAYS_ON
	s_blink_on = 0;
	s_blink_level = 0;
	led_red_write(0);
	led_green_write(0);
#endif
}

// 绿灯亮（红灯灭）
void led_green(void)
{
#if !LED_TEST_ALWAYS_ON
	s_blink_on = 0;
	led_red_write(0);
	led_green_write(1);
#endif
}

// 红灯亮（绿灯灭）
void led_red(void)
{
#if !LED_TEST_ALWAYS_ON
	s_blink_on = 0;
	led_green_write(0);
	led_red_write(1);
#endif
}

// 慢闪开关：on=1 红灯慢闪；on=0 停止并全部熄灭
void led_blink(uint8_t on)
{
#if !LED_TEST_ALWAYS_ON
	if (on)
	{
		s_blink_on = 1;
		s_blink_level = 1;
		s_blink_timer = HAL_GetTick();
		led_green_write(0);
		led_red_write(1);		// 先亮起来，之后每 500ms 翻转
	}
	else
	{
		led_off();
	}
#else
	(void)on;		// 测试模式用不到这个参数
#endif
}

// 主循环里调用：处理慢闪计时
void led_task(void)
{
#if !LED_TEST_ALWAYS_ON
	if (!s_blink_on)
	{
		return;
	}
	if ((int32_t)(HAL_GetTick() - s_blink_timer) >= LED_BLINK_MS)
	{
		s_blink_timer += LED_BLINK_MS;
		s_blink_level = (uint8_t)(!s_blink_level);
		led_red_write(s_blink_level);
	}
#endif
}
