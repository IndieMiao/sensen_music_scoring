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

    private const val LIST_URL_TEMPLATE =
        "http://210.22.95.26:30027/api/audio/querySongInfo?pageNo=%d&pageSize=%d"
    private const val PAGE_SIZE = 100
    private const val MAX_PAGES = 50   // safety cap ≈ 5000 songs

    private val main = Handler(Looper.getMainLooper())

    /**
     * Fetch every page of the catalog, concatenated. Sequential — at pageSize=100
     * and ~500 songs on the server that's 5 round-trips. Callback runs on main.
     */
    fun fetchAll(onResult: (Result) -> Unit) {
        thread(name = "song-catalog", isDaemon = true) {
            val result = runCatching { fetchAllBlocking() }
                .getOrElse { Result.Err(it.message ?: it.javaClass.simpleName) }
            main.post { onResult(result) }
        }
    }

    private fun fetchAllBlocking(): Result {
        val acc = ArrayList<Song>()
        var pageNo = 1
        while (pageNo <= MAX_PAGES) {
            val page = fetchPageBlocking(pageNo) // Page or Err
            when (page) {
                is PageResult.Err -> return Result.Err(page.message)
                is PageResult.Ok -> {
                    acc.addAll(page.songs)
                    if (pageNo >= page.totalPages) return Result.Ok(acc)
                    pageNo++
                }
            }
        }
        return Result.Ok(acc)
    }

    private sealed class PageResult {
        data class Ok(val songs: List<Song>, val totalPages: Int) : PageResult()
        data class Err(val message: String) : PageResult()
    }

    private fun fetchPageBlocking(pageNo: Int): PageResult {
        val url = String.format(LIST_URL_TEMPLATE, pageNo, PAGE_SIZE)
        val conn = (URL(url).openConnection() as HttpURLConnection).apply {
            connectTimeout = 10_000
            readTimeout = 15_000
            requestMethod = "GET"
        }
        return try {
            val code = conn.responseCode
            if (code != 200) return PageResult.Err("HTTP $code")
            val body = conn.inputStream.bufferedReader(Charsets.UTF_8).use { it.readText() }
            parsePage(body)
        } finally {
            conn.disconnect()
        }
    }

    private fun parsePage(body: String): PageResult {
        val root = JSONObject(body)
        val apiCode = root.optInt("code", -1)
        if (apiCode != 0) {
            val msg = root.optString("message", "")
            return PageResult.Err(if (msg.isNotEmpty()) msg else "API code $apiCode")
        }
        val data = root.optJSONObject("data") ?: return PageResult.Err("missing data")
        // Server default when the field is absent: treat as last page (1).
        val totalPages = data.optInt("totalPages", 1).coerceAtLeast(1)
        val content = data.optJSONArray("content")
            ?: return PageResult.Err("missing data.content")

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
        return PageResult.Ok(out, totalPages)
    }
}
