#import "ESViewController.h"

#import "ESMonitor.h"

@implementation ESViewController {
  ESMonitor *_monitor;
  UILabel *_signals;
  UITextView *_log;
  UIPickerView *_picker;
  CADisplayLink *_link;
  NSMutableString *_logText;
  NSArray<NSString *> *_scenarios;
}

- (void)viewDidLoad {
  [super viewDidLoad];
  self.view.backgroundColor = UIColor.systemBackgroundColor;
  _monitor = [[ESMonitor alloc] init];
  _scenarios = _monitor.scenarioNames;
  _logText = [NSMutableString string];

  _signals = [[UILabel alloc] init];
  _signals.numberOfLines = 0;
  _signals.font = [UIFont monospacedSystemFontOfSize:13 weight:UIFontWeightRegular];
  _picker = [[UIPickerView alloc] init];
  _picker.dataSource = (id<UIPickerViewDataSource>)self;
  _picker.delegate = (id<UIPickerViewDelegate>)self;
  UIButton *inject = [UIButton buttonWithType:UIButtonTypeSystem];
  [inject setTitle:@"Inject fault" forState:UIControlStateNormal];
  [inject addTarget:self action:@selector(injectTapped) forControlEvents:UIControlEventTouchUpInside];
  _log = [[UITextView alloc] init];
  _log.editable = NO;
  _log.font = [UIFont monospacedSystemFontOfSize:11 weight:UIFontWeightRegular];

  UIStackView *stack = [[UIStackView alloc] initWithArrangedSubviews:@[ _signals, _picker, inject, _log ]];
  stack.axis = UILayoutConstraintAxisVertical;
  stack.spacing = 8;
  stack.translatesAutoresizingMaskIntoConstraints = NO;
  [self.view addSubview:stack];
  UILayoutGuide *g = self.view.safeAreaLayoutGuide;
  [NSLayoutConstraint activateConstraints:@[
    [stack.leadingAnchor constraintEqualToAnchor:g.leadingAnchor constant:12],
    [stack.trailingAnchor constraintEqualToAnchor:g.trailingAnchor constant:-12],
    [stack.topAnchor constraintEqualToAnchor:g.topAnchor constant:12],
    [stack.bottomAnchor constraintEqualToAnchor:g.bottomAnchor constant:-12],
    [_picker.heightAnchor constraintEqualToConstant:110],
  ]];

  [_monitor startSimulationWithSeed:1 speed:5.0];
  _link = [CADisplayLink displayLinkWithTarget:self selector:@selector(refresh)];
  _link.preferredFramesPerSecond = 10;  // the core never blocks the main thread
  [_link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
}

- (void)dealloc {
  [_link invalidate];
}

- (void)injectTapped {
  [_monitor injectScenarioAtIndex:[_picker selectedRowInComponent:0]];
}

- (void)refresh {
  ESSnapshot *s = _monitor.snapshot;
  NSMutableString *t = [NSMutableString stringWithFormat:@"t = %.1f s   bus load %.0f %%\n%llu frames (%llu dropped)   %ld active faults\n\n",
                                                          s.timeSeconds, s.busLoad * 100, s.framesProcessed, s.framesDropped, (long)s.activeFaultCount];
  for (ESSignal *sig in s.signals) {
    if (sig.status == ESSignalStatusValid) [t appendFormat:@"%-16@ %8.2f %@\n", sig.name, sig.value, sig.unit];
    else [t appendFormat:@"%-16@        -\n", sig.name];
  }
  _signals.text = t;
  for (ESFault *f in _monitor.pollEvents) {
    [_logText insertString:[NSString stringWithFormat:@"%@ %7.2f s %@: %@\n", f.cleared ? @"cleared" : @"RAISED ", f.timeSeconds, f.typeName, f.message]
                   atIndex:0];
  }
  _log.text = _logText;
}

#pragma mark - UIPickerView

- (NSInteger)numberOfComponentsInPickerView:(UIPickerView *)pickerView {
  return 1;
}
- (NSInteger)pickerView:(UIPickerView *)pickerView numberOfRowsInComponent:(NSInteger)component {
  return (NSInteger)_scenarios.count;
}
- (NSString *)pickerView:(UIPickerView *)pickerView titleForRow:(NSInteger)row forComponent:(NSInteger)component {
  return _scenarios[(NSUInteger)row];
}

@end
