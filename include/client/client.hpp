#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "types.hpp"
#include "../message/message.hpp"

namespace client {

    class ClientRunner {
    private:
        uint64_t num_tasks_;

        uint64_t long_duration_;
        uint64_t medium_duration_;
        uint64_t short_duration_;

        uint32_t num_clients_;

        uint16_t port_; // router port

        RequestType req_type_;

        std::string ip_;

        std::string long_text_;
        std::string short_text_;
        std::string medium_text_;

        std::vector<std::thread> clients_;
        std::vector<std::vector<uint64_t>> latencies_;

        static RequestType random_request_type();

        static WorkloadSize random_size();

        WorkloadSize create_synthetic_task(message::Message &request) const;

        WorkloadSize create_word_count_task(message::Message &request) const;

        WorkloadSize create_mixed_task(message::Message &request) const;

        void worker(uint32_t id);

        Stats compute_stats(const std::vector<std::vector<uint64_t>> &latencies, double seconds) const;

    public:
        ClientRunner(uint64_t num_tasks, uint32_t num_clients, uint16_t port, std::string ip, RequestType req_type);

        ~ClientRunner();

        void run();

        void stop();
    };
} // namespace client