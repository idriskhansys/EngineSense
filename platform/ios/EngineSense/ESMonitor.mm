#import "ESMonitor.h"

#include "enginesense/c_api.h"

@interface ESSignal ()
- (instancetype)initWithName:(NSString *)name unit:(NSString *)unit value:(double)value status:(ESSignalStatus)status;
@end
@implementation ESSignal
- (instancetype)initWithName:(NSString *)name unit:(NSString *)unit value:(double)value status:(ESSignalStatus)status {
  if ((self = [super init])) {
    _name = [name copy];
    _unit = [unit copy];
    _value = value;
    _status = status;
  }
  return self;
}
@end

@interface ESFault ()
- (instancetype)initWithNative:(const es_fault &)f cleared:(BOOL)cleared;
@end
@implementation ESFault
- (instancetype)initWithNative:(const es_fault &)f cleared:(BOOL)cleared {
  if ((self = [super init])) {
    _faultId = f.id;
    _typeName = [NSString stringWithUTF8String:f.type_name] ?: @"";
    _severity = f.severity;
    _active = f.active != 0;
    _cleared = cleared;
    _timeSeconds = (double)(cleared ? f.cleared_us : f.raised_us) / 1e6;
    _message = [NSString stringWithUTF8String:f.message] ?: @"";
  }
  return self;
}
@end

@interface ESSnapshot ()
- (instancetype)initWithTime:(NSTimeInterval)t busLoad:(double)load frames:(uint64_t)frames dropped:(uint64_t)dropped
                     running:(BOOL)running faults:(NSInteger)faults signals:(NSArray<ESSignal *> *)signals;
@end
@implementation ESSnapshot
- (instancetype)initWithTime:(NSTimeInterval)t busLoad:(double)load frames:(uint64_t)frames dropped:(uint64_t)dropped
                     running:(BOOL)running faults:(NSInteger)faults signals:(NSArray<ESSignal *> *)signals {
  if ((self = [super init])) {
    _timeSeconds = t;
    _busLoad = load;
    _framesProcessed = frames;
    _framesDropped = dropped;
    _running = running;
    _activeFaultCount = faults;
    _signals = [signals copy];
  }
  return self;
}
@end

@implementation ESMonitor {
  es_monitor *_handle;
}

+ (NSString *)version {
  return [NSString stringWithUTF8String:es_version()];
}

- (instancetype)init {
  if ((self = [super init])) {
    _handle = es_create();
    if (_handle == nullptr) return nil;
  }
  return self;
}

- (void)dealloc {
  es_destroy(_handle);  // stops worker threads and joins them
}

- (NSArray<NSString *> *)scenarioNames {
  NSMutableArray<NSString *> *names = [NSMutableArray array];
  for (int32_t i = 0; i < es_scenario_count(); ++i) [names addObject:[NSString stringWithUTF8String:es_scenario_name(i)]];
  return names;
}

- (BOOL)startSimulationWithSeed:(uint32_t)seed speed:(double)speed {
  return es_start_simulation(_handle, seed, speed) == 0;
}

- (BOOL)startReplayOfLogAtPath:(NSString *)path speed:(double)speed {
  return es_start_replay(_handle, path.fileSystemRepresentation, speed) == 0;
}

- (void)stop {
  es_stop(_handle);
}

- (BOOL)injectScenarioAtIndex:(NSInteger)index {
  return es_inject_scenario(_handle, (int32_t)index) == 0;
}

- (ESSnapshot *)snapshot {
  es_snapshot s;
  es_get_snapshot(_handle, &s);
  NSMutableArray<ESSignal *> *sigs = [NSMutableArray arrayWithCapacity:ES_SIGNAL_COUNT];
  for (int32_t i = 0; i < ES_SIGNAL_COUNT; ++i) {
    [sigs addObject:[[ESSignal alloc] initWithName:[NSString stringWithUTF8String:es_signal_name(i)]
                                              unit:[NSString stringWithUTF8String:es_signal_unit(i)]
                                             value:s.signals[i].value
                                            status:(ESSignalStatus)s.signals[i].status]];
  }
  return [[ESSnapshot alloc] initWithTime:(double)s.time_us / 1e6
                                  busLoad:s.bus_load
                                   frames:s.frames_processed
                                  dropped:s.frames_dropped
                                  running:s.running != 0
                                   faults:s.active_fault_count
                                  signals:sigs];
}

- (NSArray<ESFault *> *)activeFaults {
  es_fault buf[32];
  const int32_t n = es_get_active_faults(_handle, buf, 32);
  NSMutableArray<ESFault *> *out = [NSMutableArray arrayWithCapacity:(NSUInteger)n];
  for (int32_t i = 0; i < n; ++i) [out addObject:[[ESFault alloc] initWithNative:buf[i] cleared:NO]];
  return out;
}

- (NSArray<ESFault *> *)pollEvents {
  NSMutableArray<ESFault *> *out = [NSMutableArray array];
  es_fault f;
  int32_t cleared = 0;
  while (es_poll_event(_handle, &f, &cleared) != 0) [out addObject:[[ESFault alloc] initWithNative:f cleared:cleared != 0]];
  return out;
}

@end
