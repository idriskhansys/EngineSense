# DR-001 - Architecture and interfaces

**Scope:** layering, C ABI, threading model, requirement set. **Author/reviewer:** project author (self-review
using a checklist; no external reviewer was available - see "Limits of this review").

## Checklist and outcome

| # | Question | Finding | Action |
|---|---|---|---|
| 1 | Does the core build without any UI/OS dependency? | Yes; enforced by `check_core_deps.py` and a CI test. | none |
| 2 | Is the C ABI free of C++ types and exceptions? | Yes; opaque handle, POD structs, error codes. | none |
| 3 | Can detection be tested deterministically? | Only if time is not read from a clock. Initial design used a time debounce. | **Changed:** timestamp-driven evaluation + N-sample confirmation (REQ-DET-001). |
| 4 | What happens when the consumer is slower than the producer? | Unbounded queue would hide overload. | **Changed:** bounded queue; replay applies back-pressure, live counts drops (REQ-PERF-002). |
| 5 | Is every requirement measurable? | Three were adjectives ("fast", "robust"). | **Changed:** rewritten with numeric acceptance criteria (see `requirements.md`). |
| 6 | Are cascading faults handled? | First prototype raised EEC1 SensorDropout before BusOverload on a saturated bus. | **Changed:** 50 ms bus-load window + stale suppression (FM-03, FM-13). |
| 7 | Multi-frame J1939 transport? | TP.BAM not supported. | **Accepted limitation** (FM-14, documented). |

## Decisions recorded
- Keep detection as a rule list rather than a state machine: easier to extend and to tune per rule.
- Keep UI code out of `core/` even where Qt would be convenient (e.g. `QTimer`); the shell polls.

## Limits of this review
Single reviewer who is also the author; severity/occurrence/detection scores in the FMEA are the author's
engineering judgement, not field data. A real review should have a second engineer re-score them.
