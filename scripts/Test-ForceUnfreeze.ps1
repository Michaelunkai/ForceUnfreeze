param(
    [string]$ExePath = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build\Release\ForceUnfreeze.exe')
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $ExePath)) {
    throw "Executable not found: $ExePath"
}

$item = Get-Item -LiteralPath $ExePath
if ($item.Length -le 0) {
    throw "Executable is empty: $ExePath"
}

$logPath = Join-Path (Split-Path -Parent $ExePath) 'ForceUnfreeze.log'

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class ForceUnfreezeSmokeNative {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
}
'@

$p = Start-Process -FilePath $ExePath -PassThru
Start-Sleep -Milliseconds 900

$alive = Get-Process -Id $p.Id -ErrorAction SilentlyContinue
$hwnd = [ForceUnfreezeSmokeNative]::FindWindow('ForceUnfreezeTrayWindow', 'ForceUnfreeze')

if ($hwnd -eq [IntPtr]::Zero) {
    if ($alive) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
    throw 'ForceUnfreeze hidden control window was not found.'
}

[ForceUnfreezeSmokeNative]::SendMessage($hwnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 900
$still = Get-Process -Id $p.Id -ErrorAction SilentlyContinue
if ($still) {
    Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
    throw 'ForceUnfreeze did not exit after WM_CLOSE.'
}

if (-not (Test-Path -LiteralPath $logPath)) {
    throw "Expected log was not created: $logPath"
}
$logText = Get-Content -LiteralPath $logPath -Raw
if ($logText -notmatch 'Keyboard hook installed') {
    throw 'Expected keyboard hook log entry was not found.'
}

[pscustomobject]@{
    ExePath = $ExePath
    Length = $item.Length
    Started = [bool]$alive
    WindowFound = $true
    ExitedAfterClose = $true
    LogVerified = $true
}

$p2 = Start-Process -FilePath $ExePath -PassThru
Start-Sleep -Milliseconds 900
$hwnd2 = [ForceUnfreezeSmokeNative]::FindWindow('ForceUnfreezeTrayWindow', 'ForceUnfreeze')
if ($hwnd2 -eq [IntPtr]::Zero) {
    if (Get-Process -Id $p2.Id -ErrorAction SilentlyContinue) { Stop-Process -Id $p2.Id -Force -ErrorAction SilentlyContinue }
    throw 'Second smoke run did not create the hidden control window.'
}

$exitRequester = Start-Process -FilePath $ExePath -ArgumentList '--exit' -PassThru
$exitRequester.WaitForExit(5000) | Out-Null
Start-Sleep -Milliseconds 900
if (Get-Process -Id $p2.Id -ErrorAction SilentlyContinue) {
    Stop-Process -Id $p2.Id -Force -ErrorAction SilentlyContinue
    throw 'ForceUnfreeze did not exit after second-instance --exit request.'
}

[pscustomobject]@{
    SecondInstanceExitRequest = $true
    ExitRequesterCode = $exitRequester.ExitCode
    ExistingInstanceExited = $true
}
