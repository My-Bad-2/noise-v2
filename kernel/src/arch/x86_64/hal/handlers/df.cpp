#include "hal/handlers/df.hpp"
#include "arch.hpp"
#include "cpu/exception.hpp"
#include "hal/interface/interrupt.hpp"
#include "libs/log.hpp"
#include "cpu/registers.hpp"

namespace kernel::arch::handlers {
cpu::IrqStatus DFHandler::handle(cpu::arch::TrapFrame* frame) {
    // Double faults are almost always unrecoverable, usually indicating
    // stack corruption or a nested exception failure. We halt immediately.
    disable_interrupts();

    Cr2 cr2 = Cr2::read();

    LOG_ERROR("");
    LOG_ERROR("CRITICAL KERNEL PANIC: DOUBLE FAULT (Vector 8)");
    LOG_ERROR("----------------------------------------------");
    LOG_ERROR("The kernel stack has likely overflowed or is corrupted.");
    LOG_ERROR("Reg Dump:");
    LOG_ERROR("RIP: 0x%lx  RSP: 0x%lx", frame->rip, frame->rsp);
    LOG_ERROR("RBP: 0x%lx  CR2: 0x%lx", frame->rbp, cr2.linear_address);

    PANIC("System Halted.");
    return cpu::IrqStatus::Handled;
}
}  // namespace kernel::arch::handlers