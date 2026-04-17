#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <stdexcept>

#include "../config/system_config.hpp"

namespace utils {
    using namespace std::chrono;

    inline uint64_t now_ns_u64() {
        return duration_cast<nanoseconds>(
                steady_clock::now().time_since_epoch()
        ).count();
    }

    inline void validate_index(int i, int n, const char* option) {
        if (i >= n) {
            throw std::invalid_argument("Missing value for " + std::string(option));
        }
    }

    template<typename T>
    inline void validate_numeric(T val, const std::string& label) {
        if (val <= 0) {
            throw std::invalid_argument(label + " must be greater than zero");
        }
    }

    inline void validate_port(int port) {
        if (port <= 0 || port > 65535) {
            throw std::invalid_argument("Port must be in range 1-65535");
        }
    }

    inline void validate_workers(size_t workers) {
        if (workers == 0 || workers > config::MAX_WORKER_THREADS) {
            throw std::invalid_argument(
                    "Workers must be in range 1-" + std::to_string(config::MAX_WORKER_THREADS)
            );
        }
    }

    inline void validate_clients(size_t clients) {
        if (clients == 0 || clients > config::MAX_CLIENTS) {
            throw std::invalid_argument(
                    "Clients must be in range 1-" + std::to_string(config::MAX_CLIENTS)
            );
        }
    }

    inline std::pair<std::string, uint16_t> parse_endpoint(const std::string& input) {
        auto pos = input.find(':');
        if (pos == std::string::npos) {
            throw std::invalid_argument("Invalid endpoint format. Expected <ip:port>");
        }

        std::string ip = input.substr(0, pos);
        std::string port_str = input.substr(pos + 1);

        int port;
        try {
            port = std::stoi(port_str);
        } catch (...) {
            throw std::invalid_argument("Invalid port in endpoint (must be numeric)");
        }

        validate_port(port);

        return {ip, static_cast<uint16_t>(port)};
    }
} // namespace utils