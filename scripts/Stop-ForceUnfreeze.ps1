param(
    [string]$ExePath = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build\Release\ForceUnfreeze.exe')
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $ExePath)) {
    throw "Executable not found: $ExePath"
}

$p = Start-Process -FilePath $ExePath -ArgumentList '--exit' -PassThru
$p.WaitForExit(5000) | Out-Null
Write-Host "Exit requested through: $ExePath"
