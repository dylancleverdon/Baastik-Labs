package com.baastiklabs.pickless.ui.screens

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowLeft
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.baastiklabs.pickless.core.detective.BmLog
import com.baastiklabs.pickless.core.detective.Explanation
import com.baastiklabs.pickless.core.detective.FeatureKind
import com.baastiklabs.pickless.core.detective.Suspect
import com.baastiklabs.pickless.core.detective.Tier
import com.baastiklabs.pickless.core.kb.AvoidItem
import com.baastiklabs.pickless.core.kb.Categories
import com.baastiklabs.pickless.ui.components.Banner
import com.baastiklabs.pickless.ui.components.Hint
import com.baastiklabs.pickless.ui.components.InfoCard
import com.baastiklabs.pickless.ui.components.KeyValue
import com.baastiklabs.pickless.ui.components.Pill
import com.baastiklabs.pickless.ui.components.SectionTitle
import com.baastiklabs.pickless.ui.components.bristolNames
import com.baastiklabs.pickless.ui.components.severityLabels
import com.baastiklabs.pickless.ui.components.timeText
import com.baastiklabs.pickless.ui.theme.Pal
import kotlinx.coroutines.launch
import java.time.LocalDate
import java.time.YearMonth
import java.time.format.DateTimeFormatter

@Composable
fun HistoryScreen() {
    val c = LocalContainer.current
    val nav = LocalNav.current
    var month by rememberSaveable { mutableStateOf(YearMonth.now().toString()) }
    val ym = YearMonth.parse(month)
    val bms by c.db.bm().all().collectAsState(initial = emptyList())
    val foods by c.db.food().all().collectAsState(initial = emptyList())
    val checkins by c.db.checkins().all().collectAsState(initial = emptyList())
    val prefs by c.settings.prefs.collectAsState(initial = null)
    val rule = prefs?.badBmRule
    val badByDay = bms.filter { rule?.isBad(BmLog(it.id, it.time, it.bristol, it.urgency, it.accident)) == true }.groupingBy { c.repo.epochDay(it.time) }.eachCount()
    val bmByDay = bms.groupingBy { c.repo.epochDay(it.time) }.eachCount()
    val loggedDays = foods.map { c.repo.epochDay(it.time) }.toSet()
    val overall = checkins.associate { it.epochDay to it.overall }
    LazyColumn(Modifier.statusBarsPadding().padding(horizontal = 16.dp)) {
        item {
            Row(Modifier.padding(top = 18.dp), verticalAlignment = Alignment.CenterVertically) {
                Text("History", style = MaterialTheme.typography.headlineMedium, modifier = Modifier.weight(1f))
                IconButton(onClick = { month = ym.minusMonths(1).toString() }) { Icon(Icons.AutoMirrored.Filled.KeyboardArrowLeft, "Previous month") }
                Text(ym.format(DateTimeFormatter.ofPattern("MMM yyyy")), style = MaterialTheme.typography.titleMedium)
                IconButton(onClick = { month = ym.plusMonths(1).toString() }) { Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, "Next month") }
            }
        }
        item {
            Row { listOf("M", "T", "W", "T", "F", "S", "S").forEach { Text(it, Modifier.weight(1f), textAlign = TextAlign.Center, style = MaterialTheme.typography.labelMedium) } }
            val first = ym.atDay(1)
            val offset = first.dayOfWeek.value - 1
            val days = ym.lengthOfMonth()
            val cells = (0 until offset).map { null } + (1..days).map { ym.atDay(it) }
            cells.chunked(7).forEach { week ->
                Row {
                    (0 until 7).forEach { i ->
                        val date = week.getOrNull(i)
                        Box(Modifier.weight(1f).aspectRatio(1f).padding(3.dp)) {
                            if (date != null) {
                                val d = date.toEpochDay()
                                val bad = badByDay[d] ?: 0
                                val bg = when {
                                    bad >= 2 -> Pal.signals.avoidBg
                                    bad == 1 -> Pal.signals.cautionBg
                                    d in loggedDays || (bmByDay[d] ?: 0) > 0 -> Pal.signals.goodBg
                                    else -> MaterialTheme.colorScheme.surfaceContainer
                                }
                                Column(
                                    Modifier.matchParentSizeCompat().clip(RoundedCornerShape(10.dp)).background(bg).clickable { nav.navigate("day/$d") },
                                    horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.Center,
                                ) {
                                    Text("${date.dayOfMonth}", style = MaterialTheme.typography.labelLarge, color = if (date == LocalDate.now()) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurface)
                                    overall[d]?.let { o -> Text(listOf("", "😣", "🙁", "😐", "🙂", "😄").getOrElse(o) { "" }, style = MaterialTheme.typography.labelMedium) }
                                }
                            }
                        }
                    }
                }
            }
            Hint("Green: logged, no bad BMs. Amber: 1 bad BM. Red: 2 or more.", Modifier.padding(top = 8.dp))
        }
        item { Spacer(Modifier.height(24.dp)) }
    }
}

private fun Modifier.matchParentSizeCompat(): Modifier = this.fillMaxWidth().aspectRatio(1f)

@Composable
fun DayScreen(day: Long) {
    val c = LocalContainer.current
    val nav = LocalNav.current
    val scope = rememberUiScope()
    val start = c.repo.startOf(day)
    val end = c.repo.startOf(day + 1)
    val foods by c.db.food().between(start, end).collectAsState(initial = emptyList())
    val bms by c.db.bm().all().collectAsState(initial = emptyList())
    val symptoms by c.db.symptoms().all().collectAsState(initial = emptyList())
    val checkin by c.db.checkins().observe(day).collectAsState(initial = null)
    Page(LocalDate.ofEpochDay(day).format(DateTimeFormatter.ofPattern("EEEE, MMM d"))) {
        item {
            InfoCard(onClick = { nav.navigate("checkin/$day") }) {
                Text("Check-in", style = MaterialTheme.typography.titleMedium)
                val ci = checkin
                if (ci == null) Hint("Not filled in. Tap to add.") else Text(
                    listOfNotNull(ci.overall?.let { "overall $it/5" }, ci.stress?.let { "stress $it/4" }, ci.sleepQuality?.let { "sleep $it/4" },
                        ci.nicotine?.let { "nicotine $it" }, if (ci.sick) "sick" else null, if (ci.travel) "travel" else null).joinToString(" · "),
                    style = MaterialTheme.typography.bodyMedium,
                )
            }
        }
        item { SectionTitle("Food") }
        foods.forEach { f ->
            item(key = "f${f.id}") {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    TimelineRowCompact(timeText(f.time), f.name, "${f.portionLabel} · ${f.meal}", Modifier.weight(1f)) { nav.navigate("result/${enc(f.ref)}") }
                    TextButton(onClick = { scope.launch { c.db.food().delete(f); c.insights.refreshSoon() } }) { Text("Delete") }
                }
            }
        }
        if (foods.isEmpty()) item { Hint("No food logged.") }
        item { SectionTitle("BMs") }
        bms.filter { it.time in start until end }.forEach { b ->
            item(key = "b${b.id}") { TimelineRowCompact(timeText(b.time), "Type ${b.bristol}", bristolNames[b.bristol] ?: "") { nav.navigate("bm/${b.id}") } }
        }
        item { SectionTitle("Symptoms") }
        symptoms.filter { it.time in start until end }.forEach { s ->
            item(key = "s${s.id}") {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    TimelineRowCompact(timeText(s.time), s.type.replaceFirstChar { it.uppercase() }, severityLabels.getOrElse(s.severity) { "" }, Modifier.weight(1f)) {}
                    TextButton(onClick = { scope.launch { c.db.symptoms().delete(s); c.insights.refreshSoon() } }) { Text("Delete") }
                }
            }
        }
    }
}

@Composable
fun TimelineRowCompact(time: String, title: String, detail: String, modifier: Modifier = Modifier, onClick: () -> Unit) {
    Row(modifier.fillMaxWidth().clip(RoundedCornerShape(12.dp)).clickable(onClick = onClick).padding(vertical = 8.dp, horizontal = 4.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(time, style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.width(64.dp))
        Column(Modifier.weight(1f)) { Text(title, style = MaterialTheme.typography.bodyLarge); if (detail.isNotBlank()) Hint(detail) }
    }
}

@Composable
fun TierPill(t: Tier) {
    val s = Pal.signals
    val (fg, bg) = when (t) { Tier.STRONG -> s.avoid to s.avoidBg; Tier.MODERATE -> s.caution to s.cautionBg; else -> s.unknown to s.unknownBg }
    Pill(t.label, fg, bg)
}

@Composable
fun SuspectCard(s: Suspect, onClick: () -> Unit) {
    InfoCard(onClick = onClick) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(s.label, style = MaterialTheme.typography.titleMedium, modifier = Modifier.weight(1f))
            TierPill(s.tier)
        }
        Text("${s.outcome.replaceFirstChar { it.uppercase() }} ${"%.1f".format(s.ratio)}× more likely after it · seen ${s.exposedDays} ${if (s.outcome == "bad BMs") "days" else "times"}", style = MaterialTheme.typography.bodyMedium)
        s.typicalLagHours?.let { Hint("Usually about ${it.toInt()} hours later") }
        if (s.includes.isNotEmpty()) Hint("Explains: ${s.includes.joinToString()}")
        if (s.tangledWith.isNotEmpty()) Hint("Often eaten with: ${s.tangledWith.joinToString()}")
    }
}

@Composable
fun InsightsScreen() {
    val c = LocalContainer.current
    val nav = LocalNav.current
    val scope = rememberUiScope()
    val report by c.insights.report.collectAsState()
    val busy by c.insights.busy.collectAsState()
    val weekly by c.db.weekly().all().collectAsState(initial = emptyList())
    LaunchedEffect(Unit) { runCatching { c.insights.refresh() } }
    LazyColumn(Modifier.statusBarsPadding().padding(horizontal = 16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        item {
            Row(Modifier.padding(top = 18.dp), verticalAlignment = Alignment.CenterVertically) {
                Text("Trigger detective", style = MaterialTheme.typography.headlineMedium, modifier = Modifier.weight(1f))
                if (busy) CircularProgressIndicator(Modifier.width(22.dp).height(22.dp), strokeWidth = 2.dp)
                else IconButton(onClick = { scope.launch { runCatching { c.insights.refresh() } } }) { Icon(Icons.Filled.Refresh, "Refresh") }
            }
        }
        val r = report
        if (r == null) { item { Hint("Working it out…") }; return@LazyColumn }
        if (!r.quality.solid) item {
            Banner("Early read: still learning", "This is a little inaccurate right now and will firm up as you log. To get there: ${r.quality.missing.joinToString("; ")}.", Pal.signals.caution, Pal.signals.cautionBg)
        }
        item {
            Hint(if (r.analyzedDays == 0) "Log what you eat and rate each BM: the detective starts comparing from the first full day."
                else "Based on ${r.analyzedDays} logged day${if (r.analyzedDays == 1) "" else "s"}. On a typical day there's a ${"%.0f".format(r.baselineRate * 100)}% chance of a bad BM in the next day or so. Suspects are things that raise that.")
        }
        r.nextTest?.let { t -> item { Banner("Next best step", t.why, Pal.signals.good, Pal.signals.goodBg) { nav.navigate("coach") } } }
        item { SectionTitle("Suspects") }
        if (r.suspects.isEmpty()) item { Hint("Nothing stands out yet. That's normal early on, or it's good news.") }
        r.suspects.forEach { s -> item(key = "s${s.key}") { SuspectCard(s) { nav.navigate("suspect/${enc(s.key)}") } } }
        if (r.symptomSuspects.isNotEmpty()) {
            item { SectionTitle("Linked to bloating, pain or gas") }
            r.symptomSuspects.forEach { s -> item(key = "y${s.key}") { SuspectCard(s) { nav.navigate("suspect/${enc("sym:" + s.key)}") } } }
        }
        if (r.lifestyle.isNotEmpty()) {
            item { SectionTitle("Not food") }
            r.lifestyle.forEach { s -> item(key = "l${s.key}") { SuspectCard(s) { nav.navigate("suspect/${enc(s.key)}") } } }
        }
        if (r.safe.isNotEmpty()) item {
            SectionTitle("Safe for you so far")
            InfoCard { r.safe.forEach { KeyValue(it.label, "${it.exposedDays} days, ${it.badAfter} bad") } }
        }
        item {
            SectionTitle("BM pattern")
            val p = r.bmPattern
            InfoCard {
                Text(p.pattern, style = MaterialTheme.typography.titleSmall)
                Spacer(Modifier.height(8.dp))
                BristolBars(p.byType)
                Spacer(Modifier.height(8.dp))
                KeyValue("BMs per day", "%.1f".format(p.perDay))
                KeyValue("Loose (types 6-7)", "%.0f%%".format(p.looseShare * 100))
                KeyValue("Hard (types 1-2)", "%.0f%%".format(p.hardShare * 100))
                KeyValue("Urgent", "%.0f%%".format(p.urgentShare * 100))
                if (p.accidents > 0) KeyValue("Accidents", "${p.accidents}")
            }
        }
        if (weekly.size >= 2) item {
            SectionTitle("Weekly severity")
            InfoCard { LineChart(weekly.map { it.total.toFloat() }, 500f); Hint("Lower is better. Under 175 is mild.") }
        }
        item {
            Spacer(Modifier.height(8.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedButton(onClick = { nav.navigate("report") }, modifier = Modifier.weight(1f)) { Text("Report for your doctor") }
                OutlinedButton(onClick = { nav.navigate("weekly") }, modifier = Modifier.weight(1f)) { Text("Weekly check-in") }
            }
            Hint("Suspects, not diagnoses: confirm them with a planned food test (Coach).", Modifier.padding(vertical = 12.dp))
        }
    }
}

@Composable
fun BristolBars(byType: Map<Int, Int>) {
    val max = (byType.values.maxOrNull() ?: 0).coerceAtLeast(1)
    val s = Pal.signals
    Row(Modifier.fillMaxWidth().height(110.dp), verticalAlignment = Alignment.Bottom, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        for (t in 1..7) {
            val n = byType[t] ?: 0
            val color = when (t) { 3, 4 -> s.good; 2, 5 -> s.caution; else -> s.avoid }
            Column(Modifier.weight(1f), horizontalAlignment = Alignment.CenterHorizontally) {
                Text("$n", style = MaterialTheme.typography.labelMedium)
                Box(Modifier.fillMaxWidth().height((70.0 * n / max).dp.coerceAtLeast(2.dp)).clip(RoundedCornerShape(6.dp)).background(color))
                Text("$t", style = MaterialTheme.typography.labelMedium)
            }
        }
    }
}

@Composable
fun LineChart(values: List<Float>, max: Float) {
    val color = MaterialTheme.colorScheme.primary
    val grid = MaterialTheme.colorScheme.outlineVariant
    Canvas(Modifier.fillMaxWidth().height(120.dp)) {
        if (values.size < 2) return@Canvas
        val w = size.width
        val h = size.height
        for (i in 0..4) drawLine(grid, Offset(0f, h * i / 4), Offset(w, h * i / 4), 1f)
        val pts = values.mapIndexed { i, v -> Offset(w * i / (values.size - 1), h - h * (v / max).coerceIn(0f, 1f)) }
        for (i in 1 until pts.size) drawLine(color, pts[i - 1], pts[i], 5f)
        pts.forEach { drawCircle(color, 6f, it) }
        drawRect(color.copy(alpha = 0f), Offset.Zero, Size(1f, 1f))
    }
}

@Composable
fun SuspectScreen(key: String) {
    val c = LocalContainer.current
    val nav = LocalNav.current
    val snack = LocalSnack.current
    val scope = rememberUiScope()
    val report by c.insights.report.collectAsState()
    val sym = key.startsWith("sym:")
    val k = key.removePrefix("sym:")
    val s = report?.let { r -> (if (sym) r.symptomSuspects else r.suspects + r.lifestyle).firstOrNull { it.key == k } }
    Page(s?.label ?: "Suspect") {
        if (s == null) { item { Hint("This suspect isn't in the latest report.") }; return@Page }
        item {
            InfoCard {
                Row(verticalAlignment = Alignment.CenterVertically) { Text("Evidence", style = MaterialTheme.typography.titleMedium, modifier = Modifier.weight(1f)); TierPill(s.tier) }
                KeyValue("After it", "${s.badAfter} of ${s.exposedDays} (${"%.0f".format(s.exposedRate * 100)}%)")
                KeyValue("Other times", "%.0f%%".format(s.baselineRate * 100))
                KeyValue("How much more likely", "%.1f×".format(s.ratio))
                KeyValue("Chance it's a real effect", "%.0f%%".format(s.probHarm * 100))
                s.typicalLagHours?.let { KeyValue("Usually", "${it.toInt()} h later") }
                if (s.sameAs.isNotEmpty()) Hint("Same days as: ${s.sameAs.joinToString()}")
                if (s.includes.isNotEmpty()) Hint("Also explains: ${s.includes.joinToString()}")
                if (s.tangledWith.isNotEmpty()) Hint("Often together with: ${s.tangledWith.joinToString()}")
                s.separation?.let { Hint(it) }
            }
        }
        item {
            Spacer(Modifier.height(8.dp))
            val cat = when {
                s.key.startsWith("tag:") -> Categories.byId(s.key.removePrefix("tag:"))
                s.key.startsWith("avoid:cat:") -> Categories.byId(s.key.removePrefix("avoid:cat:"))
                else -> null
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                if (cat != null) OutlinedButton(onClick = {
                    scope.launch {
                        c.settings.updateAvoid { list -> if (list.any { it.categoryId == cat.id }) list else list + AvoidItem.ofCategory(cat.id, c.repo.today()) }
                        snack.showSnackbar("${cat.label} is on your avoid list")
                    }
                }, modifier = Modifier.weight(1f)) { Text("Add to avoid list") }
                if (s.kind == FeatureKind.FOOD || s.kind == FeatureKind.INGREDIENT) {
                    val foodId = s.key.substringAfter(':').takeIf { c.kb[it] != null }
                    if (foodId != null) OutlinedButton(onClick = {
                        scope.launch {
                            c.settings.updateAvoid { list -> if (list.any { it.foodId == foodId }) list else list + AvoidItem.ofFood(c.kb.require(foodId), c.repo.today()) }
                            snack.showSnackbar("Added to your avoid list")
                        }
                    }, modifier = Modifier.weight(1f)) { Text("Avoid it") }
                }
                Button(onClick = { nav.navigate("coach") }, modifier = Modifier.weight(1f)) { Text("Test it") }
            }
        }
        if (s.evidence.isNotEmpty()) {
            item { SectionTitle("Day by day") }
            s.evidence.forEach { e ->
                item {
                    val (fg, bg) = if (e.bad) Pal.signals.avoid to Pal.signals.avoidBg else Pal.signals.good to Pal.signals.goodBg
                    Row(Modifier.fillMaxWidth().padding(vertical = 3.dp).clip(RoundedCornerShape(12.dp)).background(bg).padding(10.dp)) {
                        Column(Modifier.weight(1f)) {
                            Text(LocalDate.ofEpochDay(e.epochDay).format(DateTimeFormatter.ofPattern("EEE MMM d")), style = MaterialTheme.typography.labelLarge)
                            if (e.eaten.isNotEmpty()) Hint(e.eaten.joinToString())
                        }
                        Text(e.outcome, color = fg, style = MaterialTheme.typography.bodySmall)
                    }
                }
            }
        }
    }
}

@Composable
fun ExplainScreen(bmId: Long) {
    val c = LocalContainer.current
    val nav = LocalNav.current
    var e by remember { mutableStateOf<Explanation?>(null) }
    var done by remember { mutableStateOf(false) }
    LaunchedEffect(bmId) { e = c.insights.explain(bmId); done = true }
    Page("Why was that one bad?") {
        val x = e
        if (x == null) { item { if (!done) CircularProgressIndicator(Modifier.padding(24.dp)) else Hint("Couldn't find that BM.") }; return@Page }
        item {
            Banner(if (x.bad) "Type ${x.bm.bristol}" else "Not a bad one", x.summary, if (x.bad) Pal.signals.caution else Pal.signals.good, if (x.bad) Pal.signals.cautionBg else Pal.signals.goodBg)
        }
        if (x.culprits.isNotEmpty()) {
            item { SectionTitle("Likely culprits (last 3 days)") }
            x.culprits.forEachIndexed { i, cu ->
                item {
                    InfoCard {
                        Row { Text("${i + 1}. ${cu.label}", style = MaterialTheme.typography.titleMedium, modifier = Modifier.weight(1f)); cu.hoursBefore?.let { Hint("${it.toInt()} h before") } }
                        Text(cu.reason, style = MaterialTheme.typography.bodyMedium)
                        cu.history?.let { Hint("This ${it}") }
                    }
                }
            }
        }
        if (x.lifestyle.isNotEmpty()) {
            item { SectionTitle("Also going on") }
            x.lifestyle.forEach { l -> item { InfoCard { Text(l.label, style = MaterialTheme.typography.titleSmall); Hint(l.reason) } } }
        }
        item {
            Spacer(Modifier.height(12.dp))
            Hint("These are ranked by how well the timing fits (food usually shows up 6 to 36 hours later), how much FODMAP or avoid-list food was involved, and what the detective has learned so far.")
            Row(Modifier.padding(top = 12.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedButton(onClick = { nav.navigate("insights") }, modifier = Modifier.weight(1f)) { Text("See all suspects") }
                Button(onClick = { nav.popBackStack("today", false) }, modifier = Modifier.weight(1f)) { Text("Done") }
            }
        }
    }
}
