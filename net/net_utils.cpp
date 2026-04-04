#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <net/if.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include "net_utils.hpp"

namespace net {

    int connect_to_server(const std::string &ip, uint16_t port, int connection_type) {
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

        uint32_t type = htonl(static_cast<uint32_t>(connection_type));
        if (send_all(sock_fd, reinterpret_cast<const char *>(&type), sizeof(type)) < 0) {
            close(sock_fd);
            return -1;
        }

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
        if (listen(sock_fd, backlog) < 0) {
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
    int recv_all(int sock_fd, char *buffer, size_t len) {
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
} // namespace net