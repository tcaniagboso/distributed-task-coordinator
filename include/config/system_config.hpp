#pragma once
#include <cstdint>

namespace config {
    // Worker config
    constexpr size_t NUM_WORKER_THREADS = 4;
    constexpr size_t WORKER_QUEUE_CAPACITY = 64;
    constexpr uint8_t RETRY_COUNT = 2;
    constexpr uint64_t HEARTBEAT_INTERVAL_NS = 1000000000ULL;

    // Coordinator Config
    constexpr size_t COORDINATOR_QUEUE_CAPACITY = 1024;
    constexpr size_t MAX_QUEUE_DEPTH = NUM_WORKER_THREADS;
    constexpr uint64_t HEARTBEAT_TIMEOUT_NS  = 5000000000ULL;
    constexpr uint64_t SWEEP_INTERVAL_NS = 1000000000ULL;
    constexpr size_t MAX_LOG_SIZE = 10000;

    // Sockets config
    constexpr size_t BACKLOG = 256;
} // namespace config