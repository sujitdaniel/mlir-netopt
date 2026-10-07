#ifndef SPMC_H_
#define SPMC_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>

namespace cpp {
namespace parallel {

// Bounded lock-free single-producer, multiple-consumer ring buffer.
//
// head_ and tail_ are 64-bit counters that only ever increase; the slot for
// counter i is i & mask_. Because the counters never wrap in practice, a
// consumer that stalls between reading head_ and its CAS cannot be fooled by
// the index coming back around to the same value (ABA).
//
// Slots are std::atomic<T>. A consumer holding a stale head_ may read a slot
// the producer is overwriting; with atomic slots that is not a data race, and
// the consumer's CAS fails so the stale value is discarded.
//
// T must be trivially copyable and lock-free as an atomic: integers,
// pointers, or small packet descriptors, which is what a packet queue
// carries.
template <class T>
class Spmc {
  static_assert(std::is_trivially_copyable_v<T>,
                "Spmc<T> requires a trivially copyable T");
  static_assert(std::atomic<T>::is_always_lock_free,
                "Spmc<T> requires std::atomic<T> to be lock-free");

public:
  explicit Spmc(std::size_t capacity)
      : capacity_{capacity}, mask_{capacity - 1},
        slots_{std::make_unique<std::atomic<T>[]>(capacity)} {
    if (capacity == 0 || (capacity & (capacity - 1)) != 0) {
      throw std::invalid_argument("Spmc capacity must be a power of two");
    }
  }

  Spmc(const Spmc &) = delete;
  Spmc &operator=(const Spmc &) = delete;

  // Producer only. Returns false if the queue is full.
  bool push_back(const T &val) {
    const std::uint64_t tail = tail_.load(std::memory_order_relaxed);
    // Acquire pairs with the consumers' release CAS: every consumer that
    // advanced head_ past a slot has finished reading it before we reuse it.
    const std::uint64_t head = head_.load(std::memory_order_acquire);
    if (tail - head == capacity_) {
      return false;
    }
    slots_[tail & mask_].store(val, std::memory_order_relaxed);
    // Release publishes the slot write to consumers.
    tail_.store(tail + 1, std::memory_order_release);
    return true;
  }

  // Any number of consumers. Returns std::nullopt if the queue is empty.
  std::optional<T> pop_front() {
    std::uint64_t head = head_.load(std::memory_order_relaxed);
    while (true) {
      const std::uint64_t tail = tail_.load(std::memory_order_acquire);
      // Independent loads of head_ and tail_ can observe a newer head with a
      // stale tail. Treat that as empty; using only == would try to claim a
      // slot the producer has not written yet.
      if (head >= tail) {
        return std::nullopt;
      }
      const T val = slots_[head & mask_].load(std::memory_order_relaxed);
      // On failure, head is reloaded with the current value and we retry.
      if (head_.compare_exchange_weak(head, head + 1,
                                      std::memory_order_release,
                                      std::memory_order_relaxed)) {
        return val;
      }
    }
  }

  // Exact when no other thread is active; a snapshot otherwise.
  std::size_t size() const {
    // Load head_ first: head_ never passes tail_, so tail >= head here.
    const std::uint64_t head = head_.load(std::memory_order_acquire);
    const std::uint64_t tail = tail_.load(std::memory_order_acquire);
    if (tail < head) {
      return 0;
    }
    return static_cast<std::size_t>(tail - head);
  }

  bool empty() const { return size() == 0; }

  std::size_t capacity() const { return capacity_; }

private:
  static constexpr std::size_t kCacheLine = 64;

  const std::size_t capacity_;
  const std::uint64_t mask_;
  const std::unique_ptr<std::atomic<T>[]> slots_;

  // Separate cache lines so consumers updating head_ don't invalidate the
  // producer's tail_ line on every pop (false sharing).
  alignas(kCacheLine) std::atomic<std::uint64_t> head_{0}; // consumers
  alignas(kCacheLine) std::atomic<std::uint64_t> tail_{0}; // producer
};

} // namespace parallel
} // namespace cpp

#endif // SPMC_H_
