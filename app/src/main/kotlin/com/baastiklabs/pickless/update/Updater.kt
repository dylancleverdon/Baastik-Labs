package com.baastiklabs.pickless.update

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.pm.PackageInfo
import android.content.pm.PackageInstaller
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.provider.Settings as AndroidSettings
import com.baastiklabs.pickless.BuildConfig
import com.baastiklabs.pickless.PicklessApp
import com.baastiklabs.pickless.core.update.UpdateManifest
import com.baastiklabs.pickless.core.update.Updates
import com.baastiklabs.pickless.log.AppLog
import com.baastiklabs.pickless.net.Http
import com.baastiklabs.pickless.notify.Notifications
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.withContext
import java.io.File

/**
 * Self-updater, the same idea as Norg's and Tutti's: read `manifest.json` from this repo's rolling
 * `pickless-channel` release, download a newer APK from a `pickless-v*` release, check its SHA-256
 * and signing certificate, then install it. On Android 12+ that needs no tap; otherwise (or if the
 * system asks) a notification says "Update ready: tap to install".
 */
class Updater(private val context: Context) {
    sealed class State {
        data object Idle : State()
        data object Checking : State()
        data class UpToDate(val at: Long) : State()
        data class Downloading(val version: String) : State()
        data class Ready(val version: String) : State()
        data class Failed(val message: String) : State()
        data object NeedsPermission : State()
    }

    private val _state = MutableStateFlow<State>(State.Idle)
    val state: StateFlow<State> = _state

    private val dir = File(context.cacheDir, "updates")

    fun canInstall(): Boolean = Build.VERSION.SDK_INT < Build.VERSION_CODES.O || context.packageManager.canRequestPackageInstalls()

    fun permissionIntent(): Intent = Intent(AndroidSettings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:${context.packageName}"))
        .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)

    /** Check, download and install if there's a newer build. Safe to call often. */
    suspend fun check(userAsked: Boolean = false): State = withContext(Dispatchers.IO) {
        _state.value = State.Checking
        val app = context.applicationContext as PicklessApp
        val body = Http.getText(BuildConfig.UPDATE_MANIFEST_URL + "?t=" + System.currentTimeMillis() / 600_000)
        if (body == null) return@withContext State.Failed("Couldn't reach GitHub.").also { _state.value = it }
        val m = Updates.parse(body) ?: return@withContext State.Failed("The update manifest is unreadable.").also { _state.value = it }
        m.suggestions?.endpoint?.takeIf { it.isNotBlank() }?.let { app.container.settings.setManifestEndpoint(it) }
        app.container.settings.setLastUpdateCheck(System.currentTimeMillis())
        if (!Updates.shouldUpdate(BuildConfig.VERSION_CODE, m, BuildConfig.REPO, Build.VERSION.SDK_INT)) {
            AppLog.i("update", "up to date (${BuildConfig.VERSION_NAME}, channel ${m.version})")
            cleanup()
            return@withContext State.UpToDate(System.currentTimeMillis()).also { _state.value = it }
        }
        if (!app.container.settings.current().autoUpdate && !userAsked) {
            Notifications.updateAvailable(context, m.version)
            return@withContext State.Idle.also { _state.value = it }
        }
        if (!canInstall()) {
            Notifications.allowUpdates(context)
            return@withContext State.NeedsPermission.also { _state.value = it }
        }
        _state.value = State.Downloading(m.version)
        val apk = File(dir, "pickless-${m.versionCode}.apk")
        try {
            val sha = if (apk.exists() && apk.length() == m.apk.size) Updates.sha256Hex(apk.readBytes()) else Http.download(m.apk.url, apk)
            if (sha != m.apk.sha256) {
                apk.delete()
                AppLog.w("update", "checksum mismatch for ${m.version}")
                return@withContext State.Failed("The download didn't match its checksum, so it wasn't installed.").also { _state.value = it }
            }
            if (!sameSigner(apk)) {
                apk.delete()
                AppLog.w("update", "signer mismatch for ${m.version}")
                return@withContext State.Failed("The download isn't signed by Pickless, so it wasn't installed.").also { _state.value = it }
            }
            install(apk, m)
            State.Ready(m.version).also { _state.value = it }
        } catch (e: Exception) {
            AppLog.e("update", "download failed", e)
            State.Failed("Download failed; will try again later.").also { _state.value = it }
        }
    }

    private fun cleanup() { dir.listFiles()?.forEach { it.delete() } }

    @Suppress("DEPRECATION")
    private fun signatures(info: PackageInfo?): Set<String> {
        if (info == null) return emptySet()
        val sigs = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) info.signingInfo?.apkContentsSigners else info.signatures
        return sigs?.map { Updates.sha256Hex(it.toByteArray()) }?.toSet() ?: emptySet()
    }

    @Suppress("DEPRECATION")
    private fun sameSigner(apk: File): Boolean {
        val pm = context.packageManager
        val flags = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) PackageManager.GET_SIGNING_CERTIFICATES else PackageManager.GET_SIGNATURES
        val mine = signatures(pm.getPackageInfo(context.packageName, flags))
        val theirs = signatures(pm.getPackageArchiveInfo(apk.path, flags))
        val pkg = pm.getPackageArchiveInfo(apk.path, 0)?.packageName
        return pkg == context.packageName && mine.isNotEmpty() && mine == theirs
    }

    private fun install(apk: File, m: UpdateManifest) {
        val installer = context.packageManager.packageInstaller
        val params = PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL).apply {
            setAppPackageName(context.packageName)
            setSize(apk.length())
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) setRequireUserAction(PackageInstaller.SessionParams.USER_ACTION_NOT_REQUIRED)
        }
        val id = installer.createSession(params)
        installer.openSession(id).use { session ->
            session.openWrite("pickless.apk", 0, apk.length()).use { out -> apk.inputStream().use { it.copyTo(out) }; session.fsync(out) }
            val intent = Intent(context, InstallReceiver::class.java).putExtra("version", m.version)
            val flags = PendingIntent.FLAG_UPDATE_CURRENT or (if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) PendingIntent.FLAG_MUTABLE else 0)
            val pi = PendingIntent.getBroadcast(context, id, intent, flags)
            session.commit(pi.intentSender)
        }
        AppLog.i("update", "installing ${m.version}")
    }
}

/** Receives the install result; asks the owner to confirm when Android needs a tap. */
class InstallReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        val status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE)
        val version = intent.getStringExtra("version") ?: ""
        when (status) {
            PackageInstaller.STATUS_PENDING_USER_ACTION -> {
                @Suppress("DEPRECATION")
                val confirm = if (Build.VERSION.SDK_INT >= 33) intent.getParcelableExtra(Intent.EXTRA_INTENT, Intent::class.java) else intent.getParcelableExtra(Intent.EXTRA_INTENT)
                if (confirm != null) Notifications.confirmInstall(context, version, confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
                AppLog.i("update", "install of $version needs a tap")
            }
            PackageInstaller.STATUS_SUCCESS -> AppLog.i("update", "installed $version")
            else -> AppLog.w("update", "install of $version failed: $status ${intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE)}")
        }
    }
}
