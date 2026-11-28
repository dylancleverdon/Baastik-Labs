package com.baastiklabs.pickless.log

import android.content.Context
import android.os.Build
import java.io.File
import java.io.PrintWriter
import java.io.StringWriter
import java.time.LocalDateTime
import java.time.format.DateTimeFormatter

/**
 * A small on-disk log (two 256 KB files) the suggestion box can attach with "Send logs".
 * Diagnostics only: never write diary contents here.
 */
object AppLog {
    private lateinit var dir: File
    private val lock = Any()
    private val fmt = DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss")
    private const val MAX = 256 * 1024

    fun init(context: Context) {
        dir = File(context.filesDir, "logs").apply { mkdirs() }
        val previous = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { t, e ->
            runCatching {
                val sw = StringWriter()
                e.printStackTrace(PrintWriter(sw))
                File(dir, "crash.txt").writeText("${LocalDateTime.now().format(fmt)} on ${t.name}\n$sw")
                e("crash", "uncaught ${e.javaClass.simpleName}: ${e.message}")
            }
            previous?.uncaughtException(t, e)
        }
        i("app", "start ${Build.MANUFACTURER} ${Build.MODEL} Android ${Build.VERSION.RELEASE} (SDK ${Build.VERSION.SDK_INT})")
    }

    fun i(tag: String, msg: String) = write("I", tag, msg)
    fun w(tag: String, msg: String) = write("W", tag, msg)
    fun e(tag: String, msg: String, t: Throwable? = null) = write("E", tag, msg + (t?.let { " :: ${it.javaClass.simpleName}: ${it.message}" } ?: ""))

    private fun write(level: String, tag: String, msg: String) {
        if (!::dir.isInitialized) return
        synchronized(lock) {
            runCatching {
                val f = File(dir, "pickless.log")
                if (f.length() > MAX) f.renameTo(File(dir, "pickless.1.log"))
                f.appendText("${LocalDateTime.now().format(fmt)} $level/$tag: ${msg.take(800)}\n")
            }
        }
    }

    /** The last crash, if it hasn't been looked at. */
    fun crash(): File? = if (::dir.isInitialized) File(dir, "crash.txt").takeIf { it.exists() } else null

    fun clearCrash() { crash()?.delete() }

    /** Recent logs as text for "Send logs" (newest last). */
    fun recent(maxChars: Int = 200_000): String {
        if (!::dir.isInitialized) return ""
        val parts = listOf("pickless.1.log", "pickless.log").map { File(dir, it) }.filter { it.exists() }.joinToString("") { it.readText() }
        val crash = crash()?.readText()?.let { "\n--- last crash ---\n$it" } ?: ""
        return (parts + crash).takeLast(maxChars)
    }
}
