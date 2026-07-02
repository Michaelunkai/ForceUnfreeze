param(
    [ValidateSet('Release','Debug')]
    [string]$Configuration = 'Release',
    [switch]$InstallStartup,
    [switch]$InstallScheduledTask
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path

& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Root 'scripts\Generate-Icon.ps1')

$buildDir = Join-Path $Root "build\$Configuration"
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

$vsDevCmdCandidates = @(
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat",
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat",
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat",
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
)
$vsDevCmd = $vsDevCmdCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $vsDevCmd) {
    throw 'Visual Studio 2022 Build Tools were not found. Install the Desktop development with C++ workload.'
}

$obj = Join-Path $buildDir 'main.obj'
$res = Join-Path $buildDir 'ForceUnfreeze.res'
$exe = Join-Path $buildDir 'ForceUnfreeze.exe'
$src = Join-Path $Root 'src\main.cpp'
$rc = Join-Path $Root 'res\ForceUnfreeze.rc'

$debugFlags = if ($Configuration -eq 'Debug') { '/Zi /Od /MTd' } else { '/O2 /MT /DNDEBUG' }
$cmd = @"
call "$vsDevCmd" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%
rc /nologo /fo "$res" "$rc"
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /EHsc /W4 /permissive- /I"$Root\include" /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX $debugFlags /c "$src" /Fo"$obj"
if errorlevel 1 exit /b %errorlevel%
link /nologo /SUBSYSTEM:WINDOWS /MANIFEST:NO /OUT:"$exe" "$obj" "$res" user32.lib shell32.lib advapi32.lib psapi.lib dwmapi.lib
exit /b %errorlevel%
"@

$cmdPath = Join-Path $buildDir 'compile.cmd'
Set-Content -LiteralPath $cmdPath -Value $cmd -Encoding ASCII
& cmd.exe /c "`"$cmdPath`""
if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code $LASTEXITCODE"
}

if (-not (Test-Path $exe)) {
    throw "Build did not produce $exe"
}

$fileInfo = Get-Item $exe
Write-Host "Build successful: $exe"
Write-Host "Size: $([math]::Round($fileInfo.Length / 1KB, 2)) KB"
Write-Host "Modified: $($fileInfo.LastWriteTime)"

$verifyCmd = @"
@echo off
echo === ForceUnfreeze Build Verification ===
echo Checking executable...
if not exist "$exe" (
    echo ERROR: Executable not found
    exit /b 1
)
echo Checking manifest...
if not exist "$buildDir\embedded.manifest" (
    echo WARNING: Embedded manifest not found
)
echo Checking resource file...
if not exist "$buildDir\ForceUnfreeze.res" (
    echo WARNING: Resource file not found
)
echo Build verification complete.
"@
$verifyPath = Join-Path $buildDir 'verify.cmd'
Set-Content -LiteralPath $verifyPath -Value $verifyCmd -Encoding ASCII
& cmd.exe /c "`"$verifyPath`""

if ($InstallStartup) {
    $startup = [Environment]::GetFolderPath('Startup')
    $shortcutPath = Join-Path $startup 'ForceUnfreeze.lnk'
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut($shortcutPath)
    $shortcut.TargetPath = $exe
    $shortcut.WorkingDirectory = Split-Path -Parent $exe
    $shortcut.IconLocation = "$exe,0"
    $shortcut.Save()
    Write-Host "Installed startup shortcut: $shortcutPath"
}

if ($InstallScheduledTask) {
    $taskName = 'ForceUnfreeze'
    $quotedExe = '"' + $exe + '"'
    schtasks.exe /Create /TN $taskName /TR $quotedExe /SC ONLOGON /RL HIGHEST /F | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to install scheduled task $taskName"
    }
    Write-Host "Installed scheduled task: $taskName"
}

Write-Host "ForceUnfreeze executable: $exe"
