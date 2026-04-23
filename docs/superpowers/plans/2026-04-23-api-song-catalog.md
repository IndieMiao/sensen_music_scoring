# API-backed song catalog — Android demo implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the Android demo's asset-bundled song list with an API-backed catalog. Zips are downloaded on demand to `cacheDir/songs/<id>.zip` and cached indefinitely.

**Architecture:** Three new seams on the demo side:
1. `SongCatalog.kt` — HTTP + JSON for the `querySongInfo` list endpoint.
2. `SongStaging.kt` (replaces `SongAssets.kt`) — download + extract from `cacheDir`.
3. `MainActivity.kt` state machine — adds `DOWNLOADING`, plus picker loading/error substates.

SDK, JNI, and C++ core are **untouched**. The `SongHighlightSamples/*.zip` files stay on disk (C++ integration tests still need them) but stop being packaged into the APK.

**Spec:** `docs/superpowers/specs/2026-04-23-api-song-catalog-design.md`

**Tech stack:** Kotlin, `java.net.HttpURLConnection`, `org.json` (both built into Android). No new Gradle deps.

**Build command (Windows, MSYS bash):**
```bash
export JAVA_HOME="/c/Program Files/Android/Android Studio/jbr"
./gradlew :demo-android:assembleDebug
```
First-time `JAVA_HOME` export can be skipped if the shell already has it set.

**Testing:** There are no unit tests for `demo-android` today, and this change is pure UI/networking glue. Verification is: the module compiles (`:demo-android:assembleDebug` green) at each task boundary, plus a final manual smoke test on-device.

---

## File structure

- **New:** `demo-android/src/main/kotlin/com/sensen/singscoring/demo/SongCatalog.kt` — network + parse, exposes `Song` data class and `fetchFirstPage(onResult)`.
- **New:** `demo-android/src/main/kotlin/com/sensen/singscoring/demo/SongStaging.kt` — owns `cacheDir/songs/`, does download + mp3 extraction. Keeps the `SongAssets.Staged` return shape, renamed.
- **Deleted:** `demo-android/src/main/kotlin/com/sensen/singscoring/demo/SongAssets.kt`.
- **Modified:** `demo-android/src/main/kotlin/com/sensen/singscoring/demo/MainActivity.kt` — new state, new renders, picker substates, `SongAssets.Song` → `SongCatalog.Song`.
- **New:** `demo-android/src/main/res/xml/network_security_config.xml` — cleartext allowed only for `210.22.95.26`.
- **Modified:** `demo-android/src/main/AndroidManifest.xml` — `INTERNET` permission + `networkSecurityConfig` attribute.
- **Modified:** `demo-android/build.gradle.kts` — remove asset src dir.

---

### Task 1: Unbundle the sample zips from the APK and wire the manifest for network access

**Files:**
- Modify: `demo-android/build.gradle.kts:35` (remove the `assets.srcDirs` line)
- Modify: `demo-android/src/main/AndroidManifest.xml:4` (add INTERNET permission + networkSecurityConfig)
- Create: `demo-android/src/main/res/xml/network_security_config.xml`

This task only touches build config and resources. It intentionally leaves `SongAssets.kt` and the asset-based flow in place for one task — the code still builds and runs against the bundled zips until Task 4 rewires it. This means `assembleDebug` passes throughout, keeping every commit buildable.

- [ ] **Step 1: Remove the asset source-set binding**

Edit `demo-android/build.gradle.kts`. Delete the line at `sourceSets["main"].assets.srcDirs("../SongHighlightSamples")`. The final shape of that stanza:

```kotlin
    sourceSets["main"].java.srcDirs("src/main/kotlin")
```

- [ ] **Step 2: Add the INTERNET permission and network security config reference**

Edit `demo-android/src/main/AndroidManifest.xml` to match:

```xml
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android">

    <uses-permission android:name="android.permission.RECORD_AUDIO" />
    <uses-permission android:name="android.permission.INTERNET" />

    <application
        android:allowBackup="false"
        android:label="@string/app_name"
        android:networkSecurityConfig="@xml/network_security_config"
        android:theme="@style/Theme.SingScoringDemo">

        <activity
            android:name=".MainActivity"
            android:exported="true"
            android:configChanges="orientation|screenSize|screenLayout|keyboardHidden">
            <intent-filter>
                <action android:name="android.intent.action.MAIN" />
                <category android:name="android.intent.category.LAUNCHER" />
            </intent-filter>
        </activity>
    </application>
</manifest>
```

- [ ] **Step 3: Create the network security config**

Create the directory if it does not exist, then create `demo-android/src/main/res/xml/network_security_config.xml` with exactly:

```xml
<?xml version="1.0" encoding="utf-8"?>
<network-security-config>
    <domain-config cleartextTrafficPermitted="true">
        <domain includeSubdomains="false">210.22.95.26</domain>
    </domain-config>
</network-security-config>
```

The narrow scope matters: HTTPS stays required for everything else (including the CDN zip downloads).

- [ ] **Step 4: Build**

Run:
```bash
./gradlew :demo-android:assembleDebug
```

Expected: `BUILD SUCCESSFUL`. The APK in `demo-android/build/outputs/apk/debug/` will be noticeably smaller now that `SongHighlightSamples/*.zip` is no longer bundled.

- [ ] **Step 5: Commit**

```bash
git add demo-android/build.gradle.kts \
        demo-android/src/main/AndroidManifest.xml \
        demo-android/src/main/res/xml/network_security_config.xml
git commit -m "chore(demo): unbundle sample zips + allow network for querySongInfo"
```

---

### Task 2: Add `SongCatalog.kt` with the HTTP + JSON client

**Files:**
- Create: `demo-android/src/main/kotlin/com/sensen/singscoring/demo/SongCatalog.kt`

New file. Nothing else calls it yet; Task 4 wires it into `MainActivity`. The file builds on its own.

- [ ] **Step 1: Create `SongCatalog.kt`**

Create `demo-android/src/main/kotlin/com/sensen/singscoring/demo/SongCatalog.kt` with exactly:

```kotlin
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
```

- [ ] **Step 2: Build**

Run:
```bash
./gradlew :demo-android:assembleDebug
```

Expected: `BUILD SUCCESSFUL`. The file is self-contained — MainActivity still uses `SongAssets` until Task 4.

- [ ] **Step 3: Commit**

```bash
git add demo-android/src/main/kotlin/com/sensen/singscoring/demo/SongCatalog.kt
git commit -m "feat(demo): SongCatalog — fetch + parse querySongInfo list"
```

---

### Task 3: Add `SongStaging.kt` alongside `SongAssets.kt`

**Files:**
- Create: `demo-android/src/main/kotlin/com/sensen/singscoring/demo/SongStaging.kt`

New file. `SongAssets.kt` remains in place until Task 4 to keep the tree buildable. `SongStaging` speaks the new `SongCatalog.Song` type and downloads to `cacheDir/songs/<id>.zip`.

- [ ] **Step 1: Create `SongStaging.kt`**

Create `demo-android/src/main/kotlin/com/sensen/singscoring/demo/SongStaging.kt` with exactly:

```kotlin
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
```

- [ ] **Step 2: Build**

Run:
```bash
./gradlew :demo-android:assembleDebug
```

Expected: `BUILD SUCCESSFUL`. `MainActivity` still references `SongAssets`; coexistence is intentional.

- [ ] **Step 3: Commit**

```bash
git add demo-android/src/main/kotlin/com/sensen/singscoring/demo/SongStaging.kt
git commit -m "feat(demo): SongStaging — download + extract to cacheDir"
```

---

### Task 4: Rewire `MainActivity` to use the catalog + staging, add `DOWNLOADING` state

**Files:**
- Modify: `demo-android/src/main/kotlin/com/sensen/singscoring/demo/MainActivity.kt` (full rewrite of the picker, download flow, and state enum)

This is the largest task. It swaps `SongAssets.Song` → `SongCatalog.Song` across the file, rewrites `renderPicker` to be async with loading/ready/error substates, inserts a `DOWNLOADING` state, and updates existing `startPreview` / `startCountdown` to read from `SongStaging` instead of `SongAssets`.

- [ ] **Step 1: Replace the contents of `MainActivity.kt`**

Overwrite `demo-android/src/main/kotlin/com/sensen/singscoring/demo/MainActivity.kt` with exactly:

```kotlin
package com.sensen.singscoring.demo

import android.Manifest
import android.content.pm.PackageManager
import android.graphics.Color
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.view.Gravity
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.widget.Button
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import com.sensen.singscoring.SingScoringSession
import java.io.File
import java.util.zip.ZipInputStream
import kotlin.concurrent.thread

class MainActivity : AppCompatActivity() {

    private enum class State { PICKER, DOWNLOADING, PREVIEW, COUNTDOWN, RECORDING, SCORING, RESULT }

    private val sampleRate = 44100
    private var state = State.PICKER
    private var recorder: AudioRecorder? = null
    private var pendingSong: SongCatalog.Song? = null
    private var stagedZipPath: String? = null
    private var lyrics: List<LrcLine> = emptyList()

    // Recording buffer (owned by the activity; the recorder just appends).
    private val pcm = ArrayList<FloatArray>()
    private var pcmTotalSamples = 0
    private var recordingStartMs = 0L  // SystemClock.elapsedRealtime when "Sing!" hits

    private val main = Handler(Looper.getMainLooper())
    private var autoStopRunnable: Runnable? = null
    private var mediaPlayer: android.media.MediaPlayer? = null
    private var previewAutoAdvance: Runnable? = null
    private var scoringGeneration = 0
    private var downloadGeneration = 0
    private var catalogGeneration = 0

    private val root by lazy { FrameLayout(this) }

    private val requestMicPermission = registerForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { granted ->
        if (granted) pendingSong?.let { beginDownload(it) }
        else toastLike("Microphone permission denied")
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        root.setPadding(48, 96, 48, 48)
        setContentView(root)
        renderPicker()
    }

    override fun onDestroy() {
        super.onDestroy()
        main.removeCallbacksAndMessages(null)
        cancelAutoStop()
        recorder?.stop(); recorder = null
        mediaPlayer?.release(); mediaPlayer = null
    }

    // --- screens -----------------------------------------------------------

    private fun renderPicker() {
        state = State.PICKER
        root.removeAllViews()
        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            layoutParams = FrameLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT)
        }
        col.addView(titleView("SingScoring demo"))
        col.addView(subtitleView("SDK ${SingScoringSession.version} — loading songs…"))
        root.addView(col)

        val gen = ++catalogGeneration
        SongCatalog.fetchFirstPage { result ->
            if (gen != catalogGeneration || state != State.PICKER) return@fetchFirstPage
            when (result) {
                is SongCatalog.Result.Ok -> renderPickerReady(result.songs)
                is SongCatalog.Result.Err -> renderPickerError(result.message)
            }
        }
    }

    private fun renderPickerReady(songs: List<SongCatalog.Song>) {
        root.removeAllViews()
        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            layoutParams = FrameLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT)
        }
        col.addView(titleView("SingScoring demo"))
        col.addView(subtitleView("SDK ${SingScoringSession.version} — pick a song"))

        val scroll = ScrollView(this)
        val list = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        songs.forEach { song ->
            list.addView(songButton(song))
        }
        scroll.addView(list)
        col.addView(scroll, LinearLayout.LayoutParams(MATCH_PARENT, 0, 1f))
        root.addView(col)
    }

    private fun renderPickerError(message: String) {
        root.removeAllViews()
        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            layoutParams = FrameLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT)
        }
        col.addView(titleView("SingScoring demo"))
        col.addView(subtitleView("Couldn't load songs: $message"))
        col.addView(Button(this).apply {
            text = "Retry"
            layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)
                .apply { topMargin = 16 }
            setOnClickListener { renderPicker() }
        })
        root.addView(col)
    }

    private fun renderDownloading(song: SongCatalog.Song) {
        state = State.DOWNLOADING
        root.removeAllViews()
        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER_HORIZONTAL
            layoutParams = FrameLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT)
        }
        col.addView(titleRowWithBack("🎵  ${song.name}"))
        col.addView(subtitleView("Downloading song…"))
        root.addView(col)
    }

    private fun renderCountdown(song: SongCatalog.Song, secondsLeft: Int) {
        state = State.COUNTDOWN
        root.removeAllViews()
        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER_HORIZONTAL
            layoutParams = FrameLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT)
        }
        col.addView(titleRowWithBack("🎤  ${song.name}"))
        col.addView(subtitleView("Get ready to sing the chorus."))
        col.addView(TextView(this).apply {
            text = if (secondsLeft > 0) secondsLeft.toString() else "Sing!"
            textSize = 96f
            gravity = Gravity.CENTER
            layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)
                .apply { topMargin = 96 }
        })
        root.addView(col)
    }

    private fun renderRecording(song: SongCatalog.Song) {
        state = State.RECORDING
        root.removeAllViews()
        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            layoutParams = FrameLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT)
        }
        col.addView(titleRowWithBack("🎤  ${song.name}"))

        val view = LyricsScrollView(this).apply {
            setLines(lyrics)
            setClock { SystemClock.elapsedRealtime() - recordingStartMs }
            layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, 0, 1f)
                .apply { topMargin = 16; bottomMargin = 16 }
        }
        col.addView(view)

        col.addView(Button(this).apply {
            text = "Stop & score"
            layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)
            setOnClickListener { finishAndScore() }
        })
        root.addView(col)
    }

    private fun renderScoring(song: SongCatalog.Song) {
        state = State.SCORING
        root.removeAllViews()
        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER_HORIZONTAL
            layoutParams = FrameLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT)
        }
        col.addView(titleRowWithBack(song.name))
        col.addView(subtitleView("Scoring…"))
        root.addView(col)
    }

    private fun renderResult(song: SongCatalog.Song, score: Int) {
        state = State.RESULT
        root.removeAllViews()
        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER_HORIZONTAL
            layoutParams = FrameLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT)
        }
        col.addView(titleView(song.name))

        val passed = score >= 60
        col.addView(TextView(this).apply {
            text = score.toString()
            textSize = 96f
            setTextColor(if (passed) Color.parseColor("#2E7D32") else Color.parseColor("#C62828"))
            gravity = Gravity.CENTER
            layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)
                .apply { topMargin = 64; bottomMargin = 16 }
        })
        col.addView(subtitleView(if (passed) "Passed" else "Needs work (pass ≥ 60)"))

        col.addView(Button(this).apply {
            text = "Pick another song"
            layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)
                .apply { topMargin = 64 }
            setOnClickListener { renderPicker() }
        })
        root.addView(col)
    }

    private fun renderPreview(song: SongCatalog.Song) {
        state = State.PREVIEW
        root.removeAllViews()
        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            layoutParams = FrameLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT)
        }
        col.addView(titleRowWithBack("🎵  ${song.name}"))
        col.addView(subtitleView("Listen to the chorus…"))

        val view = LyricsScrollView(this).apply {
            setLines(lyrics)
            setClock { mediaPlayer?.currentPosition?.toLong() ?: 0L }
            layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, 0, 1f)
                .apply { topMargin = 16; bottomMargin = 16 }
        }
        col.addView(view)

        col.addView(Button(this).apply {
            text = "Skip →"
            layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)
            setOnClickListener { stopPreviewAndCountdown(song) }
        })
        root.addView(col)
    }

    // --- flow --------------------------------------------------------------

    private fun onSongPicked(song: SongCatalog.Song) {
        pendingSong = song
        val granted = ContextCompat.checkSelfPermission(
            this, Manifest.permission.RECORD_AUDIO
        ) == PackageManager.PERMISSION_GRANTED
        if (granted) beginDownload(song) else requestMicPermission.launch(Manifest.permission.RECORD_AUDIO)
    }

    private fun beginDownload(song: SongCatalog.Song) {
        renderDownloading(song)
        val gen = ++downloadGeneration
        SongStaging.download(this, song) { result ->
            if (gen != downloadGeneration || state != State.DOWNLOADING) return@download
            when (result) {
                is SongStaging.DownloadResult.Ok -> startPreview(song)
                is SongStaging.DownloadResult.Err -> {
                    toastLike("Download failed: ${result.message}")
                    renderPicker()
                }
            }
        }
    }

    private fun startPreview(song: SongCatalog.Song) {
        val staged = try {
            SongStaging.stage(this, song)
        } catch (e: Exception) {
            toastLike("Failed to stage song: ${e.message}")
            return
        }
        stagedZipPath = staged.zipPath
        lyrics = readLyrics(staged.zipPath, song.id)

        try {
            val mp = android.media.MediaPlayer().apply {
                setDataSource(staged.mp3Path)
                prepare()
                start()
            }
            mediaPlayer = mp
        } catch (e: Exception) {
            toastLike("Preview unavailable: ${e.message}")
            startCountdown(song)
            return
        }

        renderPreview(song)

        previewAutoAdvance = Runnable { stopPreviewAndCountdown(song) }
        main.postDelayed(previewAutoAdvance!!, 13_000L)
    }

    private fun stopPreviewAndCountdown(song: SongCatalog.Song) {
        previewAutoAdvance?.let { main.removeCallbacks(it) }
        previewAutoAdvance = null
        mediaPlayer?.stop()
        mediaPlayer?.release()
        mediaPlayer = null
        startCountdown(song)
    }

    private fun startCountdown(song: SongCatalog.Song) {
        // Stage (idempotent) + parse LRC up-front so the recording phase has nothing to wait on.
        val staged = try {
            SongStaging.stage(this, song)
        } catch (e: Exception) {
            toastLike("Failed to stage song: ${e.message}")
            return
        }
        stagedZipPath = staged.zipPath
        lyrics = readLyrics(staged.zipPath, song.id)

        // 3 → 2 → 1 → "Sing!" → start recording.
        renderCountdown(song, 3)
        main.postDelayed({ renderCountdown(song, 2) }, 1000)
        main.postDelayed({ renderCountdown(song, 1) }, 2000)
        main.postDelayed({
            renderCountdown(song, 0)            // "Sing!"
            beginRecording(song)                // also picks up the t=0 instant
        }, 3000)
    }

    private fun beginRecording(song: SongCatalog.Song) {
        pcm.clear()
        pcmTotalSamples = 0
        recordingStartMs = SystemClock.elapsedRealtime()

        val rec = AudioRecorder(sampleRate) { samples, count ->
            val copy = FloatArray(count)
            System.arraycopy(samples, 0, copy, 0, count)
            synchronized(pcm) {
                pcm.add(copy)
                pcmTotalSamples += count
            }
        }
        try {
            rec.start()
        } catch (e: Exception) {
            toastLike("Recorder start failed: ${e.message}")
            return
        }
        recorder = rec

        main.postDelayed({ if (state == State.COUNTDOWN) renderRecording(song) }, 250)

        val melodyEndMs = stagedZipPath?.let {
            runCatching { SingScoringSession.melodyEndMs(it) }.getOrDefault(-1L)
        } ?: -1L
        val tailMs = if (melodyEndMs > 0) melodyEndMs + 1500L else 60_000L
        autoStopRunnable = Runnable { if (state == State.RECORDING) finishAndScore() }
        main.postDelayed(autoStopRunnable!!, tailMs)
    }

    private fun finishAndScore() {
        val song = pendingSong ?: return
        val zip = stagedZipPath ?: return
        cancelAutoStop()
        recorder?.stop(); recorder = null

        renderScoring(song)

        val flat: FloatArray = synchronized(pcm) {
            val out = FloatArray(pcmTotalSamples)
            var off = 0
            for (chunk in pcm) {
                System.arraycopy(chunk, 0, out, off, chunk.size)
                off += chunk.size
            }
            out
        }

        var peak = 0f
        var sumsq = 0.0
        for (v in flat) {
            val a = if (v < 0f) -v else v
            if (a > peak) peak = a
            sumsq += v.toDouble() * v.toDouble()
        }
        val rms = if (flat.isNotEmpty()) kotlin.math.sqrt(sumsq / flat.size) else 0.0
        val durMs = if (sampleRate > 0) flat.size.toLong() * 1000L / sampleRate else 0L
        android.util.Log.i(
            "ss-demo",
            "pcm samples=${flat.size} rate=$sampleRate durMs=$durMs peak=$peak rms=${"%.4f".format(rms)}"
        )

        val gen = ++scoringGeneration
        thread(name = "ss-scoring", isDaemon = true) {
            val score = try {
                SingScoringSession.score(zip, flat, sampleRate)
            } catch (_: Exception) { 10 }
            main.post { if (gen == scoringGeneration) renderResult(song, score) }
        }
    }

    private fun cancelAutoStop() {
        autoStopRunnable?.let { main.removeCallbacks(it) }
        autoStopRunnable = null
    }

    private fun returnToPicker() {
        // Pending delayed work: countdown splash, preview auto-advance, auto-stop.
        main.removeCallbacksAndMessages(null)
        previewAutoAdvance = null
        autoStopRunnable = null

        mediaPlayer?.let { runCatching { it.stop() }; it.release() }
        mediaPlayer = null

        recorder?.let { runCatching { it.stop() } }
        recorder = null
        synchronized(pcm) {
            pcm.clear()
            pcmTotalSamples = 0
        }

        // Drop any in-flight background result that posts back after we leave.
        scoringGeneration++
        downloadGeneration++

        renderPicker()
    }

    // --- helpers -----------------------------------------------------------

    private fun readLyrics(zipPath: String, songId: String): List<LrcLine> {
        val target = "${songId}_chorus.lrc"
        return try {
            ZipInputStream(File(zipPath).inputStream()).use { zis ->
                while (true) {
                    val e = zis.nextEntry ?: return emptyList()
                    if (e.name.endsWith(target)) {
                        val text = zis.readBytes().toString(Charsets.UTF_8)
                        return LrcParser.parse(text)
                    }
                }
                @Suppress("UNREACHABLE_CODE") emptyList()
            }
        } catch (_: Exception) { emptyList() }
    }

    // --- view helpers ------------------------------------------------------

    private fun songButton(song: SongCatalog.Song): LinearLayout {
        // Two-line row: name on top, "singer • rhythm" beneath. Implemented as a
        // clickable vertical LinearLayout with two TextViews — Android's Button
        // doesn't lay two lines out cleanly with a size hierarchy.
        val row = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            isClickable = true
            isFocusable = true
            setPadding(32, 24, 32, 24)
            background = ContextCompat.getDrawable(context,
                android.R.drawable.list_selector_background)
            setOnClickListener { onSongPicked(song) }
            layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)
                .apply { topMargin = 16 }
        }
        row.addView(TextView(this).apply {
            text = song.name
            textSize = 18f
            setTextColor(Color.BLACK)
        })
        val sub = buildString {
            append(song.singer)
            if (song.rhythm.isNotEmpty()) {
                if (song.singer.isNotEmpty()) append("  •  ")
                append(song.rhythm)
            }
        }
        if (sub.isNotEmpty()) {
            row.addView(TextView(this).apply {
                text = sub
                textSize = 13f
                setTextColor(Color.DKGRAY)
                layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)
                    .apply { topMargin = 4 }
            })
        }
        return row
    }

    private fun titleView(text: String) = TextView(this).apply {
        this.text = text
        textSize = 26f
        layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)
    }

    private fun titleRowWithBack(text: String): LinearLayout {
        val row = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)
        }
        row.addView(Button(this).apply {
            this.text = "← Songs"
            setOnClickListener { returnToPicker() }
            layoutParams = LinearLayout.LayoutParams(WRAP_CONTENT, WRAP_CONTENT)
                .apply { marginEnd = 16 }
        })
        row.addView(TextView(this).apply {
            this.text = text
            textSize = 26f
            layoutParams = LinearLayout.LayoutParams(0, WRAP_CONTENT, 1f)
        })
        return row
    }

    private fun subtitleView(text: String) = TextView(this).apply {
        this.text = text
        textSize = 16f
        layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)
            .apply { topMargin = 8; bottomMargin = 16 }
    }

    private fun toastLike(msg: String) {
        android.util.Log.w("SingScoringDemo", msg)
        android.widget.Toast.makeText(this, msg, android.widget.Toast.LENGTH_LONG).show()
    }
}
```

- [ ] **Step 2: Build**

Run:
```bash
./gradlew :demo-android:assembleDebug
```

Expected: `BUILD SUCCESSFUL`. The unresolved-reference to `SongAssets` is gone (MainActivity no longer names it). `SongAssets.kt` still exists and still compiles as an orphan object — Task 5 removes it.

If you see `unresolved reference: SongAssets` somewhere, that means a line was missed during the rewrite — grep the file:
```bash
grep -n SongAssets demo-android/src/main/kotlin/com/sensen/singscoring/demo/MainActivity.kt
```
Expected: no matches.

- [ ] **Step 3: Commit**

```bash
git add demo-android/src/main/kotlin/com/sensen/singscoring/demo/MainActivity.kt
git commit -m "feat(demo): API-backed picker + DOWNLOADING state"
```

---

### Task 5: Remove the orphan `SongAssets.kt`

**Files:**
- Delete: `demo-android/src/main/kotlin/com/sensen/singscoring/demo/SongAssets.kt`

`SongAssets` has no callers after Task 4. Removing it in its own commit keeps the diff clean.

- [ ] **Step 1: Verify no remaining references**

Run:
```bash
grep -rn SongAssets demo-android/ || echo "no matches"
```

Expected: `no matches`. If anything shows up, stop and fix it before deleting the file.

- [ ] **Step 2: Delete the file**

```bash
git rm demo-android/src/main/kotlin/com/sensen/singscoring/demo/SongAssets.kt
```

- [ ] **Step 3: Build**

Run:
```bash
./gradlew :demo-android:assembleDebug
```

Expected: `BUILD SUCCESSFUL`.

- [ ] **Step 4: Commit**

```bash
git commit -m "chore(demo): drop orphan SongAssets (replaced by SongCatalog + SongStaging)"
```

---

### Task 6: Manual smoke test on-device

**Files:** none (verification only — no code changes, no commit)

- [ ] **Step 1: Install on a connected device**

With the device on USB and `adb` on PATH (`. scripts/env.sh` puts it there):

```bash
./gradlew :demo-android:installDebug
adb shell am start -n com.sensen.singscoring.demo/.MainActivity
```

- [ ] **Step 2: Verify picker → download → full flow**

Walk through the following on-device, watching `adb logcat -s SingScoringDemo:* ss-demo:*` in another terminal:

1. **Fresh launch online** — picker briefly shows "SDK X.Y.Z — loading songs…" then renders ~100 two-line buttons.
2. **Pick any song** — "← Songs" + song-name title + "Downloading song…" appears. After the download completes, PREVIEW starts playing the mp3. Confirm with:
   ```bash
   adb shell ls /data/data/com.sensen.singscoring.demo/cache/songs/
   ```
   and expect a directory named after the picked song's id, containing `<id>.zip` and `<id>_chorus.mp3`.
3. **Second pick of the same song** — DOWNLOADING passes through in well under a second (cache hit, no network).
4. **Back during DOWNLOADING** — pick a song, hit "← Songs" before download finishes. Returns to picker. Re-picking the same song works (no stuck `.part`). Verify:
   ```bash
   adb shell ls /data/data/com.sensen.singscoring.demo/cache/songs/<id>/
   ```
   After a clean back-cancel there should be neither `<id>.zip` nor `<id>.zip.part` (the `.part` is deleted on failure). If only `.part` is present, the follow-up `download()` will delete it and start fresh — that's still correct, just noisier on disk.
5. **Offline error** — enable airplane mode, kill + relaunch the app. Picker shows "Couldn't load songs: …" and a **Retry** button. Turn off airplane mode, tap Retry, list loads.
6. **Full flow end-to-end** — pick → download → preview → countdown → record → score → RESULT. Confirm the integer score lands in [10, 99] and the score path (zip → MIDI → YIN → aggregate) works exactly as before.

- [ ] **Step 3: Verify APK size dropped**

```bash
ls -lh demo-android/build/outputs/apk/debug/demo-android-debug.apk
```

Compare against the pre-change size from the previous branch (main). The `SongHighlightSamples/*.zip` total is several MB; the new APK should be that much smaller.

No commit — this is a verification task.

---

## Rollback plan

If any task past Task 1 breaks the app on-device in a way that can't be reproduced or fixed from logs:

```bash
git log --oneline main..HEAD
git revert <commit-sha-of-last-good-task>..HEAD
```

Tasks 1–3 are additive and inert (they add files/resources but change no runtime behavior), so reverting Task 4 alone is sufficient to restore the bundled-asset flow — **except** that Task 1 removed the asset `srcDirs` and dropped `SongHighlightSamples` from the APK. If Task 4 is reverted, also revert Task 1 to re-bundle the zips, otherwise the old `SongAssets.list()` finds nothing.
