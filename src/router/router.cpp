#include <iostream>
#include <sys/socket.h>
#include <unistd.h>

#include "../../include/config/system_config.hpp"
#include "../../include/net/net_utils.hpp"
#include "../../include/router/router.hpp"
#include "../../include/rpc/server_connection.hpp"

namespace router {

    Router::Router(uint16_t port, std::vector<RouterShardGroup> shards)
            : listen_fd_{-1},
              port_{port},
              running_{false},
              shards_{std::move(shards)},
              workers_{} {
        workers_.reserve(config::MAX_CLIENTS * 2);
    }

    Router::~Router() {
        stop();
    }

    void Router::stop() {
        running_.store(false, std::memory_order_release);
        if (listen_fd_ >= 0) {
            close(listen_fd_);
        }
        // delay because worker vector is dynamic. what if increases?
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        for (auto &worker: workers_) {
            if (worker.joinable()) worker.join();
        }
    }

    bool Router::try_connection(rpc::Client &connection, const Endpoint &endpoint, const message::Message &request,
                                message::Message &response) {
        if (connection.fd() < 0 && !connection.connect(endpoint.ip_, endpoint.port_)) {
            return false;
        }
        if (connection.send_with_retry(request, config::ROUTER_CONNECTION_RETRY_COUNT) <= 0) {
            if (!connection.connect(endpoint.ip_, endpoint.port_) ||
                connection.send_with_retry(request, config::ROUTER_CONNECTION_RETRY_COUNT) <= 0) {
                connection.close_connection();
                return false;
            }
        }

        if (config::DEBUG) {
            std::cout << "Routed task " << request.submit_.task_id_ << "\n";
        }

        if (connection.receive_with_retry(response, config::ROUTER_CONNECTION_RETRY_COUNT) <= 0) {
//            if (!connection.connect(endpoint.ip_, endpoint.port_)) {
//                return false;
//            }
            connection.close_connection();
            return false;
        }

        if (config::DEBUG) {
            std::cout << "Received result for task" << request.submit_.task_id_ << "\n";
        }

        return true;
    }

    void Router::worker(int fd) {
        rpc::ServerConnection server{fd};
        auto &shards = shards_;
        auto num_shards = shards.size();
        std::vector<ThreadShard> shard_connections;
        shard_connections.reserve(num_shards);

        size_t routing_index = std::hash<std::thread::id>{}(std::this_thread::get_id()) % num_shards;

        for (size_t i = 0; i < num_shards; i++) {
            shard_connections.emplace_back();
        }

        while (running_.load(std::memory_order_acquire)) {
            message::Message request;
            message::Message response;

            if (server.receive_with_retry(request, config::ROUTER_CONNECTION_RETRY_COUNT) <= 0) {
                break;
            }

            bool sent = false;

            for (int retry = 0; retry <= config::RETRY_SHARDS && !sent; retry++) {
                for (size_t attempt = 0; attempt < num_shards && !sent; attempt++) {
                    auto idx = (routing_index + attempt) % num_shards;
                    auto &shard = shards[idx];
                    auto &primary_endpoint = shard.primary_;
                    auto &backup_endpoint = shard.backup_;
                    auto &primary = shard_connections[idx].primary_;
                    auto &backup = shard_connections[idx].backup_;
                    auto current = shard.active_target_.load(std::memory_order_acquire);
                    if (current == ActiveTarget::PRIMARY) {
                        if (config::DEBUG) {
                            std::cout << "[ROUTER] Sending task " << request.submit_.task_id_
                                      << " to PRIMARY\n";
                        }
                        if (!try_connection(primary, primary_endpoint, request, response)) {
                            if (config::DEBUG) {
                                std::cout << "[ROUTER] PRIMARY failed for task "
                                          << request.submit_.task_id_ << "\n";
                                std::cout << "[ROUTER] Trying BACKUP for task "
                                          << request.submit_.task_id_ << "\n";
                            }
                            if (try_connection(backup, backup_endpoint, request, response)) {
                                if (config::DEBUG) {
                                    std::cout << "[ROUTER] BACKUP succeeded for task "
                                              << request.submit_.task_id_ << "\n";
                                }
                                ActiveTarget expected = ActiveTarget::PRIMARY;
                                shard.active_target_.compare_exchange_strong(expected, ActiveTarget::BACKUP);
                                sent = true;
                            }
                        } else {
                            sent = true;
                        }
                    } else {
                        if (config::DEBUG) {
                            std::cout << "[ROUTER] Sending task " << request.submit_.task_id_
                                      << " to BACKUP\n";
                        }
                        if (!try_connection(backup, backup_endpoint, request, response)) {
                            if (config::DEBUG) {
                                std::cout << "[ROUTER] BACKUP failed for task "
                                          << request.submit_.task_id_ << "\n";
                                std::cout << "[ROUTER] Trying PRIMARY for task "
                                          << request.submit_.task_id_ << "\n";
                            }
                            if (try_connection(primary, primary_endpoint, request, response)) {
                                if (config::DEBUG) {
                                    std::cout << "[ROUTER] PRIMARY succeeded for task "
                                              << request.submit_.task_id_ << "\n";
                                }
                                ActiveTarget expected = ActiveTarget::BACKUP;
                                shard.active_target_.compare_exchange_strong(expected, ActiveTarget::PRIMARY);
                                sent = true;
                            }
                        } else {
                            sent = true;
                        }
                    }

                    if (!sent && retry < config::RETRY_SHARDS) {
                        // Give the Backup Coordinator a moment to promote itself
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    }
                }
            }

            if (!sent) {
                message::Message fail_msg{message::MessageType::RESULT};
                fail_msg.result_.task_id_ = request.submit_.task_id_;
                fail_msg.result_.success_ = 0;
                fail_msg.result_.result_ = 0;

                if (server.send_with_retry(fail_msg, config::ROUTER_CONNECTION_RETRY_COUNT) <= 0) {
                    break;
                }
                continue;
            }

            routing_index = (routing_index + 1) % num_shards;

            if (server.send_with_retry(response, config::ROUTER_CONNECTION_RETRY_COUNT) <= 0) {
                break;
            }
        }

        server.close_connection();

        for (auto &connections: shard_connections) {
            connections.close_connections();
        }
    }

    void Router::run() {
        running_.store(true, std::memory_order_release);
        listen_fd_ = net::create_listening_socket(port_, config::BACKLOG);
        if (listen_fd_ < 0) return;

        struct sockaddr_in addr{};
        socklen_t addr_len{sizeof(addr)};

        while (running_.load(std::memory_order_acquire)) {
            addr_len = sizeof(addr);
            int accepted_fd = accept(listen_fd_, reinterpret_cast<sockaddr *>(&addr), &addr_len);
            if (accepted_fd < 0) {
                continue;
            }

            workers_.emplace_back(&Router::worker, this, accepted_fd);
        }

        if (listen_fd_ >= 0) {
            close(listen_fd_);
        }
    }

} // namespace router
