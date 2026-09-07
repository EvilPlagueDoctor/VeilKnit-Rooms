package com.veilknit.rooms

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import com.veilknit.rooms.ui.VeilKnitRoomsApp
import com.veilknit.rooms.ui.theme.VeilKnitRoomsTheme

class MainActivity : ComponentActivity() {
    private val viewModel by viewModels<RoomsViewModel>()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            VeilKnitRoomsTheme {
                VeilKnitRoomsApp(viewModel)
            }
        }
    }
}
