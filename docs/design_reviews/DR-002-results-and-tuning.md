# DR-002 - Verification results and threshold tuning

**Inputs:** `docs/results.md` (200 seeds/scenario, 1000 fault-free minutes, noise sweep), FMEA, CI status.

## Findings

| # | Observation | Assessment | Action |
|---|---|---|---|
| 1 | All 11 scenarios detected 200/200 within budget. Tightest Cpk is 1.1 (coolant over-temperature, battery low, ECU DTC). | Passes but with thin margin: confirmation count x sample period dominates the budget. | Keep budgets; revisit if confirmation counts rise. |
| 2 | 0 false alarms in 1000 simulated minutes (rule-of-three bound 0.18/h). | Meets the false-alarm requirement for simulated noise only. | Real-bus validation out of scope (simulated data). |
| 3 | Rate-of-change rules start false-alarming at ~4x nominal sensor noise; threshold rules do not. | Real weakness (FM-15, open). The rate limits are only ~10 sigma above noise. | Proposed: derive rate from a low-pass/regression slope over N samples, or scale limit with estimated noise. Not implemented. |
| 4 | Fuel-pressure saturation was initially undetectable (plausibility limit equalled full scale). | Fixed: plausible high limit 800 kPa. | Regression test added. |
| 5 | Throughput ~2.4 M frames/s on the build host vs ~105 frames/s of simulated nominal traffic (>20 000x headroom). | Large headroom. | none |

## Decision
Accept v1.0 with FM-14 and FM-15 open and documented in `technical_report.md`.

## Limits of this review
Self-review. Metrics are for a *simulated* bus with Gaussian noise; real sensors have drift, quantisation and
non-Gaussian outliers that would change false-alarm figures.
