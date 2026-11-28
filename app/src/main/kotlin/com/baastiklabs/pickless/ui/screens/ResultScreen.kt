package com.baastiklabs.pickless.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Star
import androidx.compose.material.icons.outlined.StarOutline
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.FilterChip
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
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
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import com.baastiklabs.pickless.core.analysis.Analysis
import com.baastiklabs.pickless.core.analysis.Analyzer
import com.baastiklabs.pickless.core.analysis.FodmapSource
import com.baastiklabs.pickless.core.analysis.HitLevel
import com.baastiklabs.pickless.core.analysis.Verdict
import com.baastiklabs.pickless.core.fodmap.FodmapGroup
import com.baastiklabs.pickless.core.fodmap.FodmapLevel
import com.baastiklabs.pickless.core.fodmap.FodmapLimit
import com.baastiklabs.pickless.core.fodmap.FodmapScore
import com.baastiklabs.pickless.core.kb.Portion
import com.baastiklabs.pickless.data.CorrectionEntity
import com.baastiklabs.pickless.net.WebLookup
import com.baastiklabs.pickless.ui.components.FodmapGauge
import com.baastiklabs.pickless.ui.components.Hint
import com.baastiklabs.pickless.ui.components.InfoCard
import com.baastiklabs.pickless.ui.components.LoadBar
import com.baastiklabs.pickless.ui.components.Pill
import com.baastiklabs.pickless.ui.components.SectionTitle
import com.baastiklabs.pickless.ui.components.TimeField
import com.baastiklabs.pickless.ui.components.VerdictPill
import com.baastiklabs.pickless.ui.theme.Pal
import com.baastiklabs.pickless.work.Jobs
import kotlinx.coroutines.launch
import java.time.LocalTime

fun fmtG(g: Double): String = if (g >= 10) "${g.toInt()} g" else "${"%.1f".format(g)} g"

fun portionName(p: Portion): String = when (p.name) { "serving" -> "1 serving"; "each" -> "1 whole"; "100 g" -> "100 g"; else -> "1 ${p.name}" }

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun ResultScreen(ref: String) {
    val c = LocalContainer.current
    val nav = LocalNav.current
    val snack = LocalSnack.current
    val scope = rememberUiScope()
    var reload by remember { mutableIntStateOf(0) }
    var analysis by remember { mutableStateOf<Analysis?>(null) }
    var loaded by remember { mutableStateOf(false) }
    var grams by rememberSaveable { mutableStateOf(-1.0) }
    var correcting by remember { mutableStateOf(false) }
    var looking by remember { mutableStateOf(false) }
    var timesEaten by remember { mutableIntStateOf(0) }
    val prefs by c.settings.prefs.collectAsState(initial = null)
    val report by c.insights.report.collectAsState()
    val productKey = ref.takeIf { it.startsWith("product:") }?.removePrefix("product:")
    var product by remember { mutableStateOf<com.baastiklabs.pickless.data.ProductEntity?>(null) }
    LaunchedEffect(ref, reload) {
        analysis = c.repo.analyze(ref)
        loaded = true
        if (grams < 0) grams = analysis?.portion?.grams ?: 100.0
        timesEaten = c.db.food().byRef(ref).size
        product = productKey?.let { c.db.products().get(it) }
    }
    val a = analysis
    Page(a?.title ?: "Checking…", actions = {
        if (a != null) {
            val fav = if (productKey != null) product?.favorite == true else ref in (prefs?.favorites ?: emptySet())
            IconButton(onClick = {
                scope.launch {
                    if (productKey != null) product?.let { c.db.products().upsert(it.copy(favorite = !it.favorite)) } else c.settings.toggleFavorite(ref)
                    reload++
                }
            }) { Icon(if (fav) Icons.Filled.Star else Icons.Outlined.StarOutline, "Favorite") }
        }
    }, bottom = {
        if (a != null) Surface(tonalElevation = 3.dp) {
            Row(Modifier.fillMaxWidth().navigationBarsPadding().padding(12.dp), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                OutlinedButton(onClick = { nav.navigate("suggest/${enc("Wrong food info for \"${a.title}\" ($ref): ")}") }, modifier = Modifier.weight(1f)) { Text("Report wrong info") }
                Button(onClick = { nav.navigate("log/${enc(ref)}") }, modifier = Modifier.weight(1f)) { Text("Log it") }
            }
        }
    }) {
        if (a == null) {
            item {
                if (!loaded) CircularProgressIndicator(Modifier.padding(32.dp)) else Column {
                    Text("Pickless couldn't make sense of this one.", style = MaterialTheme.typography.bodyLarge)
                    if (ref.startsWith("guess:") && prefs?.braveKey?.isNotBlank() == true) {
                        Button(onClick = {
                            looking = true
                            scope.launch {
                                when (val r = c.web.lookup(ref.removePrefix("guess:"), 100.0)) {
                                    is WebLookup.Outcome.Ok -> reload++
                                    is WebLookup.Outcome.Problem -> snack.showSnackbar(r.message)
                                }
                                looking = false
                            }
                        }, enabled = !looking) { Text(if (looking) "Looking it up…" else "Look it up on FODMAP sites") }
                    } else Hint("Try a simpler name, scan the barcode, or snap the label.")
                }
            }
            return@Page
        }
        val g = if (grams > 0) grams else a.portion.grams
        val at = a.fodmapAt(g)
        item {
            val (vfg, vbg) = Pal.verdict(a.verdict)
            Column(Modifier.fillMaxWidth().padding(top = 4.dp).clip(RoundedCornerShape(20.dp)).background(vbg).padding(16.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        when (a.verdict) { Verdict.GOOD -> "Looks good for you"; Verdict.CAUTION -> "Caution"; Verdict.AVOID -> "Avoid"; Verdict.UNKNOWN -> "Not enough info" },
                        style = MaterialTheme.typography.headlineSmall, color = vfg, modifier = Modifier.weight(1f),
                    )
                    VerdictPill(a.verdict)
                }
                a.subtitle?.let { Hint(it) }
                Spacer(Modifier.height(6.dp))
                if (a.verdictReasons.isEmpty()) Text("Nothing from your avoid list, and low FODMAP at ${portionName(a.portion)}.", style = MaterialTheme.typography.bodyMedium)
                a.verdictReasons.forEach { Text("• $it", style = MaterialTheme.typography.bodyMedium) }
                product?.userVerdict?.let { Text(if (it == "fine") "You marked this fine for you." else "You marked this bad for you.", style = MaterialTheme.typography.bodySmall, fontWeight = FontWeight.SemiBold) }
            }
        }
        if (a.findings.isNotEmpty()) {
            item { SectionTitle("Your avoid list") }
            a.findings.forEach { f ->
                item {
                    val (fg, bg) = if (f.level == HitLevel.AVOID) Pal.signals.avoid to Pal.signals.avoidBg else Pal.signals.caution to Pal.signals.cautionBg
                    InfoCard(container = bg) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Text(f.item.label, style = MaterialTheme.typography.titleMedium, color = fg, modifier = Modifier.weight(1f))
                            Pill(when (f.level) { HitLevel.AVOID -> "Contains"; HitLevel.CAUTION -> "Possibly"; HitLevel.TRACE -> "May contain" }, fg, MaterialTheme.colorScheme.surface)
                        }
                        f.evidence.take(5).forEach { e ->
                            Text("\"${e.text}\": ${e.reason}", style = MaterialTheme.typography.bodyMedium, modifier = Modifier.padding(top = 4.dp))
                        }
                    }
                }
            }
        }
        item {
            SectionTitle("FODMAP") { Hint(a.fodmapSource.label + " · " + a.confidence.label.lowercase() + " confidence") }
            InfoCard {
                Row(verticalAlignment = Alignment.Bottom) {
                    Text("%.1f".format(at.personalScore ?: at.score), style = MaterialTheme.typography.headlineLarge, color = Pal.fodmap(at.level).first)
                    Text(" / 10  ${at.level.label} at ${fmtG(g)}", style = MaterialTheme.typography.titleMedium, modifier = Modifier.padding(bottom = 6.dp))
                }
                if (at.personalScore != null) Hint("For you: ${"%.1f".format(at.personalScore)} (standard ${"%.1f".format(at.score)}), adjusted by your test results.")
                FodmapGauge(at.personalScore ?: at.score)
                Spacer(Modifier.height(8.dp))
                FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    a.portions.forEach { p -> FilterChip(selected = kotlin.math.abs(g - p.grams) < 0.5, onClick = { grams = p.grams }, label = { Text("${portionName(p)} · ${fmtG(p.grams)}") }) }
                }
                val base = a.portion.grams
                Slider(value = (g / base).toFloat().coerceIn(0.1f, 3f), onValueChange = { grams = (it * base).toDouble() }, valueRange = 0.1f..3f)
                a.profile.safeGrams(a.tolerance)?.let { safe -> Hint("Low FODMAP up to about ${fmtG(safe)} (${"%.1f".format(safe / base)}× ${portionName(a.portion)}).") }
                if (at.loads.isEmpty()) Text("No FODMAPs to speak of at this amount.", style = MaterialTheme.typography.bodyMedium)
                at.loads.forEach { l ->
                    val drivers = a.profile.drivers[l.group]?.take(3)?.joinToString { it.name.lowercase() }
                    LoadBar("${l.group.label}${drivers?.let { " · $it" } ?: ""}", l.score, "${"%.1f".format(l.units)}× low amount")
                }
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    TextButton(onClick = { correcting = true }) { Text("Correct this") }
                    if ((a.fodmapSource == FodmapSource.GUESS || a.fodmapSource == FodmapSource.NONE) && prefs?.braveKey?.isNotBlank() == true) {
                        TextButton(onClick = {
                            looking = true
                            scope.launch {
                                val q = if (ref.startsWith("guess:")) ref.removePrefix("guess:") else a.title
                                when (val r = c.web.lookup(q, a.portion.grams)) {
                                    is WebLookup.Outcome.Ok -> {
                                        if (!ref.startsWith("guess:") && r.result.limits.isNotEmpty()) {
                                            c.db.corrections().upsert(CorrectionEntity(ref, c.repo.encodeLimits(r.result.fodmapLimits()), FodmapSource.WEB.name, r.result.summary))
                                        }
                                        snack.showSnackbar(r.result.summary)
                                        reload++
                                    }
                                    is WebLookup.Outcome.Problem -> snack.showSnackbar(r.message)
                                }
                                looking = false
                            }
                        }, enabled = !looking) { Text(if (looking) "Looking…" else "Look it up") }
                    }
                }
            }
        }
        if (a.ingredients.isNotEmpty()) {
            item { SectionTitle(if (ref.startsWith("product:")) "Ingredients" else "What's in it") }
            item {
                InfoCard {
                    a.ingredients.forEach { m ->
                        val color = when {
                            m.avoidLabels.isNotEmpty() -> Pal.signals.avoid
                            m.cautionLabels.isNotEmpty() -> Pal.signals.caution
                            else -> MaterialTheme.colorScheme.onSurface
                        }
                        Row(Modifier.padding(start = (m.depth * 14).dp, top = 3.dp, bottom = 3.dp)) {
                            Column(Modifier.weight(1f)) {
                                Text(m.labelText.ifBlank { m.entryName ?: "" }, color = color, fontWeight = if (m.avoidLabels.isNotEmpty()) FontWeight.SemiBold else FontWeight.Normal)
                                val bits = buildList {
                                    if (m.entryName != null && !m.entryName.equals(m.labelText, true)) add(m.entryName)
                                    if (m.avoidLabels.isNotEmpty()) add(m.avoidLabels.joinToString())
                                    if (m.cautionLabels.isNotEmpty()) add("maybe " + m.cautionLabels.joinToString().lowercase())
                                    if (m.fodmapGroups.isNotEmpty()) add(m.fodmapGroups.joinToString { it.label.lowercase() })
                                    if (m.minor) add("small amount")
                                    if (m.entryId == null) add("not recognized")
                                }
                                if (bits.isNotEmpty()) Hint(bits.joinToString(" · "))
                            }
                        }
                    }
                }
            }
        }
        if (a.notes.isNotEmpty()) item {
            SectionTitle("Notes")
            a.notes.forEach { Hint("• $it", Modifier.padding(bottom = 4.dp)) }
        }
        item {
            SectionTitle("You and this food")
            val key = if (ref.startsWith("food:")) ref else "food:$ref"
            val stat = report?.statOf(key)
            InfoCard {
                Text(if (timesEaten == 0) "Not logged yet." else "Logged $timesEaten time${if (timesEaten == 1) "" else "s"}.", style = MaterialTheme.typography.bodyMedium)
                if (stat != null && stat.exposed >= 2) {
                    Text("Bad BMs followed ${stat.bad} of the ${stat.exposed} days you had it (${"%.1f".format(stat.ratio)}× your usual rate).", style = MaterialTheme.typography.bodyMedium)
                }
                if (productKey != null) Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    FilterChip(selected = product?.userVerdict == "fine", onClick = { scope.launch { product?.let { c.db.products().upsert(it.copy(userVerdict = if (it.userVerdict == "fine") null else "fine")) }; reload++ } }, label = { Text("Fine for me") })
                    FilterChip(selected = product?.userVerdict == "bad", onClick = { scope.launch { product?.let { c.db.products().upsert(it.copy(userVerdict = if (it.userVerdict == "bad") null else "bad")) }; reload++ } }, label = { Text("Bad for me") })
                }
            }
        }
        item { Spacer(Modifier.height(40.dp)) }
    }
    if (correcting && a != null) CorrectDialog(a, onDismiss = { correcting = false }) { limits ->
        scope.launch {
            if (limits == null) c.db.corrections().delete(ref) else c.db.corrections().upsert(CorrectionEntity(ref, c.repo.encodeLimits(limits), FodmapSource.USER.name))
            correcting = false
            reload++
            snack.showSnackbar(if (limits == null) "Back to the built-in estimate" else "Saved your correction")
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun CorrectDialog(a: Analysis, onDismiss: () -> Unit, onSave: (List<FodmapLimit>?) -> Unit) {
    var level by remember { mutableStateOf<FodmapLevel?>(null) }
    var none by remember { mutableStateOf(false) }
    var group by remember { mutableStateOf(a.profile.rates.keys.firstOrNull() ?: FodmapGroup.FRUCTANS) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Correct the FODMAP rating") },
        text = {
            Column {
                Text("At ${portionName(a.portion)} (${fmtG(a.portion.grams)}), this is:", style = MaterialTheme.typography.bodyMedium)
                FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    FilterChip(selected = none, onClick = { none = true; level = null }, label = { Text("No FODMAPs") })
                    FodmapLevel.entries.forEach { l -> FilterChip(selected = level == l, onClick = { level = l; none = false }, label = { Text(l.label) }) }
                }
                if (level != null) {
                    Spacer(Modifier.height(8.dp))
                    Text("Mostly:", style = MaterialTheme.typography.bodyMedium)
                    FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                        FodmapGroup.entries.forEach { g -> FilterChip(selected = group == g, onClick = { group = g }, label = { Text(g.label) }) }
                    }
                }
                Hint("Use what the Monash app or your dietitian says, or what you've noticed.")
            }
        },
        confirmButton = {
            TextButton(enabled = none || level != null, onClick = {
                val p = a.portion.grams
                onSave(
                    if (none) emptyList() else when (level!!) {
                        FodmapLevel.LOW -> listOf(FodmapLimit(group, p * 1.2, p * 2.4))
                        FodmapLevel.MODERATE -> listOf(FodmapLimit(group, p * 0.6, p * 1.25))
                        FodmapLevel.HIGH -> listOf(FodmapLimit(group, p * 0.25, p * 0.5))
                    },
                )
            }) { Text("Save") }
        },
        dismissButton = { Row { TextButton(onClick = { onSave(null) }) { Text("Reset") }; TextButton(onClick = onDismiss) { Text("Cancel") } } },
    )
}

fun mealFor(hour: Int): String = when (hour) { in 4..10 -> "breakfast"; in 11..14 -> "lunch"; in 15..16 -> "snack"; in 17..21 -> "dinner"; else -> "snack" }

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun LogFoodScreen(ref: String) {
    val c = LocalContainer.current
    val nav = LocalNav.current
    val snack = LocalSnack.current
    val ctx = androidx.compose.ui.platform.LocalContext.current
    val scope = rememberUiScope()
    var a by remember { mutableStateOf<Analysis?>(null) }
    var grams by rememberSaveable { mutableStateOf(-1.0) }
    var gramsText by rememberSaveable { mutableStateOf("") }
    var portionLabel by rememberSaveable { mutableStateOf("") }
    var time by rememberSaveable { mutableStateOf(System.currentTimeMillis()) }
    var meal by rememberSaveable { mutableStateOf(mealFor(LocalTime.now().hour)) }
    var note by rememberSaveable { mutableStateOf("") }
    var stack by remember { mutableStateOf<com.baastiklabs.pickless.core.analysis.MealFodmap?>(null) }
    val prefs by c.settings.prefs.collectAsState(initial = null)
    LaunchedEffect(ref) {
        a = c.repo.analyze(ref)
        a?.let { if (grams < 0) { grams = it.portion.grams; gramsText = it.portion.grams.toInt().toString(); portionLabel = portionName(it.portion) } }
    }
    val an = a
    LaunchedEffect(an, grams, time, meal) {
        if (an == null || grams <= 0) return@LaunchedEffect
        val earlier = c.db.food().betweenOnce(time - 90 * 60_000L, time + 30 * 60_000L).filter { it.meal == meal }
        val parts = earlier.map { e -> com.baastiklabs.pickless.core.fodmap.FodmapProfile(c.repo.features(e).fodmapPerGram.mapNotNull { (k, v) -> FodmapGroup.byId(k)?.let { it to com.baastiklabs.pickless.core.fodmap.FodmapProfile.Rate(v, 2.0) } }.toMap()) to e.grams } + (an.profile to grams)
        stack = if (earlier.isEmpty()) null else c.repo.analyzer.meal(parts, prefs?.tolerance ?: emptyMap())
    }
    Page("Log ${an?.title ?: ""}") {
        if (an == null) { item { CircularProgressIndicator(Modifier.padding(24.dp)) }; return@Page }
        item {
            SectionTitle("How much")
            FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                for (p in an.portions) for (mult in listOf(0.5, 1.0, 1.5, 2.0)) {
                    if (p.name == "100 g" && mult != 1.0) continue
                    val gg = p.grams * mult
                    val unit = portionName(p).removePrefix("1 ")
                    val plural = if (mult > 1.0 && !unit.endsWith("s") && unit != "whole") unit + "s" else unit
                    val label = if (mult == 1.0) portionName(p) else "${if (mult == 0.5) "½" else if (mult == 1.5) "1½" else "2"} $plural"
                    FilterChip(selected = kotlin.math.abs(gg - grams) < 0.5, onClick = { grams = gg; gramsText = gg.toInt().toString(); portionLabel = label }, label = { Text(label) })
                }
            }
            OutlinedTextField(gramsText, { t -> gramsText = t.filter { it.isDigit() || it == '.' }; gramsText.toDoubleOrNull()?.let { grams = it; portionLabel = "${it.toInt()} g" } },
                label = { Text("Grams") }, singleLine = true, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal), modifier = Modifier.width(160.dp))
            val at = an.fodmapAt(grams.coerceAtLeast(1.0))
            Spacer(Modifier.height(6.dp))
            Text("FODMAP at this amount: ${"%.1f".format(at.personalScore ?: at.score)} (${at.level.label.lowercase()})", color = Pal.fodmap(at.level).first)
        }
        stack?.let { s ->
            item {
                Spacer(Modifier.height(8.dp))
                val lvl = FodmapScore.level(s.score)
                InfoCard(container = Pal.fodmap(lvl).second) {
                    Text("This ${meal} so far: FODMAP ${"%.1f".format(s.score)} (${lvl.label.lowercase()})", style = MaterialTheme.typography.titleSmall)
                    if (s.stacked) Text("Each food is fine alone, but together they stack up${s.loads.firstOrNull()?.let { " in ${it.group.label.lowercase()}" } ?: ""}.", style = MaterialTheme.typography.bodyMedium)
                }
            }
        }
        item {
            SectionTitle("When")
            TimeField(time, { time = it; meal = mealFor(java.time.Instant.ofEpochMilli(it).atZone(c.repo.zone).hour) })
            Spacer(Modifier.height(8.dp))
            com.baastiklabs.pickless.ui.components.ChoiceRow(listOf("breakfast", "lunch", "dinner", "snack", "drink").map { it to it.replaceFirstChar { ch -> ch.uppercase() } }, meal, { meal = it })
            OutlinedTextField(note, { note = it }, label = { Text("Note (optional)") }, modifier = Modifier.fillMaxWidth().padding(top = 8.dp))
            Spacer(Modifier.height(16.dp))
            Button(onClick = {
                scope.launch {
                    c.repo.logFood(ref, an, grams, portionLabel.ifBlank { fmtG(grams) }, time, meal, note.ifBlank { null })
                    c.insights.refreshSoon()
                    prefs?.let { if (it.nudges && System.currentTimeMillis() - time < 60 * 60_000L) Jobs.nudgeAfterMeal(ctx, meal, it.nudgeHours) }
                    snack.showSnackbar("Logged ${an.title}")
                    nav.popBackStack("today", inclusive = false).let { ok -> if (!ok) nav.navigate("today") }
                }
            }, modifier = Modifier.fillMaxWidth().height(52.dp)) { Text("Log it") }
            HorizontalDivider(Modifier.padding(vertical = 16.dp))
            Hint("Logged foods feed the trigger detective. If it's ${Analyzer.portionText(an.portion)} most days, logging it every time still helps: absences matter too.")
        }
    }
}
