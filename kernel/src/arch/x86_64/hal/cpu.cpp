#include "hal/cpu.hpp"
#include "arch.hpp"
#include "cpu/cpu.hpp"
#include "cpu/gdt.hpp"
#include "cpu/idt.hpp"
#include "cpu/registers.hpp"
#include "cpu/regs.h"
#include "hal/lapic.hpp"
#include "cpu/simd.hpp"

#define STACK_SIZE memory::PAGE_SIZE_4K

namespace kernel::cpu {
namespace {
std::byte* nmi_stack          = nullptr;
std::byte* double_fault_stack = nullptr;
}  // namespace

void CPUCoreManager::allow_io_port(PerCPUData* cpu, uint16_t port, bool enable) {
    // Delegate to arch layer to flip the corresponding bit in the TSS I/O map.
    arch::GDTManager::set_io_perm(&cpu->arch, port, enable);
}

uint32_t CPUCoreManager::get_curr_cpu_id() {
    uint32_t id;
    // Load the cpu_id field from the current GS‑based PerCPUData.
    asm volatile("mov %%gs:%c1, %0" : "=r"(id) : "i"(__builtin_offsetof(PerCPUData, cpu_id)));
    return id;
}

PerCPUData* CPUCoreManager::get_curr_cpu() {
    PerCPUData* self;
    asm volatile("movq %%gs:0, %0" : "=r"(self));
    return self;
}

void arch::CPUData::init(CPUData* arch, uint64_t stack_top) {
    if (nmi_stack == nullptr) {
        // Dedicated IST stack for NMIs to avoid clobbering arbitrary stacks
        // during asynchronous events.
        nmi_stack = new std::byte[STACK_SIZE];
    }

    if (double_fault_stack == nullptr) {
        // Dedicated IST stack for double faults; this is critical because
        // double faults often arise from stack corruption/overflow.
        double_fault_stack = new std::byte[STACK_SIZE];
    }

    arch->tss_block.header.ist[0] = reinterpret_cast<uintptr_t>(nmi_stack) + STACK_SIZE;
    arch->tss_block.header.ist[1] = reinterpret_cast<uintptr_t>(double_fault_stack) + STACK_SIZE;

    GDTManager::setup_gdt(arch);
    GDTManager::setup_tss(arch, stack_top);

    hal::Lapic::init();
    hal::Lapic::calibrate();
    SIMD::init();
}

void arch::CPUData::commit_state(PerCPUData* cpu) {
    // Install the per‑CPU GDT/TSS and set GS base to this CPU's data.
    GDTManager::load_tables(&cpu->arch);
    IDTManager::load_table();

    uint64_t gs_base = reinterpret_cast<uint64_t>(cpu);

    kernel::arch::Msr msr;
    msr.index = MSR_GS_BASE;
    msr.value = gs_base;
    msr.write();

    ::kernel::arch::enable_interrupts();
}
}  // namespace kernel::cpu