# Demo 30s recording cap + countdown indicator

**Date:** 2026-04-23
**Scope:** `demo-android/` only. No SDK change, no binding change.

## Problem

The SDK now clips `ref_notes` to the actual PCM duration (the `C1` change landed at `e100377`), so a demo that records a full 60s chorus against a song that needs only 30s of material now scores correctly. But the reverse remains: the demo still records the full chorus regardless of length, so a 60s chorus demands 60s of sustained precision from the user — the length-unfairness complaint the spec at `docs/superpowers/specs/2026-04-23-phrase-alignment-and-pcm-clipping-design.md` explicitly deferred to the product side.

## Design

### Tunable cap in `MainActivity`

```kotlin
private const val kMaxSingDurationMs: Long = 30_000L
```

At the companion / private-const level of `MainActivity`. Single call site; no reason to hoist into a separate config file today. Tune in place as real-user data arrives.

### Recording-duration calculation

Replaces the existing `tailMs` block around `demo-android/.../MainActivity.kt:411-416`:

```kotlin
val melodyEndMs = stagedZipPath
    ?.let { runCatching { SingScoringSession.melodyEndMs(it) }.getOrDefault(-1L) }
    ?: -1L

val songTailMs = if (melodyEndMs > 0L) melodyEndMs + 1500L else kMaxSingDurationMs
val recordingDurationMs = minOf(songTailMs, kMaxSingDurationMs)

autoStopRunnable = Runnable { if (state == State.RECORDING) finishAndScore() }
main.postDelayed(autoStopRunnable!!, recordingDurationMs)
```

Behavior by case:

| Chorus `melodyEndMs` | `songTailMs` | `recordingDurationMs` | Result |
|---|---|---|---|
| 15000 ms | 16500 | 16500 | Record the full chorus + 1.5s tail (unchanged from today). |
| 28500 ms | 30000 | 30000 | Exactly at the cap. |
| 60000 ms | 61500 | 30000 | Capped at 30s. |
| unknown (≤0) | 30000 | 30000 | Fall back to the cap (was 60000 ms). |

The fallback change from 60000 → 30000 when `melody_end_ms` is unknown is not a regression: the SDK's C1 clip now handles the scoring horizon correctly regardless of how long the demo records, so there's no correctness reason to record for a full minute when metadata is missing.

### Countdown indicator

Inline in the title row as `"🎤  SongName    0:15 / 0:30"`. The existing `titleRowWithBack` helper produces a `LinearLayout` with the back button and a `TextView` for the title; we'll extend that row with a second `TextView` on the right showing the elapsed/total pair.

A `Choreographer.FrameCallback` drives the text. It reads `SystemClock.elapsedRealtime() - recordingStartMs` (the same clock `LyricsScrollView` already uses — single source of truth), formats as `m:ss / m:ss`, and re-posts itself while the view is attached and `state == State.RECORDING`. When the view detaches or the state leaves `RECORDING`, the callback is removed. No separate timer, no thread hop.

The elapsed value is clamped to `recordingDurationMs` in the formatter so the display never overshoots "0:30 / 0:30" even if `finishAndScore` runs a frame late. `finishAndScore` itself is triggered by the same `recordingDurationMs` postDelayed runnable that's there today.

### Lyrics

No change. `LyricsScrollView` continues to receive the full parsed LRC; if the chorus exceeds 30 s, the user simply doesn't reach the later lines before auto-stop fires. We deliberately do **not** filter the LRC at the 30 s boundary — doing so would create a second source of truth about the scoring cap that must stay in sync with the SDK's PCM-duration clip. Leaving the display verbatim avoids that drift.

### State machine

No change to the existing `PICKER → STAGING → COUNTDOWN → RECORDING → SCORING → RESULT` progression. The countdown TextView and its Choreographer callback are owned by the view tree created in `renderRecording`; standard Android lifecycle cleans them up on view detach.

## Non-goals

- No change to the per-song `_chorus.json` metadata, MIDI parsing, or LRC parsing.
- No change to `SingScoringSession.melodyEndMs(...)` — still represents the full MIDI end; the demo is what decides how much of it to capture.
- No visible indicator of *which* frames were actually sent to the SDK. The SDK's `SS_LOGI` already reports `notes=N (clipped from M, endMs=...)` for debugging.
- No settings UI for `kMaxSingDurationMs`. It's a source-level tunable; changes are developer-initiated.

## Files touched

- `demo-android/src/main/kotlin/com/sensen/singscoring/demo/MainActivity.kt`
  - Add `kMaxSingDurationMs` const.
  - Replace the `tailMs` block in `beginRecording` with the `recordingDurationMs` calc above.
  - Add a nullable-default parameter `countdownTotalMs: Long? = null` to `titleRowWithBack`. When non-null, the helper adds a second TextView on the right side and registers the Choreographer callback against it. Non-recording callers pass nothing; only `renderRecording` passes `recordingDurationMs`.
  - Add a `Choreographer.FrameCallback` member on the activity that formats and re-posts the countdown text; register on attach of the recording view, cancel on the transition out of `State.RECORDING`.
- `CHANGELOG.md`
  - One Unreleased bullet under the Demo section describing the cap and the countdown.

No tests change. The demo has no unit tests to regress; the core SDK tests are independent.

## Risks

- **Very short choruses (<3 s):** `recordingDurationMs` would be `<4.5 s`, giving the user almost no recording time. In practice the bundled fixtures have chorus lengths 10+ s, so this is a theoretical concern. If a real-world song triggers it, the fix is a floor on `recordingDurationMs` (e.g., `max(songTailMs, 5000L)`) — not adding today.
- **Mid-word auto-stop for 30–31 s choruses:** a chorus whose `melodyEndMs` is, say, 29000 ms yields `songTailMs=30500`, clamped to 30000. The last 500 ms of intended tail is lost. Acceptable because scoring-wise the last MIDI note ends at 29000 ms and the tail was only for YIN window alignment; losing 500 ms of silence is inconsequential.

## Version / changelog

Land as an Unreleased entry in `CHANGELOG.md`. Demo-only change; the published AAR is unaffected. No version bump.
