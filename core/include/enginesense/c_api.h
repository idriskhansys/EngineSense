/* Stable C ABI over the C++ core. This is the only surface the Android (JNI) and iOS
 * (Objective-C++) wrappers depend on, so the C++ internals can change freely.
 *
 * Threading: an es_monitor handle may be used from one thread at a time (typically the UI
 * thread). The monitor's own worker threads are internal. */
#ifndef ENGINESENSE_C_API_H
#define ENGINESENSE_C_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ES_SIGNAL_COUNT 8
#define ES_MESSAGE_LEN 160

typedef struct es_monitor es_monitor;

typedef struct {
  double value;
  int32_t status; /* 0 unknown, 1 valid, 2 not available, 3 error */
} es_signal;

typedef struct {
  uint64_t time_us;
  double bus_load; /* 0..1 */
  uint64_t frames_processed;
  uint64_t frames_dropped;
  int32_t running;
  int32_t active_fault_count;
  es_signal signals[ES_SIGNAL_COUNT];
} es_snapshot;

typedef struct {
  uint64_t id;
  int32_t type;     /* FaultType enum value */
  int32_t severity; /* 0 info, 1 warning, 2 critical */
  int32_t active;
  uint64_t raised_us;
  uint64_t cleared_us;
  double value;
  char type_name[32];
  char message[ES_MESSAGE_LEN];
} es_fault;

const char* es_version(void);

es_monitor* es_create(void);
void es_destroy(es_monitor* m);

/* speed: 1.0 real time, 0 = unthrottled. Returns 0 on success. */
int es_start_simulation(es_monitor* m, uint32_t seed, double speed);
/* Replay a candump-format log. Returns 0 on success, -1 if the file cannot be read. */
int es_start_replay(es_monitor* m, const char* path, double speed);
void es_stop(es_monitor* m);

/* Inject standard scenario `index` into a running simulation. Returns 0, or -1 for bad index. */
int es_inject_scenario(es_monitor* m, int32_t index);

void es_get_snapshot(es_monitor* m, es_snapshot* out);
/* Copies up to `capacity` active faults; returns the number copied. */
int32_t es_get_active_faults(es_monitor* m, es_fault* out, int32_t capacity);
/* Pops the next raised/cleared notification. Returns 1 and fills *out and *cleared (0 raised,
 * 1 cleared), or 0 if none is pending. */
int32_t es_poll_event(es_monitor* m, es_fault* out, int32_t* cleared);
/* Copies up to `max_points` recent samples (decimated to >= 100 ms) of a signal; returns count. */
int32_t es_get_history(es_monitor* m, int32_t signal, double* t_s, double* values, int32_t max_points);

const char* es_signal_name(int32_t signal);
const char* es_signal_unit(int32_t signal);
int32_t es_scenario_count(void);
const char* es_scenario_name(int32_t index);
const char* es_scenario_description(int32_t index);

#ifdef __cplusplus
}
#endif
#endif
