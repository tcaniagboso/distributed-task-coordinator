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
              replication_start_index_{0},
              port_{port},
              is_primary_{false},
              running_{false},
              network_thread_{},
              scheduler_thread_{},
              incoming_queue_{new lock_free::SPSCQueue<IncomingEvent>(config::COORDINATOR_QUEUE_CAPACITY)},
              outgoing_queue_{new lock_free::SPSCQueue<OutgoingEvent>(config::COORDINATOR_QUEUE_CAPACITY)},
              tasks_{},
              client_fds_{},
              early_complete_reps_{},
              replication_log_{},
              queued_backlog_{},
              completed_backlog_{},
              workers_states_{},
              active_workers_{},
              available_ids_{},
              peer_{std::move(peer)} {}

    Coordinator::~Coordinator() {
        stop();
    }

    bool Coordinator::try_push_outgoing(const coordinator::OutgoingEvent &event) {
        int retry = 0;
        while (!outgoing_queue_->try_push(event)) {
            if (++retry > config::RETRY_COUNT) return false;
            std::this_thread::yield();
        }

        return true;
    }

    bool Coordinator::try_push_incoming(const IncomingEvent &event) {
        int retry = 0;
        while (!incoming_queue_->try_push(event)) {
            if (++retry > config::RETRY_COUNT) return false;
            std::this_thread::yield();
        }

        return true;
    }

    void Coordinator::add_to_log(const message::Message &msg) {
        replication_log_.push_back(msg);
        if (replication_log_.size() > config::MAX_LOG_SIZE) {
            replication_log_.pop_front();
        }
    }

    void Coordinator::mark_inactive(uint32_t worker_id) {
        auto active_index = workers_states_[worker_id].active_index_;
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

        if (state.alive_) {
            mark_inactive(worker_id);
            available_ids_.push_back(worker_id);
        }

        state.mark_dead();

        auto &running_tasks = state.running_tasks_;

        while (!running_tasks.empty()) {
            auto task_id = *running_tasks.begin();
            auto &task = tasks_[task_id];
            task.state_ = task::TaskState::QUEUED;
            queued_backlog_.push_back(task_id);
            state.running_tasks_.erase(state.running_tasks_.begin());
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
        size_t num_operations = replication_log_.size();
        while (replication_start_index_ < num_operations) {
            const auto &msg = replication_log_[replication_start_index_];

            OutgoingEvent event{};
            event.msg_ = msg;

            if (!try_push_outgoing(event)) return;

            replication_start_index_++;
        }
    }

    bool Coordinator::assign(task::Task &task) {
        bool assigned = false;

        uint32_t num_active = active_workers_.size();
        for (uint32_t i = 0; i < num_active; i++) {
            size_t active_idx = (rr_index_ + i) % active_workers_.size();
            auto worker_id = active_workers_[active_idx];
            if (workers_states_[worker_id].running_tasks_.size() >= config::MAX_QUEUE_DEPTH) {
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
            if (!try_push_outgoing(event)) return false;

            if (config::DEBUG) {
                std::cout << "Assigning task " << task.id_ << " to worker\n";
            }

            rr_index_ = (active_idx + 1) % active_workers_.size(); // Increment when we actually assign

            // Update states
            task.worker_id_ = worker_id;
            task.state_ = task::TaskState::RUNNING;
            task.started_at_ = utils::now_ns_u64();
            worker_state.running_tasks_.insert(task.id_);

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
        msg.result_.result_ = task.result_;
        msg.epoch_ = current_epoch_;

        auto it = client_fds_.find(task.client_id_);
        int fd = -1;
        if (it != client_fds_.end()) {
            fd = it->second;
        }
        OutgoingEvent event{fd, msg};
        if (!try_push_outgoing(event)) return false;

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
                break;
        }

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
        if (it == tasks_.end()) {
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
    }

    void Coordinator::process_heartbeat_event(const coordinator::IncomingEvent &event) {
        const auto &msg = event.msg_.heartbeat_;

        auto &state = workers_states_[msg.worker_id_];
        state.last_heartbeat_ns_ = utils::now_ns_u64();
        state.alive_ = true;
    }

    void Coordinator::process_register_event(const coordinator::IncomingEvent &event) {
        uint32_t worker_id;
        if (!available_ids_.empty()) {
            worker_id = available_ids_.back();
        } else {
            worker_id = static_cast<uint32_t>(workers_states_.size());
        }

        message::Message msg{message::MessageType::ACKNOWLEDGE};
        msg.acknowledge_ = message::AcknowledgeMsg{worker_id};
        OutgoingEvent out_event{event.fd_, msg};
        if (!try_push_outgoing(out_event)) return;

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

    void Coordinator::process_snapshot_event(const coordinator::IncomingEvent &event) {
        is_primary_ = false;
        current_epoch_ = event.msg_.epoch_;
        replication_start_index_ = 0;
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

        if (!try_push_outgoing(event)) return;
    }

    void Coordinator::scheduler_worker() {
        while (running_.load(std::memory_order_acquire)) {
            if (is_primary_) {
                replicate();
                process_backlog();
            }

            IncomingEvent event{};
            if (!incoming_queue_->try_pop(event)) {
                std::this_thread::yield();
                continue;
            }

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
                case message::MessageType::HEARTBEAT:
                    process_heartbeat_event(event);
                    break;
                case message::MessageType::REGISTER:
                    process_register_event(event);
                    break;
                case message::MessageType::SNAPSHOT:
                    process_snapshot_event(event);
                    break;
                case message::MessageType::PEER_RECONNECTED:
                    process_peer_reconnected();
                    break;
                default:
                    std::cerr << "Unknown message type: "
                              << static_cast<int>(event.msg_.type_) << "\n";
                    break;
            }

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

            if (!try_push_incoming(event)) return;

            peer.alive_ = true;
        }
    }

    void Coordinator::send_replication_message(const OutgoingEvent &event, PeerNode &peer) {
        if (!peer.alive_ || !peer.connection_ || peer.connection_->fd() < 0) {
            reconnect_to_peer(peer);
            return;
        }

        if (net::send_message(peer.connection_->fd(), event.msg_) <= 0) {
            peer.alive_ = false;
            peer.connection_ = nullptr;
            reconnect_to_peer(peer);
        }
    }

    void Coordinator::network_worker() {

        int listen_fd = net::create_listening_socket(port_, config::BACKLOG);
        if (listen_fd < 0) return;

        std::vector<int> conns;
        std::vector<pollfd> pfds;
        pfds.reserve(128);

        if (connect_to_peer(peer_)) {
            peer_.alive_ = true;
        }

        while (running_.load(std::memory_order_acquire)) {

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
            int ret = poll(pfds.data(), pfds.size(), 10);

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
                    int n = net::receive_message(pfd.fd, msg);

                    if (n <= 0) {
                        close(pfd.fd);
                        conns.erase(std::remove(conns.begin(), conns.end(), pfd.fd), conns.end());
                        continue;
                    }

                    if (config::DEBUG) {
                        std::cout << "Coordinator received message from fd " << pfd.fd << "\n";
                    }

                    IncomingEvent event{};
                    event.fd_ = pfd.fd;
                    event.msg_ = std::move(msg);

                    try_push_incoming(event);

                    if (config::DEBUG) {
                        std::cout << "Pushed to incoming queue\n";
                    }
                }
            }



            // Outgoing
            OutgoingEvent out{};
            while (outgoing_queue_->try_pop(out)) {

                if (out.msg_.is_replication()) {
                    send_replication_message(out, peer_);
                } else {
                    if (out.fd_ >= 0) {
                        net::send_message(out.fd_, out.msg_);
                    }
                }
            }
        }

        close(listen_fd);
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
