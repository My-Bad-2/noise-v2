#pragma once

#include <cstdint>
#include <cstddef>

namespace kernel::sched::arch {
struct Thread {
    uintptr_t tss_stack_ptr;
    uint8_t* fpu_save_region;
};
}  // namespace kernel::sched::arch