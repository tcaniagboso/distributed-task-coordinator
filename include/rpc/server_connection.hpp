#pragma once

#include "../message/message.hpp"

namespace rpc {

    class ServerConnection {
    private:
        int fd_;

    public:
        explicit ServerConnection(int fd);

        ~ServerConnection();

        void close_connection();

        int send(const message::Message &msg) const;

        int receive(message::Message &msg) const;

        int send_with_retry(const message::Message& msg, uint32_t retries) const;

        int receive_with_retry(message::Message& msg, uint32_t retries) const;
    };
} // namespace rpc