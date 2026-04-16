#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../message/message.hpp"
#include "../rpc/client.hpp"
#include "types.hpp"

namespace top {

    class Top {
    private:
        rpc::Client connection_;
        int last_drawn_row_;
        uint16_t port_;
        std::string ip_;

        static void draw_worker_metrics(const WorkerMetrics& metrics, int& row) ;

        void draw_coordinator_metrics(const CoordinatorMetrics& metrics, int& row) const;

        static void draw_all_worker_metrics(const std::vector<WorkerMetrics>& workers, int& row) ;

        bool fetch_metrics(message::Message& response) const ;

        void draw(message::TopResponseMsg& response);

    public:
        explicit Top(std::string ip, uint16_t port);

        ~Top();

        void close();

        void run();
    };
} // namespace top