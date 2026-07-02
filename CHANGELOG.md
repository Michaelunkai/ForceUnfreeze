# Changelog

## 1.3.0.0 - 2026-07-03

- Added runtime hardening: high process priority, shutdown no-retry behavior, Windows restart registration, and power-throttling opt-out.
- Added keyboard-hook watchdog recovery.
- Added Explorer taskbar icon restoration after shell restart.
- Added UTF-8 recovery logging beside the executable.
- Added second-instance controls: launch again to request recovery, launch with `--exit` to stop an existing tray instance.
- Added repeated-trigger escalation: a second recovery request within 60 seconds restarts Explorer without rebooting the machine.
- Added tray notification after recovery completion.
- Added scheduled-task startup installer/uninstaller scripts.
- Added operator wrapper scripts and broader smoke tests.
- Added Control Flow Guard and stricter build verification for the embedded administrator manifest.
