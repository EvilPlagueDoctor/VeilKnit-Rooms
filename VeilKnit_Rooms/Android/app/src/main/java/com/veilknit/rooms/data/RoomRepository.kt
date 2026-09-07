package com.veilknit.rooms.data

import android.content.Context
import com.veilknit.rooms.daemon.VeilKnitClient
import org.json.JSONArray
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.io.File
import java.security.MessageDigest

private const val PRIVATE_STATE_MANIFEST_KEY = "rooms/state-v1/manifest"
private const val PRIVATE_STATE_CHUNK_BYTES = 192 * 1024

/**
 * Rooms' durable room state lives in the daemon-owned encrypted private app vault.
 *
 * The files in this class are retained only for credentials (which are needed before
 * authentication) and for one-time migration from older plaintext Rooms builds.
 */
class RoomRepository(context: Context) {
    private val filesDir = context.filesDir
    private val legacyRoomFile = File(filesDir, "rooms-v1.json")
    private val legacyCredentialFile = File(filesDir, "credential-v1.json")
    private val profileRoot = File(filesDir, "profiles")

    fun roomFile(profileId: String): File = File(profileDirectory(profileId), "rooms-v1.json")
    fun credentialFile(profileId: String): File = File(profileDirectory(profileId), "credential-v3.json")

    fun legacyCredential(): File = legacyCredentialFile

    fun hasLocalRooms(profileId: String): Boolean = roomFile(profileId).isFile

    fun loadLocalRooms(profileId: String): List<Room> = loadRoomsFrom(roomFile(profileId))

    fun deleteLocalRooms(profileId: String) {
        runCatching { roomFile(profileId).delete() }
    }

    fun deleteLegacyRooms() {
        runCatching { legacyRoomFile.delete() }
    }

    /**
     * Migrate the pre-account-aware room database only after the caller has
     * proven that the legacy credential authenticates against this profile.
     */
    @Synchronized
    fun migrateLegacyRooms(profileId: String) {
        val destination = roomFile(profileId)
        if (destination.exists() || !legacyRoomFile.isFile) return
        destination.parentFile?.mkdirs()
        val temporary = File(destination.parentFile, "${destination.name}.tmp")
        legacyRoomFile.copyTo(temporary, overwrite = true)
        if (!temporary.renameTo(destination)) {
            temporary.delete()
            error("Could not migrate the legacy room database")
        }
    }

    suspend fun loadEncryptedRooms(api: VeilKnitClient): List<Room>? {
        val manifestBytes = api.getPrivateValue(PRIVATE_STATE_MANIFEST_KEY) ?: return null
        val manifest = JSONObject(manifestBytes.toString(Charsets.UTF_8))
        require(manifest.optInt("version") == 1) { "Unsupported encrypted Rooms state version" }
        val generation = manifest.getString("generation")
        val chunkCount = manifest.getInt("chunk_count")
        val byteLength = manifest.getLong("byte_length")
        val expectedSha = manifest.getString("sha256")
        require(generation.isNotBlank() && chunkCount > 0 && byteLength >= 0) {
            "Encrypted Rooms state manifest is incomplete"
        }

        val output = ByteArrayOutputStream(minOf(byteLength, 1024L * 1024L).toInt())
        repeat(chunkCount) { index ->
            val chunk = api.getPrivateValue(privateStateChunkKey(generation, index))
                ?: error("Encrypted Rooms state is missing data chunk $index")
            output.write(chunk)
        }
        val bytes = output.toByteArray()
        require(bytes.size.toLong() == byteLength) { "Encrypted Rooms state length does not match its manifest" }
        require(sha256Hex(bytes) == expectedSha) { "Encrypted Rooms state failed its integrity check" }
        return deserializeRooms(bytes)
    }

    suspend fun saveEncryptedRooms(api: VeilKnitClient, rooms: List<Room>) {
        val previous = api.getPrivateValue(PRIVATE_STATE_MANIFEST_KEY)?.let { bytes ->
            runCatching { JSONObject(bytes.toString(Charsets.UTF_8)) }.getOrNull()
        }
        val bytes = serializeRooms(rooms)
        val generation = randomGeneration()
        val chunkCount = maxOf(1, (bytes.size + PRIVATE_STATE_CHUNK_BYTES - 1) / PRIVATE_STATE_CHUNK_BYTES)
        var written = 0
        try {
            repeat(chunkCount) { index ->
                val start = index * PRIVATE_STATE_CHUNK_BYTES
                val end = minOf(start + PRIVATE_STATE_CHUNK_BYTES, bytes.size)
                val chunk = if (start >= bytes.size) ByteArray(0) else bytes.copyOfRange(start, end)
                api.putPrivateValue(privateStateChunkKey(generation, index), chunk)
                written++
            }
            val manifest = JSONObject()
                .put("version", 1)
                .put("generation", generation)
                .put("chunk_count", chunkCount)
                .put("byte_length", bytes.size)
                .put("sha256", sha256Hex(bytes))
            api.putPrivateValue(PRIVATE_STATE_MANIFEST_KEY, manifest.toString().toByteArray(Charsets.UTF_8))
        } catch (error: Throwable) {
            repeat(written) { index -> runCatching { api.deletePrivateValue(privateStateChunkKey(generation, index)) } }
            throw error
        }

        val previousGeneration = previous?.optString("generation").orEmpty()
        val previousCount = previous?.optInt("chunk_count", 0) ?: 0
        if (previousGeneration.isNotBlank() && previousGeneration != generation && previousCount > 0) {
            repeat(previousCount) { index ->
                runCatching { api.deletePrivateValue(privateStateChunkKey(previousGeneration, index)) }
            }
        }
    }

    private fun serializeRooms(rooms: List<Room>): ByteArray {
        val values = JSONArray()
        rooms.forEach { values.put(it.toJson()) }
        return JSONObject()
            .put("version", 1)
            .put("rooms", values)
            .toString()
            .toByteArray(Charsets.UTF_8)
    }

    private fun deserializeRooms(bytes: ByteArray): List<Room> = runCatching {
        val root = JSONObject(bytes.toString(Charsets.UTF_8))
        val values = root.optJSONArray("rooms") ?: JSONArray()
        buildList {
            for (index in 0 until values.length()) add(roomFromJson(values.getJSONObject(index)))
        }
    }.getOrElse { throw IllegalStateException("Encrypted Rooms state could not be decoded", it) }

    private fun loadRoomsFrom(file: File): List<Room> = runCatching {
        if (!file.isFile) return emptyList()
        val root = JSONObject(file.readText())
        val values = root.optJSONArray("rooms") ?: JSONArray()
        buildList {
            for (index in 0 until values.length()) add(roomFromJson(values.getJSONObject(index)))
        }
    }.getOrDefault(emptyList())

    private fun profileDirectory(profileId: String): File {
        require(profileId.isNotBlank()) { "Daemon profile id is unavailable" }
        val digest = MessageDigest.getInstance("SHA-256")
            .digest(profileId.toByteArray(Charsets.UTF_8))
            .joinToString("") { "%02x".format(it) }
        return File(profileRoot, digest.take(32))
    }

    private fun privateStateChunkKey(generation: String, index: Int): String =
        "rooms/state-v1/$generation/$index"

    private fun randomGeneration(): String {
        val bytes = ByteArray(16)
        java.security.SecureRandom().nextBytes(bytes)
        return bytes.joinToString("") { "%02x".format(it) }
    }

    private fun sha256Hex(bytes: ByteArray): String = MessageDigest.getInstance("SHA-256")
        .digest(bytes)
        .joinToString("") { "%02x".format(it) }
}
