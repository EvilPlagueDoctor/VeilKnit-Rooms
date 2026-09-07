package com.veilknit.rooms.daemon

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.os.IBinder
import android.os.RemoteException
import com.example.veilknit_deamon.ipc.IVeilKnitApi
import com.example.veilknit_deamon.ipc.IVeilKnitStreamCallback
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.callbackFlow
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeout
import org.json.JSONObject
import java.util.concurrent.atomic.AtomicReference

sealed interface BinderConnectionState {
    data object Disconnected : BinderConnectionState
    data object Connecting : BinderConnectionState
    data object Connected : BinderConnectionState
    data class Error(val message: String) : BinderConnectionState
}

class DaemonConnector(private val context: Context) {
    private val service = AtomicReference<IVeilKnitApi?>(null)
    private val mutableState = MutableStateFlow<BinderConnectionState>(BinderConnectionState.Disconnected)
    val state = mutableState.asStateFlow()
    private var connectionDeferred = CompletableDeferred<IVeilKnitApi>()

    private val serviceConnection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, binder: IBinder?) {
            val api = IVeilKnitApi.Stub.asInterface(binder)
            service.set(api)
            mutableState.value = BinderConnectionState.Connected
            if (!connectionDeferred.isCompleted) connectionDeferred.complete(api)
        }

        override fun onServiceDisconnected(name: ComponentName?) {
            service.set(null)
            mutableState.value = BinderConnectionState.Disconnected
            connectionDeferred = CompletableDeferred()
        }

        override fun onBindingDied(name: ComponentName?) {
            service.set(null)
            mutableState.value = BinderConnectionState.Error("Daemon Binder service died")
            connectionDeferred = CompletableDeferred()
        }

        override fun onNullBinding(name: ComponentName?) {
            service.set(null)
            val error = IllegalStateException("Daemon returned a null Binder")
            mutableState.value = BinderConnectionState.Error(error.message.orEmpty())
            if (!connectionDeferred.isCompleted) connectionDeferred.completeExceptionally(error)
        }
    }

    suspend fun connect(timeoutMs: Long = 8_000): IVeilKnitApi {
        service.get()?.let { return it }
        mutableState.value = BinderConnectionState.Connecting
        if (connectionDeferred.isCompleted) connectionDeferred = CompletableDeferred()
        val intent = Intent(DAEMON_ACTION).apply {
            component = ComponentName(DAEMON_PACKAGE, DAEMON_SERVICE)
        }
        val bound = context.bindService(intent, serviceConnection, Context.BIND_AUTO_CREATE)
        if (!bound) {
            val error = IllegalStateException(
                "VeilKnit Daemon is not installed, is not signed with the same certificate, or does not include the Android API bridge.",
            )
            mutableState.value = BinderConnectionState.Error(error.message.orEmpty())
            throw error
        }
        return withTimeout(timeoutMs) { connectionDeferred.await() }
    }

    fun disconnect() {
        runCatching { context.unbindService(serviceConnection) }
        service.set(null)
        mutableState.value = BinderConnectionState.Disconnected
        connectionDeferred = CompletableDeferred()
    }

    suspend fun daemonState(): JSONObject = withContext(Dispatchers.IO) {
        JSONObject(connect().daemonStateJson)
    }

    suspend fun transact(request: JSONObject): JSONObject = withContext(Dispatchers.IO) {
        val raw = connect().transact(request.toString())
        val envelope = JSONObject(raw)
        if (!envelope.optBoolean("ok")) {
            val error = envelope.optJSONObject("error")
            throw DaemonApiException(
                error?.optString("code", "daemon_error") ?: "daemon_error",
                error?.optString("message", "Unknown daemon error") ?: "Unknown daemon error",
            )
        }
        envelope.optJSONObject("result")
            ?: throw DaemonApiException("invalid_response", "Daemon response did not contain a result")
    }

    fun subscribe(request: JSONObject): Flow<JSONObject> = callbackFlow {
        val api = try {
            connect()
        } catch (error: Throwable) {
            close(error)
            return@callbackFlow
        }
        val callback = object : IVeilKnitStreamCallback.Stub() {
            override fun onLine(line: String?) {
                if (line.isNullOrBlank()) return
                runCatching { JSONObject(line) }
                    .onSuccess { trySend(it) }
                    .onFailure { close(it) }
            }

            override fun onClosed(reason: String?) {
                close(RemoteException(reason ?: "Daemon stream closed"))
            }
        }
        val id = try {
            api.subscribe(request.toString(), callback)
        } catch (error: Throwable) {
            close(error)
            return@callbackFlow
        }
        awaitClose { runCatching { api.unsubscribe(id) } }
    }

    private val IVeilKnitApi.daemonStateJson: String
        get() = getDaemonStateJson()

    companion object {
        const val DAEMON_PACKAGE = "com.example.veilknit_deamon"
        const val DAEMON_SERVICE = "com.example.veilknit_deamon.VeilKnitApiService"
        const val DAEMON_ACTION = "com.example.veilknit_deamon.BIND_LOCAL_API"
    }
}

class DaemonApiException(val code: String, message: String) : RuntimeException(message)
