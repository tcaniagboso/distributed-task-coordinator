#pragma once

#include <atomic>
#include <deque>
#include <thread>
#include <unordered_map>
#include <vector>

#include "types.hpp"
#include "../lock_free/spsc_queue.hpp"
#include "../message/message.hpp"
#include "../task/task.hpp"

namespace coordinator {
    class Coordinator {

    private:
        uint64_t last_sweep_{};
        uint64_t current_epoch_{0};
        size_t rr_index_{0};
        size_t replication_start_index_{0};
        uint16_t port_{};

        bool is_primary_{false};

        std::atomic<bool> running_{false};

        std::thread network_thread_{};
        std::thread scheduler_thread_{};

        std::unique_ptr<lock_free::SPSCQueue<IncomingEvent>> incoming_queue_;
        std::unique_ptr<lock_free::SPSCQueue<OutgoingEvent>> outgoing_queue_;

        std::unordered_map<uint64_t, task::Task> tasks_{};
        std::unordered_map<uint32_t, int> client_fds_{};
        std::unordered_map<uint64_t, IncomingEvent> early_complete_reps_{};

        std::deque<message::Message> replication_log_{};
        std::deque<uint64_t> queued_backlog_{};
        std::deque<uint64_t> completed_backlog_{};

        std::vector<WorkerState> workers_states_{};
        std::vector<uint32_t> active_workers_{};
        std::vector<uint32_t> available_ids_{};

        PeerNode peer_;

        bool try_push_outgoing(const OutgoingEvent& event);
        bool try_push_incoming(const IncomingEvent& event);

        void add_to_log(const message::Message& msg);

        void mark_inactive(uint32_t worker_id);

        bool is_alive(uint32_t worker_id);

        void sweep_workers();

        void replicate();

        bool assign(task::Task& task);

        bool send_result(task::Task& task);

        void process_backlog();
        void process_submit_event(const IncomingEvent &event);
        void process_complete_event(const IncomingEvent& event);
        void process_assign_replicate_event(const IncomingEvent& event);
        void process_complete_replicate_event(const IncomingEvent& event);
        void process_heartbeat_event(const IncomingEvent& event);
        void process_register_event(const IncomingEvent& event);
        void process_snapshot_event(const IncomingEvent& event);
        void process_peer_reconnected();

        static bool connect_to_peer(PeerNode& peer);
        void reconnect_to_peer(PeerNode& peer);
        void send_replication_message(const OutgoingEvent& event, PeerNode& peer);

        void scheduler_worker();

        void network_worker();

    public:
        explicit Coordinator() = delete;

        Coordinator(uint16_t port, PeerNode&& peer);

        ~Coordinator();

        void stop();

        void run();
    };
} // namespace coordinator
