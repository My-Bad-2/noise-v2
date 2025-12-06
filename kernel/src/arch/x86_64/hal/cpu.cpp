#include "hal/cpu.hpp"
#include "cpu/cpu.hpp"
#include "cpu/gdt.hpp"

namespace kernel::cpu {
void CPUCoreManager::allow_io_port(PerCPUData* cpu, uint16_t port, bool enable) {
    // Delegate to arch layer to flip the corresponding bit in the TSS I/O map.
    arch::GDTManager::set_io_perm(&cpu->arch, port, enable);
}

uint32_t CPUCoreManager::get_curr_cpu_id() {
    uint32_t id;
    // Load the cpu_id field from the current GS‑based PerCPUData.
    asm volatile("mov %%gs:8, %0" : "=r"(id));
    return id;
}

void arch::CPUData::init(CPUData* arch, uint64_t stack_top) {
    GDTManager::setup_gdt(arch);
    GDTManager::setup_tss(arch, stack_top);
}

void arch::CPUData::commit_state(CPUData* cpu) {
    // Install the per‑CPU GDT/TSS and set GS base to this CPU's data.
    GDTManager::load_tables(cpu);
}
}  // namespace kernel::cpu