# VeilKnit Rooms architecture

## Authority

Authority is room-scoped and deterministic:

1. creator;
2. creator-appointed true moderators;
3. bounded moderator helpers;
4. recovery participants for availability only;
5. ordinary members.

Only the creator can appoint a true moderator. Moderators may appoint helpers up
to their owner-assigned limit. Helpers cannot delegate further.

All room events are encrypted and signed with daemon-held application keys.
Private app signing keys and DHT writer packages never cross the local API.

## Wire envelopes

Every live/mailbox event contains public routing metadata and an AES-GCM
ciphertext. The canonical outer JSON, excluding `signature_hex`, is signed in a
domain specific to the event kind.

Implemented event kinds include:

- `chat`
- `presence`
- `join_request`
- `join_accepted`
- `manifest`
- `history_page`
- `replica_manifest`
- `replica_history_page`
- `role_grant`
- `moderation`
- `replica_ad`

The room key is derived from the invite access secret, room ID, and authority
epoch.

## Storage

Subkey 0 stores a signed manifest. Subkeys 1 through 255 store signed history
pages. Pages currently contain up to eight message envelopes to remain beneath
the daemon's 32 KiB value limit.

Owner stores are canonical. Replica stores are availability aids; clients accept
message signatures from them but do not treat replica manifests as authority.

## Recovery

Messages are sent to all known non-banned members and the owner. A deduplication
ID prevents relay loops. If owner presence has aged past the recovery threshold,
new messages are labelled recovery traffic. Mailbox delivery eventually carries
the same signed event to the owner for canonical history inclusion.

## Reputation

Daemon reputation is advisory and displayed per member. Room bans and role
changes remain room-specific signed events. The app may request a daemon
app-scoped restriction, but that restriction does not apply to other apps.
