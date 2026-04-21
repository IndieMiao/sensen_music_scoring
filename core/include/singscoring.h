#ifndef SINGSCORING_H
#define SINGSCORING_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ss_session ss_session;

/**
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

#ifdef __cplusplus
}
#endif

#endif /* SINGSCORING_H */
