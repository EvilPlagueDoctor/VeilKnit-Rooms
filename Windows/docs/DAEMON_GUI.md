# Integrated daemon GUI

## Login and signup

`VeilKnitDaemon.exe` opens directly to the protected account screen. Login and
signup execute off the UI thread. After authentication, the existing Tokio
network runtime starts in a background thread.

## Persistent top bar

The top bar remains visible on every administration tab and contains:

- a coloured network-state dot and status word;
- the active account name;
- the Settings button;
- the Safe shutdown button.

The main-DHT banner appears immediately below the top-level navigation. Its key
can be selected manually or copied with one click.

## Tabs

### Overview

Shows network/DHT status, pending application count, startup-stage progress, and
recent structured events.

### Activity

Displays all structured daemon events. The text filter searches event source
and event details.

### Handshakes

Provides fields for a peer main-DHT address and a direct test message. It can:

- start a handshake;
- inspect handshake status;
- send a test application message after establishment;
- display only handshake-related events.

### Applications

Lists pending local application registrations, requested capabilities, and
Approve/Deny controls. Application and identity events are shown below.

### Mailbox and Reputation

These tabs read the same background event stream but show only events from the
selected subsystem.

### Console

Preserves the old administration workflow. The prompt shown above the input is
the exact prompt currently expected by the legacy command handler. Press Enter
to send the line.

GUI actions that internally use legacy commands are disabled while a multi-step
console operation is waiting for an answer. This prevents an approval ID,
handshake DHT, or shutdown command from being consumed as unrelated input.

## Tray behaviour

On Windows, minimize and close hide the daemon window in the notification area.
The tray menu provides:

- **Open VeilKnit**
- **Safe shutdown**

The daemon remains active while hidden. The tray icon and menu are created by
the Rust daemon process itself; there is no secondary launcher process.

## Shutdown

Safe shutdown queues the daemon's normal `q` administration operation, which
flushes mailbox/DHT state and then invokes `NetworkSupervisor` shutdown hooks.
The window closes only after the runtime reports that shutdown completed.
