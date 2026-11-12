package com.enginesense;

/**
 * Host-JVM mirror of the Kotlin EngineSenseNative object (same class name + native signatures), used to exercise
 * the real JNI bridge on a desktop JVM in CI without an Android device. See run.sh.
 */
public class EngineSenseNative {
  static { System.loadLibrary("enginesense_jni"); }

  public static native long nativeCreate();
  public static native void nativeDestroy(long h);
  public static native int nativeStartSimulation(long h, int seed, double speed);
  public static native void nativeStop(long h);
  public static native int nativeInjectScenario(long h, int i);
  public static native double[] nativeSnapshot(long h);
  public static native String[] nativePollEvents(long h, int max);
  public static native String nativeSignalName(int i);
  public static native int nativeScenarioCount();
  public static native String nativeScenarioName(int i);

  public static void main(String[] args) throws Exception {
    long h = nativeCreate();
    check(h != 0, "create");
    check(nativeScenarioCount() >= 10, "scenario count");
    check(nativeStartSimulation(h, 1, 40.0) == 0, "start");
    Thread.sleep(300);
    check(nativeInjectScenario(h, 0) == 0, "inject");  // coolant_overtemp
    boolean raised = false;
    for (int i = 0; i < 100 && !raised; ++i) {
      Thread.sleep(50);
      for (String e : nativePollEvents(h, 32)) raised |= e.startsWith("R|") && e.contains("|OverTemperature|");
    }
    double[] s = nativeSnapshot(h);
    System.out.printf("frames=%.0f %s=%.1f overtemp_raised=%b%n", s[2], nativeSignalName(2), s[6 + 2 * 2], raised);
    check(s[2] > 1000, "frames processed");
    check(raised, "OverTemperature event delivered through JNI");
    nativeDestroy(h);
    System.out.println("JNI host test OK");
  }

  private static void check(boolean ok, String what) {
    if (!ok) { System.err.println("FAILED: " + what); System.exit(1); }
  }
}
