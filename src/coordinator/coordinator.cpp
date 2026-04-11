#include "../../include/coordinator/coordinator.hpp"
#include "../../include/config/system_config.hpp"

namespace coordinator {

    Coordinator::Coordinator(std::string ip, uint16_t port, coordinator::PeerNode peer)
            : stats_{},
              workers_count_{0},
              replication_start_index_{0},
              port_{port},
              is_primary_{false},
              running_{false},
              ip_{std::move(ip)},
              network_thread_{},
              scheduler_thread_{},
              incoming_queue_{new lock_free::SPSCQueue<IncomingEvent>(config::COORDINATOR_QUEUE_CAPACITY)},
              outgoing_queue_{new lock_free::SPSCQueue<OutgoingEvent>(config::COORDINATOR_QUEUE_CAPACITY)},
              tasks_{},
              operations_{},
              workers_{},
              available_ids_{},
              worker_ring_{},
              queued_backlog_{},
              completed_backlog_{},
              peer_{std::move(peer)} {}

    Coordinator::~Coordinator() {
        stop();
    }

    void Coordinator::replicate() {
        size_t num_operations = operations_.size();
        while (replication_start_index_ < num_operations) {
            auto &operation = operations_[replication_start_index_];
            message::Message msg{};
            switch (operation.type_) {
                case OperationType::QUEUE:
                    msg.type_ = message::MessageType::QUEUED_REPLICATE;
                    msg.queued_rep_ = message::QueuedReplicationMsg(operation.task_id_, operation.queued_at_,
                                                                    operation.client_id_);
                    break;
                case OperationType::ASSIGN:
                    msg.type_ = message::MessageType::ASSIGNED_REPLICATE;
                    msg.assigned_rep_ = message::AssignedReplicationMsg(operation.task_id_, operation.worker_id_);
                    break;
                case OperationType::COMPLETE:
                    msg.type_ = message::MessageType::COMPLETED_REPLICATE;
                    msg.completed_rep_ = message::CompletedReplicationMsg(operation.task_id_, operation.started_at_,
                                                                          operation.completed_at_,
                                                                          operation.worker_id_);
                    break;
                default:
                    break;
            }

            OutgoingEvent event{};
            event.msg_ = msg;

            int retry = 0;

            while (!outgoing_queue_->try_push(event)) {
                retry++;
                if (retry > config::RETRY_COUNT) {
                    return;
                }
            }

            replication_start_index_++;
        }
    }

    bool Coordinator::assign(task::Task &task) {
        bool assigned = false;

        while (!worker_ring_.empty()) {
            auto worker_id = worker_ring_.front();
            if (!workers_[worker_id].alive_) {
                worker_ring_.pop_front();
                continue;
            }

            auto &worker_state = workers_[worker_id];
            message::Message msg{message::MessageType::ASSIGN};
            msg.assign_.type_ = task.type_;
            msg.assign_.task_id_ = task.id_;
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
            int retry = 0;

            while (!outgoing_queue_->try_push(event)) {
                retry++;

                if (retry > config::RETRY_COUNT) return false;
            }

            // Update states
            task.worker_id_ = worker_id;
            task.state_ = task::TaskState::RUNNING;
            worker_state.running_tasks_.insert(task.id_);
            auto &worker_stats = worker_state.stats_;
            worker_stats.total_running_count_++;
            stats_.total_running_count_++;
            stats_.total_queued_count_--;
            switch (task.type_) {
                case task::TaskType::SYNTHETIC:
                    worker_stats.synthetic_running_count_++;
                    stats_.synthetic_queued_count_--;
                    stats_.synthetic_running_count_++;
                    break;
                case task::TaskType::WORD_COUNT:
                    worker_stats.word_count_running_count_++;
                    stats_.word_count_queued_count_--;
                    stats_.word_count_running_count_++;
                    break;
                default:
                    break;
            }

            // Add assign operation for replication
            Operation operation{task.id_, OperationType::ASSIGN, task.type_};
            operation.client_id_ = task.client_id_; // we don't have to do this
            operation.queued_at_ = task.queued_at_; // we don't have to do this
            operation.worker_id_ = worker_id;
            operations_.push_back(operation);
            worker_ring_.pop_front();

            if (worker_state.running_tasks_.size() < config::MAX_QUEUE_DEPTH) {
                worker_ring_.push_back(worker_id);
            }

            assigned = true;
            break;
        }

        return assigned;
    }

    bool Coordinator::send_result(task::Task &task) {
        message::Message msg{message::MessageType::RESULT};
        msg.result_.success_ = task.success_;
        msg.result_.result_ = task.id_;
        if (task.type_ == task::TaskType::WORD_COUNT) msg.result_.success_ = task.word_count_task_.result_;

        OutgoingEvent event{task.router_fd_, msg};
        int retry = 0;
        while (!outgoing_queue_->try_push(event)) {
            retry++;
            if (retry > config::RETRY_COUNT) {
                return false;
            }
        }

        Operation operation{task.id_, OperationType::COMPLETE, task.type_};
        operation.queued_at_ = task.queued_at_;
        operation.started_at_ = task.started_at_;
        operation.completed_at_ = task.completed_at_;
        operation.client_id_ = task.client_id_;
        operation.worker_id_ = task.worker_id_;
        if (task.type_ == task::TaskType::WORD_COUNT) operation.result_ = task.word_count_task_.result_;
        operations_.push_back(operation);

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

            auto& task = it->second;
            if (send_result(task)) {
                completed_backlog_.pop_front();
                replicate();
            } else {
                break;
            }
        }

        while (!queued_backlog_.empty()) {
            auto task_id = queued_backlog_.front();
            auto& task = tasks_.at(task_id);
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
        task::Task task{submit_msg.task_id_, submit_msg.client_id_, submit_msg.type_};
        stats_.total_queued_count_++;
        switch (task.type_) {
            case task::TaskType::SYNTHETIC:
                task.synthetic_task_ = task::SyntheticTask{submit_msg.duration_us_};
                stats_.synthetic_queued_count_++;
                break;
            case task::TaskType::WORD_COUNT:
                task.word_count_task_ = task::WordCountTask{submit_msg.words_};
                stats_.word_count_queued_count_++;
            default:
                break;
        }

        Operation operation{task.id_, OperationType::QUEUE, task.type_};
        operation.queued_at_ = task.queued_at_;
        operation.client_id_ = task.client_id_;
        operations_.push_back(operation);
        if (!assign(task)) {
            queued_backlog_.push_back(task.id_);
        }
        tasks_[task.id_] = task;
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

        auto& worker_state = workers_[task.worker_id_];
        worker_state.running_tasks_.erase(task.id_);
        worker_ring_.push_back(task.worker_id_);

        // Update statistics
        worker_state.stats_.total_running_count_--;
        worker_state.stats_.total_completed_count_++;
        stats_.total_running_count_--;
        stats_.total_completed_count_++;
        switch(task.type_) {
            case task::TaskType::SYNTHETIC:
                worker_state.stats_.synthetic_running_count_--;
                worker_state.stats_.synthetic_completed_count_++;
                stats_.synthetic_running_count_--;
                stats_.synthetic_completed_count_++;
                break;
            case task::TaskType::WORD_COUNT:
                task.word_count_task_.result_ = complete_msg.result_;
                worker_state.stats_.word_count_running_count_--;
                worker_state.stats_.word_count_completed_count_++;
                stats_.word_count_running_count_--;
                stats_.word_count_completed_count_++;
                break;
            default:
                break;
        }

        if (!send_result(task)) {
            completed_backlog_.push_back(task.id_);
        }
        replicate();
    }

    void Coordinator::process_queue_replicate_event(const IncomingEvent& event) {

    }

    void Coordinator::process_assign_replicate_event(const IncomingEvent &event) {

    }

    void Coordinator::process_complete_replicate_event(const IncomingEvent &event) {

    }

    void Coordinator::scheduler_worker() {
        while (running_.load(std::memory_order_acquire)) {

            if (is_primary_) {
                replicate();
                process_backlog();
            }

            IncomingEvent event{};
            while (!incoming_queue_->try_pop(event)) {
                std::this_thread::yield();
            }

            switch (event.msg_.type_) {
                case message::MessageType::SUBMIT:
                    process_submit_event(event);
                    break;
                case message::MessageType::COMPLETE:
                    process_complete_event(event);
                    break;
                case message::MessageType::QUEUED_REPLICATE:
                    process_queue_replicate_event(event);
                    break;
                case message::MessageType::ASSIGNED_REPLICATE:
                    process_assign_replicate_event(event);
                    break;
                case message::MessageType::COMPLETED_REPLICATE:
                    process_complete_replicate_event(event);
                    break;
                case message::MessageType::HEARTBEAT:
                    process_heartbeat_event(event);
                case message::MessageType::REGISTER:
                    process_register_event(event);
                    break;
                default:
                    break;
            }
        }
    }

    void Coordinator::stop() {
        if (running_.load(std::memory_order_acquire)) {
            running_.store(false, std::memory_order_release);

            if (network_thread_.joinable()) network_thread_.join();

            if (scheduler_thread_.joinable()) scheduler_thread_.join();
        }
    }


} // namespace coordinator
