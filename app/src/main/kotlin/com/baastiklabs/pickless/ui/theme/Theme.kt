package com.baastiklabs.pickless.ui.theme

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp
import com.baastiklabs.pickless.core.analysis.Verdict
import com.baastiklabs.pickless.core.fodmap.FodmapLevel

private val Light = lightColorScheme(
    primary = Color(0xFF2F6B4F), onPrimary = Color.White,
    primaryContainer = Color(0xFFCFE7D9), onPrimaryContainer = Color(0xFF0B2A1C),
    secondary = Color(0xFFB4762A), onSecondary = Color.White,
    secondaryContainer = Color(0xFFF7E2C2), onSecondaryContainer = Color(0xFF3B2405),
    tertiary = Color(0xFF4F6C8F), tertiaryContainer = Color(0xFFD6E3F5),
    background = Color(0xFFFBF8F2), onBackground = Color(0xFF1C211E),
    surface = Color(0xFFFBF8F2), onSurface = Color(0xFF1C211E),
    surfaceVariant = Color(0xFFE9E4DA), onSurfaceVariant = Color(0xFF4A4F4A),
    surfaceContainer = Color(0xFFF3EEE5), surfaceContainerHigh = Color(0xFFEDE7DC), surfaceContainerLow = Color(0xFFF7F3EC),
    outline = Color(0xFF7B807A), outlineVariant = Color(0xFFCAC6BC),
    error = Color(0xFFB3261E),
)

private val Dark = darkColorScheme(
    primary = Color(0xFF8FCFAE), onPrimary = Color(0xFF003822),
    primaryContainer = Color(0xFF175237), onPrimaryContainer = Color(0xFFCFE7D9),
    secondary = Color(0xFFF0BE7A), onSecondary = Color(0xFF452B00),
    secondaryContainer = Color(0xFF5E3F0E), onSecondaryContainer = Color(0xFFF7E2C2),
    tertiary = Color(0xFFAFC8EA), tertiaryContainer = Color(0xFF36506F),
    background = Color(0xFF121714), onBackground = Color(0xFFE1E5E1),
    surface = Color(0xFF121714), onSurface = Color(0xFFE1E5E1),
    surfaceVariant = Color(0xFF3F4541), onSurfaceVariant = Color(0xFFC0C6C0),
    surfaceContainer = Color(0xFF1B211D), surfaceContainerHigh = Color(0xFF242B26), surfaceContainerLow = Color(0xFF171C19),
    outline = Color(0xFF8A928B), outlineVariant = Color(0xFF3F4541),
)

/** Verdict and FODMAP colors, tuned to stay readable in both themes. */
@Immutable
data class Signals(
    val good: Color, val goodBg: Color, val caution: Color, val cautionBg: Color, val avoid: Color, val avoidBg: Color,
    val unknown: Color, val unknownBg: Color,
)

private val LightSignals = Signals(
    good = Color(0xFF1E7A4C), goodBg = Color(0xFFD9F0E2), caution = Color(0xFF9A5B00), cautionBg = Color(0xFFFCE8C8),
    avoid = Color(0xFFB3261E), avoidBg = Color(0xFFF9DEDC), unknown = Color(0xFF5F6368), unknownBg = Color(0xFFE8E6E1),
)
private val DarkSignals = Signals(
    good = Color(0xFF7FD9A6), goodBg = Color(0xFF173D29), caution = Color(0xFFF3C06B), cautionBg = Color(0xFF4A3410),
    avoid = Color(0xFFF2B8B5), avoidBg = Color(0xFF5C1D19), unknown = Color(0xFFC4C7C5), unknownBg = Color(0xFF2F3330),
)

val LocalSignals = staticCompositionLocalOf { LightSignals }

private val Type = Typography(
    headlineLarge = TextStyle(fontFamily = FontFamily.Serif, fontWeight = FontWeight.SemiBold, fontSize = 30.sp, lineHeight = 36.sp),
    headlineMedium = TextStyle(fontFamily = FontFamily.Serif, fontWeight = FontWeight.SemiBold, fontSize = 25.sp, lineHeight = 31.sp),
    headlineSmall = TextStyle(fontFamily = FontFamily.Serif, fontWeight = FontWeight.SemiBold, fontSize = 21.sp, lineHeight = 27.sp),
    titleLarge = TextStyle(fontWeight = FontWeight.SemiBold, fontSize = 19.sp, lineHeight = 25.sp),
    titleMedium = TextStyle(fontWeight = FontWeight.SemiBold, fontSize = 16.sp, lineHeight = 22.sp),
    titleSmall = TextStyle(fontWeight = FontWeight.SemiBold, fontSize = 14.sp, lineHeight = 20.sp),
    bodyLarge = TextStyle(fontSize = 16.sp, lineHeight = 23.sp),
    bodyMedium = TextStyle(fontSize = 14.sp, lineHeight = 20.sp),
    bodySmall = TextStyle(fontSize = 12.5.sp, lineHeight = 17.sp),
    labelLarge = TextStyle(fontWeight = FontWeight.SemiBold, fontSize = 14.sp),
    labelMedium = TextStyle(fontWeight = FontWeight.Medium, fontSize = 12.sp),
)

@Composable
fun PicklessTheme(content: @Composable () -> Unit) {
    val dark = isSystemInDarkTheme()
    androidx.compose.runtime.CompositionLocalProvider(LocalSignals provides if (dark) DarkSignals else LightSignals) {
        MaterialTheme(colorScheme = if (dark) Dark else Light, typography = Type, content = content)
    }
}

object Pal {
    val signals: Signals @Composable get() = LocalSignals.current

    @Composable
    fun verdict(v: Verdict): Pair<Color, Color> = with(signals) {
        when (v) {
            Verdict.GOOD -> good to goodBg
            Verdict.CAUTION -> caution to cautionBg
            Verdict.AVOID -> avoid to avoidBg
            Verdict.UNKNOWN -> unknown to unknownBg
        }
    }

    @Composable
    fun fodmap(l: FodmapLevel): Pair<Color, Color> = with(signals) {
        when (l) {
            FodmapLevel.LOW -> good to goodBg
            FodmapLevel.MODERATE -> caution to cautionBg
            FodmapLevel.HIGH -> avoid to avoidBg
        }
    }
}
