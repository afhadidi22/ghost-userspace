// schedulers/evolved_fifo/scheduler.h
#pragma once
#include <deque>
#include <limits>
#include <memory>
#include <functional>

#include "absl/synchronization/mutex.h"
#include "schedulers/fifo/per_cpu/fifo_scheduler.h"

namespace ghost {

// ---------- 1. policy --------------------------------------------------
struct DefaultBsPolicy {
  static double ScoreTask(const FifoTask* t,
                          const FifoTask* /*prev*/,
                          size_t pos) {
    return (t->prio_boost ? 1000.0 : 0.0) - static_cast<double>(pos);
  }
  static double ScoreCpu(const FifoTask* /*t*/, const Cpu& cpu) {
    return -static_cast<double>(cpu.id());
  }
};

// ---------- forward‑declare the template *as a template* ----------------
template <typename Policy> class BsFifoSchedulerBase;

// ---------- 2. run‑queue helper ----------------------------------------
class BsFifoRq : public FifoRq {
 public:
  template <typename ScoreFn>
  FifoTask* PopBest(ScoreFn&& score) {
    absl::MutexLock lock(&mu_);
    if (rq_.empty()) return nullptr;
    size_t best = 0;
    double best_s = std::numeric_limits<double>::lowest();
    for (size_t i = 0; i < rq_.size(); ++i) {
      double s = score(rq_[i], i);
      if (s > best_s) { best_s = s; best = i; }
    }
    FifoTask* t = rq_[best];
    rq_.erase(rq_.cbegin() + best);
    t->run_state = FifoTaskState::kRunnable;
    return t;
  }
  using FifoRq::Empty;
 private:
  template <typename P> friend class BsFifoSchedulerBase;  // << fix ①
};

// ---------- 3. scheduler template --------------------------------------
template <typename Policy = DefaultBsPolicy>
class BsFifoSchedulerBase : public FifoScheduler {
 public:
  using FifoScheduler::FifoScheduler;

 private:
  struct BsCpuState : public CpuState { BsFifoRq run_queue; };
  BsCpuState cpu_states_[MAX_CPUS];

  CpuState*  cpu_state(const Cpu& c)       { return &cpu_states_[c.id()]; }
  BsCpuState* bcs(const Cpu& c)            { return &cpu_states_[c.id()]; }

  Cpu AssignCpu(FifoTask* t) override {
    Cpu best = *cpus().begin();
    double best_s = Policy::ScoreCpu(t, best);
    for (const Cpu& c : cpus()) {
      double s = Policy::ScoreCpu(t, c);
      if (s > best_s) { best = c; best_s = s; }
    }
    return best;
  }

  FifoTask* PickNextTask(CpuState* cs_) override {
    auto* cs = static_cast<BsCpuState*>(cs_);
    if (cs->run_queue.Empty() && !cs->current) return nullptr;

    FifoTask* best = cs->current;
    double best_s = std::numeric_limits<double>::lowest();
    if (best) best_s = Policy::ScoreTask(best, cs->current, 0);

    auto scorer = [&](FifoTask* t, size_t i) {
      return Policy::ScoreTask(t, cs->current, i);
    };
    FifoTask* cand = cs->run_queue.PopBest(scorer);
    if (cand && Policy::ScoreTask(cand, cs->current, 0) > best_s)
      best = cand;
    else if (cand)
      cs->run_queue.Enqueue(cand);
    return best;
  }
};

// convenient alias
using BsFifoScheduler = BsFifoSchedulerBase<DefaultBsPolicy>;

}  // namespace ghost
