package com.baastiklabs.pickless

import android.graphics.Bitmap
import androidx.compose.ui.graphics.asAndroidBitmap
import androidx.compose.ui.test.ExperimentalTestApi
import androidx.compose.ui.test.captureToImage
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.createEmptyComposeRule
import androidx.compose.ui.test.onAllNodesWithText
import androidx.compose.ui.test.onFirst
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.onRoot
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollToNode
import androidx.compose.ui.test.performTextInput
import androidx.compose.ui.test.hasScrollAction
import androidx.test.core.app.ActivityScenario
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.baastiklabs.pickless.data.BmEntity
import kotlinx.coroutines.runBlocking
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode
import java.io.File

/** Launches the real app under Robolectric, walks the main flows and saves screenshots. */
@OptIn(ExperimentalTestApi::class)
@RunWith(AndroidJUnit4::class)
@Config(sdk = [32], qualifiers = "w411dp-h891dp-xxhdpi")
@GraphicsMode(GraphicsMode.Mode.NATIVE)
class AppSmokeTest {
    @get:Rule val compose = createEmptyComposeRule()

    private val app get() = ApplicationProvider.getApplicationContext<PicklessApp>()

    private var scenario: ActivityScenario<MainActivity>? = null

    /** Draw the activity's window into a PNG (Robolectric native graphics). */
    private fun shot(name: String) {
        val dir = System.getProperty("pickless.screenshots") ?: return
        compose.waitForIdle()
        scenario?.onActivity { activity ->
            runCatching {
                val v = activity.window.decorView
                val bmp = Bitmap.createBitmap(v.width.coerceAtLeast(1), v.height.coerceAtLeast(1), Bitmap.Config.ARGB_8888)
                v.draw(android.graphics.Canvas(bmp))
                File(dir).mkdirs()
                File(dir, "$name.png").outputStream().use { bmp.compress(Bitmap.CompressFormat.PNG, 100, it) }
            }.onFailure { System.err.println("SCREENSHOT $name failed: $it") }
        }
    }

    /** Generous timeout: shared CI machines can be many times slower than a laptop. */
    private fun waitText(text: String, timeout: Long = 20_000) {
        compose.waitUntil(timeout) { compose.onAllNodesWithText(text, substring = true).fetchSemanticsNodes().isNotEmpty() }
    }

    /** Wait for the detective's first analysis to land, so nothing on Today changes under a tap. */
    private fun settle() {
        compose.waitUntil(20_000) { app.container.insights.report.value != null }
        compose.waitForIdle()
    }

    private fun onboarded() = runBlocking { app.container.settings.setOnboarded(true) }

    @Test fun onboardingWalkthrough() {
        runBlocking { app.container.settings.setOnboarded(false) } // DataStore is a JVM singleton across Robolectric tests
        ActivityScenario.launch(MainActivity::class.java).also { scenario = it }.use {
            waitText("Welcome to Pickless")
            shot("01-onboarding")
            repeat(4) { compose.onNodeWithText("Next").performClick(); compose.waitForIdle() }
            shot("02-onboarding-reminders")
            compose.onNodeWithText("Next").performClick()
            waitText("Last bits")
            compose.onNodeWithText("Start").performClick()
            waitText("Log BM")
        }
    }

    @Test fun checkAFoodAndLogIt() {
        onboarded()
        ActivityScenario.launch(MainActivity::class.java).also { scenario = it }.use {
            waitText("Log BM")
            settle()
            shot("03-today-empty")
            compose.onNodeWithText("Check").performClick()
            waitText("Check a food")
            compose.onNodeWithText("Type a food, dish or product").performTextInput("oat milk")
            waitText("Oat milk")
            shot("04-search")
            compose.onAllNodesWithText("Oat milk").onFirst().performClick()
            waitText("Your avoid list")
            shot("05-result-oat-milk")
            compose.onNodeWithText("Log it").performClick()
            waitText("How much")
            shot("06-log-food")
            compose.onAllNodesWithText("Log it").onFirst().performClick()
            waitText("Today")
        }
    }

    @Test fun almondMilkLooksGood() {
        onboarded()
        ActivityScenario.launch(MainActivity::class.java).also { scenario = it }.use {
            waitText("Log BM")
            compose.onNodeWithText("Check").performClick()
            compose.onNodeWithText("Type a food, dish or product").performTextInput("almond milk")
            waitText("Almond milk")
            compose.onAllNodesWithText("Almond milk").onFirst().performClick()
            waitText("Looks good for you")
            shot("07-result-almond-milk")
        }
    }

    @Test fun logABadBmAndSeeWhy() {
        onboarded()
        ActivityScenario.launch(MainActivity::class.java).also { scenario = it }.use {
            waitText("Log BM")
            compose.onNodeWithText("Log BM").performClick()
            waitText("Mushy, ragged")
            shot("08-log-bm")
            compose.onNodeWithText("Mushy, ragged").performClick()
            compose.onNodeWithText("Save").performClick()
            waitText("Why was that one bad?")
            shot("09-explain")
        }
    }

    @Test fun mainTabsRender() {
        onboarded()
        runBlocking { app.container.db.bm().insert(BmEntity(time = System.currentTimeMillis() - 3_600_000, bristol = 4)) }
        ActivityScenario.launch(MainActivity::class.java).also { scenario = it }.use {
            waitText("Log BM")
            compose.onNodeWithText("Insights").performClick()
            waitText("Trigger detective")
            shot("10-insights")
            compose.onNodeWithText("History").performClick()
            waitText("Green: logged")
            shot("11-history")
            compose.onNodeWithText("More").performClick()
            waitText("Avoid list")
            shot("12-more")
            compose.onNodeWithText("Coach: food tests").performClick()
            waitText("Your avoid list")
            shot("13-coach")
        }
    }

    @Test fun settingsAndAvoidList() {
        onboarded()
        ActivityScenario.launch(MainActivity::class.java).also { scenario = it }.use {
            waitText("Log BM")
            compose.onNodeWithText("More").performClick()
            compose.onNodeWithText("Avoid list").performClick()
            waitText("Brussels sprouts")
            shot("14-avoid-list")
            compose.onNodeWithText("Brussels sprouts").assertExists()
        }
        ActivityScenario.launch(MainActivity::class.java).also { scenario = it }.use {
            waitText("Log BM")
            compose.onNodeWithText("More").performClick()
            compose.onNodeWithText("Settings").performClick()
            waitText("Bad BMs are")
            shot("15-settings")
        }
    }

    @Test fun checkInAndCard() {
        onboarded()
        ActivityScenario.launch(MainActivity::class.java).also { scenario = it }.use {
            waitText("About 20 seconds")
            settle()
            compose.onNodeWithText("About 20 seconds", substring = true).performClick()
            waitText("Overall gut day")
            shot("16-checkin")
        }
        ActivityScenario.launch(MainActivity::class.java).also { scenario = it }.use {
            waitText("Log BM")
            compose.onNodeWithText("More").performClick()
            compose.onNodeWithText("Restaurant card").performClick()
            waitText("For medical reasons")
            shot("17-restaurant-card")
        }
    }

    @Suppress("unused")
    private fun scrollTo(text: String) = compose.onAllNodes(hasScrollAction()).onFirst().performScrollToNode(hasText(text))
}
