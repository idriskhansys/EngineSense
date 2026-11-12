# Architecture

EngineSense is a **portable C++17 core** with thin platform shells. All decoding, fault logic, simulation and
statistics-relevant behaviour lives in `core/`, which has no UI, OS or Qt dependency (enforced by
`tools/check_core_deps.py`, REQ-ARCH-001).

```
 ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐
 │ Qt 6 desktop │  │ Android app  │  │ iOS app          │
 │ (Windows/    │  │ Kotlin + JNI │  │ UIKit + Obj-C++  │
 │  Linux/macOS)│  │              │  │                  │
 └──────┬───────┘  └──────┬───────┘  └────────┬─────────┘
        │ C++ API         │ C ABI             │ C ABI
        └─────────────────┴───────────────────┘
                          │
              ┌───────────▼────────────┐
              │ c_api.h  (stable C ABI)│
              ├────────────────────────┤
              │ Monitor                │  producer → BoundedQueue → consumer → snapshot
              ├────────────────────────┤
              │ FaultEngine + Rules    │  deterministic, timestamp-driven
              ├────────────────────────┤
              │ Signals / DTC / J1939  │  table-driven codec
              ├────────────────────────┤
              │ Simulator / Scenarios  │  seeded RNG, fault injection
              │ LogIO (candump -L)     │
              └────────────────────────┘
```

## Layers

| Layer | Files | Responsibility |
|---|---|---|
| Frame & protocol | `can_frame`, `j1939` | 29-bit identifier split (PDU1/PDU2), PGN constants, nominal periods |
| Codec | `signals`, `dtc` | Table-driven `SignalDef` (SPN, start bit, length, scale, offset, valid range); not-available / error classification; DM1 lamp + DTC parsing |
| Detection | `fault`, `rules`, `bus_load`, `fault_engine` | Rule interface, built-in rules, DTC lifecycle, consequential-fault suppression |
| Simulation | `simulator`, `scenarios` | Seeded splitmix64 + Box-Muller RNG (identical streams on every platform), nominal traffic, 11 injectable scenarios with latency budgets |
| I/O | `log_io` | SocketCAN `candump -L` read/write, malformed lines counted not fatal |
| Concurrency | `bounded_queue`, `monitor` | Source thread → bounded queue → engine thread → mutex-guarded snapshot for the UI |
| ABI | `c_api.h/.cpp` | Opaque handle, plain structs, no exceptions across the boundary; used by JNI and Obj-C++ |

## Key design decisions

1. **Time comes from the frames, not the clock.** The engine evaluates on a 10 ms grid driven by frame
   timestamps. Replaying a log, running the simulator at 100x and running live all give identical results,
   which is what makes golden-log regression tests and the Monte-Carlo benchmarks possible.
2. **N-consecutive-sample confirmation instead of time debounce.** A threshold rule needs N bad samples in a
   row. This is independent of bus rate, and the latency/glitch trade-off is directly measurable
   (`docs/figures/confirmation_tradeoff.png`).
3. **Hysteresis and a clear timer** on every condition so a signal hovering at a limit does not chatter.
4. **Stale-PGN rules use 3x the nominal period**, and are suppressed while the bus is overloaded
   (a loaded bus delays frames; it should raise `BusOverload`, not a cascade of dropouts).
   Bus load is measured over two windows: 1 s (reported) and 50 ms (fast enough to lead dropout detection).
5. **Rules are an interface** (`Rule::evaluate(EvalContext&)`); adding a rule does not touch the engine.
6. **Back-pressure vs. drops is explicit.** Replay blocks the producer when the queue is full; live mode drops
   and *counts*. The invariant `processed + dropped == sent` is tested.
7. **C ABI, not C++ ABI, across languages.** One ~80-line header is the entire surface the JNI and Obj-C++
   wrappers depend on. The JNI layer is exercised on a real JVM in CI (`platform/android/host-test`).

## Data flow (live monitor)

1. `Monitor::start` spawns a producer (simulator or log replay) pushing `CanFrame`s into `BoundedQueue`.
2. The consumer thread pops frames, decodes signals, advances `FaultEngine`, updates the snapshot under a
   mutex and appends fault events to a ring.
3. The UI polls `snapshot()` / `poll_event()` at 30 Hz; it never touches the queue or the engine.

## Verification architecture

Requirements (`docs/requirements.md`) -> tests tagged `// Verifies: REQ-...` -> `tools/trace.py` generates
`docs/traceability.md` and fails if a requirement has no test. CI jobs carry the same tags for requirements
that are verified by the pipeline itself (sanitizers, static analysis, cross-platform builds).
