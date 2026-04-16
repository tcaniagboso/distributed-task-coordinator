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

    void prepare_send_buffer(serialization::BufferWriter& writer, const message::Message& message);

    int get_payload_size(int sock_fd, uint32_t& payload_size);

    int send_message(int sock_fd, const message::Message& message);

    int receive_message(int sock_fd, message::Message& message);

    int send_message_with_retry(int sock_fd, const message::Message& message, uint32_t retries);

    int receive_message_with_retry(int sock_fd, message::Message& message, uint32_t retries);
} // namespace net