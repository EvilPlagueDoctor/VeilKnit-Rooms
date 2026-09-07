# Android room-join crash containment

This build fixes a process-crash path in the Android Rooms client.

## Root cause

`RoomEngine` launches UI/network operations as root coroutines from a `SupervisorJob` scope. The existing `reportErrors()` helper used `Job.invokeOnCompletion` to observe failures and update the UI. `invokeOnCompletion` does not consume an uncaught coroutine exception. A transient exception from a room operation could therefore still reach Android's uncaught-exception handler and terminate the process.

Joining a room was particularly exposed because its final direct `join_request` to the room owner deliberately rethrows a delivery failure. An offline owner, temporarily unavailable route/mailbox, Binder failure, malformed response, or similar transient condition could therefore kill the whole app instead of producing a recoverable join error.

## Changes

- Added a `CoroutineExceptionHandler` to the application `RoomEngine` scope. Root-coroutine failures are now contained instead of reaching Android's process-level uncaught exception handler.
- Contained failures are written to Logcat under tag `VeilKnitRooms`, including their stack trace.
- `reportErrors()` still reports the operation failure in the Rooms UI, but it no longer declares the daemon disconnected merely because a room-level operation failed.
- `joinRoom()` now treats failure to deliver the initial `join_request` as recoverable. The room remains stored locally and the UI tells the user to use Retry when the owner/network becomes reachable.
- Existing synchronization behavior is unchanged: history synchronization may fail independently and already reports that condition without aborting the join.

## Capturing future failures

Filter Logcat for:

```
VeilKnitRooms
```

A contained operation failure now includes the original exception and stack trace without terminating Rooms.

## Validation note

A full Android Gradle compile could not be run in the sandbox because Gradle 9.4.1 is not cached and the environment cannot reach `services.gradle.org`. The change is source-local to `RoomEngine.kt`; desktop/shared sources were not modified.
