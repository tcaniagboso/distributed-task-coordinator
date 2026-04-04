#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../serialization/buffer.hpp"

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
    };

    struct WordCountTask {
        std::string words_{};

        WordCountTask() = default;

        WordCountTask(std::string words)
                : words_{std::move(words)} {}
    };

    struct Task {
        uint64_t task_id_;
        uint64_t queued_at_; // When Coordinator assigns task
        uint64_t started_at_; // When Worker starts task
        uint64_t completed_at_; // When Worker completes task

        uint32_t client_id_;

        int router_fd_;

        TaskType type_;
        TaskState state_;

        SyntheticTask synthetic_task_;
        WordCountTask word_count_task_;

        Task(uint64_t task_id, uint32_t client_id, TaskType type)
                : task_id_{task_id},
                  client_id_{client_id},
                  type_{type},
                  state_{TaskState::QUEUED} {}
    };


} // namespace task