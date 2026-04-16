#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "../lock_free/spsc_queue.hpp"
#include "../message/message.hpp"
#include "../rpc/client.hpp"

namespace worker {
    using namespace std::chrono;

    class Worker {
    private:

        uint64_t next_worker_;

        uint64_t last_heartbeat_ns_;

        size_t num_workers_;

        size_t last_start_;

        uint32_t id_;

        uint16_t port_; // coordinator port

        std::atomic<bool> running_;

        std::string ip_; // coordinator ip

        std::unique_ptr<rpc::Client> connection_; // client connection to coordinator

        std::thread network_thread_;

        std::vector <std::thread> execution_threads_;

        std::vector<std::unique_ptr<lock_free::SPSCQueue<message::AssignMsg>>> task_queues_;

        std::vector<std::unique_ptr<lock_free::SPSCQueue<message::CompleteMsg>>> response_queues_;

        bool connect();

        bool register_with_server();

        static void execute_synthetic(const message::AssignMsg& task, message::CompleteMsg& response);

        static void execute_word_count(const message::AssignMsg& task, message::CompleteMsg& response);

        void execution_worker(size_t id);

        void network_worker();

    public:
        Worker(std::string ip, uint16_t port, size_t num_workers);

        ~Worker();

        void run();

        void stop();
    };

} // namespace worker