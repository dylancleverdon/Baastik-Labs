package com.baastiklabs.pickless.ui.screens

import android.content.Intent
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material.icons.filled.Block
import androidx.compose.material.icons.filled.Bookmark
import androidx.compose.material.icons.filled.Description
import androidx.compose.material.icons.filled.Lightbulb
import androidx.compose.material.icons.filled.Restaurant
import androidx.compose.material.icons.filled.Science
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.Shield
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Checkbox
import androidx.compose.material3.FilterChip
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.ListItem
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.health.connect.client.PermissionController
import com.baastiklabs.pickless.BuildConfig
import com.baastiklabs.pickless.core.coach.Coach
import com.baastiklabs.pickless.core.kb.AvoidItem
import com.baastiklabs.pickless.core.kb.AvoidStatus
import com.baastiklabs.pickless.core.kb.Categories
import com.baastiklabs.pickless.core.kb.Strictness
import com.baastiklabs.pickless.core.kb.TracePolicy
import com.baastiklabs.pickless.core.suggest.SuggestionKinds
import com.baastiklabs.pickless.data.CustomFoodEntity
import com.baastiklabs.pickless.data.CustomItem
import com.baastiklabs.pickless.health.Health
import com.baastiklabs.pickless.suggest.SuggestionBox
import com.baastiklabs.pickless.update.Updater
import com.baastiklabs.pickless.ui.components.ChoiceRow
import com.baastiklabs.pickless.ui.components.Hint
import com.baastiklabs.pickless.ui.components.InfoCard
import com.baastiklabs.pickless.ui.components.KeyValue
import com.baastiklabs.pickless.ui.components.Pill
import com.baastiklabs.pickless.ui.components.SectionTitle
import com.baastiklabs.pickless.ui.components.whenText
import com.baastiklabs.pickless.ui.theme.Pal
import com.baastiklabs.pickless.work.Jobs
import kotlinx.coroutines.launch

@Composable
fun MoreScreen() {
    val nav = LocalNav.current
    LazyColumn(Modifier.statusBarsPadding().padding(horizontal = 16.dp)) {
        item { Text("More", style = MaterialTheme.typography.headlineMedium, modifier = Modifier.padding(top = 18.dp, bottom = 8.dp)) }
        item {
            Column {
                MoreItem(Icons.Filled.Block, "Avoid list", "Dairy, oats, soy and the rest, how strict each is") { nav.navigate("avoid") }
                MoreItem(Icons.Filled.Science, "Coach: food tests", "Reintroduce foods one at a time, step by step") { nav.navigate("coach") }
                MoreItem(Icons.Filled.Bookmark, "My meals & recipes", "Save a usual breakfast or a recipe, log it in one tap") { nav.navigate("foods") }
                MoreItem(Icons.Filled.Restaurant, "Restaurant card", "Show staff what you can't eat") { nav.navigate("card") }
                MoreItem(Icons.Filled.Description, "Report for your doctor", "PDF, spreadsheet export and backups") { nav.navigate("report") }
                MoreItem(Icons.Filled.Lightbulb, "Suggestion box", "Ideas, bugs, wrong food info, send logs") { nav.navigate("suggest") }
                MoreItem(Icons.Filled.Settings, "Settings", "Reminders, nicotine, Brave key, updates, app lock") { nav.navigate("settings") }
                MoreItem(Icons.Filled.Shield, "Privacy & health notes", "What stays on your phone, and when to see a doctor") { nav.navigate("privacy") }
            }
        }
    }
}

@Composable
private fun MoreItem(icon: ImageVector, title: String, subtitle: String, onClick: () -> Unit) {
    InfoCard(onClick = onClick, container = MaterialTheme.colorScheme.surfaceContainerLow, modifier = Modifier.padding(vertical = 4.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Icon(icon, null, tint = MaterialTheme.colorScheme.primary)
            Column(Modifier.weight(1f).padding(start = 14.dp)) {
                Text(title, style = MaterialTheme.typography.titleMedium)
                Hint(subtitle)
            }
            Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, null)
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun AvoidListScreen() {
    val c = LocalContainer.current
    val scope = rememberUiScope()
    val prefs by c.settings.prefs.collectAsState(initial = null)
    var adding by remember { mutableStateOf(false) }
    var editing by remember { mutableStateOf<AvoidItem?>(null) }
    val items = prefs?.avoid ?: emptyList()
    Page("Avoid list") {
        item { Hint("Everything you scan or type is checked against this list. Statuses change as you test foods in Coach.") }
        items.forEach { item ->
            item(key = item.key) {
                InfoCard(onClick = { editing = item }, modifier = Modifier.padding(top = 8.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Column(Modifier.weight(1f)) {
                            Text(item.label, style = MaterialTheme.typography.titleMedium)
                            Hint(item.category?.about ?: item.term?.let { "Any label mentioning \"$it\"" } ?: "This food and anything made with it")
                        }
                        val (fg, bg) = when (item.status) {
                            AvoidStatus.TOLERATED -> Pal.signals.good to Pal.signals.goodBg
                            AvoidStatus.TRIGGER -> Pal.signals.avoid to Pal.signals.avoidBg
                            else -> Pal.signals.caution to Pal.signals.cautionBg
                        }
                        Pill(item.status.label, fg, bg)
                    }
                    if (item.strictness != Strictness.STANDARD || item.traces != TracePolicy.CAUTION) Hint("Strictness: ${item.strictness.name.lowercase()} · \"may contain\": ${item.traces.name.lowercase()}")
                }
            }
        }
        item { Spacer(Modifier.height(12.dp)); Button(onClick = { adding = true }, modifier = Modifier.fillMaxWidth()) { Text("Add something") } }
    }
    if (adding) {
        var term by remember { mutableStateOf("") }
        AlertDialog(
            onDismissRequest = { adding = false },
            title = { Text("Add to avoid list") },
            text = {
                Column {
                    FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                        Categories.all.filter { cat -> items.none { it.categoryId == cat.id } }.forEach { cat ->
                            FilterChip(selected = false, onClick = {
                                scope.launch { c.settings.updateAvoid { it + AvoidItem.ofCategory(cat.id, c.repo.today()) } }
                                adding = false
                            }, label = { Text(cat.label) })
                        }
                    }
                    Spacer(Modifier.height(8.dp))
                    OutlinedTextField(term, { term = it }, label = { Text("Or a food or word (e.g. garlic, coconut)") }, singleLine = true)
                }
            },
            confirmButton = {
                TextButton(enabled = term.isNotBlank(), onClick = {
                    scope.launch {
                        val hit = c.kb.search(term, 1).firstOrNull()?.takeIf { it.score >= 6 }?.entry
                        c.settings.updateAvoid { it + (if (hit != null) AvoidItem.ofFood(hit, c.repo.today()) else AvoidItem.ofTerm(term, c.repo.today())) }
                    }
                    adding = false
                }) { Text("Add") }
            },
            dismissButton = { TextButton(onClick = { adding = false }) { Text("Close") } },
        )
    }
    editing?.let { item ->
        var status by remember(item) { mutableStateOf(item.status) }
        var strict by remember(item) { mutableStateOf(item.strictness) }
        var traces by remember(item) { mutableStateOf(item.traces) }
        AlertDialog(
            onDismissRequest = { editing = null },
            title = { Text(item.label) },
            text = {
                Column {
                    Text("Status", style = MaterialTheme.typography.titleSmall)
                    ChoiceRow(AvoidStatus.entries.map { it to it.label }, status, { status = it })
                    item.category?.cautionMeans?.takeIf { it.isNotBlank() }?.let { means ->
                        Text("How strict", style = MaterialTheme.typography.titleSmall, modifier = Modifier.padding(top = 8.dp))
                        ChoiceRow(listOf(Strictness.RELAXED to "Relaxed", Strictness.STANDARD to "Standard", Strictness.STRICT to "Strict"), strict, { strict = it })
                        Hint("Caution covers $means. Strict counts those as avoid; relaxed ignores them.")
                    }
                    Text("\"May contain\" warnings", style = MaterialTheme.typography.titleSmall, modifier = Modifier.padding(top = 8.dp))
                    ChoiceRow(listOf(TracePolicy.IGNORE to "Ignore", TracePolicy.CAUTION to "Caution", TracePolicy.AVOID to "Avoid"), traces, { traces = it })
                }
            },
            confirmButton = {
                TextButton(onClick = {
                    scope.launch { c.settings.updateAvoid { list -> list.map { if (it.key == item.key) it.copy(status = status, strictness = strict, traces = traces) else it } } }
                    editing = null
                }) { Text("Save") }
            },
            dismissButton = {
                TextButton(onClick = { scope.launch { c.settings.updateAvoid { list -> list.filterNot { it.key == item.key } } }; editing = null }) { Text("Remove") }
            },
        )
    }
}

@Composable
fun RestaurantCardScreen() {
    val c = LocalContainer.current
    val prefs by c.settings.prefs.collectAsState(initial = null)
    val items = prefs?.avoid?.filter { it.status.flags } ?: emptyList()
    Page("Restaurant card") {
        item {
            Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(24.dp)).background(Color.White).padding(24.dp)) {
                Text("Thank you for helping!", color = Color.Black, fontSize = 22.sp, fontWeight = FontWeight.Bold)
                Spacer(Modifier.height(6.dp))
                Text("For medical reasons I can't eat:", color = Color.Black, fontSize = 18.sp)
                Spacer(Modifier.height(10.dp))
                items.forEach { i ->
                    val detail = when (i.categoryId) {
                        "dairy" -> "milk, butter, cheese, cream, yogurt, whey"
                        "oat" -> "oats, oat milk, granola"
                        "soy" -> "soy, tofu, soy sauce, edamame, soy milk"
                        "fried" -> "anything deep-fried or battered"
                        "spicy" -> "chili, hot sauce, cayenne, jalapeño"
                        "brussels-sprouts" -> "Brussels sprouts"
                        else -> i.category?.about?.substringBefore('.') ?: ""
                    }
                    Text("• ${i.label}", color = Color.Black, fontSize = 20.sp, fontWeight = FontWeight.SemiBold)
                    if (detail.isNotBlank()) Text("   $detail", color = Color(0xFF333333), fontSize = 16.sp)
                    Spacer(Modifier.height(6.dp))
                }
                Spacer(Modifier.height(8.dp))
                Text("A little oil or salt is fine. Could you tell me what's in the dishes, or suggest one that works? Thank you!", color = Color.Black, fontSize = 16.sp)
            }
            Hint("Turn your screen brightness up and hand your phone over.", Modifier.padding(top = 12.dp))
        }
    }
}

@Composable
fun MyFoodsScreen() {
    val c = LocalContainer.current
    val nav = LocalNav.current
    val foods by c.db.custom().all().collectAsState(initial = emptyList())
    Page("My meals & recipes") {
        item { Hint("Build your usual meals once and log them with one tap. FODMAP adds up across everything in them.") }
        foods.forEach { f ->
            item(key = f.id) {
                InfoCard(onClick = { nav.navigate("result/${enc("custom:${f.id}")}") }, modifier = Modifier.padding(top = 8.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Column(Modifier.weight(1f)) { Text(f.name, style = MaterialTheme.typography.titleMedium); Hint(c.repo.items(f).joinToString { it.name }) }
                        TextButton(onClick = { nav.navigate("recipe/${f.id}") }) { Text("Edit") }
                    }
                }
            }
        }
        item { Spacer(Modifier.height(12.dp)); Button(onClick = { nav.navigate("recipe/0") }, modifier = Modifier.fillMaxWidth()) { Text("New meal or recipe") } }
    }
}

@Composable
fun RecipeScreen(id: Long?) {
    val c = LocalContainer.current
    val nav = LocalNav.current
    val scope = rememberUiScope()
    var name by rememberSaveable { mutableStateOf("") }
    val items = remember { mutableStateListOf<CustomItem>() }
    var existing by remember { mutableStateOf<CustomFoodEntity?>(null) }
    var query by remember { mutableStateOf("") }
    LaunchedEffect(id) {
        val e = id?.takeIf { it > 0 }?.let { c.db.custom().get(it) } ?: return@LaunchedEffect
        existing = e; name = e.name; items.clear(); items.addAll(c.repo.items(e))
    }
    val hits = remember(query) { if (query.length < 2) emptyList() else c.kb.search(query, 8) }
    Page(if (existing != null) "Edit meal" else "New meal") {
        item {
            OutlinedTextField(name, { name = it }, label = { Text("Name (e.g. My usual breakfast)") }, modifier = Modifier.fillMaxWidth(), singleLine = true)
            SectionTitle("What's in it")
        }
        items.forEachIndexed { i, it ->
            item {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(it.name, modifier = Modifier.weight(1f))
                    OutlinedTextField(it.grams.toInt().toString(), { t -> t.toDoubleOrNull()?.let { g -> items[i] = items[i].copy(grams = g) } },
                        singleLine = true, label = { Text("g") }, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number), modifier = Modifier.padding(horizontal = 8.dp).fillMaxWidth(0.3f))
                    TextButton(onClick = { items.removeAt(i) }) { Text("Remove") }
                }
            }
        }
        item {
            OutlinedTextField(query, { query = it }, label = { Text("Add a food") }, modifier = Modifier.fillMaxWidth(), singleLine = true)
            hits.forEach { h ->
                TextButton(onClick = { items.add(CustomItem("food:${h.entry.id}", h.entry.name, h.entry.defaultPortion.grams)); query = "" }) { Text("+ ${h.entry.name} (${h.entry.defaultPortion.grams.toInt()} g)") }
            }
            Spacer(Modifier.height(16.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                existing?.let { e -> OutlinedButton(onClick = { scope.launch { c.db.custom().delete(e); nav.popBackStack() } }, modifier = Modifier.weight(1f)) { Text("Delete") } }
                Button(enabled = name.isNotBlank() && items.isNotEmpty(), onClick = {
                    scope.launch {
                        val total = items.sumOf { it.grams }
                        val e = CustomFoodEntity(existing?.id ?: 0, name.trim(), c.repo.encodeItems(items.toList()), total)
                        if (existing != null) c.db.custom().update(e) else c.db.custom().insert(e)
                        nav.popBackStack()
                    }
                }, modifier = Modifier.weight(1f)) { Text("Save") }
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun SuggestionScreen(prefill: String?) {
    val c = LocalContainer.current
    val nav = LocalNav.current
    val snack = LocalSnack.current
    val scope = rememberUiScope()
    val prefs by c.settings.prefs.collectAsState(initial = null)
    val recent by c.db.suggestions().recent().collectAsState(initial = emptyList())
    var kind by rememberSaveable { mutableStateOf(if (prefill?.startsWith("Wrong food") == true) "Wrong food info" else "Idea") }
    var text by rememberSaveable { mutableStateOf(prefill ?: "") }
    var logs by rememberSaveable { mutableStateOf(false) }
    var data by rememberSaveable { mutableStateOf(false) }
    var pasting by remember { mutableStateOf(false) }
    val ctx2 = LocalContext.current
    val connected = prefs?.endpoint?.let(SuggestionBox::validEndpoint) == true
    Page("Suggestion box") {
        item {
            if (!connected) InfoCard(container = Pal.signals.cautionBg) {
                Text("Not connected to your sheet yet", style = MaterialTheme.typography.titleSmall)
                Hint("Suggestions are kept on the phone and sent once it's connected. One-time setup: deploy the sheet's script (see SETUP in the repo), open its link on your computer, then scan the QR code it shows.")
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    TextButton(onClick = { nav.navigate("scan?mode=qr") }) { Text("Scan QR code") }
                    TextButton(onClick = { pasting = true }) { Text("Paste link") }
                }
            } else Hint("Connected: suggestions go to \"elimination diet app suggestion sheet\" in your Drive.")
            SectionTitle("What kind")
            ChoiceRow(SuggestionKinds.all.map { it to it }, kind, { kind = it })
            OutlinedTextField(text, { text = it }, label = { Text("Your suggestion") }, modifier = Modifier.fillMaxWidth().height(180.dp).padding(top = 8.dp))
            Row(verticalAlignment = Alignment.CenterVertically) { Checkbox(logs, { logs = it }); Text("Send logs (app diagnostics only)") }
            Row(verticalAlignment = Alignment.CenterVertically) { Checkbox(data, { data = it }); Text("Include my last 14 days of logs") }
            if (data) Hint("Only tick this when it helps explain a detective or food problem. It goes to your own Drive.")
            Spacer(Modifier.height(10.dp))
            Button(enabled = text.isNotBlank(), onClick = {
                scope.launch {
                    c.suggestions.add(kind, text.trim(), logs, if (data) c.suggestions.dataSummary(c.repo) else null)
                    snack.showSnackbar(if (connected) "Sent (or sending shortly)" else "Saved; it'll send once the sheet is connected")
                    text = ""; logs = false; data = false
                }
            }, modifier = Modifier.fillMaxWidth().height(52.dp)) { Text("Send") }
        }
        if (recent.isNotEmpty()) {
            item { SectionTitle("Recent") }
            recent.take(10).forEach { s -> item(key = s.id) { KeyValue("${s.kind}: ${s.text.take(40)}${if (s.text.length > 40) "…" else ""}", if (s.sentAt != null) "sent" else "waiting") } }
        }
    }
    if (pasting) {
        var url by remember { mutableStateOf("") }
        AlertDialog(
            onDismissRequest = { pasting = false },
            title = { Text("Paste the script's /exec link") },
            text = { OutlinedTextField(url, { url = it }, singleLine = true, label = { Text("https://script.google.com/macros/s/…/exec") }) },
            confirmButton = {
                TextButton(enabled = SuggestionBox.validEndpoint(url), onClick = {
                    scope.launch { c.settings.setEndpoint(url); Jobs.sendSuggestions(ctx2); snack.showSnackbar(if (c.suggestions.test(url)) "Connected" else "Saved; the sheet didn't answer yet") }
                    pasting = false
                }) { Text("Save") }
            },
            dismissButton = { TextButton(onClick = { pasting = false }) { Text("Cancel") } },
        )
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun SettingsScreen() {
    val c = LocalContainer.current
    val nav = LocalNav.current
    val snack = LocalSnack.current
    val ctx = LocalContext.current
    val scope = rememberUiScope()
    val prefs by c.settings.prefs.collectAsState(initial = null)
    val update by c.updater.state.collectAsState()
    var packCount by remember { mutableStateOf(-1L) }
    var packMsg by remember { mutableStateOf("") }
    val healthLauncher = rememberLauncherForActivityResult(PermissionController.createRequestPermissionResultContract()) { granted ->
        scope.launch { c.settings.setHealthConnect(granted.isNotEmpty()) }
    }
    LaunchedEffect(Unit) { packCount = c.pack.count() }
    val p = prefs ?: return
    Page("Settings") {
        item {
            SectionTitle("Bad BMs are…")
            ChoiceRow(listOf("loose" to "Mostly loose", "hard" to "Mostly hard", "both" to "Both"), p.badBm, { v -> scope.launch { c.settings.setBadBm(v); c.insights.refreshSoon() } })
            Hint("Loose = type 6-7, type 5 with urgency, or an accident.")
        }
        item {
            SectionTitle("Reminders")
            KeyValue("Evening check-in", "%d:%02d %s".format(if (p.checkinHour % 12 == 0) 12 else p.checkinHour % 12, p.checkinMinute, if (p.checkinHour < 12) "am" else "pm"))
            ChoiceRow(listOf(19 to "7 pm", 20 to "8 pm", 21 to "9 pm", 22 to "10 pm"), p.checkinHour, { h -> scope.launch { c.settings.setCheckinTime(h, 30); Jobs.scheduleCheckIn(ctx, h, 30) } })
            ToggleRow("Nudge after meals", p.nudges, { on -> scope.launch { c.settings.setNudges(on, p.nudgeHours) } }, "A \"how's your gut?\" a few hours after logging a meal")
            if (p.nudges) ChoiceRow(listOf(2 to "2 h", 3 to "3 h", 4 to "4 h"), p.nudgeHours, { h -> scope.launch { c.settings.setNudges(true, h) } })
            ToggleRow("Weekly check-in reminder", p.weeklyReminder, { on -> scope.launch { c.settings.setWeekly(on); Jobs.scheduleAll(ctx, p.checkinHour, p.checkinMinute, on) } })
        }
        item {
            SectionTitle("Nicotine")
            ToggleRow("Track nicotine in the check-in", p.nicotineTracking, { on -> scope.launch { c.settings.setNicotine(on, p.nicotineUsual) } }, "Defaults to \"usual\": only tap on days that differ")
            if (p.nicotineTracking) com.baastiklabs.pickless.ui.components.Stepper(p.nicotineUsual, { n -> scope.launch { c.settings.setNicotine(true, n) } }, "Usual pouches a day")
            ToggleRow("Track my cycle", p.cycleTracking, { on -> scope.launch { c.settings.setCycle(on) } }, "Adds a period toggle to the check-in")
        }
        item {
            SectionTitle("Looking up unknown foods (Brave Search)")
            var key by remember { mutableStateOf(p.braveKey) }
            OutlinedTextField(key, { key = it }, label = { Text("Brave Search API key") }, singleLine = true, visualTransformation = PasswordVisualTransformation(), modifier = Modifier.fillMaxWidth())
            Row(verticalAlignment = Alignment.CenterVertically) {
                Hint("Used ${p.braveUsedThisMonth} of ${p.braveCap} this month. Stays well inside Brave's \$5 monthly credit.", Modifier.weight(1f))
                TextButton(onClick = { scope.launch { c.settings.setBrave(key, p.braveCap); snack.showSnackbar(if (key.isBlank()) "Key removed" else "Key saved on this phone") } }) { Text("Save") }
            }
            ChoiceRow(listOf(100 to "100/mo", 300 to "300/mo", 600 to "600/mo"), p.braveCap, { cap -> scope.launch { c.settings.setBrave(p.braveKey, cap) } })
        }
        item {
            SectionTitle("US product pack (offline barcode data)")
            KeyValue("Installed", if (packCount > 0) "%,d products (%s)".format(packCount, p.packVersion) else "Not yet")
            Hint("USDA's database of branded US foods. Downloads on Wi-Fi by itself; scans fall back to Open Food Facts.")
            TextButton(onClick = { scope.launch { packMsg = "Downloading…"; packMsg = c.pack.refresh(force = true); packCount = c.pack.count() } }) { Text("Update now") }
            if (packMsg.isNotBlank()) Hint(packMsg)
        }
        item {
            SectionTitle("Health Connect (optional)")
            if (!Health.available(ctx)) Hint("Health Connect isn't on this phone.") else {
                ToggleRow("Pre-fill sleep, exercise and period", p.healthConnect, { on ->
                    if (on) healthLauncher.launch(Health.permissions) else scope.launch { c.settings.setHealthConnect(false) }
                }, "Read-only, from a watch or fitness app")
            }
        }
        item {
            SectionTitle("Updates")
            KeyValue("Version", "${BuildConfig.VERSION_NAME} (${BuildConfig.VERSION_CODE})")
            KeyValue("Status", when (val u = update) {
                is Updater.State.Checking -> "Checking…"
                is Updater.State.Downloading -> "Downloading ${u.version}…"
                is Updater.State.Ready -> "Installing ${u.version}"
                is Updater.State.UpToDate -> "Up to date"
                is Updater.State.Failed -> u.message
                Updater.State.NeedsPermission -> "Needs permission to install"
                Updater.State.Idle -> if (p.lastUpdateCheck > 0) "Last checked ${whenText(p.lastUpdateCheck)}" else "Checks every hour"
            })
            ToggleRow("Install updates automatically", p.autoUpdate, { on -> scope.launch { c.settings.setAutoUpdate(on) } })
            Row {
                TextButton(onClick = { scope.launch { c.updater.check(userAsked = true) } }) { Text("Check now") }
                if (!c.updater.canInstall()) TextButton(onClick = { ctx.startActivity(c.updater.permissionIntent()) }) { Text("Allow installing updates") }
            }
        }
        item {
            SectionTitle("Privacy")
            ToggleRow("Lock with fingerprint or PIN", p.appLock, { on -> scope.launch { c.settings.setAppLock(on) } })
            OutlinedTextField(p.suggestFrom, { v -> scope.launch { c.settings.setFrom(v) } }, label = { Text("Name on suggestions") }, singleLine = true, modifier = Modifier.fillMaxWidth())
        }
        item {
            SectionTitle("Medicine & supplement list")
            var meds by remember { mutableStateOf(p.meds.joinToString(", ")) }
            OutlinedTextField(meds, { meds = it }, label = { Text("Shown as chips in the check-in") }, modifier = Modifier.fillMaxWidth())
            TextButton(onClick = { scope.launch { c.settings.setMeds(meds.split(',').map { it.trim() }.filter { it.isNotBlank() }) } }) { Text("Save list") }
        }
        item {
            HorizontalDivider(Modifier.padding(vertical = 12.dp))
            TextButton(onClick = { nav.navigate("onboarding") }) { Text("Run the setup again") }
        }
    }
}

@Composable
fun PrivacyScreen() {
    val ctx = LocalContext.current
    Page("Privacy & health notes") {
        item {
            InfoCard {
                Text("Your data stays on your phone", style = MaterialTheme.typography.titleMedium)
                Text("No account, no ads, no tracking. Pickless only goes online to look up barcodes and product names (Open Food Facts), to download updates and the US product pack (this app's GitHub releases), for Brave lookups if you add a key, and to send suggestions to your own Google Sheet. Android's backup keeps your diary if you change phones.", style = MaterialTheme.typography.bodyMedium)
            }
            Spacer(Modifier.height(10.dp))
            InfoCard {
                Text("Health Connect", style = MaterialTheme.typography.titleMedium)
                Text("If you turn it on, Pickless reads sleep, exercise and period days to pre-fill the check-in. It never writes to Health Connect and never sends that data anywhere.", style = MaterialTheme.typography.bodyMedium)
            }
            Spacer(Modifier.height(10.dp))
            InfoCard(container = Pal.signals.avoidBg) {
                Text("See a doctor, don't analyze, if you notice", style = MaterialTheme.typography.titleMedium)
                Text("Blood in the stool or black, tarry stool · weight loss you can't explain · waking up at night with symptoms or diarrhea · fever · symptoms that start after 50 or keep getting worse · a family history of colon cancer, IBD or celiac disease.", style = MaterialTheme.typography.bodyMedium)
            }
            Spacer(Modifier.height(10.dp))
            InfoCard {
                Text("What Pickless is", style = MaterialTheme.typography.titleMedium)
                Text("A tool for your elimination diet: estimates and suspects, not diagnoses. FODMAP numbers are estimates from published research and dietitian guidance; the Monash University FODMAP app is the reference if you want the lab-tested figures. If gluten is on your list and celiac disease hasn't been ruled out, the test needs gluten in your diet: ask your doctor before going strict.", style = MaterialTheme.typography.bodyMedium)
            }
            TextButton(onClick = { ctx.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse("https://github.com/${BuildConfig.REPO}/tree/pickless"))) }) { Text("Source code") }
            Hint("Product data from Open Food Facts (ODbL) and USDA FoodData Central (public domain).")
        }
    }
}
