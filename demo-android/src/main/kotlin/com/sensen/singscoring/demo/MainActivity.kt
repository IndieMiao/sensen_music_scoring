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

    private enum class State { PICKER, COUNTDOWN, RECORDING, SCORING, RESULT }

    private val sampleRate = 44100
    private var state = State.PICKER
    private var recorder: AudioRecorder? = null
    private var pendingSong: SongAssets.Song? = null
    private var stagedZipPath: String? = null
    private var lyrics: List<LrcLine> = emptyList()

    // Recording buffer (owned by the activity; the recorder just appends).
    private val pcm = ArrayList<FloatArray>()
    private var pcmTotalSamples = 0
    private var recordingStartMs = 0L  // SystemClock.elapsedRealtime when "Sing!" hits

    private val main = Handler(Looper.getMainLooper())
    private var autoStopRunnable: Runnable? = null

    private val root by lazy { FrameLayout(this) }

    private val requestMicPermission = registerForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { granted ->
        if (granted) pendingSong?.let { startCountdown(it) }
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
        col.addView(subtitleView("SDK ${SingScoringSession.version} — pick a song"))

        val scroll = ScrollView(this)
        val list = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        SongAssets.list(this).forEach { song ->
            list.addView(Button(this).apply {
                text = "${song.displayName}  (${song.code})"
                setOnClickListener { onSongPicked(song) }
                layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)
                    .apply { topMargin = 16 }
            })
        }
        scroll.addView(list)
        col.addView(scroll, LinearLayout.LayoutParams(MATCH_PARENT, 0, 1f))
        root.addView(col)
    }

    private fun renderCountdown(song: SongAssets.Song, secondsLeft: Int) {
        state = State.COUNTDOWN
        root.removeAllViews()
        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER_HORIZONTAL
            layoutParams = FrameLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT)
        }
        col.addView(titleView("🎤  ${song.displayName}"))
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

    private fun renderRecording(song: SongAssets.Song) {
        state = State.RECORDING
        root.removeAllViews()
        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            layoutParams = FrameLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT)
        }
        col.addView(titleView("🎤  ${song.displayName}"))

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

    private fun renderScoring(song: SongAssets.Song) {
        state = State.SCORING
        root.removeAllViews()
        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER_HORIZONTAL
            layoutParams = FrameLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT)
        }
        col.addView(titleView(song.displayName))
        col.addView(subtitleView("Scoring…"))
        root.addView(col)
    }

    private fun renderResult(song: SongAssets.Song, score: Int) {
        state = State.RESULT
        root.removeAllViews()
        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER_HORIZONTAL
            layoutParams = FrameLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT)
        }
        col.addView(titleView(song.displayName))

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

    // --- flow --------------------------------------------------------------

    private fun onSongPicked(song: SongAssets.Song) {
        pendingSong = song
        val granted = ContextCompat.checkSelfPermission(
            this, Manifest.permission.RECORD_AUDIO
        ) == PackageManager.PERMISSION_GRANTED
        if (granted) startCountdown(song) else requestMicPermission.launch(Manifest.permission.RECORD_AUDIO)
    }

    private fun startCountdown(song: SongAssets.Song) {
        // Stage the zip and parse LRC up-front so the recording phase has nothing to wait on.
        val staged = try {
            SongAssets.stage(this, song)
        } catch (e: Exception) {
            toastLike("Failed to stage song: ${e.message}")
            return
        }
        stagedZipPath = staged.zipPath
        lyrics = readLyrics(staged.zipPath, song.code)

        // 3 → 2 → 1 → "Sing!" → start recording.
        renderCountdown(song, 3)
        main.postDelayed({ renderCountdown(song, 2) }, 1000)
        main.postDelayed({ renderCountdown(song, 1) }, 2000)
        main.postDelayed({
            renderCountdown(song, 0)            // "Sing!"
            beginRecording(song)                // also picks up the t=0 instant
        }, 3000)
    }

    private fun beginRecording(song: SongAssets.Song) {
        pcm.clear()
        pcmTotalSamples = 0
        recordingStartMs = SystemClock.elapsedRealtime()

        val rec = AudioRecorder(sampleRate) { samples, count ->
            // Copy into our own array so the recorder can reuse its buffer.
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

        // Move from "Sing!" splash to the actual scrolling-lyrics screen after a brief beat
        // so the user sees the splash render. 250 ms is enough for the eye.
        main.postDelayed({ if (state == State.COUNTDOWN) renderRecording(song) }, 250)

        // Auto-stop a short tail past the last MIDI note — that's the scoring
        // horizon. LRC can end well before the melody (or after), so using it
        // here would truncate capture and drag the score to the [10,99] floor
        // via uncovered notes. Fall back to a loose 60 s cap if the SDK can't
        // tell us (unparseable MIDI → the session would return 10 anyway).
        val melodyEndMs = stagedZipPath?.let { runCatching { SingScoringSession.melodyEndMs(it) }.getOrDefault(-1L) } ?: -1L
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

        thread(name = "ss-scoring", isDaemon = true) {
            val score = try {
                SingScoringSession.score(zip, flat, sampleRate)
            } catch (_: Exception) { 10 }
            main.post { renderResult(song, score) }
        }
    }

    private fun cancelAutoStop() {
        autoStopRunnable?.let { main.removeCallbacks(it) }
        autoStopRunnable = null
    }

    // --- helpers -----------------------------------------------------------

    private fun readLyrics(zipPath: String, songCode: String): List<LrcLine> {
        val target = "${songCode}_chorus.lrc"
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
        layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)
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
