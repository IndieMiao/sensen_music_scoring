package com.sensen.singscoring.demo

import android.os.Handler
import android.os.Looper
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import kotlin.concurrent.thread

/**
 * Song catalog backed by the querySongInfo API.
 *
 * Replaces what SongAssets.list(...) used to do against bundled-zip metadata.
 * The list call returns each song's direct zip URL in `resourceUrl`, so there
 * is no separate detail call in the download path.
 */
object SongCatalog {

    data class Song(
        val id: String,
        val name: String,
        val singer: String,
        val rhythm: String,   // "fast" | "slow"
        val zipUrl: String,
    )

    sealed class Result {
        data class Ok(val songs: List<Song>) : Result()
        data class Err(val message: String) : Result()
    }

    private const val LIST_URL =
        "http://210.22.95.26:30027/api/audio/querySongInfo?pageNo=1&pageSize=100"

    private val main = Handler(Looper.getMainLooper())

    /** Fetch page 1 (100 songs). Callback runs on the main thread. */
    fun fetchFirstPage(onResult: (Result) -> Unit) {
        thread(name = "song-catalog", isDaemon = true) {
            val result = runCatching { fetchBlocking() }
                .getOrElse { Result.Err(it.message ?: it.javaClass.simpleName) }
            main.post { onResult(result) }
        }
    }

    private fun fetchBlocking(): Result {
        val conn = (URL(LIST_URL).openConnection() as HttpURLConnection).apply {
            connectTimeout = 10_000
            readTimeout = 15_000
            requestMethod = "GET"
        }
        return try {
            val code = conn.responseCode
            if (code != 200) return Result.Err("HTTP $code")
            val body = conn.inputStream.bufferedReader(Charsets.UTF_8).use { it.readText() }
            parse(body)
        } finally {
            conn.disconnect()
        }
    }

    private fun parse(body: String): Result {
        val root = JSONObject(body)
        val apiCode = root.optInt("code", -1)
        if (apiCode != 0) {
            val msg = root.optString("message", "")
            return Result.Err(if (msg.isNotEmpty()) msg else "API code $apiCode")
        }
        val content = root.optJSONObject("data")?.optJSONArray("content")
            ?: return Result.Err("missing data.content")

        val out = ArrayList<Song>(content.length())
        for (i in 0 until content.length()) {
            val o = content.optJSONObject(i) ?: continue
            val id = o.optString("id", "")
            val name = o.optString("name", "")
            val singer = o.optString("singer", "")
            val rhythm = o.optString("rhythm", "")
            val zipUrl = o.optString("resourceUrl", "")
            if (id.isEmpty() || name.isEmpty() || zipUrl.isEmpty()) continue
            out.add(Song(id, name, singer, rhythm, zipUrl))
        }
        return Result.Ok(out)
    }
}
