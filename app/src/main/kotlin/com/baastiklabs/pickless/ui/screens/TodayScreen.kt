package com.baastiklabs.pickless.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.LocalCafe
import androidx.compose.material.icons.filled.LocalDrink
import androidx.compose.material.icons.filled.QrCodeScanner
import androidx.compose.material.icons.filled.Restaurant
import androidx.compose.material.icons.filled.SentimentDissatisfied
import androidx.compose.material.icons.filled.WaterDrop
import androidx.compose.material.icons.filled.Wc
import androidx.compose.material3.AssistChip
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.SnackbarResult
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.unit.dp
import com.baastiklabs.pickless.core.analysis.Verdict
import com.baastiklabs.pickless.core.coach.Coach
import com.baastiklabs.pickless.core.coach.Phase
import com.baastiklabs.pickless.core.detective.Tier
import com.baastiklabs.pickless.data.QuickEventEntity
import com.baastiklabs.pickless.ui.components.Banner
import com.baastiklabs.pickless.ui.components.Hint
import com.baastiklabs.pickless.ui.components.InfoCard
import com.baastiklabs.pickless.ui.components.Pill
import com.baastiklabs.pickless.ui.components.SectionTitle
import com.baastiklabs.pickless.ui.components.VerdictPill
import com.baastiklabs.pickless.ui.components.bristolNames
import com.baastiklabs.pickless.ui.components.severityLabels
import com.baastiklabs.pickless.ui.components.timeText
import com.baastiklabs.pickless.ui.theme.Pal
import kotlinx.coroutines.launch
import java.time.LocalDate
import java.time.LocalTime
import java.time.format.DateTimeFormatter

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun TodayScreen() {
    val c = LocalContainer.current
    val nav = LocalNav.current
    val snack = LocalSnack.current
    val scope = rememberUiScope()
    val repo = c.repo
    val today = repo.today()
    val start = repo.startOf(today)
    val end = repo.startOf(today + 1)
    val foods by c.db.food().between(start, end).collectAsState(initial = emptyList())
    val bms by c.db.bm().all().collectAsState(initial = emptyList())
    val symptoms by c.db.symptoms().all().collectAsState(initial = emptyList())
    val events by c.db.events().all().collectAsState(initial = emptyList())
    val checkin by c.db.checkins().observe(today).collectAsState(initial = null)
    val report by c.insights.report.collectAsState()
    val prefs by c.settings.prefs.collectAsState(initial = null)
    val challenge by c.db.challenges().active().collectAsState(initial = null)
    LaunchedEffect(Unit) { if (c.insights.report.value == null) runCatching { c.insights.refresh() } }

    val todaysBms = bms.filter { it.time in start until end }
    val todaysSymptoms = symptoms.filter { it.time in start until end }
    val todaysEvents = events.filter { it.time in start until end }
    val hour = LocalTime.now().hour
    val greeting = when (hour) { in 5..11 -> "Good morning"; in 12..17 -> "Good afternoon"; else -> "Good evening" }

    fun quick(type: String, label: String) = scope.launch {
        val id = c.db.events().insert(QuickEventEntity(time = System.currentTimeMillis(), type = type))
        c.insights.refreshSoon()
        if (snack.showSnackbar("$label logged", actionLabel = "Undo") == SnackbarResult.ActionPerformed) {
            c.db.events().delete(QuickEventEntity(id = id, time = 0, type = type))
        }
    }

    LazyColumn(Modifier.statusBarsPadding().padding(horizontal = 16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        item {
            Column(Modifier.padding(top = 18.dp, bottom = 4.dp)) {
                Text(LocalDate.now().format(DateTimeFormatter.ofPattern("EEEE, MMMM d")), style = MaterialTheme.typography.labelLarge, color = MaterialTheme.colorScheme.primary)
                Text(greeting, style = MaterialTheme.typography.headlineLarge)
            }
        }
        item {
            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                BigAction("Log BM", Icons.Filled.Wc, Modifier.weight(1f)) { nav.navigate("bm") }
                BigAction("Symptom", Icons.Filled.SentimentDissatisfied, Modifier.weight(1f)) { nav.navigate("symptom") }
            }
        }
        item {
            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                BigAction("Log food", Icons.Filled.Restaurant, Modifier.weight(1f)) { nav.navigate("check") }
                BigAction("Scan", Icons.Filled.QrCodeScanner, Modifier.weight(1f)) { nav.navigate("scan?mode=product") }
            }
        }
        item {
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                AssistChip(onClick = { quick("coffee", "Coffee") }, label = { Text("+ Coffee") }, leadingIcon = { Icon(Icons.Filled.LocalCafe, null, Modifier.size(18.dp)) })
                AssistChip(onClick = { quick("alcohol", "Drink") }, label = { Text("+ Alcohol") }, leadingIcon = { Icon(Icons.Filled.LocalDrink, null, Modifier.size(18.dp)) })
                AssistChip(onClick = { quick("water", "Water") }, label = { Text("+ Water") }, leadingIcon = { Icon(Icons.Filled.WaterDrop, null, Modifier.size(18.dp)) })
                if (prefs?.nicotineTracking == true) AssistChip(onClick = { quick("nicotine", "Extra pouch") }, label = { Text("+ Pouch") }, leadingIcon = { Icon(Icons.Filled.Add, null, Modifier.size(18.dp)) })
            }
        }

        // The latest BM was bad: explain it.
        val lastBad = todaysBms.firstOrNull()?.takeIf { prefs?.badBmRule?.isBad(com.baastiklabs.pickless.core.detective.BmLog(it.id, it.time, it.bristol, it.urgency, it.accident)) == true }
        if (lastBad != null) item {
            Banner("Why was that one bad?", "See the likely culprits from the last 3 days.", Pal.signals.caution, Pal.signals.cautionBg) { nav.navigate("explain/${lastBad.id}") }
        }

        report?.let { r ->
            item {
                val top = r.suspects.firstOrNull { it.tier.rank >= Tier.WEAK.rank }
                val q = r.quality
                InfoCard(onClick = { nav.navigate("insights") }) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text("Trigger detective", style = MaterialTheme.typography.titleMedium, modifier = Modifier.weight(1f))
                        if (!q.solid) Pill("Early read", Pal.signals.caution, Pal.signals.cautionBg)
                    }
                    Spacer(Modifier.height(4.dp))
                    Text(
                        when {
                            top != null -> "Top suspect: ${top.label} (${top.tier.label.lowercase()}). Bad BMs ${"%.1f".format(top.ratio)}× more likely after it."
                            r.analyzedDays == 0 -> "Log meals and BMs and the detective starts working right away."
                            else -> "No clear suspect yet after ${r.analyzedDays} day${if (r.analyzedDays == 1) "" else "s"}. Keep logging."
                        },
                        style = MaterialTheme.typography.bodyMedium,
                    )
                    if (!q.solid) Hint("Still learning, so this is a little inaccurate for now. To firm it up: ${q.missing.first()}.")
                }
            }
        }

        challenge?.let { e ->
            val p = Coach.protocol(e.protocolId)
            if (p != null) item {
                val st = Coach.status(c.coach.toModel(e), p, today)
                InfoCard(onClick = { nav.navigate("coach") }, container = MaterialTheme.colorScheme.tertiaryContainer) {
                    Text("Food test: ${p.title}", style = MaterialTheme.typography.titleMedium)
                    Text(st.message, style = MaterialTheme.typography.bodyMedium)
                    if (st.phase == Phase.DOSING && today !in c.coach.toModel(e).dosesTaken) {
                        TextButton(onClick = { scope.launch { c.coach.logDose(e); snack.showSnackbar("Dose logged") } }) { Text("I had today's dose") }
                    }
                }
            }
        }

        if (checkin == null) item {
            InfoCard(onClick = { nav.navigate("checkin") }, container = MaterialTheme.colorScheme.primaryContainer) {
                Text(if (hour >= 17) "Evening check-in" else "Daily check-in", style = MaterialTheme.typography.titleMedium)
                Text("Sleep, stress, nicotine and anything unusual. About 20 seconds.", style = MaterialTheme.typography.bodyMedium)
            }
        }

        item { SectionTitle("Today") }
        val entries = buildList {
            foods.forEach { add(Triple(it.time, "food", it.id)) }
            todaysBms.forEach { add(Triple(it.time, "bm", it.id)) }
            todaysSymptoms.forEach { add(Triple(it.time, "symptom", it.id)) }
            todaysEvents.forEach { add(Triple(it.time, "event", it.id)) }
        }.sortedByDescending { it.first }
        if (entries.isEmpty()) item { Hint("Nothing logged yet today. Tap Log food or Scan when you eat, and Log BM after each one.") }
        items(entries, key = { "${it.second}-${it.third}" }) { (time, kind, id) ->
            when (kind) {
                "food" -> foods.first { it.id == id }.let { f ->
                    TimelineRow(timeText(time), f.name, "${f.portionLabel} · ${f.meal}", runCatching { Verdict.valueOf(f.verdict) }.getOrNull()) { nav.navigate("result/${enc(f.ref)}") }
                }
                "bm" -> todaysBms.first { it.id == id }.let { b ->
                    TimelineRow(timeText(time), "BM · type ${b.bristol}", bristolNames[b.bristol] ?: "", null) { nav.navigate("bm/${b.id}") }
                }
                "symptom" -> todaysSymptoms.first { it.id == id }.let { s ->
                    TimelineRow(timeText(time), s.type.replaceFirstChar { it.uppercase() }, severityLabels.getOrElse(s.severity) { "" }, null) {}
                }
                else -> todaysEvents.first { it.id == id }.let { e ->
                    TimelineRow(timeText(time), e.type.replaceFirstChar { it.uppercase() }, "quick log", null) {}
                }
            }
        }
        item { Spacer(Modifier.height(24.dp)) }
    }
}

@Composable
fun BigAction(label: String, icon: ImageVector, modifier: Modifier = Modifier, onClick: () -> Unit) {
    FilledTonalButton(onClick = onClick, modifier = modifier.height(64.dp), shape = RoundedCornerShape(18.dp)) {
        Icon(icon, null)
        Spacer(Modifier.width(10.dp))
        Text(label, style = MaterialTheme.typography.titleMedium)
    }
}

@Composable
fun TimelineRow(time: String, title: String, detail: String, verdict: Verdict?, onClick: () -> Unit) {
    InfoCard(onClick = onClick, container = MaterialTheme.colorScheme.surfaceContainerLow) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(time, style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.width(64.dp))
            Column(Modifier.weight(1f)) {
                Text(title, style = MaterialTheme.typography.bodyLarge)
                if (detail.isNotBlank()) Hint(detail)
            }
            verdict?.let { VerdictPill(it) }
        }
    }
}

@Suppress("unused")
private fun fillWidth() = Modifier.fillMaxWidth()
