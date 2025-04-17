// schedulers/bs_fifo/bs_fifo_agent.cc
#include <cstdint>
#include <string>
#include <vector>

#include "absl/debugging/symbolize.h"
#include "absl/flags/parse.h"
#include "lib/agent.h"
#include "lib/enclave.h"
#include "schedulers/evolved_fifo/scheduler.h"

ABSL_FLAG(std::string, ghost_cpus, "1-5", "cpulist");
ABSL_FLAG(std::string, enclave, "", "Connect to preexisting enclave directory");

namespace ghost {

static void ParseAgentConfig(AgentConfig* config) {
  CpuList ghost_cpus =
      MachineTopology()->ParseCpuStr(absl::GetFlag(FLAGS_ghost_cpus));
  CHECK(!ghost_cpus.Empty());

  config->topology_ = MachineTopology();
  config->cpus_ = ghost_cpus;

  std::string path = absl::GetFlag(FLAGS_enclave);
  if (!path.empty()) {
    int fd = open(path.c_str(), O_PATH);
    CHECK_GE(fd, 0);
    config->enclave_fd_ = fd;
  }
}

template <class EnclaveT>
class FullBsFifoAgent : public FullAgent<EnclaveT> {
 public:
  explicit FullBsFifoAgent(AgentConfig cfg) : FullAgent<EnclaveT>(cfg) {
    scheduler_ = std::make_unique<BsFifoScheduler>(
        &this->enclave_, *this->enclave_.cpus(),
        std::make_shared<ThreadSafeMallocTaskAllocator<FifoTask>>());
    this->StartAgentTasks();
    this->enclave_.Ready();
  }
  ~FullBsFifoAgent() override { this->TerminateAgentTasks(); }

  std::unique_ptr<Agent> MakeAgent(const Cpu& cpu) override {
    return std::make_unique<FifoAgent>(&this->enclave_, cpu, scheduler_.get());
  }
  Scheduler* SchedulerForAgent() override { return scheduler_.get(); }

 private:
  std::unique_ptr<BsFifoScheduler> scheduler_;
};

}  // namespace ghost

int main(int argc, char* argv[]) {
  absl::InitializeSymbolizer(argv[0]);
  absl::ParseCommandLine(argc, argv);

  ghost::AgentConfig cfg;
  ghost::ParseAgentConfig(&cfg);

  auto proc =
      new ghost::AgentProcess<
          ghost::FullBsFifoAgent<ghost::LocalEnclave>,
          ghost::AgentConfig>(cfg);

  ghost::GhostHelper()->InitCore();
  printf("bs_fifo_agent: ready\n");
  fflush(stdout);

  ghost::Notification exit;
  ghost::GhostSignals::AddHandler(SIGINT,
                                  [&exit](int) {
                                    exit.Notify();
                                    return true;
                                  });
  exit.WaitForNotification();
  delete proc;
  printf("bs_fifo_agent: done\n");
}
