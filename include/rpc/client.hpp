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

        int send(const message::Message &msg) const;

        int receive(message::Message &msg) const;

        int send_with_retry(const message::Message& msg, uint32_t retries) const;

        int receive_with_retry(message::Message& msg, uint32_t retries) const;

        int fd() const;
    };
} // namespace rpc