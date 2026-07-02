$ErrorActionPreference = 'Stop'

$taskName = 'ForceUnfreeze'
schtasks.exe /Delete /TN $taskName /F | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw "Failed to remove scheduled task $taskName"
}

Write-Host "Removed scheduled task: $taskName"
