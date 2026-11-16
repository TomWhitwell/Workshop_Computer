/**
 * @file queue.hpp
 * @brief Wait-free SPSC queue for cross-core packet/frame handoff between cores.
 *
 * The firmware uses two instances: core0's callback pushes control Packets
 * into the input queue and pops produced 24 kHz frames from the output queue;
 * core1's worker does the reverse. Desktop code is synchronous and never
 * queues. Counters are 32-bit sequence numbers whose unsigned difference
 * encodes occupancy across wrap-around; ring slots are selected by a mask.
 *
 *
 * @date 2026-11-16
 */
#pragma once
#include "config.hpp"
#include <atomic>
#include <cstdint>
#include <type_traits>

namespace computer {
/// @brief Wait-free single-producer/single-consumer ring of plain data.
///
/// Exactly one producer and one consumer. Release/acquire publishes complete
/// objects. Aligned native-word loads/stores plus barriers avoid a spin lock.
/// Neither owner may reset the counters while the other is active.
template <class T, unsigned N = timing::queueCapacity> class Queue {
    static_assert(N && !(N & (N - 1)), "power-of-two capacity required");
    static_assert(std::is_trivially_copyable<T>::value, "plain data only");

public:
    /// @brief Number of ring slots; power of two, as the mask arithmetic needs.
    static constexpr unsigned capacity = N;
    /// @brief Leave the queue empty starting at an arbitrary sequence number.
    ///
    /// An empty queue can start at any sequence number. This also lets rollover
    /// tests exercise a real empty queue rather than modifying live internals.
    /// @param initialSequence Baseline value assigned to both sequence counters.
    explicit constexpr Queue(uint32_t initialSequence = 0)
        : write(initialSequence), read(initialSequence) {}

    /// @brief Try to publish one element; never blocks, waits or allocates.
    /// @param value Element copied into the next free ring slot.
    /// @return True when stored; false when the ring was full (caller retries).
    /// @note Safe in IRQ context: constant-time attempt with no failure loops.
    bool push(const T &value) {
        const auto position = write.load(std::memory_order_relaxed);
        if (position - read.load(std::memory_order_acquire) == N) {
            return false;
        }
        data[position & (N - 1)] = value;
        write.store(position + 1, std::memory_order_release);
        return true;
    }
    /// @brief Try to consume the oldest published element; never blocks.
    /// @param value Receives the element; untouched when the queue is empty.
    /// @return True when an element was consumed; false when the queue was empty.
    /// @note The slot being popped cannot be overwritten until the consumer
    ///       publishes the incremented read counter (it needs N pushes to wrap).
    bool pop(T &value) {
        const auto position = read.load(std::memory_order_relaxed);
        if (position == write.load(std::memory_order_acquire)) {
            return false;
        }
        value = data[position & (N - 1)];
        read.store(position + 1, std::memory_order_release);
        return true;
    }
    /// @brief Owner-side occupancy in elements.
    ///
    /// Owner-side occupancy snapshot, not a transactional third-party reading.
    /// @return Published minus consumed count in unsigned sequence arithmetic.
    uint32_t size() const {
        return write.load(std::memory_order_acquire) - read.load(std::memory_order_acquire);
    }

private:
    /// @brief Ring slots; slot index is the sequence counter's low bits.
    T data[N]{};
    /// @brief Producer (write) and consumer (read) sequence counters.
    std::atomic<uint32_t> write, read; // Unsigned wrap is intentional.
};
} // namespace computer
