#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>

#include "../message/message.hpp"
#include "../rpc/client.hpp"
#include "../utils/utils.hpp"

namespace coordinator {
    struct IncomingEvent {
        int fd_{-1};
        message::Message msg_{};

        explicit IncomingEvent() = default;

        explicit IncomingEvent(int fd, message::Message msg)
                : fd_{fd},
                  msg_{std::move(msg)} {}
    };

    struct OutgoingEvent {
        int fd_{-1};
        message::Message msg_{};

        explicit OutgoingEvent() = default;

        explicit OutgoingEvent(int fd, message::Message msg)
                : fd_{fd},
                  msg_{std::move(msg)} {}
    };

    enum class OperationType: uint8_t {
        QUEUE,
        ASSIGN,
        COMPLETE
    };

    struct Operation {
        uint64_t task_id_{};
        uint64_t queued_at_{};
        uint64_t started_at_{};
        uint64_t completed_at_{};
        uint32_t client_id_{};
        uint32_t worker_id_{};
        uint32_t result_{};

        OperationType type_{};
        task::TaskType task_type_{};

        explicit Operation() = default;

        explicit Operation(uint64_t task_id, OperationType type, task::TaskType task_type)
                : task_id_{task_id},
                  type_{type},
                  task_type_{task_type} {}
    };

    struct TaskStats {
        uint64_t total_completed_count_{0};
        uint64_t total_running_count_{0};
        uint64_t total_queued_count_{0};
        uint64_t word_count_running_count_{0};
        uint64_t synthetic_running_count_{0};
        uint64_t word_count_completed_count_{0};
        uint64_t synthetic_completed_count_{0};
        uint64_t word_count_queued_count_{0};
        uint64_t synthetic_queued_count_{0};

        explicit TaskStats() = default;
    };

    struct WorkerState {
        uint64_t last_heartbeat_ns_{utils::now_ns_u64()};
        TaskStats stats_{};
        int fd_{-1};
        bool alive_{true};
        std::unordered_set<uint64_t> running_tasks_{};

        explicit WorkerState() = default;

        explicit WorkerState(int fd)
                : fd_{fd},
                  alive_{true} {}

        void mark_dead() {
            fd_ = -1;
            alive_ = false;
        }
    };

    struct PeerNode {
        std::string ip_;
        uint16_t port_{};
        bool alive_{false};
        std::unique_ptr<rpc::Client> connection_;

        explicit PeerNode() = default;
        explicit PeerNode(std::string ip, uint16_t port)
            : ip_{std::move(ip)},
              port_{port},
              alive_{false},
              connection_{nullptr} {}
    };
} // namespace coordinator