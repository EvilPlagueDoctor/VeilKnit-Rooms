# VeilKnit Rooms v0.3.0

## Branding and palette

- Added the supplied VeilKnit shield/weave logo as the Windows executable icon and as the brand mark at the top of the room rail.
- Applied the Android project's VeilKnit palette:
  - window `#131315`
  - panel `#1C1C1F`
  - edit surface `#161618`
  - text `#EBEBEE`
  - muted text `#A5A5AC`
  - accent red `#EF233C`
  - dark red `#911223`
  - border `#414147`
  - success `#55C271`
  - warning `#FFC857`
- Added dark owner-drawn toolbar and Send buttons.
- Darkened the create/join/policy prompt windows.

## Local daemon connection commands

Type these into the normal message composer and press Enter:

```text
/reconnect
```

Closes the current API subscription/client and reconnects using the saved credential.

```text
/reauthorize
```

Closes the connection, removes `%LOCALAPPDATA%\VeilKnit\Rooms\credential-v1.json`, and creates a fresh authorization request in the daemon.

Aliases:

```text
/reconnect-daemon
/reset-daemon
/reset-credential
```

Type `/commands` or `/help` to display the local command summary in the room log. Slash commands are handled locally and are never sent to the room.

## Included daemon fixes

The complete source package also includes the v0.2.1 tray callback type fix and the v0.2.2 handshake-address whitespace normalization fix.
