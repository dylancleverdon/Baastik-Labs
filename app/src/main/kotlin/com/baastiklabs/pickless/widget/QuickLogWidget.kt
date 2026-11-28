package com.baastiklabs.pickless.widget

import android.content.Context
import android.content.Intent
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.glance.GlanceId
import androidx.glance.GlanceModifier
import androidx.glance.action.clickable
import androidx.glance.appwidget.GlanceAppWidget
import androidx.glance.appwidget.GlanceAppWidgetReceiver
import androidx.glance.appwidget.action.actionStartActivity
import androidx.glance.appwidget.cornerRadius
import androidx.glance.appwidget.provideContent
import androidx.glance.background
import androidx.glance.layout.Alignment
import androidx.glance.layout.Box
import androidx.glance.layout.Row
import androidx.glance.layout.Spacer
import androidx.glance.layout.fillMaxSize
import androidx.glance.layout.fillMaxWidth
import androidx.glance.layout.padding
import androidx.glance.layout.width
import androidx.glance.text.FontWeight
import androidx.glance.text.Text
import androidx.glance.text.TextStyle
import androidx.glance.unit.ColorProvider
import com.baastiklabs.pickless.MainActivity

/** Home-screen widget: log a BM or symptom, or scan, in one tap. */
class QuickLogWidget : GlanceAppWidget() {
    override suspend fun provideGlance(context: Context, id: GlanceId) {
        provideContent { Content(context) }
    }

    @Composable
    private fun Content(context: Context) {
        Row(
            GlanceModifier.fillMaxSize().background(ColorProvider(Color(0xFFF4EFE6))).cornerRadius(20.dp).padding(8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Button(context, "BM", "bm", GlanceModifier.defaultWeight())
            Spacer(GlanceModifier.width(6.dp))
            Button(context, "Symptom", "symptom", GlanceModifier.defaultWeight())
            Spacer(GlanceModifier.width(6.dp))
            Button(context, "Scan", "scan?mode=product", GlanceModifier.defaultWeight())
        }
    }

    @Composable
    private fun Button(context: Context, label: String, route: String, modifier: GlanceModifier) {
        val intent = Intent(context, MainActivity::class.java).putExtra(MainActivity.EXTRA_ROUTE, route)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP).setAction("pickless.$route")
        Box(
            modifier.fillMaxWidth().background(ColorProvider(Color(0xFF2F6B4F))).cornerRadius(14.dp).padding(vertical = 12.dp).clickable(actionStartActivity(intent)),
            contentAlignment = Alignment.Center,
        ) {
            Text(label, style = TextStyle(color = ColorProvider(Color.White), fontSize = 15.sp, fontWeight = FontWeight.Bold))
        }
    }
}

class QuickLogWidgetReceiver : GlanceAppWidgetReceiver() {
    override val glanceAppWidget: GlanceAppWidget = QuickLogWidget()
}
