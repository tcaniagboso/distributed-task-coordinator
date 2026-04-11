#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

#include "net_utils.hpp"

namespace net {

    int connect_to_server(const std::string &ip, uint16_t port) {
        int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_fd < 0) return -1;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);

        if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0) {
            close(sock_fd);
            return -1;
        }

        if (connect(sock_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
            close(sock_fd);
            return -1;
        }

        int flag = 1;
        setsockopt(sock_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

        return sock_fd;
    }

    int create_listening_socket(uint16_t port, size_t backlog) {
        int sock_fd = socket(AF_INET, SOCK_STREAM, 0);

        if (sock_fd < 0) {
            return -1;
        }

        int opt = 1;
        setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in my_addr{};
        my_addr.sin_family = AF_INET;
        my_addr.sin_port = htons(port);
        my_addr.sin_addr.s_addr = INADDR_ANY;

        if ((bind(sock_fd, reinterpret_cast<sockaddr *>(&my_addr), sizeof(my_addr))) < 0) {
            close(sock_fd);
            return -1;
        }

        // Listen to incoming connections
        if (listen(sock_fd, static_cast<int>(backlog)) < 0) {
            close(sock_fd);
            return -1;
        }

        return sock_fd;
    }

    // Send all data
    int send_all(int sock_fd, const char *buffer, size_t len) {
        size_t total = 0;

        while (total < len) {
            ssize_t n = send(sock_fd, buffer + total, len - total, 0);

            if (n > 0) {
                total += static_cast<size_t>(n);
                continue;
            }

            if (n == 0) return -1;
            if (errno == EINTR) continue;

            return -1;
        }

        return static_cast<int>(total);
    }

    // Wait and receive all data
    int receive_all(int sock_fd, char *buffer, size_t len) {
        size_t total = 0;

        while (total < len) {
            ssize_t n = recv(sock_fd, buffer + total, len - total, 0);

            if (n > 0) {
                total += static_cast<size_t>(n);
                continue;
            }

            if (n == 0) return 0;
            if (errno == EINTR) continue;

            return -1;
        }

        return static_cast<int>(total);
    }

    int send_message(int sock_fd, const message::Message &message) {
        serialization::BufferWriter writer{};
        writer.reserve_u32();

        message.serialize(writer);

        auto &buffer = writer.get_buffer();

        auto payload_size = static_cast<uint32_t>(buffer.size() - sizeof(uint32_t));
        writer.write_payload_size(payload_size);

        return send_all(sock_fd, buffer.data(), buffer.size());
    }

    int receive_message(int sock_fd, message::Message &message) {
        uint32_t encoded_size = 0;

        int n = receive_all(sock_fd, reinterpret_cast<char *>(&encoded_size), sizeof(encoded_size));

        if (n <= 0) {
            return n;
        }

        uint32_t payload_size = ntohl(encoded_size);

        const uint32_t MAX_MESSAGE_SIZE = 1 << 20;

        if (payload_size == 0 || payload_size > MAX_MESSAGE_SIZE) {
            return -1;
        }

        std::vector<char> buffer(payload_size);

        if (receive_all(sock_fd, buffer.data(), buffer.size()) <= 0) {
            return -1;
        }

        serialization::BufferReader reader{buffer.data(), buffer.size()};

        message.deserialize(reader);

        return static_cast<int>(payload_size);
    }
} // namespace net