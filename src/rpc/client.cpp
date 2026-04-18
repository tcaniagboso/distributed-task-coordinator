#include <unistd.h>
#include <sys/socket.h>

#include "../../include/rpc/client.hpp"
#include "../../include/net/net_utils.hpp"

namespace rpc {
    Client::Client()
            : sock_fd_{-1} {}

    Client::~Client() {
        close_connection();
    }

    void Client::close_connection() {
        if (sock_fd_ >= 0) {
            shutdown(sock_fd_, SHUT_WR);
            close(sock_fd_);
            sock_fd_ = -1;
        }
    }

    bool Client::connect(const std::string& ip, uint16_t port) {
        close_connection();
        sock_fd_ = net::connect_to_server(ip, port);
        return sock_fd_ >= 0;
    }

    int Client::send(const message::Message &msg) const {
        if (sock_fd_ < 0) {
            return -1;
        }

        return net::send_message(sock_fd_, msg);
    }

    int Client::receive(message::Message &msg) const {
        if (sock_fd_ < 0) {
            return -1;
        }

        return net::receive_message(sock_fd_, msg);
    }

    int Client::send_with_retry(const message::Message &msg, uint32_t retries) const {
        if (sock_fd_ < 0) return -1;

        return net::send_message_with_retry(sock_fd_, msg, retries);
    }

    int Client::receive_with_retry(message::Message &msg, uint32_t retries) const {
        if (sock_fd_ < 0) return -1;

        return net::receive_message_with_retry(sock_fd_, msg, retries);
    }

    int Client::fd() const { return sock_fd_; }
} // namespace rpc