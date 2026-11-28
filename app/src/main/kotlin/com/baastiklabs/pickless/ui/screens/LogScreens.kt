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
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
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
import androidx.compose.ui.unit.dp
import com.baastiklabs.pickless.core.detective.BmLog
import com.baastiklabs.pickless.core.detective.Nicotine
import com.baastiklabs.pickless.data.BmEntity
import com.baastiklabs.pickless.data.CheckInEntity
import com.baastiklabs.pickless.data.SymptomEntity
import com.baastiklabs.pickless.data.WeeklyEntity
import com.baastiklabs.pickless.health.Health
import com.baastiklabs.pickless.ui.components.BristolPicker
import com.baastiklabs.pickless.ui.components.ChoiceRow
import com.baastiklabs.pickless.ui.components.Hint
import com.baastiklabs.pickless.ui.components.InfoCard
import com.baastiklabs.pickless.ui.components.SectionTitle
import com.baastiklabs.pickless.ui.components.SeverityPicker
import com.baastiklabs.pickless.ui.components.Stepper
import com.baastiklabs.pickless.ui.components.TimeField
import com.baastiklabs.pickless.ui.theme.Pal
import kotlinx.coroutines.launch
import java.time.LocalDate
import java.time.format.DateTimeFormatter

@Composable
fun ToggleRow(label: String, checked: Boolean, onChange: (Boolean) -> Unit, hint: String? = null) {
    Row(Modifier.fillMaxWidth().padding(vertical = 4.dp), verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f)) {
            Text(label, style = MaterialTheme.typography.bodyLarge)
            hint?.let { Hint(it) }
        }
        Switch(checked, onChange)
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun BmScreen(id: Long? = null) {
    val c = LocalContainer.current
    val nav = LocalNav.current
    val snack = LocalSnack.current
    val scope = rememberUiScope()
    var existing by remember { mutableStateOf<BmEntity?>(null) }
    var type by rememberSaveable { mutableStateOf<Int?>(null) }
    var time by rememberSaveable { mutableStateOf(System.currentTimeMillis()) }
    var urgency by rememberSaveable { mutableStateOf(0) }
    var pain by rememberSaveable { mutableStateOf(0) }
    var straining by rememberSaveable { mutableStateOf(false) }
    var incomplete by rememberSaveable { mutableStateOf(false) }
    var accident by rememberSaveable { mutableStateOf(false) }
    var blood by rememberSaveable { mutableStateOf(false) }
    var mucus by rememberSaveable { mutableStateOf(false) }
    var color by rememberSaveable { mutableStateOf("normal") }
    var details by rememberSaveable { mutableStateOf(false) }
    var redFlag by remember { mutableStateOf<Long?>(null) }
    val prefs by c.settings.prefs.collectAsState(initial = null)
    LaunchedEffect(id) {
        val e = id?.let { c.db.bm().byId(it) } ?: return@LaunchedEffect
        existing = e; type = e.bristol; time = e.time; urgency = e.urgency; pain = e.pain; straining = e.straining
        incomplete = e.incomplete; accident = e.accident; blood = e.blood; mucus = e.mucus; color = e.color ?: "normal"; details = true
    }
    Page(if (existing != null) "Edit BM" else "Log a BM") {
        item {
            Hint("Pick the closest shape (Bristol Stool Scale). 3 and 4 are ideal.", Modifier.padding(bottom = 10.dp))
            BristolPicker(type, { type = it })
            Spacer(Modifier.height(12.dp))
            TimeField(time, { time = it })
            TextButton(onClick = { details = !details }) { Text(if (details) "Hide details" else "Add details (urgency, pain…)") }
        }
        if (details) item {
            InfoCard {
                Text("Urgency", style = MaterialTheme.typography.titleSmall)
                ChoiceRow(listOf(0 to "None", 1 to "Some", 2 to "Rushed", 3 to "Couldn't wait"), urgency, { urgency = it })
                Text("Pain", style = MaterialTheme.typography.titleSmall, modifier = Modifier.padding(top = 8.dp))
                SeverityPicker(pain, { pain = it })
                ToggleRow("Straining", straining, { straining = it })
                ToggleRow("Felt incomplete", incomplete, { incomplete = it })
                ToggleRow("Accident", accident, { accident = it })
                ToggleRow("Mucus", mucus, { mucus = it })
                ToggleRow("Blood", blood, { blood = it })
                Text("Color", style = MaterialTheme.typography.titleSmall, modifier = Modifier.padding(top = 8.dp))
                ChoiceRow(listOf("normal" to "Normal", "black" to "Black / tarry", "pale" to "Pale / clay", "red" to "Red"), color, { color = it })
            }
        }
        item {
            Spacer(Modifier.height(14.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                if (existing != null) OutlinedButton(onClick = { scope.launch { c.db.bm().delete(existing!!); c.insights.refreshSoon(); nav.popBackStack() } }, modifier = Modifier.weight(1f)) { Text("Delete") }
                Button(enabled = type != null, onClick = {
                    scope.launch {
                        val e = BmEntity(existing?.id ?: 0, time, type!!, urgency, pain, straining, incomplete, accident, blood, mucus, color.takeIf { it != "normal" })
                        val newId = if (existing != null) { c.db.bm().update(e); e.id } else c.db.bm().insert(e)
                        c.insights.refreshSoon()
                        val flagged = blood || color in setOf("black", "pale", "red")
                        val bad = prefs?.badBmRule?.isBad(BmLog(newId, time, type!!, urgency, accident)) == true
                        when {
                            flagged -> redFlag = newId
                            bad -> nav.navigate("explain/$newId") { popUpTo("today") }
                            else -> { snack.showSnackbar("Logged type ${type}"); nav.popBackStack() }
                        }
                    }
                }, modifier = Modifier.weight(1f).height(52.dp)) { Text("Save") }
            }
        }
    }
    redFlag?.let { bmId ->
        AlertDialog(
            onDismissRequest = { redFlag = null; nav.popBackStack() },
            title = { Text("Worth telling a doctor") },
            text = { Text("Blood, black, pale or red stool isn't something to analyze with an app, and it isn't typical IBS. Please contact your doctor, soon if it keeps happening, and urgently if there's a lot of blood or you feel unwell.") },
            confirmButton = { TextButton(onClick = { redFlag = null; nav.navigate("explain/$bmId") { popUpTo("today") } }) { Text("OK") } },
        )
    }
}

val symptomTypes = listOf("bloating", "pain", "gas", "cramps", "nausea", "reflux", "fatigue", "headache", "brain fog", "other")

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun SymptomScreen() {
    val c = LocalContainer.current
    val nav = LocalNav.current
    val snack = LocalSnack.current
    val scope = rememberUiScope()
    var chosen by remember { mutableStateOf(setOf<String>()) }
    var severity by rememberSaveable { mutableStateOf(2) }
    var time by rememberSaveable { mutableStateOf(System.currentTimeMillis()) }
    var note by rememberSaveable { mutableStateOf("") }
    Page("Log a symptom") {
        item {
            SectionTitle("What")
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                symptomTypes.forEach { t -> FilterChip(selected = t in chosen, onClick = { chosen = if (t in chosen) chosen - t else chosen + t }, label = { Text(t.replaceFirstChar { it.uppercase() }) }) }
            }
            SectionTitle("How bad")
            SeverityPicker(severity, { severity = it })
            SectionTitle("When")
            TimeField(time, { time = it })
            OutlinedTextField(note, { note = it }, label = { Text("Note (optional)") }, modifier = Modifier.fillMaxWidth().padding(top = 10.dp))
            Spacer(Modifier.height(16.dp))
            Button(enabled = chosen.isNotEmpty(), onClick = {
                scope.launch {
                    chosen.forEach { c.db.symptoms().insert(SymptomEntity(time = time, type = it, severity = severity, note = note.ifBlank { null })) }
                    c.insights.refreshSoon()
                    snack.showSnackbar("Logged")
                    nav.popBackStack()
                }
            }, modifier = Modifier.fillMaxWidth().height(52.dp)) { Text("Save") }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun CheckInScreen(day: Long?) {
    val c = LocalContainer.current
    val nav = LocalNav.current
    val snack = LocalSnack.current
    val ctx = androidx.compose.ui.platform.LocalContext.current
    val scope = rememberUiScope()
    val d = day ?: c.repo.today()
    val prefs by c.settings.prefs.collectAsState(initial = null)
    var e by remember { mutableStateOf(CheckInEntity(d, nicotine = "usual")) }
    var loaded by remember { mutableStateOf(false) }
    var bmCount by remember { mutableStateOf(0) }
    var sleepFromHealth by remember { mutableStateOf<String?>(null) }
    LaunchedEffect(d) {
        c.db.checkins().get(d)?.let { e = it }
        bmCount = c.db.bm().allOnce().count { c.repo.epochDay(it.time) == d }
        loaded = true
        if (prefs?.healthConnect == true && e.sleepHours == null) {
            Health.lastNight(ctx, d)?.let { s -> e = e.copy(sleepHours = s.hours, exercise = e.exercise ?: s.exerciseLevel, period = e.period ?: s.period); sleepFromHealth = "From Health Connect" }
        }
    }
    val p = prefs ?: return
    Page("Check-in · ${LocalDate.ofEpochDay(d).format(DateTimeFormatter.ofPattern("EEE MMM d"))}") {
        if (!loaded) return@Page
        item {
            SectionTitle("Overall gut day")
            ChoiceRow(listOf(1 to "Awful", 2 to "Bad", 3 to "OK", 4 to "Good", 5 to "Great"), e.overall, { e = e.copy(overall = it) })
            SectionTitle("Stress")
            ChoiceRow(listOf(0 to "None", 1 to "A little", 2 to "Some", 3 to "High", 4 to "Very high"), e.stress, { e = e.copy(stress = it) })
            SectionTitle("Sleep last night")
            ChoiceRow(listOf(0 to "Terrible", 1 to "Poor", 2 to "OK", 3 to "Good", 4 to "Great"), e.sleepQuality, { e = e.copy(sleepQuality = it) })
            Stepper((e.sleepHours ?: 7.0).toInt(), { e = e.copy(sleepHours = it.toDouble()) }, "Hours of sleep${sleepFromHealth?.let { " ($it)" } ?: ""}", max = 14)
            if (p.nicotineTracking) {
                SectionTitle("Nicotine")
                ChoiceRow(Nicotine.entries.reversed().map { it.id to it.label }, e.nicotine ?: "usual", { e = e.copy(nicotine = it) })
                Hint(if (p.nicotineUsual > 0) "Usual is about ${p.nicotineUsual} pouches a day. Only change this on days that were different." else "Leave it on Usual unless today was different.")
            }
            SectionTitle("Drinks")
            Stepper(e.alcohol ?: 0, { e = e.copy(alcohol = it) }, "Alcoholic drinks")
            Stepper(e.caffeine ?: 0, { e = e.copy(caffeine = it) }, "Coffee, tea or energy drinks")
            SectionTitle("Exercise")
            ChoiceRow(listOf(0 to "None", 1 to "Light", 2 to "Moderate", 3 to "Hard"), e.exercise, { e = e.copy(exercise = it) })
            if (p.cycleTracking) ToggleRow("Period", e.period == true, { e = e.copy(period = it) })
            SectionTitle("Medicine & supplements")
            FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                val taken = e.meds.split(',').filter { it.isNotBlank() }.toSet()
                p.meds.forEach { m -> FilterChip(selected = m in taken, onClick = { e = e.copy(meds = (if (m in taken) taken - m else taken + m).joinToString(",")) }, label = { Text(m) }) }
            }
            SectionTitle("Anything unusual")
            ToggleRow("Sick today", e.sick, { e = e.copy(sick = it) }, "Sick and travel days are left out of the detective")
            ToggleRow("Traveling", e.travel, { e = e.copy(travel = it) })
            if (bmCount == 0) ToggleRow("No BM today", e.noBm == true, { e = e.copy(noBm = it) }, "A day with no BM is useful information too")
            OutlinedTextField(e.note ?: "", { e = e.copy(note = it.ifBlank { null }) }, label = { Text("Note (optional)") }, modifier = Modifier.fillMaxWidth().padding(top = 8.dp))
            Spacer(Modifier.height(16.dp))
            Button(onClick = {
                scope.launch {
                    c.db.checkins().upsert(e.copy(updatedAt = System.currentTimeMillis()))
                    c.insights.refreshSoon()
                    snack.showSnackbar("Check-in saved")
                    nav.popBackStack()
                }
            }, modifier = Modifier.fillMaxWidth().height(52.dp)) { Text("Save check-in") }
        }
    }
}

@Composable
fun WeeklyScreen() {
    val c = LocalContainer.current
    val nav = LocalNav.current
    val snack = LocalSnack.current
    val scope = rememberUiScope()
    val history by c.db.weekly().all().collectAsState(initial = emptyList())
    var pain by rememberSaveable { mutableStateOf(0f) }
    var painDays by rememberSaveable { mutableStateOf(0f) }
    var bloat by rememberSaveable { mutableStateOf(0f) }
    var dis by rememberSaveable { mutableStateOf(0f) }
    var life by rememberSaveable { mutableStateOf(0f) }
    @Composable
    fun Q(title: String, left: String, right: String, value: Float, max: Float, onChange: (Float) -> Unit) {
        Text(title, style = MaterialTheme.typography.titleSmall, modifier = Modifier.padding(top = 14.dp))
        Slider(value, onChange, valueRange = 0f..max, steps = if (max == 10f) 9 else 0)
        Row { Hint(left, Modifier.weight(1f)); Hint(right) }
    }
    val total = (pain + painDays * 10 + bloat + dis + life).toInt()
    Page("Weekly check-in") {
        item {
            Hint("Five questions about the last 10 days, scored the way GI clinics score IBS severity (0-500).")
            Q("How bad was your stomach pain?", "None", "Very severe", pain, 100f) { pain = it }
            Q("On how many of the last 10 days did you have pain? (${painDays.toInt()})", "0 days", "10 days", painDays, 10f) { painDays = it }
            Q("How bad was bloating or swelling?", "None", "Very severe", bloat, 100f) { bloat = it }
            Q("How unhappy are you with your bowel habits?", "Not at all", "Very", dis, 100f) { dis = it }
            Q("How much did IBS get in the way of life?", "Not at all", "Completely", life, 100f) { life = it }
            val band = when { total < 75 -> "in remission range"; total < 175 -> "mild"; total < 300 -> "moderate"; else -> "severe" }
            val tone = when { total < 175 -> Pal.signals.good; total < 300 -> Pal.signals.caution; else -> Pal.signals.avoid }
            Text("Score: $total ($band)", style = MaterialTheme.typography.titleLarge, color = tone, modifier = Modifier.padding(top = 16.dp))
            Spacer(Modifier.height(12.dp))
            Button(onClick = {
                scope.launch {
                    c.db.weekly().upsert(WeeklyEntity(c.repo.today(), pain.toInt(), painDays.toInt(), bloat.toInt(), dis.toInt(), life.toInt()))
                    snack.showSnackbar("Saved: $total")
                    nav.popBackStack()
                }
            }, modifier = Modifier.fillMaxWidth().height(52.dp)) { Text("Save") }
        }
        if (history.isNotEmpty()) {
            item { SectionTitle("Past scores") }
            history.takeLast(12).reversed().forEach { w ->
                item { Row(Modifier.padding(vertical = 4.dp)) { Text(LocalDate.ofEpochDay(w.epochDay).format(DateTimeFormatter.ofPattern("MMM d")), modifier = Modifier.weight(1f)); Text("${w.total}") } }
            }
        }
    }
}
