# Demo 30s Recording Cap + Countdown Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Cap demo recording duration at `min(chorus + 1500ms tail, 30s)` and surface the budget to the user as an "elapsed / total" countdown in the recording screen's title row. See `docs/superpowers/specs/2026-04-23-demo-30s-cap-and-countdown-design.md`.

**Architecture:** Three small changes to `demo-android/.../MainActivity.kt` — a tunable const, a two-line calculation inside `beginRecording`, and a nullable countdown parameter on `titleRowWithBack` driven by a Choreographer frame callback. No new files, no new tests, no SDK touch.

**Tech Stack:** Kotlin 2.0.21, Android AppCompat, Android Choreographer, Gradle 8.9 / AGP 8.6.0. JDK 21. Gradle wrapper + `JAVA_HOME` env var are the only runtime requirements for verification.

---

## File Structure

**Modified (single Kotlin file):**
- `demo-android/src/main/kotlin/com/sensen/singscoring/demo/MainActivity.kt`
  - Add `kMaxSingDurationMs` private const (~line 61, with the other instance-state declarations).
  - Store computed `recordingDurationMs` on the activity so `renderRecording` can read it after `beginRecording` computes it.
  - Replace the `tailMs` block at lines 411–416 with the capped `recordingDurationMs` calculation.
  - Extend `titleRowWithBack` (line 578) to accept `countdownTotalMs: Long? = null`; when non-null, add a second TextView wired to a `Choreographer.FrameCallback` that formats `m:ss / m:ss` against `recordingStartMs`.
  - Pass `recordingDurationMs` from `renderRecording` (line 193) into `titleRowWithBack`. No other callers change.
  - Add a private `fmtMmSs(ms: Long): String` helper.
  - `import android.view.Choreographer` at the top.

**Modified:**
- `CHANGELOG.md` — one Unreleased bullet describing the cap + countdown.

**Created:** None.

---

## Task 1: Add the `kMaxSingDurationMs` const and `recordingDurationMs` capping

**Files:**
- Modify: `demo-android/src/main/kotlin/com/sensen/singscoring/demo/MainActivity.kt`

Replaces the uncapped `tailMs` logic inside `beginRecording` and stores the computed duration on the activity so the countdown view can read the same value.

- [ ] **Step 1: Add the const + instance variable**

In `MainActivity.kt`, locate the block of instance-state declarations around lines 32–60 (between `private var state = State.PICKER` and the `private val root by lazy` line). Append these two lines to the end of that block, right after `private var lastPickedSongId: String? = null` at line 60:

```kotlin
    // Max recording duration — tune here. Applied as min(chorus + 1500ms tail, this).
    private val kMaxSingDurationMs: Long = 30_000L
    private var recordingDurationMs: Long = kMaxSingDurationMs
```

Kotlin style note: `const val` is not allowed on a class property that references another property at initialisation, so `private val` is correct here. Using `val` makes the compile-time intent clear.

- [ ] **Step 2: Replace the `tailMs` block inside `beginRecording`**

At `MainActivity.kt:411-416`, the current code reads:

```kotlin
        val melodyEndMs = stagedZipPath?.let {
            runCatching { SingScoringSession.melodyEndMs(it) }.getOrDefault(-1L)
        } ?: -1L
        val tailMs = if (melodyEndMs > 0) melodyEndMs + 1500L else 60_000L
        autoStopRunnable = Runnable { if (state == State.RECORDING) finishAndScore() }
        main.postDelayed(autoStopRunnable!!, tailMs)
```

Replace with:

```kotlin
        val melodyEndMs = stagedZipPath?.let {
            runCatching { SingScoringSession.melodyEndMs(it) }.getOrDefault(-1L)
        } ?: -1L
        val songTailMs = if (melodyEndMs > 0L) melodyEndMs + 1500L else kMaxSingDurationMs
        recordingDurationMs = minOf(songTailMs, kMaxSingDurationMs)
        autoStopRunnable = Runnable { if (state == State.RECORDING) finishAndScore() }
        main.postDelayed(autoStopRunnable!!, recordingDurationMs)
```

The `-1L` fallback for `melodyEndMs` in the first assignment still drops through to the `else kMaxSingDurationMs` arm — no change needed there.

- [ ] **Step 3: Build the demo APK to verify compilation**

From the repo root (MSYS bash on Windows):

```bash
export JAVA_HOME="/c/Program Files/Android/Android Studio/jbr"
./gradlew :demo-android:assembleDebug
```

Expected: `BUILD SUCCESSFUL`. A Linux/macOS host can skip the `export` if `JAVA_HOME` is already set.

If the build fails: read the compiler error carefully — the most common problem will be accidentally writing `const val` (forbidden on a class property) or forgetting a new import. No new imports are needed for this task.

- [ ] **Step 4: Commit**

```bash
git add demo-android/src/main/kotlin/com/sensen/singscoring/demo/MainActivity.kt
git commit -m "feat(demo): cap recording duration at 30s (tunable)

Replaces the uncapped 'melodyEndMs + 1500ms tail' duration with
min(songTail, kMaxSingDurationMs). Short choruses record their full
span as before; long choruses cap at 30s. kMaxSingDurationMs is a
private const on MainActivity — tune in place.

The SDK already clips ref_notes to the actual PCM duration (C1), so
this is a pure product-side tightening with no correctness impact on
the SDK side."
```

---

## Task 2: Countdown indicator in the title row

**Files:**
- Modify: `demo-android/src/main/kotlin/com/sensen/singscoring/demo/MainActivity.kt`

Extends `titleRowWithBack` with an optional countdown that renders `m:ss / m:ss` and self-drives via Choreographer. Only the recording screen will pass a non-null value; all other callers get the existing behaviour.

- [ ] **Step 1: Add the Choreographer import**

Near the top of `MainActivity.kt` (around line 10, after `import android.os.SystemClock`), add:

```kotlin
import android.view.Choreographer
```

- [ ] **Step 2: Add the `fmtMmSs` helper**

Append just before the closing `}` of `MainActivity` (i.e., below `toastLike` around line 609, at the bottom of the class body):

```kotlin
    private fun fmtMmSs(ms: Long): String {
        val totalSeconds = (ms / 1000L).coerceAtLeast(0L)
        val m = totalSeconds / 60L
        val s = totalSeconds % 60L
        return "%d:%02d".format(m, s)
    }
```

- [ ] **Step 3: Extend `titleRowWithBack` with the countdown parameter**

The current definition at lines 578–596 is:

```kotlin
    private fun titleRowWithBack(text: String): LinearLayout {
        val row = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)
        }
        row.addView(Button(this).apply {
            this.text = "← Songs"
            setOnClickListener { returnToPicker() }
            layoutParams = LinearLayout.LayoutParams(WRAP_CONTENT, WRAP_CONTENT)
                .apply { marginEnd = 16 }
        })
        row.addView(TextView(this).apply {
            this.text = text
            textSize = 26f
            layoutParams = LinearLayout.LayoutParams(0, WRAP_CONTENT, 1f)
        })
        return row
    }
```

Replace with:

```kotlin
    private fun titleRowWithBack(text: String, countdownTotalMs: Long? = null): LinearLayout {
        val row = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)
        }
        row.addView(Button(this).apply {
            this.text = "← Songs"
            setOnClickListener { returnToPicker() }
            layoutParams = LinearLayout.LayoutParams(WRAP_CONTENT, WRAP_CONTENT)
                .apply { marginEnd = 16 }
        })
        row.addView(TextView(this).apply {
            this.text = text
            textSize = 26f
            layoutParams = LinearLayout.LayoutParams(0, WRAP_CONTENT, 1f)
        })
        if (countdownTotalMs != null) {
            row.addView(countdownTextView(countdownTotalMs))
        }
        return row
    }

    // Self-driven "elapsed / total" readout clocked off recordingStartMs.
    // The Choreographer callback re-posts itself while the view is attached and
    // auto-unregisters in onDetachedFromWindow — view lifecycle drives cleanup.
    private fun countdownTextView(totalMs: Long): TextView {
        val tv = object : TextView(this) {
            private val cb = object : Choreographer.FrameCallback {
                override fun doFrame(frameTimeNanos: Long) {
                    val elapsed = (SystemClock.elapsedRealtime() - recordingStartMs)
                        .coerceIn(0L, totalMs)
                    text = "${fmtMmSs(elapsed)} / ${fmtMmSs(totalMs)}"
                    if (isAttachedToWindow) {
                        Choreographer.getInstance().postFrameCallback(this)
                    }
                }
            }

            override fun onAttachedToWindow() {
                super.onAttachedToWindow()
                Choreographer.getInstance().postFrameCallback(cb)
            }

            override fun onDetachedFromWindow() {
                super.onDetachedFromWindow()
                Choreographer.getInstance().removeFrameCallback(cb)
            }
        }
        tv.textSize = 18f
        tv.layoutParams = LinearLayout.LayoutParams(WRAP_CONTENT, WRAP_CONTENT)
            .apply { marginStart = 16 }
        // Seed the initial value so the view isn't blank between attach and first frame.
        tv.text = "${fmtMmSs(0L)} / ${fmtMmSs(totalMs)}"
        return tv
    }
```

Notes:
- The anonymous-class `object : TextView(this)` subclass mirrors the pattern used in `LyricsScrollView.kt:38-57` (it also owns a `Choreographer.FrameCallback` scoped to the view's attach/detach lifecycle).
- `Long?` with a default of `null` means **existing callers of `titleRowWithBack("...")` continue to compile unchanged** — no other call site needs touching in this task.
- `coerceIn(0L, totalMs)` defensively clamps the displayed value so a stray late frame can't render `0:31 / 0:30`.

- [ ] **Step 4: Build the demo APK to verify compilation**

```bash
./gradlew :demo-android:assembleDebug
```

Expected: `BUILD SUCCESSFUL`. If `Choreographer` is undefined, the import from Step 1 was missed. If `fmtMmSs` is undefined, Step 2 was missed.

Do NOT run the app on a device in this task — the countdown isn't wired to `renderRecording` yet, so it wouldn't fire.

- [ ] **Step 5: Commit**

```bash
git add demo-android/src/main/kotlin/com/sensen/singscoring/demo/MainActivity.kt
git commit -m "feat(demo): add countdown indicator option to titleRowWithBack

New nullable countdownTotalMs parameter on titleRowWithBack. When
non-null, an 'm:ss / m:ss' TextView is added to the right end of the
row, self-driven via a Choreographer.FrameCallback that reads
recordingStartMs. The callback is scoped to the TextView's attach /
detach lifecycle — navigating away removes the view and cancels the
callback automatically.

No caller passes the parameter yet; this change is a no-op until the
next task wires renderRecording to supply recordingDurationMs."
```

---

## Task 3: Wire `renderRecording` to supply the countdown total

**Files:**
- Modify: `demo-android/src/main/kotlin/com/sensen/singscoring/demo/MainActivity.kt`

Single line change — `renderRecording` now passes the `recordingDurationMs` that `beginRecording` already stored.

- [ ] **Step 1: Update the `renderRecording` call site**

The current line 193 in `renderRecording` reads:

```kotlin
        col.addView(titleRowWithBack("🎤  ${song.name}"))
```

Change to:

```kotlin
        col.addView(titleRowWithBack("🎤  ${song.name}", countdownTotalMs = recordingDurationMs))
```

None of the other `titleRowWithBack(...)` call sites (lines 161, 174, 219, 283) change — they all omit the new parameter and fall through to the `null` default, giving them the unchanged old behaviour.

- [ ] **Step 2: Install and smoke-test on an attached device**

Build and install:

```bash
./gradlew :demo-android:installDebug
```

With an Android device connected via ADB. Launch the app, pick any bundled song (short or long), and confirm the countdown in the top-right of the recording screen:

- Counter starts at `0:00 / 0:30` (or `0:XX / 0:YY` where YY matches the chosen chorus when it's <30s + tail).
- Counter increments once per second.
- When the counter hits the total, auto-stop fires and the app transitions to the scoring screen (observe `logcat -s ss-core` to confirm `finalize:` is logged).
- Navigating back before the counter finishes also works — no Choreographer leak (the TextView's `onDetachedFromWindow` cancels the callback).

Record the observed behaviour in the commit message.

If no device is available, skip this step and rely on CI's `android-build` to catch the compile. Note that CI does not exercise the UI, so a manual pass on a device is valuable — but not required for the code to land.

- [ ] **Step 3: Commit**

```bash
git add demo-android/src/main/kotlin/com/sensen/singscoring/demo/MainActivity.kt
git commit -m "feat(demo): show recording countdown in the recording screen

renderRecording now passes the stored recordingDurationMs to
titleRowWithBack's new countdownTotalMs parameter. Short choruses
show their natural 'chorus + 1.5s tail' total; long choruses show a
30s total. Non-recording screens are unchanged."
```

---

## Task 4: CHANGELOG entry

**Files:**
- Modify: `CHANGELOG.md`

- [ ] **Step 1: Add an Unreleased bullet**

Open `CHANGELOG.md`. Find the existing `## Unreleased` section near the top. Append to the bullet list that follows the `### Changed` heading (immediately before the two existing `**Phrase-level time alignment**` / `**PCM-duration clipping**` bullets landed in `e100377`):

```markdown
- **Demo: recording duration capped at 30s with on-screen countdown.** `MainActivity` now caps `recordingDurationMs` at `min(melodyEndMs + 1500ms, 30_000L)` — short choruses still record end-to-end, long choruses stop at 30 s. The recording screen's title row gets an `m:ss / m:ss` countdown so the user can see the budget. `kMaxSingDurationMs` is a `private val` on `MainActivity` — tune in place. SDK behaviour unchanged.
```

- [ ] **Step 2: Commit**

```bash
git add CHANGELOG.md
git commit -m "docs(changelog): note demo 30s cap and countdown indicator

Unreleased entry describing the product-side complement to the SDK's
C1 clip. SDK is unchanged."
```

---

## Verification

After Task 4, confirm the whole change set:

- [ ] **Demo builds cleanly:**

```bash
./gradlew :demo-android:assembleDebug
```

Expected: `BUILD SUCCESSFUL`.

- [ ] **AAR still builds cleanly:**

```bash
./gradlew :singscoring:assembleDebug
```

Expected: `BUILD SUCCESSFUL`. This should be trivially true (no SDK files touched) but worth confirming end-to-end.

- [ ] **Commit log is clean:**

```bash
git log --oneline HEAD~4..HEAD
```

Expected: 4 commits, one per task, all with plain subjects and no Claude attribution. Subject lines:

```
docs(changelog): note demo 30s cap and countdown indicator
feat(demo): show recording countdown in the recording screen
feat(demo): add countdown indicator option to titleRowWithBack
feat(demo): cap recording duration at 30s (tunable)
```

- [ ] **Public ABI unchanged:**

```bash
git diff main~4 -- core/include/singscoring.h
```

Expected: no output. (This plan touches only `demo-android/` and `CHANGELOG.md`.)

---

## Self-review notes

- **Spec coverage:** every section of the spec maps to a task:
  - Tunable cap → Task 1 Step 1.
  - Recording-duration calc + auto-stop rewire → Task 1 Step 2.
  - Countdown TextView + Choreographer lifecycle → Task 2.
  - Wiring `renderRecording` to the countdown → Task 3.
  - CHANGELOG → Task 4.
  - The spec's "Lyrics: no change" section is implicitly honoured — no task touches `LyricsScrollView.kt` or the lyrics wiring.
- **Type consistency:** `recordingDurationMs: Long` is defined in Task 1 and consumed by Task 3 with matching type. `countdownTotalMs: Long?` in Task 2 accepts the value from Task 3.
- **No placeholders.** Every step shows exact code or exact commands.
- **No new tests.** The demo has no unit tests today; adding the first one would be its own scope. The smoke-test in Task 3 Step 2 is the manual verification.
