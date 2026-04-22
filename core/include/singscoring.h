#ifndef SINGSCORING_H
#define SINGSCORING_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ss_session ss_session;

/**
 * Advanced — for chunked / pre-warmed flows. Most callers should use ss_score
 * (below) for the standard one-shot path.
 *
 * Open a scoring session from a song zip on disk.
 * The zip must contain [songCode]_chorus.{mp3,mid,lrc,json}.
 * Returns NULL on failure.
 */
ss_session* ss_open(const char* zip_path);

/**
 * Feed raw PCM microphone samples. Mono float32, interleaved is irrelevant
 * because this is mono. sample_rate is in Hz (e.g., 44100 or 16000).
 * Can be called repeatedly as audio arrives.
 */
void ss_feed_pcm(ss_session* s, const float* samples, int n_samples, int sample_rate);

/**
 * Finalize and return a score in [10, 99]. 60 is the pass threshold.
 * Safe to call once per session. After this the session is frozen.
 */
int ss_finalize_score(ss_session* s);

/**
 * Release all resources. Safe to pass NULL.
 */
void ss_close(ss_session* s);

/**
 * SDK version string, e.g. "0.1.0". Lifetime is program-static.
 */
const char* ss_version(void);

/**
 * Last reference-melody note end-time, in milliseconds from MIDI t=0.
 * This is the scoring horizon (per CLAUDE.md): callers that auto-stop
 * capture should use this value, not the LRC's last line nor the JSON
 * `duration`.
 *
 * Returns -1 on failure (unreadable zip, no parseable MIDI, no notes).
 */
long long ss_melody_end_ms(const char* zip_path);

/**
 * One-shot scoring: open the song zip, score the supplied PCM buffer
 * against the chorus MIDI, and release everything. Equivalent to
 * ss_open + ss_feed_pcm + ss_finalize_score + ss_close, but doesn't
 * require the caller to manage a session handle.
 *
 * PCM contract: mono float32, sample_rate Hz. Sample 0 is treated as
 * MIDI t=0 — caller is responsible for starting capture in sync with
 * the chorus reference (e.g., when the lyrics scroll begins).
 *
 * Returns a score in [10, 99]. Returns 10 on any failure
 * (unreadable zip, empty PCM, parse failure, etc.).
 */
int ss_score(const char* zip_path,
             const float* samples, int n_samples, int sample_rate);

#ifdef __cplusplus
}
#endif

#endif /* SINGSCORING_H */
