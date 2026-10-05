# 按键消抖模拟测试（电脑端）

这个文件夹里的测试**不用接单片机**：把 `Core/Src/key.c` 的真实源码配上"假引脚"，
在电脑上模拟按键波形（弹跳、毛刺、长按……），验证消抖和事件逻辑。

## 怎么跑

在项目根目录打开 PowerShell：

```powershell
powershell -ExecutionPolicy Bypass -File tools\key_test\run_test.ps1
```

需要电脑上装有 gcc（本机路径：`D:\code\w64devkit\bin\gcc.exe`）。

## 文件说明

| 文件 | 说明 |
| --- | --- |
| main.h | "假 main.h"：给 key.c 提供假的 GPIO 读引脚函数（仅测试用） |
| test_main.c | 测试程序：模拟各种按键波形，检查产生的事件对不对 |
| run_test.ps1 | 一键脚本：把最新的 key.c / key.h 拷过来 → 编译 → 运行 |
| key.c / key.h | 运行脚本时自动从 Core/ 拷贝，不用手动维护 |

## 测试覆盖的场景

1. 干净短按 → 恰好 1 个 SHORT 事件
2. 按下/松开各弹跳 10ms → 仍只有 1 个 SHORT
3. 10ms 毛刺干扰 → 0 事件
4. 长按 1.2 秒 → LONG(≈800ms) + REPEAT(每 200ms)，松开不产生 SHORT
5. 按住中途接触抖 10ms → 不影响 LONG，无多余事件
6. 双击 → 2 个 SHORT
