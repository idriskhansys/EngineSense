// Objective-C facade over the C++ core's stable C ABI (enginesense/c_api.h).
// Foundation only, so it can be used from Objective-C, Swift (via bridging header) and tests.
#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, ESSignalStatus) {
  ESSignalStatusUnknown = 0,
  ESSignalStatusValid = 1,
  ESSignalStatusNotAvailable = 2,
  ESSignalStatusError = 3,
};

@interface ESSignal : NSObject
@property(nonatomic, readonly, copy) NSString *name;
@property(nonatomic, readonly, copy) NSString *unit;
@property(nonatomic, readonly) double value;
@property(nonatomic, readonly) ESSignalStatus status;
@end

@interface ESFault : NSObject
@property(nonatomic, readonly) uint64_t faultId;
@property(nonatomic, readonly, copy) NSString *typeName;
@property(nonatomic, readonly) NSInteger severity;  // 0 info, 1 warning, 2 critical
@property(nonatomic, readonly) BOOL active;
@property(nonatomic, readonly) BOOL cleared;        // a "cleared" notification
@property(nonatomic, readonly) NSTimeInterval timeSeconds;
@property(nonatomic, readonly, copy) NSString *message;
@end

@interface ESSnapshot : NSObject
@property(nonatomic, readonly) NSTimeInterval timeSeconds;
@property(nonatomic, readonly) double busLoad;  // 0...1
@property(nonatomic, readonly) uint64_t framesProcessed;
@property(nonatomic, readonly) uint64_t framesDropped;
@property(nonatomic, readonly) BOOL running;
@property(nonatomic, readonly) NSInteger activeFaultCount;
@property(nonatomic, readonly, copy) NSArray<ESSignal *> *signals;
@end

/// Owns one native monitor. Use from one thread at a time (normally the main thread).
@interface ESMonitor : NSObject
@property(class, nonatomic, readonly, copy) NSString *version;
@property(nonatomic, readonly, copy) NSArray<NSString *> *scenarioNames;

- (BOOL)startSimulationWithSeed:(uint32_t)seed speed:(double)speed;
- (BOOL)startReplayOfLogAtPath:(NSString *)path speed:(double)speed;
- (void)stop;
- (BOOL)injectScenarioAtIndex:(NSInteger)index;

- (ESSnapshot *)snapshot;
- (NSArray<ESFault *> *)activeFaults;
/// Pops raised/cleared notifications accumulated since the last call.
- (NSArray<ESFault *> *)pollEvents;
@end

NS_ASSUME_NONNULL_END
