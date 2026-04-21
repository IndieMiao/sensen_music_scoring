package com.sensen.singscoring.demo

import android.Manifest
import android.content.pm.PackageManager
import android.graphics.Color
import android.net.Uri
import android.os.Bundle
import android.view.Gravity
import android.view.View
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
import androidx.media3.common.MediaItem
import androidx.media3.exoplayer.ExoPlayer
import com.sensen.singscoring.SingScoringSession

class MainActivity : AppCompatActivity() {

    private enum class State { PICKER, RUNNING, RESULT }

    private val sampleRate = 44100
    private var state = State.PICKER
    private var session: SingScoringSession? = null
    private var recorder: AudioRecorder? = null
    private var player: ExoPlayer? = null
    private var pendingSong: SongAssets.Song? = null

    private val root by lazy { FrameLayout(this) }

    private val requestMicPermission = registerForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { granted ->
        if (granted) pendingSong?.let { startRun(it) }
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
        stopEverything()
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

    private fun renderRunning(song: SongAssets.Song) {
        state = State.RUNNING
        root.removeAllViews()
        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            layoutParams = FrameLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT)
        }
        col.addView(titleView("🎤  ${song.displayName}"))
        col.addView(subtitleView("Playback + capture running.\nSing along to the reference track."))
        col.addView(Button(this).apply {
            text = "Stop & score"
            layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)
                .apply { topMargin = 48 }
            setOnClickListener { finishAndScore() }
        })
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
        if (granted) startRun(song) else requestMicPermission.launch(Manifest.permission.RECORD_AUDIO)
    }

    private fun startRun(song: SongAssets.Song) {
        val staged = try {
            SongAssets.stage(this, song)
        } catch (e: Exception) {
            toastLike("Failed to stage song: ${e.message}")
            return
        }
        val newSession = try {
            SingScoringSession.open(staged.zipPath)
        } catch (e: Exception) {
            toastLike("Failed to open song: ${e.message}")
            return
        }
        session = newSession

        val rec = AudioRecorder(sampleRate) { samples, count ->
            try { newSession.feedPcm(samples, sampleRate, count) } catch (_: Exception) {}
        }
        try {
            rec.start()
        } catch (e: Exception) {
            toastLike("Recorder start failed: ${e.message}")
            newSession.close()
            session = null
            return
        }
        recorder = rec

        val p = ExoPlayer.Builder(this).build().apply {
            setMediaItem(MediaItem.fromUri(Uri.fromFile(java.io.File(staged.mp3Path))))
            prepare()
            playWhenReady = true
        }
        player = p

        renderRunning(song)
    }

    private fun finishAndScore() {
        val song = pendingSong ?: return
        recorder?.stop(); recorder = null
        player?.apply { stop(); release() }; player = null

        val score = session?.let {
            try { it.finalizeScore() } catch (_: Exception) { 10 }
        } ?: 10
        session?.close(); session = null

        renderResult(song, score)
    }

    private fun stopEverything() {
        recorder?.stop(); recorder = null
        player?.release(); player = null
        session?.close(); session = null
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
        // Kept dependency-light — no Toast to avoid extra resource churn on failure paths.
        android.util.Log.w("SingScoringDemo", msg)
        TextView(this).apply {
            text = msg
            setTextColor(Color.parseColor("#C62828"))
        }.also { root.addView(it) }
    }
}
