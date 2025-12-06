#include "hal/cpu.hpp"
#include <string.h>
#include "memory/vmm.hpp"
#include "libs/log.hpp"

namespace kernel::cpu {
// NOLINTNEXTLINE
PerCPUData* CPUCoreManager::init_core(uint32_t cpu_id, uintptr_t stack_top) {
    // For now we use a simple `new` to obtain per‑CPU storage. In a more
    // advanced setup this could come from a dedicated per‑CPU allocator.
    PerCPUData* cpu = new PerCPUData;
    memset(cpu, 0, sizeof(PerCPUData));

    cpu->self        = cpu;
    cpu->cpu_id      = cpu_id;
    cpu->status_flag = 1;  // Mark as "online/initialized" (policy‑specific).

    arch::CPUData::init(&cpu->arch, stack_top);
    arch::CPUData::commit_state(&cpu->arch);

    LOG_INFO("CPU: initialized core id=%u per_cpu=%p stack_top=0x%lx", cpu_id, cpu, stack_top);
    return cpu;
}
}  // namespace kernel::cpu