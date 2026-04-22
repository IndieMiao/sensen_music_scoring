package com.sensen.singscoring.demo

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.view.Choreographer
import android.view.View

/**
 * Centered scrolling lyrics. Driven by an external elapsed-time supplier so
 * the view doesn't own the clock — the activity's recording start time is
 * the truth, and this view just renders against it.
 *
 *   - active line is centered horizontally and vertically, drawn larger.
 *   - upcoming lines fade above; past lines fade below (or vice versa,
 *     depending on scroll direction; we scroll past lines upward).
 *   - smooth interpolation between adjacent lines using a fractional
 *     position so motion is continuous.
 */
class LyricsScrollView(context: Context) : View(context) {

    private var lines: List<LrcLine> = emptyList()
    private var elapsedMs: () -> Long = { 0L }

    private val activePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.WHITE
        textSize = 56f
        textAlign = Paint.Align.CENTER
        isFakeBoldText = true
    }
    private val inactivePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.parseColor("#80FFFFFF")  // 50% white
        textSize = 40f
        textAlign = Paint.Align.CENTER
    }

    private val frameCallback = object : Choreographer.FrameCallback {
        override fun doFrame(frameTimeNanos: Long) {
            invalidate()
            if (isAttachedToWindow) Choreographer.getInstance().postFrameCallback(this)
        }
    }

    fun setLines(lines: List<LrcLine>) { this.lines = lines; invalidate() }
    fun setClock(supplier: () -> Long) { this.elapsedMs = supplier }

    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        setBackgroundColor(Color.parseColor("#101010"))
        Choreographer.getInstance().postFrameCallback(frameCallback)
    }

    override fun onDetachedFromWindow() {
        super.onDetachedFromWindow()
        Choreographer.getInstance().removeFrameCallback(frameCallback)
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        if (lines.isEmpty()) return

        val now = elapsedMs()

        // Find the active line: largest index whose timeMs <= now.
        var active = -1
        for (i in lines.indices) {
            if (lines[i].timeMs <= now) active = i else break
        }

        // Fractional position for smooth scroll between lines.
        val frac: Float = if (active >= 0 && active + 1 < lines.size) {
            val span = (lines[active + 1].timeMs - lines[active].timeMs).coerceAtLeast(1)
            ((now - lines[active].timeMs).toFloat() / span).coerceIn(0f, 1f)
        } else 0f

        val centerX = width / 2f
        val centerY = height / 2f
        val rowH = 80f

        // Anchor: y of the active line (slides up as `frac` grows toward the next line).
        val activeY = centerY - frac * rowH

        for (i in lines.indices) {
            val y = activeY + (i - active) * rowH
            if (y < -rowH || y > height + rowH) continue
            val paint = if (i == active) activePaint else inactivePaint
            canvas.drawText(lines[i].text, centerX, y, paint)
        }
    }
}
