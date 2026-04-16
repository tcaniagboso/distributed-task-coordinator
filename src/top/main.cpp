#include <csignal>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>

#include "../../include/top/top.hpp"

void print_help() {
    std::cout << "Usage: ./top [OPTIONS]\n\n";

    std::cout << "Options:\n";
    std::cout << "  -p, --port <port>           Router port (required)\n";
    std::cout << "      --ip <address>          Router IP (default: 127.0.0.1) (optional)\n";
    std::cout << "  -h, --help                  Show this help message\n\n";

    std::cout << "Example:\n";
    std::cout << "  ./top -p 9000\n";
}

int main(int argc, char *argv[]) {
    signal(SIGPIPE, SIG_IGN);
    std::string ip = "127.0.0.1";
    uint16_t port{};

    bool port_set{false};

    try {
        for (int i = 1; i < argc; i++) {
            const auto &cur = argv[i];
            if (std::strcmp(cur, "-h") == 0 || std::strcmp(cur, "--help") == 0) {
                print_help();
                return 0;
            } else if (std::strcmp(cur, "-p") == 0 || std::strcmp(cur, "--port") == 0) {
                utils::validate_index(i + 1, argc, argv[i]);
                int p;
                try {
                    p = std::stoi(argv[++i]);
                } catch (...) {
                    throw std::invalid_argument("Invalid value for " + std::string(argv[i - 1]) + " (must be numeric)");
                }
                utils::validate_port(p);
                port = static_cast<uint16_t>(p);
                port_set = true;
            } else if (std::strcmp(cur, "--ip") == 0) {
                utils::validate_index(i + 1, argc, argv[i]);
                ip = argv[++i];
            } else {
                throw std::invalid_argument("Invalid argument: " + std::string(argv[i]));
            }
        }

        if (!port_set) {
            throw std::invalid_argument(
                    "Missing required options: --port"
            );
        }

    } catch (std::exception &e) {
        std::cerr << e.what() << "\n\n";
        print_help();
        return -1;
    }

    top::Top runner{ip, port};
    runner.run();

    return 0;
}