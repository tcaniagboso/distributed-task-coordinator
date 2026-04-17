#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

#include "../../include/config/system_config.hpp"
#include "../../include/net/net_utils.hpp"

namespace net {

    int connect_to_server(const std::string &ip, uint16_t port) {
        int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_fd < 0) return -1;

        // Timeout
        struct timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = config::SOCKET_TIMEOUT_MS;

        setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

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

            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return -2; // special code for timeout
            }

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

            if (n == 0) return -1;
            if (errno == EINTR) continue;

            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return -2; // special code for timeout
            }

            return -1;
        }

        return static_cast<int>(total);
    }

    void prepare_send_buffer(serialization::BufferWriter &writer, const message::Message &message) {
        writer.reserve_u32();

        message.serialize(writer);

        auto &buffer = writer.get_buffer();

        auto payload_size = static_cast<uint32_t>(buffer.size() - sizeof(uint32_t));
        writer.write_payload_size(payload_size);
    }

    int get_payload_size(int sock_fd, uint32_t &payload_size) {
        uint32_t encoded_size = 0;

        int n = receive_all(sock_fd, reinterpret_cast<char *>(&encoded_size), sizeof(encoded_size));

        if (n == -2) return -2;   // timeout
        if (n <= 0) return -1;    // closed / fatal

        payload_size = ntohl(encoded_size);

        const uint32_t MAX_MESSAGE_SIZE = 1 << 24;
        if (payload_size == 0 || payload_size > MAX_MESSAGE_SIZE) {
            return -3;            // invalid payload size
        }

        return 1;                 // success
    }

    int send_message(int sock_fd, const message::Message &message) {
        serialization::BufferWriter writer{};
        prepare_send_buffer(writer, message);

        auto &buffer = writer.get_buffer();
        int n = send_all(sock_fd, buffer.data(), buffer.size());

        if (n == -2) return -2;
        else if (n <= 0) return -1;
        else return n;
    }

    int receive_message(int sock_fd, message::Message &message) {
        uint32_t payload_size = 0;

        int header_status = get_payload_size(sock_fd, payload_size);
        if (header_status < 0) return header_status;

        std::vector<char> buffer(payload_size);

        int n = receive_all(sock_fd, buffer.data(), buffer.size());
        if (n == -2) return -2;   // timeout
        if (n <= 0) return -1;    // closed / fatal

        serialization::BufferReader reader{buffer.data(), buffer.size()};
        message.deserialize(reader);

        return static_cast<int>(payload_size);
    }

    int send_message_with_retry(int sock_fd, const message::Message &message, uint32_t retries) {
        serialization::BufferWriter writer{};
        prepare_send_buffer(writer, message);

        auto &buffer = writer.get_buffer();

        for (uint32_t retry = 0; retry <= retries; retry++) {
            int n = send_all(sock_fd, buffer.data(), buffer.size());

            if (n == -2) continue; // timeout
            if (n <= 0) return -1; // fatal
            return n;
        }

        return -1;
    }

    int receive_message_with_retry(int sock_fd, message::Message &message, uint32_t retries) {
        // Returns:
        //  > 0 : bytes sent/received successfully
        //  -2  : timeout
        //  -1  : fatal error or closed connection
        //  -3  : invalid framing/message size (receive only)

        uint32_t payload_size = 0;
        int header_status = -1;

        for (uint32_t retry = 0; retry <= retries; retry++) {
            header_status = get_payload_size(sock_fd, payload_size);

            if (header_status == -2) continue; // timeout on header, safe to retry
            if (header_status < 0) return header_status;
            break;
        }

        if (header_status != 1) return -1;

        std::vector<char> buffer(payload_size);

        int n = receive_all(sock_fd, buffer.data(), buffer.size());
        if (n == -2) return -1;   // timeout during payload read: treat as fatal
        if (n <= 0) return -1;

        serialization::BufferReader reader{buffer.data(), buffer.size()};
        message.deserialize(reader);

        return static_cast<int>(payload_size);
    }
} // namespace net