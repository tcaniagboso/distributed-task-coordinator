#pragma once

#include <cstdint>
#include <limits>
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

    struct WorkerState {
        uint64_t tasks_completed_{0};
        uint64_t total_latency_ns_{0};
        uint64_t last_heartbeat_ns_{utils::now_ns_u64()};
        size_t active_index_{};
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

    struct Metrics {
        uint64_t total_latency_ns{0};
        uint64_t tasks_completed_{0};
        uint64_t tasks_queued_{0};
        uint64_t tasks_running_{0};
        uint64_t min_latency_ns{0};
        uint64_t max_latency_ns{0};

        std::deque<uint64_t> latencies_{};
        std::deque<uint64_t> completion_times_ns_{};

        explicit Metrics() = default;
    };
} // namespace coordinator