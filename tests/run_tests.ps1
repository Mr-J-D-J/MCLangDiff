# MCLangDiff 测试脚本（Windows PowerShell 5.1+）
#
# 用法：
#   powershell -NoProfile -ExecutionPolicy Bypass -File tests\run_tests.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File tests\run_tests.ps1 -SkipBuild
#
# 覆盖内容：
#   1. 调用 build.bat 生成 bin\MCLangDiff.exe
#   2. 编译并运行核心逻辑测试 tests\core_tests.cpp
#   3. 通过 CLI（--text）生成 JSON / LANG 报告，校验统计、条目、顺序、BOM、CRLF
#   4. 校验错误场景的退出码（格式不一致、新旧同一文件、文件不存在）

param(
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'

$root = Split-Path $PSScriptRoot -Parent
$src = Join-Path $root 'src'
$tmp = Join-Path $PSScriptRoot 'tmp'
$app = Join-Path $root 'bin\MCLangDiff.exe'
$samples = Join-Path $root 'samples'

if (!(Test-Path $tmp)) {
    New-Item -ItemType Directory -Path $tmp | Out-Null
}

$script:checks = 0
$script:failures = 0

function Write-Result {
    param([bool]$Condition, [string]$Message)
    $script:checks++
    if ($Condition) {
        Write-Host "  ok    $Message" -ForegroundColor DarkGreen
    } else {
        $script:failures++
        Write-Host "  FAIL  $Message" -ForegroundColor Red
    }
}

function Assert-Equal {
    param($Expected, $Actual, [string]$Message)
    Write-Result ($Expected -eq $Actual) $Message
    if ($Expected -ne $Actual) {
        Write-Host "        期望：$Expected  实际：$Actual" -ForegroundColor Yellow
    }
}

function Assert-Contains {
    param([string]$Text, [string]$Needle, [string]$Message)
    Write-Result ($Text.Contains($Needle)) $Message
    if (!$Text.Contains($Needle)) {
        Write-Host "        缺少：$Needle" -ForegroundColor Yellow
    }
}

function Assert-True {
    param([bool]$Condition, [string]$Message)
    Write-Result $Condition $Message
}

function Invoke-Report {
    param([string]$Old, [string]$New, [string]$Out)
    $arguments = '--text "{0}" "{1}" "{2}"' -f $Old, $New, $Out
    $process = Start-Process -FilePath $app -ArgumentList $arguments -Wait -PassThru
    return $process.ExitCode
}

Write-Host '== 1. 构建应用 ==' -ForegroundColor Cyan
if ($SkipBuild -and (Test-Path $app)) {
    Write-Host '  跳过构建（使用已有 exe）'
} else {
    & (Join-Path $root 'build.bat')
    if ($LASTEXITCODE -ne 0) {
        Write-Host '构建失败。' -ForegroundColor Red
        exit 1
    }
    Assert-True (Test-Path $app) '生成 bin\MCLangDiff.exe'
}

Write-Host '== 2. 编译核心测试 ==' -ForegroundColor Cyan
$compileArgs = @(
    '-std=c++17', '-O2', '-municode',
    '-DUNICODE', '-D_UNICODE', '-D_WIN32_WINNT=0x0A00', '-D_WIN32_IE=0x0A00',
    "-I$src",
    (Join-Path $PSScriptRoot 'core_tests.cpp'),
    (Join-Path $src 'lang_file.cpp'),
    (Join-Path $src 'diff.cpp'),
    (Join-Path $src 'util.cpp'),
    '-o', (Join-Path $tmp 'core_tests.exe'),
    '-static', '-static-libgcc', '-static-libstdc++'
)
& g++ @compileArgs
if ($LASTEXITCODE -ne 0) {
    Write-Host '核心测试编译失败。' -ForegroundColor Red
    exit 1
}
Assert-True (Test-Path (Join-Path $tmp 'core_tests.exe')) '生成 tests\tmp\core_tests.exe'

Write-Host '== 3. 运行核心测试 ==' -ForegroundColor Cyan
& (Join-Path $tmp 'core_tests.exe') | ForEach-Object { Write-Host "  $_" }
Assert-Equal 0 $LASTEXITCODE '核心测试全部通过'

Write-Host '== 4. CLI 报告（JSON） ==' -ForegroundColor Cyan
$oldJson = Join-Path $samples 'old.json'
$newJson = Join-Path $samples 'new.json'
$reportJson = Join-Path $tmp 'report_json.txt'
if (Test-Path $reportJson) { Remove-Item $reportJson -Force }
Assert-Equal 0 (Invoke-Report $oldJson $newJson $reportJson) '报告生成退出码为 0'
Assert-True (Test-Path $reportJson) '生成 JSON 报告文件'

if (Test-Path $reportJson) {
    $bytes = [System.IO.File]::ReadAllBytes($reportJson)
    Assert-True ($bytes.Length -gt 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) '报告包含 UTF-8 BOM'

    $text = [System.IO.File]::ReadAllText($reportJson, [System.Text.Encoding]::UTF8)
    Assert-Contains $text '[A] 新增键 / Added' '包含 A. 新增键 区块'
    Assert-Contains $text '[B] 删除键 / Removed' '包含 B. 删除键 区块'
    Assert-Contains $text '[C] 值变更 / Changed' '包含 C. 值变更 区块'
    Assert-Contains $text '统计：新增键 3 个 · 条目 3 条 · 来源：新文件' 'A 块统计信息正确'
    Assert-Contains $text '统计：删除键 2 个 · 条目 2 条 · 来源：旧文件' 'B 块统计信息正确'
    Assert-Contains $text '统计：值变更 2 个 · 旧条目 2 条 / 新条目 2 条' 'C 块统计信息正确'
    Assert-Contains $text '汇总：新增 3 · 删除 2 · 值变更 2 · 未变化 12' '汇总统计正确'
    Assert-Contains $text '"item.demo.new_only.name" : "仅存在于新文件"' '保留 JSON 原条目空格写法'
    Assert-Contains $text '旧 / Old' '值变更块标注旧条目'
    Assert-Contains $text '新 / New' '值变更块标注新条目'
    Assert-True $text.Contains("`r`n") '报告使用 CRLF 换行'

    $addedFirst = $text.IndexOf('"block.demo.new_block.name"')
    $addedSecond = $text.IndexOf('"gui.demo.settings"')
    $addedThird = $text.IndexOf('"item.demo.new_only.name"')
    Assert-True ($addedFirst -ge 0 -and $addedFirst -lt $addedSecond -and $addedSecond -lt $addedThird) '新增条目按新文件出现顺序'

    $removedFirst = $text.IndexOf('"gui.demo.legacy"')
    $removedSecond = $text.IndexOf('"item.demo.old_only.name"')
    Assert-True ($removedFirst -ge 0 -and $removedFirst -lt $removedSecond) '删除条目按旧文件出现顺序'

    $changedFirst = $text.IndexOf('"item.demo.pickaxe.name": "演示镐"')
    $changedSecond = $text.IndexOf('"gui.demo.close": "关闭"')
    Assert-True ($changedFirst -ge 0 -and $changedFirst -lt $changedSecond) '值变更条目按新文件出现顺序'
}

Write-Host '== 5. CLI 报告（LANG） ==' -ForegroundColor Cyan
$oldLang = Join-Path $samples 'old.lang'
$newLang = Join-Path $samples 'new.lang'
$reportLang = Join-Path $tmp 'report_lang.txt'
if (Test-Path $reportLang) { Remove-Item $reportLang -Force }
Assert-Equal 0 (Invoke-Report $oldLang $newLang $reportLang) 'LANG 报告生成退出码为 0'
if (Test-Path $reportLang) {
    $text = [System.IO.File]::ReadAllText($reportLang, [System.Text.Encoding]::UTF8)
    Assert-Contains $text 'block.demo.new_block.name=新方块' 'LANG 新增条目保留 key=value 格式'
    Assert-Contains $text '旧 / Old' 'LANG 变更块标注旧条目'
    Assert-Contains $text '统计：新增键 3 个' 'LANG 统计正确'
    Assert-True (!$text.Contains('## 演示语言文件')) '注释行不进入报告'
}

Write-Host '== 6. 错误处理 ==' -ForegroundColor Cyan
$badOut = Join-Path $tmp 'should_not_exist.txt'
if (Test-Path $badOut) { Remove-Item $badOut -Force }
Assert-Equal 2 (Invoke-Report $oldJson $newLang $badOut) '格式不一致返回退出码 2'
Assert-True (!(Test-Path $badOut)) '失败时不写出报告文件'
Assert-Equal 2 (Invoke-Report $oldJson $oldJson $badOut) '新旧为同一文件返回退出码 2'
Assert-Equal 2 (Invoke-Report $oldJson (Join-Path $samples 'missing.json') $badOut) '文件不存在返回退出码 2'

Write-Host ''
if ($script:failures -eq 0) {
    Write-Host ("全部测试通过（{0} 项检查）。" -f $script:checks) -ForegroundColor Green
    exit 0
}
Write-Host ("测试失败：{0} / {1} 项未通过。" -f $script:failures, $script:checks) -ForegroundColor Red
exit 1
