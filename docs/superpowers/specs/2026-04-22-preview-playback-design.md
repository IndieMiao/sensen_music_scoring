# Preview Playback Before Countdown — Design Spec

**Date:** 2026-04-22  
**Scope:** `demo-android` only — no changes to core C++ or AAR SDK.

---

## Goals

1. Bundle all 19 songs from `SongHighlightSamples/` in the APK (was 4).
2. After a song is picked, play its chorus MP3 for up to 13 seconds while showing the scrolling lyrics, so the user can hear the melody before singing.
3. Provide a Skip button to advance early to the countdown.

---

## Non-goals

- No changes to the scoring engine, JNI layer, or AAR.
- No changes to `LyricsScrollView`, `AudioRecorder`, `LrcParser`, or `SongAssets`.
- No ExoPlayer / Media3 dependency — `android.media.MediaPlayer` only.

---

## Songs expansion

No code changes required. `demo-android/build.gradle.kts` already has:

```kotlin
sourceSets["main"].assets.srcDirs("../SongHighlightSamples")
```

All 19 zips are in `SongHighlightSamples/` and `SongAssets.list()` already loads every `.zip` from assets. Rebuilding the APK is sufficient.

---

## State machine

Add `PREVIEW` to the existing `State` enum, between `PICKER` and `COUNTDOWN`:

```
PICKER → PREVIEW → COUNTDOWN → RECORDING → SCORING → RESULT
```

---

## Implementation — `MainActivity.kt`

### New fields

```kotlin
private var mediaPlayer: android.media.MediaPlayer? = null
private var previewAutoAdvance: Runnable? = null
```

### `onSongPicked` change

Replace the call to `startCountdown(song)` with `startPreview(song)`. The microphone permission check is unchanged.

### `startPreview(song: SongAssets.Song)`

1. Stage the zip via `SongAssets.stage(this, song)` (same try/catch pattern as the existing `startCountdown`). Sets `stagedZipPath` and parses `lyrics`.
2. Create `MediaPlayer`, call `setDataSource(staged.mp3Path)`, `prepare()`, `start()`.
3. If `prepare()` throws, call `toastLike(...)` and fall through to `startCountdown(song)` directly — the user is never blocked by a broken MP3.
4. Call `renderPreview(song)`.
5. Post `previewAutoAdvance` runnable at **13 000 ms** → calls `stopPreviewAndCountdown(song)`.

### `renderPreview(song: SongAssets.Song)`

- Title: `"🎵  ${song.displayName}"`
- Subtitle: `"Listen to the chorus…"`
- `LyricsScrollView` with clock lambda `{ mediaPlayer?.currentPosition?.toLong() ?: 0L }` — exact same view class as the recording screen, no modifications.
- `"Skip →"` `Button` at bottom — calls `stopPreviewAndCountdown(song)`.

### `stopPreviewAndCountdown(song: SongAssets.Song)`

1. Remove `previewAutoAdvance` from the main handler.
2. `mediaPlayer?.stop(); mediaPlayer?.release(); mediaPlayer = null`
3. Call existing `startCountdown(song)` — staging is idempotent (`SongAssets.stage` checks file existence), so no double-extraction.

### `onDestroy` addition

```kotlin
mediaPlayer?.release(); mediaPlayer = null
```

Added alongside the existing `recorder?.stop(); recorder = null`.

---

## What does NOT change

| File | Status |
|------|--------|
| `core/` C++ sources | Untouched |
| `bindings/` JNI / AAR | Untouched |
| `SongAssets.kt` | Untouched |
| `LyricsScrollView.kt` | Untouched |
| `AudioRecorder.kt` | Untouched |
| `LrcParser.kt` | Untouched |
| `startCountdown` / `beginRecording` / `finishAndScore` | Untouched |

---

## Error handling

| Scenario | Behaviour |
|----------|-----------|
| `MediaPlayer.prepare()` throws | `toastLike(...)`, skip straight to `startCountdown(song)` |
| MP3 shorter than 13 s | `previewAutoAdvance` fires at 13 s; MediaPlayer is already stopped naturally — `stop()` is a no-op on a completed player, then `release()` proceeds normally |
| User navigates back (presses back button) | `onDestroy` releases `mediaPlayer`; existing back-press behaviour unchanged |

---

## Files changed

- `demo-android/src/main/kotlin/com/sensen/singscoring/demo/MainActivity.kt` — add `PREVIEW` state, new fields, `startPreview`, `renderPreview`, `stopPreviewAndCountdown`, update `onSongPicked` and `onDestroy`.
- No other files.
