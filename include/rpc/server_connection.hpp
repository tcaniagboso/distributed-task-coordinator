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

        bool send(const message::Message &msg) const;

        bool receive(message::Message &msg) const;
    };
} // namespace rpc