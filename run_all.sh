#!/bin/bash

set -e

echo "=================================="
echo "Preparing Benchmark Environment"
echo "=================================="

# Make scripts executable
chmod +x run_benchmark.sh
chmod +x benchmark_shards.sh
chmod +x benchmark_workers.sh
chmod +x benchmark_clients.sh
chmod +x benchmark_workload.sh

echo ""
echo "=================================="
echo "Running ALL Benchmark Experiments"
echo "=================================="

RESULT_FILE="results.csv"
rm -f "$RESULT_FILE"

echo ""
echo "----- Shard Scaling -----"
./benchmark_shards.sh

echo ""
echo "----- Worker Scaling -----"
./benchmark_workers.sh

echo ""
echo "----- Client Scaling -----"
./benchmark_clients.sh

echo ""
echo "----- Workload Comparison -----"
./benchmark_workload.sh

echo ""
echo "=================================="
echo "All benchmarks completed"
echo "Results saved to $RESULT_FILE"
echo "=================================="