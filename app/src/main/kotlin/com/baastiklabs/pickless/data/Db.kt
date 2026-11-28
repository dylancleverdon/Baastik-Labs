package com.baastiklabs.pickless.data

import android.content.Context
import androidx.room.Dao
import androidx.room.Database
import androidx.room.Delete
import androidx.room.Entity
import androidx.room.Insert
import androidx.room.OnConflictStrategy
import androidx.room.PrimaryKey
import androidx.room.Query
import androidx.room.Room
import androidx.room.RoomDatabase
import androidx.room.Update
import androidx.room.Upsert
import kotlinx.coroutines.flow.Flow
import kotlinx.serialization.Serializable

/** Something eaten. [ref] says what: "food:<kb id>", "product:<gtin key>", "custom:<id>", "guess:<text>". */
@Serializable
@Entity(tableName = "food_log")
data class FoodLogEntity(
    @PrimaryKey(autoGenerate = true) val id: Long = 0,
    val time: Long,
    val meal: String,
    val ref: String,
    val name: String,
    val grams: Double,
    val portionLabel: String,
    /** core Features as JSON (FODMAP per gram, food/ingredient/tag keys) at the time it was logged. */
    val featuresJson: String,
    /** Avoid-list item keys it broke (comma separated). */
    val avoidHits: String,
    val verdict: String,
    val note: String? = null,
    val challengeDose: Boolean = false,
)

@Serializable
@Entity(tableName = "bm_log")
data class BmEntity(
    @PrimaryKey(autoGenerate = true) val id: Long = 0,
    val time: Long,
    val bristol: Int,
    val urgency: Int = 0,
    val pain: Int = 0,
    val straining: Boolean = false,
    val incomplete: Boolean = false,
    val accident: Boolean = false,
    val blood: Boolean = false,
    val mucus: Boolean = false,
    val color: String? = null,
    val note: String? = null,
)

@Serializable
@Entity(tableName = "symptom_log")
data class SymptomEntity(
    @PrimaryKey(autoGenerate = true) val id: Long = 0,
    val time: Long,
    val type: String,
    val severity: Int,
    val note: String? = null,
)

@Serializable
@Entity(tableName = "checkin")
data class CheckInEntity(
    @PrimaryKey val epochDay: Long,
    val overall: Int? = null,
    val stress: Int? = null,
    val sleepQuality: Int? = null,
    val sleepHours: Double? = null,
    val nicotine: String? = null,
    val nicotineCount: Int? = null,
    val alcohol: Int? = null,
    val caffeine: Int? = null,
    val exercise: Int? = null,
    val period: Boolean? = null,
    val sick: Boolean = false,
    val travel: Boolean = false,
    val noBm: Boolean? = null,
    val meds: String = "",
    val note: String? = null,
    val updatedAt: Long = System.currentTimeMillis(),
)

@Serializable
@Entity(tableName = "quick_event")
data class QuickEventEntity(
    @PrimaryKey(autoGenerate = true) val id: Long = 0,
    val time: Long,
    val type: String,
    val amount: Double = 1.0,
)

/** The weekly IBS severity check-in (IBS-SSS style, 0-500). */
@Serializable
@Entity(tableName = "weekly")
data class WeeklyEntity(
    @PrimaryKey val epochDay: Long,
    val painSeverity: Int,
    val painDays: Int,
    val bloating: Int,
    val dissatisfaction: Int,
    val interference: Int,
) {
    val total: Int get() = painSeverity + painDays * 10 + bloating + dissatisfaction + interference
}

/** A product looked up or read from a label, cached so scans work offline. [json] is a core ProductInput. */
@Serializable
@Entity(tableName = "product")
data class ProductEntity(
    @PrimaryKey val key: String,
    val name: String,
    val json: String,
    val source: String,
    val fetchedAt: Long,
    val favorite: Boolean = false,
    /** "fine" or "bad": the owner's own verdict from the result screen. */
    val userVerdict: String? = null,
)

/** A saved meal or recipe made of other foods. [itemsJson] is a list of CustomItem. */
@Serializable
@Entity(tableName = "custom_food")
data class CustomFoodEntity(
    @PrimaryKey(autoGenerate = true) val id: Long = 0,
    val name: String,
    val itemsJson: String,
    val servingGrams: Double,
    val favorite: Boolean = true,
    val createdAt: Long = System.currentTimeMillis(),
)

/** The owner's (or a web lookup's) FODMAP numbers for a food, keyed like core corrections. */
@Serializable
@Entity(tableName = "correction")
data class CorrectionEntity(
    @PrimaryKey val key: String,
    val limitsJson: String,
    val source: String,
    val note: String? = null,
    val updatedAt: Long = System.currentTimeMillis(),
)

@Entity(tableName = "web_lookup")
data class WebLookupEntity(@PrimaryKey val query: String, val resultJson: String, val at: Long)

@Entity(tableName = "suggestion")
data class SuggestionEntity(
    @PrimaryKey val id: String,
    val kind: String,
    val text: String,
    val logPath: String? = null,
    val createdAt: Long,
    val sentAt: Long? = null,
    val lastError: String? = null,
    val tries: Int = 0,
)

@Serializable
@Entity(tableName = "challenge")
data class ChallengeEntity(
    @PrimaryKey(autoGenerate = true) val id: Long = 0,
    val protocolId: String,
    val itemKey: String,
    val startEpochDay: Long,
    /** Epoch days a dose was eaten, comma separated. */
    val dosesTaken: String = "",
    /** "dose:reaction" pairs, comma separated. */
    val reactions: String = "",
    val stoppedAtDose: Int? = null,
    val result: String? = null,
    val active: Boolean = true,
    val finishedAt: Long? = null,
)

@Dao
interface FoodLogDao {
    @Insert suspend fun insert(e: FoodLogEntity): Long
    @Update suspend fun update(e: FoodLogEntity)
    @Delete suspend fun delete(e: FoodLogEntity)
    @Query("SELECT * FROM food_log ORDER BY time DESC") fun all(): Flow<List<FoodLogEntity>>
    @Query("SELECT * FROM food_log ORDER BY time") suspend fun allOnce(): List<FoodLogEntity>
    @Query("SELECT * FROM food_log WHERE time BETWEEN :from AND :to ORDER BY time") fun between(from: Long, to: Long): Flow<List<FoodLogEntity>>
    @Query("SELECT * FROM food_log WHERE time BETWEEN :from AND :to ORDER BY time") suspend fun betweenOnce(from: Long, to: Long): List<FoodLogEntity>
    @Query("SELECT * FROM food_log WHERE ref = :ref ORDER BY time DESC") suspend fun byRef(ref: String): List<FoodLogEntity>
    @Query("SELECT ref, name, MAX(time) AS time FROM food_log GROUP BY ref ORDER BY MAX(time) DESC LIMIT :limit") suspend fun recentRefs(limit: Int): List<RecentRef>
    @Query("SELECT * FROM food_log WHERE id = :id") suspend fun byId(id: Long): FoodLogEntity?
}

data class RecentRef(val ref: String, val name: String, val time: Long)

@Dao
interface BmDao {
    @Insert suspend fun insert(e: BmEntity): Long
    @Update suspend fun update(e: BmEntity)
    @Delete suspend fun delete(e: BmEntity)
    @Query("SELECT * FROM bm_log ORDER BY time DESC") fun all(): Flow<List<BmEntity>>
    @Query("SELECT * FROM bm_log ORDER BY time") suspend fun allOnce(): List<BmEntity>
    @Query("SELECT * FROM bm_log WHERE id = :id") suspend fun byId(id: Long): BmEntity?
}

@Dao
interface SymptomDao {
    @Insert suspend fun insert(e: SymptomEntity): Long
    @Update suspend fun update(e: SymptomEntity)
    @Delete suspend fun delete(e: SymptomEntity)
    @Query("SELECT * FROM symptom_log ORDER BY time DESC") fun all(): Flow<List<SymptomEntity>>
    @Query("SELECT * FROM symptom_log ORDER BY time") suspend fun allOnce(): List<SymptomEntity>
}

@Dao
interface CheckInDao {
    @Upsert suspend fun upsert(e: CheckInEntity)
    @Query("SELECT * FROM checkin WHERE epochDay = :day") suspend fun get(day: Long): CheckInEntity?
    @Query("SELECT * FROM checkin WHERE epochDay = :day") fun observe(day: Long): Flow<CheckInEntity?>
    @Query("SELECT * FROM checkin ORDER BY epochDay") suspend fun allOnce(): List<CheckInEntity>
    @Query("SELECT * FROM checkin ORDER BY epochDay DESC") fun all(): Flow<List<CheckInEntity>>
    @Delete suspend fun delete(e: CheckInEntity)
}

@Dao
interface QuickEventDao {
    @Insert suspend fun insert(e: QuickEventEntity): Long
    @Delete suspend fun delete(e: QuickEventEntity)
    @Query("SELECT * FROM quick_event ORDER BY time DESC") fun all(): Flow<List<QuickEventEntity>>
    @Query("SELECT * FROM quick_event ORDER BY time") suspend fun allOnce(): List<QuickEventEntity>
}

@Dao
interface WeeklyDao {
    @Upsert suspend fun upsert(e: WeeklyEntity)
    @Query("SELECT * FROM weekly ORDER BY epochDay") fun all(): Flow<List<WeeklyEntity>>
    @Query("SELECT * FROM weekly ORDER BY epochDay") suspend fun allOnce(): List<WeeklyEntity>
}

@Dao
interface ProductDao {
    @Upsert suspend fun upsert(e: ProductEntity)
    @Query("SELECT * FROM product WHERE `key` = :key") suspend fun get(key: String): ProductEntity?
    @Query("SELECT * FROM product WHERE favorite = 1 ORDER BY name") fun favorites(): Flow<List<ProductEntity>>
    @Query("SELECT * FROM product WHERE name LIKE '%' || :q || '%' ORDER BY fetchedAt DESC LIMIT 20") suspend fun search(q: String): List<ProductEntity>
    @Query("SELECT * FROM product ORDER BY fetchedAt") suspend fun allOnce(): List<ProductEntity>
}

@Dao
interface CustomFoodDao {
    @Insert suspend fun insert(e: CustomFoodEntity): Long
    @Update suspend fun update(e: CustomFoodEntity)
    @Delete suspend fun delete(e: CustomFoodEntity)
    @Query("SELECT * FROM custom_food ORDER BY name") fun all(): Flow<List<CustomFoodEntity>>
    @Query("SELECT * FROM custom_food ORDER BY name") suspend fun allOnce(): List<CustomFoodEntity>
    @Query("SELECT * FROM custom_food WHERE id = :id") suspend fun get(id: Long): CustomFoodEntity?
}

@Dao
interface CorrectionDao {
    @Upsert suspend fun upsert(e: CorrectionEntity)
    @Query("DELETE FROM correction WHERE `key` = :key") suspend fun delete(key: String)
    @Query("SELECT * FROM correction") fun all(): Flow<List<CorrectionEntity>>
    @Query("SELECT * FROM correction") suspend fun allOnce(): List<CorrectionEntity>
}

@Dao
interface WebLookupDao {
    @Upsert suspend fun upsert(e: WebLookupEntity)
    @Query("SELECT * FROM web_lookup WHERE `query` = :q") suspend fun get(q: String): WebLookupEntity?
}

@Dao
interface SuggestionDao {
    @Upsert suspend fun upsert(e: SuggestionEntity)
    @Query("SELECT * FROM suggestion WHERE sentAt IS NULL ORDER BY createdAt") suspend fun pending(): List<SuggestionEntity>
    @Query("SELECT * FROM suggestion ORDER BY createdAt DESC LIMIT 50") fun recent(): Flow<List<SuggestionEntity>>
}

@Dao
interface ChallengeDao {
    @Insert suspend fun insert(e: ChallengeEntity): Long
    @Update suspend fun update(e: ChallengeEntity)
    @Query("SELECT * FROM challenge WHERE active = 1 LIMIT 1") fun active(): Flow<ChallengeEntity?>
    @Query("SELECT * FROM challenge WHERE active = 1 LIMIT 1") suspend fun activeOnce(): ChallengeEntity?
    @Query("SELECT * FROM challenge ORDER BY startEpochDay DESC") fun all(): Flow<List<ChallengeEntity>>
    @Query("SELECT * FROM challenge ORDER BY startEpochDay") suspend fun allOnce(): List<ChallengeEntity>
}

@Database(
    entities = [
        FoodLogEntity::class, BmEntity::class, SymptomEntity::class, CheckInEntity::class, QuickEventEntity::class, WeeklyEntity::class,
        ProductEntity::class, CustomFoodEntity::class, CorrectionEntity::class, WebLookupEntity::class, SuggestionEntity::class, ChallengeEntity::class,
    ],
    version = 1,
    exportSchema = true,
)
abstract class PicklessDb : RoomDatabase() {
    abstract fun food(): FoodLogDao
    abstract fun bm(): BmDao
    abstract fun symptoms(): SymptomDao
    abstract fun checkins(): CheckInDao
    abstract fun events(): QuickEventDao
    abstract fun weekly(): WeeklyDao
    abstract fun products(): ProductDao
    abstract fun custom(): CustomFoodDao
    abstract fun corrections(): CorrectionDao
    abstract fun web(): WebLookupDao
    abstract fun suggestions(): SuggestionDao
    abstract fun challenges(): ChallengeDao

    companion object {
        const val NAME = "pickless.db"

        fun open(context: Context): PicklessDb = Room.databaseBuilder(context, PicklessDb::class.java, NAME).build()
    }
}
