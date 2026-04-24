package com.sensen.singscoring.demo

import android.Manifest
import android.content.pm.PackageManager
import android.graphics.Color
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.view.Choreographer
import android.view.Gravity
import android.view.View
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.widget.Button
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.sensen.singscoring.SingScoringSession
import com.sensen.singscoring.demo.ui.Palette
import com.sensen.singscoring.demo.ui.ScoreRingView
import com.sensen.singscoring.demo.ui.SongListAdapter
import com.sensen.singscoring.demo.ui.dp
import java.io.File
import java.util.zip.ZipInputStream
import kotlin.concurrent.thread
import kotlin.math.roundToInt

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

    // Result-screen toggle: true = show the UI-level remapped score, false = show raw.
    // Resets to true on every new result / return to picker.
    private var showRemapped: Boolean = true
    private var lastRawScore: Int = -1

    // Preserve picker list position across navigation away and back.
    private var pickerRecyclerView: RecyclerView? = null
    private var pickerListState: android.os.Parcelable? = null
    private var searchDebounce: Runnable? = null
    // Last song the user picked — highlighted on return to the list.
    private var lastPickedSongId: String? = null

    // Max recording duration — tune here. Applied as min(chorus + 1500ms tail, this).
    private val kMaxSingDurationMs: Long = 30_000L
    private var recordingDurationMs: Long = kMaxSingDurationMs

    private val root by lazy { FrameLayout(this) }

    private val requestMicPermission = registerForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { granted ->
        if (granted) pendingSong?.let { beginDownload(it) }
        else toastLike("Microphone permission denied")
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.statusBarColor = Palette.BG
        WindowInsetsControllerCompat(window, window.decorView)
            .isAppearanceLightStatusBars = false
        root.setBackgroundColor(Palette.BG)
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
        pickerRecyclerView = null
        root.removeAllViews()
        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            layoutParams = FrameLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT)
        }
        col.addView(titleView("SingScoring"))
        col.addView(subtitleView("Loading songs…"))
        root.addView(col)

        val gen = ++catalogGeneration
        SongCatalog.fetchAll { result ->
            if (gen != catalogGeneration || state != State.PICKER) return@fetchAll
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
        col.addView(titleView("SingScoring"))
        col.addView(subtitleView("Pick a song"))

        // --- search bar --------------------------------------------------
        val searchContainerBg = android.graphics.drawable.GradientDrawable().apply {
            setColor(Palette.SURFACE_ELEVATED)
            cornerRadius = 24.dp(this@MainActivity).toFloat()
        }
        val searchRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            background = searchContainerBg
            setPadding(20.dp(this@MainActivity), 8.dp(this@MainActivity),
                       20.dp(this@MainActivity), 8.dp(this@MainActivity))
            layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)
                .apply { topMargin = 8; bottomMargin = 16 }
        }
        searchRow.addView(TextView(this).apply {
            text = "🔍"
            textSize = 16f
            setTextColor(Palette.TEXT_SECONDARY)
            layoutParams = LinearLayout.LayoutParams(WRAP_CONTENT, WRAP_CONTENT)
                .apply { marginEnd = 12.dp(this@MainActivity) }
        })
        val searchField = android.widget.EditText(this).apply {
            hint = "Search songs or artists"
            setHintTextColor(Palette.TEXT_TERTIARY)
            setTextColor(Palette.TEXT_PRIMARY)
            textSize = 16f
            setSingleLine(true)
            setBackgroundColor(android.graphics.Color.TRANSPARENT)
            setPadding(0, 0, 0, 0)
            layoutParams = LinearLayout.LayoutParams(0, WRAP_CONTENT, 1f)
        }
        searchRow.addView(searchField)
        col.addView(searchRow)

        // --- list + empty state -----------------------------------------
        val listContainer = FrameLayout(this).apply {
            layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, 0, 1f)
        }
        val recycler = RecyclerView(this).apply {
            layoutManager = LinearLayoutManager(this@MainActivity)
            setHasFixedSize(true)
            layoutParams = FrameLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT)
        }
        val emptyLabel = TextView(this).apply {
            textSize = 13f
            setTextColor(Palette.TEXT_SECONDARY)
            gravity = Gravity.CENTER
            visibility = View.GONE
            layoutParams = FrameLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)
                .apply { gravity = Gravity.CENTER }
        }

        val adapter = SongListAdapter(
            onClick = { song ->
                pickerListState = recycler.layoutManager?.onSaveInstanceState()
                onSongPicked(song)
            },
            onFilterApplied = { visibleCount ->
                val q = searchField.text?.toString()?.trim().orEmpty()
                if (visibleCount == 0 && q.isNotEmpty()) {
                    emptyLabel.text = "No songs match \"$q\""
                    emptyLabel.visibility = View.VISIBLE
                    recycler.visibility = View.INVISIBLE
                } else {
                    emptyLabel.visibility = View.GONE
                    recycler.visibility = View.VISIBLE
                }
            },
        )
        adapter.setSongs(songs)
        adapter.setHighlightedId(lastPickedSongId)
        recycler.adapter = adapter
        pickerListState?.let { recycler.layoutManager?.onRestoreInstanceState(it) }
        pickerRecyclerView = recycler

        listContainer.addView(recycler)
        listContainer.addView(emptyLabel)
        col.addView(listContainer)

        // --- search wiring (debounced 150 ms) ---------------------------
        searchField.addTextChangedListener(object : android.text.TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
            override fun afterTextChanged(s: android.text.Editable?) {
                searchDebounce?.let { main.removeCallbacks(it) }
                val q = s?.toString().orEmpty()
                val r = Runnable {
                    // Adapter may have been replaced if we navigated away then back.
                    if (pickerRecyclerView === recycler) adapter.setQuery(q)
                }
                searchDebounce = r
                main.postDelayed(r, 150L)
            }
        })

        // --- version footer ---------------------------------------------
        col.addView(TextView(this).apply {
            text = "Demo ${BuildConfig.VERSION_NAME} · SDK ${SingScoringSession.version}"
            textSize = 11f
            setTextColor(Palette.TEXT_TERTIARY)
            gravity = Gravity.CENTER
            layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)
                .apply { topMargin = 12 }
        })

        root.addView(col)
    }

    private fun renderPickerError(message: String) {
        state = State.PICKER
        root.removeAllViews()
        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            layoutParams = FrameLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT)
        }
        col.addView(titleView("SingScoring"))
        col.addView(subtitleView("Couldn't load songs: $message"))

        val pillBg = android.graphics.drawable.GradientDrawable().apply {
            setColor(Palette.ACCENT)
            cornerRadius = 24.dp(this@MainActivity).toFloat()
        }
        col.addView(Button(this).apply {
            text = "Retry"
            setTextColor(android.graphics.Color.BLACK)
            background = pillBg
            stateListAnimator = null
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
        col.addView(titleRowWithBack("🎤  ${song.name}", countdownTotalMs = recordingDurationMs))

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
        lastRawScore = score
        showRemapped = true   // Each new result starts on the remapped view.
        drawResultBody(song)
    }

    private fun drawResultBody(song: SongCatalog.Song) {
        root.removeAllViews()
        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER_HORIZONTAL
            layoutParams = FrameLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT)
        }
        col.addView(titleView(song.name))

        val raw = lastRawScore
        val displayed = if (showRemapped) remapScore(raw) else raw
        // Pass/fail follows the displayed number: 60 is the pass line on both
        // scales. Note: raw=59 remaps to 60, so toggling a 59 flips red↔green
        // — acceptable per the spec.
        val passed = displayed >= 60

        col.addView(TextView(this).apply {
            text = displayed.toString()
            textSize = 96f
            setTextColor(if (passed) Color.parseColor("#2E7D32") else Color.parseColor("#C62828"))
            gravity = Gravity.CENTER
            layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)
                .apply { topMargin = 64; bottomMargin = 16 }
        })
        col.addView(subtitleView(if (passed) "Passed" else "Needs work (pass ≥ 60)"))

        col.addView(Button(this).apply {
            text = if (showRemapped) "Show raw score" else "Show new score"
            layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)
                .apply { topMargin = 32 }
            setOnClickListener {
                showRemapped = !showRemapped
                drawResultBody(song)
            }
        })

        col.addView(Button(this).apply {
            text = "Pick another song"
            layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)
                .apply { topMargin = 32 }
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
        lastPickedSongId = song.id
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
        val songTailMs = if (melodyEndMs > 0L) melodyEndMs + 1500L else kMaxSingDurationMs
        recordingDurationMs = minOf(songTailMs, kMaxSingDurationMs)
        autoStopRunnable = Runnable { if (state == State.RECORDING) finishAndScore() }
        main.postDelayed(autoStopRunnable!!, recordingDurationMs)
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
        catalogGeneration++

        lastRawScore = -1
        showRemapped = true

        renderPicker()
    }

    // --- helpers -----------------------------------------------------------

    /**
     * UI-level score remap. Pure function — no state, no side effects.
     * Maps raw engine score (s ∈ [10, 99]) to a display score per the
     * 2026-04-23 spec:
     *   s < 15       → 1
     *   15 ≤ s ≤ 59  → [1, 60]
     *   60 ≤ s ≤ 70  → [60, 95]
     *   71 ≤ s ≤ 99  → [96, 100]   (coerceAtMost(100) guards s > 99)
     */
    private fun remapScore(raw: Int): Int = when {
        raw < 15 -> 1
        raw <= 59 -> 1 + ((raw - 15) * 59.0 / 44.0).roundToInt()
        raw <= 70 -> 60 + ((raw - 60) * 35.0 / 10.0).roundToInt()
        else -> (96 + ((raw - 71) * 4.0 / 29.0).roundToInt()).coerceAtMost(100)
    }

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

    private fun titleView(text: String) = TextView(this).apply {
        this.text = text
        textSize = 26f
        setTextColor(Palette.TEXT_PRIMARY)
        layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)
    }

    private fun titleRowWithBack(text: String, countdownTotalMs: Long? = null): LinearLayout {
        val row = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)
        }
        row.addView(TextView(this).apply {
            this.text = "← Songs"
            textSize = 16f
            setTextColor(Palette.TEXT_SECONDARY)
            isClickable = true
            isFocusable = true
            setPadding(0, 16, 24, 16)
            setOnClickListener { returnToPicker() }
            layoutParams = LinearLayout.LayoutParams(WRAP_CONTENT, WRAP_CONTENT)
                .apply { marginEnd = 16 }
        })
        row.addView(TextView(this).apply {
            this.text = text
            textSize = 22f
            setTextColor(Palette.TEXT_PRIMARY)
            layoutParams = LinearLayout.LayoutParams(0, WRAP_CONTENT, 1f)
        })
        if (countdownTotalMs != null) {
            row.addView(countdownTextView(countdownTotalMs))
        }
        return row
    }

    // Self-driven "elapsed / total" readout clocked off recordingStartMs.
    // The Choreographer callback re-posts itself while the view is attached and
    // auto-unregisters in onDetachedFromWindow — view lifecycle drives cleanup.
    private fun countdownTextView(totalMs: Long): TextView {
        val tv = object : TextView(this) {
            private val cb = object : Choreographer.FrameCallback {
                override fun doFrame(frameTimeNanos: Long) {
                    val elapsed = (SystemClock.elapsedRealtime() - recordingStartMs)
                        .coerceIn(0L, totalMs)
                    text = "${fmtMmSs(elapsed)} / ${fmtMmSs(totalMs)}"
                    if (isAttachedToWindow) {
                        Choreographer.getInstance().postFrameCallback(this)
                    }
                }
            }

            override fun onAttachedToWindow() {
                super.onAttachedToWindow()
                Choreographer.getInstance().postFrameCallback(cb)
            }

            override fun onDetachedFromWindow() {
                super.onDetachedFromWindow()
                Choreographer.getInstance().removeFrameCallback(cb)
            }
        }
        tv.textSize = 18f
        tv.setTextColor(Palette.TEXT_SECONDARY)
        tv.layoutParams = LinearLayout.LayoutParams(WRAP_CONTENT, WRAP_CONTENT)
            .apply { marginStart = 16 }
        // Seed the initial value so the view isn't blank between attach and first frame.
        tv.text = "${fmtMmSs(0L)} / ${fmtMmSs(totalMs)}"
        return tv
    }

    private fun subtitleView(text: String) = TextView(this).apply {
        this.text = text
        textSize = 13f
        setTextColor(Palette.TEXT_SECONDARY)
        layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)
            .apply { topMargin = 8; bottomMargin = 16 }
    }

    private fun toastLike(msg: String) {
        android.util.Log.w("SingScoringDemo", msg)
        android.widget.Toast.makeText(this, msg, android.widget.Toast.LENGTH_LONG).show()
    }

    private fun fmtMmSs(ms: Long): String {
        val totalSeconds = (ms / 1000L).coerceAtLeast(0L)
        val m = totalSeconds / 60L
        val s = totalSeconds % 60L
        return "%d:%02d".format(m, s)
    }
}
