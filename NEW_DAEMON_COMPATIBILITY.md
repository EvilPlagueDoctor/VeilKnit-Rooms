# New daemon compatibility update

This Rooms source tree has been adapted to the account-aware VeilKnit daemon supplied with this project.

## What changed

- Android now reads `profile_id` and `daemon_instance_id` from `getDaemonStateJson()` before authenticating.
- Android credentials and room databases are stored separately for each daemon profile.
- Desktop endpoint discovery now reads `profile_id` and follows the daemon SDK's profile-scoped credential layout:
  `DaemonNetwork/credentials/<profile_id>/<app_id>.json`.
- Desktop room databases are also stored per daemon profile.
- Legacy unscoped Rooms data is migrated only after its protocol-v3 credential successfully authenticates against the active profile.
- Protocol-v1/v2 credential files are no longer rewritten into protocol v3; they require API-v3 authorization/rotation.
- Both platform implementations discard the old message subscription and reconnect when the daemon-side stream is replaced.
- `get_identity` consumes the additive `display_name` and `profile_id` fields while preserving the legacy `username` alias as a fallback.
- The Android AIDL contract is unchanged and matches the supplied daemon's Binder interface.

## Lost/forgotten credentials

The new daemon keeps the application registration even when the client-side credential is missing. If Rooms reports that `veilknit.rooms` is already registered, use the daemon **Applications** page to rotate the Rooms application key, then reconnect.

## Validation performed

- All Rooms API action names used by the Kotlin/C++ clients were checked against the supplied daemon source: no missing actions were found.
- Both Android AIDL files were byte-for-byte compared with the supplied daemon AIDL files: they match.
- The shared C++ core/console built successfully with CMake/GCC 14.2 and OpenSSL 3.5.6.
- `ctest` passed the Rooms core test suite (1/1).
- A full Android Gradle compile could not run in the isolated build environment because Gradle 9.4.1 was not cached and the wrapper could not reach `services.gradle.org`. A local Kotlin parser/type pass found no parser-level syntax errors; unresolved Android/Compose/project symbols are expected without the Android Gradle classpath.
