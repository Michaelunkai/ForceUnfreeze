# ForceUnfreeze

Native Windows 11 tray utility that listens for `F1` pressed five times within two seconds or held for three seconds, then runs a non-reboot recovery pass intended to restore responsiveness.

The recovery pass uses Windows-supported actions only:

- Synthesize `Win+Ctrl+Shift+B` to ask Windows to reset the graphics driver.
- Broadcast timeout-safe responsiveness nudges.
- Nudge visible windows and hung windows through timeout-safe messages.
- Boost foreground process priority.
- Trim process working sets.
- Restart Explorer only if the shell window appears missing or hung.
- Open Task Manager as a last-resort operator surface if it is not already open.
- Keep the recovery worker on a high-priority thread.
- Opt the utility itself out of execution-speed power throttling where Windows supports it.
- Register the app for Windows restart recovery if the tray process crashes.
- Restore the tray icon after Explorer/taskbar restarts.
- Log startup and recovery steps to `ForceUnfreeze.log` beside the executable.
- Escalate a repeated recovery trigger within 60 seconds into an Explorer restart, while still avoiding a machine reboot.

The tray menu includes a status line, `Trigger Recovery`, and `Exit`. Exiting from the tray unhooks the global keyboard hook and removes the tray icon.

If ForceUnfreeze is already running, launching `ForceUnfreeze.exe` again requests a recovery pass from the existing tray process. Launching `ForceUnfreeze.exe --exit` asks the existing tray process to exit cleanly.

Wrapper scripts:

```powershell
.\scripts\Invoke-ForceUnfreezeRecovery.ps1
.\scripts\Stop-ForceUnfreeze.ps1
```

Build:

```powershell
.\build.ps1
```

Optional startup shortcut:

```powershell
.\build.ps1 -InstallStartup
```

Optional elevated logon scheduled task:

```powershell
.\build.ps1 -InstallScheduledTask
# or
.\scripts\Install-ForceUnfreezeScheduledTask.ps1
```

Remove the scheduled task:

```powershell
.\scripts\Uninstall-ForceUnfreezeScheduledTask.ps1
```

Smoke test:

```powershell
.\scripts\Test-ForceUnfreeze.ps1
```
