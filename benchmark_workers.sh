#!/bin/bash

WORKERS_LIST=(1 2 4 8)

SHARDS=2
EXEC_THREADS=4
CLIENTS=64
TASKS=10000
TYPE="mixed"

for w in "${WORKERS_LIST[@]}"; do
  echo "Running worker scaling: workers=$w"
  ./run_benchmark.sh "$SHARDS" "$w" "$EXEC_THREADS" "$CLIENTS" "$TASKS" "$TYPE"
done