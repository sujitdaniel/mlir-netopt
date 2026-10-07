#!/bin/bash
# Builds and runs the ring buffer checks and benchmark. Run from the repo root:
#   ./bench/run_bench.sh
# Close other heavy apps first; plug in if on a laptop. Results are written
# to bench/results.txt along with the machine they came from.
set -euo pipefail

CXX="${CXX:-c++}"
OUT=bench/build
mkdir -p "$OUT"

echo "== 1/3 Correctness under ThreadSanitizer =="
"$CXX" -std=c++17 -O1 -g -fsanitize=thread -pthread \
  test/cpp/spmc_stress.cpp -o "$OUT/spmc_stress_tsan"
"$OUT/spmc_stress_tsan"

echo
echo "== 2/3 Correctness, optimized build =="
"$CXX" -std=c++17 -O2 -pthread test/cpp/spmc_stress.cpp -o "$OUT/spmc_stress"
"$OUT/spmc_stress" 2000000

echo
echo "== 3/3 Benchmark =="
"$CXX" -std=c++17 -O2 -pthread bench/bench_spmc.cpp -o "$OUT/bench_spmc"
{
  echo "machine: $(uname -sm)"
  if [[ "$(uname -s)" == "Darwin" ]]; then
    echo "cpu: $(sysctl -n machdep.cpu.brand_string)"
  else
    echo "cpu: $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ //')"
  fi
  echo "compiler: $("$CXX" --version | head -1)"
  echo
  "$OUT/bench_spmc" "$@"
} | tee bench/results.txt
