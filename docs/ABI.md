# SingScoring C ABI

The entire public surface is the six functions in
[`core/include/singscoring.h`](../core/include/singscoring.h). Everything
above the C boundary — JNI, the Kotlin `SingScoringSession`, the Obj-C
`SSCSession`, its Swift alias — is a thin marshaller that must never touch
engine internals.

```c
ss_session* ss_open(const char* zip_path);
void        ss_feed_pcm(ss_session*, const float* samples, int n, int sample_rate);
int         ss_finalize_score(ss_session*);   // 10..99
void        ss_close(ss_session*);
const char* ss_version(void);

// One-shot — recommended path for app integrations.
int         ss_score(const char* zip_path,
                     const float* samples, int n, int sample_rate);  // 10..99
```

## One-shot scoring

`ss_score` opens the zip, scores the supplied PCM buffer, and releases
everything in one call. Sample 0 is treated as MIDI t=0 — the caller is
responsible for starting capture in sync with the chorus reference (e.g.,
when a lyrics scroll begins). Returns the same `[10, 99]` integer as
`ss_finalize_score`. This is the recommended entry point for app
integrations; the `open / feed_pcm / finalize_score / close` quartet is
kept for advanced flows that need a session handle (chunked upload,
pre-warming, parallel takes against one open session).

## Lifecycle

```
      open            feed_pcm*          finalize_score        close
 ──────────────▶ ──────────────────▶ ────────────────────▶ ──────────▶
 (zip on disk)   (audio thread OK)    (any thread, once)
```

- `ss_score` collapses the lifecycle above into a single call; the diagram and bullets that follow describe the streaming variant.
- `ss_open` returns `NULL` on failure. Treat as "could not parse zip".
- `ss_feed_pcm` accepts mono float32 in `[-1, 1]`. Silently drops calls with
  a `sample_rate` different from the first non-empty one — the engine does
  not resample internally.
- `ss_finalize_score` is idempotent after the first call; subsequent
  `ss_feed_pcm` calls are ignored. Always returns an integer in `[10, 99]`.
- `ss_close` accepts `NULL`.

## Thread-safety

- One session is not safe to use from multiple threads concurrently. Typical
  setup: UI thread calls `open/finalize/close`, the audio callback thread
  owns `feed_pcm`.
- Separate sessions are fully independent — you may score two songs at once
  in different sessions.

## Stability

- Pre-1.0 the ABI may change between minor versions. Check `ss_version()`
  against the matching `CHANGELOG.md` entry when integrating a new build.
- From 1.0.0 onward only a major-version bump may break the six functions.

## What's *not* exposed

- Per-note scores, pitch tracks, latency measurements, MIDI contents — all
  live in the core C++ layer and are only visible to the unit tests. If a
  platform wrapper needs these they'll be added through new C ABI functions,
  not by leaking internal types.
