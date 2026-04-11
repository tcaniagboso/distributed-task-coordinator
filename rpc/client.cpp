#include <unistd.h>
#include <sys/socket.h>

#include "client.hpp"
#include "../net/net_utils.hpp"

namespace rpc {
    Client::Client(const std::string &ip, uint16_t port)
            : sock_fd_{net::connect_to_server(ip, port)} {}

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

    bool Client::send(const message::Message &msg) const {
        if (sock_fd_ < 0) {
            return false;
        }

        return net::send_message(sock_fd_, msg) > 0;
    }

    bool Client::receive(message::Message &msg) const {
        if (sock_fd_ < 0) {
            return false;
        }

        return net::receive_message(sock_fd_, msg) > 0;
    }

    int Client::fd() const { return sock_fd_; }
} // namespace rpc