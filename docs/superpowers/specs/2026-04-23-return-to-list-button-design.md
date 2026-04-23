# Return-to-list button on demo detail screens

## Goal

Let a demo user bail out of a song at any point between song-pick and score-display and land back on the picker. Today the only way back to the picker is to sit through the full flow until the RESULT screen's "Pick another song" button.

## Scope

Add a **"← Songs"** button on four of the six states in `demo-android/src/main/kotlin/com/sensen/singscoring/demo/MainActivity.kt`:

- `PREVIEW`
- `COUNTDOWN`
- `RECORDING`
- `SCORING`

No change to `PICKER` (already the destination) or `RESULT` (already has "Pick another song").

## UI

- Label: **"← Songs"** (short, text-style button; not a full-width primary action).
- Position: **top-left**, on the same horizontal row as the title.
- Implementation: wrap the existing title `TextView` and the new button in a horizontal `LinearLayout`, button first, title second with `layout_weight=1`. Add via a small helper so the four call sites stay one line each.

## Behavior

Press = silent return to picker. No confirmation dialog, even on RECORDING (discarding an in-progress capture is intentional — the button is clearly outside the primary-action path, and the primary "Stop & score" remains distinct).

A single `returnToPicker()` helper does all teardown then calls `renderPicker()`. Per-source cleanup:

| From      | Cleanup                                                                     |
|-----------|-----------------------------------------------------------------------------|
| PREVIEW   | cancel `previewAutoAdvance`; stop + release `mediaPlayer`                   |
| COUNTDOWN | `main.removeCallbacksAndMessages(null)` to kill pending 3 → 2 → 1 → Sing   |
| RECORDING | `cancelAutoStop()`; `recorder?.stop()`; clear `pcm`; reset `pcmTotalSamples` |
| SCORING   | bump `scoringGeneration`; the background thread's `main.post` is dropped    |

`returnToPicker()` calls all of the above unconditionally — each teardown is a no-op if the resource is already null, so it's safe to run from any state.

### Scoring-generation guard

`SingScoringSession.score(...)` runs on a daemon thread we can't interrupt. Today the thread posts `renderResult(song, score)` back to the main looper; if the user has already returned to the picker, that post would stomp the UI.

Add:

```kotlin
private var scoringGeneration = 0
```

- Increment in `finishAndScore()` before spawning the thread; capture the value as `val gen = scoringGeneration`.
- Increment in `returnToPicker()`.
- The main-thread post becomes: `main.post { if (gen == scoringGeneration) renderResult(song, score) }`.

## Non-goals

- No back-button (hardware) handling change. Android's system back already works via the activity; this is an in-UI affordance.
- No confirm dialog.
- No animation / transition polish.
- No changes to the SDK, JNI, or core.

## Testing

Manual, on the demo APK:

1. Pick a song → PREVIEW → press "← Songs" → lands on picker, no MediaPlayer still audible.
2. Pick a song → PREVIEW → wait → COUNTDOWN → press "← Songs" during "3"/"2"/"1" → lands on picker, no "Sing!" splash appears after return.
3. Pick a song → sing for ~2 s on RECORDING → press "← Songs" → lands on picker, picker is responsive (recorder released), no auto-stop fires later.
4. Pick a song → "Stop & score" → SCORING → press "← Songs" → lands on picker, and the native score result never replaces the picker when it completes.

No new unit/CI tests — the change is pure demo-app UI glue.
