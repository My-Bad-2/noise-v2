#include "cpu/exception.hpp"
#include "libs/log.hpp"

namespace kernel::cpu::arch {
void TrapFrame::print() {
    LOG_INFO("CS : 0x%lx RIP: 0x%lx EFL: 0x%lx", this->cs, this->rip, this->rflags);
    LOG_INFO("RAX: 0x%lx RBX: 0x%lx RCX: 0x%lx", this->rax, this->rbx, this->rcx);
    LOG_INFO("RDX: 0x%lx RSI: 0x%lx RBP: 0x%lx", this->rdx, this->rsi, this->rbp);
    LOG_INFO("RSP: 0x%lx R8: 0x%lx R9: 0x%lx", this->rsp, this->r8, this->r9);
    LOG_INFO("R10: 0x%lx R11: 0x%lx R12: 0x%lx", this->r10, this->r11, this->r12);
    LOG_INFO("R13: 0x%lx R14: 0x%lx R15: 0x%lx", this->r13, this->r14, this->r15);
    LOG_INFO("EC: 0x%lx USP: 0x%lx USS: 0x%lx", this->error_code, this->rsp, this->ss);
}
}  // namespace kernel::cpu::arch