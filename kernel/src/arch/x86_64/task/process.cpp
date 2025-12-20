#include "task/process.hpp"
#include "cpu/exception.hpp"
#include "cpu/regs.h"
#include "task/scheduler.hpp"
#include <string.h>
#include <new>
#include "cpu/simd.hpp"

namespace kernel::task {
struct SwitchContext {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t rbp;
    uint64_t rbx;
    uint64_t return_address;
};

struct KernelStackLayout {
    SwitchContext switch_ctx;
    uintptr_t argument;
    uintptr_t entry_func;
};

struct UserStackLayout {
    SwitchContext switch_ctx;
    cpu::arch::TrapFrame trap_frame;
    uintptr_t thread_exit_addr;
};

std::byte* Thread::clean_fpu_state     = nullptr;
size_t Thread::fpu_state_size          = 0;
std::align_val_t Thread::fpu_alignment = std::align_val_t(16);

extern "C" void trap_return();
extern "C" void kernel_thread_entry();
extern "C" void thread_exit() {
    // Entry point when a thread's main function returns.
    LOG_DEBUG("Task: thread entry function returned; terminating");
    Scheduler::get().terminate();
}

void Thread::arch_init(uintptr_t entry, uintptr_t arg) {
    using namespace cpu::arch;

    if (!clean_fpu_state) {
        fpu_state_size = SIMD::get_save_size();
        fpu_alignment  = (fpu_state_size > 512) ? std::align_val_t(64) : std::align_val_t(16);

        clean_fpu_state = new (fpu_alignment) std::byte[fpu_state_size];

        memset(clean_fpu_state, 0, fpu_state_size);
        SIMD::save(clean_fpu_state);
    }

    // Allocate the kernel stack and set up the initial context.
    this->kernel_stack = new std::byte[KSTACK_SIZE];
    this->fpu_storage  = new (fpu_alignment) std::byte[fpu_state_size];

    if (!this->fpu_storage || !this->kernel_stack) {
        PANIC("Cannot Allocate Thread resources");
    }

    memcpy(this->fpu_storage, clean_fpu_state, fpu_state_size);

    uintptr_t stack_top = reinterpret_cast<uintptr_t>(this->kernel_stack) + KSTACK_SIZE;

    if (this->is_user_thread) {
        auto* layout = reinterpret_cast<UserStackLayout*>(stack_top - sizeof(UserStackLayout));
        memset(layout, 0, sizeof(UserStackLayout));

        layout->trap_frame.cs     = 0x23;  // User Code | RPL 3
        layout->trap_frame.ss     = 0x1B;  // User Data | RPL 3
        layout->trap_frame.rflags = FLAGS_IF | FLAGS_RESERVED_ONES;
        layout->trap_frame.rip    = entry;  // User Entry Point
        layout->trap_frame.rdi    = arg;    // User Stack Pointer

        // Safety return address (if IRET fails somehow)
        layout->thread_exit_addr = reinterpret_cast<uintptr_t>(thread_exit);

        // TrapFrame internal RSP points to safety exit
        layout->trap_frame.rsp            = reinterpret_cast<uintptr_t>(&layout->thread_exit_addr);
        layout->switch_ctx.return_address = reinterpret_cast<uintptr_t>(trap_return);

        this->kernel_stack_ptr = reinterpret_cast<uintptr_t>(&layout->switch_ctx);
    } else {
        auto* layout = reinterpret_cast<KernelStackLayout*>(stack_top - sizeof(KernelStackLayout));
        memset(layout, 0, sizeof(KernelStackLayout));

        layout->entry_func = entry;
        layout->argument   = arg;

        layout->switch_ctx.return_address = reinterpret_cast<uintptr_t>(kernel_thread_entry);

        this->kernel_stack_ptr = reinterpret_cast<uintptr_t>(&layout->switch_ctx);
    }
}
}  // namespace kernel::task