# VeilKnit Rooms

VeilKnit Rooms is a decentralized room-chat client for the VeilKnit daemon API v3.

## Platforms

- `Android/` — Kotlin/Compose application.
- `Desktop/` — shared C++ room engine and daemon SDK, Windows GUI, Linux console, and Linux GTK 3 GUI.
- `Linux/` — build/run scripts for the two Linux frontends.

## Daemon authorization

Start the daemon first. Rooms will request the `veilknit.rooms` application identity and capabilities. Approve the request in the daemon. Existing protocol-1/2 credentials are migrated to protocol 3 when compatible.

## Linux security correction

The Linux build uses OpenSSL AES-256-GCM and `RAND_bytes`. The earlier non-Windows test cipher was removed and is not suitable for production.

## Privacy-safe releases

See [PRIVACY_BUILD.md](PRIVACY_BUILD.md). Do not publish debug outputs, PDB files, Android signing keys, local credentials, or room databases.

No open-source license has been selected in this package. Add a `LICENSE` before accepting external redistribution or contributions.
