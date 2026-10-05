# 按键消抖模拟测试：自动拷贝最新的 key.c/key.h，编译并运行
# 用法: powershell -ExecutionPolicy Bypass -File tools\key_test\run_test.ps1
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$proj = Split-Path (Split-Path $here -Parent) -Parent

Copy-Item "$proj\Core\Src\key.c" (Join-Path $here "key.c") -Force
Copy-Item "$proj\Core\Inc\key.h" (Join-Path $here "key.h") -Force

$gcc = "D:\code\w64devkit\bin\gcc.exe"
if (-not (Test-Path $gcc))
{
	Write-Output "找不到 gcc: $gcc"
	exit 1
}

Set-Location $here
& $gcc -std=c99 -O1 -Wall -o key_test.exe test_main.c key.c
if ($LASTEXITCODE -ne 0)
{
	Write-Output "编译失败"
	exit 1
}
.\key_test.exe
exit $LASTEXITCODE
