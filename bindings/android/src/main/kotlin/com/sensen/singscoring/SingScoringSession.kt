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

        @JvmStatic private external fun nativeOpen(zipPath: String): Long
        @JvmStatic private external fun nativeFeedPcm(handle: Long, samples: FloatArray, count: Int, sampleRate: Int)
        @JvmStatic private external fun nativeFinalize(handle: Long): Int
        @JvmStatic private external fun nativeClose(handle: Long)
        @JvmStatic private external fun nativeVersion(): String
    }
}
