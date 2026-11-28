package com.baastiklabs.pickless.report

import android.content.Context
import android.content.Intent
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Typeface
import android.graphics.pdf.PdfDocument
import android.net.Uri
import androidx.core.content.FileProvider
import com.baastiklabs.pickless.BuildConfig
import com.baastiklabs.pickless.core.coach.ChallengeResult
import com.baastiklabs.pickless.core.coach.Coach
import com.baastiklabs.pickless.core.detective.Report
import com.baastiklabs.pickless.core.detective.Tier
import com.baastiklabs.pickless.data.BmEntity
import com.baastiklabs.pickless.data.ChallengeEntity
import com.baastiklabs.pickless.data.CheckInEntity
import com.baastiklabs.pickless.data.CorrectionEntity
import com.baastiklabs.pickless.data.CustomFoodEntity
import com.baastiklabs.pickless.data.FoodLogEntity
import com.baastiklabs.pickless.data.ProductEntity
import com.baastiklabs.pickless.data.QuickEventEntity
import com.baastiklabs.pickless.data.Repository
import com.baastiklabs.pickless.data.SymptomEntity
import com.baastiklabs.pickless.data.WeeklyEntity
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import java.io.File
import java.time.Instant
import java.time.LocalDate
import java.time.format.DateTimeFormatter
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

/** PDF report for a doctor or dietitian, spreadsheet export, and full backup / restore. */
object Exports {
    private val json = Json { prettyPrint = false; encodeDefaults = true; ignoreUnknownKeys = true }

    fun share(context: Context, file: File, mime: String, title: String) {
        val uri: Uri = FileProvider.getUriForFile(context, "${context.packageName}.files", file)
        val send = Intent(Intent.ACTION_SEND).setType(mime).putExtra(Intent.EXTRA_STREAM, uri).addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        context.startActivity(Intent.createChooser(send, title).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
    }

    private fun dir(context: Context) = File(context.cacheDir, "exports").apply { mkdirs() }

    /** A Letter-size PDF covering the last [days] days. */
    suspend fun pdf(context: Context, repo: Repository, report: Report?, days: Int): File = withContext(Dispatchers.IO) {
        val since = System.currentTimeMillis() - days * 86_400_000L
        val bms = repo.db.bm().allOnce().filter { it.time >= since }
        val foods = repo.db.food().allOnce().filter { it.time >= since }
        val symptoms = repo.db.symptoms().allOnce().filter { it.time >= since }
        val checkins = repo.db.checkins().allOnce().filter { repo.startOf(it.epochDay) >= since }
        val weekly = repo.db.weekly().allOnce().filter { repo.startOf(it.epochDay) >= since }
        val challenges = repo.db.challenges().allOnce()
        val prefs = repo.settings.current()
        val doc = PdfDocument()
        val w = 612; val h = 792; val margin = 48f
        var pageNo = 0
        var page: PdfDocument.Page? = null
        var y = 0f
        val title = Paint().apply { textSize = 20f; typeface = Typeface.create(Typeface.SERIF, Typeface.BOLD); color = Color.rgb(28, 60, 44) }
        val head = Paint().apply { textSize = 13f; typeface = Typeface.DEFAULT_BOLD; color = Color.rgb(28, 60, 44) }
        val body = Paint().apply { textSize = 10.5f; color = Color.rgb(30, 30, 30) }
        val small = Paint().apply { textSize = 8.5f; color = Color.rgb(100, 100, 100) }
        fun newPage() {
            page?.let { doc.finishPage(it) }
            pageNo++
            page = doc.startPage(PdfDocument.PageInfo.Builder(w, h, pageNo).create())
            y = margin
            page!!.canvas.drawText("Pickless report · page $pageNo", margin, h - 24f, small)
        }
        fun ensure(space: Float) { if (page == null || y + space > h - 48f) newPage() }
        fun line(text: String, p: Paint = body, gap: Float = 4f) {
            val words = text.split(' ')
            var cur = ""
            for (word in words) {
                val t = if (cur.isEmpty()) word else "$cur $word"
                if (p.measureText(t) > w - 2 * margin) { ensure(p.textSize + gap); page!!.canvas.drawText(cur, margin, y + p.textSize, p); y += p.textSize + gap; cur = word } else cur = t
            }
            if (cur.isNotEmpty()) { ensure(p.textSize + gap); page!!.canvas.drawText(cur, margin, y + p.textSize, p); y += p.textSize + gap }
        }
        fun section(name: String) { y += 10f; ensure(40f); line(name, head, 6f) }
        val fmt = DateTimeFormatter.ofPattern("MMM d, yyyy")
        newPage()
        line("Gut and diet summary", title, 8f)
        line("${LocalDate.now().minusDays(days.toLong()).format(fmt)} to ${LocalDate.now().format(fmt)} (last $days days). Made with Pickless ${BuildConfig.VERSION_NAME}, a self-tracking app; values are the patient's own logs.", small, 10f)

        section("Bowel movements")
        val n = bms.size
        val byType = (1..7).associateWith { t -> bms.count { it.bristol == t } }
        line("$n BMs logged (${"%.1f".format(n.toDouble() / days.coerceAtLeast(1))} per day). Bristol types: " + byType.entries.joinToString { "${it.key}: ${it.value}" } + ".")
        if (n > 0) {
            val loose = bms.count { it.bristol >= 6 } * 100 / n
            val hard = bms.count { it.bristol <= 2 } * 100 / n
            line("Loose (6-7): $loose%. Hard (1-2): $hard%. Urgent: ${bms.count { it.urgency >= 2 } * 100 / n}%. Accidents: ${bms.count { it.accident }}. Blood noted: ${bms.count { it.blood }}.")
            // Bristol bar chart
            ensure(90f)
            val canvas = page!!.canvas
            val max = (byType.values.maxOrNull() ?: 1).coerceAtLeast(1)
            val bar = Paint()
            for (t in 1..7) {
                val x = margin + (t - 1) * 60f
                val bh = 60f * (byType[t] ?: 0) / max
                bar.color = when (t) { 3, 4 -> Color.rgb(30, 122, 76); 2, 5 -> Color.rgb(200, 130, 20); else -> Color.rgb(179, 38, 30) }
                canvas.drawRect(x, y + 70f - bh, x + 40f, y + 70f, bar)
                canvas.drawText("Type $t", x, y + 84f, small)
            }
            y += 92f
        }
        report?.bmPattern?.let { line("Pattern: ${it.pattern}.") }

        section("Symptoms")
        if (symptoms.isEmpty()) line("None logged.") else symptoms.groupBy { it.type }.forEach { (type, list) ->
            line("${type.replaceFirstChar { it.uppercase() }}: ${list.size} times, average severity ${"%.1f".format(list.map { it.severity }.average())} of 4.")
        }
        if (weekly.isNotEmpty()) line("Weekly IBS severity score (0-500): " + weekly.joinToString { "${LocalDate.ofEpochDay(it.epochDay).format(DateTimeFormatter.ofPattern("MMM d"))}: ${it.total}" } + ".")

        section("Diet")
        line("Avoiding: " + prefs.avoid.joinToString { "${it.label} (${it.status.label.lowercase()})" } + ".")
        line("${foods.size} foods logged on ${foods.map { repo.epochDay(it.time) }.toSet().size} days.")
        val done = challenges.filter { !it.active && it.result != null }
        if (done.isNotEmpty()) {
            line("Reintroduction tests:")
            done.forEach { e -> line("  • ${Coach.protocol(e.protocolId)?.title ?: e.protocolId}: ${ChallengeResult.byId(e.result)?.label ?: "-"}") }
        }

        section("Possible triggers (from the logs)")
        if (report == null || report.suspects.isEmpty()) line("No clear suspects in this period.") else {
            if (!report.quality.solid) line("Early read: not much data yet, so these are tentative.", small)
            report.suspects.filter { it.tier.rank >= Tier.WEAK.rank }.take(10).forEach { s ->
                line("• ${s.label}: bad BMs ${"%.1f".format(s.ratio)}× more likely after it (${s.badAfter} of ${s.exposedDays} days; evidence ${s.tier.label.lowercase()})")
            }
            report.lifestyle.take(5).forEach { s -> line("• ${s.label}: ${"%.1f".format(s.ratio)}× (${s.badAfter}/${s.exposedDays} days)") }
        }

        section("Daily check-ins")
        if (checkins.isEmpty()) line("None.") else {
            fun avg(f: (CheckInEntity) -> Int?) = checkins.mapNotNull(f).takeIf { it.isNotEmpty() }?.average()?.let { "%.1f".format(it) } ?: "-"
            line("${checkins.size} check-ins. Average overall ${avg { it.overall }}/5, stress ${avg { it.stress }}/4, sleep quality ${avg { it.sleepQuality }}/4.")
            val meds = checkins.flatMap { it.meds.split(',') }.filter { it.isNotBlank() }.groupingBy { it }.eachCount()
            if (meds.isNotEmpty()) line("Medicine and supplements: " + meds.entries.joinToString { "${it.key} (${it.value} days)" } + ".")
            val nic = checkins.mapNotNull { it.nicotine }.groupingBy { it }.eachCount()
            if (nic.isNotEmpty()) line("Nicotine vs usual: " + nic.entries.joinToString { "${it.key} ${it.value} days" } + ".")
        }
        y += 12f
        line("Suspects are statistical associations from self-tracking, not diagnoses. Red-flag symptoms (blood, weight loss, night-time symptoms) should be assessed by a clinician.", small)
        page?.let { doc.finishPage(it) }
        val f = File(dir(context), "pickless-report-${LocalDate.now()}.pdf")
        f.outputStream().use { doc.writeTo(it) }
        doc.close()
        f
    }

    private fun csv(vararg v: Any?): String = v.joinToString(",") { x -> val s = x?.toString() ?: ""; if (s.any { it == ',' || it == '"' || it == '\n' }) "\"" + s.replace("\"", "\"\"") + "\"" else s }

    suspend fun spreadsheet(context: Context, repo: Repository): File = withContext(Dispatchers.IO) {
        val f = File(dir(context), "pickless-data-${LocalDate.now()}.zip")
        val iso = DateTimeFormatter.ISO_LOCAL_DATE_TIME.withZone(repo.zone)
        fun t(ms: Long) = iso.format(Instant.ofEpochMilli(ms))
        ZipOutputStream(f.outputStream()).use { zip ->
            fun entry(name: String, lines: List<String>) { zip.putNextEntry(ZipEntry(name)); zip.write(lines.joinToString("\n").toByteArray()); zip.closeEntry() }
            entry("food.csv", listOf(csv("time", "meal", "food", "grams", "portion", "verdict", "avoid hits", "note")) + repo.db.food().allOnce().map { csv(t(it.time), it.meal, it.name, it.grams, it.portionLabel, it.verdict, it.avoidHits, it.note) })
            entry("bms.csv", listOf(csv("time", "bristol", "urgency", "pain", "straining", "incomplete", "accident", "blood", "mucus", "color")) + repo.db.bm().allOnce().map { csv(t(it.time), it.bristol, it.urgency, it.pain, it.straining, it.incomplete, it.accident, it.blood, it.mucus, it.color) })
            entry("symptoms.csv", listOf(csv("time", "type", "severity", "note")) + repo.db.symptoms().allOnce().map { csv(t(it.time), it.type, it.severity, it.note) })
            entry("checkins.csv", listOf(csv("date", "overall", "stress", "sleep quality", "sleep hours", "nicotine", "alcohol", "caffeine", "exercise", "period", "sick", "travel", "no bm", "meds", "note")) +
                repo.db.checkins().allOnce().map { csv(LocalDate.ofEpochDay(it.epochDay), it.overall, it.stress, it.sleepQuality, it.sleepHours, it.nicotine, it.alcohol, it.caffeine, it.exercise, it.period, it.sick, it.travel, it.noBm, it.meds, it.note) })
            entry("weekly.csv", listOf(csv("date", "pain", "pain days", "bloating", "dissatisfaction", "interference", "total")) + repo.db.weekly().allOnce().map { csv(LocalDate.ofEpochDay(it.epochDay), it.painSeverity, it.painDays, it.bloating, it.dissatisfaction, it.interference, it.total) })
        }
        f
    }

    @Serializable
    data class Backup(
        val app: String = "pickless", val version: Int = 1, val made: Long = System.currentTimeMillis(),
        val food: List<FoodLogEntity> = emptyList(), val bms: List<BmEntity> = emptyList(), val symptoms: List<SymptomEntity> = emptyList(),
        val checkins: List<CheckInEntity> = emptyList(), val events: List<QuickEventEntity> = emptyList(), val weekly: List<WeeklyEntity> = emptyList(),
        val products: List<ProductEntity> = emptyList(), val custom: List<CustomFoodEntity> = emptyList(), val corrections: List<CorrectionEntity> = emptyList(),
        val challenges: List<ChallengeEntity> = emptyList(),
    )

    suspend fun backup(context: Context, repo: Repository): File = withContext(Dispatchers.IO) {
        val db = repo.db
        val b = Backup(
            food = db.food().allOnce(), bms = db.bm().allOnce(), symptoms = db.symptoms().allOnce(), checkins = db.checkins().allOnce(),
            events = db.events().allOnce(), weekly = db.weekly().allOnce(), products = db.products().allOnce(), custom = db.custom().allOnce(),
            corrections = db.corrections().allOnce(), challenges = db.challenges().allOnce(),
        )
        File(dir(context), "pickless-backup-${LocalDate.now()}.json").apply { writeText(json.encodeToString(Backup.serializer(), b)) }
    }

    /** Adds everything from a backup that isn't already on the phone. Returns a summary. */
    suspend fun restore(context: Context, repo: Repository, uri: Uri): String = withContext(Dispatchers.IO) {
        val text = context.contentResolver.openInputStream(uri)?.use { it.readBytes().decodeToString() } ?: return@withContext "Couldn't read the file."
        val b = runCatching { json.decodeFromString(Backup.serializer(), text) }.getOrNull() ?: return@withContext "That isn't a Pickless backup."
        val db = repo.db
        val haveFood = db.food().allOnce().map { it.time to it.name }.toSet()
        b.food.filter { (it.time to it.name) !in haveFood }.forEach { db.food().insert(it.copy(id = 0)) }
        val haveBm = db.bm().allOnce().map { it.time }.toSet()
        b.bms.filter { it.time !in haveBm }.forEach { db.bm().insert(it.copy(id = 0)) }
        val haveSym = db.symptoms().allOnce().map { it.time to it.type }.toSet()
        b.symptoms.filter { (it.time to it.type) !in haveSym }.forEach { db.symptoms().insert(it.copy(id = 0)) }
        b.checkins.forEach { if (db.checkins().get(it.epochDay) == null) db.checkins().upsert(it) }
        val haveEv = db.events().allOnce().map { it.time to it.type }.toSet()
        b.events.filter { (it.time to it.type) !in haveEv }.forEach { db.events().insert(it.copy(id = 0)) }
        b.weekly.forEach { db.weekly().upsert(it) }
        b.products.forEach { if (db.products().get(it.key) == null) db.products().upsert(it) }
        val haveCustom = db.custom().allOnce().map { it.name }.toSet()
        b.custom.filter { it.name !in haveCustom }.forEach { db.custom().insert(it.copy(id = 0)) }
        b.corrections.forEach { db.corrections().upsert(it) }
        "Restored ${b.food.size} foods, ${b.bms.size} BMs, ${b.symptoms.size} symptoms and ${b.checkins.size} check-ins (skipping duplicates)."
    }
}
