package com.veilknit.rooms.data

import org.json.JSONArray
import org.json.JSONObject

enum class ConnectionState { Disconnected, Connecting, Authorizing, Connected, Error }
enum class Role(val wire: String, val label: String) {
    Owner("owner", "Owner"),
    Moderator("moderator", "Moderator"),
    Helper("helper", "Helper"),
    Member("member", "Member");

    companion object {
        fun fromWire(value: String): Role = entries.firstOrNull { it.wire == value } ?: Member
    }
}

data class Member(
    val mainDht: String,
    val displayName: String = "Unknown",
    val signingKey: String = "",
    val role: Role = Role.Member,
    val online: Boolean = false,
    val banned: Boolean = false,
    val replica: Boolean = false,
    val lastSeen: Long = 0,
    val maxHelpers: Int = 0,
    val replicaRecordKey: String = "",
    val reputationClass: String = "",
    val reputationConfidence: Int = 0,
)

data class ChatMessage(
    val eventId: String,
    val roomId: String,
    val senderMainDht: String = "",
    val senderName: String = "",
    val senderSigningKey: String = "",
    val text: String,
    val wireJson: String = "",
    val createdAt: Long = 0,
    val verified: Boolean = false,
    val pending: Boolean = false,
    val deleted: Boolean = false,
    val system: Boolean = false,
    val recovery: Boolean = false,
)

data class ReplicaInfo(
    val mainDht: String,
    val recordKey: String,
    val generation: Long = 0,
    val lastSeen: Long = 0,
)

data class Room(
    val roomId: String,
    val name: String,
    val ownerMainDht: String,
    val ownerSigningKey: String,
    val accessSecretHex: String,
    val canonicalRecordKey: String = "",
    val ownedStoreId: String = "",
    val ownedStoreSubkeys: Int = 0,
    val ownedStoreValueLimit: Int = 0,
    val ownedStoreGeneration: Long = 0,
    val authorityEpoch: Long = 1,
    val manifestGeneration: Long = 0,
    val latestPage: Int = 1,
    val messagesPerPage: Int = 4,
    val createdAt: Long = 0,
    val ownerLastSeen: Long = 0,
    val localReplica: Boolean = false,
    val joined: Boolean = true,
    val suspended: Boolean = false,
    val reachabilityFailures: Int = 0,
    val lastReachabilityError: String = "",
    val members: Map<String, Member> = emptyMap(),
    val replicas: List<ReplicaInfo> = emptyList(),
    val messages: List<ChatMessage> = emptyList(),
    val bannedPhrases: List<String> = emptyList(),
    val deletedMessageIds: List<String> = emptyList(),
)

data class RoomsUiState(
    val connection: ConnectionState = ConnectionState.Disconnected,
    val status: String = "Not connected",
    val username: String = "",
    val mainDht: String = "",
    val signingKey: String = "",
    val rooms: List<Room> = emptyList(),
    val selectedRoomIndex: Int = -1,
    val demoMode: Boolean = false,
    val authorizationPending: Boolean = false,
    val drawerOpen: Boolean = false,
    val operationLog: List<String> = emptyList(),
    val busyOperation: String? = null,
) {
    val selectedRoom: Room?
        get() = rooms.getOrNull(selectedRoomIndex)
}

fun Member.toJson(): JSONObject = JSONObject()
    .put("main_dht", mainDht)
    .put("display_name", displayName)
    .put("signing_key", signingKey)
    .put("role", role.wire)
    .put("online", online)
    .put("banned", banned)
    .put("replica", replica)
    .put("last_seen", lastSeen)
    .put("max_helpers", maxHelpers)
    .put("replica_record_key", replicaRecordKey)
    .put("reputation_class", reputationClass)
    .put("reputation_confidence", reputationConfidence)

fun memberFromJson(value: JSONObject): Member = Member(
    mainDht = value.optString("main_dht"),
    displayName = value.optString("display_name", "Unknown"),
    signingKey = value.optString("signing_key"),
    role = Role.fromWire(value.optString("role", "member")),
    online = value.optBoolean("online"),
    banned = value.optBoolean("banned"),
    replica = value.optBoolean("replica"),
    lastSeen = value.optLong("last_seen"),
    maxHelpers = value.optInt("max_helpers"),
    replicaRecordKey = value.optString("replica_record_key"),
    reputationClass = value.optString("reputation_class"),
    reputationConfidence = value.optInt("reputation_confidence"),
)

fun ChatMessage.toJson(): JSONObject = JSONObject()
    .put("event_id", eventId)
    .put("room_id", roomId)
    .put("sender_main_dht", senderMainDht)
    .put("sender_name", senderName)
    .put("sender_signing_key", senderSigningKey)
    .put("text", text)
    .put("wire_json", wireJson)
    .put("created_at", createdAt)
    .put("verified", verified)
    .put("pending", pending)
    .put("deleted", deleted)
    .put("system", system)
    .put("recovery", recovery)

fun chatMessageFromJson(value: JSONObject): ChatMessage = ChatMessage(
    eventId = value.optString("event_id"),
    roomId = value.optString("room_id"),
    senderMainDht = value.optString("sender_main_dht"),
    senderName = value.optString("sender_name"),
    senderSigningKey = value.optString("sender_signing_key"),
    text = value.optString("text"),
    wireJson = value.optString("wire_json"),
    createdAt = value.optLong("created_at"),
    verified = value.optBoolean("verified"),
    pending = value.optBoolean("pending"),
    deleted = value.optBoolean("deleted"),
    system = value.optBoolean("system"),
    recovery = value.optBoolean("recovery"),
)

fun Room.toJson(): JSONObject {
    val memberArray = JSONArray()
    members.values.forEach { memberArray.put(it.toJson()) }
    val replicaArray = JSONArray()
    replicas.forEach { replica ->
        replicaArray.put(
            JSONObject()
                .put("main_dht", replica.mainDht)
                .put("record_key", replica.recordKey)
                .put("generation", replica.generation)
                .put("last_seen", replica.lastSeen),
        )
    }
    val messageArray = JSONArray()
    messages.takeLast(1_000).forEach { messageArray.put(it.toJson()) }
    val phraseArray = JSONArray()
    bannedPhrases.forEach { phraseArray.put(it) }
    val deletedArray = JSONArray()
    deletedMessageIds.forEach { deletedArray.put(it) }
    return JSONObject()
        .put("room_id", roomId)
        .put("name", name)
        .put("owner_main_dht", ownerMainDht)
        .put("owner_signing_key", ownerSigningKey)
        .put("access_secret_hex", accessSecretHex)
        .put("canonical_record_key", canonicalRecordKey)
        .put("owned_store_id", ownedStoreId)
        .put("owned_store_subkeys", ownedStoreSubkeys)
        .put("owned_store_value_limit", ownedStoreValueLimit)
        .put("owned_store_generation", ownedStoreGeneration)
        .put("authority_epoch", authorityEpoch)
        .put("manifest_generation", manifestGeneration)
        .put("latest_page", latestPage)
        .put("messages_per_page", messagesPerPage)
        .put("created_at", createdAt)
        .put("owner_last_seen", ownerLastSeen)
        .put("local_replica", localReplica)
        .put("joined", joined)
    .put("suspended", suspended)
    .put("reachability_failures", reachabilityFailures)
    .put("last_reachability_error", lastReachabilityError)
        .put("members", memberArray)
        .put("replicas", replicaArray)
        .put("messages", messageArray)
        .put("banned_phrases", phraseArray)
        .put("deleted_message_ids", deletedArray)
}

fun roomFromJson(value: JSONObject): Room {
    val members = linkedMapOf<String, Member>()
    value.optJSONArray("members")?.let { array ->
        for (index in 0 until array.length()) {
            val member = memberFromJson(array.getJSONObject(index))
            members[member.mainDht] = member
        }
    }
    val replicas = buildList {
        value.optJSONArray("replicas")?.let { array ->
            for (index in 0 until array.length()) {
                val item = array.getJSONObject(index)
                add(
                    ReplicaInfo(
                        mainDht = item.optString("main_dht"),
                        recordKey = item.optString("record_key"),
                        generation = item.optLong("generation"),
                        lastSeen = item.optLong("last_seen"),
                    ),
                )
            }
        }
    }
    val messages = buildList {
        value.optJSONArray("messages")?.let { array ->
            for (index in 0 until array.length()) add(chatMessageFromJson(array.getJSONObject(index)))
        }
    }
    fun strings(name: String): List<String> = buildList {
        value.optJSONArray(name)?.let { array ->
            for (index in 0 until array.length()) add(array.optString(index))
        }
    }
    return Room(
        roomId = value.getString("room_id"),
        name = value.optString("name", "Unnamed room"),
        ownerMainDht = value.optString("owner_main_dht"),
        ownerSigningKey = value.optString("owner_signing_key"),
        accessSecretHex = value.optString("access_secret_hex"),
        canonicalRecordKey = value.optString("canonical_record_key"),
        ownedStoreId = value.optString("owned_store_id"),
        ownedStoreSubkeys = value.optInt("owned_store_subkeys"),
        ownedStoreValueLimit = value.optInt("owned_store_value_limit"),
        ownedStoreGeneration = value.optLong("owned_store_generation"),
        authorityEpoch = value.optLong("authority_epoch", 1),
        manifestGeneration = value.optLong("manifest_generation"),
        latestPage = value.optInt("latest_page", 1),
        messagesPerPage = value.optInt("messages_per_page", 4),
        createdAt = value.optLong("created_at"),
        ownerLastSeen = value.optLong("owner_last_seen"),
        localReplica = value.optBoolean("local_replica"),
        joined = value.optBoolean("joined", true),
        suspended = value.optBoolean("suspended"),
        reachabilityFailures = value.optInt("reachability_failures"),
        lastReachabilityError = value.optString("last_reachability_error"),
        members = members,
        replicas = replicas,
        messages = messages,
        bannedPhrases = strings("banned_phrases"),
        deletedMessageIds = strings("deleted_message_ids"),
    )
}
