package com.sensen.singscoring.demo

import android.content.Context
import android.os.Handler
import android.os.Looper
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import java.util.zip.ZipInputStream
import kotlin.concurrent.thread

/**
 * Stages a song for scoring: downloads its zip (if not already cached) and
 * extracts the chorus mp3 for preview playback. Idempotent per song.
 *
 * Cache layout under cacheDir/songs/<id>/:
 *   <id>.zip              — downloaded once, then served from disk
 *   <id>_chorus.mp3       — extracted on first stage
 *   <id>.zip.part         — transient; renamed to <id>.zip on successful download
 */
object SongStaging {

    data class Staged(val zipPath: String, val mp3Path: String)

    sealed class DownloadResult {
        data class Ok(val zipPath: String) : DownloadResult()
        data class Err(val message: String) : DownloadResult()
    }

    private val main = Handler(Looper.getMainLooper())

    /**
     * Ensure the zip for [song] is present in cacheDir. Callback runs on main.
     * If already cached, returns Ok without hitting the network.
     */
    fun download(ctx: Context, song: SongCatalog.Song, onResult: (DownloadResult) -> Unit) {
        val dir = File(ctx.cacheDir, "songs/${song.id}").apply { mkdirs() }
        val zip = File(dir, "${song.id}.zip")
        val part = File(dir, "${song.id}.zip.part")

        thread(name = "song-download-${song.id}", isDaemon = true) {
            val result = runCatching {
                if (zip.exists() && zip.length() > 0) {
                    DownloadResult.Ok(zip.absolutePath)
                } else {
                    if (part.exists()) part.delete()
                    downloadTo(song.zipUrl, part)
                    if (!part.renameTo(zip)) {
                        throw IllegalStateException("rename .part → .zip failed")
                    }
                    DownloadResult.Ok(zip.absolutePath)
                }
            }.getOrElse {
                part.delete()
                DownloadResult.Err(it.message ?: it.javaClass.simpleName)
            }
            main.post { onResult(result) }
        }
    }

    /**
     * Extract <id>_chorus.mp3 alongside the zip if it isn't there yet,
     * and return both paths. The zip must already be on disk (see download).
     */
    fun stage(ctx: Context, song: SongCatalog.Song): Staged {
        val dir = File(ctx.cacheDir, "songs/${song.id}").apply { mkdirs() }
        val zip = File(dir, "${song.id}.zip")
        val mp3 = File(dir, "${song.id}_chorus.mp3")
        check(zip.exists()) { "stage called before download: ${zip.absolutePath}" }

        if (!mp3.exists()) {
            ZipInputStream(zip.inputStream()).use { zis ->
                while (true) {
                    val e = zis.nextEntry ?: break
                    if (e.name.endsWith("_chorus.mp3")) {
                        mp3.outputStream().use { zis.copyTo(it) }
                        break
                    }
                }
            }
        }
        return Staged(zip.absolutePath, mp3.absolutePath)
    }

    private fun downloadTo(url: String, dst: File) {
        val conn = (URL(url).openConnection() as HttpURLConnection).apply {
            connectTimeout = 10_000
            readTimeout = 60_000
            requestMethod = "GET"
            instanceFollowRedirects = true
        }
        try {
            val code = conn.responseCode
            if (code != 200) throw IllegalStateException("HTTP $code downloading $url")
            conn.inputStream.use { input ->
                dst.outputStream().use { out -> input.copyTo(out) }
            }
        } finally {
            conn.disconnect()
        }
    }
}
