param(
    [string]$ExePath = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build\Release\ForceUnfreeze.exe')
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $ExePath)) {
    throw "Executable not found: $ExePath"
}

$taskName = 'ForceUnfreeze'
$quotedExe = '"' + (Resolve-Path -LiteralPath $ExePath).Path + '"'
schtasks.exe /Create /TN $taskName /TR $quotedExe /SC ONLOGON /RL HIGHEST /F | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw "Failed to install scheduled task $taskName"
}

Write-Host "Installed scheduled task: $taskName -> $ExePath"
