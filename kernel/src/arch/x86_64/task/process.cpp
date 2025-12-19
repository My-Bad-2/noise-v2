#include "task/process.hpp"
#include "cpu/exception.hpp"
#include "libs/log.hpp"
#include "task/scheduler.hpp"
#include <string.h>
#include <new>
#include "cpu/simd.hpp"

extern "C" void switch_trampoline();

namespace kernel::task {
using namespace cpu::arch;

namespace {
size_t fpu_size;
std::align_val_t fpu_alignment;
std::byte* clean_fpu_storage;

void thread_exit() {
    // Entry point when a thread's main function returns.
    LOG_DEBUG("Task: thread entry function returned; terminating");
    Scheduler::get().terminate();
}
}  // namespace

struct SwitchContext {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t rbp;
    uint64_t rbx;
    uint64_t return_address;
};

namespace {
void setup_kstack(Thread* thread, uintptr_t entry, uintptr_t arg) {
    // Build an initial kernel stack frame so that when we context-switch into this
    // thread for the first time, it will enter at `entry(arg)` and return via
    // `thread_exit` when the function completes.
    uintptr_t stack_top = reinterpret_cast<uintptr_t>(thread->kernel_stack) + KSTACK_SIZE;

    uintptr_t return_addr_slot                      = stack_top - 8;
    *reinterpret_cast<uintptr_t*>(return_addr_slot) = reinterpret_cast<uintptr_t>(thread_exit);

    uintptr_t frame_addr  = return_addr_slot - sizeof(TrapFrame);
    uintptr_t switch_addr = frame_addr - sizeof(SwitchContext);

    thread->kernel_stack_ptr = reinterpret_cast<uintptr_t>(switch_addr);

    TrapFrame* frame       = reinterpret_cast<TrapFrame*>(frame_addr);
    SwitchContext* context = reinterpret_cast<SwitchContext*>(switch_addr);

    memset(frame, 0, sizeof(TrapFrame));
    memset(context, 0, sizeof(SwitchContext));

    frame->ss     = 0x10;
    frame->rsp    = return_addr_slot;
    frame->cs     = 0x08;
    frame->rflags = 0x202;
    frame->rdi    = arg;
    frame->rip    = entry;

    context->return_address = reinterpret_cast<uintptr_t>(switch_trampoline);
}
}  // namespace

void Thread::arch_init(uintptr_t entry, uintptr_t arg) {
    // Allocate the kernel stack and set up the initial context.
    this->kernel_stack = new std::byte[KSTACK_SIZE];

    if (fpu_size == 0) {
        fpu_size = SIMD::get_save_size();

        if (fpu_size > 512) {
            // FPU State higher than SSE requires 64-byte alignment
            fpu_alignment = std::align_val_t(64);
        } else {
            fpu_alignment = std::align_val_t(16);
        }

        clean_fpu_storage = new (fpu_alignment) std::byte[fpu_size];
        SIMD::save(clean_fpu_storage);
    }

    if (!this->fpu_storage) {
        this->fpu_storage = new (fpu_alignment) std::byte[fpu_size];
    }

    if (!this->fpu_storage) {
        PANIC("Cannot Allocate FPU storage for Thread %lu", this->tid);
    }

    memset(this->fpu_storage, 0, fpu_size);
    setup_kstack(this, entry, arg);
}
}  // namespace kernel::task