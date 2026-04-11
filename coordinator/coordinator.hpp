#pragma once

#include <atomic>
#include <deque>
#include <thread>
#include <unordered_map>
#include <vector>

#include "types.hpp"
#include "../lock_free/spsc_queue.hpp"
#include "../task/task.hpp"

namespace coordinator {
    class Coordinator {

    private:
        TaskStats stats_{};
        size_t workers_count_{0};  // Keeps track of alive workers
        size_t replication_start_index_{0};
        uint16_t port_;

        bool is_primary_{false};

        std::atomic<bool> running_{false};

        std::string ip_;
        std::thread network_thread_{};
        std::thread scheduler_thread_{};

        std::unique_ptr<lock_free::SPSCQueue<IncomingEvent>> incoming_queue_;
        std::unique_ptr<lock_free::SPSCQueue<OutgoingEvent>> outgoing_queue_;

        std::unordered_map<uint64_t, task::Task> tasks_{};

        std::vector<Operation> operations_{};
        std::vector<WorkerState> workers_{};
        std::vector<uint32_t> available_ids_{};

        std::deque<uint32_t> worker_ring_{};
        std::deque<uint64_t> queued_backlog_{};
        std::deque<uint64_t> completed_backlog_{};

        PeerNode peer_;

        void replicate();
        bool assign(task::Task& task);
        bool send_result(task::Task& task);
        void process_backlog();
        void process_submit_event(const IncomingEvent &event);
        void process_complete_event(const IncomingEvent& event);
        void process_queue_replicate_event(const IncomingEvent& event);
        void process_assign_replicate_event(const IncomingEvent& event);
        void process_complete_replicate_event(const IncomingEvent& event);
        void process_heartbeat_event(const IncomingEvent& event);
        void process_register_event(const IncomingEvent& event);

        void scheduler_worker();

        void network_worker();

    public:
        explicit Coordinator() = default;

        explicit Coordinator(std::string ip, uint16_t port, PeerNode peer);

        ~Coordinator();

        void stop();

        void start();
    };
} // namespace coordinator
