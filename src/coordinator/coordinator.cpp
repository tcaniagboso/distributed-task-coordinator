#include <algorithm>
#include <iostream>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../../include/coordinator/coordinator.hpp"
#include "../../include/net/net_utils.hpp"

namespace coordinator {

    Coordinator::Coordinator(uint16_t port, coordinator::PeerNode &&peer)
            : last_sweep_{},
              current_epoch_{0},
              rr_index_{},
              replication_read_ptr_{0},
              replication_write_ptr_{0},
              port_{port},
              is_primary_{false},
              running_{false},
              network_thread_{},
              scheduler_thread_{},
              control_queue_{new lock_free::SPSCQueue<IncomingEvent>(config::COORDINATOR_CONTROL_QUEUE_CAPACITY)},
              task_queue_{new lock_free::SPSCQueue<IncomingEvent>(config::COORDINATOR_TASK_QUEUE_CAPACITY)},
              outgoing_queue_{new lock_free::SPSCQueue<OutgoingEvent>(config::COORDINATOR_OUTGOING_QUEUE_CAPACITY)},
              tasks_{},
              client_fds_{},
              early_complete_reps_{},
              replication_log_(config::MAX_LOG_SIZE),
              queued_backlog_{},
              completed_backlog_{},
              workers_states_{},
              active_workers_{},
              available_ids_{},
              metrics_{},
              peer_{std::move(peer)} {}

    Coordinator::~Coordinator() {
        stop();
    }

    bool Coordinator::try_pop_incoming(coordinator::IncomingEvent &event, lock_free::SPSCQueue<IncomingEvent> &queue) {
        uint32_t retry = 0;
        uint32_t backoff_us = 1;
        while (!queue.try_pop(event)) {
            if (++retry > config::QUEUE_RETRY_COUNT) return false;
            std::this_thread::sleep_for(std::chrono::microseconds(backoff_us));
            backoff_us = std::min(backoff_us << 1, config::MAX_BACKOFF_US);
        }

        return true;
    }

    bool Coordinator::try_push_outgoing(const coordinator::OutgoingEvent &event,
                                        lock_free::SPSCQueue<OutgoingEvent> &queue) {
        uint32_t retry = 0;
        uint32_t backoff_us = 1;
        while (!queue.try_push(event)) {
            if (++retry > config::QUEUE_RETRY_COUNT) return false;
            std::this_thread::sleep_for(std::chrono::microseconds(backoff_us));
            backoff_us = std::min(backoff_us << 1, config::MAX_BACKOFF_US);
        }

        return true;
    }

    bool Coordinator::try_push_incoming(const IncomingEvent &event, lock_free::SPSCQueue<IncomingEvent> &queue) {
        uint32_t retry = 0;
        uint32_t backoff_us = 1;
        while (!queue.try_push(event)) {
            if (++retry > config::QUEUE_RETRY_COUNT) return false;
            std::this_thread::sleep_for(std::chrono::microseconds(backoff_us));
            backoff_us <<= 1;
            backoff_us = std::min(backoff_us, config::MAX_BACKOFF_US);
        }

        return true;
    }

    void Coordinator::record_enqueue(Metrics &metrics) {
        metrics.tasks_queued_++;
    }

    void Coordinator::record_assignment(Metrics &metrics) {
        // Update metrics
        metrics.tasks_queued_ -= (metrics.tasks_queued_ > 0) ? 1 : 0;
        metrics.tasks_running_++;
    }

    void Coordinator::record_completion_worker(WorkerState &worker_state, const task::Task &task) {
        worker_state.tasks_completed_++;

        uint64_t latency = task.completed_at_ - task.started_at_;
        worker_state.total_latency_ns_ += latency;
    }

    void Coordinator::record_completion_system(Metrics &metrics, const task::Task &task) {
        metrics.tasks_running_ -= (metrics.tasks_running_ > 0) ? 1 : 0;
        metrics.tasks_completed_++;
        uint64_t latency = task.completed_at_ - task.queued_at_;
        metrics.latencies_.push_back(latency);

        if (metrics.latencies_.size() > config::MAX_LATENCY_SAMPLES) {
            metrics.latencies_.pop_front();
        }

        metrics.total_latency_ns += latency;

        if (metrics.tasks_completed_ == 1) {
            metrics.min_latency_ns = latency;
        } else {
            metrics.min_latency_ns = std::min(metrics.min_latency_ns, latency);
        }

        metrics.max_latency_ns = std::max(metrics.max_latency_ns, latency);
        metrics.completion_times_ns_.push_back(task.completed_at_);
    }

    uint64_t Coordinator::get_rolling_throughput(Metrics &metrics) {
        uint64_t now = utils::now_ns_u64();

        while (!metrics.completion_times_ns_.empty() &&
               now - metrics.completion_times_ns_.front() > config::THROUGHPUT_WINDOW_NS) {
            metrics.completion_times_ns_.pop_front();
        }

        return metrics.completion_times_ns_.size();
    }

    void Coordinator::add_to_log(const message::Message &msg) {
        size_t next = (replication_write_ptr_ + 1) % replication_log_.size();

        // drop
        if (next == replication_read_ptr_) {
            replication_read_ptr_ = (replication_read_ptr_ + 1) % replication_log_.size();
        }

        replication_log_[replication_write_ptr_] = msg;
        replication_write_ptr_ = next;
    }

    void Coordinator::mark_inactive(uint32_t worker_id) {
        if (active_workers_.empty()) return;
        auto active_index = workers_states_[worker_id].active_index_;
        if (active_index >= active_workers_.size()) return;
        auto last_worker_id = active_workers_.back();
        std::swap(active_workers_[active_index], active_workers_.back());
        active_workers_.pop_back();
        workers_states_[last_worker_id].active_index_ = active_index;
    }

    bool Coordinator::is_alive(uint32_t worker_id) {
        auto &state = workers_states_[worker_id];

        if (!state.alive_) {
            return false;
        }

        auto now = utils::now_ns_u64();

        if (now - state.last_heartbeat_ns_ <= config::HEARTBEAT_TIMEOUT_NS) return true;

        mark_inactive(worker_id);
        available_ids_.push_back(worker_id);

        state.mark_dead();
        auto &running_tasks = state.running_tasks_;
        while (!running_tasks.empty()) {
            auto it = running_tasks.begin();
            auto task_id = *it;
            auto &task = tasks_.at(task_id);
            task.state_ = task::TaskState::QUEUED;
            queued_backlog_.push_back(task_id);
            state.running_tasks_.erase(it);
        }

        return false;
    }

    void Coordinator::sweep_workers() {
        auto worker_count = static_cast<uint32_t>(workers_states_.size());

        for (uint32_t worker_id = 0; worker_id < worker_count; worker_id++) {
            is_alive(worker_id);
        }
    }

    void Coordinator::replicate() {
        while (replication_read_ptr_ != replication_write_ptr_) {
            const auto &msg = replication_log_[replication_read_ptr_];

            OutgoingEvent event{};
            event.msg_ = msg;

            if (!try_push_outgoing(event, *outgoing_queue_)) return;

            replication_read_ptr_ = (replication_read_ptr_ + 1) % replication_log_.size();
        }
    }

    bool Coordinator::assign(task::Task &task) {
        bool assigned = false;

        uint32_t num_active = active_workers_.size();
        for (uint32_t i = 0; i < num_active; i++) {
            size_t active_idx = (rr_index_ + i) % active_workers_.size();
            auto worker_id = active_workers_[active_idx];
            if (workers_states_[worker_id].running_tasks_.size() >= config::WORKER_MAX_QUEUE_DEPTH) {
                continue;
            }

            auto &worker_state = workers_states_[worker_id];
            message::Message msg{message::MessageType::ASSIGN};
            msg.assign_.type_ = task.type_;
            msg.assign_.task_id_ = task.id_;
            msg.epoch_ = current_epoch_;
            switch (task.type_) {
                case task::TaskType::SYNTHETIC:
                    msg.assign_.duration_us_ = task.synthetic_task_.duration_us_;
                    break;
                case task::TaskType::WORD_COUNT:
                    msg.assign_.words_ = task.word_count_task_.words_;
                    break;
                default:
                    break;
            }

            // Try sending outgoing event
            OutgoingEvent event{worker_state.fd_, msg};
            if (!try_push_outgoing(event, *outgoing_queue_)) return false;

            if (config::DEBUG) {
                std::cout << "Assigning task " << task.id_ << " to worker\n";
            }

            rr_index_ = (active_idx + 1) % active_workers_.size(); // Increment when we actually assign

            // Update states
            task.worker_id_ = worker_id;
            task.state_ = task::TaskState::RUNNING;
            task.started_at_ = utils::now_ns_u64();
            worker_state.running_tasks_.insert(task.id_);

            record_assignment(metrics_);

            // Add assign operation for replication
            message::Message assign_rep{message::MessageType::ASSIGNED_REPLICATE};
            assign_rep.epoch_ = current_epoch_;
            assign_rep.assigned_rep_ = message::AssignedReplicationMsg{task.id_, task.queued_at_, task.started_at_,
                                                                       task.client_id_,
                                                                       task.worker_id_, task.type_};
            switch (task.type_) {
                case task::TaskType::SYNTHETIC:
                    assign_rep.assigned_rep_.duration_us_ = task.synthetic_task_.duration_us_;
                    break;
                case task::TaskType::WORD_COUNT:
                    assign_rep.assigned_rep_.words_ = task.word_count_task_.words_;
                    break;
                default:
                    break;
            }

            add_to_log(assign_rep);

            assigned = true;
            break;
        }

        return assigned;
    }

    bool Coordinator::send_result(task::Task &task) {
        message::Message msg{message::MessageType::RESULT};
        msg.result_.success_ = task.success_;
        msg.result_.task_id_ = task.id_;

        if (task.type_ == task::TaskType::WORD_COUNT) msg.result_.result_ = task.result_;
        msg.epoch_ = current_epoch_;

        auto it = client_fds_.find(task.client_id_);
        int fd = -1;
        if (it != client_fds_.end()) {
            fd = it->second;
        }
        OutgoingEvent event{fd, msg};
        if (!try_push_outgoing(event, *outgoing_queue_)) return false;

        message::Message complete_rep{message::MessageType::COMPLETED_REPLICATE};
        complete_rep.epoch_ = current_epoch_;
        complete_rep.completed_rep_ = message::CompletedReplicationMsg{task.id_, task.started_at_, task.completed_at_,
                                                                       task.worker_id_, task.result_, task.success_};
        add_to_log(complete_rep);

        return true;
    }

    void Coordinator::process_backlog() {
        while (!completed_backlog_.empty()) {
            auto task_id = completed_backlog_.front();
            auto it = tasks_.find(task_id);
            if (it == tasks_.end()) {
                completed_backlog_.pop_front();
                continue;
            }

            auto &task = it->second;
            if (send_result(task)) {
                completed_backlog_.pop_front();
                replicate();
            } else {
                break;
            }
        }

        while (!queued_backlog_.empty()) {
            auto task_id = queued_backlog_.front();
            auto &task = tasks_.at(task_id);
            if (assign(task)) {
                queued_backlog_.pop_front();
                replicate();
            } else {
                break;
            }
        }
    }

    void Coordinator::process_submit_event(const IncomingEvent &event) {
        is_primary_ = true;
        auto &submit_msg = event.msg_.submit_;
        client_fds_[submit_msg.client_id_] = event.fd_;
        task::Task task{submit_msg.task_id_, submit_msg.client_id_, submit_msg.type_};
        switch (task.type_) {
            case task::TaskType::SYNTHETIC:
                task.synthetic_task_ = task::SyntheticTask{submit_msg.duration_us_};
                break;
            case task::TaskType::WORD_COUNT:
                task.word_count_task_ = task::WordCountTask{submit_msg.words_};
                break;
            default:
                break;
        }

        tasks_[task.id_] = task;
        auto &stored_task = tasks_[task.id_];
        record_enqueue(metrics_);
        if (!assign(stored_task)) {
            queued_backlog_.push_back(stored_task.id_);
        }
        replicate();
    }

    void Coordinator::process_complete_event(const IncomingEvent &event) {
        is_primary_ = true;
        auto &complete_msg = event.msg_.complete_;
        auto it = tasks_.find(complete_msg.task_id_);
        if (it == tasks_.end()) return;

        auto &task = it->second;
        task.started_at_ = complete_msg.started_at_;
        task.completed_at_ = complete_msg.completed_at_;
        task.state_ = task::TaskState::COMPLETED;
        task.success_ = complete_msg.success_;

        auto &worker_state = workers_states_[task.worker_id_];
        worker_state.running_tasks_.erase(task.id_);

        switch (task.type_) {
            case task::TaskType::WORD_COUNT:
                task.result_ = complete_msg.result_;
                break;
            default:
                task.result_ = {};
                break;
        }

        // Update metrics
        record_completion_system(metrics_, task);
        record_completion_worker(worker_state, task);

        if (!send_result(task)) {
            completed_backlog_.push_back(task.id_);
        }
        replicate();
    }

    void Coordinator::process_assign_replicate_event(const IncomingEvent &event) {
        is_primary_ = false;
        if (event.msg_.epoch_ < current_epoch_) return;
        current_epoch_ = event.msg_.epoch_;
        const auto &assign_rep_msg = event.msg_.assigned_rep_;
        task::Task task{assign_rep_msg.task_id_, assign_rep_msg.client_id_, assign_rep_msg.task_type_};
        task.worker_id_ = assign_rep_msg.worker_id_;
        task.queued_at_ = assign_rep_msg.queued_at_;
        task.started_at_ = assign_rep_msg.started_at_;
        task.state_ = task::TaskState::RUNNING;
        switch (task.type_) {
            case task::TaskType::SYNTHETIC:
                task.synthetic_task_.duration_us_ = assign_rep_msg.duration_us_;
                break;
            case task::TaskType::WORD_COUNT:
                task.word_count_task_.words_ = assign_rep_msg.words_;
                break;
            default:
                break;
        }

        tasks_[task.id_] = task;

        // Update metrics
        record_assignment(metrics_);

        auto it = early_complete_reps_.find(task.id_);
        if (it != early_complete_reps_.end()) {
            process_complete_replicate_event(it->second);
            early_complete_reps_.erase(it);
        }
    }

    void Coordinator::process_complete_replicate_event(const IncomingEvent &event) {
        is_primary_ = false;
        if (event.msg_.epoch_ < current_epoch_) return;
        const auto &completed_rep_msg = event.msg_.completed_rep_;
        auto it = tasks_.find(completed_rep_msg.task_id_);
        // check completed for new client runs
        if (it == tasks_.end() || it->second.state_ == task::TaskState::COMPLETED) {
            early_complete_reps_[completed_rep_msg.task_id_] = event;
            return;
        }

        current_epoch_ = event.msg_.epoch_;
        auto &task = it->second;
        task.state_ = task::TaskState::COMPLETED;
        task.worker_id_ = completed_rep_msg.worker_id_;
        task.started_at_ = completed_rep_msg.started_at_;
        task.completed_at_ = completed_rep_msg.completed_at_;
        task.success_ = completed_rep_msg.success_;
        task.result_ = completed_rep_msg.result_;

        // Update metrics
        record_completion_system(metrics_, task);
    }

    void Coordinator::process_heartbeat_event(const IncomingEvent &event) {
        const auto &msg = event.msg_.heartbeat_;

        auto &state = workers_states_[msg.worker_id_];
        state.last_heartbeat_ns_ = utils::now_ns_u64();
        state.alive_ = true;
    }

    void Coordinator::process_register_event(const IncomingEvent &event) {
        uint32_t worker_id;
        if (!available_ids_.empty()) {
            worker_id = available_ids_.back();
        } else {
            worker_id = static_cast<uint32_t>(workers_states_.size());
        }

        message::Message msg{message::MessageType::ACKNOWLEDGE};
        msg.acknowledge_ = message::AcknowledgeMsg{worker_id};
        OutgoingEvent out_event{event.fd_, msg};
        if (!try_push_outgoing(out_event, *outgoing_queue_)) return;

        if (!available_ids_.empty()) {
            available_ids_.pop_back();
            auto &state = workers_states_[worker_id];
            state.alive_ = true;
            state.last_heartbeat_ns_ = utils::now_ns_u64();
            state.fd_ = event.fd_;
        } else {
            workers_states_.emplace_back(event.fd_);
            auto &state = workers_states_[worker_id];
            state.alive_ = true;
            state.last_heartbeat_ns_ = utils::now_ns_u64();
        }

        workers_states_[worker_id].active_index_ = active_workers_.size();
        active_workers_.push_back(worker_id);
    }

    void Coordinator::process_snapshot_event(const IncomingEvent &event) {
        is_primary_ = false;
        current_epoch_ = event.msg_.epoch_;
        replication_read_ptr_ = 0;
        tasks_ = event.msg_.snapshot_.tasks_;

        queued_backlog_.clear();
        completed_backlog_.clear();
        active_workers_.clear();
        early_complete_reps_.clear();
        available_ids_.clear();
        replication_log_.clear();
        client_fds_.clear();
    }

    void Coordinator::process_peer_reconnected() {
        if (!is_primary_) return;
        message::Message msg{message::MessageType::SNAPSHOT};
        msg.snapshot_ = message::SnapshotMsg(tasks_);
        msg.epoch_ = ++current_epoch_;

        OutgoingEvent event{};
        event.msg_ = msg;

        if (!try_push_outgoing(event, *outgoing_queue_)) return;
    }

    void Coordinator::process_top_request(const IncomingEvent &event) {
        message::Message msg{message::MessageType::TOP_RESPONSE};

        auto &coordinator_metrics = msg.top_response_.coordinator_metrics_;

        // ---- Copy latencies for percentile computation ----
        std::vector<uint64_t> copy(metrics_.latencies_.begin(), metrics_.latencies_.end());

        if (!copy.empty()) {
            size_t size = copy.size();

            // ---- p95 ----
            size_t p95_idx = (size * 95) / 100;
            std::nth_element(copy.begin(), copy.begin() + p95_idx, copy.end());
            coordinator_metrics.p95_latency_us_ = copy[p95_idx] / 1000;

            // ---- min / max (use minmax_element instead of nth_element again) ----
            std::pair<std::vector<uint64_t>::iterator, std::vector<uint64_t>::iterator> minmax =
                    std::minmax_element(copy.begin(), copy.end());

            coordinator_metrics.min_latency_us_ = *minmax.first / 1000;
            coordinator_metrics.max_latency_us_ = *minmax.second / 1000;
        } else {
            coordinator_metrics.p95_latency_us_ = 0;
            coordinator_metrics.min_latency_us_ = 0;
            coordinator_metrics.max_latency_us_ = 0;
        }

        // ---- avg latency (guard divide-by-zero) ----
        if (metrics_.tasks_completed_ > 0) {
            coordinator_metrics.avg_latency_us_ =
                    metrics_.total_latency_ns / (metrics_.tasks_completed_ * 1000);
        } else {
            coordinator_metrics.avg_latency_us_ = 0;
        }

        // ---- basic counters ----
        coordinator_metrics.tasks_completed_ = metrics_.tasks_completed_;
        coordinator_metrics.tasks_running_ = metrics_.tasks_running_;
        coordinator_metrics.tasks_queued_ = metrics_.tasks_queued_;

        coordinator_metrics.throughput_ = get_rolling_throughput(metrics_);
        coordinator_metrics.active_workers_ = active_workers_.size();
        coordinator_metrics.is_primary_ = static_cast<uint8_t>(is_primary_);

        // ---- worker metrics ----
        auto &workers_metrics = msg.top_response_.workers_metrics_;

        uint64_t now = utils::now_ns_u64();  // compute once

        size_t num_workers = workers_states_.size();
        for (size_t id = 0; id < num_workers; id++) {
            const auto &worker_state = workers_states_[id];
            top::WorkerMetrics worker_metrics{};

            // avg latency (guard divide-by-zero)
            if (worker_state.tasks_completed_ > 0) {
                worker_metrics.avg_latency_us_ =
                        worker_state.total_latency_ns_ / (worker_state.tasks_completed_ * 1000);
            } else {
                worker_metrics.avg_latency_us_ = 0;
            }

            worker_metrics.tasks_completed_ = worker_state.tasks_completed_;
            worker_metrics.tasks_running_ = worker_state.running_tasks_.size();

            // correct heartbeat: delta, not absolute
            worker_metrics.last_heartbeat_ms_ =
                    (now - worker_state.last_heartbeat_ns_) / 1000000;

            worker_metrics.worker_id_ = id;
            worker_metrics.alive_ = static_cast<uint8_t>(worker_state.alive_);

            workers_metrics.push_back(worker_metrics);
        }

        // ---- send response (blocking with backoff) ----
        OutgoingEvent out(event.fd_, msg);
        uint32_t backoff_us = 1;
        while (!try_push_outgoing(out, *outgoing_queue_)) {
            std::this_thread::sleep_for(std::chrono::microseconds(backoff_us));
            backoff_us = std::min(backoff_us << 1, config::MAX_BACKOFF_US);
        }
    }

    void Coordinator::process_control_queue(lock_free::SPSCQueue<IncomingEvent> &queue) {
        uint64_t processed = 0;
        IncomingEvent event{};
        while (processed < config::CONTROL_BUDGET && try_pop_incoming(event, queue)) {
            switch (event.msg_.type_) {
                case message::MessageType::HEARTBEAT:
                    process_heartbeat_event(event);
                    processed++;
                    break;
                case message::MessageType::REGISTER:
                    process_register_event(event);
                    processed++;
                    break;
                default:
                    std::cerr << "Unknown message type: "
                              << static_cast<int>(event.msg_.type_) << "\n";
                    break;
            }
        }
    }

    void Coordinator::process_task_queue(lock_free::SPSCQueue<IncomingEvent> &queue) {
        uint64_t limit = active_workers_.empty() ?
                         config::TASK_FALLBACK_BUDGET :
                         1ULL * active_workers_.size() * config::WORKER_MAX_QUEUE_DEPTH;
        uint64_t processed = 0;
        IncomingEvent event{};
        while (processed < limit && try_pop_incoming(event, queue)) {
            switch (event.msg_.type_) {
                case message::MessageType::SUBMIT:
                    process_submit_event(event);
                    break;
                case message::MessageType::COMPLETE:
                    process_complete_event(event);
                    break;
                case message::MessageType::ASSIGNED_REPLICATE:
                    process_assign_replicate_event(event);
                    break;
                case message::MessageType::COMPLETED_REPLICATE:
                    process_complete_replicate_event(event);
                    break;
                case message::MessageType::SNAPSHOT:
                    process_snapshot_event(event);
                    break;
                case message::MessageType::PEER_RECONNECTED:
                    process_peer_reconnected();
                    break;
                case message::MessageType::TOP_REQUEST:
                    process_top_request(event);
                    break;
                default:
                    std::cerr << "Unknown message type: "
                              << static_cast<int>(event.msg_.type_) << "\n";
                    break;
            }
            processed++;
        }
    }

    void Coordinator::scheduler_worker() {
        while (running_.load(std::memory_order_acquire)) {
            if (is_primary_) {
                process_backlog();
                replicate();
            }

            process_control_queue(*control_queue_);
            process_task_queue(*task_queue_);

            auto now = utils::now_ns_u64();
            if (now - last_sweep_ >= config::SWEEP_INTERVAL_NS) {
                sweep_workers();
                last_sweep_ = utils::now_ns_u64();
            }
        }
    }

    bool Coordinator::connect_to_peer(PeerNode &peer) {
        peer.connection_.reset(new rpc::Client{peer.ip_, peer.port_});
        if (peer.connection_->fd() < 0) {
            peer.alive_ = false;
            peer.connection_ = nullptr;
            return false;
        }

        return true;
    }

    void Coordinator::reconnect_to_peer(PeerNode &peer) {
        if (connect_to_peer(peer)) {
            message::Message msg{message::MessageType::PEER_RECONNECTED};
            IncomingEvent event{};
            event.msg_ = msg;

            if (!try_push_incoming(event, *task_queue_)) return;

            peer.alive_ = true;
        }
    }

    void Coordinator::send_replication_message(const OutgoingEvent &event, PeerNode &peer) {
        if (!peer.alive_ || !peer.connection_ || peer.connection_->fd() < 0) {
            reconnect_to_peer(peer);
            return;
        }

        if (net::send_message_with_retry(peer.connection_->fd(), event.msg_, config::CONNECTION_RETRY_COUNT) <= 0) {
            peer.alive_ = false;
            peer.connection_ = nullptr;
            reconnect_to_peer(peer);
        }
    }

    bool Coordinator::communicate_incoming_event(const IncomingEvent &event) {
        switch (event.msg_.type_) {
            case message::MessageType::HEARTBEAT:
            case message::MessageType::REGISTER:
                return try_push_incoming(event, *control_queue_);
            default:
                return try_push_incoming(event, *task_queue_);
        }
    }

    void Coordinator::network_worker() {

        int listen_fd = net::create_listening_socket(port_, config::BACKLOG);
        if (listen_fd < 0) return;

        std::deque<IncomingEvent> backlog;

        std::vector<int> conns;
        std::vector<pollfd> pfds;
        pfds.reserve(128);

        if (connect_to_peer(peer_)) {
            peer_.alive_ = true;
        }

        while (running_.load(std::memory_order_acquire)) {

            // Process backlog
            int backlog_budget = 64;
            while (!backlog.empty() && backlog_budget--) {
                if (!communicate_incoming_event(backlog.front())) break;
                backlog.pop_front();
            }

            // Build Poll set
            pfds.clear();

            // listening socket
            pfds.push_back({listen_fd, POLLIN, 0});

            // peer
            if (peer_.alive_ && peer_.connection_) {
                pfds.push_back({peer_.connection_->fd(), POLLIN, 0});
            }

            // Workers / Clients
            for (int fd: conns) {
                pfds.push_back({fd, POLLIN, 0});
            }

            // POLL
            int ret = poll(pfds.data(), pfds.size(), 1);

            if (ret > 0) {

                for (auto &pfd: pfds) {

                    // Error
                    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {

                        if (peer_.connection_ && pfd.fd == peer_.connection_->fd()) {
                            peer_.alive_ = false;
                            peer_.connection_ = nullptr;
                        } else {
                            close(pfd.fd);
                            conns.erase(std::remove(conns.begin(), conns.end(), pfd.fd), conns.end());
                        }

                        continue;
                    }

                    if (!(pfd.revents & POLLIN)) continue;

                    // New connection
                    if (pfd.fd == listen_fd) {

                        int fd = accept(listen_fd, nullptr, nullptr);
                        if (fd >= 0) {
                            conns.push_back(fd);
                        }
                        continue;
                    }

                    // Existing connection
                    message::Message msg{};
                    int n = net::receive_message_with_retry(pfd.fd, msg, config::CONNECTION_RETRY_COUNT);

                    if (n == -1) {
                        close(pfd.fd);
                        conns.erase(std::remove(conns.begin(), conns.end(), pfd.fd), conns.end());
                        continue;
                    } else if (n == -3) {
                        // invalid message size
                        continue;
                    }

                    if (config::DEBUG) {
                        std::cout << "Coordinator received message from fd " << pfd.fd << "\n";
                    }

                    IncomingEvent event{};
                    event.fd_ = pfd.fd;
                    event.msg_ = std::move(msg);

                    if (!communicate_incoming_event(event)) {
                        backlog.push_back(event);
                    }

                    if (config::DEBUG) {
                        std::cout << "Tried Pushing to incoming queue\n";
                    }
                }
            }



            // Outgoing
            size_t processed = 0;
            OutgoingEvent out{};
            while (processed < config::COORDINATOR_OUT_BUDGET && outgoing_queue_->try_pop(out)) {
                if (out.msg_.is_replication()) {
                    send_replication_message(out, peer_);
                } else {
                    if (out.fd_ >= 0) {
                        // If we can't send then worker or router is dead
                        net::send_message_with_retry(out.fd_, out.msg_, config::CONNECTION_RETRY_COUNT);
                    }
                }
                processed++;
            }
        }

        close(listen_fd);
        running_.store(false, std::memory_order_release);
    }

    void Coordinator::stop() {
        running_.store(false, std::memory_order_release);

        if (network_thread_.joinable()) network_thread_.join();

        if (scheduler_thread_.joinable()) scheduler_thread_.join();
    }

    void Coordinator::run() {
        running_.store(true, std::memory_order_release);

        network_thread_ = std::thread{&Coordinator::network_worker, this};
        scheduler_thread_ = std::thread{&Coordinator::scheduler_worker, this};

        network_thread_.join();
        scheduler_thread_.join();
    }
} // namespace coordinator
