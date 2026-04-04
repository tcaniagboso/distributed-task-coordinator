#pragma once

#include <cstdint>
#include <string>


// Utility functions for sending and receiving data over a socket connection
namespace net {
    int connect_to_server(const std::string &ip, uint16_t port, int connection_type);

    int create_listening_socket(uint16_t port, size_t backlog);

    int send_all(int sock_fd, const char *buffer, int len);

    int recv_all(int sock_fd, char *buffer, int len);
} // namespace net