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
