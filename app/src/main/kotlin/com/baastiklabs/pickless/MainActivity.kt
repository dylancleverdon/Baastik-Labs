package com.baastiklabs.pickless

import android.content.Intent
import android.os.Bundle
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.biometric.BiometricManager
import androidx.biometric.BiometricPrompt
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.core.content.ContextCompat
import androidx.fragment.app.FragmentActivity
import androidx.lifecycle.lifecycleScope
import com.baastiklabs.pickless.ui.screens.AppRoot
import com.baastiklabs.pickless.ui.theme.PicklessTheme
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.launch

class MainActivity : FragmentActivity() {
    companion object {
        const val EXTRA_ROUTE = "route"
    }

    /** A screen to open, from a notification, the widget or Health Connect. */
    val routeRequests = MutableStateFlow<String?>(null)
    private var unlocked by mutableStateOf(false)
    private var lockChecked by mutableStateOf(false)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        handle(intent)
        val container = (application as PicklessApp).container
        lifecycleScope.launch {
            val lock = container.settings.current().appLock
            if (!lock) unlocked = true else authenticate()
            lockChecked = true
        }
        setContent {
            PicklessTheme {
                AppRoot(container = container, routes = routeRequests, locked = lockChecked && !unlocked, onUnlock = ::authenticate)
            }
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        handle(intent)
    }

    private fun handle(intent: Intent?) {
        val route = intent?.getStringExtra(EXTRA_ROUTE)
            ?: if (intent?.action == "androidx.health.ACTION_SHOW_PERMISSIONS_RATIONALE" || intent?.action == Intent.ACTION_VIEW_PERMISSION_USAGE) "privacy" else null
        if (route != null) routeRequests.value = route
    }

    private fun authenticate() {
        val bm = BiometricManager.from(this)
        val authenticators = BiometricManager.Authenticators.BIOMETRIC_WEAK or BiometricManager.Authenticators.DEVICE_CREDENTIAL
        if (bm.canAuthenticate(authenticators) != BiometricManager.BIOMETRIC_SUCCESS) { unlocked = true; return }
        val prompt = BiometricPrompt(this, ContextCompat.getMainExecutor(this), object : BiometricPrompt.AuthenticationCallback() {
            override fun onAuthenticationSucceeded(result: BiometricPrompt.AuthenticationResult) { unlocked = true }
        })
        prompt.authenticate(BiometricPrompt.PromptInfo.Builder().setTitle("Unlock Pickless").setAllowedAuthenticators(authenticators).build())
    }
}
