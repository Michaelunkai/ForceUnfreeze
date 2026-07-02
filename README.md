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

The tray menu includes a status line, `Trigger Recovery`, and `Exit`. Exiting from the tray unhooks the global keyboard hook and removes the tray icon.

Build:

```powershell
.\build.ps1
```

Optional startup shortcut:

```powershell
.\build.ps1 -InstallStartup
```
