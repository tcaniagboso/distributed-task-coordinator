#pragma once

#include <cstdint>
#include <iostream>

namespace client {
    enum class TextMode : uint8_t {
        CLEAN,
        NOISY
    };

    enum class WorkloadSize : uint8_t {
        SHORT,
        MEDIUM,
        LONG
    };

    enum class RequestType : uint8_t {
        SYNTHETIC = 1,
        WORD_COUNT = 2,
        MIXED = 3
    };

    struct Stats {
        uint64_t completed_;
        uint64_t max_latency_us_;
        uint64_t min_latency_us_;
        uint64_t p50_latency_us_;
        uint64_t p95_latency_us_;
        double avg_latency_us_;
        double throughput_ops_;

        static void print(const Stats &stats) {
            std::cout << "Tasks Completed: " << stats.completed_ << '\n';
            std::cout << "Throughput (tasks/sec): " << stats.throughput_ops_ << '\n';
            std::cout << "Avg latency (us): " << stats.avg_latency_us_ << '\n';
            std::cout << "p50 latency (us): " << stats.p50_latency_us_ << '\n';
            std::cout << "p95 latency (us): " << stats.p95_latency_us_ << '\n';
            std::cout << "Min latency (us): " << stats.min_latency_us_ << '\n';
            std::cout << "Max latency (us): " << stats.max_latency_us_ << '\n';
        }
    };
} // namespace client