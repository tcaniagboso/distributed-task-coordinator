#pragma once

#include <cstdint>
#include <string>

#include "../message/message.hpp"

namespace rpc {
    class Client {
    private:
        int sock_fd_;

    public:
        explicit Client(const std::string &ip, uint16_t port);

        ~Client();

        void close_connection();

        bool connect(const std::string& ip, uint16_t port);
        bool send(const message::Message &msg) const;

        bool receive(message::Message &msg) const;

        int fd() const;
    };
} // namespace rpc