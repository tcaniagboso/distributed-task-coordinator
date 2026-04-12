#pragma once

#include <thread>

#include "types.hpp"
#include "../message/message.hpp"

namespace router {

    class Router {
    private:
        int listen_fd_;
        uint16_t port_;
        std::atomic<bool> running_{false};
        std::vector<RouterShardGroup> shards_;
        std::vector<std::thread> workers_;

        static bool try_connection(rpc::Client& connection, const Endpoint& endpoint, const message::Message& request, message::Message& response);

        void worker(int fd);

    public:
        explicit Router(uint16_t port, std::vector<RouterShardGroup> shards);

        ~Router();

        void stop();

        void run();

    };
} // namespace router