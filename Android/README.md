# VeilKnit Rooms for Android

Native Kotlin/Jetpack Compose port of the desktop VeilKnit Rooms client.

## What is ported

- Discord-style responsive UI for phones, tablets, portrait, and landscape.
- The red/black VeilKnit palette and shield logo.
- Protocol-v1 daemon authorization and HMAC-SHA256 authentication.
- Live messages plus mailbox retrieval through the Android daemon.
- AES-256-GCM encrypted `veilknit.rooms` envelopes compatible with the desktop client.
- Daemon-held Ed25519 application signatures.
- Room creation and `VKROOM1:` invitation codes.
- DHT manifest/history pages and optional room replicas.
- Creator, moderator, helper, and member roles.
- Room-local bans, deletion tombstones, blocked phrases, and reputation lookup.
- Local `/reconnect`, `/reauthorize`, `/sync`, `/replica`, and `/commands` commands.
- Local `rooms.json` and protected daemon credential storage in the Rooms app sandbox.

## Required daemon build

Android application sandboxes cannot directly open another application's private
Unix-domain socket. The companion daemon project therefore includes a
signature-protected Binder proxy named `VeilKnitApiService`.

Install the supplied updated **VeilKnitDaemon_Android** APK first. Both APKs must
be signed with the same certificate. Debug builds created on the same computer
normally use the same Android debug keystore automatically.

## Build in Android Studio Panda

1. Open this folder as an Android Studio project.
2. Allow Gradle synchronization to finish.
3. Select an Android 10/API 29 or newer device.
4. Build or run the `app` configuration.

Command line:

```bat
Build-VeilKnit-Rooms-Android.bat
```

The APK is written under:

```text
app\build\outputs\apk\debug\VeilKnitRooms-debug.apk
```

## First connection

1. Install and start the updated VeilKnit daemon APK.
2. Log into the daemon and wait for it to become ready.
3. Open VeilKnit Rooms.
4. Rooms requests authorization and displays an authorization-pending status.
5. In the daemon, open **Applications**, show pending requests, and approve
   `veilknit.rooms` (older `veilknit.rooms.android` credentials remain accepted by the updated daemon).
6. Rooms authenticates automatically.

## Reauthorization

Type `/reauthorize` in the message composer, or use **Reset daemon authorization**
from the top-right menu. Only `credential-v1.json` is removed; room history and
memberships remain.

## Release signing

The daemon API permission has `protectionLevel="signature"`. When producing
release APKs, configure both projects to use the same release signing key. If
they use different keys, Android correctly refuses the Rooms-to-daemon Binder
connection.

## Current limitations

- Room stores use 64 subkeys (63 history locations after the manifest) and do not yet rotate archives.
- Voice, video, attachments, reactions, and multiple channels are not yet ported.
- The project has been source-checked here, but this environment does not contain
  the Android SDK, so the first full Android Studio compile must run on your
  machine.


## 0.1.0 room storage

Room stores use 64 subkeys and four messages per page. The app reads
`max_value_bytes` from the daemon and checks manifest and page bytes before
`write_app_store`. Older development room databases and 256-subkey stores are
not imported by this baseline.

## Current daemon compatibility

This copy targets VeilKnit local API protocol 3 and `veilknit/app-auth/v2`. Existing protocol-1/2 app credential files are migrated in place when the daemon still recognizes the approved app secret. If the active daemon username has not approved Rooms, use **Reset daemon authorization** (or `/reauthorize`) and approve **VeilKnit Rooms** in the daemon Applications page.

