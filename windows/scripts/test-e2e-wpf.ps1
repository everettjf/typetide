# E2E 第二轮：WPF 宿主（UIA TextPattern 主路径 + 选区锚点）。
# 前置同 test-e2e.ps1（mock 8199 + TYPETIDE_DATA_DIR 测试配置）。
param(
    [string]$Exe = "$PSScriptRoot\..\build\TypeTide.exe",
    [string]$Expected = "MOCK_TRANSLATION_OK",
    [switch]$Real   # 真实模型：不比对固定译文，轮询等待并做宽松断言
)
$ErrorActionPreference = "Stop"

Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class Win {
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string cls, string title);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, StringBuilder l);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    public struct RECT { public int Left, Top, Right, Bottom; }
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
    for ($i = 0; $i -lt 10; $i++) {
        [Win]::keybd_event(0x10, 0, 0, [UIntPtr]::Zero)
        [Win]::keybd_event(0x10, 0, 2, [UIntPtr]::Zero)
        [Win]::SetForegroundWindow($hwnd) | Out-Null
        Start-Sleep -Milliseconds 150
        if ([Win]::GetForegroundWindow() -eq $hwnd) { return $true }
    }
    return $false
}

$NUL = [NullString]::Value
$VK_MENU = 0x12; $VK_ESCAPE = 0x1B
$failures = 0
function Check([bool]$cond, [string]$name, [string]$detail = "") {
    if ($cond) { Write-Output "  ok   $name" }
    else { Write-Output "  FAIL $name $detail"; $script:failures++ }
}

$sync = Join-Path $env:TEMP "typetide-wpf-sync.txt"
Remove-Item $sync -ErrorAction SilentlyContinue

Write-Output "[wpf] launching TypeTide + WPF host"
$app = Start-Process -FilePath $Exe -PassThru
$hostProc = Start-Process powershell -ArgumentList "-NoProfile", "-File", "`"$PSScriptRoot\wpf-host.ps1`"", "-SyncFile", "`"$sync`"" -PassThru
$hostHwnd = [IntPtr]::Zero
for ($i = 0; $i -lt 60 -and $hostHwnd -eq [IntPtr]::Zero; $i++) {
    Start-Sleep -Milliseconds 250
    $hostHwnd = [Win]::FindWindowW($NUL, "TypeTideWpfHost")
}
Check ($hostHwnd -ne [IntPtr]::Zero) "wpf host window found"
Check (Ensure-Foreground $hostHwnd) "wpf host foregrounded"
Set-Clipboard -Value "剪贴板原始内容"
Start-Sleep -Milliseconds 500

Write-Output "[wpf] Alt+D → popup (UIA TextPattern path)"
Send-Combo @($VK_MENU) 0x44

$popup = [IntPtr]::Zero
for ($i = 0; $i -lt 60 -and $popup -eq [IntPtr]::Zero; $i++) {
    Start-Sleep -Milliseconds 500
    $popup = [Win]::FindWindowW("TypeTidePopup", $NUL)
}
Check ($popup -ne [IntPtr]::Zero) "popup exists"
if ($popup -ne [IntPtr]::Zero) {
    function Read-Popup([IntPtr]$h) {
        $sb = New-Object System.Text.StringBuilder 8192
        [Win]::SendMessageW($h, 0x000D, [IntPtr]8192, $sb) | Out-Null
        return $sb.ToString()
    }
    # 等流式输出稳定
    $text = ""; $prev = $null
    for ($i = 0; $i -lt 120; $i++) {
        Start-Sleep -Milliseconds 500
        $text = Read-Popup $popup
        if ($text -and $text -eq $prev) { break }
        $prev = $text
    }
    Write-Output "  popup text: '$text'"
    if ($Real) {
        Check ($text.Trim().Length -gt 0 -and $text -match "[A-Za-z]") "popup translation (real model)" "got '$text'"
    } else {
        Check ($text -eq $Expected) "popup translation" "got '$text'"
    }

    # 锚点合理性：弹窗应与宿主窗口所在区域相邻（同屏且距离不夸张）
    $pr = New-Object Win+RECT; [Win]::GetWindowRect($popup, [ref]$pr) | Out-Null
    $hr = New-Object Win+RECT; [Win]::GetWindowRect($hostHwnd, [ref]$hr) | Out-Null
    Write-Output "  popup at ($($pr.Left),$($pr.Top))  host at ($($hr.Left),$($hr.Top))-($($hr.Right),$($hr.Bottom))"
    $near = ($pr.Left -ge $hr.Left - 400) -and ($pr.Left -le $hr.Right + 400) -and
            ($pr.Top -ge $hr.Top - 300) -and ($pr.Top -le $hr.Bottom + 300)
    Check $near "popup anchored near selection"
}

Send-Combo @() $VK_ESCAPE
Start-Sleep -Milliseconds 800

Write-Output "[wpf] Ctrl+Shift+R → rewrite (selection persists in WPF)"
Ensure-Foreground $hostHwnd | Out-Null
Start-Sleep -Milliseconds 300
Send-Combo @(0x11, 0x10) 0x52

$content = ""
for ($i = 0; $i -lt 120; $i++) {
    Start-Sleep -Milliseconds 500
    $content = if (Test-Path $sync) { [System.IO.File]::ReadAllText($sync, [System.Text.Encoding]::UTF8) } else { "" }
    if ($content -and $content -ne "你好世界，今天天气很好。") { break }
}
Start-Sleep -Milliseconds 500
Write-Output "  wpf text now: '$content'"
if ($Real) {
    Check ($content.Trim().Length -gt 0 -and $content -ne "你好世界，今天天气很好。" -and $content -match "[A-Za-z]") `
        "rewrite replaced with English text" "got '$content'"
} else {
    Check ($content.Trim() -eq $Expected) "rewrite replaced text" "got '$content'"
}

Write-Output "[wpf] cleanup"
Stop-Process -Id $hostProc.Id -Force -ErrorAction SilentlyContinue
Stop-Process -Id $app.Id -Force -ErrorAction SilentlyContinue
Remove-Item $sync -ErrorAction SilentlyContinue

if ($failures -gt 0) { Write-Output "`n$failures FAILURE(S)"; exit 1 }
Write-Output "`nall wpf e2e checks passed"
exit 0
