#!/bin/bash

# =========================
# Usage:
# ./run_benchmark.sh <shards> <workers_per_shard> <clients> <total_tasks> <task_type>
# Example:
# ./run_benchmark.sh 2 4 64 100000 mixed
# =========================

set -e

# =========================
# Help / Usage
# =========================
if [[ "$1" == "-h" || "$1" == "--help" ]]; then
  echo "Usage:"
  echo "  ./run_benchmark.sh <shards> <workers_per_shard> <clients> <total_tasks> <task_type>"
  echo ""
  echo "Arguments:"
  echo "  shards               Number of shards (each has primary + backup)"
  echo "  workers_per_shard    Number of workers per shard"
  echo "  clients              Number of client threads"
  echo "  total_tasks          Total tasks across all clients"
  echo "  task_type            synthetic | word | mixed"
  echo "  --no-warmup          Skip warm-up run (faster, less accurate)"
  echo ""
  echo "Example:"
  echo "  ./run_benchmark.sh 2 4 64 100000 mixed"
  echo ""
  echo "Description:"
  echo "  Starts coordinators, router, and workers, runs the client,"
  echo "  and reports throughput and latency metrics."
  exit 0
fi

if [ $# -lt 5 ] || [ $# -gt 6 ]; then
  echo "Error: Invalid number of arguments."
  echo "Run './run_benchmark.sh -h' for usage."
  exit 1
fi


SHARDS=$1
WORKERS=$2
CLIENTS=$3
TOTAL_TASKS=$4
TASK_TYPE=$5

SKIP_WARMUP=0

SKIP_WARMUP=0

if [ $# -eq 6 ]; then
  if [[ "$6" == "--no-warmup" ]]; then
    SKIP_WARMUP=1
  else
    echo "Error: Unknown option '$6'"
    echo "Run './run_benchmark.sh -h' for usage."
    exit 1
  fi
fi

BASE_COORD_PORT=7000
ROUTER_PORT=9000
RESULT_FILE="results.csv"

# =========================
# Cleanup
# =========================
pre_cleanup() {
  pkill coordinator 2>/dev/null || true
  pkill worker 2>/dev/null || true
  pkill router 2>/dev/null || true
}

cleanup() {
  echo "Cleaning up processes..."

  for pid in "${PRIMARY_PIDS[@]}"; do
    kill -9 "$pid" 2>/dev/null || true
  done

  for pid in "${BACKUP_PIDS[@]}"; do
    kill -9 "$pid" 2>/dev/null || true
  done

  for pid in "${WORKER_PIDS[@]}"; do
    kill -9 "$pid" 2>/dev/null || true
  done

  kill -9 "$ROUTER_PID" 2>/dev/null || true
}

pre_cleanup
sleep 1

echo ""
echo "=============================="
echo "Running Benchmark"
echo "Shards=$SHARDS Workers/Shard=$WORKERS Clients=$CLIENTS Tasks=$TOTAL_TASKS Type=$TASK_TYPE"
echo "=============================="

# =========================
# Start Coordinators
# =========================
PRIMARY_PIDS=()
BACKUP_PIDS=()
SHARD_ADDRS=()

for ((i=0; i<$SHARDS; i++)); do
  PRIMARY_PORT=$((BASE_COORD_PORT + i*2))
  BACKUP_PORT=$((PRIMARY_PORT + 1))

  PRIMARY_ADDR="127.0.0.1:$PRIMARY_PORT"
  BACKUP_ADDR="127.0.0.1:$BACKUP_PORT"

  SHARD_ADDRS+=("$PRIMARY_ADDR,$BACKUP_ADDR")

  ./coordinator -p $PRIMARY_PORT --peer $BACKUP_ADDR &
  PRIMARY_PID=$!
  PRIMARY_PIDS+=($PRIMARY_PID)

  ./coordinator -p $BACKUP_PORT --peer $PRIMARY_ADDR &
  BACKUP_PID=$!
  BACKUP_PIDS+=($BACKUP_PID)
done

sleep 1

# =========================
# Start Router
# =========================
ROUTER_CMD=(./router -p "$ROUTER_PORT")

for shard in "${SHARD_ADDRS[@]}"; do
  ROUTER_CMD+=(-s "$shard")
done

"${ROUTER_CMD[@]}" &
ROUTER_PID=$!

sleep 1

# =========================
# Start Workers
# =========================
WORKER_PIDS=()

for ((i=0; i<$SHARDS; i++)); do
  PRIMARY_PORT=$((BASE_COORD_PORT + i*2))

  for ((j=0; j<$WORKERS; j++)); do
    ./worker -p $PRIMARY_PORT -w 1 &
    WORKER_PIDS+=($!)
  done
done

sleep 1

# =========================
# Print System Layout
# =========================
echo ""
echo "=============================="
echo "SYSTEM LAYOUT"
echo "=============================="

for ((i=0; i<$SHARDS; i++)); do
  PRIMARY_PORT=$((BASE_COORD_PORT + i*2))
  BACKUP_PORT=$((PRIMARY_PORT + 1))

  echo "Shard $i:"
  echo "  Primary   → 127.0.0.1:$PRIMARY_PORT | PID=${PRIMARY_PIDS[$i]}"
  echo "  Secondary → 127.0.0.1:$BACKUP_PORT | PID=${BACKUP_PIDS[$i]}"
done

echo ""
echo "Router:"
echo "  → 127.0.0.1:$ROUTER_PORT | PID=$ROUTER_PID"

echo ""
echo "Workers:"
for ((i=0; i<${#WORKER_PIDS[@]}; i++)); do
  echo "  Worker $i → PID=${WORKER_PIDS[$i]}"
done

echo ""
echo "To kill a process: kill -9 <PID>"
echo ""

# =========================
# Compute tasks per client
# =========================
echo "Tasks per client: $TOTAL_TASKS"

# =========================
# Warm-up run
# =========================
if [ "$SKIP_WARMUP" -eq 0 ]; then
  echo "Running warm-up..."
  ./client -p $ROUTER_PORT -c $CLIENTS -n $TOTAL_TASKS -t $TASK_TYPE >/dev/null 2>&1 || true
  sleep 1
else
  echo "Skipping warm-up..."
fi

# =========================
# Timed Run
# =========================
echo "Running measured benchmark..."

START=$(date +%s%N)

CLIENT_OUTPUT=$(./client -p $ROUTER_PORT -c $CLIENTS -n $TOTAL_TASKS -t $TASK_TYPE)

END=$(date +%s%N)

DURATION_NS=$((END - START))
DURATION_MS=$((DURATION_NS / 1000000))

echo ""
echo "=============================="
echo "CLIENT OUTPUT"
echo "=============================="
echo "$CLIENT_OUTPUT"

# =========================
# Extract throughput
# =========================
THROUGHPUT=$(echo "$CLIENT_OUTPUT" | grep "Throughput" | awk '{print $3}')
AVG_LAT=$(echo "$CLIENT_OUTPUT" | grep "Avg latency" | awk '{print $4}')
P95=$(echo "$CLIENT_OUTPUT" | grep "p95 latency" | awk '{print $4}')

echo ""
echo "=============================="
echo "RESULT SUMMARY"
echo "=============================="
echo "Duration (ms): $DURATION_MS"
echo "Throughput (tasks/sec): $THROUGHPUT"

# =========================
# Save CSV
# =========================
if [ ! -f "$RESULT_FILE" ]; then
  echo "shards,workers_per_shard,clients,total_tasks,type,duration_ms,throughput" > $RESULT_FILE
fi

echo "$SHARDS,$WORKERS,$CLIENTS,$TOTAL_TASKS,$TASK_TYPE,$THROUGHPUT,$AVG_LAT,$P95,$DURATION_MS" >> $RESULT_FILE

echo ""
echo "Saved to $RESULT_FILE"
echo "=============================="

trap cleanup EXIT
cleanup
sleep 1