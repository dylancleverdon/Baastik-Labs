package com.baastiklabs.pickless.net

import android.content.Context
import android.database.sqlite.SQLiteDatabase
import com.baastiklabs.pickless.BuildConfig
import com.baastiklabs.pickless.core.analysis.ProductInput
import com.baastiklabs.pickless.core.barcode.Gtin
import com.baastiklabs.pickless.core.fodmap.FodmapGroup
import com.baastiklabs.pickless.core.fodmap.FodmapLimit
import com.baastiklabs.pickless.core.lookup.BraveFodmap
import com.baastiklabs.pickless.core.lookup.OpenFoodFacts
import com.baastiklabs.pickless.core.text.TextNorm
import com.baastiklabs.pickless.core.update.Updates
import com.baastiklabs.pickless.data.PicklessDb
import com.baastiklabs.pickless.data.ProductEntity
import com.baastiklabs.pickless.data.Settings
import com.baastiklabs.pickless.data.WebLookupEntity
import com.baastiklabs.pickless.log.AppLog
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import okhttp3.OkHttpClient
import okhttp3.Request
import java.io.File
import java.io.IOException
import java.security.MessageDigest
import java.util.concurrent.TimeUnit
import java.util.zip.GZIPInputStream

object Http {
    val client: OkHttpClient by lazy {
        OkHttpClient.Builder()
            .connectTimeout(15, TimeUnit.SECONDS)
            .readTimeout(45, TimeUnit.SECONDS)
            .followRedirects(true)
            .addInterceptor { chain ->
                chain.proceed(chain.request().newBuilder().header("User-Agent", OpenFoodFacts.userAgent(BuildConfig.VERSION_NAME)).build())
            }
            .build()
    }

    /** Body text for a 200 response, or null on any failure (callers fall back gracefully). */
    suspend fun getText(url: String, headers: Map<String, String> = emptyMap()): String? = withContext(Dispatchers.IO) {
        runCatching {
            val req = Request.Builder().url(url).apply { headers.forEach { (k, v) -> header(k, v) } }.build()
            client.newCall(req).execute().use { r -> if (r.isSuccessful) r.body?.string() else { AppLog.w("http", "${r.code} for ${url.substringBefore('?')}"); null } }
        }.onFailure { AppLog.w("http", "failed ${url.substringBefore('?')}: ${it.javaClass.simpleName}") }.getOrNull()
    }

    /** Stream a download to [dest], returning its SHA-256 hex, or throw. */
    suspend fun download(url: String, dest: File): String = withContext(Dispatchers.IO) {
        val req = Request.Builder().url(url).build()
        client.newCall(req).execute().use { r ->
            if (!r.isSuccessful) throw IOException("HTTP ${r.code}")
            val md = MessageDigest.getInstance("SHA-256")
            dest.parentFile?.mkdirs()
            r.body!!.byteStream().use { input ->
                dest.outputStream().use { out ->
                    val buf = ByteArray(64 * 1024)
                    while (true) {
                        val n = input.read(buf)
                        if (n < 0) break
                        md.update(buf, 0, n)
                        out.write(buf, 0, n)
                    }
                }
            }
            md.digest().joinToString("") { "%02x".format(it) }
        }
    }
}

/** The offline pack of US branded products (USDA FoodData Central), published to the pickless-data release. */
class UsdaPack(private val context: Context, private val settings: Settings) {
    private val dir = File(context.filesDir, "packs")
    private val file = File(dir, "usda.db")
    @Volatile private var db: SQLiteDatabase? = null

    val installed: Boolean get() = file.exists()

    private fun open(): SQLiteDatabase? {
        db?.let { return it }
        if (!file.exists()) return null
        return runCatching { SQLiteDatabase.openDatabase(file.path, null, SQLiteDatabase.OPEN_READONLY) }
            .onFailure { AppLog.e("pack", "open failed", it) }.getOrNull()?.also { db = it }
    }

    fun count(): Long = open()?.let { d -> runCatching { d.rawQuery("SELECT COUNT(*) FROM products", null).use { c -> if (c.moveToFirst()) c.getLong(0) else 0 } }.getOrDefault(0) } ?: 0

    fun lookup(key: String): ProductInput? {
        val d = open() ?: return null
        return runCatching {
            d.rawQuery("SELECT gtin, name, brand, ingredients, serving_g, serving_text, category, fat, sugar, fiber FROM products WHERE gtin = ?", arrayOf(key)).use { c ->
                if (!c.moveToFirst()) null else row(c)
            }
        }.getOrNull()
    }

    fun search(query: String, limit: Int = 15): List<ProductInput> {
        val d = open() ?: return emptyList()
        val words = TextNorm.words(query).filter { it.length >= 2 }.take(5)
        if (words.isEmpty()) return emptyList()
        val match = words.joinToString(" ") { "$it*" }
        return runCatching {
            d.rawQuery(
                "SELECT p.gtin, p.name, p.brand, p.ingredients, p.serving_g, p.serving_text, p.category, p.fat, p.sugar, p.fiber " +
                    "FROM products_fts f JOIN products p ON p.rowid = f.docid WHERE products_fts MATCH ? LIMIT $limit",
                arrayOf(match),
            ).use { c -> buildList { while (c.moveToNext()) add(row(c)) } }
        }.onFailure { AppLog.w("pack", "search failed: ${it.message}") }.getOrDefault(emptyList())
    }

    private fun row(c: android.database.Cursor): ProductInput {
        fun d(i: Int) = if (c.isNull(i)) null else c.getDouble(i)
        fun s(i: Int) = if (c.isNull(i)) null else c.getString(i)
        return ProductInput(
            code = s(0), name = s(1), brand = s(2), ingredientsText = s(3), servingGrams = d(4), servingLabel = s(5),
            categories = listOfNotNull(s(6)), fatPer100 = d(7), sugarsPer100 = d(8), fiberPer100 = d(9), source = "usda",
        )
    }

    /** Download a newer pack if the data manifest has one. Returns a status line. */
    suspend fun refresh(force: Boolean = false): String = withContext(Dispatchers.IO) {
        val body = Http.getText(BuildConfig.DATA_MANIFEST_URL) ?: return@withContext "Couldn't reach the data release."
        val packs = Updates.parsePacks(body) ?: return@withContext "Data manifest unreadable."
        val pack = packs.packs.firstOrNull { it.id == "usda-branded" } ?: return@withContext "No US product pack published yet."
        if (!Updates.trusted(pack.url, BuildConfig.REPO)) return@withContext "Untrusted pack location."
        val current = settings.current().packVersion
        if (!force && current == pack.version && file.exists()) return@withContext "Up to date (${pack.version})."
        val gz = File(dir, "usda.db.gz.part")
        val tmp = File(dir, "usda.db.part")
        try {
            val sha = Http.download(pack.url, gz)
            if (sha != pack.sha256) { gz.delete(); return@withContext "Download didn't match its checksum; will retry." }
            GZIPInputStream(gz.inputStream().buffered()).use { input -> tmp.outputStream().use { input.copyTo(it, 64 * 1024) } }
            gz.delete()
            val ok = runCatching { SQLiteDatabase.openDatabase(tmp.path, null, SQLiteDatabase.OPEN_READONLY).use { it.rawQuery("SELECT COUNT(*) FROM products", null).use { c -> c.moveToFirst() && c.getLong(0) > 0 } } }.getOrDefault(false)
            if (!ok) { tmp.delete(); return@withContext "Pack was damaged; will retry." }
            synchronized(this@UsdaPack) {
                db?.close(); db = null
                file.delete()
                tmp.renameTo(file)
            }
            settings.setPackVersion(pack.version)
            AppLog.i("pack", "installed ${pack.version} (${pack.products} products)")
            "Installed ${pack.version}: ${"%,d".format(pack.products)} US products."
        } catch (e: Exception) {
            gz.delete(); tmp.delete()
            AppLog.e("pack", "download failed", e)
            "Download failed (${e.javaClass.simpleName}); will retry."
        }
    }
}

sealed class Lookup {
    data class Found(val product: ProductEntity, val input: ProductInput, val fromCache: Boolean) : Lookup()
    data class NotFound(val key: String, val offline: Boolean) : Lookup()
    data object Invalid : Lookup()
}

/** Barcode -> product: phone cache, then the offline US pack, then Open Food Facts. */
class Products(private val db: PicklessDb, private val pack: UsdaPack) {
    private val json = Json { ignoreUnknownKeys = true; encodeDefaults = true }

    fun decode(e: ProductEntity): ProductInput = runCatching { json.decodeFromString(ProductInput.serializer(), e.json) }.getOrElse { ProductInput(name = e.name) }

    suspend fun lookup(raw: String): Lookup = withContext(Dispatchers.IO) {
        val key = Gtin.key(raw) ?: return@withContext Lookup.Invalid
        db.products().get(key)?.let { return@withContext Lookup.Found(it, decode(it), true) }
        pack.lookup(key)?.let { input -> return@withContext Lookup.Found(save(key, input.copy(code = key)), input, false) }
        var reached = false
        for (code in Gtin.lookupCodes(key)) {
            val body = Http.getText(OpenFoodFacts.productUrl(code)) ?: continue
            reached = true
            val input = OpenFoodFacts.parseProduct(body) ?: continue
            AppLog.i("lookup", "found on Open Food Facts")
            return@withContext Lookup.Found(save(key, input.copy(code = key)), input, false)
        }
        Lookup.NotFound(key, offline = !reached)
    }

    suspend fun save(key: String, input: ProductInput, keepFlags: Boolean = true): ProductEntity {
        val old = if (keepFlags) db.products().get(key) else null
        val e = ProductEntity(key, input.name ?: "Product ${Gtin.display(key)}", json.encodeToString(ProductInput.serializer(), input), input.source,
            System.currentTimeMillis(), old?.favorite ?: false, old?.userVerdict)
        db.products().upsert(e)
        return e
    }

    /** Search the US pack and Open Food Facts by name. */
    suspend fun search(q: String): List<ProductInput> = withContext(Dispatchers.IO) {
        val local = pack.search(q)
        val online = if (q.length >= 3) Http.getText(OpenFoodFacts.searchUrl(q, 15))?.let { OpenFoodFacts.parseSearch(it) } ?: emptyList() else emptyList()
        (local + online).filter { !it.name.isNullOrBlank() }.distinctBy { (it.code ?: it.name)!!.lowercase() }.take(25)
    }
}

@Serializable
data class WebLimit(val group: String, val low: Double, val moderate: Double)

@Serializable
data class WebSource(val title: String, val url: String, val host: String, val excerpt: String)

@Serializable
data class WebResult(
    val food: String,
    val verdict: String,
    val summary: String,
    val groups: List<String>,
    val lowServingText: String? = null,
    val limits: List<WebLimit>,
    val sources: List<WebSource>,
    val at: Long,
) {
    fun fodmapLimits(): List<FodmapLimit> = limits.mapNotNull { l -> FodmapGroup.byId(l.group)?.let { FodmapLimit(it, l.low, l.moderate) } }
}

/** Brave Search lookups for foods Pickless doesn't know. Capped per month so it stays inside the free credit. */
class WebLookup(private val db: PicklessDb, private val settings: Settings) {
    private val json = Json { ignoreUnknownKeys = true; encodeDefaults = true }

    suspend fun cached(food: String): WebResult? = db.web().get(TextNorm.normalize(food))?.let { runCatching { json.decodeFromString(WebResult.serializer(), it.resultJson) }.getOrNull() }

    sealed class Outcome {
        data class Ok(val result: WebResult) : Outcome()
        data class Problem(val message: String) : Outcome()
    }

    suspend fun lookup(food: String, defaultServingGrams: Double): Outcome {
        cached(food)?.let { return Outcome.Ok(it) }
        val p = settings.current()
        if (p.braveKey.isBlank()) return Outcome.Problem("Add your Brave Search key in Settings to look foods up.")
        if (!p.braveAvailable) return Outcome.Problem("This month's lookups (${p.braveCap}) are used up, so it can't cost you anything. More next month.")
        val body = Http.getText(BraveFodmap.url(food), mapOf(BraveFodmap.KEY_HEADER to p.braveKey, "Accept" to "application/json"))
        settings.countBrave()
        if (body == null) return Outcome.Problem("Couldn't reach Brave Search. Check the connection or the key.")
        val r = BraveFodmap.parse(food, body, defaultServingGrams)
        val result = WebResult(
            food, r.verdict.name, r.summary, r.groups.map { it.id }, r.lowServingText,
            r.limits.map { WebLimit(it.group.id, it.lowGrams, it.moderateGrams) },
            r.sources.map { WebSource(it.title, it.url, it.host, it.excerpt) }, System.currentTimeMillis(),
        )
        db.web().upsert(WebLookupEntity(TextNorm.normalize(food), json.encodeToString(WebResult.serializer(), result), result.at))
        AppLog.i("web", "lookup done: ${r.verdict}")
        return Outcome.Ok(result)
    }
}
