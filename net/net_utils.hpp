#pragma once

#include <cstdint>
#include <string>

#include "../message/message.hpp"


// Utility functions for sending and receiving data over a socket connection
namespace net {
    int connect_to_server(const std::string &ip, uint16_t port);

    int create_listening_socket(uint16_t port, size_t backlog);

    int send_all(int sock_fd, const char *buffer, size_t len);

    int receive_all(int sock_fd, char *buffer, size_t len);

    int send_message(int sock_fd, const message::Message& message);

    int receive_message(int sock_fd, message::Message& message);
} // namespace net