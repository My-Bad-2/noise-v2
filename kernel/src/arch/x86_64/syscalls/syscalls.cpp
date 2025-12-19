#include <stdint.h>
#include "arch.hpp"
#include "cpu/exception.hpp"
#include "libs/log.hpp"

namespace kernel {
using namespace cpu::arch;

extern "C" void syscall_handler(uint64_t syscall_num, TrapFrame* frame) {
    arch::enable_interrupts();
    switch (syscall_num) {
        case 0: {
            const char* user_msg = reinterpret_cast<const char*>(frame->rdi);
            LOG_INFO(user_msg);
            frame->rax = 0;
            break;
        }
        default: {
            LOG_ERROR("Unknown Syscall Number %lu", syscall_num);
            frame->rax = static_cast<uint64_t>(-1);
            break;
        }
    }
}
}  // namespace kernel