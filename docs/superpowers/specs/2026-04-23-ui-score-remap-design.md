# UI-level score remap with toggle (demo app)

## Motivation

The C++ scoring core returns an integer in `[10, 99]`, and the demo app renders it
verbatim on the result screen. We want to experiment with a friendlier display
curve — one that compresses the "passed" band and expands the high-end — without
disturbing the engine or any tests. The remap therefore lives entirely in the
demo UI; the SDK, the ABI, and all core/integration tests stay untouched.

A toggle on the result screen lets us flip between the raw engine score and the
remapped score, so we can compare them on the same take.

## Scope

**In scope** — `demo-android/src/main/kotlin/com/sensen/singscoring/demo/MainActivity.kt` only.

**Out of scope** (do not touch):
- `core/` C++ scoring
- `bindings/android/src/main/{cpp,kotlin}/...` (SDK surface)
- `bindings/ios/...`
- `tests/` (they assert against raw engine output)
- Pass threshold (stays at 60 on both scales)

## Remap function

A pure, piecewise-linear map from raw score (the `Int` returned by
`SingScoringSession.score(...)`) to display score. All arithmetic in `Int`, with
`Math.round` on the boundary expressions to avoid truncation surprises.

| Raw band            | Mapped band | Formula                               |
| ------------------- | ----------- | ------------------------------------- |
| `s < 15`            | `1`         | clamp (SDK floor is 10)               |
| `15 ≤ s ≤ 59`       | `[1, 60]`   | `1 + round((s - 15) * 59.0 / 44.0)`   |
| `60 ≤ s ≤ 70`       | `[60, 95]`  | `60 + round((s - 60) * 35.0 / 10.0)`  |
| `71 ≤ s ≤ 100`      | `[96, 100]` | `96 + round((s - 71) * 4.0 / 29.0)`   |

Boundary checks (all rounded):

- `14 → 1`, `15 → 1` — low clamp meets low band at `1`.
- `59 → 60`, `60 → 60` — both scales share `60` at the pass line (intended).
- `70 → 95`, `71 → 96` — continuous across the mid/high split.
- `99 → 100`, `100 → 100` — upper band saturates at `100`.

The function lives as a private helper on `MainActivity` (no new file, no new
package). Signature:

```kotlin
private fun remapScore(raw: Int): Int
```

It is pure (no state, no side effects) so it can be unit-tested later without
any Android machinery if we decide it's worth a test.

## UI changes

All changes are local to the result screen.

1. **State** — two new `private var` fields on `MainActivity`:
   - `lastRawScore: Int = -1` — the raw score for the result currently on screen.
   - `showRemapped: Boolean = true` — default view is the remapped ("new") score.
   Both reset to `-1` / `true` whenever we leave the result screen (picker,
   new song selection) so each result starts fresh on the remapped view.

2. **`renderResult(song, score)`** — stash `score` into `lastRawScore`, then
   draw via a helper `drawResultBody(...)`:
   - A big score `TextView` showing either `remapScore(raw)` or `raw` depending
     on `showRemapped`.
   - Pass/fail colour and subtitle computed from **the displayed score** against
     the `>= 60` threshold — i.e. whichever number the user is looking at drives
     the pass/fail styling. Note a single edge case: `raw = 59` remaps to `60`,
     so toggling a 59 flips red→green. This is acceptable (and arguably the
     point of the remap — raw `59` sits at the top of the low band and the
     display scale rewards it); callers can tell which scale is shown from the
     toggle button's current label.
   - A plain-text toggle `Button` labelled:
     - `"Show raw score"` when `showRemapped == true`
     - `"Show new score"` when `showRemapped == false`
     Clicking it flips `showRemapped` and re-renders the body only (no state lost).
   - The existing "Pick another song" button, unchanged.

3. **No persistence** — the toggle preference does not survive leaving the
   result screen. Simpler than adding `SharedPreferences`, and the demo is a
   sandbox anyway.

## Non-goals

- Surfacing both scores side-by-side. We want a single prominent number.
- Changing the `[10, 99]` engine floor/ceiling or any ABI contract.
- Adding the remap to the iOS demo (none exists yet in this branch).
- Persisting the toggle across app restarts.

## Risk / rollback

The change is self-contained to one Kotlin file in the demo app. Reverting is a
single-file revert. No released SDK surface moves.
