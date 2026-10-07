// Correctness stress test for spmc.h.
//
// One producer pushes 1..N, several consumers pop concurrently. Passes only
// if every value is popped exactly once and each consumer sees its values in
// increasing order (FIFO). Small capacities force constant wraparound.
//
// Build and run under ThreadSanitizer:
//   c++ -std=c++17 -O1 -g -fsanitize=thread -pthread spmc_stress.cpp -o spmc_stress
//   ./spmc_stress
#include "spmc.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <thread>
#include <vector>

using cpp::parallel::Spmc;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
      std::exit(1);                                                            \
    }                                                                          \
  } while (0)

static void test_single_threaded() {
  Spmc<int> q(8);
  CHECK(q.empty());
  CHECK(q.size() == 0);
  CHECK(q.capacity() == 8);
  CHECK(!q.pop_front().has_value());

  for (int i = 0; i < 8; ++i) {
    CHECK(q.push_back(i));
  }
  CHECK(q.size() == 8);
  CHECK(!q.push_back(99)); // full at exactly capacity

  for (int i = 0; i < 8; ++i) {
    std::optional<int> v = q.pop_front();
    CHECK(v.has_value() && *v == i); // FIFO
  }
  CHECK(q.empty());

  bool threw = false;
  try {
    Spmc<int> bad(1000); // not a power of two
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  CHECK(threw);
}

static void test_concurrent(std::size_t capacity, int consumers,
                            std::uint64_t n) {
  Spmc<std::uint64_t> q(capacity);
  std::atomic<bool> done{false};
  std::vector<std::vector<std::uint64_t>> got(consumers);

  std::vector<std::thread> threads;
  for (int c = 0; c < consumers; ++c) {
    threads.emplace_back([&, c] {
      std::vector<std::uint64_t> &mine = got[c];
      while (true) {
        std::optional<std::uint64_t> v = q.pop_front();
        if (v) {
          mine.push_back(*v);
        } else if (done.load(std::memory_order_acquire) && q.empty()) {
          return;
        } else {
          std::this_thread::yield();
        }
      }
    });
  }

  for (std::uint64_t i = 1; i <= n; ++i) {
    while (!q.push_back(i)) {
      std::this_thread::yield();
    }
  }
  done.store(true, std::memory_order_release);
  for (std::thread &t : threads) {
    t.join();
  }

  std::vector<std::uint8_t> seen(n + 1, 0);
  std::uint64_t total = 0;
  for (const std::vector<std::uint64_t> &mine : got) {
    for (std::size_t i = 0; i < mine.size(); ++i) {
      CHECK(mine[i] >= 1 && mine[i] <= n);
      CHECK(seen[mine[i]] == 0); // no duplicates
      seen[mine[i]] = 1;
      if (i > 0) {
        CHECK(mine[i] > mine[i - 1]); // per-consumer FIFO order
      }
    }
    total += mine.size();
  }
  CHECK(total == n); // nothing lost

  std::printf("ok  capacity=%-5zu consumers=%d items=%llu\n", capacity,
              consumers, static_cast<unsigned long long>(n));
}

int main(int argc, char **argv) {
  const std::uint64_t n =
      argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 200000;

  test_single_threaded();
  std::printf("ok  single-threaded API checks\n");

  for (std::size_t capacity : {2, 4, 1024}) {
    for (int consumers : {1, 2, 4, 8}) {
      test_concurrent(capacity, consumers, n);
    }
  }
  std::printf("all passed\n");
  return 0;
}
