package com.baastiklabs.pickless

import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.baastiklabs.pickless.core.analysis.Verdict
import com.baastiklabs.pickless.data.BmEntity
import com.baastiklabs.pickless.data.CheckInEntity
import com.baastiklabs.pickless.data.QuickEventEntity
import com.baastiklabs.pickless.core.detective.Nicotine
import com.baastiklabs.pickless.suggest.SuggestionBox
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.annotation.Config

/** The app's data plumbing: logging through Room into the detective. */
@RunWith(AndroidJUnit4::class)
@Config(sdk = [32])
class RepositoryTest {
    private val c get() = ApplicationProvider.getApplicationContext<PicklessApp>().container

    @Test fun logsFlowIntoTheDetective() = runBlocking {
        val repo = c.repo
        val day = 86_400_000L
        val start = repo.startOf(repo.today() - 20)
        for (d in 0 until 18) {
            val t = start + d * day
            val ref = if (d % 3 == 0) "food:pizza" else "food:chicken-rice"
            val a = repo.analyze(ref)!!
            repo.logFood(ref, a, a.portion.grams, "1 serving", t + 12 * 3_600_000L, "lunch")
            c.db.bm().insert(BmEntity(time = t + 9 * 3_600_000L, bristol = 4))
            if (d % 3 == 0) c.db.bm().insert(BmEntity(time = t + 31 * 3_600_000L, bristol = 7, urgency = 3))
            c.db.checkins().upsert(CheckInEntity(repo.epochDay(t), overall = 3, nicotine = "usual"))
        }
        val diary = repo.diary()
        assertEquals(18, diary.meals.size)
        assertTrue(diary.meals.first().keys.any { it.startsWith("food:") })
        val report = c.insights.refresh()
        assertTrue(report.analyzedDays >= 15)
        assertTrue(report.suspects.isNotEmpty())
        assertTrue(report.suspects.first().label.isNotBlank())
        val bad = c.db.bm().allOnce().last { it.bristol == 7 }
        val why = c.insights.explain(bad.id)
        assertNotNull(why)
        assertTrue(why!!.bad)
    }

    @Test fun extraPouchesMakeANicotineDay() = runBlocking {
        val repo = c.repo
        val t = repo.startOf(repo.today() - 2) + 10 * 3_600_000L
        c.db.events().insert(QuickEventEntity(time = t, type = "nicotine"))
        val d = repo.diary().days.first { it.epochDay == repo.epochDay(t) }
        assertEquals(Nicotine.MORE, d.nicotine)
    }

    @Test fun avoidListIsAppliedToAnalysis() = runBlocking {
        assertEquals(Verdict.AVOID, c.repo.analyze("food:latte")!!.verdict)
        assertEquals(Verdict.AVOID, c.repo.analyze("food:brussels-sprouts")!!.verdict)
        assertEquals(Verdict.GOOD, c.repo.analyze("food:almond-milk")!!.verdict)
        assertTrue(c.repo.analyze("guess:kohlrabi slaw") != null)
    }

    @Test fun suggestionEndpointRules() {
        assertTrue(SuggestionBox.validEndpoint("https://script.google.com/macros/s/AKfy123/exec"))
        assertTrue(!SuggestionBox.validEndpoint("https://evil.example/exec"))
        assertTrue(!SuggestionBox.validEndpoint("http://script.google.com/macros/s/x/exec"))
    }
}
