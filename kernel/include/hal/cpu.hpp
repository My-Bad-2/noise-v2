#pragma once

#include <cstdint>
#include "cpu/cpu.hpp"

namespace kernel::task {
struct Thread;
}

namespace kernel::cpu {
struct alignas(CACHE_LINE_SIZE) PerCPUData {
    PerCPUData* self;
    uint32_t cpu_id;
    uint32_t status_flag;
    task::Thread* curr_thread;
    task::Thread* idle_thread;
    arch::CPUData arch;
};

class CPUCoreManager {
   public:
    static PerCPUData* init_core(uint32_t cpu_id, uintptr_t stack_top);
    static void allow_io_port(PerCPUData* cpu, uint16_t port, bool enable);
    static uint32_t get_curr_cpu_id();
    static PerCPUData* get_curr_cpu();

    static bool initialized() {
        return smp_initialized;
    }

   private:
    static bool smp_initialized;
};
}  // namespace kernel::cpu