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
        size_t replication_read_ptr_{0};
        size_t replication_write_ptr_{0};
        uint16_t port_{};

        bool is_primary_{false};

        std::atomic<bool> running_{false};

        std::thread network_thread_{};
        std::thread scheduler_thread_{};

        std::unique_ptr<lock_free::SPSCQueue<IncomingEvent>> control_queue_;
        std::unique_ptr<lock_free::SPSCQueue<IncomingEvent>> task_queue_;
        std::unique_ptr<lock_free::SPSCQueue<OutgoingEvent>> outgoing_queue_;

        std::unordered_map<uint64_t, task::Task> tasks_{};
        std::unordered_map<uint32_t, int> client_fds_{};
        std::unordered_map<uint64_t, IncomingEvent> early_complete_reps_{};

        std::vector<message::Message> replication_log_{};
        std::deque<uint64_t> queued_backlog_{};
        std::deque<uint64_t> completed_backlog_{};

        std::vector<WorkerState> workers_states_{};
        std::vector<uint32_t> active_workers_{};
        std::vector<uint32_t> available_ids_{};

        Metrics metrics_;

        PeerNode peer_;

        static bool try_pop_incoming(IncomingEvent& event, lock_free::SPSCQueue<IncomingEvent>& queue);
        static bool try_push_outgoing(const OutgoingEvent& event, lock_free::SPSCQueue<OutgoingEvent>& queue);
        static bool try_push_incoming(const IncomingEvent& event, lock_free::SPSCQueue<IncomingEvent>& queue);

        void add_to_log(const message::Message& msg);

        static void record_enqueue(Metrics& metrics);
        static void record_assignment(Metrics& metrics);
        static void record_completion_worker(WorkerState& worker_state, const task::Task& task);
        static void record_completion_system(Metrics& metrics, const task::Task& task);

        static uint64_t get_rolling_throughput(Metrics& metrics);

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
        void process_top_request(const IncomingEvent &event);

        void process_control_queue(lock_free::SPSCQueue<IncomingEvent>& queue);
        void process_task_queue(lock_free::SPSCQueue<IncomingEvent>& queue);


        static bool connect_to_peer(PeerNode& peer);
        void reconnect_to_peer(PeerNode& peer);
        void send_replication_message(const OutgoingEvent& event, PeerNode& peer);

        bool communicate_incoming_event(const IncomingEvent& event);

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
