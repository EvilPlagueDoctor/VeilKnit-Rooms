# VeilKnit Rooms user guide

## Main interface

- The far-left rail selects rooms.
- The next panel selects `# general` or the local `# room-log`.
- The centre displays verified messages.
- The right panel lists owners, moderators, helpers, and members.
- The bottom composer sends with Enter; Shift+Enter inserts a new line.

## Toolbar

- **Connect** reconnects to the daemon.
- **+ Room** creates an encrypted room and owner history DHT.
- **Join** accepts a `VKROOM1:` invite.
- **Sync** reads the owner DHT and falls back to known replica stores.
- **Share Invite** displays a scannable QR code and provides a copyable room descriptor and access secret.
- **Replica** creates or disables this node's room-history replica store.
- **Room Policy** edits blocked phrases, one per line.
- **Demo** creates a local room without daemon traffic.

## Member administration

Right-click a member to:

- appoint a true moderator; creator only;
- appoint a helper; creator or eligible moderator;
- return someone to ordinary member status;
- ban or unban them in this application;
- copy their main DHT address;
- refresh the daemon reputation summary.

The room engine enforces authority even if a menu item is visible to someone
without permission.

Right-click a message to publish a signed deletion tombstone or copy its event
ID. Cooperative clients hide tombstoned content; decentralized deletion cannot
remove copies already saved by another person.

## Replication and creator absence

The room owner maintains canonical DHT history. Any participant can volunteer as
a replica and advertise a secondary record key. Live messages are gossiped to
known room members. Therefore, two active participants can continue a quiet room
when its creator and moderators are unavailable.

The creator's signed manifest remains authoritative when it becomes reachable.
Replicas preserve availability but do not acquire creator authority.

## Starting the integrated daemon

`VeilKnitDaemon.exe` now contains its own administration interface. Log in or
create an account in that window, then wait for the connection indicator to
become green. Start VeilKnit Rooms afterward and approve it from the daemon's
**Applications** tab.

Closing or minimizing the daemon hides it in the Windows notification area by
default. Use the tray menu's **Safe shutdown** action when you want the network
runtime to stop and publish offline presence.


## Reconnecting the room app to the daemon

Commands can be typed into the normal message composer. They are processed locally and are not posted to the room.

- `/reconnect` closes the current API connection and reconnects with the saved credential.
- `/reauthorize` removes the Rooms credential file and requests fresh approval from the daemon.
- `/commands` shows the currently available local commands in `#room-log`.

Use `/reauthorize` after changing daemon authentication/protocol code or when the daemon says the saved credential is no longer valid. The file removed is `%LOCALAPPDATA%\VeilKnit\Rooms\credential-v1.json`. Room history and room settings are kept.
