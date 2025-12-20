#pragma once

#include <cstdint>
#include <atomic>
#include "cpu/cpu.hpp"
#include "libs/spinlock.hpp"
#include "libs/vector.hpp"
#include "task/process.hpp"
#include "task/scheduler.hpp"
#include "memory/pcid_manager.hpp"
#include "cpu/cpu.hpp"
#include "boot/limine.h"

namespace kernel::cpu {
struct alignas(CACHE_LINE_SIZE) PerCpuData {
    PerCpuData* self;
    uint32_t apic_id;
    uint32_t acpi_id;
    uint32_t core_idx;

    bool is_bsp;
    bool reschedule_needed;
    std::atomic<bool> is_online;

    uintptr_t kstack_top;

    task::Thread* curr_thread;
    task::Thread* idle_thread;
    task::Thread* reaper_thread;
    task::Scheduler sched;
    memory::PcidManager* pcid_manager;

    arch::CpuData arch;

    PerCpuData(uint32_t idx, limine_mp_info* info);
    void init(void* bsp_stack_top = nullptr);
    void commit();

   private:
    void arch_init();
};

class CpuCoreManager {
   public:
    static CpuCoreManager& get();

    void init(void* bsp_stack_top);

    PerCpuData* get_current_core();
    PerCpuData* get_core_by_index(uint32_t index);

    size_t get_total_cores() const;
    void send_ipi(uint32_t dest, uint8_t vector);

    static void tlb_shootdown(uintptr_t virt_addr);
    static void tlb_shootdown(uintptr_t start, size_t count);
    static void call_on_core(uint32_t core_idx, void (*func)(void*), void* arg);
    static void stop_other_cores();

    void allow_io_port(uint16_t port, bool enable) {
        PerCpuData* data = this->get_current_core();
        data->arch.gdt->set_io_perm(port, enable);
    }

    bool initialized() const {
        return !this->cores.empty();
    }

   private:
    static void init_syscalls();

    [[noreturn]] static void ap_entry_func(limine_mp_info* info);
    [[noreturn]] static void ap_main(PerCpuData* data);

    static bool send_ipi_to_others(uint8_t vector);
    static void wait_for_acks();

    Vector<PerCpuData*> cores;
    SpinLock lock;
};
}  // namespace kernel::cpu