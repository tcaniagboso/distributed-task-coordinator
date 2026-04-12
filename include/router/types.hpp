#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "../rpc/client.hpp"

namespace router {
    enum class ActiveTarget : uint8_t {
        PRIMARY,
        BACKUP
    };

    struct Endpoint {
        uint16_t port_{};
        std::string ip_{};

        explicit Endpoint() = default;
        explicit Endpoint(std::string ip, uint16_t port)
            : port_{port},
              ip_{std::move(ip)} {}
    };

    struct RouterShardGroup {
        Endpoint primary_;
        Endpoint backup_;

        std::atomic<ActiveTarget> active_target_{ActiveTarget::PRIMARY};

        explicit RouterShardGroup(Endpoint  p, Endpoint  b)
                : primary_(std::move(p)), backup_(std::move(b)), active_target_(ActiveTarget::PRIMARY) {}

        RouterShardGroup(const RouterShardGroup&) = delete;
        RouterShardGroup& operator=(const RouterShardGroup&) = delete;

        RouterShardGroup(RouterShardGroup&& other) noexcept
                : primary_(std::move(other.primary_)),
                  backup_(std::move(other.backup_)),
                  active_target_(other.active_target_.load()) {}

        RouterShardGroup& operator=(RouterShardGroup&& other) noexcept {
            if (this != &other) {
                primary_ = std::move(other.primary_);
                backup_ = std::move(other.backup_);
                active_target_.store(other.active_target_.load());
            }
            return *this;
        }
    };

    struct ThreadShard {
        rpc::Client primary_;
        rpc::Client backup_;

        explicit ThreadShard(const Endpoint& p, const Endpoint& b)
                : primary_{p.ip_, p.port_},
                  backup_{b.ip_, b.port_} {}

        void close_connections() {
            primary_.close_connection();
            backup_.close_connection();
        }
    };
} // namespace router