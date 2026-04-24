package com.sensen.singscoring.demo.ui

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.view.Gravity
import android.widget.FrameLayout
import android.widget.TextView
import androidx.annotation.ColorInt

@SuppressLint("ViewConstructor")
class ScoreRingView(context: Context) : FrameLayout(context) {

    private val ringPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = 2.dp(context).toFloat()
        color = Palette.ACCENT
    }

    private val number: TextView = TextView(context).apply {
        textSize = 96f
        setTextColor(Palette.ACCENT)
        gravity = Gravity.CENTER
        layoutParams = LayoutParams(
            LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT,
        ).apply { gravity = Gravity.CENTER }
    }

    init {
        setWillNotDraw(false)  // FrameLayout skips onDraw by default; we need dispatchDraw to fire the ring.
        addView(number)
    }

    fun setDisplayed(value: Int, @ColorInt color: Int) {
        number.text = value.toString()
        number.setTextColor(color)
        ringPaint.color = color
        invalidate()
    }

    override fun dispatchDraw(canvas: Canvas) {
        // Draw ring first, then children, so children render on top if they overlap.
        val inset = ringPaint.strokeWidth / 2f + 8.dp(context).toFloat()
        val cx = width / 2f
        val cy = height / 2f
        val radius = (minOf(width, height) / 2f) - inset
        if (radius > 0f) {
            canvas.drawCircle(cx, cy, radius, ringPaint)
        }
        super.dispatchDraw(canvas)
    }
}
