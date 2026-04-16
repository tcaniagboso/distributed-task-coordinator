#include <unistd.h>

#include "../../include/rpc/server_connection.hpp"
#include "../../include/net/net_utils.hpp"

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

    int ServerConnection::send(const message::Message &msg) const {
        if (fd_ < 0) {
            return -1;
        }

        return net::send_message(fd_, msg);
    }

    int ServerConnection::receive(message::Message &msg) const {
        if (fd_ < 0) {
            return -1;
        }

        return net::receive_message(fd_, msg);
    }

    int ServerConnection::send_with_retry(const message::Message &msg, uint32_t retries) const {
        if (fd_ < 0) {
            return -1;
        }

        return net::send_message_with_retry(fd_, msg, retries);
    }

    int ServerConnection::receive_with_retry(message::Message &msg, uint32_t retries) const {
        if (fd_ < 0) {
            return -1;
        }

        return net::receive_message_with_retry(fd_, msg, retries);
    }
} // namespace rpc
