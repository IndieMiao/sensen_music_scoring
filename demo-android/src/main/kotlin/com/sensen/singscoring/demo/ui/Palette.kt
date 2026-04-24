package com.sensen.singscoring.demo.ui

import android.content.Context

object Palette {
    // Backgrounds
    const val BG: Int = 0xFF121212.toInt()           // screen
    const val SURFACE: Int = 0xFF181818.toInt()      // rows / cards
    const val SURFACE_ELEVATED: Int = 0xFF282828.toInt()  // search bar container

    // Text
    const val TEXT_PRIMARY: Int = 0xFFFFFFFF.toInt()
    const val TEXT_SECONDARY: Int = 0xFFB3B3B3.toInt()
    const val TEXT_TERTIARY: Int = 0xFF6A6A6A.toInt()   // footer

    // Accents
    const val ACCENT: Int = 0xFF1DB954.toInt()        // Spotify green — pass, primary CTA
    const val FAIL: Int = 0xFFF15E6C.toInt()          // result screen fail color only
    const val OUTLINE: Int = 0xFF535353.toInt()       // outlined-button stroke
}

fun Int.dp(context: Context): Int =
    (this * context.resources.displayMetrics.density + 0.5f).toInt()
