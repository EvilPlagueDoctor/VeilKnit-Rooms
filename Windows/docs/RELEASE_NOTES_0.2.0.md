# Release notes 0.2.0

## Integrated Rust daemon GUI

- Replaced the external Win32 launcher with an `egui`/`eframe` frontend inside
  the Rust daemon executable.
- Added GUI login and signup.
- Added coloured network-attachment status.
- Added selectable/copyable main-DHT banner.
- Added Overview, Activity, Handshakes, Applications, Mailbox, Reputation, and
  Console tabs.
- Added background structured-event capture and tab filtering.
- Added pending-app Approve/Deny buttons.
- Added direct handshake testing and test-message delivery.
- Preserved the legacy line-command administration interface.
- Added Windows notification-area minimization and safe-shutdown menu.
- Added persistent settings and safe-shutdown controls.

## Room GUI

- Retains the 0.1.2 expanded header so channel/status text no longer overlaps
  the room toolbar.

## Packaging

- Removed the obsolete separate `VeilKnitDaemonGui.exe` wrapper target.
- `VeilKnitDaemon.exe` is now the GUI and the network daemon in one process.
