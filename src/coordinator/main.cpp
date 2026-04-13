#include <iostream>
#include <cstdint>
#include <cstring>
#include <stdexcept>

#include "../../include/coordinator/coordinator.hpp"

void print_help() {
    std::cout << "Usage: ./coordinator [OPTIONS]\n\n";

    std::cout << "Options:\n";
    std::cout << "  -p, --port <port>           Coordinator port (required)\n";
    std::cout << "      --peer <ip:port>        Peer node address (ip:port) (required)\n";
    std::cout << "  -h, --help                  Show this help message\n\n";

    std::cout << "Example:\n";
    std::cout << "  ./coordinator -p 9000 --peer 127.0.0.1:9001\n";
}

int main(int argc, char *argv[]) {
    uint16_t port{};
    std::vector<coordinator::PeerNode> peers{};
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
                } catch(...) {
                    throw std::invalid_argument("Invalid value for " + std::string(argv[i - 1]) + " (must be numeric)");
                }
                utils::validate_port(p);
                port = static_cast<uint16_t>(p);
                port_set = true;
            } else if (std::strcmp(cur, "--peer") == 0) {
                utils::validate_index(i + 1, argc, argv[i]);
                auto endpoint = utils::parse_endpoint(argv[++i]);
                peers.emplace_back(endpoint.first, endpoint.second);
            } else {
                throw std::invalid_argument("Invalid argument: " + std::string(argv[i]));
            }
        }

        if (!port_set || peers.size() != 1) {
            throw std::invalid_argument("Missing required options: --port, --peer");
        }

    } catch (std::exception &e) {
        std::cerr << e.what() << "\n\n";
        print_help();
        return -1;
    }

    coordinator::Coordinator coordinator{port, std::move(peers.front())};
    coordinator.run();

    return 0;
}