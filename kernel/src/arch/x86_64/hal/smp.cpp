#include "arch.hpp"
#include "boot/limine.h"
#include "cpu/gdt.hpp"
#include "cpu/idt.hpp"
#include "cpu/registers.hpp"
#include "hal/lapic.hpp"
#include "hal/smp_manager.hpp"
#include "libs/log.hpp"
#include "cpu/regs.h"
#include "cpu/simd.hpp"
#include "boot/boot.h"
#include "memory/pagemap.hpp"

namespace kernel::cpu {
namespace {
std::byte* nmi_stack = nullptr;
std::byte* df_stack  = nullptr;
}  // namespace

void PerCpuData::arch_init() {
    if (nmi_stack == nullptr) {
        nmi_stack = new std::byte[0x1000];
    }

    if (df_stack == nullptr) {
        df_stack = new std::byte[0x1000];
    }

    if (!nmi_stack || !df_stack) {
        PANIC("IST Stack Allocation failed!");
    }

    this->arch.gdt->set_ist(0, reinterpret_cast<uintptr_t>(nmi_stack) + 0x1000);
    this->arch.gdt->set_ist(1, reinterpret_cast<uintptr_t>(df_stack) + 0x1000);

    this->arch.gdt->setup_gdt();
    this->arch.gdt->setup_tss(this->kstack_top);
}

void PerCpuData::commit() {
    this->arch.gdt->load_tables();
    arch::IDTManager::load_table();

    hal::Lapic::init();
    hal::Lapic::calibrate();
    arch::SIMD::init();

    kernel::arch::Msr msr;
    msr.index = MSR_GS_BASE;
    msr.value = reinterpret_cast<uintptr_t>(this);
    msr.write();

    hal::Timer::init();

    kernel::arch::enable_interrupts();
}

void CpuCoreManager::ap_main(PerCpuData* data) {
    data->arch.gdt->load_tables();
    arch::IDTManager::load_table();

    hal::Lapic::init();
    hal::Lapic::calibrate();
    arch::SIMD::init();

    kernel::arch::Msr msr;
    msr.index = MSR_GS_BASE;
    msr.value = reinterpret_cast<uintptr_t>(data);
    msr.write();

    data->is_online.store(true);
    hal::Timer::init();

    kernel::arch::enable_interrupts();

    LOG_INFO("AP Core %u (APIC %u) is online!", data->core_idx, data->apic_id);

    kernel::arch::halt(true);
}

void CpuCoreManager::ap_entry_func(limine_mp_info* info) {
    memory::PageMap::get_kernel_map()->load();
    PerCpuData* data = reinterpret_cast<PerCpuData*>(info->extra_argument);

    asm volatile("mov %0, %%rsp" ::"r"(data->kstack_top) : "memory");
    asm volatile("mov %0, %%rdi" ::"r"(data) : "memory");
    asm volatile("call *%0" ::"r"(ap_main) : "memory");
    kernel::arch::halt(true);
}

PerCpuData* CpuCoreManager::get_current_core() {
    PerCpuData* val;
    asm volatile("mov %%gs:0, %0" : "=r"(val));
    return val;
}

PerCpuData::PerCpuData(uint32_t idx, limine_mp_info* info)
    : self(this),
      acpi_id(info->processor_id),
      core_idx(idx),
      apic_id(info->lapic_id),
      pcid_manager(new memory::PcidManager),
      arch() {
    this->is_bsp = (info->lapic_id == mp_request.response->bsp_lapic_id);
    this->is_online.store(this->is_bsp);
}

void CpuCoreManager::send_ipi(uint32_t dest, uint8_t vector) {
    hal::Lapic::send_ipi(dest, vector);
}
}  // namespace kernel::cpu