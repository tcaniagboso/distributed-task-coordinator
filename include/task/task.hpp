#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../serialization/buffer.hpp"
#include "../utils/utils.hpp"

namespace task {

    enum class TaskType : uint8_t {
        SYNTHETIC,
        WORD_COUNT
    };

    enum class TaskState : uint8_t {
        QUEUED,
        RUNNING,
        COMPLETED
    };

    struct SyntheticTask {
        uint32_t duration_us_;

        explicit SyntheticTask() = default;

        explicit SyntheticTask(uint32_t duration_us)
                : duration_us_{duration_us} {}
    };

    struct WordCountTask {
        std::string words_{};
        uint32_t result_{};

        explicit WordCountTask() = default;

        explicit WordCountTask(std::string words)
                : words_{std::move(words)} {}
    };

    struct Task {
        uint64_t id_{};
        uint64_t queued_at_{}; // When Coordinator creates task
        uint64_t started_at_{}; // When Worker starts task
        uint64_t completed_at_{}; // When Worker completes task

        uint32_t client_id_{};
        uint32_t worker_id_{};

        int router_fd_{};

        uint8_t success_{};

        TaskType type_{};
        TaskState state_{};

        SyntheticTask synthetic_task_{};
        WordCountTask word_count_task_{};

        explicit Task() = default;

        explicit Task(uint64_t id, uint32_t client_id, TaskType type)
                : id_{id},
                  queued_at_{utils::now_ns_u64()},
                  client_id_{client_id},
                  type_{type},
                  state_{TaskState::QUEUED} {}
    };


} // namespace task