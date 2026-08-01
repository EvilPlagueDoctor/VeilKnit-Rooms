# VeilKnit Rooms v0.3.1

## Fixed

- Fixed Win32 text dialogs closing immediately after appearing. The prompt edit control was accidentally using the same control ID as the dialog's OK button, so focus/change notifications were interpreted as an OK click.
- Create Room, Join Room, and Room Policy dialogs now remain open until the user chooses OK, Cancel, or closes the window.

## Room creation feedback

- `+ Room` changes to `Creating...` while the daemon creates the room's DHT store and initial manifest.
- The button is disabled until creation finishes or fails, preventing duplicate room-creation requests.
- The header/status text shows the room currently being created.
- The button automatically returns to `+ Room` after success or failure.
