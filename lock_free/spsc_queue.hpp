#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>

namespace lock_free {
    template<typename T>
    class SPSCQueue {
    private:
        struct alignas(64) PaddedAtomic {
            std::atomic <size_t> value{0};
            std::array<char, 64 - sizeof(std::atomic < size_t > )> padding{};
        };
        static_assert(sizeof(PaddedAtomic) == 64);

        PaddedAtomic head_;
        PaddedAtomic tail_;

        std::unique_ptr<T[]> buffer_;
        size_t capacity_;

    public:
        static constexpr const char *name() {
            return "Lock-Free SPSC Queue";
        }

        explicit SPSCQueue(size_t capacity);

        bool try_push(const T &value);

        bool try_pop(T &value);

        [[nodiscard]] bool empty() const;

        [[nodiscard]] bool full() const;

        [[nodiscard]] size_t size() const;
    };

    template<typename T>
    SPSCQueue<T>::SPSCQueue(size_t capacity)
            : head_{},
              tail_{},
              buffer_{std::make_unique<T[]>(capacity)},
              capacity_{capacity} {}

    template<typename T>
    bool SPSCQueue<T>::full() const {
        size_t tail = tail_.value.load(std::memory_order_relaxed);
        size_t head = head_.value.load(std::memory_order_relaxed);
        return tail - head == capacity_;
    }

    template<typename T>
    bool SPSCQueue<T>::empty() const {
        size_t tail = tail_.value.load(std::memory_order_relaxed);
        size_t head = head_.value.load(std::memory_order_relaxed);
        return tail == head;
    }

    template<typename T>
    size_t SPSCQueue<T>::size() const {
        size_t tail = tail_.value.load(std::memory_order_relaxed);
        size_t head = head_.value.load(std::memory_order_relaxed);
        return tail - head;
    }

    template<typename T>
    bool SPSCQueue<T>::try_pop(T &value) {
        size_t head = head_.value.load(std::memory_order_acquire);
        size_t tail = tail_.value.load(std::memory_order_relaxed);
        if (tail == head) {
            return false;
        }

        size_t index = head % capacity_;
        value = buffer_[index];
        head_.value.store(head + 1, std::memory_order_release);
        return true;
    }

    template<typename T>
    bool SPSCQueue<T>::try_push(const T &value) {
        size_t head = head_.value.load(std::memory_order_relaxed);
        size_t tail = tail_.value.load(std::memory_order_acquire);

        if (tail - head == capacity_) {
            return false;
        }

        size_t index = tail % capacity_;
        buffer_[index] = value;
        tail_.value.store(tail + 1, std::memory_order_release);
        return true;
    }
} // namespace lock_free
