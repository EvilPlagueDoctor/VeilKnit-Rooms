# VeilKnit Rooms

VeilKnit Rooms is a decentralized room-chat client for the VeilKnit daemon API v3.

## Platforms

- `Android/` — Kotlin/Compose application.
- `Windows/` — shared C++ room engine and daemon SDK plus the Windows GUI (also used by the Linux frontends).
- `Linux/` — build/run scripts for the two Linux frontends.

## Daemon authorization

Start the daemon first. Rooms will request the `veilknit.rooms` application identity and capabilities. Approve the request in the daemon.

This build follows the daemon's account-aware `profile_id` model: credentials and saved room state are selected per daemon account, and a daemon restart/account switch drops the old session before reconnecting. Legacy **protocol-3** credentials/room data are migrated only after the credential successfully authenticates against the active profile. Protocol-1/2 credentials are no longer upgraded in place; use the daemon's Applications screen to authorize/rotate the app for API v3.

## Linux security correction

The Linux build uses OpenSSL AES-256-GCM and `RAND_bytes`. The earlier non-Windows test cipher was removed and is not suitable for production.

## Privacy-safe releases

See [PRIVACY_BUILD.md](PRIVACY_BUILD.md). Do not publish debug outputs, PDB files, Android signing keys, local credentials, or room databases.

No open-source license has been selected in this package. Add a `LICENSE` before accepting external redistribution or contributions.
