# Current limitations

- One `#general` text channel per room.
- No voice, video, file attachments, reactions, threads, or notifications yet.
- Room history currently uses one 64-subkey store and does not rotate to archive stores after page 63.
- A room member currently has one active room signing key. Multi-device key certificates and key rotation remain future work.
- Recovery traffic is labelled, but formal recovery branch comparison/merging is not yet implemented.
- Private membership key epochs and post-removal key rotation are not yet implemented; the current open-encrypted room key is carried by the invite.
- The daemon must already be running and logged in before Rooms can authenticate.
- The two APKs must be signed by the same Android certificate because the Binder API is signature-protected.
