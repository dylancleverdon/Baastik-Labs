package com.baastiklabs.pickless.core.update

import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json

/**
 * The update manifest published on the rolling `pickless-channel` release. The app installs a
 * build only from this repo's `pickless-*` releases, over HTTPS, and only if the file's SHA-256
 * matches. The suggestion box endpoint rides along so it can be connected without a new build.
 */
@Serializable
data class UpdateManifest(
    val app: String = "pickless",
    val version: String,
    val versionCode: Int,
    val apk: Asset,
    val minSdk: Int = 26,
    val notes: String = "",
    val published: String = "",
    val suggestions: Suggestions? = null,
) {
    @Serializable
    data class Asset(val url: String, val sha256: String, val size: Long)

    @Serializable
    data class Suggestions(val endpoint: String? = null)
}

@Serializable
data class DataPacks(val packs: List<Pack> = emptyList()) {
    @Serializable
    data class Pack(val id: String, val version: String, val url: String, val sha256: String, val size: Long, val products: Long = 0, val note: String = "")
}

object Updates {
    val json = Json { ignoreUnknownKeys = true; isLenient = true; encodeDefaults = true }

    fun parse(body: String): UpdateManifest? = runCatching { json.decodeFromString(UpdateManifest.serializer(), body) }.getOrNull()

    fun parsePacks(body: String): DataPacks? = runCatching { json.decodeFromString(DataPacks.serializer(), body) }.getOrNull()

    /** Only this repo's pickless releases count as a source of builds or data. */
    fun trusted(url: String, repo: String): Boolean =
        url.startsWith("https://github.com/$repo/releases/download/pickless-")

    fun shouldUpdate(currentCode: Int, m: UpdateManifest, repo: String, sdk: Int): Boolean =
        m.app == "pickless" && m.versionCode > currentCode && sdk >= m.minSdk && trusted(m.apk.url, repo) &&
            m.apk.sha256.matches(Regex("[0-9a-f]{64}")) && m.apk.size > 0

    fun sha256Hex(bytes: ByteArray): String =
        java.security.MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }
}
