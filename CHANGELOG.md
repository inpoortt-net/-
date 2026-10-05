# test 0.1 修改日志（STM32F103C8T6 + SSD1306 OLED）

## v0.1 — OLED 主菜单界面（显示时间 + 日期）　2026-10-04

### 新增文件

| 文件 | 作用 |
| --- | --- |
| Core/Src/oled.c、Core/Inc/oled.h | SSD1306 驱动（I2C1，自动识别地址 0x3C/0x3D，显存缓冲 + 整屏刷新） |
| Core/Inc/oledfont.h | 8x16 ASCII 点阵字库（95 个字符，由 Consolas Bold 14px 二值化生成） |
| Core/Src/ui.c、Core/Inc/ui.h | 界面层：主菜单页面 + 软件时钟（标题栏 + 时间 + 日期星期） |

### 修改文件

| 文件 | 改动 |
| --- | --- |
| Core/Src/main.c | 增加 `#include"ui.h"`；外设初始化后调用 `ui_init()`；主循环调用 `ui_task()` |
| MDK-ARM/test 0.1.uvprojx | 把 oled.c、ui.c 加入 Application/User/Core 编译组 |

### 备份（回退用）

改动前的原文件在 `backup\v0.1-主菜单\`（main.c、main.h、test 0.1.uvprojx）。

回退方法：把备份文件复制回原来位置覆盖；新增的 4 个文件可直接删除，同时从 uvprojx 中移除 oled.c、ui.c。

### SHA256（当前版本）

```
349B9FE0CD48920B5478E624FF20099516FD9A157DF3C8D5DC6CC9F0AF257058  Core\Src\oled.c
DB53866CD1754A5932D40B38A20207E447C2AF971A205DAEF5071B44F4A109AF  Core\Inc\oled.h
1E45563ED55BE22C71FC01ECEE08AD29FC2812D6B4E65DF995A89E258810B47C  Core\Inc\oledfont.h
12CD76A31C23CAFC16B3335922C81D69F4ADB615C29056355A79D077451EF77A  Core\Src\ui.c
92BEFCD999EB925E896B476E4AFEC549F99894C15B539C7ACEBD84B7EFD37090  Core\Inc\ui.h
F82F4188B5929FB467DA00898C024071298F3B59923386AA922D1AB80B017945  Core\Src\main.c
547B984C3B62C2688B1A82A6D264D09695D1AF1D8DE86963C4F0ECD2E35B97DB  MDK-ARM\test 0.1.uvprojx
```

改动前的备份文件哈希：

```
1BF788B4B80409CA4DAE9342C321CEBCE884965D6F236978CF92241373B8B52C  backup\v0.1-主菜单\main.c
A00893E609E738ABA5B7930F82FFC9F5B910FB14503E938507D4EA5DC38C37AE  backup\v0.1-主菜单\main.h
3953B5B0997D402D338811BD4E52DB085349ADCC135D9DAEE72FDFC29B068932  backup\v0.1-主菜单\test 0.1.uvprojx
```

### 编译验证

- Keil MDK（ARMCC V5.06 update 7）：**0 Error(s), 0 Warning(s)**
- Program Size: Code=6808　RO-data=2236　RW-data=32　ZI-data=2744
- 生成的烧录文件：`MDK-ARM\test 0.1\test 0.hex`（注意：文件名就是“test 0.hex”，不带最后的“1”）

### 功能说明与注意

- 上电后 OLED 显示：白色标题栏 “MAIN MENU” + 时间（hh:mm:ss）+ 日期星期（yyyy-mm-dd Www），下半屏留空给以后加菜单项。
- 时间用软件时钟：以**编译时刻**为起点走时，掉电不保存；重新编译烧录后会从新的编译时刻开始。以后可加 RTC 或按键校时（`ui_clock_set()` 接口已留好）。
- 屏幕按 128x64 设计；若实际是 128x32 屏，需要调整布局。
- 若屏幕不亮：检查 PB6->SCL、PB7->SDA、供电和上拉；驱动会自动尝试 0x3C、0x3D 两个地址。
- 用 CubeMX 重新生成代码后，可能需要把 oled.c、ui.c 重新加回 Keil 工程。

## v0.2 — 按键 + 菜单交互 + 密码锁 + 时间断电保存　2026-10-04

### CubeMX 配置（在 CubeMX 里完成）

- PA0~PA3：GPIO_Input + Pull-up，标签 KEY_UP / KEY_DOWN / KEY_OK / KEY_BACK（对应按钮 UP / DOWN / OK / BACK）
- CubeMX 重新生成了 main.c / main.h / stm32f1xx_it.c，上一版的用户代码（ui_init / ui_task）保留完好

### 新增文件

| 文件 | 作用 |
| --- | --- |
| Core/Src/key.c、Core/Inc/key.h | 4 按键驱动：每 10ms 扫描、30ms 消抖；事件：短按 / 长按(0.8s) / 连发(0.2s)；SysTick 中断里扫描，事件队列给主循环取 |
| Core/Src/clock.c、Core/Inc/clock.h | 时钟模块（从 ui.c 拆出）：走时 + 闰年 + 星期；每整分钟把时间写入 Flash 最后一页（0x0800FC00），上电自动找回 |

### 修改文件

| 文件 | 改动 |
| --- | --- |
| Core/Src/main.c | 增加 `#include"key.h"`；`ui_init()` 前调用 `key_init()` |
| Core/Src/stm32f1xx_it.c | SysTick 中断里加 `key_tick()`（写在 USER CODE 区，CubeMX 重新生成不会丢） |
| Core/Src/ui.c、Core/Inc/ui.h | 改成页面状态机：主页 → 主菜单列表 → 密码锁；密码输入/校验/清空/自动重试逻辑（原时钟代码移到 clock.c） |
| MDK-ARM/test 0.1.uvprojx | 加入 key.c、clock.c |
| test 0.1.ioc | CubeMX 生成的按键引脚配置 |

### 备份（回退用）

改动前的原文件在 `backup\v0.2-按键与菜单\`（main.c、main.h、stm32f1xx_it.c、ui.c、ui.h、uvprojx、ioc）。

回退方法：把备份文件复制回原位置覆盖；新增的 4 个文件（key/clock）可直接删除，同时从 uvprojx 中移除 key.c、clock.c。

### SHA256（当前版本）

```
63BD31D7315A97824EBACE924B7EE31361140B4F0D74542702CB8E298450D67D  Core\Src\key.c
3D637231C7491412D0F76C764D7366B1567D9814A787C881A2BEE864E0B0404D  Core\Inc\key.h
E54A5B29D9BA243DC8FF1423C042D4A19F791C62F6792470C824B28D10824935  Core\Src\clock.c
81BC3D9AC157FFE72AD26BA1F2AD2FAA11E92F2BE851500EE70B904D6780C21B  Core\Inc\clock.h
95E8E52F27687D5F9498007B48CD77CA8C02F3394B7A8BBDB60CFA462AEE139F  Core\Src\ui.c
31B8870B6F610D6E9D83BCA6057E73D7E99E0DBFBCFD5BFD93E27D57324B03FB  Core\Inc\ui.h
9CD3A5C1317CA9BF1B4554CCCDEB95AB07B3E824852C5963E6F863EC77DD243D  Core\Src\main.c
BC14F413167D07E4319F58BDAAAF0DFE2B9C290495DBFC13AC112B6521F05D67  Core\Inc\main.h
56A88E88E09BC42242C19287AD533873A020E91BE4A0D714616B08641EBE1E11  Core\Src\stm32f1xx_it.c
DCF0ED08BB485B69B54BAA9CDB1E3FAD905B0FE2A294A334A19DEEA105E63F9A  MDK-ARM\test 0.1.uvprojx
A356C9C8C22829A3D5B4CEE0507196E082E6CC5CF77D8273D057A776B1E6FE51  test 0.1.ioc
```

改动前的备份文件哈希：

```
13DE218EF46F5DD7127FC02DBA51B3730CA6FB59726C2C001FB6F7C77BCBFB5A  backup\v0.2-按键与菜单\main.c
BC14F413167D07E4319F58BDAAAF0DFE2B9C290495DBFC13AC112B6521F05D67  backup\v0.2-按键与菜单\main.h
D2787D0F161BF514F80A74FF91ACF21E50F01262A95658164042121A343A3A4E  backup\v0.2-按键与菜单\stm32f1xx_it.c
12CD76A31C23CAFC16B3335922C81D69F4ADB615C29056355A79D077451EF77A  backup\v0.2-按键与菜单\ui.c
92BEFCD999EB925E896B476E4AFEC549F99894C15B539C7ACEBD84B7EFD37090  backup\v0.2-按键与菜单\ui.h
DC9512C09079B6401B624E77939834586F71D39C6D875715B21A457A02304EBE  backup\v0.2-按键与菜单\test 0.1.uvprojx
A356C9C8C22829A3D5B4CEE0507196E082E6CC5CF77D8273D057A776B1E6FE51  backup\v0.2-按键与菜单\test 0.1.ioc
```

### 编译验证

- Keil MDK（ARMCC V5.06 update 7）：**0 Error(s), 0 Warning(s)**
- Program Size: Code=9400　RO-data=2284　RW-data=64　ZI-data=2816
- 生成的烧录文件：`MDK-ARM\test 0.1\test 0.hex`（文件名不带最后的“1”）

### 功能说明与注意

- 按键：PA0=UP、PA1=DOWN、PA2=OK、PA3=BACK；接法：按钮一脚接引脚、一脚接 GND（CubeMX 已开内部上拉，按下为低电平）。
- 主页（时间日期）：按 OK 进主菜单；主菜单页 30 秒没操作自动回主页。
- 主菜单：目前只有 "Password Lock" 一项，选中的一项反白显示；以后加功能改 `ui.c` 里的菜单名数组 + `ui_menu_key()` 分支。
- 密码锁：默认密码 `1234`（改 `ui.c` 里 `s_password[]` 即可，支持 4~8 位）。
  - UP/DOWN：调当前位数字（按住快滚）；OK 短按：确认一位；长按 OK：提交（不足 4 位只提示、不提交）
  - BACK 短按：删一位（一位都没输入时返回菜单）；长按 BACK：全部清空
  - 正确/错误显示 2 秒 → 自动清空回到“等待输入”，可无限重试，不需要重新上电
- 时间断电保存：每过整分钟自动存一次 Flash；上电时恢复“断电前最后保存的时间”（最多差 1 分钟）。
  - 原理/限制：没有电池时，断电期间时钟不走，只能恢复上次保存值（不会再回到编译时间）。
  - 想要“断电也完全不停”：需要给板子 VBAT 接一颗 CR2032 纽扣电池，并改用 STM32 自带 RTC（以后需要再说）。
  - 保存占用 Flash 最后一页（0x0800FC00，63KB 处），程序目前约 12KB 不会冲突；以后程序超过 63KB 时要把保存位置挪一挪。

## v0.3 — 密码系统完整版：中文状态显示 + 屏幕修改密码（存 Flash）　2026-10-04

### 新增文件

| 文件 | 作用 |
| --- | --- |
| Core/Inc/oledfont_cn.h | 16x16 汉字点阵字库（44 个字，宋体 SimSun 16px 二值化，脚本生成 + 回读校验） |
| Core/Src/settings.c、Core/Inc/settings.h | 密码存储模块：默认 1234，修改后写进 Flash 0x0800F800 页（掉电不丢），和时钟存储同一套“格子”机制 |

### 修改文件

| 文件 | 改动 |
| --- | --- |
| Core/Src/oled.c、Core/Inc/oled.h | 新增 UTF-8 解码 + `oled_show_cn()` / `oled_show_text()` / `oled_text_width()`，支持 ASCII 和汉字混排显示 |
| Core/Src/ui.c | 界面全面中文化；新增“修改密码”页面（输入新密码 → 再输一次，一致才保存；成功后自动回密码锁页）；密码校验改用 settings 模块 |
| MDK-ARM/test 0.1.uvprojx | 加入 settings.c；C 编译器 Misc Controls 增加 `--no_multibyte_chars`（让 ARMCC v5 逐字节透传 UTF-8 中文字符串，否则报 “missing closing quote”） |

### 备份（回退用）

改动前的原文件在 `backup\v0.3-密码升级\`（ui.c、oled.c、oled.h、uvprojx）。

回退方法：把备份文件复制回原位置覆盖；新增的 3 个文件（oledfont_cn.h、settings.c/h）可直接删除，同时从 uvprojx 中移除 settings.c 和 `--no_multibyte_chars` 参数。

### SHA256（当前版本）

```
BCD18034A0E49B7AC065836BEF04AE1C2F6103BC842EC997A9FB963FC0E55CE3  Core\Src\ui.c
A06563BF39F49E05B3C1879B4F36A97342A0CC86A7A5C6767F8932962DC62E20  Core\Src\oled.c
6A45630E842081AFE92CE6293474C072BA31F40601BFA848C70EB0E22BC7BE26  Core\Inc\oled.h
EAE4505DCBD45A0A2D24CF10A8486164F4D1841C0E9B24EBFFFF25673C83D1A4  Core\Inc\oledfont_cn.h
63FA411A87AD3E4AC0B41842FAF352F195D30D5DD86EFB69F5F473A5D53E8833  Core\Src\settings.c
E056F367AD404E9D629A394730548185E63D1B0196D21FE795351C0077A15124  Core\Inc\settings.h
2444473DDEF8FA3C4310C7A369A0B401DA6F10D5C30CAE288EE2F698816096B9  MDK-ARM\test 0.1.uvprojx
```

改动前的备份文件哈希：

```
349B9FE0CD48920B5478E624FF20099516FD9A157DF3C8D5DC6CC9F0AF257058  backup\v0.3-密码升级\oled.c
DB53866CD1754A5932D40B38A20207E447C2AF971A205DAEF5071B44F4A109AF  backup\v0.3-密码升级\oled.h
4D20D4533CCCEA429C21A1D967BE8BB7BB3EB8BC9E1CED39E635D145D0E7E040  backup\v0.3-密码升级\test 0.1.uvprojx
95E8E52F27687D5F9498007B48CD77CA8C02F3394B7A8BBDB60CFA462AEE139F  backup\v0.3-密码升级\ui.c
```

### 编译验证

- Keil MDK（ARMCC V5.06 update 7）：**0 Error(s), 0 Warning(s)**
- Program Size: Code=11476　RO-data=3796　RW-data=84　ZI-data=2836
- 生成的烧录文件：`MDK-ARM\test 0.1\test 0.hex`

### 功能说明与注意（按键作用）

- 主页：显示时间日期；**OK** = 进菜单。
- 菜单：**UP/DOWN** 移动选项，**OK** 进入（密码锁 / 修改密码），**BACK** 返回主页；30 秒没操作自动回主页。
- 密码锁页：
  - **UP/DOWN**：调当前位数字（0~9 循环，按住快速滚）
  - **OK 短按**：确认一位（已确认的显示成 *，屏幕上不显示真实密码）
  - **OK 长按**：提交校验（不足 4 位只提示“至少4位”，不算错误）
  - **BACK 短按**：删掉最后一位继续改；一位都没输入时 = 返回菜单
  - **BACK 长按**：全部清空（提示“已清空”）
  - 正确：“密码正确 / 输入成功”；错误：“密码错误 / 请重新输入”；显示 2 秒后自动清空回到等待输入，可无限重试
- 修改密码页（第二个界面）：
  - 输入方式同上；先输新密码（长按 OK 提交）→ 再输一次 → 两次一致 →“修改成功”并**保存到 Flash**，2 秒后自动回到密码锁页，可以直接试新密码
  - 两次不一致：提示“两次不一致 / 请重新输入”，2 秒后回到第一步重来
  - BACK 短按在空输入时：第二步退回第一步；第一步退回菜单
- 注意：
  - 中文源码是 UTF-8；Keil 编辑器如果显示乱码，Edit → Configuration → Encoding 选 UTF-8（不影响编译）
  - 默认密码 1234；用“修改密码”改完后存 Flash，掉电/重启保留；重新烧录固件一般也保留（除非下载时选了整片擦除）
  - 密码存 Flash 0x0800F800 页、时钟存 0x0800FC00 页；程序目前约 15KB，两者都不冲突

## v0.4 — 蜂鸣器音效 + 主频升到 72MHz（HSE 晶振）　2026-10-04

### CubeMX 配置（在 CubeMX 里完成）

- 时钟：HSE 8MHz 晶振 + PLL ×9 → SYSCLK = **72MHz**（APB1 分频 /2 = 36MHz，Flash 等待周期自动变 2）
- TIM3 Channel3：PWM Generation → **PB0** 输出；PSC=71、ARR=369、Pulse=185、**CH Polarity = Low**（匹配低电平触发的蜂鸣器模块）、AutoReloadPreload = Enable
- 生成后检查：`--no_multibyte_chars` 未被覆盖、5 个用户 .c 文件都还在工程里（本轮都保住了，未做善后修改）

### 新增文件

| 文件 | 作用 |
| --- | --- |
| Core/Src/buzzer.c、Core/Inc/buzzer.h | 蜂鸣器驱动：PWM 变调发声 + 后台音序播放（SysTick 里 10ms 步进，不阻塞按键和屏幕）；音色参数集中在 buzzer.c 顶部 |

### 修改文件

| 文件 | 改动 |
| --- | --- |
| Core/Src/main.c | CubeMX 生成：72MHz 时钟配置、MX_TIM3_Init（用户代码区完好） |
| Core/Src/stm32f1xx_hal_msp.c | CubeMX 生成：PB0 复用为 TIM3_CH3（AF 推挽输出） |
| Core/Src/stm32f1xx_it.c | SysTick 中断里加 `buzzer_tick()`（USER CODE 区） |
| Core/Src/ui.c | 接入音效：按键“嘀”、正确上行“叮-咚”、错误下行“嘟-呜”、提示两短声、确认声、开机一声 |
| MDK-ARM/test 0.1.uvprojx | 加入 buzzer.c |

### 备份（回退用）

改动前的文件在 `backup\v0.4-蜂鸣器\`（main.c、ui.c、stm32f1xx_it.c、uvprojx）。

回退方法：把备份文件复制回原位置覆盖；删除 buzzer.c/buzzer.h 并从 uvprojx 中移除 buzzer.c；时钟和 TIM3 的改动在 CubeMX 里改回去。

### SHA256（当前版本）

```
6F591D3106B773667492542FD52B18BC02E0BEA7C984BFABB637E67D027D561E  Core\Src\buzzer.c
62E7620EEBE28A316F9CE30FEE54F355C6CA75A711C5971F3829B7F40ABDABC1  Core\Inc\buzzer.h
4AB42FCD711905E4BD21CB17EB18AEC434FEB831A28874D0D56E8BBB8D7EE0BB  Core\Src\ui.c
BE73B56DBD180AC54D1F55CA658F92D26875DE2F534C5F31FDAF30446786362F  Core\Src\stm32f1xx_it.c
BC47F960D693C082D83916C51A55DEC73009E9E911000B146D2601CCD73234AE  Core\Src\main.c
6DA8402EBACED3F8C5DDFAE54A0F461CB907AD14D0FCE7DC0A44E48B9ED8E3F6  Core\Src\stm32f1xx_hal_msp.c
1D06783C20159DAA0DAD7FC3F86A5096249B5225B6CC5487E5F44BDBC85E59DB  MDK-ARM\test 0.1.uvprojx
```

改动前的备份文件哈希：

```
BC47F960D693C082D83916C51A55DEC73009E9E911000B146D2601CCD73234AE  backup\v0.4-蜂鸣器\main.c
BCD18034A0E49B7AC065836BEF04AE1C2F6103BC842EC997A9FB963FC0E55CE3  backup\v0.4-蜂鸣器\ui.c
56A88E88E09BC42242C19287AD533873A020E91BE4A0D714616B08641EBE1E11  backup\v0.4-蜂鸣器\stm32f1xx_it.c
42D513EDF27D6449E1A9394048CEDE99C6EDE58849631772674DB3A363FF7108  backup\v0.4-蜂鸣器\test 0.1.uvprojx
```

### 编译验证

- Keil MDK（ARMCC V5.06 update 7）：**0 Error(s), 0 Warning(s)**
- Program Size: Code=13580　RO-data=3832　RW-data=96　ZI-data=2904
- 生成的烧录文件：`MDK-ARM\test 0.1\test 0.hex`

### 功能说明与注意

- 接线：蜂鸣器模块 **VCC→3.3V**（不要接 5V：PNP 模块供 5V 时，3.3V 高电平关不断三极管，会一直响/发热）、GND→GND、I/O→PB0。
- 音效表：

| 场景 | 声音 |
| --- | --- |
| 开机 | 一声短“嘀”（听到说明蜂鸣器接线正常） |
| 按键（短按/长按） | 轻短“嘀” |
| 密码正确 / 修改成功 | 上行“叮-咚” |
| 密码错误 / 两次不一致 | 下行“嘟-呜” |
| 至少4位 / 已清空 | 两短声提示 |
| 进入“再输一次” | 一声中音确认 |

- 改音色：`Core/Src/buzzer.c` 顶部的音符表（频率=音调，ms=时长）。
- 如果闲置时蜂鸣器一直响/发热：说明触发极性反了——把 CubeMX 里 TIM3 CH3 的 CH Polarity 改成 High 重新生成（或告诉我处理）。
- 72MHz 前提：板子必须有 8MHz 晶振；没有晶振会卡在 Error_Handler（黑屏）。

## v0.5 — 按键消抖升级（更快、更稳，带模拟测试）　2026-10-04

### 修改文件

| 文件 | 改动 |
| --- | --- |
| Core/Src/key.c | 消抖重写为“四态状态机”（空闲 → 按下消抖 → 按住 → 松开消抖）：扫描提速到 5ms；按下/松开各需连续 20ms 稳定才确认；按住中接触抖动不会误判；长按/连发改成毫秒精确计时（参数和扫描周期解耦，改数字就是改毫秒） |

### 新增文件（电脑端测试，不属于单片机工程）

| 文件 | 作用 |
| --- | --- |
| tools/key_test/ | 按键消抖模拟测试：用假引脚在电脑上模拟弹跳/毛刺/长按波形，验证的就是 Core/Src/key.c 的真实源码；`run_test.ps1` 一键编译并运行，README 里有说明 |

### 备份（回退用）

改动前在 `backup\v0.5-按键消抖\key.c`。

### SHA256（当前版本）

```
5E0986F77E555FA628FBC88A6D397E36DAFD7CEBFC338D491E1B4EED63A2A03C  Core\Src\key.c
2BDE60650D124BB7E4EF566785F8812B46D82BD931FC40FB3D4D68293C6C5416  tools\key_test\main.h
ED68D4B53B8FED33CB13EBAB179F9570813600D57D6485D36DE56DE9F84E144B  tools\key_test\test_main.c
```

改动前的备份文件哈希：

```
63BD31D7315A97824EBACE924B7EE31361140B4F0D74542702CB8E298450D67D  backup\v0.5-按键消抖\key.c
```

### 验证

- **电脑端模拟测试（6 项全部通过）**：
  1. 干净短按 → 恰好 1 个 SHORT（约 120ms 处发出，即松开后 20ms 确认）
  2. 按下/松开各弹跳 10ms → 仍只有 1 个 SHORT
  3. 10ms 毛刺 → 0 事件
  4. 长按 1.2s → LONG @≈820ms + REPEAT @≈1020ms，松开不产生 SHORT
  5. 按住中途接触抖 10ms → 不影响 LONG，无多余事件
  6. 双击 → 2 个 SHORT
- Keil MDK（ARMCC V5.06 update 7）：**0 Error(s), 0 Warning(s)**
- Program Size: Code=13624　RO-data=3832　RW-data=96　ZI-data=2920
- 生成的烧录文件：`MDK-ARM\test 0.1\test 0.hex`

### 说明

- 想调手感：改 `Core/Src/key.c` 顶部 5 个参数（扫描周期/按下消抖/松开消抖/长按时长/连发间隔），全部是毫秒，互不影响。
- 和旧版比：响应更快（20ms 确认 vs 30ms）、更稳（三阶段消抖，按住中的接触抖动不会误判成“松开再按下”）。

## v0.6 — 连续输错锁定（错 3 次锁 30 秒，掉电不逃）　2026-10-04

### 修改文件

| 文件 | 改动 |
| --- | --- |
| Core/Src/ui.c | 密码锁新增锁定逻辑：连续输错 3 次 → 锁定 30 秒，屏幕显示“已锁定 / 请等待 N 秒”倒计时；输错时提示“还剩 N 次”；锁定中只有 BACK 能退出页面；输对 / 锁定到期 / 修改密码后错误次数清零；开机时若上次是锁定状态，继续锁定 |
| Core/Src/settings.c、Core/Inc/settings.h | 新增“连续输错次数”的 Flash 存储（'FAIL' 记录）；通用 16 字节记录写入抽成 `settings_flash_write()`，密码和错误次数共用同一页存储机制 |
| Core/Inc/oledfont_cn.h | 汉字库 44 → 48 字（新增：还、剩、定、秒） |

### 备份（回退用）

改动前在 `backup\v0.6-输错锁定\`（ui.c、settings.c、settings.h、oledfont_cn.h）。

### SHA256（当前版本）

```
864EEB75B528AB536B37E18F7E405BF2BE7589D1740EFDBD402584F8B2467F53  Core\Src\ui.c
B85A8DE6B8095B5749831894583671031517ABB48EDE3AC256E25CBAC2628031  Core\Src\settings.c
3A8F50B43B347794B3851BC645AF07BF77BC22D09EE2165CB58F832049D3D899  Core\Inc\settings.h
DB9932BC7EC74A3D6F362203DAAD96DCC8738403B35667C76EC19F2F09EA48A4  Core\Inc\oledfont_cn.h
```

改动前的备份文件哈希：

```
4AB42FCD711905E4BD21CB17EB18AEC434FEB831A28874D0D56E8BBB8D7EE0BB  backup\v0.6-输错锁定\ui.c
63FA411A87AD3E4AC0B41842FAF352F195D30D5DD86EFB69F5F473A5D53E8833  backup\v0.6-输错锁定\settings.c
E056F367AD404E9D629A394730548185E63D1B0196D21FE795351C0077A15124  backup\v0.6-输错锁定\settings.h
EAE4505DCBD45A0A2D24CF10A8486164F4D1841C0E9B24EBFFFF25673C83D1A4  backup\v0.6-输错锁定\oledfont_cn.h
```

### 编译验证

- Keil MDK（ARMCC V5.06 update 7）：**0 Error(s), 0 Warning(s)**
- Program Size: Code=14092　RO-data=3968　RW-data=100　ZI-data=2924
- 生成的烧录文件：`MDK-ARM\test 0.1\test 0.hex`

### 功能说明与注意

- 规则（想改就改 `ui.c` 顶部）：
  - `PWD_FAIL_MAX = 3`：连续输错几次锁定
  - `PWD_LOCK_MS = 30000`：锁定时长（毫秒）
- 界面流程：
  - 输错（未锁）：状态“密码错误”，底部“还剩 N 次”，2 秒后自动清空
  - 错满 3 次：进入“已锁定”画面，中间“请等待 N 秒”倒计时（每秒刷新），底部“错误 3 次”
  - 锁定期间：除 BACK（退出到菜单）外按键无效；锁定计时不受页面切换影响
  - 锁定到期：自动清零并回到“等待输入”，一声中音提示可重试
- 清零条件：输对密码 / 锁定到期 / 修改密码成功
- 断电免疫：错误次数存 Flash（0x0800F800 页，和密码共用格子机制）；断电重启后仍锁定（重新计 30 秒），不会因为重启而跳过锁定
- 已知限制（演示版）：修改密码不要求验证旧密码——被锁时理论上可以进“修改密码”换密码绕过锁定。正式版建议加“先验证旧密码才能修改”，需要的话下一步做。

## v0.7 — 红/绿状态灯（正确绿、错误红、锁定红灯慢闪）　2026-10-04

### 新增文件

| 文件 | 作用 |
| --- | --- |
| Core/Src/led.c、Core/Inc/led.h | 状态灯驱动：`led_green()` / `led_red()` / `led_off()` / `led_blink()`（慢闪）+ `led_task()` 计时；非阻塞 |

### 修改文件

| 文件 | 改动 |
| --- | --- |
| Core/Src/ui.c | 接入灯光：密码正确/修改成功→绿灯亮 2 秒；密码错误/两次不一致→红灯亮 2 秒；锁定 30 秒→红灯慢闪（0.5 秒翻转）；结果结束/离开锁定页/锁定到期→自动熄灭；`ui_task()` 里调 `led_task()` |
| MDK-ARM/test 0.1.uvprojx | 加入 led.c |
| （CubeMX 配置） | PA4=LED_GREEN、PB1=LED_RED，推挽输出、初始低电平（由用户在 CubeMX 配置并生成） |

### 备份（回退用）

改动前在 `backup\v0.7-红绿灯\`（ui.c、uvprojx）。

### SHA256（当前版本）

```
FD9269143DD0E8B667D8FBF433EC72D663EF215E8787A741FE9FC24191593A92  Core\Src\led.c
33CEB4AE2DB8FBC2D7B795F7183F80DA3CC383DF89C105BB08B0693CE973825D  Core\Inc\led.h
F75E056635D861117D38C564B0DF640681539E449A3EE39BFFC8798B0BA41553  Core\Src\ui.c
DCBFD81241859A99AAF500BE899D717082704211B760218EDFF58BC21120FDAE  MDK-ARM\test 0.1.uvprojx
```

改动前的备份文件哈希：

```
864EEB75B528AB536B37E18F7E405BF2BE7589D1740EFDBD402584F8B2467F53  backup\v0.7-红绿灯\ui.c
1D06783C20159DAA0DAD7FC3F86A5096249B5225B6CC5487E5F44BDBC85E59DB  backup\v0.7-红绿灯\test 0.1.uvprojx
```

### 编译验证

- Keil MDK（ARMCC V5.06 update 7）：**0 Error(s), 0 Warning(s)**
- Program Size: Code=14460　RO-data=3968　RW-data=108　ZI-data=2924
- 生成的烧录文件：`MDK-ARM\test 0.1\test 0.hex`

### 功能说明

- 接线：引脚 —(220Ω~1kΩ 电阻)— LED 长脚(+)，LED 短脚(−) — GND；红灯=PB1、绿灯=PA4，高电平点亮。
- 灯效：正确/修改成功=绿灯亮 2 秒；错误/不一致=红灯亮 2 秒；锁定 30 秒=红灯慢闪（每 0.5 秒翻转）；其余全灭。
- 慢闪周期可改：`Core/Src/led.c` 顶部 `LED_BLINK_MS`。

## v0.7.1 — LED 常亮测试模式（临时调试）　2026-10-04

- 现象：v0.7 烧录后两个状态灯不亮，需要先判断是硬件还是软件问题
- 改动：`Core/Src/led.c` 顶部新增开关 `LED_TEST_ALWAYS_ON = 1`：两个灯上电常亮，其余灯光逻辑暂停
- 改前备份：`backup\v0.7.1-LED测试\led.c`（哈希 `FD9269143DD0E8B667D8FBF433EC72D663EF215E8787A741FE9FC24191593A92`）
- 恢复方法：硬件确认后把开关改回 0、重新编译烧录即可

## v0.7.2 — 恢复正式灯效（LED 硬件验证通过）　2026-10-05

- 用户用 v0.7.1 常亮测试版确认：**红灯、绿灯接线均正常**（此前绿灯不亮为插线/认脚问题，已解决）
- 改动：`Core/Src/led.c` 的 `LED_TEST_ALWAYS_ON` 由 1 改回 **0**，正式灯效恢复：正确绿 / 错误红 / 锁定红灯慢闪
- 备份（改动前 = 测试版）：`backup\v0.7.2-恢复灯效\led.c`（哈希 `8F4E80A91020DC201A3BB0D1433907076F1D927381217457EEA06FDF22D7C5FA`）
- SHA256（当前版本）：`6076ADBB406607E0848C3765920BB3D40EAD82524136404F9122DD6DB9F9F998  Core\Src\led.c`
- 编译验证：**0 Error(s), 0 Warning(s)**；Program Size: Code=14460　RO-data=3968　RW-data=108　ZI-data=2924
- 生成的烧录文件：`MDK-ARM\test 0.1\test 0.hex`
- 备注：以后想再做硬件体检，把开关改成 1 重新编译即可（方法已写在 led.c 注释里）

## v0.7.3 — 锁定规则修正：只有输对密码才清零次数　2026-10-05

- 用户反馈：锁定 30 秒到期后会【自动清零】错误次数 → 每轮都是固定 3 次机会，不符合“输对密码才重置”的预期
- 改动：`Core/Src/ui.c` 锁定到期逻辑：
  - 到期【不再清零】错误次数（改为只解除锁定）
  - 到期后只再给 1 次机会（次数压回“上限-1”）；再输错一次立刻重新锁 30 秒
  - 清零时机：输对密码 / 修改密码成功（原本就会清，保持不变）
- 备份（改前）：`backup\v0.7.3-锁定规则\ui.c`（哈希 `F75E056635D861117D38C564B0DF640681539E449A3EE39BFFC8798B0BA41553`）
- SHA256（当前版本）：`756C99FB428ABFD21FC2FC128EF34DCE4BFE7EB5945E9CD48D0CAEFAB3273AE7  Core\Src\ui.c`
- 编译验证：**0 Error(s), 0 Warning(s)**；Program Size: Code=14472　RO-data=3968　RW-data=108　ZI-data=2924
- 生成的烧录文件：`MDK-ARM\test 0.1\test 0.hex`
- 备注：想改成“到期给 2 次机会”或“锁定时间逐次加长”，都是改一两个数的事

## v0.7.4 — LED 接法可选（拉电流/灌电流，一个开关切换）　2026-10-05

- 背景：用户看到教程把“拉电流接法”（GPIO→电阻→长脚，短脚→GND，高电平点亮）标为“不推荐新手”，确认自己是否用错、想对比评估
- 结论：**当前硬件/代码一直就是拉电流接法，工作正常**；STM32 的拉/灌电流能力对称（每脚 ±20mA 量级），两种接法都合法（“只推荐灌电流”是 51 单片机时代的习惯）
- 改动：`Core/Src/led.c` 顶部新增 `LED_SINK_MODE` 开关：
  - `0`（默认）= 拉电流接法（当前）：输出高 = 亮
  - `1` = 灌电流接法：输出低 = 亮（需按对应方式重新接线：3.3V→长脚，短脚→电阻→GPIO）
- 行为不变：默认值 0，与 v0.7.3 完全一致（编译产物同尺寸 Code=14472）
- 备份（改前）：`backup\v0.7.4-LED接法\led.c`（哈希 `6076ADBB406607E0848C3765920BB3D40EAD82524136404F9122DD6DB9F9F998`）
- SHA256（当前版本）：`D717834E17A7E8E52FEA257BDF33166F19E176B67C25277700A562FA753AB7C7  Core\Src\led.c`
- 编译验证：**0 Error(s), 0 Warning(s)**；Program Size: Code=14472　RO-data=3968　RW-data=108　ZI-data=2924
- 生成的烧录文件：`MDK-ARM\test 0.1\test 0.hex`

## v0.7.5 — 回退 v0.7.4（撤销 LED 接法开关）　2026-10-05

- 按用户要求回退到上一个版本：撤销 `LED_SINK_MODE` 接法开关
- `Core/Src/led.c` 恢复为 v0.7.3 版本（哈希 `6076ADBB406607E0848C3765920BB3D40EAD82524136404F9122DD6DB9F9F998`），与 v0.7.3 完全一致
- 行为：拉电流接法（输出高 = 亮），代码中不再有接法开关
- 备份（回退前的 v0.7.4 版本）：`backup\v0.7.5-回退\led.c`（哈希 `D717834E17A7E8E52FEA257BDF33166F19E176B67C25277700A562FA753AB7C7`）
- 编译验证：**0 Error(s), 0 Warning(s)**；Program Size: Code=14472　RO-data=3968　RW-data=108　ZI-data=2924
- 生成的烧录文件：`MDK-ARM\test 0.1\test 0.hex`
- 备注：锁定规则修正（v0.7.3）保留，不受本次回退影响

## v0.7.6 — LED 改为开漏接法（低电平点亮）　2026-10-05

- 用户决定：两个 LED 改为“开漏输出 + 接 3.3V 一侧”的接法（低电平点亮）
- 改动：
  - `Core/Src/led.c`：新增 `led_pins_config()`，上电时把 PA4/PB1 统一配置为**开漏输出**（不依赖 CubeMX 的推挽配置，重新生成也不会丢）；亮灭极性反转（拉低 = 亮，放开 = 灭）
  - `Core/Src/led.c`、`Core/Inc/led.h`：注释更新为新接法
  - `Core/Src/ui.c`：`led_init()` 提前到 `ui_init()` 最前面（上电立即熄灭，避免点灯窗口）
- 接线（用户操作）：3.3V → LED 长脚(+) → LED 短脚(−) → 电阻(220Ω~1kΩ) → PA4(绿)/PB1(红)
- 备份（改前）：`backup\v0.7.6-LED开漏\`（led.c `6076ADBB…`、led.h `33CEB4AE…`、ui.c `756C99FB…`）
- SHA256（当前版本）：

```
1C3A89D0303FA1DC52FE4697113C2D54D2B3E41FC7BF811292F7135A24439034  Core\Src\led.c
09204B4B37EA0DB3D4E3785982AF3C9877E061E7F6B49AA86AADD4832857F748  Core\Inc\led.h
5D0472702CA8AD68E22AF688476F38F992086C979996625A59DFE44DFF4A5274  Core\Src\ui.c
```

- 编译验证：**0 Error(s), 0 Warning(s)**；Program Size: Code=14520　RO-data=3968　RW-data=108　ZI-data=2924
- 生成的烧录文件：`MDK-ARM\test 0.1\test 0.hex`
- 警告：本固件只适配“低电平点亮”的新接法；**接线改好之前不要烧这版**（旧接法下灯会失效）

## v0.7.7 — 教学注释加强（只有注释变化，行为零改动）　2026-10-05

- 目的：为 RoboMaster 二面做准备——代码要"读得懂、讲得出"（面试会看代码、提问）
- 改动：15 个用户文件中 14 个只加/改注释，**代码一行未动**：
  - 7 个 .c（key/oled/clock/settings/buzzer/led/ui）：顶部换成统一“模块头”（职责 / 怎么跑 / 为什么 / 调参位置）
  - `Core/Src/main.c`：USER CODE 0 区新增“项目总览”（分层结构 + 阅读入口，CubeMX 重新生成不会丢）
  - 6 个 .h：补一行模块说明
  - oled.c / clock.c / settings.c：关键机制补“为什么”注释（页格式显存、账本式 Flash 存储、I2C 刷新耗时）
- 备份（改前）：`backup\v0.7.7-注释版\`（15 个文件）
- SHA256（当前版本）：

```
03C29DA6B6B466C10C4DEE01511F4051C21827248CC9A0AE3034E57573474E8C  Core\Src\key.c
C527224611228F8B417D9C7A5BEAB2189C576650473D84F922AA0C1050F6384B  Core\Inc\key.h
9BCA0DDE3E3C7F98A17F0C5BC6F184CA721FB1E8C72C5F51F3A806896A87C121  Core\Src\oled.c
D84A442896B5DB0D118435904C409028DA7EDB4DDBEE486C0A2BC7128ED00D57  Core\Inc\oled.h
DFDA7CE79F816D05621F1CF64AC0F34689A2507AC274207C265FBC78771624DB  Core\Src\clock.c
86DC721058EB1B6FEFC50E9ECA0F1F231DC0FA13BC91480650076D284268038F  Core\Inc\clock.h
3C8A7D9E0543D1161495A64F0A6A0BCEF2EAFB0AA2A676EDD042A84FA7F4D3C6  Core\Src\settings.c
2B45E06B54F58A033FB8A0455A5D633E74B78D62E33C5CA51671B608625F1682  Core\Inc\settings.h
978D93E164270C253202D46F5B89E9E1B99385E670DB19E9AC34280B1057B2AC  Core\Src\buzzer.c
01B5CA422CA95B8DFDAC2E5E788F86A42EB0A1D3FD69206E97A3A7192D53760F  Core\Inc\buzzer.h
79B9A85F21BFC951681DE74C23C052273517C8835F01743516A7B1660EC4837E  Core\Src\led.c
E4826C6DEB9D29A58488BA5D5962ABB4CBBDCD782A47BBC979FCD426EE0424C1  Core\Src\ui.c
4CCF5039BD5D21952DCE93C92A2187D7CA706395B6516828F5E29CC9CF34665A  Core\Inc\ui.h
C6C2AC623D0D8DACB868CA876701B7CA7472B7322E2ECE9254E40ADB35D53C8C  Core\Src\main.c
```

（`Core\Inc\led.h` 未改动，哈希与 v0.7.6 相同：09204B4B37EA0DB3D4E3785982AF3C9877E061E7F6B49AA86AADD4832857F748）

- 编译验证：**0 Error(s), 0 Warning(s)**；Program Size: Code=14520　RO-data=3968　RW-data=108　ZI-data=2924
  （与 v0.7.6 完全一致 → 证明只有注释变化；另有“去注释后纯代码逐字符比对一致”的复核）
- 追加（同日）：`Core\Src\stm32f1xx_it.c` 修正过期注释（按键扫描“10ms”→“5ms”；v0.5 起实际为 5ms），并在 SysTick 处补充“我们加的两行”说明——仅注释改动，编译 0/0、尺寸不变；备份（改前）：`backup\v0.7.7-注释版\stm32f1xx_it.c`（哈希 `BE73B56DBD180AC54D1F55CA658F92D26875DE2F534C5F31FDAF30446786362F`）；SHA256（当前）：`92DBF434F4C932C1D38708BC3534A8F7CEFA9080256CAC1B13BB5AB04F97CD97`
- 功能说明：**无任何行为变化**，《按键操作说明.md》无需更新
- 配套材料：`docs\代码导读手册.md`（学习导读）、`docs\代码全文.html`/`代码全文.md`（注释版代码导出）

## 维护记录（非版本改动）

- 2026-10-05：备份整理——`backup\v0.1~v0.7.6` 的 13 个旧版本文件夹统一归档为 `backup\v0.1-v0.7.6-历史备份存档.zip`（33 个文件，校验一致，内容不变；回退时解压对应文件夹即可，方法见 `backup\说明.txt`）；`backup\` 仅保留最新的 `v0.7.7-注释版\`（16 个文件）
- 2026-10-05：审查修正（5 轮审查第 2 轮发现）——`Core\Src\oled.c` 注释“1025 字节 / ≈90ms”修正为“一千多字节（8 包 ×129 + 命令）/ ≈90~100ms”；`Core\Src\ui.c` 注释“重画 ≈ 90ms”改为“约 0.1 秒”（均为注释，编译 0/0、尺寸不变）；SHA256（当前）：`2FF98987763E09F797AC2D98F205A4C8720D4A8BF9F6ED75032ACE1637D07BB6`（oled.c）、`91C914CE8754ED7271EFB6DEAE0A9C5F51F8391EADBA6CAF01CDE7D116C8492F`（ui.c）
