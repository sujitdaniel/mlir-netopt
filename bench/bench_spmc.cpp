// Benchmark for test/cpp/spmc.h: the lock-free SPMC ring buffer against the
// same ring guarded by a std::mutex.
//
// Throughput: one producer pushes N items, C consumers pop them all. Reported
// as millions of items per second, where each item is one push plus one pop.
// Every run is checked: the sum and count of popped items must match exactly,
// so a lost or duplicated item fails the benchmark.
//
// Latency: the producer timestamps one item every --gap-ns nanoseconds and
// consumers record (pop time - push time). The gap keeps the queue near
// empty, so this measures the producer-to-consumer handoff, not queueing.
// The clock read itself (tens of ns) is included in every sample.
//
// Build:  c++ -std=c++17 -O2 -pthread bench_spmc.cpp -o bench_spmc
// Run:    ./bench_spmc            (defaults below)
//         ./bench_spmc --help
#include "../test/cpp/spmc.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

inline std::uint64_t now_ns() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          Clock::now().time_since_epoch())
          .count());
}

inline void cpu_relax() {
#if defined(__x86_64__) || defined(__i386__)
  __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
  asm volatile("yield");
#endif
}

// Baseline: the same power-of-two ring, with one mutex around push and pop.
template <class T>
class MutexQueue {
public:
  explicit MutexQueue(std::size_t capacity)
      : mask_{capacity - 1}, slots_(capacity) {}

  bool push_back(const T &val) {
    std::lock_guard<std::mutex> lock(mu_);
    if (tail_ - head_ == slots_.size()) {
      return false;
    }
    slots_[tail_ & mask_] = val;
    ++tail_;
    return true;
  }

  std::optional<T> pop_front() {
    std::lock_guard<std::mutex> lock(mu_);
    if (head_ == tail_) {
      return std::nullopt;
    }
    T val = slots_[head_ & mask_];
    ++head_;
    return val;
  }

private:
  std::mutex mu_;
  const std::uint64_t mask_;
  std::vector<T> slots_;
  std::uint64_t head_ = 0;
  std::uint64_t tail_ = 0;
};

using LockFree = cpp::parallel::Spmc<std::uint64_t>;
using Locked = MutexQueue<std::uint64_t>;

constexpr std::uint64_t kStop = 0; // sentinel; real items are never 0

template <class Q>
void push_spin(Q &q, std::uint64_t v) {
  while (!q.push_back(v)) {
    cpu_relax();
  }
}

// Starts `count` threads that all wait on one flag, so timing starts together.
struct StartGate {
  std::atomic<int> ready{0};
  std::atomic<bool> go{false};
  void arrive_and_wait() {
    ready.fetch_add(1, std::memory_order_acq_rel);
    while (!go.load(std::memory_order_acquire)) {
      cpu_relax();
    }
  }
  void open_when(int count) {
    while (ready.load(std::memory_order_acquire) < count) {
      std::this_thread::yield();
    }
    go.store(true, std::memory_order_release);
  }
};

// Returns millions of items per second, or a negative value if the
// checksum failed.
template <class Q>
double run_throughput(std::size_t capacity, int consumers, std::uint64_t n) {
  Q q(capacity);
  StartGate gate;
  std::vector<std::uint64_t> sums(consumers, 0), counts(consumers, 0);
  std::vector<std::uint64_t> finish(consumers, 0);
  std::uint64_t start = 0;

  std::vector<std::thread> threads;
  for (int c = 0; c < consumers; ++c) {
    threads.emplace_back([&, c] {
      std::uint64_t sum = 0, count = 0;
      gate.arrive_and_wait();
      while (true) {
        std::optional<std::uint64_t> v = q.pop_front();
        if (!v) {
          cpu_relax();
          continue;
        }
        if (*v == kStop) {
          break;
        }
        sum += *v;
        ++count;
      }
      finish[c] = now_ns();
      sums[c] = sum;
      counts[c] = count;
    });
  }
  threads.emplace_back([&] {
    gate.arrive_and_wait();
    start = now_ns();
    for (std::uint64_t i = 1; i <= n; ++i) {
      push_spin(q, i);
    }
    // One stop marker per consumer; each consumer exits on its first.
    for (int c = 0; c < consumers; ++c) {
      push_spin(q, kStop);
    }
  });

  gate.open_when(consumers + 1);
  for (std::thread &t : threads) {
    t.join();
  }

  std::uint64_t sum = 0, count = 0;
  for (int c = 0; c < consumers; ++c) {
    sum += sums[c];
    count += counts[c];
  }
  if (count != n || sum != n * (n + 1) / 2) {
    return -1.0;
  }
  const std::uint64_t end = *std::max_element(finish.begin(), finish.end());
  const double seconds = static_cast<double>(end - start) * 1e-9;
  return static_cast<double>(n) / seconds / 1e6;
}

template <class Q>
std::vector<std::uint64_t> run_latency(std::size_t capacity, int consumers,
                                       std::uint64_t samples,
                                       std::uint64_t gap_ns) {
  Q q(capacity);
  StartGate gate;
  std::vector<std::vector<std::uint64_t>> per(consumers);

  std::vector<std::thread> threads;
  for (int c = 0; c < consumers; ++c) {
    per[c].reserve(samples);
    threads.emplace_back([&, c] {
      std::vector<std::uint64_t> &mine = per[c];
      gate.arrive_and_wait();
      while (true) {
        std::optional<std::uint64_t> v = q.pop_front();
        if (!v) {
          cpu_relax();
          continue;
        }
        if (*v == kStop) {
          break;
        }
        mine.push_back(now_ns() - *v);
      }
    });
  }
  threads.emplace_back([&] {
    gate.arrive_and_wait();
    std::uint64_t next = now_ns();
    for (std::uint64_t i = 0; i < samples; ++i) {
      next += gap_ns;
      while (now_ns() < next) {
        cpu_relax();
      }
      push_spin(q, now_ns());
    }
    for (int c = 0; c < consumers; ++c) {
      push_spin(q, kStop);
    }
  });

  gate.open_when(consumers + 1);
  for (std::thread &t : threads) {
    t.join();
  }

  std::vector<std::uint64_t> all;
  all.reserve(samples);
  for (const std::vector<std::uint64_t> &v : per) {
    all.insert(all.end(), v.begin(), v.end());
  }
  std::sort(all.begin(), all.end());
  return all;
}

std::uint64_t percentile(const std::vector<std::uint64_t> &sorted, double p) {
  if (sorted.empty()) {
    return 0;
  }
  std::size_t idx = static_cast<std::size_t>(p * (sorted.size() - 1) + 0.5);
  return sorted[std::min(idx, sorted.size() - 1)];
}

template <class Q>
double median_throughput(std::size_t capacity, int consumers, std::uint64_t n,
                         int runs, bool &ok) {
  run_throughput<Q>(capacity, consumers, n / 10); // warm-up, discarded
  std::vector<double> results;
  for (int r = 0; r < runs; ++r) {
    double mops = run_throughput<Q>(capacity, consumers, n);
    if (mops < 0) {
      ok = false;
      return 0.0;
    }
    results.push_back(mops);
  }
  std::sort(results.begin(), results.end());
  return results[results.size() / 2];
}

std::vector<int> parse_list(const char *s) {
  std::vector<int> out;
  std::string str(s);
  std::size_t pos = 0;
  while (pos < str.size()) {
    std::size_t comma = str.find(',', pos);
    if (comma == std::string::npos) {
      comma = str.size();
    }
    out.push_back(std::atoi(str.substr(pos, comma - pos).c_str()));
    pos = comma + 1;
  }
  return out;
}

void usage() {
  std::printf(
      "usage: bench_spmc [options]\n"
      "  --items N            items per throughput run (default 10000000)\n"
      "  --runs R             throughput runs per point, median kept (5)\n"
      "  --consumers a,b,c    consumer counts to test (1,2,4)\n"
      "  --capacity C         queue capacity, power of two (1024)\n"
      "  --latency-samples K  timestamped items per latency run (200000)\n"
      "  --gap-ns G           ns between timestamped items (2000)\n");
}

} // namespace

int main(int argc, char **argv) {
  std::uint64_t items = 10000000;
  int runs = 5;
  std::vector<int> consumer_counts = {1, 2, 4};
  std::size_t capacity = 1024;
  std::uint64_t samples = 200000;
  std::uint64_t gap_ns = 2000;

  // Options come in "--name value" pairs.
  for (int i = 1; i < argc; i += 2) {
    const char *a = argv[i];
    const char *v = i + 1 < argc ? argv[i + 1] : nullptr;
    if (!std::strcmp(a, "--help") || !std::strcmp(a, "-h")) {
      usage();
      return 0;
    }
    if (!v) {
      usage();
      return 1;
    }
    if (!std::strcmp(a, "--items")) {
      items = std::strtoull(v, nullptr, 10);
    } else if (!std::strcmp(a, "--runs")) {
      runs = std::atoi(v);
    } else if (!std::strcmp(a, "--consumers")) {
      consumer_counts = parse_list(v);
    } else if (!std::strcmp(a, "--capacity")) {
      capacity = std::strtoull(v, nullptr, 10);
    } else if (!std::strcmp(a, "--latency-samples")) {
      samples = std::strtoull(v, nullptr, 10);
    } else if (!std::strcmp(a, "--gap-ns")) {
      gap_ns = std::strtoull(v, nullptr, 10);
    } else {
      usage();
      return 1;
    }
  }

  const unsigned hw = std::thread::hardware_concurrency();
  std::printf("SPMC ring buffer benchmark\n");
  std::printf("hardware threads: %u | capacity: %zu | items/run: %llu | "
              "runs: %d (median)\n\n",
              hw, capacity, static_cast<unsigned long long>(items), runs);

  std::printf("Throughput, 1 producer + N consumers "
              "(M items/s; each item = 1 push + 1 pop)\n");
  std::printf("%-10s %12s %12s %9s\n", "consumers", "lock-free", "mutex",
              "speedup");
  bool ok = true;
  double best_speedup_mops = 0, best_speedup = 0;
  int best_speedup_c = 0;
  for (int c : consumer_counts) {
    double lf = median_throughput<LockFree>(capacity, c, items, runs, ok);
    double mx = median_throughput<Locked>(capacity, c, items, runs, ok);
    if (!ok) {
      std::printf("CHECKSUM FAILED at %d consumers: items lost or "
                  "duplicated\n",
                  c);
      return 1;
    }
    const bool over = hw != 0 && static_cast<unsigned>(c + 1) > hw;
    std::printf("%-10d %12.1f %12.1f %8.1fx%s\n", c, lf, mx, lf / mx,
                over ? "  (more threads than cores)" : "");
    if (!over && c >= best_speedup_c) {
      best_speedup_c = c;
      best_speedup_mops = lf;
      best_speedup = lf / mx;
    }
  }
  std::printf("checksums: all runs matched (no lost or duplicated items)\n\n");

  std::printf("Handoff latency, producer -> consumer "
              "(ns; 1 item every %llu ns)\n",
              static_cast<unsigned long long>(gap_ns));
  std::printf("%-10s %-10s %8s %8s %8s %10s\n", "consumers", "queue", "p50",
              "p99", "p99.9", "max");
  std::uint64_t lat_p50 = 0, lat_p99 = 0;
  int lat_c = 0;
  for (int c : consumer_counts) {
    const bool over = hw != 0 && static_cast<unsigned>(c + 1) > hw;
    std::vector<std::uint64_t> lf =
        run_latency<LockFree>(capacity, c, samples, gap_ns);
    std::vector<std::uint64_t> mx =
        run_latency<Locked>(capacity, c, samples, gap_ns);
    std::printf("%-10d %-10s %8llu %8llu %8llu %10llu%s\n", c, "lock-free",
                static_cast<unsigned long long>(percentile(lf, 0.50)),
                static_cast<unsigned long long>(percentile(lf, 0.99)),
                static_cast<unsigned long long>(percentile(lf, 0.999)),
                static_cast<unsigned long long>(lf.empty() ? 0 : lf.back()),
                over ? "  (more threads than cores)" : "");
    std::printf("%-10s %-10s %8llu %8llu %8llu %10llu\n", "", "mutex",
                static_cast<unsigned long long>(percentile(mx, 0.50)),
                static_cast<unsigned long long>(percentile(mx, 0.99)),
                static_cast<unsigned long long>(percentile(mx, 0.999)),
                static_cast<unsigned long long>(mx.empty() ? 0 : mx.back()));
    if (!over && c >= lat_c) {
      lat_c = c;
      lat_p50 = percentile(lf, 0.50);
      lat_p99 = percentile(lf, 0.99);
    }
  }

  if (best_speedup_c > 0) {
    std::printf("\nFor the resume (largest consumer count that fits your "
                "cores):\n");
    std::printf("  %.1fM items/s at %d consumer%s, %.1fx a mutex queue; "
                "p50/p99 handoff %llu/%llu ns at %d consumer%s\n",
                best_speedup_mops, best_speedup_c,
                best_speedup_c == 1 ? "" : "s", best_speedup,
                static_cast<unsigned long long>(lat_p50),
                static_cast<unsigned long long>(lat_p99), lat_c,
                lat_c == 1 ? "" : "s");
  }
  return 0;
}
