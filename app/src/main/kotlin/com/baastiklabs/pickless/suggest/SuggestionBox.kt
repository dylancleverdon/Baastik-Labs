package com.baastiklabs.pickless.suggest

import android.content.Context
import android.os.Build
import com.baastiklabs.pickless.BuildConfig
import com.baastiklabs.pickless.core.suggest.SuggestionPayload
import com.baastiklabs.pickless.data.PicklessDb
import com.baastiklabs.pickless.data.Repository
import com.baastiklabs.pickless.data.Settings
import com.baastiklabs.pickless.data.SuggestionEntity
import com.baastiklabs.pickless.log.AppLog
import com.baastiklabs.pickless.net.Http
import com.baastiklabs.pickless.work.Jobs
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.serialization.json.Json
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import java.io.File
import java.time.Instant
import java.time.LocalDateTime
import java.time.format.DateTimeFormatter
import java.util.UUID

/**
 * The suggestion box, like Tutti's: suggestions are queued on the phone and posted to the
 * "elimination diet app suggestion sheet" through its Apps Script web app (suggestions/Code.gs).
 * Each has an id, and the script ignores ids it already has, so retries never duplicate a row.
 * "Send logs" attaches diagnostics; diary data only goes along if "include my data" is ticked.
 */
class SuggestionBox(private val context: Context, private val db: PicklessDb, private val settings: Settings) {
    private val json = Json { encodeDefaults = true }
    private val dir = File(context.filesDir, "suggestions").apply { mkdirs() }

    companion object {
        fun validEndpoint(url: String): Boolean {
            val u = url.trim()
            return u.startsWith("https://script.google.com/macros/s/") && (u.endsWith("/exec") || u.contains("/exec?"))
        }
    }

    suspend fun add(kind: String, text: String, sendLogs: Boolean, includeData: String?) {
        val id = UUID.randomUUID().toString()
        var logPath: String? = null
        if (sendLogs || includeData != null) {
            val f = File(dir, "$id.txt")
            f.writeText(buildString {
                append("Pickless ${BuildConfig.VERSION_NAME} (${BuildConfig.VERSION_CODE}) on ${Build.MANUFACTURER} ${Build.MODEL}, Android ${Build.VERSION.RELEASE} (SDK ${Build.VERSION.SDK_INT})\n")
                if (sendLogs) append("\n--- app log ---\n").append(AppLog.recent())
                if (includeData != null) append("\n--- the owner's data, included on purpose ---\n").append(includeData)
            })
            logPath = f.path
        }
        db.suggestions().upsert(SuggestionEntity(id, kind, text.take(5000), logPath, System.currentTimeMillis()))
        AppLog.i("suggest", "queued $kind${if (logPath != null) " with logs" else ""}")
        Jobs.sendSuggestions(context)
    }

    /** Try to send everything waiting. Returns how many are still waiting. */
    suspend fun sendPending(): Int = withContext(Dispatchers.IO) {
        val endpoint = settings.current().endpoint
        val pending = db.suggestions().pending()
        if (pending.isEmpty()) return@withContext 0
        if (!validEndpoint(endpoint)) return@withContext pending.size
        val from = settings.current().suggestFrom
        var left = 0
        for (s in pending) {
            val payload = SuggestionPayload(
                id = s.id, kind = s.kind, text = s.text, from = from, version = BuildConfig.VERSION_NAME,
                phone = "${Build.MANUFACTURER} ${Build.MODEL}", android = "Android ${Build.VERSION.RELEASE}",
                at = LocalDateTime.now().format(DateTimeFormatter.ISO_LOCAL_DATE_TIME),
                log = s.logPath?.let { File(it) }?.takeIf { it.exists() }?.readText()?.takeLast(7_000_000),
            )
            val ok = runCatching {
                val req = Request.Builder().url(endpoint)
                    .post(json.encodeToString(SuggestionPayload.serializer(), payload).toRequestBody("application/json".toMediaType())).build()
                Http.client.newCall(req).execute().use { r -> r.isSuccessful && (r.body?.string() ?: "").contains("\"ok\":true") }
            }.getOrElse { AppLog.w("suggest", "send failed: ${it.javaClass.simpleName}"); false }
            if (ok) {
                db.suggestions().upsert(s.copy(sentAt = System.currentTimeMillis(), lastError = null, tries = s.tries + 1))
                s.logPath?.let { File(it).delete() }
            } else {
                db.suggestions().upsert(s.copy(lastError = "Not sent yet", tries = s.tries + 1))
                left++
            }
        }
        AppLog.i("suggest", "sent ${pending.size - left}, waiting $left")
        left
    }

    /** Ping the endpoint (the script answers GET with {"ok":true}). */
    suspend fun test(url: String): Boolean = withContext(Dispatchers.IO) {
        if (!validEndpoint(url)) return@withContext false
        Http.getText(if (url.contains("?")) "$url&format=json" else "$url?format=json")?.contains("\"ok\":true") == true
    }

    /** Last 14 days in plain text, for "include my data". */
    suspend fun dataSummary(repo: Repository): String {
        val since = System.currentTimeMillis() - 14 * 86_400_000L
        val fmt = DateTimeFormatter.ofPattern("MM-dd HH:mm").withZone(repo.zone)
        val sb = StringBuilder()
        sb.append("Avoid list: ").append(settings.current().avoid.joinToString { "${it.label} (${it.status.label})" }).append('\n')
        repo.db.food().allOnce().filter { it.time >= since }.forEach { sb.append("food ${fmt.format(Instant.ofEpochMilli(it.time))} ${it.name} ${it.grams.toInt()}g ${it.verdict}\n") }
        repo.db.bm().allOnce().filter { it.time >= since }.forEach { sb.append("bm ${fmt.format(Instant.ofEpochMilli(it.time))} type ${it.bristol} urgency ${it.urgency}\n") }
        repo.db.symptoms().allOnce().filter { it.time >= since }.forEach { sb.append("symptom ${fmt.format(Instant.ofEpochMilli(it.time))} ${it.type} ${it.severity}\n") }
        repo.db.checkins().allOnce().filter { repo.startOf(it.epochDay) >= since }.forEach { sb.append("checkin day ${it.epochDay} stress ${it.stress} sleep ${it.sleepQuality} nicotine ${it.nicotine} overall ${it.overall}\n") }
        return sb.toString()
    }
}
