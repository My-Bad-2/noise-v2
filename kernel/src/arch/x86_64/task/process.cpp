#include "task/process.hpp"
#include "cpu/exception.hpp"
#include "libs/log.hpp"
#include "task/scheduler.hpp"
#include <string.h>

extern "C" void switch_trampoline();

namespace kernel::task {
using namespace cpu::arch;

namespace {
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

    LOG_DEBUG("Task: thread %lu initialized (kstack_top=%p, sp=%p)", thread->tid, stack_top,
              switch_addr);
}
}  // namespace

void Thread::arch_init(uintptr_t entry, uintptr_t arg) {
    // Allocate the kernel stack and set up the initial context.
    this->kernel_stack = new std::byte[KSTACK_SIZE];

    LOG_DEBUG("Task: allocated kernel stack at %p", this->kernel_stack);
    setup_kstack(this, entry, arg);
}
}  // namespace kernel::task