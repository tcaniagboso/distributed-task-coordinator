#include <algorithm>
#include <chrono>
#include <limits>
#include <random>

#include "../../include/client/client.hpp"
#include "../../include/client/workload_utils.hpp"
#include "../../include/rpc/client.hpp"

namespace {
    thread_local std::mt19937 rng(std::random_device{}());
}

namespace client {

    ClientRunner::ClientRunner(uint64_t num_tasks, uint32_t num_clients, uint16_t port, std::string ip,
                               client::RequestType req_type)
            : num_tasks_{num_tasks},
              long_duration_{},
              medium_duration_{},
              short_duration_{},
              num_clients_{num_clients},
              port_{port},
              req_type_{req_type},
              ip_{std::move(ip)},
              latencies_(num_clients) {

        clients_.reserve(num_clients);
    }

    ClientRunner::~ClientRunner() {
        stop();
    }

    RequestType ClientRunner::random_request_type() {
        static thread_local std::bernoulli_distribution dist(0.5);

        return dist(rng)
               ? RequestType::SYNTHETIC
               : RequestType::WORD_COUNT;
    }

    WorkloadSize ClientRunner::random_size() {
        static thread_local std::discrete_distribution<int> dist({70, 20, 10});

        int idx = dist(rng);

        switch (idx) {
            case 0:
                return WorkloadSize::SHORT;
            case 1:
                return WorkloadSize::MEDIUM;
            case 2:
                return WorkloadSize::LONG;
            default:
                return WorkloadSize::SHORT;
        }
    }

    WorkloadSize ClientRunner::create_synthetic_task(message::Message &request) const {
        WorkloadSize size = random_size();
        request.submit_.type_ = task::TaskType::SYNTHETIC;
        switch (size) {
            case WorkloadSize::LONG:
                request.submit_.duration_us_ = long_duration_;
                break;
            case WorkloadSize::MEDIUM:
                request.submit_.duration_us_ = medium_duration_;
                break;
            case WorkloadSize::SHORT:
                request.submit_.duration_us_ = short_duration_;
                break;
        }

        return size;
    }

    WorkloadSize ClientRunner::create_word_count_task(message::Message &request) const {
        WorkloadSize size = random_size();
        request.submit_.type_ = task::TaskType::WORD_COUNT;
        switch (size) {
            case WorkloadSize::LONG:
                request.submit_.words_ = long_text_;
                break;
            case WorkloadSize::MEDIUM:
                request.submit_.words_ = medium_text_;
                break;
            case WorkloadSize::SHORT:
                request.submit_.words_ = short_text_;
                break;
        }

        return size;
    }

    WorkloadSize ClientRunner::create_mixed_task(message::Message &request) const {
        RequestType type = random_request_type();

        switch (type) {
            case RequestType::WORD_COUNT:
                return create_word_count_task(request);
            case RequestType::SYNTHETIC:
            default:
                return create_synthetic_task(request);
        }
    }

    void ClientRunner::worker(uint32_t id) {
        const auto &ip = ip_;
        const auto &port = port_;
        rpc::Client client{};
        client.connect(ip_, port_);

        uint64_t task_id_base = num_tasks_ * id;

        auto &latencies = latencies_[id];
        latencies.reserve(num_tasks_);

        for (uint64_t task = 0; task < num_tasks_; task++) {
            bool completed = false;
            for (int retry = 0; retry <= config::RETRY_TASK && !completed; retry++) {
                uint64_t task_id = task_id_base + task;

                message::Message request{message::MessageType::SUBMIT};
                message::Message response;

                request.submit_.client_id_ = id;
                request.submit_.task_id_ = task_id;

                WorkloadSize size;
                switch (req_type_) {
                    case RequestType::MIXED:
                        size = create_mixed_task(request);
                        break;
                    case RequestType::SYNTHETIC:
                        size = create_synthetic_task(request);
                        break;
                    case RequestType::WORD_COUNT:
                        size = create_word_count_task(request);
                        break;
                }

                auto start = std::chrono::steady_clock::now();
                if (client.send_with_retry(request, config::CLIENT_CONNECTION_RETRY_COUNT) <= 0) {
                    if (!client.connect(ip, port)
                        || client.send_with_retry(request, config::CLIENT_CONNECTION_RETRY_COUNT) <= 0) {
                        continue;
                    }
                }

                if (config::DEBUG) {
                    std::cout << "Sent task " << task_id << "\n";
                    std::cout << "Waiting for response " << task_id << "\n";
                }


                if (client.receive_with_retry(response, config::CLIENT_CONNECTION_RETRY_COUNT) <= 0) {
                    continue;
                }

                if (config::DEBUG) {
                    std::cout << "Received result for task" << task_id << "\n";
                }

                auto end = std::chrono::steady_clock::now();

                if (response.result_.success_ == 0) {
                    continue;

                }

                auto duration = end - start;
                uint64_t latency = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
                latencies.push_back(latency);

                if (request.submit_.type_ == task::TaskType::WORD_COUNT) {
                    assert(response.result_.result_ == static_cast<uint32_t>(get_text_size(size)));
                }

                completed = true;
            }

            if (!completed) break;
        }

        client.close_connection();
    }

    Stats ClientRunner::compute_stats(const std::vector<std::vector<uint64_t>> &latencies, double seconds) const {
        // Compute MAX, MIN, and AVG Latencies
        uint64_t max_val = std::numeric_limits<uint64_t>::max();
        uint64_t completed = 0;
        uint64_t latency_sum = 0;
        uint64_t max_latency = 0;
        uint64_t min_latency = max_val;
        std::vector<uint64_t> cleaned_latencies;
        cleaned_latencies.reserve(num_clients_ * num_tasks_);
        for (const auto &row: latencies) {
            for (uint64_t cur_latency: row) {
                completed++;
                max_latency = std::max(max_latency, cur_latency);
                min_latency = std::min(min_latency, cur_latency);
                latency_sum += cur_latency;
                cleaned_latencies.push_back(cur_latency);
            }
        }

        std::sort(cleaned_latencies.begin(), cleaned_latencies.end());
        size_t p50_index = completed / 2;
        size_t p95_index = completed * 95 / 100;

        Stats s{};
        s.completed_ = completed;
        s.avg_latency_us_ = (completed > 0) ? static_cast<double>(latency_sum) / static_cast<double>(completed) : 0.0;
        s.min_latency_us_ = (completed > 0) ? min_latency : 0;
        s.max_latency_us_ = (completed > 0) ? max_latency : 0;
        s.p50_latency_us_ = (completed > 0) ? cleaned_latencies[p50_index] : 0;
        s.p95_latency_us_ = (completed > 0) ? cleaned_latencies[p95_index] : 0;
        s.throughput_ops_ = (seconds > 0.0) ? static_cast<double>(completed) / seconds : 0.0;

        return s;
    }

    void ClientRunner::run() {

        if (req_type_ == RequestType::MIXED || req_type_ == RequestType::WORD_COUNT) {
            long_text_ = client::generate_text(client::get_text_size(WorkloadSize::LONG));
            medium_text_ = client::generate_text(client::get_text_size(WorkloadSize::MEDIUM));
            short_text_ = client::generate_text(client::get_text_size(WorkloadSize::SHORT));
        }

        if (req_type_ == RequestType::MIXED || req_type_ == RequestType::SYNTHETIC) {
            long_duration_ = client::get_duration(WorkloadSize::LONG);
            medium_duration_ = client::get_duration(WorkloadSize::MEDIUM);
            short_duration_ = client::get_duration(WorkloadSize::SHORT);
        }

        auto start = std::chrono::steady_clock::now();

        for (uint32_t client = 0; client < num_clients_; client++) {
            clients_.emplace_back(&ClientRunner::worker, this, client);
        }

        for (auto &t: clients_) {
            if (t.joinable()) t.join();
        }

        auto end = std::chrono::steady_clock::now();

        double total_time_secs = std::chrono::duration<double>(end - start).count();

        Stats stats = compute_stats(latencies_, total_time_secs);
        client::Stats::print(stats);
    }

    void ClientRunner::stop() {
        for (auto &client: clients_) {
            if (client.joinable()) client.join();
        }
    }
} // namespace client