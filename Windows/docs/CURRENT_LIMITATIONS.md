# Current limitations and validation boundary

This is an early full-stack development build.

## Not yet implemented

- Voice, video, screen sharing, reactions, threads, attachments, and search
- Multiple named text channels inside one room
- Private membership key distribution and key rotation
- Formal recovery-epoch voting and branch merge UI
- History-store rotation after 255 pages
- Attachment/chunk stores
- Fine-grained helper permission editor; helpers currently receive deletion power
- Owner key transfer and app signing-key rotation recovery
- A user-friendly daemon authorization window; approval occurs in the daemon console
- Notifications, tray mode, autostart, updater, and installer

## Current limits

- Messages: 2,000 UTF-8 bytes
- History page: 8 messages
- First owner/replica store generation: 255 pages, roughly 2,040 messages
- The invite access secret is shareable. It obscures public-room traffic but does
  not provide revocable private membership.

## Security notes

- Windows builds use BCrypt AES-256-GCM.
- The non-Windows crypto path exists only so portable core tests can run; it is
  not intended for deployment.
- A remote sender key is initially pinned from room events/manifest information.
  A stronger user-main-DHT-to-app-key certificate publication mechanism remains
  desirable in the daemon protocol.
- Deletion is a signed cooperative tombstone, not guaranteed destruction.

## Validation performed

Successfully built and tested in this environment:

- C++17 core
- VeilKnit C++ SDK static library
- SHA-256 test vector
- encrypted payload round trip
- invite-code round trip
- JSON room persistence
- room engine demo startup and shutdown

Not available in this environment:

- Visual Studio 2022
- Windows SDK headers and libraries
- Rust/Cargo toolchain
- A live Veilid network and daemon process

Therefore the Windows GUI and included Rust daemon still require the first native
Windows build. Build scripts and project files are supplied, but no claim is made
that those two targets were executed here.

## Integrated daemon GUI 0.2.0

- The GUI currently adapts the existing line-oriented administration command
  handler rather than replacing every command with a typed internal action.
  Structured buttons are disabled while a multi-step console prompt is active
  to prevent command-input collisions.
- There is not yet an account logout/restart-with-another-account operation;
  safely shut down and reopen the daemon to change accounts.
- The tray integration is enabled on Windows. Other platforms run the same GUI
  without the notification-area behaviour.
- This source was assembled and the unchanged portable C++ core was re-tested
  in the build environment, but the environment did not contain Cargo or a
  Windows desktop toolchain. The integrated Rust GUI still requires its first
  native Windows `cargo build` on the project machine.
