#!/bin/bash

SHARDS_LIST=(1 2 4 8)

WORKERS=4
EXEC_THREADS=4
CLIENTS=64
TASKS=10000
TYPE="mixed"

for s in "${SHARDS_LIST[@]}"; do
  echo "Running shard scaling: shards=$s"
  ./run_benchmark.sh "$s" "$WORKERS" "$EXEC_THREADS" "$CLIENTS" "$TASKS" "$TYPE"
done