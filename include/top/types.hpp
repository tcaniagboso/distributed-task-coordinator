#pragma once

#include <cstdint>

#include "../serialization/buffer.hpp"
namespace top {
    struct CoordinatorMetrics {
        uint64_t min_latency_us_{};
        uint64_t max_latency_us_{};
        uint64_t avg_latency_us_{};
        uint64_t p95_latency_us_{};
        uint64_t tasks_completed_{};
        uint64_t tasks_queued_{};
        uint64_t tasks_running_{};
        uint64_t throughput_{};
        uint32_t active_workers_{};
        uint8_t is_primary_{};

        explicit CoordinatorMetrics() = default;

        void serialize(serialization::BufferWriter &writer) const {
            writer.write_u64(min_latency_us_);
            writer.write_u64(max_latency_us_);
            writer.write_u64(avg_latency_us_);
            writer.write_u64(p95_latency_us_);
            writer.write_u64(tasks_completed_);
            writer.write_u64(tasks_queued_);
            writer.write_u64(tasks_running_);
            writer.write_u64(throughput_);
            writer.write_u32(active_workers_);
            writer.write_u8(is_primary_);
        }

        void deserialize(serialization::BufferReader &reader) {
            min_latency_us_ = reader.read_u64();
            max_latency_us_ = reader.read_u64();
            avg_latency_us_ = reader.read_u64();
            p95_latency_us_ = reader.read_u64();
            tasks_completed_ = reader.read_u64();
            tasks_queued_ = reader.read_u64();
            tasks_running_ = reader.read_u64();
            throughput_ = reader.read_u64();
            active_workers_ = reader.read_u32();
            is_primary_ = reader.read_u8();
        }
    };

    struct WorkerMetrics {
        uint64_t avg_latency_us_{};
        uint64_t tasks_completed_{};
        uint64_t tasks_running_{};
        uint64_t last_heartbeat_ms_{};
        uint32_t worker_id_{};
        uint8_t alive_{};

        explicit WorkerMetrics() = default;

        void serialize(serialization::BufferWriter &writer) const {
            writer.write_u64(avg_latency_us_);
            writer.write_u64(tasks_completed_);
            writer.write_u64(tasks_running_);
            writer.write_u64(last_heartbeat_ms_);
            writer.write_u32(worker_id_);
            writer.write_u8(alive_);
        }

        void deserialize(serialization::BufferReader &reader) {
            avg_latency_us_ = reader.read_u64();
            tasks_completed_ = reader.read_u64();
            tasks_running_ = reader.read_u64();
            last_heartbeat_ms_ = reader.read_u64();
            worker_id_ = reader.read_u32();
            alive_ = reader.read_u8();
        }
    };
} // namespace top