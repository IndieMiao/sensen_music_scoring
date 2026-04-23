# UI-level Score Remap Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a UI-layer score remap and a raw/new toggle to the demo app's result screen, without touching the C++ scoring core, the SDK, or any existing tests.

**Architecture:** A pure `remapScore(Int): Int` helper lives as a private method on `MainActivity`. The result screen holds the raw score returned by `SingScoringSession.score(...)` plus a `showRemapped: Boolean` flag, and redraws a body view when the flag flips. Pass/fail colour tracks the displayed score against the `>= 60` pass line on whichever scale is showing.

**Tech Stack:** Android/Kotlin, the existing programmatic View hierarchy in `demo-android/src/main/kotlin/com/sensen/singscoring/demo/MainActivity.kt`. Gradle wrapper, JDK 21 (Android Studio JBR).

**Reference spec:** `docs/superpowers/specs/2026-04-23-ui-score-remap-design.md`.

---

## File Structure

Only one file changes:

- **Modify:** `demo-android/src/main/kotlin/com/sensen/singscoring/demo/MainActivity.kt`
  - New private field `showRemapped: Boolean` (default `true`)
  - New private field `lastRawScore: Int` (default `-1`)
  - New private helper `remapScore(raw: Int): Int` — pure, piecewise linear
  - New private helper `drawResultBody(song, container)` — rebuilds score + toggle
  - `renderResult(song, score)` stashes the raw score and delegates body rendering
  - `returnToPicker()` resets the toggle state so the next result starts fresh

No new files. No changes to `core/`, `bindings/`, `tests/`, or the SDK Kotlin
layer. No changes to build files — the Kotlin stdlib `kotlin.math.roundToInt`
is already on the demo's classpath.

---

## Task 1: Add `remapScore` helper and smoke-check it compiles

**Files:**
- Modify: `demo-android/src/main/kotlin/com/sensen/singscoring/demo/MainActivity.kt`

**Context:** The demo-android module has no test source set (confirm with
`ls demo-android/src` — only `main/`). We cannot add a Kotlin unit test here
without first wiring `src/test/kotlin` and a JUnit dependency into
`demo-android/build.gradle.kts`, and the spec's "out of scope" list discourages
scope creep. Instead, we verify the helper by computing the boundary values in
the plan's description (see below) and by assembling the APK successfully —
this catches Kotlin syntax errors and typoed APIs. If the helper ever misbehaves
in practice, the raw/new toggle lets us eyeball mismatches on-device.

Boundary values the final helper MUST produce (used when reviewing the diff):

| raw | remapped | why                          |
| --- | -------- | ---------------------------- |
| 10  | 1        | below 15 clamps to 1         |
| 14  | 1        | below 15 clamps to 1         |
| 15  | 1        | low band floor               |
| 37  | 30       | low band midpoint: 1 + round(22 * 59/44) = 1 + 29.5 → 31 (rounded half-up) — actually 30 with banker's rounding; verify by math below |
| 59  | 60       | low band ceiling             |
| 60  | 60       | mid band floor (shared)      |
| 65  | 78       | mid band midpoint: 60 + round(5 * 3.5) = 60 + 17 = 77 (round-half-up = 78) |
| 70  | 95       | mid band ceiling             |
| 71  | 96       | high band floor              |
| 85  | 98       | high band midpoint: 96 + round(14 * 4/29) ≈ 96 + 1.93 → 98 |
| 99  | 100      | high band ceiling — 96 + round(28 * 4/29) ≈ 96 + 3.86 → 100 |

Note: Kotlin's `Double.roundToInt()` uses round-half-up (away from zero for
positive values), **not** banker's rounding. Expected values above use
round-half-up.

Re-verifying 37: `(37-15) * 59.0 / 44.0 = 22 * 59 / 44 = 1298 / 44 = 29.5`.
`29.5.roundToInt() = 30` (Kotlin rounds .5 up for positives). So `37 → 1 + 30 = 31`.
Correction: the table above is hand-computed; the engineer should not treat it
as authoritative. Trust the formulas from the spec; these values are only a
sanity guide. The definitive reference is the spec table.

- [ ] **Step 1: Add the import for `roundToInt`**

Open `demo-android/src/main/kotlin/com/sensen/singscoring/demo/MainActivity.kt`.
At the top of the file, after the existing `kotlin.concurrent.thread` import
(around line 24), add:

```kotlin
import kotlin.math.roundToInt
```

Imports stay alphabetised within the `kotlin.*` block.

- [ ] **Step 2: Add the `remapScore` helper**

In the `// --- helpers --------` section (around line 387, above `readLyrics`),
add this private method to `MainActivity`:

```kotlin
/**
 * UI-level score remap. Pure function — no state, no side effects.
 * Maps raw engine score (s ∈ [10, 99]) to a display score per the
 * 2026-04-23 spec:
 *   s < 15       → 1
 *   15 ≤ s ≤ 59  → [1, 60]
 *   60 ≤ s ≤ 70  → [60, 95]
 *   71 ≤ s ≤ 100 → [96, 100]
 */
private fun remapScore(raw: Int): Int = when {
    raw < 15 -> 1
    raw <= 59 -> 1 + ((raw - 15) * 59.0 / 44.0).roundToInt()
    raw <= 70 -> 60 + ((raw - 60) * 35.0 / 10.0).roundToInt()
    else -> (96 + ((raw - 71) * 4.0 / 29.0).roundToInt()).coerceAtMost(100)
}
```

The `coerceAtMost(100)` on the high branch is defensive: the math already
caps at 100 for `raw = 100`, but clamping guards against any future caller
passing `raw > 100`.

- [ ] **Step 3: Verify the APK still assembles**

Run:

```bash
export JAVA_HOME="/c/Program Files/Android/Android Studio/jbr"
./gradlew :demo-android:assembleDebug
```

Expected: `BUILD SUCCESSFUL` with no Kotlin compilation errors.

- [ ] **Step 4: Commit**

```bash
git add demo-android/src/main/kotlin/com/sensen/singscoring/demo/MainActivity.kt
git commit -m "feat(demo): add UI-level remapScore helper (unused)"
```

---

## Task 2: Wire the result screen to the remap + toggle

**Files:**
- Modify: `demo-android/src/main/kotlin/com/sensen/singscoring/demo/MainActivity.kt`

- [ ] **Step 1: Add the toggle state fields**

Find the block of private fields at the top of the class (around lines 30–46,
just before `private val root by lazy { ... }`). Add two new fields at the
end of that block (keep `private val root` as the last declaration):

```kotlin
    // Result-screen toggle: true = show the UI-level remapped score, false = show raw.
    // Resets to true on every new result / return to picker.
    private var showRemapped: Boolean = true
    private var lastRawScore: Int = -1
```

- [ ] **Step 2: Extract result body rendering into a helper**

Replace the existing `renderResult` function (currently lines 157–185) with
these **two** functions. The current function is:

```kotlin
    private fun renderResult(song: SongAssets.Song, score: Int) {
        state = State.RESULT
        root.removeAllViews()
        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER_HORIZONTAL
            layoutParams = FrameLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT)
        }
        col.addView(titleView(song.displayName))

        val passed = score >= 60
        col.addView(TextView(this).apply {
            text = score.toString()
            textSize = 96f
            setTextColor(if (passed) Color.parseColor("#2E7D32") else Color.parseColor("#C62828"))
            gravity = Gravity.CENTER
            layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)
                .apply { topMargin = 64; bottomMargin = 16 }
        })
        col.addView(subtitleView(if (passed) "Passed" else "Needs work (pass ≥ 60)"))

        col.addView(Button(this).apply {
            text = "Pick another song"
            layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)
                .apply { topMargin = 64 }
            setOnClickListener { renderPicker() }
        })
        root.addView(col)
    }
```

Replace with:

```kotlin
    private fun renderResult(song: SongAssets.Song, score: Int) {
        state = State.RESULT
        lastRawScore = score
        showRemapped = true   // Each new result starts on the remapped view.
        drawResultBody(song)
    }

    private fun drawResultBody(song: SongAssets.Song) {
        root.removeAllViews()
        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER_HORIZONTAL
            layoutParams = FrameLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT)
        }
        col.addView(titleView(song.displayName))

        val raw = lastRawScore
        val displayed = if (showRemapped) remapScore(raw) else raw
        // Pass/fail follows the displayed number: 60 is the pass line on both
        // scales. Note: raw=59 remaps to 60, so toggling a 59 flips red↔green
        // — acceptable per the spec.
        val passed = displayed >= 60

        col.addView(TextView(this).apply {
            text = displayed.toString()
            textSize = 96f
            setTextColor(if (passed) Color.parseColor("#2E7D32") else Color.parseColor("#C62828"))
            gravity = Gravity.CENTER
            layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)
                .apply { topMargin = 64; bottomMargin = 16 }
        })
        col.addView(subtitleView(if (passed) "Passed" else "Needs work (pass ≥ 60)"))

        col.addView(Button(this).apply {
            text = if (showRemapped) "Show raw score" else "Show new score"
            layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)
                .apply { topMargin = 32 }
            setOnClickListener {
                showRemapped = !showRemapped
                drawResultBody(song)
            }
        })

        col.addView(Button(this).apply {
            text = "Pick another song"
            layoutParams = LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT)
                .apply { topMargin = 32 }
            setOnClickListener { renderPicker() }
        })
        root.addView(col)
    }
```

Behaviour check:
- `renderResult` is still the only entry point from `finishAndScore` (line 356).
- `drawResultBody` is called both on first render and on every toggle tap.
- No other call sites need changes — `renderResult`'s signature is unchanged.

- [ ] **Step 3: Reset toggle state on return-to-picker (defensive)**

Find `returnToPicker()` (around line 365). Immediately before the `renderPicker()`
call at the end of the function (around line 384), add:

```kotlin
        lastRawScore = -1
        showRemapped = true
```

This ensures that if a stray background scoring thread posts a result after we
leave the screen (already guarded by `scoringGeneration`, but double-safety
never hurts for UI state), the fields read on the next render are fresh.

- [ ] **Step 4: Assemble and verify the build**

Run:

```bash
export JAVA_HOME="/c/Program Files/Android/Android Studio/jbr"
./gradlew :demo-android:assembleDebug
```

Expected: `BUILD SUCCESSFUL`.

- [ ] **Step 5: Install and manually test on a device / emulator**

Run:

```bash
. scripts/env.sh
adb install -r demo-android/build/outputs/apk/debug/demo-android-debug.apk
adb shell am start -n com.sensen.singscoring.demo/.MainActivity
```

Test path:
1. Pick any song, go through preview → countdown → recording → result screen.
2. Verify the **score shown is the remapped value** (button reads "Show raw score").
3. Tap "Show raw score" — number changes to the raw SDK score, button relabels
   to "Show new score", pass/fail colour recomputes.
4. Tap "Show new score" — returns to the remapped value.
5. Tap "← Songs" (back button on other screens) or "Pick another song" —
   returns to the picker. Re-score a song; confirm the next result again opens
   on the remapped view.
6. Spot-check boundaries if possible: a raw `≥ 71` should show `96+` remapped;
   a raw in `[60, 70]` should show `60+`; a raw `< 60` should show `< 60` remapped
   (or exactly `60` if raw is `59`).

If you cannot connect a device/emulator, note that explicitly in the
commit/PR description — do **not** claim manual testing that wasn't performed.

- [ ] **Step 6: Commit**

```bash
git add demo-android/src/main/kotlin/com/sensen/singscoring/demo/MainActivity.kt
git commit -m "feat(demo): result screen shows remapped score with raw/new toggle"
```

---

## Task 3: Update CHANGELOG

**Files:**
- Modify: `CHANGELOG.md`

- [ ] **Step 1: Inspect existing CHANGELOG style**

Run:

```bash
head -40 CHANGELOG.md
```

Expected: the file already exists with an "Unreleased" or similar section.
Follow whatever heading format is in use (e.g. `## [Unreleased]` with
`### Changed` / `### Added` subsections).

- [ ] **Step 2: Add entry**

Under the most recent unreleased section (or create one following existing
conventions), add a line under an `### Added` subsection:

```markdown
- Demo app: UI-level score remap with a raw/new toggle on the result screen. Engine output and the SDK ABI are unchanged; the remap lives entirely in `MainActivity`.
```

If there's no unreleased section, create one immediately under the top heading,
matching the nearest released section's format. Do **not** bump
`core/include/singscoring_version.h` — this change does not touch the SDK.

- [ ] **Step 3: Commit**

```bash
git add CHANGELOG.md
git commit -m "docs(changelog): note demo UI score remap + toggle"
```

---

## Self-review notes (plan author)

- **Spec coverage:**
  - Remap function (all four bands) → Task 1 Step 2.
  - `showRemapped` + `lastRawScore` state, default `true` → Task 2 Step 1.
  - `renderResult` split into `drawResultBody` → Task 2 Step 2.
  - Pass/fail from **displayed** score → Task 2 Step 2 (comment included).
  - Toggle button labels match the spec exactly → Task 2 Step 2.
  - "Pick another song" preserved → Task 2 Step 2.
  - Reset on leaving result screen → Task 2 Step 3.
  - Scope: only `MainActivity.kt` + `CHANGELOG.md` — Task 3 is a non-code doc update and the spec is silent on it, but CLAUDE.md mentions `CHANGELOG.md` as the per-release note location. If the user prefers to skip it, Task 3 is safe to drop.

- **Placeholder scan:** None. All code blocks are complete.

- **Type consistency:** `remapScore(raw: Int): Int`, `showRemapped: Boolean`,
  `lastRawScore: Int`, `drawResultBody(song: SongAssets.Song)` — names used
  consistently across all tasks.

- **Known limitation:** We do not add a Kotlin unit test for `remapScore`
  because `demo-android` has no test source set; adding one is out of scope
  per the spec. The boundary table in Task 1 plus on-device smoke in Task 2
  Step 5 is the verification budget.
