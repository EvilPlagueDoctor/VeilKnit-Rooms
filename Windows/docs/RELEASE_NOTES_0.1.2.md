# VeilKnit Rooms / Tooling v0.1.2

## What changed

### Rooms GUI
- Increased the top header area so the selected channel title and status line sit **below** the toolbar buttons rather than drawing behind them.
- Added a compact header status line showing connection state, member count, and whether local replication is active.

### New daemon GUI
- Added `VeilKnitDaemonGui.exe`, a native Win32 control panel for the Rust daemon.
- Starts and stops the daemon process.
- Captures stdout and stderr into a scrolling log window.
- Lets you send console commands directly to the daemon (`app-pending`, `app-approve`, `q`, and so on).
- Lets you save the captured log to a text file.
- Can launch `VeilKnitRooms.exe` from the same directory.

### Build packaging
- The top-level build script now copies `VeilKnitDaemonGui.exe` into the assembled `dist\<Configuration>` folder.

## Expected release folder

```text
VeilKnitDaemon.exe
VeilKnitDaemonGui.exe
VeilKnitRooms.exe
README.md
USER_GUIDE.md
CURRENT_LIMITATIONS.md
```
