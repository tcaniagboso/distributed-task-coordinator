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

        uint32_t result_{};

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

        void serialize(serialization::BufferWriter &writer) const {
            writer.write_u64(id_);
            writer.write_u8(static_cast<uint8_t>(type_));
            writer.write_u8(static_cast<uint8_t>(state_));
            writer.write_u32(client_id_);
            writer.write_u32(worker_id_);

            writer.write_u64(queued_at_);
            writer.write_u64(started_at_);
            writer.write_u64(completed_at_);

            writer.write_u8(success_);
            writer.write_u32(result_);

            switch (type_) {
                case TaskType::SYNTHETIC:
                    writer.write_u64(synthetic_task_.duration_us_);
                    break;

                case TaskType::WORD_COUNT:
                    writer.write_string(word_count_task_.words_);
                    break;

                default:
                    break;
            }
        }

        void deserialize(serialization::BufferReader &reader) {
            id_ = reader.read_u64();
            type_ = static_cast<TaskType>(reader.read_u8());
            state_ = static_cast<TaskState>(reader.read_u8());
            client_id_ = reader.read_u32();
            worker_id_ = reader.read_u32();

            // Always read all timestamps
            queued_at_ = reader.read_u64();
            started_at_ = reader.read_u64();
            completed_at_ = reader.read_u64();

            success_ = reader.read_u8();
            result_ = reader.read_u32();

            switch (type_) {
                case TaskType::SYNTHETIC:
                    synthetic_task_.duration_us_ = reader.read_u64();
                    break;

                case TaskType::WORD_COUNT:
                    word_count_task_.words_ = reader.read_string();
                    break;

                default:
                    break;
            }
        }
    };
} // namespace task