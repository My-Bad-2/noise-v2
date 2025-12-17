#pragma once

#include "task/scheduler.hpp"
#include "cpu/exception.hpp"
#include "hal/interface/interrupt.hpp"
#include "hal/interrupt.hpp"

namespace kernel::task {
class RescheduleHandler : public cpu::IInterruptHandler {
   public:
    const char* name() const {
        return "Reschedule";
    }

    cpu::IrqStatus handle(cpu::arch::TrapFrame*) {
        Scheduler& sched = Scheduler::get();
        sched.schedule();

        return cpu::IrqStatus::Handled;
    }
};

void register_reschedule_handler() {
    static RescheduleHandler handler;
    cpu::arch::InterruptDispatcher::register_handler(IPI_RESCHEDULE_VECTOR, &handler, true);
}
}  // namespace kernel::task