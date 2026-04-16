#include <chrono>
#include <ncurses.h>
#include <iostream>
#include <thread>
#include <utility>

#include "../../include/config/system_config.hpp"
#include "../../include/top/top.hpp"

namespace top {

    Top::Top(std::string ip, uint16_t port)
            : connection_{ip, port},
              last_drawn_row_{0},
              port_{port},
              ip_{std::move(ip)} {}

    Top::~Top() {
        close();
    }

    void Top::draw_worker_metrics(const top::WorkerMetrics &metrics, int& row) {
        if (row >= LINES - 1) return;

        move(row, 0);
        clrtoeol(); // Clear the line BEFORE printing new data

        // Color: green if alive, red if dead
        if (metrics.alive_) attron(COLOR_PAIR(2));
        else attron(COLOR_PAIR(3));

        mvprintw(row++, 0,
                 "%4u | %-5s | %8lu | %10lu | %10lu | %10lu",
                 metrics.worker_id_,
                 metrics.alive_ ? "[UP]" : "[DOWN]",
                 metrics.tasks_running_,
                 metrics.tasks_completed_,
                 metrics.avg_latency_us_,
                 metrics.last_heartbeat_ms_
        );
        attroff(COLOR_PAIR(2));
        attroff(COLOR_PAIR(3));
        clrtoeol();
    }

    void Top::draw_all_worker_metrics(const std::vector<WorkerMetrics> &workers, int& row) {
        attron(COLOR_PAIR(4) | A_BOLD);
        mvprintw(row++, 0, "Workers:");
        attroff(COLOR_PAIR(4) | A_BOLD);
        clrtoeol();

        // header
        attron(A_BOLD);
        mvprintw(row++, 0,
                 " ID  | Alive | Running  | Completed  | Avg Lat(us) | Last HB(ms)");
        attroff(A_BOLD);
        clrtoeol();

        for (const auto &w : workers) {
            if (row >= LINES - 1) break;
            draw_worker_metrics(w, row);
        }
    }

    void Top::draw_coordinator_metrics(const CoordinatorMetrics &metrics, int& row) const {
        attron(COLOR_PAIR(4) | A_BOLD);
        mvprintw(row++, 0, "Coordinator:");
        attroff(COLOR_PAIR(4) | A_BOLD);

        attron(COLOR_PAIR(1));
        mvprintw(row++, 2, "IP Address: %s", ip_.c_str());
        clrtoeol();
        mvprintw(row++, 2, "Port: %d", port_);
        clrtoeol();
        mvprintw(row++, 2, "Primary: %d", metrics.is_primary_);
        clrtoeol();
        mvprintw(row++, 2, "Throughput: %lu tasks/sec", metrics.throughput_);
        clrtoeol();
        mvprintw(row++, 2, "Queued: %lu", metrics.tasks_queued_);
        clrtoeol();
        mvprintw(row++, 2, "Running: %lu", metrics.tasks_running_);
        clrtoeol();
        mvprintw(row++, 2, "Completed: %lu", metrics.tasks_completed_);
        clrtoeol();
        mvprintw(row++, 2, "Active Workers: %d", metrics.active_workers_);
        clrtoeol();
        attroff(COLOR_PAIR(1));

        mvprintw(row++, 2, "Avg Latency: %lu us", metrics.avg_latency_us_);
        clrtoeol();
        mvprintw(row++, 2, "P95 Latency: %lu us", metrics.p95_latency_us_);
        clrtoeol();
        mvprintw(row++, 2, "Min Latency: %lu us", metrics.min_latency_us_);
        clrtoeol();
        mvprintw(row++, 2, "Max Latency: %lu us", metrics.max_latency_us_);
        clrtoeol();

        row++;
    }

    bool Top::fetch_metrics(message::Message &response) const {
        message::Message top_request{message::MessageType::TOP_REQUEST};

        if (connection_.send_with_retry(top_request, config::CONNECTION_RETRY_COUNT) <= 0)
            return false;

        if (connection_.receive_with_retry(response, config::CONNECTION_RETRY_COUNT) <= 0)
            return false;

        return true;
    }

    void Top::draw(message::TopResponseMsg &response) {
        int row = 0;
        draw_coordinator_metrics(response.coordinator_metrics_, row);
        draw_all_worker_metrics(response.workers_metrics_, row);

        for (int r = row; r < last_drawn_row_; ++r) {
            move(r, 0);
            clrtoeol();
        }

        last_drawn_row_ = row;
    }

    void Top::close() {
        connection_.close_connection();
    }

    void Top::run() {
        initscr();
        noecho();
        cbreak();
        curs_set(0);
        nodelay(stdscr, TRUE);

        // ---- COLORS ----
        if (has_colors()) {
            start_color();
            init_pair(1, COLOR_YELLOW, COLOR_BLACK); // labels
            init_pair(2, COLOR_GREEN, COLOR_BLACK);  // alive
            init_pair(3, COLOR_RED, COLOR_BLACK);    // dead
            init_pair(4, COLOR_CYAN, COLOR_BLACK);   // headers
        }

        message::Message response{};

        while (true) {
            erase();
            move(0, 0);

            if (!fetch_metrics(response)) {
                attron(COLOR_PAIR(3) | A_BOLD);
                mvprintw(0, 0,
                         "Lost connection to Coordinator on ip %s and port %d",
                         ip_.c_str(), port_);
                attroff(COLOR_PAIR(3) | A_BOLD);
                clrtoeol();

                refresh();
                break;
            }

            draw(response.top_response_);

            // 4. Clear any remaining lines from previous runs that weren't overwritten
            for (int r = last_drawn_row_; r < LINES; ++r) {
                move(r, 0);
                clrtoeol();
            }

            mvprintw(LINES - 1, 0, "Press 'q' to quit");
            clrtoeol();

            refresh();

            int ch = getch();
            if (ch == 'q') break;

            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        endwin();
    }

} // namespace top