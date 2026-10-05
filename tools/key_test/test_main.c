// key.c 的主机模拟测试：不用接板子，在电脑上模拟按键波形，验证消抖和事件对不对
// 编译：gcc -std=c99 -O1 -o key_test.exe test_main.c key.c
#include <stdio.h>
#include <string.h>
#include "key.h"

uint8_t g_test_level[4];		// 假电平（给 main.h 里的假读引脚函数用）

static uint32_t sim_ms = 0;		// 模拟时间（毫秒）

typedef struct
{
	uint32_t ms;
	uint8_t key;
	uint8_t ev;
} ev_rec_t;

static ev_rec_t s_events[128];
static int s_event_count = 0;
static int s_fail = 0;

// 模拟过 ms 毫秒：每 1ms 调一次 key_tick()，并收集产生的事件
static void run_ms(uint32_t ms)
{
	uint32_t i;
	uint8_t k, e;

	for (i = 0; i < ms; i++)
	{
		sim_ms++;
		key_tick();
		while (key_get_event(&k, &e))
		{
			if (s_event_count < 128)
			{
				s_events[s_event_count].ms = sim_ms;
				s_events[s_event_count].key = k;
				s_events[s_event_count].ev = e;
				s_event_count++;
			}
		}
	}
}

static void test_begin(const char *name)
{
	s_event_count = 0;
	memset(g_test_level, 0, sizeof(g_test_level));
	key_init();
	sim_ms = 0;
	printf("== %s ==\n", name);
}

// 检查：第 n 个事件是不是 指定按键+类型，时间在 [lo, hi] 毫秒之间
static void check_event(int n, uint8_t key, uint8_t ev, uint32_t lo, uint32_t hi)
{
	if (n >= s_event_count)
	{
		printf("  [FAIL] 第 %d 个事件不存在（期望 ev=%d）\n", n, ev);
		s_fail++;
		return;
	}
	if (s_events[n].key != key || s_events[n].ev != ev ||
		s_events[n].ms < lo || s_events[n].ms > hi)
	{
		printf("  [FAIL] 第 %d 个事件：实际 key=%d ev=%d t=%lums，期望 key=%d ev=%d t=%lu~%lu\n",
			n, s_events[n].key, s_events[n].ev, (unsigned long)s_events[n].ms,
			key, ev, (unsigned long)lo, (unsigned long)hi);
		s_fail++;
		return;
	}
	printf("  [OK] 事件 %d：ev=%d @%lums\n", n, ev, (unsigned long)s_events[n].ms);
}

// 检查事件总数
static void check_count(int n)
{
	if (s_event_count != n)
	{
		printf("  [FAIL] 事件总数 = %d，期望 %d\n", s_event_count, n);
		s_fail++;
	}
	else
	{
		printf("  [OK] 事件总数 = %d\n", n);
	}
}

// ---------- 测试用例 ----------

// 1. 干净短按：按下 100ms 松开 → 恰好 1 个 SHORT
static void test_clean_short(void)
{
	test_begin("干净短按 → 恰好 1 个 SHORT");
	g_test_level[KEY_UP] = 1;
	run_ms(100);
	g_test_level[KEY_UP] = 0;
	run_ms(100);
	check_count(1);
	check_event(0, KEY_UP, KEY_EVENT_SHORT, 100, 150);
}

// 2. 带弹跳的短按：按下、松开各抖 10ms → 仍应恰好 1 个 SHORT
static void test_bouncy_short(void)
{
	uint32_t t;

	test_begin("弹跳短按（按下/松开各抖 10ms）→ 仍只有 1 个 SHORT");
	for (t = 0; t < 10; t++)
	{
		g_test_level[KEY_OK] = (uint8_t)(t % 2);
		run_ms(1);
	}
	g_test_level[KEY_OK] = 1;
	run_ms(90);
	for (t = 0; t < 10; t++)
	{
		g_test_level[KEY_OK] = (uint8_t)((t % 2) ? 0 : 1);
		run_ms(1);
	}
	g_test_level[KEY_OK] = 0;
	run_ms(100);
	check_count(1);
	check_event(0, KEY_OK, KEY_EVENT_SHORT, 100, 170);
}

// 3. 10ms 毛刺：不应产生任何事件
static void test_glitch(void)
{
	test_begin("10ms 毛刺干扰 → 0 个事件");
	g_test_level[KEY_DOWN] = 1;
	run_ms(10);
	g_test_level[KEY_DOWN] = 0;
	run_ms(200);
	check_count(0);
}

// 4. 长按 1.2 秒：LONG ≈800ms，之后每 200ms 一个 REPEAT，松开不产生 SHORT
static void test_long_repeat(void)
{
	test_begin("长按 1.2s → LONG + REPEAT，松开无 SHORT");
	g_test_level[KEY_UP] = 1;
	run_ms(1200);
	g_test_level[KEY_UP] = 0;
	run_ms(100);
	check_count(2);
	check_event(0, KEY_UP, KEY_EVENT_LONG, 800, 850);
	check_event(1, KEY_UP, KEY_EVENT_REPEAT, 1000, 1070);
}

// 5. 按住中途接触抖 10ms：不应多出事件，LONG 照常触发
static void test_bounce_during_hold(void)
{
	test_begin("按住中抖动 10ms → 不影响 LONG，无多余事件");
	g_test_level[KEY_UP] = 1;
	run_ms(500);
	g_test_level[KEY_UP] = 0;
	run_ms(10);
	g_test_level[KEY_UP] = 1;
	run_ms(700);
	g_test_level[KEY_UP] = 0;
	run_ms(100);
	check_count(2);
	check_event(0, KEY_UP, KEY_EVENT_LONG, 800, 870);
	check_event(1, KEY_UP, KEY_EVENT_REPEAT, 990, 1090);
}

// 6. 双击：两次干净短按 → 2 个 SHORT
static void test_double_click(void)
{
	test_begin("双击 → 2 个 SHORT");
	g_test_level[KEY_BACK] = 1;
	run_ms(60);
	g_test_level[KEY_BACK] = 0;
	run_ms(60);
	g_test_level[KEY_BACK] = 1;
	run_ms(60);
	g_test_level[KEY_BACK] = 0;
	run_ms(100);
	check_count(2);
	check_event(0, KEY_BACK, KEY_EVENT_SHORT, 60, 100);
	check_event(1, KEY_BACK, KEY_EVENT_SHORT, 180, 240);
}

int main(void)
{
	printf("按键消抖模拟测试（纯软件，不需要板子）\n\n");
	test_clean_short();
	test_bouncy_short();
	test_glitch();
	test_long_repeat();
	test_bounce_during_hold();
	test_double_click();
	printf("\n========== 结果：%s（失败 %d 项）==========\n",
		s_fail == 0 ? "全部通过" : "有失败", s_fail);
	return s_fail == 0 ? 0 : 1;
}
