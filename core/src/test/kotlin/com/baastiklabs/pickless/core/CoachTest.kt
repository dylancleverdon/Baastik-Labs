package com.baastiklabs.pickless.core

import com.baastiklabs.pickless.core.coach.Challenge
import com.baastiklabs.pickless.core.coach.ChallengeResult
import com.baastiklabs.pickless.core.coach.Coach
import com.baastiklabs.pickless.core.coach.Phase
import com.baastiklabs.pickless.core.detective.Diary
import com.baastiklabs.pickless.core.kb.AvoidItem
import com.baastiklabs.pickless.core.kb.Categories
import com.baastiklabs.pickless.core.kb.KnowledgeBase
import com.baastiklabs.pickless.core.update.UpdateManifest
import com.baastiklabs.pickless.core.update.Updates
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

class CoachTest {
    private val kb = KnowledgeBase.load()

    @Test fun everyDefaultAvoidItemHasATest() {
        for (c in Categories.defaultAvoidList) {
            val steps = Coach.stepsFor(AvoidItem.ofCategory(c))
            assertTrue(steps.isNotEmpty(), "no test for $c")
            steps.flatMap { it.doses }.forEach { d -> assertNotNull(kb[d.foodId!!], "dose food ${d.foodId} missing") }
        }
        Coach.protocols.flatMap { it.doses }.forEach { d -> assertNotNull(kb[d.foodId!!], "dose food ${d.foodId} missing") }
    }

    @Test fun dairyIsTestedInSteps() {
        val dairy = AvoidItem.ofCategory("dairy")
        assertEquals("dairy-1-lactose-free", Coach.nextStep(dairy, emptyMap())!!.id)
        assertEquals("dairy-3-milk", Coach.nextStep(dairy, mapOf("dairy-1-lactose-free" to ChallengeResult.TOLERATED, "dairy-2-hard-cheese" to ChallengeResult.TOLERATED))!!.id)
        assertNull(Coach.nextStep(dairy, mapOf("dairy-1-lactose-free" to ChallengeResult.TRIGGER)))
    }

    @Test fun readinessAndSuggestion() {
        val today = 20_000L
        val young = AvoidItem.ofCategory("oat", sinceEpochDay = today - 5)
        assertTrue(!Coach.readiness(young, Diary(emptyList(), emptyList()), today, false).ready)
        val old = AvoidItem.ofCategory("oat", sinceEpochDay = today - 30)
        assertTrue(Coach.readiness(old, Diary(emptyList(), emptyList()), today, false).ready)
        val s = Coach.suggest(listOf(young, AvoidItem.ofCategory("spicy", sinceEpochDay = today - 30)), emptyMap(), Diary(emptyList(), emptyList()), today, false, null)
        assertEquals("spicy-1", s!!.protocol.id)
    }

    @Test fun challengeFlow() {
        val p = Coach.protocol("oat-1")!!
        var c = Challenge(p.id, 100)
        assertEquals(Phase.DOSING, Coach.status(c, p, 100).phase)
        c = Coach.record(c, p, 1, 0)
        c = Coach.record(c, p, 2, 0)
        c = Coach.record(c, p, 3, 2)
        assertEquals(3, c.stoppedAtDose)
        assertEquals(Phase.WASHOUT, Coach.status(c, p, 104).phase)
        assertEquals(ChallengeResult.SMALL_OK, Coach.conclude(c, p).result)
        val trigger = Coach.conclude(Coach.record(Challenge(p.id, 100), p, 1, 3), p)
        assertEquals(ChallengeResult.TRIGGER, trigger.result)
        val clean = (1..3).fold(Challenge(p.id, 100)) { acc, d -> Coach.record(acc, p, d, 0) }
        assertEquals(ChallengeResult.TOLERATED, Coach.conclude(clean, p).result)
    }

    @Test fun updatesOnlyFromThisRepo() {
        val repo = "dylancleverdon/Baastik-Labs"
        val good = UpdateManifest(version = "0.1.5", versionCode = 5, apk = UpdateManifest.Asset("https://github.com/$repo/releases/download/pickless-v0.1.5/Pickless-0.1.5.apk", "a".repeat(64), 10))
        assertTrue(Updates.shouldUpdate(4, good, repo, 34))
        assertTrue(!Updates.shouldUpdate(5, good, repo, 34))
        assertTrue(!Updates.shouldUpdate(4, good.copy(apk = good.apk.copy(url = "https://evil.example/x.apk")), repo, 34))
        assertTrue(!Updates.shouldUpdate(4, good.copy(apk = good.apk.copy(url = "https://github.com/$repo/releases/download/norg-v1/x.apk")), repo, 34))
        val parsed = Updates.parse(Updates.json.encodeToString(UpdateManifest.serializer(), good))
        assertEquals(good, parsed)
    }
}
