package com.sensen.singscoring.demo

import android.os.Bundle
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.sensen.singscoring.SingScoringSession

class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val tv = TextView(this).apply {
            text = "SingScoring SDK ${SingScoringSession.version}\n" +
                   "Phase 0 — scaffolding works."
            textSize = 18f
            setPadding(64, 128, 64, 64)
        }
        setContentView(tv)
    }
}
