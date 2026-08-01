package com.veilknit.rooms.daemon

import com.veilknit.rooms.protocol.Encoding
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.Flow
import org.json.JSONArray
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.io.File
import java.util.concurrent.atomic.AtomicLong

private const val PROTOCOL_VERSION = 3
private const val AUTH_DOMAIN = "veilknit/app-auth/v2"

val StandardCapabilities = listOf(
    "SendMessages",
    "ReceiveMessages",
    "ManageOwnStorage",
    "ReadOwnStorage",
    "ReadPublicProfiles",
    "SubscribeNetworkStatus",
    "SubmitReputation",
    "RequestAppScopedRestriction",
    "InspectOwnReputationSubmissions",
    "SignAppData",
)

data class Credential(
    val protocolVersion: Int = PROTOCOL_VERSION,
    val endpoint: String = "android-binder",
    val appId: String,
    val displayName: String,
    val secretHex: String,
    val credentialGeneration: Long,
) {
    fun save(file: File) {
        file.parentFile?.mkdirs()
        file.writeText(
            JSONObject()
                .put("protocol_version", protocolVersion)
                .put("endpoint", endpoint)
                .put("app_id", appId)
                .put("display_name", displayName)
                .put("secret_hex", secretHex)
                .put("credential_generation", credentialGeneration)
                .toString(2),
        )
    }

    companion object {
        fun load(file: File): Credential {
            val value = JSONObject(file.readText())
            val storedProtocol = value.optInt("protocol_version", 1)
            require(storedProtocol in 1..PROTOCOL_VERSION) {
                "Credential protocol $storedProtocol is newer than this Rooms build"
            }
            return Credential(
                // v1/v2 files contain the same app secret and generation. The
                // new client upgrades their wire protocol/auth domain in place.
                protocolVersion = PROTOCOL_VERSION,
                endpoint = value.optString("endpoint", "android-binder"),
                appId = value.getString("app_id"),
                displayName = value.optString("display_name", "VeilKnit Rooms"),
                secretHex = value.getString("secret_hex"),
                credentialGeneration = value.getLong("credential_generation"),
            )
        }
    }
}

data class Session(
    val appId: String,
    val sessionId: String,
    val tokenHex: String,
    val authenticatedAt: Long,
    val expiresAt: Long,
    val capabilities: List<String>,
)

data class LocalIdentity(val username: String, val mainDht: String)
data class SigningIdentity(val publicKeyHex: String, val keyGeneration: Long)
data class SignatureResult(val signatureHex: String, val publicKeyHex: String)
data class StoreDescriptor(
    val storeId: String,
    val recordKey: String,
    val generation: Long,
    val name: String,
    val subkeyCount: Int,
    val maxValueBytes: Int,
)
data class StoreValue(val location: Int, val bytes: ByteArray?, val isNull: Boolean, val error: String?)
data class StoreRead(val descriptor: StoreDescriptor?, val recordKey: String?, val values: List<StoreValue>)

class VeilKnitClient private constructor(
    private val connector: DaemonConnector,
    val session: Session,
) {
    private val nextRequestId = AtomicLong(3)

    suspend fun identity(): LocalIdentity {
        val result = request("get_identity")
        expectType(result, "identity")
        return LocalIdentity(result.getString("username"), result.getString("main_dht"))
    }

    suspend fun signingIdentity(): SigningIdentity {
        val result = request("get_app_signing_identity")
        expectType(result, "app_signing_identity")
        val identity = result.getJSONObject("identity")
        return SigningIdentity(identity.getString("public_key_hex"), identity.getLong("key_generation"))
    }

    suspend fun sign(domain: String, payload: ByteArray): SignatureResult {
        val result = request(
            "sign_app_payload",
            JSONObject().put("domain", domain).put("payload_base64", Encoding.base64(payload)),
        )
        expectType(result, "app_payload_signed")
        val signature = result.getJSONObject("signature")
        return SignatureResult(signature.getString("signature_hex"), signature.getString("public_key_hex"))
    }

    suspend fun verify(publicKeyHex: String, domain: String, payload: ByteArray, signatureHex: String): Boolean {
        val result = request(
            "verify_app_signature",
            JSONObject()
                .put("public_key_hex", publicKeyHex)
                .put("domain", domain)
                .put("payload_base64", Encoding.base64(payload))
                .put("signature_hex", signatureHex),
        )
        expectType(result, "app_signature_verified")
        return result.getBoolean("valid")
    }

    suspend fun sendMessage(recipientMainDht: String, payload: ByteArray): String {
        val result = request(
            "send_message",
            JSONObject()
                .put("recipient_main_dht", recipientMainDht)
                .put("payload_base64", Encoding.base64(payload))
                .put("await_response", false),
        )
        expectType(result, "message_queued")
        return result.getString("message_id_hex")
    }

    suspend fun triggerMessageRetrieval() {
        expectType(request("trigger_message_retrieval"), "message_retrieval_scheduled")
    }

    fun subscribeMessages(): Flow<JSONObject> {
        val envelope = requestEnvelope(
            action = "subscribe_messages",
            body = JSONObject().put("session_token", session.tokenHex),
        )
        return connector.subscribe(envelope)
    }

    suspend fun listStores(): List<StoreDescriptor> {
        val result = request("list_app_stores")
        expectType(result, "app_stores")
        val stores = result.getJSONArray("stores")
        return buildList {
            for (index in 0 until stores.length()) add(parseStore(stores.getJSONObject(index)))
        }
    }

    suspend fun createStore(name: String, subkeyCount: Int = 64): StoreDescriptor {
        val result = request(
            "create_app_store",
            JSONObject().put("name", name).put("subkey_count", subkeyCount).put("initialize", true),
        )
        expectType(result, "app_store_created")
        return parseStore(result.getJSONObject("store"))
    }

    suspend fun writeStore(
        storeId: String,
        writes: List<Pair<Int, ByteArray>>,
        expectedGeneration: Long? = null,
    ): StoreDescriptor {
        val values = JSONArray()
        writes.forEach { (location, bytes) ->
            values.put(JSONObject().put("location", location).put("value_base64", Encoding.base64(bytes)))
        }
        val body = JSONObject().put("store_id", storeId).put("writes", values)
        expectedGeneration?.let { body.put("expected_generation", it) }
        val result = request("write_app_store", body)
        expectType(result, "app_store_written")
        return parseStore(result.getJSONObject("store"))
    }

    suspend fun readPublicStore(recordKey: String, locations: List<Int>, forceRefresh: Boolean = true): StoreRead {
        val locationArray = JSONArray()
        locations.forEach(locationArray::put)
        val result = request(
            "read_public_store",
            JSONObject()
                .put("record_key", recordKey)
                .put("locations", locationArray)
                .put("force_refresh", forceRefresh),
        )
        expectType(result, "public_store_read")
        return StoreRead(
            descriptor = null,
            recordKey = result.getString("record_key"),
            values = parseStoreValues(result.getJSONArray("values")),
        )
    }

    suspend fun requestRestriction(subject: String, reason: String): Long {
        val result = request(
            "request_app_restriction",
            JSONObject()
                .put("subject_main_dht", subject)
                .put("restriction_action", "restrict")
                .put("reason", reason),
        )
        expectType(result, "app_restriction_requested")
        return result.getLong("decision_id")
    }

    suspend fun reputationView(subject: String): JSONObject {
        val result = request("get_reputation_view", JSONObject().put("subject_main_dht", subject))
        expectType(result, "reputation_view")
        return result.getJSONObject("view")
    }

    private suspend fun request(action: String, body: JSONObject = JSONObject()): JSONObject {
        body.put("session_token", session.tokenHex)
        return connector.transact(requestEnvelope(action, body))
    }

    private fun requestEnvelope(action: String, body: JSONObject): JSONObject {
        val envelope = JSONObject()
            .put("protocol_version", PROTOCOL_VERSION)
            .put("request_id", nextRequestId.getAndIncrement())
            .put("action", action)
        body.keys().forEach { key -> envelope.put(key, body.get(key)) }
        return envelope
    }

    companion object {
        suspend fun register(
            connector: DaemonConnector,
            appId: String,
            displayName: String,
            onPending: suspend () -> Unit,
            timeoutMs: Long = 15 * 60 * 1_000L,
        ): Credential {
            val token = Encoding.randomHex(32)
            val capabilities = JSONArray().also { array -> StandardCapabilities.forEach(array::put) }
            val pending = connector.transact(
                JSONObject()
                    .put("protocol_version", PROTOCOL_VERSION)
                    .put("request_id", 1)
                    .put("action", "request_app_registration")
                    .put("app_id", appId)
                    .put("display_name", displayName)
                    .put("requested_capabilities", capabilities)
                    .put("request_token_hex", token),
            )
            expectType(pending, "app_registration_pending")
            val requestId = pending.getLong("request_id")
            onPending()
            val deadline = System.currentTimeMillis() + timeoutMs
            while (System.currentTimeMillis() < deadline) {
                val result = connector.transact(
                    JSONObject()
                        .put("protocol_version", PROTOCOL_VERSION)
                        .put("request_id", 2)
                        .put("action", "get_app_registration_status")
                        .put("registration_request_id", requestId)
                        .put("request_token_hex", token),
                )
                when (result.getString("type")) {
                    "app_registration_approved" -> return Credential(
                        protocolVersion = result.getInt("protocol_version"),
                        endpoint = "android-binder",
                        appId = result.getString("app_id"),
                        displayName = result.getString("display_name"),
                        secretHex = result.getString("secret_hex"),
                        credentialGeneration = result.getLong("credential_generation"),
                    )
                    "app_registration_rejected" -> error(result.optString("reason", "Authorization rejected"))
                    "app_registration_expired" -> error("Application authorization expired")
                    "app_registration_still_pending" -> delay(750)
                    else -> error("Unexpected registration response: ${result.getString("type")}")
                }
            }
            error("Application authorization timed out")
        }

        suspend fun authenticate(connector: DaemonConnector, credential: Credential): VeilKnitClient {
            require(credential.protocolVersion == PROTOCOL_VERSION) { "Credential protocol mismatch" }
            val capabilities = JSONArray().also { array -> StandardCapabilities.forEach(array::put) }
            val challenge = connector.transact(
                JSONObject()
                    .put("protocol_version", PROTOCOL_VERSION)
                    .put("request_id", 1)
                    .put("action", "begin_authentication")
                    .put("app_id", credential.appId)
                    .put("requested_capabilities", capabilities),
            )
            expectType(challenge, "authentication_challenge")
            val challengeCapabilities = challenge.getJSONArray("requested_capabilities").toStringList()
            val generation = challenge.getLong("credential_generation")
            require(generation == credential.credentialGeneration) { "Credential generation mismatch" }
            val proof = computeAuthProof(
                credential = credential,
                challengeId = challenge.getLong("challenge_id"),
                nonce = Encoding.unhex(challenge.getString("nonce_hex")),
                issuedAt = challenge.getLong("issued_at"),
                expiresAt = challenge.getLong("expires_at"),
                generation = generation,
                capabilities = challengeCapabilities,
            )
            val result = connector.transact(
                JSONObject()
                    .put("protocol_version", PROTOCOL_VERSION)
                    .put("request_id", 2)
                    .put("action", "finish_authentication")
                    .put("app_id", credential.appId)
                    .put("challenge_id", challenge.getLong("challenge_id"))
                    .put("proof_hex", Encoding.hex(proof)),
            )
            expectType(result, "authentication_succeeded")
            return VeilKnitClient(
                connector,
                Session(
                    appId = result.getString("app_id"),
                    sessionId = result.getString("session_id"),
                    tokenHex = result.getString("session_token_hex"),
                    authenticatedAt = result.getLong("authenticated_at"),
                    expiresAt = result.getLong("expires_at"),
                    capabilities = result.getJSONArray("capabilities").toStringList(),
                ),
            )
        }

        private fun computeAuthProof(
            credential: Credential,
            challengeId: Long,
            nonce: ByteArray,
            issuedAt: Long,
            expiresAt: Long,
            generation: Long,
            capabilities: List<String>,
        ): ByteArray {
            require(nonce.size == 32)
            val output = ByteArrayOutputStream()
            output.write(AUTH_DOMAIN.toByteArray())
            output.write(Encoding.littleEndianInt(credential.appId.toByteArray().size))
            output.write(credential.appId.toByteArray())
            output.write(Encoding.littleEndianLong(challengeId))
            output.write(nonce)
            output.write(Encoding.littleEndianLong(issuedAt))
            output.write(Encoding.littleEndianLong(expiresAt))
            output.write(Encoding.littleEndianLong(generation))
            output.write(Encoding.littleEndianInt(capabilities.size))
            capabilities.forEach { capability ->
                output.write(capability.toByteArray())
                output.write(0)
            }
            return Encoding.hmacSha256(Encoding.unhex(credential.secretHex), output.toByteArray())
        }

        private fun parseStore(value: JSONObject): StoreDescriptor = StoreDescriptor(
            storeId = value.getString("store_id"),
            recordKey = value.getString("record_key"),
            generation = value.getLong("generation"),
            name = value.optString("name"),
            subkeyCount = value.optInt("subkey_count"),
            maxValueBytes = value.optInt("max_value_bytes"),
        )

        private fun parseStoreValues(values: JSONArray): List<StoreValue> = buildList {
            for (index in 0 until values.length()) {
                val value = values.getJSONObject(index)
                add(
                    StoreValue(
                        location = value.getInt("location"),
                        bytes = value.optString("value_base64").takeIf(String::isNotEmpty)?.let(Encoding::unbase64),
                        isNull = value.optBoolean("is_null"),
                        error = value.optString("error").takeIf(String::isNotEmpty),
                    ),
                )
            }
        }

        private fun expectType(value: JSONObject, expected: String) {
            val actual = value.optString("type")
            require(actual == expected) { "Expected $expected, received $actual" }
        }
    }
}

private fun JSONArray.toStringList(): List<String> = buildList {
    for (index in 0 until length()) add(getString(index))
}
