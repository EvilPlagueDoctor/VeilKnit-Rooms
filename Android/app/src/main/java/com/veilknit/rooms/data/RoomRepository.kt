package com.veilknit.rooms.data

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

class RoomRepository(context: Context) {
    private val roomFile = File(context.filesDir, "rooms-v1.json")
    val credentialFile = File(context.filesDir, "credential-v1.json")

    fun loadRooms(): List<Room> = runCatching {
        if (!roomFile.isFile) return emptyList()
        val root = JSONObject(roomFile.readText())
        val values = root.optJSONArray("rooms") ?: JSONArray()
        buildList {
            for (index in 0 until values.length()) add(roomFromJson(values.getJSONObject(index)))
        }
    }.getOrDefault(emptyList())

    @Synchronized
    fun saveRooms(rooms: List<Room>) {
        val values = JSONArray()
        rooms.forEach { values.put(it.toJson()) }
        val root = JSONObject().put("version", 1).put("rooms", values)
        val temporary = File(roomFile.parentFile, "${roomFile.name}.tmp")
        temporary.writeText(root.toString())
        if (roomFile.exists() && !roomFile.delete()) error("Could not replace room database")
        if (!temporary.renameTo(roomFile)) error("Could not commit room database")
    }
}
