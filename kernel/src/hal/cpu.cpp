#include "hal/cpu.hpp"
#include <string.h>
#include "libs/log.hpp"
#include "arch.hpp"
#include "task/process.hpp"

namespace kernel::cpu {
namespace {
void idle_loop(void*) {
    LOG_DEBUG("hello");
    kernel::arch::halt(true);
}
}  // namespace

PerCPUData* CPUCoreManager::init_core(uint32_t cpu_id, uintptr_t stack_top) {
    PerCPUData* cpu = new PerCPUData;
    memset(cpu, 0, sizeof(PerCPUData));

    cpu->self        = cpu;
    cpu->cpu_id      = cpu_id;
    cpu->status_flag = 1;

    arch::CPUData::init(&cpu->arch, stack_top);
    arch::CPUData::commit_state(cpu);

    cpu->idle_thread = new task::Thread(nullptr, idle_loop, nullptr);
    cpu->curr_thread = cpu->idle_thread;

    LOG_INFO("CPU: initialized core id=%u per_cpu=%p stack_top=0x%lx", cpu_id, cpu, stack_top);
    return cpu;
}
}  // namespace kernel::cpu