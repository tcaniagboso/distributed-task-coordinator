#pragma once

#include <chrono>
#include <cstdint>

namespace utils {
    using namespace std::chrono;

    inline uint64_t now_ns_u64() {
        return duration_cast<nanoseconds>(
                steady_clock::now().time_since_epoch()
        ).count();
    }
} // namespace utils