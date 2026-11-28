package com.baastiklabs.pickless.ui.screens

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.baastiklabs.pickless.report.Exports
import com.baastiklabs.pickless.ui.components.ChoiceRow
import com.baastiklabs.pickless.ui.components.Hint
import com.baastiklabs.pickless.ui.components.InfoCard
import com.baastiklabs.pickless.ui.components.SectionTitle
import kotlinx.coroutines.launch

@Composable
fun ReportScreen() {
    val c = LocalContainer.current
    val snack = LocalSnack.current
    val ctx = LocalContext.current
    val scope = rememberUiScope()
    val report by c.insights.report.collectAsState()
    var days by remember { mutableStateOf(30) }
    var busy by remember { mutableStateOf(false) }
    val restore = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) scope.launch { snack.showSnackbar(Exports.restore(ctx, c.repo, uri)); c.insights.refreshSoon() }
    }
    Page("Reports & backups") {
        item {
            SectionTitle("Report for your doctor or dietitian")
            InfoCard {
                Text("A one-page-or-so PDF: BM pattern with a Bristol chart, symptoms, IBS severity scores, your avoid list and test results, possible triggers and check-in averages.", style = MaterialTheme.typography.bodyMedium)
                ChoiceRow(listOf(14 to "2 weeks", 30 to "30 days", 90 to "90 days", 365 to "1 year"), days, { days = it })
                Button(enabled = !busy, onClick = {
                    busy = true
                    scope.launch {
                        val r = report ?: runCatching { c.insights.refresh() }.getOrNull()
                        val f = Exports.pdf(ctx, c.repo, r, days)
                        busy = false
                        Exports.share(ctx, f, "application/pdf", "Share report")
                    }
                }, modifier = Modifier.fillMaxWidth()) { Text(if (busy) "Making it…" else "Create PDF") }
            }
            SectionTitle("Spreadsheet")
            OutlinedButton(onClick = { scope.launch { Exports.share(ctx, Exports.spreadsheet(ctx, c.repo), "application/zip", "Share data") } }, modifier = Modifier.fillMaxWidth()) { Text("Export everything as CSV files") }
            SectionTitle("Backup")
            Hint("Android's own backup already keeps your diary when you change phones. This makes a file you can keep yourself.")
            Spacer(Modifier.height(6.dp))
            OutlinedButton(onClick = { scope.launch { Exports.share(ctx, Exports.backup(ctx, c.repo), "application/json", "Save backup") } }, modifier = Modifier.fillMaxWidth()) { Text("Save a backup file") }
            OutlinedButton(onClick = { restore.launch(arrayOf("application/json", "*/*")) }, modifier = Modifier.fillMaxWidth()) { Text("Restore from a backup file") }
        }
    }
}
