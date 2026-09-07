package com.veilknit.rooms

import android.app.Application
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import androidx.lifecycle.AndroidViewModel
import com.veilknit.rooms.data.Role
import com.veilknit.rooms.data.RoomEngine

class RoomsViewModel(application: Application) : AndroidViewModel(application) {
    val engine = RoomEngine(application)
    val state = engine.state

    init {
        engine.start()
    }

    fun submit(text: String) = engine.submitText(text)
    fun createRoom(name: String) = engine.createRoom(name)
    fun joinRoom(code: String) = engine.joinRoom(code)
    fun selectRoom(index: Int) = engine.selectRoom(index)
    fun removeRoom() = engine.removeSelectedRoom()
    fun retryRoom() = engine.retrySelectedRoom()
    fun reconnect(reset: Boolean = false) = engine.connect(reset)
    fun syncRoom() = engine.syncSelectedRoom()
    fun toggleReplica() = engine.toggleReplica()
    fun changeRole(subject: String, role: Role) = engine.changeMemberRole(subject, role)
    fun toggleBan(subject: String) = engine.toggleMemberBan(subject)
    fun deleteMessage(eventId: String) = engine.deleteMessage(eventId)
    fun setPhrases(phrases: List<String>) = engine.setBannedPhrases(phrases)
    fun refreshReputation(subject: String) = engine.refreshMemberReputation(subject)
    fun inviteCode(): String = engine.selectedInviteCode()
    fun copyOperationLog() {
        val clipboard = getApplication<Application>().getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        clipboard.setPrimaryClip(ClipData.newPlainText("VeilKnit Rooms log", engine.copyableOperationLog()))
    }

    override fun onCleared() {
        engine.stop()
        super.onCleared()
    }
}
