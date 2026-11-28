package com.baastiklabs.pickless.core.coach

import com.baastiklabs.pickless.core.detective.BadBm
import com.baastiklabs.pickless.core.detective.Diary
import com.baastiklabs.pickless.core.detective.Report
import com.baastiklabs.pickless.core.fodmap.FodmapGroup
import com.baastiklabs.pickless.core.kb.AvoidItem
import com.baastiklabs.pickless.core.kb.AvoidStatus

/** One day of a test: what to eat and how much. */
data class Dose(val day: Int, val what: String, val amount: String, val grams: Double, val foodId: String?)

/**
 * A reintroduction test: three days of rising doses, then a washout back on the elimination diet.
 * [target] is an avoid category id ("dairy") or a FODMAP group ("fodmap:lactose"). Multi-step
 * targets (dairy, soy) separate the parts: lactose-free milk tests milk protein before regular milk
 * tests lactose. IDs are stored: never rename.
 */
data class Protocol(
    val id: String,
    val target: String,
    val step: Int,
    val title: String,
    val why: String,
    val doses: List<Dose>,
    val washoutDays: Int = 3,
    val tips: List<String> = emptyList(),
)

enum class ChallengeResult(val id: String, val label: String) {
    TOLERATED("tolerated", "Tolerated"),
    SMALL_OK("small-ok", "OK in small amounts"),
    TRIGGER("trigger", "Trigger"),
    INCONCLUSIVE("inconclusive", "Inconclusive");

    companion object { fun byId(id: String?) = entries.firstOrNull { it.id == id } }
}

enum class Phase { DOSING, WASHOUT, DONE }

/** A test in progress. [reactions] holds the day's reaction (0 none .. 3 severe) for each dose taken. */
data class Challenge(
    val protocolId: String,
    val startEpochDay: Long,
    val dosesTaken: List<Long> = emptyList(),
    val reactions: Map<Int, Int> = emptyMap(),
    val stoppedAtDose: Int? = null,
    val result: ChallengeResult? = null,
)

data class Readiness(val ready: Boolean, val reasons: List<String>)

data class Suggestion(val item: AvoidItem, val protocol: Protocol, val why: String)

data class ChallengeStatus(
    val phase: Phase,
    /** 1-based dose day to do today, if any. */
    val doseToday: Dose?,
    val dayNumber: Int,
    val message: String,
    val result: ChallengeResult?,
)

object Coach {
    /** Suggested time on the elimination diet before testing something. */
    const val MIN_AVOID_DAYS = 14

    val protocols: List<Protocol> = listOf(
        Protocol("dairy-1-lactose-free", "dairy", 1, "Dairy, step 1: lactose-free milk",
            "Lactose-free milk still has milk protein but no lactose, so this tests dairy itself.",
            listOf(Dose(1, "Lactose-free milk", "½ cup", 120.0, "lactose-free-milk"), Dose(2, "Lactose-free milk", "1 cup", 240.0, "lactose-free-milk"), Dose(3, "Lactose-free milk", "1½ cups", 360.0, "lactose-free-milk")),
            tips = listOf("Have it plain or in a lactose-free latte, not with other new foods.")),
        Protocol("dairy-2-hard-cheese", "dairy", 2, "Dairy, step 2: hard cheese",
            "Hard cheese is dairy with almost no lactose. If milk protein was fine, this usually is too.",
            listOf(Dose(1, "Cheddar or Swiss", "1 slice", 21.0, "cheese"), Dose(2, "Cheddar or Swiss", "2 slices", 42.0, "cheese"), Dose(3, "Cheddar or Swiss", "3 slices", 63.0, "cheese"))),
        Protocol("dairy-3-milk", "dairy", 3, "Dairy, step 3: regular milk",
            "Regular milk adds lactose. If steps 1-2 were fine and this isn't, it's the lactose, and lactose-free dairy is open to you.",
            listOf(Dose(1, "Regular milk", "¼ cup", 60.0, "milk"), Dose(2, "Regular milk", "½ cup", 120.0, "milk"), Dose(3, "Regular milk", "1 cup", 240.0, "milk"))),
        Protocol("soy-1-tofu", "soy", 1, "Soy, step 1: firm tofu",
            "Firm tofu is soy with most of its FODMAPs pressed out, so this tests soy itself.",
            listOf(Dose(1, "Firm tofu", "¼ cup", 60.0, "tofu-firm"), Dose(2, "Firm tofu", "½ cup", 125.0, "tofu-firm"), Dose(3, "Firm tofu", "¾ cup", 190.0, "tofu-firm"))),
        Protocol("soy-2-soy-milk", "soy", 2, "Soy, step 2: whole-bean soy milk",
            "Whole-bean soy milk adds soy's GOS. If tofu was fine and this isn't, the GOS is the issue rather than soy.",
            listOf(Dose(1, "Soy milk (whole soybeans)", "¼ cup", 60.0, "soy-milk-whole"), Dose(2, "Soy milk (whole soybeans)", "½ cup", 120.0, "soy-milk-whole"), Dose(3, "Soy milk (whole soybeans)", "1 cup", 240.0, "soy-milk-whole"))),
        Protocol("oat-1", "oat", 1, "Oats: plain rolled oats",
            "Plain oats cooked in water or lactose-free milk, nothing else new.",
            listOf(Dose(1, "Rolled oats (dry)", "¼ cup", 20.0, "oats"), Dose(2, "Rolled oats (dry)", "½ cup", 40.0, "oats"), Dose(3, "Rolled oats (dry)", "¾ cup", 60.0, "oats"))),
        Protocol("fried-1", "fried", 1, "Fried food: french fries",
            "Deep-fried food is a fat trigger. Pick one fried food and keep it the same all three days.",
            listOf(Dose(1, "French fries", "small handful", 40.0, "french-fries"), Dose(2, "French fries", "small order", 70.0, "french-fries"), Dose(3, "French fries", "medium order", 115.0, "french-fries")),
            tips = listOf("Have them with a plain meal you know is safe.")),
        Protocol("spicy-1", "spicy", 1, "Spicy food: hot sauce",
            "Capsaicin (chili heat) speeds up the gut for some people. Add it to a meal you know is safe.",
            listOf(Dose(1, "Hot sauce", "½ tsp", 2.5, "hot-sauce"), Dose(2, "Hot sauce", "1 tsp", 5.0, "hot-sauce"), Dose(3, "Hot sauce", "2 tsp", 10.0, "hot-sauce"))),
        Protocol("brussels-sprouts-1", "brussels-sprouts", 1, "Brussels sprouts",
            "Cooked sprouts as a side to a meal you know is safe.",
            listOf(Dose(1, "Brussels sprouts", "2 sprouts", 38.0, "brussels-sprouts"), Dose(2, "Brussels sprouts", "4 sprouts", 76.0, "brussels-sprouts"), Dose(3, "Brussels sprouts", "6 sprouts", 114.0, "brussels-sprouts"))),
        Protocol("fodmap-lactose", "fodmap:lactose", 1, "Lactose (FODMAP test)", "Regular milk tests lactose on its own.",
            listOf(Dose(1, "Milk", "¼ cup", 60.0, "milk"), Dose(2, "Milk", "½ cup", 120.0, "milk"), Dose(3, "Milk", "1 cup", 240.0, "milk"))),
        Protocol("fodmap-fructose", "fodmap:fructose", 1, "Fructose (FODMAP test)", "Honey is mostly a fructose test.",
            listOf(Dose(1, "Honey", "1 tsp", 7.0, "honey"), Dose(2, "Honey", "2 tsp", 14.0, "honey"), Dose(3, "Honey", "1 tbsp", 21.0, "honey"))),
        Protocol("fodmap-fructans-wheat", "fodmap:fructans", 1, "Fructans: wheat bread", "Wheat bread tests grain fructans. Garlic and onion are tested separately: people react differently to each.",
            listOf(Dose(1, "White bread", "1 slice", 28.0, "bread-white"), Dose(2, "White bread", "2 slices", 56.0, "bread-white"), Dose(3, "White bread", "3 slices", 84.0, "bread-white"))),
        Protocol("fodmap-fructans-garlic", "fodmap:fructans", 2, "Fructans: garlic", "Cooked garlic in a meal you know is safe.",
            listOf(Dose(1, "Garlic", "¼ clove", 0.75, "garlic"), Dose(2, "Garlic", "½ clove", 1.5, "garlic"), Dose(3, "Garlic", "1 clove", 3.0, "garlic"))),
        Protocol("fodmap-fructans-onion", "fodmap:fructans", 3, "Fructans: onion", "Cooked onion in a meal you know is safe.",
            listOf(Dose(1, "Onion", "1 tbsp", 10.0, "onion"), Dose(2, "Onion", "¼ onion", 28.0, "onion"), Dose(3, "Onion", "½ onion", 55.0, "onion"))),
        Protocol("fodmap-gos", "fodmap:gos", 1, "GOS: canned lentils", "Canned, rinsed lentils test GOS.",
            listOf(Dose(1, "Canned lentils", "¼ cup", 50.0, "lentils"), Dose(2, "Canned lentils", "½ cup", 100.0, "lentils"), Dose(3, "Canned lentils", "1 cup", 200.0, "lentils"))),
        Protocol("fodmap-sorbitol", "fodmap:sorbitol", 1, "Sorbitol: avocado", "Avocado tests sorbitol.",
            listOf(Dose(1, "Avocado", "¼ avocado", 40.0, "avocado"), Dose(2, "Avocado", "½ avocado", 75.0, "avocado"), Dose(3, "Avocado", "¾ avocado", 110.0, "avocado"))),
        Protocol("fodmap-mannitol", "fodmap:mannitol", 1, "Mannitol: mushrooms", "Button mushrooms test mannitol.",
            listOf(Dose(1, "Button mushrooms", "½ cup", 35.0, "mushrooms"), Dose(2, "Button mushrooms", "1 cup", 70.0, "mushrooms"), Dose(3, "Button mushrooms", "1½ cups", 105.0, "mushrooms"))),
    )

    private val byId = protocols.associateBy { it.id }

    fun protocol(id: String): Protocol? = byId[id]

    /** Steps for an avoid item, in order. Custom items get a generic plan built from the food. */
    fun stepsFor(item: AvoidItem): List<Protocol> {
        item.categoryId?.let { c -> protocols.filter { it.target == c }.sortedBy { it.step }.takeIf { it.isNotEmpty() }?.let { return it } }
        val food = item.foodId ?: return emptyList()
        return listOf(Protocol("custom-${item.key}", item.key, 1, item.label, "Add it to a meal you know is safe.",
            listOf(Dose(1, item.label, "small portion", 30.0, food), Dose(2, item.label, "medium portion", 60.0, food), Dose(3, item.label, "normal portion", 100.0, food))))
    }

    /** Which step of a multi-step target comes after [done] results. */
    fun nextStep(item: AvoidItem, completed: Map<String, ChallengeResult>): Protocol? {
        val steps = stepsFor(item)
        for (p in steps) {
            val r = completed[p.id] ?: return p
            if (r == ChallengeResult.TRIGGER) return null // stop climbing once a step reacts
        }
        return null
    }

    fun readiness(item: AvoidItem, diary: Diary, today: Long, busy: Boolean, badBm: BadBm = BadBm.MOSTLY_LOOSE): Readiness {
        val reasons = mutableListOf<String>()
        if (item.status == AvoidStatus.TOLERATED) return Readiness(false, listOf("Already back in your diet."))
        if (busy) reasons += "Finish the test that's running first: one food at a time keeps results clear."
        val since = item.sinceEpochDay
        if (since != null && today - since < MIN_AVOID_DAYS) reasons += "Avoided for ${today - since} days; testing works best after $MIN_AVOID_DAYS (2 to 6 weeks is typical)."
        val recentBad = diary.bms.count { badBm.isBad(it) && diary.dayOf(it.time) >= today - 5 }
        if (recentBad > 1) reasons += "$recentBad bad BMs in the last 5 days. Wait for a calmer stretch so a reaction stands out."
        val roughDays = diary.symptoms.filter { it.severity >= 3 && diary.dayOf(it.time) >= today - 5 }.map { diary.dayOf(it.time) }.toSet().size
        if (roughDays >= 2) reasons += "Strong symptoms on $roughDays of the last 5 days."
        return Readiness(reasons.isEmpty(), reasons)
    }

    /**
     * What to test next: among items that are ready, the one the detective suspects least first.
     * It's the one most likely to pass, so your diet widens fastest.
     */
    fun suggest(
        avoid: List<AvoidItem>, completed: Map<String, ChallengeResult>, diary: Diary, today: Long, busy: Boolean, report: Report?,
        badBm: BadBm = BadBm.MOSTLY_LOOSE,
    ): Suggestion? {
        if (busy) return null
        val options = avoid.filter { it.status == AvoidStatus.AVOIDING || it.status == AvoidStatus.SMALL_OK }.mapNotNull { item ->
            val p = nextStep(item, completed) ?: return@mapNotNull null
            if (!readiness(item, diary, today, false, badBm).ready) return@mapNotNull null
            val suspicion = report?.statOf("avoid:${item.key}")?.probHarm ?: report?.statOf("tag:${item.categoryId}")?.probHarm ?: 0.5
            Triple(item, p, suspicion)
        }
        val best = options.minByOrNull { it.third } ?: return null
        val why = if (best.third < 0.5) "The detective doesn't suspect it much, so it's likely to pass and give you back a food."
        else "It's been avoided long enough to test."
        return Suggestion(best.first, best.second, why)
    }

    /** Today's state of a running test. */
    fun status(c: Challenge, p: Protocol, today: Long): ChallengeStatus {
        if (c.result != null) return ChallengeStatus(Phase.DONE, null, (today - c.startEpochDay + 1).toInt(), "Finished: ${c.result.label.lowercase()}.", c.result)
        val day = (today - c.startEpochDay).toInt() + 1
        val stopped = c.stoppedAtDose
        val dosingDays = stopped ?: p.doses.size
        return when {
            stopped == null && day <= p.doses.size -> {
                val dose = p.doses[day - 1]
                val taken = c.dosesTaken.contains(today)
                ChallengeStatus(Phase.DOSING, dose, day, if (taken) "Dose ${dose.day} logged. Note how you feel over the next day." else "Day ${dose.day}: ${dose.amount} of ${dose.what.lowercase()}.", null)
            }
            day <= dosingDays + p.washoutDays -> ChallengeStatus(Phase.WASHOUT, null, day,
                "Washout: back to your elimination diet for ${dosingDays + p.washoutDays - day + 1} more day(s) before the next test.", null)
            else -> ChallengeStatus(Phase.DONE, null, day, "Ready to see the result.", null)
        }
    }

    /**
     * Reaction for a dose day, from the logs: a bad BM within 36 hours or strong symptoms within
     * 24 hours. 0 none, 1 mild, 2 clear, 3 severe.
     */
    fun reactionAfter(doseTime: Long, diary: Diary, badBm: BadBm = BadBm.MOSTLY_LOOSE): Int {
        val h = 3_600_000L
        val bad = diary.bms.filter { it.time in (doseTime + h)..(doseTime + 36 * h) && badBm.isBad(it) }
        val sym = diary.symptoms.filter { it.time in (doseTime + h / 2)..(doseTime + 24 * h) }.maxOfOrNull { it.severity } ?: 0
        return when {
            bad.any { it.accident || it.bristol == 7 } || sym >= 4 -> 3
            bad.isNotEmpty() || sym >= 3 -> 2
            sym >= 2 -> 1
            else -> 0
        }
    }

    /** Record a dose's reaction and decide whether to stop. */
    fun record(c: Challenge, p: Protocol, doseDay: Int, reaction: Int): Challenge {
        val reactions = c.reactions + (doseDay to reaction)
        var next = c.copy(reactions = reactions)
        if (reaction >= 2 && c.stoppedAtDose == null) next = next.copy(stoppedAtDose = doseDay)
        return next
    }

    /** Result once dosing and washout are over. */
    fun conclude(c: Challenge, p: Protocol): Challenge {
        val stop = c.stoppedAtDose
        val result = when {
            stop == 1 -> ChallengeResult.TRIGGER
            stop != null -> ChallengeResult.SMALL_OK
            c.reactions.size < p.doses.size -> ChallengeResult.INCONCLUSIVE
            (c.reactions[p.doses.size] ?: 0) == 1 -> ChallengeResult.SMALL_OK
            else -> ChallengeResult.TOLERATED
        }
        return c.copy(result = result)
    }

    /** What a finished test means for the avoid list. */
    fun statusAfter(result: ChallengeResult, isLastStep: Boolean): AvoidStatus? = when (result) {
        ChallengeResult.TRIGGER -> AvoidStatus.TRIGGER
        ChallengeResult.SMALL_OK -> AvoidStatus.SMALL_OK
        ChallengeResult.TOLERATED -> if (isLastStep) AvoidStatus.TOLERATED else null
        ChallengeResult.INCONCLUSIVE -> null
    }

    /** What a finished FODMAP test means for the personal FODMAP score (multiplier on that group). */
    fun toleranceAfter(result: ChallengeResult): Double? = when (result) {
        ChallengeResult.TOLERATED -> 0.3
        ChallengeResult.SMALL_OK -> 0.7
        ChallengeResult.TRIGGER -> 1.5
        ChallengeResult.INCONCLUSIVE -> null
    }

    fun fodmapGroupOf(p: Protocol): FodmapGroup? = if (p.target.startsWith("fodmap:")) FodmapGroup.byId(p.target.removePrefix("fodmap:")) else null
}
