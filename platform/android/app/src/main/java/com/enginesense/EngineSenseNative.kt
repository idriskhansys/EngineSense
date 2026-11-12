package com.enginesense

/** Thin Kotlin facade over the C++ core (via JNI -> c_api.h). */
object EngineSenseNative {
    init { System.loadLibrary("enginesense_jni") }

    @JvmStatic external fun nativeCreate(): Long
    @JvmStatic external fun nativeDestroy(handle: Long)
    @JvmStatic external fun nativeStartSimulation(handle: Long, seed: Int, speed: Double): Int
    @JvmStatic external fun nativeStartReplay(handle: Long, path: String, speed: Double): Int
    @JvmStatic external fun nativeStop(handle: Long)
    @JvmStatic external fun nativeInjectScenario(handle: Long, index: Int): Int
    @JvmStatic external fun nativeSnapshot(handle: Long): DoubleArray
    @JvmStatic external fun nativePollEvents(handle: Long, maxEvents: Int): Array<String>
    @JvmStatic external fun nativeSignalName(index: Int): String
    @JvmStatic external fun nativeSignalUnit(index: Int): String
    @JvmStatic external fun nativeScenarioCount(): Int
    @JvmStatic external fun nativeScenarioName(index: Int): String
}

/** Idiomatic wrapper owning one native monitor. Call [close] from onDestroy. */
class EngineMonitor : AutoCloseable {
    private val handle = EngineSenseNative.nativeCreate()

    val signalCount = 8
    val scenarioNames: List<String> by lazy { (0 until EngineSenseNative.nativeScenarioCount()).map { EngineSenseNative.nativeScenarioName(it) } }

    fun startSimulation(seed: Int = 1, speed: Double = 1.0) = EngineSenseNative.nativeStartSimulation(handle, seed, speed) == 0
    fun startReplay(path: String, speed: Double = 1.0) = EngineSenseNative.nativeStartReplay(handle, path, speed) == 0
    fun stop() = EngineSenseNative.nativeStop(handle)
    fun inject(scenarioIndex: Int) = EngineSenseNative.nativeInjectScenario(handle, scenarioIndex) == 0

    data class Signal(val name: String, val unit: String, val value: Double, val status: Int)
    data class Snapshot(val timeS: Double, val busLoad: Double, val frames: Long, val dropped: Long,
                        val running: Boolean, val activeFaults: Int, val signals: List<Signal>)

    fun snapshot(): Snapshot {
        val a = EngineSenseNative.nativeSnapshot(handle)
        val sigs = (0 until signalCount).map {
            Signal(EngineSenseNative.nativeSignalName(it), EngineSenseNative.nativeSignalUnit(it), a[6 + 2 * it], a[7 + 2 * it].toInt())
        }
        return Snapshot(a[0], a[1], a[2].toLong(), a[3].toLong(), a[4] != 0.0, a[5].toInt(), sigs)
    }

    /** Raw event lines "R|severity|type|time_s|message" (R = raised, C = cleared). */
    fun pollEvents(max: Int = 64): List<String> = EngineSenseNative.nativePollEvents(handle, max).toList()

    override fun close() { stop(); EngineSenseNative.nativeDestroy(handle) }
}
