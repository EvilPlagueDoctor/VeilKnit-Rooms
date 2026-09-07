@file:OptIn(androidx.compose.foundation.ExperimentalFoundationApi::class)

package com.veilknit.rooms.ui

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.graphics.Bitmap
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Block
import androidx.compose.material.icons.filled.ContentCopy
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Groups
import androidx.compose.material.icons.filled.Link
import androidx.compose.material.icons.filled.Menu
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.filled.Person
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.QrCode2
import androidx.compose.material.icons.filled.Send
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.Storage
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalNavigationDrawer
import androidx.compose.material3.NavigationDrawerItem
import androidx.compose.material3.NavigationDrawerItemDefaults
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.material3.rememberDrawerState
import androidx.compose.material3.DrawerValue
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.veilknit.rooms.R
import com.veilknit.rooms.RoomsViewModel
import com.veilknit.rooms.data.ChatMessage
import com.veilknit.rooms.data.ConnectionState
import com.veilknit.rooms.data.Member
import com.veilknit.rooms.data.Role
import com.veilknit.rooms.data.Room
import com.veilknit.rooms.data.RoomsUiState
import com.veilknit.rooms.ui.theme.VeilBorder
import com.veilknit.rooms.ui.theme.VeilEdit
import com.veilknit.rooms.ui.theme.VeilError
import com.veilknit.rooms.ui.theme.VeilMuted
import com.veilknit.rooms.ui.theme.VeilPanel
import com.veilknit.rooms.ui.theme.VeilRed
import com.veilknit.rooms.ui.theme.VeilRedDark
import com.veilknit.rooms.ui.theme.VeilSuccess
import com.veilknit.rooms.ui.theme.VeilText
import com.veilknit.rooms.ui.theme.VeilWarning
import com.veilknit.rooms.ui.theme.VeilWindow
import com.google.zxing.BarcodeFormat
import com.google.zxing.qrcode.QRCodeWriter
import kotlinx.coroutines.launch
import java.text.DateFormat
import java.util.Date

private enum class DialogType { CreateRoom, JoinRoom, RemoveRoom, ShareInvite, Policy, Members, Reauthorize }

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun VeilKnitRoomsApp(viewModel: RoomsViewModel) {
    val state by viewModel.state.collectAsStateWithLifecycle()
    val snackbar = remember { SnackbarHostState() }
    var dialog by remember { mutableStateOf<DialogType?>(null) }
    var selectedMember by remember { mutableStateOf<Member?>(null) }
    var selectedMessage by remember { mutableStateOf<ChatMessage?>(null) }
    var showRoomLog by rememberSaveable { mutableStateOf(false) }
    val context = LocalContext.current

    LaunchedEffect(state.status) {
        if (state.connection == ConnectionState.Error) snackbar.showSnackbar(state.status)
    }

    BoxWithConstraints(Modifier.fillMaxSize()) {
        val wide = maxWidth >= 900.dp
        if (wide) {
            Scaffold(
                containerColor = VeilWindow,
                snackbarHost = { SnackbarHost(snackbar) },
                topBar = {
                    RoomsTopBar(
                        state = state,
                        onMenu = {},
                        showMenu = false,
                        onCreate = { dialog = DialogType.CreateRoom },
                        onJoin = { dialog = DialogType.JoinRoom },
                        onRemoveRoom = { dialog = DialogType.RemoveRoom },
                        onRetryRoom = viewModel::retryRoom,
                        onSync = viewModel::syncRoom,
                        onShareInvite = { dialog = DialogType.ShareInvite },
                        onMembers = { dialog = DialogType.Members },
                        showMembers = false,
                        onReplica = viewModel::toggleReplica,
                        onPolicy = { dialog = DialogType.Policy },
                        onReconnect = { viewModel.reconnect(false) },
                        onReauthorize = { dialog = DialogType.Reauthorize },
                        onCopyLog = viewModel::copyOperationLog,
                    )
                },
            ) { padding ->
                Row(Modifier.fillMaxSize().padding(padding)) {
                    RoomRail(state, viewModel::selectRoom, Modifier.width(78.dp).fillMaxHeight())
                    ChannelPanel(
                        state = state,
                        showRoomLog = showRoomLog,
                        onChannel = { showRoomLog = it },
                        modifier = Modifier.width(220.dp).fillMaxHeight(),
                    )
                    ChatPanel(
                        state = state,
                        showRoomLog = showRoomLog,
                        onSubmit = viewModel::submit,
                        onMessageLongPress = { selectedMessage = it },
                        modifier = Modifier.weight(1f).fillMaxHeight(),
                    )
                    MemberPanel(
                        state.selectedRoom,
                        onMember = {
                            selectedMember = it
                            viewModel.refreshReputation(it.mainDht)
                        },
                        modifier = Modifier.width(250.dp).fillMaxHeight(),
                    )
                }
            }
        } else {
            val drawerState = rememberDrawerState(DrawerValue.Closed)
            val scope = rememberCoroutineScope()
            ModalNavigationDrawer(
                drawerState = drawerState,
                drawerContent = {
                    Card(
                        modifier = Modifier.widthIn(max = 310.dp).fillMaxHeight(),
                        shape = RoundedCornerShape(0.dp),
                        colors = CardDefaults.cardColors(containerColor = VeilPanel),
                    ) {
                        Row(Modifier.fillMaxSize()) {
                            RoomRail(
                                state,
                                onSelect = { index -> viewModel.selectRoom(index); scope.launch { drawerState.close() } },
                                modifier = Modifier.width(76.dp).fillMaxHeight(),
                            )
                            ChannelPanel(
                                state = state,
                                showRoomLog = showRoomLog,
                                onChannel = { log ->
                                    showRoomLog = log
                                    scope.launch { drawerState.close() }
                                },
                                modifier = Modifier.weight(1f).fillMaxHeight(),
                            )
                        }
                    }
                },
            ) {
                Scaffold(
                    containerColor = VeilWindow,
                    snackbarHost = { SnackbarHost(snackbar) },
                    topBar = {
                        RoomsTopBar(
                            state = state,
                            onMenu = { scope.launch { drawerState.open() } },
                            showMenu = true,
                            onCreate = { dialog = DialogType.CreateRoom },
                            onJoin = { dialog = DialogType.JoinRoom },
                            onRemoveRoom = { dialog = DialogType.RemoveRoom },
                            onRetryRoom = viewModel::retryRoom,
                            onSync = viewModel::syncRoom,
                            onShareInvite = { dialog = DialogType.ShareInvite },
                            onMembers = { dialog = DialogType.Members },
                            showMembers = true,
                            onReplica = viewModel::toggleReplica,
                            onPolicy = { dialog = DialogType.Policy },
                            onReconnect = { viewModel.reconnect(false) },
                            onReauthorize = { dialog = DialogType.Reauthorize },
                            onCopyLog = viewModel::copyOperationLog,
                        )
                    },
                ) { padding ->
                    ChatPanel(
                        state = state,
                        showRoomLog = showRoomLog,
                        onSubmit = viewModel::submit,
                        onMessageLongPress = { selectedMessage = it },
                        modifier = Modifier.fillMaxSize().padding(padding),
                    )
                }
            }
        }
    }

    when (dialog) {
        DialogType.CreateRoom -> TextEntryDialog(
            title = "Create room",
            label = "Room name",
            initial = "Model Airplanes",
            onDismiss = { dialog = null },
            onConfirm = { viewModel.createRoom(it); dialog = null },
        )
        DialogType.JoinRoom -> TextEntryDialog(
            title = "Join room",
            label = "VKROOM1 invite code",
            multiline = true,
            onDismiss = { dialog = null },
            onConfirm = { viewModel.joinRoom(it); dialog = null },
        )
        DialogType.RemoveRoom -> AlertDialog(
            onDismissRequest = { dialog = null },
            title = { Text("Remove room?") },
            text = {
                Text(
                    "Remove ‘${state.selectedRoom?.name.orEmpty()}’ from this device? " +
                        "Local membership and history will be deleted. Distributed copies are not deleted.",
                )
            },
            confirmButton = {
                Button(
                    onClick = { viewModel.removeRoom(); dialog = null },
                    colors = ButtonDefaults.buttonColors(containerColor = VeilRed),
                ) {
                    Icon(Icons.Default.Delete, null)
                    Spacer(Modifier.width(6.dp))
                    Text("Remove")
                }
            },
            dismissButton = { TextButton(onClick = { dialog = null }) { Text("Cancel") } },
        )
        DialogType.ShareInvite -> ShareInviteDialog(
            invite = viewModel.inviteCode(),
            onCopy = { copyText(context, viewModel.inviteCode(), "Room invite") },
            onDismiss = { dialog = null },
        )
        DialogType.Policy -> TextEntryDialog(
            title = "Blocked phrases",
            label = "One phrase per line",
            initial = state.selectedRoom?.bannedPhrases?.joinToString("\n").orEmpty(),
            multiline = true,
            onDismiss = { dialog = null },
            onConfirm = { value -> viewModel.setPhrases(value.lines()); dialog = null },
        )
        DialogType.Members -> AlertDialog(
            onDismissRequest = { dialog = null },
            confirmButton = { TextButton(onClick = { dialog = null }) { Text("Close") } },
            title = { Text("Room members") },
            text = {
                LazyColumn(Modifier.fillMaxWidth().height(430.dp)) {
                    items(state.selectedRoom?.members?.values?.toList().orEmpty(), key = Member::mainDht) { member ->
                        MemberRow(member, onClick = { selectedMember = member })
                    }
                }
            },
        )
        DialogType.Reauthorize -> AlertDialog(
            onDismissRequest = { dialog = null },
            title = { Text("Request fresh authorization?") },
            text = { Text("This deletes only the Rooms daemon credential. Room memberships and local history are retained.") },
            confirmButton = {
                Button(onClick = { viewModel.reconnect(true); dialog = null }, colors = ButtonDefaults.buttonColors(containerColor = VeilRed)) {
                    Text("Reauthorize")
                }
            },
            dismissButton = { TextButton(onClick = { dialog = null }) { Text("Cancel") } },
        )
        null -> Unit
    }

    selectedMember?.let { member ->
        MemberActionsDialog(
            member = state.selectedRoom?.members?.get(member.mainDht) ?: member,
            localMainDht = state.mainDht,
            room = state.selectedRoom,
            onDismiss = { selectedMember = null },
            onRole = { viewModel.changeRole(member.mainDht, it); selectedMember = null },
            onBan = { viewModel.toggleBan(member.mainDht); selectedMember = null },
            onCopy = { copyText(context, member.mainDht, "DHT address") },
            onReputation = { viewModel.refreshReputation(member.mainDht) },
        )
    }

    selectedMessage?.let { message ->
        val room = state.selectedRoom
        val localRole = room?.members?.get(state.mainDht)?.role ?: Role.Member
        val sender = room?.members?.get(message.senderMainDht)
        val senderIsOwner = message.senderMainDht == room?.ownerMainDht
        val canDelete = localRole == Role.Owner || localRole == Role.Moderator || localRole == Role.Helper
        val canManageSender = message.senderMainDht != state.mainDht && !senderIsOwner
        val canBanSender = canManageSender && (
            localRole == Role.Owner || (localRole == Role.Moderator && sender?.role != Role.Moderator)
        )
        AlertDialog(
            onDismissRequest = { selectedMessage = null },
            title = { Text(message.senderName.ifBlank { "Message" }) },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    Text(message.text)
                    HorizontalDivider(Modifier.padding(vertical = 6.dp))
                    TextButton(onClick = {
                        copyText(context, message.eventId, "Message ID")
                        selectedMessage = null
                    }) {
                        Icon(Icons.Default.ContentCopy, null)
                        Spacer(Modifier.width(6.dp))
                        Text("Copy message ID")
                    }
                    if (canDelete) {
                        TextButton(onClick = {
                            viewModel.deleteMessage(message.eventId)
                            selectedMessage = null
                        }) {
                            Icon(Icons.Default.Delete, null)
                            Spacer(Modifier.width(6.dp))
                            Text("Delete message")
                        }
                    }
                    if (canManageSender && localRole == Role.Owner) {
                        TextButton(onClick = {
                            viewModel.changeRole(message.senderMainDht, Role.Moderator)
                            selectedMessage = null
                        }) { Text("Make sender moderator") }
                    } else if (canManageSender && localRole == Role.Moderator && sender?.role != Role.Moderator) {
                        TextButton(onClick = {
                            viewModel.changeRole(message.senderMainDht, Role.Helper)
                            selectedMessage = null
                        }) { Text("Make sender helper") }
                    }
                    if (canBanSender) {
                        TextButton(onClick = {
                            viewModel.toggleBan(message.senderMainDht)
                            selectedMessage = null
                        }) {
                            Icon(Icons.Default.Block, null)
                            Spacer(Modifier.width(6.dp))
                            Text(if (sender?.banned == true) "Unban sender" else "Ban sender")
                        }
                    }
                }
            },
            confirmButton = { TextButton(onClick = { selectedMessage = null }) { Text("Close") } },
        )
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun RoomsTopBar(
    state: RoomsUiState,
    onMenu: () -> Unit,
    showMenu: Boolean,
    onCreate: () -> Unit,
    onJoin: () -> Unit,
    onRemoveRoom: () -> Unit,
    onRetryRoom: () -> Unit,
    onSync: () -> Unit,
    onShareInvite: () -> Unit,
    onMembers: () -> Unit,
    showMembers: Boolean,
    onReplica: () -> Unit,
    onPolicy: () -> Unit,
    onReconnect: () -> Unit,
    onReauthorize: () -> Unit,
    onCopyLog: () -> Unit,
) {
    var expanded by remember { mutableStateOf(false) }
    TopAppBar(
        colors = TopAppBarDefaults.topAppBarColors(containerColor = VeilPanel, titleContentColor = VeilText),
        navigationIcon = {
            if (showMenu) IconButton(onClick = onMenu) { Icon(Icons.Default.Menu, "Rooms") }
            else Image(
                painterResource(R.drawable.veilknit_logo),
                contentDescription = null,
                modifier = Modifier.padding(start = 10.dp).size(38.dp).clip(RoundedCornerShape(8.dp)),
                contentScale = ContentScale.Fit,
            )
        },
        title = {
            Column {
                Text(state.selectedRoom?.name ?: "VeilKnit Rooms", maxLines = 1, overflow = TextOverflow.Ellipsis)
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Box(Modifier.size(8.dp).background(connectionColor(state.connection), CircleShape))
                    Spacer(Modifier.width(6.dp))
                    Text(state.status, style = MaterialTheme.typography.labelSmall, color = VeilMuted, maxLines = 1, overflow = TextOverflow.Ellipsis)
                }
            }
        },
        actions = {
            if (showMembers && state.selectedRoom != null) {
                IconButton(onClick = onMembers) { Icon(Icons.Default.Groups, "Members") }
            }
            IconButton(
                onClick = onSync,
                enabled = state.selectedRoom != null && state.selectedRoom?.suspended != true,
            ) { Icon(Icons.Default.Refresh, "Synchronize") }
            Box {
                IconButton(onClick = { expanded = true }) { Icon(Icons.Default.MoreVert, "Room actions") }
                DropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
                    DropdownMenuItem(text = { Text(if (state.busyOperation == null) "Create room" else state.busyOperation + "…") }, leadingIcon = { Icon(Icons.Default.Add, null) }, enabled = state.busyOperation == null, onClick = { expanded = false; onCreate() })
                    DropdownMenuItem(text = { Text("Join room") }, leadingIcon = { Icon(Icons.Default.Link, null) }, enabled = state.busyOperation == null, onClick = { expanded = false; onJoin() })
                    DropdownMenuItem(
                        text = { Text("Remove room") },
                        leadingIcon = { Icon(Icons.Default.Delete, null) },
                        enabled = state.selectedRoom != null,
                        onClick = { expanded = false; onRemoveRoom() },
                    )
                    if (state.selectedRoom?.suspended == true) {
                        DropdownMenuItem(
                            text = { Text("Retry room") },
                            leadingIcon = { Icon(Icons.Default.Refresh, null) },
                            onClick = { expanded = false; onRetryRoom() },
                        )
                    }
                    DropdownMenuItem(text = { Text("Share invite / QR") }, leadingIcon = { Icon(Icons.Default.QrCode2, null) }, enabled = state.selectedRoom != null && state.selectedRoom?.suspended != true, onClick = { expanded = false; onShareInvite() })
                    DropdownMenuItem(text = { Text(if (state.selectedRoom?.localReplica == true) "Stop replica" else "Become replica") }, leadingIcon = { Icon(Icons.Default.Storage, null) }, enabled = state.selectedRoom != null && state.selectedRoom?.suspended != true, onClick = { expanded = false; onReplica() })
                    DropdownMenuItem(text = { Text("Room policy") }, leadingIcon = { Icon(Icons.Default.Settings, null) }, enabled = state.selectedRoom != null && state.selectedRoom?.suspended != true, onClick = { expanded = false; onPolicy() })
                    HorizontalDivider()
                    DropdownMenuItem(text = { Text("Copy operation log") }, onClick = { expanded = false; onCopyLog() })
                    DropdownMenuItem(text = { Text("Reconnect to daemon") }, onClick = { expanded = false; onReconnect() })
                    DropdownMenuItem(text = { Text("Reset daemon authorization") }, onClick = { expanded = false; onReauthorize() })
                }
            }
        },
    )
}

@Composable
private fun RoomRail(state: RoomsUiState, onSelect: (Int) -> Unit, modifier: Modifier = Modifier) {
    LazyColumn(
        modifier = modifier.background(VeilWindow).padding(vertical = 10.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        contentPadding = PaddingValues(bottom = 24.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        item {
            Image(
                painterResource(R.drawable.veilknit_logo),
                contentDescription = "VeilKnit",
                modifier = Modifier.size(54.dp).clip(RoundedCornerShape(14.dp)),
            )
            HorizontalDivider(Modifier.padding(horizontal = 14.dp, vertical = 6.dp), color = VeilBorder)
        }
        itemsIndexed(state.rooms, key = { _, room -> room.roomId }) { index, room ->
            val selected = index == state.selectedRoomIndex
            val roomShape = if (selected) RoundedCornerShape(14.dp) else CircleShape
            Box(contentAlignment = Alignment.CenterStart) {
                if (selected) Box(Modifier.width(4.dp).height(34.dp).background(VeilRed, RoundedCornerShape(topEnd = 4.dp, bottomEnd = 4.dp)))
                Box(
                    modifier = Modifier
                        .padding(start = 10.dp)
                        .size(48.dp)
                        .clip(roomShape)
                        .background(if (room.suspended) VeilError.copy(alpha = 0.28f) else if (selected) VeilRedDark else VeilPanel)
                        .border(1.dp, if (room.suspended) VeilError else if (selected) VeilRed else VeilBorder, roomShape)
                        .drawBehind {
                            if (room.suspended) {
                                drawLine(
                                    color = VeilError,
                                    start = androidx.compose.ui.geometry.Offset(size.width * 0.18f, size.height * 0.82f),
                                    end = androidx.compose.ui.geometry.Offset(size.width * 0.82f, size.height * 0.18f),
                                    strokeWidth = 4.dp.toPx(),
                                )
                            }
                        }
                        .combinedClickable(onClick = { onSelect(index) }, onLongClick = { onSelect(index) }),
                    contentAlignment = Alignment.Center,
                ) {
                    Text(roomInitials(room.name), fontWeight = FontWeight.Bold, color = VeilText)
                }
            }
        }
    }
}

@Composable
private fun ChannelPanel(
    state: RoomsUiState,
    showRoomLog: Boolean,
    onChannel: (Boolean) -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(modifier.background(VeilPanel).padding(12.dp)) {
        Text(state.selectedRoom?.name ?: "No room", fontWeight = FontWeight.Bold, color = VeilText, maxLines = 1, overflow = TextOverflow.Ellipsis)
        Spacer(Modifier.height(18.dp))
        Text("TEXT CHANNELS", style = MaterialTheme.typography.labelSmall, color = VeilMuted)
        NavigationDrawerItem(
            label = { Text("# general") },
            selected = !showRoomLog,
            onClick = { onChannel(false) },
            colors = NavigationDrawerItemDefaults.colors(selectedContainerColor = VeilEdit, selectedTextColor = VeilText),
        )
        NavigationDrawerItem(
            label = { Text("# room-log") },
            selected = showRoomLog,
            onClick = { onChannel(true) },
            colors = NavigationDrawerItemDefaults.colors(selectedContainerColor = VeilEdit, selectedTextColor = VeilText),
        )
        Spacer(Modifier.weight(1f))
        HorizontalDivider(color = VeilBorder)
        Spacer(Modifier.height(10.dp))
        Text(state.username.ifBlank { "Not connected" }, fontWeight = FontWeight.SemiBold, color = VeilText, maxLines = 1)
        Text(shortDht(state.mainDht), style = MaterialTheme.typography.labelSmall, color = VeilMuted, maxLines = 1)
    }
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun ChatPanel(
    state: RoomsUiState,
    showRoomLog: Boolean,
    onSubmit: (String) -> Unit,
    onMessageLongPress: (ChatMessage) -> Unit,
    modifier: Modifier = Modifier,
) {
    var compose by rememberSaveable { mutableStateOf("") }
    Column(modifier.background(VeilWindow)) {
        val room = state.selectedRoom
        LazyColumn(
            modifier = Modifier.weight(1f).fillMaxWidth(),
            reverseLayout = true,
            contentPadding = PaddingValues(horizontal = 14.dp, vertical = 12.dp),
            verticalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            if (room == null) {
                item { EmptyRoomState() }
            } else {
                val visibleMessages = room.messages.filter { it.system == showRoomLog }
                items(visibleMessages.asReversed(), key = ChatMessage::eventId) { message ->
                    MessageRow(
                        message = message,
                        mine = message.senderMainDht == state.mainDht,
                        onLongPress = { if (!message.system) onMessageLongPress(message) },
                    )
                }
            }
        }
        Row(
            Modifier.fillMaxWidth().background(VeilPanel).padding(10.dp),
            verticalAlignment = Alignment.Bottom,
        ) {
            OutlinedTextField(
                value = compose,
                onValueChange = { compose = it },
                placeholder = {
                    Text(
                        when {
                            room == null -> "Create or join a room"
                            room.suspended -> "Room paused — use Retry from the menu"
                            showRoomLog -> "Room log is read-only"
                            else -> "Message #general or type /commands"
                        },
                    )
                },
                enabled = room != null && !room.suspended && !showRoomLog,
                modifier = Modifier.weight(1f),
                maxLines = 5,
                keyboardOptions = KeyboardOptions(imeAction = ImeAction.Send),
                keyboardActions = KeyboardActions(onSend = {
                    if (compose.isNotBlank()) { onSubmit(compose); compose = "" }
                }),
            )
            Spacer(Modifier.width(8.dp))
            IconButton(
                onClick = { if (compose.isNotBlank()) { onSubmit(compose); compose = "" } },
                enabled = room != null && !room.suspended && !showRoomLog,
                modifier = Modifier.background(VeilRed, RoundedCornerShape(14.dp)).size(52.dp),
            ) {
                Icon(Icons.Default.Send, "Send", tint = VeilText)
            }
        }
    }
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun MessageRow(message: ChatMessage, mine: Boolean, onLongPress: () -> Unit) {
    if (message.system) {
        Text(
            message.text,
            style = MaterialTheme.typography.labelMedium,
            color = VeilMuted,
            modifier = Modifier.fillMaxWidth().padding(vertical = 5.dp, horizontal = 8.dp),
        )
        return
    }
    Row(
        Modifier.fillMaxWidth().combinedClickable(onClick = {}, onLongClick = onLongPress).padding(vertical = 4.dp),
        verticalAlignment = Alignment.Top,
    ) {
        Box(
            Modifier.size(38.dp).background(if (mine) VeilRedDark else VeilPanel, CircleShape),
            contentAlignment = Alignment.Center,
        ) {
            Text(roomInitials(message.senderName), fontWeight = FontWeight.Bold, color = VeilText)
        }
        Spacer(Modifier.width(10.dp))
        Column(Modifier.weight(1f)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(message.senderName.ifBlank { shortDht(message.senderMainDht) }, fontWeight = FontWeight.SemiBold, color = if (mine) VeilRed else VeilText)
                Spacer(Modifier.width(8.dp))
                Text(formatTime(message.createdAt), style = MaterialTheme.typography.labelSmall, color = VeilMuted)
                if (message.recovery) {
                    Spacer(Modifier.width(6.dp))
                    Text("RECOVERY", style = MaterialTheme.typography.labelSmall, color = VeilWarning)
                }
            }
            Text(
                if (message.deleted) "[message deleted]" else message.text,
                color = if (message.deleted) VeilMuted else VeilText,
                fontFamily = if (message.text.startsWith("```") && message.text.endsWith("```")) FontFamily.Monospace else FontFamily.Default,
            )
        }
    }
}

@Composable
private fun MemberPanel(room: Room?, onMember: (Member) -> Unit, modifier: Modifier = Modifier) {
    LazyColumn(modifier.background(VeilPanel).padding(12.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
        Role.entries.forEach { role ->
            val members = room?.members?.values?.filter { it.role == role }.orEmpty()
            if (members.isNotEmpty()) {
                item { Text("${role.label.uppercase()}S — ${members.size}", style = MaterialTheme.typography.labelSmall, color = VeilMuted, modifier = Modifier.padding(top = 12.dp, bottom = 4.dp)) }
                items(members, key = Member::mainDht) { member -> MemberRow(member, { onMember(member) }) }
            }
        }
    }
}

@Composable
private fun MemberRow(member: Member, onClick: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().clip(RoundedCornerShape(8.dp)).combinedClickable(onClick = onClick, onLongClick = onClick).padding(8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(Modifier.size(9.dp).background(if (member.banned) VeilError else if (member.online) VeilSuccess else VeilMuted, CircleShape))
        Spacer(Modifier.width(9.dp))
        Column(Modifier.weight(1f)) {
            Text(member.displayName.ifBlank { shortDht(member.mainDht) }, color = if (member.banned) VeilMuted else VeilText, maxLines = 1, overflow = TextOverflow.Ellipsis)
            Text(
                buildString {
                    if (member.replica) append("replica • ")
                    append(
                        if (member.reputationClass.isBlank() || member.reputationConfidence == 0) "reputation pending"
                        else "${member.reputationClass} ${member.reputationConfidence}%"
                    )
                },
                style = MaterialTheme.typography.labelSmall,
                color = VeilMuted,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
    }
}

@Composable
private fun MemberActionsDialog(
    member: Member,
    localMainDht: String,
    room: Room?,
    onDismiss: () -> Unit,
    onRole: (Role) -> Unit,
    onBan: () -> Unit,
    onCopy: () -> Unit,
    onReputation: () -> Unit,
) {
    val localRole = room?.members?.get(localMainDht)?.role ?: Role.Member
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(member.displayName) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text(shortDht(member.mainDht, 16), color = VeilMuted)
                Text("Role: ${member.role.label}")
                if (member.reputationClass.isNotBlank()) Text("Reputation: ${member.reputationClass} (${member.reputationConfidence}%)")
                if (localRole == Role.Owner || localRole == Role.Moderator) {
                    HorizontalDivider(Modifier.padding(vertical = 6.dp))
                    if (member.mainDht != room?.ownerMainDht) {
                        if (localRole == Role.Owner) {
                            TextButton(onClick = { onRole(Role.Moderator) }) { Text("Make moderator") }
                            if (member.role != Role.Member) TextButton(onClick = { onRole(Role.Member) }) { Text("Make member") }
                        } else if (member.role != Role.Moderator) {
                            TextButton(onClick = { onRole(Role.Helper) }) { Text("Make helper") }
                            if (member.role != Role.Member) TextButton(onClick = { onRole(Role.Member) }) { Text("Make member") }
                        }
                        if (localRole == Role.Owner || member.role != Role.Moderator) {
                            TextButton(onClick = onBan) {
                                Icon(Icons.Default.Block, null)
                                Spacer(Modifier.width(6.dp))
                                Text(if (member.banned) "Unban" else "Ban")
                            }
                        }
                    }
                }
                TextButton(onClick = onCopy) { Icon(Icons.Default.ContentCopy, null); Spacer(Modifier.width(6.dp)); Text("Copy DHT address") }
                TextButton(onClick = onReputation) { Icon(Icons.Default.Person, null); Spacer(Modifier.width(6.dp)); Text("Refresh reputation") }
            }
        },
        confirmButton = { TextButton(onClick = onDismiss) { Text("Close") } },
    )
}

@Composable
private fun ShareInviteDialog(
    invite: String,
    onCopy: () -> Unit,
    onDismiss: () -> Unit,
) {
    val qrBitmap = remember(invite) { invite.takeIf(String::isNotBlank)?.let(::inviteQrBitmap) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Share room") },
        text = {
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                if (qrBitmap != null) {
                    Image(
                        bitmap = qrBitmap.asImageBitmap(),
                        contentDescription = "Scannable room invitation",
                        modifier = Modifier.size(280.dp).background(Color.White).padding(8.dp),
                    )
                    Spacer(Modifier.height(12.dp))
                } else {
                    Text("This room does not have a published invitation yet.", color = VeilMuted)
                }
                SelectionContainer {
                    Text(invite, style = MaterialTheme.typography.labelSmall, color = VeilMuted, maxLines = 6)
                }
            }
        },
        confirmButton = {
            Button(onClick = onCopy, enabled = invite.isNotBlank(), colors = ButtonDefaults.buttonColors(containerColor = VeilRed)) {
                Icon(Icons.Default.ContentCopy, null)
                Spacer(Modifier.width(6.dp))
                Text("Copy invite")
            }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Close") } },
    )
}

private fun inviteQrBitmap(value: String): Bitmap {
    val matrix = QRCodeWriter().encode(value, BarcodeFormat.QR_CODE, 768, 768)
    val pixels = IntArray(matrix.width * matrix.height)
    for (y in 0 until matrix.height) {
        for (x in 0 until matrix.width) {
            pixels[y * matrix.width + x] = if (matrix[x, y]) android.graphics.Color.BLACK else android.graphics.Color.WHITE
        }
    }
    return Bitmap.createBitmap(matrix.width, matrix.height, Bitmap.Config.ARGB_8888).apply {
        setPixels(pixels, 0, matrix.width, 0, 0, matrix.width, matrix.height)
    }
}

@Composable
private fun TextEntryDialog(
    title: String,
    label: String,
    initial: String = "",
    multiline: Boolean = false,
    onDismiss: () -> Unit,
    onConfirm: (String) -> Unit,
) {
    var text by rememberSaveable(title) { mutableStateOf(initial) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = {
            OutlinedTextField(
                value = text,
                onValueChange = { text = it },
                label = { Text(label) },
                minLines = if (multiline) 5 else 1,
                maxLines = if (multiline) 10 else 1,
                modifier = Modifier.fillMaxWidth(),
            )
        },
        confirmButton = {
            Button(onClick = { onConfirm(text) }, enabled = text.isNotBlank(), colors = ButtonDefaults.buttonColors(containerColor = VeilRed)) {
                Text("OK")
            }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    )
}

@Composable
private fun EmptyRoomState() {
    Column(
        Modifier.fillMaxWidth().padding(32.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        Image(painterResource(R.drawable.veilknit_logo), null, Modifier.size(150.dp).clip(RoundedCornerShape(28.dp)))
        Spacer(Modifier.height(18.dp))
        Text("No room selected", style = MaterialTheme.typography.headlineSmall, fontWeight = FontWeight.Bold)
        Text("Use the menu to create or join a room.", color = VeilMuted)
    }
}

private fun connectionColor(state: ConnectionState): Color = when (state) {
    ConnectionState.Connected -> VeilSuccess
    ConnectionState.Connecting, ConnectionState.Authorizing -> VeilWarning
    ConnectionState.Error -> VeilError
    ConnectionState.Disconnected -> VeilMuted
}

private fun copyText(context: Context, text: String, label: String) {
    if (text.isBlank()) return
    context.getSystemService(ClipboardManager::class.java).setPrimaryClip(ClipData.newPlainText(label, text))
}

private fun roomInitials(name: String): String = name.trim().split(Regex("\\s+")).filter(String::isNotBlank).take(2).joinToString("") { it.first().uppercase() }.ifBlank { "?" }
private fun shortDht(value: String, amount: Int = 8): String = if (value.length <= amount * 2 + 1) value else "${value.take(amount)}…${value.takeLast(amount)}"
private fun formatTime(seconds: Long): String = if (seconds <= 0) "" else DateFormat.getTimeInstance(DateFormat.SHORT).format(Date(seconds * 1_000))
