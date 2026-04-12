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

    struct WorkerState {
        uint64_t last_heartbeat_ns_{utils::now_ns_u64()};
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