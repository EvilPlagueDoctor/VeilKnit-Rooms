# Rooms encrypted local-state migration

Rooms previously persisted its room database itself as plaintext JSON. This build moves that durable room state into the new VeilKnit daemon private-app-storage vault.

## What Rooms used to persist locally

The room database contains much more than UI preferences. For each room it can include:

- room id/name and owner identity/signing key
- the room `access_secret_hex`
- canonical DHT record key and owned store id/generation/limits
- authority and manifest generations
- local replica/join/suspension/reachability state
- member identities, signing keys, roles, online state and reputation cache
- replica record keys/generations
- banned phrases and deleted-message ids
- up to the last 1,000 messages per room, including message text and wire JSON

Older Android builds stored this in `filesDir/rooms-v1.json` (later under a profile-scoped directory). Desktop builds stored it under the Rooms application-data directory, including profile-scoped `rooms-v1.json` files.

## New storage path

After daemon authentication, Rooms now stores the serialized database through the authenticated API actions:

- `put_private_value`
- `get_private_value`
- `delete_private_value`

The daemon chooses the vault from the active account/profile plus authenticated `veilknit.rooms` app identity. Rooms cannot select another account or app vault.

The database is split into 192 KiB generation-scoped chunks. A small manifest containing generation, chunk count, total byte length, and SHA-256 is written last. This makes a new snapshot authoritative only after every encrypted chunk has been committed and avoids the daemon's 512 KiB per-value limit. The smaller chunk size also leaves headroom for base64/JSON and Android Binder transport overhead.

The daemon private-storage implementation encrypts these values at rest with authenticated encryption. Its account storage master is protected by the daemon's account-encryption machinery and per-app keys are derived for each application vault.

## Migration behavior

On the first successful connection with this build:

1. Rooms authenticates to the active daemon account.
2. It first checks for an encrypted Rooms manifest in that account/app vault.
3. If no encrypted state exists, it looks for the old account-scoped plaintext room database (or the pre-account-aware database only when its legacy credential proved ownership of the active profile).
4. It writes the complete state into daemon encrypted storage.
5. Only after that succeeds does it delete the plaintext room database.

The encrypted daemon copy is authoritative on later starts.

## What still remains outside daemon private storage

### Application credential

Rooms still needs its API-v3 application credential before it can authenticate. That credential contains the app id, credential generation, and the 32-byte `secret_hex`. Putting the only copy of that credential inside an authenticated private vault would be circular: Rooms would need the credential in order to retrieve the credential.

Android keeps this credential in its application-private files directory. Windows/Linux use the daemon SDK's account-scoped credential location. Protecting this bootstrap secret further would require platform credential protection (for example Android Keystore / Windows DPAPI) or a different daemon authentication design.

### Windows crash-safe diagnostic log

The Windows GUI still keeps `rooms-running.log` under the Rooms local application-data `logs` directory. It intentionally omits chat message content but can contain timestamps, connection/status text, room-id prefixes, identity/profile information, counts, and error messages. It remains local because its purpose is to survive and diagnose daemon failures/crashes; writing it only through the daemon would defeat that use case.

Linux currently writes diagnostic output to stderr rather than a persistent Rooms log file. Android does not implement the Windows `rooms-running.log` file.
