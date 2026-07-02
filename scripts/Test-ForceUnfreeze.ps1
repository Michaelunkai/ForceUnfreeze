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
if ($item.VersionInfo.ProductName -ne 'ForceUnfreeze') {
    throw "Unexpected ProductName metadata: $($item.VersionInfo.ProductName)"
}
if ($item.VersionInfo.FileVersion -ne '1.3.3.0') {
    throw "Unexpected FileVersion metadata: $($item.VersionInfo.FileVersion)"
}

$logPath = Join-Path (Split-Path -Parent $ExePath) 'ForceUnfreeze.log'

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class ForceUnfreezeSmokeNative {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
  [StructLayout(LayoutKind.Sequential)]
  public struct INPUT {
    public uint type;
    public InputUnion U;
  }
  [StructLayout(LayoutKind.Explicit)]
  public struct InputUnion {
    [FieldOffset(0)] public KEYBDINPUT ki;
    [FieldOffset(0)] public MOUSEINPUT mi;
  }
  [StructLayout(LayoutKind.Sequential)]
  public struct MOUSEINPUT {
    public int dx;
    public int dy;
    public uint mouseData;
    public uint dwFlags;
    public uint time;
    public UIntPtr dwExtraInfo;
  }
  [StructLayout(LayoutKind.Sequential)]
  public struct KEYBDINPUT {
    public ushort wVk;
    public ushort wScan;
    public uint dwFlags;
    public uint time;
    public UIntPtr dwExtraInfo;
  }
  [DllImport("user32.dll", SetLastError=true)] public static extern uint SendInput(uint nInputs, INPUT[] pInputs, int cbSize);
  public const uint INPUT_KEYBOARD = 1;
  public const uint KEYEVENTF_KEYUP = 0x0002;
  public static uint TapF1(int count) {
    uint sent = 0;
    for (int i = 0; i < count; i++) {
      INPUT[] inputs = new INPUT[2];
      inputs[0].type = INPUT_KEYBOARD;
      inputs[0].U.ki.wVk = 0x70;
      inputs[1].type = INPUT_KEYBOARD;
      inputs[1].U.ki.wVk = 0x70;
      inputs[1].U.ki.dwFlags = KEYEVENTF_KEYUP;
      sent += SendInput(2, inputs, Marshal.SizeOf(typeof(INPUT)));
      System.Threading.Thread.Sleep(120);
    }
    return sent;
  }
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

$sent = [ForceUnfreezeSmokeNative]::TapF1(5)
if ($sent -lt 10) {
    if ($alive) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
    throw "SendInput failed to send five F1 taps. Sent inputs: $sent"
}

$deadline = (Get-Date).AddSeconds(8)
$f1RecoveryVerified = $false
do {
    Start-Sleep -Milliseconds 250
    if (Test-Path -LiteralPath $logPath) {
        $currentLog = Get-Content -LiteralPath $logPath -Raw
        if ($currentLog -match 'F1 down detected via' -and $currentLog -match 'Recovery triggered') {
            $f1RecoveryVerified = $true
            break
        }
    }
} while ((Get-Date) -lt $deadline)

if (-not $f1RecoveryVerified) {
    if ($alive) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
    throw 'Five F1 taps did not trigger recovery.'
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
    VersionVerified = $true
    F1TriggerVerified = $true
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

$noInstanceExit = Start-Process -FilePath $ExePath -ArgumentList '--exit' -PassThru
$noInstanceExit.WaitForExit(5000) | Out-Null
Start-Sleep -Milliseconds 500
$leftovers = Get-Process ForceUnfreeze -ErrorAction SilentlyContinue
if ($leftovers) {
    $leftovers | Stop-Process -Force -ErrorAction SilentlyContinue
    throw 'ForceUnfreeze --exit with no existing instance left a process running.'
}

[pscustomobject]@{
    NoExistingInstanceExitCode = $noInstanceExit.ExitCode
    NoExistingInstanceLeftProcess = $false
}
