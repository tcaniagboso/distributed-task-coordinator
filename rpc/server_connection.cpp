#include <unistd.h>

#include "server_connection.hpp"
#include "../net/net_utils.hpp"

namespace rpc {

    ServerConnection::ServerConnection(int fd)
            : fd_{fd} {}

    ServerConnection::~ServerConnection() {
        close_connection();
    }

    void ServerConnection::close_connection() {
        if (fd_ >= 0) {
            close(fd_);
            fd_ = -1;
        }
    }

    bool ServerConnection::send(const message::Message &msg) const {
        if (fd_ < 0) {
            return false;
        }

        return net::send_message(fd_, msg) > 0;
    }

    bool ServerConnection::receive(message::Message &msg) const {
        if (fd_ < 0) {
            return false;
        }

        return net::receive_message(fd_, msg) > 0;
    }
} // namespace rpc
