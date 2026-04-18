#!/bin/bash

CLIENT_LIST=(1 4 16 64 128 256)

SHARDS=2
WORKERS=4
EXEC_THREADS=4
TASKS=1000
TYPE="mixed"

for c in "${CLIENT_LIST[@]}"; do
  echo "Running client scaling: clients=$c"
  ./run_benchmark.sh "$SHARDS" "$WORKERS" "$EXEC_THREADS" "$c" "$TASKS" "$TYPE" --no-warmup
done