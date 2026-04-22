package com.sensen.singscoring

/**
 * Kotlin wrapper around the native scoring session.
 *
 * Typical flow:
 *   val session = SingScoringSession.open(zipPath)
 *   // in your audio callback: session.feedPcm(floats, 44100)
 *   val score = session.finalizeScore()  // 10..99
 *   session.close()
 */
class SingScoringSession private constructor(private var handle: Long) : AutoCloseable {

    /**
     * Feed mic samples. [count] defaults to the full array; pass the actual
     * number of valid samples when reading into a reusable buffer.
     */
    fun feedPcm(samples: FloatArray, sampleRate: Int, count: Int = samples.size) {
        check(handle != 0L) { "session already closed" }
        require(count in 0..samples.size) { "count=$count out of range [0, ${samples.size}]" }
        if (count == 0) return
        nativeFeedPcm(handle, samples, count, sampleRate)
    }

    /** Returns a score in [10, 99]. 60 is the pass threshold. */
    fun finalizeScore(): Int {
        check(handle != 0L) { "session already closed" }
        return nativeFinalize(handle)
    }

    override fun close() {
        if (handle != 0L) {
            nativeClose(handle)
            handle = 0L
        }
    }

    companion object {
        init {
            System.loadLibrary("singscoring")
        }

        /** @throws IllegalArgumentException if the zip cannot be opened. */
        fun open(zipPath: String): SingScoringSession {
            val h = nativeOpen(zipPath)
            require(h != 0L) { "failed to open zip: $zipPath" }
            return SingScoringSession(h)
        }

        /** SDK version, e.g. "0.1.0". */
        val version: String get() = nativeVersion()

        /**
         * Last reference-melody note end-time in milliseconds from MIDI t=0 —
         * i.e., the scoring horizon. Callers that auto-stop capture should use
         * this, not the LRC last-line time or `json.duration`. Returns -1 if
         * the zip is unreadable or contains no parseable MIDI notes.
         */
        @JvmStatic
        fun melodyEndMs(zipPath: String): Long = nativeMelodyEndMs(zipPath)

        /**
         * One-shot scoring. Open the song zip, score [samples] (mono float32 at
         * [sampleRate] Hz) against the chorus MIDI, and release the session in a
         * single call. Returns a score in [10, 99]. The first sample is treated
         * as MIDI t=0 — caller starts capture in sync with the lyrics scroll.
         */
        @JvmStatic
        fun score(
            zipPath: String,
            samples: FloatArray,
            sampleRate: Int,
            count: Int = samples.size
        ): Int {
            require(count in 0..samples.size) { "count=$count out of range [0, ${samples.size}]" }
            if (count == 0) return 10
            return nativeScore(zipPath, samples, count, sampleRate)
        }

        @JvmStatic private external fun nativeOpen(zipPath: String): Long
        @JvmStatic private external fun nativeFeedPcm(handle: Long, samples: FloatArray, count: Int, sampleRate: Int)
        @JvmStatic private external fun nativeFinalize(handle: Long): Int
        @JvmStatic private external fun nativeClose(handle: Long)
        @JvmStatic private external fun nativeVersion(): String
        @JvmStatic private external fun nativeScore(
            zipPath: String, samples: FloatArray, count: Int, sampleRate: Int
        ): Int
        @JvmStatic private external fun nativeMelodyEndMs(zipPath: String): Long
    }
}
