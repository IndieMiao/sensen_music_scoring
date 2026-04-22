package com.sensen.singscoring.demo

/**
 * Tiny LRC parser for the scrolling-lyrics view. Mirrors the C++ core's
 * lrc_parser at the level we need for display:
 *
 *   - `[mm:ss.xx] text`  → one entry per timestamp
 *   - `[mm:ss.xxx] text` → milliseconds also accepted
 *   - `[ti:...]`, `[ar:...]`, etc. → skipped
 *   - one line may carry multiple leading timestamps (each emits an entry)
 *   - returned list is sorted by `timeMs`
 *
 * Display-only: the SDK never sees these timestamps. This parser stays
 * with the demo, not the SDK.
 */
data class LrcLine(val timeMs: Long, val text: String)

object LrcParser {
    private val tagRegex = Regex("""\[(\d+):(\d+)(?:\.(\d+))?]""")

    fun parse(input: String): List<LrcLine> {
        val out = mutableListOf<LrcLine>()
        for (rawLine in input.lineSequence()) {
            val line = rawLine.trimEnd('\r')
            if (line.isEmpty()) continue

            val matches = tagRegex.findAll(line).toList()
            if (matches.isEmpty()) continue  // metadata lines like [ti:...] don't match (no colon-numeric)

            val text = line.substring(matches.last().range.last + 1).trim()
            if (text.isEmpty()) continue

            for (m in matches) {
                val mm = m.groupValues[1].toLong()
                val ss = m.groupValues[2].toLong()
                val frac = m.groupValues[3]
                val fracMs = when (frac.length) {
                    0 -> 0L
                    1 -> frac.toLong() * 100
                    2 -> frac.toLong() * 10
                    3 -> frac.toLong()
                    else -> frac.substring(0, 3).toLong()
                }
                out.add(LrcLine(mm * 60_000L + ss * 1_000L + fracMs, text))
            }
        }
        out.sortBy { it.timeMs }
        return out
    }
}
