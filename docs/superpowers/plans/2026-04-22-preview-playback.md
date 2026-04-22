# Preview Playback Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Play the chorus MP3 for 13 seconds (with scrolling lyrics and a Skip button) after a song is picked, before the 3-2-1 countdown starts; also rebuild with all 19 sample songs.

**Architecture:** Single-file change to `MainActivity.kt` — add a `PREVIEW` state, a `MediaPlayer` field, and three new methods (`startPreview`, `renderPreview`, `stopPreviewAndCountdown`). Songs expansion needs no code change: the assets source dir already points at `SongHighlightSamples/`; a clean rebuild picks up all 19 zips automatically.

**Tech Stack:** Kotlin, Android `android.media.MediaPlayer` (no new dependencies), existing `LyricsScrollView` / `SongAssets` unchanged.

---

## File map

| File | Change |
|------|--------|
| `demo-android/src/main/kotlin/com/sensen/singscoring/demo/MainActivity.kt` | Add `PREVIEW` enum value; add `mediaPlayer`/`previewAutoAdvance` fields; add `startPreview`, `renderPreview`, `stopPreviewAndCountdown`; update `onSongPicked`, `requestMicPermission`, `onDestroy` |

No other files change. No new files.

> **Note:** The demo app has no Android unit-test infrastructure (all tests are C++ GoogleTest in `core/`). TDD steps below use build verification + install/run as the feedback loop.

---

## Environment setup

All `./gradlew` commands must be run from the repo root (`sensen_music_scoring/`). On Windows MSYS bash, set JAVA_HOME first:

```bash
export JAVA_HOME="/c/Program Files/Android/Android Studio/jbr"
```

Gradle does not need `env.sh` (the wrapper finds the JDK automatically), but the export above is required in the shell that invokes Gradle.

---

## Task 1: Add PREVIEW state and new fields

**Files:**
- Modify: `demo-android/src/main/kotlin/com/sensen/singscoring/demo/MainActivity.kt`

- [ ] **Step 1.1: Add `PREVIEW` to the State enum**

  In `MainActivity.kt`, find line 28:
  ```kotlin
  private enum class State { PICKER, COUNTDOWN, RECORDING, SCORING, RESULT }
  ```
  Replace with:
  ```kotlin
  private enum class State { PICKER, PREVIEW, COUNTDOWN, RECORDING, SCORING, RESULT }
  ```

- [ ] **Step 1.2: Add the two new fields**

  After the existing `private var autoStopRunnable: Runnable? = null` field (line 43), add:
  ```kotlin
  private var mediaPlayer: android.media.MediaPlayer? = null
  private var previewAutoAdvance: Runnable? = null
  ```

- [ ] **Step 1.3: Verify it builds**

  ```bash
  export JAVA_HOME="/c/Program Files/Android/Android Studio/jbr"
  ./gradlew :demo-android:assembleDebug 2>&1 | tail -5
  ```
  Expected last line: `BUILD SUCCESSFUL in Xs`

- [ ] **Step 1.4: Commit**

  ```bash
  git add demo-android/src/main/kotlin/com/sensen/singscoring/demo/MainActivity.kt
  git commit -m "feat(demo): add PREVIEW state and mediaPlayer fields"
  ```

---

## Task 2: Implement the three new methods

**Files:**
- Modify: `demo-android/src/main/kotlin/com/sensen/singscoring/demo/MainActivity.kt`

- [ ] **Step 2.1: Add `startPreview`**

  Insert the following method after the closing brace of `renderResult` (before the `// --- flow ---` comment):

  ```kotlin
  private fun startPreview(song: SongAssets.Song) {
      val staged = try {
          SongAssets.stage(this, song)
      } catch (e: Exception) {
          toastLike("Failed to stage song: ${e.message}")
          return
      }
      stagedZipPath = staged.zipPath
      lyrics = readLyrics(staged.zipPath, song.code)

      try {
          val mp = android.media.MediaPlayer().apply {
              setDataSource(staged.mp3Path)
              prepare()
              start()
          }
          mediaPlayer = mp
      } catch (e: Exception) {
          toastLike("Preview unavailable: ${e.message}")
          startCountdown(song)
          return
      }

      renderPreview(song)

      previewAutoAdvance = Runnable { stopPreviewAndCountdown(song) }
      main.postDelayed(previewAutoAdvance!!, 13_000L)
  }
  ```

- [ ] **Step 2.2: Add `renderPreview`**

  Insert immediately after `startPreview`:

  ```kotlin
  private fun renderPreview(song: SongAssets.Song) {
      state = State.PREVIEW
      root.removeAllViews()
      val col = LinearLayout(this).apply {
          orientation = LinearLayout.VERTICAL
          layoutParams = FrameLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT)
      }
      col.addView(titleView("🎵  ${song.displayName}"))
      col.addView(subtitleView("Listen to the chorus…"))

      val view = LyricsScrollView(this).apply {
          setLines(lyrics)
          setClock { mediaPlayer?.currentPosition?.toLong() ?: 0L }
          layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, 0, 1f)
              .apply { topMargin = 16; bottomMargin = 16 }
      }
      col.addView(view)

      col.addView(Button(this).apply {
          text = "Skip →"
          layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)
          setOnClickListener { stopPreviewAndCountdown(song) }
      })
      root.addView(col)
  }
  ```

- [ ] **Step 2.3: Add `stopPreviewAndCountdown`**

  Insert immediately after `renderPreview`:

  ```kotlin
  private fun stopPreviewAndCountdown(song: SongAssets.Song) {
      previewAutoAdvance?.let { main.removeCallbacks(it) }
      previewAutoAdvance = null
      mediaPlayer?.stop()
      mediaPlayer?.release()
      mediaPlayer = null
      startCountdown(song)
  }
  ```

- [ ] **Step 2.4: Verify it builds**

  ```bash
  ./gradlew :demo-android:assembleDebug 2>&1 | tail -5
  ```
  Expected last line: `BUILD SUCCESSFUL in Xs`

- [ ] **Step 2.5: Commit**

  ```bash
  git add demo-android/src/main/kotlin/com/sensen/singscoring/demo/MainActivity.kt
  git commit -m "feat(demo): implement startPreview / renderPreview / stopPreviewAndCountdown"
  ```

---

## Task 3: Wire call sites and update onDestroy

**Files:**
- Modify: `demo-android/src/main/kotlin/com/sensen/singscoring/demo/MainActivity.kt`

- [ ] **Step 3.1: Update `requestMicPermission` callback**

  Find (around line 50):
  ```kotlin
  if (granted) pendingSong?.let { startCountdown(it) }
  ```
  Replace with:
  ```kotlin
  if (granted) pendingSong?.let { startPreview(it) }
  ```

- [ ] **Step 3.2: Update `onSongPicked`**

  Find (around line 186):
  ```kotlin
  if (granted) startCountdown(song) else requestMicPermission.launch(Manifest.permission.RECORD_AUDIO)
  ```
  Replace with:
  ```kotlin
  if (granted) startPreview(song) else requestMicPermission.launch(Manifest.permission.RECORD_AUDIO)
  ```

- [ ] **Step 3.3: Update `onDestroy`**

  Find (around line 62):
  ```kotlin
  recorder?.stop(); recorder = null
  ```
  Replace with:
  ```kotlin
  recorder?.stop(); recorder = null
  mediaPlayer?.release(); mediaPlayer = null
  ```

- [ ] **Step 3.4: Verify clean build with all 19 songs**

  ```bash
  ./gradlew clean :demo-android:assembleDebug 2>&1 | tail -5
  ```
  Expected last line: `BUILD SUCCESSFUL in Xs`

  Confirm all 19 zips are bundled (count lines containing `.zip` in merged assets):
  ```bash
  find demo-android/build/intermediates/assets -name "*.zip" | wc -l
  ```
  Expected: `19`

- [ ] **Step 3.5: Install on device and manually verify the full flow**

  ```bash
  ./gradlew :demo-android:installDebug
  ```

  Checklist:
  - [ ] Song picker shows all 19 songs
  - [ ] Tapping a song shows the preview screen with title "🎵 [song name]" and subtitle "Listen to the chorus…"
  - [ ] MP3 plays audibly
  - [ ] Lyrics scroll in sync with playback
  - [ ] After 13 seconds, screen automatically advances to the 3-2-1 countdown
  - [ ] Tapping "Skip →" immediately stops the MP3 and goes to the 3-2-1 countdown
  - [ ] After countdown, recording, scoring, and result screens all work as before
  - [ ] Rotating the device or navigating away during preview does not crash

- [ ] **Step 3.6: Commit**

  ```bash
  git add demo-android/src/main/kotlin/com/sensen/singscoring/demo/MainActivity.kt
  git commit -m "feat(demo): wire preview into song-pick flow; all 19 songs bundled"
  ```
