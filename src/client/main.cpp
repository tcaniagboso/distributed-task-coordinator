#include <iostream>
#include <cstdint>
#include <cstring>
#include <stdexcept>

#include "../../include/client/client.hpp"

void print_help() {
    std::cout << "Usage: ./client [OPTIONS]\n\n";

    std::cout << "Options:\n";
    std::cout << "  -p, --port <port>           Router port (required)\n";
    std::cout << "  -c, --clients <clients>     Number of client threads (required)\n";
    std::cout << "  -n, --num_tasks <tasks>     Tasks per client (required)\n";
    std::cout << "  -t, --task_type <type>      synthetic | word | mixed (required)\n";
    std::cout << "      --ip <address>          Router IP (default: 127.0.0.1) (optional)\n";
    std::cout << "  -h, --help                  Show this help message\n\n";

    std::cout << "Example:\n";
    std::cout << "  ./client -p 9000 -c 4 -n 10000 -t mixed\n";
}

client::RequestType get_request_type(const char *request_type) {
    if (std::strcmp(request_type, "synthetic") == 0) {
        return client::RequestType::SYNTHETIC;
    } else if (std::strcmp(request_type, "word") == 0) {
        return client::RequestType::WORD_COUNT;
    } else if (std::strcmp(request_type, "mixed") == 0) {
        return client::RequestType::MIXED;
    }

    throw std::invalid_argument("Invalid task type: " + std::string(request_type)
                                + "\nTask type must be one of: [synthetic, word, mixed]");
}

int main(int argc, char *argv[]) {
    std::string ip = "127.0.0.1";
    uint16_t port{};
    uint32_t clients{};
    uint64_t tasks{};
    client::RequestType task_type{};

    bool port_set{false};
    bool clients_set{false};
    bool tasks_set{false};
    bool task_type_set{false};

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
            } else if (std::strcmp(cur, "-c") == 0 || std::strcmp(cur, "--clients") == 0) {
                utils::validate_index(i + 1, argc, argv[i]);
                try {
                    clients = std::stoul(argv[++i]);
                } catch (...) {
                    throw std::invalid_argument("Invalid value for " + std::string(argv[i - 1]) + " (must be numeric)");
                }
                utils::validate_numeric(clients, "Clients");
                clients_set = true;
            } else if (std::strcmp(cur, "-n") == 0 || std::strcmp(cur, "--num_tasks") == 0) {
                utils::validate_index(i + 1, argc, argv[i]);
                try {
                    tasks = std::stoull(argv[++i]);
                } catch (...) {
                    throw std::invalid_argument("Invalid value for " + std::string(argv[i - 1]) + " (must be numeric)");
                }
                utils::validate_numeric(tasks, "Number of tasks");
                tasks_set = true;
            } else if (std::strcmp(cur, "-t") == 0 || std::strcmp(cur, "--task_type") == 0) {
                utils::validate_index(i + 1, argc, argv[i]);
                task_type = get_request_type(argv[++i]);
                task_type_set = true;
            } else if (std::strcmp(cur, "--ip") == 0) {
                utils::validate_index(i + 1, argc, argv[i]);
                ip = argv[++i];
            } else {
                throw std::invalid_argument("Invalid argument: " + std::string(argv[i]));
            }
        }

        if (!port_set || !task_type_set || !tasks_set || !clients_set) {
            throw std::invalid_argument(
                    "Missing required options: --port, --clients, --num_tasks, --task_type"
            );
        }

    } catch (std::exception &e) {
        std::cerr << e.what() << "\n\n";
        print_help();
        return -1;
    }

    client::ClientRunner runner{tasks, clients, port, ip, task_type};
    runner.run();

    return 0;
}