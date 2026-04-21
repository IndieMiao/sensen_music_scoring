package com.sensen.singscoring.demo

import android.content.Context
import java.io.File
import java.util.zip.ZipInputStream

/**
 * Manages the song-highlight zip assets bundled with the demo.
 *
 * The SDK opens songs by filesystem path, and ExoPlayer needs a URI for the mp3,
 * so both the zip and the extracted mp3 are staged into the app's cache dir.
 */
object SongAssets {

    data class Song(
        val code: String,        // e.g. "7162848696587380"
        val displayName: String, // Chinese name parsed from the JSON
        val zipAssetName: String // e.g. "7162848696587380.zip"
    )

    fun list(ctx: Context): List<Song> {
        val names = ctx.assets.list("")?.filter { it.endsWith(".zip") } ?: emptyList()
        return names.sorted().map { zip ->
            val code = zip.removeSuffix(".zip")
            val display = readDisplayName(ctx, zip, code) ?: code
            Song(code, display, zip)
        }
    }

    /**
     * Extract the zip to cacheDir/songs/<code>/, returning (zipPath, mp3Path).
     * Idempotent — subsequent calls return the cached paths.
     */
    fun stage(ctx: Context, song: Song): Staged {
        val dir = File(ctx.cacheDir, "songs/${song.code}").apply { mkdirs() }
        val zipOut = File(dir, song.zipAssetName)
        val mp3Out = File(dir, "${song.code}_chorus.mp3")

        if (!zipOut.exists()) {
            ctx.assets.open(song.zipAssetName).use { src ->
                zipOut.outputStream().use { dst -> src.copyTo(dst) }
            }
        }
        if (!mp3Out.exists()) {
            ZipInputStream(zipOut.inputStream()).use { zis ->
                while (true) {
                    val e = zis.nextEntry ?: break
                    if (e.name.endsWith("_chorus.mp3")) {
                        mp3Out.outputStream().use { zis.copyTo(it) }
                        break
                    }
                }
            }
        }
        return Staged(zipOut.absolutePath, mp3Out.absolutePath)
    }

    data class Staged(val zipPath: String, val mp3Path: String)

    // Pull `"name":"..."` out of the JSON without adding a JSON dependency. Flat schema,
    // plus we only need one field. Matches the C++ core's hand-rolled parser style.
    private fun readDisplayName(ctx: Context, zipAsset: String, code: String): String? {
        return try {
            ctx.assets.open(zipAsset).use { src ->
                ZipInputStream(src).use { zis ->
                    while (true) {
                        val e = zis.nextEntry ?: return null
                        if (e.name == "${code}_chorus.json") {
                            val text = zis.readBytes().toString(Charsets.UTF_8)
                            val marker = "\"name\""
                            val i = text.indexOf(marker)
                            if (i < 0) return null
                            val q1 = text.indexOf('"', i + marker.length + 1)
                            if (q1 < 0) return null
                            val q2 = text.indexOf('"', q1 + 1)
                            if (q2 < 0) return null
                            return text.substring(q1 + 1, q2)
                        }
                    }
                    @Suppress("UNREACHABLE_CODE") null
                }
            }
        } catch (_: Exception) { null }
    }
}
