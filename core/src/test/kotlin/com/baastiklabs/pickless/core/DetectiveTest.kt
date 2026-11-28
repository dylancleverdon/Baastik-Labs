package com.baastiklabs.pickless.core

import com.baastiklabs.pickless.core.detective.BmLog
import com.baastiklabs.pickless.core.detective.DayLog
import com.baastiklabs.pickless.core.detective.Detective
import com.baastiklabs.pickless.core.detective.Diary
import com.baastiklabs.pickless.core.detective.MealLog
import com.baastiklabs.pickless.core.detective.Nicotine
import com.baastiklabs.pickless.core.detective.Tier
import java.time.LocalDate
import java.time.ZoneId
import kotlin.random.Random
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

/** The detective against simulated diaries with known (planted) triggers, and with none. */
class DetectiveTest {
    private val zone = ZoneId.of("America/Los_Angeles")
    private val H = 3_600_000L
    private val detective = Detective({ it.substringAfter(':').replace('-', ' ').replaceFirstChar { c -> c.uppercase() } })

    private val breakfasts = listOf(setOf("food:eggs", "tag:egg"), setOf("food:toast", "tag:wheat"), setOf("food:banana"), setOf("food:rice-cakes"), setOf("food:smoothie", "food:banana"))
    private val lunches = listOf(setOf("food:chicken-rice"), setOf("food:salad"), setOf("food:sandwich", "tag:wheat"), setOf("food:sushi", "tag:fish"), setOf("food:pasta", "tag:wheat", "tag:garlic"), setOf("food:burrito-bowl", "tag:legume", "tag:onion"))
    private val dinners = listOf(setOf("food:steak", "food:potato"), setOf("food:pizza", "tag:wheat", "tag:garlic"), setOf("food:curry", "tag:garlic", "tag:onion", "tag:spicy"), setOf("food:salmon"), setOf("food:tacos", "tag:onion"), setOf("food:stir-fry", "tag:garlic"))
    private val snacks = listOf(setOf("food:chips", "tag:fried"), setOf("food:apple"), setOf("food:popcorn"), setOf("food:nuts", "tag:tree-nut"))

    internal fun simulate(seed: Int, days: Int, trigger: String?, effect: Double = 0.55, base: Double = 0.12, nicotineEffect: Double = 0.0): Pair<Diary, Long> {
        val rnd = Random(seed)
        val start = LocalDate.of(2026, 9, 1)
        val meals = mutableListOf<MealLog>()
        val bms = mutableListOf<BmLog>()
        val checkins = mutableListOf<DayLog>()
        var id = 1L
        for (d in 0 until days) {
            val date = start.plusDays(d.toLong())
            val day0 = date.atStartOfDay(zone).toInstant().toEpochMilli()
            val today = mutableListOf<MealLog>()
            fun meal(hour: Int, pool: List<Set<String>>) {
                val keys = pool.random(rnd)
                today += MealLog(id++, day0 + hour * H + rnd.nextLong(0, H), keys, names = keys.filter { it.startsWith("food:") }.map { it.removePrefix("food:") })
            }
            meal(8, breakfasts); meal(13, lunches); meal(19, dinners)
            if (rnd.nextDouble() < 0.5) meal(16, snacks)
            meals += today
            val nicotine = if (rnd.nextDouble() < 0.2) Nicotine.MORE else Nicotine.USUAL
            checkins += DayLog(epochDay = date.toEpochDay(), stress = rnd.nextInt(0, 5), sleepQuality = rnd.nextInt(1, 5), nicotine = nicotine)
            val exposed = trigger != null && today.any { trigger in it.keys }
            val p = base + (if (exposed) effect else 0.0) + (if (nicotine == Nicotine.MORE) nicotineEffect else 0.0)
            // normal BMs
            repeat(rnd.nextInt(1, 3)) { bms += BmLog(id++, day0 + (7 + rnd.nextInt(0, 12)) * H, bristol = rnd.nextInt(3, 6)) }
            if (rnd.nextDouble() < p) bms += BmLog(id++, day0 + 24 * H + (6 + rnd.nextInt(0, 6)) * H, bristol = if (rnd.nextBoolean()) 6 else 7, urgency = 2)
        }
        val now = start.plusDays(days.toLong() + 1).atStartOfDay(zone).toInstant().toEpochMilli()
        return Diary(meals, bms, days = checkins, zone = zone) to now
    }

    @Test fun findsPlantedTrigger() {
        var top = 0
        var confident = 0
        repeat(10) { seed ->
            val (diary, now) = simulate(seed, 45, "tag:garlic")
            val r = detective.analyze(diary, now)
            val first = r.suspects.firstOrNull()
            if (first != null && (first.key == "tag:garlic" || "Garlic" in first.sameAs)) {
                top++
                if (first.tier.rank >= Tier.MODERATE.rank) confident++
            }
        }
        assertTrue(top >= 8, "garlic was the top suspect in only $top/10 runs")
        assertTrue(confident >= 7, "garlic was confidently found in only $confident/10 runs")
    }

    @Test fun doesNotInventTriggers() {
        var strong = 0
        var moderate = 0
        repeat(30) { seed ->
            val (diary, now) = simulate(1000 + seed, 45, null)
            val r = detective.analyze(diary, now)
            strong += r.suspects.count { it.tier == Tier.STRONG }
            moderate += r.suspects.count { it.tier == Tier.MODERATE }
        }
        assertTrue(strong <= 1, "invented $strong strong suspects across 30 null diaries")
        assertTrue(moderate <= 6, "invented $moderate moderate suspects across 30 null diaries")
    }

    @Test fun lifestyleFactorsAreSeparate() {
        val (diary, now) = simulate(7, 60, null, nicotineEffect = 0.6)
        val r = detective.analyze(diary, now)
        assertTrue(r.lifestyle.any { it.key == "life:nicotine-more" }, r.lifestyle.map { it.key }.toString())
        assertTrue(r.suspects.none { it.key.startsWith("life:") })
    }

    @Test fun earlyReadUntilDataIsSolid() {
        val (short, nowShort) = simulate(3, 6, "tag:garlic")
        val q1 = detective.analyze(short, nowShort).quality
        assertTrue(!q1.solid && q1.missing.isNotEmpty())
        val (long, nowLong) = simulate(3, 50, "tag:garlic")
        val q2 = detective.analyze(long, nowLong).quality
        assertTrue(q2.solid, q2.missing.toString())
    }

    @Test fun explainsABadBm() {
        val (diary, now) = simulate(11, 40, "tag:garlic")
        val r = detective.analyze(diary, now)
        val bad = diary.bms.filter { it.bristol >= 6 }.maxBy { it.time }
        val e = detective.explain(diary, bad, r)
        assertTrue(e.bad)
        assertTrue(e.culprits.isNotEmpty())
        val good = diary.bms.first { it.bristol in 3..5 }
        assertEquals(false, detective.explain(diary, good, r).bad)
    }

    @Test fun bmPattern() {
        val (diary, _) = simulate(5, 30, "tag:garlic", effect = 0.8)
        val p = detective.bmPattern(diary)
        assertTrue(p.count > 30 && p.byType.values.sum() == p.count)
    }
}
