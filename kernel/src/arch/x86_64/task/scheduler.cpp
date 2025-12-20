#pragma once

#include "task/scheduler.hpp"
#include "cpu/exception.hpp"
#include "hal/interface/interrupt.hpp"
#include "hal/interrupt.hpp"
#include "cpu/simd.hpp"
#include "libs/log.hpp"

namespace kernel::task {
class RescheduleHandler : public cpu::IInterruptHandler {
   public:
    const char* name() const {
        return "Reschedule";
    }

    cpu::IrqStatus handle(cpu::arch::TrapFrame*) {
        // IRQ handler will handle rescheduling
        return cpu::IrqStatus::Handled;
    }
};

void Scheduler::save_fpu(std::byte* buffer) {
    if (!buffer) {
        PANIC("Scheduler somehow passed buffer as nullptr");
    }

    // Since we are eagerly switching the FPU state, clear
    // the Task-Switched flag in CR0.
    asm volatile("clts");
    cpu::arch::SIMD::save(buffer);
}

void Scheduler::restore_fpu(std::byte* buffer) {
    if (!buffer) {
        PANIC("Scheduler somehow passed buffer as nullptr");
    }

    // Since we are eagerly switching the FPU state, clear
    // the Task-Switched flag in CR0.
    asm volatile("clts");
    cpu::arch::SIMD::restore(buffer);
}

void register_reschedule_handler() {
    static RescheduleHandler handler;
    cpu::arch::InterruptDispatcher::register_handler(IPI_RESCHEDULE_VECTOR, &handler, true);
}
}  // namespace kernel::task