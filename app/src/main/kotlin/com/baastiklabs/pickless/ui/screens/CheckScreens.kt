package com.baastiklabs.pickless.ui.screens

import androidx.camera.core.ImageCapture
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.CameraAlt
import androidx.compose.material.icons.filled.QrCodeScanner
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.ShoppingCart
import androidx.compose.material.icons.filled.Star
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.FilledTonalButton
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
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.unit.dp
import com.baastiklabs.pickless.camera.Buzz
import com.baastiklabs.pickless.camera.CameraView
import com.baastiklabs.pickless.camera.barcodeAnalysis
import com.baastiklabs.pickless.camera.captureText
import com.baastiklabs.pickless.camera.ingredientsSection
import com.baastiklabs.pickless.camera.rememberCameraPermission
import com.baastiklabs.pickless.camera.vibrate
import com.baastiklabs.pickless.core.analysis.ProductInput
import com.baastiklabs.pickless.core.analysis.Verdict
import com.baastiklabs.pickless.core.barcode.Gtin
import com.baastiklabs.pickless.core.kb.EntryKind
import com.baastiklabs.pickless.data.RecentRef
import com.baastiklabs.pickless.net.Lookup
import com.baastiklabs.pickless.suggest.SuggestionBox
import com.baastiklabs.pickless.ui.components.Hint
import com.baastiklabs.pickless.ui.components.SectionTitle
import com.baastiklabs.pickless.ui.components.VerdictPill
import com.baastiklabs.pickless.ui.theme.Pal
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import java.util.UUID

@Composable
fun CheckScreen() {
    val c = LocalContainer.current
    val nav = LocalNav.current
    val scope = rememberUiScope()
    var query by rememberSaveable { mutableStateOf("") }
    val prefs by c.settings.prefs.collectAsState(initial = null)
    val custom by c.db.custom().all().collectAsState(initial = emptyList())
    val favProducts by c.db.products().favorites().collectAsState(initial = emptyList())
    var recent by remember { mutableStateOf<List<RecentRef>>(emptyList()) }
    var online by remember { mutableStateOf<List<ProductInput>>(emptyList()) }
    var searching by remember { mutableStateOf(false) }
    LaunchedEffect(Unit) { recent = c.db.food().recentRefs(12) }
    LaunchedEffect(query) {
        online = emptyList()
        if (query.trim().length < 3) return@LaunchedEffect
        delay(650)
        searching = true
        online = runCatching { c.products.search(query.trim()) }.getOrDefault(emptyList())
        searching = false
    }
    val kbHits = remember(query) { if (query.isBlank()) emptyList() else c.kb.search(query, 20) }

    fun openProduct(p: ProductInput) = scope.launch {
        val key = p.code?.let { Gtin.key(it) } ?: "name:${UUID.randomUUID()}"
        c.products.save(key, p.copy(code = key))
        nav.navigate("result/${enc("product:$key")}")
    }

    LazyColumn(Modifier.statusBarsPadding().padding(horizontal = 16.dp)) {
        item {
            Text("Check a food", style = MaterialTheme.typography.headlineMedium, modifier = Modifier.padding(top = 18.dp, bottom = 10.dp))
            OutlinedTextField(
                value = query, onValueChange = { query = it }, modifier = Modifier.fillMaxWidth(), singleLine = true,
                placeholder = { Text("Type a food, dish or product") }, leadingIcon = { Icon(Icons.Filled.Search, null) },
                keyboardOptions = KeyboardOptions(imeAction = ImeAction.Search), keyboardActions = KeyboardActions(onSearch = {}),
                shape = RoundedCornerShape(16.dp),
            )
            Spacer(Modifier.height(10.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                FilledTonalButton(onClick = { nav.navigate("scan?mode=product") }, modifier = Modifier.weight(1f)) { Icon(Icons.Filled.QrCodeScanner, null); Spacer(Modifier.width(6.dp)); Text("Scan") }
                FilledTonalButton(onClick = { nav.navigate("label?code=&name=") }, modifier = Modifier.weight(1f)) { Icon(Icons.Filled.CameraAlt, null); Spacer(Modifier.width(6.dp)); Text("Label") }
                FilledTonalButton(onClick = { nav.navigate("scan?mode=shopping") }, modifier = Modifier.weight(1f)) { Icon(Icons.Filled.ShoppingCart, null); Spacer(Modifier.width(6.dp)); Text("Shop") }
            }
        }
        if (query.isBlank()) {
            val favRefs = prefs?.favorites ?: emptySet()
            if (favRefs.isNotEmpty() || favProducts.isNotEmpty() || custom.isNotEmpty()) item { SectionTitle("Favorites & your meals") }
            items(custom.filter { it.favorite }, key = { "custom${it.id}" }) { cf -> Row1(cf.name, "Your meal") { nav.navigate("result/${enc("custom:${cf.id}")}") } }
            items(favProducts, key = { "fp${it.key}" }) { p -> Row1(p.name, "Product", star = true) { nav.navigate("result/${enc("product:${p.key}")}") } }
            items(favRefs.filter { it.startsWith("food:") }.toList(), key = { "ff$it" }) { ref -> Row1(c.kb[ref.removePrefix("food:")]?.name ?: ref, "Food", star = true) { nav.navigate("result/${enc(ref)}") } }
            if (recent.isNotEmpty()) item { SectionTitle("Recent") }
            items(recent, key = { "r${it.ref}" }) { r -> Row1(r.name, refKind(r.ref)) { nav.navigate("result/${enc(r.ref)}") } }
            item { Hint("Tip: \"soy milk\" shows both kinds, because whole-bean and soy-protein soy milk score very differently.", Modifier.padding(top = 12.dp)) }
        } else {
            if (kbHits.isNotEmpty()) item { SectionTitle("Foods & dishes") }
            items(kbHits, key = { "kb${it.entry.id}" }) { h ->
                Row1(h.entry.name, if (h.entry.kind == EntryKind.DISH) "Dish (typical recipe)" else h.entry.category.replace('-', ' ')) { nav.navigate("result/${enc("food:${h.entry.id}")}") }
            }
            val mine = custom.filter { it.name.contains(query.trim(), ignoreCase = true) }
            if (mine.isNotEmpty()) item { SectionTitle("Your meals") }
            items(mine, key = { "m${it.id}" }) { cf -> Row1(cf.name, "Your meal") { nav.navigate("result/${enc("custom:${cf.id}")}") } }
            item {
                SectionTitle("Products") { if (searching) CircularProgressIndicator(Modifier.padding(4.dp).width(18.dp).height(18.dp), strokeWidth = 2.dp) }
                if (!searching && online.isEmpty() && query.trim().length >= 3) Hint("No products found online (or you're offline).")
            }
            items(online, key = { "p${it.code ?: it.name}" }) { p -> Row1(p.name ?: "Product", listOfNotNull(p.brand, if (p.source == "usda") "US pack" else "Open Food Facts").joinToString(" · ")) { openProduct(p) } }
            item {
                SectionTitle("Not finding it?")
                OutlinedButton(onClick = { nav.navigate("result/${enc("guess:${query.trim()}")}") }, modifier = Modifier.fillMaxWidth()) {
                    Text("Check \"${query.trim()}\" as typed (educated guess)")
                }
            }
        }
        item { Spacer(Modifier.height(24.dp)) }
    }
}

private fun refKind(ref: String) = when (ref.substringBefore(':')) { "product" -> "Product"; "custom" -> "Your meal"; "guess" -> "Typed"; else -> "Food" }

@Composable
private fun Row1(title: String, subtitle: String, star: Boolean = false, onClick: () -> Unit) {
    ListItem(
        headlineContent = { Text(title) }, supportingContent = { Text(subtitle) },
        trailingContent = { if (star) Icon(Icons.Filled.Star, null, tint = MaterialTheme.colorScheme.secondary) },
        colors = ListItemDefaults.colors(containerColor = Color.Transparent),
        modifier = Modifier.clip(RoundedCornerShape(12.dp)).clickable(onClick = onClick),
    )
}

/** Barcode scanning for one product, shopping mode, or the suggestion sheet's QR code. */
@Composable
fun ScanScreen(mode: String) {
    val c = LocalContainer.current
    val nav = LocalNav.current
    val snack = LocalSnack.current
    val ctx = LocalContext.current
    val scope = rememberUiScope()
    val (granted, ask) = rememberCameraPermission()
    var busy by remember { mutableStateOf(false) }
    var last by remember { mutableStateOf("") }
    var notFound by remember { mutableStateOf<String?>(null) }
    val shopping = remember { mutableStateListOf<Triple<String, String, Verdict>>() }
    val qr = mode == "qr"

    val analysis = remember {
        barcodeAnalysis(qr) { raw ->
            if (busy || raw == last) return@barcodeAnalysis
            last = raw
            busy = true
            scope.launch {
                try {
                    if (qr) {
                        if (SuggestionBox.validEndpoint(raw)) {
                            c.settings.setEndpoint(raw)
                            val ok = c.suggestions.test(raw)
                            vibrate(ctx, Buzz.GOOD)
                            snack.showSnackbar(if (ok) "Connected to your suggestion sheet" else "Saved. The sheet didn't answer yet; suggestions will keep trying.")
                            com.baastiklabs.pickless.work.Jobs.sendSuggestions(ctx)
                            nav.popBackStack()
                        } else snack.showSnackbar("That QR code isn't the suggestion sheet's link")
                        return@launch
                    }
                    when (val r = c.products.lookup(raw)) {
                        is Lookup.Found -> {
                            val a = c.repo.analyze("product:${r.product.key}")
                            val v = a?.verdict ?: Verdict.UNKNOWN
                            vibrate(ctx, when (v) { Verdict.AVOID -> Buzz.AVOID; Verdict.GOOD -> Buzz.GOOD; else -> Buzz.CAUTION })
                            if (mode == "shopping") shopping.add(0, Triple(r.product.key, r.product.name, v))
                            else nav.navigate("result/${enc("product:${r.product.key}")}") { popUpTo("scan?mode={mode}") { inclusive = true } }
                        }
                        is Lookup.NotFound -> { vibrate(ctx, Buzz.CAUTION); notFound = r.key }
                        Lookup.Invalid -> {}
                    }
                } finally {
                    delay(if (mode == "shopping") 1200 else 400)
                    busy = false
                    if (mode == "shopping") last = ""
                }
            }
        }
    }
    Page(when (mode) { "shopping" -> "Shopping mode"; "qr" -> "Scan the sheet's QR code"; else -> "Scan a barcode" }) {
        item {
            if (!granted) {
                Column(Modifier.fillMaxWidth().padding(24.dp), horizontalAlignment = Alignment.CenterHorizontally) {
                    Text("Pickless needs the camera to read barcodes.")
                    Button(onClick = ask) { Text("Allow camera") }
                }
            } else {
                Box(Modifier.fillMaxWidth().aspectRatio(if (mode == "shopping") 1.6f else 0.9f).clip(RoundedCornerShape(20.dp))) {
                    CameraView(Modifier.fillMaxSize(), analysis, null)
                    Box(Modifier.align(Alignment.Center).fillMaxWidth(0.75f).height(2.dp).background(MaterialTheme.colorScheme.primary.copy(alpha = 0.8f)))
                    if (busy && mode != "shopping") CircularProgressIndicator(Modifier.align(Alignment.Center))
                }
                Hint(
                    when (mode) {
                        "shopping" -> "Keep scanning: one buzz is fine, two buzzes is caution, one long buzz is avoid."
                        "qr" -> "Open the sheet's script link on your computer: it shows a QR code. Point the camera at it."
                        else -> "Hold the barcode inside the frame. Works offline for anything you've scanned before."
                    },
                    Modifier.padding(top = 10.dp),
                )
            }
        }
        if (mode == "shopping") {
            items(shopping, key = { it.first + it.second.hashCode() }) { (key, name, v) ->
                val (fg, bg) = Pal.verdict(v)
                Row(Modifier.fillMaxWidth().padding(top = 8.dp).clip(RoundedCornerShape(14.dp)).background(bg).padding(12.dp), verticalAlignment = Alignment.CenterVertically) {
                    Text(name, color = MaterialTheme.colorScheme.onSurface, modifier = Modifier.weight(1f))
                    VerdictPill(v)
                    TextButton(onClick = { nav.navigate("result/${enc("product:$key")}") }) { Text("Details", color = fg) }
                }
            }
        }
    }
    notFound?.let { key ->
        AlertDialog(
            onDismissRequest = { notFound = null; last = "" },
            title = { Text("Not in the databases yet") },
            text = { Text("Snap the ingredients label and Pickless will read it and remember this barcode.") },
            confirmButton = { TextButton(onClick = { notFound = null; nav.navigate("label?code=${enc(key)}&name=") }) { Text("Snap the label") } },
            dismissButton = { TextButton(onClick = { notFound = null; last = "" }) { Text("Keep scanning") } },
        )
    }
}

/** Photograph an ingredients list; Pickless reads it on the phone and checks it. */
@Composable
fun LabelScreen(code: String?, name: String?) {
    val c = LocalContainer.current
    val nav = LocalNav.current
    val ctx = LocalContext.current
    val scope = rememberUiScope()
    val (granted, ask) = rememberCameraPermission()
    val capture = remember { ImageCapture.Builder().setCaptureMode(ImageCapture.CAPTURE_MODE_MAXIMIZE_QUALITY).build() }
    var text by rememberSaveable { mutableStateOf("") }
    var productName by rememberSaveable { mutableStateOf(name ?: "") }
    var reading by remember { mutableStateOf(false) }
    var showCamera by rememberSaveable { mutableStateOf(true) }
    Page("Snap the label") {
        item {
            if (showCamera) {
                if (!granted) Button(onClick = ask) { Text("Allow camera") } else {
                    Box(Modifier.fillMaxWidth().aspectRatio(0.8f).clip(RoundedCornerShape(20.dp))) {
                        CameraView(Modifier.fillMaxSize(), null, capture)
                        if (reading) CircularProgressIndicator(Modifier.align(Alignment.Center))
                    }
                    Spacer(Modifier.height(10.dp))
                    Button(onClick = {
                        reading = true
                        scope.launch {
                            val ocr = captureText(ctx, capture)
                            reading = false
                            if (ocr != null) { text = ingredientsSection(ocr); showCamera = false }
                        }
                    }, modifier = Modifier.fillMaxWidth(), enabled = !reading) { Icon(Icons.Filled.CameraAlt, null); Spacer(Modifier.width(8.dp)); Text("Read the ingredients") }
                    Hint("Fill the frame with the ingredients list, in good light. Nothing leaves your phone.", Modifier.padding(top = 8.dp))
                    TextButton(onClick = { showCamera = false }) { Text("Type it instead") }
                }
            } else {
                OutlinedTextField(productName, { productName = it }, label = { Text("Product name") }, modifier = Modifier.fillMaxWidth(), singleLine = true)
                Spacer(Modifier.height(8.dp))
                OutlinedTextField(text, { text = it }, label = { Text("Ingredients (fix any misread words)") }, modifier = Modifier.fillMaxWidth().height(260.dp))
                Spacer(Modifier.height(10.dp))
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    OutlinedButton(onClick = { showCamera = true }, modifier = Modifier.weight(1f)) { Text("Retake") }
                    Button(onClick = {
                        scope.launch {
                            val key = code?.let { Gtin.key(it) } ?: "label:${UUID.randomUUID()}"
                            c.products.save(key, ProductInput(code = key, name = productName.ifBlank { "Label photo" }, ingredientsText = text, source = "label"))
                            nav.navigate("result/${enc("product:$key")}") { popUpTo("label?code={code}&name={name}") { inclusive = true } }
                        }
                    }, modifier = Modifier.weight(1f), enabled = text.isNotBlank()) { Text("Check it") }
                }
            }
        }
    }
}
