package com.veilknit.rooms.data

import android.content.Context
import com.veilknit.rooms.daemon.Credential
import com.veilknit.rooms.daemon.DaemonConnector
import com.veilknit.rooms.daemon.VeilKnitClient
import com.veilknit.rooms.protocol.Encoding
import com.veilknit.rooms.protocol.decodeEnvelope
import com.veilknit.rooms.protocol.makeInviteCode
import com.veilknit.rooms.protocol.parseInviteCode
import com.veilknit.rooms.protocol.roomManifestBody
import com.veilknit.rooms.protocol.signedEnvelope
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.catch
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import org.json.JSONArray
import org.json.JSONObject
import java.util.Locale

private const val ROOM_STORE_SUBKEYS = 64
private const val ROOM_MESSAGES_PER_PAGE = 4
private const val FALLBACK_ROOM_VALUE_LIMIT = 15_872
private const val MAX_CHAT_UTF8_BYTES = 2_000

class RoomEngine(context: Context) {
    private val appContext = context.applicationContext
    private val repository = RoomRepository(appContext)
    private val connector = DaemonConnector(appContext)
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val lock = Mutex()
    private var client: VeilKnitClient? = null
    private var subscriptionJob: Job? = null
    private var heartbeatJob: Job? = null

    private val initialRooms = repository.loadRooms()
    private val mutableState = MutableStateFlow(
        RoomsUiState(
            rooms = initialRooms,
            selectedRoomIndex = if (initialRooms.isEmpty()) -1 else 0,
        ),
    )
    val state: StateFlow<RoomsUiState> = mutableState.asStateFlow()

    fun start() {
        connect(resetCredential = false)
        if (heartbeatJob == null) heartbeatJob = scope.launch { heartbeatLoop() }
    }

    fun stop() {
        subscriptionJob?.cancel()
        connector.disconnect()
        scope.cancel()
    }

    fun connect(resetCredential: Boolean) {
        scope.launch {
            setStatus(ConnectionState.Connecting, if (resetCredential) "Resetting daemon authorization…" else "Connecting to VeilKnit Daemon…")
            runCatching { connectInternal(resetCredential) }
                .onFailure { setStatus(ConnectionState.Error, it.message ?: "Could not connect to daemon") }
        }
    }

    private suspend fun connectInternal(resetCredential: Boolean) {
        subscriptionJob?.cancel()
        client = null
        connector.disconnect()
        if (resetCredential) repository.credentialFile.delete()
        connector.connect()

        val credential = if (repository.credentialFile.isFile) {
            Credential.load(repository.credentialFile)
        } else {
            mutableState.update { it.copy(connection = ConnectionState.Authorizing, status = "Approve VeilKnit Rooms in the daemon Applications tab", authorizationPending = true) }
            VeilKnitClient.register(
                connector = connector,
                appId = APP_ID,
                displayName = APP_NAME,
                onPending = { mutableState.update { it.copy(authorizationPending = true) } },
            ).also { it.save(repository.credentialFile) }
        }

        val api = VeilKnitClient.authenticate(connector, credential)
        // Rewrite older credential-v1.json files using protocol 3 after the
        // daemon confirms the secret is still valid.
        credential.save(repository.credentialFile)
        val identity = api.identity()
        val signing = api.signingIdentity()
        val ownedStores = api.listStores().associateBy { it.storeId }
        client = api
        lock.withLock {
            val current = mutableState.value
            val rooms = current.rooms.map { room ->
                val descriptor = ownedStores[room.ownedStoreId]
                val member = room.members[identity.mainDht] ?: Member(mainDht = identity.mainDht)
                room.copy(
                    ownedStoreSubkeys = descriptor?.subkeyCount ?: room.ownedStoreSubkeys,
                    ownedStoreValueLimit = descriptor?.maxValueBytes ?: room.ownedStoreValueLimit,
                    ownedStoreGeneration = descriptor?.generation ?: room.ownedStoreGeneration,
                    members = room.members + (
                        identity.mainDht to member.copy(
                            displayName = member.displayName.takeUnless { it == "Unknown" || it.isBlank() } ?: identity.username,
                            signingKey = signing.publicKeyHex,
                            online = true,
                            lastSeen = now(),
                        )
                    ),
                )
            }
            mutableState.value = current.copy(
                connection = ConnectionState.Connected,
                status = "Connected as ${identity.username}",
                username = identity.username,
                mainDht = identity.mainDht,
                signingKey = signing.publicKeyHex,
                rooms = rooms,
                demoMode = false,
                authorizationPending = false,
            )
            saveRoomsLocked()
        }
        startSubscription(api)
        runCatching { api.triggerMessageRetrieval() }
        mutableState.value.rooms.filterNot(Room::suspended).map(Room::roomId).forEach { syncRoomInternal(it) }

        // Re-announce joins after reconnect. Older platform-specific app IDs
        // could leave a room saved locally even though the owner's daemon never
        // delivered the original join request.
        mutableState.value.rooms
            .filter { !it.suspended && it.ownerMainDht != identity.mainDht }
            .forEach { room ->
                runCatching {
                    sendControl(
                        room,
                        "join_request",
                        JSONObject()
                            .put("display_name", identity.username)
                            .put("signing_key", signing.publicKeyHex),
                        directRecipient = room.ownerMainDht,
                    )
                    appendOperation("Re-sent join request to ${shortIdentity(room.ownerMainDht)}")
                }.onFailure { error ->
                    appendOperation("Could not re-send join request — ${error.message}")
                }
            }
    }

    private fun startSubscription(api: VeilKnitClient) {
        subscriptionJob?.cancel()
        subscriptionJob = scope.launch {
            api.subscribeMessages()
                .catch { error -> setStatus(ConnectionState.Error, "Message stream stopped: ${error.message}") }
                .collect { line ->
                    if (line.optString("stream") != "application_messages") return@collect
                    val event = line.optJSONObject("event") ?: return@collect
                    val payload = event.optString("payload_base64")
                    if (payload.isNotBlank()) {
                        val sender = event.optString("sender_main_dht")
                        appendOperation("Received daemon message from ${shortIdentity(sender)}")
                        handleWireMessage(Encoding.unbase64(payload).toString(Charsets.UTF_8), true)
                    }
                }
        }
    }

    fun submitText(text: String) {
        val cleaned = text.trim()
        if (cleaned.isEmpty()) return
        if (cleaned.startsWith('/')) {
            handleCommand(cleaned)
        } else {
            sendChat(cleaned)
        }
    }

    private fun handleCommand(command: String) {
        when (command.lowercase(Locale.ROOT)) {
            "/reconnect", "/reconnect-daemon" -> connect(false)
            "/reauthorize", "/reset-daemon", "/reset-credential" -> connect(true)
            "/sync" -> syncSelectedRoom()
            "/replica" -> toggleReplica()
            "/commands", "/help" -> addSystemMessageToSelected(
                "Local commands: /reconnect, /reauthorize, /sync, /replica, /commands. Slash commands are not sent to the room.",
            )
            else -> addSystemMessageToSelected("Unknown local command: $command")
        }
    }

    fun createRoom(name: String) = scope.launch {
        beginOperation("Creating room")
        val trimmed = name.trim()
        if (trimmed.isEmpty()) return@launch
        val api = requireClient()
        val snapshot = mutableState.value
        require(snapshot.mainDht.isNotBlank()) { "Connect to the daemon before creating a room" }
        var room = Room(
            roomId = Encoding.randomHex(16),
            name = trimmed,
            ownerMainDht = snapshot.mainDht,
            ownerSigningKey = snapshot.signingKey,
            accessSecretHex = Encoding.randomHex(32),
            createdAt = now(),
            ownerLastSeen = now(),
            messagesPerPage = ROOM_MESSAGES_PER_PAGE,
            localReplica = true,
            members = mapOf(
                snapshot.mainDht to Member(
                    mainDht = snapshot.mainDht,
                    displayName = snapshot.username,
                    signingKey = snapshot.signingKey,
                    role = Role.Owner,
                    online = true,
                    replica = true,
                    lastSeen = now(),
                    maxHelpers = 8,
                ),
            ),
        )
        room = room.copy(messages = listOf(systemMessage(room.roomId, "Room created. Copy the invite to add people.")))
        val store = api.createStore(safeStoreName(trimmed, room.roomId, false), ROOM_STORE_SUBKEYS)
        room = room.copy(
            canonicalRecordKey = store.recordKey,
            ownedStoreId = store.storeId,
            ownedStoreSubkeys = store.subkeyCount,
            ownedStoreValueLimit = store.maxValueBytes,
            ownedStoreGeneration = store.generation,
            members = room.members + (
                snapshot.mainDht to room.members.getValue(snapshot.mainDht).copy(replicaRecordKey = store.recordKey)
            ),
        )
        room = persistRoom(room)
        lock.withLock {
            val current = mutableState.value
            mutableState.value = current.copy(rooms = current.rooms + room, selectedRoomIndex = current.rooms.size)
            saveRoomsLocked()
        }
        sendControl(room, "presence", JSONObject().put("replica", true).put("record_key", room.canonicalRecordKey))
    }.reportErrors("Creating room")

    fun joinRoom(inviteCode: String) = scope.launch {
        beginOperation("Joining room")
        val invite = parseInviteCode(inviteCode)
        val snapshot = mutableState.value
        require(snapshot.mainDht.isNotBlank()) { "Connect to the daemon before joining a room" }
        if (snapshot.rooms.any { it.roomId == invite.roomId }) {
            selectRoom(snapshot.rooms.indexOfFirst { it.roomId == invite.roomId })
            return@launch
        }
        var room = Room(
            roomId = invite.roomId,
            name = invite.roomName,
            ownerMainDht = invite.ownerMainDht,
            ownerSigningKey = invite.ownerPublicKey,
            accessSecretHex = invite.accessSecretHex,
            canonicalRecordKey = invite.recordKey,
            authorityEpoch = invite.authorityEpoch,
            createdAt = now(),
            members = mapOf(
                invite.ownerMainDht to Member(
                    mainDht = invite.ownerMainDht,
                    displayName = "Room owner",
                    signingKey = invite.ownerPublicKey,
                    role = Role.Owner,
                ),
                snapshot.mainDht to Member(
                    mainDht = snapshot.mainDht,
                    displayName = snapshot.username,
                    signingKey = snapshot.signingKey,
                    role = Role.Member,
                    online = true,
                    lastSeen = now(),
                ),
            ),
            messages = listOf(systemMessage(invite.roomId, "Joined ${invite.roomName}; synchronizing history…")),
        )
        lock.withLock {
            val current = mutableState.value
            mutableState.value = current.copy(rooms = current.rooms + room, selectedRoomIndex = current.rooms.size)
            saveRoomsLocked()
        }
        syncRoomInternal(room.roomId)
        room = roomById(room.roomId) ?: room
        sendControl(
            room,
            "join_request",
            JSONObject().put("display_name", snapshot.username).put("signing_key", snapshot.signingKey),
            directRecipient = room.ownerMainDht,
        )
        appendOperation("Join request sent to ${shortIdentity(room.ownerMainDht)}; waiting for owner acceptance")
    }.reportErrors("Joining room")

    fun sendChat(text: String) = scope.launch {
        beginOperation("Sending chat message")
        require(text.toByteArray(Charsets.UTF_8).size <= MAX_CHAT_UTF8_BYTES) {
            "Messages are currently limited to $MAX_CHAT_UTF8_BYTES UTF-8 bytes"
        }
        val room = selectedRoom() ?: error("No room is selected")
        require(!room.suspended) { "This room is paused. Use Retry before contacting it again." }
        val snapshot = mutableState.value
        require(localMember(room)?.banned != true) { "You are banned from this room" }
        val lowered = text.lowercase(Locale.ROOT)
        require(room.bannedPhrases.none { it.isNotBlank() && lowered.contains(it.lowercase(Locale.ROOT)) }) {
            "Message matches the room phrase policy"
        }
        val recovery = room.ownerLastSeen + 180 < now()
        val body = JSONObject().put("text", text).put("recovery", recovery)
        val wire = makeEnvelope(room, "chat", body)
        sendEnvelopeToRoom(room, wire)
        handleWireMessage(wire, false)
    }.reportErrors("Sending chat message")

    fun selectRoom(index: Int) {
        mutableState.update { current ->
            if (index !in current.rooms.indices) current else current.copy(selectedRoomIndex = index)
        }
    }

    fun removeSelectedRoom() = scope.launch {
        val room = selectedRoom() ?: return@launch
        val localMainDht = mutableState.value.mainDht
        if (!room.suspended && room.ownerMainDht != localMainDht && client != null) {
            runCatching {
                sendControl(room, "leave", JSONObject().put("subject", localMainDht))
            }.onFailure { error ->
                appendOperation("Could not announce room departure — ${error.message}")
            }
        }
        lock.withLock {
            val current = mutableState.value
            val removedIndex = current.rooms.indexOfFirst { it.roomId == room.roomId }
            if (removedIndex < 0) return@withLock
            val updatedRooms = current.rooms.filterNot { it.roomId == room.roomId }
            val selected = when {
                updatedRooms.isEmpty() -> -1
                current.selectedRoomIndex > removedIndex -> current.selectedRoomIndex - 1
                current.selectedRoomIndex >= updatedRooms.size -> updatedRooms.lastIndex
                else -> current.selectedRoomIndex
            }
            mutableState.value = current.copy(
                rooms = updatedRooms,
                selectedRoomIndex = selected,
                status = "Removed room ‘${room.name}’ from this device",
            )
            saveRoomsLocked()
        }
        appendOperation("Removed local room ${room.roomId.take(8)}")
    }.reportErrors("Removing room")

    fun retrySelectedRoom() = scope.launch {
        val selected = selectedRoom() ?: return@launch
        val room = lock.withLock {
            val current = mutableState.value
            val index = current.rooms.indexOfFirst { it.roomId == selected.roomId }
            if (index < 0) return@withLock selected
            val cleared = current.rooms[index].copy(
                suspended = false,
                reachabilityFailures = 0,
                lastReachabilityError = "",
            )
            val rooms = current.rooms.toMutableList().also { it[index] = cleared }
            mutableState.value = current.copy(rooms = rooms)
            saveRoomsLocked()
            cleared
        }
        mutableState.update { it.copy(status = "Retrying room ‘${room.name}’…") }
        syncRoomInternal(room.roomId)
        val refreshed = roomById(room.roomId) ?: room
        val snapshot = mutableState.value
        runCatching {
            if (refreshed.ownerMainDht != snapshot.mainDht) {
                sendControl(
                    refreshed,
                    "join_request",
                    JSONObject()
                        .put("display_name", snapshot.username)
                        .put("signing_key", snapshot.signingKey),
                    directRecipient = refreshed.ownerMainDht,
                )
            } else {
                sendControl(
                    refreshed,
                    "presence",
                    JSONObject()
                        .put("replica", refreshed.localReplica)
                        .put("record_key", localMember(refreshed)?.replicaRecordKey.orEmpty()),
                )
            }
        }.onFailure { error -> appendOperation("Room retry failed — ${error.message}") }
    }.reportErrors("Retrying room")

    fun selectedInviteCode(): String = mutableState.value.selectedRoom
        ?.takeIf { it.canonicalRecordKey.isNotBlank() }
        ?.let(::makeInviteCode)
        .orEmpty()

    fun syncSelectedRoom() {
        val room = mutableState.value.selectedRoom ?: return
        if (room.suspended) {
            mutableState.update { it.copy(status = "This room is paused. Use Retry before contacting it again.") }
            return
        }
        scope.launch { beginOperation("Synchronizing room"); syncRoomInternal(room.roomId) }.reportErrors("Synchronizing room")
    }

    fun toggleReplica() = scope.launch {
        beginOperation("Updating replica state")
        var room = selectedRoom() ?: error("No room selected")
        val api = requireClient()
        val enable = !room.localReplica
        if (enable && room.ownedStoreId.isBlank()) {
            val store = api.createStore(safeStoreName(room.name, room.roomId, true), ROOM_STORE_SUBKEYS)
            val self = localMember(room) ?: Member(mainDht = mutableState.value.mainDht)
            room = room.copy(
                ownedStoreId = store.storeId,
                ownedStoreSubkeys = store.subkeyCount,
                ownedStoreValueLimit = store.maxValueBytes,
                ownedStoreGeneration = store.generation,
                localReplica = true,
                members = room.members + (self.mainDht to self.copy(replica = true, replicaRecordKey = store.recordKey)),
            )
            room = persistRoom(room)
        } else {
            val self = localMember(room)
            room = room.copy(
                localReplica = enable,
                members = if (self == null) room.members else room.members + (self.mainDht to self.copy(replica = enable)),
            )
        }
        replaceRoom(room)
        sendControl(
            room,
            "replica_ad",
            JSONObject()
                .put("enabled", enable)
                .put("record_key", localMember(room)?.replicaRecordKey.orEmpty()),
        )
    }.reportErrors("Updating replica state")

    fun changeMemberRole(subject: String, role: Role) = scope.launch {
        val room = selectedRoom() ?: error("No room selected")
        val myRole = localMember(room)?.role ?: Role.Member
        require(subject != room.ownerMainDht) { "The creator role cannot be changed" }
        if (role == Role.Moderator) require(myRole == Role.Owner) { "Only the creator can appoint true moderators" }
        if (role == Role.Helper) require(myRole == Role.Moderator) { "Only true moderators can appoint helpers" }
        require(myRole == Role.Owner || myRole == Role.Moderator) { "You do not have role-management permission" }
        if (myRole == Role.Moderator && role == Role.Helper) {
            val me = localMember(room)!!
            val helpers = room.members.values.count { it.role == Role.Helper }
            require(me.maxHelpers > 0 && helpers < me.maxHelpers) { "Your helper delegation limit has been reached" }
        }
        sendControl(
            room,
            "role_grant",
            JSONObject()
                .put("subject", subject)
                .put("role", role.wire)
                .put("max_helpers", if (role == Role.Moderator) 4 else 0),
        )
    }.reportErrors()

    fun toggleMemberBan(subject: String) = scope.launch {
        val room = selectedRoom() ?: error("No room selected")
        require(canModerate(room, mutableState.value.mainDht, "ban")) { "You do not have ban permission" }
        require(subject != room.ownerMainDht) { "The creator cannot be banned" }
        val member = room.members[subject]
        if (member?.role == Role.Moderator) require(mutableState.value.mainDht == room.ownerMainDht) {
            "Only the creator can ban a true moderator"
        }
        val action = if (member?.banned == true) "unban" else "ban"
        sendControl(room, "moderation", JSONObject().put("action", action).put("subject", subject))
        if (action == "ban") runCatching { requireClient().requestRestriction(subject, "Banned in room ${room.roomId}") }
    }.reportErrors()

    fun deleteMessage(eventId: String) = scope.launch {
        val room = selectedRoom() ?: error("No room selected")
        require(canModerate(room, mutableState.value.mainDht, "delete")) { "You cannot delete messages" }
        sendControl(room, "moderation", JSONObject().put("action", "delete").put("message_id", eventId))
    }.reportErrors()

    fun setBannedPhrases(phrases: List<String>) = scope.launch {
        val room = selectedRoom() ?: error("No room selected")
        require(canModerate(room, mutableState.value.mainDht, "set_phrases")) { "You cannot change phrase policy" }
        val array = JSONArray()
        phrases.map(String::trim).filter(String::isNotEmpty).forEach(array::put)
        sendControl(room, "moderation", JSONObject().put("action", "set_phrases").put("phrases", array))
    }.reportErrors()

    fun refreshMemberReputation(subject: String) = scope.launch {
        val view = requireClient().reputationView(subject)
        val classification = view.opt("class")?.toString().orEmpty()
        val confidence = view.optInt("confidence")
        val room = selectedRoom() ?: return@launch
        val member = room.members[subject] ?: return@launch
        replaceRoom(
            room.copy(
                members = room.members + (subject to member.copy(reputationClass = classification, reputationConfidence = confidence)),
            ),
        )
    }.reportErrors()

    private suspend fun handleWireMessage(wire: String, allowRelay: Boolean) {
        val roomId = runCatching { JSONObject(wire).optString("room_id") }.getOrNull().orEmpty()
        val room = roomById(roomId) ?: return
        if (room.suspended) {
            appendOperation("Ignored room event for paused room ${room.roomId.take(8)}")
            return
        }
        val decoded = decodeEnvelope(requireClient(), room, wire)
        appendOperation("Room event ${decoded.kind} from ${shortIdentity(decoded.senderMainDht)}")
        var updated = room
        val senderWasKnown = updated.members.containsKey(decoded.senderMainDht)
        var sender = updated.members[decoded.senderMainDht] ?: Member(mainDht = decoded.senderMainDht)
        if (sender.displayName == "Unknown" || sender.displayName.isBlank()) sender = sender.copy(displayName = decoded.senderName)
        if (sender.signingKey.isBlank()) sender = sender.copy(signingKey = decoded.senderPublicKey)
        require(sender.signingKey == decoded.senderPublicKey) { "Sender signing key changed without a room grant" }
        sender = sender.copy(online = true, lastSeen = now())
        updated = updated.copy(
            members = updated.members + (sender.mainDht to sender),
            ownerLastSeen = if (sender.mainDht == updated.ownerMainDht) now() else updated.ownerLastSeen,
        )

        when (decoded.kind) {
            "chat" -> {
                if (updated.messages.any { it.eventId == decoded.eventId } || sender.banned) {
                    noteRoomSuccess(room.roomId)
                    return
                }
                val text = decoded.body.getString("text")
                if (updated.bannedPhrases.any { it.isNotBlank() && text.contains(it, ignoreCase = true) }) {
                    noteRoomSuccess(room.roomId)
                    return
                }
                val message = ChatMessage(
                    eventId = decoded.eventId,
                    roomId = updated.roomId,
                    senderMainDht = decoded.senderMainDht,
                    senderName = decoded.senderName,
                    senderSigningKey = decoded.senderPublicKey,
                    text = text,
                    wireJson = wire,
                    createdAt = decoded.createdAt,
                    verified = true,
                    deleted = decoded.eventId in updated.deletedMessageIds,
                    recovery = decoded.body.optBoolean("recovery"),
                )
                updated = updated.copy(messages = updated.messages + message)
                replaceRoom(updated)
                noteRoomSuccess(room.roomId)
                if ((updated.ownerMainDht == mutableState.value.mainDht || updated.localReplica) && updated.ownedStoreId.isNotBlank()) {
                    runCatching { persistRoom(updated) }
                }
                if (allowRelay && decoded.senderMainDht != mutableState.value.mainDht) sendEnvelopeToRoom(updated, wire, decoded.senderMainDht)
                return
            }
            "presence" -> updated = updated.copy(
                members = updated.members + (sender.mainDht to sender.copy(
                    replica = decoded.body.optBoolean("replica"),
                    replicaRecordKey = decoded.body.optString("record_key"),
                )),
            )
            "join_request" -> {
                sender = sender.copy(
                    displayName = decoded.body.optString("display_name", decoded.senderName),
                    signingKey = decoded.senderPublicKey,
                    role = if (sender.role == Role.Owner) Role.Member else sender.role,
                )
                updated = updated.copy(
                    members = updated.members + (sender.mainDht to sender),
                    messages = if (senderWasKnown) updated.messages else updated.messages + systemMessage(updated.roomId, "${sender.displayName} joined the room."),
                )
                replaceRoom(updated)
                noteRoomSuccess(room.roomId)
                if (updated.ownerMainDht == mutableState.value.mainDht) {
                    // Publish the new member in the canonical manifest before
                    // telling the joiner to read it. This avoids a stale-DHT
                    // race immediately after the join handshake.
                    updated = persistRoom(updated)
                    sendControl(updated, "join_accepted", roomManifestBody(updated), decoded.senderMainDht)
                }
                return
            }
            "join_accepted", "manifest" -> {
                require(decoded.senderMainDht == updated.ownerMainDht && decoded.senderPublicKey == updated.ownerSigningKey) {
                    "Manifest was not signed by the room owner"
                }
                updated = applyManifest(updated, decoded.body)
                if (decoded.kind == "join_accepted") {
                    appendOperation("Owner accepted the room join")
                    scope.launch {
                        // The owner has just committed the member-bearing
                        // manifest. DHT propagation can lag the live reply, so
                        // retry instead of leaving the room permanently in the
                        // initial "history unavailable" state.
                        repeat(3) { attempt ->
                            delay(if (attempt == 0) 1_500 else 3_000)
                            runCatching { syncRoomInternal(updated.roomId) }
                            if (mutableState.value.status == "Room history synchronized") return@launch
                        }
                    }
                }
            }
            "role_grant" -> {
                val subject = decoded.body.getString("subject")
                val role = Role.fromWire(decoded.body.getString("role"))
                if (role == Role.Moderator) require(decoded.senderMainDht == updated.ownerMainDht) { "Only owner may appoint moderators" }
                if (role == Role.Helper) require(updated.members[decoded.senderMainDht]?.role == Role.Moderator) { "Only a true moderator may appoint helpers" }
                val member = updated.members[subject] ?: Member(mainDht = subject)
                updated = updated.copy(
                    members = updated.members + (subject to member.copy(role = role, maxHelpers = decoded.body.optInt("max_helpers"))),
                    messages = updated.messages + systemMessage(updated.roomId, "${member.displayName} is now a ${role.label}."),
                )
            }
            "moderation" -> {
                val action = decoded.body.getString("action")
                require(canModerate(updated, decoded.senderMainDht, action)) { "Issuer lacks moderation authority" }
                when (action) {
                    "ban", "unban" -> {
                        val subject = decoded.body.getString("subject")
                        require(subject != updated.ownerMainDht) { "Room owner cannot be banned" }
                        val member = updated.members[subject] ?: Member(mainDht = subject)
                        val banned = action == "ban"
                        updated = updated.copy(
                            members = updated.members + (subject to member.copy(banned = banned)),
                            messages = updated.messages + systemMessage(updated.roomId, "${member.displayName} was ${if (banned) "banned" else "unbanned"}."),
                        )
                    }
                    "delete" -> {
                        val id = decoded.body.getString("message_id")
                        updated = updated.copy(
                            deletedMessageIds = (updated.deletedMessageIds + id).distinct(),
                            messages = updated.messages.map { if (it.eventId == id) it.copy(deleted = true) else it },
                        )
                    }
                    "set_phrases" -> {
                        val phrases = decoded.body.getJSONArray("phrases").stringList()
                        updated = updated.copy(
                            bannedPhrases = phrases,
                            messages = updated.messages + systemMessage(updated.roomId, "The room phrase policy was updated."),
                        )
                    }
                }
            }
            "replica_ad" -> {
                val enabled = decoded.body.optBoolean("enabled")
                val recordKey = decoded.body.optString("record_key")
                val replica = ReplicaInfo(sender.mainDht, recordKey, lastSeen = now())
                updated = updated.copy(
                    members = updated.members + (sender.mainDht to sender.copy(replica = enabled, replicaRecordKey = recordKey)),
                    replicas = if (enabled) (updated.replicas.filterNot { it.mainDht == sender.mainDht } + replica)
                    else updated.replicas.filterNot { it.mainDht == sender.mainDht },
                )
            }
            "leave" -> {
                val subject = decoded.body.optString("subject", decoded.senderMainDht)
                require(subject == decoded.senderMainDht) { "A leave event may only remove its signer" }
                require(subject != updated.ownerMainDht) { "The room owner cannot leave without closing the room" }
                val member = updated.members[subject]
                updated = updated.copy(
                    members = updated.members - subject,
                    replicas = updated.replicas.filterNot { it.mainDht == subject },
                    messages = updated.messages + systemMessage(
                        updated.roomId,
                        "${member?.displayName ?: shortIdentity(subject)} left the room.",
                    ),
                )
            }
            "replica_manifest", "history_page", "replica_history_page" -> {
                noteRoomSuccess(room.roomId)
                return
            }
        }
        replaceRoom(updated)
        noteRoomSuccess(room.roomId)
    }

    private suspend fun sendControl(room: Room, kind: String, body: JSONObject, directRecipient: String = "") {
        require(!room.suspended || kind == "leave") { "This room is paused. Use Retry before contacting it again." }
        val wire = makeEnvelope(room, kind, body)
        if (directRecipient.isNotBlank()) {
            runCatching { requireClient().sendMessage(directRecipient, wire.toByteArray()) }
                .onSuccess {
                    noteRoomSuccess(room.roomId)
                    appendOperation("Queued $kind for ${shortIdentity(directRecipient)}")
                }
                .onFailure { error ->
                    noteRoomFailure(room.roomId, error.message ?: "Direct room delivery failed")
                    throw error
                }
        } else sendEnvelopeToRoom(room, wire)
        handleWireMessage(wire, false)
    }

    private suspend fun makeEnvelope(room: Room, kind: String, body: JSONObject): String {
        val snapshot = mutableState.value
        return signedEnvelope(
            requireClient(), room, kind, snapshot.mainDht, snapshot.username, snapshot.signingKey, body,
        )
    }

    private suspend fun sendEnvelopeToRoom(room: Room, wire: String, omit: String = "") {
        require(!room.suspended) { "This room is paused. Use Retry before contacting it again." }
        val recipients = buildSet {
            add(room.ownerMainDht)
            room.members.values.filterNot(Member::banned).forEach { add(it.mainDht) }
        }.filter { it.isNotBlank() && it != mutableState.value.mainDht && it != omit }

        var delivered = 0
        var lastError: Throwable? = null
        recipients.forEach { recipient ->
            runCatching { requireClient().sendMessage(recipient, wire.toByteArray()) }
                .onSuccess {
                    delivered += 1
                    appendOperation("Queued room event for ${shortIdentity(recipient)}")
                }
                .onFailure { error ->
                    lastError = error
                    appendOperation("Room event delivery failed for ${shortIdentity(recipient)} — ${error.message}")
                }
        }
        if (recipients.isNotEmpty() && delivered == 0) {
            val message = buildString {
                append("Could not queue the room event for any peer")
                lastError?.message?.takeIf(String::isNotBlank)?.let { append(": ").append(it) }
            }
            noteRoomFailure(room.roomId, message)
            throw IllegalStateException(message, lastError)
        }
        if (delivered > 0) noteRoomSuccess(room.roomId)
    }

    private suspend fun persistRoom(source: Room): Room {
        if (source.ownedStoreId.isBlank()) return source
        val history = source.messages.filter { !it.system && it.wireJson.isNotBlank() }.map(ChatMessage::wireJson)
        val messagesPerPage = source.messagesPerPage.coerceAtLeast(1)
        val latestPage = if (history.isEmpty()) 1 else ((history.size - 1) / messagesPerPage + 1)
        val subkeyCount = source.ownedStoreSubkeys.takeIf { it > 0 } ?: ROOM_STORE_SUBKEYS
        require(latestPage < subkeyCount) { "Room store is full; archive rotation is not implemented" }
        val begin = (latestPage - 1) * messagesPerPage
        var room = source.copy(latestPage = latestPage)
        val pageBody = JSONObject()
            .put("page", latestPage)
            .put("writer_main_dht", mutableState.value.mainDht)
            .put("messages", JSONArray().also { array -> history.drop(begin).forEach(array::put) })
        val pageKind = if (room.ownerMainDht == mutableState.value.mainDht) "history_page" else "replica_history_page"
        val pageWire = makeEnvelope(room, pageKind, pageBody)
        val manifestBody = roomManifestBody(room).apply {
            if (room.ownerMainDht != mutableState.value.mainDht) put("canonical_record_key", room.canonicalRecordKey)
        }
        val manifestKind = if (room.ownerMainDht == mutableState.value.mainDht) "manifest" else "replica_manifest"
        val manifestWire = makeEnvelope(room, manifestKind, manifestBody)
        val valueLimit = room.ownedStoreValueLimit.takeIf { it > 0 } ?: FALLBACK_ROOM_VALUE_LIMIT
        val manifestBytes = manifestWire.toByteArray()
        val pageBytes = pageWire.toByteArray()
        require(manifestBytes.size <= valueLimit) {
            "Room manifest is ${manifestBytes.size} bytes, exceeding this store's $valueLimit-byte value limit"
        }
        require(pageBytes.size <= valueLimit) {
            "Room history page is ${pageBytes.size} bytes, exceeding this store's $valueLimit-byte value limit"
        }
        val descriptor = requireClient().writeStore(
            storeId = room.ownedStoreId,
            writes = listOf(0 to manifestBytes, latestPage to pageBytes),
            expectedGeneration = room.ownedStoreGeneration,
        )
        room = room.copy(
            ownedStoreSubkeys = descriptor.subkeyCount,
            ownedStoreValueLimit = descriptor.maxValueBytes,
            ownedStoreGeneration = descriptor.generation,
            manifestGeneration = room.manifestGeneration + 1,
        )
        replaceRoom(room)
        return room
    }

    private suspend fun syncRoomInternal(roomId: String) {
        var room = roomById(roomId) ?: return
        if (room.suspended) return
        var synced = false
        var firstError: String? = null
        if (room.canonicalRecordKey.isNotBlank()) {
            val result = runCatching { room = syncFromRecord(room, room.canonicalRecordKey, true) }
            synced = result.isSuccess
            result.exceptionOrNull()?.let { error ->
                firstError = error.message ?: error::class.java.simpleName
                appendOperation("Canonical history sync failed for ${room.roomId.take(8)} — $firstError")
            }
        }
        if (!synced) {
            for (replica in room.replicas) {
                if (replica.recordKey.isBlank()) continue
                val result = runCatching { room = syncFromRecord(room, replica.recordKey, false) }
                if (result.isSuccess) {
                    synced = true
                    break
                }
                if (firstError == null) firstError = result.exceptionOrNull()?.message
            }
        }
        replaceRoom(room)
        mutableState.update {
            it.copy(
                status = if (synced) {
                    "Room history synchronized"
                } else {
                    firstError?.let { message -> "History unavailable: $message" }
                        ?: "No reachable history store; live/mailbox gossip remains available"
                },
            )
        }
        if (synced) noteRoomSuccess(roomId)
        else noteRoomFailure(roomId, firstError ?: "No reachable history store")
    }

    private suspend fun syncFromRecord(source: Room, recordKey: String, canonical: Boolean): Room {
        var room = source
        val manifestRead = requireClient().readPublicStore(recordKey, listOf(0), true)
        val manifestBytes = manifestRead.values.firstOrNull()?.bytes ?: error("History manifest unavailable")
        val manifest = decodeEnvelope(requireClient(), room, manifestBytes.toString(Charsets.UTF_8))
        if (canonical) {
            require(manifest.kind == "manifest" && manifest.senderMainDht == room.ownerMainDht && manifest.senderPublicKey == room.ownerSigningKey) {
                "Canonical manifest is not owner-signed"
            }
            room = applyManifest(room, manifest.body)
        } else require(manifest.kind == "replica_manifest") { "Not a replica manifest" }
        replaceRoom(room)
        val latest = manifest.body.optInt("latest_page", 1).coerceAtMost(ROOM_STORE_SUBKEYS - 1)
        for (locations in (1..latest).toList().chunked(6)) {
            val pageRead = requireClient().readPublicStore(recordKey, locations, true)
            for (value in pageRead.values) {
                val bytes = value.bytes ?: continue
                runCatching {
                    val page = decodeEnvelope(requireClient(), room, bytes.toString(Charsets.UTF_8))
                    if (page.kind == "history_page" || page.kind == "replica_history_page") {
                        page.body.getJSONArray("messages").stringList().forEach { wire ->
                            runCatching { handleWireMessage(wire, false) }
                        }
                    }
                }
            }
        }
        return roomById(room.roomId) ?: room
    }

    private fun applyManifest(room: Room, manifest: JSONObject): Room {
        val members = room.members.toMutableMap()
        manifest.optJSONArray("members")?.let { array ->
            for (index in 0 until array.length()) {
                val item = array.getJSONObject(index)
                val id = item.getString("main_dht")
                val old = members[id] ?: Member(mainDht = id)
                members[id] = old.copy(
                    displayName = item.optString("display_name", old.displayName),
                    signingKey = item.optString("signing_key", old.signingKey),
                    role = Role.fromWire(item.optString("role", "member")),
                    banned = item.optBoolean("banned"),
                    replica = item.optBoolean("replica"),
                    replicaRecordKey = item.optString("replica_record_key"),
                    maxHelpers = item.optInt("max_helpers"),
                )
            }
        }
        val deleted = manifest.optJSONArray("deleted_message_ids")?.stringList().orEmpty()
        return room.copy(
            name = manifest.optString("name", room.name),
            authorityEpoch = manifest.optLong("authority_epoch", room.authorityEpoch),
            manifestGeneration = manifest.optLong("manifest_generation", room.manifestGeneration),
            latestPage = manifest.optInt("latest_page", room.latestPage),
            messagesPerPage = manifest.optInt("messages_per_page", room.messagesPerPage),
            members = members,
            bannedPhrases = manifest.optJSONArray("banned_phrases")?.stringList().orEmpty(),
            deletedMessageIds = deleted,
            messages = room.messages.map { if (it.eventId in deleted) it.copy(deleted = true) else it },
        )
    }

    private suspend fun heartbeatLoop() {
        while (true) {
            delay(30_000)
            if (mutableState.value.connection != ConnectionState.Connected) continue
            val rooms = mutableState.value.rooms
            rooms.filterNot(Room::suspended).forEach { original ->
                var room = original.copy(
                    members = original.members.mapValues { (_, member) ->
                        if (member.mainDht != mutableState.value.mainDht && member.lastSeen + 120 < now()) member.copy(online = false) else member
                    },
                )
                replaceRoom(room)
                runCatching {
                    sendControl(
                        room,
                        "presence",
                        JSONObject()
                            .put("replica", room.localReplica)
                            .put("record_key", localMember(room)?.replicaRecordKey.orEmpty()),
                    )
                }.onFailure { error -> appendOperation("Room heartbeat failed for ${room.roomId.take(8)} — ${error.message}") }
            }
        }
    }

    private suspend fun noteRoomFailure(roomId: String, error: String) {
        lock.withLock {
            val current = mutableState.value
            val index = current.rooms.indexOfFirst { it.roomId == roomId }
            if (index < 0) return@withLock
            val room = current.rooms[index]
            if (room.suspended) return@withLock
            val failures = (room.reachabilityFailures + 1).coerceAtMost(3)
            val suspended = failures >= 3
            val updated = room.copy(
                suspended = suspended,
                reachabilityFailures = failures,
                lastReachabilityError = error,
            )
            val rooms = current.rooms.toMutableList().also { it[index] = updated }
            mutableState.value = current.copy(
                rooms = rooms,
                status = if (suspended && current.selectedRoomIndex == index) {
                    "Room paused after repeated failures. Use Retry from the room menu."
                } else current.status,
            )
            saveRoomsLocked()
            if (suspended) appendOperation("Paused unreachable room ${room.roomId.take(8)} — $error")
        }
    }

    private suspend fun noteRoomSuccess(roomId: String) {
        lock.withLock {
            val current = mutableState.value
            val index = current.rooms.indexOfFirst { it.roomId == roomId }
            if (index < 0) return@withLock
            val room = current.rooms[index]
            if (room.suspended || (room.reachabilityFailures == 0 && room.lastReachabilityError.isBlank())) return@withLock
            val rooms = current.rooms.toMutableList().also {
                it[index] = room.copy(reachabilityFailures = 0, lastReachabilityError = "")
            }
            mutableState.value = current.copy(rooms = rooms)
            saveRoomsLocked()
        }
    }

    private fun canModerate(room: Room, issuer: String, action: String): Boolean {
        if (issuer == room.ownerMainDht) return true
        val member = room.members[issuer] ?: return false
        if (member.banned) return false
        return when (member.role) {
            Role.Moderator -> action != "grant_moderator"
            Role.Helper -> action == "delete"
            else -> false
        }
    }

    private fun selectedRoom(): Room? = mutableState.value.selectedRoom
    private fun roomById(roomId: String): Room? = mutableState.value.rooms.firstOrNull { it.roomId == roomId }
    private fun localMember(room: Room): Member? = room.members[mutableState.value.mainDht]
    private fun requireClient(): VeilKnitClient = client ?: error("Not connected to VeilKnit Daemon")

    private suspend fun replaceRoom(room: Room) = lock.withLock {
        val current = mutableState.value
        val rooms = current.rooms.map { existing ->
            if (existing.roomId != room.roomId) existing else mergeRoomSnapshots(existing, room)
        }
        mutableState.value = current.copy(rooms = rooms)
        saveRoomsLocked()
    }

    /**
     * Background history synchronization and heartbeat work may finish using a
     * snapshot captured before a locally-sent message was appended. Replacing
     * the whole room with that stale snapshot made the new message disappear
     * from the UI. Room events are immutable and identified by eventId, so a
     * union is safe; moderation represents deletion with a tombstone flag.
     */
    private fun mergeRoomSnapshots(existing: Room, incoming: Room): Room {
        val messagesById = linkedMapOf<String, ChatMessage>()
        existing.messages.forEach { message -> messagesById[message.eventId] = message }
        incoming.messages.forEach { message ->
            val previous = messagesById[message.eventId]
            messagesById[message.eventId] = if (previous == null) message else message.copy(
                deleted = previous.deleted || message.deleted,
                verified = previous.verified || message.verified,
                pending = previous.pending && message.pending,
                wireJson = message.wireJson.ifBlank { previous.wireJson },
            )
        }

        val mergedMessages = messagesById.values
            .sortedWith(compareBy<ChatMessage> { it.createdAt }.thenBy { it.eventId })
            .takeLast(1_000)

        return incoming.copy(
            ownedStoreGeneration = maxOf(existing.ownedStoreGeneration, incoming.ownedStoreGeneration),
            manifestGeneration = maxOf(existing.manifestGeneration, incoming.manifestGeneration),
            latestPage = maxOf(existing.latestPage, incoming.latestPage),
            ownerLastSeen = maxOf(existing.ownerLastSeen, incoming.ownerLastSeen),
            // Reachability suspension is strictly local state. Background
            // work may finish with an old Room snapshot after the user has
            // pressed Retry; never let that stale snapshot re-pause the room.
            suspended = existing.suspended,
            reachabilityFailures = existing.reachabilityFailures,
            lastReachabilityError = existing.lastReachabilityError,
            messages = mergedMessages,
            deletedMessageIds = (existing.deletedMessageIds + incoming.deletedMessageIds).distinct(),
        )
    }

    private fun saveRoomsLocked() = repository.saveRooms(mutableState.value.rooms)

    private fun setStatus(connection: ConnectionState, text: String) {
        appendOperation(text)
        mutableState.update { it.copy(connection = connection, status = text, authorizationPending = connection == ConnectionState.Authorizing) }
    }

    private fun beginOperation(name: String) {
        appendOperation("Started: $name")
        mutableState.update { it.copy(busyOperation = name) }
    }

    private fun endOperation(name: String, error: Throwable? = null) {
        appendOperation(if (error == null) "Completed: $name" else "Failed: $name — ${error.message}")
        mutableState.update { it.copy(busyOperation = null) }
    }

    private fun appendOperation(text: String) {
        val line = "${java.time.LocalDateTime.now()}  $text"
        mutableState.update { current -> current.copy(operationLog = (current.operationLog + line).takeLast(1000)) }
    }

    fun copyableOperationLog(): String = mutableState.value.operationLog.takeLast(1000).joinToString("\n")


    private fun addSystemMessageToSelected(text: String) = scope.launch {
        val room = selectedRoom() ?: return@launch
        replaceRoom(room.copy(messages = room.messages + systemMessage(room.roomId, text)))
    }

    private fun systemMessage(roomId: String, text: String): ChatMessage = ChatMessage(
        eventId = Encoding.randomHex(12),
        roomId = roomId,
        senderName = "Room",
        text = text,
        createdAt = now(),
        verified = true,
        system = true,
    )

    private fun safeStoreName(name: String, id: String, replica: Boolean): String {
        val prefix = if (replica) "replica-" else "room-"
        val cleaned = name.lowercase(Locale.ROOT).mapNotNull { character ->
            when {
                character.isLetterOrDigit() -> character
                character == ' ' || character == '-' || character == '_' -> '-'
                else -> null
            }
        }.joinToString("").take(38)
        return "$prefix$cleaned-${id.take(8)}"
    }

    private fun now(): Long = System.currentTimeMillis() / 1_000

    private fun Job.reportErrors(operation: String? = null): Job = also { job ->
        job.invokeOnCompletion { error ->
            if (operation != null) endOperation(operation, error)
            if (error != null && error !is kotlinx.coroutines.CancellationException) {
                setStatus(ConnectionState.Error, error.message ?: "Operation failed")
            }
        }
    }

    private fun shortIdentity(value: String): String = when {
        value.length <= 18 -> value
        else -> value.take(10) + "…" + value.takeLast(6)
    }

    companion object {
        const val APP_ID = "veilknit.rooms"
        const val APP_NAME = "VeilKnit Rooms Android"
    }
}

private fun JSONArray.stringList(): List<String> = buildList {
    for (index in 0 until length()) add(optString(index))
}
