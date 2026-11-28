package com.baastiklabs.pickless.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.baastiklabs.pickless.core.coach.ChallengeResult
import com.baastiklabs.pickless.core.coach.Coach
import com.baastiklabs.pickless.core.coach.Phase
import com.baastiklabs.pickless.core.coach.Protocol
import com.baastiklabs.pickless.core.fodmap.FodmapGroup
import com.baastiklabs.pickless.core.kb.AvoidItem
import com.baastiklabs.pickless.core.kb.AvoidStatus
import com.baastiklabs.pickless.ui.components.Banner
import com.baastiklabs.pickless.ui.components.ChoiceRow
import com.baastiklabs.pickless.ui.components.Hint
import com.baastiklabs.pickless.ui.components.InfoCard
import com.baastiklabs.pickless.ui.components.KeyValue
import com.baastiklabs.pickless.ui.components.Pill
import com.baastiklabs.pickless.ui.components.SectionTitle
import com.baastiklabs.pickless.ui.theme.Pal
import kotlinx.coroutines.launch

@Composable
fun CoachScreen() {
    val c = LocalContainer.current
    val snack = LocalSnack.current
    val scope = rememberUiScope()
    val prefs by c.settings.prefs.collectAsState(initial = null)
    val active by c.db.challenges().active().collectAsState(initial = null)
    val history by c.db.challenges().all().collectAsState(initial = emptyList())
    val report by c.insights.report.collectAsState()
    var completed by remember { mutableStateOf<Map<String, ChallengeResult>>(emptyMap()) }
    var muddy by remember { mutableStateOf<List<String>>(emptyList()) }
    var tick by remember { mutableIntStateOf(0) }
    var diary by remember { mutableStateOf<com.baastiklabs.pickless.core.detective.Diary?>(null) }
    LaunchedEffect(tick, active?.id) {
        c.coach.evaluate()
        completed = c.coach.completed()
        muddy = active?.let { c.coach.contamination(it) } ?: emptyList()
        diary = c.repo.diary()
    }
    val p = prefs ?: return
    val today = c.repo.today()
    Page("Coach: food tests") {
        item {
            Hint("Elimination diets end by testing foods back in, one at a time: three days of rising amounts, then a few days off before the next. Pickless reads your BMs and symptoms to decide the result.")
        }
        val a = active
        val ap = a?.let { Coach.protocol(it.protocolId) }
        if (a != null && ap != null) {
            item {
                SectionTitle("Running now")
                val model = c.coach.toModel(a)
                val st = Coach.status(model, ap, today)
                InfoCard(container = MaterialTheme.colorScheme.tertiaryContainer) {
                    Text(ap.title, style = MaterialTheme.typography.titleMedium)
                    Hint(ap.why)
                    Spacer(Modifier.height(6.dp))
                    Text(st.message, style = MaterialTheme.typography.bodyLarge)
                    ap.doses.forEach { d ->
                        val r = model.reactions[d.day]
                        KeyValue("Day ${d.day}: ${d.amount} ${d.what.lowercase()}", when {
                            r == null && d.day <= st.dayNumber -> if (today - a.startEpochDay + 1 == d.day.toLong()) "today" else "waiting for results"
                            r == null -> "coming up"
                            else -> listOf("no reaction", "mild", "clear reaction", "strong reaction")[r]
                        })
                    }
                    if (st.phase == Phase.DOSING && today !in model.dosesTaken) {
                        Button(onClick = { scope.launch { c.coach.logDose(a); snack.showSnackbar("Dose logged: watch how you feel over the next day"); tick++ } }, modifier = Modifier.fillMaxWidth()) { Text("I had today's dose") }
                    }
                    if (st.phase == Phase.DOSING && st.dayNumber > 1) {
                        Text("How did yesterday's dose go? (Pickless also reads your logs.)", style = MaterialTheme.typography.bodySmall, modifier = Modifier.padding(top = 8.dp))
                        ChoiceRow(listOf(0 to "Fine", 1 to "Mild", 2 to "Clear", 3 to "Strong"), model.reactions[st.dayNumber - 1], { r -> scope.launch { c.coach.setReaction(a, st.dayNumber - 1, r); tick++ } })
                    }
                    ap.tips.forEach { Hint("Tip: $it") }
                    TextButton(onClick = { scope.launch { c.coach.stop(a); tick++ } }) { Text("Stop this test") }
                }
                if (muddy.isNotEmpty()) Banner("This test might be muddied", "You also had: ${muddy.joinToString()}. Keep everything else to your elimination diet until the test is done.", Pal.signals.caution, Pal.signals.cautionBg, Modifier.padding(top = 8.dp))
            }
        } else {
            val d = diary
            val suggestion = d?.let { Coach.suggest(p.avoid, completed, it, today, false, report, p.badBmRule) }
            if (suggestion != null) item {
                SectionTitle("Suggested next")
                InfoCard(container = Pal.signals.goodBg) {
                    Text(suggestion.protocol.title, style = MaterialTheme.typography.titleMedium)
                    Text(suggestion.why, style = MaterialTheme.typography.bodyMedium)
                    Hint(suggestion.protocol.why)
                    Button(onClick = { scope.launch { c.coach.start(suggestion.item, suggestion.protocol); tick++ } }, modifier = Modifier.padding(top = 8.dp)) { Text("Start this test") }
                }
            }
        }
        item { SectionTitle("Your avoid list") }
        p.avoid.forEach { item ->
            item(key = item.key) { AvoidTestCard(item, completed, active == null, diary) { prot -> scope.launch { c.coach.start(item, prot); tick++ } } }
        }
        item {
            SectionTitle("FODMAP tests")
            Hint("These personalize your FODMAP score: a group you tolerate stops counting against foods.")
        }
        Coach.protocols.filter { it.target.startsWith("fodmap:") }.forEach { prot ->
            item(key = prot.id) {
                val g = Coach.fodmapGroupOf(prot)
                val tol = g?.let { p.tolerance[it] }
                InfoCard(modifier = Modifier.padding(top = 6.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Column(Modifier.weight(1f)) {
                            Text(prot.title, style = MaterialTheme.typography.titleSmall)
                            Hint(completed[prot.id]?.label ?: tol?.let { toleranceLabel(it) } ?: "Not tested")
                        }
                        if (active == null) TextButton(onClick = {
                            scope.launch { c.coach.start(AvoidItem(key = prot.target, label = prot.title), prot); tick++ }
                        }) { Text("Start") }
                    }
                }
            }
        }
        val done = history.filter { !it.active }
        if (done.isNotEmpty()) {
            item { SectionTitle("Finished") }
            done.forEach { e -> item(key = "h${e.id}") { KeyValue(Coach.protocol(e.protocolId)?.title ?: e.protocolId, ChallengeResult.byId(e.result)?.label ?: "-") } }
        }
    }
}

fun toleranceLabel(f: Double): String = when { f <= 0.4 -> "Tolerated"; f < 1.0 -> "OK in small amounts"; f > 1.0 -> "Sensitive"; else -> "Not tested" }

@Composable
private fun AvoidTestCard(item: AvoidItem, completed: Map<String, ChallengeResult>, canStart: Boolean, diary: com.baastiklabs.pickless.core.detective.Diary?, onStart: (Protocol) -> Unit) {
    val c = LocalContainer.current
    val next = Coach.nextStep(item, completed)
    val readiness = diary?.let { Coach.readiness(item, it, c.repo.today(), !canStart) }
    InfoCard(modifier = Modifier.padding(top = 6.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(item.label, style = MaterialTheme.typography.titleMedium, modifier = Modifier.weight(1f))
            val (fg, bg) = when (item.status) { AvoidStatus.TOLERATED -> Pal.signals.good to Pal.signals.goodBg; AvoidStatus.TRIGGER -> Pal.signals.avoid to Pal.signals.avoidBg; else -> Pal.signals.caution to Pal.signals.cautionBg }
            Pill(item.status.label, fg, bg)
        }
        val steps = Coach.stepsFor(item)
        steps.forEach { s -> Hint("Step ${s.step}: ${s.title.substringAfter(": ")}${completed[s.id]?.let { " · ${it.label.lowercase()}" } ?: ""}") }
        if (next != null && item.status != AvoidStatus.TOLERATED) {
            readiness?.reasons?.forEach { Hint("• $it") }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                if (readiness?.ready == true) Button(onClick = { onStart(next) }, enabled = canStart) { Text("Start ${if (steps.size > 1) "step ${next.step}" else "test"}") }
                else OutlinedButton(onClick = { onStart(next) }, enabled = canStart) { Text("Start anyway") }
            }
        } else if (steps.isEmpty()) Hint("No guided test for this one yet: try it in a small portion on a calm day and log how it goes.")
    }
}

@Suppress("unused")
private fun groupName(g: FodmapGroup) = g.label
