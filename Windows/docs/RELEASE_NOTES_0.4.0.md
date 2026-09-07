# VeilKnit Rooms v0.4.0 / daemon transport update

## Cross-platform room delivery

Windows and Android Rooms now use the same wire application identifier:

```text
veilknit.rooms
```

The updated daemons also map the older `veilknit.rooms.desktop` and
`veilknit.rooms.android` identifiers to that shared queue, so existing saved
credentials do not have to be removed merely for this migration.

## Quick and maintained handshakes

Walks and one-off presence checks continue to create short-lived handshakes.
Direct application delivery now requests a maintained handshake. Both peers
exchange check-ins while that application session is active.

A maintained request may explicitly replace an already established quick
handshake. This fixes the previous retry loop where the remote side ignored the
upgrade until the quick session expired.

## Joining and room history

The room owner commits the member-bearing manifest before sending
`join_accepted`. Joiners retry the history read after a short DHT propagation
window. Live and mailbox delivery remain usable while history is still
replicating.

## Invitations

The Windows Share Invite action now shows a scannable QR code and selectable
invite text. The Android Share Invite action provides the same QR workflow.

## Android interface

- The member-list action is in the top bar on narrow screens and no longer
  overlaps the Send button.
- `# room-log` appears in the channel drawer and filters system events.
- Unknown reputation is shown as `reputation pending` instead of `user 0%`.
