package com.sensen.singscoring.demo

import android.Manifest
import android.annotation.SuppressLint
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder
import androidx.annotation.RequiresPermission
import kotlin.concurrent.thread

/**
 * Minimal float-PCM mic capture. Runs its read loop on its own thread and hands
 * chunks straight to the caller's callback — which is expected to be cheap
 * (e.g. forward to SingScoringSession.feedPcm).
 */
class AudioRecorder(
    private val sampleRate: Int = 44100,
    private val onPcm: (FloatArray, Int) -> Unit
) {

    @Volatile private var running = false
    private var recorder: AudioRecord? = null
    private var workerThread: Thread? = null

    @SuppressLint("MissingPermission")
    @RequiresPermission(Manifest.permission.RECORD_AUDIO)
    fun start() {
        if (running) return

        val channel = AudioFormat.CHANNEL_IN_MONO
        val encoding = AudioFormat.ENCODING_PCM_FLOAT
        val minBytes = AudioRecord.getMinBufferSize(sampleRate, channel, encoding)
        require(minBytes > 0) { "AudioRecord rejected $sampleRate Hz float mono on this device" }
        val bufBytes = minBytes * 2

        val rec = AudioRecord(
            MediaRecorder.AudioSource.VOICE_RECOGNITION,
            sampleRate, channel, encoding, bufBytes
        )
        check(rec.state == AudioRecord.STATE_INITIALIZED) { "AudioRecord failed to initialize" }
        recorder = rec
        running = true
        rec.startRecording()

        workerThread = thread(name = "ss-audio-capture", isDaemon = true) {
            val chunk = FloatArray(bufBytes / 4 / 2)  // half buffer per read — balance latency vs syscalls
            while (running) {
                val n = rec.read(chunk, 0, chunk.size, AudioRecord.READ_BLOCKING)
                if (n > 0) onPcm(chunk, n)
                else if (n < 0) break
            }
        }
    }

    fun stop() {
        if (!running) return
        running = false
        workerThread?.join(500)
        workerThread = null
        recorder?.apply {
            runCatching { stop() }
            release()
        }
        recorder = null
    }
}
