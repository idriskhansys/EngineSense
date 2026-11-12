# Technical Report - EngineSense v1.0

## 1. Summary
EngineSense decodes (simulated) SAE J1939 telemetry from a CAN 2.0B bus, detects faults with a deterministic
rule engine, and presents them in a Windows/Linux desktop monitor plus Android and iOS shells that share one
C++17 core. The project is verified against 28 measurable requirements (`requirements.md`), each traced to
tests (`traceability.md`), with FMEA-driven fault injection and statistical analysis of detection performance
(`results.md`).

**Scope limit:** all traffic is simulated (seeded Gaussian noise on nominal J1939 message rates) or replayed
from logs. No claims are made about performance on a real vehicle bus.

## 2. Method
1. Requirements first, each with a numeric acceptance criterion and a verification that runs in CI.
2. FMEA (15 failure modes, RPN = S x O x D) to choose which faults to inject and which rules to build.
3. Fault-injection simulator: 11 scenarios, each with a latency budget, seeded for reproducibility.
4. Monte-Carlo benchmark (200 seeds/scenario, 1000 fault-free minutes, noise/threshold/confirmation sweeps).
5. Quality gates: `-Werror` warnings, clang-tidy, cppcheck, ASan/UBSan, TSan, golden-log regression.

## 3. Results (full detail in `results.md`)
- **Detection:** 200/200 detections in every scenario, all within budget; one-sided 95 % lower bound on
  detection rate 98.5 %. Tightest process capability Cpk = 1.1 (over-temperature, battery, ECU DTC).
- **False alarms:** 0 in 1000 simulated minutes (95 % upper bound 0.18/h by the rule of three).
- **Throughput:** ~2.4 M frames/s single-threaded (Release, build host) against ~105 frames/s of nominal traffic.
- **Tests:** 105 tests, all passing on Linux (GCC, Release; also under ASan/UBSan and TSan during development).

## 4. Issues found, root causes and corrective actions {#lessons-learned}

| # | Issue | Root cause | Corrective action |
|---|---|---|---|
| 1 | EEC1 *SensorDropout* raised before *BusOverload* on a saturated bus | Overload delays frames; the 1 s load window reacted too slowly | Added 50 ms load window; stale rules suppressed under load; regression test (REQ-FDE-002) |
| 2 | Bogus dropouts at the end of log replay | Replay flushed the engine to a time past the last frame | `flush_us` defaults to 0 |
| 3 | Saturated fuel-pressure sensor undetectable | Plausible-range upper limit equalled full scale, so saturation was "plausible" | Plausible high limit 800 kPa; scenario magnitude 1000 kPa |
| 4 | Flaky real-time drop-count test under TSan | Test depended on wall-clock scheduling | Deterministic consumer delay hook and invariant `processed + dropped == sent` |
| 5 | Qt build failure | Qt `signals` keyword macro clashes with core identifiers | `QT_NO_KEYWORDS`, `Q_SLOTS` |
| 6 | Noise sweep overstated false alarms | Experiment counted all rules for a threshold study | Split by fault type; exposed issue 7 |
| 7 | Rate-of-change rules false-alarm at >= 4x noise | Rate limits only ~10 sigma above nominal noise | **Open (FM-15).** Proposed: slope over N samples or noise-scaled limit |
| 8 | Static-analysis findings (unused variable, identical switch branches, float loop counter, by-value param, memset on floats) | Normal drift during fast development | Fixed; analysers now gate CI |
| 9 | Test-expectation mistakes (frame counts, stale-PGN assumptions) | Wrong mental model of simulated traffic volume | Tests rewritten around measured rates |

### Lessons learned
- Making time an *input* (frame timestamps) rather than a clock read was the single most useful decision:
  it made fault logic, replay and Monte-Carlo studies deterministic.
- Measuring the system found a weakness (issue 7) that unit tests did not. Statistical experiments belong in
  the verification plan, not just as reporting.
- Consequential-fault suppression matters for operators as much as correctness: the root cause must not be
  buried among its symptoms.
- Traceability tooling that *fails the build* keeps requirements honest as the code changes.

## 5. Known limitations
- **TP.BAM / multi-frame DM1** is not decoded; only the first DTC of a single-frame DM1 is visible (FM-14).
- **Rate-rule noise sensitivity** (FM-15, above).
- **Simulated data only**; sensor drift, quantisation, and non-Gaussian outliers are not modelled.
- FMEA severity/occurrence/detection scores are author estimates, not field data.
- iOS wrapper is built in CI on macOS; it was not compiled in the authoring environment.

## 6. Roadmap
Transport protocol (TP.BAM/RTS-CTS), noise-adaptive rate rules, real CAN adapter input (SocketCAN, PCAN),
DM2/DM11 and freeze-frame data, persisted logs in the UI.
