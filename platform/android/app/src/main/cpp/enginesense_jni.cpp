// JNI bridge: Kotlin <-> stable C ABI (enginesense/c_api.h). No C++ core types cross this file.
#include <jni.h>

#include <algorithm>
#include <string>
#include <vector>

#include "enginesense/c_api.h"

namespace {
es_monitor* handle(jlong h) { return reinterpret_cast<es_monitor*>(h); }  // NOLINT
}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL Java_com_enginesense_EngineSenseNative_nativeCreate(JNIEnv*, jclass) {
  return reinterpret_cast<jlong>(es_create());  // NOLINT
}

JNIEXPORT void JNICALL Java_com_enginesense_EngineSenseNative_nativeDestroy(JNIEnv*, jclass, jlong h) {
  es_destroy(handle(h));
}

JNIEXPORT jint JNICALL Java_com_enginesense_EngineSenseNative_nativeStartSimulation(JNIEnv*, jclass, jlong h, jint seed,
                                                                                    jdouble speed) {
  return es_start_simulation(handle(h), static_cast<uint32_t>(seed), speed);
}

JNIEXPORT jint JNICALL Java_com_enginesense_EngineSenseNative_nativeStartReplay(JNIEnv* env, jclass, jlong h, jstring path,
                                                                                jdouble speed) {
  const char* p = env->GetStringUTFChars(path, nullptr);
  if (p == nullptr) return -1;
  const int rc = es_start_replay(handle(h), p, speed);
  env->ReleaseStringUTFChars(path, p);
  return rc;
}

JNIEXPORT void JNICALL Java_com_enginesense_EngineSenseNative_nativeStop(JNIEnv*, jclass, jlong h) { es_stop(handle(h)); }

JNIEXPORT jint JNICALL Java_com_enginesense_EngineSenseNative_nativeInjectScenario(JNIEnv*, jclass, jlong h, jint index) {
  return es_inject_scenario(handle(h), index);
}

/// Layout: [time_s, bus_load, frames, dropped, running, faultCount, (value, status) x ES_SIGNAL_COUNT]
JNIEXPORT jdoubleArray JNICALL Java_com_enginesense_EngineSenseNative_nativeSnapshot(JNIEnv* env, jclass, jlong h) {
  es_snapshot s;
  es_get_snapshot(handle(h), &s);
  std::vector<jdouble> v = {static_cast<double>(s.time_us) / 1e6, s.bus_load, static_cast<double>(s.frames_processed),
                            static_cast<double>(s.frames_dropped), static_cast<double>(s.running),
                            static_cast<double>(s.active_fault_count)};
  for (const es_signal& sig : s.signals) {
    v.push_back(sig.value);
    v.push_back(static_cast<double>(sig.status));
  }
  jdoubleArray out = env->NewDoubleArray(static_cast<jsize>(v.size()));
  if (out != nullptr) env->SetDoubleArrayRegion(out, 0, static_cast<jsize>(v.size()), v.data());
  return out;
}

/// Pops pending notifications as "R|severity|type|time_s|message" / "C|..." strings.
JNIEXPORT jobjectArray JNICALL Java_com_enginesense_EngineSenseNative_nativePollEvents(JNIEnv* env, jclass, jlong h,
                                                                                         jint max_events) {
  std::vector<std::string> lines;
  es_fault f;
  int32_t cleared = 0;
  while (static_cast<jint>(lines.size()) < max_events && es_poll_event(handle(h), &f, &cleared) != 0) {
    const double t = static_cast<double>(cleared != 0 ? f.cleared_us : f.raised_us) / 1e6;
    lines.push_back(std::string(cleared != 0 ? "C|" : "R|") + std::to_string(f.severity) + "|" + f.type_name + "|" +
                    std::to_string(t) + "|" + f.message);
  }
  jclass str = env->FindClass("java/lang/String");
  jobjectArray out = env->NewObjectArray(static_cast<jsize>(lines.size()), str, nullptr);
  for (std::size_t i = 0; i < lines.size(); ++i) {
    jstring js = env->NewStringUTF(lines[i].c_str());
    env->SetObjectArrayElement(out, static_cast<jsize>(i), js);
    env->DeleteLocalRef(js);
  }
  return out;
}

JNIEXPORT jstring JNICALL Java_com_enginesense_EngineSenseNative_nativeSignalName(JNIEnv* env, jclass, jint i) {
  return env->NewStringUTF(es_signal_name(i));
}
JNIEXPORT jstring JNICALL Java_com_enginesense_EngineSenseNative_nativeSignalUnit(JNIEnv* env, jclass, jint i) {
  return env->NewStringUTF(es_signal_unit(i));
}
JNIEXPORT jint JNICALL Java_com_enginesense_EngineSenseNative_nativeScenarioCount(JNIEnv*, jclass) {
  return es_scenario_count();
}
JNIEXPORT jstring JNICALL Java_com_enginesense_EngineSenseNative_nativeScenarioName(JNIEnv* env, jclass, jint i) {
  return env->NewStringUTF(es_scenario_name(i));
}

}  // extern "C"
