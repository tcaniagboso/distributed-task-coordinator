#include <cctype>
#include <poll.h>
#include <limits>

#include "../../include/worker/worker.hpp"
#include "../../include/config/system_config.hpp"
#include "../../include/net/net_utils.hpp"
#include "../../include/utils/utils.hpp"

namespace worker {

    Worker::Worker(std::string ip, uint16_t port, size_t num_workers)
            : next_worker_{0},
              last_heartbeat_ns_{utils::now_ns_u64()},
              num_workers_{num_workers},
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

    void Worker::connect() {
        int backoff_ms = 10;

        while (true) {
            connection_.reset(new rpc::Client{ip_, port_});

            if (connection_->fd() >= 0) {
                if (register_with_server()) {
                    return;
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
            backoff_ms = std::min(backoff_ms * 2, 1000);
        }
    }

    bool Worker::register_with_server() {
        message::Message register_msg{message::MessageType::REGISTER};

        bool result = connection_->send(register_msg);

        if (!result) return false;

        message::Message ack_message;
        result = connection_->receive(ack_message);

        if (!result) return false;

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

            while (!task_queues_[id]->try_pop(task)) {
                std::this_thread::yield();
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

            while (!response_queues_[id]->try_push(response)) {
                std::this_thread::yield();
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

        connect();

        while (running_.load(std::memory_order_acquire)) {
            // Poll for Data
            struct pollfd pfd{connection_->fd(), POLLIN, 0};

            int ret = poll(&pfd, 1, 10); // small timeout (10ms)

            if (ret > 0) {
                if (pfd.revents & (POLLHUP | POLLERR)) {
                    connect();
                    continue;
                }

                if (pfd.revents & POLLIN) {
                    message::Message msg;
                    while (!connection_->receive(msg)) {
                        connect(); // coordinator died
                    }

                    // Assign task
                    size_t idx = next_worker_ % num_workers_;
                    int retry = 0;
                    if (msg.type_ != message::MessageType::ASSIGN) continue;
                    while (!task_queues_[idx]->try_push(msg.assign_)) {
                        std::this_thread::yield();
                        if (++retry > config::RETRY_COUNT) {
                            next_worker_++;
                            idx = next_worker_ % num_workers_;
                            retry = 0;
                        }
                    }
                    next_worker_++;
                }
            }

            // Send responses
            for (int i = 0; i < num_workers_; i++) {
                message::CompleteMsg response{};
                while (response_queues_[i]->try_pop(response)) {
                    message::Message complete_msg{message::MessageType::COMPLETE};
                    complete_msg.complete_ = response;

                    while (!connection_->send(complete_msg)) {
                        connect();
                    }
                }
            }


            // Send heartbeat
            uint64_t now = utils::now_ns_u64();
            if (now - last_heartbeat_ns_ >= config::HEARTBEAT_INTERVAL_NS) {
                message::Message heartbeat{message::MessageType::HEARTBEAT};
                heartbeat.heartbeat_ = message::HeartBeatMsg{id_};
                while (!connection_->send(heartbeat)) {
                    connect();
                }
                last_heartbeat_ns_ = utils::now_ns_u64();
            }
        }
    }

    void Worker::run() {

        running_.store(true, std::memory_order_release);

        network_thread_ = std::thread{&Worker::network_worker, this};

        for (int i = 0; i < num_workers_; i++) {
            execution_threads_.emplace_back(&Worker::execution_worker, this, i);
        }
    }

    void Worker::stop() {
        if (running_.load(std::memory_order_acquire)) {
            running_.store(false, std::memory_order_release);

            if (network_thread_.joinable()) {
                network_thread_.join();
            }

            for (auto &t: execution_threads_) {
                if (t.joinable()) t.join();
            }
        }
    }

} // namespace worker
