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
    std::atomic<bool> is_online;

    uintptr_t kstack_top;

    task::Thread* curr_thread;
    task::Thread* idle_thread;
    task::Scheduler sched;
    memory::PcidManager* pcid_manager;

    arch::CpuData arch;

    PerCpuData(uint32_t idx, limine_mp_info* info);
    void init();
    void commit();

   private:
    void arch_init();
};

class CpuCoreManager {
   public:
    static CpuCoreManager& get();

    void init();

    PerCpuData* get_current_core();
    PerCpuData* get_core_by_index(uint32_t index);

    size_t get_total_cores() const;
    void send_ipi(uint32_t dest, uint8_t vector);

    void allow_io_port(uint16_t port, bool enable) {
        PerCpuData* data = this->get_current_core();
        data->arch.gdt->set_io_perm(port, enable);
    }

    bool initialized() const {
        return !this->cores.empty();
    }

   private:
    [[noreturn]] static void ap_entry_func(limine_mp_info* info);
    [[noreturn]] static void ap_main(PerCpuData* data);

    Vector<PerCpuData*> cores;
    SpinLock lock;
};
}  // namespace kernel::cpu