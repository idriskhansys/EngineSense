package com.enginesense

import android.app.Activity
import android.graphics.Typeface
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.ViewGroup
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.Spinner
import android.widget.TextView

/** Minimal companion UI: live signals, bus load, fault log, fault injection. All work happens in the shared C++ core. */
class MainActivity : Activity() {
    private val monitor = EngineMonitor()
    private val ui = Handler(Looper.getMainLooper())
    private lateinit var signals: TextView
    private lateinit var faults: TextView
    private val log = ArrayDeque<String>()

    private val tick = object : Runnable {
        override fun run() {
            val s = monitor.snapshot()
            signals.text = buildString {
                append(String.format("t = %.1f s   bus load %.0f %%   %d frames (%d dropped)   %d active faults\n\n",
                    s.timeS, s.busLoad * 100, s.frames, s.dropped, s.activeFaults))
                s.signals.forEach { sig ->
                    val v = if (sig.status == 1) String.format("%8.2f %s", sig.value, sig.unit) else "      -  "
                    append(String.format("%-16s %s\n", sig.name, v))
                }
            }
            monitor.pollEvents().forEach { line ->
                val p = line.split("|", limit = 5)
                log.addFirst("${if (p[0] == "R") "RAISED " else "cleared"} ${p[3].take(6)} s  ${p[2]}: ${p[4]}")
                while (log.size > 60) log.removeLast()
            }
            faults.text = log.joinToString("\n")
            ui.postDelayed(this, 100)  // 10 Hz poll; the core never blocks the UI thread
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val root = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(24, 24, 24, 24) }
        signals = TextView(this).apply { typeface = Typeface.MONOSPACE; textSize = 13f }
        faults = TextView(this).apply { typeface = Typeface.MONOSPACE; textSize = 11f }
        val spinner = Spinner(this).apply {
            adapter = ArrayAdapter(this@MainActivity, android.R.layout.simple_spinner_dropdown_item, monitor.scenarioNames)
        }
        val inject = Button(this).apply { text = "Inject fault"; setOnClickListener { monitor.inject(spinner.selectedItemPosition) } }
        val restart = Button(this).apply { text = "Restart simulation"; setOnClickListener { log.clear(); monitor.startSimulation(seed = 1, speed = 5.0) } }
        root.addView(signals)
        root.addView(spinner)
        root.addView(inject)
        root.addView(restart)
        root.addView(ScrollView(this).apply { addView(faults) },
            LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f))
        setContentView(root)
        monitor.startSimulation(seed = 1, speed = 5.0)
    }

    override fun onResume() { super.onResume(); ui.post(tick) }
    override fun onPause() { ui.removeCallbacks(tick); super.onPause() }
    override fun onDestroy() { monitor.close(); super.onDestroy() }
}
