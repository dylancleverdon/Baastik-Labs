package com.baastiklabs.pickless.ui.components

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Remove
import androidx.compose.material.icons.outlined.Schedule
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.DatePicker
import androidx.compose.material3.DatePickerDialog
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TimePicker
import androidx.compose.material3.rememberDatePickerState
import androidx.compose.material3.rememberTimePickerState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.baastiklabs.pickless.core.analysis.Verdict
import com.baastiklabs.pickless.core.fodmap.FodmapLevel
import com.baastiklabs.pickless.core.fodmap.FodmapScore
import com.baastiklabs.pickless.ui.theme.Pal
import java.time.Instant
import java.time.LocalDate
import java.time.LocalDateTime
import java.time.ZoneId
import java.time.format.DateTimeFormatter
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.sin

@Composable
fun SectionTitle(text: String, modifier: Modifier = Modifier, action: (@Composable () -> Unit)? = null) {
    Row(modifier.fillMaxWidth().padding(top = 18.dp, bottom = 8.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(text, style = MaterialTheme.typography.titleMedium, modifier = Modifier.weight(1f))
        action?.invoke()
    }
}

@Composable
fun Pill(text: String, fg: Color, bg: Color, modifier: Modifier = Modifier) {
    Text(
        text, color = fg, style = MaterialTheme.typography.labelMedium, fontWeight = FontWeight.SemiBold,
        modifier = modifier.clip(RoundedCornerShape(50)).background(bg).padding(horizontal = 10.dp, vertical = 4.dp),
    )
}

@Composable
fun VerdictPill(v: Verdict, modifier: Modifier = Modifier) {
    val (fg, bg) = Pal.verdict(v)
    Pill(when (v) { Verdict.GOOD -> "✓ Good"; Verdict.CAUTION -> "! Caution"; Verdict.AVOID -> "✕ Avoid"; Verdict.UNKNOWN -> "? Not sure" }, fg, bg, modifier)
}

@Composable
fun FodmapPill(score: Double, modifier: Modifier = Modifier) {
    val level = FodmapScore.level(score)
    val (fg, bg) = Pal.fodmap(level)
    Pill("FODMAP ${"%.1f".format(score)}", fg, bg, modifier)
}

@Composable
fun InfoCard(modifier: Modifier = Modifier, container: Color = MaterialTheme.colorScheme.surfaceContainer, onClick: (() -> Unit)? = null, content: @Composable () -> Unit) {
    val m = modifier.fillMaxWidth()
    if (onClick != null) {
        Card(onClick = onClick, modifier = m, colors = CardDefaults.cardColors(containerColor = container), shape = RoundedCornerShape(18.dp)) {
            Column(Modifier.padding(16.dp)) { content() }
        }
    } else {
        Card(modifier = m, colors = CardDefaults.cardColors(containerColor = container), shape = RoundedCornerShape(18.dp)) {
            Column(Modifier.padding(16.dp)) { content() }
        }
    }
}

/** 0-10 gauge with low / moderate / high zones and a marker. */
@Composable
fun FodmapGauge(score: Double, modifier: Modifier = Modifier, height: Dp = 14.dp) {
    val s = Pal.signals
    val track = MaterialTheme.colorScheme.onSurface
    Canvas(modifier.fillMaxWidth().height(height + 10.dp).semantics { contentDescription = "FODMAP score ${"%.1f".format(score)} out of 10" }) {
        val h = height.toPx()
        val top = 5.dp.toPx()
        val w = size.width
        val r = CornerRadius(h / 2, h / 2)
        drawRoundRect(s.goodBg, Offset(0f, top), Size(w * 0.3f, h), r)
        drawRect(s.cautionBg, Offset(w * 0.3f, top), Size(w * 0.3f, h))
        drawRoundRect(s.avoidBg, Offset(w * 0.6f, top), Size(w * 0.4f, h), r)
        drawRect(s.avoidBg, Offset(w * 0.6f, top), Size(h, h))
        val x = (score.coerceIn(0.0, 10.0) / 10.0 * w).toFloat()
        val color = when (FodmapScore.level(score)) { FodmapLevel.LOW -> s.good; FodmapLevel.MODERATE -> s.caution; FodmapLevel.HIGH -> s.avoid }
        drawCircle(color, radius = h * 0.75f, center = Offset(x.coerceIn(h, w - h), top + h / 2))
        drawCircle(track.copy(alpha = 0.0f), radius = 1f, center = Offset(0f, 0f))
    }
}

@Composable
fun LoadBar(label: String, score: Double, detail: String, modifier: Modifier = Modifier) {
    val (fg, bg) = Pal.fodmap(FodmapScore.level(score))
    Column(modifier.fillMaxWidth().padding(vertical = 5.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(label, style = MaterialTheme.typography.bodyMedium, modifier = Modifier.weight(1f))
            Text(detail, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        Spacer(Modifier.height(4.dp))
        Box(Modifier.fillMaxWidth().height(8.dp).clip(RoundedCornerShape(4.dp)).background(bg)) {
            Box(Modifier.fillMaxWidth((score / 10.0).toFloat().coerceIn(0.03f, 1f)).height(8.dp).clip(RoundedCornerShape(4.dp)).background(fg))
        }
    }
}

/** Plain-shape reference drawings for the Bristol Stool Scale (no photos, ever). */
@Composable
fun BristolGlyph(type: Int, modifier: Modifier = Modifier, color: Color = Color(0xFF8B6B4A)) {
    Canvas(modifier.size(54.dp, 32.dp)) {
        val w = size.width
        val h = size.height
        val cy = h / 2
        when (type) {
            1 -> listOf(0.14f, 0.36f, 0.6f, 0.84f).forEachIndexed { i, fx -> drawCircle(color, radius = h * (0.17f + (i % 2) * 0.03f), center = Offset(w * fx, cy + if (i % 2 == 0) -3f else 4f)) }
            2 -> { for (i in 0..5) drawCircle(color, radius = h * 0.24f, center = Offset(w * (0.12f + i * 0.152f), cy + if (i % 2 == 0) -2f else 2f)) }
            3 -> {
                drawRoundRect(color, Offset(w * 0.04f, cy - h * 0.24f), Size(w * 0.92f, h * 0.48f), CornerRadius(h * 0.24f))
                val crack = color.copy(alpha = 1f).let { Color(it.red * 0.6f, it.green * 0.6f, it.blue * 0.6f) }
                for (i in 1..4) drawLine(crack, Offset(w * (0.18f * i), cy - h * 0.24f), Offset(w * (0.18f * i + 0.03f), cy - h * 0.06f), strokeWidth = 2.2f, cap = StrokeCap.Round)
            }
            4 -> {
                val p = Path().apply {
                    moveTo(w * 0.06f, cy)
                    cubicTo(w * 0.3f, cy - h * 0.34f, w * 0.55f, cy + h * 0.34f, w * 0.94f, cy - h * 0.04f)
                }
                drawPath(p, color, style = Stroke(width = h * 0.36f, cap = StrokeCap.Round))
            }
            5 -> listOf(0.2f, 0.5f, 0.8f).forEach { fx -> drawOval(color, Offset(w * fx - w * 0.12f, cy - h * 0.2f), Size(w * 0.24f, h * 0.4f)) }
            6 -> {
                val p = Path()
                val n = 14
                for (i in 0..n) {
                    val a = 2 * PI * i / n
                    val rr = (if (i % 2 == 0) 0.42f else 0.31f)
                    val x = w * 0.5f + (cos(a) * w * rr).toFloat()
                    val y = cy + (sin(a) * h * (rr + 0.04f)).toFloat()
                    if (i == 0) p.moveTo(x, y) else p.lineTo(x, y)
                }
                p.close()
                drawPath(p, color)
            }
            else -> {
                for (row in 0..1) {
                    val p = Path()
                    val y0 = cy - h * 0.16f + row * h * 0.32f
                    p.moveTo(w * 0.06f, y0)
                    for (i in 1..6) p.quadraticTo(w * (0.06f + 0.147f * i - 0.07f), y0 + if (i % 2 == 0) 5f else -5f, w * (0.06f + 0.147f * i), y0)
                    drawPath(p, color, style = Stroke(width = 4f, cap = StrokeCap.Round))
                }
            }
        }
    }
}

val bristolNames = mapOf(
    1 to "Separate hard lumps", 2 to "Lumpy and hard", 3 to "Sausage with cracks", 4 to "Smooth and soft",
    5 to "Soft blobs", 6 to "Mushy, ragged", 7 to "Watery",
)

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun BristolPicker(selected: Int?, onPick: (Int) -> Unit, modifier: Modifier = Modifier) {
    val s = Pal.signals
    Column(modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        for (t in 1..7) {
            val tone = when (t) { 3, 4 -> s.good; 2, 5 -> s.caution; else -> s.avoid }
            val isSel = selected == t
            Surface(
                onClick = { onPick(t) }, shape = RoundedCornerShape(14.dp),
                color = if (isSel) MaterialTheme.colorScheme.primaryContainer else MaterialTheme.colorScheme.surfaceContainer,
                border = if (isSel) BorderStroke(2.dp, MaterialTheme.colorScheme.primary) else null,
                modifier = Modifier.fillMaxWidth().semantics { contentDescription = "Type $t, ${bristolNames[t]}" },
            ) {
                Row(Modifier.padding(horizontal = 14.dp, vertical = 10.dp), verticalAlignment = Alignment.CenterVertically) {
                    Box(Modifier.size(30.dp).clip(CircleShape).background(tone.copy(alpha = 0.16f)), contentAlignment = Alignment.Center) {
                        Text("$t", fontWeight = FontWeight.Bold, color = tone)
                    }
                    Spacer(Modifier.width(12.dp))
                    BristolGlyph(t)
                    Spacer(Modifier.width(12.dp))
                    Text(bristolNames[t] ?: "", style = MaterialTheme.typography.bodyLarge, modifier = Modifier.weight(1f))
                }
            }
        }
    }
}

/** A row of choices (single select). */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun <T> ChoiceRow(options: List<Pair<T, String>>, selected: T?, onSelect: (T) -> Unit, modifier: Modifier = Modifier) {
    FlowRow(modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
        for ((value, label) in options) FilterChip(selected = value == selected, onClick = { onSelect(value) }, label = { Text(label) })
    }
}

val severityLabels = listOf("None", "Mild", "Moderate", "Severe", "Very severe")

@Composable
fun SeverityPicker(value: Int?, onChange: (Int) -> Unit, modifier: Modifier = Modifier, labels: List<String> = severityLabels) {
    ChoiceRow(labels.mapIndexed { i, l -> i to l }, value, onChange, modifier)
}

@Composable
fun Stepper(value: Int, onChange: (Int) -> Unit, label: String, modifier: Modifier = Modifier, max: Int = 30) {
    Row(modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        Text(label, style = MaterialTheme.typography.bodyLarge, modifier = Modifier.weight(1f))
        IconButton(onClick = { onChange((value - 1).coerceAtLeast(0)) }) { Icon(Icons.Filled.Remove, "Less") }
        Text("$value", style = MaterialTheme.typography.titleMedium, modifier = Modifier.width(32.dp), textAlign = TextAlign.Center)
        IconButton(onClick = { onChange((value + 1).coerceAtMost(max)) }) { Icon(Icons.Filled.Add, "More") }
    }
}

private val timeFmt = DateTimeFormatter.ofPattern("h:mm a")
private val dayFmt = DateTimeFormatter.ofPattern("EEE MMM d")

fun whenText(time: Long, zone: ZoneId = ZoneId.systemDefault()): String {
    val dt = Instant.ofEpochMilli(time).atZone(zone)
    val today = LocalDate.now(zone)
    val day = when (dt.toLocalDate()) { today -> "Today"; today.minusDays(1) -> "Yesterday"; else -> dt.format(dayFmt) }
    return "$day ${dt.format(timeFmt)}"
}

fun timeText(time: Long, zone: ZoneId = ZoneId.systemDefault()): String = Instant.ofEpochMilli(time).atZone(zone).format(timeFmt)

/** Shows a time ("Today 8:30 AM") and lets you change date and time. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun TimeField(time: Long, onChange: (Long) -> Unit, modifier: Modifier = Modifier) {
    var pickDate by remember { mutableStateOf(false) }
    var pickTime by remember { mutableStateOf(false) }
    var date by remember(time) { mutableStateOf(Instant.ofEpochMilli(time).atZone(ZoneId.systemDefault()).toLocalDate()) }
    OutlinedButton(onClick = { pickDate = true }, modifier = modifier, contentPadding = PaddingValues(horizontal = 14.dp, vertical = 8.dp)) {
        Icon(Icons.Outlined.Schedule, null, Modifier.size(18.dp))
        Spacer(Modifier.width(8.dp))
        Text(whenText(time))
    }
    if (pickDate) {
        val st = rememberDatePickerState(initialSelectedDateMillis = date.atStartOfDay(ZoneId.of("UTC")).toInstant().toEpochMilli())
        DatePickerDialog(
            onDismissRequest = { pickDate = false },
            confirmButton = {
                TextButton(onClick = {
                    st.selectedDateMillis?.let { date = Instant.ofEpochMilli(it).atZone(ZoneId.of("UTC")).toLocalDate() }
                    pickDate = false; pickTime = true
                }) { Text("Next") }
            },
            dismissButton = { TextButton(onClick = { pickDate = false }) { Text("Cancel") } },
        ) { DatePicker(st) }
    }
    if (pickTime) {
        val cur = Instant.ofEpochMilli(time).atZone(ZoneId.systemDefault())
        val st = rememberTimePickerState(cur.hour, cur.minute, false)
        AlertDialog(
            onDismissRequest = { pickTime = false },
            confirmButton = {
                TextButton(onClick = {
                    val dt = LocalDateTime.of(date, java.time.LocalTime.of(st.hour, st.minute))
                    onChange(dt.atZone(ZoneId.systemDefault()).toInstant().toEpochMilli().coerceAtMost(System.currentTimeMillis()))
                    pickTime = false
                }) { Text("Done") }
            },
            dismissButton = { TextButton(onClick = { pickTime = false }) { Text("Cancel") } },
            text = { TimePicker(st) },
        )
    }
}

@Composable
fun Hint(text: String, modifier: Modifier = Modifier) {
    Text(text, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = modifier)
}

@Composable
fun KeyValue(label: String, value: String, modifier: Modifier = Modifier) {
    Row(modifier.fillMaxWidth().padding(vertical = 3.dp)) {
        Text(label, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.weight(1f))
        Text(value, style = MaterialTheme.typography.bodyMedium, fontWeight = FontWeight.Medium)
    }
}

@Composable
fun Banner(title: String, body: String, fg: Color, bg: Color, modifier: Modifier = Modifier, onClick: (() -> Unit)? = null) {
    Column(
        modifier.fillMaxWidth().clip(RoundedCornerShape(16.dp)).background(bg).border(1.dp, fg.copy(alpha = 0.25f), RoundedCornerShape(16.dp))
            .let { if (onClick != null) it.clickable(onClick = onClick) else it }.padding(14.dp),
    ) {
        Text(title, color = fg, style = MaterialTheme.typography.titleSmall)
        Spacer(Modifier.height(2.dp))
        Text(body, color = MaterialTheme.colorScheme.onSurface, style = MaterialTheme.typography.bodyMedium)
    }
}
