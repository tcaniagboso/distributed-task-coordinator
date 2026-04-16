#!/bin/bash

TYPES=("synthetic" "word" "mixed")

SHARDS=2
WORKERS=4
CLIENTS=64
TASKS=10000

for t in "${TYPES[@]}"; do
  echo "Running workload: type=$t"
  ./run_benchmark.sh "$SHARDS" "$WORKERS" "$CLIENTS" "$TASKS" "$t" --no-warmup
done