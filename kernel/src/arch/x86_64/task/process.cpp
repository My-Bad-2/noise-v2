#include "task/process.hpp"
#include "cpu/exception.hpp"
#include "libs/log.hpp"
#include <string.h>

namespace kernel::task {
using namespace cpu::arch;

namespace {
void setup_kstack(Thread* thread, uintptr_t entry, uintptr_t arg) {
    uintptr_t stack_addr = reinterpret_cast<uintptr_t>(thread->kernel_stack) + KSTACK_SIZE;
    uintptr_t frame_addr = stack_addr - sizeof(TrapFrame);

    TrapFrame* frame = reinterpret_cast<TrapFrame*>(frame_addr);
    memset(frame, 0, sizeof(TrapFrame));

    frame->ss     = 0x10;
    frame->rsp    = reinterpret_cast<uintptr_t>(stack_addr - (sizeof(uint64_t) * 2));
    frame->cs     = 0x08;
    frame->rflags = 0x202;

    frame->rax = arg;
    frame->rip = entry;

    thread->kernel_stack_ptr = reinterpret_cast<uintptr_t>(frame);

    frame->print();

    LOG_DEBUG("frame = %p", frame);
}
}  // namespace

void Thread::arch_init(uintptr_t entry, uintptr_t arg) {
    this->kernel_stack = new std::byte[KSTACK_SIZE];

    LOG_DEBUG("kstack = %p", this->kernel_stack);
    setup_kstack(this, entry, arg);
}
}  // namespace kernel::task