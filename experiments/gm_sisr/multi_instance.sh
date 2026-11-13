#!/usr/bin/env bash
# Multi-instance evaluation protocol for GM-SISR.
# Phase A ("bks")     : long-run reference per instance, all instances in parallel.
# Phase B ("champ")   : $MASK at 60s x N seeds per instance (harness parallelises inside).
# Usage: bash multi_instance.sh bks        [mask]
#        bash multi_instance.sh champ 768  [mask]
set -u

ROOT="Z:/Desktop/250926"
BIN="experiments/gm_sisr/gm_sisr.exe"
HAR="experiments/gm_sisr/fast_parallel_bench.py"
OUT="experiments/gm_sisr/output/multi"

INSTANCES=(
  testcases/benchmark/bench_01_tight_assignment.txt
  testcases/benchmark/bench_02_loose_sequencing.txt
  testcases/benchmark/bench_03_clustered_basins.txt
  testcases/benchmark/bench_04_capacity_bottleneck.txt
  testcases/benchmark/bench_05_asymmetric_random.txt
  testcases/benchmark/bench_06_asymmetric_metric.txt
  testcases/benchmark/bench_07_dense_scale_500.txt
  testcases/phase1_ladder/ladder_03_V350.txt
  testcases/legacy_perturbation/test_500.txt
  testcases/legacy_perturbation/test_850.txt
)

cd "$ROOT" || exit 1
mkdir -p "$OUT"

PHASE="${1:-bks}"
MASK="${2:-768}"

# --- Phase A: internal best-known-so-far from long runs (180s, seeds 1..3, 1 proc/instance) ---
if [ "$PHASE" = "bks" ]; then
  for tc in "${INSTANCES[@]}"; do
    name=$(basename "$tc" .txt)
    (
      best=0
      for s in 1 2 3; do
        o=$("$BIN" 180 "$tc" "$s" 0 "$MASK" 2>&1 >/dev/null | grep -a RESULT | sed 's/.*obj=\([0-9]*\).*/\1/')
        [ -n "$o" ] && [ "$o" -gt "$best" ] 2>/dev/null && best=$o
      done
      echo "BKS $name $best"
    ) > "$OUT/bks_${name}.txt" 2>&1 &
  done
  wait
  grep -h "^BKS" "$OUT"/bks_*.txt | sort -k2 > "$OUT/bks_table.txt"
  cat "$OUT/bks_table.txt"
  exit 0
fi

# --- Phase B: champion comparison at fixed budget ---
for tc in "${INSTANCES[@]}"; do
  name=$(basename "$tc" .txt)
  python "$HAR" --binary "$BIN" --testcase "$tc" --time 60 --n-seeds 6 --seed-start 1 \
      --workers 6 --mask "$MASK" > "$OUT/champ_${name}_m${MASK}.log" 2>&1
  echo "done $name mask=$MASK"
done
echo ALLDONE
