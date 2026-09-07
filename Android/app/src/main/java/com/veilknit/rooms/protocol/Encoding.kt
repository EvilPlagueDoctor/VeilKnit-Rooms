package com.veilknit.rooms.protocol

import java.util.Base64
import org.json.JSONArray
import org.json.JSONObject
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest
import java.security.SecureRandom
import javax.crypto.Cipher
import javax.crypto.Mac
import javax.crypto.spec.GCMParameterSpec
import javax.crypto.spec.SecretKeySpec

object Encoding {
    private val random = SecureRandom()

    fun randomBytes(size: Int): ByteArray = ByteArray(size).also(random::nextBytes)
    fun randomHex(size: Int): String = hex(randomBytes(size))

    fun hex(bytes: ByteArray): String = bytes.joinToString("") { "%02x".format(it.toInt() and 0xff) }
    fun unhex(value: String): ByteArray {
        require(value.length % 2 == 0) { "Hex string has an odd length" }
        return ByteArray(value.length / 2) { index ->
            value.substring(index * 2, index * 2 + 2).toInt(16).toByte()
        }
    }

    fun base64(bytes: ByteArray): String = Base64.getEncoder().encodeToString(bytes)
    fun unbase64(value: String): ByteArray = Base64.getDecoder().decode(value)
    fun base64Url(bytes: ByteArray): String = Base64.getUrlEncoder().withoutPadding().encodeToString(bytes)
    fun unbase64Url(value: String): ByteArray = Base64.getUrlDecoder().decode(value)

    fun sha256(bytes: ByteArray): ByteArray = MessageDigest.getInstance("SHA-256").digest(bytes)
    fun hmacSha256(key: ByteArray, bytes: ByteArray): ByteArray = Mac.getInstance("HmacSHA256").run {
        init(SecretKeySpec(key, "HmacSHA256"))
        doFinal(bytes)
    }

    fun littleEndianInt(value: Int): ByteArray = ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN).putInt(value).array()
    fun littleEndianLong(value: Long): ByteArray = ByteBuffer.allocate(8).order(ByteOrder.LITTLE_ENDIAN).putLong(value).array()

    fun deriveRoomKey(secretHex: String, roomId: String, epoch: Long): ByteArray {
        val context = "$roomId:$epoch:veilknit.rooms.key.v1".toByteArray()
        return sha256(unhex(secretHex) + context)
    }

    data class CipherText(val nonceHex: String, val cipherBase64: String, val tagHex: String)

    fun encryptAesGcm(plaintext: ByteArray, key: ByteArray, associatedData: String): CipherText {
        require(key.size == 32) { "Room key must be 32 bytes" }
        val nonce = randomBytes(12)
        val cipher = Cipher.getInstance("AES/GCM/NoPadding")
        cipher.init(Cipher.ENCRYPT_MODE, SecretKeySpec(key, "AES"), GCMParameterSpec(128, nonce))
        cipher.updateAAD(associatedData.toByteArray())
        val combined = cipher.doFinal(plaintext)
        val encrypted = combined.copyOfRange(0, combined.size - 16)
        val tag = combined.copyOfRange(combined.size - 16, combined.size)
        return CipherText(hex(nonce), base64Url(encrypted), hex(tag))
    }

    fun decryptAesGcm(value: CipherText, key: ByteArray, associatedData: String): ByteArray {
        val nonce = unhex(value.nonceHex)
        val combined = unbase64Url(value.cipherBase64) + unhex(value.tagHex)
        val cipher = Cipher.getInstance("AES/GCM/NoPadding")
        cipher.init(Cipher.DECRYPT_MODE, SecretKeySpec(key, "AES"), GCMParameterSpec(128, nonce))
        cipher.updateAAD(associatedData.toByteArray())
        return cipher.doFinal(combined)
    }
}

/** Canonical sorted-key JSON used by the desktop client for signatures. */
object CanonicalJson {
    fun stringify(value: Any?): String = when (value) {
        null, JSONObject.NULL -> "null"
        is JSONObject -> value.keys().asSequence().toList().sorted().joinToString(prefix = "{", postfix = "}") { key ->
            "${quote(key)}:${stringify(value.opt(key))}"
        }
        is JSONArray -> (0 until value.length()).joinToString(prefix = "[", postfix = "]") { index -> stringify(value.opt(index)) }
        is String -> quote(value)
        is Boolean -> if (value) "true" else "false"
        is Number -> number(value)
        else -> quote(value.toString())
    }

    private fun number(value: Number): String {
        val text = value.toString()
        require(text != "NaN" && text != "Infinity" && text != "-Infinity") { "Non-finite JSON number" }
        return text
    }

    private fun quote(value: String): String = buildString {
        append('"')
        value.forEach { character ->
            when (character) {
                '"' -> append("\\\"")
                '\\' -> append("\\\\")
                '\b' -> append("\\b")
                '\u000c' -> append("\\f")
                '\n' -> append("\\n")
                '\r' -> append("\\r")
                '\t' -> append("\\t")
                else -> if (character.code < 0x20) append("\\u%04x".format(character.code)) else append(character)
            }
        }
        append('"')
    }
}

fun JSONObject.copyWithout(key: String): JSONObject = JSONObject(toString()).apply { remove(key) }
