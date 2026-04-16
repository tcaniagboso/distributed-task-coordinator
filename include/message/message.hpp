#pragma once

#include <cstdint>
#include <unordered_map>

#include "../serialization/buffer.hpp"
#include "../task/task.hpp"
#include "../top/types.hpp"

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
        ACKNOWLEDGE,
        SNAPSHOT,
        PEER_RECONNECTED,
        TOP_REQUEST,
        TOP_RESPONSE
    };

    // Client -> Router -> Coordinator
    struct SubmitMsg {
        uint64_t task_id_{};

        uint32_t client_id_{};
        uint32_t duration_us_{};

        task::TaskType type_{};

        std::string words_{};

        explicit SubmitMsg() = default;

        explicit SubmitMsg(uint64_t task_id, uint32_t client_id, task::TaskType type)
                : task_id_{task_id},
                  client_id_{client_id},
                  type_{type} {}

        void serialize(serialization::BufferWriter &writer) const {
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
        uint64_t task_id_{};

        uint32_t duration_us_{};

        task::TaskType type_{};

        std::string words_{};

        explicit AssignMsg() = default;

        explicit AssignMsg(uint64_t task_id, task::TaskType type)
                : task_id_{task_id},
                  type_{type} {}

        void serialize(serialization::BufferWriter &writer) const {
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

        explicit CompleteMsg() = default;

        explicit CompleteMsg(uint64_t task_id, uint64_t started_at, uint64_t completed_at, uint8_t success)
                : task_id_{task_id},
                  started_at_{started_at},
                  completed_at_{completed_at},
                  result_{},
                  success_{success} {}

        void serialize(serialization::BufferWriter &writer) const {
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

        explicit ResultMsg() = default;

        explicit ResultMsg(uint64_t task_id, uint32_t result, uint8_t success)
                : task_id_{task_id},
                  result_{result},
                  success_{success} {}

        void serialize(serialization::BufferWriter &writer) const {
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

    // Primary Coordinator -> Backup Coordinator
    struct AssignedReplicationMsg {
        uint64_t duration_us_{};
        uint64_t task_id_{};
        uint64_t queued_at_{};
        uint64_t started_at_{};

        uint32_t client_id_{};
        uint32_t worker_id_{};

        task::TaskType task_type_{};
        std::string words_{};

        explicit AssignedReplicationMsg() = default;

        explicit AssignedReplicationMsg(uint64_t task_id_, uint64_t queued_at, uint64_t started_at, uint32_t client_id,
                                        uint32_t worker_id,
                                        task::TaskType task_type)
                : task_id_{task_id_},
                  queued_at_{queued_at},
                  started_at_{started_at},
                  client_id_{client_id},
                  worker_id_{worker_id},
                  task_type_{task_type} {}

        void serialize(serialization::BufferWriter &writer) const {
            writer.write_u64(task_id_);
            writer.write_u64(queued_at_);
            writer.write_u64(started_at_);
            writer.write_u32(client_id_);
            writer.write_u32(worker_id_);
            writer.write_u8(static_cast<uint8_t>(task_type_));
            switch (task_type_) {
                case task::TaskType::SYNTHETIC:
                    writer.write_u64(duration_us_);
                    break;
                case task::TaskType::WORD_COUNT:
                    writer.write_string(words_);
                    break;
                default:
                    break;
            }
        }

        void deserialize(serialization::BufferReader &reader) {
            task_id_ = reader.read_u64();
            queued_at_ = reader.read_u64();
            started_at_ = reader.read_u64();
            client_id_ = reader.read_u32();
            worker_id_ = reader.read_u32();
            task_type_ = static_cast<task::TaskType>(reader.read_u8());
            switch (task_type_) {
                case task::TaskType::SYNTHETIC:
                    duration_us_ = reader.read_u64();
                    break;
                case task::TaskType::WORD_COUNT:
                    words_ = reader.read_string();
                    break;
                default:
                    break;
            }
        }
    };

    // Primary Coordinator -> Backup Coordinator
    struct CompletedReplicationMsg {
        uint64_t task_id_;
        uint64_t started_at_;
        uint64_t completed_at_;

        uint32_t worker_id_;
        uint32_t result_;

        uint8_t success_;

        explicit CompletedReplicationMsg() = default;

        explicit CompletedReplicationMsg(uint64_t task_id, uint64_t started_at, uint64_t completed_at,
                                         uint32_t worker_id, uint32_t result, uint8_t success)
                : task_id_{task_id},
                  started_at_{started_at},
                  completed_at_{completed_at},
                  worker_id_{worker_id},
                  result_{result},
                  success_{success} {}

        void serialize(serialization::BufferWriter &writer) const {
            writer.write_u64(task_id_);
            writer.write_u64(started_at_);
            writer.write_u64(completed_at_);
            writer.write_u32(worker_id_);
            writer.write_u32(result_);
            writer.write_u8(success_);
        }

        void deserialize(serialization::BufferReader &reader) {
            task_id_ = reader.read_u64();
            started_at_ = reader.read_u64();
            completed_at_ = reader.read_u64();
            worker_id_ = reader.read_u32();
            result_ = reader.read_u32();
            success_ = reader.read_u8();
        }
    };

    // Worker -> Coordinator
    struct HeartBeatMsg {
        uint32_t worker_id_;

        explicit HeartBeatMsg() = default;

        explicit HeartBeatMsg(uint32_t worker_id)
                : worker_id_{worker_id} {}

        void serialize(serialization::BufferWriter &writer) const {
            writer.write_u32(worker_id_);
        }

        void deserialize(serialization::BufferReader &reader) {
            worker_id_ = reader.read_u32();
        }
    };

    // Coordinator -> Worker
    struct AcknowledgeMsg {
        uint32_t worker_id_;

        explicit AcknowledgeMsg() = default;

        explicit AcknowledgeMsg(uint32_t worker_id)
                : worker_id_{worker_id} {}

        void serialize(serialization::BufferWriter &writer) const {
            writer.write_u32(worker_id_);
        }

        void deserialize(serialization::BufferReader &reader) {
            worker_id_ = reader.read_u32();
        }
    };

    struct SnapshotMsg {
        std::unordered_map<uint64_t, task::Task> tasks_;

        explicit SnapshotMsg() = default;

        explicit SnapshotMsg(const std::unordered_map<uint64_t, task::Task> &tasks)
                : tasks_{tasks} {}

        void serialize(serialization::BufferWriter &writer) const {
            writer.write_u32(tasks_.size());
            for (const auto &task: tasks_) {
                task.second.serialize(writer);
            }
        }

        void deserialize(serialization::BufferReader &reader) {
            uint32_t num_tasks = reader.read_u32();

            tasks_.clear();
            tasks_.reserve(num_tasks);

            for (uint32_t t = 0; t < num_tasks; t++) {
                task::Task task;
                task.deserialize(reader);
                tasks_.emplace(task.id_, std::move(task));
            }
        }
    };

    // For Top command
    struct TopResponseMsg {
        top::CoordinatorMetrics coordinator_metrics_;
        std::vector<top::WorkerMetrics> workers_metrics_;

        explicit TopResponseMsg() = default;

        void serialize(serialization::BufferWriter &writer) const {
            coordinator_metrics_.serialize(writer);

            writer.write_u32(workers_metrics_.size());

            for (const auto& metrics : workers_metrics_) {
                metrics.serialize(writer);
            }
        }

        void deserialize(serialization::BufferReader &reader) {
            coordinator_metrics_.deserialize(reader);

            size_t num_workers = reader.read_u32();

            workers_metrics_.clear();
            workers_metrics_.reserve(num_workers);

            for (uint32_t w = 0; w < num_workers; w++) {
                workers_metrics_.emplace_back();
                workers_metrics_.back().deserialize(reader);
            }
        }
    };

    struct Message {
        uint64_t epoch_{};
        MessageType type_{};
        SubmitMsg submit_{};
        AssignMsg assign_{};
        CompleteMsg complete_{};
        ResultMsg result_{};
        AssignedReplicationMsg assigned_rep_{};
        CompletedReplicationMsg completed_rep_{};
        HeartBeatMsg heartbeat_{};
        AcknowledgeMsg acknowledge_{};
        SnapshotMsg snapshot_{};
        TopResponseMsg top_response_{};

        explicit Message() = default;

        explicit Message(MessageType type) : type_{type} {}

        void serialize(serialization::BufferWriter &writer) const {
            writer.write_u64(epoch_);
            writer.write_u8(static_cast<uint8_t>(type_));

            switch (type_) {
                case MessageType::SUBMIT:
                    submit_.serialize(writer);
                    break;
                case MessageType::ASSIGN:
                    assign_.serialize(writer);
                    break;
                case MessageType::COMPLETE:
                    complete_.serialize(writer);
                    break;
                case MessageType::RESULT:
                    result_.serialize(writer);
                    break;
                case MessageType::ASSIGNED_REPLICATE:
                    assigned_rep_.serialize(writer);
                    break;
                case MessageType::COMPLETED_REPLICATE:
                    completed_rep_.serialize(writer);
                    break;
                case MessageType::HEARTBEAT:
                    heartbeat_.serialize(writer);
                    break;
                case MessageType::ACKNOWLEDGE:
                    acknowledge_.serialize(writer);
                    break;
                case MessageType::SNAPSHOT:
                    snapshot_.serialize(writer);
                    break;
                case MessageType::TOP_RESPONSE:
                    top_response_.serialize(writer);
                    break;
                default:
                    break;
            }
        }

        void deserialize(serialization::BufferReader &reader) {
            epoch_ = reader.read_u64();
            type_ = static_cast<MessageType>(reader.read_u8());

            switch (type_) {
                case MessageType::SUBMIT:
                    submit_.deserialize(reader);
                    break;
                case MessageType::ASSIGN:
                    assign_.deserialize(reader);
                    break;
                case MessageType::COMPLETE:
                    complete_.deserialize(reader);
                    break;
                case MessageType::RESULT:
                    result_.deserialize(reader);
                    break;
                case MessageType::ASSIGNED_REPLICATE:
                    assigned_rep_.deserialize(reader);
                    break;
                case MessageType::COMPLETED_REPLICATE:
                    completed_rep_.deserialize(reader);
                    break;
                case MessageType::HEARTBEAT:
                    heartbeat_.deserialize(reader);
                    break;
                case MessageType::ACKNOWLEDGE:
                    acknowledge_.deserialize(reader);
                    break;
                case MessageType::SNAPSHOT:
                    snapshot_.deserialize(reader);
                    break;
                case MessageType::TOP_RESPONSE:
                    top_response_.deserialize(reader);
                    break;
                default:
                    break;
            }
        }

        bool is_replication() const {
            return type_ == MessageType::ASSIGNED_REPLICATE || type_ == MessageType::COMPLETED_REPLICATE ||
                   type_ == MessageType::SNAPSHOT;
        }
    };

} // namespace message