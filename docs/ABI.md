# SingScoring C ABI

The entire public surface is the five functions in
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
```

## Lifecycle

```
      open            feed_pcm*          finalize_score        close
 ──────────────▶ ──────────────────▶ ────────────────────▶ ──────────▶
 (zip on disk)   (audio thread OK)    (any thread, once)
```

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
- From 1.0.0 onward only a major-version bump may break the five functions.

## What's *not* exposed

- Per-note scores, pitch tracks, latency measurements, MIDI contents — all
  live in the core C++ layer and are only visible to the unit tests. If a
  platform wrapper needs these they'll be added through new C ABI functions,
  not by leaking internal types.
