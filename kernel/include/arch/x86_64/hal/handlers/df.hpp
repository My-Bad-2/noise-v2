#pragma once

#include "cpu/exception.hpp"
#include "hal/interface/interrupt.hpp"

namespace kernel::arch::handlers {
class DFHandler : public cpu::IInterruptHandler {
   public:
    const char* name() const override {
        return "Double Fault";
    }

    cpu::IrqStatus handle(cpu::arch::TrapFrame* frame) override;
};
}  // namespace kernel::arch::handlers