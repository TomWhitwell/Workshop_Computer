#pragma once
#include "config.hpp"
#include <atomic>
#include <cstdint>
#include <type_traits>

namespace computer {
// Exactly one producer and one consumer. Release/acquire publishes complete
// objects. Aligned native-word loads/stores plus barriers avoid a spin lock.
// Neither owner may reset the counters while the other is active.
template <class T, unsigned N = timing::queueCapacity> class Queue {
    static_assert(N && !(N & (N - 1)), "power-of-two capacity required");
    static_assert(std::is_trivially_copyable<T>::value, "plain data only");

public:
    static constexpr unsigned capacity = N;
    // An empty queue can start at any unsigned sequence number. Occupancy
    // arithmetic remains valid when the counters wrap.
    explicit constexpr Queue(uint32_t initialSequence = 0)
        : write(initialSequence), read(initialSequence) {}

    bool push(const T &value) {
        const auto position = write.load(std::memory_order_relaxed);
        if (position - read.load(std::memory_order_acquire) == N) {
            return false;
        }
        data[position & (N - 1)] = value;
        write.store(position + 1, std::memory_order_release);
        return true;
    }
    bool pop(T &value) {
        const auto position = read.load(std::memory_order_relaxed);
        if (position == write.load(std::memory_order_acquire)) {
            return false;
        }
        value = data[position & (N - 1)];
        read.store(position + 1, std::memory_order_release);
        return true;
    }
    // Owner-side occupancy snapshot, not a transactional third-party reading.
    uint32_t size() const {
        return write.load(std::memory_order_acquire) - read.load(std::memory_order_acquire);
    }

private:
    T data[N]{};
    std::atomic<uint32_t> write, read; // Unsigned wrap is intentional.
};
} // namespace computer
