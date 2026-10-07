# Ring buffer benchmark

Measures the lock-free SPMC ring buffer in `test/cpp/spmc.h` against the same
ring guarded by a `std::mutex`.

```bash
./bench/run_bench.sh          # from the repo root
```

The script runs three steps and stops if any fails:

1. **ThreadSanitizer stress test** (`test/cpp/spmc_stress.cpp`): 1 producer,
   1–8 consumers, capacities 2, 4 and 1024. It passes only if every item is
   popped exactly once and each consumer sees its items in order.
2. **The same stress test, optimized build**, with 2M items per case.
3. **Benchmark** (`bench/bench_spmc.cpp`), saved to `bench/results.txt` with
   the CPU and compiler it ran on.

## What the numbers mean

- **Throughput**: millions of items per second through the queue, where
  each item is one push and one pop. The median of 5 runs is kept. Every run
  checks the sum and count of popped items, so lost or duplicated items fail
  the run.
- **Handoff latency**: the producer timestamps one item every 2 µs, and
  consumers record pop time minus push time. The gap keeps the queue near
  empty, so this is the producer-to-consumer handoff, not queueing delay.
  Each sample includes one clock read.
- Rows marked `(more threads than cores)` are oversubscribed. Don't quote
  them.

Any options you add are passed to the benchmark, e.g.
`./bench/run_bench.sh --consumers 1,2,4,8 --items 20000000`. After one run,
`bench/build/bench_spmc --help` lists them all. Binaries go to
`bench/build/`, so add that folder to `.gitignore`.

## What changed in `spmc.h`

- **Buffer allocation.** `buf{static_cast<int>(size)}` used braces, so for
  `Spmc<int>` the vector took the size as a one-element list. The queue
  held 1 slot and wrote out of bounds from the second push on (found with
  AddressSanitizer).
- **Index wraparound (ABA).** Indices wrapped modulo capacity, so a consumer
  that stalled mid-pop could CAS successfully after the index came back
  around. That popped stale or extra items (seen at capacity 4 with 4
  consumers). Indices are now 64-bit counters that only increase, so they
  never come back around in practice.
- **Slot data race.** A consumer with a stale index could read a slot while
  the producer wrote it (flagged by ThreadSanitizer). Slots are now
  `std::atomic<T>`, and `T` must be lock-free as an atomic: integers,
  pointers or packet descriptors.
- **`size()` and the empty check.** `size()` returned the capacity on an
  empty queue, and `operator bool` returned true when empty. They are
  replaced by `size()` and `empty()`.
- **Full capacity.** The queue now holds all `capacity` items (it used to
  waste one slot). `head_` and `tail_` sit on separate cache lines.
