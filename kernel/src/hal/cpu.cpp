#include "hal/cpu.hpp"
#include <string.h>
#include "libs/log.hpp"
#include "arch.hpp"
#include "libs/vector.hpp"
#include "memory/pcid_manager.hpp"
#include "task/process.hpp"
#include "task/scheduler.hpp"

namespace kernel::cpu {
namespace {
void idle_loop(void*) {
    kernel::arch::halt(true);
}

Vector<PerCPUData*> cpus;
}  // namespace

bool CPUCoreManager::smp_initialized = false;

PerCPUData* CPUCoreManager::init_core(uint32_t cpu_id, uintptr_t stack_top) {
    PerCPUData* cpu = new PerCPUData;
    memset(reinterpret_cast<void*>(cpu), 0, sizeof(PerCPUData));

    cpu->self        = cpu;
    cpu->cpu_id      = cpu_id;
    cpu->status_flag = 1;

    arch::CPUData::init(&cpu->arch, stack_top);
    arch::CPUData::commit_state(cpu);

    cpu->idle_thread = new task::Thread(task::kernel_proc, idle_loop, nullptr);
    cpu->curr_thread = cpu->idle_thread;

    cpu->pcid_manager              = new memory::PcidManager;
    cpu->idle_thread->thread_state = task::Running;

    smp_initialized = true;
    cpus.push_back(cpu);
    cpu->sched.init(cpu_id);
    cpu->pcid_manager->init();

    LOG_INFO("CPU: initialized core id=%u per_cpu=%p stack_top=0x%lx", cpu_id, cpu, stack_top);
    return cpu;
}

PerCPUData* CPUCoreManager::get_cpu(uint32_t id) {
    if (id >= cpus.size()) {
        PANIC("Invalid CPU Id requested %u", id);
    }

    return cpus[id];
}

uint32_t CPUCoreManager::get_core_count() {
    return static_cast<uint32_t>(cpus.size());
}
}  // namespace kernel::cpu