package com.baastiklabs.pickless.ui.screens

import android.Manifest
import android.os.Build
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.FilterChip
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import com.baastiklabs.pickless.core.kb.AvoidItem
import com.baastiklabs.pickless.core.kb.Categories
import com.baastiklabs.pickless.ui.components.ChoiceRow
import com.baastiklabs.pickless.ui.components.Hint
import com.baastiklabs.pickless.ui.components.InfoCard
import com.baastiklabs.pickless.ui.components.Stepper
import com.baastiklabs.pickless.work.Jobs
import kotlinx.coroutines.launch

/** First-run setup, pre-filled with the owner's answers; every step can be changed later in Settings. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun OnboardingScreen() {
    val c = LocalContainer.current
    val nav = LocalNav.current
    val ctx = LocalContext.current
    val scope = rememberUiScope()
    val prefs by c.settings.prefs.collectAsState(initial = null)
    var step by rememberSaveable { mutableIntStateOf(0) }
    val p = prefs ?: return
    var chosen by remember { mutableStateOf(p.avoid.mapNotNull { it.categoryId }.toSet()) }
    var sinceDays by rememberSaveable { mutableIntStateOf(14) }
    val notif = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { step++ }
    val steps = 6
    Column(Modifier.fillMaxSize().statusBarsPadding().navigationBarsPadding().padding(20.dp)) {
        LinearProgressIndicator(progress = { (step + 1f) / steps }, modifier = Modifier.fillMaxWidth())
        Spacer(Modifier.height(20.dp))
        Column(Modifier.weight(1f).verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            when (step) {
                0 -> {
                    Text("Welcome to Pickless", style = MaterialTheme.typography.headlineLarge)
                    Text("Scan or type a food to check it against your avoid list and get a FODMAP score. Log BMs (just a 1-7 rating) and Pickless works out what's behind the bad ones, then coaches you through testing foods back in, so you get less picky over time.", style = MaterialTheme.typography.bodyLarge)
                    InfoCard { Text("Pickless gives estimates and suspects, not medical advice. Blood, black stool, weight loss or symptoms at night are for your doctor, not an app.", style = MaterialTheme.typography.bodyMedium) }
                    Hint("Everything stays on your phone.")
                }
                1 -> {
                    Text("What are you avoiding?", style = MaterialTheme.typography.headlineMedium)
                    Hint("Pre-filled with your list. Tap to change; you can fine-tune strictness later.")
                    FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        Categories.all.forEach { cat -> FilterChip(selected = cat.id in chosen, onClick = { chosen = if (cat.id in chosen) chosen - cat.id else chosen + cat.id }, label = { Text(cat.label) }) }
                    }
                    Hint("Soy lecithin and soybean oil count as \"caution\", not \"avoid\" (they're refined and have almost no soy protein). Change that per item in the avoid list.")
                    Text("How long have you been avoiding them?", style = MaterialTheme.typography.titleMedium)
                    ChoiceRow(listOf(0 to "Just starting", 7 to "About a week", 14 to "2 weeks", 30 to "A month", 60 to "2+ months"), sinceDays, { sinceDays = it })
                    Hint("So the coach knows when a food is ready to test (usually after 2 to 6 weeks).")
                }
                2 -> {
                    Text("What does a bad BM look like for you?", style = MaterialTheme.typography.headlineMedium)
                    ChoiceRow(listOf("loose" to "Mostly loose", "hard" to "Mostly hard", "both" to "Both"), p.badBm, { v -> scope.launch { c.settings.setBadBm(v) } })
                    Hint("Loose means type 6-7, a type 5 with urgency, or an accident. The detective hunts for whatever comes before those.")
                }
                3 -> {
                    Text("Nicotine", style = MaterialTheme.typography.headlineMedium)
                    Text("Nicotine speeds up the gut for many people, so the evening check-in asks about it, defaulting to \"usual\" so normal days take zero taps.", style = MaterialTheme.typography.bodyLarge)
                    Stepper(p.nicotineUsual, { n -> scope.launch { c.settings.setNicotine(true, n) } }, "Usual pouches a day")
                    TextButton(onClick = { scope.launch { c.settings.setNicotine(false, 0) }; step++ }) { Text("I don't use nicotine") }
                }
                4 -> {
                    Text("Reminders", style = MaterialTheme.typography.headlineMedium)
                    Text("An evening check-in (about 20 seconds) and an optional nudge a few hours after meals.", style = MaterialTheme.typography.bodyLarge)
                    ChoiceRow(listOf(19 to "7:30 pm", 20 to "8:30 pm", 21 to "9:30 pm", 22 to "10:30 pm"), p.checkinHour, { h -> scope.launch { c.settings.setCheckinTime(h, 30) } })
                }
                else -> {
                    Text("Last bits (optional)", style = MaterialTheme.typography.headlineMedium)
                    InfoCard {
                        Text("Let Pickless update itself", style = MaterialTheme.typography.titleMedium)
                        Hint("Android asks once: turn on \"Allow from this source\". Samsung: if updates are blocked, turn off Settings > Security and privacy > Auto Blocker.")
                        if (!c.updater.canInstall()) OutlinedButton(onClick = { ctx.startActivity(c.updater.permissionIntent()) }) { Text("Allow updates") }
                        else Text("Allowed ✓", color = MaterialTheme.colorScheme.primary)
                    }
                    InfoCard {
                        Text("Brave Search key", style = MaterialTheme.typography.titleMedium)
                        Hint("For looking up foods Pickless doesn't know. Paste it here (it stays on this phone). Capped at 300 lookups a month.")
                        var key by remember { mutableStateOf(p.braveKey) }
                        OutlinedTextField(key, { key = it; scope.launch { c.settings.setBrave(it, p.braveCap) } }, singleLine = true, visualTransformation = PasswordVisualTransformation(), modifier = Modifier.fillMaxWidth(), label = { Text("Key (optional)") })
                    }
                    InfoCard {
                        Text("Suggestion sheet", style = MaterialTheme.typography.titleMedium)
                        Hint("Connect it any time from More > Suggestion box by scanning the QR code the sheet's script shows.")
                    }
                }
            }
        }
        Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            if (step > 0) OutlinedButton(onClick = { step-- }, modifier = Modifier.weight(1f)) { Text("Back") }
            Button(onClick = {
                scope.launch {
                    when (step) {
                        1 -> {
                            val since = c.repo.today() - sinceDays
                            c.settings.updateAvoid { list ->
                                val kept = list.filter { it.categoryId == null || it.categoryId in chosen }.map { it.copy(sinceEpochDay = since) }
                                kept + chosen.filter { id -> kept.none { it.categoryId == id } }.map { AvoidItem.ofCategory(it, since) }
                            }
                            step++
                        }
                        4 -> {
                            val pp = c.settings.current()
                            Jobs.scheduleAll(ctx, pp.checkinHour, pp.checkinMinute, pp.weeklyReminder)
                            if (Build.VERSION.SDK_INT >= 33) notif.launch(Manifest.permission.POST_NOTIFICATIONS) else step++
                        }
                        steps - 1 -> {
                            c.settings.setOnboarded(true)
                            nav.navigate("today") { popUpTo("onboarding") { inclusive = true } }
                        }
                        else -> step++
                    }
                }
            }, modifier = Modifier.weight(1f).height(52.dp)) { Text(if (step == steps - 1) "Start" else "Next") }
        }
    }
}
