# 端到端冒烟测试：真实驱动 TypeTide.exe + 自建编辑窗口 + mock 后端。
# 前置：mock-openai.ps1 已在 8199 端口运行；TYPETIDE_DATA_DIR 指向含
# openai/8199 配置的测试目录（Alt+D 读、Ctrl+Shift+R 写、rewritePreview=false）。
param(
    [string]$Exe = "$PSScriptRoot\..\build\TypeTide.exe",
    [string]$Expected = "MOCK_TRANSLATION_OK",
    [switch]$Real,   # 真实模型：不比对固定译文，轮询等待并做宽松断言
    [byte]$ReadKey = 0x44,
    [byte]$RewriteKey = 0x52,
    [byte[]]$RewriteModifiers = @(0x11, 0x10)
)
$ErrorActionPreference = "Stop"

Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class Win {
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string cls, string title);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowExW(IntPtr parent, IntPtr after, string cls, string title);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, StringBuilder l);
    [DllImport("user32.dll", EntryPoint="SendMessageW")] public static extern IntPtr SendMessageInt(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT rect);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
}
"@

function Send-Combo([byte[]]$mods, [byte]$vk) {
    foreach ($m in $mods) { [Win]::keybd_event($m, 0, 0, [UIntPtr]::Zero) }
    [Win]::keybd_event($vk, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 50
    [Win]::keybd_event($vk, 0, 2, [UIntPtr]::Zero)
    foreach ($m in $mods) { [Win]::keybd_event($m, 0, 2, [UIntPtr]::Zero) }
}

function Ensure-Foreground([IntPtr]$hwnd) {
    # 后台进程直接 SetForegroundWindow 会被前台锁拒绝；
    # 注入一次无害按键让本进程成为“最近输入来源”后即可切换。
    for ($i = 0; $i -lt 10; $i++) {
        [Win]::keybd_event(0x10, 0, 0, [UIntPtr]::Zero)   # Shift down
        [Win]::keybd_event(0x10, 0, 2, [UIntPtr]::Zero)   # Shift up
        [Win]::SetForegroundWindow($hwnd) | Out-Null
        Start-Sleep -Milliseconds 150
        if ([Win]::GetForegroundWindow() -eq $hwnd) { return $true }
    }
    return $false
}

function Get-WindowText([IntPtr]$hwnd) {
    $sb = New-Object System.Text.StringBuilder 8192
    [Win]::SendMessageW($hwnd, 0x000D, [IntPtr]8192, $sb) | Out-Null   # WM_GETTEXT
    return $sb.ToString()
}

$VK_MENU = 0x12; $VK_CONTROL = 0x11; $VK_ESCAPE = 0x1B
$NUL = [NullString]::Value   # PowerShell 会把 $null 封送成空串，必须用 NullString
$failures = 0
function Check([bool]$cond, [string]$name, [string]$detail = "") {
    if ($cond) { Write-Output "  ok   $name" }
    else { Write-Output "  FAIL $name $detail"; $script:failures++ }
}

Write-Output "[e2e] launching TypeTide"
$app = Start-Process -FilePath $Exe -PassThru
Start-Sleep -Seconds 2
Check (-not $app.HasExited) "app running"

Write-Output "[e2e] launching edit host"
$host2 = Start-Process powershell -ArgumentList "-NoProfile", "-File", "`"$PSScriptRoot\edit-host.ps1`"" -PassThru
$hostHwnd = [IntPtr]::Zero
for ($i = 0; $i -lt 60 -and $hostHwnd -eq [IntPtr]::Zero; $i++) {
    Start-Sleep -Milliseconds 250
    $hostHwnd = [Win]::FindWindowW($NUL, "TypeTideTestHost")
}
Check ($hostHwnd -ne [IntPtr]::Zero) "edit host window found"
$editHwnd = [Win]::FindWindowExW($hostHwnd, [IntPtr]::Zero, $NUL, $NUL)  # 第一个子窗口 = 文本框
Check ($editHwnd -ne [IntPtr]::Zero) "edit control found"

Write-Output "[e2e] setting Chinese text + selection"
$sb = New-Object System.Text.StringBuilder
[void]$sb.Append("你好世界，今天天气很好。")
[Win]::SendMessageW($editHwnd, 0x000C, [IntPtr]::Zero, $sb) | Out-Null   # WM_SETTEXT
Start-Sleep -Milliseconds 200
Check ((Get-WindowText $editHwnd) -eq "你好世界，今天天气很好。") "text set in edit"
Set-Clipboard -Value "剪贴板原始内容"   # 替换后应还原成它
Check (Ensure-Foreground $hostHwnd) "edit host foregrounded"
Start-Sleep -Milliseconds 300
# EM_SETSEL 全选（消息级，确定性；注入 Ctrl+A 偶发丢失）
[Win]::SendMessageInt($editHwnd, 0x00B1, [IntPtr]::Zero, [IntPtr](-1)) | Out-Null
Start-Sleep -Milliseconds 300

Write-Output "[e2e] Alt+D → popup"
Send-Combo @($VK_MENU) $ReadKey

# 轮询等弹窗出现（真实模型冷启动可能较慢）
$popup = [IntPtr]::Zero
for ($i = 0; $i -lt 60 -and $popup -eq [IntPtr]::Zero; $i++) {
    Start-Sleep -Milliseconds 500
    $popup = [Win]::FindWindowW("TypeTidePopup", $NUL)
}
Check ($popup -ne [IntPtr]::Zero) "popup window exists"
if ($popup -ne [IntPtr]::Zero) {
    # 等译文流式输出完成（连续两次读取相同且非空视为稳定）
    $text = ""; $prev = $null
    for ($i = 0; $i -lt 120; $i++) {
        Start-Sleep -Milliseconds 500
        $text = Get-WindowText $popup
        if ($text -and $text -eq $prev) { break }
        $prev = $text
    }
    Write-Output "  popup text: '$text'"
    if ($Real) {
        Check ($text.Trim().Length -gt 0) "popup translation non-empty"
        Check ($text -ne "你好世界，今天天气很好。") "popup translation differs from source"
        Check ($text -match "[A-Za-z]") "popup translation looks English" "got '$text'"
    } else {
        Check ($text -eq $Expected) "popup translation" "got '$text'"
    }
}

Write-Output "[e2e] hold Replace, drag out to cancel, then release Replace"
if ($popup -ne [IntPtr]::Zero) {
    $rect = New-Object Win+RECT
    [Win]::GetClientRect($popup, [ref]$rect) | Out-Null
    $scale = [Win]::GetDpiForWindow($popup) / 96.0
    $x = $rect.Right - [int](50 * $scale)
    $y = $rect.Bottom - [int](28 * $scale)
    $point = [IntPtr](($y -shl 16) -bor $x)
    [Win]::SendMessageInt($popup, 0x0201, [IntPtr]1, $point) | Out-Null
    Start-Sleep -Milliseconds 500 # Longer than the old 250ms mouse wait.
    Check ([Win]::FindWindowW("TypeTidePopup", $NUL) -eq $popup) "Replace waits for mouse-up"
    Check ((Get-WindowText $editHwnd) -eq "你好世界，今天天气很好。") "holding Replace leaves selection unchanged"
    [Win]::SendMessageInt($popup, 0x0202, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    Check ([Win]::FindWindowW("TypeTidePopup", $NUL) -eq $popup) "release outside cancels Replace"
    [Win]::SendMessageInt($popup, 0x0201, [IntPtr]1, $point) | Out-Null
    [Win]::SendMessageInt($popup, 0x0202, [IntPtr]::Zero, $point) | Out-Null
    for ($i = 0; $i -lt 30; $i++) {
        Start-Sleep -Milliseconds 100
        if ((Get-WindowText $editHwnd) -eq $text) { break }
    }
    Check ((Get-WindowText $editHwnd) -eq $text) "Replace pastes full translation, not literal V"
    Start-Sleep -Milliseconds 500
    Check ((Get-Clipboard -Raw) -eq "剪贴板原始内容") "Replace restores clipboard"
    Check ([Win]::FindWindowW("TypeTidePopup", $NUL) -eq [IntPtr]::Zero) "Replace closes popup"
    [Win]::SendMessageW($editHwnd, 0x000C, [IntPtr]::Zero, $sb) | Out-Null
}

Write-Output "[e2e] rewrite shortcut → replace in place"
Ensure-Foreground $hostHwnd | Out-Null
Start-Sleep -Milliseconds 300
[Win]::SendMessageInt($editHwnd, 0x00B1, [IntPtr]::Zero, [IntPtr](-1)) | Out-Null   # EM_SETSEL 全选
Start-Sleep -Milliseconds 300
Send-Combo $RewriteModifiers $RewriteKey

# 轮询等内容被替换
$content = ""
for ($i = 0; $i -lt 120; $i++) {
    Start-Sleep -Milliseconds 500
    $content = Get-WindowText $editHwnd
    if ($content -and $content -ne "你好世界，今天天气很好。") { break }
}
Start-Sleep -Milliseconds 500   # 等剪贴板还原完成
Write-Output "  edit now: '$content'"
if ($Real) {
    Check ($content.Trim().Length -gt 0 -and $content -ne "你好世界，今天天气很好。" -and $content -match "[A-Za-z]") `
        "rewrite replaced with English text" "got '$content'"
} else {
    Check ($content.Trim() -eq $Expected) "rewrite replaced text" "got '$content'"
}

# 剪贴板应已还原为替换前内容
$clip = Get-Clipboard -Raw
Check ($clip -eq "剪贴板原始内容") "clipboard restored" "got '$clip'"

Write-Output "[e2e] cleanup"
Stop-Process -Id $host2.Id -Force -ErrorAction SilentlyContinue
Stop-Process -Id $app.Id -Force -ErrorAction SilentlyContinue

if ($failures -gt 0) { Write-Output "`n$failures FAILURE(S)"; exit 1 }
Write-Output "`nall e2e checks passed"
exit 0
