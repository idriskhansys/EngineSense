#!/usr/bin/env sh
# Regenerate the regression baseline after an *intentional* detection-behaviour change.
# Review the diff of tests/data/golden_mixed_faults.csv before committing it.
set -eu
CLI="${1:-build/apps/cli/enginesense_cli}"
"$CLI" simulate --seed 7 --duration 30 \
  --inject coolant_overtemp@8 --inject ecu_dtc@14 --inject et1_dropout@20 \
  --out tests/data/golden_mixed.log --faults tests/data/golden_mixed_faults.csv
