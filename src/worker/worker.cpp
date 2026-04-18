#include <algorithm>
#include <cctype>
#include <iostream>
#include <poll.h>
#include <limits>

#include "../../include/worker/worker.hpp"
#include "../../include/net/net_utils.hpp"

namespace worker {

    Worker::Worker(std::string ip, uint16_t port, size_t num_workers)
            : next_worker_{0},
              last_heartbeat_ns_{utils::now_ns_u64()},
              num_workers_{num_workers},
              last_start_{0},
              id_{std::numeric_limits<uint32_t>::max()},
              port_{port},
              running_{false},
              ip_{std::move(ip)},
              connection_{nullptr},
              network_thread_{},
              execution_threads_{},
              task_queues_{},
              response_queues_{} {
        execution_threads_.reserve(num_workers);

        task_queues_.reserve(num_workers);
        response_queues_.reserve(num_workers);

        for (size_t i = 0; i < num_workers; i++) {
            task_queues_.emplace_back(new lock_free::SPSCQueue<message::AssignMsg>(config::WORKER_QUEUE_CAPACITY));
            response_queues_.emplace_back(
                    new lock_free::SPSCQueue<message::CompleteMsg>(config::WORKER_QUEUE_CAPACITY));
        }
    }

    Worker::~Worker() {
        stop();
    }

    bool Worker::connect() {
        int backoff_ms = 10;

        for (uint32_t retry = 0; retry < config::REGISTER_RETRY_COUNT; retry++){
            connection_.reset(new rpc::Client{});
            connection_->connect(ip_, port_);

            if (connection_->fd() >= 0) {
                if (register_with_server()) {
                    return true;
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
            backoff_ms = std::min(backoff_ms * 2, 1000);
        }

        if (connection_) {
            connection_->close_connection();
        }

        connection_.reset();
        return false;
    }

    bool Worker::register_with_server() {
        message::Message register_msg{message::MessageType::REGISTER};

        int result = connection_->send_with_retry(register_msg, config::WORKER_CONNECTION_RETRY_COUNT);

        if (result <= 0) return false;

        message::Message ack_message;
        result = connection_->receive_with_retry(ack_message, config::WORKER_CONNECTION_RETRY_COUNT);

        if (result <= 0) return false;

        id_ = ack_message.acknowledge_.worker_id_;
        last_heartbeat_ns_ = utils::now_ns_u64(); // if we register that is a heartbeat
        return true;
    }

    void Worker::execute_synthetic(const message::AssignMsg &task, message::CompleteMsg &response) {
        response.task_id_ = task.task_id_;
        response.started_at_ = utils::now_ns_u64();
        std::this_thread::sleep_for(microseconds(task.duration_us_));
        response.completed_at_ = utils::now_ns_u64();
        response.success_ = 1;
    }

    void Worker::execute_word_count(const message::AssignMsg &task,
                                    message::CompleteMsg &response) {
        response.task_id_ = task.task_id_;
        response.started_at_ = utils::now_ns_u64();

        const auto &words = task.words_;
        size_t i = 0;
        size_t n = words.size();
        uint32_t count = 0;

        while (i < n) {
            // skip delimiters
            while (i < n && !std::isalnum(static_cast<unsigned char>(words[i]))) {
                i++;
            }

            count += (i < n);

            // consume word
            while (i < n) {
                auto c = static_cast<unsigned char>(words[i]);
                if (std::isalnum(c)) {
                    i++;
                } else if (c == '\'') {
                    if (i + 1 < n && std::isalnum(static_cast<unsigned char>(words[i + 1]))) {
                        i++;
                    } else {
                        break;
                    }
                } else {
                    break;
                }
            }
        }

        response.result_ = count;
        response.completed_at_ = utils::now_ns_u64();
        response.success_ = 1;
    }

    void Worker::execution_worker(size_t id) {

        while (running_.load(std::memory_order_acquire)) {
            message::AssignMsg task{};
            message::CompleteMsg response{};

            uint32_t backoff_us = 1;
            while (running_.load(std::memory_order_acquire) && !task_queues_[id]->try_pop(task)) {
                std::this_thread::sleep_for(std::chrono::microseconds(backoff_us));
                backoff_us = std::min(backoff_us << 1, config::MAX_BACKOFF_US);
            }

            if (!running_.load(std::memory_order_acquire)) break;

            if (config::DEBUG) {
                std::cout << "Executing task " << task.task_id_ << "\n";
            }
            switch (task.type_) {
                case (task::TaskType::SYNTHETIC):
                    execute_synthetic(task, response);
                    break;
                case (task::TaskType::WORD_COUNT):
                    execute_word_count(task, response);
                    break;
                default:
                    break;
            }

            backoff_us = 1;
            while (!response_queues_[id]->try_push(response)) {
                std::this_thread::sleep_for(std::chrono::microseconds(backoff_us));
                backoff_us = std::min<uint32_t>(backoff_us << 1, config::MAX_BACKOFF_US);
            }

            if (config::DEBUG) {
                std::cout << "Completed task " << task.task_id_ << "\n";
            }
        }
    }

    void Worker::network_worker() {

        // while loop trying Register first while(!register_to_server) reconnect again
        // Enter a while(true) loop
        //  poll if sock_fd has data, if it does use connection to get the data
        // use next_worker_ % num_workers to assign to a worker, then increment next_worker
        // check response queues and send back responses if they exist
        // check last heartbeat time and send heartbeat if it has passed a certain duration

        if (!connect()) {
            running_.store(false, std::memory_order_release);
            return;
        }

        while (running_.load(std::memory_order_acquire)) {
            // Poll for Data
            struct pollfd pfd{connection_->fd(), POLLIN, 0};

            int ret = poll(&pfd, 1, 10); // small timeout (10ms)

            if (ret > 0) {
                if (pfd.revents & (POLLHUP | POLLERR)) {
                    if (!connect()) {
                        running_.store(false, std::memory_order_release);
                        return;
                    }
                    continue;
                }

                if (pfd.revents & POLLIN) {
                    message::Message msg;
                    int n = connection_->receive_with_retry(msg, config::WORKER_CONNECTION_RETRY_COUNT);
                    if (n == -3) continue;
                    if (n <= 0) {
                        running_.store(false, std::memory_order_release);
                        return;
                    }

                    // Assign task
                    size_t idx = next_worker_;
                    uint32_t retry = 0;
                    if (msg.type_ != message::MessageType::ASSIGN) continue;
                    uint32_t backoff_us = 1;
                    while (!task_queues_[idx]->try_push(msg.assign_)) {
                        if (++retry > config::QUEUE_RETRY_COUNT) {
                            next_worker_ = (next_worker_ + 1) % num_workers_;;
                            idx = next_worker_;
                            retry = 0;
                        } else {
                            std::this_thread::sleep_for(std::chrono::microseconds(backoff_us));
                            backoff_us = std::min<uint32_t>(backoff_us << 1, config::MAX_BACKOFF_US);
                        }
                    }

                    next_worker_ = (next_worker_ + 1) % num_workers_;
                }
            }

            // Send responses
            size_t total_processed = 0;

            size_t start = last_start_;  // rotating index

            for (size_t k = 0; k < num_workers_ && total_processed < config::WORKER_OUT_BUDGET; k++) {
                size_t i = (start + k) % num_workers_;

                message::CompleteMsg response{};
                size_t local_processed = 0;

                while (response_queues_[i]->try_pop(response)) {
                    message::Message complete_msg{message::MessageType::COMPLETE};
                    complete_msg.complete_ = response;

                    if (connection_->send_with_retry(complete_msg, config::WORKER_CONNECTION_RETRY_COUNT) <= 0) {
                        running_.store(false, std::memory_order_release);
                        return;
                    }

                    total_processed++;
                    local_processed++;

                    if (total_processed >= config::WORKER_OUT_BUDGET) break;

                    if (local_processed >= config::WORKER_OUT_PER_THREAD_CAP) break;
                }
            }

            last_start_ = (start + 1) % num_workers_;


            // Send heartbeat
            uint64_t now = utils::now_ns_u64();
            if (now - last_heartbeat_ns_ >= config::HEARTBEAT_INTERVAL_NS) {
                message::Message heartbeat{message::MessageType::HEARTBEAT};
                heartbeat.heartbeat_ = message::HeartBeatMsg{id_};
                if (connection_->send_with_retry(heartbeat, config::WORKER_CONNECTION_RETRY_COUNT) <= 0) {
                    running_.store(false, std::memory_order_release);
                    return;
                }
                last_heartbeat_ns_ = utils::now_ns_u64();
            }
        }
    }

    void Worker::run() {
        running_.store(true, std::memory_order_release);

        network_thread_ = std::thread{&Worker::network_worker, this};

        for (size_t i = 0; i < num_workers_; i++) {
            execution_threads_.emplace_back(&Worker::execution_worker, this, i);
        }

        for (auto &t: execution_threads_) {
            t.join();
        }

        network_thread_.join();
        running_.store(false, std::memory_order_release);
    }

    void Worker::stop() {
        running_.store(false, std::memory_order_release);

        if (network_thread_.joinable()) {
            network_thread_.join();
        }

        for (auto &t: execution_threads_) {
            if (t.joinable()) t.join();
        }
    }

} // namespace worker
