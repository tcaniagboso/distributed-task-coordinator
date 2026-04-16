#pragma once
#include <cstdint>

namespace config {
    // Worker config
    constexpr size_t MAX_WORKER_THREADS = 8;
    constexpr size_t WORKER_QUEUE_CAPACITY = 64;
    constexpr uint64_t HEARTBEAT_INTERVAL_NS = 200000000ULL;  // 200ms
    constexpr size_t WORKER_OUT_BUDGET = 128;

    // Retry Config
    constexpr uint32_t QUEUE_RETRY_COUNT = 10;
    constexpr uint32_t CONNECTION_RETRY_COUNT = 5;
    constexpr uint32_t REGISTER_RETRY_COUNT = 10;

    // Coordinator Config
    constexpr size_t COORDINATOR_CONTROL_QUEUE_CAPACITY = 64;
    constexpr size_t CONTROL_BUDGET = COORDINATOR_CONTROL_QUEUE_CAPACITY / 2;
    constexpr size_t COORDINATOR_TASK_QUEUE_CAPACITY = 16384;
    constexpr size_t TASK_FALLBACK_BUDGET = 1024;
    constexpr size_t COORDINATOR_OUTGOING_QUEUE_CAPACITY = 2 * COORDINATOR_TASK_QUEUE_CAPACITY;
    constexpr size_t COORDINATOR_OUT_BUDGET = 1024;
    constexpr size_t WORKER_MAX_QUEUE_DEPTH = WORKER_QUEUE_CAPACITY;
    constexpr uint64_t HEARTBEAT_TIMEOUT_NS  = 1000000000ULL; // 1s
    constexpr uint64_t SWEEP_INTERVAL_NS     = 200000000ULL;  // 200ms
    constexpr size_t MAX_LOG_SIZE = 10000;
    constexpr uint64_t THROUGHPUT_WINDOW_NS = 1000000000ULL;
    constexpr size_t MAX_LATENCY_SAMPLES = 10000;

    // Sockets config
    constexpr size_t BACKLOG = 256;

    // Word count sizes (number of words)
    constexpr size_t SHORT_TEXT_WORDS  = 10;
    constexpr size_t MEDIUM_TEXT_WORDS = 100;
    constexpr size_t LONG_TEXT_WORDS   = 1000;

    // Synthetic durations (microseconds)
    constexpr uint64_t SHORT_DURATION_US  = 100;
    constexpr uint64_t MEDIUM_DURATION_US = 1000;
    constexpr uint64_t LONG_DURATION_US   = 5000;

    // Other
    constexpr uint32_t MAX_BACKOFF_US = 100;
    constexpr size_t MAX_CLIENTS = 256;

    // Debug
    constexpr bool DEBUG = false;
} // namespace config