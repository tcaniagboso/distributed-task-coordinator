#pragma once

#include <cstdint>

#include "../buffer/buffer.hpp"
#include "../task/task.hpp"

namespace message {
    enum class MessageType : uint8_t {
        SUBMIT,
        ASSIGN,
        COMPLETE,
        RESULT,
        ASSIGNED_REPLICATE,
        COMPLETED_REPLICATE,
        HEARTBEAT,
        REGISTER,
    };

    // Client -> Router -> Coordinator
    struct SubmitMsg {
        uint64_t task_id_;

        uint32_t client_id_;
        uint32_t duration_us_;

        task::TaskType type_;

        std::string words_{};

        SubmitMsg() = default;

        SubmitMsg(uint64_t task_id, uint32_t client_id, task::TaskType type)
                : task_id_{task_id},
                  client_id_{client_id},
                  type_{type} {}

        void serialize(serialization::BufferWriter &writer) {
            writer.write_u64(task_id_);
            writer.write_u32(client_id_);
            writer.write_u8(static_cast<uint8_t>(type_));
            if (type_ == task::TaskType::WORD_COUNT) {
                writer.write_string(words_);
            } else {
                writer.write_u32(duration_us_);
            }
        }

        void deserialize(serialization::BufferReader &reader) {
            task_id_ = reader.read_u64();
            client_id_ = reader.read_u32();
            type_ = static_cast<task::TaskType>(reader.read_u8());
            if (type_ == task::TaskType::WORD_COUNT) {
                words_ = reader.read_string();
            } else {
                duration_us_ = reader.read_u32();
            }
        }
    };

    // Coordinator -> Worker
    struct AssignMsg {
        uint64_t task_id_;

        uint32_t duration_us_;

        task::TaskType type_;

        std::string words_{};

        AssignMsg() = default;

        AssignMsg(uint64_t task_id, task::TaskType type)
                : task_id_{task_id},
                  type_{type} {}

        void serialize(serialization::BufferWriter &writer) {
            writer.write_u64(task_id_);
            writer.write_u8(static_cast<uint8_t>(type_));
            if (type_ == task::TaskType::WORD_COUNT) {
                writer.write_string(words_);
            } else {
                writer.write_u32(duration_us_);
            }
        }

        void deserialize(serialization::BufferReader &reader) {
            task_id_ = reader.read_u64();
            type_ = static_cast<task::TaskType>(reader.read_u8());
            if (type_ == task::TaskType::WORD_COUNT) {
                words_ = reader.read_string();
            } else {
                duration_us_ = reader.read_u32();
            }
        }
    };

    // Worker -> Coordinator
    struct CompleteMsg {
        uint64_t task_id_;
        uint64_t started_at_;
        uint64_t completed_at_;
        uint32_t result_; // only for word count for word_count tasks

        uint8_t success_;

        CompleteMsg() = default;

        CompleteMsg(uint64_t task_id, uint64_t started_at, uint64_t completed_at, uint8_t success)
                : task_id_{task_id},
                  started_at_{started_at},
                  completed_at_{completed_at},
                  success_{success} {}

        void serialize(serialization::BufferWriter &writer) {
            writer.write_u64(task_id_);
            writer.write_u64(started_at_);
            writer.write_u64(completed_at_);
            writer.write_u32(result_);
            writer.write_u8(success_);
        }

        void deserialize(serialization::BufferReader &reader) {
            task_id_ = reader.read_u64();
            started_at_ = reader.read_u64();
            completed_at_ = reader.read_u64();
            result_ = reader.read_u32();
            success_ = reader.read_u8();
        }

    };

    // Coordinator -> Router -> Client
    struct ResultMsg {
        uint64_t task_id_;

        uint32_t result_;

        uint8_t success_;

        ResultMsg() = default;

        ResultMsg(uint64_t task_id, uint32_t result, uint8_t success)
                : task_id_{task_id},
                  result_{result},
                  success_{success} {}

        void serialize(serialization::BufferWriter &writer) {
            writer.write_u64(task_id_);
            writer.write_u32(result_);
            writer.write_u8(success_);
        }

        void deserialize(serialization::BufferReader &reader) {
            task_id_ = reader.read_u64();
            result_ = reader.read_u32();
            success_ = reader.read_u8();
        }
    };

    struct AssignedReplicationMsg {
        uint64_t task_id_;
        uint64_t queued_at_;

        uint32_t worker_id_;

        AssignedReplicationMsg() = default;

        AssignedReplicationMsg(uint64_t task_id_, uint64_t queued_at, uint32_t worker_id)
                : task_id_{task_id_},
                  queued_at_{queued_at},
                  worker_id_{worker_id} {}

        void serialize(serialization::BufferWriter &writer) {
            writer.write_u64(task_id_);
            writer.write_u64(queued_at_);
            writer.write_u32(worker_id_);
        }

        void deserialize(serialization::BufferReader &reader) {
            task_id_ = reader.read_u64();
            queued_at_ = reader.read_u64();
            worker_id_ = reader.read_u32();
        }
    };

    struct CompletedReplicationMsg {
        uint64_t task_id_;
        uint64_t started_at_;
        uint64_t completed_at_;

        uint32_t worker_id_;

        CompletedReplicationMsg() = default;

        CompletedReplicationMsg(uint64_t task_id, uint64_t started_at, uint64_t completed_at, uint32_t worker_id)
                : task_id_{task_id},
                  started_at_{started_at},
                  completed_at_{completed_at},
                  worker_id_{worker_id} {}

        void serialize(serialization::BufferWriter &writer) {
            writer.write_u64(task_id_);
            writer.write_u64(started_at_);
            writer.write_u64(completed_at_);
            writer.write_u32(worker_id_);
        }

        void deserialize(serialization::BufferReader &reader) {
            task_id_ = reader.read_u64();
            started_at_ = reader.read_u64();
            completed_at_ = reader.read_u64();
            worker_id_ = reader.read_u32();
        }
    };

    struct HeartBeatMsg {
        uint32_t worker_id_;

        HeartBeatMsg() = default;

        HeartBeatMsg(uint32_t worker_id)
                : worker_id_{worker_id} {}

        void serialize(serialization::BufferWriter &writer) {
            writer.write_u32(worker_id_);
        }

        void deserialize(serialization::BufferReader &reader) {
            worker_id_ = reader.read_u32();
        }
    };

    struct Message {
        MessageType type;

        int router_fd_;

        SubmitMsg submit;
        AssignMsg assign;
        CompleteMsg complete;
        ResultMsg result;
        AssignedReplicationMsg assigned_rep;
        CompletedReplicationMsg completed_rep;
        HeartBeatMsg heartbeat;
    };

} // namespace message