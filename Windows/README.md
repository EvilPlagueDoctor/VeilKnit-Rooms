# VeilKnit Rooms v0.4.0 + Integrated Daemon GUI 0.3.0

This source tree contains the early VeilKnit network daemon and a native C++17
room-chat application intended for Visual Studio 2022.

The daemon is now a **single Rust GUI program**. It no longer needs a separate
launcher or console wrapper.

## Included

### VeilKnit daemon

- Native Rust `egui`/`eframe` login and signup window
- Coloured network-state indicator: logged out, authenticating, connecting,
  connected, disconnected, stopping, or failed
- Selectable main-DHT banner with a clipboard button
- Overview, Activity, Handshakes, Applications, Mailbox, Reputation, and
  Console tabs
- Background event recording with tab-specific filtering
- Pending application requests with **Approve** and **Deny** buttons
- Old-style daemon command console; Enter submits the current line
- Handshake test page with peer DHT, handshake/status controls, and a direct
  test-message field
- Settings and safe-shutdown controls in the persistent top bar
- Minimize/close to Windows system tray
- Tray commands to reopen the daemon or request safe shutdown

### VeilKnit Rooms

- Native dark Win32 interface
- Encrypted room invitations and messages
- Live delivery plus mailbox fallback
- DHT-backed owner and replica history
- Creator, moderator, helper, and member roles
- Room-scoped moderation and recovery behaviour

## Build everything

Open **Developer PowerShell for VS 2022**:

```powershell
cd path\to\VeilKnit-Rooms
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\Build-All.ps1 -Configuration Release
```

The assembled programs are placed in:

```text
dist\Release\
    VeilKnitDaemon.exe
    VeilKnitRooms.exe
```

The Rust crate still produces `veilid_test_node.exe` internally; the assembly
script copies it as `VeilKnitDaemon.exe`.

See [docs/BUILDING.md](docs/BUILDING.md) for prerequisites and manual commands.

## First run

1. Start `VeilKnitDaemon.exe`.
2. Log in or create an account in the daemon window.
3. Wait for the top indicator to become **Connected**.
4. Start `VeilKnitRooms.exe`.
5. Open the daemon's **Applications** tab.
6. Approve the pending VeilKnit Rooms request.
7. The room app authenticates automatically.

The **Console** tab remains available for all of the original administration
commands. For example, `help`, `app-pending`, `mail status`, walk controls, DHT
test commands, and `q` still work.

## Safe shutdown and the tray

Minimizing or closing the daemon window hides it in the Windows notification
area by default. Right-click the tray icon to reopen it or choose **Safe
shutdown**. Safe shutdown publishes offline presence and runs the daemon's
registered shutdown hooks before exiting.

If an old multi-step console command is waiting for an answer, finish that
prompt first. Structured GUI actions are temporarily disabled so their values
cannot be accidentally consumed by the wrong console prompt.

## Source layout

- `daemon/` — integrated Rust GUI and network daemon
- `third_party/veilknit_cpp/` — C++ protocol-v1 SDK
- `src/` — room engine, persistence, crypto, and Win32 GUI
- `tests/` — portable room-core tests
- `scripts/` — build and assembly scripts
- `docs/` — build, GUI, architecture, and limitations documentation


## Local connection commands

Type `/reconnect` in the message box to reconnect to the daemon with the current credential. Type `/reauthorize` to remove the saved Rooms credential and request fresh approval. `/commands` displays the command summary.


## 0.1.0 room storage

Room history stores use 64 subkeys, giving the daemon a conservative
15,872-byte value budget. Rooms writes four messages per page, refreshes each
owned store's `subkey_count`, `generation`, and `max_value_bytes`, and checks
manifest/page sizes before writing.

This unreleased baseline does not import older development room databases or
256-subkey room stores. Room history archive rotation is not yet implemented;
a full store reports a clear capacity error.

## Current daemon compatibility

This copy targets VeilKnit local API protocol 3 and `veilknit/app-auth/v2`. Existing protocol-1/2 app credential files are migrated in place when the daemon still recognizes the approved app secret. If the active daemon username has not approved Rooms, use **Reset daemon authorization** (or `/reauthorize`) and approve **VeilKnit Rooms** in the daemon Applications page.



## Linux frontends

The same C++ room engine now builds `veilknit-rooms-console` and, when GTK 3 development files are installed, `veilknit-rooms-gui`. See the repository-level `Linux/README.md`.
