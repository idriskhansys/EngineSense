# Requirements

Every requirement is **measurable** and has a **verification method** that runs in CI. Requirement IDs are
referenced from the tests (`// Verifies: REQ-...`); `tools/trace.py` builds [traceability.md](traceability.md)
from those tags and fails the build if any requirement below has no test.

Targets are for the *simulated* J1939 bus described in [architecture.md](architecture.md): 250 kbit/s, SAE J1939
nominal message rates, seeded Gaussian sensor noise.

## Decoding & logging

| ID | Requirement | Acceptance criterion |
|----|-------------|----------------------|
| REQ-DEC-001 | Decode SPNs of EEC1, ET1, EFL/P1 and VEP1 per J1939-71 scaling. | Exact match on hand-computed reference vectors; encode->decode error <= 0.5 LSB over the full valid range of every signal. |
| REQ-DEC-002 | Classify "not available" (0xFF..) and "error" (0xFE.., 0xFB..0xFD) indicators. | All 8/16-bit boundary values classified correctly; never reported as a measurement. |
| REQ-DEC-003 | Split a 29-bit identifier into priority, PGN, source and destination for PDU1 and PDU2. | Known vectors correct; `make_id(parse_id(x)) == x` for 20 000 random identifiers. |
| REQ-DTC-001 | Parse DM1 lamp status and DTC fields (19-bit SPN, FMI, OC, CM). | Reference frames incl. SPN > 65 535; build/parse round-trip on 5 000 random DTCs. |
| REQ-LOG-001 | Read and write SocketCAN `candump -L` logs losslessly; tolerate malformed input. | Round-trip equality on simulated traffic; malformed lines counted, never fatal. |

## Fault-detection latency (measured from injection start, 25 seeds in CI, 200 in `bench`)

| ID | Requirement | Acceptance criterion |
|----|-------------|----------------------|
| REQ-LAT-001 | Coolant over-temperature (+30 degC step) is detected. | <= 2.5 s on every seed. |
| REQ-LAT-002 | Loss of the 10 ms engine-speed message (EEC1) is detected. | <= 100 ms on every seed. |
| REQ-LAT-003 | Loss of the 1 s temperature message (ET1) is detected. | <= 3.5 s on every seed. |
| REQ-LAT-004 | CAN bus overload (95 % load) is detected. | <= 2.0 s on every seed. |
| REQ-LAT-005 | All other injected faults (low oil pressure, over-speed, low battery, signal error flag, out-of-range sensor, implausible rate, ECU DTC) are detected. | Per-scenario budget in `core/src/scenarios.cpp` (0.5-2.5 s) on every seed. |

## Detection quality

| ID | Requirement | Acceptance criterion |
|----|-------------|----------------------|
| REQ-ACC-001 | No false alarms on fault-free traffic. | 0 alarms in 100 simulated minutes (CI test) and 1 000 simulated minutes (`bench nominal`), also with 2x sensor noise. |
| REQ-ACC-002 | Every injected fault of nominal magnitude is detected. | Detection rate 100 % for each scenario (25 seeds CI, 200 `bench`). |
| REQ-ACC-003 | Faults are attributed correctly. | No fault type other than the injected one (plus documented legitimate secondaries) is raised in any trial. |

## Fault-engine behaviour

| ID | Requirement | Acceptance criterion |
|----|-------------|----------------------|
| REQ-FDE-001 | Limit violations are debounced by consecutive-sample confirmation and use hysteresis. | A single out-of-limit sample never raises; a fault inside the hysteresis band neither clears nor re-triggers; clears only after 2 s below the band. |
| REQ-FDE-002 | Consequential dropout faults are suppressed while the bus is overloaded. | 0 `SensorDropout` faults in 25 bus-overload trials. |
| REQ-FDE-003 | ECU-reported DTCs are tracked through their lifecycle. | Raised once; cleared when DM1 reports none or refresh stops for 3 s. |
| REQ-DET-001 | Processing is deterministic. | Same seed -> identical frames; same frames -> identical fault report (byte-identical CSV). |

## Performance & robustness

| ID | Requirement | Acceptance criterion |
|----|-------------|----------------------|
| REQ-PERF-001 | Detection core throughput. | >= 100 000 frames/s single-threaded (Release); relaxed to 5 000 in Debug/sanitizer builds. (Real J1939 bus: <= ~1 800 frames/s.) |
| REQ-PERF-002 | UI responsiveness: reading the monitor state never waits for detection work. | `snapshot()` + history copy p99 <= 5 ms (Release) / 16 ms (Debug) while the engine is saturated (a 60 Hz UI frame is 16.7 ms). |
| REQ-PERF-003 | No silent frame loss. | Replay with back-pressure: processed == sent, dropped == 0. Real-time mode: processed + dropped == sent. |
| REQ-ROB-001 | Malformed input never crashes or triggers UB. | 300 000 random frames, all DLC/PGN combinations, random log text, timestamp jumps: pass under ASan + UBSan. |
| REQ-SIM-001 | Simulator produces realistic traffic. | Per-PGN rate within 5 % of J1939 nominal; nominal bus load 3-12 %; nominal signals stay in the operating envelope for 10 min. |
| REQ-SIM-002 | Fault injection has the documented frame-level effect. | Step/set/ramp/error/dropout/overload/DTC injections verified on decoded frames. |

## Architecture & process

| ID | Requirement | Acceptance criterion |
|----|-------------|----------------------|
| REQ-ARCH-001 | The core is portable and free of UI/platform dependencies. | Builds and passes tests on GCC, Clang, MSVC (Linux/Windows/macOS CI matrix); links only the C++ standard library + pthreads. |
| REQ-ARCH-002 | A stable C ABI is the only surface used by the Android and iOS apps. | `c_api.h` exercised by tests incl. NULL/range safety; JNI and Objective-C++ layers call nothing else. |
| REQ-ARCH-003 | Detection rules can be added without modifying the engine. | A user-defined `Rule` subclass is raised through `FaultEngine::add_rule`. |
| REQ-QUAL-001 | Quality gates on every push. | CI runs unit/integration tests, clang-tidy (warnings as errors), cppcheck, ASan+UBSan, TSan, traceability check. |
| REQ-QUAL-002 | Behaviour changes are visible. | Golden-log regression test; any change in the fault report fails CI until the baseline is deliberately regenerated. |
