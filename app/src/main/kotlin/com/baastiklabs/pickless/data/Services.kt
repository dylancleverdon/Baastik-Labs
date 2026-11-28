package com.baastiklabs.pickless.data

import android.content.Context
import com.baastiklabs.pickless.core.coach.Challenge
import com.baastiklabs.pickless.core.coach.ChallengeResult
import com.baastiklabs.pickless.core.coach.Coach
import com.baastiklabs.pickless.core.coach.Phase
import com.baastiklabs.pickless.core.coach.Protocol
import com.baastiklabs.pickless.core.detective.Detective
import com.baastiklabs.pickless.core.detective.Explanation
import com.baastiklabs.pickless.core.detective.Report
import com.baastiklabs.pickless.core.kb.AvoidItem
import com.baastiklabs.pickless.core.kb.AvoidStatus
import com.baastiklabs.pickless.log.AppLog
import com.baastiklabs.pickless.notify.Notifications
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/** Runs the trigger detective in the background and keeps the latest report. */
class Insights(private val repo: Repository, private val settings: Settings, private val scope: CoroutineScope) {
    private val _report = MutableStateFlow<Report?>(null)
    val report: StateFlow<Report?> = _report
    private val _busy = MutableStateFlow(false)
    val busy: StateFlow<Boolean> = _busy
    private var pending: Job? = null

    /** Recompute soon (coalesces bursts of logging). */
    fun refreshSoon() {
        pending?.cancel()
        pending = scope.launch { delay(1200); refresh() }
    }

    suspend fun refresh(): Report = withContext(Dispatchers.Default) {
        _busy.value = true
        try {
            val p = settings.current()
            val detective = Detective(repo.labeler(), p.badBmRule)
            val r = detective.analyze(repo.diary(), System.currentTimeMillis())
            _report.value = r
            r
        } catch (e: Exception) {
            AppLog.e("detective", "analysis failed", e)
            throw e
        } finally {
            _busy.value = false
        }
    }

    suspend fun explain(bmId: Long): Explanation? = withContext(Dispatchers.Default) {
        val bm = repo.db.bm().byId(bmId) ?: return@withContext null
        val p = settings.current()
        val diary = repo.diary()
        val log = diary.bms.firstOrNull { it.id == bm.id } ?: return@withContext null
        Detective(repo.labeler(), p.badBmRule).explain(diary, log, _report.value ?: runCatching { refresh() }.getOrNull())
    }
}

/** Reintroduction tests: start one, log doses, read reactions from the diary, apply the result. */
class CoachService(private val context: Context, private val repo: Repository, private val settings: Settings, private val insights: Insights) {
    private val db = repo.db

    fun toModel(e: ChallengeEntity) = Challenge(
        e.protocolId, e.startEpochDay,
        e.dosesTaken.split(',').mapNotNull { it.toLongOrNull() },
        e.reactions.split(',').mapNotNull { p -> p.split(':').takeIf { it.size == 2 }?.let { (d, r) -> d.toIntOrNull()?.let { it to (r.toIntOrNull() ?: 0) } } }.toMap(),
        e.stoppedAtDose, ChallengeResult.byId(e.result),
    )

    private fun ChallengeEntity.with(c: Challenge) = copy(
        dosesTaken = c.dosesTaken.joinToString(","), reactions = c.reactions.entries.joinToString(",") { "${it.key}:${it.value}" },
        stoppedAtDose = c.stoppedAtDose, result = c.result?.id,
    )

    suspend fun completed(): Map<String, ChallengeResult> = db.challenges().allOnce().filter { !it.active && it.result != null }
        .associate { it.protocolId to (ChallengeResult.byId(it.result) ?: ChallengeResult.INCONCLUSIVE) }

    suspend fun start(item: AvoidItem, protocol: Protocol) {
        if (db.challenges().activeOnce() != null) return
        db.challenges().insert(ChallengeEntity(protocolId = protocol.id, itemKey = item.key, startEpochDay = repo.today()))
        settings.updateAvoid { list -> list.map { if (it.key == item.key) it.copy(status = AvoidStatus.TESTING) else it } }
        AppLog.i("coach", "started ${protocol.id}")
    }

    suspend fun logDose(e: ChallengeEntity) {
        val p = Coach.protocol(e.protocolId) ?: return
        val c = toModel(e)
        val day = (repo.today() - e.startEpochDay).toInt() + 1
        val dose = p.doses.getOrNull(day - 1) ?: return
        val ref = dose.foodId?.let { "food:$it" } ?: return
        val a = repo.analyze(ref) ?: return
        repo.logFood(ref, a, dose.grams, dose.amount, System.currentTimeMillis(), "snack", note = "Test: ${p.title}, day ${dose.day}", challengeDose = true)
        db.challenges().update(e.with(c.copy(dosesTaken = (c.dosesTaken + repo.today()).distinct())))
        insights.refreshSoon()
    }

    suspend fun setReaction(e: ChallengeEntity, doseDay: Int, reaction: Int) {
        val p = Coach.protocol(e.protocolId) ?: return
        db.challenges().update(e.with(Coach.record(toModel(e), p, doseDay, reaction)))
    }

    /** Read reactions from the logs and finish the test when its time is up. */
    suspend fun evaluate(): ChallengeEntity? {
        val e = db.challenges().activeOnce() ?: return null
        val p = Coach.protocol(e.protocolId) ?: return e
        var c = toModel(e)
        val diary = repo.diary()
        val doseTimes = db.food().allOnce().filter { it.challengeDose && repo.epochDay(it.time) >= e.startEpochDay }.sortedBy { it.time }
        doseTimes.forEachIndexed { i, log ->
            val doseDay = (repo.epochDay(log.time) - e.startEpochDay).toInt() + 1
            if (doseDay in 1..p.doses.size && c.reactions[doseDay] == null && System.currentTimeMillis() - log.time > 36 * 3_600_000L) {
                c = Coach.record(c, p, doseDay, Coach.reactionAfter(log.time, diary, settings.current().badBmRule))
            }
            if (i > 10) return@forEachIndexed
        }
        val status = Coach.status(c, p, repo.today())
        var updated = e.with(c)
        if (status.phase == Phase.DONE && c.result == null) {
            c = Coach.conclude(c, p)
            updated = e.with(c).copy(active = false, finishedAt = System.currentTimeMillis())
            apply(e.itemKey, p, c.result!!)
        }
        db.challenges().update(updated)
        return updated
    }

    suspend fun stop(e: ChallengeEntity) {
        db.challenges().update(e.copy(active = false, finishedAt = System.currentTimeMillis(), result = ChallengeResult.INCONCLUSIVE.id))
        settings.updateAvoid { list -> list.map { if (it.key == e.itemKey && it.status == AvoidStatus.TESTING) it.copy(status = AvoidStatus.AVOIDING) else it } }
    }

    private suspend fun apply(itemKey: String, p: Protocol, result: ChallengeResult) {
        Coach.fodmapGroupOf(p)?.let { g -> Coach.toleranceAfter(result)?.let { settings.setTolerance(g, it) } }
        val item = settings.current().avoid.firstOrNull { it.key == itemKey }
        if (item != null) {
            val steps = Coach.stepsFor(item)
            val last = steps.lastOrNull()?.id == p.id
            val newStatus = Coach.statusAfter(result, last) ?: AvoidStatus.AVOIDING
            settings.updateAvoid { list -> list.map { if (it.key == itemKey) it.copy(status = newStatus) else it } }
        }
        AppLog.i("coach", "finished ${p.id}: ${result.id}")
        Notifications.coach(context, "${p.title}: ${result.label.lowercase()}. Your avoid list and scores are updated.")
    }

    /** Things eaten during a test that could muddy it. */
    suspend fun contamination(e: ChallengeEntity): List<String> {
        val start = repo.startOf(e.startEpochDay)
        val p = Coach.protocol(e.protocolId) ?: return emptyList()
        val target = settings.current().avoid.firstOrNull { it.key == e.itemKey }?.key
        return db.food().allOnce().filter { it.time >= start && !it.challengeDose }.mapNotNull { log ->
            val hits = log.avoidHits.split(',').filter { it.isNotBlank() && it != target }
            if (hits.isNotEmpty()) "${log.name} (on your avoid list)" else null
        }.distinct().take(5).also { if (it.isNotEmpty()) AppLog.i("coach", "possible contamination during ${p.id}") }
    }

    suspend fun dailyNotice() {
        val e = db.challenges().activeOnce() ?: return
        val p = Coach.protocol(e.protocolId) ?: return
        val s = Coach.status(toModel(e), p, repo.today())
        val dose = s.doseToday
        if (s.phase == Phase.DOSING && dose != null && repo.today() !in toModel(e).dosesTaken) {
            Notifications.coach(context, "Day ${s.dayNumber} of ${p.title.lowercase()}: ${dose.amount} of ${dose.what.lowercase()}.")
        }
    }
}
