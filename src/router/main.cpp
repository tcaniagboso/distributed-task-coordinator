#include <csignal>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>

#include "../../include/router/router.hpp"

void print_help() {
    std::cout << "Usage: ./router [OPTIONS]\n\n";

    std::cout << "Options:\n";
    std::cout << "  -p, --port <port>                    Router port (required)\n";
    std::cout
            << "  -s, --shard <ip:port,ip:port>        Primary and backup nodes addresses (ip:port,ip:port) (required)\n";
    std::cout << "  -h, --help                           Show this help message\n\n";

    std::cout << "Example:\n";
    std::cout << "  ./router -p 9000 -s 127.0.0.1:9001,127.0.0.1:9002\n";
}

router::RouterShardGroup parse_shard(const std::string &shard) {
    auto pos = shard.find(',');
    if (pos == std::string::npos) {
        throw std::invalid_argument("Invalid shard format. Use <ip:port,ip:port>");
    }

    if (shard.find(',', pos + 1) != std::string::npos) {
        throw std::invalid_argument("Shard must contain exactly 2 endpoints");
    }

    std::string primary_str = shard.substr(0, pos);
    std::string backup_str = shard.substr(pos + 1);

    auto primary_pair = utils::parse_endpoint(primary_str);
    router::Endpoint primary = router::Endpoint(primary_pair.first, primary_pair.second);

    auto backup_pair = utils::parse_endpoint(backup_str);
    router::Endpoint backup = router::Endpoint(backup_pair.first, backup_pair.second);

    return router::RouterShardGroup{primary, backup};
}

int main(int argc, char *argv[]) {
    signal(SIGPIPE, SIG_IGN);
    uint16_t port{};
    std::vector<router::RouterShardGroup> shards{};
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
            } else if (std::strcmp(cur, "-s") == 0 || std::strcmp(cur, "--shard") == 0) {
                utils::validate_index(i + 1, argc, argv[i]);
                shards.push_back(parse_shard(argv[++i]));
            } else {
                throw std::invalid_argument("Invalid argument: " + std::string(argv[i]));
            }
        }

        if (!port_set || shards.empty()) {
            throw std::invalid_argument("Missing required options: --port, --shard");
        }

    } catch (std::exception &e) {
        std::cerr << e.what() << "\n\n";
        print_help();
        return -1;
    }

    router::Router router{port, std::move(shards)};
    router.run();

    return 0;
}