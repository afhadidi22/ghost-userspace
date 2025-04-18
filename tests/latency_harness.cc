// A tiny ghOSt scheduler benchmark: 5 periodic low‑latency threads compete
// with 20 CPU‑hog threads.  We measure wake‑up overshoot of the periodic
// threads; lower is better.
//
// Build:  bazel build //experiments/latency:latency_harness
// Run:    GHOST_WTS="0.8,1.2,1,0,2,0" ./latency_harness --secs=6

#include <algorithm>
#include <atomic>
#include <numeric>
#include <thread>
#include <vector>

#include "lib/base.h"
#include "lib/ghost.h"

using ghost::GhostThread;

constexpr int kLatencyThreads   = 5;
constexpr int kBackgroundThreads = 20;
constexpr absl::Duration kPeriod = absl::Milliseconds(2);

struct OvershootStats {
  void add(absl::Duration d) {
    auto us = absl::ToDoubleMicroseconds(d);
    data.push_back(us);
  }
  void report() const {
    if (data.empty()) return;
    std::vector<double> v = data;
    std::sort(v.begin(), v.end());
    double mean = std::accumulate(v.begin(), v.end(), 0.0) / v.size();
    double p99  = v[static_cast<size_t>(0.99 * (v.size() - 1))];
    printf("avg_overshoot_ms=%0.3f\n", mean / 1000.0);
    printf("p99_overshoot_ms=%0.3f\n", p99  / 1000.0);
  }
  std::vector<double> data;
};

int main(int argc, char** argv) {
  int secs = 6;                          // default test length
  if (argc == 3 && std::string(argv[1]) == "--secs") secs = atoi(argv[2]);

  OvershootStats stats;
  std::atomic<bool> stop{false};

  // ----------------  latency‑sensitive group  -----------------
  std::vector<std::unique_ptr<GhostThread>> latency;
  latency.reserve(kLatencyThreads);
  for (int i = 0; i < kLatencyThreads; ++i) {
    latency.emplace_back(new GhostThread(GhostThread::KernelScheduler::kGhost,
      [&stats,&stop] {
        absl::Time next = MonotonicNow();
        while (!stop.load(std::memory_order_relaxed)) {
          next += kPeriod;
          ghost::SleepUntil(next);               // blocks in ghOSt
          absl::Duration lag = MonotonicNow() - next;
          if (lag >= absl::ZeroDuration())
            stats.add(lag);
        }
      }));
  }

  // ----------------  background burners  -----------------
  std::vector<std::unique_ptr<GhostThread>> burners;
  burners.reserve(kBackgroundThreads);
  for (int i = 0; i < kBackgroundThreads; ++i) {
    burners.emplace_back(new GhostThread(GhostThread::KernelScheduler::kGhost,
      [&stop] {
        while (!stop.load(std::memory_order_relaxed)) {
          asm volatile("" ::: "memory");         // prevent optimisation
        }
      }));
  }

  absl::SleepFor(absl::Seconds(secs));
  stop.store(true, std::memory_order_relaxed);

  for (auto& t : latency)  t->Join();
  for (auto& t : burners)  t->Join();

  stats.report();
  return 0;
}
