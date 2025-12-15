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
    LOG_DEBUG("Thread has returned. Terminating...");
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

    LOG_DEBUG("Thread %lu initialized. KStack Top: %p, SP: %p", thread->tid, stack_top,
              switch_addr);
}
}  // namespace

void Thread::arch_init(uintptr_t entry, uintptr_t arg) {
    this->kernel_stack = new std::byte[KSTACK_SIZE];

    LOG_DEBUG("kstack = %p", this->kernel_stack);
    setup_kstack(this, entry, arg);
}
}  // namespace kernel::task