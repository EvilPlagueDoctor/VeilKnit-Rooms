package com.veilknit.rooms.protocol

import com.veilknit.rooms.daemon.VeilKnitClient
import com.veilknit.rooms.data.Member
import com.veilknit.rooms.data.Role
import com.veilknit.rooms.data.Room
import org.json.JSONArray
import org.json.JSONObject

private const val ROOM_PROTOCOL = "veilknit.rooms"
private const val INVITE_PREFIX = "VKROOM1:"

data class DecodedEnvelope(
    val kind: String,
    val roomId: String,
    val eventId: String,
    val senderMainDht: String,
    val senderName: String,
    val senderPublicKey: String,
    val createdAt: Long,
    val body: JSONObject,
    val wireJson: String,
)

data class RoomInvite(
    val roomId: String,
    val roomName: String,
    val ownerMainDht: String,
    val ownerPublicKey: String,
    val recordKey: String,
    val accessSecretHex: String,
    val authorityEpoch: Long,
)

private fun signingDomain(kind: String): String = "veilknit/rooms/$kind/v1"

private fun signaturePayloadV2(envelope: JSONObject): ByteArray = buildString {
    append("VKROOMSIG2\n")
    fun text(name: String) {
        append(name)
        append('=')
        append(Encoding.base64Url(envelope.getString(name).toByteArray(Charsets.UTF_8)))
        append('\n')
    }
    fun number(name: String) {
        append(name)
        append('=')
        append(envelope.getLong(name))
        append('\n')
    }
    text("protocol")
    number("version")
    text("kind")
    text("room_id")
    text("event_id")
    text("sender_main_dht")
    text("sender_name")
    text("sender_public_key")
    number("authority_epoch")
    number("created_at")
    text("nonce_hex")
    text("cipher_base64")
    text("tag_hex")
}.toByteArray(Charsets.UTF_8)

private fun stripSignatureMember(wireJson: String): String? {
    val marker = "\"signature_hex\""
    val keyPosition = wireJson.indexOf(marker)
    if (keyPosition < 0) return null
    val colon = wireJson.indexOf(':', keyPosition + marker.length)
    if (colon < 0) return null
    val valueStart = wireJson.indexOf('"', colon + 1)
    if (valueStart < 0) return null
    var escaped = false
    var valueEnd = valueStart + 1
    while (valueEnd < wireJson.length) {
        val character = wireJson[valueEnd]
        if (escaped) {
            escaped = false
        } else if (character == '\\') {
            escaped = true
        } else if (character == '"') {
            valueEnd += 1
            break
        }
        valueEnd += 1
    }
    if (valueEnd > wireJson.length) return null
    var eraseStart = keyPosition
    var eraseEnd = valueEnd
    if (eraseStart > 0 && wireJson[eraseStart - 1] == ',') eraseStart -= 1
    else if (eraseEnd < wireJson.length && wireJson[eraseEnd] == ',') eraseEnd += 1
    return wireJson.removeRange(eraseStart, eraseEnd)
}

suspend fun signedEnvelope(
    client: VeilKnitClient,
    room: Room,
    kind: String,
    senderMainDht: String,
    senderName: String,
    senderPublicKey: String,
    body: JSONObject,
    eventId: String = Encoding.randomHex(16),
): String {
    val createdAt = System.currentTimeMillis() / 1_000
    val associated = "${room.roomId}:$kind:$eventId"
    val key = Encoding.deriveRoomKey(room.accessSecretHex, room.roomId, room.authorityEpoch)
    val encrypted = Encoding.encryptAesGcm(CanonicalJson.stringify(body).toByteArray(), key, associated)
    val envelope = JSONObject()
        .put("protocol", ROOM_PROTOCOL)
        .put("version", 2)
        .put("kind", kind)
        .put("room_id", room.roomId)
        .put("event_id", eventId)
        .put("sender_main_dht", senderMainDht)
        .put("sender_name", senderName)
        .put("sender_public_key", senderPublicKey)
        .put("authority_epoch", room.authorityEpoch)
        .put("created_at", createdAt)
        .put("nonce_hex", encrypted.nonceHex)
        .put("cipher_base64", encrypted.cipherBase64)
        .put("tag_hex", encrypted.tagHex)

    var signature = client.sign(signingDomain(kind), signaturePayloadV2(envelope))
    if (signature.publicKeyHex != envelope.getString("sender_public_key")) {
        // The daemon's returned key is the key that actually signed. Re-sign
        // once after updating the envelope so a migrated/rotated app key can
        // never produce a self-contradictory envelope.
        envelope.put("sender_public_key", signature.publicKeyHex)
        signature = client.sign(signingDomain(kind), signaturePayloadV2(envelope))
        require(signature.publicKeyHex == envelope.getString("sender_public_key")) {
            "Daemon application signing key changed while creating an envelope"
        }
    }
    envelope.put("signature_hex", signature.signatureHex)
    return CanonicalJson.stringify(envelope)
}

suspend fun decodeEnvelope(client: VeilKnitClient, room: Room, wireJson: String): DecodedEnvelope {
    val parsed = JSONObject(wireJson)
    require(parsed.getString("protocol") == ROOM_PROTOCOL) { "Not a VeilKnit Rooms envelope" }
    val version = parsed.getInt("version")
    require(version == 1 || version == 2) { "Unsupported room protocol version" }
    require(parsed.getString("room_id") == room.roomId) { "Room ID mismatch" }
    val kind = parsed.getString("kind")
    val signature = parsed.getString("signature_hex")
    val unsigned = parsed.copyWithout("signature_hex")
    val publicKey = parsed.getString("sender_public_key")
    val verified = if (version == 2) {
        client.verify(publicKey, signingDomain(kind), signaturePayloadV2(unsigned), signature)
    } else {
        val canonicalValid = client.verify(
            publicKey,
            signingDomain(kind),
            CanonicalJson.stringify(unsigned).toByteArray(Charsets.UTF_8),
            signature,
        )
        canonicalValid || stripSignatureMember(wireJson)?.let { exact ->
            client.verify(publicKey, signingDomain(kind), exact.toByteArray(Charsets.UTF_8), signature)
        } == true
    }
    require(verified) { "Invalid room envelope signature (protocol v$version)" }
    val eventId = parsed.getString("event_id")
    val associated = "${room.roomId}:$kind:$eventId"
    val epoch = parsed.getLong("authority_epoch")
    val key = Encoding.deriveRoomKey(room.accessSecretHex, room.roomId, epoch)
    val plaintext = Encoding.decryptAesGcm(
        Encoding.CipherText(
            parsed.getString("nonce_hex"),
            parsed.getString("cipher_base64"),
            parsed.getString("tag_hex"),
        ),
        key,
        associated,
    )
    return DecodedEnvelope(
        kind = kind,
        roomId = room.roomId,
        eventId = eventId,
        senderMainDht = parsed.getString("sender_main_dht"),
        senderName = parsed.getString("sender_name"),
        senderPublicKey = publicKey,
        createdAt = parsed.getLong("created_at"),
        body = JSONObject(plaintext.toString(Charsets.UTF_8)),
        wireJson = wireJson,
    )
}

fun makeInviteCode(room: Room): String {
    val invite = JSONObject()
        .put("v", 1)
        .put("room_id", room.roomId)
        .put("room_name", room.name)
        .put("owner_main_dht", room.ownerMainDht)
        .put("owner_public_key", room.ownerSigningKey)
        .put("record_key", room.canonicalRecordKey)
        .put("access_secret_hex", room.accessSecretHex)
        .put("authority_epoch", room.authorityEpoch)
    return INVITE_PREFIX + Encoding.base64Url(CanonicalJson.stringify(invite).toByteArray())
}

fun parseInviteCode(original: String): RoomInvite {
    val code = original.trim()
    require(code.startsWith(INVITE_PREFIX)) { "Invite code does not begin with VKROOM1:" }
    val value = JSONObject(Encoding.unbase64Url(code.removePrefix(INVITE_PREFIX)).toString(Charsets.UTF_8))
    require(value.getInt("v") == 1) { "Unsupported invite version" }
    return RoomInvite(
        roomId = value.getString("room_id"),
        roomName = value.getString("room_name"),
        ownerMainDht = value.getString("owner_main_dht"),
        ownerPublicKey = value.getString("owner_public_key"),
        recordKey = value.getString("record_key"),
        accessSecretHex = value.getString("access_secret_hex"),
        authorityEpoch = value.optLong("authority_epoch", 1),
    )
}

fun roomManifestBody(room: Room): JSONObject {
    val members = JSONArray()
    room.members.values.forEach { member -> members.put(memberManifest(member)) }
    val phrases = JSONArray().also { array -> room.bannedPhrases.forEach(array::put) }
    val deleted = JSONArray().also { array -> room.deletedMessageIds.forEach(array::put) }
    return JSONObject()
        .put("schema", "veilknit.rooms.manifest.v1")
        .put("room_id", room.roomId)
        .put("name", room.name)
        .put("owner_main_dht", room.ownerMainDht)
        .put("owner_signing_key", room.ownerSigningKey)
        .put("authority_epoch", room.authorityEpoch)
        .put("manifest_generation", room.manifestGeneration)
        .put("latest_page", room.latestPage)
        .put("messages_per_page", room.messagesPerPage)
        .put("updated_at", System.currentTimeMillis() / 1_000)
        .put("members", members)
        .put("banned_phrases", phrases)
        .put("deleted_message_ids", deleted)
}

private fun memberManifest(member: Member): JSONObject = JSONObject()
    .put("main_dht", member.mainDht)
    .put("display_name", member.displayName)
    .put("signing_key", member.signingKey)
    .put("role", member.role.wire)
    .put("banned", member.banned)
    .put("replica", member.replica)
    .put("replica_record_key", member.replicaRecordKey)
    .put("max_helpers", member.maxHelpers)
